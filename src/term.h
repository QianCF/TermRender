#pragma once
#include <string>
#include <deque>
#include <cstdint>

namespace tr {

enum class Key {
    None,
    Char,
    Enter,
    Esc,
    Backspace,
    Tab,
    Space,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Delete,
    Resize,
    Tick
};

struct KeyEvent {
    Key key = Key::None;
    std::string text; // UTF-8 for Key::Char
};

struct TermSize {
    int cols = 0;
    int rows = 0;
    bool valid() const { return cols > 0 && rows > 0; }
};

class Terminal {
public:
    Terminal();
    ~Terminal();

    bool init();
    void shutdown();
    bool valid() const { return valid_; }

    // Return the most recently reported terminal size. The size is obtained
    // exclusively through the ANSI "CSI 18 t" query; no native console API is
    // used. request_size_query() asks for a fresh report.
    TermSize size() const { return cached_; }

    // Ask the terminal to report its size (CSI 18 t). The reply is consumed
    // by read_key() and updates the cached size. Safe to call every frame.
    void request_size_query();

    // Send a size query and wait (up to timeout_ms) for the reply. Used once
    // at startup so the first frame already knows the size.
    TermSize query_size(int timeout_ms);

    // Wait up to timeout_ms for a key. Returns false on timeout.
    bool read_key(KeyEvent &ev, int timeout_ms);

    // Read and parse whatever input is available (waiting up to timeout_ms for
    // the first byte). Key events are queued for read_key(); size reports update
    // the cached size immediately. Safe to call at any time.
    void pump(int timeout_ms);

    // Buffered output.
    void write(const std::string &s);
    void flush();

    void hide_cursor();
    void show_cursor();
    void enter_alt_screen();
    void leave_alt_screen();

    // Probe the terminal for how it renders ambiguous clusters (emoji with
    // VS16, ZWJ chains) using cursor-position reports, and set the text
    // metrics globals accordingly. Ported from sshfm's client calibration.
    void calibrate_emoji(int rows);

private:
    bool wait_input(int timeout_ms);
    void read_available(std::string &out);
    bool parse_one(KeyEvent &ev);
    int read_cpr(int timeout_ms);
    int probe_cluster_width(const std::string &cluster);

    bool valid_ = false;
    std::string inbuf_;
    std::deque<KeyEvent> pending_;
    std::string outbuf_;
    TermSize cached_{0, 0};

#ifdef _WIN32
    unsigned pending_high_ = 0; // pending UTF-16 high surrogate (Windows input)
    void *h_in_ = nullptr;
    void *h_out_ = nullptr;
    unsigned long in_mode_ = 0;
    unsigned long out_mode_ = 0;
#else
    int fd_in_ = 0;
    int fd_out_ = 1;
    bool raw_saved_ = false;
    void *saved_termios_ = nullptr;
#endif
};

} // namespace tr
