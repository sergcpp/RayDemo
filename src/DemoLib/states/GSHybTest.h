#pragma once

#include <Eng/GameState.h>
#include <Eng/go/Go.h>
#include <Ren/Camera.h>
#include <Ren/MVec.h>
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

class GSHybTest : public GameState {
    GameBase *game_;
    std::weak_ptr<GameStateManager> state_manager_;
    std::shared_ptr<Ren::Context> ctx_;

    std::shared_ptr<Gui::Renderer> ui_renderer_;
    std::shared_ptr<Gui::BaseElement> ui_root_;
    std::shared_ptr<Gui::BitmapFont> font_;

    std::vector<std::shared_ptr<Ray::RendererBase>> gpu_tracers_;
    std::vector<std::shared_ptr<Ray::SceneBase>> gpu_scenes_;

    std::shared_ptr<Ray::RendererBase> cpu_tracer_;
    std::shared_ptr<Ray::SceneBase> cpu_scene_;

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

    std::vector<Ray::RendererBase::stats_t> stats_;

    float gpu_gpu_div_fac_ = 0.5f;
    float gpu_cpu_div_fac_ = 0.85f;
    bool gpu_cpu_div_fac_dirty_ = false, gpu_gpu_div_dac_dirty_ = false;

    bool draw_limits_ = true;
#if !defined(DISABLE_OCL)
    std::vector<Ray::Ocl::Platform> ocl_platforms_;
#endif
    std::vector<Ray::RegionContext> gpu_region_contexts_;
    std::vector<Ray::RegionContext> cpu_region_contexts_;

    void UpdateRegionContexts();
    void UpdateEnvironment(const Ren::Vec3f &sun_dir);
public:
    explicit GSHybTest(GameBase *game);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};
