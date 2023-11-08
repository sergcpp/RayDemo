#include "RendererDX.h"

#include <functional>
#include <utility>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <d3d12.h>

#include "CDFUtils.h"
#include "Core.h"
#include "CoreDX.h"
#include "SceneDX.h"
#include "UNetFilter.h"

#include "Dx/BufferDX.h"
#include "Dx/DebugMarkerDX.h"
#include "Dx/DescriptorPoolDX.h"
#include "Dx/DrawCallDX.h"
#include "Dx/PipelineDX.h"
#include "Dx/ProgramDX.h"
#include "Dx/SamplerDX.h"
#include "Dx/ShaderDX.h"
#include "Dx/TextureDX.h"

#include "../Log.h"

#include "shaders/sort_common.h"
#include "shaders/types.h"

#define DEBUG_HWRT 0
#define RUN_IN_LOCKSTEP 0
#define DISABLE_SORTING 0
#define ENABLE_RT_PIPELINE 0

static_assert(sizeof(Types::tri_accel_t) == sizeof(Ray::tri_accel_t), "!");
static_assert(sizeof(Types::bvh_node_t) == sizeof(Ray::bvh_node_t), "!");
static_assert(sizeof(Types::light_bvh_node_t) == sizeof(Ray::light_bvh_node_t), "!");
static_assert(sizeof(Types::light_wbvh_node_t) == sizeof(Ray::light_wbvh_node_t), "!");
static_assert(sizeof(Types::vertex_t) == sizeof(Ray::vertex_t), "!");
static_assert(sizeof(Types::mesh_t) == sizeof(Ray::mesh_t), "!");
static_assert(sizeof(Types::mesh_instance_t) == sizeof(Ray::mesh_instance_t), "!");
static_assert(sizeof(Types::light_t) == sizeof(Ray::light_t), "!");
static_assert(sizeof(Types::material_t) == sizeof(Ray::material_t), "!");
static_assert(sizeof(Types::atlas_texture_t) == sizeof(Ray::atlas_texture_t), "!");
static_assert(sizeof(Types::ray_chunk_t) == sizeof(Ray::ray_chunk_t), "!");
static_assert(sizeof(Types::ray_hash_t) == sizeof(Ray::ray_hash_t), "!");

static_assert(Types::LIGHT_TYPE_SPHERE == Ray::LIGHT_TYPE_SPHERE, "!");
static_assert(Types::LIGHT_TYPE_DIR == Ray::LIGHT_TYPE_DIR, "!");
static_assert(Types::LIGHT_TYPE_LINE == Ray::LIGHT_TYPE_LINE, "!");
static_assert(Types::LIGHT_TYPE_RECT == Ray::LIGHT_TYPE_RECT, "!");
static_assert(Types::LIGHT_TYPE_DISK == Ray::LIGHT_TYPE_DISK, "!");
static_assert(Types::LIGHT_TYPE_TRI == Ray::LIGHT_TYPE_TRI, "!");
static_assert(Types::FILTER_BOX == int(Ray::ePixelFilter::Box), "!");
static_assert(Types::FILTER_GAUSSIAN == int(Ray::ePixelFilter::Gaussian), "!");
static_assert(Types::FILTER_BLACKMAN_HARRIS == int(Ray::ePixelFilter::BlackmanHarris), "!");
static_assert(Types::FILTER_TABLE_SIZE == Ray::FILTER_TABLE_SIZE, "!");

namespace Ray {
extern const int LUT_DIMS;
extern const uint32_t *transform_luts[];

int round_up(int v, int align);
namespace Dx {
#include "shaders/output/convolution_112_112_fp16.comp.cso.inl"
#include "shaders/output/convolution_112_112_fp32.comp.cso.inl"
#include "shaders/output/convolution_32_32_Downsample_fp16.comp.cso.inl"
#include "shaders/output/convolution_32_32_Downsample_fp32.comp.cso.inl"
#include "shaders/output/convolution_32_3_img_fp16.comp.cso.inl"
#include "shaders/output/convolution_32_3_img_fp32.comp.cso.inl"
#include "shaders/output/convolution_32_48_Downsample_fp16.comp.cso.inl"
#include "shaders/output/convolution_32_48_Downsample_fp32.comp.cso.inl"
#include "shaders/output/convolution_48_64_Downsample_fp16.comp.cso.inl"
#include "shaders/output/convolution_48_64_Downsample_fp32.comp.cso.inl"
#include "shaders/output/convolution_64_32_fp16.comp.cso.inl"
#include "shaders/output/convolution_64_32_fp32.comp.cso.inl"
#include "shaders/output/convolution_64_64_fp16.comp.cso.inl"
#include "shaders/output/convolution_64_64_fp32.comp.cso.inl"
#include "shaders/output/convolution_64_80_Downsample_fp16.comp.cso.inl"
#include "shaders/output/convolution_64_80_Downsample_fp32.comp.cso.inl"
#include "shaders/output/convolution_80_96_fp16.comp.cso.inl"
#include "shaders/output/convolution_80_96_fp32.comp.cso.inl"
#include "shaders/output/convolution_96_96_fp16.comp.cso.inl"
#include "shaders/output/convolution_96_96_fp32.comp.cso.inl"
#include "shaders/output/convolution_Img_3_32_fp16.comp.cso.inl"
#include "shaders/output/convolution_Img_3_32_fp32.comp.cso.inl"
#include "shaders/output/convolution_Img_6_32_fp16.comp.cso.inl"
#include "shaders/output/convolution_Img_6_32_fp32.comp.cso.inl"
#include "shaders/output/convolution_Img_9_32_fp16.comp.cso.inl"
#include "shaders/output/convolution_Img_9_32_fp32.comp.cso.inl"
#include "shaders/output/convolution_concat_112_48_96_fp16.comp.cso.inl"
#include "shaders/output/convolution_concat_112_48_96_fp32.comp.cso.inl"
#include "shaders/output/convolution_concat_64_3_64_fp16.comp.cso.inl"
#include "shaders/output/convolution_concat_64_3_64_fp32.comp.cso.inl"
#include "shaders/output/convolution_concat_64_6_64_fp16.comp.cso.inl"
#include "shaders/output/convolution_concat_64_6_64_fp32.comp.cso.inl"
#include "shaders/output/convolution_concat_64_9_64_fp16.comp.cso.inl"
#include "shaders/output/convolution_concat_64_9_64_fp32.comp.cso.inl"
#include "shaders/output/convolution_concat_96_32_64_fp16.comp.cso.inl"
#include "shaders/output/convolution_concat_96_32_64_fp32.comp.cso.inl"
#include "shaders/output/convolution_concat_96_64_112_fp16.comp.cso.inl"
#include "shaders/output/convolution_concat_96_64_112_fp32.comp.cso.inl"
#include "shaders/output/debug_rt.comp.cso.inl"
#include "shaders/output/filter_variance.comp.cso.inl"
#include "shaders/output/intersect_area_lights.comp.cso.inl"
// #include "shaders/output/intersect_scene.rchit.cso.inl"
// #include "shaders/output/intersect_scene.rgen.cso.inl"
// #include "shaders/output/intersect_scene.rmiss.cso.inl"
#include "shaders/output/intersect_scene_hwrt_atlas.comp.cso.inl"
#include "shaders/output/intersect_scene_hwrt_bindless.comp.cso.inl"
// #include "shaders/output/intersect_scene_indirect.rgen.cso.inl"
#include "shaders/output/intersect_scene_indirect_hwrt_atlas.comp.cso.inl"
#include "shaders/output/intersect_scene_indirect_hwrt_bindless.comp.cso.inl"
#include "shaders/output/intersect_scene_indirect_swrt_atlas.comp.cso.inl"
#include "shaders/output/intersect_scene_indirect_swrt_bindless.comp.cso.inl"
#include "shaders/output/intersect_scene_shadow_hwrt_atlas.comp.cso.inl"
#include "shaders/output/intersect_scene_shadow_hwrt_bindless.comp.cso.inl"
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
#include "shaders/output/sort_hash_rays.comp.cso.inl"
#include "shaders/output/sort_init_count_table.comp.cso.inl"
#include "shaders/output/sort_reduce.comp.cso.inl"
#include "shaders/output/sort_reorder_rays.comp.cso.inl"
#include "shaders/output/sort_scan.comp.cso.inl"
#include "shaders/output/sort_scan_add.comp.cso.inl"
#include "shaders/output/sort_scatter.comp.cso.inl"
} // namespace Dx
} // namespace Ray

#define NS Dx
#include "RendererGPU.h"
#include "RendererGPU_kernels.h"
#undef NS

Ray::Dx::Renderer::Renderer(const settings_t &s, ILog *log) {
    ctx_ = std::make_unique<Context>();
    const bool res = ctx_->Init(log, s.preferred_device);
    if (!res) {
        throw std::runtime_error("Error initializing directx context!");
    }

    assert(Types::RAND_SAMPLES_COUNT == Ray::RAND_SAMPLES_COUNT);
    assert(Types::RAND_DIMS_COUNT == Ray::RAND_DIMS_COUNT);

    use_hwrt_ = (s.use_hwrt && ctx_->ray_query_supported());
    use_bindless_ = s.use_bindless && ctx_->max_sampled_images() >= 16384u;
    use_tex_compression_ = s.use_tex_compression;
    use_fp16_ = ctx_->fp16_supported();
    use_subgroup_ = ctx_->subgroup_supported();
    log->Info("HWRT        is %s", use_hwrt_ ? "enabled" : "disabled");
    log->Info("Bindless    is %s", use_bindless_ ? "enabled" : "disabled");
    log->Info("Compression is %s", use_tex_compression_ ? "enabled" : "disabled");
    log->Info("Float16     is %s", use_fp16_ ? "enabled" : "disabled");
    log->Info("Subgroup    is %s", use_subgroup_ ? "enabled" : "disabled");
    log->Info("===========================================");

    sh_prim_rays_gen_simple_ = Shader{"Primary Raygen Simple", ctx_.get(),
                                      internal_shaders_output_primary_ray_gen_simple_comp_cso, eShaderType::Comp, log};
    sh_prim_rays_gen_adaptive_ =
        Shader{"Primary Raygen Adaptive", ctx_.get(), internal_shaders_output_primary_ray_gen_adaptive_comp_cso,
               eShaderType::Comp, log};
    if (use_hwrt_) {
        sh_intersect_scene_ =
            Shader{"Intersect Scene (Primary) (HWRT)",
                   ctx_.get(),
                   use_bindless_ ? internal_shaders_output_intersect_scene_hwrt_bindless_comp_cso
                                 : internal_shaders_output_intersect_scene_hwrt_atlas_comp_cso,
                   use_bindless_ ? int(internal_shaders_output_intersect_scene_hwrt_bindless_comp_cso_size)
                                 : int(internal_shaders_output_intersect_scene_hwrt_atlas_comp_cso_size),
                   eShaderType::Comp,
                   log};
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
        sh_intersect_scene_indirect_ =
            Shader{"Intersect Scene (Secondary) (HWRT)",
                   ctx_.get(),
                   use_bindless_ ? internal_shaders_output_intersect_scene_indirect_hwrt_bindless_comp_cso
                                 : internal_shaders_output_intersect_scene_indirect_hwrt_atlas_comp_cso,
                   use_bindless_ ? int(internal_shaders_output_intersect_scene_indirect_hwrt_bindless_comp_cso_size)
                                 : int(internal_shaders_output_intersect_scene_indirect_hwrt_atlas_comp_cso_size),
                   eShaderType::Comp,
                   log};
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

    sh_intersect_area_lights_ = Shader{"Intersect Area Lights", ctx_.get(),
                                       internal_shaders_output_intersect_area_lights_comp_cso, eShaderType::Comp, log};
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
        sh_intersect_scene_shadow_ =
            Shader{"Intersect Scene (Shadow) (HWRT)",
                   ctx_.get(),
                   use_bindless_ ? internal_shaders_output_intersect_scene_shadow_hwrt_bindless_comp_cso
                                 : internal_shaders_output_intersect_scene_shadow_hwrt_atlas_comp_cso,
                   use_bindless_ ? int(internal_shaders_output_intersect_scene_shadow_hwrt_bindless_comp_cso_size)
                                 : int(internal_shaders_output_intersect_scene_shadow_hwrt_atlas_comp_cso_size),
                   eShaderType::Comp,
                   log};
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
    sh_prepare_indir_args_ = Shader{"Prepare Indir Args", ctx_.get(),
                                    internal_shaders_output_prepare_indir_args_comp_cso, eShaderType::Comp, log};
    sh_mix_incremental_ =
        Shader{"Mix Incremental", ctx_.get(), internal_shaders_output_mix_incremental_comp_cso, eShaderType::Comp, log};
    sh_mix_incremental_b_ = Shader{"Mix Incremental B", ctx_.get(), internal_shaders_output_mix_incremental_b_comp_cso,
                                   eShaderType::Comp, log};
    sh_mix_incremental_n_ = Shader{"Mix Incremental N", ctx_.get(), internal_shaders_output_mix_incremental_n_comp_cso,
                                   eShaderType::Comp, log};
    sh_mix_incremental_bn_ = Shader{"Mix Incremental BN", ctx_.get(),
                                    internal_shaders_output_mix_incremental_bn_comp_cso, eShaderType::Comp, log};
    sh_postprocess_ =
        Shader{"Postprocess", ctx_.get(), internal_shaders_output_postprocess_comp_cso, eShaderType::Comp, log};
    sh_filter_variance_ =
        Shader{"Filter Variance", ctx_.get(), internal_shaders_output_filter_variance_comp_cso, eShaderType::Comp, log};
    sh_nlm_filter_ =
        Shader{"NLM Filter", ctx_.get(), internal_shaders_output_nlm_filter_comp_cso, eShaderType::Comp, log};
    sh_nlm_filter_b_ =
        Shader{"NLM Filter B", ctx_.get(), internal_shaders_output_nlm_filter_b_comp_cso, eShaderType::Comp, log};
    sh_nlm_filter_n_ =
        Shader{"NLM Filter N", ctx_.get(), internal_shaders_output_nlm_filter_n_comp_cso, eShaderType::Comp, log};
    sh_nlm_filter_bn_ =
        Shader{"NLM Filter BN", ctx_.get(), internal_shaders_output_nlm_filter_bn_comp_cso, eShaderType::Comp, log};
    if (use_hwrt_) {
        sh_debug_rt_ =
            Shader{"Debug RT", ctx_.get(), internal_shaders_output_debug_rt_comp_cso, eShaderType::Comp, log};
    }

    sh_sort_hash_rays_ =
        Shader{"Sort Hash Rays", ctx_.get(), internal_shaders_output_sort_hash_rays_comp_cso, eShaderType::Comp, log};
    sh_sort_init_count_table_ = Shader{"Sort Init Count Table", ctx_.get(),
                                       internal_shaders_output_sort_init_count_table_comp_cso, eShaderType::Comp, log};
    sh_sort_reduce_ =
        Shader{"Sort Reduce", ctx_.get(), internal_shaders_output_sort_reduce_comp_cso, eShaderType::Comp, log};
    sh_sort_scan_ = Shader{"Sort Scan", ctx_.get(), internal_shaders_output_sort_scan_comp_cso, eShaderType::Comp, log};
    sh_sort_scan_add_ =
        Shader{"Sort Scan Add", ctx_.get(), internal_shaders_output_sort_scan_add_comp_cso, eShaderType::Comp, log};
    sh_sort_scatter_ =
        Shader{"Sort Scatter", ctx_.get(), internal_shaders_output_sort_scatter_comp_cso, eShaderType::Comp, log};
    sh_sort_reorder_rays_ = Shader{"Sort Reorder Rays", ctx_.get(), internal_shaders_output_sort_reorder_rays_comp_cso,
                                   eShaderType::Comp, log};

    /*sh_intersect_scene_rgen_ = Shader{"Intersect Scene RGEN",
                                      ctx_.get(),
                                      internal_shaders_output_intersect_scene_rgen_cso,
                                      eShaderType::RayGen,
                                      log};
    sh_intersect_scene_indirect_rgen_ = Shader{"Intersect Scene Indirect RGEN",
                                               ctx_.get(),
                                               internal_shaders_output_intersect_scene_indirect_rgen_spv,
                                               eShaderType::RayGen,
                                               log};
    sh_intersect_scene_rchit_ = Shader{"Intersect Scene RCHIT",
                                       ctx_.get(),
                                       internal_shaders_output_intersect_scene_rchit_spv,
                                       eShaderType::ClosestHit,
                                       log};
    sh_intersect_scene_rmiss_ = Shader{"Intersect Scene RMISS",
                                       ctx_.get(),
                                       internal_shaders_output_intersect_scene_rmiss_spv,
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
    if (use_hwrt_) {
        prog_debug_rt_ = Program{"Debug RT", ctx_.get(), &sh_debug_rt_, log};
    }
    prog_sort_hash_rays_ = Program{"Hash Rays", ctx_.get(), &sh_sort_hash_rays_, log};
    prog_sort_init_count_table_ = Program{"Init Count Table", ctx_.get(), &sh_sort_init_count_table_, log};
    prog_sort_reduce_ = Program{"Sort Reduce", ctx_.get(), &sh_sort_reduce_, log};
    prog_sort_scan_ = Program{"Sort Scan", ctx_.get(), &sh_sort_scan_, log};
    prog_sort_scan_add_ = Program{"Sort Scan Add", ctx_.get(), &sh_sort_scan_add_, log};
    prog_sort_scatter_ = Program{"Sort Scatter", ctx_.get(), &sh_sort_scatter_, log};
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
        (use_hwrt_ && !pi_debug_rt_.Init(ctx_.get(), &prog_debug_rt_, log)) ||
        !pi_sort_hash_rays_.Init(ctx_.get(), &prog_sort_hash_rays_, log) ||
        (use_subgroup_ && !pi_sort_init_count_table_.Init(ctx_.get(), &prog_sort_init_count_table_, log)) ||
        (use_subgroup_ && !pi_sort_reduce_.Init(ctx_.get(), &prog_sort_reduce_, log)) ||
        (use_subgroup_ && !pi_sort_scan_.Init(ctx_.get(), &prog_sort_scan_, log)) ||
        (use_subgroup_ && !pi_sort_scan_add_.Init(ctx_.get(), &prog_sort_scan_add_, log)) ||
        (use_subgroup_ && !pi_sort_scatter_.Init(ctx_.get(), &prog_sort_scatter_, log)) ||
        !pi_sort_reorder_rays_.Init(ctx_.get(), &prog_sort_reorder_rays_, log)
        //(use_hwrt_ && !pi_intersect_scene_rtpipe_.Init(ctx_.get(), &prog_intersect_scene_rtpipe_, log)) ||
        //(use_hwrt_ &&
        //! pi_intersect_scene_indirect_rtpipe_.Init(ctx_.get(), &prog_intersect_scene_indirect_rtpipe_, log))
    ) {
        throw std::runtime_error("Error initializing pipeline!");
    }

    random_seq_buf_ = Buffer{"Random Seq", ctx_.get(), eBufType::Storage,
                             uint32_t(RAND_DIMS_COUNT * 2 * RAND_SAMPLES_COUNT * sizeof(uint32_t))};
    counters_buf_ = Buffer{"Counters", ctx_.get(), eBufType::Storage, sizeof(uint32_t) * 32};
    indir_args_buf_ = Buffer{"Indir Args", ctx_.get(), eBufType::Indirect, 32 * sizeof(DispatchIndirectCommand)};

    { // zero out counters
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

        const uint32_t zeros[32] = {};
        counters_buf_.UpdateImmediate(0, 32 * sizeof(uint32_t), zeros, cmd_buf);

        Buffer temp_upload_buf{"Temp upload buf", ctx_.get(), eBufType::Upload, random_seq_buf_.size()};
        { // update stage buffer
            uint8_t *mapped_ptr = temp_upload_buf.Map();
            memcpy(mapped_ptr, __pmj02_samples, RAND_DIMS_COUNT * 2 * RAND_SAMPLES_COUNT * sizeof(uint32_t));
            temp_upload_buf.Unmap();
        }

        CopyBufferToBuffer(temp_upload_buf, 0, random_seq_buf_, 0,
                           RAND_DIMS_COUNT * 2 * RAND_SAMPLES_COUNT * sizeof(uint32_t), cmd_buf);

        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

        temp_upload_buf.FreeImmediate();
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
}

Ray::eRendererType Ray::Dx::Renderer::type() const { return eRendererType::DirectX12; }

const char *Ray::Dx::Renderer::device_name() const { return ctx_->device_name().c_str(); }

void Ray::Dx::Renderer::RenderScene(const SceneBase *_s, RegionContext &region) {
    const auto s = dynamic_cast<const Dx::Scene *>(_s);
    if (!s) {
        return;
    }

    const uint32_t macro_tree_root = s->macro_nodes_root_;

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

    ++region.iteration;

    const Ray::camera_t &cam = s->cams_[s->current_cam()._index];

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
                CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());
                TransitionResourceStates(cmd_buf, AllStages, AllStages, img_transitions);
                static const float rgba[] = {0.0f, 0.0f, 0.0f, 0.0f};
                ClearColorImage(base_color_buf_, rgba, cmd_buf);
                EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf,
                                      ctx_->temp_command_pool());
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
                CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());
                TransitionResourceStates(cmd_buf, AllStages, AllStages, img_transitions);
                static const float rgba[] = {0.0f, 0.0f, 0.0f, 0.0f};
                ClearColorImage(depth_normals_buf_, rgba, cmd_buf);
                EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf,
                                      ctx_->temp_command_pool());
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

    // TODO: Use common command buffer for all uploads
    if (cam.filter != filter_table_filter_ || cam.filter_width != filter_table_width_) {
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

        UpdateFilterTable(cmd_buf, cam.filter, cam.filter_width);
        filter_table_filter_ = cam.filter;
        filter_table_width_ = cam.filter_width;

        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
    }

    if (loaded_view_transform_ != cam.view_transform) {
        if (cam.view_transform != eViewTransform::Standard) {
            CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

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

            EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf,
                                  ctx_->temp_command_pool());

            temp_upload_buf.FreeImmediate();
        }
        loaded_view_transform_ = cam.view_transform;
    }

    const scene_data_t sc_data = {&s->env_,
                                  s->mesh_instances_.gpu_buf(),
                                  s->mi_indices_.buf(),
                                  s->meshes_.gpu_buf(),
                                  s->vtx_indices_.gpu_buf(),
                                  s->vertices_.gpu_buf(),
                                  s->nodes_.gpu_buf(),
                                  s->tris_.gpu_buf(),
                                  s->tri_indices_.gpu_buf(),
                                  s->tri_materials_.gpu_buf(),
                                  s->materials_.gpu_buf(),
                                  s->atlas_textures_.gpu_buf(),
                                  s->lights_.gpu_buf(),
                                  s->li_indices_.buf(),
                                  int(s->li_indices_.size()),
                                  s->visible_lights_count_,
                                  s->blocker_lights_count_,
                                  s->light_wnodes_.buf(),
                                  s->rt_tlas_,
                                  s->env_map_qtree_.tex,
                                  int(s->env_map_qtree_.mips.size())};

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
    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());
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

    { // transition resources
        SmallVector<TransitionInfo, 16> res_transitions;

        for (const auto &tex_atlas : s->tex_atlases_) {
            if (tex_atlas.resource_state != eResState::ShaderResource) {
                res_transitions.emplace_back(&tex_atlas, eResState::ShaderResource);
            }
        }

        if (sc_data.mi_indices && sc_data.mi_indices.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.mi_indices, eResState::ShaderResource);
        }
        if (sc_data.meshes && sc_data.meshes.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.meshes, eResState::ShaderResource);
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
        if (sc_data.env_qtree.resource_state != eResState::ShaderResource) {
            res_transitions.emplace_back(&sc_data.env_qtree, eResState::ShaderResource);
        }

        TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);
    }

    // Allocate bindless texture descriptors
    if (s->bindless_tex_data_.srv_descr_table.count) {
        // TODO: refactor this!
        ID3D12Device *device = ctx_->device();
        const UINT CBV_SRV_UAV_INCR = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        DescrSizes descr_sizes;
        descr_sizes.cbv_srv_uav_count += s->bindless_tex_data_.srv_descr_table.count;

        const PoolRefs pool_refs = ctx_->default_descr_alloc()->Alloc(descr_sizes);

        D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle = pool_refs.cbv_srv_uav.heap->GetCPUDescriptorHandleForHeapStart();
        srv_cpu_handle.ptr += CBV_SRV_UAV_INCR * pool_refs.cbv_srv_uav.offset;
        D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_handle = pool_refs.cbv_srv_uav.heap->GetGPUDescriptorHandleForHeapStart();
        srv_gpu_handle.ptr += CBV_SRV_UAV_INCR * pool_refs.cbv_srv_uav.offset;

        device->CopyDescriptorsSimple(descr_sizes.cbv_srv_uav_count, srv_cpu_handle,
                                      D3D12_CPU_DESCRIPTOR_HANDLE{s->bindless_tex_data_.srv_descr_table.cpu_ptr},
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        s->bindless_tex_data_.srv_descr_table.gpu_heap = pool_refs.cbv_srv_uav.heap;
        s->bindless_tex_data_.srv_descr_table.gpu_ptr = srv_gpu_handle.ptr;
    }

    const rect_t rect = region.rect();
    const uint32_t rand_seed = Ref::hash((region.iteration - 1) / RAND_SAMPLES_COUNT);

    { // generate primary rays
        DebugMarker _(ctx_.get(), cmd_buf, "GeneratePrimaryRays");
        timestamps_[ctx_->backend_frame].primary_ray_gen[0] = ctx_->WriteTimestamp(cmd_buf, true);
        kernel_GeneratePrimaryRays(cmd_buf, cam, rand_seed, rect, random_seq_buf_, filter_table_, region.iteration,
                                   required_samples_buf_, counters_buf_, prim_rays_buf_);
        timestamps_[ctx_->backend_frame].primary_ray_gen[1] = ctx_->WriteTimestamp(cmd_buf, false);
    }

    { // prepare indirect args
        DebugMarker _(ctx_.get(), cmd_buf, "PrepareIndirArgs");
        kernel_PrepareIndirArgs(cmd_buf, counters_buf_, indir_args_buf_);
    }

#if DEBUG_HWRT
    { // debug
        DebugMarker _(cmd_buf, "Debug HWRT");
        kernel_DebugRT(cmd_buf, sc_data, macro_tree_root, prim_rays_buf_, temp_buf_);
    }
#else
    { // trace primary rays
        DebugMarker _(ctx_.get(), cmd_buf, "IntersectScenePrimary");
        timestamps_[ctx_->backend_frame].primary_trace[0] = ctx_->WriteTimestamp(cmd_buf, true);
        kernel_IntersectScene(cmd_buf, indir_args_buf_, 0, counters_buf_, cam.pass_settings, sc_data, random_seq_buf_,
                              rand_seed, region.iteration, macro_tree_root, cam.fwd, cam.clip_end - cam.clip_start,
                              s->tex_atlases_, s->bindless_tex_data_, prim_rays_buf_, prim_hits_buf_);
        timestamps_[ctx_->backend_frame].primary_trace[1] = ctx_->WriteTimestamp(cmd_buf, false);
    }

    Texture2D null_tex;
    Texture2D &temp_base_color = base_color_buf_.ready() ? temp_buf1_ : null_tex;

    { // shade primary hits
        DebugMarker _(ctx_.get(), cmd_buf, "ShadePrimaryHits");
        timestamps_[ctx_->backend_frame].primary_shade[0] = ctx_->WriteTimestamp(cmd_buf, true);
        kernel_ShadePrimaryHits(cmd_buf, cam.pass_settings, s->env_, indir_args_buf_, 0, prim_hits_buf_, prim_rays_buf_,
                                sc_data, random_seq_buf_, rand_seed, region.iteration, rect, s->tex_atlases_,
                                s->bindless_tex_data_, temp_buf0_, secondary_rays_buf_, shadow_rays_buf_, counters_buf_,
                                temp_base_color, temp_depth_normals_buf_);
        timestamps_[ctx_->backend_frame].primary_shade[1] = ctx_->WriteTimestamp(cmd_buf, false);
    }

    { // prepare indirect args
        DebugMarker _(ctx_.get(), cmd_buf, "PrepareIndirArgs");
        kernel_PrepareIndirArgs(cmd_buf, counters_buf_, indir_args_buf_);
    }

    { // trace shadow rays
        DebugMarker _(ctx_.get(), cmd_buf, "TraceShadow");
        timestamps_[ctx_->backend_frame].primary_shadow[0] = ctx_->WriteTimestamp(cmd_buf, true);
        kernel_IntersectSceneShadow(cmd_buf, cam.pass_settings, indir_args_buf_, 2, counters_buf_, sc_data,
                                    random_seq_buf_, rand_seed, region.iteration, macro_tree_root,
                                    cam.pass_settings.clamp_direct, s->tex_atlases_, s->bindless_tex_data_,
                                    shadow_rays_buf_, temp_buf0_);
        timestamps_[ctx_->backend_frame].primary_shadow[1] = ctx_->WriteTimestamp(cmd_buf, false);
    }

    timestamps_[ctx_->backend_frame].secondary_sort.clear();
    timestamps_[ctx_->backend_frame].secondary_trace.clear();
    timestamps_[ctx_->backend_frame].secondary_shade.clear();
    timestamps_[ctx_->backend_frame].secondary_shadow.clear();

    for (int bounce = 1; bounce <= cam.pass_settings.max_total_depth; ++bounce) {
#if !DISABLE_SORTING
        timestamps_[ctx_->backend_frame].secondary_sort.push_back(ctx_->WriteTimestamp(cmd_buf, true));

        if (!use_hwrt_ && use_subgroup_) {
            DebugMarker _(ctx_.get(), cmd_buf, "Sort Rays");

            kernel_SortHashRays(cmd_buf, indir_args_buf_, secondary_rays_buf_, counters_buf_, root_min, cell_size,
                                ray_hashes_bufs_[0]);
            RadixSort(cmd_buf, indir_args_buf_, ray_hashes_bufs_, count_table_buf_, counters_buf_, reduce_table_buf_);
            kernel_SortReorderRays(cmd_buf, indir_args_buf_, 0, secondary_rays_buf_, ray_hashes_bufs_[0], counters_buf_,
                                   1, prim_rays_buf_);

            std::swap(secondary_rays_buf_, prim_rays_buf_);
        }

        timestamps_[ctx_->backend_frame].secondary_sort.push_back(ctx_->WriteTimestamp(cmd_buf, false));
#endif // !DISABLE_SORTING

        timestamps_[ctx_->backend_frame].secondary_trace.push_back(ctx_->WriteTimestamp(cmd_buf, true));
        { // trace secondary rays
            DebugMarker _(ctx_.get(), cmd_buf, "IntersectSceneSecondary");
            kernel_IntersectScene(cmd_buf, indir_args_buf_, 0, counters_buf_, cam.pass_settings, sc_data,
                                  random_seq_buf_, rand_seed, region.iteration, macro_tree_root, nullptr, -1.0f,
                                  s->tex_atlases_, s->bindless_tex_data_, secondary_rays_buf_, prim_hits_buf_);
        }

        if (sc_data.visible_lights_count) {
            DebugMarker _(ctx_.get(), cmd_buf, "IntersectAreaLights");
            kernel_IntersectAreaLights(cmd_buf, sc_data, indir_args_buf_, counters_buf_, secondary_rays_buf_,
                                       prim_hits_buf_);
        }

        timestamps_[ctx_->backend_frame].secondary_trace.push_back(ctx_->WriteTimestamp(cmd_buf, false));

        { // shade secondary hits
            DebugMarker _(ctx_.get(), cmd_buf, "ShadeSecondaryHits");
            timestamps_[ctx_->backend_frame].secondary_shade.push_back(ctx_->WriteTimestamp(cmd_buf, true));
            const float clamp_val = (bounce == 1) ? cam.pass_settings.clamp_direct : cam.pass_settings.clamp_indirect;
            kernel_ShadeSecondaryHits(cmd_buf, cam.pass_settings, clamp_val, s->env_, indir_args_buf_, 0,
                                      prim_hits_buf_, secondary_rays_buf_, sc_data, random_seq_buf_, rand_seed,
                                      region.iteration, s->tex_atlases_, s->bindless_tex_data_, temp_buf0_,
                                      prim_rays_buf_, shadow_rays_buf_, counters_buf_);
            timestamps_[ctx_->backend_frame].secondary_shade.push_back(ctx_->WriteTimestamp(cmd_buf, false));
        }

        { // prepare indirect args
            DebugMarker _(ctx_.get(), cmd_buf, "PrepareIndirArgs");
            kernel_PrepareIndirArgs(cmd_buf, counters_buf_, indir_args_buf_);
        }

        { // trace shadow rays
            DebugMarker _(ctx_.get(), cmd_buf, "TraceShadow");
            timestamps_[ctx_->backend_frame].secondary_shadow.push_back(ctx_->WriteTimestamp(cmd_buf, true));
            kernel_IntersectSceneShadow(cmd_buf, cam.pass_settings, indir_args_buf_, 2, counters_buf_, sc_data,
                                        random_seq_buf_, rand_seed, region.iteration, macro_tree_root,
                                        cam.pass_settings.clamp_indirect, s->tex_atlases_, s->bindless_tex_data_,
                                        shadow_rays_buf_, temp_buf0_);
            timestamps_[ctx_->backend_frame].secondary_shadow.push_back(ctx_->WriteTimestamp(cmd_buf, false));
        }

        std::swap(secondary_rays_buf_, prim_rays_buf_);
    }
#endif

    { // prepare result
        DebugMarker _(ctx_.get(), cmd_buf, "Prepare Result");

        Texture2D &clean_buf = dual_buf_[(region.iteration - 1) % 2];

        // factor used to compute incremental average
        const float main_mix_factor = 1.0f / float((region.iteration + 1) / 2);
        const float aux_mix_factor = 1.0f / float(region.iteration);

        kernel_MixIncremental(cmd_buf, main_mix_factor, aux_mix_factor, rect, region.iteration, temp_buf0_,
                              temp_base_color, temp_depth_normals_buf_, required_samples_buf_, clean_buf,
                              base_color_buf_, depth_normals_buf_);
    }

    { // output final buffer, prepare variance
        DebugMarker _(ctx_.get(), cmd_buf, "Postprocess frame");

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
    EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
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
    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());
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
        DebugMarker _(ctx_.get(), cmd_buf, "Filter Variance");
        kernel_FilterVariance(cmd_buf, raw_variance, rect, variance_threshold_, region.iteration, filtered_variance,
                              required_samples_buf_);
    }

    { // Apply NLM Filter
        DebugMarker _(ctx_.get(), cmd_buf, "NLM Filter");
        kernel_NLMFilter(cmd_buf, raw_final_buf_, filtered_variance, 1.0f, 0.45f, base_color_buf_, 64.0f,
                         depth_normals_buf_, 32.0f, raw_filtered_buf_, tonemap_params_.view_transform,
                         tonemap_params_.inv_gamma, rect, final_buf_);
    }

    timestamps_[ctx_->backend_frame].denoise[1] = ctx_->WriteTimestamp(cmd_buf, false);

    //////////////////////////////////////////////////////////////////////////////////

#if RUN_IN_LOCKSTEP
    EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
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

void Ray::Dx::Renderer::DenoiseImage(const int pass, const RegionContext &region) {
    CommandBuffer cmd_buf = {};
    if (pass == 0) {
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
        cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());
#else
        ID3D12CommandAllocator *cmd_alloc = ctx_->draw_cmd_alloc(ctx_->backend_frame);
        cmd_buf = ctx_->draw_cmd_buf();

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
        timestamps_[ctx_->backend_frame].denoise[0] = ctx_->WriteTimestamp(cmd_buf, true);
    } else {
#if RUN_IN_LOCKSTEP
        cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());
#else
        cmd_buf = ctx_->draw_cmd_buf();
#endif
    }

    //////////////////////////////////////////////////////////////////////////////////

    const int w_rounded = 16 * ((w_ + 15) / 16);
    const int h_rounded = 16 * ((h_ + 15) / 16);

    rect_t r = region.rect();
    if (pass < 15) {
        r.w = 16 * ((r.w + 15) / 16);
        r.h = 16 * ((r.h + 15) / 16);
    }

    Buffer *weights = &unet_weights_[0];
    const unet_weight_offsets_t *offsets = &unet_offsets_[0];
    if (base_color_buf_.ready() && depth_normals_buf_.ready()) {
        weights = &unet_weights_[2];
        offsets = &unet_offsets_[2];
    } else if (base_color_buf_.ready()) {
        weights = &unet_weights_[1];
        offsets = &unet_offsets_[1];
    }

    switch (pass) {
    case 0: {
        const int output_stride = round_up(w_rounded + 1, 16) + 1;
        if (base_color_buf_.ready() && depth_normals_buf_.ready()) {
            DebugMarker _(ctx_.get(), cmd_buf, "Convolution 9 32");
            kernel_Convolution(cmd_buf, 9, 32, raw_final_buf_, base_color_buf_, depth_normals_buf_,
                               zero_border_sampler_, r, w_rounded, h_rounded, *weights, offsets->enc_conv0_weight,
                               offsets->enc_conv0_bias, unet_tensors_heap_, unet_tensors_.enc_conv0_offset,
                               output_stride);
        } else if (base_color_buf_.ready()) {
            DebugMarker _(ctx_.get(), cmd_buf, "Convolution 6 32");
            kernel_Convolution(cmd_buf, 6, 32, raw_final_buf_, base_color_buf_, {}, zero_border_sampler_, r, w_rounded,
                               h_rounded, *weights, offsets->enc_conv0_weight, offsets->enc_conv0_bias,
                               unet_tensors_heap_, unet_tensors_.enc_conv0_offset, output_stride);
        } else {
            DebugMarker _(ctx_.get(), cmd_buf, "Convolution 3 32");
            kernel_Convolution(cmd_buf, 3, 32, raw_final_buf_, {}, {}, zero_border_sampler_, r, w_rounded, h_rounded,
                               *weights, offsets->enc_conv0_weight, offsets->enc_conv0_bias, unet_tensors_heap_,
                               unet_tensors_.enc_conv0_offset, output_stride);
        }
    } break;
    case 1: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 32 32 Downscale");
        const int input_stride = round_up(w_rounded + 1, 16) + 1, output_stride = round_up(w_rounded / 2 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 32, 32, unet_tensors_heap_, unet_tensors_.enc_conv0_offset, input_stride, r,
                           w_rounded, h_rounded, *weights, offsets->enc_conv1_weight, offsets->enc_conv1_bias,
                           unet_tensors_heap_, unet_tensors_.pool1_offset, output_stride, true);
    } break;
    case 2: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 32 48 Downscale");
        r.x = r.x / 2;
        r.y = r.y / 2;
        r.w = (r.w + 1) / 2;
        r.h = (r.h + 1) / 2;

        const int input_stride = round_up(w_rounded / 2 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 4 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 32, 48, unet_tensors_heap_, unet_tensors_.pool1_offset, input_stride, r,
                           w_rounded / 2, h_rounded / 2, *weights, offsets->enc_conv2_weight, offsets->enc_conv2_bias,
                           unet_tensors_heap_, unet_tensors_.pool2_offset, output_stride, true);
    } break;
    case 3: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 48 64 Downscale");
        r.x = r.x / 4;
        r.y = r.y / 4;
        r.w = (r.w + 3) / 4;
        r.h = (r.h + 3) / 4;

        const int input_stride = round_up(w_rounded / 4 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 8 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 48, 64, unet_tensors_heap_, unet_tensors_.pool2_offset, input_stride, r,
                           w_rounded / 4, h_rounded / 4, *weights, offsets->enc_conv3_weight, offsets->enc_conv3_bias,
                           unet_tensors_heap_, unet_tensors_.pool3_offset, output_stride, true);
    } break;
    case 4: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 64 80 Downscale");
        r.x = r.x / 8;
        r.y = r.y / 8;
        r.w = (r.w + 7) / 8;
        r.h = (r.h + 7) / 8;

        const int input_stride = round_up(w_rounded / 8 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 16 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 64, 80, unet_tensors_heap_, unet_tensors_.pool3_offset, input_stride, r,
                           w_rounded / 8, h_rounded / 8, *weights, offsets->enc_conv4_weight, offsets->enc_conv4_bias,
                           unet_tensors_heap_, unet_tensors_.pool4_offset, output_stride, true);
    } break;
    case 5: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 80 96");
        r.x = r.x / 16;
        r.y = r.y / 16;
        r.w = (r.w + 15) / 16;
        r.h = (r.h + 15) / 16;

        const int input_stride = round_up(w_rounded / 16 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 16 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 80, 96, unet_tensors_heap_, unet_tensors_.pool4_offset, input_stride, r,
                           w_rounded / 16, h_rounded / 16, *weights, offsets->enc_conv5a_weight,
                           offsets->enc_conv5a_bias, unet_tensors_heap_, unet_tensors_.enc_conv5a_offset, output_stride,
                           false);
    } break;
    case 6: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 96 96");
        r.x = r.x / 16;
        r.y = r.y / 16;
        r.w = (r.w + 15) / 16;
        r.h = (r.h + 15) / 16;

        const int input_stride = round_up(w_rounded / 16 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 16 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 96, 96, unet_tensors_heap_, unet_tensors_.enc_conv5a_offset, input_stride, r,
                           w_rounded / 16, h_rounded / 16, *weights, offsets->enc_conv5b_weight,
                           offsets->enc_conv5b_bias, unet_tensors_heap_, unet_tensors_.upsample4_offset, output_stride,
                           false);
    } break;
    case 7: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution Concat 96 64 112");
        r.x = r.x / 8;
        r.y = r.y / 8;
        r.w = (r.w + 7) / 8;
        r.h = (r.h + 7) / 8;

        const int input_stride1 = round_up(w_rounded / 16 + 1, 16) + 1,
                  input_stride2 = round_up(w_rounded / 8 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 8 + 1, 16) + 1;
        kernel_ConvolutionConcat(cmd_buf, 96, 64, 112, unet_tensors_heap_, unet_tensors_.upsample4_offset,
                                 input_stride1, true, unet_tensors_heap_, unet_tensors_.pool3_offset, input_stride2, r,
                                 w_rounded / 8, h_rounded / 8, *weights, offsets->dec_conv4a_weight,
                                 offsets->dec_conv4a_bias, unet_tensors_heap_, unet_tensors_.dec_conv4a_offset,
                                 output_stride);
    } break;
    case 8: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 112 112");
        r.x = r.x / 8;
        r.y = r.y / 8;
        r.w = (r.w + 7) / 8;
        r.h = (r.h + 7) / 8;

        const int input_stride = round_up(w_rounded / 8 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 8 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 112, 112, unet_tensors_heap_, unet_tensors_.dec_conv4a_offset, input_stride, r,
                           w_rounded / 8, h_rounded / 8, *weights, offsets->dec_conv4b_weight, offsets->dec_conv4b_bias,
                           unet_tensors_heap_, unet_tensors_.upsample3_offset, output_stride, false);
    } break;
    case 9: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution Concat 112 48 96");
        r.x = r.x / 4;
        r.y = r.y / 4;
        r.w = (r.w + 3) / 4;
        r.h = (r.h + 3) / 4;

        const int input_stride1 = round_up(w_rounded / 8 + 1, 16) + 1,
                  input_stride2 = round_up(w_rounded / 4 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 4 + 1, 16) + 1;
        kernel_ConvolutionConcat(cmd_buf, 112, 48, 96, unet_tensors_heap_, unet_tensors_.upsample3_offset,
                                 input_stride1, true, unet_tensors_heap_, unet_tensors_.pool2_offset, input_stride2, r,
                                 w_rounded / 4, h_rounded / 4, *weights, offsets->dec_conv3a_weight,
                                 offsets->dec_conv3a_bias, unet_tensors_heap_, unet_tensors_.dec_conv3a_offset,
                                 output_stride);
    } break;
    case 10: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 96 96");
        r.x = r.x / 4;
        r.y = r.y / 4;
        r.w = (r.w + 3) / 4;
        r.h = (r.h + 3) / 4;

        const int input_stride = round_up(w_rounded / 4 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 4 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 96, 96, unet_tensors_heap_, unet_tensors_.dec_conv3a_offset, input_stride, r,
                           w_rounded / 4, h_rounded / 4, *weights, offsets->dec_conv3b_weight, offsets->dec_conv3b_bias,
                           unet_tensors_heap_, unet_tensors_.upsample2_offset, output_stride, false);
    } break;
    case 11: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution Concat 96 32 64");
        r.x = r.x / 2;
        r.y = r.y / 2;
        r.w = (r.w + 1) / 2;
        r.h = (r.h + 1) / 2;

        const int input_stride1 = round_up(w_rounded / 4 + 1, 16) + 1,
                  input_stride2 = round_up(w_rounded / 2 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 2 + 1, 16) + 1;
        kernel_ConvolutionConcat(cmd_buf, 96, 32, 64, unet_tensors_heap_, unet_tensors_.upsample2_offset, input_stride1,
                                 true, unet_tensors_heap_, unet_tensors_.pool1_offset, input_stride2, r, w_rounded / 2,
                                 h_rounded / 2, *weights, offsets->dec_conv2a_weight, offsets->dec_conv2a_bias,
                                 unet_tensors_heap_, unet_tensors_.dec_conv2a_offset, output_stride);
    } break;
    case 12: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 64 64");
        r.x = r.x / 2;
        r.y = r.y / 2;
        r.w = (r.w + 1) / 2;
        r.h = (r.h + 1) / 2;

        const int input_stride = round_up(w_rounded / 2 + 1, 16) + 1,
                  output_stride = round_up(w_rounded / 2 + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 64, 64, unet_tensors_heap_, unet_tensors_.dec_conv2a_offset, input_stride, r,
                           w_rounded / 2, h_rounded / 2, *weights, offsets->dec_conv2b_weight, offsets->dec_conv2b_bias,
                           unet_tensors_heap_, unet_tensors_.upsample1_offset, output_stride, false);
    } break;
    case 13: {
        const int input_stride = round_up(w_rounded / 2 + 1, 16) + 1, output_stride = round_up(w_rounded + 1, 16) + 1;
        if (base_color_buf_.ready() && depth_normals_buf_.ready()) {
            DebugMarker _(ctx_.get(), cmd_buf, "Convolution Concat 64 9 64");
            kernel_ConvolutionConcat(cmd_buf, 64, 9, 64, unet_tensors_heap_, unet_tensors_.upsample1_offset,
                                     input_stride, true, raw_final_buf_, base_color_buf_, depth_normals_buf_,
                                     zero_border_sampler_, r, w_rounded, h_rounded, *weights,
                                     offsets->dec_conv1a_weight, offsets->dec_conv1a_bias, unet_tensors_heap_,
                                     unet_tensors_.dec_conv1a_offset, output_stride);
        } else if (base_color_buf_.ready()) {
            DebugMarker _(ctx_.get(), cmd_buf, "Convolution Concat 64 6 64");
            kernel_ConvolutionConcat(cmd_buf, 64, 6, 64, unet_tensors_heap_, unet_tensors_.upsample1_offset,
                                     input_stride, true, raw_final_buf_, base_color_buf_, {}, zero_border_sampler_, r,
                                     w_rounded, h_rounded, *weights, offsets->dec_conv1a_weight,
                                     offsets->dec_conv1a_bias, unet_tensors_heap_, unet_tensors_.dec_conv1a_offset,
                                     output_stride);
        } else {
            DebugMarker _(ctx_.get(), cmd_buf, "Convolution Concat 64 3 64");
            kernel_ConvolutionConcat(cmd_buf, 64, 3, 64, unet_tensors_heap_, unet_tensors_.upsample1_offset,
                                     input_stride, true, raw_final_buf_, {}, {}, zero_border_sampler_, r, w_rounded,
                                     h_rounded, *weights, offsets->dec_conv1a_weight, offsets->dec_conv1a_bias,
                                     unet_tensors_heap_, unet_tensors_.dec_conv1a_offset, output_stride);
        }
    } break;
    case 14: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 64 32");
        const int input_stride = round_up(w_rounded + 1, 16) + 1, output_stride = round_up(w_rounded + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 64, 32, unet_tensors_heap_, unet_tensors_.dec_conv1a_offset, input_stride, r,
                           w_rounded, h_rounded, *weights, offsets->dec_conv1b_weight, offsets->dec_conv1b_bias,
                           unet_tensors_heap_, unet_tensors_.dec_conv1b_offset, output_stride, false);
    } break;
    case 15: {
        DebugMarker _(ctx_.get(), cmd_buf, "Convolution 32 3 Img ");
        const int input_stride = round_up(w_rounded + 1, 16) + 1;
        kernel_Convolution(cmd_buf, 32, 3, unet_tensors_heap_, unet_tensors_.dec_conv1b_offset, input_stride,
                           tonemap_params_.inv_gamma, r, w_, h_, *weights, offsets->dec_conv0_weight,
                           offsets->dec_conv0_bias, raw_filtered_buf_, final_buf_);
    } break;
    }

    //////////////////////////////////////////////////////////////////////////////////

    if (pass == 15) {
        timestamps_[ctx_->backend_frame].denoise[1] = ctx_->WriteTimestamp(cmd_buf, false);

#if RUN_IN_LOCKSTEP
        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
#else
        HRESULT hr = cmd_buf->Close();
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
}

Ray::color_data_rgba_t Ray::Dx::Renderer::get_pixels_ref(const bool tonemap) const {
    if (frame_dirty_ || pixel_readback_is_tonemapped_ != tonemap) {
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

        { // download result
            DebugMarker _(ctx_.get(), cmd_buf, "Download Result");

            // TODO: fix this!
            const auto &buffer_to_use = tonemap ? final_buf_ : raw_filtered_buf_;

            const TransitionInfo res_transitions[] = {{&buffer_to_use, eResState::CopySrc},
                                                      {&pixel_readback_buf_, eResState::CopyDst}};
            TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

            CopyImageToBuffer(buffer_to_use, 0, 0, 0, w_, h_, pixel_readback_buf_, cmd_buf, 0);
        }

        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

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
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

        { // download result
            DebugMarker _(ctx_.get(), cmd_buf, "Download Result");

            const TransitionInfo res_transitions[] = {{&buffer_to_use, eResState::CopySrc},
                                                      {&stage_buffer_to_use, eResState::CopyDst}};
            TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

            CopyImageToBuffer(buffer_to_use, 0, 0, 0, w_, h_, stage_buffer_to_use, cmd_buf, 0);
        }

        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

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

bool Ray::Dx::Renderer::InitUNetPipelines() {
    ILog *log = ctx_->log();

    auto select_shader = [this](Span<const uint8_t> default_shader, Span<const uint8_t> fp16_shader) {
        return use_fp16_ ? fp16_shader : default_shader;
    };

    sh_convolution_Img_3_32_ = Shader{"Convolution Img 3 32", ctx_.get(),
                                      select_shader(internal_shaders_output_convolution_Img_3_32_fp32_comp_cso,
                                                    internal_shaders_output_convolution_Img_3_32_fp16_comp_cso),
                                      eShaderType::Comp, log};
    sh_convolution_Img_6_32_ = Shader{"Convolution Img 6 32", ctx_.get(),
                                      select_shader(internal_shaders_output_convolution_Img_6_32_fp32_comp_cso,
                                                    internal_shaders_output_convolution_Img_6_32_fp16_comp_cso),
                                      eShaderType::Comp, log};
    sh_convolution_Img_9_32_ = Shader{"Convolution Img 9 32", ctx_.get(),
                                      select_shader(internal_shaders_output_convolution_Img_9_32_fp32_comp_cso,
                                                    internal_shaders_output_convolution_Img_9_32_fp16_comp_cso),
                                      eShaderType::Comp, log};
    sh_convolution_32_32_Downsample_ =
        Shader{"Convolution 32 32 Downsample", ctx_.get(),
               select_shader(internal_shaders_output_convolution_32_32_Downsample_fp32_comp_cso,
                             internal_shaders_output_convolution_32_32_Downsample_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_32_48_Downsample_ =
        Shader{"Convolution 32 48 Downsample", ctx_.get(),
               select_shader(internal_shaders_output_convolution_32_48_Downsample_fp32_comp_cso,
                             internal_shaders_output_convolution_32_48_Downsample_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_48_64_Downsample_ =
        Shader{"Convolution 48 64 Downsample", ctx_.get(),
               select_shader(internal_shaders_output_convolution_48_64_Downsample_fp32_comp_cso,
                             internal_shaders_output_convolution_48_64_Downsample_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_64_80_Downsample_ =
        Shader{"Convolution 64 80 Downsample", ctx_.get(),
               select_shader(internal_shaders_output_convolution_64_80_Downsample_fp32_comp_cso,
                             internal_shaders_output_convolution_64_80_Downsample_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_64_64_ = Shader{"Convolution 64 64", ctx_.get(),
                                   select_shader(internal_shaders_output_convolution_64_64_fp32_comp_cso,
                                                 internal_shaders_output_convolution_64_64_fp16_comp_cso),
                                   eShaderType::Comp, log};
    sh_convolution_64_32_ = Shader{"Convolution 64 32", ctx_.get(),
                                   select_shader(internal_shaders_output_convolution_64_32_fp32_comp_cso,
                                                 internal_shaders_output_convolution_64_32_fp16_comp_cso),
                                   eShaderType::Comp, log};
    sh_convolution_80_96_ = Shader{"Convolution 80 96", ctx_.get(),
                                   select_shader(internal_shaders_output_convolution_80_96_fp32_comp_cso,
                                                 internal_shaders_output_convolution_80_96_fp16_comp_cso),
                                   eShaderType::Comp, log};
    sh_convolution_96_96_ = Shader{"Convolution 96 96", ctx_.get(),
                                   select_shader(internal_shaders_output_convolution_96_96_fp32_comp_cso,
                                                 internal_shaders_output_convolution_96_96_fp16_comp_cso),
                                   eShaderType::Comp, log};
    sh_convolution_112_112_ = Shader{"Convolution 112 112", ctx_.get(),
                                     select_shader(internal_shaders_output_convolution_112_112_fp32_comp_cso,
                                                   internal_shaders_output_convolution_112_112_fp16_comp_cso),
                                     eShaderType::Comp, log};
    sh_convolution_concat_96_64_112_ =
        Shader{"Convolution Concat 96 64 112", ctx_.get(),
               select_shader(internal_shaders_output_convolution_concat_96_64_112_fp32_comp_cso,
                             internal_shaders_output_convolution_concat_96_64_112_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_concat_112_48_96_ =
        Shader{"Convolution Concat 112 48 96", ctx_.get(),
               select_shader(internal_shaders_output_convolution_concat_112_48_96_fp32_comp_cso,
                             internal_shaders_output_convolution_concat_112_48_96_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_concat_96_32_64_ =
        Shader{"Convolution Concat 96 32 64", ctx_.get(),
               select_shader(internal_shaders_output_convolution_concat_96_32_64_fp32_comp_cso,
                             internal_shaders_output_convolution_concat_96_32_64_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_concat_64_3_64_ =
        Shader{"Convolution Concat 64 3 64", ctx_.get(),
               select_shader(internal_shaders_output_convolution_concat_64_3_64_fp32_comp_cso,
                             internal_shaders_output_convolution_concat_64_3_64_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_concat_64_6_64_ =
        Shader{"Convolution Concat 64 6 64", ctx_.get(),
               select_shader(internal_shaders_output_convolution_concat_64_6_64_fp32_comp_cso,
                             internal_shaders_output_convolution_concat_64_6_64_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_concat_64_9_64_ =
        Shader{"Convolution Concat 64 9 64", ctx_.get(),
               select_shader(internal_shaders_output_convolution_concat_64_9_64_fp32_comp_cso,
                             internal_shaders_output_convolution_concat_64_9_64_fp16_comp_cso),
               eShaderType::Comp, log};
    sh_convolution_32_3_img_ = Shader{"Convolution 32 3 Img", ctx_.get(),
                                      select_shader(internal_shaders_output_convolution_32_3_img_fp32_comp_cso,
                                                    internal_shaders_output_convolution_32_3_img_fp16_comp_cso),
                                      eShaderType::Comp, log};

    prog_convolution_Img_3_32_ = Program{"Convolution Img 3 32", ctx_.get(), &sh_convolution_Img_3_32_, log};
    prog_convolution_Img_6_32_ = Program{"Convolution Img 6 32", ctx_.get(), &sh_convolution_Img_6_32_, log};
    prog_convolution_Img_9_32_ = Program{"Convolution Img 9 32", ctx_.get(), &sh_convolution_Img_9_32_, log};
    prog_convolution_32_32_Downsample_ =
        Program{"Convolution 32 32", ctx_.get(), &sh_convolution_32_32_Downsample_, log};
    prog_convolution_32_48_Downsample_ =
        Program{"Convolution 32 48", ctx_.get(), &sh_convolution_32_48_Downsample_, log};
    prog_convolution_48_64_Downsample_ =
        Program{"Convolution 48 64", ctx_.get(), &sh_convolution_48_64_Downsample_, log};
    prog_convolution_64_80_Downsample_ =
        Program{"Convolution 64 80", ctx_.get(), &sh_convolution_64_80_Downsample_, log};
    prog_convolution_64_64_ = Program{"Convolution 64 64", ctx_.get(), &sh_convolution_64_64_, log};
    prog_convolution_64_32_ = Program{"Convolution 64 32", ctx_.get(), &sh_convolution_64_32_, log};
    prog_convolution_80_96_ = Program{"Convolution 80 96", ctx_.get(), &sh_convolution_80_96_, log};
    prog_convolution_96_96_ = Program{"Convolution 96 96", ctx_.get(), &sh_convolution_96_96_, log};
    prog_convolution_112_112_ = Program{"Convolution 112 112", ctx_.get(), &sh_convolution_112_112_, log};
    prog_convolution_concat_96_64_112_ =
        Program{"Convolution Concat 96 64 112", ctx_.get(), &sh_convolution_concat_96_64_112_, log};
    prog_convolution_concat_112_48_96_ =
        Program{"Convolution Concat 112 48 96", ctx_.get(), &sh_convolution_concat_112_48_96_, log};
    prog_convolution_concat_96_32_64_ =
        Program{"Convolution Concat 96 32 64", ctx_.get(), &sh_convolution_concat_96_32_64_, log};
    prog_convolution_concat_64_3_64_ =
        Program{"Convolution Concat 64 3 64", ctx_.get(), &sh_convolution_concat_64_3_64_, log};
    prog_convolution_concat_64_6_64_ =
        Program{"Convolution Concat 64 6 64", ctx_.get(), &sh_convolution_concat_64_6_64_, log};
    prog_convolution_concat_64_9_64_ =
        Program{"Convolution Concat 64 9 64", ctx_.get(), &sh_convolution_concat_64_9_64_, log};
    prog_convolution_32_3_img_ = Program{"Convolution 32 3 Img", ctx_.get(), &sh_convolution_32_3_img_, log};

    return pi_convolution_Img_3_32_.Init(ctx_.get(), &prog_convolution_Img_3_32_, log) &&
           pi_convolution_Img_6_32_.Init(ctx_.get(), &prog_convolution_Img_6_32_, log) &&
           pi_convolution_Img_9_32_.Init(ctx_.get(), &prog_convolution_Img_9_32_, log) &&
           pi_convolution_32_32_Downsample_.Init(ctx_.get(), &prog_convolution_32_32_Downsample_, log) &&
           pi_convolution_32_48_Downsample_.Init(ctx_.get(), &prog_convolution_32_48_Downsample_, log) &&
           pi_convolution_48_64_Downsample_.Init(ctx_.get(), &prog_convolution_48_64_Downsample_, log) &&
           pi_convolution_64_80_Downsample_.Init(ctx_.get(), &prog_convolution_64_80_Downsample_, log) &&
           pi_convolution_64_64_.Init(ctx_.get(), &prog_convolution_64_64_, log) &&
           pi_convolution_64_32_.Init(ctx_.get(), &prog_convolution_64_32_, log) &&
           pi_convolution_80_96_.Init(ctx_.get(), &prog_convolution_80_96_, log) &&
           pi_convolution_96_96_.Init(ctx_.get(), &prog_convolution_96_96_, log) &&
           pi_convolution_112_112_.Init(ctx_.get(), &prog_convolution_112_112_, log) &&
           pi_convolution_concat_96_64_112_.Init(ctx_.get(), &prog_convolution_concat_96_64_112_, log) &&
           pi_convolution_concat_112_48_96_.Init(ctx_.get(), &prog_convolution_concat_112_48_96_, log) &&
           pi_convolution_concat_96_32_64_.Init(ctx_.get(), &prog_convolution_concat_96_32_64_, log) &&
           pi_convolution_concat_64_3_64_.Init(ctx_.get(), &prog_convolution_concat_64_3_64_, log) &&
           pi_convolution_concat_64_6_64_.Init(ctx_.get(), &prog_convolution_concat_64_6_64_, log) &&
           pi_convolution_concat_64_9_64_.Init(ctx_.get(), &prog_convolution_concat_64_9_64_, log) &&
           pi_convolution_32_3_img_.Init(ctx_.get(), &prog_convolution_32_3_img_, log);
}

void Ray::Dx::Renderer::kernel_IntersectScene(CommandBuffer cmd_buf, const pass_settings_t &settings,
                                              const scene_data_t &sc_data, const Buffer &rand_seq,
                                              const uint32_t rand_seed, const int iteration, const rect_t &rect,
                                              const uint32_t node_index, const float cam_fwd[3], const float clip_dist,
                                              Span<const TextureAtlas> tex_atlases, const BindlessTexData &bindless_tex,
                                              const Buffer &rays, const Buffer &out_hits) {
    const TransitionInfo res_transitions[] = {{&rays, eResState::UnorderedAccess},
                                              {&out_hits, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 32> bindings = {
        {eBindTarget::SBufRO, IntersectScene::VERTICES_BUF_SLOT, sc_data.vertices},
        {eBindTarget::SBufRO, IntersectScene::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
        {eBindTarget::SBufRO, IntersectScene::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
        {eBindTarget::SBufRO, IntersectScene::MATERIALS_BUF_SLOT, sc_data.materials},
        {eBindTarget::SBufRO, IntersectScene::RANDOM_SEQ_BUF_SLOT, rand_seq},
        {eBindTarget::SBufRW, IntersectScene::RAYS_BUF_SLOT, rays},
        {eBindTarget::SBufRW, IntersectScene::OUT_HITS_BUF_SLOT, out_hits}};

    if (use_bindless_) {
        bindings.emplace_back(eBindTarget::Sampler, Types::TEXTURES_SAMPLER_SLOT, bindless_tex.shared_sampler);
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_SIZE_SLOT, bindless_tex.tex_sizes);

        bindings.emplace_back(eBindTarget::DescrTable, 2, bindless_tex.srv_descr_table);

        // assert(tex_descr_set);
        // vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi_intersect_scene_.layout(), 1, 1,
        //                         &tex_descr_set, 0, nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArraySampled, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    if (use_hwrt_) {
        bindings.emplace_back(eBindTarget::AccStruct, IntersectScene::TLAS_SLOT, sc_data.rt_tlas);
    } else {
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::TRIS_BUF_SLOT, sc_data.tris);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::TRI_INDICES_BUF_SLOT, sc_data.tri_indices);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::NODES_BUF_SLOT, sc_data.nodes);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::MESHES_BUF_SLOT, sc_data.meshes);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::MI_INDICES_BUF_SLOT, sc_data.mi_indices);
    }

    IntersectScene::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.node_index = node_index;
    uniform_params.clip_dist = clip_dist;
    uniform_params.min_transp_depth = settings.min_transp_depth;
    uniform_params.max_transp_depth = settings.max_transp_depth;
    uniform_params.rand_seed = rand_seed;
    uniform_params.iteration = iteration;
    if (cam_fwd) {
        memcpy(&uniform_params.cam_fwd[0], &cam_fwd[0], 3 * sizeof(float));
    }

    const uint32_t grp_count[3] = {
        uint32_t((rect.w + IntersectScene::LOCAL_GROUP_SIZE_X - 1) / IntersectScene::LOCAL_GROUP_SIZE_X),
        uint32_t((rect.h + IntersectScene::LOCAL_GROUP_SIZE_Y - 1) / IntersectScene::LOCAL_GROUP_SIZE_Y), 1u};

    DispatchCompute(cmd_buf, pi_intersect_scene_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Dx::Renderer::kernel_IntersectScene(CommandBuffer cmd_buf, const Buffer &indir_args,
                                              const int indir_args_index, const Buffer &counters,
                                              const pass_settings_t &settings, const scene_data_t &sc_data,
                                              const Buffer &rand_seq, const uint32_t rand_seed, const int iteration,
                                              uint32_t node_index, const float cam_fwd[3], const float clip_dist,
                                              Span<const TextureAtlas> tex_atlases, const BindlessTexData &bindless_tex,
                                              const Buffer &rays, const Buffer &out_hits) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&counters, eResState::ShaderResource},
                                              {&rays, eResState::UnorderedAccess},
                                              {&out_hits, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 32> bindings = {
        {eBindTarget::SBufRO, IntersectScene::VERTICES_BUF_SLOT, sc_data.vertices},
        {eBindTarget::SBufRO, IntersectScene::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
        {eBindTarget::SBufRO, IntersectScene::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
        {eBindTarget::SBufRO, IntersectScene::MATERIALS_BUF_SLOT, sc_data.materials},
        {eBindTarget::SBufRO, IntersectScene::RANDOM_SEQ_BUF_SLOT, rand_seq},
        {eBindTarget::SBufRW, IntersectScene::RAYS_BUF_SLOT, rays},
        {eBindTarget::SBufRO, IntersectScene::COUNTERS_BUF_SLOT, counters},
        {eBindTarget::SBufRW, IntersectScene::OUT_HITS_BUF_SLOT, out_hits}};

    if (use_bindless_) {
        bindings.emplace_back(eBindTarget::Sampler, Types::TEXTURES_SAMPLER_SLOT, bindless_tex.shared_sampler);
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_SIZE_SLOT, bindless_tex.tex_sizes);

        bindings.emplace_back(eBindTarget::DescrTable, 2, bindless_tex.srv_descr_table);

        // assert(tex_descr_set);
        // vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi_intersect_scene_indirect_.layout(), 1, 1,
        //                         &tex_descr_set, 0, nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArraySampled, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    if (use_hwrt_) {
        bindings.emplace_back(eBindTarget::AccStruct, IntersectScene::TLAS_SLOT, sc_data.rt_tlas);
    } else {
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::TRIS_BUF_SLOT, sc_data.tris);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::TRI_INDICES_BUF_SLOT, sc_data.tri_indices);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::NODES_BUF_SLOT, sc_data.nodes);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::MESHES_BUF_SLOT, sc_data.meshes);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances);
        bindings.emplace_back(eBindTarget::SBufRO, IntersectScene::MI_INDICES_BUF_SLOT, sc_data.mi_indices);
    }

    IntersectScene::Params uniform_params = {};
    uniform_params.node_index = node_index;
    uniform_params.clip_dist = clip_dist;
    uniform_params.min_transp_depth = settings.min_transp_depth;
    uniform_params.max_transp_depth = settings.max_transp_depth;
    uniform_params.rand_seed = rand_seed;
    uniform_params.iteration = iteration;
    if (cam_fwd) {
        memcpy(&uniform_params.cam_fwd[0], &cam_fwd[0], 3 * sizeof(float));
    }

    DispatchComputeIndirect(cmd_buf, pi_intersect_scene_indirect_, indir_args,
                            indir_args_index * sizeof(DispatchIndirectCommand), bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Dx::Renderer::kernel_ShadePrimaryHits(
    CommandBuffer cmd_buf, const pass_settings_t &settings, const environment_t &env, const Buffer &indir_args,
    const int indir_args_index, const Buffer &hits, const Buffer &rays, const scene_data_t &sc_data,
    const Buffer &rand_seq, const uint32_t rand_seed, const int iteration, const rect_t &rect,
    Span<const TextureAtlas> tex_atlases, const BindlessTexData &bindless_tex, const Texture2D &out_img,
    const Buffer &out_rays, const Buffer &out_sh_rays, const Buffer &inout_counters, const Texture2D &out_base_color,
    const Texture2D &out_depth_normals) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&hits, eResState::ShaderResource},
                                              {&rays, eResState::ShaderResource},
                                              {&rand_seq, eResState::ShaderResource},
                                              {&out_img, eResState::UnorderedAccess},
                                              {&out_rays, eResState::UnorderedAccess},
                                              {&out_sh_rays, eResState::UnorderedAccess},
                                              {&inout_counters, eResState::UnorderedAccess},
                                              {&out_base_color, eResState::UnorderedAccess},
                                              {&out_depth_normals, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 32> bindings = {{eBindTarget::SBufRO, Shade::HITS_BUF_SLOT, hits},
                                         {eBindTarget::SBufRO, Shade::RAYS_BUF_SLOT, rays},
                                         {eBindTarget::SBufRO, Shade::LIGHTS_BUF_SLOT, sc_data.lights},
                                         {eBindTarget::SBufRO, Shade::LI_INDICES_BUF_SLOT, sc_data.li_indices},
                                         {eBindTarget::SBufRO, Shade::TRIS_BUF_SLOT, sc_data.tris},
                                         {eBindTarget::SBufRO, Shade::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
                                         {eBindTarget::SBufRO, Shade::MATERIALS_BUF_SLOT, sc_data.materials},
                                         {eBindTarget::SBufRO, Shade::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances},
                                         {eBindTarget::SBufRO, Shade::VERTICES_BUF_SLOT, sc_data.vertices},
                                         {eBindTarget::SBufRO, Shade::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
                                         {eBindTarget::SBufRO, Shade::RANDOM_SEQ_BUF_SLOT, rand_seq},
                                         {eBindTarget::SBufRO, Shade::LIGHT_WNODES_BUF_SLOT, sc_data.light_wnodes},
                                         {eBindTarget::Tex2D, Shade::ENV_QTREE_TEX_SLOT, sc_data.env_qtree},
                                         {eBindTarget::Image, Shade::OUT_IMG_SLOT, out_img},
                                         {eBindTarget::SBufRW, Shade::OUT_RAYS_BUF_SLOT, out_rays},
                                         {eBindTarget::SBufRW, Shade::OUT_SH_RAYS_BUF_SLOT, out_sh_rays},
                                         {eBindTarget::SBufRW, Shade::INOUT_COUNTERS_BUF_SLOT, inout_counters}};

    if (out_base_color.ready()) {
        bindings.emplace_back(eBindTarget::Image, Shade::OUT_BASE_COLOR_IMG_SLOT, out_base_color);
    }
    if (out_depth_normals.ready()) {
        bindings.emplace_back(eBindTarget::Image, Shade::OUT_DEPTH_NORMALS_IMG_SLOT, out_depth_normals);
    }

    Shade::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.iteration = iteration;
    uniform_params.li_count = sc_data.li_count;
    uniform_params.env_qtree_levels = sc_data.env_qtree_levels;
    uniform_params.regularize_alpha = settings.regularize_alpha;

    uniform_params.max_ray_depth = Ref::pack_ray_depth(settings.max_diff_depth, settings.max_spec_depth,
                                                       settings.max_refr_depth, settings.max_transp_depth);
    uniform_params.max_total_depth = settings.max_total_depth;
    uniform_params.min_total_depth = settings.min_total_depth;

    uniform_params.rand_seed = rand_seed;

    memcpy(&uniform_params.env_col[0], env.env_col, 3 * sizeof(float));
    memcpy(&uniform_params.env_col[3], &env.env_map, sizeof(uint32_t));
    memcpy(&uniform_params.back_col[0], env.back_col, 3 * sizeof(float));
    memcpy(&uniform_params.back_col[3], &env.back_map, sizeof(uint32_t));

    uniform_params.env_map_res = env.env_map_res;
    uniform_params.back_map_res = env.back_map_res;

    uniform_params.env_rotation = env.env_map_rotation;
    uniform_params.back_rotation = env.back_map_rotation;
    uniform_params.env_light_index = sc_data.env->light_index;

    uniform_params.clamp_val = (settings.clamp_direct != 0.0f) ? settings.clamp_direct : FLT_MAX;

    Pipeline *pi = &pi_shade_primary_;
    if (out_base_color.ready()) {
        if (out_depth_normals.ready()) {
            pi = &pi_shade_primary_bn_;
        } else {
            pi = &pi_shade_primary_b_;
        }
    } else if (out_depth_normals.ready()) {
        pi = &pi_shade_primary_n_;
    }

    if (use_bindless_) {
        bindings.emplace_back(eBindTarget::Sampler, Types::TEXTURES_SAMPLER_SLOT, bindless_tex.shared_sampler);
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_SIZE_SLOT, bindless_tex.tex_sizes);
        bindings.emplace_back(eBindTarget::DescrTable, 2, bindless_tex.srv_descr_table);
    } else {
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArraySampled, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    DispatchComputeIndirect(cmd_buf, *pi, indir_args, indir_args_index * sizeof(DispatchIndirectCommand), bindings,
                            &uniform_params, sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Dx::Renderer::kernel_ShadeSecondaryHits(
    CommandBuffer cmd_buf, const pass_settings_t &settings, float clamp_val, const environment_t &env,
    const Buffer &indir_args, const int indir_args_index, const Buffer &hits, const Buffer &rays,
    const scene_data_t &sc_data, const Buffer &rand_seq, const uint32_t rand_seed, const int iteration,
    Span<const TextureAtlas> tex_atlases, const BindlessTexData &bindless_tex, const Texture2D &out_img,
    const Buffer &out_rays, const Buffer &out_sh_rays, const Buffer &inout_counters) {
    const TransitionInfo res_transitions[] = {
        {&indir_args, eResState::IndirectArgument}, {&hits, eResState::ShaderResource},
        {&rays, eResState::ShaderResource},         {&rand_seq, eResState::ShaderResource},
        {&out_img, eResState::UnorderedAccess},     {&out_rays, eResState::UnorderedAccess},
        {&out_sh_rays, eResState::UnorderedAccess}, {&inout_counters, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 32> bindings = {{eBindTarget::SBufRO, Shade::HITS_BUF_SLOT, hits},
                                         {eBindTarget::SBufRO, Shade::RAYS_BUF_SLOT, rays},
                                         {eBindTarget::SBufRO, Shade::LIGHTS_BUF_SLOT, sc_data.lights},
                                         {eBindTarget::SBufRO, Shade::LI_INDICES_BUF_SLOT, sc_data.li_indices},
                                         {eBindTarget::SBufRO, Shade::TRIS_BUF_SLOT, sc_data.tris},
                                         {eBindTarget::SBufRO, Shade::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
                                         {eBindTarget::SBufRO, Shade::MATERIALS_BUF_SLOT, sc_data.materials},
                                         {eBindTarget::SBufRO, Shade::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances},
                                         {eBindTarget::SBufRO, Shade::VERTICES_BUF_SLOT, sc_data.vertices},
                                         {eBindTarget::SBufRO, Shade::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
                                         {eBindTarget::SBufRO, Shade::RANDOM_SEQ_BUF_SLOT, rand_seq},
                                         {eBindTarget::SBufRO, Shade::LIGHT_WNODES_BUF_SLOT, sc_data.light_wnodes},
                                         {eBindTarget::Tex2D, Shade::ENV_QTREE_TEX_SLOT, sc_data.env_qtree},
                                         {eBindTarget::Image, Shade::OUT_IMG_SLOT, out_img},
                                         {eBindTarget::SBufRW, Shade::OUT_RAYS_BUF_SLOT, out_rays},
                                         {eBindTarget::SBufRW, Shade::OUT_SH_RAYS_BUF_SLOT, out_sh_rays},
                                         {eBindTarget::SBufRW, Shade::INOUT_COUNTERS_BUF_SLOT, inout_counters}};

    Shade::Params uniform_params = {};
    uniform_params.iteration = iteration;
    uniform_params.li_count = sc_data.li_count;
    uniform_params.env_qtree_levels = sc_data.env_qtree_levels;
    uniform_params.regularize_alpha = settings.regularize_alpha;

    uniform_params.max_ray_depth = Ref::pack_ray_depth(settings.max_diff_depth, settings.max_spec_depth,
                                                       settings.max_refr_depth, settings.max_transp_depth);
    uniform_params.max_total_depth = settings.max_total_depth;
    uniform_params.min_total_depth = settings.min_total_depth;

    uniform_params.rand_seed = rand_seed;

    memcpy(&uniform_params.env_col[0], env.env_col, 3 * sizeof(float));
    memcpy(&uniform_params.env_col[3], &env.env_map, sizeof(uint32_t));
    memcpy(&uniform_params.back_col[0], env.back_col, 3 * sizeof(float));
    memcpy(&uniform_params.back_col[3], &env.back_map, sizeof(uint32_t));

    uniform_params.env_map_res = env.env_map_res;
    uniform_params.back_map_res = env.back_map_res;

    uniform_params.env_rotation = env.env_map_rotation;
    uniform_params.back_rotation = env.back_map_rotation;
    uniform_params.env_light_index = sc_data.env->light_index;

    uniform_params.clamp_val = (clamp_val != 0.0f) ? clamp_val : FLT_MAX;

    if (use_bindless_) {
        bindings.emplace_back(eBindTarget::Sampler, Types::TEXTURES_SAMPLER_SLOT, bindless_tex.shared_sampler);
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_SIZE_SLOT, bindless_tex.tex_sizes);
        bindings.emplace_back(eBindTarget::DescrTable, 2, bindless_tex.srv_descr_table);

        // assert(tex_descr_set);
        // vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi_shade_secondary_.layout(), 1, 1,
        //                         &tex_descr_set, 0, nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArraySampled, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    DispatchComputeIndirect(cmd_buf, pi_shade_secondary_, indir_args,
                            indir_args_index * sizeof(DispatchIndirectCommand), bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Dx::Renderer::kernel_IntersectSceneShadow(
    CommandBuffer cmd_buf, const pass_settings_t &settings, const Buffer &indir_args, const int indir_args_index,
    const Buffer &counters, const scene_data_t &sc_data, const Buffer &rand_seq, const uint32_t rand_seed,
    const int iteration, const uint32_t node_index, const float clamp_val, Span<const TextureAtlas> tex_atlases,
    const BindlessTexData &bindless_tex, const Buffer &sh_rays, const Texture2D &out_img) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&counters, eResState::ShaderResource},
                                              {&sh_rays, eResState::ShaderResource},
                                              {&out_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 32> bindings = {
        {eBindTarget::SBufRO, IntersectSceneShadow::TRIS_BUF_SLOT, sc_data.tris},
        {eBindTarget::SBufRO, IntersectSceneShadow::TRI_INDICES_BUF_SLOT, sc_data.tri_indices},
        {eBindTarget::SBufRO, IntersectSceneShadow::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
        {eBindTarget::SBufRO, IntersectSceneShadow::MATERIALS_BUF_SLOT, sc_data.materials},
        {eBindTarget::SBufRO, IntersectSceneShadow::NODES_BUF_SLOT, sc_data.nodes},
        {eBindTarget::SBufRO, IntersectSceneShadow::MESHES_BUF_SLOT, sc_data.meshes},
        {eBindTarget::SBufRO, IntersectSceneShadow::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances},
        {eBindTarget::SBufRO, IntersectSceneShadow::MI_INDICES_BUF_SLOT, sc_data.mi_indices},
        {eBindTarget::SBufRO, IntersectSceneShadow::VERTICES_BUF_SLOT, sc_data.vertices},
        {eBindTarget::SBufRO, IntersectSceneShadow::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
        {eBindTarget::SBufRO, IntersectSceneShadow::SH_RAYS_BUF_SLOT, sh_rays},
        {eBindTarget::SBufRO, IntersectSceneShadow::COUNTERS_BUF_SLOT, counters},
        {eBindTarget::SBufRO, IntersectSceneShadow::LIGHTS_BUF_SLOT, sc_data.lights},
        {eBindTarget::SBufRO, IntersectSceneShadow::LIGHT_WNODES_BUF_SLOT, sc_data.light_wnodes},
        {eBindTarget::SBufRO, IntersectSceneShadow::RANDOM_SEQ_BUF_SLOT, rand_seq},
        {eBindTarget::Image, IntersectSceneShadow::INOUT_IMG_SLOT, out_img}};

    if (use_hwrt_) {
        bindings.emplace_back(eBindTarget::AccStruct, IntersectSceneShadow::TLAS_SLOT, sc_data.rt_tlas);
    }

    if (use_bindless_) {
        bindings.emplace_back(eBindTarget::Sampler, Types::TEXTURES_SAMPLER_SLOT, bindless_tex.shared_sampler);
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_SIZE_SLOT, bindless_tex.tex_sizes);
        bindings.emplace_back(eBindTarget::DescrTable, 2, bindless_tex.srv_descr_table);

        // assert(tex_descr_set);
        // vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi_intersect_scene_shadow_.layout(), 1, 1,
        //                         &tex_descr_set, 0, nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBufRO, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArraySampled, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    IntersectSceneShadow::Params uniform_params = {};
    uniform_params.node_index = node_index;
    uniform_params.max_transp_depth = settings.max_transp_depth;
    uniform_params.lights_node_index = 0; // tree root
    uniform_params.blocker_lights_count = sc_data.blocker_lights_count;
    uniform_params.clamp_val = (clamp_val != 0.0f) ? clamp_val : FLT_MAX;
    uniform_params.rand_seed = rand_seed;
    uniform_params.iteration = iteration;

    DispatchComputeIndirect(cmd_buf, pi_intersect_scene_shadow_, indir_args,
                            indir_args_index * sizeof(DispatchIndirectCommand), bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

Ray::RendererBase *Ray::Dx::CreateRenderer(const settings_t &s, ILog *log) { return new Dx::Renderer(s, log); }
