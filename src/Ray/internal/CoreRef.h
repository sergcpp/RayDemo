#pragma once

#include <vector>

#include "Core.h"

#pragma push_macro("NS")
#undef NS

#define NS Ref
#if defined(_M_AMD64) || defined(_M_X64) || (!defined(__ANDROID__) && defined(__x86_64__)) ||                          \
    (defined(_M_IX86_FP) && _M_IX86_FP == 2)
#define USE_SSE2
// #pragma message("Ray::Ref::simd_vec will use SSE2")
#elif defined(__ARM_NEON__) || defined(__aarch64__) || defined(_M_ARM) || defined(_M_ARM64)
#define USE_NEON
// #pragma message("Ray::Ref::simd_vec will use NEON")
#elif defined(__ANDROID__) && (defined(__i386__) || defined(__x86_64__))
#define USE_SSE2
// #pragma message("Ray::Ref::simd_vec will use SSE2")
#else
#pragma message("Ray::Ref::simd_vec will not use SIMD")
#endif

#include "simd/simd_vec.h"

#undef USE_SSE2
#undef USE_NEON
#undef NS

#pragma pop_macro("NS")

namespace Ray {
namespace Cpu {
class TexStorageBase;
template <typename T, int N> class TexStorageLinear;
template <typename T, int N> class TexStorageTiled;
template <typename T, int N> class TexStorageSwizzled;
using TexStorageRGBA = TexStorageSwizzled<uint8_t, 4>;
using TexStorageRGB = TexStorageSwizzled<uint8_t, 3>;
using TexStorageRG = TexStorageSwizzled<uint8_t, 2>;
using TexStorageR = TexStorageSwizzled<uint8_t, 1>;
} // namespace Cpu
namespace Ref {
struct ray_data_t {
    // origin and direction
    float o[3], d[3], pdf;
    // throughput color of ray
    float c[3];
    // stack of ior values
    float ior[4];
    // ray cone params
    float cone_width, cone_spread;
    // 16-bit pixel coordinates of ray ((x << 16) | y)
    int xy;
    // four 8-bit ray depth counters
    int depth;
};
static_assert(sizeof(ray_data_t) == 72, "!");

struct shadow_ray_t {
    // origin
    float o[3];
    // four 8-bit ray depth counters
    int depth;
    // direction and distance
    float d[3], dist;
    // throughput color of ray
    float c[3];
    // 16-bit pixel coordinates of ray ((x << 16) | y)
    int xy;
};
static_assert(sizeof(shadow_ray_t) == 48, "!");

struct hit_data_t {
    int mask;
    int obj_index;
    int prim_index;
    float t, u, v;

    explicit hit_data_t(eUninitialize) {}
    hit_data_t();
};

struct surface_t {
    simd_fvec4 P, T, B, N, plane_N;
    simd_fvec2 uvs;
};

struct derivatives_t {
    simd_fvec4 do_dx, do_dy, dd_dx, dd_dy;
    simd_fvec2 duv_dx, duv_dy;
    simd_fvec4 dndx, dndy;
    float ddn_dx, ddn_dy;
};

struct light_sample_t {
    simd_fvec4 col, L, lp;
    float area = 0, dist_mul = 1, pdf = 0;
    uint32_t cast_shadow : 1;
    uint32_t from_env : 1;
    uint32_t _pad0 : 30;
};
static_assert(sizeof(light_sample_t) == 64, "!");

force_inline int hash(int x) {
    unsigned ret = reinterpret_cast<const unsigned &>(x);
    ret = ((ret >> 16) ^ ret) * 0x45d9f3b;
    ret = ((ret >> 16) ^ ret) * 0x45d9f3b;
    ret = (ret >> 16) ^ ret;
    return reinterpret_cast<const int &>(ret);
}

force_inline simd_fvec4 rgbe_to_rgb(const color_t<uint8_t, 4> &rgbe) {
    const float f = std::exp2(float(rgbe.v[3]) - 128.0f);
    return simd_fvec4{to_norm_float(rgbe.v[0]) * f, to_norm_float(rgbe.v[1]) * f, to_norm_float(rgbe.v[2]) * f, 1.0f};
}

force_inline int total_depth(const ray_data_t &r) {
    const int diff_depth = r.depth & 0x000000ff;
    const int spec_depth = (r.depth >> 8) & 0x000000ff;
    const int refr_depth = (r.depth >> 16) & 0x000000ff;
    const int transp_depth = (r.depth >> 24) & 0x000000ff;
    return diff_depth + spec_depth + refr_depth + transp_depth;
}

force_inline int total_depth(const shadow_ray_t &r) {
    const int diff_depth = r.depth & 0x000000ff;
    const int spec_depth = (r.depth >> 8) & 0x000000ff;
    const int refr_depth = (r.depth >> 16) & 0x000000ff;
    const int transp_depth = (r.depth >> 24) & 0x000000ff;
    return diff_depth + spec_depth + refr_depth + transp_depth;
}

// Generation of rays
void GeneratePrimaryRays(const camera_t &cam, const rect_t &r, int w, int h, const float random_seq[], int iteration,
                         const uint16_t required_samples[], aligned_vector<ray_data_t> &out_rays);
void SampleMeshInTextureSpace(int iteration, int obj_index, int uv_layer, const mesh_t &mesh, const transform_t &tr,
                              const uint32_t *vtx_indices, const vertex_t *vertices, const rect_t &r, int w, int h,
                              const float *random_seq, aligned_vector<ray_data_t> &out_rays,
                              aligned_vector<hit_data_t> &out_inters);

// Sorting of rays
int SortRays_CPU(Span<ray_data_t> rays, const float root_min[3], const float cell_size[3], uint32_t *hash_values,
                 uint32_t *scan_values, ray_chunk_t *chunks, ray_chunk_t *chunks_temp);
int SortRays_GPU(Span<ray_data_t> rays, const float root_min[3], const float cell_size[3], uint32_t *hash_values,
                 int *head_flags, uint32_t *scan_values, ray_chunk_t *chunks, ray_chunk_t *chunks_temp,
                 uint32_t *skeleton);

// Intersect primitives
bool IntersectTris_ClosestHit(const float ro[3], const float rd[3], const tri_accel_t *tris, int tri_start, int tri_end,
                              int obj_index, hit_data_t &out_inter);
bool IntersectTris_ClosestHit(const float ro[3], const float rd[3], const mtri_accel_t *mtris, int tri_start,
                              int tri_end, int obj_index, hit_data_t &out_inter);
bool IntersectTris_AnyHit(const float ro[3], const float rd[3], const tri_accel_t *tris,
                          const tri_mat_data_t *materials, const uint32_t *indices, int tri_start, int tri_end,
                          int obj_index, hit_data_t &out_inter);
bool IntersectTris_AnyHit(const float ro[3], const float rd[3], const mtri_accel_t *mtris,
                          const tri_mat_data_t *materials, const uint32_t *indices, int tri_start, int tri_end,
                          int obj_index, hit_data_t &out_inter);

// traditional bvh traversal with stack for outer nodes
bool Traverse_MacroTree_WithStack_ClosestHit(const float ro[3], const float rd[3], const bvh_node_t *nodes,
                                             uint32_t root_index, const mesh_instance_t *mesh_instances,
                                             const uint32_t *mi_indices, const mesh_t *meshes,
                                             const transform_t *transforms, const tri_accel_t *tris,
                                             const uint32_t *tri_indices, hit_data_t &inter);
bool Traverse_MacroTree_WithStack_ClosestHit(const float ro[3], const float rd[3], const mbvh_node_t *oct_nodes,
                                             uint32_t root_index, const mesh_instance_t *mesh_instances,
                                             const uint32_t *mi_indices, const mesh_t *meshes,
                                             const transform_t *transforms, const mtri_accel_t *mtris,
                                             const uint32_t *tri_indices, hit_data_t &inter);
// returns whether hit was solid
bool Traverse_MacroTree_WithStack_AnyHit(const float ro[3], const float rd[3], const bvh_node_t *nodes,
                                         uint32_t root_index, const mesh_instance_t *mesh_instances,
                                         const uint32_t *mi_indices, const mesh_t *meshes,
                                         const transform_t *transforms, const mtri_accel_t *mtris,
                                         const tri_mat_data_t *materials, const uint32_t *tri_indices,
                                         hit_data_t &inter);
bool Traverse_MacroTree_WithStack_AnyHit(const float ro[3], const float rd[3], const mbvh_node_t *nodes,
                                         uint32_t root_index, const mesh_instance_t *mesh_instances,
                                         const uint32_t *mi_indices, const mesh_t *meshes,
                                         const transform_t *transforms, const tri_accel_t *tris,
                                         const tri_mat_data_t *materials, const uint32_t *tri_indices,
                                         hit_data_t &inter);
// traditional bvh traversal with stack for inner nodes
bool Traverse_MicroTree_WithStack_ClosestHit(const float ro[3], const float rd[3], const float inv_d[3],
                                             const bvh_node_t *nodes, uint32_t root_index, const tri_accel_t *tris,
                                             int obj_index, hit_data_t &inter);
bool Traverse_MicroTree_WithStack_ClosestHit(const float ro[3], const float rd[3], const float inv_d[3],
                                             const mbvh_node_t *nodes, uint32_t root_index, const mtri_accel_t *mtris,
                                             int obj_index, hit_data_t &inter);
// returns whether hit was solid
bool Traverse_MicroTree_WithStack_AnyHit(const float ro[3], const float rd[3], const float inv_d[3],
                                         const bvh_node_t *nodes, uint32_t root_index, const mtri_accel_t *mtris,
                                         const tri_mat_data_t *materials, const uint32_t *tri_indices, int obj_index,
                                         hit_data_t &inter);
bool Traverse_MicroTree_WithStack_AnyHit(const float ro[3], const float rd[3], const float inv_d[3],
                                         const mbvh_node_t *nodes, uint32_t root_index, const tri_accel_t *tris,
                                         const tri_mat_data_t *materials, const uint32_t *tri_indices, int obj_index,
                                         hit_data_t &inter);

// BRDFs
float BRDF_PrincipledDiffuse(const simd_fvec4 &V, const simd_fvec4 &N, const simd_fvec4 &L, const simd_fvec4 &H,
                             float roughness);

simd_fvec4 Evaluate_OrenDiffuse_BSDF(const simd_fvec4 &V, const simd_fvec4 &N, const simd_fvec4 &L, float roughness,
                                     const simd_fvec4 &base_color);
simd_fvec4 Sample_OrenDiffuse_BSDF(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N, const simd_fvec4 &I,
                                   float roughness, const simd_fvec4 &base_color, float rand_u, float rand_v,
                                   simd_fvec4 &out_V);

simd_fvec4 Evaluate_PrincipledDiffuse_BSDF(const simd_fvec4 &V, const simd_fvec4 &N, const simd_fvec4 &L,
                                           float roughness, const simd_fvec4 &base_color, const simd_fvec4 &sheen_color,
                                           bool uniform_sampling);
simd_fvec4 Sample_PrincipledDiffuse_BSDF(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                                         const simd_fvec4 &I, float roughness, const simd_fvec4 &base_color,
                                         const simd_fvec4 &sheen_color, bool uniform_sampling, float rand_u,
                                         float rand_v, simd_fvec4 &out_V);

simd_fvec4 Evaluate_GGXSpecular_BSDF(const simd_fvec4 &view_dir_ts, const simd_fvec4 &sampled_normal_ts,
                                     const simd_fvec4 &reflected_dir_ts, float alpha_x, float alpha_y, float spec_ior,
                                     float spec_F0, const simd_fvec4 &spec_col);
simd_fvec4 Sample_GGXSpecular_BSDF(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N, const simd_fvec4 &I,
                                   float roughness, float anisotropic, float spec_ior, float spec_F0,
                                   const simd_fvec4 &spec_col, float rand_u, float rand_v, simd_fvec4 &out_V);

simd_fvec4 Evaluate_GGXRefraction_BSDF(const simd_fvec4 &view_dir_ts, const simd_fvec4 &sampled_normal_ts,
                                       const simd_fvec4 &refr_dir_ts, float roughness2, float eta,
                                       const simd_fvec4 &refr_col);
simd_fvec4 Sample_GGXRefraction_BSDF(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N, const simd_fvec4 &I,
                                     float roughness, float eta, const simd_fvec4 &refr_col, float rand_u, float rand_v,
                                     simd_fvec4 &out_V);

simd_fvec4 Evaluate_PrincipledClearcoat_BSDF(const simd_fvec4 &view_dir_ts, const simd_fvec4 &sampled_normal_ts,
                                             const simd_fvec4 &reflected_dir_ts, float clearcoat_roughness2,
                                             float clearcoat_ior, float clearcoat_F0);
simd_fvec4 Sample_PrincipledClearcoat_BSDF(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                                           const simd_fvec4 &I, float clearcoat_roughness2, float clearcoat_ior,
                                           float clearcoat_F0, float rand_u, float rand_v, simd_fvec4 &out_V);

float Evaluate_EnvQTree(float y_rotation, const simd_fvec4 *const *qtree_mips, int qtree_levels, const simd_fvec4 &L);
simd_fvec4 Sample_EnvQTree(float y_rotation, const simd_fvec4 *const *qtree_mips, int qtree_levels, float rand,
                           float rx, float ry);

// Transform
void TransformRay(const float ro[3], const float rd[3], const float *xform, float out_ro[3], float out_rd[3]);
simd_fvec4 TransformPoint(const simd_fvec4 &p, const float *xform);
simd_fvec4 TransformDirection(const simd_fvec4 &p, const float *xform);
simd_fvec4 TransformNormal(const simd_fvec4 &n, const float *inv_xform);

// Sample Texture
simd_fvec4 SampleNearest(const Cpu::TexStorageBase *const textures[], uint32_t index, const simd_fvec2 &uvs, int lod);
simd_fvec4 SampleBilinear(const Cpu::TexStorageBase *const textures[], uint32_t index, const simd_fvec2 &uvs, int lod,
                          const simd_fvec2 &rand);
simd_fvec4 SampleBilinear(const Cpu::TexStorageBase &storage, uint32_t tex, const simd_fvec2 &iuvs, int lod,
                          const simd_fvec2 &rand);
simd_fvec4 SampleTrilinear(const Cpu::TexStorageBase *const textures[], uint32_t index, const simd_fvec2 &uvs,
                           float lod, const simd_fvec2 &rand);
simd_fvec4 SampleAnisotropic(const Cpu::TexStorageBase *const textures[], uint32_t index, const simd_fvec2 &uvs,
                             const simd_fvec2 &duv_dx, const simd_fvec2 &duv_dy);
simd_fvec4 SampleLatlong_RGBE(const Cpu::TexStorageRGBA &storage, uint32_t index, const simd_fvec4 &dir,
                              float y_rotation, const simd_fvec2 &rand);

// Trace rays through scene hierarchy
void IntersectScene(Span<ray_data_t> rays, int min_transp_depth, int max_transp_depth, const float *random_seq,
                    const scene_data_t &sc, uint32_t root_index, const Cpu::TexStorageBase *const textures[],
                    Span<hit_data_t> out_inter);
simd_fvec4 IntersectScene(const shadow_ray_t &r, int max_transp_depth, const scene_data_t &sc, uint32_t node_index,
                          const float *random_seq, const Cpu::TexStorageBase *const textures[]);

// Pick point on any light source for evaluation
void SampleLightSource(const simd_fvec4 &P, const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                       const scene_data_t &sc, const Cpu::TexStorageBase *const textures[], const float random_seq[],
                       const float sample_off[2], light_sample_t &ls);

// Account for visible lights contribution
void IntersectAreaLights(Span<const ray_data_t> rays, const light_t lights[], Span<const uint32_t> visible_lights,
                         const transform_t transforms[], Span<hit_data_t> inout_inters);
float IntersectAreaLights(const shadow_ray_t &ray, const light_t lights[], Span<const uint32_t> blocker_lights,
                          const transform_t transforms[]);

void TraceRays(Span<ray_data_t> rays, int min_transp_depth, int max_transp_depth, const scene_data_t &sc,
               uint32_t node_index, bool trace_lights, const Cpu::TexStorageBase *const textures[],
               const float random_seq[], Span<hit_data_t> out_inter);
void TraceShadowRays(Span<const shadow_ray_t> rays, int max_transp_depth, float _clamp_val, const scene_data_t &sc,
                     uint32_t node_index, const float random_seq[], const Cpu::TexStorageBase *const textures[],
                     int img_w, color_rgba_t *out_color);

// Get environment collor at direction
simd_fvec4 Evaluate_EnvColor(const ray_data_t &ray, const environment_t &env, const Cpu::TexStorageRGBA &tex_storage,
                             const simd_fvec2 &rand);
// Get light color at intersection point
simd_fvec4 Evaluate_LightColor(const ray_data_t &ray, const hit_data_t &inter, const environment_t &env,
                               const Cpu::TexStorageRGBA &tex_storage, const light_t *lights, const simd_fvec2 &rand);

// Evaluate individual nodes
simd_fvec4 Evaluate_DiffuseNode(const light_sample_t &ls, const ray_data_t &ray, const surface_t &surf,
                                const simd_fvec4 &base_color, float roughness, float mix_weight, shadow_ray_t &sh_r);
void Sample_DiffuseNode(const ray_data_t &ray, const surface_t &surf, const simd_fvec4 &base_color, float roughness,
                        float rand_u, float rand_v, float mix_weight, ray_data_t &new_ray);

simd_fvec4 Evaluate_GlossyNode(const light_sample_t &ls, const ray_data_t &ray, const surface_t &surf,
                               const simd_fvec4 &base_color, float roughness2, float spec_ior, float spec_F0,
                               float mix_weight, shadow_ray_t &sh_r);
void Sample_GlossyNode(const ray_data_t &ray, const surface_t &surf, const simd_fvec4 &base_color, float roughness,
                       float spec_ior, float spec_F0, float rand_u, float rand_v, float mix_weight,
                       ray_data_t &new_ray);

simd_fvec4 Evaluate_RefractiveNode(const light_sample_t &ls, const ray_data_t &ray, const surface_t &surf,
                                   const simd_fvec4 &base_color, float roughness2, float eta, float mix_weight,
                                   shadow_ray_t &sh_r);
void Sample_RefractiveNode(const ray_data_t &ray, const surface_t &surf, const simd_fvec4 &base_color, float roughness,
                           bool is_backfacing, float int_ior, float ext_ior, float rand_u, float rand_v,
                           float mix_weight, ray_data_t &new_ray);

struct diff_params_t {
    simd_fvec4 base_color;
    simd_fvec4 sheen_color;
    float roughness;
};

struct spec_params_t {
    simd_fvec4 tmp_col;
    float roughness;
    float ior;
    float F0;
    float anisotropy;
};

struct clearcoat_params_t {
    float roughness;
    float ior;
    float F0;
};

struct transmission_params_t {
    float roughness;
    float int_ior;
    float eta;
    float fresnel;
    bool backfacing;
};

struct lobe_weights_t {
    float diffuse, specular, clearcoat, refraction;
};

simd_fvec4 Evaluate_PrincipledNode(const light_sample_t &ls, const ray_data_t &ray, const surface_t &surf,
                                   const lobe_weights_t &lobe_weights, const diff_params_t &diff,
                                   const spec_params_t &spec, const clearcoat_params_t &coat,
                                   const transmission_params_t &trans, float metallic, float N_dot_L, float mix_weight,
                                   shadow_ray_t &sh_r);
void Sample_PrincipledNode(const pass_settings_t &ps, const ray_data_t &ray, const surface_t &surf,
                           const lobe_weights_t &lobe_weights, const diff_params_t &diff, const spec_params_t &spec,
                           const clearcoat_params_t &coat, const transmission_params_t &trans, float metallic,
                           float rand_u, float rand_v, float mix_rand, float mix_weight, ray_data_t &new_ray);

// Shade
color_rgba_t ShadeSurface(const pass_settings_t &ps, const hit_data_t &inter, const ray_data_t &ray,
                          const float *random_seq, const scene_data_t &sc, uint32_t node_index,
                          const Cpu::TexStorageBase *const textures[], ray_data_t *out_secondary_rays,
                          int *out_secondary_rays_count, shadow_ray_t *out_shadow_rays, int *out_shadow_rays_count,
                          color_rgba_t *out_base_color, color_rgba_t *out_depth_normal);
void ShadePrimary(const pass_settings_t &ps, Span<const hit_data_t> inters, Span<const ray_data_t> rays,
                  const float *random_seq, const scene_data_t &sc, uint32_t node_index,
                  const Cpu::TexStorageBase *const textures[], ray_data_t *out_secondary_rays,
                  int *out_secondary_rays_count, shadow_ray_t *out_shadow_rays, int *out_shadow_rays_count, int img_w,
                  float mix_factor, color_rgba_t *out_color, color_rgba_t *out_base_color,
                  color_rgba_t *out_depth_normal);
void ShadeSecondary(const pass_settings_t &ps, Span<const hit_data_t> inters, Span<const ray_data_t> rays,
                    const float *random_seq, const scene_data_t &sc, uint32_t node_index,
                    const Cpu::TexStorageBase *const textures[], ray_data_t *out_secondary_rays,
                    int *out_secondary_rays_count, shadow_ray_t *out_shadow_rays, int *out_shadow_rays_count, int img_w,
                    color_rgba_t *out_color);

// Denoise
template <int WINDOW_SIZE = 7, int NEIGHBORHOOD_SIZE = 3>
void JointNLMFilter(const color_rgba_t input[], const rect_t &rect, int input_stride, float alpha, float damping,
                    const color_rgba_t variance[], const color_rgba_t feature0[], float feature0_weight,
                    const color_rgba_t feature1[], float feature1_weight, const rect_t &output_rect, int output_stride,
                    color_rgba_t output[]);

// Tonemap

// https://gpuopen.com/learn/optimized-reversible-tonemapper-for-resolve/
force_inline simd_fvec4 vectorcall reversible_tonemap(const simd_fvec4 c) {
    return c / (std::max(c.get<0>(), std::max(c.get<1>(), c.get<2>())) + 1.0f);
}

force_inline simd_fvec4 vectorcall reversible_tonemap_invert(const simd_fvec4 c) {
    return c / (1.0f - std::max(c.get<0>(), std::max(c.get<1>(), c.get<2>())));
}

struct tonemap_params_t {
    eViewTransform view_transform;
    float inv_gamma;
};

force_inline simd_fvec4 vectorcall TonemapStandard(simd_fvec4 c) {
    UNROLLED_FOR(i, 3, {
        if (c.get<i>() < 0.0031308f) {
            c.set<i>(12.92f * c.get<i>());
        } else {
            c.set<i>(1.055f * std::pow(c.get<i>(), (1.0f / 2.4f)) - 0.055f);
        }
    })
    return c;
}

simd_fvec4 vectorcall TonemapFilmic(eViewTransform view_transform, simd_fvec4 color);

force_inline simd_fvec4 vectorcall Tonemap(const tonemap_params_t &params, simd_fvec4 c) {
    if (params.view_transform == eViewTransform::Standard) {
        c = TonemapStandard(c);
    } else {
        c = TonemapFilmic(params.view_transform, c);
    }

    if (params.inv_gamma != 1.0f) {
        c = pow(c, simd_fvec4{params.inv_gamma, params.inv_gamma, params.inv_gamma, 1.0f});
    }

    return clamp(c, 0.0f, 1.0f);
}

} // namespace Ref
} // namespace Ray
