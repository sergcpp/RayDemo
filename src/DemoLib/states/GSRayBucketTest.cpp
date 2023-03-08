#include "GSRayBucketTest.h"

#include <fstream>

#if defined(USE_SW_RENDER)
#include <Ren/SW/SW.h>
#include <Ren/SW/SWframebuffer.h>
#endif

#include <Eng/GameStateManager.h>
#include <Ray/Log.h>
#include <Ren/Context.h>
#include <Ren/Utils.h>
#include <Sys/AssetFile.h>
#include <Sys/Json.h>
#include <Sys/Time_.h>
#include <Sys/ThreadPool.h>
#include <Gui/Renderer.h>

#include "../Viewer.h"
#include "../load/Load.h"
#include "../ui/FontStorage.h"

namespace GSRayBucketTestInternal {
const float FORWARD_SPEED = 8.0f;
const int BUCKET_SIZE = 48;
const int PASSES = 1;
const int SPP_PORTION = 256;

// From wikipedia page about Hilbert curve

void rot(int n, int *x, int *y, int rx, int ry) {
    if (ry == 0) {
        if (rx == 1) {
            *x = n-1 - *x;
            *y = n-1 - *y;
        }

        //Swap x and y
        int t  = *x;
        *x = *y;
        *y = t;
    }
}

void d2xy(int n, int d, int *x, int *y) {
    int rx, ry, s, t = d;
    *x = *y = 0;
    for (s = 1; s < n; s *= 2) {
        rx = 1 & (t / 2);
        ry = 1 & (t ^ rx);
        rot(s, x, y, rx, ry);
        *x += s * rx;
        *y += s * ry;
        t /= 4;
    }
}
}

GSRayBucketTest::GSRayBucketTest(GameBase *game) : game_(game) {
    state_manager_  = game->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    ctx_            = game->GetComponent<Ren::Context>(REN_CONTEXT_KEY);

    ui_renderer_    = game->GetComponent<Gui::Renderer>(UI_RENDERER_KEY);
    ui_root_        = game->GetComponent<Gui::BaseElement>(UI_ROOT_KEY);

    const auto fonts = game->GetComponent<FontStorage>(UI_FONTS_KEY);
    font_ = fonts->FindFont("main_font");

    ray_renderer_   = game->GetComponent<Ray::RendererBase>(RAY_RENDERER_KEY);

    threads_        = game->GetComponent<Sys::ThreadPool>(THREAD_POOL_KEY);
}

void GSRayBucketTest::UpdateRegionContexts() {
    using namespace GSRayBucketTestInternal;

    for (size_t i = 0; i < is_aborted_.size(); i++) {
        is_aborted_[i] = true;
    }

    for (const auto &e : events_) {
        e.wait();
    }

    is_aborted_.clear();
    events_.clear();
    region_contexts_.clear();
    last_reg_context_ = 0;
    cur_spp_ = 0;

    const auto rt = ray_renderer_->type();
    const auto sz = ray_renderer_->size();

    if (rt == Ray::RendererRef || rt == Ray::RendererSSE2 || rt == Ray::RendererSSE41 || rt == Ray::RendererAVX || rt == Ray::RendererAVX2 || rt == Ray::RendererNEON) {
        /*for (int y = 0; y < sz.second; y += BUCKET_SIZE) {
            for (int x = 0; x < sz.first; x += BUCKET_SIZE) {
                auto rect = Ray::rect_t{ x, y, 
                    std::min(sz.first - x, BUCKET_SIZE),
                    std::min(sz.second - y, BUCKET_SIZE) };

                region_contexts_.emplace_back(rect);
            }
        }*/

        int resx = sz.first / BUCKET_SIZE + (sz.first % BUCKET_SIZE != 0);
        int resy = sz.second / BUCKET_SIZE + (sz.second % BUCKET_SIZE != 0);

        int res =  std::max(resx, resy);

        // round up to next power of two
        res--;
        res |= res >> 1;
        res |= res >> 2;
        res |= res >> 4;
        res |= res >> 8;
        res |= res >> 16;
        res++;

        for (int i = 0; i < res * res; i++) {
            int x, y;

            d2xy(res, i, &x, &y);

            if (x > resx - 1 || y > resy - 1) continue;

            x *= BUCKET_SIZE;
            y *= BUCKET_SIZE;

            auto rect = Ray::rect_t{ x, y,
                    std::min(sz.first - x, BUCKET_SIZE),
                    std::min(sz.second - y, BUCKET_SIZE) };

            region_contexts_.emplace_back(rect);
        }

    } else {
        auto rect = Ray::rect_t{ 0, 0, sz.first, sz.second };
        region_contexts_.emplace_back(rect);
    }

    is_active_.resize(region_contexts_.size(), false);
    is_aborted_.resize(region_contexts_.size(), false);

    if (rt == Ray::RendererRef || rt == Ray::RendererSSE2 || rt == Ray::RendererSSE41 || rt == Ray::RendererAVX || rt == Ray::RendererAVX2 || rt == Ray::RendererNEON) {
        auto render_job = [this](int i, int m) {
            if (is_aborted_[i]) return;

            {
                auto t = std::chrono::high_resolution_clock::now();

                std::lock_guard<std::mutex> _(timers_mutex_);
                if (start_time_ == std::chrono::high_resolution_clock::time_point{}) {
                    start_time_ = t;
                }
            }

            is_active_[i] = true;
            for (int j = 0; j < SPP_PORTION * m; j++) {
                ray_renderer_->RenderScene(ray_scene_.get(), region_contexts_[i]);
            }
            is_active_[i] = false;

            {
                auto t = std::chrono::high_resolution_clock::now();

                std::lock_guard<std::mutex> _(timers_mutex_);
                end_time_ = t;
            }
        };

        for (int s = 0; s < PASSES; s++) {
            for (int i = 0; i < (int)region_contexts_.size(); i++) {
                events_.push_back(threads_->Enqueue(render_job, i, (1 << s)));
            }
        }
    } else {
        //ray_renderer_->RenderScene(ray_scene_, region_contexts_[0]);
    }
}

void GSRayBucketTest::UpdateEnvironment(const Ren::Vec3f &sun_dir) {
    /*if (ray_scene_) {
        Ray::environment_desc_t env_desc = {};

        ray_scene_->GetEnvironment(env_desc);

        memcpy(&env_desc.sun_dir[0], Ren::ValuePtr(sun_dir), 3 * sizeof(float));

        ray_scene_->SetEnvironment(env_desc);

        invalidate_preview_ = true;
    }*/
}

void GSRayBucketTest::Enter() {
    using namespace Ren;

    JsObject js_scene;

    auto app_params = game_->GetComponent<AppParams>(APP_PARAMS_KEY);

    {
        std::ifstream in_file(app_params->scene_name, std::ios::binary);
        if (!js_scene.Read(in_file)) {
            ray_renderer_->log()->Error("Failed to parse scene file!");
        }
    }

    if (js_scene.Size()) {
        ray_scene_ = LoadScene(ray_renderer_.get(), js_scene, app_params->max_tex_res, threads_.get());

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
    ray_scene_->GetCamera(Ray::CameraHandle{0}, cam_desc);

    memcpy(&view_origin_[0], &cam_desc.origin[0], 3 * sizeof(float));
    memcpy(&view_dir_[0], &cam_desc.fwd[0], 3 * sizeof(float));

    UpdateRegionContexts();
}

void GSRayBucketTest::Exit() {
    for (size_t i = 0; i < is_aborted_.size(); i++) is_aborted_[i] = true;
    for (const auto &e : events_) e.wait();
}

void GSRayBucketTest::Draw(uint64_t dt_us) {
    using namespace GSRayBucketTestInternal;

    //renderer_->ClearColorAndDepth(0, 0, 0, 1);

    {   // update camera
        Ray::camera_desc_t cam_desc;
        ray_scene_->GetCamera(Ray::CameraHandle{0}, cam_desc);

        memcpy(&cam_desc.origin[0], Ren::ValuePtr(view_origin_), 3 * sizeof(float));
        memcpy(&cam_desc.fwd[0], Ren::ValuePtr(view_dir_), 3 * sizeof(float));

        cam_desc.max_refr_depth = 8;
        cam_desc.max_total_depth = 8;

        ray_scene_->SetCamera(Ray::CameraHandle{0}, cam_desc);
    }

    uint64_t t1 = Sys::GetTimeMs();

    if (invalidate_preview_) {
        ray_renderer_->Clear({0, 0, 0, 0});
        UpdateRegionContexts();
        invalidate_preview_ = false;
    }

    //const auto rt = ray_renderer_->type();

    int w, h;

    std::tie(w, h) = ray_renderer_->size();
    const auto *pixel_data = ray_renderer_->get_pixels_ref();

#if defined(USE_SW_RENDER)
    swBlitPixels(0, 0, 0, SW_FLOAT, SW_FRGBA, w, h, (const void *)pixel_data, 1);

    float pix_row[BUCKET_SIZE][4];

    for (int i = 0; i < (int)region_contexts_.size(); i++) {
        if (!is_active_[i]) continue;

        for (int j = 0; j < BUCKET_SIZE; j++) {
            pix_row[j][0] = 1.0f;
            pix_row[j][1] = 1.0f;
            pix_row[j][2] = 1.0f;
            pix_row[j][3] = 1.0f;
        }

        const auto &rc = region_contexts_[i];
        swBlitPixels(rc.rect().x, rc.rect().y, 0, SW_FLOAT, SW_FRGBA, rc.rect().w, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x, rc.rect().y + 1, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x, rc.rect().y + 2, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x, rc.rect().y + 3, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x, rc.rect().y + 4, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x + BUCKET_SIZE - 1, rc.rect().y + 1, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x + BUCKET_SIZE - 1, rc.rect().y + 2, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x + BUCKET_SIZE - 1, rc.rect().y + 3, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x + BUCKET_SIZE - 1, rc.rect().y + 4, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);

        swBlitPixels(rc.rect().x, rc.rect().y + BUCKET_SIZE - 1, 0, SW_FLOAT, SW_FRGBA, rc.rect().w, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x, rc.rect().y + BUCKET_SIZE - 1 - 1, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x, rc.rect().y + BUCKET_SIZE - 1 - 2, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x, rc.rect().y + BUCKET_SIZE - 1 - 3, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x, rc.rect().y + BUCKET_SIZE - 1 - 4, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x + BUCKET_SIZE - 1, rc.rect().y + BUCKET_SIZE - 1 - 1, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x + BUCKET_SIZE - 1, rc.rect().y + BUCKET_SIZE - 1 - 2, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x + BUCKET_SIZE - 1, rc.rect().y + BUCKET_SIZE - 1 - 3, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
        swBlitPixels(rc.rect().x + BUCKET_SIZE - 1, rc.rect().y + BUCKET_SIZE - 1 - 4, 0, SW_FLOAT, SW_FRGBA, 1, 1, (const void *)&pix_row[0][0], 1);
    }
#endif

    bool ready = true;

    for (size_t i = 0; i < events_.size(); i++) {
        if (events_[i].wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) {
            ready = false;
            break;
        }
    }

    if (ready) {
        auto dt = std::chrono::duration<double>{ end_time_ - start_time_ };

        auto result = game_->GetComponent<double>(TEST_RESULT_KEY);
        *result = dt.count();

        //auto sm = state_manager_.lock();
        //sm->PopLater();
    }

    int dt_ms = int(Sys::GetTimeMs() - t1);
    time_acc_ += dt_ms;
    time_counter_++;

    if (time_counter_ == 20) {
        cur_time_stat_ms_ = float(time_acc_) / time_counter_;
        time_acc_ = 0;
        time_counter_ = 0;
    }

#if 0
    {
        // ui draw
        ui_renderer_->BeginDraw();

        Ray::RendererBase::stats_t st = {};
        ray_renderer_->GetStats(st);

        float font_height = font_->height(ui_root_.get());

        std::string stats1;
        stats1 += "res:   ";
        stats1 += std::to_string(ray_renderer_->size().first);
        stats1 += "x";
        stats1 += std::to_string(ray_renderer_->size().second);

        std::string stats2;
        stats2 += "tris:  ";
        stats2 += std::to_string(ray_scene_->triangle_count());

        std::string stats3;
        stats3 += "nodes: ";
        stats3 += std::to_string(ray_scene_->node_count());

        std::string stats4;
        stats4 += "pass:  ";
        stats4 += std::to_string(region_contexts_[0].iteration);

        font_->DrawText(ui_renderer_.get(), stats1.c_str(), { -1, 1 - 1 * font_height }, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), stats2.c_str(), { -1, 1 - 2 * font_height }, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), stats3.c_str(), { -1, 1 - 3 * font_height }, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), stats4.c_str(), { -1, 1 - 4 * font_height }, ui_root_.get());

        ui_renderer_->EndDraw();
    }
#endif

    ctx_->ProcessTasks();
}

void GSRayBucketTest::Update(uint64_t dt_us) {
    using namespace Ren;

    const float Pi = 3.14159265358979323846f;

    Vec3f up = { 0, 1, 0 };
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
        tr = Translate(tr, Vec3f{ 0, std::sin(angle * Pi / 180.0f) * 200.0f, 0 });
        //tr = math::rotate(tr, math::radians(angle), math::vec3{ 1, 0, 0 });
        //tr = math::rotate(tr, math::radians(angle), math::vec3{ 0, 1, 0 });
        ray_scene_->SetMeshInstanceTransform(Ray::MeshInstanceHandle{1}, ValuePtr(tr));
    }
    //_L = math::normalize(_L);

    //////////////////////////////////////////////////////////////////////////


}

void GSRayBucketTest::HandleInput(const InputManager::Event &evt) {
    using namespace GSRayBucketTestInternal;
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
            Vec3f up = { 0, 1, 0 };
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
            Vec3f up = { 1, 0, 0 };
            Vec3f side = Normalize(Cross(sun_dir_, up));
            up = Cross(side, sun_dir_);

            Mat4f rot;
            rot = Rotate(rot, evt.raw_key == 'e' ? 0.025f : -0.025f, up);
            rot = Rotate(rot, evt.raw_key == 'e' ? 0.025f : -0.025f, side);

            Mat3f rot_m3 = Mat3f(rot);

            sun_dir_ = sun_dir_ * rot_m3;

            UpdateEnvironment(sun_dir_);
        }
    }
    break;
    case InputManager::RAW_INPUT_KEY_UP: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_UP) {
            forward_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_DOWN) {
            forward_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_LEFT) {
            side_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_RIGHT) {
            side_speed_ = 0;
        }
    }
    break;
    case InputManager::RAW_INPUT_RESIZE:
        for (size_t i = 0; i < is_aborted_.size(); i++) is_aborted_[i] = true;
        for (const auto &e : events_) e.wait();

        is_aborted_.clear();
        events_.clear();

        ray_renderer_->Resize((int)evt.point.x, (int)evt.point.y);
        UpdateRegionContexts();
        break;
    default:
        break;
    }
}
