#include "gltf_loader.h"
#include "image.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

namespace tr {

namespace {

// Open a UTF-8 path (handles non-ASCII names on Windows).
FILE *open_utf8(const std::string &path) {
#ifdef _WIN32
    int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (n > 0) {
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &w[0], n);
        return _wfopen(w.c_str(), L"rb");
    }
#endif
    return std::fopen(path.c_str(), "rb");
}

cgltf_result utf8_file_read(const cgltf_memory_options *, const cgltf_file_options *,
                            const char *path, cgltf_size *size, void **data) {
    FILE *f = open_utf8(path);
    if (!f) return cgltf_result_file_not_found;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz < 0) { std::fclose(f); return cgltf_result_io_error; }
    void *buf = std::malloc((size_t)sz ? (size_t)sz : 1);
    if (!buf) { std::fclose(f); return cgltf_result_out_of_memory; }
    size_t rd = std::fread(buf, 1, (size_t)sz, f);
    std::fclose(f);
    if (rd != (size_t)sz) { std::free(buf); return cgltf_result_io_error; }
    *size = (cgltf_size)sz;
    *data = buf;
    return cgltf_result_success;
}

void utf8_file_release(const cgltf_memory_options *, const cgltf_file_options *, void *data,
                       cgltf_size) {
    std::free(data);
}

std::string dirname_of(const std::string &path) {
    size_t p = path.find_last_of("/\\");
    if (p == std::string::npos) return ".";
    if (p == 0) return path.substr(0, 1);
    return path.substr(0, p);
}

std::string uri_decode(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int h = hex(s[i + 1]), l = hex(s[i + 2]);
            if (h >= 0 && l >= 0) {
                out.push_back((char)((h << 4) | l));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::vector<unsigned char> base64_decode(const char *data, size_t len) {
    static int8_t table[256];
    static bool init = false;
    if (!init) {
        for (int i = 0; i < 256; i++) table[i] = -1;
        const char *a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; i++) table[(unsigned char)a[i]] = (int8_t)i;
        init = true;
    }
    std::vector<unsigned char> out;
    out.reserve(len * 3 / 4 + 3);
    int val = 0, bits = -8;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)data[i];
        if (c == '=') break;
        int8_t d = table[c];
        if (d < 0) continue;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 0) {
            out.push_back((unsigned char)((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

bool read_floats(const cgltf_accessor *acc, std::vector<float> &out) {
    if (!acc) return false;
    size_t comps = cgltf_num_components(acc->type);
    out.resize(acc->count * comps);
    if (out.empty()) return true;
    cgltf_accessor_unpack_floats(acc, out.data(), out.size());
    return true;
}

void transform_vertices(std::vector<Vertex> &verts, const Mat4 &world, const Mat4 &normal_mat) {
    for (auto &v : verts) {
        v.pos = transform_point(world, v.pos);
        v.normal = normalize(transform_dir(normal_mat, v.normal));
        v.tangent = normalize(transform_dir(world, v.tangent));
    }
}

float det3(const Mat4 &m) {
    return m.m[0] * (m.m[5] * m.m[10] - m.m[6] * m.m[9]) -
           m.m[4] * (m.m[1] * m.m[10] - m.m[2] * m.m[9]) +
           m.m[8] * (m.m[1] * m.m[6] - m.m[2] * m.m[5]);
}

struct Loader {
    cgltf_data *data = nullptr;
    Scene &scene;
    std::string base_dir;
    std::map<const cgltf_image *, int> image_map;
    std::map<std::string, int> texture_map; // key: image idx + sampler + transform
    std::string error;

    explicit Loader(Scene &s) : scene(s) {}

    int add_image(const cgltf_image *img) {
        if (!img) return -1;
        auto it = image_map.find(img);
        if (it != image_map.end()) return it->second;

        const unsigned char *ptr = nullptr;
        size_t size = 0;
        std::vector<unsigned char> file_buf;
        std::vector<unsigned char> b64_buf;

        if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
            const cgltf_buffer_view *bv = img->buffer_view;
            ptr = (const unsigned char *)bv->buffer->data + bv->offset;
            size = bv->size;
        } else if (img->uri) {
            std::string uri = img->uri;
            if (uri.rfind("data:", 0) == 0) {
                size_t comma = uri.find(',');
                if (comma == std::string::npos) return -1;
                bool is_b64 = uri.find(";base64") != std::string::npos &&
                              uri.find(";base64") < comma;
                if (is_b64) {
                    b64_buf = base64_decode(uri.data() + comma + 1, uri.size() - comma - 1);
                    ptr = b64_buf.data();
                    size = b64_buf.size();
                } else {
                    std::string raw = uri_decode(uri.substr(comma + 1));
                    b64_buf.assign(raw.begin(), raw.end());
                    ptr = b64_buf.data();
                    size = b64_buf.size();
                }
            } else {
                std::string path = base_dir + "/" + uri_decode(uri);
                FILE *f = open_utf8(path);
                if (!f) return -1;
                std::fseek(f, 0, SEEK_END);
                long sz = std::ftell(f);
                std::fseek(f, 0, SEEK_SET);
                if (sz <= 0) { std::fclose(f); return -1; }
                file_buf.resize((size_t)sz);
                size_t rd = std::fread(file_buf.data(), 1, file_buf.size(), f);
                std::fclose(f);
                if (rd != file_buf.size()) return -1;
                ptr = file_buf.data();
                size = file_buf.size();
            }
        }
        if (!ptr || size == 0) return -1;

        Image im;
        if (!decode_image_memory(ptr, size, im)) return -1;
        int idx = (int)scene.images.size();
        scene.images.push_back(std::move(im));
        image_map[img] = idx;
        return idx;
    }

    int add_texture_view(const cgltf_texture_view &tv, ColorSpace cs) {
        if (!tv.texture || !tv.texture->image) return -1;
        int img = add_image(tv.texture->image);
        if (img < 0) return -1;

        Texture t;
        t.image = img;
        t.texcoord = tv.has_transform && tv.transform.has_texcoord ? tv.transform.texcoord : tv.texcoord;
        t.colorspace = cs;
        const cgltf_sampler *s = tv.texture->sampler;
        if (s) {
            switch (s->wrap_s) {
            case cgltf_wrap_mode_clamp_to_edge: t.wrap_s = WrapMode::Clamp; break;
            case cgltf_wrap_mode_mirrored_repeat: t.wrap_s = WrapMode::Mirror; break;
            default: t.wrap_s = WrapMode::Repeat; break;
            }
            switch (s->wrap_t) {
            case cgltf_wrap_mode_clamp_to_edge: t.wrap_t = WrapMode::Clamp; break;
            case cgltf_wrap_mode_mirrored_repeat: t.wrap_t = WrapMode::Mirror; break;
            default: t.wrap_t = WrapMode::Repeat; break;
            }
        }
        if (tv.has_transform) {
            t.has_transform = true;
            t.uv_offset = {tv.transform.offset[0], tv.transform.offset[1]};
            t.uv_scale = {tv.transform.scale[0], tv.transform.scale[1]};
            t.uv_rotation = tv.transform.rotation;
        }
        int idx = (int)scene.textures.size();
        scene.textures.push_back(t);
        return idx;
    }

    int add_material(const cgltf_material *mat) {
        Material m;
        if (mat) {
            if (mat->name) m.name = mat->name;

            if (mat->has_pbr_specular_glossiness) {
                // Legacy KHR_materials_pbrSpecularGlossiness.
                const cgltf_pbr_specular_glossiness &sg = mat->pbr_specular_glossiness;
                m.has_spec_gloss = true;
                m.sg_diffuse = {sg.diffuse_factor[0], sg.diffuse_factor[1], sg.diffuse_factor[2]};
                m.sg_diffuse_alpha = sg.diffuse_factor[3];
                m.sg_specular = {sg.specular_factor[0], sg.specular_factor[1],
                                 sg.specular_factor[2]};
                m.sg_glossiness = sg.glossiness_factor;
                m.sg_diffuse_tex = add_texture_view(sg.diffuse_texture, ColorSpace::SRGB);
                m.sg_spec_gloss_tex = add_texture_view(sg.specular_glossiness_texture,
                                                       ColorSpace::SRGB);
                m.base_alpha = m.sg_diffuse_alpha;
            } else {
                const cgltf_pbr_metallic_roughness &pbr = mat->pbr_metallic_roughness;
                m.base_color = {pbr.base_color_factor[0], pbr.base_color_factor[1],
                                pbr.base_color_factor[2]};
                m.base_alpha = pbr.base_color_factor[3];
                m.metallic = pbr.metallic_factor;
                m.roughness = pbr.roughness_factor;
                m.base_color_tex = add_texture_view(pbr.base_color_texture, ColorSpace::SRGB);
                m.metallic_roughness_tex =
                    add_texture_view(pbr.metallic_roughness_texture, ColorSpace::Linear);
            }

            m.normal_tex = add_texture_view(mat->normal_texture, ColorSpace::Linear);
            m.normal_scale = mat->normal_texture.scale != 0 ? mat->normal_texture.scale : 1.0f;
            m.occlusion_tex = add_texture_view(mat->occlusion_texture, ColorSpace::Linear);
            m.occlusion_strength =
                mat->occlusion_texture.scale != 0 ? mat->occlusion_texture.scale : 1.0f;
            m.emissive_tex = add_texture_view(mat->emissive_texture, ColorSpace::SRGB);
            m.emissive = {mat->emissive_factor[0], mat->emissive_factor[1],
                          mat->emissive_factor[2]};
            if (mat->has_emissive_strength)
                m.emissive_strength = mat->emissive_strength.emissive_strength;
            switch (mat->alpha_mode) {
            case cgltf_alpha_mode_mask: m.alpha_mode = AlphaMode::Mask; break;
            case cgltf_alpha_mode_blend: m.alpha_mode = AlphaMode::Blend; break;
            default: m.alpha_mode = AlphaMode::Opaque; break;
            }
            m.alpha_cutoff = mat->alpha_cutoff;
            m.double_sided = mat->double_sided;
            m.unlit = mat->unlit;

            if (mat->has_ior) {
                m.has_ior = true;
                m.ior = mat->ior.ior;
            }
            if (mat->has_specular) {
                m.has_specular = true;
                m.specular_factor = mat->specular.specular_factor;
                m.specular_color = {mat->specular.specular_color_factor[0],
                                    mat->specular.specular_color_factor[1],
                                    mat->specular.specular_color_factor[2]};
                m.specular_tex = add_texture_view(mat->specular.specular_texture,
                                                  ColorSpace::Linear);
                m.specular_color_tex = add_texture_view(mat->specular.specular_color_texture,
                                                        ColorSpace::SRGB);
            }
            if (mat->has_sheen) {
                m.has_sheen = true;
                m.sheen_color = {mat->sheen.sheen_color_factor[0],
                                 mat->sheen.sheen_color_factor[1],
                                 mat->sheen.sheen_color_factor[2]};
                m.sheen_roughness = mat->sheen.sheen_roughness_factor;
                m.sheen_color_tex =
                    add_texture_view(mat->sheen.sheen_color_texture, ColorSpace::SRGB);
                m.sheen_roughness_tex =
                    add_texture_view(mat->sheen.sheen_roughness_texture, ColorSpace::Linear);
            }
            if (mat->has_clearcoat) {
                m.has_clearcoat = true;
                m.clearcoat = mat->clearcoat.clearcoat_factor;
                m.clearcoat_roughness = mat->clearcoat.clearcoat_roughness_factor;
                m.clearcoat_tex =
                    add_texture_view(mat->clearcoat.clearcoat_texture, ColorSpace::Linear);
                m.clearcoat_roughness_tex =
                    add_texture_view(mat->clearcoat.clearcoat_roughness_texture,
                                     ColorSpace::Linear);
                m.clearcoat_normal_tex =
                    add_texture_view(mat->clearcoat.clearcoat_normal_texture, ColorSpace::Linear);
            }
            if (mat->has_transmission) {
                m.has_transmission = true;
                m.transmission = mat->transmission.transmission_factor;
                m.transmission_tex =
                    add_texture_view(mat->transmission.transmission_texture, ColorSpace::Linear);
            }
            if (mat->has_volume) {
                m.has_volume = true;
                m.thickness_factor = mat->volume.thickness_factor;
                m.thickness_tex =
                    add_texture_view(mat->volume.thickness_texture, ColorSpace::Linear);
                m.attenuation_color = {mat->volume.attenuation_color[0],
                                        mat->volume.attenuation_color[1],
                                        mat->volume.attenuation_color[2]};
                m.attenuation_distance = mat->volume.attenuation_distance;
            }
            if (mat->has_iridescence) {
                m.has_iridescence = true;
                m.iridescence_factor = mat->iridescence.iridescence_factor;
                m.iridescence_ior = mat->iridescence.iridescence_ior;
                m.iridescence_thickness_min = mat->iridescence.iridescence_thickness_min;
                m.iridescence_thickness_max = mat->iridescence.iridescence_thickness_max;
                m.iridescence_tex =
                    add_texture_view(mat->iridescence.iridescence_texture, ColorSpace::Linear);
                m.iridescence_thickness_tex = add_texture_view(
                    mat->iridescence.iridescence_thickness_texture, ColorSpace::Linear);
            }
            if (mat->has_anisotropy) {
                m.has_anisotropy = true;
                m.anisotropy_strength = mat->anisotropy.anisotropy_strength;
                m.anisotropy_rotation = mat->anisotropy.anisotropy_rotation;
                m.anisotropy_tex =
                    add_texture_view(mat->anisotropy.anisotropy_texture, ColorSpace::Linear);
            }
            if (mat->has_dispersion) {
                m.has_dispersion = true;
                m.dispersion = mat->dispersion.dispersion;
            }
            if (mat->has_diffuse_transmission) {
                m.has_diffuse_transmission = true;
                m.diffuse_transmission_factor = mat->diffuse_transmission.diffuse_transmission_factor;
                m.diffuse_transmission_color = {
                    mat->diffuse_transmission.diffuse_transmission_color_factor[0],
                    mat->diffuse_transmission.diffuse_transmission_color_factor[1],
                    mat->diffuse_transmission.diffuse_transmission_color_factor[2]};
                m.diffuse_transmission_tex = add_texture_view(
                    mat->diffuse_transmission.diffuse_transmission_texture, ColorSpace::Linear);
                m.diffuse_transmission_color_tex = add_texture_view(
                    mat->diffuse_transmission.diffuse_transmission_color_texture,
                    ColorSpace::SRGB);
            }
        }
        int idx = (int)scene.materials.size();
        scene.materials.push_back(m);
        return idx;
    }

    void add_primitive(const cgltf_primitive *prim, const Mat4 &world, const Mat4 &nmat,
                       bool det_positive) {
        if (prim->type != cgltf_primitive_type_triangles) return;

        const cgltf_accessor *pos = nullptr, *nor = nullptr;
        const cgltf_accessor *uv0 = nullptr, *uv1 = nullptr, *tan = nullptr, *col = nullptr;
        for (cgltf_size i = 0; i < prim->attributes_count; i++) {
            const cgltf_attribute &a = prim->attributes[i];
            if (a.type == cgltf_attribute_type_position && a.index == 0) pos = a.data;
            else if (a.type == cgltf_attribute_type_normal && a.index == 0) nor = a.data;
            else if (a.type == cgltf_attribute_type_texcoord && a.index == 0) uv0 = a.data;
            else if (a.type == cgltf_attribute_type_texcoord && a.index == 1) uv1 = a.data;
            else if (a.type == cgltf_attribute_type_tangent && a.index == 0) tan = a.data;
            else if (a.type == cgltf_attribute_type_color && a.index == 0) col = a.data;
        }
        if (!pos) return;

        std::vector<float> pf, nf, u0f, u1f, tf, cf;
        read_floats(pos, pf);
        if (nor) read_floats(nor, nf);
        if (uv0) read_floats(uv0, u0f);
        if (uv1) read_floats(uv1, u1f);
        if (tan) read_floats(tan, tf);
        if (col) read_floats(col, cf);

        size_t count = pos->count;
        std::vector<Vertex> verts(count);
        size_t ccomps = cf.size() / std::max<size_t>(count, 1);
        for (size_t i = 0; i < count; i++) {
            Vertex &v = verts[i];
            v.pos = {pf[i * 3], pf[i * 3 + 1], pf[i * 3 + 2]};
            if (!nf.empty()) v.normal = {nf[i * 3], nf[i * 3 + 1], nf[i * 3 + 2]};
            if (!u0f.empty()) v.uv0 = {u0f[i * 2], u0f[i * 2 + 1]};
            if (!u1f.empty()) v.uv1 = {u1f[i * 2], u1f[i * 2 + 1]};
            if (ccomps >= 3) {
                v.color = {cf[i * ccomps], cf[i * ccomps + 1], cf[i * ccomps + 2],
                           ccomps >= 4 ? cf[i * ccomps + 3] : 1.0f};
                v.has_color = true;
            }
            if (!tf.empty()) {
                v.tangent = {tf[i * 4], tf[i * 4 + 1], tf[i * 4 + 2]};
                v.tangent_sign = tf[i * 4 + 3];
            }
        }

        std::vector<uint32_t> idx;
        if (prim->indices) {
            size_t ic = prim->indices->count;
            idx.resize(ic);
            for (size_t i = 0; i < ic; i++)
                idx[i] = (uint32_t)cgltf_accessor_read_index(prim->indices, i);
        } else {
            idx.resize(count);
            for (size_t i = 0; i < count; i++) idx[i] = (uint32_t)i;
        }

        transform_vertices(verts, world, nmat);

        Mesh mesh;
        mesh.vertices = std::move(verts);
        mesh.indices = std::move(idx);
        mesh.material = add_material(prim->material);
        mesh.det_positive = det_positive;
        scene.triangle_count += mesh.indices.size() / 3;
        scene.meshes.push_back(std::move(mesh));
    }

    void traverse(const cgltf_node *node) {
        float wm[16];
        cgltf_node_transform_world(node, wm);
        Mat4 world;
        std::memcpy(world.m, wm, sizeof(float) * 16);
        Mat4 nmat = mat4_transpose(mat4_inverse(world));
        bool dpos = det3(world) >= 0.0f;

        if (node->mesh) {
            /* A skinned mesh's node transform is ignored per the glTF spec; its
             * vertices are already in the skin's bind space, so using them
             * directly renders the bind pose (no animation is evaluated). */
            Mat4 prim_world = world;
            Mat4 prim_nmat = nmat;
            bool prim_dpos = dpos;
            if (node->skin) {
                prim_world.identity();
                prim_nmat.identity();
                prim_dpos = true;
            }
            for (cgltf_size i = 0; i < node->mesh->primitives_count; i++)
                add_primitive(&node->mesh->primitives[i], prim_world, prim_nmat, prim_dpos);
        }

        if (node->light) {
            Light l;
            const cgltf_light *gl = node->light;
            l.color = {gl->color[0], gl->color[1], gl->color[2]};
            l.intensity = gl->intensity;
            l.position = transform_point(world, {0, 0, 0});
            l.direction = normalize(transform_dir(world, {0, 0, -1}));
            l.range = gl->range;
            l.inner_angle = gl->spot_inner_cone_angle;
            l.outer_angle = gl->spot_outer_cone_angle;
            switch (gl->type) {
            case cgltf_light_type_directional: l.type = LightType::Directional; break;
            case cgltf_light_type_spot: l.type = LightType::Spot; break;
            default: l.type = LightType::Point; break;
            }
            scene.lights.push_back(l);
        }

        if (node->camera && node->camera->type == cgltf_camera_type_perspective &&
            !scene.camera.valid) {
            const cgltf_camera_perspective &p = node->camera->data.perspective;
            scene.camera.valid = true;
            scene.camera.position = transform_point(world, {0, 0, 0});
            scene.camera.forward = normalize(transform_dir(world, {0, 0, -1}));
            scene.camera.up = normalize(transform_dir(world, {0, 1, 0}));
            scene.camera.yfov = p.yfov;
            scene.camera.znear = p.znear;
            scene.camera.zfar = p.has_zfar ? p.zfar : 1000.0f;
            if (p.has_aspect_ratio) scene.camera.aspect = p.aspect_ratio;
        }

        for (cgltf_size i = 0; i < node->children_count; i++)
            traverse(node->children[i]);
    }
};

} // namespace

bool load_gltf(const std::string &path, Scene &scene, std::string &error) {
    cgltf_options options;
    std::memset(&options, 0, sizeof(options));
    options.file.read = utf8_file_read;
    options.file.release = utf8_file_release;
    cgltf_data *data = nullptr;

    cgltf_result r = cgltf_parse_file(&options, path.c_str(), &data);
    if (r != cgltf_result_success) {
        error = "failed to parse glTF/GLB (code " + std::to_string((int)r) + ")";
        return false;
    }

    r = cgltf_load_buffers(&options, data, path.c_str());
    if (r != cgltf_result_success) {
        cgltf_free(data);
        error = "failed to load buffers (code " + std::to_string((int)r) + ")";
        return false;
    }

    Loader loader(scene);
    loader.data = data;
    loader.base_dir = dirname_of(path);
    scene.source_name = path;

    const cgltf_scene *sc = data->scene;
    if (!sc && data->scenes_count > 0) sc = &data->scenes[0];
    if (sc) {
        for (cgltf_size i = 0; i < sc->nodes_count; i++)
            loader.traverse(sc->nodes[i]);
    } else {
        for (cgltf_size i = 0; i < data->nodes_count; i++)
            if (!data->nodes[i].parent) loader.traverse(&data->nodes[i]);
    }

    cgltf_free(data);

    if (scene.meshes.empty() && scene.lights.empty()) {
        error = "no renderable geometry or lights found";
        return false;
    }

    scene.compute_bounds();
    if (scene.camera.valid && scene.has_bbox) {
        float dx = scene.bbox_max.x - scene.bbox_min.x;
        float dy = scene.bbox_max.y - scene.bbox_min.y;
        scene.camera.aspect = dx > 0 && dy > 0 ? 1.0f : scene.camera.aspect;
    }
    return true;
}

} // namespace tr
