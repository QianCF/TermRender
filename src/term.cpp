#include "term.h"
#include "textutil.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#else
#include <unistd.h>
#include <termios.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <csignal>
#endif

namespace tr {

namespace {
inline bool is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }
inline int u8len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}
#ifdef _WIN32
inline void append_utf8(std::string &out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}
#endif
} // namespace

Terminal::Terminal() {}
Terminal::~Terminal() { shutdown(); }

bool Terminal::init() {
#ifdef _WIN32
    h_in_ = GetStdHandle(STD_INPUT_HANDLE);
    h_out_ = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h_in_ == INVALID_HANDLE_VALUE || h_out_ == INVALID_HANDLE_VALUE) return false;
    if (!GetConsoleMode(h_in_, &in_mode_)) return false;
    if (!GetConsoleMode(h_out_, &out_mode_)) return false;

    DWORD im = in_mode_;
    /* Read console input records directly (ReadConsoleInputW) and synthesise
     * the VT byte sequences ourselves: this is far more reliable than
     * ENABLE_VIRTUAL_TERMINAL_INPUT + ReadFile on Windows consoles. */
    im &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT |
            ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT);
    im |= ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(h_in_, im);

    DWORD om = out_mode_;
    om |= ENABLE_VIRTUAL_TERMINAL_PROCESSING | ENABLE_PROCESSED_OUTPUT;
    om &= ~ENABLE_WRAP_AT_EOL_OUTPUT;
    SetConsoleMode(h_out_, om);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    valid_ = true;
#else
    fd_in_ = 0;
    fd_out_ = 1;
    struct termios *t = new termios();
    if (tcgetattr(fd_in_, t) != 0) {
        delete t;
        return false;
    }
    saved_termios_ = t;
    raw_saved_ = true;
    struct termios raw = *t;
    raw.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    raw.c_oflag &= ~OPOST;
    raw.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    raw.c_cflag &= ~(CSIZE | PARENB);
    raw.c_cflag |= CS8;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(fd_in_, TCSAFLUSH, &raw);
    valid_ = true;
#endif
    return valid_;
}

void Terminal::shutdown() {
    if (!valid_) return;
    flush();
#ifdef _WIN32
    if (h_in_ && h_in_ != INVALID_HANDLE_VALUE) SetConsoleMode(h_in_, in_mode_);
    if (h_out_ && h_out_ != INVALID_HANDLE_VALUE) SetConsoleMode(h_out_, out_mode_);
#else
    if (raw_saved_ && saved_termios_) {
        tcsetattr(fd_in_, TCSAFLUSH, (struct termios *)saved_termios_);
        delete (struct termios *)saved_termios_;
        saved_termios_ = nullptr;
    }
#endif
    valid_ = false;
}

void Terminal::request_size_query() {
    write("\x1b[18t");
    /* send it right away: the loop may otherwise have nothing else to flush */
    flush();
}

TermSize Terminal::query_size(int timeout_ms) {
    write("\x1b[18t");
    flush();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!cached_.valid()) {
        int remaining = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now())
                            .count();
        if (remaining <= 0) break;
        if (!wait_input(remaining)) break;
        read_available(inbuf_);
        KeyEvent ev;
        while (parse_one(ev)) {
            /* drop anything typed while we waited for the size report */
        }
    }
    return cached_;
}

void Terminal::write(const std::string &s) { outbuf_ += s; }

void Terminal::flush() {
    if (outbuf_.empty()) return;
#ifdef _WIN32
    const char *p = outbuf_.data();
    size_t remaining = outbuf_.size();
    while (remaining > 0) {
        DWORD written = 0;
        if (!WriteFile((HANDLE)h_out_, p, (DWORD)remaining, &written, nullptr) || written == 0)
            break;
        p += written;
        remaining -= written;
    }
#else
    size_t off = 0;
    while (off < outbuf_.size()) {
        ssize_t n = ::write(fd_out_, outbuf_.data() + off, outbuf_.size() - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
#endif
    outbuf_.clear();
}

void Terminal::hide_cursor() { write("\x1b[?25l"); }
void Terminal::show_cursor() { write("\x1b[?25h"); }
void Terminal::enter_alt_screen() { write("\x1b[?1049h"); }
void Terminal::leave_alt_screen() { write("\x1b[?1049l"); }

// Read one cursor-position report (ESC[row;colR); returns col, -1 on none.
int Terminal::read_cpr(int timeout_ms) {
    std::string reply;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        int remaining = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now())
                            .count();
        if (remaining <= 0) break;
        if (!wait_input(remaining)) break;
        std::string chunk;
        read_available(chunk);
        if (chunk.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        reply += chunk;
        size_t p = reply.find('R');
        if (p != std::string::npos) {
            size_t esc = reply.rfind("\x1b[", p);
            if (esc != std::string::npos) {
                int r = 0, c = 0;
                if (std::sscanf(reply.c_str() + esc, "\x1b[%d;%dR", &r, &c) == 2)
                    return c;
            }
            return -1;
        }
    }
    return -1;
}

// Ask the terminal how wide it renders one probe cluster. The cursor column is
// measured before and after, so the result is independent of where it started.
int Terminal::probe_cluster_width(const std::string &cluster) {
    write("\x1b[6n");
    flush();
    int base = read_cpr(250);
    if (base < 0) return -1;
    write(cluster + "\x1b[6n");
    flush();
    int after = read_cpr(250);
    if (after < 0) return -1;
    return after - base;
}

void Terminal::calibrate_emoji(int rows) {
    if (rows < 1) rows = 1;
    char buf[48];
    std::snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2K", rows);
    write(buf);
    flush();

    // VS16 presentation: 2 when honoured, 1 when the terminal ignores it.
    int w = probe_cluster_width("\xE2\x98\xBA\xEF\xB8\x8F"); // ☺️
    if (w == 1) g_w_vs16 = 1;
    // ZWJ chain: 2 when composed, more when exploded into separate emoji.
    w = probe_cluster_width("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9"
                            "\xE2\x80\x8D\xF0\x9F\x91\xA6"); // 👨‍👩‍👦
    if (w > 2) g_zwj_composed = false;

    std::snprintf(buf, sizeof(buf), "\x1b[%d;1H\x1b[2K", rows);
    write(buf);
    flush();
}

bool Terminal::wait_input(int timeout_ms) {
#ifdef _WIN32
    DWORD r = WaitForSingleObject((HANDLE)h_in_, timeout_ms < 0 ? INFINITE : (DWORD)timeout_ms);
    return r == WAIT_OBJECT_0;
#else
    struct pollfd pfd;
    pfd.fd = fd_in_;
    pfd.events = POLLIN;
    int r = poll(&pfd, 1, timeout_ms);
    return r > 0 && (pfd.revents & POLLIN);
#endif
}

void Terminal::read_available(std::string &out) {
#ifdef _WIN32
    DWORD avail = 0;
    if (!GetNumberOfConsoleInputEvents((HANDLE)h_in_, &avail) || avail == 0) return;
    INPUT_RECORD recs[128];
    DWORD want = avail > 128 ? 128 : avail;
    DWORD got = 0;
    if (!ReadConsoleInputW((HANDLE)h_in_, recs, want, &got)) return;
    for (DWORD i = 0; i < got; i++) {
        if (recs[i].EventType != KEY_EVENT) continue;
        const KEY_EVENT_RECORD &k = recs[i].Event.KeyEvent;
        if (!k.bKeyDown) continue;
        unsigned cp = (unsigned)k.uChar.UnicodeChar;
        if (cp == 0) {
            switch (k.wVirtualKeyCode) {
            case VK_UP: out += "\x1b[A"; break;
            case VK_DOWN: out += "\x1b[B"; break;
            case VK_RIGHT: out += "\x1b[C"; break;
            case VK_LEFT: out += "\x1b[D"; break;
            case VK_HOME: out += "\x1b[H"; break;
            case VK_END: out += "\x1b[F"; break;
            case VK_PRIOR: out += "\x1b[5~"; break;
            case VK_NEXT: out += "\x1b[6~"; break;
            case VK_DELETE: out += "\x1b[3~"; break;
            default: break;
            }
            continue;
        }
        if (cp >= 0xD800 && cp <= 0xDBFF) { // high surrogate: wait for the low
            pending_high_ = cp;
            continue;
        }
        if (cp >= 0xDC00 && cp <= 0xDFFF) { // low surrogate
            if (pending_high_) {
                unsigned full = 0x10000 + ((pending_high_ - 0xD800) << 10) + (cp - 0xDC00);
                append_utf8(out, full);
                pending_high_ = 0;
            }
            continue;
        }
        pending_high_ = 0;
        append_utf8(out, cp);
    }
#else
    char buf[512];
    for (;;) {
        ssize_t n = ::read(fd_in_, buf, sizeof(buf));
        if (n <= 0) break;
        out.append(buf, (size_t)n);
        if (n < (ssize_t)sizeof(buf)) {
            // drain until EAGAIN using a zero-timeout poll
            struct pollfd pfd;
            pfd.fd = fd_in_;
            pfd.events = POLLIN;
            if (poll(&pfd, 1, 0) <= 0) break;
        }
    }
#endif
}

bool Terminal::parse_one(KeyEvent &ev) {
    if (inbuf_.empty()) return false;
    unsigned char c = (unsigned char)inbuf_[0];

    if (c == 0x1b) {
        if (inbuf_.size() < 2) return false; // wait for the rest
        if (inbuf_[1] == '[') {
            // CSI: parameters then final byte
            size_t k = 2;
            while (k < inbuf_.size()) {
                unsigned char ch = (unsigned char)inbuf_[k];
                if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?') k++;
                else break;
            }
            if (k >= inbuf_.size()) return false; // final byte not arrived
            unsigned char fin = (unsigned char)inbuf_[k];
            std::string params = inbuf_.substr(2, k - 2);
            size_t eat = k + 1;

            if (fin == 't') {
                // window/text-area size report: CSI 8 ; rows ; cols t
                int vals[4] = {0, 0, 0, 0};
                int vi = 0;
                size_t start = 0;
                for (size_t i = 0; i <= params.size() && vi < 4; i++) {
                    if (i == params.size() || params[i] == ';') {
                        vals[vi++] = std::atoi(params.substr(start, i - start).c_str());
                        start = i + 1;
                    }
                }
                if (vi >= 3 && vals[0] == 8) {
                    bool changed = false;
                    if (vals[2] > 0 && vals[2] != cached_.cols) {
                        cached_.cols = vals[2];
                        changed = true;
                    }
                    if (vals[1] > 0 && vals[1] != cached_.rows) {
                        cached_.rows = vals[1];
                        changed = true;
                    }
                    inbuf_.erase(0, eat);
                    if (changed) {
                        ev.key = Key::Resize;
                        return true;
                    }
                    return false;
                }
                inbuf_.erase(0, eat);
                return false;
            }

            Key kk = Key::None;
            switch (fin) {
            case 'A': kk = Key::Up; break;
            case 'B': kk = Key::Down; break;
            case 'C': kk = Key::Right; break;
            case 'D': kk = Key::Left; break;
            case 'H': kk = Key::Home; break;
            case 'F': kk = Key::End; break;
            case '~': {
                int p = std::atoi(params.c_str());
                switch (p) {
                case 1: case 7: kk = Key::Home; break;
                case 3: kk = Key::Delete; break;
                case 4: case 8: kk = Key::End; break;
                case 5: kk = Key::PageUp; break;
                case 6: kk = Key::PageDown; break;
                default: break;
                }
                break;
            }
            default: break;
            }
            inbuf_.erase(0, eat);
            if (kk != Key::None) {
                ev.key = kk;
                return true;
            }
            return false;
        } else if (inbuf_[1] == 'O') {
            if (inbuf_.size() < 3) return false;
            Key kk = Key::None;
            switch (inbuf_[2]) {
            case 'A': kk = Key::Up; break;
            case 'B': kk = Key::Down; break;
            case 'C': kk = Key::Right; break;
            case 'D': kk = Key::Left; break;
            case 'H': kk = Key::Home; break;
            case 'F': kk = Key::End; break;
            default: break;
            }
            inbuf_.erase(0, 3);
            if (kk != Key::None) {
                ev.key = kk;
                return true;
            }
            return false;
        }
        inbuf_.erase(0, 1);
        ev.key = Key::Esc;
        return true;
    }

    // NOTE: control bytes (0x03 ^C, 0x0c ^L, 0x11 ^Q, 0x13 ^S, ...) are NOT
    // special-cased here. They flow through as Key::Char carrying the raw byte,
    // exactly like any other control character, so a text prompt inserts them
    // naturally. Only contexts that actually need a given key react to its byte.
    if (c == 0x7f || c == 0x08) { inbuf_.erase(0, 1); ev.key = Key::Backspace; return true; }
    if (c == 0x0d || c == 0x0a) { inbuf_.erase(0, 1); ev.key = Key::Enter; return true; }
    if (c == 0x09) { inbuf_.erase(0, 1); ev.key = Key::Tab; return true; }
    if (c == 0x20) { inbuf_.erase(0, 1); ev.key = Key::Space; ev.text = " "; return true; }

    int len = u8len(c);
    if ((int)inbuf_.size() < len) return false;
    for (int i = 1; i < len; i++)
        if (!is_cont((unsigned char)inbuf_[i])) { len = 1; break; }
    ev.key = Key::Char;
    ev.text = inbuf_.substr(0, (size_t)len);
    inbuf_.erase(0, (size_t)len);
    return true;
}

void Terminal::pump(int timeout_ms) {
    if (timeout_ms > 0) {
        if (wait_input(timeout_ms)) read_available(inbuf_);
    } else {
        read_available(inbuf_);
    }
    KeyEvent ev;
    while (parse_one(ev)) {
        if (ev.key != Key::None) pending_.push_back(ev);
        ev = KeyEvent();
    }
}

bool Terminal::read_key(KeyEvent &ev, int timeout_ms) {
    if (!pending_.empty()) {
        ev = pending_.front();
        pending_.pop_front();
        return true;
    }

    ev = KeyEvent();
    bool infinite = (timeout_ms < 0);
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(infinite ? 0 : timeout_ms);

    for (;;) {
        pump(0);
        if (!pending_.empty()) {
            ev = pending_.front();
            pending_.pop_front();
            return true;
        }

        // A lone ESC: give the terminal a moment to deliver a sequence
        // (arrow keys etc.) before deciding it was a plain Escape.
        if (inbuf_.size() == 1 && (unsigned char)inbuf_[0] == 0x1b) {
            if (!wait_input(25)) {
                inbuf_.clear();
                ev.key = Key::Esc;
                return true;
            }
            continue;
        }

        // Incomplete escape sequence: wait briefly for the rest.
        if (!inbuf_.empty()) {
            if (!wait_input(30)) {
                inbuf_.clear();
                continue;
            }
            continue;
        }

        int remaining;
        if (infinite) {
            remaining = -1;
        } else {
            remaining = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                            deadline - std::chrono::steady_clock::now())
                            .count();
            if (remaining <= 0) return false;
        }
        if (!wait_input(remaining)) return false;
    }
}

} // namespace tr
