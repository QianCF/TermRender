#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tr {

constexpr float PI = 3.14159265358979323846f;
constexpr float INV_PI = 0.31830988618379067154f;
constexpr float EPS = 1e-6f;
constexpr float INF = std::numeric_limits<float>::infinity();

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sqr(float x) { return x * x; }
inline float radians(float d) { return d * (PI / 180.0f); }

struct Vec2 {
    float x = 0, y = 0;
    Vec2() {}
    Vec2(float a, float b) : x(a), y(b) {}
    Vec2 operator+(const Vec2 &o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2 &o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    Vec2 operator*(const Vec2 &o) const { return {x * o.x, y * o.y}; }
    Vec2 &operator+=(const Vec2 &o) { x += o.x; y += o.y; return *this; }
};
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() {}
    explicit Vec3(float v) : x(v), y(v), z(v) {}
    Vec3(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3 operator*(const Vec3 &o) const { return {x * o.x, y * o.y, z * o.z}; }
    Vec3 operator/(const Vec3 &o) const { return {x / o.x, y / o.y, z / o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 &operator+=(const Vec3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3 &operator-=(const Vec3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3 &operator*=(const Vec3 &o) { x *= o.x; y *= o.y; z *= o.z; return *this; }
    Vec3 &operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3 &operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    float operator[](int i) const { return (&x)[i]; }
    float &operator[](int i) { return (&x)[i]; }
};

inline Vec3 operator*(float s, const Vec3 &v) { return v * s; }
inline float dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3 &a, const Vec3 &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3 &v) { return std::sqrt(dot(v, v)); }
inline float length_sq(const Vec3 &v) { return dot(v, v); }
inline Vec3 normalize(const Vec3 &v) {
    float l = length(v);
    return l > 0 ? v / l : Vec3(0, 0, 0);
}
inline Vec3 minv(const Vec3 &a, const Vec3 &b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 maxv(const Vec3 &a, const Vec3 &b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline float max_comp(const Vec3 &v) { return std::max(v.x, std::max(v.y, v.z)); }
inline float min_comp(const Vec3 &v) { return std::min(v.x, std::min(v.y, v.z)); }
inline float luminance(const Vec3 &v) { return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z; }
inline Vec3 lerp(const Vec3 &a, const Vec3 &b, float t) { return a + (b - a) * t; }
inline Vec3 expv(const Vec3 &v) { return {std::exp(v.x), std::exp(v.y), std::exp(v.z)}; }
inline bool is_black(const Vec3 &v) { return v.x <= 0 && v.y <= 0 && v.z <= 0; }
inline bool is_finite(const Vec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Build an orthonormal basis around n (n must be normalized).
inline void basis(const Vec3 &n, Vec3 &t, Vec3 &b) {
    float sign = n.z >= 0 ? 1.0f : -1.0f;
    float a = -1.0f / (sign + n.z);
    float bb = n.x * n.y * a;
    t = Vec3(1.0f + sign * n.x * n.x * a, sign * bb, -sign * n.x);
    b = Vec3(bb, sign + n.y * n.y * a, -n.y);
}

// Matrix, column-major (matches glTF / OpenGL convention).
struct Vec4 {
    float x = 0, y = 0, z = 0, w = 1;
    Vec4() {}
    explicit Vec4(float s) : x(s), y(s), z(s), w(s) {}
    Vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    Vec4(const Vec3 &v, float d) : x(v.x), y(v.y), z(v.z), w(d) {}
    Vec4 operator+(const Vec4 &o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
    Vec3 xyz() const { return {x, y, z}; }
    Vec3 rgb() const { return {x, y, z}; }
};

struct Mat4 {
    float m[16];
    Mat4() { identity(); }
    void identity() {
        for (int i = 0; i < 16; i++) m[i] = 0;
        m[0] = m[5] = m[10] = m[15] = 1;
    }
    static Mat4 from_cols(const Vec3 &cx, const Vec3 &cy, const Vec3 &cz) {
        Mat4 r;
        r.m[0] = cx.x; r.m[1] = cx.y; r.m[2] = cx.z; r.m[3] = 0;
        r.m[4] = cy.x; r.m[5] = cy.y; r.m[6] = cy.z; r.m[7] = 0;
        r.m[8] = cz.x; r.m[9] = cz.y; r.m[10] = cz.z; r.m[11] = 0;
        r.m[12] = r.m[13] = r.m[14] = 0; r.m[15] = 1;
        return r;
    }
};

inline Mat4 operator*(const Mat4 &a, const Mat4 &b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}

inline Vec3 transform_point(const Mat4 &M, const Vec3 &p) {
    float x = M.m[0] * p.x + M.m[4] * p.y + M.m[8] * p.z + M.m[12];
    float y = M.m[1] * p.x + M.m[5] * p.y + M.m[9] * p.z + M.m[13];
    float z = M.m[2] * p.x + M.m[6] * p.y + M.m[10] * p.z + M.m[14];
    float w = M.m[3] * p.x + M.m[7] * p.y + M.m[11] * p.z + M.m[15];
    if (w != 0 && w != 1) { x /= w; y /= w; z /= w; }
    return {x, y, z};
}

inline Vec3 transform_dir(const Mat4 &M, const Vec3 &p) {
    return {M.m[0] * p.x + M.m[4] * p.y + M.m[8] * p.z,
            M.m[1] * p.x + M.m[5] * p.y + M.m[9] * p.z,
            M.m[2] * p.x + M.m[6] * p.y + M.m[10] * p.z};
}

inline Mat4 mat4_transpose(const Mat4 &a) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) r.m[c * 4 + row] = a.m[row * 4 + c];
    return r;
}

inline Mat4 mat4_inverse(const Mat4 &a) {
    const float *m = a.m;
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    Mat4 r;
    if (det == 0) return r;
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}

inline Mat4 mat4_translation(const Vec3 &t) {
    Mat4 r;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}
inline Mat4 mat4_scale(const Vec3 &s) {
    Mat4 r;
    r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
    return r;
}
inline Mat4 mat4_from_quat(float x, float y, float z, float w) {
    Mat4 r;
    float xx = x*x, yy = y*y, zz = z*z, xy = x*y, xz = x*z, yz = y*z, wx = w*x, wy = w*y, wz = w*z;
    r.m[0] = 1-2*(yy+zz); r.m[1] = 2*(xy+wz);   r.m[2] = 2*(xz-wy);   r.m[3] = 0;
    r.m[4] = 2*(xy-wz);   r.m[5] = 1-2*(xx+zz); r.m[6] = 2*(yz+wx);   r.m[7] = 0;
    r.m[8] = 2*(xz+wy);   r.m[9] = 2*(yz-wx);   r.m[10] = 1-2*(xx+yy); r.m[11] = 0;
    r.m[12] = r.m[13] = r.m[14] = 0; r.m[15] = 1;
    return r;
}

struct Ray {
    Vec3 o, d;
    float tmin = 0.0f;
    float tmax = INF;
    Vec3 at(float t) const { return o + d * t; }
};

// George Marsaglia multiply-with-carry PRNG. Matches dspbr-pt shader/rng.glsl
// (rng_init / george_marsaglia_rng / rng_float) exactly.
struct Rng {
    uint32_t x = 0, y = 0;

    Rng() = default;
    explicit Rng(uint32_t seed) { init(0, 0, (int)seed); }
    Rng(int px, int py, int seed) { init(px, py, seed); }

    // Well-defined float->uint32 wrap (matches the intended GLSL modulo). A
    // plain cast is UB when the value exceeds UINT32_MAX (happens for large
    // seeds), which on some platforms yields 0 and then (0,0) makes the MWC
    // state degenerate and emit a constant forever.
    static uint32_t f2u(double v) {
        double w = std::fmod(v, 4294967296.0);
        if (w < 0.0) w += 4294967296.0;
        return (uint32_t)w;
    }
    void init(int px, int py, int seed) {
        // gl_FragCoord = pixel center; offset = vec2(seed*17, 0)
        double fx = (double)px + 0.5;
        double fy = (double)py + 0.5;
        double off = (double)seed * 17.0;
        uint32_t a = f2u(397.6432 * (fx + off));
        uint32_t b = f2u(397.6432 * fy);
        uint32_t c = f2u(32.9875 * (fy + off));
        uint32_t d = f2u(32.9875 * fx);
        x = a ^ c;
        y = b ^ d;
        if (x == 0u && y == 0u) { // never allow a degenerate MWC state
            x = 0x9E3779B9u ^ (uint32_t)seed;
            y = 0x85EBCA6Bu + (uint32_t)px * 2246822519u + (uint32_t)py;
            if (x == 0u && y == 0u) x = 1u;
        }
    }
    uint32_t next_u32() {
        x = 36969u * (x & 65535u) + (x >> 16u);
        y = 18000u * (y & 65535u) + (y >> 16u);
        return (x << 16u) + y;
    }
    float next() { return (float)next_u32() / 4294967295.0f; }
    Vec2 next2() { return {next(), next()}; }
};

// Cosine-weighted hemisphere sample around n.
inline Vec3 sample_cosine(const Vec3 &n, const Vec2 &u) {
    float r = std::sqrt(u.x);
    float phi = 2.0f * PI * u.y;
    float x = r * std::cos(phi);
    float y = r * std::sin(phi);
    float z = std::sqrt(std::max(0.0f, 1.0f - u.x));
    Vec3 t, b;
    basis(n, t, b);
    return normalize(t * x + b * y + n * z);
}

inline float pdf_cosine(float cos_theta) { return cos_theta * INV_PI; }

inline Vec3 fresnel_schlick(const Vec3 &f0, float cos_theta) {
    float f = std::pow(clampf(1.0f - cos_theta, 0.0f, 1.0f), 5.0f);
    return f0 + (Vec3(1, 1, 1) - f0) * f;
}

// GGX/Trowbridge-Reitz normal distribution.
inline float distribution_ggx(float NdotH, float a) {
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d);
}

// Smith geometry term (height-correlated).
inline float geometry_smith(float NdotV, float NdotL, float a) {
    float a2 = a * a;
    float gv = NdotL * std::sqrt(NdotV * NdotV * (1 - a2) + a2);
    float gl = NdotV * std::sqrt(NdotL * NdotL * (1 - a2) + a2);
    return 0.5f / std::max(gv + gl, EPS);
}

// Sample GGX half-vector around n.
inline Vec3 sample_ggx(const Vec3 &n, float a, const Vec2 &u) {
    float phi = 2.0f * PI * u.x;
    float cos_theta = std::sqrt((1.0f - u.y) / (1.0f + (a * a - 1.0f) * u.y));
    float sin_theta = std::sqrt(std::max(0.0f, 1.0f - cos_theta * cos_theta));
    Vec3 h_local(sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta);
    Vec3 t, b;
    basis(n, t, b);
    return normalize(t * h_local.x + b * h_local.y + n * h_local.z);
}

inline float pdf_ggx(const Vec3 &n, const Vec3 &h, float a) {
    float NdotH = dot(n, h);
    if (NdotH <= 0) return 0;
    return distribution_ggx(NdotH, a) * NdotH;
}

inline Vec3 srgb_to_linear(const Vec3 &c) {
    auto f = [](float x) {
        return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    };
    return {f(c.x), f(c.y), f(c.z)};
}

} // namespace tr
