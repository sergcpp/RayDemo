
#include "shaders/convolution_interface.h"
#include "shaders/filter_variance_interface.h"
#include "shaders/intersect_area_lights_interface.h"
#include "shaders/mix_incremental_interface.h"
#include "shaders/nlm_filter_interface.h"
#include "shaders/postprocess_interface.h"
#include "shaders/prepare_indir_args_interface.h"
#include "shaders/primary_ray_gen_interface.h"
#include "shaders/sort_add_partial_sums_interface.h"
#include "shaders/sort_hash_rays_interface.h"
#include "shaders/sort_init_count_table_interface.h"
#include "shaders/sort_reorder_rays_interface.h"
#include "shaders/sort_scan_interface.h"
#include "shaders/sort_write_sorted_hashes_interface.h"

void Ray::NS::Renderer::kernel_GeneratePrimaryRays(CommandBuffer cmd_buf, const camera_t &cam, const int hi,
                                                   const rect_t &rect, const Buffer &random_seq, const int iteration,
                                                   const Texture2D &req_samples_img, const Buffer &inout_counters,
                                                   const Buffer &out_rays) {
    const TransitionInfo res_transitions[] = {{&random_seq, eResState::ShaderResource},
                                              {&req_samples_img, eResState::ShaderResource},
                                              {&inout_counters, eResState::UnorderedAccess},
                                              {&out_rays, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBufRO, PrimaryRayGen::HALTON_SEQ_BUF_SLOT, random_seq},
                                {eBindTarget::Tex2D, PrimaryRayGen::REQUIRED_SAMPLES_IMG_SLOT, req_samples_img},
                                {eBindTarget::SBufRW, PrimaryRayGen::INOUT_COUNTERS_BUF_SLOT, inout_counters},
                                {eBindTarget::SBufRW, PrimaryRayGen::OUT_RAYS_BUF_SLOT, out_rays}};

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
    uniform_params.cam_fwd[3] = cam.clip_start;
    memcpy(&uniform_params.cam_side[0], cam.side, 3 * sizeof(float));
    uniform_params.cam_side[3] = cam.focus_distance;
    memcpy(&uniform_params.cam_up[0], cam.up, 3 * sizeof(float));
    uniform_params.cam_up[3] = cam.sensor_height;
    uniform_params.cam_fstop = cam.fstop;
    uniform_params.cam_focal_length = cam.focal_length;
    uniform_params.cam_lens_rotation = cam.lens_rotation;
    uniform_params.cam_lens_ratio = cam.lens_ratio;
    uniform_params.cam_filter_and_lens_blades = (int(cam.filter) << 8) | cam.lens_blades;
    uniform_params.shift_x = cam.shift[0];
    uniform_params.shift_y = cam.shift[1];
    uniform_params.iteration = iteration;

    const bool adaptive = (iteration > cam.pass_settings.min_samples) && cam.pass_settings.min_samples != -1;
    DispatchCompute(cmd_buf, adaptive ? pi_prim_rays_gen_adaptive_ : pi_prim_rays_gen_simple_, grp_count, bindings,
                    &uniform_params, sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_IntersectAreaLights(CommandBuffer cmd_buf, const scene_data_t &sc_data,
                                                   const Buffer &indir_args, const Buffer &counters, const Buffer &rays,
                                                   const Buffer &inout_hits) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&counters, eResState::ShaderResource},
                                              {&rays, eResState::ShaderResource},
                                              {&inout_hits, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {
        {eBindTarget::SBufRO, IntersectAreaLights::RAYS_BUF_SLOT, rays},
        {eBindTarget::SBufRO, IntersectAreaLights::LIGHTS_BUF_SLOT, sc_data.lights},
        {eBindTarget::SBufRO, IntersectAreaLights::VISIBLE_LIGHTS_BUF_SLOT, sc_data.visible_lights},
        {eBindTarget::SBufRO, IntersectAreaLights::TRANSFORMS_BUF_SLOT, sc_data.transforms},
        {eBindTarget::SBufRO, IntersectAreaLights::COUNTERS_BUF_SLOT, counters},
        {eBindTarget::SBufRW, IntersectAreaLights::INOUT_HITS_BUF_SLOT, inout_hits}};

    IntersectAreaLights::Params uniform_params = {};
    uniform_params.img_size[0] = w_;
    uniform_params.img_size[1] = h_;
    uniform_params.visible_lights_count = sc_data.visible_lights_count;

    DispatchComputeIndirect(cmd_buf, pi_intersect_area_lights_, indir_args, 0, bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_MixIncremental(CommandBuffer cmd_buf, const float main_mix_factor,
                                              const float aux_mix_factor, const rect_t &rect, const int iteration,
                                              const Texture2D &temp_img, const Texture2D &temp_base_color,
                                              const Texture2D &temp_depth_normals, const Texture2D &req_samples,
                                              const Texture2D &out_img, const Texture2D &out_base_color,
                                              const Texture2D &out_depth_normals) {
    const TransitionInfo res_transitions[] = {
        {&temp_img, eResState::UnorderedAccess},         {&temp_base_color, eResState::UnorderedAccess},
        {&req_samples, eResState::UnorderedAccess},      {&out_img, eResState::UnorderedAccess},
        {&out_base_color, eResState::UnorderedAccess},   {&temp_depth_normals, eResState::UnorderedAccess},
        {&out_depth_normals, eResState::UnorderedAccess}};
    SmallVector<Binding, 16> bindings = {{eBindTarget::Image, MixIncremental::IN_TEMP_IMG_SLOT, temp_img},
                                         {eBindTarget::Image, MixIncremental::IN_REQ_SAMPLES_SLOT, req_samples},
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
    uniform_params.iteration = iteration;

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

void Ray::NS::Renderer::kernel_Postprocess(CommandBuffer cmd_buf, const Texture2D &img0_buf, const float img0_weight,
                                           const Texture2D &img1_buf, const float img1_weight, const float exposure,
                                           const float inv_gamma, const rect_t &rect, const float variance_threshold,
                                           const int iteration, const Texture2D &out_pixels,
                                           const Texture2D &out_raw_pixels, const Texture2D &out_variance,
                                           const Texture2D &out_req_samples) const {
    const TransitionInfo res_transitions[] = {
        {&img0_buf, eResState::UnorderedAccess},       {&img1_buf, eResState::UnorderedAccess},
        {&tonemap_lut_, eResState::ShaderResource},    {&out_pixels, eResState::UnorderedAccess},
        {&out_raw_pixels, eResState::UnorderedAccess}, {&out_variance, eResState::UnorderedAccess},
        {&out_req_samples, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::Image, Postprocess::IN_IMG0_SLOT, img0_buf},
                                {eBindTarget::Image, Postprocess::IN_IMG1_SLOT, img1_buf},
                                {eBindTarget::Tex3D, Postprocess::TONEMAP_LUT_SLOT, tonemap_lut_},
                                {eBindTarget::Image, Postprocess::OUT_IMG_SLOT, out_pixels},
                                {eBindTarget::Image, Postprocess::OUT_RAW_IMG_SLOT, out_raw_pixels},
                                {eBindTarget::Image, Postprocess::OUT_VARIANCE_IMG_SLOT, out_variance},
                                {eBindTarget::Image, Postprocess::OUT_REQ_SAMPLES_IMG_SLOT, out_req_samples}};

    const uint32_t grp_count[3] = {
        uint32_t((rect.w + Postprocess::LOCAL_GROUP_SIZE_X - 1) / Postprocess::LOCAL_GROUP_SIZE_X),
        uint32_t((rect.h + Postprocess::LOCAL_GROUP_SIZE_Y - 1) / Postprocess::LOCAL_GROUP_SIZE_Y), 1u};

    Postprocess::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.exposure = exposure;
    uniform_params.inv_gamma = inv_gamma;
    uniform_params.img0_weight = img0_weight;
    uniform_params.img1_weight = img1_weight;
    uniform_params.tonemap_mode = (loaded_view_transform_ == eViewTransform::Standard) ? 0 : 1;
    uniform_params.variance_threshold = variance_threshold;
    uniform_params.iteration = iteration;

    DispatchCompute(cmd_buf, pi_postprocess_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_FilterVariance(CommandBuffer cmd_buf, const Texture2D &img_buf, const rect_t &rect,
                                              const float variance_threshold, const int iteration,
                                              const Texture2D &out_variance, const Texture2D &out_req_samples) {
    const TransitionInfo res_transitions[] = {{&img_buf, eResState::ShaderResource},
                                              {&out_variance, eResState::UnorderedAccess},
                                              {&out_req_samples, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::Tex2DSampled, FilterVariance::IN_IMG_SLOT, img_buf},
                                {eBindTarget::Image, FilterVariance::OUT_IMG_SLOT, out_variance},
                                {eBindTarget::Image, FilterVariance::OUT_REQ_SAMPLES_IMG_SLOT, out_req_samples}};

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
    uniform_params.variance_threshold = variance_threshold;
    uniform_params.iteration = iteration;

    DispatchCompute(cmd_buf, pi_filter_variance_, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_NLMFilter(CommandBuffer cmd_buf, const Texture2D &img_buf, const Texture2D &var_buf,
                                         const float alpha, const float damping, const Texture2D &base_color_img,
                                         const float base_color_weight, const Texture2D &depth_normals_img,
                                         const float depth_normals_weight, const Texture2D &out_raw_img,
                                         const eViewTransform view_transform, const float inv_gamma, const rect_t &rect,
                                         const Texture2D &out_img) {
    const TransitionInfo res_transitions[] = {
        {&img_buf, eResState::ShaderResource},           {&var_buf, eResState::ShaderResource},
        {&tonemap_lut_, eResState::ShaderResource},      {&base_color_img, eResState::ShaderResource},
        {&depth_normals_img, eResState::ShaderResource}, {&out_img, eResState::UnorderedAccess},
        {&out_raw_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    SmallVector<Binding, 16> bindings = {{eBindTarget::Tex2DSampled, NLMFilter::IN_IMG_SLOT, img_buf},
                                         {eBindTarget::Tex2DSampled, NLMFilter::VARIANCE_IMG_SLOT, var_buf},
                                         {eBindTarget::Tex3D, NLMFilter::TONEMAP_LUT_SLOT, tonemap_lut_},
                                         {eBindTarget::Image, NLMFilter::OUT_IMG_SLOT, out_img},
                                         {eBindTarget::Image, NLMFilter::OUT_RAW_IMG_SLOT, out_raw_img}};

    if (base_color_img.ready()) {
        bindings.emplace_back(eBindTarget::Tex2DSampled, NLMFilter::BASE_COLOR_IMG_SLOT, base_color_img);
    }
    if (depth_normals_img.ready()) {
        bindings.emplace_back(eBindTarget::Tex2DSampled, NLMFilter::DEPTH_NORMAL_IMG_SLOT, depth_normals_img);
    }

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
    uniform_params.inv_gamma = inv_gamma;
    uniform_params.tonemap_mode = (loaded_view_transform_ == eViewTransform::Standard) ? 0 : 1;
    uniform_params.base_color_weight = base_color_weight;
    uniform_params.depth_normal_weight = depth_normals_weight;

    Pipeline *pi = &pi_nlm_filter_;
    if (base_color_img.ready() && depth_normals_img.ready()) {
        pi = &pi_nlm_filter_bn_;
    } else if (base_color_img.ready()) {
        pi = &pi_nlm_filter_b_;
    } else if (depth_normals_img.ready()) {
        pi = &pi_nlm_filter_n_;
    }

    DispatchCompute(cmd_buf, *pi, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_Convolution(CommandBuffer cmd_buf, int in_channels, int out_channels,
                                           const Texture2D &img_buf1, const Texture2D &img_buf2,
                                           const Texture2D &img_buf3, const Sampler &sampler, const rect_t &rect, int w,
                                           int h, const Buffer &weights, uint32_t weights_offset,
                                           uint32_t biases_offset, const Buffer &out_buf, uint32_t output_offset,
                                           int output_stride, const Texture2D &out_debug_img) {
    const TransitionInfo res_transitions[] = {{&img_buf1, eResState::ShaderResource},
                                              {&img_buf2, eResState::ShaderResource},
                                              {&img_buf3, eResState::ShaderResource},
                                              {&out_buf, eResState::UnorderedAccess},
                                              {&out_debug_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const int el_sz = use_fp16_ ? sizeof(uint16_t) : sizeof(float);
    const uint32_t weights_size = out_channels * 3 * round_up(3 * in_channels, 8) * el_sz;
    const uint32_t biases_size = out_channels * el_sz;
    const uint32_t output_size = output_stride * (h + 2) * out_channels * el_sz;

    SmallVector<Binding, 8> bindings = {
        {eBindTarget::Tex2D, Convolution::IN_IMG1_SLOT, img_buf1},
        {eBindTarget::Sampler, Convolution::IN_SAMPLER_SLOT, sampler},
        {eBindTarget::SBufRO, Convolution::WEIGHTS_BUF_SLOT, weights_offset, weights_size, weights},
        {eBindTarget::SBufRO, Convolution::BIASES_BUF_SLOT, biases_offset, biases_size, weights},
        {eBindTarget::SBufRW, Convolution::OUT_BUF_SLOT, output_offset, output_size, out_buf}};

    if (img_buf2.ready()) {
        bindings.emplace_back(eBindTarget::Tex2D, Convolution::IN_IMG2_SLOT, img_buf2);
    }
    if (img_buf3.ready()) {
        bindings.emplace_back(eBindTarget::Tex2D, Convolution::IN_IMG3_SLOT, img_buf3);
    }
    if (out_debug_img.ready()) {
        bindings.emplace_back(eBindTarget::Image, Convolution::OUT_DEBUG_IMG_SLOT, out_debug_img);
    }

    const uint32_t grp_count[3] = {uint32_t((rect.w + Convolution::TILE_M - 1) / Convolution::TILE_M),
                                   uint32_t((rect.h + 1) / 2),
                                   uint32_t((out_channels + Convolution::TILE_N - 1) / Convolution::TILE_N)};

    Convolution::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.inv_img_size[0] = 1.0f / float(w_);
    uniform_params.inv_img_size[1] = 1.0f / float(h_);
    uniform_params.output_stride = output_stride;
    uniform_params.in_dims[0] = w;
    uniform_params.in_dims[1] = h;
    uniform_params.out_dims[0] = w;
    uniform_params.out_dims[1] = h;

    Pipeline *pi = nullptr;
    if (in_channels == 3 && out_channels == 32) {
        pi = &pi_convolution_DirectImg_3_32_;
    } else if (in_channels == 6 && out_channels == 32) {
        assert(img_buf2.ready());
        pi = &pi_convolution_DirectImg_6_32_;
    } else if (in_channels == 9 && out_channels == 32) {
        assert(img_buf2.ready() && img_buf3.ready());
        pi = &pi_convolution_DirectImg_9_32_;
    }

    DispatchCompute(cmd_buf, *pi, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_Convolution(CommandBuffer cmd_buf, int in_channels, int out_channels,
                                           const Buffer &input_buf, uint32_t input_offset, int input_stride,
                                           const rect_t &rect, int w, int h, const Buffer &weights,
                                           uint32_t weights_offset, uint32_t biases_offset, const Buffer &out_buf,
                                           uint32_t output_offset, int output_stride, const bool downsample,
                                           const Texture2D &out_debug_img) {
    const TransitionInfo res_transitions[] = {{&out_buf, eResState::UnorderedAccess},
                                              {&out_debug_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const int el_sz = use_fp16_ ? sizeof(uint16_t) : sizeof(float);
    const uint32_t weights_size = out_channels * 3 * round_up(3 * in_channels, 8) * el_sz;
    const uint32_t biases_size = out_channels * el_sz;
    const uint32_t output_size = output_stride * (h + 2) * out_channels * el_sz;

    SmallVector<Binding, 16> bindings = {
        {eBindTarget::SBufRO, Convolution::IN_BUF1_SLOT, input_offset, input_buf},
        {eBindTarget::SBufRO, Convolution::WEIGHTS_BUF_SLOT, weights_offset, weights_size, weights},
        {eBindTarget::SBufRO, Convolution::BIASES_BUF_SLOT, biases_offset, biases_size, weights},
        {eBindTarget::SBufRW, Convolution::OUT_BUF_SLOT, output_offset, output_size, out_buf}};

    if (out_debug_img.ready()) {
        bindings.emplace_back(eBindTarget::Image, Convolution::OUT_DEBUG_IMG_SLOT, out_debug_img);
    }

    const uint32_t grp_count[3] = {uint32_t((rect.w + Convolution::TILE_M - 1) / Convolution::TILE_M),
                                   uint32_t((rect.h + 1) / 2),
                                   uint32_t((out_channels + Convolution::TILE_N - 1) / Convolution::TILE_N)};

    Convolution::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.input_stride1 = input_stride;
    uniform_params.output_stride = output_stride;
    uniform_params.in_dims[0] = w;
    uniform_params.in_dims[1] = h;
    if (downsample) {
        uniform_params.out_dims[0] = w / 2;
        uniform_params.out_dims[1] = h / 2;
    } else {
        uniform_params.out_dims[0] = w;
        uniform_params.out_dims[1] = h;
    }

    Pipeline *pi = nullptr;
    if (in_channels == 32 && out_channels == 32 && downsample) {
        pi = &pi_convolution_Direct_32_32_Downsample_;
    } else if (in_channels == 32 && out_channels == 48 && downsample) {
        pi = &pi_convolution_Direct_32_48_Downsample_;
    } else if (in_channels == 48 && out_channels == 64 && downsample) {
        pi = &pi_convolution_Direct_48_64_Downsample_;
    } else if (in_channels == 64 && out_channels == 80 && downsample) {
        pi = &pi_convolution_Direct_64_80_Downsample_;
    } else if (in_channels == 64 && out_channels == 64) {
        pi = &pi_convolution_Direct_64_64_;
    } else if (in_channels == 64 && out_channels == 32) {
        pi = &pi_convolution_Direct_64_32_;
    } else if (in_channels == 80 && out_channels == 96) {
        pi = &pi_convolution_Direct_80_96_;
    } else if (in_channels == 96 && out_channels == 96) {
        pi = &pi_convolution_Direct_96_96_;
    } else if (in_channels == 112 && out_channels == 112) {
        pi = &pi_convolution_Direct_112_112_;
    }

    DispatchCompute(cmd_buf, *pi, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_Convolution(CommandBuffer cmd_buf, int in_channels, int out_channels,
                                           const Buffer &input_buf, uint32_t input_offset, int input_stride,
                                           const float inv_gamma, const rect_t &rect, int w, int h,
                                           const Buffer &weights, uint32_t weights_offset, uint32_t biases_offset,
                                           const Texture2D &out_img, const Texture2D &out_tonemapped_img) {
    const TransitionInfo res_transitions[] = {{&input_buf, eResState::ShaderResource},
                                              {&out_img, eResState::UnorderedAccess},
                                              {&out_tonemapped_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const int el_sz = use_fp16_ ? sizeof(uint16_t) : sizeof(float);
    const uint32_t weights_size = out_channels * 3 * round_up(3 * in_channels, 8) * el_sz;
    const uint32_t biases_size = out_channels * el_sz;

    const Binding bindings[] = {
        {eBindTarget::SBufRO, Convolution::IN_BUF1_SLOT, input_offset, input_buf},
        {eBindTarget::SBufRO, Convolution::WEIGHTS_BUF_SLOT, weights_offset, weights_size, weights},
        {eBindTarget::SBufRO, Convolution::BIASES_BUF_SLOT, biases_offset, biases_size, weights},
        {eBindTarget::Tex3D, Convolution::TONEMAP_LUT_SLOT, tonemap_lut_},
        {eBindTarget::Image, Convolution::OUT_IMG_SLOT, out_img},
        {eBindTarget::Image, Convolution::OUT_TONEMAPPED_IMG_SLOT, out_tonemapped_img}};

    const uint32_t grp_count[3] = {uint32_t((rect.w + Convolution::TILE_M - 1) / Convolution::TILE_M),
                                   uint32_t((rect.h + 1) / 2),
                                   uint32_t((out_channels + Convolution::TILE_N - 1) / Convolution::TILE_N)};

    Convolution::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.input_stride1 = input_stride;
    uniform_params.tonemap_mode = (loaded_view_transform_ == eViewTransform::Standard) ? 0 : 1;
    uniform_params.inv_gamma = inv_gamma;
    uniform_params.in_dims[0] = w;
    uniform_params.in_dims[1] = h;
    uniform_params.out_dims[0] = w;
    uniform_params.out_dims[1] = h;

    Pipeline *pi = nullptr;
    if (in_channels == 32 && out_channels == 3) {
        pi = &pi_convolution_Direct_32_3_img_;
    }

    DispatchCompute(cmd_buf, *pi, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_ConvolutionConcat(CommandBuffer cmd_buf, int in_channels1, int in_channels2,
                                                 int out_channels, const Buffer &input_buf1, uint32_t input_offset1,
                                                 int input_stride1, bool upscale1, const Buffer &input_buf2,
                                                 uint32_t input_offset2, int input_stride2, const rect_t &rect, int w,
                                                 int h, const Buffer &weights, uint32_t weights_offset,
                                                 uint32_t biases_offset, const Buffer &out_buf, uint32_t output_offset,
                                                 int output_stride, const Texture2D &out_debug_img) {
    const TransitionInfo res_transitions[] = {{&out_buf, eResState::UnorderedAccess},
                                              {&out_debug_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const int el_sz = use_fp16_ ? sizeof(uint16_t) : sizeof(float);
    const uint32_t weights_size = out_channels * 3 * (3 * in_channels1 + round_up(3 * in_channels2, 8)) * el_sz;
    const uint32_t biases_size = out_channels * el_sz;
    const uint32_t output_size = output_stride * (h + 2) * out_channels * el_sz;

    SmallVector<Binding, 16> bindings = {
        {eBindTarget::SBufRO, Convolution::IN_BUF1_SLOT, input_offset1, input_buf1},
        {eBindTarget::SBufRO, Convolution::IN_BUF2_SLOT, input_offset2, input_buf2},
        {eBindTarget::SBufRO, Convolution::WEIGHTS_BUF_SLOT, weights_offset, weights_size, weights},
        {eBindTarget::SBufRO, Convolution::BIASES_BUF_SLOT, biases_offset, biases_size, weights},
        {eBindTarget::SBufRW, Convolution::OUT_BUF_SLOT, output_offset, output_size, out_buf}};

    if (out_debug_img.ready()) {
        bindings.emplace_back(eBindTarget::Image, Convolution::OUT_DEBUG_IMG_SLOT, out_debug_img);
    }

    const uint32_t grp_count[3] = {uint32_t((rect.w + Convolution::TILE_M - 1) / Convolution::TILE_M),
                                   uint32_t((rect.h + 1) / 2),
                                   uint32_t((out_channels + Convolution::TILE_N - 1) / Convolution::TILE_N)};

    Convolution::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.input_stride1 = input_stride1;
    uniform_params.input_stride2 = input_stride2;
    uniform_params.output_stride = output_stride;
    uniform_params.in_dims[0] = w;
    uniform_params.in_dims[1] = h;
    uniform_params.out_dims[0] = w;
    uniform_params.out_dims[1] = h;

    Pipeline *pi = nullptr;
    if (in_channels1 == 96 && in_channels2 == 64 && out_channels == 112 && upscale1) {
        pi = &pi_convolution_concat_Direct_96_64_112_;
    } else if (in_channels1 == 112 && in_channels2 == 48 && out_channels == 96 && upscale1) {
        pi = &pi_convolution_concat_Direct_112_48_96_;
    } else if (in_channels1 == 96 && in_channels2 == 32 && out_channels == 64 && upscale1) {
        pi = &pi_convolution_concat_Direct_96_32_64_;
    }

    DispatchCompute(cmd_buf, *pi, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_ConvolutionConcat(CommandBuffer cmd_buf, int in_channels1, int in_channels2,
                                                 int out_channels, const Buffer &input_buf1, uint32_t input_offset1,
                                                 int input_stride1, bool upscale1, const Texture2D &img_buf1,
                                                 const Texture2D &img_buf2, const Texture2D &img_buf3,
                                                 const Sampler &sampler, const rect_t &rect, int w, int h,
                                                 const Buffer &weights, uint32_t weights_offset, uint32_t biases_offset,
                                                 const Buffer &out_buf, uint32_t output_offset, int output_stride,
                                                 const Texture2D &out_debug_img) {
    const TransitionInfo res_transitions[] = {{&img_buf1, eResState::ShaderResource},
                                              {&img_buf2, eResState::ShaderResource},
                                              {&out_buf, eResState::UnorderedAccess},
                                              {&out_debug_img, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const int el_sz = use_fp16_ ? sizeof(uint16_t) : sizeof(float);
    const uint32_t weights_size = out_channels * 3 * (3 * in_channels1 + round_up(3 * in_channels2, 8)) * el_sz;
    const uint32_t biases_size = out_channels * el_sz;
    const uint32_t output_size = output_stride * (h + 2) * out_channels * el_sz;

    SmallVector<Binding, 8> bindings = {
        {eBindTarget::SBufRO, Convolution::IN_BUF1_SLOT, input_offset1, input_buf1},
        {eBindTarget::Tex2D, Convolution::IN_IMG2_SLOT, img_buf1},
        {eBindTarget::Sampler, Convolution::IN_SAMPLER_SLOT, sampler},
        {eBindTarget::SBufRO, Convolution::WEIGHTS_BUF_SLOT, weights_offset, weights_size, weights},
        {eBindTarget::SBufRO, Convolution::BIASES_BUF_SLOT, biases_offset, biases_size, weights},
        {eBindTarget::SBufRW, Convolution::OUT_BUF_SLOT, output_offset, output_size, out_buf}};

    if (img_buf2.ready()) {
        bindings.emplace_back(eBindTarget::Tex2D, Convolution::IN_IMG3_SLOT, img_buf2);
    }
    if (img_buf3.ready()) {
        bindings.emplace_back(eBindTarget::Tex2D, Convolution::IN_IMG4_SLOT, img_buf3);
    }
    if (out_debug_img.ready()) {
        bindings.emplace_back(eBindTarget::Image, Convolution::OUT_DEBUG_IMG_SLOT, out_debug_img);
    }

    const uint32_t grp_count[3] = {uint32_t((rect.w + Convolution::TILE_M - 1) / Convolution::TILE_M),
                                   uint32_t((rect.h + 1) / 2),
                                   uint32_t((out_channels + Convolution::TILE_N - 1) / Convolution::TILE_N)};

    Convolution::Params uniform_params = {};
    uniform_params.rect[0] = rect.x;
    uniform_params.rect[1] = rect.y;
    uniform_params.rect[2] = rect.w;
    uniform_params.rect[3] = rect.h;
    uniform_params.inv_img_size[0] = 1.0f / float(w_);
    uniform_params.inv_img_size[1] = 1.0f / float(h_);
    uniform_params.input_stride1 = input_stride1;
    uniform_params.output_stride = output_stride;
    uniform_params.in_dims[0] = w;
    uniform_params.in_dims[1] = h;
    uniform_params.out_dims[0] = w;
    uniform_params.out_dims[1] = h;

    Pipeline *pi = nullptr;
    if (in_channels1 == 64 && in_channels2 == 3 && out_channels == 64 && upscale1) {
        pi = &pi_convolution_concat_Direct_64_3_64_;
    } else if (in_channels1 == 64 && in_channels2 == 6 && out_channels == 64 && upscale1) {
        assert(img_buf2.ready());
        pi = &pi_convolution_concat_Direct_64_6_64_;
    } else if (in_channels1 == 64 && in_channels2 == 9 && out_channels == 64 && upscale1) {
        assert(img_buf2.ready() && img_buf3.ready());
        pi = &pi_convolution_concat_Direct_64_9_64_;
    }

    DispatchCompute(cmd_buf, *pi, grp_count, bindings, &uniform_params, sizeof(uniform_params),
                    ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_PrepareIndirArgs(CommandBuffer cmd_buf, const Buffer &inout_counters,
                                                const Buffer &out_indir_args) {
    const TransitionInfo res_transitions[] = {{&inout_counters, eResState::UnorderedAccess},
                                              {&out_indir_args, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBufRW, PrepareIndirArgs::INOUT_COUNTERS_BUF_SLOT, inout_counters},
                                {eBindTarget::SBufRW, PrepareIndirArgs::OUT_INDIR_ARGS_SLOT, out_indir_args}};

    const uint32_t grp_count[3] = {1u, 1u, 1u};
    DispatchCompute(cmd_buf, pi_prepare_indir_args_, grp_count, bindings, nullptr, 0, ctx_->default_descr_alloc(),
                    ctx_->log());
}

void Ray::NS::Renderer::kernel_SortHashRays(CommandBuffer cmd_buf, const Buffer &indir_args, const Buffer &rays,
                                            const Buffer &counters, const float root_min[3], const float cell_size[3],
                                            const Buffer &out_hashes) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&rays, eResState::ShaderResource},
                                              {&counters, eResState::ShaderResource},
                                              {&out_hashes, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBufRO, SortHashRays::RAYS_BUF_SLOT, rays},
                                {eBindTarget::SBufRO, SortHashRays::COUNTERS_BUF_SLOT, counters},
                                {eBindTarget::SBufRW, SortHashRays::OUT_HASHES_BUF_SLOT, out_hashes}};

    SortHashRays::Params uniform_params = {};
    memcpy(&uniform_params.root_min[0], root_min, 3 * sizeof(float));
    memcpy(&uniform_params.cell_size[0], cell_size, 3 * sizeof(float));

    DispatchComputeIndirect(cmd_buf, pi_sort_hash_rays_, indir_args, 0, bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_SortScan(CommandBuffer cmd_buf, const bool exclusive, const Buffer &indir_args,
                                        const int indir_args_index, const Buffer &input, const int input_offset,
                                        const int input_stride, const Buffer &out_scan_values,
                                        const Buffer &out_partial_sums) {
    static_assert(SortScan::SCAN_PORTION == SORT_SCAN_PORTION, "!");

    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&input, eResState::ShaderResource},
                                              {&out_scan_values, eResState::UnorderedAccess},
                                              {&out_partial_sums, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBufRO, SortScan::INPUT_BUF_SLOT, input},
                                {eBindTarget::SBufRW, SortScan::OUT_SCAN_VALUES_BUF_SLOT, out_scan_values},
                                {eBindTarget::SBufRW, SortScan::OUT_PARTIAL_SUMS_BUF_SLOT, out_partial_sums}};

    SortScan::Params uniform_params = {};
    uniform_params.offset = input_offset;
    uniform_params.stride = input_stride;

    DispatchComputeIndirect(cmd_buf, exclusive ? pi_sort_exclusive_scan_ : pi_sort_inclusive_scan_, indir_args,
                            indir_args_index * sizeof(DispatchIndirectCommand), bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_SortAddPartialSums(CommandBuffer cmd_buf, const Buffer &indir_args,
                                                  const int indir_args_index, const Buffer &partials_sums,
                                                  const Buffer &inout_values) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&partials_sums, eResState::ShaderResource},
                                              {&inout_values, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBufRO, SortAddPartialSums::PART_SUMS_BUF_SLOT, partials_sums},
                                {eBindTarget::SBufRW, SortAddPartialSums::INOUT_BUF_SLOT, inout_values}};

    DispatchComputeIndirect(cmd_buf, pi_sort_add_partial_sums_, indir_args,
                            indir_args_index * sizeof(DispatchIndirectCommand), bindings, nullptr, 0,
                            ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_SortInitCountTable(CommandBuffer cmd_buf, const int shift, const Buffer &indir_args,
                                                  const int indir_args_index, const Buffer &hashes,
                                                  const Buffer &counters, const int counter_index,
                                                  const Buffer &out_count_table) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&hashes, eResState::ShaderResource},
                                              {&counters, eResState::ShaderResource},
                                              {&out_count_table, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBufRO, SortInitCountTable::HASHES_BUF_SLOT, hashes},
                                {eBindTarget::SBufRO, SortInitCountTable::COUNTERS_BUF_SLOT, counters},
                                {eBindTarget::SBufRW, SortInitCountTable::OUT_COUNT_TABLE_BUF_SLOT, out_count_table}};

    SortInitCountTable::Params uniform_params = {};
    uniform_params.counter = counter_index;
    uniform_params.shift = shift;

    DispatchComputeIndirect(cmd_buf, pi_sort_init_count_table_, indir_args,
                            indir_args_index * sizeof(DispatchIndirectCommand), bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_SortWriteSortedHashes(CommandBuffer cmd_buf, const int shift, const Buffer &indir_args,
                                                     const int indir_args_index, const Buffer &hashes,
                                                     const Buffer &offsets, const Buffer &counters, int counter_index,
                                                     int chunks_counter_index, const Buffer &out_chunks) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&hashes, eResState::ShaderResource},
                                              {&offsets, eResState::ShaderResource},
                                              {&counters, eResState::ShaderResource},
                                              {&out_chunks, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBufRO, SortWriteSortedChunks::HASHES_BUF_SLOT, hashes},
                                {eBindTarget::SBufRO, SortWriteSortedChunks::OFFSETS_BUF_SLOT, offsets},
                                {eBindTarget::SBufRO, SortWriteSortedChunks::COUNTERS_BUF_SLOT, counters},
                                {eBindTarget::SBufRW, SortWriteSortedChunks::OUT_HASHES_BUF_SLOT, out_chunks}};

    SortWriteSortedChunks::Params uniform_params = {};
    uniform_params.counter = counter_index;
    uniform_params.chunks_counter = chunks_counter_index;
    uniform_params.shift = shift;

    DispatchComputeIndirect(cmd_buf, pi_sort_write_sorted_hashes_, indir_args,
                            indir_args_index * sizeof(DispatchIndirectCommand), bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}

void Ray::NS::Renderer::kernel_SortReorderRays(CommandBuffer cmd_buf, const Buffer &indir_args,
                                               const int indir_args_index, const Buffer &in_rays, const Buffer &indices,
                                               const Buffer &counters, const int counter_index,
                                               const Buffer &out_rays) {
    const TransitionInfo res_transitions[] = {{&indir_args, eResState::IndirectArgument},
                                              {&in_rays, eResState::ShaderResource},
                                              {&indices, eResState::ShaderResource},
                                              {&counters, eResState::ShaderResource},
                                              {&out_rays, eResState::UnorderedAccess}};
    TransitionResourceStates(cmd_buf, AllStages, AllStages, res_transitions);

    const Binding bindings[] = {{eBindTarget::SBufRO, SortReorderRays::RAYS_BUF_SLOT, in_rays},
                                {eBindTarget::SBufRO, SortReorderRays::INDICES_BUF_SLOT, indices},
                                {eBindTarget::SBufRO, SortReorderRays::COUNTERS_BUF_SLOT, counters},
                                {eBindTarget::SBufRW, SortReorderRays::OUT_RAYS_BUF_SLOT, out_rays}};

    SortReorderRays::Params uniform_params = {};
    uniform_params.counter = counter_index;

    DispatchComputeIndirect(cmd_buf, pi_sort_reorder_rays_, indir_args,
                            indir_args_index * sizeof(DispatchIndirectCommand), bindings, &uniform_params,
                            sizeof(uniform_params), ctx_->default_descr_alloc(), ctx_->log());
}