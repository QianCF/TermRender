#include "renderer.h"
#include "pbr.h"

#include <cstring>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <thread>
#ifndef _WIN32
#include <unistd.h>
#else
#include <windows.h>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace tr {

namespace {

int online_cpu_count() {
    int from_os = 0;
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    if (si.dwNumberOfProcessors > 0) from_os = (int)si.dwNumberOfProcessors;
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    if (n > (DWORD)from_os) from_os = (int)n;
#else
#if defined(__APPLE__)
    int n = 0;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.logicalcpu", &n, &len, nullptr, 0) == 0 && n > 0) from_os = n;
#endif
    if (from_os <= 0) {
#ifdef _SC_NPROCESSORS_ONLN
        long sc = sysconf(_SC_NPROCESSORS_ONLN);
        if (sc > 0) from_os = (int)sc;
#endif
    }
#endif
    unsigned hw = std::thread::hardware_concurrency();
    if (from_os > 0) return from_os;
    return hw > 0 ? (int)hw : 1;
}

using namespace pbr;

// Debug: TR_TRACE_PIX="x,y" logs every bounce of that pixel; TR_TRACE_TH sets
// the radiance threshold for logging (default 0).
static long trace_target() {
    static const long t = []() -> long {
        const char *e = std::getenv("TR_TRACE_PIX");
        if (e && e[0]) {
            int x = 0, y = 0;
            if (std::sscanf(e, "%d,%d", &x, &y) == 2) return (long)y * 100000 + x;
        }
        return -1;
    }();
    return t;
}
static float trace_th() {
    static const float v = []() -> float {
        const char *e = std::getenv("TR_TRACE_TH");
        return e ? (float)std::atof(e) : 0.0f;
    }();
    return v;
}
static bool &force_trace() {
    static bool b = false;
    return b;
}

// Uncorrelated per-frame seed: a splitmix64 stream advanced once per batch,
// seeded from the OS RNG + clock. Ensures every accumulation batch (including
// restarts after a camera/settings change) uses a fresh, uncorrelated stream.
static uint32_t fresh_frame_seed() {
    static std::atomic<uint64_t> state = []() {
        std::random_device rd;
        uint64_t s = ((uint64_t)rd() << 32) ^ (uint64_t)rd();
        s ^= (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        return s;
    }();
    uint64_t z = state.fetch_add(0x9E3779B97F4A7C15ull) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= (z >> 31);
    return (uint32_t)z;
}

// dspbr-pt shader/constants.glsl
const float EPS_COS = 1.0e-3f;
const float EPS_PDF = 1.0e-3f;
const float MINIMUM_ROUGHNESS = 1.0e-4f;
const float TFAR_MAX = 1.0e5f;
const int RR_START_DEPTH = 2;
const float RR_TERMINATION_PROB = 0.1f;

// MaterialClosure.event_type flags (material.glsl)
enum : int { E_DELTA = 0x00002, E_REFLECTION = 0x00004, E_TRANSMISSION = 0x00008 };

// ------------------------------------------------------------------ types

struct RGBA {
    Vec3 c{0, 0, 0};
    float a = 1.0f;
};

struct Tri {
    Vec3 p0, e1, e2;
    Vec3 n0, n1, n2;
    Vec2 a0, a1, a2; // TEXCOORD_0
    Vec2 b0, b1, b2; // TEXCOORD_1
    Vec4 c0, c1, c2; // COLOR_0
    bool has_color = false;
    Vec3 t0, t1, t2;
    float s0, s1, s2;
    float uv_density = 0.0f;
    int mat = 0;
    bool double_sided = false;
    bool det_positive = true; // orientation of the node transform
};

struct BvhNode {
    Vec3 bmin, bmax;
    int start = 0, count = 0;
    int left = -1, right = -1;
};

// A light-emitting triangle, used for next-event estimation against emissive
// geometry (so emissive materials actually illuminate the scene).
struct EmissiveTri {
    int tri = -1;
    float area = 0.0f;
    float power = 0.0f; // area * luminance(emissive)
    float cdf = 0.0f;   // normalized cumulative power
};

static const std::vector<EmissiveTri> kEmptyEmis;

inline int emis_lower_bound(const std::vector<EmissiveTri> &e, float r) {
    int lo = 0, hi = (int)e.size();
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (e[mid].cdf < r)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < (int)e.size() ? lo : (int)e.size() - 1;
}

inline Vec3 tri_min(const Tri &t) { return minv(t.p0, minv(t.p0 + t.e1, t.p0 + t.e2)); }
inline Vec3 tri_max(const Tri &t) { return maxv(t.p0, maxv(t.p0 + t.e1, t.p0 + t.e2)); }
inline Vec3 tri_centroid(const Tri &t) {
    return (t.p0 + (t.p0 + t.e1) + (t.p0 + t.e2)) * (1.0f / 3.0f);
}

// -------------------------------------------------------------- textures

inline int wrap_i(int i, int n, WrapMode m) {
    if (n <= 0) return 0;
    switch (m) {
    case WrapMode::Repeat:
        i %= n;
        if (i < 0) i += n;
        return i;
    case WrapMode::Mirror: {
        int period = 2 * n;
        i %= period;
        if (i < 0) i += period;
        if (i >= n) i = period - 1 - i;
        return i;
    }
    default:
        return i < 0 ? 0 : (i >= n ? n - 1 : i);
    }
}

inline RGBA level_texel(const std::vector<uint8_t> &d, int w, int x, int y) {
    RGBA r;
    size_t o = ((size_t)y * w + x) * 4;
    r.c = Vec3(d[o] / 255.0f, d[o + 1] / 255.0f, d[o + 2] / 255.0f);
    r.a = d[o + 3] / 255.0f;
    return r;
}

RGBA sample_level(const Image &im, int level, Vec2 uv, WrapMode ws, WrapMode wt) {
    int w = im.width, h = im.height;
    const std::vector<uint8_t> *data = &im.pixels;
    if (level > (int)im.mips.size()) level = (int)im.mips.size();
    if (level > 0) {
        for (int i = 0; i < level; i++) {
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        data = &im.mips[level - 1];
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    float fx = uv.x * w - 0.5f;
    float fy = uv.y * h - 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float ax = fx - x0, ay = fy - y0;

    int xa = wrap_i(x0, w, ws), xb = wrap_i(x0 + 1, w, ws);
    int ya = wrap_i(y0, h, wt), yb = wrap_i(y0 + 1, h, wt);

    RGBA c00 = level_texel(*data, w, xa, ya), c10 = level_texel(*data, w, xb, ya);
    RGBA c01 = level_texel(*data, w, xa, yb), c11 = level_texel(*data, w, xb, yb);

    RGBA out;
    out.c = lerp(lerp(c00.c, c10.c, ax), lerp(c01.c, c11.c, ax), ay);
    out.a = lerpf(lerpf(c00.a, c10.a, ax), lerpf(c01.a, c11.a, ax), ay);
    return out;
}

RGBA sample_tex(const Scene &sc, int tex_idx, Vec2 uv, float uv_footprint) {
    RGBA white{Vec3(1, 1, 1), 1.0f};
    if (tex_idx < 0 || tex_idx >= (int)sc.textures.size()) return white;
    const Texture &t = sc.textures[tex_idx];
    if (t.image < 0 || t.image >= (int)sc.images.size()) return white;
    const Image &im = sc.images[t.image];
    if (!im.valid()) return white;

    if (t.has_transform) {
        float x = uv.x * t.uv_scale.x;
        float y = uv.y * t.uv_scale.y;
        float cr = std::cos(t.uv_rotation), sr = std::sin(t.uv_rotation);
        uv.x = cr * x - sr * y + t.uv_offset.x;
        uv.y = sr * x + cr * y + t.uv_offset.y;
    }

    int maxdim = std::max(im.width, im.height);
    int maxlevel = (int)im.mips.size();
    float lod = 0.0f;
    if (uv_footprint > 0.0f && maxdim > 0) lod = std::log2(uv_footprint * (float)maxdim);
    lod = clampf(lod, 0.0f, (float)maxlevel);
    int l0 = (int)lod;
    float f = lod - (float)l0;

    RGBA out = sample_level(im, l0, uv, t.wrap_s, t.wrap_t);
    if (f > 0.0f && l0 < maxlevel) {
        RGBA b = sample_level(im, l0 + 1, uv, t.wrap_s, t.wrap_t);
        out.c = lerp(out.c, b.c, f);
        out.a = lerpf(out.a, b.a, f);
    }
    if (t.colorspace == ColorSpace::SRGB) out.c = srgb_to_linear(out.c);
    return out;
}

// ---------------------------------------------------------- intersection

inline bool intersect_tri(const Tri &t, const Ray &ray, float &tout, float &u, float &v) {
    Vec3 pvec = cross(ray.d, t.e2);
    float det = dot(t.e1, pvec);
    // dspbr-pt does NOT cull back faces (bvh.glsl intersectTriangle; and
    // sample_bsdf_bounce's ignoreBackfaces is disabled). Culling would drop the
    // exit interface of a transmissive solid (glass -> "only outer wall"), so
    // both faces are kept and two-sidedness is handled via fix_normals.
    if (std::fabs(det) < 1e-12f) return false;
    float inv = 1.0f / det;
    Vec3 tvec = ray.o - t.p0;
    u = dot(tvec, pvec) * inv;
    if (u < -1e-5f || u > 1.0f + 1e-5f) return false;
    Vec3 qvec = cross(tvec, t.e1);
    v = dot(ray.d, qvec) * inv;
    if (v < -1e-5f || u + v > 1.0f + 1e-5f) return false;
    float tt = dot(t.e2, qvec) * inv;
    if (tt < ray.tmin || tt > ray.tmax) return false;
    tout = tt;
    return true;
}

struct Hit {
    float t = 0;
    int tri = -1;
    float u = 0, v = 0;
};

inline bool slab_test(const BvhNode &n, const Ray &ray, float t1) {
    float t0 = ray.tmin;
    for (int a = 0; a < 3; a++) {
        float d = ray.d[a];
        float inv = 1.0f / (std::fabs(d) < 1e-12f ? (d >= 0 ? 1e-12f : -1e-12f) : d);
        float ta = (n.bmin[a] - ray.o[a]) * inv;
        float tb = (n.bmax[a] - ray.o[a]) * inv;
        if (ta > tb) std::swap(ta, tb);
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return false;
    }
    return true;
}

bool bvh_intersect(const std::vector<Tri> &tris, const std::vector<BvhNode> &nodes,
                   const Ray &ray, Hit &hit) {
    if (nodes.empty()) return false;
    int stack[64];
    int sp = 0;
    stack[sp++] = 0;
    float best = ray.tmax;
    int bestTri = -1;
    float bu = 0, bv = 0;
    while (sp > 0) {
        const BvhNode &n = nodes[stack[--sp]];
        if (!slab_test(n, ray, best)) continue;
        if (n.left < 0) {
            for (int i = n.start; i < n.start + n.count; i++) {
                float tt, u, v;
                if (intersect_tri(tris[i], ray, tt, u, v) && tt < best) {
                    best = tt;
                    bestTri = i;
                    bu = u;
                    bv = v;
                }
            }
        } else {
            stack[sp++] = n.left;
            stack[sp++] = n.right;
        }
    }
    if (bestTri < 0) return false;
    hit.t = best;
    hit.tri = bestTri;
    hit.u = bu;
    hit.v = bv;
    return true;
}

bool any_occlusion(const std::vector<Tri> &tris, const std::vector<BvhNode> &nodes, const Ray &ray) {
    Hit h;
    return bvh_intersect(tris, nodes, ray, h);
}

// ------------------------------------------- material closure (material.glsl)

struct MaterialClosure {
    PbrGltfMaterial material;
    float cutout_opacity = 1.0f;
    bool thin_walled = true;
    bool double_sided = false;
    Vec3 n, ng;      // shading / geometric normal, fixed to face wi
    Vec3 tangent;    // anisotropy tangent (rotation applied)
    bool backside = false;
    Vec3 base_color; // for unlit / debug
    int event_type = 0; // persists across bounces (configure does not reset it)
};

RGBA sample_mat(const Scene &sc, const Texture *tx, int tex_idx, Vec2 uv0, Vec2 uv1,
                float footprint) {
    if (tex_idx < 0) return RGBA{Vec3(1, 1, 1), 1.0f};
    Vec2 uv = (tx && tx->texcoord == 1) ? uv1 : uv0;
    return sample_tex(sc, tex_idx, uv, footprint);
}

inline Vec2 tex_uv(const Scene &sc, int tex_idx, Vec2 uv0, Vec2 uv1) {
    if (tex_idx >= 0 && tex_idx < (int)sc.textures.size() &&
        sc.textures[tex_idx].texcoord == 1)
        return uv1;
    return uv0;
}

// fix_normals (utils.glsl): makes n/ng face wi, returns whether we are on the
// back side. Mutates ng (the flipped geometric normal is stored in the closure).
bool fix_normals(Vec3 &n, Vec3 &ng, const Vec3 &wi) {
    bool backside = false;
    if (dot(wi, ng) < 0.0f) {
        ng = -ng;
        backside = true;
    }
    if (dot(ng, n) < 0.0f) n = -n;
    return backside;
}

Vec4 rotation_to_tangent(float angle, const Vec3 &normal, const Vec4 &tangent) {
    if (angle > 0.0f) {
        Vec3 t = tangent.xyz();
        Vec3 b = cross(normal, t) * tangent.w;
        Vec3 tt = normalize(t * std::cos(angle) + b * std::sin(angle));
        return Vec4(tt, tangent.w);
    }
    return tangent;
}

// configure_gltf_material (material.glsl): fills the PbrGltfMaterial and the
// closure from the scene material and the hit attributes.
void configure_gltf_material(const Scene &sc, const Material &mat, Vec2 uv0, Vec2 uv1,
                             Vec4 vcol, bool has_vcol, Vec3 n_geo, Vec3 n_shade, Vec4 tangent,
                             const Vec3 &wi, float footprint, bool has_volume_thick,
                             MaterialClosure &c) {
    PbrGltfMaterial m = pbr::defaultGltfPbrMaterial();

    RGBA base = sample_mat(sc, mat.base_color_tex >= 0 ? &sc.textures[mat.base_color_tex] : nullptr,
                           mat.base_color_tex, uv0, uv1, footprint);
    m.baseColorFactor = Vec4(mat.base_color * base.c, mat.base_alpha * base.a);
    float opacity = m.baseColorFactor.w;
    if (has_vcol) {
        m.baseColorFactor.x *= vcol.x;
        m.baseColorFactor.y *= vcol.y;
        m.baseColorFactor.z *= vcol.z;
        opacity *= vcol.w;
    }
    m.baseColorFactor.w = opacity;
    c.base_color = m.baseColorFactor.xyz();
    c.double_sided = mat.double_sided;

    // alpha: cutout logic (material.glsl)
    float cutout = opacity;
    if (mat.alpha_mode == AlphaMode::Mask)
        cutout = (opacity >= mat.alpha_cutoff) ? 1.0f : 0.0f;
    else if (mat.alpha_mode == AlphaMode::Opaque)
        cutout = 1.0f;
    c.cutout_opacity = cutout;

    m.transmissionFactor =
        mat.has_transmission
            ? mat.transmission *
                  sample_mat(sc, mat.transmission_tex >= 0 ? &sc.textures[mat.transmission_tex] : nullptr,
                             mat.transmission_tex, uv0, uv1, footprint).c.x
            : 0.0f;
    m.diffuseTransmissionFactor =
        mat.has_diffuse_transmission
            ? mat.diffuse_transmission_factor *
                  sample_mat(sc,
                             mat.diffuse_transmission_tex >= 0
                                 ? &sc.textures[mat.diffuse_transmission_tex]
                                 : nullptr,
                             mat.diffuse_transmission_tex, uv0, uv1, footprint).c.x
            : 0.0f;
    m.diffuseTransmissionColorFactor =
        mat.diffuse_transmission_color *
        (mat.diffuse_transmission_color_tex >= 0
             ? sample_mat(sc, &sc.textures[mat.diffuse_transmission_color_tex],
                          mat.diffuse_transmission_color_tex, uv0, uv1, footprint).c
             : Vec3(1, 1, 1));

    c.thin_walled = !has_volume_thick;
    m.ior = mat.has_ior ? mat.ior : 1.5f;

    RGBA mr = sample_mat(sc,
                         mat.metallic_roughness_tex >= 0
                             ? &sc.textures[mat.metallic_roughness_tex]
                             : nullptr,
                         mat.metallic_roughness_tex, uv0, uv1, footprint);
    m.metallicFactor = mat.metallic * mr.c.z;
    m.roughnessFactor = mat.roughness * mr.c.y;

    RGBA aniso = sample_mat(sc,
                            mat.anisotropy_tex >= 0 ? &sc.textures[mat.anisotropy_tex] : nullptr,
                            mat.anisotropy_tex, uv0, uv1, footprint);
    m.anisotropyStrength = mat.anisotropy_strength * aniso.c.z; // strength: blue
    Vec3 aniso_dir(std::cos(mat.anisotropy_rotation), std::sin(mat.anisotropy_rotation), 0.0f);
    if (mat.anisotropy_tex >= 0) aniso_dir = Vec3(aniso.c.x * 2.0f - 1.0f, aniso.c.y * 2.0f - 1.0f, 0.0f);
    m.anisotropyRotation = std::atan2(aniso_dir.y, aniso_dir.x);

    Vec3 spec_col(1, 1, 1);
    if (mat.has_specular) {
        spec_col = mat.specular_color;
        if (mat.specular_color_tex >= 0) {
            // material.glsl: matData.specularColorFactor * pow(specularColor.rgb, 2.2)
            Vec3 s = sample_mat(sc, &sc.textures[mat.specular_color_tex], mat.specular_color_tex,
                                uv0, uv1, footprint)
                         .c;
            spec_col = spec_col * Vec3(std::pow(s.x, 2.2f), std::pow(s.y, 2.2f), std::pow(s.z, 2.2f));
        }
    }
    m.specularColorFactor = spec_col;
    float sf = 1.0f;
    if (mat.has_specular) {
        sf = mat.specular_factor;
        if (mat.specular_tex >= 0)
            sf *= sample_mat(sc, &sc.textures[mat.specular_tex], mat.specular_tex, uv0, uv1,
                             footprint).a;
    }
    m.specularFactor = sf;

    Vec3 sheen_col(0, 0, 0);
    float sheen_rough = 0.0f;
    if (mat.has_sheen) {
        sheen_col = mat.sheen_color;
        if (mat.sheen_color_tex >= 0)
            sheen_col = sheen_col * sample_mat(sc, &sc.textures[mat.sheen_color_tex],
                                               mat.sheen_color_tex, uv0, uv1, footprint).c;
        sheen_rough = mat.sheen_roughness;
        if (mat.sheen_roughness_tex >= 0)
            sheen_rough *= sample_mat(sc, &sc.textures[mat.sheen_roughness_tex],
                                      mat.sheen_roughness_tex, uv0, uv1, footprint).c.x;
    }
    m.sheenColorFactor = sheen_col;
    m.sheenRoughnessFactor = sheen_rough;

    // material.glsl always sets emissiveStrength = 1 (KHR_materials_emissive_strength
    // is not applied by this dspbr-pt revision).
    Vec3 emissive = mat.emissive;
    if (mat.emissive_tex >= 0)
        emissive = emissive * sample_mat(sc, &sc.textures[mat.emissive_tex], mat.emissive_tex, uv0,
                                         uv1, footprint).c;
    m.emissiveFactor = emissive;
    m.emissiveStrength = 1.0f;

    if (mat.has_clearcoat) {
        float cc = mat.clearcoat, cr = mat.clearcoat_roughness;
        if (mat.clearcoat_tex >= 0)
            cc *= sample_mat(sc, &sc.textures[mat.clearcoat_tex], mat.clearcoat_tex, uv0, uv1,
                             footprint).c.x;
        if (mat.clearcoat_roughness_tex >= 0)
            cr *= sample_mat(sc, &sc.textures[mat.clearcoat_roughness_tex],
                             mat.clearcoat_roughness_tex, uv0, uv1, footprint).c.x;
        m.clearcoatFactor = cc;
        m.clearcoatRoughnessFactor = cr;
        m.clearcoatNormalTextureScale = 1.0f;
    }

    m.attenuationColor = mat.attenuation_color;
    m.attenuationDistance = mat.attenuation_distance > 0.0f ? mat.attenuation_distance : 1.0e20f;
    m.thicknessFactor = c.thin_walled ? 0.0f : 1.0f;
    m.multiscatterColorFactor = Vec3(0, 0, 0);
    m.scatterAnisotropy = 0.0f;

    m.iridescenceFactor = 0.0f;
    m.iridescenceIor = 1.3f;
    m.iridescenceThickness = 400.0f;
    if (mat.has_iridescence) {
        float f = mat.iridescence_factor;
        if (mat.iridescence_tex >= 0)
            f *= sample_mat(sc, &sc.textures[mat.iridescence_tex], mat.iridescence_tex, uv0, uv1,
                            footprint).c.x;
        float th = mat.iridescence_thickness_min;
        if (mat.iridescence_thickness_tex >= 0)
            th = lerpf(mat.iridescence_thickness_min, mat.iridescence_thickness_max,
                       sample_mat(sc, &sc.textures[mat.iridescence_thickness_tex],
                                  mat.iridescence_thickness_tex, uv0, uv1, footprint).c.y);
        m.iridescenceFactor = f;
        m.iridescenceIor = mat.iridescence_ior;
        m.iridescenceThickness = th;
    }
    m.dispersion = mat.has_dispersion ? mat.dispersion : 0.0f;
    m.normalTextureScale = mat.normal_scale;
    c.material = m;

    // normals / tangent
    Vec3 n = n_shade;
    if (!is_finite(n) || length_sq(n) < 1e-12f) n = n_geo; // degenerate interpolation
    if (mat.normal_tex >= 0) {
        RGBA t = sample_mat(sc, &sc.textures[mat.normal_tex], mat.normal_tex, uv0, uv1, footprint);
        Vec3 nt(t.c.x * 2.0f - 1.0f, t.c.y * 2.0f - 1.0f, t.c.z * 2.0f - 1.0f);
        nt.x *= mat.normal_scale;
        nt.y *= mat.normal_scale;
        // Build an orthonormal basis from n and the tangent. If the tangent is
        // degenerate (parallel to n / near zero) do NOT normalize a zero vector
        // (that yields NaN -> black edge pixels); pick a valid perpendicular.
        Vec3 tp = tangent.xyz() - n * dot(tangent.xyz(), n);
        Vec3 T;
        if (length_sq(tp) > 1e-12f) {
            T = normalize(tp);
        } else {
            Vec3 up = std::fabs(n.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
            T = normalize(cross(up, n));
            if (!is_finite(T) || length_sq(T) < 1e-12f) T = Vec3(1, 0, 0);
        }
        Vec3 B = cross(n, T) * (tangent.w == 0.0f ? 1.0f : tangent.w);
        Vec3 nm = T * nt.x + B * nt.y + n * nt.z;
        if (is_finite(nm) && length_sq(nm) > 1e-12f) n = normalize(nm);
    }
    Vec3 ng = n_geo;
    if (!is_finite(ng) || length_sq(ng) < 1e-12f) ng = n;
    c.n = n;
    c.ng = ng;
    c.backside = fix_normals(c.n, c.ng, wi);
    if (!is_finite(c.n) || !is_finite(c.ng) || length_sq(c.n) < 1e-12f) {
        // Last-resort fallback: use the ray direction so shading stays finite.
        c.n = c.ng = (-wi);
    }

    Vec4 t4(tangent.xyz(), tangent.w);
    c.tangent = rotation_to_tangent(m.anisotropyRotation + (float)PI, c.n, t4).xyz();
    if (!is_finite(c.tangent) || length_sq(c.tangent) < 1e-12f) {
        Vec3 up = std::fabs(c.n.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
        c.tangent = normalize(cross(up, c.n));
        if (!is_finite(c.tangent) || length_sq(c.tangent) < 1e-12f) c.tangent = Vec3(1, 0, 0);
    }
}

// --------------------------------------------- adapter (pbr_material_adapter)

PbrGltfState make_state(const MaterialClosure &c, float medium_ior) {
    PbrLayerNormals ln;
    ln.rawGeometry = c.ng;
    ln.geometry = c.ng;
    ln.shadingGeometry = c.n;
    ln.interfaceBase = c.n;
    ln.base = c.n;
    ln.clearcoat = c.n;
    ln.anisotropyTangent = c.tangent;
    PbrClosure closure = pbr::pbrBuildClosureFromGltf(c.material, ln, c.tangent);
    PbrTransport tr;
    tr.currentMediumIor = medium_ior;
    tr.interfaceIor = c.material.ior;
    tr.thinWalled = c.thin_walled ? 1.0f : 0.0f;
    tr._pad0 = 0.0f;
    return pbr::pbrPrepareStateFromGltf(c.material, closure, tr);
}

PbrNormals make_normals(const MaterialClosure &c) {
    PbrNormals n;
    n.rawGeometryNormal = c.ng;
    n.transmissionNormal = c.n;
    n.baseNormal = c.n;
    n.clearcoatNormal = c.n;
    return n;
}

// pbr_material_eval: returns the BSDF (without cosine factor).
Vec3 material_eval(const PbrGltfState &st, const PbrNormals &nm, const Vec3 &n, const Vec3 &wi,
                   const Vec3 &wo) {
    PbrDirections d{wi, wo};
    return pbr::pbrEvalGltfState(st, d, nm) / std::max(std::fabs(dot(n, wo)), 1.0e-4f);
}

inline float mis_balance(float a, float b) { return a / (a + b); }

// ---------------------------------------------------------------- lights

inline Vec3 env_gradient(const Vec3 &d, float intensity) {
    // Neutral (achromatic) studio gradient used when no HDR is loaded.
    float t = 0.5f * (d.y + 1.0f);
    return lerp(Vec3(0.03f, 0.03f, 0.03f), Vec3(0.22f, 0.22f, 0.22f), t) * intensity;
}

Vec3 env_at(const EnvMap *env, const Vec3 &d, float intensity) {
    return env ? env_eval(*env, d) * intensity : env_gradient(d, intensity);
}

// Direct punctual lights (lighting.glsl sampleAndEvaluatePointLight, extended
// to directional/spot). Delta lights, so no MIS pdf is needed.
Vec3 punctual_lights(const Scene &sc, const std::vector<Tri> &tris,
                     const std::vector<BvhNode> &nodes, const MaterialClosure &c,
                     const PbrGltfState &st, const PbrNormals &nm, const Vec3 &p, const Vec3 &wi) {
    Vec3 L(0, 0, 0);
    Vec3 n = c.backside ? -c.n : c.n;
    for (const Light &l : sc.lights) {
        Vec3 light_dir;
        Vec3 emission;
        float dist2 = 1.0f;
        float tmax = TFAR_MAX;
        if (l.type == LightType::Directional) {
            light_dir = normalize(-l.direction);
            emission = l.color * l.intensity;
        } else {
            Vec3 d = l.position - p;
            dist2 = dot(d, d);
            if (dist2 < 1e-8f) continue;
            light_dir = d * (1.0f / std::sqrt(dist2));
            emission = l.color * l.intensity;
            tmax = std::sqrt(dist2) - 1e-3f;
            if (l.type == LightType::Spot) {
                float cos_t = dot(normalize(l.direction), -light_dir);
                float co = std::cos(l.outer_angle), ci = std::cos(l.inner_angle);
                emission = emission * clampf((cos_t - co) / std::max(1e-4f, ci - co), 0.0f, 1.0f);
            }
        }
        float cosNL = dot(light_dir, n);
        if (cosNL <= EPS_COS) continue;
        if (tmax <= 1e-4f) continue;
        float seps = std::max(1e-4f, 1e-4f * std::sqrt(dist2)); // geometry normal offset
        Ray shadow{p + c.ng * seps, light_dir, 0.0f, tmax};
        if (any_occlusion(tris, nodes, shadow)) continue;
        Vec3 f = material_eval(st, nm, n, wi, light_dir);
        L += f * (emission / std::max(dist2, 1e-6f)) * cosNL;
    }
    return L;
}

// Emitted radiance of an emissive triangle at barycentric (u,v), matching the
// material closure (emissive factor * emissive texture, emissiveStrength = 1).
Vec3 emissive_radiance(const Scene &sc, const Tri &t, float u, float v) {
    const Material &mat = sc.materials[t.mat];
    Vec3 e = mat.emissive;
    if (mat.emissive_tex >= 0) {
        float w0 = 1.0f - u - v;
        Vec2 uv0 = t.a0 * w0 + t.a1 * u + t.a2 * v;
        Vec2 uv1 = t.b0 * w0 + t.b1 * u + t.b2 * v;
        e = e * sample_mat(sc, &sc.textures[mat.emissive_tex], mat.emissive_tex, uv0, uv1, 1e-4f).c;
    }
    return e;
}

// ------------------------------------------------------------------ trace

// eval_direct_light_contribution (misptdl.glsl): environment (IBL) NEE with MIS,
// the punctual lights, and NEE against emissive geometry. Returns radiance (no
// path weight).
Vec3 direct_light(const Scene &sc, const std::vector<Tri> &tris, const std::vector<BvhNode> &nodes,
                  const MaterialClosure &c, const PbrGltfState &state, const PbrNormals &nm,
                  const Vec3 &p, const Vec3 &wi, const RenderSettings &st, const EnvMap *env,
                  Rng &rng, const std::vector<EmissiveTri> &emis, float emis_total) {
    Vec3 L(0, 0, 0);
    Vec3 n = c.backside ? -c.n : c.n;

    if (env) {
        float epdf;
        Vec3 ed = env_sample(*env, rng.next(), rng.next(), epdf);
        float cosNL = dot(ed, n);
        if (cosNL > EPS_COS && epdf > EPS_PDF) {
            // Offset along the GEOMETRIC normal: the shading normal can point
            // into the surface near silhouette edges, which caused self-shadowing
            // (dark edge pixels).
            Ray shadow{p + c.ng * st.ray_eps, ed, 0.0f, TFAR_MAX};
            if (!any_occlusion(tris, nodes, shadow)) {
                Vec3 bsdf = material_eval(state, nm, n, wi, ed);
                float bpdf = pbr::pbrPdfGltfState(state, PbrDirections{wi, ed}, nm);
                Vec3 Ld = env_at(env, ed, st.env_intensity) * bsdf * (cosNL / epdf);
                if (bpdf > EPS_PDF) Ld = Ld * mis_balance(epdf, bpdf);
                L += Ld;
            }
        }
    } else {
        // Procedural (analytic) environment: cosine-sampled NEE with MIS. Without
        // this the default env is only reached by rare BSDF rays, leaving grazing
        // silhouette pixels almost unlit (near-black edge speckles).
        Vec3 ed = sample_cosine(n, rng.next2());
        float cosNL = dot(ed, n);
        float epdf = cosNL * INV_PI;
        if (cosNL > EPS_COS && epdf > EPS_PDF) {
            Ray shadow{p + c.ng * st.ray_eps, ed, 0.0f, TFAR_MAX};
            if (!any_occlusion(tris, nodes, shadow)) {
                Vec3 bsdf = material_eval(state, nm, n, wi, ed);
                float bpdf = pbr::pbrPdfGltfState(state, PbrDirections{wi, ed}, nm);
                Vec3 Ld = env_gradient(ed, st.env_intensity) * bsdf * (cosNL / epdf);
                if (bpdf > EPS_PDF) Ld = Ld * mis_balance(epdf, bpdf);
                L += Ld;
            }
        }
    }

    L += punctual_lights(sc, tris, nodes, c, state, nm, p, wi);

    // NEE against emissive geometry (area light sampling with MIS against BSDF
    // sampling). This is what makes emissive materials illuminate the scene.
    if (emis_total > 0.0f && !emis.empty()) {
        const std::vector<EmissiveTri> &E = emis;
        int ei = emis_lower_bound(E, rng.next());
        const EmissiveTri &lt = E[ei];
        const Tri &t = tris[lt.tri];
        float u = rng.next(), v = rng.next();
        if (u + v > 1.0f) {
            u = 1.0f - u;
            v = 1.0f - v;
        }
        Vec3 lp = t.p0 + t.e1 * u + t.e2 * v;
        Vec3 ln = normalize(cross(t.e1, t.e2));
        Vec3 d = lp - p;
        float dist2 = dot(d, d);
        if (dist2 > 1e-8f) {
            float dist = std::sqrt(dist2);
            Vec3 ld = d * (1.0f / dist);
            float cosL = std::fabs(dot(ln, ld)); // two-sided emission
            float cosS = dot(ld, n);
            if (cosL > 1e-4f && cosS > EPS_COS) {
                float seps = std::max(st.ray_eps, 1e-4f * dist);
                Ray shadow{p + c.ng * seps, ld, 0.0f, dist - 1e-3f};
                if (!any_occlusion(tris, nodes, shadow)) {
                    Vec3 emit = emissive_radiance(sc, t, u, v);
                    float pdf_area = (lt.power / emis_total) / lt.area;
                    float pdf_solid = pdf_area * dist2 / cosL;
                    Vec3 f = material_eval(state, nm, n, wi, ld);
                    Vec3 contrib = emit * f * (cosS / pdf_solid);
                    float bpdf = pbr::pbrPdfGltfState(state, PbrDirections{wi, ld}, nm);
                    if (bpdf > 0.0f) contrib = contrib * mis_balance(pdf_solid, bpdf);
                    L += contrib;
                }
            }
        }
    }
    return L;
}

Vec3 trace_path(const Scene &sc, const std::vector<Tri> &tris, const std::vector<BvhNode> &nodes,
                Ray ray, Rng &rng, const RenderSettings &st, float pixel_world,
                const EnvMap *env, long dbg = -1,
                const std::atomic<bool> *cancel = nullptr,
                const std::vector<EmissiveTri> *emis = nullptr, float emis_total = 0.0f,
                const std::vector<int> *tri_emis = nullptr) {
    Vec3 L(0, 0, 0);
    Vec3 beta(1, 1, 1);
    float last_pdf = 0.0f;
    bool last_specular = false; // dspbr: "pinhole camera is considered singular"
    float medium_ior = 1.0f;
    bool vlog = force_trace() || (dbg >= 0 && dbg == trace_target());
    bool capture = (!vlog && trace_th() > 0.0f);
    std::string log;
    auto emit = [&](const char *fmt, ...) {
        if (!vlog && !capture) return;
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (vlog)
            std::fputs(buf, stdout);
        else
            log += buf;
    };
    auto finish = [&](Vec3 r, bool do_clamp = true) {
        if (vlog || (capture && max_comp(r) > trace_th())) {
            if (capture) std::fputs(log.c_str(), stdout);
            std::printf("[px %ld] L=(%.4f,%.4f,%.4f)\n", dbg, r.x, r.y, r.z);
            std::fflush(stdout);
        }
        if (do_clamp && st.clamp_threshold > 0.0f)
            r = minv(maxv(r, Vec3(0, 0, 0)),
                     Vec3(st.clamp_threshold, st.clamp_threshold, st.clamp_threshold));
        return r;
    };

    // fillRenderState equivalent. Reuses `c` so event_type persists across
    // bounces (configure_gltf_material does not write event_type).
    MaterialClosure c;
    auto fill = [&](const Ray &r, const Hit &h) {
        const Tri &tri = tris[h.tri];
        const Material &mat = sc.materials[tri.mat];
        float w0 = 1.0f - h.u - h.v;
        Vec2 uv0 = tri.a0 * w0 + tri.a1 * h.u + tri.a2 * h.v;
        Vec2 uv1 = tri.b0 * w0 + tri.b1 * h.u + tri.b2 * h.v;
        Vec4 vcol = tri.c0 * w0 + tri.c1 * h.u + tri.c2 * h.v;
        float footprint = std::max(1e-7f, h.t * pixel_world * tri.uv_density);
        Vec3 n_geo = normalize(cross(tri.e1, tri.e2));
        if (dot(n_geo, r.d) > 0) n_geo = -n_geo;
        Vec3 n_shade = normalize(tri.n0 * w0 + tri.n1 * h.u + tri.n2 * h.v);
        if (length_sq(n_shade) < 1e-10f) n_shade = n_geo;
        Vec3 tang = tri.t0 * w0 + tri.t1 * h.u + tri.t2 * h.v;
        float tsign = tri.s0 * w0 + tri.s1 * h.u + tri.s2 * h.v;
        bool has_volume_thick = mat.has_volume && mat.thickness_factor > 0.0f;
        configure_gltf_material(sc, mat, uv0, uv1, vcol, tri.has_color, n_geo, n_shade,
                                Vec4(tang, tsign), -r.d, footprint, has_volume_thick, c);
    };

    emit("[px %ld] start o=(%.3f,%.3f,%.3f) d=(%.3f,%.3f,%.3f)\n", dbg, ray.o.x, ray.o.y,
         ray.o.z, ray.d.x, ray.d.y, ray.d.z);

    Hit hit;
    if (!bvh_intersect(tris, nodes, ray, hit)) {
        Vec3 e = env_at(env, ray.d, st.env_intensity);
        emit("  primary miss env=(%.4f,%.4f,%.4f)\n", e.x, e.y, e.z);
        return finish(e, /*do_clamp=*/false); // dspbr returns background unclamped
    }
    fill(ray, hit);

    int bounce = 0;
    for (;;) {
        // Prompt cancellation (checked every bounce so even a pathological
        // camera can always be interrupted) and a hard safety cap.
        if (cancel && cancel->load(std::memory_order_relaxed)) break;
        if (bounce > 4096) break;
        if (!(bounce < st.max_bounces ||
              (last_specular && bounce < st.max_specular_bounces)))
            break;

        // Russian roulette (dspbr check_russian_roulette_path_termination):
        // fixed termination probability, weight compensated every iteration.
        beta = beta * (1.0f / (1.0f - RR_TERMINATION_PROB));
        if (bounce > RR_START_DEPTH && rng.next() <= RR_TERMINATION_PROB) {
            emit("  b%-2d RR terminate\n", bounce);
            break;
        }

        // Absorption inside a medium (uses the distance travelled by this ray).
        if (c.backside && !c.thin_walled) {
            Vec3 att = maxv(c.material.attenuationColor, Vec3(1e-3f, 1e-3f, 1e-3f));
            float dist = std::max(c.material.attenuationDistance, 1e-4f);
            Vec3 sigma(-std::log(att.x), -std::log(att.y), -std::log(att.z));
            sigma = sigma / dist;
            beta = beta * expv(sigma * -hit.t);
        }

        emit("  b%-2d hit mat='%s' rough=%.3f metal=%.3f alpha=%.3f back=%d transp=%.2f thin=%d\n",
             bounce, sc.materials[tris[hit.tri].mat].name.c_str(), c.material.roughnessFactor,
             c.material.metallicFactor, c.cutout_opacity, (int)c.backside,
             c.material.transmissionFactor, (int)c.thin_walled);

        Vec3 p = ray.at(hit.t);
        // Emissive contribution, MIS-weighted against emissive NEE when the
        // previous bounce was a BSDF sample (not the camera / a delta bounce).
        Vec3 emit_rad = c.material.emissiveFactor * c.material.emissiveStrength;
        if (max_comp(emit_rad) > 0.0f) {
            float w = 1.0f;
            if (bounce > 0 && !last_specular && last_pdf > 0.0f && emis_total > 0.0f && tri_emis &&
                hit.tri < (int)tri_emis->size() && (*tri_emis)[hit.tri] >= 0) {
                const EmissiveTri &lt = (*emis)[(*tri_emis)[hit.tri]];
                Vec3 d = p - ray.o;
                float dist2 = dot(d, d);
                if (dist2 > 1e-8f && lt.area > 0.0f) {
                    float dist = std::sqrt(dist2);
                    Vec3 ld = d * (1.0f / dist);
                    Vec3 ln = normalize(cross(tris[hit.tri].e1, tris[hit.tri].e2));
                    float cosL = std::fabs(dot(ln, ld));
                    float pdf_area = (lt.power / emis_total) / lt.area;
                    float pdf_light = cosL > 1e-4f ? pdf_area * dist2 / cosL : 0.0f;
                    if (pdf_light > 0.0f) w = mis_balance(last_pdf, pdf_light);
                }
            }
            L += emit_rad * beta * w;
        }

        if (sc.materials[tris[hit.tri].mat].unlit) {
            L += beta * c.base_color;
            break;
        }

        last_specular = (c.event_type & E_DELTA) != 0;

        Vec3 wi = -ray.d;
        PbrGltfState state = make_state(c, medium_ior);
        PbrNormals nm = make_normals(c);

        // Direct lighting FIRST, so it is never lost when the BSDF sample below
        // turns out degenerate (pdf < EPS_PDF). Evaluating NEE after sampling
        // used to return ~0 at grazing/silhouette pixels -> near-black edges.
        if (!last_specular)
            L += direct_light(sc, tris, nodes, c, state, nm, p, wi, st, env, rng,
                              emis ? *emis : kEmptyEmis, emis_total) *
                 beta;

        // sample_bsdf_bounce (misptdl.glsl)
        Vec3 bounce_weight(1, 1, 1);
        float pdf = 1.0f;
        Vec3 wo;
        if (rng.next() > c.cutout_opacity) {
            // cutout / alpha: treat as a delta that continues straight through
            c.event_type |= E_DELTA;
            wo = -wi;
            emit("  b%-2d alpha pass-through\n", bounce);
        } else {
            PbrRandoms r;
            r.component = rng.next();
            r.lobe = rng.next2();
            r.boundary = rng.next();
            PbrSample sm =
                pbr::pbrSampleGltfState(state, PbrDirections{wi, Vec3(0, 0, 0)}, nm, r);
            c.event_type = 0;
            if (sm.specular > 0.5f && c.material.roughnessFactor <= MINIMUM_ROUGHNESS)
                c.event_type |= E_DELTA;
            if (dot(sm.direction, c.ng) < 0.0f)
                c.event_type |= E_TRANSMISSION;
            else
                c.event_type |= E_REFLECTION;
            wo = sm.direction;
            pdf = sm.pdf;
            bounce_weight = sm.bsdfOverPdf;
            if (sm.crossedBoundary > 0.5f) medium_ior = sm.nextMediumIor;
            if (pdf < EPS_PDF) {
                emit("  b%-2d pdf<EPS_PDF terminate\n", bounce);
                return finish(L);
            }
        }
        if (!is_finite(wo) || !is_finite(bounce_weight) || !std::isfinite(pdf))
            break; // degenerate sample

        last_pdf = pdf;

        beta = beta * bounce_weight;

        Vec3 off = (dot(wo, c.ng) < 0.0f) ? -c.ng : c.ng;
        ray = Ray{p + off * st.ray_eps, normalize(wo), 0.0f, TFAR_MAX};

        if (bvh_intersect(tris, nodes, ray, hit)) {
            fill(ray, hit);
            bounce++;
        } else {
            float misw = 1.0f;
            if (!last_specular) {
                // MIS against the corresponding NEE pdf (env importance map, or
                // the cosine pdf for the procedural environment).
                float bp = env ? env_pdf(*env, ray.d)
                               : std::max(dot(ray.d, c.n), 0.0f) * INV_PI;
                if (bp > 0.0f) misw = mis_balance(last_pdf, bp);
            }
            Vec3 e = env_at(env, ray.d, st.env_intensity);
            emit("  b%-2d miss env=(%.4f,%.4f,%.4f) mis=%.3f beta=(%.4f,%.4f,%.4f)\n", bounce,
                 e.x, e.y, e.z, misw, beta.x, beta.y, beta.z);
            L += beta * e * misw;
            break;
        }
        if (max_comp(beta) < 1e-8f) break;
    }
    return finish(L);
}

inline Vec3 sat3(Vec3 v) { return minv(maxv(v, Vec3(0, 0, 0)), Vec3(1, 1, 1)); }

inline Vec3 powv3(Vec3 v, float e) {
    return {std::pow(v.x, e), std::pow(v.y, e), std::pow(v.z, e)};
}

// tonemap.frag (dspbr-pt). Modes: 0 None, 1 Reinhard, 2 Cineon, 3 ACES, 4 Uncharted2.
inline Vec3 apply_tonemap(Vec3 color, float exposure, int mode) {
    if (mode == 0) {
        return exposure * color; // LinearToneMapping
    }
    if (mode == 1) {
        color = color * exposure;
        return sat3(color / (Vec3(1, 1, 1) + color));
    }
    if (mode == 2) {
        color = color * exposure;
        color = maxv(Vec3(0, 0, 0), color - Vec3(0.004f, 0.004f, 0.004f));
        Vec3 num = color * (color * 6.2f + Vec3(0.5f));
        Vec3 den = color * (color * 6.2f + Vec3(1.7f)) + Vec3(0.06f);
        return powv3(num / den, 2.2f);
    }
    if (mode == 3) {
        color = color * (exposure / 0.6f); // pre-exposed outside the operator
        static const float in_m[9] = {0.59719f, 0.35458f, 0.04823f, 0.07600f, 0.90834f,
                                      0.01566f, 0.02840f, 0.13383f, 0.83777f};
        static const float out_m[9] = {1.60475f, -0.53108f, -0.07367f, -0.10208f, 1.10813f,
                                       -0.00605f, -0.00327f, -0.07276f, 1.07602f};
        auto mul3 = [](const float *m, Vec3 v) {
            return Vec3(m[0] * v.x + m[1] * v.y + m[2] * v.z,
                        m[3] * v.x + m[4] * v.y + m[5] * v.z,
                        m[6] * v.x + m[7] * v.y + m[8] * v.z);
        };
        color = mul3(in_m, color);
        Vec3 a = color * (color + Vec3(0.0245786f)) - Vec3(0.000090537f);
        Vec3 b = color * (Vec3(0.983729f) * color + Vec3(0.4329510f)) + Vec3(0.238081f);
        color = a / b;
        color = mul3(out_m, color);
        return sat3(color);
    }
    if (mode == 4) {
        color = color * exposure;
        auto helper = [](Vec3 x) {
            Vec3 num = x * (x * 0.15f + Vec3(0.10f * 0.50f)) + Vec3(0.20f * 0.02f);
            Vec3 den = x * (x * 0.15f + Vec3(0.50f)) + Vec3(0.20f * 0.30f);
            return maxv(num / den - Vec3(0.02f / 0.30f), Vec3(0, 0, 0));
        };
        Vec3 wp(1, 1, 1);
        return sat3(helper(color) / helper(wp));
    }
    return color;
}

inline Vec3 encode_gamma(Vec3 c) { return powv3(c, 1.0f / 2.2f); }

} // namespace

// ------------------------------------------------------------------ Impl

struct Renderer::Impl {
    std::vector<Tri> tris;
    std::vector<BvhNode> nodes;
    std::vector<EmissiveTri> emissive;
    float emissive_total = 0.0f;
    std::vector<int> tri_emis; // triangle index -> index into `emissive` (or -1)

    int build_node(std::vector<int> &order, int lo, int hi, int depth) {
        int ni = (int)nodes.size();
        nodes.push_back(BvhNode{});
        Vec3 bmin(INF), bmax(-INF), cmin(INF), cmax(-INF);
        for (int i = lo; i < hi; i++) {
            const Tri &t = tris[order[i]];
            bmin = minv(bmin, tri_min(t));
            bmax = maxv(bmax, tri_max(t));
            Vec3 c = tri_centroid(t);
            cmin = minv(cmin, c);
            cmax = maxv(cmax, c);
        }
        nodes[ni].bmin = bmin;
        nodes[ni].bmax = bmax;
        int count = hi - lo;
        Vec3 ext = cmax - cmin;
        if (count <= 4 || depth >= 32 || max_comp(ext) < 1e-9f) {
            nodes[ni].start = lo;
            nodes[ni].count = count;
            return ni;
        }
        int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
        int mid = lo + count / 2;
        std::nth_element(order.begin() + lo, order.begin() + mid, order.begin() + hi,
                         [&](int a, int b) {
                             return tri_centroid(tris[a])[axis] < tri_centroid(tris[b])[axis];
                         });
        int left = build_node(order, lo, mid, depth + 1);
        int right = build_node(order, mid, hi, depth + 1);
        nodes[ni].left = left;
        nodes[ni].right = right;
        nodes[ni].count = 0;
        return ni;
    }

    void build(const Scene &sc) {
        tris.clear();
        for (const Mesh &m : sc.meshes) {
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                const Vertex &a = m.vertices[m.indices[i]];
                const Vertex &b = m.vertices[m.indices[i + 1]];
                const Vertex &c = m.vertices[m.indices[i + 2]];
                Tri t;
                t.p0 = a.pos;
                t.e1 = b.pos - a.pos;
                t.e2 = c.pos - a.pos;
                t.n0 = a.normal;
                t.n1 = b.normal;
                t.n2 = c.normal;
                t.a0 = a.uv0; t.a1 = b.uv0; t.a2 = c.uv0;
                t.b0 = a.uv1; t.b1 = b.uv1; t.b2 = c.uv1;
                t.c0 = a.color; t.c1 = b.color; t.c2 = c.color;
                t.has_color = a.has_color || b.has_color || c.has_color;
                t.t0 = a.tangent; t.t1 = b.tangent; t.t2 = c.tangent;
                t.s0 = a.tangent_sign; t.s1 = b.tangent_sign; t.s2 = c.tangent_sign;
                // If no TANGENT attribute was authored, derive a per-face tangent
                // from the UV derivatives. A degenerate (zero) tangent would
                // collapse the kernel's anisotropic GGX frame and make the
                // distribution blow up at grazing angles.
                if (length_sq(t.t0) + length_sq(t.t1) + length_sq(t.t2) < 1e-12f) {
                    Vec3 ng = normalize(cross(t.e1, t.e2));
                    Vec2 duv1 = t.a1 - t.a0, duv2 = t.a2 - t.a0;
                    float det = duv1.x * duv2.y - duv2.x * duv1.y;
                    Vec3 T(0, 0, 0);
                    if (std::fabs(det) > 1e-12f) {
                        float r = 1.0f / det;
                        T = (t.e1 * duv2.y - t.e2 * duv1.y) * r;
                    }
                    if (length_sq(T) < 1e-12f) {
                        Vec3 up = std::fabs(ng.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
                        T = cross(up, ng);
                    }
                    T = normalize(T);
                    t.t0 = t.t1 = t.t2 = T;
                    t.s0 = t.s1 = t.s2 = 1.0f;
                }
                t.mat = m.material;
                t.double_sided =
                    (m.material >= 0 && m.material < (int)sc.materials.size())
                        ? sc.materials[m.material].double_sided
                        : false;
                t.det_positive = m.det_positive;
                {
                    Vec2 duv1 = t.a1 - t.a0, duv2 = t.a2 - t.a0;
                    float uv_area = 0.5f * std::fabs(duv1.x * duv2.y - duv2.x * duv1.y);
                    float w_area = 0.5f * length(cross(t.e1, t.e2));
                    t.uv_density = w_area > 1e-12f ? std::sqrt(uv_area / w_area) : 0.0f;
                }
                tris.push_back(t);
            }
        }
        nodes.clear();
        if (tris.empty()) return;
        std::vector<int> order(tris.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
        nodes.reserve(tris.size() * 2);
        build_node(order, 0, (int)tris.size(), 0);
        std::vector<Tri> sorted(tris.size());
        for (size_t i = 0; i < order.size(); i++) sorted[i] = tris[order[i]];
        tris.swap(sorted);

        // Collect emissive triangles (after sorting, so indices are final).
        emissive.clear();
        tri_emis.assign(tris.size(), -1);
        for (size_t i = 0; i < tris.size(); i++) {
            const Tri &t = tris[i];
            const Material &mat = sc.materials[t.mat];
            if (mat.unlit) continue;
            float area = 0.5f * length(cross(t.e1, t.e2));
            if (area <= 1e-12f) continue;
            float power = luminance(mat.emissive) * area;
            if (power <= 0.0f) continue;
            int idx = (int)emissive.size();
            EmissiveTri e;
            e.tri = (int)i;
            e.area = area;
            e.power = power;
            emissive.push_back(e);
            tri_emis[i] = idx;
        }
        emissive_total = 0.0f;
        for (EmissiveTri &e : emissive) emissive_total += e.power;
        if (emissive_total > 0.0f) {
            float acc = 0.0f;
            for (EmissiveTri &e : emissive) {
                acc += e.power;
                e.cdf = acc / emissive_total;
            }
        }
    }
};

// --------------------------------------------------------------- Renderer

Renderer::Renderer() : impl_(new Impl()) {}
Renderer::~Renderer() = default;

void Renderer::set_scene(Scene scene) {
    scene_ = std::move(scene);
    has_scene_ = true;
    impl_->build(scene_);
    reset_camera();
    // Reset accumulation AND resize display_/accum_ to the current dimensions.
    // (Previously display_ was cleared to size 0, which made the next partial
    // publish write out of bounds and produce a permanently black image.)
    reset_accum();
}

void Renderer::set_environment(EnvMap env) {
    environment_ = std::move(env);
    has_environment_ = environment_.valid();
    reset_accum();
}

void Renderer::clear_environment() {
    environment_ = EnvMap();
    has_environment_ = false;
    reset_accum();
}

void Renderer::trace_debug(int x, int y, const RenderSettings &settings, int sample) {
    if (!has_scene_ || width_ <= 0 || height_ <= 0) return;
    float aspect = (float)width_ / (float)height_;
    float tanHalf = std::tan(camera_.fov * 0.5f);
    float pixel_world = 2.0f * tanHalf / (float)height_;
    Vec3 fwd = camera_.forward(), rgt = camera_.right(), up = camera_.up();
    Rng rng(x, y, sample * std::max(1, settings.max_bounces));
    float r0 = rng.next(), r1 = rng.next();
    float nx = (2.0f * x + 1.0f) / width_ - 1.0f + r0 / width_;
    float ny = (2.0f * y + 1.0f) / height_ - 1.0f + r1 / height_;
    float sx = nx * tanHalf * aspect;
    float sy = ny * tanHalf;
    Vec3 dir = normalize(fwd + rgt * sx + up * sy);
    const EnvMap *env = has_environment_ ? &environment_ : nullptr;
    force_trace() = true;
    std::printf("== trace_debug pixel (%d,%d) sample %d ==\n", x, y, sample);
    trace_path(scene_, impl_->tris, impl_->nodes, Ray{camera_.position, dir, 0.0f, TFAR_MAX}, rng,
               settings, pixel_world, env, (long)y * 100000 + x);
    force_trace() = false;
    std::fflush(stdout);
}

void Renderer::reset_camera() {
    if (!has_scene_) {
        camera_ = CameraState();
        return;
    }
    if (scene_.camera.valid) {
        camera_.position = scene_.camera.position;
        Vec3 f = normalize(scene_.camera.forward);
        camera_.yaw = std::atan2(f.x, -f.z);
        camera_.pitch = std::asin(clampf(f.y, -1.0f, 1.0f));
        camera_.fov = clampf(scene_.camera.yfov, 0.1f, 2.5f);
        return;
    }
    camera_.fov = 0.9f;
    if (scene_.has_bbox) {
        Vec3 center = (scene_.bbox_min + scene_.bbox_max) * 0.5f;
        Vec3 size = scene_.bbox_max - scene_.bbox_min;
        float radius = std::max(1e-3f, 0.5f * length(size));
        float dist = radius / std::tan(camera_.fov * 0.5f) * 1.6f;
        Vec3 dir = normalize(Vec3(1.0f, 0.45f, 1.0f));
        camera_.position = center + dir * dist;
        Vec3 look = normalize(center - camera_.position);
        camera_.yaw = std::atan2(look.x, -look.z);
        camera_.pitch = std::asin(clampf(look.y, -1.0f, 1.0f));
    } else {
        camera_.position = Vec3(0, 0, 4);
        camera_.yaw = 0;
        camera_.pitch = 0;
    }
}

void Renderer::resize(int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;
    display_.assign((size_t)width * height, Vec3(0, 0, 0));
    reset_accum();
}

void Renderer::reset_accum() {
    size_t n = (size_t)std::max(1, width_) * std::max(1, height_);
    accum_.assign(n, Vec3(0, 0, 0));
    {
        std::lock_guard<std::mutex> lk(display_mutex_);
        display_.assign(n, Vec3(0, 0, 0));
    }
    accumulated_ = 0;
}

void Renderer::reset_accum_keep_display() {
    size_t n = (size_t)std::max(1, width_) * std::max(1, height_);
    accum_.assign(n, Vec3(0, 0, 0));
    {
        std::lock_guard<std::mutex> lk(display_mutex_);
        if (display_.size() != n) display_.assign(n, Vec3(0, 0, 0));
    }
    accumulated_ = 0;
}

std::vector<Vec3> Renderer::snapshot_display() const {
    std::lock_guard<std::mutex> lk(display_mutex_);
    return display_;
}

bool Renderer::render_frame(const RenderSettings &settings,
                            const std::function<bool()> &should_stop) {
    const EnvMap *env = has_environment_ ? &environment_ : nullptr;
    return render_frame(settings, camera_, env, should_stop, {});
}

bool Renderer::render_frame(const RenderSettings &settings, const CameraState &cam,
                            const EnvMap *env, const std::function<bool()> &should_stop,
                            const std::function<void()> &on_progress,
                            const std::atomic<bool> *cancel_flag) {
    if (!has_scene_ || width_ <= 0 || height_ <= 0) return true;
    if ((int)accum_.size() != width_ * height_) reset_accum();

    settings_ = settings;
    int spp = std::max(1, settings.samples_per_frame);
    int sample_base = accumulated_.load();
    const int denom = sample_base + spp; // divisor for the in-progress batch
    const uint32_t frame_seed = fresh_frame_seed(); // uncorrelated across frames

    float aspect = (float)width_ / (float)height_;
    float tanHalf = std::tan(cam.fov * 0.5f);
    float pixel_world = 2.0f * tanHalf / (float)height_;
    Vec3 fwd = cam.forward();
    Vec3 rgt = cam.right();
    Vec3 up = cam.up();

    auto cancelled = [&]() { return should_stop && should_stop(); };

    std::vector<Vec3> delta((size_t)width_ * height_, Vec3(0, 0, 0));
    std::atomic<bool> cancel{false};

    auto render_row = [&](int y) {
        if (cancel.load(std::memory_order_relaxed)) return;
        for (int x = 0; x < width_; x++) {
            if (cancelled()) { // cancel between pixels
                cancel.store(true, std::memory_order_relaxed);
                return;
            }
            size_t p = (size_t)y * width_ + x;
            Vec3 sum(0, 0, 0);
            for (int s = 0; s < spp; s++) {
                if (cancelled()) { // cancel between samples
                    cancel.store(true, std::memory_order_relaxed);
                    return;
                }
                // dspbr rng_init(...) then calcuateViewRay with a box-filter
                // offset. The seed is a fresh per-frame stream plus the sample
                // index, so restarts never replay the same random numbers.
                int si = sample_base + s;
                uint32_t seed = frame_seed + (uint32_t)si * 0x9E3779B9u;
                Rng rng(x, y, (int)seed);
                float r0 = rng.next();
                float r1 = rng.next();
                float nx = (2.0f * x + 1.0f) / width_ - 1.0f + r0 / width_;
                float ny = (2.0f * y + 1.0f) / height_ - 1.0f + r1 / height_;
                float sx = nx * tanHalf * aspect;
                float sy = ny * tanHalf;
                Vec3 dir = normalize(fwd + rgt * sx + up * sy);
                Ray ray{cam.position, dir, 0.0f, TFAR_MAX};
                Vec3 v = trace_path(scene_, impl_->tris, impl_->nodes, ray, rng, settings,
                                    pixel_world, env, (long)y * 100000 + x, cancel_flag,
                                    &impl_->emissive, impl_->emissive_total, &impl_->tri_emis);
                if (!is_finite(v)) v = Vec3(0, 0, 0); // guard against NaN/Inf contamination
                sum += v;
            }
            delta[p] += sum;
        }
    };

    auto render_rows_parallel = [&](int y0, int y1) {
        int nthreads = online_cpu_count();
        int count = y1 - y0;
        if (nthreads <= 1 || count <= 1) {
            for (int y = y0; y < y1; y++) {
                if (cancel.load(std::memory_order_relaxed) || cancelled()) {
                    cancel.store(true, std::memory_order_relaxed);
                    return;
                }
                render_row(y);
            }
            return;
        }
        std::atomic<int> next_y{y0};
        auto worker = [&]() {
            for (;;) {
                int y = next_y.fetch_add(1, std::memory_order_relaxed);
                if (y >= y1 || cancel.load(std::memory_order_relaxed)) break;
                if (cancelled()) {
                    cancel.store(true, std::memory_order_relaxed);
                    break;
                }
                render_row(y);
            }
        };
        std::vector<std::thread> pool;
        pool.reserve((size_t)nthreads);
        for (int t = 0; t < nthreads; t++) pool.emplace_back(worker);
        for (auto &th : pool) th.join();
    };

    int nthreads = online_cpu_count();
    // Batch size: at least `nthreads` rows so each batch actually runs in
    // parallel (a batch of 1 row would take the serial path).
    int chunk = std::max(nthreads, std::max(1, height_ / 8));
    if (chunk > height_) chunk = height_;

    float exposure = std::exp2(settings.exposure_ev);
    auto publish_rows = [&](int y0, int y1) {
        // Commit this chunk into the accumulation and refresh the display for
        // the affected rows (so a UI can show partial progress).
        std::lock_guard<std::mutex> lk(display_mutex_);
        for (int y = y0; y < y1; y++) {
            for (int x = 0; x < width_; x++) {
                size_t p = (size_t)y * width_ + x;
                accum_[p] += delta[p];
                delta[p] = Vec3(0, 0, 0);
                Vec3 c = apply_tonemap(accum_[p] / (float)denom, exposure,
                                       settings.tonemap_mode);
                if (settings.gamma) c = encode_gamma(c);
                display_[p] = c;
            }
        }
    };

    for (int y0 = 0; y0 < height_; y0 += chunk) {
        if (cancelled()) {
            cancel.store(true, std::memory_order_relaxed);
            return false;
        }
        int y1 = std::min(y0 + chunk, height_);
        render_rows_parallel(y0, y1);
        if (cancel.load(std::memory_order_relaxed)) return false;
        publish_rows(y0, y1);
        if (on_progress) on_progress();
    }

    accumulated_ += spp;
    return true;
}

} // namespace tr

