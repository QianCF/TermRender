#include "textutil.h"
#include <utility>

namespace tr {

int g_w_vs16 = 2;
bool g_zwj_composed = true;

int u8seqlen(unsigned char c)
{
	if (c < 0x80) return 1;
	if ((c & 0xE0) == 0xC0) return 2;
	if ((c & 0xF0) == 0xE0) return 3;
	if ((c & 0xF8) == 0xF0) return 4;
	return 1;
}

/* East-Asian-wide / emoji-presentation code point ranges (width 2) */
static const std::pair<unsigned, unsigned> WIDE_EXTRA[] = {
	{0x231A, 0x231B}, {0x23E9, 0x23EC}, {0x23F0, 0x23F0}, {0x23F3, 0x23F3},
	{0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653}, {0x267F, 0x267F},
	{0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB}, {0x26BD, 0x26BE},
	{0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4}, {0x26EA, 0x26EA},
	{0x26F2, 0x26F3}, {0x26F5, 0x26F5}, {0x26FA, 0x26FA}, {0x26FD, 0x26FD},
	{0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274C},
	{0x274E, 0x274E}, {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797},
	{0x27B0, 0x27B0}, {0x27BF, 0x27BF}, {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50},
	{0x2B55, 0x2B55},
	{0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E},
	{0x1F191, 0x1F19A}, {0x1F1E6, 0x1F1FF}, {0x1F200, 0x1F202}, {0x1F210, 0x1F23B},
	{0x1F240, 0x1F248}, {0x1F250, 0x1F251}, {0x1F300, 0x1F320},
	{0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393},
	{0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0},
	{0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440},
	{0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E},
	{0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596},
	{0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5},
	{0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7},
	{0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB},
	{0x1F90C, 0x1F93A}, {0x1F93C, 0x1F945}, {0x1F947, 0x1F978},
	{0x1F97A, 0x1F9CB}, {0x1F9CD, 0x1F9FF}, {0x1FA70, 0x1FA74},
	{0x1FA78, 0x1FA7A}, {0x1FA80, 0x1FA86}, {0x1FA90, 0x1FAA8},
	{0x1FAB0, 0x1FAB6}, {0x1FAC0, 0x1FAC2}, {0x1FAD0, 0x1FAD6},
};

int cpwidth(unsigned int c)
{
	if (c == 0) return 0;
	if (c < 32) return 0;
	if (c < 0x1100) return 1;
	if (c == 0x200B || c == 0x200C || c == 0x200D) return 0; /* ZWSP/ZWNJ/ZWJ */
	if (c >= 0xFE00 && c <= 0xFE0F) return 0;   /* variation selectors */
	if (c >= 0x1100 && c <= 0x115F) return 2;
	if (c >= 0x2E80 && c <= 0xA4CF) return 2;
	if (c >= 0xAC00 && c <= 0xD7A3) return 2;
	if (c >= 0xF900 && c <= 0xFAFF) return 2;
	if (c >= 0xFE30 && c <= 0xFE4F) return 2;
	if (c >= 0xFF00 && c <= 0xFF60) return 2;
	if (c >= 0xFFE0 && c <= 0xFFE6) return 2;
	if (c >= 0x20000 && c <= 0x3FFFD) return 2;
	for (const auto &r : WIDE_EXTRA)
		if (c >= r.first && c <= r.second)
			return 2;
	return 1;
}

unsigned int u8decode(const std::string &s, size_t i, int *len)
{
	unsigned char c = (unsigned char)s[i];
	int n = u8seqlen(c);
	if (i + n > s.size()) { *len = 1; return c; }
	if (n == 1) { *len = 1; return c; }
	unsigned int v = (n == 2) ? (c & 0x1F) : (n == 3) ? (c & 0x0F) : (c & 0x07);
	for (int k = 1; k < n; k++) {
		unsigned char cc = (unsigned char)s[i + k];
		if ((cc & 0xC0) != 0x80) { *len = 1; return c; }
		v = (v << 6) | (cc & 0x3F);
	}
	*len = n;
	return v;
}

bool is_extend(unsigned int c)
{
	if (c >= 0xFE00 && c <= 0xFE0F) return true;   /* variation selectors */
	if (c >= 0x0300 && c <= 0x036F) return true;   /* combining diacritics */
	if (c >= 0x1AB0 && c <= 0x1AFF) return true;
	if (c >= 0x1DC0 && c <= 0x1DFF) return true;
	if (c >= 0x20D0 && c <= 0x20F0) return true;   /* incl. keycap 20E3 */
	if (c >= 0xFE20 && c <= 0xFE2F) return true;
	if (c >= 0xE0020 && c <= 0xE007F) return true; /* subdivision tags */
	if (c >= 0x1F3FB && c <= 0x1F3FF) return true; /* skin tone modifiers */
	if (c == 0x0E31 || (c >= 0x0E34 && c <= 0x0E3A)) return true;
	if (c >= 0x0E47 && c <= 0x0E4E) return true;
	if (c >= 0x200B && c <= 0x200F) return true;   /* zero-width / LRM..RLM */
	return false;
}

bool is_ri(unsigned int c)
{
	return c >= 0x1F1E6 && c <= 0x1F1FF;           /* regional indicators */
}

size_t next_cluster(const std::string &s, size_t i)
{
	size_t n = s.size();
	int nb;
	unsigned int c = u8decode(s, i, &nb);
	size_t j = i + (size_t)nb;
	if (is_ri(c) && j < n) {                       /* flag = pair of RIs */
		int nb2;
		unsigned int c2 = u8decode(s, j, &nb2);
		if (is_ri(c2))
			j += (size_t)nb2;
	}
	for (;;) {
		if (j >= n)
			break;
		int nb2;
		unsigned int c2 = u8decode(s, j, &nb2);
		if (c2 == 0x200D) {                        /* ZWJ joins the next char */
			j += (size_t)nb2;
			if (j < n) {
				int nb3;
				u8decode(s, j, &nb3);
				j += (size_t)nb3;
			}
			continue;
		}
		if (is_extend(c2)) {
			j += (size_t)nb2;
			continue;
		}
		break;
	}
	return j;
}

size_t prev_cluster(const std::string &s, size_t i)
{
	size_t p = 0, prev = 0;
	while (p < i) {
		prev = p;
		p = next_cluster(s, p);
	}
	return prev;
}

bool is_ctrl_byte(unsigned char c)
{
	return c < 0x20 || c == 0x7F;
}

std::string ctrl_text(unsigned char c)
{
	return std::string("^") + (char)(c == 0x7F ? '?' : c + 0x40);
}

std::string disp_expand(const std::string &s)
{
	std::string o;
	for (size_t i = 0; i < s.size();) {
		unsigned char c = (unsigned char)s[i];
		if (is_ctrl_byte(c)) {
			o += "\x1b[90m";
			o += ctrl_text(c);
			o += "\x1b[39m";
			i++;
			continue;
		}
		if (c < 0x80) { o += (char)c; i++; continue; }
		size_t j = next_cluster(s, i);
		o.append(s, i, j - i);
		i = j;
	}
	return o;
}

int cluster_width(const std::string &s, size_t i, size_t end)
{
	{
		int nb0;
		unsigned int c0 = u8decode(s, i, &nb0);
		if (is_ctrl_byte((unsigned char)c0))
			return (int)ctrl_text((unsigned char)c0).size();
	}
	bool emoji = false, zwj = false;
	int w = 0;
	for (size_t k = i; k < end;) {
		int nb;
		unsigned int c = u8decode(s, k, &nb);
		if (c == 0xFE0F)
			emoji = true;                  /* emoji presentation forced */
		if (c == 0x200D)
			zwj = true;
		int cw = cpwidth(c);
		if (cw > w)
			w = cw;
		k += (size_t)nb;
	}
	if (emoji && g_w_vs16 == 2)
		return 2;
	if (zwj) {
		if (g_zwj_composed)
			return 2;
		/* the client explodes the chain: every visible member counts */
		int sum = 0;
		for (size_t k = i; k < end;) {
			int nb;
			unsigned int c = u8decode(s, k, &nb);
			if (c != 0x200D && !is_extend(c))
				sum += cpwidth(c);
			k += (size_t)nb;
		}
		return sum;
	}
	return w;
}

int u8width(const std::string &s)
{
	int w = 0;
	for (size_t i = 0; i < s.size();) {
		size_t j = next_cluster(s, i);
		w += cluster_width(s, i, j);
		i = j;
	}
	return w;
}

std::string u8clip(const std::string &s, int w)
{
	std::string out;
	int cw = 0;
	for (size_t i = 0; i < s.size();) {
		size_t j = next_cluster(s, i);
		int clw = cluster_width(s, i, j);
		if (cw + clw > w)
			break;
		out.append(s, i, j - i);
		cw += clw;
		i = j;
	}
	return out;
}

std::vector<std::string> u8wrap(const std::string &s, int w)
{
	std::vector<std::string> out;
	if (w < 1) w = 1;
	std::string cur;
	int cw = 0;
	for (size_t i = 0; i < s.size();) {
		size_t j = next_cluster(s, i);
		int clw = cluster_width(s, i, j);
		if (cw + clw > w && !cur.empty()) {
			out.push_back(cur);
			cur.clear();
			cw = 0;
		}
		cur.append(s, i, j - i);
		cw += clw;
		i = j;
	}
	out.push_back(cur);
	return out;
}

void cursor_chunk(const std::string &s, int cx, int w, int &chunk, int &col)
{
	chunk = 0;
	col = 0;
	if (w < 1) w = 1;
	std::string cur;
	for (size_t i = 0; i < s.size() && (int)i < cx;) {
		size_t j = next_cluster(s, i);
		int clw = cluster_width(s, i, j);
		if (col + clw > w && !cur.empty()) { chunk++; cur.clear(); col = 0; }
		cur.append(s, i, j - i);
		col += clw;
		i = j;
	}
}

size_t chunk_byte_offset(const std::string &s, int chunk_want, int col_want, int w)
{
	if (w < 1) w = 1;
	int chunk = 0, col = 0;
	std::string cur;
	size_t i = 0;
	while (i < s.size()) {
		size_t j = next_cluster(s, i);
		int clw = cluster_width(s, i, j);
		if (col + clw > w && !cur.empty()) { chunk++; cur.clear(); col = 0; }
		if (chunk > chunk_want)
			return i;
		if (chunk == chunk_want && col + clw > col_want)
			return i;
		cur.append(s, i, j - i);
		col += clw;
		i = j;
	}
	return s.size();
}

std::string pad_to(const std::string &s, int w, bool right)
{
	std::string t = u8clip(s, w);
	int pad = w - u8width(t);
	if (pad < 0) pad = 0;
	if (right) return std::string(pad, ' ') + t;
	return t + std::string(pad, ' ');
}

std::string cyclic_window(const std::string &s, int off, int w)
{
	std::string cyc = s + "   ";
	int W = u8width(cyc);
	if (W <= 0 || w <= 0)
		return "";
	off %= W;
	std::string out;
	int col = 0, taken = 0;
	for (int pass = 0; pass < 2 && taken < w; pass++) {
		for (size_t i = 0; i < cyc.size() && taken < w;) {
			size_t j = next_cluster(cyc, i);
			int cwp = cluster_width(cyc, i, j);
			if (col >= off) {
				out.append(cyc, i, j - i);
				taken += cwp;
			}
			col += cwp;
			i = j;
		}
	}
	return out;
}

} // namespace tr
