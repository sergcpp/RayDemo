#include "GSHybTest.h"

#include <fstream>

#if defined(USE_SW_RENDER)
#include <Ren/SW/SW.h>
#include <Ren/SW/SWframebuffer.h>
#endif

#include <Eng/GameStateManager.h>
#include <Gui/Renderer.h>
#include <Ray/RendererFactory.h>
#include <Sys/Json.h>
#include <Sys/Log.h>
#include <Sys/ThreadPool.h>
#include <Sys/Time_.h>

#include "../Viewer.h"
#include "../load/Load.h"
#include "../ui/FontStorage.h"

namespace GSHybTestInternal {
const float FORWARD_SPEED = 0.5f;
}

GSHybTest::GSHybTest(GameBase *game) : game_(game) {
    state_manager_ = game->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    ctx_ = game->GetComponent<Ren::Context>(REN_CONTEXT_KEY);

    ui_renderer_ = game->GetComponent<Gui::Renderer>(UI_RENDERER_KEY);
    ui_root_ = game->GetComponent<Gui::BaseElement>(UI_ROOT_KEY);

    const auto fonts = game->GetComponent<FontStorage>(UI_FONTS_KEY);
    font_ = fonts->FindFont("main_font");

    auto log = game->GetComponent<Ray::ILog>(LOG_KEY);

    Ray::settings_t s;
    s.w = game->width;
    s.h = game->height;
    cpu_tracer_ = std::shared_ptr<Ray::RendererBase>(Ray::CreateRenderer(
        s, log.get(),
        Ray::RendererAVX2 | Ray::RendererAVX | Ray::RendererSSE2 | Ray::RendererSSE41 | Ray::RendererRef));

    threads_ = game->GetComponent<Sys::ThreadPool>(THREAD_POOL_KEY);
}

void GSHybTest::UpdateRegionContexts() {
    gpu_region_contexts_.clear();
    cpu_region_contexts_.clear();

    if (gpu_cpu_div_fac_ > 0.95f)
        gpu_cpu_div_fac_ = 0.95f;

    const int gpu_start_hor = (int)(ctx_->h() * gpu_cpu_div_fac_);

    { // setup gpu renderers
        if (gpu_tracers_.size() == 2) {
            auto rect1 = Ray::rect_t{0, 0, (int)(ctx_->w() * gpu_gpu_div_fac_), gpu_start_hor};
            gpu_region_contexts_.emplace_back(rect1);

            auto rect2 = Ray::rect_t{(int)(ctx_->w() * gpu_gpu_div_fac_), 0,
                                     (int)(ctx_->w() * (1.0f - gpu_gpu_div_fac_)), gpu_start_hor};
            gpu_region_contexts_.emplace_back(rect2);
        } else if (gpu_tracers_.size() == 1) {
            auto rect = Ray::rect_t{0, 0, ctx_->w(), gpu_start_hor};
            gpu_region_contexts_.emplace_back(rect);
        }
    }

    { // setup cpu renderers
        const int BUCKET_SIZE_X = 128, BUCKET_SIZE_Y = 64;

        for (int y = gpu_start_hor; y < ctx_->h(); y += BUCKET_SIZE_Y) {
            for (int x = 0; x < ctx_->w(); x += BUCKET_SIZE_X) {
                auto rect =
                    Ray::rect_t{x, y, std::min(ctx_->w() - x, BUCKET_SIZE_X), std::min(ctx_->h() - y, BUCKET_SIZE_Y)};

                cpu_region_contexts_.emplace_back(rect);
            }
        }
    }

    gpu_cpu_div_fac_dirty_ = false;
    gpu_gpu_div_dac_dirty_ = false;
}

void GSHybTest::UpdateEnvironment(const Ren::Vec3f &sun_dir) {
    /*if (Ray_scene_) {
        Ray::environment_desc_t env_desc = {};

        Ray_scene_->GetEnvironment(env_desc);

        memcpy(&env_desc.sun_dir[0], math::value_ptr(sun_dir), 3 * sizeof(float));

        Ray_scene_->SetEnvironment(env_desc);

        invalidate_preview_ = true;
    }*/
}

void GSHybTest::Enter() {
    using namespace Ren;

#if !defined(DISABLE_OCL)
    /*ocl_platforms_ = Ray::Ocl::QueryPlatforms();

    for (int pi = 0; pi < (int)ocl_platforms_.size(); pi++) {
        for (int di = 0; di < (int)ocl_platforms_[pi].devices.size(); di++) {
            Ray::settings_t s;
            s.w = game_->width;
            s.h = game_->height;
            s.platform_index = pi;
            s.device_index = di;
            auto gpu_tracer = std::shared_ptr<Ray::RendererBase>(Ray::CreateRenderer(s, Ray::RendererOCL));
            if (gpu_tracer->type() == Ray::RendererOCL) {
                gpu_tracers_.push_back(gpu_tracer);
            }
        }
    }*/
#endif

    auto app_params = game_->GetComponent<AppParams>(APP_PARAMS_KEY);

    JsObject js_scene;

    {
        std::ifstream in_file("./assets/scenes/inter.json", std::ios::binary);
        if (!js_scene.Read(in_file)) {
            LOGE("Failed to parse scene file!");
        }
    }

    if (js_scene.Size()) {
        std::vector<std::future<void>> events;
        gpu_scenes_.resize(gpu_tracers_.size());
        for (size_t i = 0; i < gpu_tracers_.size(); i++) {
            events.push_back(threads_->Enqueue(
                [this, &js_scene, app_params](size_t i) {
                    gpu_scenes_[i] = LoadScene(gpu_tracers_[i].get(), js_scene, app_params->max_tex_res);
                },
                i));
        }

        auto app_params = game_->GetComponent<AppParams>(APP_PARAMS_KEY);
        cpu_scene_ = LoadScene(cpu_tracer_.get(), js_scene, app_params->max_tex_res);

        for (auto &e : events) {
            e.wait();
        }

        if (js_scene.Has("camera")) {
            const JsObject &js_cam = js_scene.at("camera").as_obj();
            if (js_cam.Has("view_target")) {
                const JsArray &js_view_target = js_cam.at("view_target").as_arr();

                view_targeted_ = true;
                view_target_[0] = float(js_view_target.at(0).as_num().val);
                view_target_[1] = float(js_view_target.at(1).as_num().val);
                view_target_[2] = float(js_view_target.at(2).as_num().val);
            }
        }
    }

    Ray::camera_desc_t cam_desc;
    // TODO: fix this
    cpu_scene_->GetCamera(Ray::Camera{0}, cam_desc);

    memcpy(&view_origin_[0], &cam_desc.origin[0], 3 * sizeof(float));
    memcpy(&view_dir_[0], &cam_desc.fwd[0], 3 * sizeof(float));

    UpdateRegionContexts();
}

void GSHybTest::Exit() {}

void GSHybTest::Draw(uint64_t dt_us) {
    // renderer_->ClearColorAndDepth(0, 0, 0, 1);

    { // update camera
        Ray::camera_desc_t cam_desc;
        cpu_scene_->GetCamera(Ray::Camera{0}, cam_desc);

        memcpy(&cam_desc.origin[0], Ren::ValuePtr(view_origin_), 3 * sizeof(float));
        memcpy(&cam_desc.fwd[0], Ren::ValuePtr(view_dir_), 3 * sizeof(float));

        for (auto &s : gpu_scenes_) {
            s->SetCamera(Ray::Camera{0}, cam_desc);
        }
        cpu_scene_->SetCamera(Ray::Camera{0}, cam_desc);
    }

    uint64_t t1 = Sys::GetTimeMs();

    if (invalidate_preview_) {
        for (auto &t : gpu_tracers_) {
            t->Clear({0, 0, 0, 0});
        }
        cpu_tracer_->Clear({0, 0, 0, 0});
        UpdateRegionContexts();
        invalidate_preview_ = false;
    }

    { // invoke renderers
        auto gpu_render_job = [this](int i) {
            gpu_tracers_[i]->RenderScene(gpu_scenes_[i].get(), gpu_region_contexts_[i]);
        };
        auto cpu_render_job = [this](int i) { cpu_tracer_->RenderScene(cpu_scene_.get(), cpu_region_contexts_[i]); };

        std::vector<std::future<void>> events;

        for (int i = 0; i < (int)gpu_region_contexts_.size(); i++) {
            events.push_back(threads_->Enqueue(gpu_render_job, i));
        }

        for (int i = 0; i < (int)cpu_region_contexts_.size(); i++) {
            events.push_back(threads_->Enqueue(cpu_render_job, i));
        }

        for (const auto &e : events) {
            e.wait();
        }
    }

    Ray::RendererBase::stats_t st = {};

    unsigned long long cpu_total = 0, gpu_total = 0;

    {
        cpu_tracer_->GetStats(st);
        cpu_tracer_->ResetStats();

        cpu_total = st.time_primary_ray_gen_us + st.time_primary_trace_us + st.time_primary_shade_us +
                    st.time_secondary_sort_us + st.time_secondary_trace_us + st.time_secondary_shade_us;
        cpu_total /= (threads_->workers_count() - 1);
    }

    if (!gpu_gpu_div_dac_dirty_ && gpu_tracers_.size() == 2) {
        Ray::RendererBase::stats_t st1 = {}, st2 = {};
        gpu_tracers_[0]->GetStats(st1);
        gpu_tracers_[1]->GetStats(st2);

        unsigned long long time_total1 = st1.time_primary_ray_gen_us + st1.time_primary_trace_us +
                                         st1.time_primary_shade_us + st1.time_secondary_sort_us +
                                         st1.time_secondary_trace_us + st1.time_secondary_shade_us;

        unsigned long long time_total2 = st2.time_primary_ray_gen_us + st2.time_primary_trace_us +
                                         st2.time_primary_shade_us + st2.time_secondary_sort_us +
                                         st2.time_secondary_trace_us + st2.time_secondary_shade_us;

        if (time_total2 > time_total1) {
            gpu_gpu_div_fac_ += 0.02f;
        } else {
            gpu_gpu_div_fac_ -= 0.02f;
        }

        if (gpu_gpu_div_fac_ < 0.05f)
            gpu_gpu_div_fac_ = 0.05f;
        if (gpu_gpu_div_fac_ > 0.95f)
            gpu_gpu_div_fac_ = 0.95f;

        gpu_gpu_div_dac_dirty_ = true;

        // LOGI("%f %f", 0.001f * time_total1, 0.001f * time_total2);
    }

    {
        for (auto &t : gpu_tracers_) {
            Ray::RendererBase::stats_t _st = {};
            t->GetStats(_st);
            t->ResetStats();

            st.time_primary_ray_gen_us += _st.time_primary_ray_gen_us;
            st.time_primary_trace_us += _st.time_primary_trace_us;
            st.time_primary_shade_us += _st.time_primary_shade_us;
            st.time_secondary_sort_us += _st.time_secondary_sort_us;
            st.time_secondary_trace_us += _st.time_secondary_trace_us;
            st.time_secondary_shade_us += _st.time_secondary_shade_us;

            gpu_total = _st.time_primary_ray_gen_us + _st.time_primary_trace_us + _st.time_primary_shade_us +
                        _st.time_secondary_sort_us + _st.time_secondary_trace_us + _st.time_secondary_shade_us;
        }
    }

    // LOGI("%i %i %f", int(cpu_total / 1000), int(gpu_total / 1000), float(cpu_total)/(cpu_total + gpu_total));
    // gpu_cpu_div_fac_ = float(cpu_total) / (cpu_total + gpu_total);

    if (!gpu_cpu_div_fac_dirty_) {
        if (cpu_total > gpu_total) {
            gpu_cpu_div_fac_ += 0.02f;
        } else {
            gpu_cpu_div_fac_ -= 0.02f;
        }
        gpu_cpu_div_fac_dirty_ = true;
    }

    // LOGI("%f", gpu_cpu_div_fac_);

    st.time_primary_ray_gen_us /= threads_->workers_count();
    st.time_primary_trace_us /= threads_->workers_count();
    st.time_primary_shade_us /= threads_->workers_count();
    st.time_secondary_sort_us /= threads_->workers_count();
    st.time_secondary_trace_us /= threads_->workers_count();
    st.time_secondary_shade_us /= threads_->workers_count();

    stats_.push_back(st);
    if (stats_.size() > 128) {
        stats_.erase(stats_.begin());
    }

    unsigned long long time_total = 0;

    for (const auto &st : stats_) {
        unsigned long long _time_total = st.time_primary_ray_gen_us + st.time_primary_trace_us +
                                         st.time_primary_shade_us + st.time_secondary_sort_us +
                                         st.time_secondary_trace_us + st.time_secondary_shade_us;
        time_total = std::max(time_total, _time_total);
    }

    if (time_total % 5000 != 0) {
        time_total += 5000 - (time_total % 5000);
    }

    int w, h;

    std::tie(w, h) = cpu_tracer_->size();
    const auto *cpu_pixel_data = cpu_tracer_->get_pixels_ref();

#if defined(USE_SW_RENDER)
    for (size_t i = 0; i < gpu_region_contexts_.size(); i++) {
        const auto &r = gpu_region_contexts_[i];
        const auto *gpu_pixel_data = gpu_tracers_[i]->get_pixels_ref();

        const auto rect = r.rect();

        if (draw_limits_) {
            for (int j = rect.y; j < rect.y + rect.h; j++) {
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[j * w + rect.x] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[j * w + rect.x + 1] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[j * w + rect.x + 2] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[j * w + rect.x + rect.w - 1] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[j * w + rect.x + rect.w - 2] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[j * w + rect.x + rect.w - 3] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
            }

            for (int j = rect.x; j < rect.x + rect.w; j++) {
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[rect.y * w + j] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[(rect.y + 1) * w + j] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[(rect.y + 2) * w + j] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[(rect.y + rect.h - 1) * w + j] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[(rect.y + rect.h - 2) * w + j] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
                const_cast<Ray::pixel_color_t *>(gpu_pixel_data)[(rect.y + rect.h - 3) * w + j] =
                    Ray::pixel_color_t{0.0f, 1.0f, 0.0f, 1.0f};
            }
        }

        swBlitPixels(rect.x, rect.y, w, SW_FLOAT, SW_FRGBA, rect.w, rect.h, (const void *)gpu_pixel_data, 1);
    }

    const int gpu_h = gpu_region_contexts_[0].rect().h;

    if (draw_limits_) {
        /*for (const auto &r : cpu_region_contexts_) {
            const auto rect = r.rect();
            for (int j = rect.y; j < rect.y + rect.h; j++) {
                const_cast<Ray::pixel_color_t*>(cpu_pixel_data)[j * w + rect.x] = Ray::pixel_color_t{ 1.0f, 0.0f,
        0.0f, 1.0f }; const_cast<Ray::pixel_color_t*>(cpu_pixel_data)[j * w + rect.x + rect.w - 1] =
        Ray::pixel_color_t{ 1.0f, 0.0f, 0.0f, 1.0f };
            }

            for (int j = rect.x; j < rect.x + rect.w; j++) {
                const_cast<Ray::pixel_color_t*>(cpu_pixel_data)[rect.y * w + j] = Ray::pixel_color_t{ 1.0f, 0.0f,
        0.0f, 1.0f }; const_cast<Ray::pixel_color_t*>(cpu_pixel_data)[(rect.y + rect.h - 1) * w + j] =
        Ray::pixel_color_t{ 1.0f, 0.0f, 0.0f, 1.0f };
            }
        }*/

        for (int j = h - gpu_h; j < h; j++) {
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[j * w] = Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[j * w + 1] = Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[j * w + 2] = Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[j * w + w - 1] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[j * w + w - 2] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[j * w + w - 3] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
        }

        for (int j = 0; j < w; j++) {
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[gpu_h * w + j] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[(gpu_h + 1) * w + j] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[(gpu_h + 2) * w + j] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[(h - 1) * w + j] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[(h - 2) * w + j] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
            const_cast<Ray::pixel_color_t *>(cpu_pixel_data)[(h - 3) * w + j] =
                Ray::pixel_color_t{1.0f, 0.0f, 0.0f, 1.0f};
        }
    }
    swBlitPixels(0, gpu_h, 0, SW_FLOAT, SW_FRGBA, w, h - gpu_h, (const void *)(cpu_pixel_data + w * gpu_h), 1);

    uint8_t stat_line[64][3];
    int off_x = 128 - (int)stats_.size();

    for (const auto &st : stats_) {
        int p0 = (int)(64 * float(st.time_secondary_shade_us) / time_total);
        int p1 = (int)(64 * float(st.time_secondary_trace_us + st.time_secondary_shade_us) / time_total);
        int p2 = (int)(64 * float(st.time_secondary_sort_us + st.time_secondary_trace_us + st.time_secondary_shade_us) /
                       time_total);
        int p3 = (int)(64 *
                       float(st.time_primary_shade_us + st.time_secondary_sort_us + st.time_secondary_trace_us +
                             st.time_secondary_shade_us) /
                       time_total);
        int p4 = (int)(64 *
                       float(st.time_primary_trace_us + st.time_primary_shade_us + st.time_secondary_sort_us +
                             st.time_secondary_trace_us + st.time_secondary_shade_us) /
                       time_total);
        int p5 = (int)(64 *
                       float(st.time_primary_ray_gen_us + st.time_primary_trace_us + st.time_primary_shade_us +
                             st.time_secondary_sort_us + st.time_secondary_trace_us + st.time_secondary_shade_us) /
                       time_total);

        int l = p5;

        for (int i = 0; i < p0; i++) {
            stat_line[i][0] = 0;
            stat_line[i][1] = 255;
            stat_line[i][2] = 255;
        }

        for (int i = p0; i < p1; i++) {
            stat_line[i][0] = 255;
            stat_line[i][1] = 0;
            stat_line[i][2] = 255;
        }

        for (int i = p1; i < p2; i++) {
            stat_line[i][0] = 255;
            stat_line[i][1] = 255;
            stat_line[i][2] = 0;
        }

        for (int i = p2; i < p3; i++) {
            stat_line[i][0] = 255;
            stat_line[i][1] = 0;
            stat_line[i][2] = 0;
        }

        for (int i = p3; i < p4; i++) {
            stat_line[i][0] = 0;
            stat_line[i][1] = 255;
            stat_line[i][2] = 0;
        }

        for (int i = p4; i < p5; i++) {
            stat_line[i][0] = 0;
            stat_line[i][1] = 0;
            stat_line[i][2] = 255;
        }

        swBlitPixels(180 + off_x, 4 + (64 - l), 0, SW_UNSIGNED_BYTE, SW_RGB, 1, l, &stat_line[0][0], 1);
        off_x++;
    }

    uint8_t hor_line[128][3];
    memset(&hor_line[0][0], 255, sizeof(hor_line));
    swBlitPixels(180, 4, 0, SW_UNSIGNED_BYTE, SW_RGB, 128, 1, &hor_line[0][0], 1);
#endif

    int dt_ms = int(Sys::GetTimeMs() - t1);
    time_acc_ += dt_ms;
    time_counter_++;

    if (time_counter_ == 20) {
        cur_time_stat_ms_ = float(time_acc_) / float(time_counter_);
        time_acc_ = 0;
        time_counter_ = 0;
    }

    {
        // ui draw
        ui_renderer_->BeginDraw();

        Ray::RendererBase::stats_t st = {};
        gpu_tracers_[0]->GetStats(st);

        float font_height = font_->height(ui_root_.get());

        std::string stats1;
        stats1 += "res:   ";
        stats1 += std::to_string(gpu_tracers_[0]->size().first);
        stats1 += "x";
        stats1 += std::to_string(gpu_tracers_[0]->size().second);

        std::string stats2;
        stats2 += "tris:  ";
        stats2 += std::to_string(cpu_scene_->triangle_count());

        std::string stats3;
        stats3 += "nodes: ";
        stats3 += std::to_string(cpu_scene_->node_count());

        std::string stats4;
        stats4 += "pass:  ";
        stats4 += std::to_string(gpu_region_contexts_[0].iteration);

        std::string stats5;
        stats5 += "time:  ";
        stats5 += std::to_string(cur_time_stat_ms_);
        stats5 += " ms";

        font_->DrawText(ui_renderer_.get(), stats1.c_str(), {-1, 1 - 1 * font_height}, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), stats2.c_str(), {-1, 1 - 2 * font_height}, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), stats3.c_str(), {-1, 1 - 3 * font_height}, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), stats4.c_str(), {-1, 1 - 4 * font_height}, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), stats5.c_str(), {-1, 1 - 5 * font_height}, ui_root_.get());

        std::string stats6 = std::to_string(time_total / 1000);
        stats6 += " ms";

        font_->DrawText(ui_renderer_.get(), stats6.c_str(), {-1 + 2 * 135.0f / w, 1 - 2 * 4.0f / h - font_height},
                        ui_root_.get());

        ui_renderer_->EndDraw();
    }

    ctx_->ProcessTasks();
}

void GSHybTest::Update(uint64_t dt_us) {
    using namespace Ren;

    const float Pi = 3.14159265358979323846f;

    Vec3f up = {0, 1, 0};
    Vec3f side = Normalize(Cross(view_dir_, up));

    view_origin_ += view_dir_ * forward_speed_;
    view_origin_ += side * side_speed_;

    if (forward_speed_ != 0 || side_speed_ != 0 || animate_) {
        invalidate_preview_ = true;
    }

    //////////////////////////////////////////////////////////////////////////

    if (animate_) {
        /*float dt_s = 0.001f * dt_ms;
        float angle = 0.5f * dt_s;

        math::mat4 rot;
        rot = math::rotate(rot, angle, { 0, 1, 0 });

        math::mat3 rot_m3 = math::mat3(rot);
        _L = _L * rot_m3;*/

        static float angle = 0;
        angle += 0.05f * (0.001f * dt_us);

        Mat4f tr(1.0f);
        tr = Translate(tr, Vec3f{0, std::sin(angle * Pi / 180.0f) * 200.0f, 0});
        // tr = math::rotate(tr, math::radians(angle), math::vec3{ 1, 0, 0 });
        // tr = math::rotate(tr, math::radians(angle), math::vec3{ 0, 1, 0 });
        // gpu_scene_->SetMeshInstanceTransform(1, math::value_ptr(tr));
        cpu_scene_->SetMeshInstanceTransform(Ray::MeshInstance{1}, ValuePtr(tr));
    }
    //_L = math::normalize(_L);

    //////////////////////////////////////////////////////////////////////////
}

void GSHybTest::HandleInput(const InputManager::Event &evt) {
    using namespace GSHybTestInternal;
    using namespace Ren;

    switch (evt.type) {
    case InputManager::RAW_INPUT_P1_DOWN:
        view_grabbed_ = true;
        break;
    case InputManager::RAW_INPUT_P1_UP:
        view_grabbed_ = false;
        break;
    case InputManager::RAW_INPUT_P1_MOVE:
        if (view_grabbed_) {
            Vec3f up = {0, 1, 0};
            Vec3f side = Normalize(Cross(view_dir_, up));
            up = Cross(side, view_dir_);

            Mat4f rot;
            rot = Rotate(rot, 0.01f * evt.move.dx, up);
            rot = Rotate(rot, 0.01f * evt.move.dy, side);

            Mat3f rot_m3 = Mat3f(rot);

            if (!view_targeted_) {
                view_dir_ = view_dir_ * rot_m3;
            } else {
                Vec3f dir = view_origin_ - view_target_;
                dir = dir * rot_m3;
                view_origin_ = view_target_ + dir;
                view_dir_ = Normalize(-dir);
            }

            invalidate_preview_ = true;
        }
        break;
    case InputManager::RAW_INPUT_KEY_DOWN: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_UP) {
            forward_speed_ = FORWARD_SPEED;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_DOWN) {
            forward_speed_ = -FORWARD_SPEED;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_LEFT) {
            side_speed_ = -FORWARD_SPEED;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_RIGHT) {
            side_speed_ = FORWARD_SPEED;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_SPACE) {
            animate_ = !animate_;
        } else if (evt.raw_key == 'e' || evt.raw_key == 'q') {
            Vec3f up = {1, 0, 0};
            Vec3f side = Normalize(Cross(sun_dir_, up));
            up = Cross(side, sun_dir_);

            Mat4f rot;
            rot = Rotate(rot, evt.raw_key == 'e' ? 0.025f : -0.025f, up);
            rot = Rotate(rot, evt.raw_key == 'e' ? 0.025f : -0.025f, side);

            Mat3f rot_m3 = Mat3f(rot);

            sun_dir_ = sun_dir_ * rot_m3;

            UpdateEnvironment(sun_dir_);
        }
    } break;
    case InputManager::RAW_INPUT_KEY_UP: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_UP) {
            forward_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_DOWN) {
            forward_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_LEFT) {
            side_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_RIGHT) {
            side_speed_ = 0;
        } else if (evt.raw_key == 'f') {
            draw_limits_ = !draw_limits_;
            invalidate_preview_ = true;
        }
    } break;
    case InputManager::RAW_INPUT_RESIZE:
        for (auto &t : gpu_tracers_) {
            t->Resize((int)evt.point.x, (int)evt.point.y);
        }
        cpu_tracer_->Resize((int)evt.point.x, (int)evt.point.y);
        UpdateRegionContexts();
        break;
    default:
        break;
    }
}
