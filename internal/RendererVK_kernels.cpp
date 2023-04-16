#include "RendererVK.h"

#include "Vk/DrawCall.h"

#include "shaders/debug_rt_interface.h"
#include "shaders/filter_variance_interface.h"
#include "shaders/intersect_area_lights_interface.h"
#include "shaders/intersect_scene_interface.h"
#include "shaders/intersect_scene_shadow_interface.h"
#include "shaders/mix_incremental_interface.h"
#include "shaders/nlm_filter_interface.h"
#include "shaders/postprocess_interface.h"
#include "shaders/prepare_indir_args_interface.h"
#include "shaders/primary_ray_gen_interface.h"
#include "shaders/shade_interface.h"

#include "shaders/types.h"

void Ray::Vk::Renderer::kernel_GeneratePrimaryRays(VkCommandBuffer cmd_buf, const camera_t &cam, const int hi,
                                                   const rect_t &rect, const Buffer &random_seq,
                                                   const Buffer &out_rays) {
    const TransitionInfo res_transitions[] = {{&random_seq, eResState::ShaderResource},
                                              {&out_rays, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBuf, PrimaryRayGen::HALTON_SEQ_BUF_SLOT, random_seq},
                                {eBindTarget::SBuf, PrimaryRayGen::OUT_RAYS_BUF_SLOT, out_rays}};

    const uint32_t grp_count[3] = {
        uint32_t((rect.w + PrimaryRayGen::LOCAL_GROUP_SIZE_X - 1) / PrimaryRayGen::LOCAL_GROUP_SIZE_X),
        uint32_t((rect.h + PrimaryRayGen::LOCAL_GROUP_SIZE_Y - 1) / PrimaryRayGen::LOCAL_GROUP_SIZE_Y), 1u};

    PrimaryRayGen::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.img_size[0] = w_;
    uniform_params.img_size[1] = h_;
    uniform_params.hi = hi;

    const float temp = std::tan(0.5f * cam.fov * PI / 180.0f);
    uniform_params.spread_angle = std::atan(2.0f * temp / float(h_));

    memcpy(&uniform_params.cam_origin[0], cam.origin, 3 * sizeof(float));
    uniform_params.cam_origin[3] = temp;
    memcpy(&uniform_params.cam_fwd[0], cam.fwd, 3 * sizeof(float));
    memcpy(&uniform_params.cam_side[0], cam.side, 3 * sizeof(float));
    uniform_params.cam_side[3] = cam.focus_distance;
    memcpy(&uniform_params.cam_up[0], cam.up, 3 * sizeof(float));
    uniform_params.cam_up[3] = cam.sensor_height;
    uniform_params.cam_fstop = cam.fstop;
    uniform_params.cam_focal_length = cam.focal_length;
    uniform_params.cam_lens_rotation = cam.lens_rotation;
    uniform_params.cam_lens_ratio = cam.lens_ratio;
    uniform_params.cam_lens_blades = cam.lens_blades;
    uniform_params.cam_clip_start = cam.clip_start;
    uniform_params.cam_filter = int(cam.filter);
    uniform_params.shift_x = cam.shift[0];
    uniform_params.shift_y = cam.shift[1];

    DispatchCompute(cmd_buf, pi_prim_rays_gen_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_IntersectScenePrimary(VkCommandBuffer cmd_buf, const pass_settings_t &settings,
                                                     const scene_data_t &sc_data, const Buffer &random_seq,
                                                     const int hi, const rect_t &rect, const uint32_t node_index,
                                                     const float cam_clip_end, Span<const TextureAtlas> tex_atlases,
                                                     VkDescriptorSet tex_descr_set, const Buffer &rays,
                                                     const Buffer &out_hits) {
    const TransitionInfo res_transitions[] = {{&rays, eResState::UnorderedAccess},
                                              {&out_hits, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    IntersectScene::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.node_index = node_index;
    uniform_params.cam_clip_end = cam_clip_end;
    uniform_params.min_transp_depth = settings.min_transp_depth;
    uniform_params.max_transp_depth = settings.max_transp_depth;
    uniform_params.hi = hi;

    SmallVector<Binding, 32> bindings = {
        {eBindTarget::SBuf, IntersectScene::VERTICES_BUF_SLOT, sc_data.vertices},
        {eBindTarget::SBuf, IntersectScene::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
        {eBindTarget::SBuf, IntersectScene::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
        {eBindTarget::SBuf, IntersectScene::MATERIALS_BUF_SLOT, sc_data.materials},
        {eBindTarget::SBuf, IntersectScene::RANDOM_SEQ_BUF_SLOT, random_seq}};

    if (use_bindless_) {
        assert(tex_descr_set);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi_intersect_scene_primary_.layout(), 1, 1,
                                &tex_descr_set, 0, nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBuf, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArray, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    if (use_hwrt_) {
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::RAYS_BUF_SLOT, rays);
        bindings.emplace_back(eBindTarget::AccStruct, IntersectScene::TLAS_SLOT, sc_data.rt_tlas);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::OUT_HITS_BUF_SLOT, out_hits);
    } else {
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::TRIS_BUF_SLOT, sc_data.tris);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::TRI_INDICES_BUF_SLOT, sc_data.tri_indices);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::MATERIALS_BUF_SLOT, sc_data.materials);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::NODES_BUF_SLOT, sc_data.nodes);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::MESHES_BUF_SLOT, sc_data.meshes);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::MI_INDICES_BUF_SLOT, sc_data.mi_indices);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::TRANSFORMS_BUF_SLOT, sc_data.transforms);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::RAYS_BUF_SLOT, rays);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::OUT_HITS_BUF_SLOT, out_hits);
    }

    const uint32_t grp_count[3] = {
        uint32_t((rect.w + IntersectScene::LOCAL_GROUP_SIZE_X - 1) / IntersectScene::LOCAL_GROUP_SIZE_X),
        uint32_t((rect.h + IntersectScene::LOCAL_GROUP_SIZE_Y - 1) / IntersectScene::LOCAL_GROUP_SIZE_Y), 1u};

    DispatchCompute(cmd_buf, pi_intersect_scene_primary_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_IntersectSceneSecondary(
    VkCommandBuffer cmd_buf, const Buffer &indir_args, const Buffer &counters, const pass_settings_t &settings,
    const scene_data_t &sc_data, const Buffer &random_seq, const int hi, uint32_t node_index,
    Span<const TextureAtlas> tex_atlases, VkDescriptorSet tex_descr_set, const Buffer &rays, const Buffer &out_hits) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&counters, eResState::ShaderResource},
                                              {&rays, eResState::UnorderedAccess},
                                              {&out_hits, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    IntersectScene::Params uniform_params = {};
    uniform_params.node_index = node_index;
    uniform_params.min_transp_depth = settings.min_transp_depth;
    uniform_params.max_transp_depth = settings.max_transp_depth;
    uniform_params.hi = hi;

    SmallVector<Binding, 32> bindings = {
        {eBindTarget::SBuf, IntersectScene::VERTICES_BUF_SLOT, sc_data.vertices},
        {eBindTarget::SBuf, IntersectScene::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
        {eBindTarget::SBuf, IntersectScene::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
        {eBindTarget::SBuf, IntersectScene::MATERIALS_BUF_SLOT, sc_data.materials},
        {eBindTarget::SBuf, IntersectScene::RANDOM_SEQ_BUF_SLOT, random_seq},
        {eBindTarget::SBuf, IntersectScene::RAYS_BUF_SLOT, rays},
        {eBindTarget::SBuf, IntersectScene::COUNTERS_BUF_SLOT, counters},
        {eBindTarget::SBuf, IntersectScene::OUT_HITS_BUF_SLOT, out_hits}};

    if (use_bindless_) {
        assert(tex_descr_set);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi_intersect_scene_secondary_.layout(), 1, 1,
                                &tex_descr_set, 0, nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBuf, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArray, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    if (use_hwrt_) {
        bindings.emplace_back(eBindTarget::AccStruct, IntersectScene::TLAS_SLOT, sc_data.rt_tlas);
    } else {
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::TRIS_BUF_SLOT, sc_data.tris);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::TRI_INDICES_BUF_SLOT, sc_data.tri_indices);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::NODES_BUF_SLOT, sc_data.nodes);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::MESHES_BUF_SLOT, sc_data.meshes);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::MI_INDICES_BUF_SLOT, sc_data.mi_indices);
        bindings.emplace_back(eBindTarget::SBuf, IntersectScene::TRANSFORMS_BUF_SLOT, sc_data.transforms);
    }

    DispatchComputeIndirect(cmd_buf, pi_intersect_scene_secondary_, indir_args, 0, bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_IntersectAreaLights(VkCommandBuffer cmd_buf, const scene_data_t &sc_data,
                                                   const Buffer &indir_args, const Buffer &counters, const Buffer &rays,
                                                   const Buffer &inout_hits) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&counters, eResState::ShaderResource},
                                              {&rays, eResState::ShaderResource},
                                              {&inout_hits, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {
        {eBindTarget::SBuf, IntersectAreaLights::RAYS_BUF_SLOT, rays},
        {eBindTarget::SBuf, IntersectAreaLights::LIGHTS_BUF_SLOT, sc_data.lights},
        {eBindTarget::SBuf, IntersectAreaLights::VISIBLE_LIGHTS_BUF_SLOT, sc_data.visible_lights},
        {eBindTarget::SBuf, IntersectAreaLights::TRANSFORMS_BUF_SLOT, sc_data.transforms},
        {eBindTarget::SBuf, IntersectAreaLights::COUNTERS_BUF_SLOT, counters},
        {eBindTarget::SBuf, IntersectAreaLights::INOUT_HITS_BUF_SLOT, inout_hits}};

    IntersectAreaLights::Params uniform_params = {};
    uniform_params.img_size[0] = w_;
    uniform_params.img_size[1] = h_;
    uniform_params.visible_lights_count = sc_data.visible_lights_count;

    DispatchComputeIndirect(cmd_buf, pi_intersect_area_lights_, indir_args, 0, bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_ShadePrimaryHits(VkCommandBuffer cmd_buf, const pass_settings_t &settings,
                                                const environment_t &env, const Buffer &hits, const Buffer &rays,
                                                const scene_data_t &sc_data, const Buffer &random_seq, const int hi,
                                                const rect_t &rect, Span<const TextureAtlas> tex_atlases,
                                                VkDescriptorSet tex_descr_set, const Texture2D &out_img,
                                                const Buffer &out_rays, const Buffer &out_sh_rays,
                                                const Buffer &inout_counters, const Texture2D &out_base_color,
                                                const Texture2D &out_depth_normals) {
    const TransitionInfo res_transitions[] = {{&hits, eResState::ShaderResource},
                                              {&rays, eResState::ShaderResource},
                                              {&random_seq, eResState::ShaderResource},
                                              {&out_img, eResState::UnorderedAccess},
                                              {&out_rays, eResState::UnorderedAccess},
                                              {&out_sh_rays, eResState::UnorderedAccess},
                                              {&inout_counters, eResState::UnorderedAccess},
                                              {&out_base_color, eResState::UnorderedAccess},
                                              {&out_depth_normals, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 32> bindings = {{eBindTarget::SBuf, Shade::HITS_BUF_SLOT, hits},
                                         {eBindTarget::SBuf, Shade::RAYS_BUF_SLOT, rays},
                                         {eBindTarget::SBuf, Shade::LIGHTS_BUF_SLOT, sc_data.lights},
                                         {eBindTarget::SBuf, Shade::LI_INDICES_BUF_SLOT, sc_data.li_indices},
                                         {eBindTarget::SBuf, Shade::TRIS_BUF_SLOT, sc_data.tris},
                                         {eBindTarget::SBuf, Shade::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
                                         {eBindTarget::SBuf, Shade::MATERIALS_BUF_SLOT, sc_data.materials},
                                         {eBindTarget::SBuf, Shade::TRANSFORMS_BUF_SLOT, sc_data.transforms},
                                         {eBindTarget::SBuf, Shade::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances},
                                         {eBindTarget::SBuf, Shade::VERTICES_BUF_SLOT, sc_data.vertices},
                                         {eBindTarget::SBuf, Shade::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
                                         {eBindTarget::SBuf, Shade::RANDOM_SEQ_BUF_SLOT, random_seq},
                                         {eBindTarget::Tex2D, Shade::ENV_QTREE_TEX_SLOT, sc_data.env_qtree},
                                         {eBindTarget::Image, Shade::OUT_IMG_SLOT, out_img},
                                         {eBindTarget::SBuf, Shade::OUT_RAYS_BUF_SLOT, out_rays},
                                         {eBindTarget::SBuf, Shade::OUT_SH_RAYS_BUF_SLOT, out_sh_rays},
                                         {eBindTarget::SBuf, Shade::INOUT_COUNTERS_BUF_SLOT, inout_counters}};

    if (out_base_color.ready()) {
        bindings.emplace_back(eBindTarget::Image, Shade::OUT_BASE_COLOR_IMG_SLOT, out_base_color);
    }
    if (out_depth_normals.ready()) {
        bindings.emplace_back(eBindTarget::Image, Shade::OUT_DEPTH_NORMALS_IMG_SLOT, out_depth_normals);
    }

    const uint32_t grp_count[3] = {uint32_t((rect.w + Shade::LOCAL_GROUP_SIZE_X - 1) / Shade::LOCAL_GROUP_SIZE_X),
                                   uint32_t((rect.h + Shade::LOCAL_GROUP_SIZE_Y - 1) / Shade::LOCAL_GROUP_SIZE_Y), 1u};

    Shade::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.hi = hi;
    uniform_params.li_count = sc_data.li_count;
    uniform_params.env_qtree_levels = sc_data.env_qtree_levels;

    uniform_params.max_diff_depth = settings.max_diff_depth;
    uniform_params.max_spec_depth = settings.max_spec_depth;
    uniform_params.max_refr_depth = settings.max_refr_depth;
    uniform_params.max_transp_depth = settings.max_transp_depth;
    uniform_params.max_total_depth = settings.max_total_depth;
    uniform_params.min_total_depth = settings.min_total_depth;
    uniform_params.min_transp_depth = settings.min_transp_depth;

    memcpy(&uniform_params.env_col[0], env.env_col, 3 * sizeof(float));
    memcpy(&uniform_params.env_col[3], &env.env_map, sizeof(uint32_t));
    memcpy(&uniform_params.back_col[0], env.back_col, 3 * sizeof(float));
    memcpy(&uniform_params.back_col[3], &env.back_map, sizeof(uint32_t));

    uniform_params.env_rotation = env.env_map_rotation;
    uniform_params.back_rotation = env.back_map_rotation;
    uniform_params.env_mult_importance = sc_data.env->multiple_importance ? 1 : 0;

    uniform_params.clamp_val =
        (settings.clamp_direct != 0.0f) ? settings.clamp_direct : std::numeric_limits<float>::max();

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
        assert(tex_descr_set);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi->layout(), 1, 1, &tex_descr_set, 0,
                                nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBuf, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArray, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    DispatchCompute(cmd_buf, *pi, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_ShadeSecondaryHits(VkCommandBuffer cmd_buf, const pass_settings_t &settings,
                                                  const environment_t &env, const Buffer &indir_args,
                                                  const Buffer &hits, const Buffer &rays, const scene_data_t &sc_data,
                                                  const Buffer &random_seq, const int hi,
                                                  Span<const TextureAtlas> tex_atlases, VkDescriptorSet tex_descr_set,
                                                  const Texture2D &out_img, const Buffer &out_rays,
                                                  const Buffer &out_sh_rays, const Buffer &inout_counters) {
    const TransitionInfo res_transitions[] = {
        {&indir_args, eResState::IndirectArgument}, {&hits, eResState::ShaderResource},
        {&rays, eResState::ShaderResource},         {&random_seq, eResState::ShaderResource},
        {&out_img, eResState::UnorderedAccess},     {&out_rays, eResState::UnorderedAccess},
        {&out_sh_rays, eResState::UnorderedAccess}, {&inout_counters, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 32> bindings = {{eBindTarget::SBuf, Shade::HITS_BUF_SLOT, hits},
                                         {eBindTarget::SBuf, Shade::RAYS_BUF_SLOT, rays},
                                         {eBindTarget::SBuf, Shade::LIGHTS_BUF_SLOT, sc_data.lights},
                                         {eBindTarget::SBuf, Shade::LI_INDICES_BUF_SLOT, sc_data.li_indices},
                                         {eBindTarget::SBuf, Shade::TRIS_BUF_SLOT, sc_data.tris},
                                         {eBindTarget::SBuf, Shade::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
                                         {eBindTarget::SBuf, Shade::MATERIALS_BUF_SLOT, sc_data.materials},
                                         {eBindTarget::SBuf, Shade::TRANSFORMS_BUF_SLOT, sc_data.transforms},
                                         {eBindTarget::SBuf, Shade::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances},
                                         {eBindTarget::SBuf, Shade::VERTICES_BUF_SLOT, sc_data.vertices},
                                         {eBindTarget::SBuf, Shade::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
                                         {eBindTarget::SBuf, Shade::RANDOM_SEQ_BUF_SLOT, random_seq},
                                         {eBindTarget::Tex2D, Shade::ENV_QTREE_TEX_SLOT, sc_data.env_qtree},
                                         {eBindTarget::Image, Shade::OUT_IMG_SLOT, out_img},
                                         {eBindTarget::SBuf, Shade::OUT_RAYS_BUF_SLOT, out_rays},
                                         {eBindTarget::SBuf, Shade::OUT_SH_RAYS_BUF_SLOT, out_sh_rays},
                                         {eBindTarget::SBuf, Shade::INOUT_COUNTERS_BUF_SLOT, inout_counters}};

    Shade::Params uniform_params = {};
    uniform_params.hi = hi;
    uniform_params.li_count = sc_data.li_count;
    uniform_params.env_qtree_levels = sc_data.env_qtree_levels;

    uniform_params.max_diff_depth = settings.max_diff_depth;
    uniform_params.max_spec_depth = settings.max_spec_depth;
    uniform_params.max_refr_depth = settings.max_refr_depth;
    uniform_params.max_transp_depth = settings.max_transp_depth;
    uniform_params.max_total_depth = settings.max_total_depth;
    uniform_params.min_total_depth = settings.min_total_depth;
    uniform_params.min_transp_depth = settings.min_transp_depth;

    memcpy(&uniform_params.env_col[0], env.env_col, 3 * sizeof(float));
    memcpy(&uniform_params.env_col[3], &env.env_map, sizeof(uint32_t));
    memcpy(&uniform_params.back_col[0], env.back_col, 3 * sizeof(float));
    memcpy(&uniform_params.back_col[3], &env.back_map, sizeof(uint32_t));

    uniform_params.env_rotation = env.env_map_rotation;
    uniform_params.back_rotation = env.back_map_rotation;
    uniform_params.env_mult_importance = sc_data.env->multiple_importance ? 1 : 0;

    uniform_params.clamp_val =
        (settings.clamp_indirect != 0.0f) ? settings.clamp_indirect : std::numeric_limits<float>::max();

    if (use_bindless_) {
        assert(tex_descr_set);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi_shade_secondary_.layout(), 1, 1,
                                &tex_descr_set, 0, nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBuf, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArray, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    DispatchComputeIndirect(cmd_buf, pi_shade_secondary_, indir_args, 0, bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_IntersectSceneShadow(VkCommandBuffer cmd_buf, const pass_settings_t &settings,
                                                    const Buffer &indir_args, const Buffer &counters,
                                                    const scene_data_t &sc_data, const uint32_t node_index,
                                                    const float clamp_val, Span<const TextureAtlas> tex_atlases,
                                                    VkDescriptorSet tex_descr_set, const Buffer &sh_rays,
                                                    const Texture2D &out_img) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&counters, eResState::ShaderResource},
                                              {&sh_rays, eResState::ShaderResource},
                                              {&out_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 32> bindings = {
        {eBindTarget::SBuf, IntersectSceneShadow::TRIS_BUF_SLOT, sc_data.tris},
        {eBindTarget::SBuf, IntersectSceneShadow::TRI_INDICES_BUF_SLOT, sc_data.tri_indices},
        {eBindTarget::SBuf, IntersectSceneShadow::TRI_MATERIALS_BUF_SLOT, sc_data.tri_materials},
        {eBindTarget::SBuf, IntersectSceneShadow::MATERIALS_BUF_SLOT, sc_data.materials},
        {eBindTarget::SBuf, IntersectSceneShadow::NODES_BUF_SLOT, sc_data.nodes},
        {eBindTarget::SBuf, IntersectSceneShadow::MESHES_BUF_SLOT, sc_data.meshes},
        {eBindTarget::SBuf, IntersectSceneShadow::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances},
        {eBindTarget::SBuf, IntersectSceneShadow::MI_INDICES_BUF_SLOT, sc_data.mi_indices},
        {eBindTarget::SBuf, IntersectSceneShadow::TRANSFORMS_BUF_SLOT, sc_data.transforms},
        {eBindTarget::SBuf, IntersectSceneShadow::VERTICES_BUF_SLOT, sc_data.vertices},
        {eBindTarget::SBuf, IntersectSceneShadow::VTX_INDICES_BUF_SLOT, sc_data.vtx_indices},
        {eBindTarget::SBuf, IntersectSceneShadow::SH_RAYS_BUF_SLOT, sh_rays},
        {eBindTarget::SBuf, IntersectSceneShadow::COUNTERS_BUF_SLOT, counters},
        {eBindTarget::SBuf, IntersectSceneShadow::LIGHTS_BUF_SLOT, sc_data.lights},
        {eBindTarget::SBuf, IntersectSceneShadow::BLOCKER_LIGHTS_BUF_SLOT, sc_data.blocker_lights},
        {eBindTarget::Image, IntersectSceneShadow::INOUT_IMG_SLOT, out_img}};

    if (use_hwrt_) {
        bindings.emplace_back(eBindTarget::AccStruct, IntersectSceneShadow::TLAS_SLOT, sc_data.rt_tlas);
    }

    if (use_bindless_) {
        assert(tex_descr_set);
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, pi_intersect_scene_shadow_.layout(), 1, 1,
                                &tex_descr_set, 0, nullptr);
    } else {
        bindings.emplace_back(eBindTarget::SBuf, Types::TEXTURES_BUF_SLOT, sc_data.atlas_textures);
        bindings.emplace_back(eBindTarget::Tex2DArray, Types::TEXTURE_ATLASES_SLOT, tex_atlases);
    }

    IntersectSceneShadow::Params uniform_params = {};
    uniform_params.node_index = node_index;
    uniform_params.max_transp_depth = settings.max_transp_depth;
    uniform_params.blocker_lights_count = sc_data.blocker_lights_count;
    uniform_params.clamp_val = (clamp_val != 0.0f) ? clamp_val : std::numeric_limits<float>::max();

    DispatchComputeIndirect(cmd_buf, pi_intersect_scene_shadow_, indir_args, sizeof(DispatchIndirectCommand), bindings,
                            &uniform_params, sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_PrepareIndirArgs(VkCommandBuffer cmd_buf, const Buffer &inout_counters,
                                                const Buffer &out_indir_args) {
    const TransitionInfo res_transitions[] = {{&inout_counters, eResState::UnorderedAccess},
                                              {&out_indir_args, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBuf, PrepareIndirectArgs::INOUT_COUNTERS_BUF_SLOT, inout_counters},
                                {eBindTarget::SBuf, PrepareIndirectArgs::OUT_INDIR_ARGS_SLOT, out_indir_args}};

    const uint32_t grp_count[3] = {1u, 1u, 1u};

    DispatchCompute(cmd_buf, pi_prepare_indir_args_, grp_count, bindings, nullptr, 0, ctx_->default_descr_alloc(),
                    ctx_->log());
}

void Ray::Vk::Renderer::kernel_MixIncremental(VkCommandBuffer cmd_buf, const float main_mix_factor,
                                              const float aux_mix_factor, const rect_t &rect, const Texture2D &temp_img,
                                              const Texture2D &temp_base_color, const Texture2D &temp_depth_normals,
                                              const Texture2D &out_img, const Texture2D &out_base_color,
                                              const Texture2D &out_depth_normals) {
    const TransitionInfo res_transitions[] = {
        {&temp_img, eResState::UnorderedAccess},           {&out_img, eResState::UnorderedAccess},
        {&temp_base_color, eResState::UnorderedAccess},    {&out_base_color, eResState::UnorderedAccess},
        {&temp_depth_normals, eResState::UnorderedAccess}, {&out_depth_normals, eResState::UnorderedAccess}};
    SmallVector<Binding, 16> bindings = {{eBindTarget::Image, MixIncremental::IN_TEMP_IMG_SLOT, temp_img},
                                         {eBindTarget::Image, MixIncremental::OUT_IMG_SLOT, out_img}};
    if (out_base_color.ready()) {
        bindings.emplace_back(eBindTarget::Image, MixIncremental::IN_TEMP_BASE_COLOR_SLOT, temp_base_color);
        bindings.emplace_back(eBindTarget::Image, MixIncremental::OUT_BASE_COLOR_IMG_SLOT, out_base_color);
    }
    if (out_depth_normals.ready()) {
        bindings.emplace_back(eBindTarget::Image, MixIncremental::IN_TEMP_DEPTH_NORMALS_SLOT, temp_depth_normals);
        bindings.emplace_back(eBindTarget::Image, MixIncremental::OUT_DEPTH_NORMALS_IMG_SLOT, out_depth_normals);
    }

    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const uint32_t grp_count[3] = {
        uint32_t((rect.w + MixIncremental::LOCAL_GROUP_SIZE_X - 1) / MixIncremental::LOCAL_GROUP_SIZE_X),
        uint32_t((rect.h + MixIncremental::LOCAL_GROUP_SIZE_Y - 1) / MixIncremental::LOCAL_GROUP_SIZE_Y), 1u};

    MixIncremental::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.main_mix_factor = main_mix_factor;
    uniform_params.aux_mix_factor = aux_mix_factor;

    Pipeline *pi = &pi_mix_incremental_;
    if (out_base_color.ready()) {
        if (out_depth_normals.ready()) {
            pi = &pi_mix_incremental_bn_;
        } else {
            pi = &pi_mix_incremental_b_;
        }
    } else if (out_depth_normals.ready()) {
        pi = &pi_mix_incremental_n_;
    }

    DispatchCompute(cmd_buf, *pi, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_Postprocess(VkCommandBuffer cmd_buf, const Texture2D &img0_buf, const float img0_weight,
                                           const Texture2D &img1_buf, const float img1_weight, const float exposure,
                                           const float inv_gamma, const bool srgb, const rect_t &rect,
                                           const Texture2D &out_pixels, const Texture2D &out_raw_pixels,
                                           const Texture2D &out_variance) const {
    const TransitionInfo res_transitions[] = {{&img0_buf, eResState::UnorderedAccess},
                                              {&img1_buf, eResState::UnorderedAccess},
                                              {&out_pixels, eResState::UnorderedAccess},
                                              {&out_raw_pixels, eResState::UnorderedAccess},
                                              {&out_variance, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::Image, Postprocess::IN_IMG0_SLOT, img0_buf},
                                {eBindTarget::Image, Postprocess::IN_IMG1_SLOT, img1_buf},
                                {eBindTarget::Image, Postprocess::OUT_IMG_SLOT, out_pixels},
                                {eBindTarget::Image, Postprocess::OUT_RAW_IMG_SLOT, out_raw_pixels},
                                {eBindTarget::Image, Postprocess::OUT_VARIANCE_IMG_SLOT, out_variance}};

    const uint32_t grp_count[3] = {
        uint32_t((rect.w + Postprocess::LOCAL_GROUP_SIZE_X - 1) / Postprocess::LOCAL_GROUP_SIZE_X),
        uint32_t((rect.h + Postprocess::LOCAL_GROUP_SIZE_Y - 1) / Postprocess::LOCAL_GROUP_SIZE_Y), 1u};

    Postprocess::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.srgb = srgb ? 1 : 0;
    uniform_params.exposure = exposure;
    uniform_params.inv_gamma = inv_gamma;
    uniform_params.img0_weight = img0_weight;
    uniform_params.img1_weight = img1_weight;

    DispatchCompute(cmd_buf, pi_postprocess_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_FilterVariance(VkCommandBuffer cmd_buf, const Texture2D &img_buf, const rect_t &rect,
                                              const Texture2D &out_variance) {
    const TransitionInfo res_transitions[] = {{&img_buf, eResState::ShaderResource},
                                              {&out_variance, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::Tex2D, FilterVariance::IN_IMG_SLOT, img_buf},
                                {eBindTarget::Image, FilterVariance::OUT_IMG_SLOT, out_variance}};

    const uint32_t grp_count[3] = {
        uint32_t((rect.w + FilterVariance::LOCAL_GROUP_SIZE_X - 1) / Postprocess::LOCAL_GROUP_SIZE_X),
        uint32_t((rect.h + FilterVariance::LOCAL_GROUP_SIZE_Y - 1) / Postprocess::LOCAL_GROUP_SIZE_Y), 1u};

    FilterVariance::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.inv_img_size[0] = 1.0f / float(w_);
    uniform_params.inv_img_size[1] = 1.0f / float(h_);

    DispatchCompute(cmd_buf, pi_filter_variance_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_NLMFilter(VkCommandBuffer cmd_buf, const Texture2D &img_buf, const Texture2D &var_buf,
                                         const float alpha, const float damping, const Texture2D &out_raw_img,
                                         const float inv_gamma, const bool srgb, const rect_t &rect,
                                         const Texture2D &out_img) {
    const TransitionInfo res_transitions[] = {{&img_buf, eResState::ShaderResource},
                                              {&var_buf, eResState::ShaderResource},
                                              {&out_img, eResState::UnorderedAccess},
                                              {&out_raw_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::Tex2D, NLMFilter::IN_IMG_SLOT, img_buf},
                                {eBindTarget::Tex2D, NLMFilter::VARIANCE_IMG_SLOT, var_buf},
                                {eBindTarget::Image, NLMFilter::OUT_IMG_SLOT, out_img},
                                {eBindTarget::Image, NLMFilter::OUT_RAW_IMG_SLOT, out_raw_img}};

    const uint32_t grp_count[3] = {
        uint32_t((rect.w + NLMFilter::LOCAL_GROUP_SIZE_X - 1) / NLMFilter::LOCAL_GROUP_SIZE_X),
        uint32_t((rect.h + NLMFilter::LOCAL_GROUP_SIZE_Y - 1) / NLMFilter::LOCAL_GROUP_SIZE_Y), 1u};

    NLMFilter::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.inv_img_size[0] = 1.0f / float(w_);
    uniform_params.inv_img_size[1] = 1.0f / float(h_);
    uniform_params.alpha = alpha;
    uniform_params.damping = damping;

    uniform_params.srgb = srgb ? 1 : 0;
    uniform_params.inv_gamma = inv_gamma;

    DispatchCompute(cmd_buf, pi_nlm_filter_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::Vk::Renderer::kernel_DebugRT(VkCommandBuffer cmd_buf, const scene_data_t &sc_data, uint32_t node_index,
                                       const Buffer &rays, const Texture2D &out_pixels) {
    const TransitionInfo res_transitions[] = {{&rays, eResState::UnorderedAccess},
                                              {&out_pixels, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBuf, DebugRT::TRIS_BUF_SLOT, sc_data.tris},
                                {eBindTarget::SBuf, DebugRT::TRI_INDICES_BUF_SLOT, sc_data.tri_indices},
                                {eBindTarget::SBuf, DebugRT::NODES_BUF_SLOT, sc_data.nodes},
                                {eBindTarget::SBuf, DebugRT::MESHES_BUF_SLOT, sc_data.meshes},
                                {eBindTarget::SBuf, DebugRT::MESH_INSTANCES_BUF_SLOT, sc_data.mesh_instances},
                                {eBindTarget::SBuf, DebugRT::MI_INDICES_BUF_SLOT, sc_data.mi_indices},
                                {eBindTarget::SBuf, DebugRT::TRANSFORMS_BUF_SLOT, sc_data.transforms},
                                {eBindTarget::AccStruct, DebugRT::TLAS_SLOT, sc_data.rt_tlas},
                                {eBindTarget::SBuf, DebugRT::RAYS_BUF_SLOT, rays},
                                {eBindTarget::Image, DebugRT::OUT_IMG_SLOT, out_pixels}};

    const uint32_t grp_count[3] = {uint32_t((w_ + DebugRT::LOCAL_GROUP_SIZE_X - 1) / DebugRT::LOCAL_GROUP_SIZE_X),
                                   uint32_t((h_ + DebugRT::LOCAL_GROUP_SIZE_Y - 1) / DebugRT::LOCAL_GROUP_SIZE_Y), 1u};

    DebugRT::Params uniform_params = {};
    uniform_params.img_size[0] = w_;
    uniform_params.img_size[1] = h_;
    uniform_params.node_index = node_index;

    DispatchCompute(cmd_buf, pi_debug_rt_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}