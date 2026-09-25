#pragma once
#include "math3d.h"
#include <vector>
#include <string>
#include <cstdint>

namespace tr {

struct HalfBlockOptions {
    bool truecolor = true;
};

// Encodes a framebuffer of pixels into a stream of lower-half-block glyphs.
// The framebuffer is bottom-up: row 0 is the bottom scanline. Each terminal
// cell takes two rows; the even row feeds the foreground (the lower half of
// the glyph) and the odd row feeds the background (the upper half).
//
// Every call redraws every row, top to bottom, like a CRT scan.
class HalfBlockScreen {
public:
    std::string render(const std::vector<Vec3> &fb, int width, int height, int term_cols,
                       int term_rows, const HalfBlockOptions &opt, int start_row = 1);

    static void rgb8(const Vec3 &c, int &r, int &g, int &b);

private:
    int quant256(int r, int g, int b);
    bool lut_ready_ = false;
    uint8_t lut_[32768];
};

} // namespace tr
