#include "CoreRef.h"

#include <cassert>
#include <algorithm>
#include <limits>

#include "TextureAtlasRef.h"

namespace Ray {
namespace Ref {
force_inline void _IntersectTri(const ray_packet_t &r, const tri_accel_t &tri, uint32_t i, hit_data_t &inter) {
    const int _next_u[] = { 1, 0, 0 },
              _next_v[] = { 2, 2, 1 };

    int iw = tri.ci & Ray::TRI_W_BITS,
        iu = _next_u[iw], iv = _next_v[iw];

    float det = r.d[iu] * tri.nu + r.d[iv] * tri.nv + r.d[iw];
    float dett = tri.np - (r.o[iu] * tri.nu + r.o[iv] * tri.nv + r.o[iw]);
    float Du = r.d[iu] * dett - (tri.pu - r.o[iu]) * det;
    float Dv = r.d[iv] * dett - (tri.pv - r.o[iv]) * det;
    float detu = tri.e1v * Du - tri.e1u * Dv;
    float detv = tri.e0u * Dv - tri.e0v * Du;

    float tmpdet0 = det - detu - detv;
    //if ((tmpdet0 >= 0 && detu >= 0 && detv >= 0) || (tmpdet0 <= 0 && detu <= 0 && detv <= 0)) {
    if ((tmpdet0 > -HIT_EPS && detu > -HIT_EPS && detv > -HIT_EPS) || 
        (tmpdet0 < HIT_EPS && detu < HIT_EPS && detv < HIT_EPS)) {
        float rdet = 1.0f / det;
        float t = dett * rdet;
        float u = detu * rdet;
        float v = detv * rdet;

        if (t > 0 && t < inter.t) {
            inter.mask_values[0] = 0xffffffff;
            inter.prim_indices[0] = i;
            inter.t = t;
            inter.u = u;
            inter.v = v;
        }
    }
}

force_inline uint32_t near_child(const ray_packet_t &r, const bvh_node_t &node) {
    return r.d[node.prim_count >> 30] < 0 ? (node.right_child & RIGHT_CHILD_BITS) : node.left_child;
}

force_inline uint32_t far_child(const ray_packet_t &r, const bvh_node_t &node) {
    return r.d[node.prim_count >> 30] < 0 ? node.left_child : (node.right_child & RIGHT_CHILD_BITS);
}

force_inline uint32_t other_child(const bvh_node_t &node, uint32_t cur_child) {
    return node.left_child == cur_child ? (node.right_child & RIGHT_CHILD_BITS) : node.left_child;
}

force_inline bool is_leaf_node(const bvh_node_t &node) {
    return (node.prim_index & LEAF_NODE_BIT) != 0;
}

force_inline bool is_leaf_node(const mbvh_node_t &node) {
    return (node.child[0] & LEAF_NODE_BIT) != 0;
}

force_inline bool bbox_test(const float o[3], const float inv_d[3], const float t, const float bbox_min[3], const float bbox_max[3]) {
    float lo_x = inv_d[0] * (bbox_min[0] - o[0]);
    float hi_x = inv_d[0] * (bbox_max[0] - o[0]);
    if (lo_x > hi_x) { float tmp = lo_x; lo_x = hi_x; hi_x = tmp; }

    float lo_y = inv_d[1] * (bbox_min[1] - o[1]);
    float hi_y = inv_d[1] * (bbox_max[1] - o[1]);
    if (lo_y > hi_y) { float tmp = lo_y; lo_y = hi_y; hi_y = tmp; }

    float lo_z = inv_d[2] * (bbox_min[2] - o[2]);
    float hi_z = inv_d[2] * (bbox_max[2] - o[2]);
    if (lo_z > hi_z) { float tmp = lo_z; lo_z = hi_z; hi_z = tmp; }

    //float tmin = std::max(lo_x, std::max(lo_y, lo_z));
    //float tmax = std::min(hi_x, std::min(hi_y, hi_z));
    float tmin = lo_x > lo_y ? lo_x : lo_y;
    if (lo_z > tmin) tmin = lo_z;
    float tmax = hi_x < hi_y ? hi_x : hi_y;
    if (hi_z < tmax) tmax = hi_z;
    tmax *= 1.00000024f;

    return tmin <= tmax && tmin <= t && tmax > 0;
}

force_inline bool bbox_test(const float p[3], const float bbox_min[3], const float bbox_max[3]) {
    return p[0] > bbox_min[0] && p[0] < bbox_max[0] &&
           p[1] > bbox_min[1] && p[1] < bbox_max[1] &&
           p[2] > bbox_min[2] && p[2] < bbox_max[2];
}

force_inline bool bbox_test(const float o[3], const float inv_d[3], const float t, const bvh_node_t &node) {
    return bbox_test(o, inv_d, t, node.bbox_min, node.bbox_max);
}

force_inline bool bbox_test(const float p[3], const bvh_node_t &node) {
    return bbox_test(p, node.bbox_min, node.bbox_max);
}

force_inline bool bbox_test_oct(const float p[3], const mbvh_node_t &node, int i) {
    return p[0] > node.bbox_min[0][i] && p[0] < node.bbox_max[0][i] &&
           p[1] > node.bbox_min[1][i] && p[1] < node.bbox_max[1][i] &&
           p[2] > node.bbox_min[2][i] && p[2] < node.bbox_max[2][i];
}

force_inline void bbox_test_oct(const float o[3], const float inv_d[3], const mbvh_node_t &node, int res[8], float dist[8]) {
    ITERATE_8({
        float lo_x = inv_d[0] * (node.bbox_min[0][i] - o[0]);
        float hi_x = inv_d[0] * (node.bbox_max[0][i] - o[0]);
        if (lo_x > hi_x) { float tmp = lo_x; lo_x = hi_x; hi_x = tmp; }

        float lo_y = inv_d[1] * (node.bbox_min[1][i] - o[1]);
        float hi_y = inv_d[1] * (node.bbox_max[1][i] - o[1]);
        if (lo_y > hi_y) { float tmp = lo_y; lo_y = hi_y; hi_y = tmp; }

        float lo_z = inv_d[2] * (node.bbox_min[2][i] - o[2]);
        float hi_z = inv_d[2] * (node.bbox_max[2][i] - o[2]);
        if (lo_z > hi_z) { float tmp = lo_z; lo_z = hi_z; hi_z = tmp; }

        float tmin = lo_x > lo_y ? lo_x : lo_y;
        if (lo_z > tmin) tmin = lo_z;
        float tmax = hi_x < hi_y ? hi_x : hi_y;
        if (hi_z < tmax) tmax = hi_z;
        tmax *= 1.00000024f;

        dist[i] = tmin;
        res[i] = (tmin <= tmax && tmax > 0) ? 1 : 0;
    })
}

force_inline int bbox_test_oct(const float o[3], const float inv_d[3], const float t, const mbvh_node_t &node, float dist[8]) {
    int mask = 0;

    ITERATE_8({
        float lo_x = inv_d[0] * (node.bbox_min[0][i] - o[0]);
        float hi_x = inv_d[0] * (node.bbox_max[0][i] - o[0]);
        if (lo_x > hi_x) { float tmp = lo_x; lo_x = hi_x; hi_x = tmp; }

        float lo_y = inv_d[1] * (node.bbox_min[1][i] - o[1]);
        float hi_y = inv_d[1] * (node.bbox_max[1][i] - o[1]);
        if (lo_y > hi_y) { float tmp = lo_y; lo_y = hi_y; hi_y = tmp; }

        float lo_z = inv_d[2] * (node.bbox_min[2][i] - o[2]);
        float hi_z = inv_d[2] * (node.bbox_max[2][i] - o[2]);
        if (lo_z > hi_z) { float tmp = lo_z; lo_z = hi_z; hi_z = tmp; }

        float tmin = lo_x > lo_y ? lo_x : lo_y;
        if (lo_z > tmin) tmin = lo_z;
        float tmax = hi_x < hi_y ? hi_x : hi_y;
        if (hi_z < tmax) tmax = hi_z;
        tmax *= 1.00000024f;

        dist[i] = tmin;
        mask |= ((tmin <= tmax && tmin <= t && tmax > 0) ? 1 : 0) << i;
    })

    return mask;
}

enum eTraversalSource { FromParent, FromChild, FromSibling };

struct stack_entry_t {
    uint32_t index;
    float dist;
};

template<int StackSize>
class TraversalStack {
public:
    stack_entry_t stack[StackSize];
    uint32_t stack_size = 0;

    force_inline void push(uint32_t index, float dist) {
        stack[stack_size++] = { index, dist };
        assert(stack_size < StackSize && "Traversal stack overflow!");
    }

    force_inline stack_entry_t pop() {
        return stack[--stack_size];
    }

    force_inline uint32_t pop_index() {
        return stack[--stack_size].index;
    }

    force_inline bool empty() const {
        return stack_size == 0;
    }

    void sort_top3() {
        assert(stack_size >= 3);
        const uint32_t i = stack_size - 3;

        if (stack[i].dist > stack[i + 1].dist) {
            if (stack[i + 1].dist > stack[i + 2].dist) {
                return;
            } else if (stack[i].dist > stack[i + 2].dist) {
                std::swap(stack[i + 1], stack[i + 2]);
            } else {
                stack_entry_t tmp = stack[i];
                stack[i] = stack[i + 2];
                stack[i + 2] = stack[i + 1];
                stack[i + 1] = tmp;
            }
        } else {
            if (stack[i].dist > stack[i + 2].dist) {
                std::swap(stack[i], stack[i + 1]);
            } else if (stack[i + 2].dist > stack[i + 1].dist) {
                std::swap(stack[i], stack[i + 2]);
            } else {
                stack_entry_t tmp = stack[i];
                stack[i] = stack[i + 1];
                stack[i + 1] = stack[i + 2];
                stack[i + 2] = tmp;
            }
        }

        assert(stack[stack_size - 3].dist >= stack[stack_size - 2].dist &&
               stack[stack_size - 2].dist >= stack[stack_size - 1].dist);
    }

    void sort_top4() {
        assert(stack_size >= 4);
        const uint32_t i = stack_size - 4;

        if (stack[i + 0].dist < stack[i + 1].dist) std::swap(stack[i + 0], stack[i + 1]);
        if (stack[i + 2].dist < stack[i + 3].dist) std::swap(stack[i + 2], stack[i + 3]);
        if (stack[i + 0].dist < stack[i + 2].dist) std::swap(stack[i + 0], stack[i + 2]);
        if (stack[i + 1].dist < stack[i + 3].dist) std::swap(stack[i + 1], stack[i + 3]);
        if (stack[i + 1].dist < stack[i + 2].dist) std::swap(stack[i + 1], stack[i + 2]);

        assert(stack[stack_size - 4].dist >= stack[stack_size - 3].dist &&
               stack[stack_size - 3].dist >= stack[stack_size - 2].dist &&
               stack[stack_size - 2].dist >= stack[stack_size - 1].dist);
    }

    void sort_topN(int count) {
        assert(stack_size >= uint32_t(count));
        const int start = int(stack_size - count);

        for (int i = start + 1; i < int(stack_size); i++) {
            const stack_entry_t key = stack[i];

            int j = i - 1;

            while (j >= start && stack[j].dist < key.dist) {
                stack[j + 1] = stack[j];
                j--;
            }

            stack[j + 1] = key;
        }

#ifndef NDEBUG
        for (int j = 0; j < count - 1; j++) {
            assert(stack[stack_size - count + j].dist >= stack[stack_size - count + j + 1].dist);
        }
#endif
    }
};

force_inline int hash(int x) {
    unsigned ret = reinterpret_cast<const unsigned &>(x);
    ret = ((ret >> 16) ^ ret) * 0x45d9f3b;
    ret = ((ret >> 16) ^ ret) * 0x45d9f3b;
    ret = (ret >> 16) ^ ret;
    return reinterpret_cast<const int &>(ret);
}

force_inline void safe_invert(const float v[3], float out_v[3]) {
    out_v[0] = 1.0f / v[0];
    out_v[1] = 1.0f / v[1];
    out_v[2] = 1.0f / v[2];

    if (v[0] <= FLT_EPS && v[0] >= 0) {
        out_v[0] = std::numeric_limits<float>::max();
    } else if (v[0] >= -FLT_EPS && v[0] < 0) {
        out_v[0] = -std::numeric_limits<float>::max();
    }

    if (v[1] <= FLT_EPS && v[1] >= 0) {
        out_v[1] = std::numeric_limits<float>::max();
    } else if (v[1] >= -FLT_EPS && v[1] < 0) {
        out_v[1] = -std::numeric_limits<float>::max();
    }

    if (v[2] <= FLT_EPS && v[2] >= 0) {
        out_v[2] = std::numeric_limits<float>::max();
    } else if (v[2] >= -FLT_EPS && v[2] < 0) {
        out_v[2] = -std::numeric_limits<float>::max();
    }
}

force_inline float clamp(float val, float min, float max) {
    return val < min ? min : (val > max ? max : val);
}

force_inline int clamp(int val, int min, int max) {
    return val < min ? min : (val > max ? max : val);
}

force_inline simd_fvec3 cross(const simd_fvec3 &v1, const simd_fvec3 &v2) {
    return simd_fvec3{ v1[1] * v2[2] - v1[2] * v2[1], v1[2] * v2[0] - v1[0] * v2[2], v1[0] * v2[1] - v1[1] * v2[0] };
}

force_inline simd_fvec3 reflect(const simd_fvec3 &I, const simd_fvec3 &N) {
    return I - 2 * dot(N, I) * N;
}

force_inline float pow5(const float v) {
    return (v * v) * (v * v) * v;
}

force_inline uint32_t get_ray_hash(const ray_packet_t &r, const float root_min[3], const float cell_size[3]) {
    int x = clamp((int)((r.o[0] - root_min[0]) / cell_size[0]), 0, 255),
        y = clamp((int)((r.o[1] - root_min[1]) / cell_size[1]), 0, 255),
        z = clamp((int)((r.o[2] - root_min[2]) / cell_size[2]), 0, 255);

    //float omega = omega_table[int(r.d[2] / 0.0625f)];
    //float std::atan2(r.d[1], r.d[0]);
    //int o = (int)(16 * omega / (PI)), p = (int)(16 * (phi + PI) / (2 * PI));

    x = morton_table_256[x];
    y = morton_table_256[y];
    z = morton_table_256[z];

    int o = morton_table_16[(int)omega_table[clamp(int((1.0f + r.d[2]) / omega_step), 0, 32)]];
    int p = morton_table_16[(int)phi_table[clamp(int((1.0f + r.d[1]) / phi_step), 0, 16)][clamp(int((1.0f + r.d[0]) / phi_step), 0, 16)]];

    return (o << 25) | (p << 24) | (y << 2) | (z << 1) | (x << 0);
}

force_inline void _radix_sort_lsb(ray_chunk_t *begin, ray_chunk_t *end, ray_chunk_t *begin1, unsigned maxshift) {
    ray_chunk_t *end1 = begin1 + (end - begin);

    for (unsigned shift = 0; shift <= maxshift; shift += 8) {
        size_t count[0x100] = {};
        for (ray_chunk_t *p = begin; p != end; p++) {
            count[(p->hash >> shift) & 0xFF]++;
        }
        ray_chunk_t *bucket[0x100], *q = begin1;
        for (int i = 0; i < 0x100; q += count[i++]) {
            bucket[i] = q;
        }
        for (ray_chunk_t *p = begin; p != end; p++) {
            *bucket[(p->hash >> shift) & 0xFF]++ = *p;
        }
        std::swap(begin, begin1);
        std::swap(end, end1);
    }
}

force_inline void radix_sort(ray_chunk_t *begin, ray_chunk_t *end, ray_chunk_t *begin1) {
    _radix_sort_lsb(begin, end, begin1, 24);
}

force_inline float construct_float(uint32_t m) {
    const uint32_t ieeeMantissa = 0x007FFFFFu; // binary32 mantissa bitmask
    const uint32_t ieeeOne = 0x3F800000u;      // 1.0 in IEEE binary32

    m &= ieeeMantissa;                     // Keep only mantissa bits (fractional part)
    m |= ieeeOne;                          // Add fractional part to 1.0

    float  f = reinterpret_cast<float &>(m);                // Range [1:2]
    return f - 1.0f;                        // Range [0:1]
}

force_inline simd_fvec4 rgbe_to_rgb(const pixel_color8_t &rgbe) {
    float f = std::exp2(float(rgbe.a) - 128.0f);
    return simd_fvec4{ to_norm_float(rgbe.r) * f,
                       to_norm_float(rgbe.g) * f,
                       to_norm_float(rgbe.b) * f, 1.0f };
}

force_inline simd_fvec4 srgb_to_rgb(const simd_fvec4 &col) {
    simd_fvec4 ret;
    ITERATE_3({
        if (col[i] > 0.04045f) {
            ret[i] = std::pow((col[i] + 0.055f) / 1.055f, 2.4f);
        } else {
            ret[i] = col[i] / 12.92f;
        }
    })
    ret[3] = col[3];

    return ret;
}

force_inline float fast_log2(float val) {
    // From https://stackoverflow.com/questions/9411823/fast-log2float-x-implementation-c

    union { float val; int32_t x; } u = { val };
    float log_2 = (float)(((u.x >> 23) & 255) - 128);
    u.x &= ~(255 << 23);
    u.x += 127 << 23;
    log_2 += ((-0.34484843f) * u.val + 2.02466578f) * u.val - 0.67487759f;
    return (log_2);
}

float get_texture_lod(const texture_t &t, const simd_fvec2 &duv_dx, const simd_fvec2 &duv_dy) {
    const simd_fvec2 sz = { (float)(t.width & TEXTURE_WIDTH_BITS), (float)t.height };
    const simd_fvec2 _duv_dx = duv_dx * sz, _duv_dy = duv_dy * sz;
    const simd_fvec2 _diagonal = _duv_dx + _duv_dy;

    // Find minimal dimention of parallelogram
    const float min_length2 = std::min(std::min(_duv_dx.length2(), _duv_dy.length2()), _diagonal.length2());
    // Find lod
    float lod = fast_log2(min_length2);
    // Substruct 1 from lod to keep 4 texels for interpolation
    lod = clamp(0.5f * lod - 1.0f, 0.0f, float(MAX_MIP_LEVEL));

    return lod;
}

}
}

Ray::Ref::hit_data_t::hit_data_t() {
    mask_values[0] = 0;
    obj_indices[0] = -1;
    prim_indices[0] = -1;
    t = MAX_DIST;
}

void Ray::Ref::GeneratePrimaryRays(int iteration, const camera_t &cam, const rect_t &r, int w, int h, const float *halton, aligned_vector<ray_packet_t> &out_rays) {
    simd_fvec3 cam_origin = { cam.origin }, fwd = { cam.fwd }, side = { cam.side }, up = { cam.up };
    float focus_distance = cam.focus_distance;

    float k = float(w) / h;
    float fov_k = std::tan(0.5f * cam.fov * PI / 180.0f) * focus_distance;

    auto get_pix_dir = [k, fov_k, focus_distance, cam_origin, fwd, side, up, w, h](const float x, const float y, const simd_fvec3 &origin) {
        simd_fvec3 p(2 * fov_k * float(x) / w - fov_k, 2 * fov_k * float(-y) / h + fov_k, focus_distance);
        p = cam_origin + k * p[0] * side + p[1] * up + p[2] * fwd;
        return normalize(p - origin);
    };

    size_t i = 0;
    out_rays.resize((size_t)r.w * r.h);

    for (int y = r.y; y < r.y + r.h; y += RayPacketDimY) {
        for (int x = r.x; x < r.x + r.w; x += RayPacketDimX) {
            ray_packet_t &out_r = out_rays[i++];

            const int index = y * w + x;
            const int hi = (iteration & (HALTON_SEQ_LEN - 1)) * HALTON_COUNT;

            float _x = (float)x;
            float _y = (float)y;

            float _unused;
            int hash_val = hash(index);

            if (cam.filter == Tent) {
                float rx = std::modf(halton[hi + 0] + construct_float(hash_val), &_unused);
                if (rx < 0.5f) {
                    rx = std::sqrt(2.0f * rx) - 1.0f;
                } else {
                    rx = 1.0f - std::sqrt(2.0f - 2 * rx);
                }

                float ry = std::modf(halton[hi + 1] + construct_float(hash(hash_val)), &_unused);
                if (ry < 0.5f) {
                    ry = std::sqrt(2.0f * ry) - 1.0f;
                } else {
                    ry = 1.0f - std::sqrt(2.0f - 2.0f * ry);
                }

                _x += 0.5f + rx;
                _y += 0.5f + ry;
            } else {
                _x += std::modf(halton[hi + 0] + construct_float(hash_val), &_unused);
                _y += std::modf(halton[hi + 1] + construct_float(hash(hash_val)), &_unused);
            }


            float ff1 = cam.focus_factor * (-0.5f + std::modf(halton[hi + 2 + 0] + construct_float(hash_val), &_unused));
            float ff2 = cam.focus_factor * (-0.5f + std::modf(halton[hi + 2 + 1] + construct_float(hash(hash_val)), &_unused));

            simd_fvec3 _origin = cam_origin + side * ff1 + up * ff2;

            simd_fvec3 _d = get_pix_dir(_x, _y, _origin);

            simd_fvec3 _dx = get_pix_dir(_x + 1, _y, _origin),
                       _dy = get_pix_dir(_x, _y + 1, _origin);

            for (int j = 0; j < 3; j++) {
                out_r.o[j] = _origin[j];
                out_r.d[j] = _d[j];
                out_r.c[j] = 1.0f;

                out_r.do_dx[j] = 0;
                out_r.dd_dx[j] = _dx[j] - _d[j];
                out_r.do_dy[j] = 0;
                out_r.dd_dy[j] = _dy[j] - _d[j];
            }

            out_r.xy = (x << 16) | y;
            out_r.ior = 1.0f;
            out_r.ray_depth = 0;
        }
    }
}

void Ray::Ref::SampleMeshInTextureSpace(int iteration, int obj_index, int uv_layer, const mesh_t &mesh, const transform_t &tr, const uint32_t *vtx_indices, const vertex_t *vertices,
                                        const rect_t &r, int width, int height, const float *halton, aligned_vector<ray_packet_t> &out_rays, aligned_vector<hit_data_t> &out_inters) {
    out_rays.resize((size_t)r.w * r.h);
    out_inters.resize(out_rays.size());

    for (int y = r.y; y < r.y + r.h; y += RayPacketDimY) {
        for (int x = r.x; x < r.x + r.w; x += RayPacketDimX) {
            int i = (y - r.y) * r.w + (x - r.x);

            ray_packet_t &out_ray = out_rays[i];
            hit_data_t &out_inter = out_inters[i];

            out_ray.xy = (x << 16) | y;
            out_ray.c[0] = out_ray.c[1] = out_ray.c[2] = 1.0f;
            out_inter.mask_values[0] = 0;
            out_inter.xy = out_ray.xy;
        }
    }

    simd_ivec2 irect_min = { r.x, r.y }, irect_max = { r.x + r.w - 1, r.y + r.h - 1 };
    simd_fvec2 size = { (float)width, (float)height };
    
    for (uint32_t tri = mesh.tris_index; tri < mesh.tris_index + mesh.tris_count; tri++) {
        const vertex_t &v0 = vertices[vtx_indices[tri * 3 + 0]];
        const vertex_t &v1 = vertices[vtx_indices[tri * 3 + 1]];
        const vertex_t &v2 = vertices[vtx_indices[tri * 3 + 2]];

        const auto t0 = simd_fvec2{ v0.t[uv_layer][0], 1.0f - v0.t[uv_layer][1] } * size;
        const auto t1 = simd_fvec2{ v1.t[uv_layer][0], 1.0f - v1.t[uv_layer][1] } * size;
        const auto t2 = simd_fvec2{ v2.t[uv_layer][0], 1.0f - v2.t[uv_layer][1] } * size;

        simd_fvec2 bbox_min = t0, bbox_max = t0;

        bbox_min = min(bbox_min, t1);
        bbox_min = min(bbox_min, t2);

        bbox_max = max(bbox_max, t1);
        bbox_max = max(bbox_max, t2);

        simd_ivec2 ibbox_min = (simd_ivec2)(bbox_min),
                   ibbox_max = simd_ivec2{ (int)std::round(bbox_max[0]), (int)std::round(bbox_max[1]) };

        if (ibbox_max[0] < irect_min[0] || ibbox_max[1] < irect_min[1] ||
            ibbox_min[0] > irect_max[0] || ibbox_min[1] > irect_max[1]) continue;

        ibbox_min = max(ibbox_min, irect_min);
        ibbox_max = min(ibbox_max, irect_max);

        const simd_fvec2 d01 = t0 - t1, d12 = t1 - t2, d20 = t2 - t0;

        float area = d01[0] * d20[1] - d20[0] * d01[1];
        if (area < FLT_EPS) continue;

        float inv_area = 1.0f / area;

        for (int y = ibbox_min[1]; y <= ibbox_max[1]; y++) {
            for (int x = ibbox_min[0]; x <= ibbox_max[0]; x++) {
                int i = (y - r.y) * r.w + (x - r.x);
                ray_packet_t &out_ray = out_rays[i];
                hit_data_t &out_inter = out_inters[i];

                if (out_inter.mask_values[0]) continue;

                const int index = y * width + x;
                const int hi = (iteration & (HALTON_SEQ_LEN - 1)) * HALTON_COUNT;

                int hash_val = hash(index);

                float _unused;
                float _x = float(x) + std::modf(halton[hi + 0] + construct_float(hash_val), &_unused);
                float _y = float(y) + std::modf(halton[hi + 1] + construct_float(hash(hash_val)), &_unused);

                float u = d01[0] * (_y - t0[1]) - d01[1] * (_x - t0[0]),
                      v = d12[0] * (_y - t1[1]) - d12[1] * (_x - t1[0]),
                      w = d20[0] * (_y - t2[1]) - d20[1] * (_x - t2[0]);

                if (u >= -FLT_EPS && v >= -FLT_EPS && w >= -FLT_EPS) {
                    const simd_fvec3 p0 = { v0.p }, p1 = { v1.p }, p2 = { v2.p };
                    const simd_fvec3 n0 = { v0.n }, n1 = { v1.n }, n2 = { v2.n };

                    u *= inv_area; v *= inv_area; w *= inv_area;

                    const simd_fvec3 p = TransformPoint(p0 * v + p1 * w + p2 * u, tr.xform),
                                     n = TransformNormal(n0 * v + n1 * w + n2 * u, tr.inv_xform);

                    const simd_fvec3 o = p + n, d = -n;

                    memcpy(&out_ray.o[0], value_ptr(o), 3 * sizeof(float));
                    memcpy(&out_ray.d[0], value_ptr(d), 3 * sizeof(float));
                    out_ray.ior = 1.0f;
                    out_ray.do_dx[0] = out_ray.do_dx[1] = out_ray.do_dx[2] = 0.0f;
                    out_ray.dd_dx[0] = out_ray.dd_dx[1] = out_ray.dd_dx[2] = 0.0f;
                    out_ray.do_dy[0] = out_ray.do_dy[1] = out_ray.do_dy[2] = 0.0f;
                    out_ray.dd_dy[0] = out_ray.dd_dy[1] = out_ray.dd_dy[2] = 0.0f;
                    out_ray.ray_depth = 0;

                    out_inter.mask_values[0] = 0xffffffff;
                    out_inter.prim_indices[0] = tri;
                    out_inter.obj_indices[0] = obj_index;
                    out_inter.t = 1.0f;
                    out_inter.u = w;
                    out_inter.v = u;
                }
            }
        }
    }
}

void Ray::Ref::SortRays_CPU(ray_packet_t *rays, size_t rays_count, const float root_min[3], const float cell_size[3],
                            uint32_t *hash_values, uint32_t *scan_values, ray_chunk_t *chunks, ray_chunk_t *chunks_temp) {
    // From "Fast Ray Sorting and Breadth-First Packet Traversal for GPU Ray Tracing" [2010]

    // compute ray hash values
    for (size_t i = 0; i < rays_count; i++) {
        hash_values[i] = get_ray_hash(rays[i], root_min, cell_size);
    }

    size_t chunks_count = 0;

    // compress codes into spans of indentical values (makes sorting stage faster)
    for (uint32_t start = 0, end = 1; end <= (uint32_t)rays_count; end++) {
        if (end == (uint32_t)rays_count ||
            (hash_values[start] != hash_values[end])) {
            chunks[chunks_count].hash = hash_values[start];
            chunks[chunks_count].base = start;
            chunks[chunks_count++].size = end - start;
            start = end;
        }
    }

    radix_sort(&chunks[0], &chunks[0] + chunks_count, &chunks_temp[0]);

    // decompress sorted spans
    size_t counter = 0;
    for (uint32_t i = 0; i < chunks_count; i++) {
        for (uint32_t j = 0; j < chunks[i].size; j++) {
            scan_values[counter++] = chunks[i].base + j;
        }
    }

    {   // reorder rays
        for (uint32_t i = 0; i < (uint32_t)rays_count; i++) {
            uint32_t j;
            while (i != (j = scan_values[i])) {
                int k = scan_values[j];
                std::swap(rays[j], rays[k]);
                std::swap(scan_values[i], scan_values[j]);
            }
        }
    }
}

void Ray::Ref::SortRays_GPU(ray_packet_t *rays, size_t rays_count, const float root_min[3], const float cell_size[3],
                            uint32_t *hash_values, int *head_flags, uint32_t *scan_values, ray_chunk_t *chunks, ray_chunk_t *chunks_temp, uint32_t *skeleton) {
    // From "Fast Ray Sorting and Breadth-First Packet Traversal for GPU Ray Tracing" [2010]

    // compute ray hash values
    for (size_t i = 0; i < rays_count; i++) {
        hash_values[i] = get_ray_hash(rays[i], root_min, cell_size);
    }

    // set head flags
    head_flags[0] = 1;
    for (size_t i = 1; i < rays_count; i++) {
        head_flags[i] = hash_values[i] != hash_values[i - 1];
    }

    size_t chunks_count = 0;

    {   // perform exclusive scan on head flags
        uint32_t cur_sum = 0;
        for (size_t i = 0; i < rays_count; i++) {
            scan_values[i] = cur_sum;
            cur_sum += head_flags[i];
        }
        chunks_count = cur_sum;
    }

    // init Ray chunks hash and base index
    for (size_t i = 0; i < rays_count; i++) {
        if (head_flags[i]) {
            chunks[scan_values[i]].hash = hash_values[i];
            chunks[scan_values[i]].base = (uint32_t)i;
        }
    }

    // init ray chunks size
    if (chunks_count) {
        for (size_t i = 0; i < chunks_count - 1; i++) {
            chunks[i].size = chunks[i + 1].base - chunks[i].base;
        }
        chunks[chunks_count - 1].size = (uint32_t)rays_count - chunks[chunks_count - 1].base;
    }

    radix_sort(&chunks[0], &chunks[0] + chunks_count, &chunks_temp[0]);

    {   // perform exclusive scan on chunks size
        uint32_t cur_sum = 0;
        for (size_t i = 0; i < chunks_count; i++) {
            scan_values[i] = cur_sum;
            cur_sum += chunks[i].size;
        }
    }

    std::fill(skeleton, skeleton + rays_count, 1);
    std::fill(head_flags, head_flags + rays_count, 0);

    // init skeleton and head flags array
    for (size_t i = 0; i < chunks_count; i++) {
        skeleton[scan_values[i]] = chunks[i].base;
        head_flags[scan_values[i]] = 1;
    }

    {   // perform a segmented scan on skeleton array
        uint32_t cur_sum = 0;
        for (size_t i = 0; i < rays_count; i++) {
            if (head_flags[i]) cur_sum = 0;
            cur_sum += skeleton[i];
            scan_values[i] = cur_sum;
        }
    }

    {   // reorder rays
        for (uint32_t i = 0; i < (uint32_t)rays_count; i++) {
            uint32_t j;
            while (i != (j = scan_values[i])) {
                int k = scan_values[j];
                std::swap(rays[j], rays[k]);
                std::swap(scan_values[i], scan_values[j]);
            }
        }
    }
}

bool Ray::Ref::IntersectTris_ClosestHit(const ray_packet_t &r, const tri_accel_t *tris, int num_tris, int obj_index, hit_data_t &out_inter) {
    hit_data_t inter;
    inter.obj_indices[0] = obj_index;
    inter.t = out_inter.t;

    for (int i = 0; i < num_tris; i++) {
        _IntersectTri(r, tris[i], i, inter);
    }

    out_inter.mask_values[0] |= inter.mask_values[0];
    out_inter.obj_indices[0] = inter.mask_values[0] ? inter.obj_indices[0] : out_inter.obj_indices[0];
    out_inter.prim_indices[0] = inter.mask_values[0] ? inter.prim_indices[0] : out_inter.prim_indices[0];
    out_inter.t = inter.t; // already contains min value
    out_inter.u = inter.mask_values[0] ? inter.u : out_inter.u;
    out_inter.v = inter.mask_values[0] ? inter.v : out_inter.v;

    return inter.mask_values[0] != 0;
}

bool Ray::Ref::IntersectTris_ClosestHit(const ray_packet_t &r, const tri_accel_t *tris, const uint32_t *indices, int num_indices, int obj_index, hit_data_t &out_inter) {
    hit_data_t inter{ Uninitialize };
    inter.mask_values[0] = 0;
    inter.obj_indices[0] = obj_index;
    inter.t = out_inter.t;

    for (int i = 0; i < num_indices; i++) {
        uint32_t index = indices[i];
        _IntersectTri(r, tris[index], index, inter);
    }

    out_inter.mask_values[0] |= inter.mask_values[0];
    out_inter.obj_indices[0] = inter.mask_values[0] ? inter.obj_indices[0] : out_inter.obj_indices[0];
    out_inter.prim_indices[0] = inter.mask_values[0] ? inter.prim_indices[0] : out_inter.prim_indices[0];
    out_inter.t = inter.t; // already contains min value
    out_inter.u = inter.mask_values[0] ? inter.u : out_inter.u;
    out_inter.v = inter.mask_values[0] ? inter.v : out_inter.v;

    return inter.mask_values[0] != 0;
}

bool Ray::Ref::IntersectTris_AnyHit(const ray_packet_t &r, const tri_accel_t *tris, int num_tris, int obj_index, hit_data_t &out_inter) {
    hit_data_t inter;
    inter.obj_indices[0] = obj_index;
    inter.t = out_inter.t;

    for (int i = 0; i < num_tris; i++) {
        _IntersectTri(r, tris[i], i, inter);
        if (inter.mask_values[0] && (tris[i].ci & TRI_SOLID_BIT)) {
            break;
        }
    }

    out_inter.mask_values[0] |= inter.mask_values[0];
    out_inter.obj_indices[0] = inter.mask_values[0] ? inter.obj_indices[0] : out_inter.obj_indices[0];
    out_inter.prim_indices[0] = inter.mask_values[0] ? inter.prim_indices[0] : out_inter.prim_indices[0];
    out_inter.t = inter.t; // already contains min value
    out_inter.u = inter.mask_values[0] ? inter.u : out_inter.u;
    out_inter.v = inter.mask_values[0] ? inter.v : out_inter.v;

    return inter.mask_values[0] != 0;
}

bool Ray::Ref::IntersectTris_AnyHit(const ray_packet_t &r, const tri_accel_t *tris, const uint32_t *indices, int num_indices, int obj_index, hit_data_t &out_inter) {
    hit_data_t inter{ Uninitialize };
    inter.mask_values[0] = 0;
    inter.obj_indices[0] = obj_index;
    inter.t = out_inter.t;

    for (int i = 0; i < num_indices; i++) {
        uint32_t index = indices[i];
        _IntersectTri(r, tris[index], index, inter);
        if (inter.mask_values[0] && (tris[index].ci & TRI_SOLID_BIT)) {
            break;
        }
    }

    out_inter.mask_values[0] |= inter.mask_values[0];
    out_inter.obj_indices[0] = inter.mask_values[0] ? inter.obj_indices[0] : out_inter.obj_indices[0];
    out_inter.prim_indices[0] = inter.mask_values[0] ? inter.prim_indices[0] : out_inter.prim_indices[0];
    out_inter.t = inter.t; // already contains min value
    out_inter.u = inter.mask_values[0] ? inter.u : out_inter.u;
    out_inter.v = inter.mask_values[0] ? inter.v : out_inter.v;

    return inter.mask_values[0] != 0;
}

#ifdef USE_STACKLESS_BVH_TRAVERSAL
bool Ray::Ref::Traverse_MacroTree_Stackless_CPU(const ray_packet_t &r, const bvh_node_t *nodes, uint32_t root_index,
                                                const mesh_instance_t *mesh_instances, const uint32_t *mi_indices, const mesh_t *meshes, const transform_t *transforms,
                                                const tri_accel_t *tris, const uint32_t *tri_indices, hit_data_t &inter) {
    bool res = false;

    float inv_d[3];
    safe_invert(r.d, inv_d);

    uint32_t cur = root_index;
    eTraversalSource src = FromSibling;

    if (!is_leaf_node(nodes[root_index])) {
        cur = near_child(r, nodes[root_index]);
        src = FromParent;
    }

    while (true) {
        switch (src) {
        case FromChild:
            if (cur == root_index || cur == 0xffffffff) return res;
            if (cur == near_child(r, nodes[nodes[cur].parent])) {
                cur = other_child(nodes[nodes[cur].parent], cur);
                src = FromSibling;
            } else {
                cur = nodes[cur].parent;
                src = FromChild;
            }
            break;
        case FromSibling:
            if (!bbox_test(r.o, inv_d, inter.t, nodes[cur])) {
                cur = nodes[cur].parent;
                src = FromChild;
            } else if (is_leaf_node(nodes[cur])) {
                // process leaf
                for (uint32_t i = nodes[cur].prim_index; i < nodes[cur].prim_index + nodes[cur].prim_count; i++) {
                    const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                    const mesh_t &m = meshes[mi.mesh_index];
                    const transform_t &tr = transforms[mi.tr_index];

                    if (!bbox_test(r.o, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) continue;

                    ray_packet_t _r = TransformRay(r, tr.inv_xform);

                    float _inv_d[3];
                    safe_invert(_r.d, _inv_d);

                    res |= Traverse_MicroTree_Stackless_CPU(_r, _inv_d, nodes, m.node_index, tris, tri_indices, (int)mi_indices[i], inter);
                }

                cur = nodes[cur].parent;
                src = FromChild;
            } else {
                cur = near_child(r, nodes[cur]);
                src = FromParent;
            }
            break;
        case FromParent:
            if (!bbox_test(r.o, inv_d, inter.t, nodes[cur])) {
                cur = other_child(nodes[nodes[cur].parent], cur);
                src = FromSibling;
            } else if (is_leaf_node(nodes[cur])) {
                // process leaf
                for (uint32_t i = nodes[cur].prim_index; i < nodes[cur].prim_index + nodes[cur].prim_count; i++) {
                    const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                    const mesh_t &m = meshes[mi.mesh_index];
                    const transform_t &tr = transforms[mi.tr_index];

                    if (!bbox_test(r.o, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) continue;

                    ray_packet_t _r = TransformRay(r, tr.inv_xform);

                    float _inv_d[3];
                    safe_invert(_r.d, _inv_d);

                    res |= Traverse_MicroTree_Stackless_CPU(_r, _inv_d, nodes, m.node_index, tris, tri_indices, (int)mi_indices[i], inter);
                }

                cur = other_child(nodes[nodes[cur].parent], cur);
                src = FromSibling;
            } else {
                cur = near_child(r, nodes[cur]);
                src = FromParent;
            }
            break;
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MacroTree_Stackless_GPU(const ray_packet_t &r, const bvh_node_t *nodes, uint32_t root_index,
                                                const mesh_instance_t *mesh_instances, const uint32_t *mi_indices, const mesh_t *meshes, const transform_t *transforms,
                                                const tri_accel_t *tris, const uint32_t *tri_indices, hit_data_t &inter) {
    bool res = false;

    float inv_d[3];
    safe_invert(r.d, inv_d);

    uint32_t cur = root_index;
    uint32_t last = root_index;

    if (!is_leaf_node(nodes[cur])) {
        cur = near_child(r, nodes[cur]);
    }

    while (true) {
        if (cur == 0xffffffff) return res;

        if (is_leaf_node(nodes[cur])) {
            for (uint32_t i = nodes[cur].prim_index; i < nodes[cur].prim_index + nodes[cur].prim_count; i++) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(r.o, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) continue;

                ray_packet_t _r = TransformRay(r, tr.inv_xform);

                float _inv_d[3] = { 1.0f / _r.d[0], 1.0f / _r.d[1], 1.0f / _r.d[2] };

                res |= Traverse_MicroTree_Stackless_GPU(_r, _inv_d, nodes, m.node_index, tris, tri_indices, (int)mi_indices[i], inter);
            }
            last = cur;
            cur = nodes[cur].parent;
            continue;
        }

        uint32_t near = near_child(r, nodes[cur]);
        uint32_t far = far_child(r, nodes[cur]);

        if (last == far) {
            last = cur;
            cur = nodes[cur].parent;
            continue;
        }

        uint32_t try_child = (last == nodes[cur].parent) ? near : far;
        if (bbox_test(r.o, inv_d, inter.t, nodes[try_child])) {
            last = cur;
            cur = try_child;
        } else {
            if (try_child == near) {
                last = near;
            } else {
                last = cur;
                cur = nodes[cur].parent;
            }
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MicroTree_Stackless_CPU(const ray_packet_t &r, const float inv_d[3], const bvh_node_t *nodes, uint32_t root_index,
                                                const tri_accel_t *tris, const uint32_t *tri_indices, int obj_index, hit_data_t &inter) {
    bool res = false;

    uint32_t cur = root_index;
    eTraversalSource src = FromSibling;

    if (!is_leaf_node(nodes[root_index])) {
        cur = near_child(r, nodes[root_index]);
        src = FromParent;
    }

    while (true) {
        switch (src) {
        case FromChild:
            if (cur == root_index || cur == 0xffffffff) return res;
            if (cur == near_child(r, nodes[nodes[cur].parent])) {
                cur = other_child(nodes[nodes[cur].parent], cur);
                src = FromSibling;
            } else {
                cur = nodes[cur].parent;
                src = FromChild;
            }
            break;
        case FromSibling:
            if (!bbox_test(r.o, inv_d, inter.t, nodes[cur])) {
                cur = nodes[cur].parent;
                src = FromChild;
            } else if (is_leaf_node(nodes[cur])) {
                // process leaf
                res |= IntersectTris_ClosestHit(r, tris, &tri_indices[nodes[cur].prim_index], nodes[cur].prim_count, obj_index, inter);

                cur = nodes[cur].parent;
                src = FromChild;
            } else {
                cur = near_child(r, nodes[cur]);
                src = FromParent;
            }
            break;
        case FromParent:
            if (!bbox_test(r.o, inv_d, inter.t, nodes[cur])) {
                cur = other_child(nodes[nodes[cur].parent], cur);
                src = FromSibling;
            } else if (is_leaf_node(nodes[cur])) {
                // process leaf
                res |= IntersectTris_ClosestHit(r, tris, &tri_indices[nodes[cur].prim_index], nodes[cur].prim_count, obj_index, inter);

                cur = other_child(nodes[nodes[cur].parent], cur);
                src = FromSibling;
            } else {
                cur = near_child(r, nodes[cur]);
                src = FromParent;
            }
            break;
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MicroTree_Stackless_GPU(const ray_packet_t &r, const float inv_d[3], const bvh_node_t *nodes, uint32_t root_index,
                                                const tri_accel_t *tris, const uint32_t *indices, int obj_index, hit_data_t &inter) {
    bool res = false;

    uint32_t cur = root_index;
    uint32_t last = root_index;

    if (!is_leaf_node(nodes[root_index])) {
        cur = near_child(r, nodes[root_index]);
        //last = cur;
    }

    while (true) {
        if (cur == 0xffffffff) return res;

        if (is_leaf_node(nodes[cur])) {
            res |= IntersectTris_ClosestHit(r, tris, &indices[nodes[cur].prim_index], nodes[cur].prim_count, obj_index, inter);

            last = cur;
            cur = nodes[cur].parent;
            continue;
        }

        uint32_t near = near_child(r, nodes[cur]);
        uint32_t far = far_child(r, nodes[cur]);

        if (last == far) {
            last = cur;
            cur = nodes[cur].parent;
            continue;
        }

        uint32_t try_child = (last == nodes[cur].parent) ? near : far;
        if (bbox_test(r.o, inv_d, inter.t, nodes[try_child])) {
            last = cur;
            cur = try_child;
        } else {
            if (try_child == near) {
                last = near;
            } else {
                last = cur;
                cur = nodes[cur].parent;
            }
        }
    }

    return res;
}
#endif

bool Ray::Ref::Traverse_MacroTree_WithStack_ClosestHit(const ray_packet_t &r, const bvh_node_t *nodes, uint32_t root_index,
                                                       const mesh_instance_t *mesh_instances, const uint32_t *mi_indices, const mesh_t *meshes, const transform_t *transforms,
                                                       const tri_accel_t *tris, const uint32_t *tri_indices, hit_data_t &inter) {
    bool res = false;

    float inv_d[3];
    safe_invert(r.d, inv_d);

    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack[stack_size++] = root_index;

    while (stack_size) {
        uint32_t cur = stack[--stack_size];

        if (!bbox_test(r.o, inv_d, inter.t, nodes[cur])) continue;

        if (!is_leaf_node(nodes[cur])) {
            stack[stack_size++] = far_child(r, nodes[cur]);
            stack[stack_size++] = near_child(r, nodes[cur]);
        } else {
            uint32_t prim_index = (nodes[cur].prim_index & PRIM_INDEX_BITS);
            for (uint32_t i = prim_index; i < prim_index + nodes[cur].prim_count; i++) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(r.o, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) continue;

                ray_packet_t _r = TransformRay(r, tr.inv_xform);

                float _inv_d[3] = { 1.0f / _r.d[0], 1.0f / _r.d[1], 1.0f / _r.d[2] };

                res |= Traverse_MicroTree_WithStack_ClosestHit(_r, _inv_d, nodes, m.node_index, tris, tri_indices, (int)mi_indices[i], inter);
            }
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MacroTree_WithStack_ClosestHit(const ray_packet_t &r, const mbvh_node_t *nodes, uint32_t root_index,
                                                       const mesh_instance_t *mesh_instances, const uint32_t *mi_indices, const mesh_t *meshes, const transform_t *transforms,
                                                       const tri_accel_t *tris, const uint32_t *tri_indices, hit_data_t &inter) {
    bool res = false;

    float inv_d[3];
    safe_invert(r.d, inv_d);

    TraversalStack<MAX_STACK_SIZE> st;
    st.push(root_index, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > inter.t) continue;

TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            float dist[8];
            long mask = bbox_test_oct(r.o, inv_d, inter.t, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask); mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                long i2 = GetFirstBit(mask); mask = ClearBit(mask, i2);
                if (mask == 0) { // two boxes were hit
                    if (dist[i] < dist[i2]) {
                        st.push(nodes[cur.index].child[i2], dist[i2]);
                        cur.index = nodes[cur.index].child[i];
                    } else {
                        st.push(nodes[cur.index].child[i], dist[i]);
                        cur.index = nodes[cur.index].child[i2];
                    }
                    goto TRAVERSE;
                }

                st.push(nodes[cur.index].child[i], dist[i]);
                st.push(nodes[cur.index].child[i2], dist[i2]);

                i = GetFirstBit(mask); mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask); mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask); mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            uint32_t prim_index = (nodes[cur.index].child[0] & PRIM_INDEX_BITS);
            for (uint32_t i = prim_index; i < prim_index + nodes[cur.index].child[1]; i++) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(r.o, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) continue;

                ray_packet_t _r = TransformRay(r, tr.inv_xform);

                const float _inv_d[3] = { 1.0f / _r.d[0], 1.0f / _r.d[1], 1.0f / _r.d[2] };
                res |= Traverse_MicroTree_WithStack_ClosestHit(_r, _inv_d, nodes, m.node_index, tris, tri_indices, (int)mi_indices[i], inter);
            }
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MacroTree_WithStack_AnyHit(const ray_packet_t &r, const bvh_node_t *nodes, uint32_t root_index,
                                                   const mesh_instance_t *mesh_instances, const uint32_t *mi_indices, const mesh_t *meshes, const transform_t *transforms,
                                                   const tri_accel_t *tris, const uint32_t *tri_indices, hit_data_t &inter) {
    bool res = false;

    float inv_d[3];
    safe_invert(r.d, inv_d);

    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack[stack_size++] = root_index;

    while (stack_size) {
        uint32_t cur = stack[--stack_size];

        if (!bbox_test(r.o, inv_d, inter.t, nodes[cur])) continue;

        if (!is_leaf_node(nodes[cur])) {
            stack[stack_size++] = far_child(r, nodes[cur]);
            stack[stack_size++] = near_child(r, nodes[cur]);
        } else {
            uint32_t prim_index = (nodes[cur].prim_index & PRIM_INDEX_BITS);
            for (uint32_t i = prim_index; i < prim_index + nodes[cur].prim_count; i++) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(r.o, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) continue;

                ray_packet_t _r = TransformRay(r, tr.inv_xform);

                float _inv_d[3] = { 1.0f / _r.d[0], 1.0f / _r.d[1], 1.0f / _r.d[2] };

                bool hit_found = Traverse_MicroTree_WithStack_AnyHit(_r, _inv_d, nodes, m.node_index, tris, tri_indices, (int)mi_indices[i], inter);
                res |= hit_found;
                if (hit_found && (tris[inter.prim_indices[0]].ci & TRI_SOLID_BIT)) {
                    return true;
                }
            }
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MacroTree_WithStack_AnyHit(const ray_packet_t &r, const mbvh_node_t *nodes, uint32_t root_index,
                                                   const mesh_instance_t *mesh_instances, const uint32_t *mi_indices, const mesh_t *meshes, const transform_t *transforms,
                                                   const tri_accel_t *tris, const uint32_t *tri_indices, hit_data_t &inter) {
    bool res = false;

    const int ray_dir_oct = ((r.d[2] > 0.0f) << 2) | ((r.d[1] > 0.0f) << 1) | (r.d[0] > 0.0f);

    int child_order[8];
    ITERATE_8({ child_order[i] = i ^ ray_dir_oct; })

    float inv_d[3];
    safe_invert(r.d, inv_d);

    TraversalStack<MAX_STACK_SIZE> st;
    st.push(root_index, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > inter.t) continue;

TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            float dist[8];
            long mask = bbox_test_oct(r.o, inv_d, inter.t, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask); mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                int i2 = GetFirstBit(mask); mask = ClearBit(mask, i2);
                if (mask == 0) { // two boxes were hit
                    if (dist[i] < dist[i2]) {
                        st.push(nodes[cur.index].child[i2], dist[i2]);
                        cur.index = nodes[cur.index].child[i];
                    } else {
                        st.push(nodes[cur.index].child[i], dist[i]);
                        cur.index = nodes[cur.index].child[i2];
                    }
                    goto TRAVERSE;
                }

                st.push(nodes[cur.index].child[i], dist[i]);
                st.push(nodes[cur.index].child[i2], dist[i2]);

                i = GetFirstBit(mask); mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask); mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask); mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            uint32_t prim_index = (nodes[cur.index].child[0] & PRIM_INDEX_BITS);
            for (uint32_t i = prim_index; i < prim_index + nodes[cur.index].child[1]; i++) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(r.o, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) continue;

                ray_packet_t _r = TransformRay(r, tr.inv_xform);

                const float _inv_d[3] = { 1.0f / _r.d[0], 1.0f / _r.d[1], 1.0f / _r.d[2] };
                bool hit_found = Traverse_MicroTree_WithStack_ClosestHit(_r, _inv_d, nodes, m.node_index, tris, tri_indices, (int)mi_indices[i], inter);
                res |= hit_found;
                if (hit_found && (tris[inter.prim_indices[0]].ci & TRI_SOLID_BIT)) {
                    return true;
                }
            }
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MicroTree_WithStack_ClosestHit(const ray_packet_t &r, const float inv_d[3], const bvh_node_t *nodes, uint32_t root_index,
                                                       const tri_accel_t *tris, const uint32_t *tri_indices, int obj_index, hit_data_t &inter) {
    bool res = false;

    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack[stack_size++] = root_index;

    while (stack_size) {
        uint32_t cur = stack[--stack_size];

        if (!bbox_test(r.o, inv_d, inter.t, nodes[cur])) continue;

        if (!is_leaf_node(nodes[cur])) {
            stack[stack_size++] = far_child(r, nodes[cur]);
            stack[stack_size++] = near_child(r, nodes[cur]);
        } else {
            res |= IntersectTris_ClosestHit(r, tris, &tri_indices[nodes[cur].prim_index & PRIM_INDEX_BITS], nodes[cur].prim_count, obj_index, inter);
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MicroTree_WithStack_ClosestHit(const ray_packet_t &r, const float inv_d[3], const mbvh_node_t *nodes, uint32_t root_index,
                                                       const tri_accel_t *tris, const uint32_t *tri_indices, int obj_index, hit_data_t &inter) {
    bool res = false;

    TraversalStack<MAX_STACK_SIZE> st;
    st.push(root_index, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > inter.t) continue;

TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            float dist[8];
            long mask = bbox_test_oct(r.o, inv_d, inter.t, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask); mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                long i2 = GetFirstBit(mask); mask = ClearBit(mask, i2);
                if (mask == 0) { // two boxes were hit
                    if (dist[i] < dist[i2]) {
                        st.push(nodes[cur.index].child[i2], dist[i2]);
                        cur.index = nodes[cur.index].child[i];
                    } else {
                        st.push(nodes[cur.index].child[i], dist[i]);
                        cur.index = nodes[cur.index].child[i2];
                    }
                    goto TRAVERSE;
                }

                st.push(nodes[cur.index].child[i], dist[i]);
                st.push(nodes[cur.index].child[i2], dist[i2]);

                i = GetFirstBit(mask); mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask); mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask); mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            res |= IntersectTris_ClosestHit(r, tris, &tri_indices[nodes[cur.index].child[0] & PRIM_INDEX_BITS], nodes[cur.index].child[1], obj_index, inter);
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MicroTree_WithStack_AnyHit(const ray_packet_t &r, const float inv_d[3], const bvh_node_t *nodes, uint32_t root_index,
                                                   const tri_accel_t *tris, const uint32_t *tri_indices, int obj_index, hit_data_t &inter) {
    bool res = false;

    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack[stack_size++] = root_index;

    while (stack_size) {
        uint32_t cur = stack[--stack_size];

        if (!bbox_test(r.o, inv_d, inter.t, nodes[cur])) continue;

        if (!is_leaf_node(nodes[cur])) {
            stack[stack_size++] = far_child(r, nodes[cur]);
            stack[stack_size++] = near_child(r, nodes[cur]);
        } else {
            bool hit_found = IntersectTris_AnyHit(r, tris, &tri_indices[nodes[cur].prim_index & PRIM_INDEX_BITS], nodes[cur].prim_count, obj_index, inter);
            res |= hit_found;
            if (hit_found && (tris[inter.prim_indices[0]].ci & TRI_SOLID_BIT)) {
                break;
            }
            
        }
    }

    return res;
}

bool Ray::Ref::Traverse_MicroTree_WithStack_AnyHit(const ray_packet_t &r, const float inv_d[3], const mbvh_node_t *nodes, uint32_t root_index,
                                                   const tri_accel_t *tris, const uint32_t *tri_indices, int obj_index, hit_data_t &inter) {
    bool res = false;

    TraversalStack<MAX_STACK_SIZE> st;
    st.push(root_index, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > inter.t) continue;

TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            float dist[8];
            long mask = bbox_test_oct(r.o, inv_d, inter.t, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask); mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                long i2 = GetFirstBit(mask); mask = ClearBit(mask, i2);
                if (mask == 0) { // two boxes were hit
                    if (dist[i] < dist[i2]) {
                        st.push(nodes[cur.index].child[i2], dist[i2]);
                        cur.index = nodes[cur.index].child[i];
                    } else {
                        st.push(nodes[cur.index].child[i], dist[i]);
                        cur.index = nodes[cur.index].child[i2];
                    }
                    goto TRAVERSE;
                }

                st.push(nodes[cur.index].child[i], dist[i]);
                st.push(nodes[cur.index].child[i2], dist[i2]);

                i = GetFirstBit(mask); mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask); mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask); mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            bool hit_found = IntersectTris_AnyHit(r, tris, &tri_indices[nodes[cur.index].child[0] & PRIM_INDEX_BITS], nodes[cur.index].child[1], obj_index, inter);
            res |= hit_found;
            if (hit_found && (tris[inter.prim_indices[0]].ci & TRI_SOLID_BIT)) {
                break;
            }
        }
    }

    return res;
}

float Ray::Ref::BRDF_OrenNayar(const simd_fvec3 &L, const simd_fvec3 &I, const simd_fvec3 &N, const simd_fvec3 &T, float sigma) {
    float sigma_sqr = sigma * sigma;
    float A = 1.0f - (sigma_sqr / (2.0f * (sigma_sqr + 0.33f)));
    float B = (0.45f * sigma_sqr) / (sigma_sqr + 0.09f);
    float cos_theta_I = dot(L, N);
    float cos_theta_O = -dot(I, N);
    float sin_theta_I = std::sqrt(std::max(0.0f, 1.0f - (cos_theta_I * cos_theta_I)));
    float sin_theta_O = std::sqrt(std::max(0.0f, 1.0f - (cos_theta_O * cos_theta_O)));
    float cos_phi_I = dot(L, T);
    float cos_phi_O = -dot(I, T);
    float sin_phi_I = std::sqrt(std::max(0.0f, 1.0f - (cos_phi_I * cos_phi_I)));
    float sin_phi_O = std::sqrt(std::max(0.0f, 1.0f - (cos_phi_O * cos_phi_O)));
    float temp = std::max(0.0f, (cos_phi_I * cos_phi_O) + (sin_phi_I * sin_phi_O));
    float sin_alpha;
    float tan_beta;
    if (cos_theta_I > cos_theta_O) {
        sin_alpha = sin_theta_O;
        tan_beta = (sin_theta_I / cos_theta_I);
    } else {
        sin_alpha = sin_theta_I;
        tan_beta = (sin_theta_O / cos_theta_O);
    }
    float result = (A + (B * temp * sin_alpha * tan_beta));
    return clamp(result, 0.0f, 1.0f);
}

Ray::Ref::ray_packet_t Ray::Ref::TransformRay(const ray_packet_t &r, const float *xform) {
    ray_packet_t _r = r;

    _r.o[0] = xform[0] * r.o[0] + xform[4] * r.o[1] + xform[8] * r.o[2] + xform[12];
    _r.o[1] = xform[1] * r.o[0] + xform[5] * r.o[1] + xform[9] * r.o[2] + xform[13];
    _r.o[2] = xform[2] * r.o[0] + xform[6] * r.o[1] + xform[10] * r.o[2] + xform[14];

    _r.d[0] = xform[0] * r.d[0] + xform[4] * r.d[1] + xform[8] * r.d[2];
    _r.d[1] = xform[1] * r.d[0] + xform[5] * r.d[1] + xform[9] * r.d[2];
    _r.d[2] = xform[2] * r.d[0] + xform[6] * r.d[1] + xform[10] * r.d[2];

    return _r;
}

Ray::Ref::simd_fvec3 Ray::Ref::TransformPoint(const simd_fvec3 &p, const float *xform) {
    return simd_fvec3{ xform[0] * p[0] + xform[4] * p[1] + xform[8] * p[2] + xform[12],
                       xform[1] * p[0] + xform[5] * p[1] + xform[9] * p[2] + xform[13],
                       xform[2] * p[0] + xform[6] * p[1] + xform[10] * p[2] + xform[14] };
}

Ray::Ref::simd_fvec3 Ray::Ref::TransformNormal(const simd_fvec3 &n, const float *inv_xform) {
    return simd_fvec3{ inv_xform[0] * n[0] + inv_xform[1] * n[1] + inv_xform[2] * n[2],
                       inv_xform[4] * n[0] + inv_xform[5] * n[1] + inv_xform[6] * n[2],
                       inv_xform[8] * n[0] + inv_xform[9] * n[1] + inv_xform[10] * n[2] };
}

Ray::Ref::simd_fvec2 Ray::Ref::TransformUV(const simd_fvec2 &_uv, const simd_fvec2 &tex_atlas_size, const texture_t &t, int mip_level) {
    simd_fvec2 pos = { (float)t.pos[mip_level][0], (float)t.pos[mip_level][1] };
    simd_fvec2 size = { (float)((t.width & TEXTURE_WIDTH_BITS) >> mip_level), (float)(t.height >> mip_level) };
    simd_fvec2 uv = _uv - floor(_uv);
    simd_fvec2 res = pos + uv * size + 1.0f;
    res /= tex_atlas_size;
    return res;
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleNearest(const TextureAtlas &atlas, const texture_t &t, const simd_fvec2 &uvs, int lod) {
    simd_fvec2 atlas_size = { atlas.size_x(), atlas.size_y() };
    simd_fvec2 _uvs = TransformUV(uvs, atlas_size, t, lod);
    _uvs = _uvs * atlas_size - 0.5f;

    int page = t.page[lod];

    const pixel_color8_t &pix = atlas.Get(page, int(_uvs[0]), int(_uvs[1]));

    return simd_fvec4{ to_norm_float(pix.r), to_norm_float(pix.g), to_norm_float(pix.b), to_norm_float(pix.a) };
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleBilinear(const TextureAtlas &atlas, const texture_t &t, const simd_fvec2 &uvs, int lod) {
    simd_fvec2 atlas_size = { atlas.size_x(), atlas.size_y() };
    simd_fvec2 _uvs = TransformUV(uvs, atlas_size, t, lod);
    _uvs = _uvs * atlas_size - 0.5f;

    int page = t.page[lod];

    const pixel_color8_t &p00 = atlas.Get(page, int(_uvs[0]) + 0, int(_uvs[1]) + 0);
    const pixel_color8_t &p01 = atlas.Get(page, int(_uvs[0]) + 1, int(_uvs[1]) + 0);
    const pixel_color8_t &p10 = atlas.Get(page, int(_uvs[0]) + 0, int(_uvs[1]) + 1);
    const pixel_color8_t &p11 = atlas.Get(page, int(_uvs[0]) + 1, int(_uvs[1]) + 1);

    float kx = _uvs[0] - std::floor(_uvs[0]), ky = _uvs[1] - std::floor(_uvs[1]);

    const auto p0 = simd_fvec4{ p01.r * kx + p00.r * (1 - kx),
                                p01.g * kx + p00.g * (1 - kx),
                                p01.b * kx + p00.b * (1 - kx),
                                p01.a * kx + p00.a * (1 - kx) };

    const auto p1 = simd_fvec4{ p11.r * kx + p10.r * (1 - kx),
                                p11.g * kx + p10.g * (1 - kx),
                                p11.b * kx + p10.b * (1 - kx),
                                p11.a * kx + p10.a * (1 - kx) };

    const float k = 1.0f / 255.0f;
    return (p1 * ky + p0 * (1.0f - ky)) * k;
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleBilinear(const TextureAtlas &atlas, const simd_fvec2 &uvs, int page) {
    const pixel_color8_t &p00 = atlas.Get(page, int(uvs[0]) + 0, int(uvs[1]) + 0);
    const pixel_color8_t &p01 = atlas.Get(page, int(uvs[0]) + 1, int(uvs[1]) + 0);
    const pixel_color8_t &p10 = atlas.Get(page, int(uvs[0]) + 0, int(uvs[1]) + 1);
    const pixel_color8_t &p11 = atlas.Get(page, int(uvs[0]) + 1, int(uvs[1]) + 1);

    simd_fvec2 k = uvs - floor(uvs);
    
    const auto _p00 = simd_fvec4{ to_norm_float(p00.r), to_norm_float(p00.g), to_norm_float(p00.b), to_norm_float(p00.a) };
    const auto _p01 = simd_fvec4{ to_norm_float(p01.r), to_norm_float(p01.g), to_norm_float(p01.b), to_norm_float(p01.a) };
    const auto _p10 = simd_fvec4{ to_norm_float(p10.r), to_norm_float(p10.g), to_norm_float(p10.b), to_norm_float(p10.a) };
    const auto _p11 = simd_fvec4{ to_norm_float(p11.r), to_norm_float(p11.g), to_norm_float(p11.b), to_norm_float(p11.a) };

    const simd_fvec4 p0X = _p01 * k[0] + _p00 * (1 - k[0]);
    const simd_fvec4 p1X = _p11 * k[0] + _p10 * (1 - k[0]);

    return (p1X * k[1] + p0X * (1 - k[1]));
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleTrilinear(const TextureAtlas &atlas, const texture_t &t, const simd_fvec2 &uvs, float lod) {
    simd_fvec4 col1 = SampleBilinear(atlas, t, uvs, (int)std::floor(lod));
    simd_fvec4 col2 = SampleBilinear(atlas, t, uvs, (int)std::ceil(lod));

    const float k = lod - std::floor(lod);
    return col1 * (1 - k) + col2 * k;
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleAnisotropic(const TextureAtlas &atlas, const texture_t &t, const simd_fvec2 &uvs, const simd_fvec2 &duv_dx, const simd_fvec2 &duv_dy) {
    int width = int(t.width & TEXTURE_WIDTH_BITS);
    simd_fvec2 sz = { (float)width, (float)t.height };

    simd_fvec2 _duv_dx = abs(duv_dx * sz);
    simd_fvec2 _duv_dy = abs(duv_dy * sz);

    float l1 = length(_duv_dx);
    float l2 = length(_duv_dy);

    float lod, k;
    simd_fvec2 step;

    if (l1 <= l2) {
        lod = fast_log2(std::min(_duv_dx[0], _duv_dx[1]));
        k = l1 / l2;
        step = duv_dy;
    } else {
        lod = fast_log2(std::min(_duv_dy[0], _duv_dy[1]));
        k = l2 / l1;
        step = duv_dx;
    }

    lod = clamp(lod, 0.0f, float(MAX_MIP_LEVEL));

    simd_fvec2 _uvs = uvs - step * 0.5f;

    int num = (int)(2.0f / k);
    num = clamp(num, 1, 4);

    step = step / float(num);

    auto res = simd_fvec4{ 0.0f };

    int lod1 = (int)std::floor(lod);
    int lod2 = (int)std::ceil(lod);

    int page1 = t.page[lod1];
    int page2 = t.page[lod2];

    simd_fvec2 pos1 = simd_fvec2{ (float)t.pos[lod1][0], (float)t.pos[lod1][1] } + 0.5f;
    simd_fvec2 size1 = { (float)(width >> lod1), (float)(t.height >> lod1) };

    simd_fvec2 pos2 = simd_fvec2{ (float)t.pos[lod2][0], (float)t.pos[lod2][1] } + 0.5f;
    simd_fvec2 size2 = { (float)(width >> lod2), (float)(t.height >> lod2) };

    const float kz = lod - std::floor(lod);

    for (int i = 0; i < num; i++) {
        _uvs = _uvs - floor(_uvs);

        simd_fvec2 _uvs1 = pos1 + _uvs * size1;
        res += (1 - kz) * SampleBilinear(atlas, _uvs1, page1);

        if (kz > 0.0001f) {
            simd_fvec2 _uvs2 = pos2 + _uvs * size2;
            res += kz * SampleBilinear(atlas, _uvs2, page2);
        }

        _uvs = _uvs + step;
    }

    return res / float(num);
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleLatlong_RGBE(const TextureAtlas &atlas, const texture_t &t, const simd_fvec3 &dir) {
    float theta = std::acos(clamp(dir[1], -1.0f, 1.0f)) / PI;
    float r = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
    float u = 0.5f * std::acos(r > FLT_EPS ? clamp(dir[0] / r, -1.0f, 1.0f) : 0.0f) / PI;
    if (dir[2] < 0.0f) u = 1.0f - u;

    simd_fvec2 pos = { (float)t.pos[0][0], (float)t.pos[0][1] },
               size = { (float)((t.width & TEXTURE_WIDTH_BITS) ), (float)(t.height) };

    simd_fvec2 uvs = pos + simd_fvec2{ u, theta } * size + simd_fvec2{ 1.0f, 1.0f };

    const pixel_color8_t &p00 = atlas.Get(t.page[0], int(uvs[0] + 0), int(uvs[1] + 0));
    const pixel_color8_t &p01 = atlas.Get(t.page[0], int(uvs[0] + 1), int(uvs[1] + 0));
    const pixel_color8_t &p10 = atlas.Get(t.page[0], int(uvs[0] + 0), int(uvs[1] + 1));
    const pixel_color8_t &p11 = atlas.Get(t.page[0], int(uvs[0] + 1), int(uvs[1] + 1));

    simd_fvec2 k = uvs - floor(uvs);

    const simd_fvec4 _p00 = rgbe_to_rgb(p00);
    const simd_fvec4 _p01 = rgbe_to_rgb(p01);
    const simd_fvec4 _p10 = rgbe_to_rgb(p10);
    const simd_fvec4 _p11 = rgbe_to_rgb(p11);

    const simd_fvec4 p0X = _p01 * k[0] + _p00 * (1 - k[0]);
    const simd_fvec4 p1X = _p11 * k[0] + _p10 * (1 - k[0]);

    return (p1X * k[1] + p0X * (1 - k[1]));
}

float Ray::Ref::ComputeVisibility(const simd_fvec3 &p1, const simd_fvec3 &p2, const float *halton, const int hi, int rand_hash2,
                                  const scene_data_t &sc, uint32_t node_index, const TextureAtlas &tex_atlas) {
    simd_fvec3 dir = p2 - p1;
    float dist = length(dir);
    dir /= dist;

    ray_packet_t r;

    memcpy(&r.o[0], value_ptr(p1), 3 * sizeof(float));
    memcpy(&r.d[0], value_ptr(dir), 3 * sizeof(float));
    
    float visibility = 1.0f;

    while (dist > HIT_EPS) {
        hit_data_t sh_inter;
        sh_inter.t = dist;

        if (sc.mnodes) {
            Traverse_MacroTree_WithStack_AnyHit(r, sc.mnodes, node_index, sc.mesh_instances, sc.mi_indices, sc.meshes, sc.transforms, sc.tris, sc.tri_indices, sh_inter);
        } else {
            Traverse_MacroTree_WithStack_AnyHit(r, sc.nodes, node_index, sc.mesh_instances, sc.mi_indices, sc.meshes, sc.transforms, sc.tris, sc.tri_indices, sh_inter);
        }
        if (!sh_inter.mask_values[0]) break;

        const tri_accel_t &tri = sc.tris[sh_inter.prim_indices[0]];
        if (tri.ci & TRI_SOLID_BIT) {
            visibility = 0;
            break;
        }

        const material_t *mat = &sc.materials[tri.mi];

        const transform_t *tr = &sc.transforms[sc.mesh_instances[sh_inter.obj_indices[0]].tr_index];

        const vertex_t &v1 = sc.vertices[sc.vtx_indices[sh_inter.prim_indices[0] * 3 + 0]];
        const vertex_t &v2 = sc.vertices[sc.vtx_indices[sh_inter.prim_indices[0] * 3 + 1]];
        const vertex_t &v3 = sc.vertices[sc.vtx_indices[sh_inter.prim_indices[0] * 3 + 2]];

        const auto I = simd_fvec3(r.d);

        float w = 1.0f - sh_inter.u - sh_inter.v;
        //simd_fvec3 sh_N = simd_fvec3(v1.n) * w + simd_fvec3(v2.n) * sh_inter.u + simd_fvec3(v3.n) * sh_inter.v;
        simd_fvec2 sh_uvs = simd_fvec2(v1.t[0]) * w + simd_fvec2(v2.t[0]) * sh_inter.u + simd_fvec2(v3.t[0]) * sh_inter.v;

        simd_fvec3 sh_plane_N;
        ExtractPlaneNormal(tri, &sh_plane_N[0]);
        sh_plane_N = TransformNormal(sh_plane_N, tr->inv_xform);

        bool skip = false;

        bool is_backfacing = dot(sh_plane_N, I) < 0.0f;

        if (is_backfacing) {
            if (tri.back_mi == 0xffffffff) {
                skip = true;
            } else {
                mat = &sc.materials[tri.back_mi];
                sh_plane_N = -sh_plane_N;
            }
        }

        if (!skip) {
            int sh_rand_hash = hash(rand_hash2);
            float sh_rand_offset = construct_float(sh_rand_hash);

            float _sh_unused;
            float sh_r = std::modf(halton[hi] + sh_rand_offset, &_sh_unused);

            // resolve mix material
            while (mat->type == MixMaterial) {
                float mix_val = SampleBilinear(tex_atlas, sc.textures[mat->textures[MAIN_TEXTURE]], sh_uvs, 0)[0] * mat->strength;

                if (sh_r > mix_val) {
                    mat = &sc.materials[mat->textures[MIX_MAT1]];
                    sh_r = (sh_r - mix_val) / (1.0f - mix_val);
                } else {
                    mat = &sc.materials[mat->textures[MIX_MAT2]];
                    sh_r = sh_r / mix_val;
                }
            }

            if (mat->type != TransparentMaterial) {
                visibility = 0;
                break;
            }
        }

        float t = sh_inter.t + HIT_BIAS;
        r.o[0] += r.d[0] * t;
        r.o[1] += r.d[1] * t;
        r.o[2] += r.d[2] * t;
        dist -= t;
    }

    return visibility;
}

void Ray::Ref::AcumulateLightContribution(const light_t &l, const simd_fvec3 &I, const simd_fvec3 &P, const simd_fvec3 &N, const simd_fvec3 &B, const simd_fvec3 &plane_N,
                                          const scene_data_t &sc, uint32_t node_index, const TextureAtlas &tex_atlas,
                                          float sigma, const float *halton, const int hi, int rand_hash2, float rand_offset, float rand_offset2, simd_fvec3 &col) {
    simd_fvec3 L = P - simd_fvec3(l.pos);
    float distance = length(L);
    float d = std::max(distance - l.radius, 0.0f);
    L /= distance;

    float _unused;
    const float z = std::modf(halton[hi + 0] + rand_offset, &_unused);

    const float dir = std::sqrt(z);
    const float phi = 2 * PI * std::modf(halton[hi + 1] + rand_offset2, &_unused);

    const float cos_phi = std::cos(phi), sin_phi = std::sin(phi);

    simd_fvec3 TT = cross(L, B);
    simd_fvec3 BB = cross(L, TT);
    const simd_fvec3 V = dir * sin_phi * BB + std::sqrt(1.0f - dir) * L + dir * cos_phi * TT;

    L = normalize(simd_fvec3(l.pos) + V * l.radius - P);

    float denom = d / l.radius + 1.0f;
    float atten = 1.0f / (denom * denom);

    atten = (atten - LIGHT_ATTEN_CUTOFF / l.brightness) / (1.0f - LIGHT_ATTEN_CUTOFF);
    atten = std::max(atten, 0.0f);

    float _dot1 = std::max(dot(L, N), 0.0f);
    float _dot2 = dot(L, simd_fvec3{ l.dir });

    if (_dot1 > FLT_EPS && _dot2 > l.spot && (l.brightness * atten) > FLT_EPS) {
        float visibility = ComputeVisibility(P + HIT_BIAS * plane_N, simd_fvec3(l.pos), halton, hi, rand_hash2, sc, node_index, tex_atlas);
        col += simd_fvec3(l.col) * _dot1 * visibility * atten * BRDF_OrenNayar(L, I, N, B, sigma);
    }
}

Ray::Ref::simd_fvec3 Ray::Ref::ComputeDirectLighting(const simd_fvec3 &I, const simd_fvec3 &P, const simd_fvec3 &N, const simd_fvec3 &B, const simd_fvec3 &plane_N,
                                                     float sigma, const float *halton, const int hi, int rand_hash, int rand_hash2, float rand_offset, float rand_offset2,
                                                     const scene_data_t &sc, uint32_t node_index, uint32_t light_node_index, const TextureAtlas &tex_atlas) {
    unused(rand_hash);

    simd_fvec3 col = { 0.0f };

    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    if (light_node_index != 0xffffffff) {
        stack[stack_size++] = light_node_index;
    }

    if (sc.mnodes) {
        while (stack_size) {
            uint32_t cur = stack[--stack_size];

            if (!is_leaf_node(sc.mnodes[cur])) {
                bool res[8];
                for (int i = 0; i < 8; i++) {
                    res[i] = bbox_test_oct(value_ptr(P), sc.mnodes[cur], i);
                }

                for (int i = 0; i < 8; i++) {
                    if (!res[i]) continue;
                    stack[stack_size++] = sc.mnodes[cur].child[i];
                }
            } else {
                uint32_t prim_index = (sc.mnodes[cur].child[0] & PRIM_INDEX_BITS);
                for (uint32_t i = prim_index; i < prim_index + sc.mnodes[cur].child[1]; i++) {
                    const light_t &l = sc.lights[sc.li_indices[i]];
                    AcumulateLightContribution(l, I, P, N, B, plane_N, sc, node_index, tex_atlas, sigma, halton, hi, rand_hash2, rand_offset, rand_offset2, col);
                }
            }
        }
    } else {
        while (stack_size) {
            uint32_t cur = stack[--stack_size];

            if (!bbox_test(value_ptr(P), sc.nodes[cur])) continue;

            if (!is_leaf_node(sc.nodes[cur])) {
                stack[stack_size++] = sc.nodes[cur].left_child;
                stack[stack_size++] = (sc.nodes[cur].right_child & RIGHT_CHILD_BITS);
            } else {
                uint32_t prim_index = (sc.nodes[cur].prim_index & PRIM_INDEX_BITS);
                for (uint32_t i = prim_index; i < prim_index + sc.nodes[cur].prim_count; i++) {
                    const light_t &l = sc.lights[sc.li_indices[i]];
                    AcumulateLightContribution(l, I, P, N, B, plane_N, sc, node_index, tex_atlas, sigma, halton, hi, rand_hash2, rand_offset, rand_offset2, col);
                }
            }
        }
    }

    return col;
}

void Ray::Ref::ComputeDerivatives(const simd_fvec3 &I, float t, const simd_fvec3 &do_dx, const simd_fvec3 &do_dy, const simd_fvec3 &dd_dx, const simd_fvec3 &dd_dy,
                                  const vertex_t &v1, const vertex_t &v2, const vertex_t &v3, const simd_fvec3 &plane_N, derivatives_t &out_der) {
    // From 'Tracing Ray Differentials' [1999]

    float dot_I_N = -dot(I, plane_N);
    float inv_dot = std::abs(dot_I_N) < FLT_EPS ? 0.0f : 1.0f / dot_I_N;
    float dt_dx = dot(do_dx + t * dd_dx, plane_N) * inv_dot;
    float dt_dy = dot(do_dy + t * dd_dy, plane_N) * inv_dot;

    out_der.do_dx = (do_dx + t * dd_dx) + dt_dx * I;
    out_der.do_dy = (do_dy + t * dd_dy) + dt_dy * I;
    out_der.dd_dx = dd_dx;
    out_der.dd_dy = dd_dy;

    // From 'Physically Based Rendering: ...' book

    const simd_fvec2 duv13 = simd_fvec2(v1.t[0]) - simd_fvec2(v3.t[0]),
                     duv23 = simd_fvec2(v2.t[0]) - simd_fvec2(v3.t[0]);
    const simd_fvec3 dp13 = simd_fvec3(v1.p) - simd_fvec3(v3.p),
                     dp23 = simd_fvec3(v2.p) - simd_fvec3(v3.p);

    const float det_uv = duv13[0] * duv23[1] - duv13[1] * duv23[0];
    const float inv_det_uv = std::abs(det_uv) < FLT_EPS ? 0 : 1.0f / det_uv;
    const simd_fvec3 dpdu = (duv23[1] * dp13 - duv13[1] * dp23) * inv_det_uv;
    const simd_fvec3 dpdv = (-duv23[0] * dp13 + duv13[0] * dp23) * inv_det_uv;

    // System of equations
    simd_fvec2 A[2] = { { dpdu[0], dpdv[0] }, { dpdu[1], dpdv[1] } };
    simd_fvec2 Bx = { out_der.do_dx[0], out_der.do_dx[1] };
    simd_fvec2 By = { out_der.do_dy[0], out_der.do_dy[1] };

    if (std::abs(plane_N[0]) > std::abs(plane_N[1]) && std::abs(plane_N[0]) > std::abs(plane_N[2])) {
        A[0] = { dpdu[1], dpdv[1] };
        A[1] = { dpdu[2], dpdv[2] };
        Bx = { out_der.do_dx[1], out_der.do_dx[2] };
        By = { out_der.do_dy[1], out_der.do_dy[2] };
    } else if (std::abs(plane_N[1]) > std::abs(plane_N[2])) {
        A[1] = { dpdu[2], dpdv[2] };
        Bx = { out_der.do_dx[0], out_der.do_dx[2] };
        By = { out_der.do_dy[0], out_der.do_dy[2] };
    }

    // Kramer's rule
    const float det = A[0][0] * A[1][1] - A[0][1] * A[1][0];
    const float inv_det = std::abs(det) < FLT_EPS ? 0 : 1.0f / det;
    
    out_der.duv_dx = simd_fvec2{ A[1][1] * Bx[0] - A[0][1] * Bx[1], A[0][0] * Bx[1] - A[1][0] * Bx[0] } * inv_det;
    out_der.duv_dy = simd_fvec2{ A[1][1] * By[0] - A[0][1] * By[1], A[0][0] * By[1] - A[1][0] * By[0] } * inv_det;

    // Derivative for normal
    const simd_fvec3 dn1 = simd_fvec3(v1.n) - simd_fvec3(v3.n), dn2 = simd_fvec3(v2.n) - simd_fvec3(v3.n);
    const simd_fvec3 dndu = (duv23[1] * dn1 - duv13[1] * dn2) * inv_det_uv;
    const simd_fvec3 dndv = (-duv23[0] * dn1 + duv13[0] * dn2) * inv_det_uv;

    out_der.dndx = dndu * out_der.duv_dx[0] + dndv * out_der.duv_dx[1];
    out_der.dndy = dndu * out_der.duv_dy[0] + dndv * out_der.duv_dy[1];

    out_der.ddn_dx = dot(dd_dx, plane_N) + dot(I, out_der.dndx);
    out_der.ddn_dy = dot(dd_dy, plane_N) + dot(I, out_der.dndy);
}


Ray::pixel_color_t Ray::Ref::ShadeSurface(const pass_info_t &pi, const hit_data_t &inter, const ray_packet_t &ray, const float *halton,
                                          const scene_data_t &sc, uint32_t node_index, uint32_t light_node_index, const TextureAtlas &tex_atlas,
                                          ray_packet_t *out_secondary_rays, int *out_secondary_rays_count) {
    if (!inter.mask_values[0]) {
        simd_fvec4 env_col = { 0.0f };
        if (pi.should_add_environment()) {
            env_col = SampleLatlong_RGBE(tex_atlas, sc.textures[sc.env->env_map], simd_fvec3{ ray.d });
            if (sc.env->env_clamp > FLT_EPS) {
                env_col = min(env_col, simd_fvec4{ sc.env->env_clamp });
            }
            env_col[3] = 1.0f;
        }
        return Ray::pixel_color_t{ ray.c[0] * env_col[0] * sc.env->env_col[0],
                                   ray.c[1] * env_col[1] * sc.env->env_col[1],
                                   ray.c[2] * env_col[2] * sc.env->env_col[2], env_col[3] };
    }

    const auto I = simd_fvec3(ray.d);
    const auto P = simd_fvec3(ray.o) + inter.t * I;

    const tri_accel_t &tri = sc.tris[inter.prim_indices[0]];

    const material_t *mat = &sc.materials[tri.mi];

    const transform_t *tr = &sc.transforms[sc.mesh_instances[inter.obj_indices[0]].tr_index];

    const vertex_t &v1 = sc.vertices[sc.vtx_indices[inter.prim_indices[0] * 3 + 0]];
    const vertex_t &v2 = sc.vertices[sc.vtx_indices[inter.prim_indices[0] * 3 + 1]];
    const vertex_t &v3 = sc.vertices[sc.vtx_indices[inter.prim_indices[0] * 3 + 2]];

    float w = 1.0f - inter.u - inter.v;
    simd_fvec3 N = simd_fvec3(v1.n) * w + simd_fvec3(v2.n) * inter.u + simd_fvec3(v3.n) * inter.v;
    simd_fvec2 uvs = simd_fvec2(v1.t[0]) * w + simd_fvec2(v2.t[0]) * inter.u + simd_fvec2(v3.t[0]) * inter.v;

    simd_fvec3 plane_N;
    ExtractPlaneNormal(tri, &plane_N[0]);
    plane_N = TransformNormal(plane_N, tr->inv_xform);

    bool is_backfacing = dot(plane_N, I) > 0.0f;

    if (is_backfacing) {
        if (tri.back_mi == 0xffffffff) {
            return pixel_color_t{ 0.0f, 0.0f, 0.0f, 0.0f };
        } else {
            mat = &sc.materials[tri.back_mi];
            plane_N = -plane_N;
            N = -N;
        }
    }

    derivatives_t surf_der;
    ComputeDerivatives(I, inter.t, ray.do_dx, ray.do_dy, ray.dd_dx, ray.dd_dy, v1, v2, v3, plane_N, surf_der);

    // used to randomize halton sequence among pixels
    int rand_hash = hash(pi.rand_index),
        rand_hash2 = hash(rand_hash),
        rand_hash3 = hash(rand_hash2);
    float rand_offset = construct_float(rand_hash),
          rand_offset2 = construct_float(rand_hash2),
          rand_offset3 = construct_float(rand_hash3);

    const int hi = (pi.iteration & (HALTON_SEQ_LEN - 1)) * HALTON_COUNT + pi.bounce * 2;

    float _unused;
    float mix_rand = std::modf(halton[hi] + rand_offset, &_unused);

    // resolve mix material
    while (mat->type == MixMaterial) {
        float mix_val = SampleBilinear(tex_atlas, sc.textures[mat->textures[MAIN_TEXTURE]], uvs, 0)[0] * mat->strength;

        // shlick fresnel
        const float mix_ior = is_backfacing ? mat->ext_ior : mat->int_ior;

        float R0 = (ray.ior - mix_ior) / (ray.ior + mix_ior);
        R0 *= R0;

        float RR;

        if (ray.ior > mix_ior) {
            float eta = ray.ior / mix_ior;
            float cosi = -dot(I, N);
            float cost2 = 1.0f - eta * eta * (1.0f - cosi * cosi);

            if (cost2 >= 0.0f) {
                float m = eta * cosi - std::sqrt(cost2);
                simd_fvec3 V = eta * I + m * N;
                RR = R0 + (1.0f - R0) * pow5(1.0f + dot(V, N));
            } else {
                RR = 1.0f;
            }
        } else {
            RR = R0 + (1.0f - R0) * pow5(1.0f + dot(I, N));
        }

        mix_val *= clamp(RR, 0.0f, 1.0f);

        if (mix_rand > mix_val) {
            mat = &sc.materials[mat->textures[MIX_MAT1]];
            mix_rand = (mix_rand - mix_val) / (1.0f - mix_val);
        } else {
            mat = &sc.materials[mat->textures[MIX_MAT2]];
            mix_rand = mix_rand / mix_val;
        }
    }

    // apply normal map
    simd_fvec3 B = simd_fvec3(v1.b) * w + simd_fvec3(v2.b) * inter.u + simd_fvec3(v3.b) * inter.v;
    simd_fvec3 T = cross(B, N);

    simd_fvec4 normals = SampleBilinear(tex_atlas, sc.textures[mat->textures[NORMALS_TEXTURE]], uvs, 0);
    normals = normals * 2.0f - 1.0f;
    N = normals[0] * B + normals[2] * N + normals[1] * T;

    N = TransformNormal(N, tr->inv_xform);
    B = TransformNormal(B, tr->inv_xform);
    T = TransformNormal(T, tr->inv_xform);

    // sample main texture
    const texture_t &main_texture = sc.textures[mat->textures[MAIN_TEXTURE]];
    const float albedo_lod = get_texture_lod(main_texture, surf_der.duv_dx, surf_der.duv_dy);
    simd_fvec4 albedo = SampleBilinear(tex_atlas, main_texture, uvs, (int)albedo_lod);
    if (main_texture.width & TEXTURE_SRGB_BIT) {
        albedo = srgb_to_rgb(albedo);
    }
    albedo[0] *= mat->main_color[0];
    albedo[1] *= mat->main_color[1];
    albedo[2] *= mat->main_color[2];

    simd_fvec3 col = { 0.0f };

    const int diff_depth = ray.ray_depth & 0x000000ff;
    const int gloss_depth = (ray.ray_depth >> 8) & 0x000000ff;
    const int refr_depth = (ray.ray_depth >> 16) & 0x000000ff;
    const int transp_depth = (ray.ray_depth >> 24) & 0x000000ff;
    const int total_depth = diff_depth + gloss_depth + refr_depth + transp_depth;

    const bool cant_terminate = total_depth < pi.settings.termination_start_depth;

    // Evaluate materials
    if (mat->type == DiffuseMaterial) {
        if (pi.should_add_direct_light()) {
            col = ComputeDirectLighting(I, P, N, B, plane_N, mat->roughness, halton, hi, rand_hash, rand_hash2, rand_offset, rand_offset2,
                                        sc, node_index, light_node_index, tex_atlas);

            if (pi.should_consider_albedo()) {
                col *= simd_fvec3(&albedo[0]);
            }
        }

        if (diff_depth < pi.settings.max_diff_depth && total_depth < pi.settings.max_total_depth) {
            const float u1 = std::modf(halton[hi + 0] + rand_offset, &_unused);
            const float u2 = std::modf(halton[hi + 1] + rand_offset2, &_unused);

            const float phi = 2 * PI * u2;

            const float cos_phi = std::cos(phi);
            const float sin_phi = std::sin(phi);

            simd_fvec3 V;
            float weight = 1.0f;

            if (pi.use_uniform_sampling()) {
                const float dir = std::sqrt(1.0f - u1 * u1);
                V = dir * sin_phi * B + u1 * N + dir * cos_phi * T;
                weight = 2 * u1;
            } else {
                const float dir = std::sqrt(u1);
                V = dir * sin_phi * B + std::sqrt(1.0f - u1) * N + dir * cos_phi * T;
            }

            weight *= BRDF_OrenNayar(V, I, N, B, mat->roughness);

            ray_packet_t r;

            r.xy = ray.xy;
            r.ior = ray.ior;
            r.ray_depth = ray.ray_depth + 0x00000001;

            memcpy(&r.o[0], value_ptr(P + HIT_BIAS * plane_N), 3 * sizeof(float));
            memcpy(&r.d[0], value_ptr(V), 3 * sizeof(float));

            r.c[0] = ray.c[0] * weight; r.c[1] = ray.c[1] * weight; r.c[2] = ray.c[2] * weight;

            if (pi.should_consider_albedo()) {
                r.c[0] *= albedo[0]; r.c[1] *= albedo[1]; r.c[2] *= albedo[2];
            }

            memcpy(&r.do_dx[0], value_ptr(surf_der.do_dx), 3 * sizeof(float));
            memcpy(&r.do_dy[0], value_ptr(surf_der.do_dy), 3 * sizeof(float));

            memcpy(&r.dd_dx[0], value_ptr(surf_der.dd_dx - 2 * (dot(I, plane_N) * surf_der.dndx + surf_der.ddn_dx * plane_N)), 3 * sizeof(float));
            memcpy(&r.dd_dy[0], value_ptr(surf_der.dd_dy - 2 * (dot(I, plane_N) * surf_der.dndy + surf_der.ddn_dy * plane_N)), 3 * sizeof(float));

            const float lum = std::max(r.c[0], std::max(r.c[1], r.c[2]));
            const float p = std::modf(halton[hi + 0] + rand_offset3, &_unused);
            const float q = cant_terminate ? 0.0f : std::max(0.05f, 1.0f - lum);
            if (p >= q) {
                r.c[0] /= 1.0f - q; r.c[1] /= 1.0f - q; r.c[2] /= 1.0f - q;
                const int index = (*out_secondary_rays_count)++;
                out_secondary_rays[index] = r;
            }
        }
    } else if (mat->type == GlossyMaterial) {
        if (gloss_depth < pi.settings.max_glossy_depth && total_depth < pi.settings.max_total_depth) {
            simd_fvec3 V = reflect(I, N);

            const float h = 1.0f - std::cos(0.5f * PI * mat->roughness * mat->roughness);
            const float z = h * std::modf(halton[hi + 0] + rand_offset, &_unused);

            const float dir = std::sqrt(z);
            const float phi = 2 * PI * std::modf(halton[hi + 1] + rand_offset2, &_unused);
            const float cos_phi = std::cos(phi);
            const float sin_phi = std::sin(phi);

            simd_fvec3 TT = cross(V, B);
            simd_fvec3 BB = cross(V, TT);

            if (dot(V, plane_N) > 0) {
                V = dir * sin_phi * BB + std::sqrt(1.0f - dir) * V + dir * cos_phi * TT;
            } else {
                V = -dir * sin_phi * BB + std::sqrt(1.0f - dir) * V - dir * cos_phi * TT;
            }

            ray_packet_t r;

            r.xy = ray.xy;
            r.ior = ray.ior;
            r.ray_depth = ray.ray_depth + 0x00000100;

            memcpy(&r.o[0], value_ptr(P + HIT_BIAS * plane_N), 3 * sizeof(float));
            memcpy(&r.d[0], value_ptr(V), 3 * sizeof(float));
            memcpy(&r.c[0], &ray.c[0], 3 * sizeof(float));

            if (pi.should_consider_albedo()) {
                r.c[0] *= albedo[0]; r.c[1] *= albedo[1]; r.c[2] *= albedo[2];
            }

            memcpy(&r.do_dx[0], value_ptr(surf_der.do_dx), 3 * sizeof(float));
            memcpy(&r.do_dy[0], value_ptr(surf_der.do_dy), 3 * sizeof(float));

            memcpy(&r.dd_dx[0], value_ptr(surf_der.dd_dx - 2 * (dot(I, plane_N) * surf_der.dndx + surf_der.ddn_dx * plane_N)), 3 * sizeof(float));
            memcpy(&r.dd_dy[0], value_ptr(surf_der.dd_dy - 2 * (dot(I, plane_N) * surf_der.dndy + surf_der.ddn_dy * plane_N)), 3 * sizeof(float));

            const float lum = std::max(r.c[0], std::max(r.c[1], r.c[2]));
            const float p = std::modf(halton[hi + 0] + rand_offset3, &_unused);
            const float q = cant_terminate ? 0.0f : std::max(0.05f, 1.0f - lum);
            if (p >= q) {
                r.c[0] /= 1.0f - q; r.c[1] /= 1.0f - q; r.c[2] /= 1.0f - q;
                const int index = (*out_secondary_rays_count)++;
                out_secondary_rays[index] = r;
            }
        }
    } else if (mat->type == RefractiveMaterial) {
        if (refr_depth < pi.settings.max_refr_depth && total_depth < pi.settings.max_total_depth) {
            float ior = is_backfacing ? mat->ext_ior : mat->int_ior;
            float eta = ray.ior / ior;
            float cosi = -dot(I, N);
            float cost2 = 1.0f - eta * eta * (1.0f - cosi * cosi);
            if (cost2 < 0) return pixel_color_t{ 0.0f, 0.0f, 0.0f, 1.0f };
            float m = eta * cosi - std::sqrt(cost2);
            simd_fvec3 V = eta * I + m * N;

            const float z = 1.0f - halton[hi + 0] * mat->roughness;
            const float temp = std::sqrt(1.0f - z * z);

            const float phi = halton[(((hash(hi) + pi.iteration) & (HALTON_SEQ_LEN - 1)) * HALTON_COUNT + pi.bounce * 2) + 0] * 2 * PI;
            const float cos_phi = std::cos(phi);
            const float sin_phi = std::sin(phi);

            simd_fvec3 TT = normalize(cross(V, B));
            simd_fvec3 BB = normalize(cross(V, TT));
            V = temp * sin_phi * BB + z * V + temp * cos_phi * TT;

            //////////////////

            float k = (eta - eta * eta * dot(I, plane_N) / dot(V, plane_N));
            float dmdx = k * surf_der.ddn_dx;
            float dmdy = k * surf_der.ddn_dy;

            ray_packet_t r;

            r.xy = ray.xy;
            r.ior = ior;
            r.ray_depth = ray.ray_depth + 0x00010000;

            memcpy(&r.o[0], value_ptr(P + HIT_BIAS * I), 3 * sizeof(float));
            memcpy(&r.d[0], value_ptr(V), 3 * sizeof(float));
            memcpy(&r.c[0], value_ptr(simd_fvec3(ray.c) * z), 3 * sizeof(float));

            memcpy(&r.do_dx[0], value_ptr(surf_der.do_dx), 3 * sizeof(float));
            memcpy(&r.do_dy[0], value_ptr(surf_der.do_dy), 3 * sizeof(float));

            memcpy(&r.dd_dx[0], value_ptr(eta * surf_der.dd_dx - (m * surf_der.dndx + dmdx * plane_N)), 3 * sizeof(float));
            memcpy(&r.dd_dy[0], value_ptr(eta * surf_der.dd_dy - (m * surf_der.dndy + dmdy * plane_N)), 3 * sizeof(float));

            const float lum = std::max(r.c[0], std::max(r.c[1], r.c[2]));
            const float p = std::modf(halton[hi + 0] + rand_offset3, &_unused);
            const float q = cant_terminate ? 0.0f : std::max(0.05f, 1.0f - lum);
            if (p >= q) {
                r.c[0] /= 1.0f - q; r.c[1] /= 1.0f - q; r.c[2] /= 1.0f - q;
                const int index = (*out_secondary_rays_count)++;
                out_secondary_rays[index] = r;
            }
        }
    } else if (mat->type == EmissiveMaterial) {
        col = mat->strength * simd_fvec3(&albedo[0]);
    } else if (mat->type == TransparentMaterial) {
        if (transp_depth < pi.settings.max_transp_depth && total_depth < pi.settings.max_total_depth) {
            ray_packet_t r;

            r.xy = ray.xy;
            r.ior = ray.ior;
            r.ray_depth = ray.ray_depth + 0x01000000;

            memcpy(&r.o[0], value_ptr(P + HIT_BIAS * I), 3 * sizeof(float));
            memcpy(&r.d[0], &ray.d[0], 3 * sizeof(float));
            memcpy(&r.c[0], &ray.c[0], 3 * sizeof(float));

            memcpy(&r.do_dx[0], &ray.do_dx[0], 3 * sizeof(float));
            memcpy(&r.do_dy[0], &ray.do_dy[0], 3 * sizeof(float));

            memcpy(&r.dd_dx[0], &ray.dd_dx[0], 3 * sizeof(float));
            memcpy(&r.dd_dy[0], &ray.dd_dy[0], 3 * sizeof(float));

            const float lum = std::max(r.c[0], std::max(r.c[1], r.c[2]));
            const float p = std::modf(halton[hi + 0] + rand_offset3, &_unused);
            const float q = cant_terminate ? 0.0f : std::max(0.05f, 1.0f - lum);
            if (p >= q) {
                r.c[0] /= 1.0f - q; r.c[1] /= 1.0f - q; r.c[2] /= 1.0f - q;
                const int index = (*out_secondary_rays_count)++;
                out_secondary_rays[index] = r;
            }
        }
    }

    return pixel_color_t{ ray.c[0] * col[0], ray.c[1] * col[1], ray.c[2] * col[2], 1.0f };
}
