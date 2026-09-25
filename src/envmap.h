#pragma once
#include "math3d.h"
#include <string>
#include <vector>

namespace tr {

// Equirectangular HDR environment map with luminance-based importance
// sampling (marginal/conditional CDFs), mirroring dspbr-pt's IBL.
struct EnvMap {
    int w = 0, h = 0;
    std::vector<float> rgb;          // linear HDR, 3 floats per texel
    std::vector<float> row_cdf;      // per-row conditional CDFs (w * h)
    std::vector<float> row_pdf;      // per-row conditional pdfs (w * h)
    std::vector<float> marginal_cdf; // h entries
    std::vector<float> marginal_pdf; // h entries
    Vec3 max_rgb{0, 0, 0};           // per-channel peak radiance (firefly clamp)

    bool valid() const { return w > 0 && h > 0 && !rgb.empty(); }
};

// Load a .hdr (radiance RGBE) file and build the sampling tables.
bool load_env_hdr(const std::string &path, EnvMap &out, std::string &err);

// Bilinear equirect lookup; returns linear RGB.
Vec3 env_eval(const EnvMap &env, const Vec3 &dir);

// Importance-sample a direction from the environment luminance.
// Returns the solid-angle pdf.
Vec3 env_sample(const EnvMap &env, float r0, float r1, float &pdf);

// Solid-angle pdf of `dir` under the importance distribution.
float env_pdf(const EnvMap &env, const Vec3 &dir);

} // namespace tr
