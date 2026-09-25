#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace tr {

/* Emoji rendering knobs, calibrated once at startup against the real terminal
 * (see Terminal::calibrate_emoji).  Ported from sshfm. */
extern int g_w_vs16;        /* width of a VS16 emoji cluster: 2 honoured, 1 ignored */
extern bool g_zwj_composed; /* ZWJ chains composed into a single glyph? */

int u8seqlen(unsigned char c);
int cpwidth(unsigned int c);
unsigned int u8decode(const std::string &s, size_t i, int *len);
bool is_extend(unsigned int c);
bool is_ri(unsigned int c);

/* Grapheme clusters: the unit of width, movement and editing. */
size_t next_cluster(const std::string &s, size_t i);
size_t prev_cluster(const std::string &s, size_t i);

bool is_ctrl_byte(unsigned char c);
std::string ctrl_text(unsigned char c);
std::string disp_expand(const std::string &s);

int cluster_width(const std::string &s, size_t i, size_t end);
int u8width(const std::string &s);
std::string u8clip(const std::string &s, int w);
std::vector<std::string> u8wrap(const std::string &s, int w);
void cursor_chunk(const std::string &s, int cx, int w, int &chunk, int &col);
size_t chunk_byte_offset(const std::string &s, int chunk_want, int col_want, int w);

/* Pad/clip to exactly `w` display columns (right = right-align). */
std::string pad_to(const std::string &s, int w, bool right);
/* A window of `w` display columns starting at display column `off`, cycling. */
std::string cyclic_window(const std::string &s, int off, int w);

} // namespace tr
