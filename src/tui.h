#pragma once
#include "term.h"
#include "term_render.h"
#include "renderer.h"
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>

namespace tr {

struct AppSettings {
    bool truecolor = true;
    int samples_per_frame = 48;
    int max_bounces = 24;
    float exposure_ev = 0.0f;
    float env_intensity = 1.0f;
    float clamp_threshold = 3.0f; // dspbr default; 0 = off
    int tonemap_mode = 0;         // 0 None,1 Reinhard,2 Cineon,3 ACES,4 Uncharted2
    float move_speed = 1.0f;
    float angle_speed = 1.0f;
};

/* TUI modelled on sshfm: reverse-video title bar, content area, reverse-video
 * key-hint band, message row and a single-line prompt. The render view is just
 * another content page: the half-block image fills the content area, with the
 * same title/hints/message/prompt furniture around it. */
class App {
public:
    int run();

private:
    enum class Mode { Menu, Settings, Prompt, Render, Message };
    enum class PromptKind { None, Path, Fov, Speed, AngleSpeed, Background };

    Mode content_view() const { return mode_ == Mode::Prompt ? prompt_return_ : mode_; }

    bool refresh_size();
    void poll_size();
    void layout();
    void render_full();
    bool marquee_tick();

    void draw_title(std::string &s);
    void draw_menu(std::string &s);
    void draw_settings(std::string &s);
    void draw_message(std::string &s);
    void draw_render(std::string &s);
    void draw_keys(std::string &s);
    void draw_msg(std::string &s);
    void draw_input(std::string &s);

    void put(std::string &s, int row, int col, const std::string &text);
    void put_fill(std::string &s, int row, const std::string &text, int w);
    std::string title_text() const;

    void handle(const KeyEvent &ev);
    void handle_menu(const KeyEvent &ev);
    void handle_settings(const KeyEvent &ev);
    void handle_prompt(const KeyEvent &ev);
    void handle_render(const KeyEvent &ev);
    void handle_message(const KeyEvent &ev);

    void open_prompt(PromptKind kind, const std::string &label, const std::string &prefill);
    void commit_prompt();
    void load_and_render(const std::string &path);

    // Rendering runs on a worker thread so this (main) thread keeps handling
    // keys and repainting partial progress. start_render() launches one batch;
    // stop_render() cancels and joins it. render_active_ is main-thread only.
    void start_render();
    void stop_render();
    RenderSettings make_render_settings() const;

    Terminal term_;
    HalfBlockScreen screen_;
    Renderer renderer_;
    AppSettings settings_;

    int cols_ = 80, rows_ = 24;
    int main_cols_ = 80;
    int main_top_ = 3, main_bottom_ = 20;
    int msg_row_ = 23, input_row_ = 24;
    int keylines_ = 1;
    int last_cols_ = -1, last_rows_ = -1;
    Mode last_mode_ = Mode::Menu;
    std::vector<std::string> keyline_cache_;

    Mode mode_ = Mode::Menu;
    Mode base_ = Mode::Menu;          // return target for the Notice page
    Mode prompt_return_ = Mode::Menu; // where a prompt returns on ok/cancel
    Mode settings_return_ = Mode::Menu; // where Settings returns via Back

    int menu_sel_ = 0;
    int settings_sel_ = 0;

    std::string status_;
    std::string notice_;
    int msg_scroll_ = 0;
    int title_scroll_ = 0;
    std::chrono::steady_clock::time_point last_marquee_{};

    std::string prompt_label_;
    std::string prompt_buf_;
    size_t prompt_cx_ = 0;
    int prompt_scroll_ = 0;
    int prompt_curcol_ = 0;
    PromptKind prompt_kind_ = PromptKind::None;

    std::string loaded_path_;
    std::string env_path_;

    bool need_render_ = false;
    // A complete accumulation image exists for the current view.
    bool has_image_ = false;
    // That image has actually been flushed to the terminal. Movement keys are
    // ignored until this is true, so an interruption can never tear the very
    // first frame while it is still being painted.
    bool frame_shown_ = false;
    bool full_redraw_ = true;
    bool running_ = true;
    std::chrono::steady_clock::time_point last_size_poll_{};

    // Worker-thread rendering state.
    std::thread render_thread_;
    std::atomic<bool> render_cancel_{false};
    std::atomic<bool> worker_done_{false};
    std::atomic<bool> worker_ok_{false};
    std::atomic<uint64_t> render_progress_{0};
    uint64_t repainted_progress_ = 0;
    bool render_active_ = false;
    std::chrono::steady_clock::time_point last_repaint_{};
    std::vector<Vec3> frame_buf_; // thread-safe snapshot of the display
};

} // namespace tr
