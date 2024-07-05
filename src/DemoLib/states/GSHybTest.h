#pragma once

#include <Ray/RendererBase.h>

#include "../eng/GameState.h"
#include "../ren/Camera.h"
#include "../ren/MVec.h"
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

class GSHybTest : public GameState {
    Viewer *viewer_ = nullptr;
    std::weak_ptr<GameStateManager> state_manager_;

    Gui::Renderer *ui_renderer_ = nullptr;
    Gui::BaseElement *ui_root_ = nullptr;
    Gui::BitmapFont *font_ = nullptr;

    std::vector<std::shared_ptr<Ray::RendererBase>> gpu_tracers_;
    std::vector<std::shared_ptr<Ray::SceneBase>> gpu_scenes_;

    std::unique_ptr<Ray::RendererBase> cpu_tracer_;
    std::unique_ptr<Ray::SceneBase> cpu_scene_;

    Sys::ThreadPool *threads_ = nullptr;

    bool animate_ = false;
    bool view_grabbed_ = false;
    bool view_targeted_ = false;
    Ren::Vec3f view_origin_ = Ren::Vec3f{0, 20, 3}, view_dir_ = Ren::Vec3f{-1, 0, 0},
               view_target_ = Ren::Vec3f{0, 0, 0};

    Ren::Vec3f sun_dir_ = Ren::Vec3f{0, 1, 0};

    bool invalidate_preview_ = true;

    float forward_speed_ = 0, side_speed_ = 0;

    float cur_time_stat_ms_ = 0;

    unsigned int time_acc_ = 0;
    int time_counter_ = 0;

    std::vector<Ray::RendererBase::stats_t> stats_;

    float gpu_gpu_div_fac_ = 0.5f;
    float gpu_cpu_div_fac_ = 0.85f;
    bool gpu_cpu_div_fac_dirty_ = false, gpu_gpu_div_dac_dirty_ = false;

    bool draw_limits_ = true;
    std::vector<Ray::RegionContext> gpu_region_contexts_;
    std::vector<Ray::RegionContext> cpu_region_contexts_;

    void UpdateRegionContexts();
    void UpdateEnvironment(const Ren::Vec3f &sun_dir);

  public:
    explicit GSHybTest(Viewer *viewer);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};
