#include "term_render.h"
#include <cstdio>
#include <cstring>

namespace tr {

namespace {

const char *HALF_BLOCK = "\xE2\x96\x84"; // U+2584 LOWER HALF BLOCK

struct Palette {
    unsigned char rgb[256][3];
    Palette() {
        static const unsigned char base[16][3] = {
            {0, 0, 0},       {128, 0, 0},   {0, 128, 0},   {128, 128, 0},
            {0, 0, 128},     {128, 0, 128}, {0, 128, 128}, {192, 192, 192},
            {128, 128, 128}, {255, 0, 0},   {0, 255, 0},   {255, 255, 0},
            {0, 0, 255},     {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
        std::memcpy(rgb, base, sizeof(base));
        static const int lv[6] = {0, 95, 135, 175, 215, 255};
        int idx = 16;
        for (int r = 0; r < 6; r++)
            for (int g = 0; g < 6; g++)
                for (int b = 0; b < 6; b++) {
                    rgb[idx][0] = (unsigned char)lv[r];
                    rgb[idx][1] = (unsigned char)lv[g];
                    rgb[idx][2] = (unsigned char)lv[b];
                    idx++;
                }
        for (int i = 0; i < 24; i++) {
            unsigned char v = (unsigned char)(8 + i * 10);
            rgb[232 + i][0] = rgb[232 + i][1] = rgb[232 + i][2] = v;
        }
    }
};

const Palette &palette() {
    static Palette p;
    return p;
}

} // namespace

void HalfBlockScreen::rgb8(const Vec3 &c, int &r, int &g, int &b) {
    r = (int)(clampf(c.x, 0.0f, 1.0f) * 255.0f + 0.5f);
    g = (int)(clampf(c.y, 0.0f, 1.0f) * 255.0f + 0.5f);
    b = (int)(clampf(c.z, 0.0f, 1.0f) * 255.0f + 0.5f);
}

int HalfBlockScreen::quant256(int r, int g, int b) {
    if (!lut_ready_) {
        const Palette &pal = palette();
        for (int ri = 0; ri < 32; ri++)
            for (int gi = 0; gi < 32; gi++)
                for (int bi = 0; bi < 32; bi++) {
                    int R = ri * 255 / 31, G = gi * 255 / 31, B = bi * 255 / 31;
                    int best = 0;
                    long bestd = 1L << 30;
                    for (int i = 0; i < 256; i++) {
                        int dr = R - pal.rgb[i][0];
                        int dg = G - pal.rgb[i][1];
                        int db = B - pal.rgb[i][2];
                        long d = (long)dr * dr + (long)dg * dg + (long)db * db;
                        if (d < bestd) {
                            bestd = d;
                            best = i;
                        }
                    }
                    lut_[(ri << 10) | (gi << 5) | bi] = (uint8_t)best;
                }
        lut_ready_ = true;
    }
    int ri = r >> 3, gi = g >> 3, bi = b >> 3;
    return lut_[(ri << 10) | (gi << 5) | bi];
}

std::string HalfBlockScreen::render(const std::vector<Vec3> &fb, int width, int height,
                                    int term_cols, int term_rows, const HalfBlockOptions &opt,
                                    int start_row) {
    std::string out;
    if (term_cols <= 0 || term_rows <= 0) return out;
    if (width != term_cols || height < term_rows * 2) return out;
    if (start_row < 1) start_row = 1;

    char buf[64];
    for (int row = 0; row < term_rows; row++) {
        int base = 2 * (term_rows - 1 - row); // bottom-up framebuffer row
        std::snprintf(buf, sizeof(buf), "\x1b[%d;1H", start_row + row);
        out += buf;

        int cur_fg = -1, cur_bg = -1;
        for (int col = 0; col < term_cols; col++) {
            // fb[base] is the lower of the two scanlines, fb[base+1] the upper.
            // The glyph's foreground fills the lower half, so it takes fb[base].
            Vec3 fg = fb[(size_t)base * width + col];
            Vec3 bg = fb[(size_t)(base + 1) * width + col];
            int r, g, b;

            if (opt.truecolor) {
                rgb8(fg, r, g, b);
                int fcode = (r << 16) | (g << 8) | b;
                if (fcode != cur_fg) {
                    std::snprintf(buf, sizeof(buf), "\x1b[38;2;%d;%d;%dm", r, g, b);
                    out += buf;
                    cur_fg = fcode;
                }
                rgb8(bg, r, g, b);
                int bcode = (r << 16) | (g << 8) | b;
                if (bcode != cur_bg) {
                    std::snprintf(buf, sizeof(buf), "\x1b[48;2;%d;%d;%dm", r, g, b);
                    out += buf;
                    cur_bg = bcode;
                }
            } else {
                rgb8(fg, r, g, b);
                int fi = quant256(r, g, b);
                if (fi != cur_fg) {
                    std::snprintf(buf, sizeof(buf), "\x1b[38;5;%dm", fi);
                    out += buf;
                    cur_fg = fi;
                }
                rgb8(bg, r, g, b);
                int bi = quant256(r, g, b);
                if (bi != cur_bg) {
                    std::snprintf(buf, sizeof(buf), "\x1b[48;5;%dm", bi);
                    out += buf;
                    cur_bg = bi;
                }
            }
            out += HALF_BLOCK;
        }
        out += "\x1b[0m";
    }
    return out;
}

} // namespace tr
