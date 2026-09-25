#include "envmap.h"

#include <cmath>
#include <cstdio>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#endif

namespace tr {

namespace {

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

inline Vec2 dir_to_uv(const Vec3 &dir, float &sin_theta) {
    float theta = std::acos(clampf(dir.y, -1.0f, 1.0f));
    float phi = std::atan2(dir.z, dir.x);
    if (phi < 0.0f) phi += 2.0f * (float)PI;
    sin_theta = std::sin(theta);
    return {phi / (2.0f * (float)PI), theta / (float)PI};
}

inline Vec3 uv_to_dir(const Vec2 &uv) {
    float theta = uv.y * (float)PI;
    float phi = uv.x * 2.0f * (float)PI;
    return {std::sin(theta) * std::cos(phi), std::cos(theta),
            std::sin(theta) * std::sin(phi)};
}

// binary search in cdf[lo .. lo+count-1]: first index with cdf >= r
inline int lower_bound_range(const std::vector<float> &cdf, int lo, int count, float r) {
    int n = count;
    while (n > 0) {
        int half = n / 2;
        int idx = lo + half;
        if (cdf[idx] < r) {
            lo = idx + 1;
            n -= half + 1;
        } else {
            n = half;
        }
    }
    return lo;
}

} // namespace

bool load_env_hdr(const std::string &path, EnvMap &out, std::string &err) {
    FILE *f = open_utf8(path);
    if (!f) {
        err = "cannot open file";
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        std::fclose(f);
        err = "empty file";
        return false;
    }
    std::vector<unsigned char> buf((size_t)sz);
    size_t rd = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (rd != buf.size()) {
        err = "read failed";
        return false;
    }

    // stb_image is compiled in image.cpp (no implementation here)
    extern bool decode_hdr_memory(const unsigned char *data, size_t size, EnvMap &out,
                                  std::string &err);
    return decode_hdr_memory(buf.data(), buf.size(), out, err);
}

Vec3 env_eval(const EnvMap &env, const Vec3 &dir) {
    if (!env.valid()) return Vec3(0, 0, 0);
    float sin_theta;
    Vec2 uv = dir_to_uv(dir, sin_theta);
    float fx = uv.x * env.w - 0.5f;
    float fy = uv.y * env.h - 0.5f;
    int x0 = (int)std::floor(fx), y0 = (int)std::floor(fy);
    float ax = fx - x0, ay = fy - y0;
    auto wrap = [](int i, int n) {
        i %= n;
        if (i < 0) i += n;
        return i;
    };
    int x1 = wrap(x0 + 1, env.w), y1 = std::max(0, std::min(env.h - 1, y0 + 1));
    x0 = wrap(x0, env.w);
    y0 = std::max(0, std::min(env.h - 1, y0));
    auto texel = [&](int x, int y) {
        const float *p = &env.rgb[((size_t)y * env.w + x) * 3];
        return Vec3(p[0], p[1], p[2]);
    };
    Vec3 c00 = texel(x0, y0), c10 = texel(x1, y0);
    Vec3 c01 = texel(x0, y1), c11 = texel(x1, y1);
    return lerp(lerp(c00, c10, ax), lerp(c01, c11, ax), ay);
}

Vec3 env_sample(const EnvMap &env, float r0, float r1, float &pdf) {
    pdf = 0.0f;
    if (!env.valid()) return Vec3(0, 1, 0);
    int y = lower_bound_range(env.marginal_cdf, 0, env.h, r1);
    if (y < 0) y = 0;
    if (y >= env.h) y = env.h - 1;
    // lower_bound_range returns an ABSOLUTE index into row_cdf
    int xabs = lower_bound_range(env.row_cdf, y * env.w, env.w, r0);
    if (xabs < y * env.w) xabs = y * env.w;
    if (xabs > (y + 1) * env.w - 1) xabs = (y + 1) * env.w - 1;
    int x = xabs - y * env.w; // column within the row
    float pdfY = env.marginal_pdf[y];
    float pdfX = env.row_pdf[xabs];
    Vec2 uv((x + 0.5f) / env.w, (y + 0.5f) / env.h);
    float theta = uv.y * (float)PI;
    float sin_theta = std::max(1e-4f, std::sin(theta));
    // pixel probability -> solid angle density
    pdf = (float)env.w * env.h * pdfY * pdfX / (2.0f * (float)PI * (float)PI * sin_theta);
    return uv_to_dir(uv);
}

float env_pdf(const EnvMap &env, const Vec3 &dir) {
    if (!env.valid()) return 0.0f;
    float sin_theta;
    Vec2 uv = dir_to_uv(dir, sin_theta);
    int x = std::min((int)(uv.x * env.w), env.w - 1);
    int y = std::min((int)(uv.y * env.h), env.h - 1);
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    sin_theta = std::max(1e-4f, sin_theta);
    return (float)env.w * env.h * env.marginal_pdf[y] * env.row_pdf[(size_t)y * env.w + x] /
           (2.0f * (float)PI * (float)PI * sin_theta);
}

} // namespace

// Implemented in image.cpp next to the stb_image implementation.
namespace tr {
bool decode_hdr_memory(const unsigned char *data, size_t size, EnvMap &out, std::string &err);
}
