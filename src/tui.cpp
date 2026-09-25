#include "tui.h"
#include "gltf_loader.h"
#include "textutil.h"
#include "envmap.h"

#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace tr {

namespace {

// printf into a dynamically-sized std::string (no fixed truncation; lines can
// be many thousands of columns wide on very wide terminals).
std::string sfmt(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = std::vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    std::string out;
    if (n > 0) {
        out.resize((size_t)n);
        std::vsnprintf(&out[0], (size_t)n + 1, fmt, ap2);
    }
    va_end(ap2);
    return out;
}

std::string base_name(const std::string &p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

const char *MENU_ITEMS[] = {"Start Render...", "Settings", "Quit"};
const int MENU_COUNT = 3;
const int SETTINGS_COUNT = 9;

const int MARQUEE_MS = 120;

} // namespace

// ------------------------------------------------------------------ helpers

static bool dbg_log() {
    static const bool b = std::getenv("TR_LOG") != nullptr;
    return b;
}
static long dbg_ms() {
    static const auto t0 = std::chrono::steady_clock::now();
    return (long)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
}

static float scene_radius(const Renderer &r) {
    const Scene &s = r.scene();
    if (!s.has_bbox) return 2.0f;
    Vec3 d = s.bbox_max - s.bbox_min;
    return std::max(0.1f, 0.5f * length(d));
}

bool App::refresh_size() {
    TermSize sz = term_.size();
    if (!sz.valid()) sz = TermSize{80, 24};
    int nc = std::max(12, sz.cols);
    int nr = std::max(6, sz.rows);
    bool changed = (nc != cols_ || nr != rows_);
    cols_ = nc;
    rows_ = nr;
    return changed;
}

void App::poll_size() {
    auto now = std::chrono::steady_clock::now();
    bool sent = false;
    if (last_size_poll_.time_since_epoch().count() == 0 ||
        now - last_size_poll_ >= std::chrono::milliseconds(50)) {
        last_size_poll_ = now;
        term_.request_size_query();
        sent = true;
    }
    /* Actively wait a moment for the report so a resize is noticed within
     * ~0.1 s even while idle. Keys read here are queued, never lost. */
    term_.pump(sent ? 25 : 0);
    if (refresh_size()) {
        full_redraw_ = true;
        // A size change is an effectful event in the render interface: cancel
        // the current render and start over at the new size.
        if (renderer_.has_scene() && content_view() == Mode::Render) need_render_ = true;
    }
}

void App::layout() {
    main_cols_ = cols_;

    // The key band reflects the actual mode (a prompt shows its own shorter
    // hint set). If that changes the drawable image area, the size check below
    // (driven by the periodic size polling) cancels and restarts the render.
    std::vector<std::string> hints;
    if (mode_ == Mode::Prompt)
        hints = {"Enter:ok", "Esc:cancel"};
    else if (mode_ == Mode::Message)
        hints = {"any key:back"};
    else if (mode_ == Mode::Menu)
        hints = {"Enter:run", "Up/Down:move"};
    else if (mode_ == Mode::Settings)
        hints = {"Up/Down:move", "Left/Right:change", "Enter:change"};
    else
        hints = {"c:speed", "^C:turn", "f:fov", "^S:settings", "arrows:look", "wasdqe:move",
                 "^Q:exit"};

    std::vector<std::vector<std::string>> lines;
    std::vector<std::string> cur;
    int curw = 0;
    for (auto &h : hints) {
        int hw = u8width(h);
        if (!cur.empty() && curw + hw + (int)cur.size() + 1 > cols_) {
            lines.push_back(cur);
            cur.clear();
            curw = 0;
        }
        cur.push_back(h);
        curw += hw;
    }
    if (!cur.empty() || lines.empty()) lines.push_back(cur);

    keyline_cache_.clear();
    for (auto &ln : lines) {
        int n = (int)ln.size();
        if (n == 0) { keyline_cache_.push_back(""); continue; }
        int hintsum = 0;
        for (auto &h : ln) hintsum += u8width(h);
        int gap = cols_ - hintsum;
        if (gap < n) gap = n;
        int base = gap / n, rem = gap % n;
        std::string out;
        for (int i = 0; i < n; i++) {
            out += ln[i];
            int g = base + (i < rem ? 1 : 0);
            if (g < 1) g = 1;
            out += std::string(g, ' ');
        }
        keyline_cache_.push_back(out);
    }
    keylines_ = (int)keyline_cache_.size();

    input_row_ = rows_;
    msg_row_ = rows_ - 1;
    main_top_ = (content_view() == Mode::Render) ? 2 : 3;
    main_bottom_ = rows_ - 2 - keylines_;
    if (main_bottom_ < main_top_) main_bottom_ = main_top_;
}

std::string App::title_text() const {
    if (mode_ == Mode::Settings) return "TermRender  -  Settings";
    if (mode_ == Mode::Message) return "TermRender  -  Notice";
    if (mode_ == Mode::Render)
        return loaded_path_.empty() ? "TermRender" : loaded_path_;
    if (mode_ == Mode::Prompt) {
        if (prompt_kind_ == PromptKind::Fov) return "TermRender  -  Field of view";
        if (prompt_kind_ == PromptKind::Speed) return "TermRender  -  Move speed";
        if (prompt_kind_ == PromptKind::AngleSpeed) return "TermRender  -  Turn speed";
        if (prompt_kind_ == PromptKind::Background) return "TermRender  -  Background HDR";
        return "TermRender  -  Start Render";
    }
    return loaded_path_.empty() ? "TermRender  -  terminal path tracer"
                                : "TermRender  -  " + loaded_path_;
}

bool App::marquee_tick() {
    auto now = std::chrono::steady_clock::now();
    if (last_marquee_.time_since_epoch().count() != 0 &&
        now - last_marquee_ < std::chrono::milliseconds(MARQUEE_MS))
        return false;
    last_marquee_ = now;

    bool changed = false;
    if (u8width(title_text()) > cols_) {
        title_scroll_++;
        changed = true;
    } else {
        title_scroll_ = 0;
    }
    if (u8width(status_) > cols_) {
        msg_scroll_++;
        changed = true;
    } else {
        msg_scroll_ = 0;
    }
    return changed;
}

// ------------------------------------------------------------------- output

void App::put(std::string &s, int row, int col, const std::string &text) {
    s += sfmt("\x1b[%d;%dH", row, col);
    s += text;
}

void App::put_fill(std::string &s, int row, const std::string &text, int w) {
    std::string t = u8clip(text, w);
    int pad = w - u8width(t);
    if (pad < 0) pad = 0;
    s += sfmt("\x1b[%d;1H%s", row, disp_expand(t).c_str());
    for (int i = 0; i < pad; i++)
        s += " ";
}

void App::draw_title(std::string &s) {
    std::string tt = title_text();
    if (u8width(tt) > cols_) tt = cyclic_window(tt, title_scroll_, cols_);
    s += "\x1b[7m";
    put_fill(s, 1, tt, cols_);
    s += "\x1b[0m";
}

void App::draw_menu(std::string &s) {
    put_fill(s, 2, "  Select a program and press Enter:", main_cols_);
    for (int i = 0; i < MENU_COUNT; i++) {
        int row = main_top_ + i;
        std::string line = "   " + std::string(MENU_ITEMS[i]);
        if (i == menu_sel_) {
            s += "\x1b[7m";
            put_fill(s, row, line, cols_);
            s += "\x1b[0m";
        } else {
            put_fill(s, row, line, cols_);
        }
    }
    for (int r = main_top_ + MENU_COUNT; r <= main_bottom_; r++)
        put_fill(s, r, "", cols_);
}

void App::draw_settings(std::string &s) {
    put_fill(s, 2, "  Change a setting with Left/Right or Enter:", main_cols_);
    auto row_text = [&](int i, const std::string &label, const std::string &value) {
        std::string line = "   " + pad_to(label, 20, false) + "  " + value;
        int row = main_top_ + i;
        if (i == settings_sel_) {
            s += "\x1b[7m";
            put_fill(s, row, line, cols_);
            s += "\x1b[0m";
        } else {
            put_fill(s, row, line, cols_);
        }
    };
    row_text(0, "Color mode", settings_.truecolor ? "TrueColor (24-bit)" : "256 colors");
    row_text(1, "Samples / frame", sfmt("%d", settings_.samples_per_frame));
    row_text(2, "Max bounces", sfmt("%d", settings_.max_bounces));
    row_text(3, "Exposure (EV)", sfmt("%+.2f", settings_.exposure_ev));
    row_text(4, "Environment", sfmt("%.2f", settings_.env_intensity));
    row_text(5, "Clamp radiance",
             settings_.clamp_threshold <= 0.0f ? std::string("off")
                                               : sfmt("%.2f", settings_.clamp_threshold));
    static const char *kTonemap[] = {"None (linear)", "Reinhard", "Cineon", "ACES",
                                     "Uncharted 2"};
    int tm = settings_.tonemap_mode;
    if (tm < 0 || tm > 4) tm = 0;
    row_text(6, "Tonemap", kTonemap[tm]);
    row_text(7, "Background (.hdr)",
             env_path_.empty() ? "procedural gradient" : base_name(env_path_));
    row_text(8, "Back", "");
    for (int r = main_top_ + SETTINGS_COUNT; r <= main_bottom_; r++)
        put_fill(s, r, "", cols_);
}

void App::draw_message(std::string &s) {
    std::vector<std::string> wrapped = u8wrap(notice_, main_cols_ - 2);
    int row = main_top_;
    for (size_t i = 0; i < wrapped.size() && row <= main_bottom_; i++, row++)
        put_fill(s, row, "  " + wrapped[i], main_cols_);
    for (; row <= main_bottom_; row++)
        put_fill(s, row, "", cols_);
}

void App::draw_render(std::string &s) {
    int img_rows = main_bottom_ - main_top_ + 1;
    int img_h = img_rows * 2;
    const std::vector<Vec3> &fb = frame_buf_;
    if ((int)fb.size() != cols_ * img_h) return;
    HalfBlockOptions opt;
    opt.truecolor = settings_.truecolor;
    s += screen_.render(fb, cols_, img_h, cols_, img_rows, opt, main_top_);
}

void App::draw_keys(std::string &s) {
    for (int i = 0; i < keylines_ && i < (int)keyline_cache_.size(); i++) {
        int row = rows_ - 2 - keylines_ + 1 + i;
        s += "\x1b[7m";
        put_fill(s, row, keyline_cache_[i], cols_);
        s += "\x1b[0m";
    }
}

void App::draw_msg(std::string &s) {
    std::string t = status_;
    if (u8width(t) > cols_)
        t = cyclic_window(t, msg_scroll_, cols_);
    put_fill(s, msg_row_, t, cols_);
}

void App::draw_input(std::string &s) {
    if (mode_ != Mode::Prompt) {
        put(s, input_row_, 1, "\x1b[K");
        return;
    }
    const std::string &label = prompt_label_;
    put(s, input_row_, 1, "\x1b[K");
    put(s, input_row_, 1, label);
    int labw = u8width(label);
    int y = cols_ - labw;
    if (y < 1) y = 1;
    int x = u8width(prompt_buf_);
    int n = (x < y / 4) ? x : y / 4;
    if (n < 1) n = 1;
    int textw = y - 1;
    if (textw < 1) textw = 1;
    int showw = (x < textw) ? x : textw;
    if (showw < n) showw = n;
    if (showw < 1) showw = 1;
    int curw = u8width(prompt_buf_.substr(0, prompt_cx_));
    if (curw < prompt_scroll_) prompt_scroll_ = curw;
    if (curw - prompt_scroll_ > showw) prompt_scroll_ = curw - showw;
    if (x > n && curw - prompt_scroll_ < n) prompt_scroll_ = curw - n;
    if (prompt_scroll_ < 0) prompt_scroll_ = 0;
    {
        int acc = 0, snap = 0;
        for (size_t i = 0; i < prompt_buf_.size();) {
            if (acc >= prompt_scroll_) { snap = acc; break; }
            size_t j = next_cluster(prompt_buf_, i);
            acc += cluster_width(prompt_buf_, i, j);
            snap = acc;
            i = j;
        }
        prompt_scroll_ = snap;
    }
    std::string shown;
    int curcol = 0;
    {
        int cw = 0;
        for (size_t i = 0; i < prompt_buf_.size();) {
            size_t j = next_cluster(prompt_buf_, i);
            int w = cluster_width(prompt_buf_, i, j);
            if (cw + w <= prompt_scroll_) { cw += w; i = j; continue; }
            int disp = cw - prompt_scroll_;
            if (disp + w > showw) break;
            if ((int)i < (int)prompt_cx_) curcol = disp + w;
            shown.append(prompt_buf_, i, j - i);
            cw += w;
            i = j;
        }
    }
    prompt_curcol_ = curcol;
    put(s, input_row_, 1 + labw, disp_expand(shown).c_str());
}

void App::render_full() {
    std::string s;
    if (cols_ != last_cols_ || rows_ != last_rows_ || mode_ != last_mode_) {
        s += "\x1b[2J";
        last_cols_ = cols_;
        last_rows_ = rows_;
        last_mode_ = mode_;
    }
    s += "\x1b[?25l";
    draw_title(s);

    Mode cv = content_view();
    if (cv == Mode::Menu)
        draw_menu(s);
    else if (cv == Mode::Settings)
        draw_settings(s);
    else if (cv == Mode::Message)
        draw_message(s);
    else if (cv == Mode::Render)
        draw_render(s);

    draw_keys(s);
    draw_msg(s);
    draw_input(s);

    if (mode_ == Mode::Prompt) {
        int labw = u8width(prompt_label_);
        int col = 1 + labw + prompt_curcol_;
        if (col < 1 + labw) col = 1 + labw;
        if (col > cols_) col = cols_;
        s += sfmt("\x1b[%d;%dH\x1b[?25h", input_row_, col);
    }
    term_.write(s);
}

// -------------------------------------------------------------------- run

int App::run() {
    if (!term_.init()) {
        std::fprintf(stderr, "TermRender: failed to initialize the terminal\n");
        return 1;
    }
    term_.enter_alt_screen();
    term_.hide_cursor();
    term_.query_size(500);
    refresh_size();
    term_.calibrate_emoji(rows_);

    using clock = std::chrono::steady_clock;

    while (running_) {
        poll_size();
        auto now = clock::now();

        // Rendering stays active whenever the *content* is the render view —
        // including while a prompt is overlaid on it (opening/typing in a prompt
        // must not pause or reset the render). Settings/Menu/Message pause it.
        if (renderer_.has_scene() && content_view() == Mode::Render) {
            layout();
            int img_rows = main_bottom_ - main_top_ + 1;
            int img_h = img_rows * 2;
            if (renderer_.width() != cols_ || renderer_.height() != img_h) {
                stop_render();
                renderer_.resize(cols_, img_h);
                frame_buf_.assign((size_t)std::max(1, cols_) * std::max(1, img_h),
                                  Vec3(0, 0, 0));
                need_render_ = true;
                has_image_ = false;
                frame_shown_ = false;
                full_redraw_ = true;
            }

            auto begin_new_accum = [&]() {
                renderer_.reset_accum_keep_display();
                need_render_ = false;
                render_progress_.store(0);
                repainted_progress_ = 0;
                last_repaint_ = now;
            };

            bool repaint = false;
            if (render_active_ && worker_done_.load()) {
                // A batch finished: ALWAYS snapshot and paint it immediately.
                // Only after that do we honour any pending movement/settings
                // change, so a completed frame is never dropped unshown.
                bool ok = worker_ok_.load();
                stop_render();
                if (ok) has_image_ = true;
                frame_buf_ = renderer_.snapshot_display();
                repainted_progress_ = render_progress_.load();
                last_repaint_ = now;
                full_redraw_ = true;
                status_ = sfmt("%s | %dx%d px | %d spp", base_name(loaded_path_).c_str(), cols_,
                               renderer_.height(), renderer_.accumulated_samples());
                if (dbg_log())
                    std::fprintf(stderr,
                                 "[done t=%ld] ok=%d accum=%d has_image=%d frame_shown=%d\n",
                                 dbg_ms(), (int)ok, renderer_.accumulated_samples(),
                                 (int)has_image_, (int)frame_shown_);
                if (need_render_) begin_new_accum();
                start_render();
            } else if (need_render_) {
                // Movement/settings while a batch is still in flight: cancel it
                // immediately and start a fresh accumulation with the new state.
                stop_render();
                begin_new_accum();
                start_render();
                full_redraw_ = true; // keep frame_buf_ (no black flash)
            } else if (!render_active_) {
                last_repaint_ = now;
                start_render();
            }

            // While a (possibly long) batch is tracing, refresh the partial
            // image at least once per second.
            if (!repaint && render_progress_.load() != repainted_progress_) {
                auto since = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 now - last_repaint_)
                                 .count();
                if (since >= 1000) repaint = true;
            }
            if (repaint) {
                frame_buf_ = renderer_.snapshot_display();
                repainted_progress_ = render_progress_.load();
                last_repaint_ = now;
                full_redraw_ = true;
                status_ = sfmt("%s | %dx%d px | %d spp", base_name(loaded_path_).c_str(), cols_,
                               renderer_.height(), renderer_.accumulated_samples());
            }
        } else {
            stop_render();
        }

        layout();

        if (full_redraw_) {
            render_full();
            full_redraw_ = false;
            term_.flush();
            if (dbg_log())
                std::fprintf(stderr, "[draw t=%ld] mode=%d img=%d\n", dbg_ms(), (int)mode_,
                             (int)has_image_);
            // The first complete image is now on screen; from here on movement
            // may interrupt freely.
            if (mode_ == Mode::Render && has_image_) frame_shown_ = true;
        }

        // Short timeout so keys stay responsive while the worker renders.
        KeyEvent ev;
        if (term_.read_key(ev, 50)) {
            handle(ev);
            /* coalesce: drain everything already queued and act once */
            KeyEvent ev2;
            while (term_.read_key(ev2, 0)) handle(ev2);
        } else {
            if (marquee_tick()) {
                std::string s;
                s += "\x1b[?25l";
                draw_title(s);
                draw_msg(s);
                term_.write(s);
                term_.flush();
            }
        }
    }

    stop_render();
    term_.show_cursor();
    term_.leave_alt_screen();
    term_.flush();
    term_.shutdown();
    return 0;
}

RenderSettings App::make_render_settings() const {
    RenderSettings rs;
    rs.max_bounces = settings_.max_bounces;
    rs.samples_per_frame = settings_.samples_per_frame;
    rs.exposure_ev = settings_.exposure_ev;
    rs.env_intensity = settings_.env_intensity;
    rs.clamp_threshold = settings_.clamp_threshold;
    rs.tonemap_mode = settings_.tonemap_mode;
    return rs;
}

void App::start_render() {
    if (render_active_) return;
    RenderSettings rs = make_render_settings();
    CameraState cam = renderer_.camera();
    const EnvMap *env = renderer_.environment();
    render_cancel_.store(false);
    worker_done_.store(false);
    worker_ok_.store(false);
    render_active_ = true;
    if (dbg_log())
        std::fprintf(stderr, "[start] pos=(%.4f,%.4f,%.4f) yaw=%.4f pitch=%.4f fov=%.3f\n",
                     cam.position.x, cam.position.y, cam.position.z, cam.yaw, cam.pitch, cam.fov);
    render_thread_ = std::thread([this, rs, cam, env]() {
        bool ok = renderer_.render_frame(
            rs, cam, env, [this]() { return render_cancel_.load(); },
            [this]() { render_progress_.fetch_add(1); }, &render_cancel_);
        worker_ok_.store(ok);
        worker_done_.store(true);
    });
}

void App::stop_render() {
    if (render_active_) {
        render_cancel_.store(true);
        if (render_thread_.joinable()) render_thread_.join();
        render_active_ = false;
    }
    render_cancel_.store(false);
    worker_done_.store(false);
}

// ----------------------------------------------------------------- handlers

void App::handle(const KeyEvent &ev) {
    // No global key capture: keys are only meaningful in the context/mode that
    // actually lists them (see each handler).
    switch (mode_) {
    case Mode::Menu: handle_menu(ev); break;
    case Mode::Settings: handle_settings(ev); break;
    case Mode::Prompt: handle_prompt(ev); break;
    case Mode::Render: handle_render(ev); break;
    case Mode::Message: handle_message(ev); break;
    }
}

void App::handle_menu(const KeyEvent &ev) {
    switch (ev.key) {
    case Key::Up:
        menu_sel_ = (menu_sel_ + MENU_COUNT - 1) % MENU_COUNT;
        full_redraw_ = true;
        break;
    case Key::Down:
        menu_sel_ = (menu_sel_ + 1) % MENU_COUNT;
        full_redraw_ = true;
        break;
    case Key::Enter:
        if (menu_sel_ == 0) {
            open_prompt(PromptKind::Path, "glTF/GLB path: ", "");
        } else if (menu_sel_ == 1) {
            settings_return_ = Mode::Menu;
            mode_ = Mode::Settings;
            settings_sel_ = 0;
            full_redraw_ = true;
        } else {
            running_ = false;
        }
        break;
    default:
        break;
    }
}

void App::handle_settings(const KeyEvent &ev) {
    auto cycle_color = [&]() { settings_.truecolor = !settings_.truecolor; };
    auto clamp_samples = [&](int d) {
        settings_.samples_per_frame = std::max(1, std::min(64, settings_.samples_per_frame + d));
    };
    auto clamp_bounces = [&](int d) {
        settings_.max_bounces = std::max(1, std::min(32, settings_.max_bounces + d));
    };
    auto clamp_exp = [&](float d) {
        settings_.exposure_ev = clampf(settings_.exposure_ev + d, -10.0f, 10.0f);
    };
    auto clamp_env = [&](float d) {
        settings_.env_intensity = clampf(settings_.env_intensity + d, 0.0f, 8.0f);
    };
    auto clamp_clamp = [&](float d) {
        settings_.clamp_threshold = clampf(settings_.clamp_threshold + d, 0.0f, 100.0f);
    };
    auto cycle_tonemap = [&](int d) {
        settings_.tonemap_mode = (settings_.tonemap_mode + d % 5 + 5) % 5;
    };
    auto leave = [&]() {
        mode_ = settings_return_;
        if (settings_return_ == Mode::Render) need_render_ = true;
        full_redraw_ = true;
    };

    switch (ev.key) {
    case Key::Up:
        settings_sel_ = (settings_sel_ + SETTINGS_COUNT - 1) % SETTINGS_COUNT;
        full_redraw_ = true;
        break;
    case Key::Down:
        settings_sel_ = (settings_sel_ + 1) % SETTINGS_COUNT;
        full_redraw_ = true;
        break;
    case Key::Left:
    case Key::Right: {
        int d = (ev.key == Key::Right) ? 1 : -1;
        switch (settings_sel_) {
        case 0: cycle_color(); break;
        case 1: clamp_samples(d); break;
        case 2: clamp_bounces(d); break;
        case 3: clamp_exp(d * 0.25f); break;
        case 4: clamp_env(d * 0.25f); break;
        case 5: clamp_clamp(d * 0.5f); break;
        case 6: cycle_tonemap(d); break;
        case 7: open_prompt(PromptKind::Background, "background .hdr path: ", env_path_);
                 return;
        case 8: leave(); return;
        }
        full_redraw_ = true;
        break;
    }
    case Key::Enter:
        switch (settings_sel_) {
        case 0: cycle_color(); break;
        case 1: clamp_samples(1); break;
        case 2: clamp_bounces(1); break;
        case 3: clamp_exp(0.25f); break;
        case 4: clamp_env(0.25f); break;
        case 5: settings_.clamp_threshold = settings_.clamp_threshold > 0.0f ? 0.0f : 3.0f;
                break;
        case 6: cycle_tonemap(1); break;
        case 7: open_prompt(PromptKind::Background, "background .hdr path: ", env_path_);
                return;
        case 8: leave(); return;
        }
        full_redraw_ = true;
        break;
    default:
        break;
    }
}

void App::handle_message(const KeyEvent &ev) {
    /* A size report (Key::Resize) must not dismiss the notice. */
    if (ev.key != Key::None && ev.key != Key::Resize && ev.key != Key::Tick) {
        mode_ = base_;
        full_redraw_ = true;
    }
}

void App::open_prompt(PromptKind kind, const std::string &label, const std::string &prefill) {
    if (mode_ != Mode::Prompt) prompt_return_ = mode_;
    prompt_kind_ = kind;
    mode_ = Mode::Prompt;
    prompt_label_ = label;
    prompt_buf_ = prefill;
    prompt_cx_ = prefill.size();
    prompt_scroll_ = 0;
    full_redraw_ = true;
}

void App::handle_prompt(const KeyEvent &ev) {
    switch (ev.key) {
    case Key::Enter:
        commit_prompt();
        break;
    case Key::Esc:
        mode_ = prompt_return_;
        full_redraw_ = true;
        break;
    case Key::Backspace:
        if (prompt_cx_ > 0) {
            size_t k = prev_cluster(prompt_buf_, prompt_cx_);
            prompt_buf_.erase(k, prompt_cx_ - k);
            prompt_cx_ = k;
        }
        full_redraw_ = true;
        break;
    case Key::Delete:
        if (prompt_cx_ < prompt_buf_.size()) {
            size_t k = next_cluster(prompt_buf_, prompt_cx_);
            prompt_buf_.erase(prompt_cx_, k - prompt_cx_);
        }
        full_redraw_ = true;
        break;
    case Key::Left:
        if (prompt_cx_ > 0) prompt_cx_ = prev_cluster(prompt_buf_, prompt_cx_);
        full_redraw_ = true;
        break;
    case Key::Right:
        if (prompt_cx_ < prompt_buf_.size()) prompt_cx_ = next_cluster(prompt_buf_, prompt_cx_);
        full_redraw_ = true;
        break;
    case Key::Home:
        prompt_cx_ = 0;
        full_redraw_ = true;
        break;
    case Key::End:
        prompt_cx_ = prompt_buf_.size();
        full_redraw_ = true;
        break;
    case Key::Char:
        prompt_buf_.insert(prompt_cx_, ev.text);
        prompt_cx_ += ev.text.size();
        full_redraw_ = true;
        break;
    case Key::Space:
        prompt_buf_.insert(prompt_cx_, " ");
        prompt_cx_ += 1;
        full_redraw_ = true;
        break;
    default:
        break;
    }
}

void App::commit_prompt() {
    PromptKind kind = prompt_kind_;
    std::string val = prompt_buf_;
    mode_ = prompt_return_;
    full_redraw_ = true;
    // Note: only prompts that actually affect the image (FOV, background, or a
    // new model path) request a re-render below. Move/turn speed are pure
    // controls and must NOT restart the accumulation.

    if (kind == PromptKind::Path) {
        if (!val.empty()) load_and_render(val);
        return;
    }
    if (kind == PromptKind::Fov) {
        float v = (float)std::atof(val.c_str());
        if (v > 1.0f) renderer_.camera().fov = clampf(radians(v), 0.05f, 2.6f);
        need_render_ = true;
        return;
    }
    if (kind == PromptKind::Speed) {
        float v = (float)std::atof(val.c_str());
        if (v > 0.0f) settings_.move_speed = clampf(v, 0.01f, 100.0f);
        return;
    }
    if (kind == PromptKind::AngleSpeed) {
        float v = (float)std::atof(val.c_str());
        if (v > 0.0f) settings_.angle_speed = clampf(v, 0.01f, 100.0f);
        return;
    }
    if (kind == PromptKind::Background) {
        if (val.empty()) {
            renderer_.clear_environment();
            env_path_.clear();
            status_ = "background: procedural gradient";
        } else {
            EnvMap env;
            std::string err;
            if (load_env_hdr(val, env, err)) {
                renderer_.set_environment(std::move(env));
                env_path_ = val;
                status_ = "background: " + base_name(val);
            } else {
                notice_ = "Failed to load HDR '" + val + "': " + err;
                base_ = Mode::Settings;
                mode_ = Mode::Message;
                full_redraw_ = true;
                return;
            }
        }
        need_render_ = true;
        return;
    }
}

void App::load_and_render(const std::string &path) {
    Scene scene;
    std::string err;
    if (!load_gltf(path, scene, err)) {
        notice_ = "Failed to load '" + path + "': " + err;
        base_ = Mode::Menu;
        mode_ = Mode::Message;
        full_redraw_ = true;
        return;
    }
    stop_render();
    renderer_.set_scene(std::move(scene));
    loaded_path_ = path;
    mode_ = Mode::Render;
    // Drop the previous model's image so a new model never shows a stale frame.
    frame_buf_.clear();
    has_image_ = false;
    frame_shown_ = false;
    need_render_ = true;
    full_redraw_ = true;
    status_ = "loaded " + base_name(path);
}

void App::handle_render(const KeyEvent &ev) {
    // The render view advertises ^Q:exit, ^S:settings, ^C:turn in its key band,
    // so it (and only it) reacts to those control bytes.
    char ch = (ev.key == Key::Char && !ev.text.empty()) ? ev.text[0] : 0;
    if (ch == 0x11) { // ^Q
        stop_render();
        mode_ = Mode::Menu;
        need_render_ = false;
        // Closing the model: drop its image so nothing lingers.
        frame_buf_.clear();
        has_image_ = false;
        frame_shown_ = false;
        full_redraw_ = true;
        return;
    }
    if (ch == 0x13) { // ^S
        settings_return_ = Mode::Render;
        mode_ = Mode::Settings;
        settings_sel_ = 0;
        full_redraw_ = true;
        return;
    }
    if (ch == 0x03) { // ^C
        open_prompt(PromptKind::AngleSpeed, "turn speed x: ",
                    sfmt("%.4g", settings_.angle_speed));
        return;
    }
    if (ev.key == Key::Char) {
        if (ch == 'c' || ch == 'C') {
            open_prompt(PromptKind::Speed, "move speed x: ", sfmt("%.4g", settings_.move_speed));
            return;
        }
        if (ch == 'f' || ch == 'F') {
            open_prompt(PromptKind::Fov, "fov (deg): ",
                        sfmt("%.1f", renderer_.camera().fov * 180.0f / PI));
            return;
        }
    }

    auto &cam = renderer_.camera();
    /* Movement is IGNORED while the current accumulation's first batch is
     * still rendering (no image yet / accumulated==0): that batch is allowed to
     * finish and paint, and the rapid movement keys arriving meanwhile are
     * simply dropped. Once the first batch is done, the next movement key
     * immediately cancels the current render and starts a new one. */
    if (!frame_shown_ || renderer_.accumulated_samples() == 0) return;
    float step = scene_radius(renderer_) * 0.03f * settings_.move_speed;
    const float rot = 0.07f * settings_.angle_speed;
    /* Movement is horizontal: forward/back follow the view yaw but ignore the
     * pitch, so looking up or down never makes you fly. */
    Vec3 fwd = cam.forward();
    Vec3 fwd_h(fwd.x, 0.0f, fwd.z);
    if (length_sq(fwd_h) < 1e-12f) fwd_h = Vec3(0, 0, -1);
    fwd_h = normalize(fwd_h);

    if (ev.key == Key::Up) { cam.pitch = clampf(cam.pitch + rot, -1.55f, 1.55f); need_render_ = true; return; }
    if (ev.key == Key::Down) { cam.pitch = clampf(cam.pitch - rot, -1.55f, 1.55f); need_render_ = true; return; }
    if (ev.key == Key::Left) { cam.yaw -= rot; need_render_ = true; return; }
    if (ev.key == Key::Right) { cam.yaw += rot; need_render_ = true; return; }

    if (ev.key == Key::Char) {
        char c = ev.text.empty() ? 0 : ev.text[0];
        if (dbg_log() && (c == 'w' || c == 'W' || c == 's' || c == 'S' || c == 'a' || c == 'A' ||
                          c == 'd' || c == 'D' || c == 'e' || c == 'E' || c == 'q' || c == 'Q'))
            std::fprintf(stderr, "[move] c=%c pos=(%.4f,%.4f,%.4f)\n", c, cam.position.x,
                         cam.position.y, cam.position.z);
        switch (c) {
        case 'w': case 'W': cam.position += fwd_h * step; need_render_ = true; break;
        case 's': case 'S': cam.position -= fwd_h * step; need_render_ = true; break;
        case 'a': case 'A': cam.position -= cam.right() * step; need_render_ = true; break;
        case 'd': case 'D': cam.position += cam.right() * step; need_render_ = true; break;
        case 'e': case 'E': cam.position.y += step; need_render_ = true; break;
        case 'q': case 'Q': cam.position.y -= step; need_render_ = true; break;
        case '+': case '=': cam.fov = clampf(cam.fov - 0.05f, 0.1f, 2.6f); need_render_ = true; break;
        case '-': cam.fov = clampf(cam.fov + 0.05f, 0.1f, 2.6f); need_render_ = true; break;
        default: break;
        }
    }
}

} // namespace tr
