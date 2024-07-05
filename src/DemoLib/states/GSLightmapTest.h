#pragma once

#include <Ray/RendererBase.h>

#include "../eng/GameState.h"
#include "../ren/Camera.h"
#include "../ren/Program.h"
#include "../ren/Texture.h"

class GameStateManager;
class Viewer;

namespace Sys {
class ThreadPool;
}

namespace Gui {
class BaseElement;
class BitmapFont;
class Renderer;
} // namespace Gui

class GSLightmapTest : public GameState {
    Viewer *viewer_;
    std::weak_ptr<GameStateManager> state_manager_;

    Gui::Renderer *ui_renderer_ = nullptr;
    Gui::BaseElement *ui_root_ = nullptr;
    Gui::BitmapFont *font_ = nullptr;

    Ray::RendererBase *ray_renderer_ = nullptr;
    std::unique_ptr<Ray::SceneBase> ray_scene_;

    Sys::ThreadPool *threads_ = nullptr;

    bool animate_ = false;
    bool view_grabbed_ = false;
    bool view_targeted_ = false;
    Ren::Vec3f view_origin_ = Ren::Vec3f{0, 20, 3}, view_dir_ = Ren::Vec3f{-1, 0, 0},
               view_target_ = Ren::Vec3f{0, 0, 0};

    float max_fwd_speed_, focal_distance_ = 100.0f;

    Ren::Vec3f sun_dir_ = Ren::Vec3f{0, 1, 0};

    bool invalidate_preview_ = true;

    float forward_speed_ = 0, side_speed_ = 0;

    float cur_time_stat_ms_ = 0;

    unsigned int time_acc_ = 0;
    int time_counter_ = 0;

    std::vector<Ray::RendererBase::stats_t> stats_;

    std::vector<Ray::RegionContext> region_contexts_;

    void UpdateRegionContexts();
    void UpdateEnvironment(const Ren::Vec3f &sun_dir);

  public:
    explicit GSLightmapTest(Viewer *viewer);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};