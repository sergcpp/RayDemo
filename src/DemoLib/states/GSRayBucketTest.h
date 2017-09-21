#pragma once

#include <chrono>
#include <future>
#include <mutex>

#include <Eng/GameState.h>
#include <Eng/go/Go.h>
#include <Ren/Camera.h>
#include <Ren/Program.h>
#include <Ren/Texture.h>

#include <Ray/RendererBase.h>

class GameBase;
class GameStateManager;
class FontStorage;

namespace Sys {
class ThreadPool;
}

namespace Gui {
class BaseElement;
class BitmapFont;
class Renderer;
}

class GSRayBucketTest : public GameState {
    GameBase *game_;
    std::weak_ptr<GameStateManager> state_manager_;
    std::shared_ptr<Ren::Context> ctx_;

    std::shared_ptr<Gui::Renderer> ui_renderer_;
    std::shared_ptr<Gui::BaseElement> ui_root_;
    std::shared_ptr<Gui::BitmapFont> font_;

    std::shared_ptr<Ray::RendererBase> ray_renderer_;
    std::shared_ptr<Ray::SceneBase> ray_scene_;

    std::shared_ptr<Sys::ThreadPool> threads_;

    bool animate_ = false;
    bool view_grabbed_ = false;
    bool view_targeted_ = false;
    Ren::Vec3f view_origin_ = { 0, 20, 3 },
               view_dir_ = { -1, 0, 0 },
               view_target_ = { 0, 0, 0 };

    Ren::Vec3f sun_dir_ = { 0, 1, 0 };

    bool invalidate_preview_ = true;

    float forward_speed_ = 0, side_speed_ = 0;

    float cur_time_stat_ms_ = 0;

    unsigned int time_acc_ = 0;
    int time_counter_ = 0;

    std::vector<Ray::RegionContext> region_contexts_;
    std::vector<std::future<void>> events_;

    // leads to race condition, but doesnt matter
    std::vector<bool> is_active_, is_aborted_;

    int last_reg_context_ = 0;
    int cur_spp_ = 0;

    std::mutex timers_mutex_;
    std::chrono::high_resolution_clock::time_point start_time_, end_time_;

    void UpdateRegionContexts();
    void UpdateEnvironment(const Ren::Vec3f &sun_dir);
public:
    explicit GSRayBucketTest(GameBase *game);

    GSRayBucketTest(const GSRayBucketTest &rhs) = delete;

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};