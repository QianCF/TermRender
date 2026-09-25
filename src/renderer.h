#pragma once
#include "scene.h"
#include "envmap.h"
#include <functional>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>

namespace tr {

struct RenderSettings {
    int max_bounces = 6;
    int samples_per_frame = 1;
    float exposure_ev = 0.0f;
    float env_intensity = 1.0f;
    // dspbr-pt u_clamp_threshold: clamp accumulated radiance to [0, x]. 0 disables.
    float clamp_threshold = 3.0f;
    // 0 None(linear), 1 Reinhard, 2 Cineon, 3 ACES, 4 Uncharted2 (tonemap.frag).
    int tonemap_mode = 0;
    bool gamma = true;            // pow(color, 1/2.2)
    float ray_eps = 1.0e-4f;      // dspbr u_ray_eps
    int max_specular_bounces = 32;
};

struct CameraState {
    Vec3 position{0, 0, 0};
    float yaw = 0.0f;   // radians, around +Y
    float pitch = 0.0f; // radians
    float fov = 0.7f;   // vertical, radians

    Vec3 forward() const {
        return normalize(Vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
                              -std::cos(yaw) * std::cos(pitch)));
    }
    Vec3 right() const { return normalize(cross(forward(), Vec3(0, 1, 0))); }
    Vec3 up() const { return cross(right(), forward()); }
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    void set_scene(Scene scene);
    Scene &scene() { return scene_; }
    const Scene &scene() const { return scene_; }
    bool has_scene() const { return has_scene_; }

    void reset_camera();
    CameraState &camera() { return camera_; }
    const CameraState &camera() const { return camera_; }

    void resize(int width, int height);
    int width() const { return width_; }
    int height() const { return height_; }

    void reset_accum();
    // Like reset_accum(), but keeps the currently displayed image (so a UI can
    // keep showing the last result while a new accumulation builds up).
    void reset_accum_keep_display();
    // Returns false if should_stop requested abort (partial batch is discarded).
    bool render_frame(const RenderSettings &settings,
                      const std::function<bool()> &should_stop = {});
    // Batch render with an explicit camera/environment snapshot, suitable for
    // running on a worker thread. Partial results are published into display()
    // after every row chunk; on_progress() is invoked afterwards. should_stop()
    // is polled between chunks and rows.
    bool render_frame(const RenderSettings &settings, const CameraState &cam, const EnvMap *env,
                      const std::function<bool()> &should_stop = {},
                      const std::function<void()> &on_progress = {},
                      const std::atomic<bool> *cancel_flag = nullptr);
    // Thread-safe copy of the current display buffer.
    std::vector<Vec3> snapshot_display() const;

    // Background: an HDR environment (IBL). When absent a procedural gradient
    // is used instead.
    void set_environment(EnvMap env);
    void clear_environment();
    bool has_environment() const { return has_environment_; }
    // Valid pointer only while has_environment(); used to hand a snapshot to a
    // worker thread (the caller must not mutate it meanwhile).
    const EnvMap *environment() const { return has_environment_ ? &environment_ : nullptr; }

    // Debug: trace a single camera ray for pixel (x,y) and print every bounce
    // to stdout. sample selects the jitter seed.
    void trace_debug(int x, int y, const RenderSettings &settings, int sample);

    // Tonemapped linear RGB display buffer, width*height entries.
    const std::vector<Vec3> &display() const { return display_; }
    int accumulated_samples() const { return accumulated_.load(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Scene scene_;
    CameraState camera_;
    RenderSettings settings_;
    std::vector<Vec3> display_;
    std::vector<Vec3> accum_;
    int width_ = 0, height_ = 0;
    std::atomic<int> accumulated_{0};
    mutable std::mutex display_mutex_;
    bool has_scene_ = false;
    EnvMap environment_;
    bool has_environment_ = false;
};

} // namespace tr
