#include "RendererRef.h"

#include <chrono>
#include <functional>
#include <random>

#include "Halton.h"
#include "SceneRef.h"

Ray::Ref::Renderer::Renderer(const settings_t &s) : use_wide_bvh_(s.use_wide_bvh), clean_buf_(s.w, s.h), final_buf_(s.w, s.h), temp_buf_(s.w, s.h) {
    auto rand_func = std::bind(std::uniform_int_distribution<int>(), std::mt19937(0));
    permutations_ = Ray::ComputeRadicalInversePermutations(g_primes, PrimesCount, rand_func);
}

std::shared_ptr<Ray::SceneBase> Ray::Ref::Renderer::CreateScene() {
    return std::make_shared<Ref::Scene>(use_wide_bvh_);
}

void Ray::Ref::Renderer::RenderScene(const std::shared_ptr<SceneBase> &_s, RegionContext &region) {
    const auto s = std::dynamic_pointer_cast<Ref::Scene>(_s);
    if (!s) return;

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
    sc_data.materials = s->materials_.empty() ? nullptr : &s->materials_[0];
    sc_data.textures = s->textures_.empty() ? nullptr : &s->textures_[0];
    sc_data.lights = s->lights_.empty() ? nullptr : &s->lights_[0];
    sc_data.li_indices = s->li_indices_.empty() ? nullptr : &s->li_indices_[0];

    const uint32_t macro_tree_root = s->macro_nodes_root_;
    const uint32_t light_tree_root = s->light_nodes_root_;

    const TextureAtlas &tex_atlas = s->texture_atlas_;

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
                    if (root_node.child[j] == 0x7fffffff) continue;

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
        rect = { 0, 0, w, h };
    }

    region.iteration++;
    if (!region.halton_seq || region.iteration % HALTON_SEQ_LEN == 0) {
        UpdateHaltonSequence(region.iteration, region.halton_seq);
    }

    PassData p;

    {
        std::lock_guard<std::mutex> _(pass_cache_mtx_);
        if (!pass_cache_.empty()) {
            p = std::move(pass_cache_.back());
            pass_cache_.pop_back();
        }

        // allocate sh data on demand
        if (cam.pass_settings.flags & OutputSH) {
            temp_buf_.Resize(w, h, true);
            clean_buf_.Resize(w, h, true);
        }
    }

    pass_info_t pass_info;

    pass_info.iteration = region.iteration;
    pass_info.bounce = 2;
    pass_info.settings = cam.pass_settings;
    pass_info.settings.max_total_depth = std::min(pass_info.settings.max_total_depth, (uint8_t)MAX_BOUNCES);

    const auto time_start = std::chrono::high_resolution_clock::now();
    std::chrono::time_point<std::chrono::high_resolution_clock> time_after_ray_gen;

    if (cam.type != Geo) {
        GeneratePrimaryRays(region.iteration, cam, rect, w, h, &region.halton_seq[0], p.primary_rays);

        time_after_ray_gen = std::chrono::high_resolution_clock::now();

        p.intersections.resize(p.primary_rays.size());

        for (size_t i = 0; i < p.primary_rays.size(); i++) {
            const ray_packet_t &r = p.primary_rays[i];
            hit_data_t &inter = p.intersections[i];

            inter = {};
            inter.xy = r.xy;

            if (macro_tree_root != 0xffffffff) {
                if (sc_data.mnodes) {
                    Traverse_MacroTree_WithStack_ClosestHit(r, sc_data.mnodes, macro_tree_root, sc_data.mesh_instances, sc_data.mi_indices, sc_data.meshes,
                                                            sc_data.transforms, sc_data.tris, sc_data.tri_indices, inter);
                } else {
                    Traverse_MacroTree_WithStack_ClosestHit(r, sc_data.nodes, macro_tree_root, sc_data.mesh_instances, sc_data.mi_indices, sc_data.meshes,
                                                            sc_data.transforms, sc_data.tris, sc_data.tri_indices, inter);
                }
            }
        }
    } else {
        const mesh_instance_t &mi = sc_data.mesh_instances[cam.mi_index];
        SampleMeshInTextureSpace(region.iteration, cam.mi_index, cam.uv_index,
                                 sc_data.meshes[mi.mesh_index], sc_data.transforms[mi.tr_index], sc_data.vtx_indices, sc_data.vertices,
                                 rect, w, h, &region.halton_seq[0], p.primary_rays, p.intersections);

        time_after_ray_gen = std::chrono::high_resolution_clock::now();
    }

    const auto time_after_prim_trace = std::chrono::high_resolution_clock::now();

    p.secondary_rays.resize(p.intersections.size());
    int secondary_rays_count = 0;

    for (size_t i = 0; i < p.intersections.size(); i++) {
        const ray_packet_t &r = p.primary_rays[i];
        const hit_data_t &inter = p.intersections[i];

        const int x = (inter.xy >> 16) & 0x0000ffff;
        const int y = inter.xy & 0x0000ffff;

        pass_info.index = y * w + x;
        pass_info.rand_index = pass_info.index;
        
        if (cam.pass_settings.flags & UseCoherentSampling) {
            const int blck_x = x % 8, blck_y = y % 8;
            pass_info.rand_index = sampling_pattern[blck_y * 8 + blck_x];
        }

        pixel_color_t col = ShadeSurface(pass_info, inter, r, &region.halton_seq[0], sc_data, macro_tree_root, light_tree_root,
                                         tex_atlas, &p.secondary_rays[0], &secondary_rays_count);
        temp_buf_.SetPixel(x, y, col);
    }

    const auto time_after_prim_shade = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> secondary_sort_time{}, secondary_trace_time{}, secondary_shade_time{};

    p.hash_values.resize(secondary_rays_count);
    //p.head_flags.resize(secondary_rays_count);
    p.scan_values.resize(secondary_rays_count);
    p.chunks.resize(secondary_rays_count);
    p.chunks_temp.resize(secondary_rays_count);
    //p.skeleton.resize(secondary_rays_count);

    if (cam.pass_settings.flags & OutputSH) {
        temp_buf_.ResetSampleData(rect);
        for (int i = 0; i < secondary_rays_count; i++) {
            const ray_packet_t &r = p.secondary_rays[i];

            const int x = (r.xy >> 16) & 0x0000ffff;
            const int y = r.xy & 0x0000ffff;

            temp_buf_.SetSampleDir(x, y, r.d[0], r.d[1], r.d[2]);
            // sample weight for indirect lightmap has all r.c[0..2]'s set to same value
            temp_buf_.SetSampleWeight(x, y, r.c[0]);
        }
    }

    for (int bounce = 0; bounce < pass_info.settings.max_total_depth && secondary_rays_count && !(pass_info.settings.flags & SkipIndirectLight); bounce++) {
        auto time_secondary_sort_start = std::chrono::high_resolution_clock::now();

        SortRays_CPU(&p.secondary_rays[0], (size_t)secondary_rays_count, root_min, cell_size,
                     &p.hash_values[0], &p.scan_values[0], &p.chunks[0], &p.chunks_temp[0]);

#if 0   // debug hash values
        static std::vector<simd_fvec3> color_table;
        if (color_table.empty()) {
            for (int i = 0; i < 1024; i++) {
                color_table.emplace_back(float(rand()) / RAND_MAX, float(rand()) / RAND_MAX, float(rand()) / RAND_MAX);
            }
        }

        for (int i = 0; i < secondary_rays_count; i++) {
            const ray_packet_t &r = p.secondary_rays[i];

            const int x = r.id.x;
            const int y = r.id.y;

            const simd_fvec3 &c = color_table[hash(p.hash_values[i]) % 1024];

            pixel_color_t col = { c[0], c[1], c[2], 1.0f };
            temp_buf_.SetPixel(x, y, col);
        }
#endif

        auto time_secondary_trace_start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < secondary_rays_count; i++) {
            const ray_packet_t &r = p.secondary_rays[i];
            hit_data_t &inter = p.intersections[i];

            inter = {};
            inter.xy = r.xy;

            if (sc_data.mnodes) {
                Traverse_MacroTree_WithStack_ClosestHit(r, sc_data.mnodes, macro_tree_root, sc_data.mesh_instances,
                                                        sc_data.mi_indices, sc_data.meshes, sc_data.transforms, sc_data.tris, sc_data.tri_indices, inter);
            } else {
                Traverse_MacroTree_WithStack_ClosestHit(r, sc_data.nodes, macro_tree_root, sc_data.mesh_instances,
                                                        sc_data.mi_indices, sc_data.meshes, sc_data.transforms, sc_data.tris, sc_data.tri_indices, inter);
            }
        }

        auto time_secondary_shade_start = std::chrono::high_resolution_clock::now();

        int rays_count = secondary_rays_count;
        secondary_rays_count = 0;
        std::swap(p.primary_rays, p.secondary_rays);

        pass_info.bounce = bounce + 3;

        for (int i = 0; i < rays_count; i++) {
            const ray_packet_t &r = p.primary_rays[i];
            const hit_data_t &inter = p.intersections[i];

            const int x = (inter.xy >> 16) & 0x0000ffff;
            const int y = inter.xy & 0x0000ffff;

            pass_info.index = y * w + x;
            pass_info.rand_index = pass_info.index;

            if (cam.pass_settings.flags & UseCoherentSampling) {
                const int blck_x = x % 8, blck_y = y % 8;
                pass_info.rand_index = sampling_pattern[blck_y * 8 + blck_x];
            }

            pixel_color_t col = ShadeSurface(pass_info, inter, r, &region.halton_seq[0], sc_data, macro_tree_root, light_tree_root,
                                             tex_atlas, &p.secondary_rays[0], &secondary_rays_count);
            col.a = 0.0f;

            temp_buf_.AddPixel(x, y, col);
        }

        auto time_secondary_shade_end = std::chrono::high_resolution_clock::now();
        secondary_sort_time += std::chrono::duration<double, std::micro>{ time_secondary_trace_start - time_secondary_sort_start };
        secondary_trace_time += std::chrono::duration<double, std::micro>{ time_secondary_shade_start - time_secondary_trace_start };
        secondary_shade_time += std::chrono::duration<double, std::micro>{ time_secondary_shade_end - time_secondary_shade_start };
    }

    {
        std::lock_guard<std::mutex> _(pass_cache_mtx_);
        pass_cache_.emplace_back(std::move(p));

        stats_.time_primary_ray_gen_us += (unsigned long long)std::chrono::duration<double, std::micro>{ time_after_ray_gen - time_start }.count();
        stats_.time_primary_trace_us += (unsigned long long)std::chrono::duration<double, std::micro>{ time_after_prim_trace - time_after_ray_gen }.count();
        stats_.time_primary_shade_us += (unsigned long long)std::chrono::duration<double, std::micro>{ time_after_prim_shade - time_after_prim_trace }.count();
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
        simd_fvec4 c = { &p.r };

        if (cam.dtype == SRGB) {
            ITERATE_3({
                if (c[i] > 0.0031308f) {
                    c[i] = std::pow(1.055f * c[i], (1.0f / 2.4f)) - 0.055f;
                } else {
                    c[i] = 12.92f * c[i];
                }
            })
        }

        if (cam.gamma != 1.0f) {
            c = pow(c, simd_fvec4{ 1.0f / cam.gamma });
        }

        if (cam.pass_settings.flags & Clamp) {
            c = clamp(c, 0.0f, 1.0f);
        }
        return pixel_color_t{ c[0], c[1], c[2], c[3] };
    };

    final_buf_.CopyFrom(clean_buf_, rect, clamp_and_gamma_correct);
}

void Ray::Ref::Renderer::UpdateHaltonSequence(int iteration, std::unique_ptr<float[]> &seq) {
    if (!seq) {
        seq.reset(new float[HALTON_COUNT * HALTON_SEQ_LEN]);
    }

    for (int i = 0; i < HALTON_SEQ_LEN; i++) {
        seq[2 * (i * HALTON_2D_COUNT + 0 ) + 0] = Ray::ScrambledRadicalInverse<2 >(&permutations_[0  ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 0 ) + 1] = Ray::ScrambledRadicalInverse<3 >(&permutations_[2  ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 1 ) + 0] = Ray::ScrambledRadicalInverse<5 >(&permutations_[5  ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 1 ) + 1] = Ray::ScrambledRadicalInverse<7 >(&permutations_[10 ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 2 ) + 0] = Ray::ScrambledRadicalInverse<11>(&permutations_[17 ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 2 ) + 1] = Ray::ScrambledRadicalInverse<13>(&permutations_[28 ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 3 ) + 0] = Ray::ScrambledRadicalInverse<17>(&permutations_[41 ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 3 ) + 1] = Ray::ScrambledRadicalInverse<19>(&permutations_[58 ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 4 ) + 0] = Ray::ScrambledRadicalInverse<23>(&permutations_[77 ], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 4 ) + 1] = Ray::ScrambledRadicalInverse<29>(&permutations_[100], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 5 ) + 0] = Ray::ScrambledRadicalInverse<31>(&permutations_[129], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 5 ) + 1] = Ray::ScrambledRadicalInverse<37>(&permutations_[160], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 6 ) + 0] = Ray::ScrambledRadicalInverse<41>(&permutations_[197], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 6 ) + 1] = Ray::ScrambledRadicalInverse<43>(&permutations_[238], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 7 ) + 0] = Ray::ScrambledRadicalInverse<47>(&permutations_[281], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 7 ) + 1] = Ray::ScrambledRadicalInverse<53>(&permutations_[328], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 8 ) + 0] = Ray::ScrambledRadicalInverse<59>(&permutations_[381], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 8 ) + 1] = Ray::ScrambledRadicalInverse<61>(&permutations_[440], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 9 ) + 0] = Ray::ScrambledRadicalInverse<67>(&permutations_[501], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 9 ) + 1] = Ray::ScrambledRadicalInverse<71>(&permutations_[568], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 10) + 0] = Ray::ScrambledRadicalInverse<73>(&permutations_[639], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 10) + 1] = Ray::ScrambledRadicalInverse<79>(&permutations_[712], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 11) + 0] = Ray::ScrambledRadicalInverse<83>(&permutations_[791], (uint64_t)(iteration + i));
        seq[2 * (i * HALTON_2D_COUNT + 11) + 1] = Ray::ScrambledRadicalInverse<89>(&permutations_[874], (uint64_t)(iteration + i));
    }
}