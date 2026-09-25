#pragma once
#include "math3d.h"
#include <vector>
#include <string>
#include <cstdint>

namespace tr {

// A decoded texture image (8-bit RGBA, row-major, top-left origin).
struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> pixels; // RGBA, 4 bytes/pixel
    std::vector<std::vector<uint8_t>> mips; // levels 1..N (level 0 is `pixels`)
    bool valid() const { return width > 0 && height > 0 && !pixels.empty(); }
};

enum class WrapMode { Repeat, Clamp, Mirror };
enum class ColorSpace { Linear, SRGB };

// A sampler view onto an image, with UV transform (KHR_texture_transform).
struct Texture {
    int image = -1;
    WrapMode wrap_s = WrapMode::Repeat;
    WrapMode wrap_t = WrapMode::Repeat;
    int texcoord = 0;
    Vec2 uv_offset{0, 0};
    Vec2 uv_scale{1, 1};
    float uv_rotation = 0;
    bool has_transform = false;
    ColorSpace colorspace = ColorSpace::Linear;
};

enum class AlphaMode { Opaque, Mask, Blend };

struct Material {
    std::string name;
    // ---- core (glTF 2.0 metallic-roughness) -----------------------------
    Vec3 base_color{1, 1, 1};
    float base_alpha = 1.0f;
    float metallic = 0.0f;
    float roughness = 1.0f;
    Vec3 emissive{0, 0, 0};
    float emissive_strength = 1.0f;

    int base_color_tex = -1;
    int metallic_roughness_tex = -1;
    int normal_tex = -1;
    float normal_scale = 1.0f;
    int occlusion_tex = -1;
    float occlusion_strength = 1.0f;
    int emissive_tex = -1;

    AlphaMode alpha_mode = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false;
    bool unlit = false;

    // ---- KHR_materials_ior ----------------------------------------------
    bool has_ior = false;
    float ior = 1.5f;

    // ---- KHR_materials_specular -----------------------------------------
    bool has_specular = false;
    float specular_factor = 1.0f;
    Vec3 specular_color{1, 1, 1};
    int specular_tex = -1;       // A channel
    int specular_color_tex = -1; // sRGB

    // ---- KHR_materials_sheen --------------------------------------------
    bool has_sheen = false;
    Vec3 sheen_color{0, 0, 0};
    float sheen_roughness = 0.0f;
    int sheen_color_tex = -1;    // sRGB
    int sheen_roughness_tex = -1; // A channel

    // ---- KHR_materials_clearcoat ----------------------------------------
    bool has_clearcoat = false;
    float clearcoat = 0.0f;
    float clearcoat_roughness = 0.0f;
    int clearcoat_tex = -1;        // R channel
    int clearcoat_roughness_tex = -1; // G channel
    int clearcoat_normal_tex = -1;

    // ---- KHR_materials_transmission -------------------------------------
    bool has_transmission = false;
    float transmission = 0.0f;
    int transmission_tex = -1; // R channel

    // ---- KHR_materials_volume -------------------------------------------
    bool has_volume = false;
    float thickness_factor = 0.0f;
    int thickness_tex = -1; // G channel
    Vec3 attenuation_color{1, 1, 1};
    float attenuation_distance = 0.0f; // 0/inf = no attenuation

    // ---- KHR_materials_iridescence --------------------------------------
    bool has_iridescence = false;
    float iridescence_factor = 0.0f;
    float iridescence_ior = 1.3f;
    float iridescence_thickness_min = 100.0f;
    float iridescence_thickness_max = 400.0f;
    int iridescence_tex = -1;         // R channel
    int iridescence_thickness_tex = -1; // G channel

    // ---- KHR_materials_anisotropy ---------------------------------------
    bool has_anisotropy = false;
    float anisotropy_strength = 0.0f;
    float anisotropy_rotation = 0.0f; // radians
    int anisotropy_tex = -1;          // B=strength, RG=direction

    // ---- KHR_materials_dispersion ---------------------------------------
    bool has_dispersion = false;
    float dispersion = 0.0f; // Abbe number; 0 = off

    // ---- KHR_materials_diffuse_transmission -----------------------------
    bool has_diffuse_transmission = false;
    float diffuse_transmission_factor = 0.0f;
    Vec3 diffuse_transmission_color{1, 1, 1};
    int diffuse_transmission_tex = -1;       // R channel
    int diffuse_transmission_color_tex = -1; // sRGB

    // ---- KHR_materials_pbrSpecularGlossiness (legacy) -------------------
    bool has_spec_gloss = false;
    Vec3 sg_diffuse{1, 1, 1};
    float sg_diffuse_alpha = 1.0f;
    Vec3 sg_specular{1, 1, 1};
    float sg_glossiness = 1.0f;
    int sg_diffuse_tex = -1;        // sRGB
    int sg_spec_gloss_tex = -1;     // RGB sRGB, A linear glossiness
};

struct Vertex {
    Vec3 pos;
    Vec3 normal;
    Vec2 uv0{0, 0}; // TEXCOORD_0
    Vec2 uv1{0, 0}; // TEXCOORD_1
    Vec4 color{1, 1, 1, 1};
    bool has_color = false;
    Vec3 tangent;
    float tangent_sign = 1.0f;
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    int material = -1;
    bool det_positive = true; // node transform preserved orientation
};

enum class LightType { Directional, Point, Spot };

struct Light {
    LightType type = LightType::Point;
    Vec3 color{1, 1, 1};
    float intensity = 1.0f;
    Vec3 position{0, 0, 0};
    Vec3 direction{0, -1, 0}; // travel direction (spot/directional)
    float range = 0.0f;        // 0 = infinite
    float inner_angle = 0.0f;
    float outer_angle = 0.7853981f;
};

struct CameraInfo {
    bool valid = false;
    Vec3 position{0, 0, 0};
    Vec3 forward{0, 0, -1};
    Vec3 up{0, 1, 0};
    float yfov = 0.7f;
    float aspect = 1.0f;
    float znear = 0.1f;
    float zfar = 1000.0f;
};

struct Scene {
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<Mesh> meshes;
    std::vector<Light> lights;
    CameraInfo camera;

    Vec3 bbox_min{0, 0, 0};
    Vec3 bbox_max{0, 0, 0};
    bool has_bbox = false;

    // Stats
    size_t triangle_count = 0;
    std::string source_name;

    void compute_bounds() {
        has_bbox = false;
        for (auto &m : meshes)
            for (auto &v : m.vertices) {
                if (!has_bbox) {
                    bbox_min = bbox_max = v.pos;
                    has_bbox = true;
                } else {
                    bbox_min = minv(bbox_min, v.pos);
                    bbox_max = maxv(bbox_max, v.pos);
                }
            }
    }
};

} // namespace tr
