#include "RendererDX.h"

#include <functional>
#include <random>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d12.h>

#include "CoreDX.h"
#include "Halton.h"
#include "SceneDX.h"
#include "UniformIntDistribution.h"

#include "Dx/DebugMarkerDX.h"
#include "Dx/DescriptorPoolDX.h"

#include "../Log.h"

#include "shaders/types.h"

#define DEBUG_HWRT 0
#define RUN_IN_LOCKSTEP 0
#define DISABLE_SORTING 0
#define ENABLE_RT_PIPELINE 0

static_assert(sizeof(Types::tri_accel_t) == sizeof(Ray::tri_accel_t), "!");
static_assert(sizeof(Types::bvh_node_t) == sizeof(Ray::bvh_node_t), "!");
static_assert(sizeof(Types::vertex_t) == sizeof(Ray::vertex_t), "!");
static_assert(sizeof(Types::mesh_t) == sizeof(Ray::mesh_t), "!");
static_assert(sizeof(Types::transform_t) == sizeof(Ray::transform_t), "!");
static_assert(sizeof(Types::mesh_instance_t) == sizeof(Ray::mesh_instance_t), "!");
static_assert(sizeof(Types::light_t) == sizeof(Ray::light_t), "!");
static_assert(sizeof(Types::material_t) == sizeof(Ray::material_t), "!");
static_assert(sizeof(Types::atlas_texture_t) == sizeof(Ray::atlas_texture_t), "!");
static_assert(sizeof(Types::ray_chunk_t) == sizeof(Ray::ray_chunk_t), "!");
static_assert(sizeof(Types::ray_hash_t) == sizeof(Ray::ray_hash_t), "!");

static_assert(Types::LIGHT_TYPE_SPHERE == Ray::LIGHT_TYPE_SPHERE, "!");
static_assert(Types::LIGHT_TYPE_SPOT == Ray::LIGHT_TYPE_SPOT, "!");
static_assert(Types::LIGHT_TYPE_DIR == Ray::LIGHT_TYPE_DIR, "!");
static_assert(Types::LIGHT_TYPE_LINE == Ray::LIGHT_TYPE_LINE, "!");
static_assert(Types::LIGHT_TYPE_RECT == Ray::LIGHT_TYPE_RECT, "!");
static_assert(Types::LIGHT_TYPE_DISK == Ray::LIGHT_TYPE_DISK, "!");
static_assert(Types::LIGHT_TYPE_TRI == Ray::LIGHT_TYPE_TRI, "!");

namespace Ray {
extern const int LUT_DIMS;
extern const uint32_t *transform_luts[];

int round_up(int v, int align);
namespace Dx {
// #include "shaders/output/debug_rt.comp.cso.inl"
#include "shaders/output/filter_variance.comp.cso.inl"
#include "shaders/output/intersect_area_lights.comp.cso.inl"
// #include "shaders/output/intersect_scene.rchit.cso.inl"
// #include "shaders/output/intersect_scene.rgen.cso.inl"
// #include "shaders/output/intersect_scene.rmiss.cso.inl"
// #include "shaders/output/intersect_scene_hwrt_atlas.comp.cso.inl"
// #include "shaders/output/intersect_scene_hwrt_bindless.comp.cso.inl"
// #include "shaders/output/intersect_scene_indirect.rgen.cso.inl"
// #include "shaders/output/intersect_scene_indirect_hwrt_atlas.comp.cso.inl"
// #include "shaders/output/intersect_scene_indirect_hwrt_bindless.comp.cso.inl"
#include "shaders/output/intersect_scene_indirect_swrt_atlas.comp.cso.inl"
#include "shaders/output/intersect_scene_indirect_swrt_bindless.comp.cso.inl"
// #include "shaders/output/intersect_scene_shadow_hwrt_atlas.comp.cso.inl"
// #include "shaders/output/intersect_scene_shadow_hwrt_bindless.comp.cso.inl"
#include "shaders/output/intersect_scene_shadow_swrt_atlas.comp.cso.inl"
#include "shaders/output/intersect_scene_shadow_swrt_bindless.comp.cso.inl"
#include "shaders/output/intersect_scene_swrt_atlas.comp.cso.inl"
#include "shaders/output/intersect_scene_swrt_bindless.comp.cso.inl"
#include "shaders/output/mix_incremental.comp.cso.inl"
#include "shaders/output/mix_incremental_b.comp.cso.inl"
#include "shaders/output/mix_incremental_bn.comp.cso.inl"
#include "shaders/output/mix_incremental_n.comp.cso.inl"
#include "shaders/output/nlm_filter.comp.cso.inl"
#include "shaders/output/nlm_filter_b.comp.cso.inl"
#include "shaders/output/nlm_filter_bn.comp.cso.inl"
#include "shaders/output/nlm_filter_n.comp.cso.inl"
#include "shaders/output/postprocess.comp.cso.inl"
#include "shaders/output/prepare_indir_args.comp.cso.inl"
#include "shaders/output/primary_ray_gen_adaptive.comp.cso.inl"
#include "shaders/output/primary_ray_gen_simple.comp.cso.inl"
#include "shaders/output/shade_primary_atlas.comp.cso.inl"
#include "shaders/output/shade_primary_atlas_b.comp.cso.inl"
#include "shaders/output/shade_primary_atlas_bn.comp.cso.inl"
#include "shaders/output/shade_primary_atlas_n.comp.cso.inl"
#include "shaders/output/shade_primary_bindless.comp.cso.inl"
#include "shaders/output/shade_primary_bindless_b.comp.cso.inl"
#include "shaders/output/shade_primary_bindless_bn.comp.cso.inl"
#include "shaders/output/shade_primary_bindless_n.comp.cso.inl"
#include "shaders/output/shade_secondary_atlas.comp.cso.inl"
#include "shaders/output/shade_secondary_bindless.comp.cso.inl"
#include "shaders/output/sort_add_partial_sums.comp.cso.inl"
#include "shaders/output/sort_exclusive_scan.comp.cso.inl"
#include "shaders/output/sort_hash_rays.comp.cso.inl"
#include "shaders/output/sort_inclusive_scan.comp.cso.inl"
#include "shaders/output/sort_init_count_table.comp.cso.inl"
#include "shaders/output/sort_reorder_rays.comp.cso.inl"
#include "shaders/output/sort_write_sorted_hashes.comp.cso.inl"
} // namespace Dx
} // namespace Ray

Ray::Dx::Renderer::Renderer(const settings_t &s, ILog *log) : loaded_halton_(-1) {
    ctx_.reset(new Context);
    const bool res = ctx_->Init(log, s.preferred_device);
    if (!res) {
        throw std::runtime_error("Error initializing directx context!");
    }

    use_hwrt_ = (s.use_hwrt && ctx_->ray_query_supported());
    use_bindless_ = s.use_bindless && ctx_->max_combined_image_samplers() >= 16384u;
    use_tex_compression_ = s.use_tex_compression;
    log->Info("HWRT        is %s", use_hwrt_ ? "enabled" : "disabled");
    log->Info("Bindless    is %s", use_bindless_ ? "enabled" : "disabled");
    log->Info("Compression is %s", use_tex_compression_ ? "enabled" : "disabled");

    sh_prim_rays_gen_simple_ = Shader{"Primary Raygen Simple",
                                      ctx_.get(),
                                      internal_shaders_output_primary_ray_gen_simple_comp_cso,
                                      internal_shaders_output_primary_ray_gen_simple_comp_cso_size,
                                      eShaderType::Comp,
                                      log};
    sh_prim_rays_gen_adaptive_ = Shader{"Primary Raygen Adaptive",
                                        ctx_.get(),
                                        internal_shaders_output_primary_ray_gen_adaptive_comp_cso,
                                        internal_shaders_output_primary_ray_gen_adaptive_comp_cso_size,
                                        eShaderType::Comp,
                                        log};
    if (use_hwrt_) {
        // sh_intersect_scene_ =
        //     Shader{"Intersect Scene (Primary) (HWRT)",
        //            ctx_.get(),
        //            use_bindless_ ? internal_shaders_output_intersect_scene_hwrt_bindless_comp_spv
        //                          : internal_shaders_output_intersect_scene_hwrt_atlas_comp_spv,
        //            use_bindless_ ? int(internal_shaders_output_intersect_scene_hwrt_bindless_comp_spv_size)
        //                          : int(internal_shaders_output_intersect_scene_hwrt_atlas_comp_spv_size),
        //            eShaderType::Comp,
        //            log};
    } else {
        sh_intersect_scene_ =
            Shader{"Intersect Scene (Primary) (SWRT)",
                   ctx_.get(),
                   use_bindless_ ? internal_shaders_output_intersect_scene_swrt_bindless_comp_cso
                                 : internal_shaders_output_intersect_scene_swrt_atlas_comp_cso,
                   use_bindless_ ? int(internal_shaders_output_intersect_scene_swrt_bindless_comp_cso_size)
                                 : int(internal_shaders_output_intersect_scene_swrt_atlas_comp_cso_size),
                   eShaderType::Comp,
                   log};
    }

    if (use_hwrt_) {
        // sh_intersect_scene_indirect_ =
        //     Shader{"Intersect Scene (Secondary) (HWRT)",
        //            ctx_.get(),
        //            use_bindless_ ? internal_shaders_output_intersect_scene_indirect_hwrt_bindless_comp_spv
        //                          : internal_shaders_output_intersect_scene_indirect_hwrt_atlas_comp_spv,
        //            use_bindless_ ? int(internal_shaders_output_intersect_scene_indirect_hwrt_bindless_comp_spv_size)
        //                          : int(internal_shaders_output_intersect_scene_indirect_hwrt_atlas_comp_spv_size),
        //            eShaderType::Comp,
        //            log};
    } else {
        sh_intersect_scene_indirect_ =
            Shader{"Intersect Scene (Secondary) (SWRT)",
                   ctx_.get(),
                   use_bindless_ ? internal_shaders_output_intersect_scene_indirect_swrt_bindless_comp_cso
                                 : internal_shaders_output_intersect_scene_indirect_swrt_atlas_comp_cso,
                   use_bindless_ ? int(internal_shaders_output_intersect_scene_indirect_swrt_bindless_comp_cso_size)
                                 : int(internal_shaders_output_intersect_scene_indirect_swrt_atlas_comp_cso_size),
                   eShaderType::Comp,
                   log};
    }

    sh_intersect_area_lights_ = Shader{"Intersect Area Lights",
                                       ctx_.get(),
                                       internal_shaders_output_intersect_area_lights_comp_cso,
                                       internal_shaders_output_intersect_area_lights_comp_cso_size,
                                       eShaderType::Comp,
                                       log};
    sh_shade_primary_ = Shader{"Shade (Primary)",
                               ctx_.get(),
                               use_bindless_ ? internal_shaders_output_shade_primary_bindless_comp_cso
                                             : internal_shaders_output_shade_primary_atlas_comp_cso,
                               use_bindless_ ? int(internal_shaders_output_shade_primary_bindless_comp_cso_size)
                                             : int(internal_shaders_output_shade_primary_atlas_comp_cso_size),
                               eShaderType::Comp,
                               log};
    sh_shade_primary_b_ = Shader{"Shade (Primary) B",
                                 ctx_.get(),
                                 use_bindless_ ? internal_shaders_output_shade_primary_bindless_b_comp_cso
                                               : internal_shaders_output_shade_primary_atlas_b_comp_cso,
                                 use_bindless_ ? int(internal_shaders_output_shade_primary_bindless_b_comp_cso_size)
                                               : int(internal_shaders_output_shade_primary_atlas_b_comp_cso_size),
                                 eShaderType::Comp,
                                 log};
    sh_shade_primary_n_ = Shader{"Shade (Primary) N",
                                 ctx_.get(),
                                 use_bindless_ ? internal_shaders_output_shade_primary_bindless_n_comp_cso
                                               : internal_shaders_output_shade_primary_atlas_n_comp_cso,
                                 use_bindless_ ? int(internal_shaders_output_shade_primary_bindless_n_comp_cso_size)
                                               : int(internal_shaders_output_shade_primary_atlas_n_comp_cso_size),
                                 eShaderType::Comp,
                                 log};
    sh_shade_primary_bn_ = Shader{"Shade (Primary) BN",
                                  ctx_.get(),
                                  use_bindless_ ? internal_shaders_output_shade_primary_bindless_bn_comp_cso
                                                : internal_shaders_output_shade_primary_atlas_bn_comp_cso,
                                  use_bindless_ ? int(internal_shaders_output_shade_primary_bindless_bn_comp_cso_size)
                                                : int(internal_shaders_output_shade_primary_atlas_bn_comp_cso_size),
                                  eShaderType::Comp,
                                  log};
    sh_shade_secondary_ = Shader{"Shade (Secondary)",
                                 ctx_.get(),
                                 use_bindless_ ? internal_shaders_output_shade_secondary_bindless_comp_cso
                                               : internal_shaders_output_shade_secondary_atlas_comp_cso,
                                 use_bindless_ ? int(internal_shaders_output_shade_secondary_bindless_comp_cso_size)
                                               : int(internal_shaders_output_shade_secondary_atlas_comp_cso_size),
                                 eShaderType::Comp,
                                 log};

    if (use_hwrt_) {
        // sh_intersect_scene_shadow_ =
        //     Shader{"Intersect Scene (Shadow) (HWRT)",
        //            ctx_.get(),
        //            use_bindless_ ? internal_shaders_output_intersect_scene_shadow_hwrt_bindless_comp_spv
        //                          : internal_shaders_output_intersect_scene_shadow_hwrt_atlas_comp_spv,
        //            use_bindless_ ? int(internal_shaders_output_intersect_scene_shadow_hwrt_bindless_comp_spv_size)
        //                          : int(internal_shaders_output_intersect_scene_shadow_hwrt_atlas_comp_spv_size),
        //            eShaderType::Comp,
        //            log};
    } else {
        sh_intersect_scene_shadow_ =
            Shader{"Intersect Scene (Shadow) (SWRT)",
                   ctx_.get(),
                   use_bindless_ ? internal_shaders_output_intersect_scene_shadow_swrt_bindless_comp_cso
                                 : internal_shaders_output_intersect_scene_shadow_swrt_atlas_comp_cso,
                   use_bindless_ ? int(internal_shaders_output_intersect_scene_shadow_swrt_bindless_comp_cso_size)
                                 : int(internal_shaders_output_intersect_scene_shadow_swrt_atlas_comp_cso_size),
                   eShaderType::Comp,
                   log};
    }
    sh_prepare_indir_args_ = Shader{"Prepare Indir Args",
                                    ctx_.get(),
                                    internal_shaders_output_prepare_indir_args_comp_cso,
                                    internal_shaders_output_prepare_indir_args_comp_cso_size,
                                    eShaderType::Comp,
                                    log};
    sh_mix_incremental_ = Shader{"Mix Incremental",
                                 ctx_.get(),
                                 internal_shaders_output_mix_incremental_comp_cso,
                                 internal_shaders_output_mix_incremental_comp_cso_size,
                                 eShaderType::Comp,
                                 log};
    sh_mix_incremental_b_ = Shader{"Mix Incremental B",
                                   ctx_.get(),
                                   internal_shaders_output_mix_incremental_b_comp_cso,
                                   internal_shaders_output_mix_incremental_b_comp_cso_size,
                                   eShaderType::Comp,
                                   log};
    sh_mix_incremental_n_ = Shader{"Mix Incremental N",
                                   ctx_.get(),
                                   internal_shaders_output_mix_incremental_n_comp_cso,
                                   internal_shaders_output_mix_incremental_n_comp_cso_size,
                                   eShaderType::Comp,
                                   log};
    sh_mix_incremental_bn_ = Shader{"Mix Incremental BN",
                                    ctx_.get(),
                                    internal_shaders_output_mix_incremental_bn_comp_cso,
                                    internal_shaders_output_mix_incremental_bn_comp_cso_size,
                                    eShaderType::Comp,
                                    log};
    sh_postprocess_ = Shader{"Postprocess",
                             ctx_.get(),
                             internal_shaders_output_postprocess_comp_cso,
                             internal_shaders_output_postprocess_comp_cso_size,
                             eShaderType::Comp,
                             log};
    sh_filter_variance_ = Shader{"Filter Variance",
                                 ctx_.get(),
                                 internal_shaders_output_filter_variance_comp_cso,
                                 internal_shaders_output_filter_variance_comp_cso_size,
                                 eShaderType::Comp,
                                 log};
    sh_nlm_filter_ = Shader{"NLM Filter",
                            ctx_.get(),
                            internal_shaders_output_nlm_filter_comp_cso,
                            internal_shaders_output_nlm_filter_comp_cso_size,
                            eShaderType::Comp,
                            log};
    sh_nlm_filter_b_ = Shader{"NLM Filter B",
                              ctx_.get(),
                              internal_shaders_output_nlm_filter_b_comp_cso,
                              internal_shaders_output_nlm_filter_b_comp_cso_size,
                              eShaderType::Comp,
                              log};
    sh_nlm_filter_n_ = Shader{"NLM Filter N",
                              ctx_.get(),
                              internal_shaders_output_nlm_filter_n_comp_cso,
                              internal_shaders_output_nlm_filter_n_comp_cso_size,
                              eShaderType::Comp,
                              log};
    sh_nlm_filter_bn_ = Shader{"NLM Filter BN",
                               ctx_.get(),
                               internal_shaders_output_nlm_filter_bn_comp_cso,
                               internal_shaders_output_nlm_filter_bn_comp_cso_size,
                               eShaderType::Comp,
                               log};
    if (use_hwrt_) {
        // sh_debug_rt_ = Shader{"Debug RT",
        //                       ctx_.get(),
        //                       internal_shaders_output_debug_rt_comp_spv,
        //                       internal_shaders_output_debug_rt_comp_spv_size,
        //                       eShaderType::Comp,
        //                       log};
    }

    sh_sort_hash_rays_ = Shader{"Sort Hash Rays",
                                ctx_.get(),
                                internal_shaders_output_sort_hash_rays_comp_cso,
                                internal_shaders_output_sort_hash_rays_comp_cso_size,
                                eShaderType::Comp,
                                log};
    sh_sort_exclusive_scan_ = Shader{"Sort Exclusive Scan",
                                     ctx_.get(),
                                     internal_shaders_output_sort_exclusive_scan_comp_cso,
                                     internal_shaders_output_sort_exclusive_scan_comp_cso_size,
                                     eShaderType::Comp,
                                     log};
    sh_sort_inclusive_scan_ = Shader{"Sort Inclusive Scan",
                                     ctx_.get(),
                                     internal_shaders_output_sort_inclusive_scan_comp_cso,
                                     internal_shaders_output_sort_inclusive_scan_comp_cso_size,
                                     eShaderType::Comp,
                                     log};
    sh_sort_add_partial_sums_ = Shader{"Sort Add Partial Sums",
                                       ctx_.get(),
                                       internal_shaders_output_sort_add_partial_sums_comp_cso,
                                       internal_shaders_output_sort_add_partial_sums_comp_cso_size,
                                       eShaderType::Comp,
                                       log};
    sh_sort_init_count_table_ = Shader{"Sort Init Count Table",
                                       ctx_.get(),
                                       internal_shaders_output_sort_init_count_table_comp_cso,
                                       internal_shaders_output_sort_init_count_table_comp_cso_size,
                                       eShaderType::Comp,
                                       log};
    sh_sort_write_sorted_hashes_ = Shader{"Sort Write Sorted Hashes",
                                          ctx_.get(),
                                          internal_shaders_output_sort_write_sorted_hashes_comp_cso,
                                          internal_shaders_output_sort_write_sorted_hashes_comp_cso_size,
                                          eShaderType::Comp,
                                          log};
    sh_sort_reorder_rays_ = Shader{"Sort Reorder Rays",
                                   ctx_.get(),
                                   internal_shaders_output_sort_reorder_rays_comp_cso,
                                   internal_shaders_output_sort_reorder_rays_comp_cso_size,
                                   eShaderType::Comp,
                                   log};

    /*sh_intersect_scene_rgen_ = Shader{"Intersect Scene RGEN",
                                      ctx_.get(),
                                      internal_shaders_output_intersect_scene_rgen_cso,
                                      internal_shaders_output_intersect_scene_rgen_cso_size,
                                      eShaderType::RayGen,
                                      log};
    sh_intersect_scene_indirect_rgen_ = Shader{"Intersect Scene Indirect RGEN",
                                               ctx_.get(),
                                               internal_shaders_output_intersect_scene_indirect_rgen_spv,
                                               internal_shaders_output_intersect_scene_indirect_rgen_spv_size,
                                               eShaderType::RayGen,
                                               log};
    sh_intersect_scene_rchit_ = Shader{"Intersect Scene RCHIT",
                                       ctx_.get(),
                                       internal_shaders_output_intersect_scene_rchit_spv,
                                       internal_shaders_output_intersect_scene_rchit_spv_size,
                                       eShaderType::ClosestHit,
                                       log};
    sh_intersect_scene_rmiss_ = Shader{"Intersect Scene RMISS",
                                       ctx_.get(),
                                       internal_shaders_output_intersect_scene_rmiss_spv,
                                       internal_shaders_output_intersect_scene_rmiss_spv_size,
                                       eShaderType::AnyHit,
                                       log};*/

    prog_prim_rays_gen_simple_ = Program{"Primary Raygen Simple", ctx_.get(), &sh_prim_rays_gen_simple_, log};
    prog_prim_rays_gen_adaptive_ = Program{"Primary Raygen Adaptive", ctx_.get(), &sh_prim_rays_gen_adaptive_, log};
    prog_intersect_scene_ = Program{"Intersect Scene (Primary)", ctx_.get(), &sh_intersect_scene_, log};
    prog_intersect_scene_indirect_ =
        Program{"Intersect Scene (Secondary)", ctx_.get(), &sh_intersect_scene_indirect_, log};
    prog_intersect_area_lights_ = Program{"Intersect Area Lights", ctx_.get(), &sh_intersect_area_lights_, log};
    prog_shade_primary_ = Program{"Shade (Primary)", ctx_.get(), &sh_shade_primary_, log};
    prog_shade_primary_b_ = Program{"Shade (Primary) B", ctx_.get(), &sh_shade_primary_b_, log};
    prog_shade_primary_n_ = Program{"Shade (Primary) N", ctx_.get(), &sh_shade_primary_n_, log};
    prog_shade_primary_bn_ = Program{"Shade (Primary) BN", ctx_.get(), &sh_shade_primary_bn_, log};
    prog_shade_secondary_ = Program{"Shade (Secondary)", ctx_.get(), &sh_shade_secondary_, log};
    prog_intersect_scene_shadow_ = Program{"Intersect Scene (Shadow)", ctx_.get(), &sh_intersect_scene_shadow_, log};
    prog_prepare_indir_args_ = Program{"Prepare Indir Args", ctx_.get(), &sh_prepare_indir_args_, log};
    prog_mix_incremental_ = Program{"Mix Incremental", ctx_.get(), &sh_mix_incremental_, log};
    prog_mix_incremental_b_ = Program{"Mix Incremental B", ctx_.get(), &sh_mix_incremental_b_, log};
    prog_mix_incremental_n_ = Program{"Mix Incremental N", ctx_.get(), &sh_mix_incremental_n_, log};
    prog_mix_incremental_bn_ = Program{"Mix Incremental BN", ctx_.get(), &sh_mix_incremental_bn_, log};
    prog_postprocess_ = Program{"Postprocess", ctx_.get(), &sh_postprocess_, log};
    prog_filter_variance_ = Program{"Filter Variance", ctx_.get(), &sh_filter_variance_, log};
    prog_nlm_filter_ = Program{"NLM Filter", ctx_.get(), &sh_nlm_filter_, log};
    prog_nlm_filter_b_ = Program{"NLM Filter B", ctx_.get(), &sh_nlm_filter_b_, log};
    prog_nlm_filter_n_ = Program{"NLM Filter N", ctx_.get(), &sh_nlm_filter_n_, log};
    prog_nlm_filter_bn_ = Program{"NLM Filter BN", ctx_.get(), &sh_nlm_filter_bn_, log};
    // prog_debug_rt_ = Program{"Debug RT", ctx_.get(), &sh_debug_rt_, log};
    prog_sort_hash_rays_ = Program{"Hash Rays", ctx_.get(), &sh_sort_hash_rays_, log};
    prog_sort_exclusive_scan_ = Program{"Exclusive Scan", ctx_.get(), &sh_sort_exclusive_scan_, log};
    prog_sort_inclusive_scan_ = Program{"Inclusive Scan", ctx_.get(), &sh_sort_inclusive_scan_, log};
    prog_sort_add_partial_sums_ = Program{"Add Partial Sums", ctx_.get(), &sh_sort_add_partial_sums_, log};
    prog_sort_init_count_table_ = Program{"Init Count Table", ctx_.get(), &sh_sort_init_count_table_, log};
    prog_sort_write_sorted_hashes_ = Program{"Write Sorted Chunks", ctx_.get(), &sh_sort_write_sorted_hashes_, log};
    prog_sort_reorder_rays_ = Program{"Reorder Rays", ctx_.get(), &sh_sort_reorder_rays_, log};
    // prog_intersect_scene_rtpipe_ = Program{"Intersect Scene",
    //                                        ctx_.get(),
    //                                        &sh_intersect_scene_rgen_,
    //                                        &sh_intersect_scene_rchit_,
    //                                       nullptr,
    //                                       &sh_intersect_scene_rmiss_,
    //                                       nullptr,
    //                                       log};
    // prog_intersect_scene_indirect_rtpipe_ = Program{"Intersect Scene Indirect",
    //                                                ctx_.get(),
    //                                                &sh_intersect_scene_indirect_rgen_,
    //                                                &sh_intersect_scene_rchit_,
    //                                                nullptr,
    //                                                &sh_intersect_scene_rmiss_,
    //                                                nullptr,
    //                                                log};

    if (!pi_prim_rays_gen_simple_.Init(ctx_.get(), &prog_prim_rays_gen_simple_, log) ||
        !pi_prim_rays_gen_adaptive_.Init(ctx_.get(), &prog_prim_rays_gen_adaptive_, log) ||
        !pi_intersect_scene_.Init(ctx_.get(), &prog_intersect_scene_, log) ||
        !pi_intersect_scene_indirect_.Init(ctx_.get(), &prog_intersect_scene_indirect_, log) ||
        !pi_intersect_area_lights_.Init(ctx_.get(), &prog_intersect_area_lights_, log) ||
        !pi_shade_primary_.Init(ctx_.get(), &prog_shade_primary_, log) ||
        !pi_shade_primary_b_.Init(ctx_.get(), &prog_shade_primary_b_, log) ||
        !pi_shade_primary_n_.Init(ctx_.get(), &prog_shade_primary_n_, log) ||
        !pi_shade_primary_bn_.Init(ctx_.get(), &prog_shade_primary_bn_, log) ||
        !pi_shade_secondary_.Init(ctx_.get(), &prog_shade_secondary_, log) ||
        !pi_intersect_scene_shadow_.Init(ctx_.get(), &prog_intersect_scene_shadow_, log) ||
        !pi_prepare_indir_args_.Init(ctx_.get(), &prog_prepare_indir_args_, log) ||
        !pi_mix_incremental_.Init(ctx_.get(), &prog_mix_incremental_, log) ||
        !pi_mix_incremental_b_.Init(ctx_.get(), &prog_mix_incremental_b_, log) ||
        !pi_mix_incremental_n_.Init(ctx_.get(), &prog_mix_incremental_n_, log) ||
        !pi_mix_incremental_bn_.Init(ctx_.get(), &prog_mix_incremental_bn_, log) ||
        !pi_postprocess_.Init(ctx_.get(), &prog_postprocess_, log) ||
        !pi_filter_variance_.Init(ctx_.get(), &prog_filter_variance_, log) ||
        !pi_nlm_filter_.Init(ctx_.get(), &prog_nlm_filter_, log) ||
        !pi_nlm_filter_b_.Init(ctx_.get(), &prog_nlm_filter_b_, log) ||
        !pi_nlm_filter_n_.Init(ctx_.get(), &prog_nlm_filter_n_, log) ||
        !pi_nlm_filter_bn_.Init(ctx_.get(), &prog_nlm_filter_bn_, log) ||
        //(use_hwrt_ && !pi_debug_rt_.Init(ctx_.get(), &prog_debug_rt_, log)) ||
        !pi_sort_hash_rays_.Init(ctx_.get(), &prog_sort_hash_rays_, log) ||
        !pi_sort_exclusive_scan_.Init(ctx_.get(), &prog_sort_exclusive_scan_, log) ||
        !pi_sort_inclusive_scan_.Init(ctx_.get(), &prog_sort_inclusive_scan_, log) ||
        !pi_sort_add_partial_sums_.Init(ctx_.get(), &prog_sort_add_partial_sums_, log) ||
        !pi_sort_init_count_table_.Init(ctx_.get(), &prog_sort_init_count_table_, log) ||
        !pi_sort_write_sorted_hashes_.Init(ctx_.get(), &prog_sort_write_sorted_hashes_, log) ||
        !pi_sort_reorder_rays_.Init(ctx_.get(), &prog_sort_reorder_rays_, log)
        //(use_hwrt_ && !pi_intersect_scene_rtpipe_.Init(ctx_.get(), &prog_intersect_scene_rtpipe_, log)) ||
        //(use_hwrt_ && !pi_intersect_scene_indirect_rtpipe_.Init(ctx_.get(), &prog_intersect_scene_indirect_rtpipe_,
        // log))
    ) {
        throw std::runtime_error("Error initializing pipeline!");
    }

    halton_seq_buf_ =
        Buffer{"Halton Seq", ctx_.get(), eBufType::Storage, sizeof(float) * HALTON_COUNT * HALTON_SEQ_LEN};
    counters_buf_ = Buffer{"Counters", ctx_.get(), eBufType::Storage, sizeof(uint32_t) * 32};
    indir_args_buf_ = Buffer{"Indir Args", ctx_.get(), eBufType::Indirect, 32 * sizeof(DispatchIndirectCommand)};

    { // zero out counters
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());

        const uint32_t zeros[32] = {};
        counters_buf_.UpdateImmediate(0, 32 * sizeof(uint32_t), zeros, cmd_buf);

        EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
    }

    { // create tonemap LUT texture
        Tex3DParams params = {};
        params.w = params.h = params.d = LUT_DIMS;
        params.usage = eTexUsage::Sampled | eTexUsage::Transfer;
        params.format = eTexFormat::RawRGB10_A2;
        params.sampling.filter = eTexFilter::BilinearNoMipmap;
        params.sampling.wrap = eTexWrap::ClampToEdge;

        tonemap_lut_ = Texture3D{"Tonemap LUT", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
    }

    Renderer::Resize(s.w, s.h);

    auto rand_func = std::bind(UniformIntDistribution<uint32_t>(), std::mt19937(0));
    permutations_ = Ray::ComputeRadicalInversePermutations(g_primes, PrimesCount, rand_func);
}

Ray::Dx::Renderer::~Renderer() {
    pixel_readback_buf_.Unmap();
    if (base_color_readback_buf_) {
        base_color_readback_buf_.Unmap();
    }
    if (depth_normals_readback_buf_) {
        depth_normals_readback_buf_.Unmap();
    }
}

const char *Ray::Dx::Renderer::device_name() const { return ctx_->device_name().c_str(); }

void Ray::Dx::Renderer::Resize(const int w, const int h) {
    if (w_ == w && h_ == h) {
        return;
    }

    const int num_pixels = w * h;

    Tex2DParams params;
    params.w = w;
    params.h = h;
    params.format = eTexFormat::RawRGBA32F;
    params.usage = eTexUsageBits::Sampled | eTexUsageBits::Storage | eTexUsageBits::Transfer;
    params.sampling.wrap = eTexWrap::ClampToEdge;

    temp_buf0_ = Texture2D{"Temp Image 0", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
    temp_buf1_ = Texture2D{"Temp Image 1", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
    dual_buf_[0] = Texture2D{"Dual Image [0]", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
    dual_buf_[1] = Texture2D{"Dual Image [1]", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
    final_buf_ = Texture2D{"Final Image", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
    raw_final_buf_ = Texture2D{"Raw Final Image", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
    raw_filtered_buf_ =
        Texture2D{"Raw Filtered Final Image", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};

    { // Texture that holds required sample count per pixel
        Tex2DParams uparams = params;
        uparams.format = eTexFormat::RawR16UI;
        required_samples_buf_ =
            Texture2D{"Required samples Image", ctx_.get(), uparams, ctx_->default_memory_allocs(), ctx_->log()};
    }

    if (frame_pixels_) {
        pixel_readback_buf_.Unmap();
        frame_pixels_ = nullptr;
    }
    pixel_readback_buf_ = Buffer{"Px Readback Buf", ctx_.get(), eBufType::Readback,
                                 uint32_t(round_up(4 * w * sizeof(float), TextureDataPitchAlignment) * h)};
    frame_pixels_ = (const color_rgba_t *)pixel_readback_buf_.Map(true /* persistent */);

    prim_rays_buf_ =
        Buffer{"Primary Rays", ctx_.get(), eBufType::Storage, uint32_t(sizeof(Types::ray_data_t) * num_pixels)};
    secondary_rays_buf_ =
        Buffer{"Secondary Rays", ctx_.get(), eBufType::Storage, uint32_t(sizeof(Types::ray_data_t) * num_pixels)};
    shadow_rays_buf_ =
        Buffer{"Shadow Rays", ctx_.get(), eBufType::Storage, uint32_t(sizeof(Types::shadow_ray_t) * num_pixels)};
    prim_hits_buf_ =
        Buffer{"Primary Hits", ctx_.get(), eBufType::Storage, uint32_t(sizeof(Types::hit_data_t) * num_pixels)};

    int scan_values_count = num_pixels;
    int scan_values_rounded_count =
        SORT_SCAN_PORTION * ((scan_values_count + SORT_SCAN_PORTION - 1) / SORT_SCAN_PORTION);

    ray_hashes_bufs_[0] = Buffer{"Ray Hashes #0", ctx_.get(), eBufType::Storage,
                                 uint32_t(sizeof(Types::ray_hash_t) * scan_values_rounded_count)};
    ray_hashes_bufs_[1] = Buffer{"Ray Hashes #1", ctx_.get(), eBufType::Storage,
                                 uint32_t(sizeof(Types::ray_hash_t) * scan_values_rounded_count)};
    count_table_buf_ =
        Buffer{"Count Table", ctx_.get(), eBufType::Storage, uint32_t(sizeof(uint32_t) * scan_values_rounded_count)};

    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());

    for (int i = 0; i < 4; ++i) {
        char name_buf[64];

        snprintf(name_buf, sizeof(name_buf), "Scan Values %i", i);
        scan_values_bufs_[i] =
            Buffer{name_buf, ctx_.get(), eBufType::Storage, uint32_t(sizeof(uint32_t) * scan_values_rounded_count)};
        scan_values_bufs_[i].Fill(0, uint32_t(sizeof(uint32_t) * scan_values_rounded_count), 0, cmd_buf);

        const int part_sums_count = (scan_values_count + SORT_SCAN_PORTION - 1) / SORT_SCAN_PORTION;
        const int part_sums_rounded_count =
            SORT_SCAN_PORTION * ((part_sums_count + SORT_SCAN_PORTION - 1) / SORT_SCAN_PORTION);
        snprintf(name_buf, sizeof(name_buf), "Partial Sums %i", i);
        partial_sums_bufs_[i] =
            Buffer{name_buf, ctx_.get(), eBufType::Storage, uint32_t(sizeof(uint32_t) * part_sums_rounded_count)};
        partial_sums_bufs_[i].Fill(0, uint32_t(sizeof(uint32_t) * part_sums_rounded_count), 0, cmd_buf);

        scan_values_count = (scan_values_count + SORT_SCAN_PORTION - 1) / SORT_SCAN_PORTION;
        scan_values_rounded_count =
            SORT_SCAN_PORTION * ((scan_values_count + SORT_SCAN_PORTION - 1) / SORT_SCAN_PORTION);
    }

    EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

    w_ = w;
    h_ = h;

    Clear(color_rgba_t{});
}

void Ray::Dx::Renderer::Clear(const color_rgba_t &c) {
    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());

    const TransitionInfo img_transitions[] = {
        {&dual_buf_[0], ResStateForClear},       {&dual_buf_[1], ResStateForClear},
        {&final_buf_, ResStateForClear},         {&raw_final_buf_, ResStateForClear},
        {&raw_filtered_buf_, ResStateForClear},  {&base_color_buf_, ResStateForClear},
        {&depth_normals_buf_, ResStateForClear}, {&required_samples_buf_, ResStateForClear}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, img_transitions);

    ClearColorImage(dual_buf_[0], c.v, cmd_buf);
    ClearColorImage(dual_buf_[1], c.v, cmd_buf);
    ClearColorImage(final_buf_, c.v, cmd_buf);
    ClearColorImage(raw_final_buf_, c.v, cmd_buf);
    ClearColorImage(raw_filtered_buf_, c.v, cmd_buf);
    if (base_color_buf_.ready()) {
        static const float rgba[] = {0.0f, 0.0f, 0.0f, 0.0f};
        ClearColorImage(base_color_buf_, rgba, cmd_buf);
    }
    if (depth_normals_buf_.ready()) {
        static const float rgba[] = {0.0f, 0.0f, 0.0f, 0.0f};
        ClearColorImage(depth_normals_buf_, rgba, cmd_buf);
    }
    { // Clear integer texture
        static const uint32_t rgba[4] = {0xffff, 0xffff, 0xffff, 0xffff};
        ClearColorImage(required_samples_buf_, rgba, cmd_buf);
    }

    EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
}

Ray::SceneBase *Ray::Dx::Renderer::CreateScene() {
    return new Dx::Scene(ctx_.get(), use_hwrt_, use_bindless_, use_tex_compression_);
}

void Ray::Dx::Renderer::RenderScene(const SceneBase *_s, RegionContext &region) {
    const auto s = dynamic_cast<const Dx::Scene *>(_s);
    if (!s) {
        return;
    }

    const uint32_t macro_tree_root = s->macro_nodes_start_;

    float root_min[3], cell_size[3];
    if (macro_tree_root != 0xffffffff) {
        float root_max[3];

        const bvh_node_t &root_node = s->tlas_root_node_;
        // s->nodes_.Get(macro_tree_root, root_node);

        UNROLLED_FOR(i, 3, {
            root_min[i] = root_node.bbox_min[i];
            root_max[i] = root_node.bbox_max[i];
        })

        UNROLLED_FOR(i, 3, { cell_size[i] = (root_max[i] - root_min[i]) / 255; })
    }

    region.iteration++;
    if (!region.halton_seq || region.iteration % HALTON_SEQ_LEN == 0) {
        UpdateHaltonSequence(region.iteration, region.halton_seq);
    }

    const Ray::camera_t &cam = s->cams_[s->current_cam()._index].cam;

    // allocate aux data on demand
    if (cam.pass_settings.flags & (Bitmask<ePassFlags>{ePassFlags::OutputBaseColor} | ePassFlags::OutputDepthNormals)) {
        const int w = final_buf_.params.w, h = final_buf_.params.h;

        Tex2DParams params;
        params.w = w;
        params.h = h;
        params.format = eTexFormat::RawRGBA32F;
        params.usage = eTexUsageBits::Storage | eTexUsageBits::Transfer | eTexUsageBits::Sampled;

        if (cam.pass_settings.flags & ePassFlags::OutputBaseColor) {
            if (!base_color_buf_.ready() || base_color_buf_.params.w != w || base_color_buf_.params.h != h) {
                base_color_buf_ = {};
                base_color_buf_ =
                    Texture2D{"Base Color Image", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
                if (base_color_pixels_) {
                    base_color_readback_buf_.Unmap();
                    base_color_pixels_ = nullptr;
                }
                base_color_readback_buf_ = {};
                base_color_readback_buf_ =
                    Buffer{"Base Color Stage Buf", ctx_.get(), eBufType::Readback,
                           uint32_t(round_up(4 * w * sizeof(float), TextureDataPitchAlignment) * h)};
                base_color_pixels_ = (const color_rgba_t *)base_color_readback_buf_.Map(true /* persistent */);

                // Perform initial clear
                const TransitionInfo img_transitions[] = {{&base_color_buf_, ResStateForClear}};
                CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());
                TransitionResourceStates(cmd_buf, AllStages, AllStages, img_transitions);
                static const float rgba[] = {0.0f, 0.0f, 0.0f, 0.0f};
                ClearColorImage(base_color_buf_, rgba, cmd_buf);
                EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
            }
        } else {
            base_color_buf_ = {};
            if (base_color_pixels_) {
                base_color_readback_buf_.Unmap();
                base_color_pixels_ = nullptr;
            }
            base_color_readback_buf_ = {};
        }
        if (cam.pass_settings.flags & ePassFlags::OutputDepthNormals) {
            if (!depth_normals_buf_.ready() || depth_normals_buf_.params.w != w || depth_normals_buf_.params.h != h) {
                temp_depth_normals_buf_ = {};
                temp_depth_normals_buf_ = Texture2D{"Temp Depth-Normals Image", ctx_.get(), params,
                                                    ctx_->default_memory_allocs(), ctx_->log()};
                depth_normals_buf_ = {};
                depth_normals_buf_ =
                    Texture2D{"Depth-Normals Image", ctx_.get(), params, ctx_->default_memory_allocs(), ctx_->log()};
                if (depth_normals_pixels_) {
                    depth_normals_readback_buf_.Unmap();
                    depth_normals_pixels_ = nullptr;
                }
                depth_normals_readback_buf_ = {};
                depth_normals_readback_buf_ =
                    Buffer{"Depth Normals Stage Buf", ctx_.get(), eBufType::Readback,
                           uint32_t(round_up(4 * w * sizeof(float), TextureDataPitchAlignment) * h)};
                depth_normals_pixels_ = (const color_rgba_t *)depth_normals_readback_buf_.Map(true /* persistent */);

                // Perform initial clear
                const TransitionInfo img_transitions[] = {{&depth_normals_buf_, ResStateForClear}};
                CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());
                TransitionResourceStates(cmd_buf, AllStages, AllStages, img_transitions);
                static const float rgba[] = {0.0f, 0.0f, 0.0f, 0.0f};
                ClearColorImage(depth_normals_buf_, rgba, cmd_buf);
                EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
            }
        } else {
            temp_depth_normals_buf_ = {};
            depth_normals_buf_ = {};
            if (depth_normals_pixels_) {
                depth_normals_readback_buf_.Unmap();
                depth_normals_pixels_ = nullptr;
            }
            depth_normals_readback_buf_ = {};
        }
    }

    if (loaded_halton_ == -1 || (region.iteration / HALTON_SEQ_LEN) != (loaded_halton_ / HALTON_SEQ_LEN)) {
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());

        Buffer temp_stage_buf{"Temp halton stage", ctx_.get(), eBufType::Upload, halton_seq_buf_.size()};
        { // update stage buffer
            uint8_t *mapped_ptr = temp_stage_buf.Map();
            memcpy(mapped_ptr, &region.halton_seq[0], sizeof(float) * HALTON_COUNT * HALTON_SEQ_LEN);
            temp_stage_buf.Unmap();
        }

        CopyBufferToBuffer(temp_stage_buf, 0, halton_seq_buf_, 0, sizeof(float) * HALTON_COUNT * HALTON_SEQ_LEN,
                           cmd_buf);

        EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

        temp_stage_buf.FreeImmediate();
        loaded_halton_ = region.iteration;
    }

    if (loaded_view_transform_ != cam.view_transform) {
        if (cam.view_transform != eViewTransform::Standard) {
            CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());

            const uint32_t data_len =
                LUT_DIMS * LUT_DIMS * round_up(LUT_DIMS * sizeof(uint32_t), TextureDataPitchAlignment);
            Buffer temp_upload_buf{"Temp tonemap LUT upload", ctx_.get(), eBufType::Upload, data_len};
            { // update stage buffer
                uint32_t *mapped_ptr = reinterpret_cast<uint32_t *>(temp_upload_buf.Map(BufMapWrite));
                const uint32_t *lut = transform_luts[int(cam.view_transform)];

                int i = 0;
                for (int yz = 0; yz < LUT_DIMS * LUT_DIMS; ++yz) {
                    memcpy(&mapped_ptr[i], &lut[yz * LUT_DIMS], LUT_DIMS * sizeof(uint32_t));
                    i += round_up(LUT_DIMS, TextureDataPitchAlignment / sizeof(uint32_t));
                }

                temp_upload_buf.Unmap();
            }

            const TransitionInfo res_transitions[] = {{&temp_upload_buf, eResState::CopySrc},
                                                      {&tonemap_lut_, eResState::CopyDst}};
            TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

            tonemap_lut_.SetSubImage(0, 0, 0, LUT_DIMS, LUT_DIMS, LUT_DIMS, eTexFormat::RawRGB10_A2, temp_upload_buf,
                                     cmd_buf, 0, data_len);

            EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

            temp_upload_buf.FreeImmediate();
        }
        loaded_view_transform_ = cam.view_transform;
    }

    const scene_data_t sc_data = {
        &s->env_, s->mesh_instances_.gpu_buf(), s->mi_indices_.buf(), s->meshes_.gpu_buf(), s->transforms_.gpu_buf(),
        s->vtx_indices_.buf(), s->vertices_.buf(), s->nodes_.buf(), s->tris_.buf(), s->tri_indices_.buf(),
        s->tri_materials_.buf(), s->materials_.gpu_buf(), s->atlas_textures_.gpu_buf(), s->lights_.gpu_buf(),
        s->li_indices_.buf(), int(s->li_indices_.size()), s->visible_lights_.buf(), int(s->visible_lights_.size()),
        s->blocker_lights_.buf(), int(s->blocker_lights_.size()),
        // s->rt_tlas_,
        s->env_map_qtree_.tex, int(s->env_map_qtree_.mips.size())};

#if !RUN_IN_LOCKSTEP
    if (ctx_->in_flight_fence(ctx_->backend_frame)->GetCompletedValue() < ctx_->fence_values[ctx_->backend_frame]) {
        HRESULT hr = ctx_->in_flight_fence(ctx_->backend_frame)
                         ->SetEventOnCompletion(ctx_->fence_values[ctx_->backend_frame], ctx_->fence_event());
        if (FAILED(hr)) {
            return;
        }

        WaitForSingleObject(ctx_->fence_event(), INFINITE);
    }

    ++ctx_->fence_values[ctx_->backend_frame];
#endif

    ctx_->ReadbackTimestampQueries(ctx_->backend_frame);
    ctx_->DestroyDeferredResources(ctx_->backend_frame);
    ctx_->default_descr_alloc()->Reset();
    ctx_->uniform_data_buf_offs[ctx_->backend_frame] = 0;

    stats_.time_primary_ray_gen_us = ctx_->GetTimestampIntervalDurationUs(
        timestamps_[ctx_->backend_frame].primary_ray_gen[0], timestamps_[ctx_->backend_frame].primary_ray_gen[1]);
    stats_.time_primary_trace_us = ctx_->GetTimestampIntervalDurationUs(
        timestamps_[ctx_->backend_frame].primary_trace[0], timestamps_[ctx_->backend_frame].primary_trace[1]);
    stats_.time_primary_shade_us = ctx_->GetTimestampIntervalDurationUs(
        timestamps_[ctx_->backend_frame].primary_shade[0], timestamps_[ctx_->backend_frame].primary_shade[1]);
    stats_.time_primary_shadow_us = ctx_->GetTimestampIntervalDurationUs(
        timestamps_[ctx_->backend_frame].primary_shadow[0], timestamps_[ctx_->backend_frame].primary_shadow[1]);

    stats_.time_secondary_sort_us = 0;
    for (int i = 0; i < int(timestamps_[ctx_->backend_frame].secondary_sort.size()); i += 2) {
        stats_.time_secondary_sort_us +=
            ctx_->GetTimestampIntervalDurationUs(timestamps_[ctx_->backend_frame].secondary_sort[i + 0],
                                                 timestamps_[ctx_->backend_frame].secondary_sort[i + 1]);
    }

    stats_.time_secondary_trace_us = 0;
    for (int i = 0; i < int(timestamps_[ctx_->backend_frame].secondary_trace.size()); i += 2) {
        stats_.time_secondary_trace_us +=
            ctx_->GetTimestampIntervalDurationUs(timestamps_[ctx_->backend_frame].secondary_trace[i + 0],
                                                 timestamps_[ctx_->backend_frame].secondary_trace[i + 1]);
    }

    stats_.time_secondary_shade_us = 0;
    for (int i = 0; i < int(timestamps_[ctx_->backend_frame].secondary_shade.size()); i += 2) {
        stats_.time_secondary_shade_us +=
            ctx_->GetTimestampIntervalDurationUs(timestamps_[ctx_->backend_frame].secondary_shade[i + 0],
                                                 timestamps_[ctx_->backend_frame].secondary_shade[i + 1]);
    }

    stats_.time_secondary_shadow_us = 0;
    for (int i = 0; i < int(timestamps_[ctx_->backend_frame].secondary_shadow.size()); i += 2) {
        stats_.time_secondary_shadow_us +=
            ctx_->GetTimestampIntervalDurationUs(timestamps_[ctx_->backend_frame].secondary_shadow[i + 0],
                                                 timestamps_[ctx_->backend_frame].secondary_shadow[i + 1]);
    }

#if RUN_IN_LOCKSTEP
    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());
#else
    ID3D12CommandAllocator *cmd_alloc = ctx_->draw_cmd_alloc(ctx_->backend_frame);
    CommandBuffer cmd_buf = ctx_->draw_cmd_buf();

    HRESULT hr = cmd_alloc->Reset();
    if (FAILED(hr)) {
        ctx_->log()->Error("Failed to reset command allocator!");
    }

    hr = cmd_buf->Reset(cmd_alloc, nullptr);
    if (FAILED(hr)) {
        ctx_->log()->Error("Failed to reset command list!");
        return;
    }
#endif

    //////////////////////////////////////////////////////////////////////////////////

    const int hi = (region.iteration % HALTON_SEQ_LEN) * HALTON_COUNT;

    { // transition resources
        SmallVector<TransitionInfo, 16> res_transitions;

        // for (const auto &tex_atlas : s->tex_atlases_) {
        //     if (tex_atlas.resource_state != eResState::ShaderResource) {
        //         res_transitions.emplace_back(&tex_atlas, eResState::ShaderResource);
        //     }
        // }

        if (sc_data.mi_indices && sc_data.mi_indices.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.mi_indices, eResState::ShaderResource);
        }
        if (sc_data.meshes && sc_data.meshes.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.meshes, eResState::ShaderResource);
        }
        if (sc_data.transforms && sc_data.transforms.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.transforms, eResState::ShaderResource);
        }
        if (sc_data.vtx_indices && sc_data.vtx_indices.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.vtx_indices, eResState::ShaderResource);
        }
        if (sc_data.vertices && sc_data.vertices.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.vertices, eResState::ShaderResource);
        }
        if (sc_data.nodes && sc_data.nodes.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.nodes, eResState::ShaderResource);
        }
        if (sc_data.tris && sc_data.tris.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.tris, eResState::ShaderResource);
        }
        if (sc_data.tri_indices && sc_data.tri_indices.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.tri_indices, eResState::ShaderResource);
        }
        if (sc_data.tri_materials && sc_data.tri_materials.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.tri_materials, eResState::ShaderResource);
        }
        if (sc_data.materials && sc_data.materials.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.materials, eResState::ShaderResource);
        }
        if (sc_data.atlas_textures && sc_data.atlas_textures.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.atlas_textures, eResState::ShaderResource);
        }
        if (sc_data.lights && sc_data.lights.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.lights, eResState::ShaderResource);
        }
        if (sc_data.li_indices && sc_data.li_indices.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.li_indices, eResState::ShaderResource);
        }
        if (sc_data.visible_lights && sc_data.visible_lights.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.visible_lights, eResState::ShaderResource);
        }
        if (sc_data.env_qtree.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.env_qtree, eResState::ShaderResource);
        }

        TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);
    }
#if 0
    VkMemoryBarrier mem_barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mem_barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    mem_barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd_buf, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1,
                         &mem_barrier, 0, nullptr, 0, nullptr);
#endif

    const rect_t rect = region.rect();

    { // generate primary rays
        DebugMarker _(cmd_buf, "GeneratePrimaryRays");
        timestamps_[ctx_->backend_frame].primary_ray_gen[0] = ctx_->WriteTimestamp(cmd_buf, true);
        kernel_GeneratePrimaryRays(cmd_buf, cam, hi, rect, halton_seq_buf_, region.iteration, required_samples_buf_,
                                   counters_buf_, prim_rays_buf_);
        timestamps_[ctx_->backend_frame].primary_ray_gen[1] = ctx_->WriteTimestamp(cmd_buf, false);
    }

    { // prepare indirect args
        DebugMarker _(cmd_buf, "PrepareIndirArgs");
        kernel_PrepareIndirArgs(cmd_buf, counters_buf_, indir_args_buf_);
    }

#if DEBUG_HWRT
    { // debug
        DebugMarker _(cmd_buf, "Debug HWRT");
        kernel_DebugRT(cmd_buf, sc_data, macro_tree_root, prim_rays_buf_, temp_buf_);
    }
#else
    { // trace primary rays
        DebugMarker _(cmd_buf, "IntersectScenePrimary");
        timestamps_[ctx_->backend_frame].primary_trace[0] = ctx_->WriteTimestamp(cmd_buf, true);
        kernel_IntersectScene(cmd_buf, indir_args_buf_, 0, counters_buf_, cam.pass_settings, sc_data, halton_seq_buf_,
                              hi + RAND_DIM_BASE_COUNT, macro_tree_root, cam.clip_end - cam.clip_start,
                              // s->tex_atlases_,
                              s->bindless_tex_data_, prim_rays_buf_, prim_hits_buf_);
        timestamps_[ctx_->backend_frame].primary_trace[1] = ctx_->WriteTimestamp(cmd_buf, false);
    }

    Texture2D &temp_base_color = temp_buf1_;

    { // shade primary hits
        DebugMarker _(cmd_buf, "ShadePrimaryHits");
        timestamps_[ctx_->backend_frame].primary_shade[0] = ctx_->WriteTimestamp(cmd_buf, true);
        kernel_ShadePrimaryHits(cmd_buf, cam.pass_settings, s->env_, indir_args_buf_, 0, prim_hits_buf_, prim_rays_buf_,
                                sc_data, halton_seq_buf_, hi + RAND_DIM_BASE_COUNT, rect, // s->tex_atlases_,
                                s->bindless_tex_data_, temp_buf0_, secondary_rays_buf_, shadow_rays_buf_, counters_buf_,
                                temp_base_color, temp_depth_normals_buf_);
        timestamps_[ctx_->backend_frame].primary_shade[1] = ctx_->WriteTimestamp(cmd_buf, false);
    }

    { // prepare indirect args
        DebugMarker _(cmd_buf, "PrepareIndirArgs");
        kernel_PrepareIndirArgs(cmd_buf, counters_buf_, indir_args_buf_);
    }

    { // trace shadow rays
        DebugMarker _(cmd_buf, "TraceShadow");
        timestamps_[ctx_->backend_frame].primary_shadow[0] = ctx_->WriteTimestamp(cmd_buf, true);
        kernel_IntersectSceneShadow(cmd_buf, cam.pass_settings, indir_args_buf_, 2, counters_buf_, sc_data,
                                    halton_seq_buf_, hi + RAND_DIM_BASE_COUNT, macro_tree_root,
                                    cam.pass_settings.clamp_direct, // s->tex_atlases_,
                                    s->bindless_tex_data_, shadow_rays_buf_, temp_buf0_);
        timestamps_[ctx_->backend_frame].primary_shadow[1] = ctx_->WriteTimestamp(cmd_buf, false);
    }

    timestamps_[ctx_->backend_frame].secondary_sort.clear();
    timestamps_[ctx_->backend_frame].secondary_trace.clear();
    timestamps_[ctx_->backend_frame].secondary_shade.clear();
    timestamps_[ctx_->backend_frame].secondary_shadow.clear();

    for (int bounce = 1; bounce <= cam.pass_settings.max_total_depth; ++bounce) {
#if !DISABLE_SORTING
        timestamps_[ctx_->backend_frame].secondary_sort.push_back(ctx_->WriteTimestamp(cmd_buf, true));

        if (!use_hwrt_) {
            DebugMarker _(cmd_buf, "Sort Rays");

            kernel_SortHashRays(cmd_buf, indir_args_buf_, secondary_rays_buf_, counters_buf_, root_min, cell_size,
                                ray_hashes_bufs_[0]);
            RadixSort(cmd_buf, indir_args_buf_, ray_hashes_bufs_, count_table_buf_, counters_buf_, partial_sums_bufs_,
                      scan_values_bufs_);
            kernel_SortReorderRays(cmd_buf, indir_args_buf_, 0, secondary_rays_buf_, ray_hashes_bufs_[0], counters_buf_,
                                   1, prim_rays_buf_);

            std::swap(secondary_rays_buf_, prim_rays_buf_);
        }

        timestamps_[ctx_->backend_frame].secondary_sort.push_back(ctx_->WriteTimestamp(cmd_buf, false));
#endif // !DISABLE_SORTING

        timestamps_[ctx_->backend_frame].secondary_trace.push_back(ctx_->WriteTimestamp(cmd_buf, true));
        { // trace secondary rays
            DebugMarker _(cmd_buf, "IntersectSceneSecondary");
            kernel_IntersectScene(cmd_buf, indir_args_buf_, 0, counters_buf_, cam.pass_settings, sc_data,
                                  halton_seq_buf_, hi + RAND_DIM_BASE_COUNT, macro_tree_root,
                                  MAX_DIST, // s->tex_atlases_,
                                  s->bindless_tex_data_, secondary_rays_buf_, prim_hits_buf_);
        }

        if (sc_data.visible_lights_count) {
            DebugMarker _(cmd_buf, "IntersectAreaLights");
            kernel_IntersectAreaLights(cmd_buf, sc_data, indir_args_buf_, counters_buf_, secondary_rays_buf_,
                                       prim_hits_buf_);
        }

        timestamps_[ctx_->backend_frame].secondary_trace.push_back(ctx_->WriteTimestamp(cmd_buf, false));

        { // shade secondary hits
            DebugMarker _(cmd_buf, "ShadeSecondaryHits");
            timestamps_[ctx_->backend_frame].secondary_shade.push_back(ctx_->WriteTimestamp(cmd_buf, true));
            kernel_ShadeSecondaryHits(cmd_buf, cam.pass_settings, s->env_, indir_args_buf_, 0, prim_hits_buf_,
                                      secondary_rays_buf_, sc_data, halton_seq_buf_, hi + RAND_DIM_BASE_COUNT,
                                      // s->tex_atlases_,
                                      s->bindless_tex_data_, temp_buf0_, prim_rays_buf_, shadow_rays_buf_,
                                      counters_buf_);
            timestamps_[ctx_->backend_frame].secondary_shade.push_back(ctx_->WriteTimestamp(cmd_buf, false));
        }

        { // prepare indirect args
            DebugMarker _(cmd_buf, "PrepareIndirArgs");
            kernel_PrepareIndirArgs(cmd_buf, counters_buf_, indir_args_buf_);
        }

        { // trace shadow rays
            DebugMarker _(cmd_buf, "TraceShadow");
            timestamps_[ctx_->backend_frame].secondary_shadow.push_back(ctx_->WriteTimestamp(cmd_buf, true));
            kernel_IntersectSceneShadow(cmd_buf, cam.pass_settings, indir_args_buf_, 2, counters_buf_, sc_data,
                                        halton_seq_buf_, hi + RAND_DIM_BASE_COUNT, macro_tree_root,
                                        cam.pass_settings.clamp_indirect, // s->tex_atlases_,
                                        s->bindless_tex_data_, shadow_rays_buf_, temp_buf0_);
            timestamps_[ctx_->backend_frame].secondary_shadow.push_back(ctx_->WriteTimestamp(cmd_buf, false));
        }

        std::swap(secondary_rays_buf_, prim_rays_buf_);
    }
#endif

    { // prepare result
        DebugMarker _(cmd_buf, "Prepare Result");

        Texture2D &clean_buf = dual_buf_[(region.iteration - 1) % 2];

        // factor used to compute incremental average
        const float main_mix_factor = 1.0f / float((region.iteration + 1) / 2);
        const float aux_mix_factor = 1.0f / float(region.iteration);

        kernel_MixIncremental(cmd_buf, main_mix_factor, aux_mix_factor, rect, region.iteration, temp_buf0_,
                              temp_base_color, temp_depth_normals_buf_, required_samples_buf_, clean_buf,
                              base_color_buf_, depth_normals_buf_);
    }

    { // output final buffer, prepare variance
        DebugMarker _(cmd_buf, "Postprocess frame");

        const int p1_samples = (region.iteration + 1) / 2;
        const int p2_samples = (region.iteration) / 2;

        const float p1_weight = float(p1_samples) / float(region.iteration);
        const float p2_weight = float(p2_samples) / float(region.iteration);

        const float exposure = std::pow(2.0f, cam.exposure);
        tonemap_params_.view_transform = cam.view_transform;
        tonemap_params_.inv_gamma = (1.0f / cam.gamma);

        variance_threshold_ = region.iteration > cam.pass_settings.min_samples
                                  ? 0.5f * cam.pass_settings.variance_threshold * cam.pass_settings.variance_threshold
                                  : 0.0f;

        kernel_Postprocess(cmd_buf, dual_buf_[0], p1_weight, dual_buf_[1], p2_weight, exposure,
                           tonemap_params_.inv_gamma, rect, variance_threshold_, region.iteration, final_buf_,
                           raw_final_buf_, temp_buf0_, required_samples_buf_);
        // Also store as denosed result until Denoise method will be called
        const TransitionInfo img_transitions[] = {{&raw_final_buf_, eResState::CopySrc},
                                                  {&raw_filtered_buf_, eResState::CopyDst}};
        TransitionResourceStates(cmd_buf, AllStages, AllStages, img_transitions);
        CopyImageToImage(cmd_buf, raw_final_buf_, 0, rect.x, rect.y, raw_filtered_buf_, 0, rect.x, rect.y, rect.w,
                         rect.h);
    }

#if RUN_IN_LOCKSTEP
    EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
#else
    ctx_->ResolveTimestampQueries(ctx_->backend_frame);

    hr = cmd_buf->Close();
    if (FAILED(hr)) {
        return;
    }

    const int prev_frame = (ctx_->backend_frame + MaxFramesInFlight - 1) % MaxFramesInFlight;

    ID3D12CommandList *pp_cmd_bufs[] = {cmd_buf};
    ctx_->graphics_queue()->ExecuteCommandLists(1, pp_cmd_bufs);

    hr = ctx_->graphics_queue()->Signal(ctx_->in_flight_fence(ctx_->backend_frame),
                                        ctx_->fence_values[ctx_->backend_frame]);
    if (FAILED(hr)) {
        return;
    }

    ctx_->render_finished_semaphore_is_set[ctx_->backend_frame] = true;
    ctx_->render_finished_semaphore_is_set[prev_frame] = false;

    ctx_->backend_frame = (ctx_->backend_frame + 1) % MaxFramesInFlight;
#endif
    frame_dirty_ = base_color_dirty_ = depth_normals_dirty_ = true;
}

void Ray::Dx::Renderer::DenoiseImage(const RegionContext &region) {
#if !RUN_IN_LOCKSTEP
    if (ctx_->in_flight_fence(ctx_->backend_frame)->GetCompletedValue() < ctx_->fence_values[ctx_->backend_frame]) {
        HRESULT hr = ctx_->in_flight_fence(ctx_->backend_frame)
                         ->SetEventOnCompletion(ctx_->fence_values[ctx_->backend_frame], ctx_->fence_event());
        if (FAILED(hr)) {
            return;
        }

        WaitForSingleObject(ctx_->fence_event(), INFINITE);
    }

    ++ctx_->fence_values[ctx_->backend_frame];
#endif

    ctx_->ReadbackTimestampQueries(ctx_->backend_frame);
    ctx_->DestroyDeferredResources(ctx_->backend_frame);
    ctx_->default_descr_alloc()->Reset();
    ctx_->uniform_data_buf_offs[ctx_->backend_frame] = 0;

    stats_.time_denoise_us = ctx_->GetTimestampIntervalDurationUs(timestamps_[ctx_->backend_frame].denoise[0],
                                                                  timestamps_[ctx_->backend_frame].denoise[1]);

#if RUN_IN_LOCKSTEP
    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());
#else
    ID3D12CommandAllocator *cmd_alloc = ctx_->draw_cmd_alloc(ctx_->backend_frame);
    CommandBuffer cmd_buf = ctx_->draw_cmd_buf();

    HRESULT hr = cmd_alloc->Reset();
    if (FAILED(hr)) {
        ctx_->log()->Error("Failed to reset command allocator!");
    }

    hr = cmd_buf->Reset(cmd_alloc, nullptr);
    if (FAILED(hr)) {
        ctx_->log()->Error("Failed to reset command list!");
        return;
    }
#endif

    // vkCmdResetQueryPool(cmd_buf, ctx_->query_pool(ctx_->backend_frame), 0, MaxTimestampQueries);

    //////////////////////////////////////////////////////////////////////////////////

    timestamps_[ctx_->backend_frame].denoise[0] = ctx_->WriteTimestamp(cmd_buf, true);

    const rect_t &rect = region.rect();

    const auto &raw_variance = temp_buf0_;
    const auto &filtered_variance = temp_buf1_;

    { // Filter variance
        DebugMarker _(cmd_buf, "Filter Variance");
        kernel_FilterVariance(cmd_buf, raw_variance, rect, variance_threshold_, region.iteration, filtered_variance,
                              required_samples_buf_);
    }

    { // Apply NLM Filter
        DebugMarker _(cmd_buf, "NLM Filter");
        kernel_NLMFilter(cmd_buf, raw_final_buf_, filtered_variance, 1.0f, 0.45f, base_color_buf_, 64.0f,
                         depth_normals_buf_, 32.0f, raw_filtered_buf_, tonemap_params_.view_transform,
                         tonemap_params_.inv_gamma, rect, final_buf_);
    }

    timestamps_[ctx_->backend_frame].denoise[1] = ctx_->WriteTimestamp(cmd_buf, false);

    //////////////////////////////////////////////////////////////////////////////////

#if RUN_IN_LOCKSTEP
    EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
#else
    hr = cmd_buf->Close();
    if (FAILED(hr)) {
        return;
    }

    const int prev_frame = (ctx_->backend_frame + MaxFramesInFlight - 1) % MaxFramesInFlight;

    ID3D12CommandList *pp_cmd_bufs[] = {cmd_buf};
    ctx_->graphics_queue()->ExecuteCommandLists(1, pp_cmd_bufs);

    hr = ctx_->graphics_queue()->Signal(ctx_->in_flight_fence(ctx_->backend_frame),
                                        ctx_->fence_values[ctx_->backend_frame]);
    if (FAILED(hr)) {
        return;
    }

    ctx_->render_finished_semaphore_is_set[ctx_->backend_frame] = true;
    ctx_->render_finished_semaphore_is_set[prev_frame] = false;

    ctx_->backend_frame = (ctx_->backend_frame + 1) % MaxFramesInFlight;
#endif
}

void Ray::Dx::Renderer::UpdateHaltonSequence(const int iteration, std::unique_ptr<float[]> &seq) {
    if (!seq) {
        seq.reset(new float[HALTON_COUNT * HALTON_SEQ_LEN]);
    }

    for (int i = 0; i < HALTON_SEQ_LEN; ++i) {
        uint32_t prime_sum = 0;
        for (int j = 0; j < HALTON_COUNT; ++j) {
            seq[i * HALTON_COUNT + j] =
                ScrambledRadicalInverse(g_primes[j], &permutations_[prime_sum], uint64_t(iteration) + i);
            prime_sum += g_primes[j];
        }
    }
}

void Ray::Dx::Renderer::RadixSort(CommandBuffer cmd_buf, const Buffer &indir_args, Buffer _hashes[2],
                                  Buffer &count_table, const Buffer &counters, Buffer partial_sums[],
                                  Buffer scan_values[]) {
    DebugMarker _(cmd_buf, "Radix Sort");

    static const int indir_args_indices[] = {6, 7, 8, 9};

    static const char *MarkerStrings[] = {"Radix Sort Iter #0 [Bits   0-4]", "Radix Sort Iter #1 [Bits   4-8]",
                                          "Radix Sort Iter #2 [Bits  8-12]", "Radix Sort Iter #3 [Bits 12-16]",
                                          "Radix Sort Iter #4 [Bits 16-20]", "Radix Sort Iter #5 [Bits 20-24]",
                                          "Radix Sort Iter #6 [Bits 24-28]", "Radix Sort Iter #7 [Bits 28-32]"};

    Buffer *hashes[] = {&_hashes[0], &_hashes[1]};
    for (int shift = 0; shift < 32; shift += 4) {
        DebugMarker _(cmd_buf, MarkerStrings[shift / 4]);

        kernel_SortInitCountTable(cmd_buf, shift, indir_args, 4, *hashes[0], counters, 4, count_table);
        ExclusiveScan(cmd_buf, indir_args, indir_args_indices, count_table, 0 /* offset */, 1 /* stride */,
                      partial_sums, scan_values);
        kernel_SortWriteSortedHashes(cmd_buf, shift, indir_args, 5, *hashes[0], scan_values[0], counters, 5, 4,
                                     *hashes[1]);
        std::swap(hashes[0], hashes[1]);
    }
    assert(hashes[0] == &_hashes[0]);
}

void Ray::Dx::Renderer::ExclusiveScan(CommandBuffer cmd_buf, const Buffer &indir_args, const int indir_args_indices[],
                                      const Buffer &input, const uint32_t offset, const uint32_t stride,
                                      const Buffer partial_sums[], const Buffer scan_values[]) {
    DebugMarker _(cmd_buf, "Exclusive Scan");

    kernel_SortExclusiveScan(cmd_buf, indir_args, indir_args_indices[0], input, offset, stride, scan_values[0],
                             partial_sums[0]);

    kernel_SortInclusiveScan(cmd_buf, indir_args, indir_args_indices[1], partial_sums[0], 0 /* offset */,
                             1 /* stride */, scan_values[1], partial_sums[1]);

    { //
        kernel_SortInclusiveScan(cmd_buf, indir_args, indir_args_indices[2], partial_sums[1], 0 /* offset */,
                                 1 /* stride */, scan_values[2], partial_sums[2]);
        { //
            kernel_SortInclusiveScan(cmd_buf, indir_args, indir_args_indices[3], partial_sums[2], 0 /* offset */,
                                     1 /* stride */, scan_values[3], partial_sums[3]);
            kernel_SortAddPartialSums(cmd_buf, indir_args, indir_args_indices[2], scan_values[3], scan_values[2]);
        }

        kernel_SortAddPartialSums(cmd_buf, indir_args, indir_args_indices[1], scan_values[2], scan_values[1]);
    }

    kernel_SortAddPartialSums(cmd_buf, indir_args, indir_args_indices[0], scan_values[1], scan_values[0]);
}

Ray::color_data_rgba_t Ray::Dx::Renderer::get_pixels_ref(const bool tonemap) const {
    if (frame_dirty_ || pixel_readback_is_tonemapped_ != tonemap) {
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());

        { // download result
            DebugMarker _(cmd_buf, "Download Result");

            // TODO: fix this!
            const auto &buffer_to_use = tonemap ? final_buf_ : raw_filtered_buf_;

            const TransitionInfo res_transitions[] = {{&buffer_to_use, eResState::CopySrc},
                                                      {&pixel_readback_buf_, eResState::CopyDst}};
            TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

            CopyImageToBuffer(buffer_to_use, 0, 0, 0, w_, h_, pixel_readback_buf_, cmd_buf, 0);
        }

        EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

        // Can be reset after vkQueueWaitIdle
        for (bool &is_set : ctx_->render_finished_semaphore_is_set) {
            is_set = false;
        }

        pixel_readback_buf_.FlushMappedRange(0, pixel_readback_buf_.size());
        frame_dirty_ = false;
        pixel_readback_is_tonemapped_ = tonemap;
    }

    return {frame_pixels_, round_up(w_, TextureDataPitchAlignment / sizeof(color_rgba_t))};
}

Ray::color_data_rgba_t Ray::Dx::Renderer::get_aux_pixels_ref(const eAUXBuffer buf) const {
    bool &dirty_flag = (buf == eAUXBuffer::BaseColor) ? base_color_dirty_ : depth_normals_dirty_;

    const auto &buffer_to_use = (buf == eAUXBuffer::BaseColor) ? base_color_buf_ : depth_normals_buf_;
    const auto &stage_buffer_to_use =
        (buf == eAUXBuffer::BaseColor) ? base_color_readback_buf_ : depth_normals_readback_buf_;

    if (dirty_flag) {
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->device(), ctx_->temp_command_pool());

        { // download result
            DebugMarker _(cmd_buf, "Download Result");

            const TransitionInfo res_transitions[] = {{&buffer_to_use, eResState::CopySrc},
                                                      {&stage_buffer_to_use, eResState::CopyDst}};
            TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

            CopyImageToBuffer(buffer_to_use, 0, 0, 0, w_, h_, stage_buffer_to_use, cmd_buf, 0);
        }

        EndSingleTimeCommands(ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

        // Can be reset after vkQueueWaitIdle
        for (bool &is_set : ctx_->render_finished_semaphore_is_set) {
            is_set = false;
        }

        stage_buffer_to_use.FlushMappedRange(0, stage_buffer_to_use.size());
        dirty_flag = false;
    }

    return {((buf == eAUXBuffer::BaseColor) ? base_color_pixels_ : depth_normals_pixels_),
            round_up(w_, TextureDataPitchAlignment / sizeof(color_rgba_t))};
}