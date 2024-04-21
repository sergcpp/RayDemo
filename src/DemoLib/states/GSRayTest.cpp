#include "GSRayTest.h"

#include <fstream>
#include <iomanip>
#include <vector>

#include <SOIL2/stb_image.h>
#include <SOIL2/stb_image_write.h>

#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#include <tinyexr/tinyexr.h>

#include <Ray/Log.h>
#include <SW/SW.h>
#include <SW/SWframebuffer.h>
#include <Sys/Json.h>
#include <Sys/ScopeExit.h>
#include <Sys/ThreadPool.h>
#include <Sys/Time_.h>

#include "../Viewer.h"
#include "../eng/GameStateManager.h"
#include "../gui/BaseElement.h"
#include "../gui/FontStorage.h"
#include "../gui/Renderer.h"
#include "../load/Load.h"

namespace GSRayTestInternal {
const float FORWARD_SPEED = 8.0f;
} // namespace GSRayTestInternal

GSRayTest::GSRayTest(Viewer *viewer) : viewer_(viewer) {
    state_manager_ = viewer->GetComponent<GameStateManager>(STATE_MANAGER_KEY);

    ui_renderer_ = viewer->ui_renderer.get();
    ui_root_ = viewer->ui_root.get();

    font_ = viewer->ui_fonts->FindFont("main_font");

    ray_renderer_ = viewer->ray_renderer.get();

    threads_ = viewer->threads.get();
}

GSRayTest::~GSRayTest() = default;

void GSRayTest::UpdateRegionContexts() {
    region_contexts_.clear();

    if (viewer_->app_params.threshold != -1 &&
        ray_renderer_->is_spatial_caching_enabled() != viewer_->app_params.use_spatial_cache) {
        std::exit(0);
    }

    const auto rt = ray_renderer_->type();
    int w, h;
    std::tie(w, h) = ray_renderer_->size();

    Ray::unet_filter_properties_t unet_props;
    if (viewer_->app_params.denoise_method == 1) {
        ray_renderer_->InitUNetFilter(true, unet_props);
        unet_denoise_passes_ = unet_props.pass_count;
    } else {
        unet_denoise_passes_ = -1;
    }

    if (Ray::RendererSupportsMultithreading(rt)) {
        const int TileSize = 64;

        for (int y = 0; y < h; y += TileSize) {
            region_contexts_.emplace_back();
            for (int x = 0; x < w; x += TileSize) {
                const auto rect = Ray::rect_t{x, y, std::min(w - x, TileSize), std::min(h - y, TileSize)};

                region_contexts_.back().emplace_back(rect);
            }
        }

        auto render_job = [this](const int i, const int j) {
#if !defined(NDEBUG) && defined(_WIN32)
            unsigned old_value;
            _controlfp_s(&old_value, _EM_INEXACT | _EM_UNDERFLOW | _EM_OVERFLOW, _MCW_EM);
#endif
            ray_renderer_->RenderScene(*ray_scene_, region_contexts_[i][j]);
        };

        auto denoise_job_nlm = [this](const int i, const int j) {
#if !defined(NDEBUG) && defined(_WIN32)
            unsigned old_value;
            _controlfp_s(&old_value, _EM_INEXACT | _EM_UNDERFLOW | _EM_OVERFLOW, _MCW_EM);
#endif
            ray_renderer_->DenoiseImage(region_contexts_[i][j]);
        };

        auto denoise_job_unet = [this](const int pass, const int i, const int j) {
#if !defined(NDEBUG) && defined(_WIN32)
            unsigned old_value;
            _controlfp_s(&old_value, _EM_INEXACT | _EM_UNDERFLOW | _EM_OVERFLOW, _MCW_EM);
#endif
            ray_renderer_->DenoiseImage(pass, region_contexts_[i][j]);
        };

        auto update_cache_job = [this](const int i, const int j) {
#if !defined(NDEBUG) && defined(_WIN32)
            unsigned old_value;
            _controlfp_s(&old_value, _EM_INEXACT | _EM_UNDERFLOW | _EM_OVERFLOW, _MCW_EM);
#endif
            ray_renderer_->UpdateSpatialCache(*ray_scene_, region_contexts_[i][j]);
        };

        std::vector<Sys::SmallVector<short, 128>> render_task_ids;

        render_tasks_ = std::make_unique<Sys::TaskList>();
        render_and_denoise_tasks_ = std::make_unique<Sys::TaskList>();
        update_cache_tasks_ = std::make_unique<Sys::TaskList>();

        for (int i = 0; i < int(region_contexts_.size()); ++i) {
            render_task_ids.emplace_back();
            for (int j = 0; j < int(region_contexts_[i].size()); ++j) {
                render_tasks_->AddTask(render_job, i, j);
                update_cache_tasks_->AddTask(update_cache_job, i, j);
                render_task_ids.back().emplace_back(render_and_denoise_tasks_->AddTask(render_job, i, j));
            }
        }

        std::vector<Sys::SmallVector<short, 128>> denoise_task_ids[16];

        for (int i = 0; i < int(region_contexts_.size()); ++i) {
            denoise_task_ids[0].emplace_back();
            for (int j = 0; j < int(region_contexts_[i].size()); ++j) {
                short id = -1;
                if (viewer_->app_params.denoise_method == 0) {
                    id = render_and_denoise_tasks_->AddTask(denoise_job_nlm, i, j);
                } else if (viewer_->app_params.denoise_method == 1) {
                    id = render_and_denoise_tasks_->AddTask(denoise_job_unet, 0, i, j);
                }

                denoise_task_ids[0].back().push_back(id);

                for (int k = -1; k <= 1; ++k) {
                    if (i + k < 0 || i + k >= int(render_task_ids.size())) {
                        continue;
                    }
                    for (int l = -1; l <= 1; ++l) {
                        if (j + l < 0 || j + l >= int(render_task_ids[i + k].size())) {
                            continue;
                        }
                        render_and_denoise_tasks_->AddDependency(id, render_task_ids[i + k][j + l]);
                    }
                }
            }
        }

        if (viewer_->app_params.denoise_method == 1) {
            for (int pass = 1; pass < unet_props.pass_count; ++pass) {
                for (int y = 0, i = 0; y < h; y += TileSize, ++i) {
                    denoise_task_ids[pass].emplace_back();
                    for (int x = 0, j = 0; x < w; x += TileSize, ++j) {
                        const short id = render_and_denoise_tasks_->AddTask(denoise_job_unet, pass, i, j);

                        denoise_task_ids[pass].back().push_back(id);

                        // Always assume dependency on previous pass
                        for (int k = -1; k <= 1; ++k) {
                            if (i + k < 0 || i + k >= int(denoise_task_ids[pass - 1].size())) {
                                continue;
                            }
                            for (int l = -1; l <= 1; ++l) {
                                if (j + l < 0 || j + l >= int(denoise_task_ids[pass - 1][i + k].size())) {
                                    continue;
                                }
                                render_and_denoise_tasks_->AddDependency(id, denoise_task_ids[pass - 1][i + k][j + l]);
                            }
                        }

                        // Account for aliasing dependency (wait for all tasks which use this memory region)
                        for (int ndx : unet_props.alias_dependencies[pass]) {
                            if (ndx == -1) {
                                break;
                            }
                            for (const auto &deps : denoise_task_ids[ndx]) {
                                for (const short dep : deps) {
                                    render_and_denoise_tasks_->AddDependency(id, dep);
                                }
                            }
                        }
                    }
                }
            }
        }

        render_tasks_->Sort();
        update_cache_tasks_->Sort();
        render_and_denoise_tasks_->Sort();
        assert(!render_and_denoise_tasks_->HasCycles());
    } else {
#if 1
        const auto rect = Ray::rect_t{0, 0, w, h};
        region_contexts_.emplace_back();
        region_contexts_.back().emplace_back(rect);
#else
        const int BucketSize = 64;

        bool skip_tile = false;
        for (int y = 0; y < h; y += BucketSize) {
            skip_tile = !skip_tile;

            region_contexts_.emplace_back();
            for (int x = 0; x < w; x += BucketSize) {
                skip_tile = !skip_tile;
                if (skip_tile) {
                    continue;
                }

                const auto rect = Ray::rect_t{x, y, std::min(w - x, BucketSize), std::min(h - y, BucketSize)};
                region_contexts_.back().emplace_back(rect);
            }
        }
#endif
    }
}

void GSRayTest::UpdateEnvironment(const Ren::Vec3f &sun_dir) {
    /*if (ray_scene_) {
        Ray::environment_desc_t env_desc = {};

        ray_scene_->GetEnvironment(env_desc);

        memcpy(&env_desc.sun_dir[0], ValuePtr(sun_dir), 3 * sizeof(float));

        ray_scene_->SetEnvironment(env_desc);

        invalidate_preview_ = true;
    }*/
}

void GSRayTest::Enter() {
    using namespace Ren;

    max_fwd_speed_ = GSRayTestInternal::FORWARD_SPEED;

    JsObject js_scene;

    {
        std::ifstream in_file(viewer_->app_params.scene_name, std::ios::binary);
        if (!js_scene.Read(in_file)) {
            ray_renderer_->log()->Error("Failed to parse scene file!");
        }
    }

#if 0
    {
        int w = 0, h = 0, channels = 0;
        uint8_t *img_data = stbi_load("moon.png", &w, &h, &channels, 0);

        std::ofstream out_file("src/Ray/internal/precomputed/__moon_tex.inl", std::ios::binary);

        out_file << "extern const int MOON_TEX_W = " << w << ";\n";
        out_file << "extern const int MOON_TEX_H = " << h << ";\n";

        out_file << "extern const uint8_t __moon_tex[" << w * h * channels << "] = {\n    ";
        for (int i = 0; i < w * h * channels; ++i) {
            out_file << int(img_data[i]) << ", ";
        }
        out_file << "\n};\n";

        stbi_image_free(img_data);
    }
#endif

#if 0
    {
        int w = 0, h = 0, channels = 0;
        uint8_t *img_data = stbi_load("cirrus_combined.png", &w, &h, &channels, 0);

        std::ofstream out_file("src/Ray/internal/precomputed/__cirrus_tex.inl", std::ios::binary);

        out_file << "extern const int CIRRUS_TEX_W = " << w << ";\n";
        out_file << "extern const int CIRRUS_TEX_H = " << h << ";\n";

        out_file << "extern const uint8_t __cirrus_tex[" << w * h * 2 << "] = {\n    ";
        for (int i = 0; i < w * h; ++i) {
            out_file << int(img_data[i * channels + 0]) << ", ";
            out_file << int(img_data[i * channels + 1]) << ", ";
        }
        out_file << "\n};\n";

        stbi_image_free(img_data);
    }
#endif

    if (js_scene.Size()) {
        try {
            const uint64_t t1 = Sys::GetTimeMs();
            ray_scene_ = LoadScene(ray_renderer_, js_scene, viewer_->app_params.max_tex_res, threads_,
                                   viewer_->app_params.camera_index);
            const uint64_t t2 = Sys::GetTimeMs();
            ray_renderer_->log()->Info("Scene loaded in %.1fs", double(t2 - t1) * 0.001);
        } catch (std::exception &e) {
            ray_renderer_->log()->Error("%s", e.what());
        }

        // TODO: do not rely on handle being the index!
        current_cam_ = ray_scene_->current_cam();

        if (js_scene.Has("cameras")) {
            const JsObject &js_cam = js_scene.at("cameras").as_arr().at(current_cam_._index).as_obj();
            if (js_cam.Has("view_target")) {
                const JsArray &js_view_target = js_cam.at("view_target").as_arr();

                view_targeted_ = true;
                view_target_[0] = float(js_view_target.at(0).as_num().val);
                view_target_[1] = float(js_view_target.at(1).as_num().val);
                view_target_[2] = float(js_view_target.at(2).as_num().val);
            }

            if (js_cam.Has("fwd_speed")) {
                max_fwd_speed_ = float(js_cam.at("fwd_speed").as_num().val);
            }
        }
    }

    Ray::camera_desc_t cam_desc;
    ray_scene_->GetCamera(current_cam_, cam_desc);

    focal_distance_ = cam_desc.focus_distance;

    cam_desc.max_diff_depth = viewer_->app_params.diff_depth;
    cam_desc.max_spec_depth = viewer_->app_params.spec_depth;
    cam_desc.max_refr_depth = viewer_->app_params.refr_depth;
    cam_desc.max_transp_depth = viewer_->app_params.transp_depth;
    cam_desc.min_total_depth = viewer_->app_params.min_total_depth;
    cam_desc.max_total_depth = total_depth_ = viewer_->app_params.max_total_depth;

    if (viewer_->app_params.clamp_direct.initialized()) {
        cam_desc.clamp_direct = viewer_->app_params.clamp_direct.GetValue();
    }
    if (viewer_->app_params.clamp_indirect.initialized()) {
        cam_desc.clamp_indirect = viewer_->app_params.clamp_indirect.GetValue();
    }

    cam_desc.min_samples = viewer_->app_params.min_samples;
    cam_desc.variance_threshold = viewer_->app_params.variance_threshold;
    cam_desc.regularize_alpha = viewer_->app_params.regularize_alpha;

    ray_scene_->SetCamera(current_cam_, cam_desc);

    memcpy(&view_origin_[0], &cam_desc.origin[0], 3 * sizeof(float));
    memcpy(&view_dir_[0], &cam_desc.fwd[0], 3 * sizeof(float));
    memcpy(&view_up_[0], &cam_desc.up[0], 3 * sizeof(float));

    UpdateRegionContexts();
    test_start_time_ = Sys::GetTimeMs();
}

void GSRayTest::Exit() {}

void GSRayTest::Draw(const uint64_t dt_us) {
    using namespace GSRayTestInternal;

    const uint64_t t1 = Sys::GetTimeMs();
    const auto &app_params = viewer_->app_params;

    if (app_params.ref_name.empty()) { // make sure camera doesn't change during reference tests
        Ray::camera_desc_t cam_desc;
        ray_scene_->GetCamera(current_cam_, cam_desc);

        memcpy(&cam_desc.origin[0], ValuePtr(view_origin_), 3 * sizeof(float));
        memcpy(&cam_desc.fwd[0], ValuePtr(view_dir_), 3 * sizeof(float));
        memcpy(&cam_desc.up[0], ValuePtr(view_up_), 3 * sizeof(float));
        cam_desc.focus_distance = focal_distance_;

        if (invalidate_preview_) {
            if (!app_params.use_spatial_cache) {
                cam_desc.max_total_depth = std::min(1, total_depth_);
            }
            last_invalidate_ = true;
        } else {
            cam_desc.max_total_depth = total_depth_;
            if (last_invalidate_) {
                invalidate_preview_ = true;
                last_invalidate_ = false;
            }
        }

        ray_scene_->SetCamera(current_cam_, cam_desc);

        if (invalidate_preview_ || last_invalidate_) {
            ray_renderer_->Clear({0, 0, 0, 0});
            for (auto &ctxs : region_contexts_) {
                for (auto &ctx : ctxs) {
                    ctx.Clear();
                }
            }
            invalidate_preview_ = false;
            test_start_time_ = Sys::GetTimeMs();
        }
    }

    const auto rt = ray_renderer_->type();

    const bool denoise_image =
        app_params.denoise_after != -1 && region_contexts_[0][0].iteration >= app_params.denoise_after;

    if (Ray::RendererSupportsMultithreading(rt)) {
        if (app_params.use_spatial_cache) {
            // Unfortunatedly has to happen in lockstep with rendering
            threads_->Enqueue(*update_cache_tasks_).wait();

            using namespace std::placeholders;
            ray_renderer_->ResolveSpatialCache(
                *ray_scene_, std::bind(&Sys::ThreadPool::ParallelFor<Ray::ParallelForFunction>, threads_, _1, _2, _3));
        }
        for (int i = 0; i < app_params.iteration_steps; ++i) {
            if (denoise_image && i == app_params.iteration_steps - 1) {
                threads_->Enqueue(*render_and_denoise_tasks_).wait();
            } else {
                threads_->Enqueue(*render_tasks_).wait();
            }
        }
    } else {
        if (app_params.use_spatial_cache) {
            for (auto &regions_row : region_contexts_) {
                for (auto &region : regions_row) {
                    ray_renderer_->UpdateSpatialCache(*ray_scene_, region);
                }
            }
            ray_renderer_->ResolveSpatialCache(*ray_scene_);
        }
        for (int i = 0; i < app_params.iteration_steps; ++i) {
            for (auto &regions_row : region_contexts_) {
                for (auto &region : regions_row) {
                    ray_renderer_->RenderScene(*ray_scene_, region);
                }
            }
            if (denoise_image && i == app_params.iteration_steps - 1) {
                if (unet_denoise_passes_ != -1) {
                    for (int pass = 0; pass < unet_denoise_passes_; ++pass) {
                        for (const auto &regions_row : region_contexts_) {
                            for (const auto &region : regions_row) {
                                ray_renderer_->DenoiseImage(pass, region);
                            }
                        }
                    }
                } else {
                    for (const auto &regions_row : region_contexts_) {
                        for (const auto &region : regions_row) {
                            ray_renderer_->DenoiseImage(region);
                        }
                    }
                }
            }
        }
    }

    { // get stats
        Ray::RendererBase::stats_t st = {};
        ray_renderer_->GetStats(st);
        ray_renderer_->ResetStats();

        if (Ray::RendererSupportsMultithreading(rt)) {
            st.time_primary_ray_gen_us /= threads_->workers_count();
            st.time_primary_trace_us /= threads_->workers_count();
            st.time_primary_shade_us /= threads_->workers_count();
            st.time_primary_shadow_us /= threads_->workers_count();
            st.time_secondary_sort_us /= threads_->workers_count();
            st.time_secondary_trace_us /= threads_->workers_count();
            st.time_secondary_shade_us /= threads_->workers_count();
            st.time_secondary_shadow_us /= threads_->workers_count();
            st.time_denoise_us /= threads_->workers_count();
            st.time_cache_update_us /= threads_->workers_count();
            st.time_cache_resolve_us /= threads_->workers_count();
        }

        stats_.push_back(st);
        if (stats_.size() > 128) {
            stats_.erase(stats_.begin());
        }

        // ray_renderer_->log()->Info("Denoise time: %fms", st.time_denoise_us * 0.001);
    }

    unsigned long long time_total = 0;

    for (const auto &st : stats_) {
        const unsigned long long _time_total =
            st.time_cache_update_us + st.time_cache_resolve_us + st.time_primary_ray_gen_us + st.time_primary_trace_us +
            st.time_primary_shade_us + st.time_primary_shadow_us + st.time_secondary_sort_us +
            st.time_secondary_trace_us + st.time_secondary_shade_us + st.time_secondary_shadow_us + st.time_denoise_us;
        time_total = std::max(time_total, _time_total);
    }

    if (time_total % 5000 != 0) {
        time_total += 5000 - (time_total % 5000);
    }

    int w, h;
    std::tie(w, h) = ray_renderer_->size();
    Ray::color_data_rgba_t pixel_data = ray_renderer_->get_pixels_ref();

#if 0
    const auto *base_color = ray_renderer_->get_aux_pixels_ref(Ray::BaseColor);
    std::vector<Ray::color_rgba_t> base_color_rgba(w * h);
    for (int i = 0; i < w * h; ++i) {
        base_color_rgba[i].v[0] = base_color[i].v[0]; // * 0.5f + 0.5f;
        base_color_rgba[i].v[1] = base_color[i].v[1]; // * 0.5f + 0.5f;
        base_color_rgba[i].v[2] = base_color[i].v[2]; // * 0.5f + 0.5f;
        base_color_rgba[i].v[3] = 1.0f;
    }

    pixel_data = base_color_rgba.data();
#endif

#if 0
    const auto *depth = ray_renderer_->get_aux_pixels_ref(Ray::Depth);
    std::vector<Ray::color_rgba_t> depth_rgba(w * h);
    for (int i = 0; i < w * h; ++i) {
        depth_rgba[i].r = depth[i];
        depth_rgba[i].g = depth[i];
        depth_rgba[i].b = depth[i];
        depth_rgba[i].a = 1.0f;
    }

    pixel_data = depth_rgba.data();
#endif

    swBlitPixels(0, 0, pixel_data.pitch, SW_FLOAT, SW_FRGBA, w, h, (const void *)pixel_data.ptr, 1);

    const double elapsed_time = double(Sys::GetTimeMs() - test_start_time_) / 1000.0;

    bool write_output = region_contexts_[0][0].iteration > 0;
    // write output image periodically
    write_output &= (region_contexts_[0][0].iteration % 128) == 0;
    if (app_params.max_samples != -1) {
        // write output image once target sample count has been reached
        write_output |= (region_contexts_[0][0].iteration == app_params.max_samples);
    }
    if (app_params.time_limit > 0) {
        // write output image if time limit has reached
        write_output |= (int(elapsed_time) >= app_params.time_limit);
    }

    if (write_output) {
        std::string base_name = app_params.scene_name;

        const size_t n1 = base_name.find_last_of('/');
        if (n1 != std::string::npos) {
            base_name = base_name.substr(n1 + 1, std::string::npos);
        }

        const size_t n2 = base_name.rfind('.');
        if (n2 != std::string::npos) {
            base_name = base_name.substr(0, n2);
        }

        // Write tonemapped image
        WritePNG(pixel_data.ptr, pixel_data.pitch, w, h, 3, false /* flip */, (base_name + ".png").c_str());

        if (app_params.output_exr) { // Write untonemapped image
            const auto raw_pixel_data = ray_renderer_->get_raw_pixels_ref();

            std::vector<Ray::color_rgba_t> raw_pixel_data_out(w * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    raw_pixel_data_out[y * w + x] = raw_pixel_data.ptr[y * raw_pixel_data.pitch + x];
                }
            }

            const char *error = nullptr;
            if (TINYEXR_SUCCESS !=
                SaveEXR(&raw_pixel_data_out[0].v[0], w, h, 4, 0, (base_name + ".exr").c_str(), &error)) {
                ray_renderer_->log()->Error("Failed to write %s (%s)", (base_name + ".exr").c_str(), error);
            }

            // TODO: Fix this!!!
            pixel_data = ray_renderer_->get_pixels_ref();
        }

        if (app_params.output_aux) { // Output base color, normals, depth
            const auto base_color = ray_renderer_->get_aux_pixels_ref(Ray::eAUXBuffer::BaseColor);
            WritePNG(base_color.ptr, base_color.pitch, w, h, 3, false /* flip */,
                     (base_name + "_base_color.png").c_str());

            const auto depth_normals = ray_renderer_->get_aux_pixels_ref(Ray::eAUXBuffer::DepthNormals);
            std::vector<Ray::color_rgba_t> depth_normals_rgba(w * h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    depth_normals_rgba[y * w + x].v[0] =
                        depth_normals.ptr[y * depth_normals.pitch + x].v[0] * 0.5f + 0.5f;
                    depth_normals_rgba[y * w + x].v[1] =
                        depth_normals.ptr[y * depth_normals.pitch + x].v[1] * 0.5f + 0.5f;
                    depth_normals_rgba[y * w + x].v[2] =
                        depth_normals.ptr[y * depth_normals.pitch + x].v[2] * 0.5f + 0.5f;
                    depth_normals_rgba[y * w + x].v[3] = depth_normals.ptr[y * depth_normals.pitch + x].v[3] * 0.1f;
                }
            }
            WritePNG(depth_normals_rgba.data(), w, w, h, 4, false /* flip */, (base_name + "_normals.png").c_str());
        }

        ray_renderer_->log()->Info("Written: %s (%i samples)", (base_name + ".png").c_str(),
                                   region_contexts_[0][0].iteration);
    }

    const bool should_compare_result = write_output && !app_params.ref_name.empty();
    if (should_compare_result) {
        const int DiffThres = 32;

        int ref_w, ref_h;
        auto ref_data = Load_stb_image(app_params.ref_name.c_str(), ref_w, ref_h);
        if (!ref_data.empty() && ref_w == w && ref_h == h) {
            int error_pixels = 0;
            double mse = 0.0f;
            for (int j = 0; j < ref_h; ++j) {
                for (int i = 0; i < ref_w; ++i) {
                    const Ray::color_rgba_t &p = pixel_data.ptr[j * pixel_data.pitch + i];

                    const auto r = uint8_t(p.v[0] * 255);
                    const auto g = uint8_t(p.v[1] * 255);
                    const auto b = uint8_t(p.v[2] * 255);

                    const uint8_t diff_r = std::abs(r - ref_data[(ref_h - j - 1) * ref_w + i].v[0]);
                    const uint8_t diff_g = std::abs(g - ref_data[(ref_h - j - 1) * ref_w + i].v[1]);
                    const uint8_t diff_b = std::abs(b - ref_data[(ref_h - j - 1) * ref_w + i].v[2]);

                    if (diff_r > DiffThres || diff_g > DiffThres || diff_b > DiffThres) {
                        ++error_pixels;
                    }

                    mse += diff_r * diff_r;
                    mse += diff_g * diff_g;
                    mse += diff_b * diff_b;
                }
            }

            mse /= 3.0;
            mse /= (ref_w * ref_h);

            double psnr = -10.0 * std::log10(mse / (255.0f * 255.0f));
            psnr = std::floor(psnr * 100.0) / 100.0;

            ray_renderer_->log()->Info("PSNR: %.2f dB, Fireflies: %i (%i samples)", psnr, error_pixels,
                                       region_contexts_[0][0].iteration);
            fflush(stdout);

            if (app_params.threshold != -1 && app_params.max_samples != -1 &&
                region_contexts_[0][0].iteration >= app_params.max_samples) {
                ray_renderer_->log()->Info("Elapsed time: %.2fm", elapsed_time / 60.0);
                if (psnr < app_params.psnr || error_pixels > app_params.threshold) {
                    ray_renderer_->log()->Info("Test failed: PSNR: %.2f/%.2f dB, Fireflies: %i/%i", psnr,
                                               app_params.psnr, error_pixels, app_params.threshold);
                    viewer_->return_code = -1;
                } else {
                    ray_renderer_->log()->Info("Test success: PSNR: %.2f/%.2f dB, Fireflies: %i/%i", psnr,
                                               app_params.psnr, error_pixels, app_params.threshold);
                }
                viewer_->terminated = true;
            }
        }
    }

    if (app_params.time_limit > 0 && int(elapsed_time) >= app_params.time_limit) {
        viewer_->terminated = true;
    }

    if (ui_enabled_ && time_total) {
        const int UiWidth = 196;
        const int UiHeight = 126;

        uint8_t stat_line[UiHeight][3];
        int off_x = (UiWidth - 64) - int(stats_.size());

        for (const auto &st : stats_) {
            const int p0 = int(UiHeight * float(st.time_denoise_us) / float(time_total));
            const int p1 = int(UiHeight * float(st.time_secondary_shadow_us + st.time_denoise_us) / float(time_total));
            const int p2 =
                int(UiHeight * float(st.time_secondary_shade_us + st.time_secondary_shadow_us + st.time_denoise_us) /
                    float(time_total));
            const int p3 = int(UiHeight *
                               float(st.time_secondary_trace_us + st.time_secondary_shade_us +
                                     st.time_secondary_shadow_us + st.time_denoise_us) /
                               float(time_total));
            const int p4 = int(UiHeight *
                               float(st.time_secondary_sort_us + st.time_secondary_trace_us +
                                     st.time_secondary_shade_us + st.time_secondary_shadow_us + st.time_denoise_us) /
                               float(time_total));
            const int p5 =
                int(UiHeight *
                    float(st.time_primary_shadow_us + st.time_secondary_sort_us + st.time_secondary_trace_us +
                          st.time_secondary_shade_us + st.time_secondary_shadow_us + st.time_denoise_us) /
                    float(time_total));
            const int p6 = int(UiHeight *
                               float(st.time_primary_shade_us + st.time_primary_shadow_us + st.time_secondary_sort_us +
                                     st.time_secondary_trace_us + st.time_secondary_shade_us +
                                     st.time_secondary_shadow_us + st.time_denoise_us) /
                               float(time_total));
            const int p7 = int(UiHeight *
                               float(st.time_primary_trace_us + st.time_primary_shade_us + st.time_primary_shadow_us +
                                     st.time_secondary_sort_us + st.time_secondary_trace_us +
                                     st.time_secondary_shade_us + st.time_secondary_shadow_us + st.time_denoise_us) /
                               float(time_total));
            const int p8 =
                int(UiHeight *
                    float(st.time_primary_ray_gen_us + st.time_primary_trace_us + st.time_primary_shade_us +
                          st.time_primary_shadow_us + st.time_secondary_sort_us + st.time_secondary_trace_us +
                          st.time_secondary_shade_us + st.time_secondary_shadow_us + st.time_denoise_us) /
                    float(time_total));
            const int p9 = int(UiHeight *
                               float(st.time_cache_resolve_us + st.time_primary_ray_gen_us + st.time_primary_trace_us +
                                     st.time_primary_shade_us + st.time_primary_shadow_us + st.time_secondary_sort_us +
                                     st.time_secondary_trace_us + st.time_secondary_shade_us +
                                     st.time_secondary_shadow_us + st.time_denoise_us) /
                               float(time_total));
            const int p10 = int(UiHeight *
                                float(st.time_cache_update_us + st.time_cache_resolve_us + st.time_primary_ray_gen_us +
                                      st.time_primary_trace_us + st.time_primary_shade_us + st.time_primary_shadow_us +
                                      st.time_secondary_sort_us + st.time_secondary_trace_us +
                                      st.time_secondary_shade_us + st.time_secondary_shadow_us + st.time_denoise_us) /
                                float(time_total));
            const int l = p10;

            for (int i = 0; i < p0; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 0;
                stat_line[i][2] = 0;
            }

            for (int i = p0; i < p1; i++) {
                stat_line[i][0] = 100;
                stat_line[i][1] = 100;
                stat_line[i][2] = 100;
            }

            for (int i = p1; i < p2; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 255;
                stat_line[i][2] = 255;
            }

            for (int i = p2; i < p3; i++) {
                stat_line[i][0] = 255;
                stat_line[i][1] = 0;
                stat_line[i][2] = 255;
            }

            for (int i = p3; i < p4; i++) {
                stat_line[i][0] = 255;
                stat_line[i][1] = 255;
                stat_line[i][2] = 0;
            }

            for (int i = p4; i < p5; i++) {
                stat_line[i][0] = 100;
                stat_line[i][1] = 100;
                stat_line[i][2] = 100;
            }

            for (int i = p5; i < p6; i++) {
                stat_line[i][0] = 255;
                stat_line[i][1] = 0;
                stat_line[i][2] = 0;
            }

            for (int i = p6; i < p7; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 255;
                stat_line[i][2] = 0;
            }

            for (int i = p7; i < p8; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 0;
                stat_line[i][2] = 255;
            }

            for (int i = p8; i < p9; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 100;
                stat_line[i][2] = 0;
            }

            for (int i = p9; i < p10; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 100;
                stat_line[i][2] = 100;
            }

            if (l) {
                swBlitPixels(180 + off_x, 4 + (UiHeight - l), 0, SW_UNSIGNED_BYTE, SW_RGB, 1, l, &stat_line[0][0], 1);
            }
            off_x++;
        }

        for (int j = 0; j < 78 && !stats_.empty(); ++j) {
            const auto &st = stats_.back();

            const float v0 = float(UiHeight) * float(st.time_denoise_us) / float(time_total);
            const float v1 = float(UiHeight) * float(st.time_secondary_shadow_us) / float(time_total);
            const float v2 = float(UiHeight) * float(st.time_secondary_shade_us) / float(time_total);
            const float v3 = float(UiHeight) * float(st.time_secondary_trace_us) / float(time_total);
            const float v4 = float(UiHeight) * float(st.time_secondary_sort_us) / float(time_total);
            const float v5 = float(UiHeight) * float(st.time_primary_shadow_us) / float(time_total);
            const float v6 = float(UiHeight) * float(st.time_primary_shade_us) / float(time_total);
            const float v7 = float(UiHeight) * float(st.time_primary_trace_us) / float(time_total);
            const float v8 = float(UiHeight) * float(st.time_primary_ray_gen_us) / float(time_total);
            const float v9 = float(UiHeight) * float(st.time_cache_resolve_us) / float(time_total);
            const float v10 = float(UiHeight) * float(st.time_cache_update_us) / float(time_total);

            const float sz = float(UiHeight) / 11.5f;
            const float k = std::min(float(j) / 16.0f, 1.0f);

            const float vv0 = (1.0f - k) * float(v0) + k * sz;
            const float vv1 = (1.0f - k) * float(v1) + k * sz;
            const float vv2 = (1.0f - k) * float(v2) + k * sz;
            const float vv3 = (1.0f - k) * float(v3) + k * sz;
            const float vv4 = (1.0f - k) * float(v4) + k * sz;
            const float vv5 = (1.0f - k) * float(v5) + k * sz;
            const float vv6 = (1.0f - k) * float(v6) + k * sz;
            const float vv7 = (1.0f - k) * float(v7) + k * sz;
            const float vv8 = (1.0f - k) * float(v8) + k * sz;
            const float vv9 = (1.0f - k) * float(v9) + k * sz;
            const float vv10 = (1.0f - k) * float(v10) + k * sz;

            const int p0 = int(vv0);
            const int p1 = int(vv0 + vv1);
            const int p2 = int(vv0 + vv1 + vv2);
            const int p3 = int(vv0 + vv1 + vv2 + vv3);
            const int p4 = int(vv0 + vv1 + vv2 + vv3 + vv4);
            const int p5 = int(vv0 + vv1 + vv2 + vv3 + vv4 + vv5);
            const int p6 = int(vv0 + vv1 + vv2 + vv3 + vv4 + vv5 + vv6);
            const int p7 = int(vv0 + vv1 + vv2 + vv3 + vv4 + vv5 + vv6 + vv7);
            const int p8 = int(vv0 + vv1 + vv2 + vv3 + vv4 + vv5 + vv6 + vv7 + vv8);
            const int p9 = int(vv0 + vv1 + vv2 + vv3 + vv4 + vv5 + vv6 + vv7 + vv8 + vv9);
            const int p10 = int(vv0 + vv1 + vv2 + vv3 + vv4 + vv5 + vv6 + vv7 + vv8 + vv9 + vv10);

            const int l = p10;

            for (int i = 0; i < p0; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 0;
                stat_line[i][2] = 0;
            }

            for (int i = p0; i < p1; i++) {
                stat_line[i][0] = 100;
                stat_line[i][1] = 100;
                stat_line[i][2] = 100;
            }

            for (int i = p1; i < p2; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 255;
                stat_line[i][2] = 255;
            }

            for (int i = p2; i < p3; i++) {
                stat_line[i][0] = 255;
                stat_line[i][1] = 0;
                stat_line[i][2] = 255;
            }

            for (int i = p3; i < p4; i++) {
                stat_line[i][0] = 255;
                stat_line[i][1] = 255;
                stat_line[i][2] = 0;
            }

            for (int i = p4; i < p5; i++) {
                stat_line[i][0] = 100;
                stat_line[i][1] = 100;
                stat_line[i][2] = 100;
            }

            for (int i = p5; i < p6; i++) {
                stat_line[i][0] = 255;
                stat_line[i][1] = 0;
                stat_line[i][2] = 0;
            }

            for (int i = p6; i < p7; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 255;
                stat_line[i][2] = 0;
            }

            for (int i = p7; i < p8; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 0;
                stat_line[i][2] = 255;
            }

            for (int i = p8; i < p9; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 100;
                stat_line[i][2] = 0;
            }

            for (int i = p9; i < p10; i++) {
                stat_line[i][0] = 0;
                stat_line[i][1] = 100;
                stat_line[i][2] = 100;
            }

            if (l) {
                swBlitPixels(180 + off_x, 4 + (UiHeight - l), 0, SW_UNSIGNED_BYTE, SW_RGB, 1, l, &stat_line[0][0], 1);
            }
            off_x++;
        }

        uint8_t hor_line[UiWidth][3];
        memset(&hor_line[0][0], 255, sizeof(hor_line));
        swBlitPixels(180, 4, 0, SW_UNSIGNED_BYTE, SW_RGB, UiWidth, 1, &hor_line[0][0], 1);
    }

    const int dt_ms = int(Sys::GetTimeMs() - t1);
    time_acc_ += dt_ms;
    time_counter_++;

    if (time_counter_ == 20) {
        cur_time_stat_ms_ = float(time_acc_) / float(time_counter_);
        time_acc_ = 0;
        time_counter_ = 0;
    }

    if (ui_enabled_) {
        ui_renderer_->BeginDraw();

        Ray::RendererBase::stats_t st = {};
        ray_renderer_->GetStats(st);

        const float font_height = font_->height(ui_root_);

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
        stats4 += std::to_string(region_contexts_[0][0].iteration);

        std::string stats5;
        stats5 += "time:  ";
        stats5 += std::to_string(cur_time_stat_ms_);
        stats5 += " ms";

        font_->DrawText(ui_renderer_, stats1.c_str(), {-1, 1 - 1 * font_height}, ui_root_);
        font_->DrawText(ui_renderer_, stats2.c_str(), {-1, 1 - 2 * font_height}, ui_root_);
        font_->DrawText(ui_renderer_, stats3.c_str(), {-1, 1 - 3 * font_height}, ui_root_);
        font_->DrawText(ui_renderer_, stats4.c_str(), {-1, 1 - 4 * font_height}, ui_root_);
        font_->DrawText(ui_renderer_, stats5.c_str(), {-1, 1 - 5 * font_height}, ui_root_);

        std::string stats6 = std::to_string(time_total / 1000);
        stats6 += " ms";

        font_->DrawText(ui_renderer_, stats6.c_str(),
                        {-1 + 2 * 135.0f / float(w), 1 - 2 * 4.0f / float(h) - font_height}, ui_root_);

        //
        const float xx = -1.0f + 2.0f * (180.0f + 152.0f) / float(ui_root_->size_px()[0]);
        const auto cur = ui_renderer_->GetParams();
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{1.0f, 1.0f, 1.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "Denoise", {xx, 1 - 2 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{1.0f, 1.0f, 1.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "SecShadow", {xx, 1 - 3 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{1.0f, 0.0f, 0.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "SecShade", {xx, 1 - 4 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{0.0f, 1.0f, 0.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "SecTrace", {xx, 1 - 5 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{0.0f, 0.0f, 1.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "SecSort", {xx, 1 - 6 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{1.0f, 1.0f, 1.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "1stShadow", {xx, 1 - 7 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{0.0f, 1.0f, 1.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "1stShade", {xx, 1 - 8 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{1.0f, 0.0f, 1.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "1stTrace", {xx, 1 - 9 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{1.0f, 1.0f, 0.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "Raygen", {xx, 1 - 10 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{1.0f, 1.0f, 1.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "CacheRES", {xx, 1 - 11 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }
        {
            ui_renderer_->EmplaceParams(Gui::Vec3f{1.0f, 1.0f, 1.0f}, 0.0f, Gui::eBlendMode::BL_ALPHA,
                                        cur.scissor_test());
            font_->DrawText(ui_renderer_, "CacheUPD", {xx, 1 - 12 * font_height}, ui_root_);
            ui_renderer_->PopParams();
        }

        ui_renderer_->EndDraw();
    }
}

void GSRayTest::Update(const uint64_t dt_us) {
    using namespace Ren;

    const float Pi = 3.14159265358979323846f;

    view_up_ = {0, 1, 0};
    Vec3f side = Normalize(Cross(view_dir_, view_up_));

    view_origin_ += view_dir_ * forward_speed_;
    view_origin_ += side * side_speed_;

    invalidate_timeout_ -= int(dt_us / 1000);

    if (forward_speed_ != 0 || side_speed_ != 0 || animate_) {
        invalidate_preview_ = true;
        invalidate_timeout_ = 100;
    } else if (invalidate_timeout_ > 0) {
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
        angle += 0.05f * (0.001f * float(dt_us));

        Mat4f tr(1.0f);
        tr = Translate(tr, Vec3f{0, std::sin(angle * Pi / 180.0f) * 200.0f, 0});
        // tr = math::rotate(tr, math::radians(angle), math::vec3{ 1, 0, 0 });
        // tr = math::rotate(tr, math::radians(angle), math::vec3{ 0, 1, 0 });
        ray_scene_->SetMeshInstanceTransform(Ray::MeshInstanceHandle{1}, ValuePtr(tr));
    }
    //_L = math::normalize(_L);

    //////////////////////////////////////////////////////////////////////////
}

void GSRayTest::HandleInput(const InputManager::Event &evt) {
    using namespace GSRayTestInternal;
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
            view_up_ = {0, 1, 0};
            Vec3f side = Normalize(Cross(view_dir_, view_up_));
            view_up_ = Cross(side, view_dir_);

            Mat4f rot;
            rot = Rotate(rot, -0.01f * evt.move.dx, view_up_);
            rot = Rotate(rot, -0.01f * evt.move.dy, side);

            Mat3f rot_m3 = Mat3f(rot);

            if (!view_targeted_) {
                view_dir_ = rot_m3 * view_dir_;
            } else {
                Vec3f dir = view_origin_ - view_target_;
                dir = rot_m3 * dir;
                view_origin_ = view_target_ + dir;
                view_dir_ = Normalize(-dir);
            }

            // LOGI("%f %f %f", view_origin_[0], view_origin_[1], view_origin_[2]);
            // LOGI("%f %f %f", view_dir_[0], view_dir_[1], view_dir_[2]);

            invalidate_preview_ = true;
            invalidate_timeout_ = 100;
        }
        break;
    case InputManager::RAW_INPUT_MOUSE_WHEEL:
        focal_distance_ += 0.1f * evt.move.dy;
        ray_renderer_->log()->Info("focal distance = %f", focal_distance_);
        invalidate_preview_ = true;
        invalidate_timeout_ = 100;
        break;
    case InputManager::RAW_INPUT_KEY_DOWN: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_UP || evt.raw_key == 'w') {
            forward_speed_ = max_fwd_speed_;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_DOWN || evt.raw_key == 's') {
            forward_speed_ = -max_fwd_speed_;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_LEFT || evt.raw_key == 'a') {
            side_speed_ = -max_fwd_speed_;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_RIGHT || evt.raw_key == 'd') {
            side_speed_ = max_fwd_speed_;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_SPACE) {
            // animate_ = !animate_;
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
        if (evt.key == InputManager::RAW_INPUT_BUTTON_UP || evt.raw_key == 'w') {
            forward_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_DOWN || evt.raw_key == 's') {
            forward_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_LEFT || evt.raw_key == 'a') {
            side_speed_ = 0;
        } else if (evt.key == InputManager::RAW_INPUT_BUTTON_RIGHT || evt.raw_key == 'd') {
            side_speed_ = 0;
        } else if (evt.raw_key == 'u') {
            ui_enabled_ = !ui_enabled_;
        } else if (evt.raw_key == 'n') {
            // toggle denoising
            if (viewer_->app_params.denoise_after != -1) {
                viewer_->app_params.denoise_after = -1;
            } else {
                viewer_->app_params.denoise_after = 1;
            }
            UpdateRegionContexts();
        }
    } break;
    case InputManager::RAW_INPUT_RESIZE:
        ray_renderer_->Resize((int)evt.point.x, (int)evt.point.y);
        UpdateRegionContexts();
        break;
    default:
        break;
    }
}
