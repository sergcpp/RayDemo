#include "RendererRef.h"

#include <chrono>
#include <functional>
#include <random>

#include "Halton.h"
#include "SceneRef.h"
#include "UniformIntDistribution.h"

#define DEBUG_ATLAS 0

namespace Ray {
thread_local Ray::Ref::PassData g_per_thread_pass_data;
}

Ray::Ref::Renderer::Renderer(const settings_t &s, ILog *log)
    : log_(log), use_wide_bvh_(s.use_wide_bvh), clean_buf_(s.w, s.h), final_buf_(s.w, s.h), temp_buf_(s.w, s.h) {
    auto rand_func = std::bind(UniformIntDistribution<uint32_t>(), std::mt19937(0));
    permutations_ = Ray::ComputeRadicalInversePermutations(g_primes, PrimesCount, rand_func);
}

Ray::SceneBase *Ray::Ref::Renderer::CreateScene() { return new Ref::Scene(log_, use_wide_bvh_); }

void Ray::Ref::Renderer::RenderScene(const SceneBase *scene, RegionContext &region) {
    const auto s = dynamic_cast<const Ref::Scene *>(scene);
    if (!s) {
        return;
    }

#ifndef NDEBUG
    // unsigned int fp_control_state = _controlfp(_EM_INEXACT, _MCW_EM);
#endif

    const camera_t &cam = s->cams_[s->current_cam()].cam;

    scene_data_t sc_data;

    sc_data.env = &s->env_;
    sc_data.mesh_instances = s->mesh_instances_.empty() ? nullptr : &s->mesh_instances_[0];
    sc_data.mi_indices = s->mi_indices_.empty() ? nullptr : &s->mi_indices_[0];
    sc_data.meshes = s->meshes_.empty() ? nullptr : &s->meshes_[0];
    sc_data.transforms = s->transforms_.empty() ? nullptr : &s->transforms_[0];
    sc_data.vtx_indices = s->vtx_indices_.empty() ? nullptr : &s->vtx_indices_[0];
    sc_data.vertices = s->vertices_.empty() ? nullptr : &s->vertices_[0];
    sc_data.nodes = s->nodes_.empty() ? nullptr : &s->nodes_[0];
    sc_data.mnodes = s->mnodes_.empty() ? nullptr : &s->mnodes_[0];
    sc_data.tris = s->tris_.empty() ? nullptr : &s->tris_[0];
    sc_data.tri_indices = s->tri_indices_.empty() ? nullptr : &s->tri_indices_[0];
    sc_data.mtris = s->mtris_.data();
    sc_data.tri_materials = s->tri_materials_.empty() ? nullptr : &s->tri_materials_[0];
    sc_data.materials = s->materials_.empty() ? nullptr : &s->materials_[0];
    sc_data.textures = s->textures_.empty() ? nullptr : &s->textures_[0];
    sc_data.lights = s->lights_.empty() ? nullptr : &s->lights_[0];
    sc_data.li_indices = {s->li_indices_.data(), s->li_indices_.size()};
    sc_data.visible_lights = {s->visible_lights_.data(), s->visible_lights_.size()};

    const uint32_t macro_tree_root = s->macro_nodes_root_;

    const TextureAtlasBase *tex_atlases[] = {&s->tex_atlas_rgba_, &s->tex_atlas_rgb_, &s->tex_atlas_rg_,
                                             &s->tex_atlas_r_};

    float root_min[3], cell_size[3];
    if (macro_tree_root != 0xffffffff) {
        float root_max[3];

        if (sc_data.mnodes) {
            const mbvh_node_t &root_node = sc_data.mnodes[macro_tree_root];

            root_min[0] = root_min[1] = root_min[2] = MAX_DIST;
            root_max[0] = root_max[1] = root_max[2] = -MAX_DIST;

            if (root_node.child[0] & LEAF_NODE_BIT) {
                ITERATE_3({ root_min[i] = root_node.bbox_min[i][0]; })
                ITERATE_3({ root_max[i] = root_node.bbox_max[i][0]; })
            } else {
                for (int j = 0; j < 8; j++) {
                    if (root_node.child[j] == 0x7fffffff) {
                        continue;
                    }

                    ITERATE_3({ root_min[i] = root_node.bbox_min[i][j]; })
                    ITERATE_3({ root_max[i] = root_node.bbox_max[i][j]; })
                }
            }
        } else {
            const bvh_node_t &root_node = sc_data.nodes[macro_tree_root];

            ITERATE_3({ root_min[i] = root_node.bbox_min[i]; })
            ITERATE_3({ root_max[i] = root_node.bbox_max[i]; })
        }

        ITERATE_3({ cell_size[i] = (root_max[i] - root_min[i]) / 255; })
    }

    const int w = final_buf_.w(), h = final_buf_.h();

    rect_t rect = region.rect();
    if (rect.w == 0 || rect.h == 0) {
        rect = {0, 0, w, h};
    }

    region.iteration++;
    if (!region.halton_seq || region.iteration % HALTON_SEQ_LEN == 0) {
        UpdateHaltonSequence(region.iteration, region.halton_seq);
    }

    PassData &p = g_per_thread_pass_data;

    {
        std::lock_guard<std::mutex> _(mtx_);

        // allocate sh data on demand
        if (cam.pass_settings.flags & OutputSH) {
            temp_buf_.Resize(w, h, true);
            clean_buf_.Resize(w, h, true);
        }
    }

    pass_info_t pass_info;

    pass_info.iteration = region.iteration;
    pass_info.bounce = 0;
    pass_info.settings = cam.pass_settings;
    pass_info.settings.max_total_depth = std::min(pass_info.settings.max_total_depth, uint8_t(MAX_BOUNCES));

    const auto time_start = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> time_after_ray_gen;

    const uint32_t hi = (region.iteration & (HALTON_SEQ_LEN - 1)) * HALTON_COUNT;

    if (cam.type != Geo) {
        GeneratePrimaryRays(region.iteration, cam, rect, w, h, &region.halton_seq[hi], p.primary_rays);

        time_after_ray_gen = std::chrono::high_resolution_clock::now();

        p.intersections.resize(p.primary_rays.size());

        for (size_t i = 0; i < p.primary_rays.size(); i++) {
            const ray_data_t &r = p.primary_rays[i];
            hit_data_t &inter = p.intersections[i];
            inter = {};

            if (macro_tree_root != 0xffffffff) {
                if (sc_data.mnodes) {
                    Traverse_MacroTree_WithStack_ClosestHit(
                        r.o, r.d, sc_data.mnodes, macro_tree_root, sc_data.mesh_instances, sc_data.mi_indices,
                        sc_data.meshes, sc_data.transforms, sc_data.mtris, sc_data.tri_indices, inter);
                } else {
                    Traverse_MacroTree_WithStack_ClosestHit(
                        r.o, r.d, sc_data.nodes, macro_tree_root, sc_data.mesh_instances, sc_data.mi_indices,
                        sc_data.meshes, sc_data.transforms, sc_data.tris, sc_data.tri_indices, inter);
                }
                // IntersectAreaLights(r, sc_data.lights, sc_data.visible_lights, sc_data.transforms, inter);
            }
        }
    } else {
        const mesh_instance_t &mi = sc_data.mesh_instances[cam.mi_index];
        SampleMeshInTextureSpace(region.iteration, cam.mi_index, cam.uv_index, sc_data.meshes[mi.mesh_index],
                                 sc_data.transforms[mi.tr_index], sc_data.vtx_indices, sc_data.vertices, rect, w, h,
                                 &region.halton_seq[hi], p.primary_rays, p.intersections);

        time_after_ray_gen = std::chrono::high_resolution_clock::now();
    }

    const auto time_after_prim_trace = std::chrono::high_resolution_clock::now();

    p.secondary_rays.resize(rect.w * rect.h);
    p.shadow_rays.resize(rect.w * rect.h);

    int secondary_rays_count = 0, shadow_rays_count = 0;

    for (size_t i = 0; i < p.intersections.size(); i++) {
        const ray_data_t &r = p.primary_rays[i];
        const hit_data_t &inter = p.intersections[i];

        const int x = (r.xy >> 16) & 0x0000ffff;
        const int y = r.xy & 0x0000ffff;

        const int px_index = y * w + x;

        const pixel_color_t col = ShadeSurface(
            px_index, pass_info, inter, r, &region.halton_seq[hi + RAND_DIM_BASE_COUNT], sc_data, macro_tree_root,
            tex_atlases, &p.secondary_rays[0], &secondary_rays_count, &p.shadow_rays[0], &shadow_rays_count);
        temp_buf_.SetPixel(x, y, col);
    }

    for (int i = 0; i < shadow_rays_count; ++i) {
        const shadow_ray_t &sh_r = p.shadow_rays[i];

        const int x = (sh_r.xy >> 16) & 0x0000ffff;
        const int y = sh_r.xy & 0x0000ffff;

        const int px_index = y * w + x;

        const float visibility = ComputeVisibility(sh_r.o, sh_r.d, sh_r.dist,
                                                   region.halton_seq[hi + RAND_DIM_BASE_COUNT + RAND_DIM_BSDF_PICK],
                                                   hash(px_index), sc_data, macro_tree_root, tex_atlases);
        pixel_color_t col;
        col.r = visibility * sh_r.c[0];
        col.g = visibility * sh_r.c[1];
        col.b = visibility * sh_r.c[2];
        col.a = 0.0f;

        temp_buf_.AddPixel(x, y, col);
    }

    const auto time_after_prim_shade = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> secondary_sort_time{}, secondary_trace_time{}, secondary_shade_time{};

    p.hash_values.resize(secondary_rays_count);
    // p.head_flags.resize(secondary_rays_count);
    p.scan_values.resize(secondary_rays_count);
    p.chunks.resize(secondary_rays_count);
    p.chunks_temp.resize(secondary_rays_count);
    // p.skeleton.resize(secondary_rays_count);

    if (cam.pass_settings.flags & OutputSH) {
        temp_buf_.ResetSampleData(rect);
        for (int i = 0; i < secondary_rays_count; i++) {
            const ray_data_t &r = p.secondary_rays[i];

            const int x = (r.xy >> 16) & 0x0000ffff;
            const int y = r.xy & 0x0000ffff;

            temp_buf_.SetSampleDir(x, y, r.d[0], r.d[1], r.d[2]);
            // sample weight for indirect lightmap has all r.c[0..2]'s set to same value
            temp_buf_.SetSampleWeight(x, y, r.c[0]);
        }
    }

    for (int bounce = 1; bounce <= pass_info.settings.max_total_depth && secondary_rays_count &&
                         !(pass_info.settings.flags & SkipIndirectLight);
         ++bounce) {
        const auto time_secondary_sort_start = std::chrono::high_resolution_clock::now();

        SortRays_CPU(&p.secondary_rays[0], size_t(secondary_rays_count), root_min, cell_size, &p.hash_values[0],
                     &p.scan_values[0], &p.chunks[0], &p.chunks_temp[0]);

#if 0 // debug hash values
        static std::vector<simd_fvec3> color_table;
        if (color_table.empty()) {
            for (int i = 0; i < 1024; i++) {
                color_table.emplace_back(float(rand()) / RAND_MAX, float(rand()) / RAND_MAX, float(rand()) / RAND_MAX);
            }
        }

        for (int i = 0; i < secondary_rays_count; i++) {
            const ray_data_t &r = p.secondary_rays[i];

            const int x = r.id.x;
            const int y = r.id.y;

            const simd_fvec3 &c = color_table[hash(p.hash_values[i]) % 1024];

            pixel_color_t col = { c[0], c[1], c[2], 1.0f };
            temp_buf_.SetPixel(x, y, col);
        }
#endif

        const auto time_secondary_trace_start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < secondary_rays_count; i++) {
            const ray_data_t &r = p.secondary_rays[i];
            hit_data_t &inter = p.intersections[i];
            inter = {};

            if (sc_data.mnodes) {
                Traverse_MacroTree_WithStack_ClosestHit(r.o, r.d, sc_data.mnodes, macro_tree_root,
                                                        sc_data.mesh_instances, sc_data.mi_indices, sc_data.meshes,
                                                        sc_data.transforms, sc_data.mtris, sc_data.tri_indices, inter);
            } else {
                Traverse_MacroTree_WithStack_ClosestHit(r.o, r.d, sc_data.nodes, macro_tree_root,
                                                        sc_data.mesh_instances, sc_data.mi_indices, sc_data.meshes,
                                                        sc_data.transforms, sc_data.tris, sc_data.tri_indices, inter);
            }
            if (r.ray_depth & 0x00ffffff) { // not only a transparency ray
                IntersectAreaLights(r, sc_data.lights, sc_data.visible_lights, sc_data.transforms, inter);
            }
        }

        const auto time_secondary_shade_start = std::chrono::high_resolution_clock::now();

        int rays_count = secondary_rays_count;
        secondary_rays_count = 0;
        shadow_rays_count = 0;
        std::swap(p.primary_rays, p.secondary_rays);

        pass_info.bounce = bounce;

        for (int i = 0; i < rays_count; ++i) {
            const ray_data_t &r = p.primary_rays[i];
            const hit_data_t &inter = p.intersections[i];

            const int x = (r.xy >> 16) & 0x0000ffff;
            const int y = r.xy & 0x0000ffff;

            const int px_index = y * w + x;

            pixel_color_t col = ShadeSurface(
                px_index, pass_info, inter, r,
                &region.halton_seq[hi + RAND_DIM_BASE_COUNT + bounce * RAND_DIM_BOUNCE_COUNT], sc_data, macro_tree_root,
                tex_atlases, &p.secondary_rays[0], &secondary_rays_count, &p.shadow_rays[0], &shadow_rays_count);
            col.a = 0.0f;

            temp_buf_.AddPixel(x, y, col);
        }

        for (int i = 0; i < shadow_rays_count; ++i) {
            const shadow_ray_t &sh_r = p.shadow_rays[i];

            const int x = (sh_r.xy >> 16) & 0x0000ffff;
            const int y = sh_r.xy & 0x0000ffff;

            const int px_index = y * w + x;

            const float visibility = ComputeVisibility(
                sh_r.o, sh_r.d, sh_r.dist,
                region.halton_seq[hi + RAND_DIM_BASE_COUNT + bounce * RAND_DIM_BOUNCE_COUNT + RAND_DIM_BSDF_PICK],
                hash(px_index), sc_data, macro_tree_root, tex_atlases);
            pixel_color_t col;
            col.r = visibility * sh_r.c[0];
            col.g = visibility * sh_r.c[1];
            col.b = visibility * sh_r.c[2];
            col.a = 0.0f;

            temp_buf_.AddPixel(x, y, col);
        }

        const auto time_secondary_shade_end = std::chrono::high_resolution_clock::now();
        secondary_sort_time +=
            std::chrono::duration<double, std::micro>{time_secondary_trace_start - time_secondary_sort_start};
        secondary_trace_time +=
            std::chrono::duration<double, std::micro>{time_secondary_shade_start - time_secondary_trace_start};
        secondary_shade_time +=
            std::chrono::duration<double, std::micro>{time_secondary_shade_end - time_secondary_shade_start};
    }

    {
        std::lock_guard<std::mutex> _(mtx_);

        stats_.time_primary_ray_gen_us +=
            (unsigned long long)std::chrono::duration<double, std::micro>{time_after_ray_gen - time_start}.count();
        stats_.time_primary_trace_us +=
            (unsigned long long)std::chrono::duration<double, std::micro>{time_after_prim_trace - time_after_ray_gen}
                .count();
        stats_.time_primary_shade_us +=
            (unsigned long long)std::chrono::duration<double, std::micro>{time_after_prim_shade - time_after_prim_trace}
                .count();
        stats_.time_secondary_sort_us += (unsigned long long)secondary_sort_time.count();
        stats_.time_secondary_trace_us += (unsigned long long)secondary_trace_time.count();
        stats_.time_secondary_shade_us += (unsigned long long)secondary_shade_time.count();
    }

    // factor used to compute incremental average
    const float mix_factor = 1.0f / region.iteration;

    clean_buf_.MixWith(temp_buf_, rect, mix_factor);
    if (cam.pass_settings.flags & OutputSH) {
        temp_buf_.ComputeSHData(rect);
        clean_buf_.MixWith_SH(temp_buf_, rect, mix_factor);
    }

    auto clamp_and_gamma_correct = [&cam](const pixel_color_t &p) {
        auto c = simd_fvec4{&p.r};

        if (cam.dtype == SRGB) {
            ITERATE_3({
                if (c[i] < 0.0031308f) {
                    c[i] = 12.92f * c[i];
                } else {
                    c[i] = 1.055f * std::pow(c[i], (1.0f / 2.4f)) - 0.055f;
                }
            })
        }

        if (cam.gamma != 1.0f) {
            c = pow(c, simd_fvec4{1.0f / cam.gamma});
        }

        if (cam.pass_settings.flags & Clamp) {
            c = clamp(c, 0.0f, 1.0f);
        }
        return pixel_color_t{c[0], c[1], c[2], c[3]};
    };

    final_buf_.CopyFrom(clean_buf_, rect, clamp_and_gamma_correct);

#if DEBUG_ATLAS
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        const float v = float(y) / final_buf_.h();
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            const float u = float(x) / final_buf_.w();

            const auto col8 = s->tex_atlas_rgb_.Get(region.iteration % s->tex_atlas_rgb_.page_count(), u, v);

            pixel_color_t col;
            col.r = float(col8.v[0]) / 255.0f;
            col.g = float(col8.v[1]) / 255.0f;
            col.b = float(col8.v[2]) / 255.0f;
            col.a = 1.0f;

            final_buf_.SetPixel(x, y, col);
        }
    }
#endif
}

void Ray::Ref::Renderer::UpdateHaltonSequence(const int iteration, std::unique_ptr<float[]> &seq) {
    if (!seq) {
        seq.reset(new float[HALTON_COUNT * HALTON_SEQ_LEN]);
    }

    for (int i = 0; i < HALTON_SEQ_LEN; ++i) {
        uint32_t prime_sum = 0;
        for (int j = 0; j < HALTON_COUNT; ++j) {
            seq[i * HALTON_COUNT + j] =
                Ray::ScrambledRadicalInverse(g_primes[j], &permutations_[prime_sum], uint64_t(iteration + i));
            prime_sum += g_primes[j];
        }
    }
}