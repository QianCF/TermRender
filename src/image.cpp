#include "image.h"
#include "envmap.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include "stb_image.h"

#include <cstdio>
#include <cmath>
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

// Build a box-filtered mip chain (levels 1..N).
void build_mipmaps(Image &im) {
    im.mips.clear();
    if (!im.valid()) return;
    int w = im.width, h = im.height;
    const std::vector<uint8_t> *src = &im.pixels;
    while (w > 1 || h > 1) {
        int nw = w > 1 ? w / 2 : 1;
        int nh = h > 1 ? h / 2 : 1;
        std::vector<uint8_t> dst((size_t)nw * nh * 4);
        for (int y = 0; y < nh; y++) {
            for (int x = 0; x < nw; x++) {
                for (int c = 0; c < 4; c++) {
                    int sum = 0, cnt = 0;
                    for (int dy = 0; dy < 2; dy++)
                        for (int dx = 0; dx < 2; dx++) {
                            int sx = x * 2 + dx, sy = y * 2 + dy;
                            if (sx >= w) sx = w - 1;
                            if (sy >= h) sy = h - 1;
                            sum += (*src)[((size_t)sy * w + sx) * 4 + c];
                            cnt++;
                        }
                    dst[((size_t)y * nw + x) * 4 + c] = (uint8_t)(sum / cnt);
                }
            }
        }
        im.mips.push_back(std::move(dst));
        src = &im.mips.back();
        w = nw;
        h = nh;
    }
}
} // namespace

bool decode_image_memory(const unsigned char *data, size_t size, Image &out) {
    int w = 0, h = 0, n = 0;
    stbi_uc *pixels = stbi_load_from_memory(data, (int)size, &w, &h, &n, 4);
    if (!pixels) return false;
    out.width = w;
    out.height = h;
    out.pixels.assign(pixels, pixels + (size_t)w * h * 4);
    stbi_image_free(pixels);
    build_mipmaps(out);
    return true;
}

bool load_image_file(const std::string &path, Image &out) {
    FILE *f = open_utf8(path);
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return false; }
    std::vector<unsigned char> buf((size_t)sz);
    size_t rd = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);
    if (rd != buf.size()) return false;
    return decode_image_memory(buf.data(), buf.size(), out);
}

// Decode a Radiance .hdr into an EnvMap and build the importance-sampling
// tables (marginal pdf/CDF over rows, conditional pdf/CDF within each row).
bool decode_hdr_memory(const unsigned char *data, size_t size, EnvMap &out, std::string &err) {
    int w = 0, h = 0, n = 0;
    float *pix = stbi_loadf_from_memory(data, (int)size, &w, &h, &n, 3);
    if (!pix) {
        err = "not a decodable HDR image";
        return false;
    }
    out.w = w;
    out.h = h;
    out.rgb.assign(pix, pix + (size_t)w * h * 3);
    stbi_image_free(pix);
    out.max_rgb = Vec3(0, 0, 0);
    for (size_t i = 0; i < out.rgb.size(); i += 3)
        out.max_rgb = maxv(out.max_rgb, Vec3(out.rgb[i], out.rgb[i + 1], out.rgb[i + 2]));

    out.row_cdf.assign((size_t)w * h, 0.0f);
    out.row_pdf.assign((size_t)w * h, 0.0f);
    out.marginal_cdf.assign((size_t)h, 0.0f);
    out.marginal_pdf.assign((size_t)h, 0.0f);

    // luminance per texel, weighted by sin(theta) (solid angle)
    std::vector<float> lum((size_t)w * h);
    for (int y = 0; y < h; y++) {
        float theta = ((y + 0.5f) / h) * (float)PI;
        float st = std::max(1e-5f, std::sin(theta));
        for (int x = 0; x < w; x++) {
            const float *p = &out.rgb[((size_t)y * w + x) * 3];
            lum[(size_t)y * w + x] =
                (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) * st;
        }
    }
    // per-row conditional distributions
    std::vector<float> row_sum((size_t)h, 0.0f);
    for (int y = 0; y < h; y++) {
        float sum = 0.0f;
        for (int x = 0; x < w; x++) {
            sum += lum[(size_t)y * w + x];
            out.row_cdf[(size_t)y * w + x] = sum;
        }
        if (sum <= 0.0f) sum = 1.0f;
        row_sum[y] = sum;
        for (int x = 0; x < w; x++) {
            out.row_cdf[(size_t)y * w + x] /= sum;
            out.row_pdf[(size_t)y * w + x] = lum[(size_t)y * w + x] / sum;
        }
        out.row_cdf[(size_t)y * w + (w - 1)] = 1.0f;
    }
    // marginal distribution over rows
    float total = 0.0f;
    for (int y = 0; y < h; y++) {
        total += row_sum[y];
        out.marginal_cdf[y] = total;
    }
    if (total <= 0.0f) total = 1.0f;
    for (int y = 0; y < h; y++) {
        out.marginal_cdf[y] /= total;
        out.marginal_pdf[y] = row_sum[y] / total;
    }
    out.marginal_cdf[h - 1] = 1.0f;
    err.clear();
    return true;
}

} // namespace tr
