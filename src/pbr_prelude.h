#pragma once
// CPU translation prelude for the slang-pbr material kernel (dspbr-pt).
// Provides GLSL-like math shims so the generated kernel compiles as C++.
#include "math3d.h"
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace tr {
namespace pbr {

using math3d_pi_dummy = void;

inline float clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline Vec3 clamp(const Vec3 &v, const Vec3 &lo, const Vec3 &hi) { return maxv(minv(v, hi), lo); }
inline Vec3 clamp(const Vec3 &v, float lo, float hi) { return maxv(minv(v, Vec3(hi)), Vec3(lo)); }
inline float saturate_(float v) { return clamp(v, 0.0f, 1.0f); }

inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 mix(const Vec3 &a, const Vec3 &b, float t) { return lerp(a, b, t); }
inline Vec3 mix(const Vec3 &a, const Vec3 &b, const Vec3 &t) {
    return a + (b - a) * t;
}
inline Vec2 mix(const Vec2 &a, const Vec2 &b, float t) { return a + (b - a) * t; }

inline Vec3 pow(const Vec3 &a, const Vec3 &b) {
    return Vec3(std::pow(a.x, b.x), std::pow(a.y, b.y), std::pow(a.z, b.z));
}
inline Vec3 pow(const Vec3 &a, float e) {
    return Vec3(std::pow(a.x, e), std::pow(a.y, e), std::pow(a.z, e));
}
inline float powf_(float a, float b) { return std::pow(a, b); }

inline Vec3 exp(const Vec3 &v) { return expv(v); }
inline Vec3 log(const Vec3 &v) { return Vec3(std::log(v.x), std::log(v.y), std::log(v.z)); }
inline Vec3 abs(const Vec3 &v) { return Vec3(std::fabs(v.x), std::fabs(v.y), std::fabs(v.z)); }
inline Vec3 sqrt(const Vec3 &v) {
    return Vec3(std::sqrt(v.x), std::sqrt(v.y), std::sqrt(v.z));
}
inline Vec3 sign(const Vec3 &v) { return Vec3(v.x < 0 ? -1.0f : (v.x > 0 ? 1.0f : 0.0f),
                                              v.y < 0 ? -1.0f : (v.y > 0 ? 1.0f : 0.0f),
                                              v.z < 0 ? -1.0f : (v.z > 0 ? 1.0f : 0.0f)); }

inline float max_(float a, float b) { return a > b ? a : b; }
inline float min_(float a, float b) { return a < b ? a : b; }
inline float max(float a, float b) { return a > b ? a : b; }
inline float min(float a, float b) { return a < b ? a : b; }
inline float sqrt(float v) { return std::sqrt(v); }
inline float abs(float v) { return std::fabs(v); }
inline float exp(float v) { return std::exp(v); }
inline float log(float v) { return std::log(v); }
inline float floor(float v) { return std::floor(v); }
inline float sign(float v) { return v < 0 ? -1.0f : (v > 0 ? 1.0f : 0.0f); }

inline Vec3 cos(const Vec3 &v) { return Vec3(std::cos(v.x), std::cos(v.y), std::cos(v.z)); }
inline Vec3 sin(const Vec3 &v) { return Vec3(std::sin(v.x), std::sin(v.y), std::sin(v.z)); }
inline Vec3 tan(const Vec3 &v) { return Vec3(std::tan(v.x), std::tan(v.y), std::tan(v.z)); }
inline float cos(float v) { return std::cos(v); }
inline float sin(float v) { return std::sin(v); }
inline float tan(float v) { return std::tan(v); }
inline float acos(float v) { return std::acos(v); }
inline float asin(float v) { return std::asin(v); }
inline float atan(float y, float x) { return std::atan2(y, x); }
inline Vec3 floor(const Vec3 &v) {
    return Vec3(std::floor(v.x), std::floor(v.y), std::floor(v.z));
}
inline Vec2 floor(const Vec2 &v) { return {std::floor(v.x), std::floor(v.y)}; }
inline Vec3 fract(const Vec3 &v) { return v - floor(v); }
inline float fract(float v) { return v - std::floor(v); }
inline Vec3 max(const Vec3 &a, const Vec3 &b) { return maxv(a, b); }
inline Vec3 max(const Vec3 &a, float b) { return maxv(a, Vec3(b)); }
inline Vec3 max(float a, const Vec3 &b) { return maxv(Vec3(a), b); }
inline Vec3 min(const Vec3 &a, const Vec3 &b) { return minv(a, b); }
inline Vec3 min(const Vec3 &a, float b) { return minv(a, Vec3(b)); }
inline Vec3 min(float a, const Vec3 &b) { return minv(Vec3(a), b); }
inline Vec2 min(const Vec2 &a, const Vec2 &b) { return {std::min(a.x, b.x), std::min(a.y, b.y)}; }
inline Vec2 max(const Vec2 &a, const Vec2 &b) { return {std::max(a.x, b.x), std::max(a.y, b.y)}; }
inline Vec2 abs(const Vec2 &v) { return {std::fabs(v.x), std::fabs(v.y)}; }

inline Vec3 reflect(const Vec3 &i, const Vec3 &n) {
    return i - n * (2.0f * dot(n, i));
}
inline Vec3 refract(const Vec3 &i, const Vec3 &n, float eta) {
    float ni = dot(n, i);
    float k = 1.0f - eta * eta * (1.0f - ni * ni);
    if (k < 0.0f) return Vec3(0.0f);
    return i * eta - n * (eta * ni + std::sqrt(k));
}

inline float length_(const Vec3 &v) { return length(v); }
inline float luminance_(const Vec3 &c) { return luminance(c); }
inline float sum(const Vec3 &v) { return v.x + v.y + v.z; }
inline float max3(const Vec3 &v) { return max_comp(v); }

const float PI_ = 3.14159265358979323846f;
const float TWO_PI_ = 2.0f * PI_;
const float ONE_OVER_PI_ = 1.0f / PI_;
const float EPS_ = 1.0e-6f;
const float EPS_COS_ = 1.0e-4f;
const float EPS_PDF_ = 1.0e-5f;
const float MINIMUM_ROUGHNESS_ = 0.045f;

} // namespace pbr
} // namespace tr
