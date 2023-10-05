﻿#include "CoreRef.h"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <tuple>

#include "TextureStorageCPU.h"

//
// Useful macros for debugging
//
#define USE_VNDF_GGX_SAMPLING 1
#define USE_NEE 1
#define USE_PATH_TERMINATION 1
#define USE_HIERARCHICAL_NEE 1
#define VECTORIZE_BBOX_INTERSECTION 1
#define VECTORIZE_TRI_INTERSECTION 1
// #define FORCE_TEXTURE_LOD 0
#define USE_STOCH_TEXTURE_FILTERING 1
#define USE_SAFE_MATH 1

namespace Ray {
#ifndef RAY_EXCHANGE_DEFINED
template <class T, class U = T> T exchange(T &obj, U &&new_value) {
    T old_value = std::move(obj);
    obj = std::forward<U>(new_value);
    return old_value;
}
#define RAY_EXCHANGE_DEFINED
#endif

namespace Ref {
#define sign_of(f) (((f) >= 0) ? 1 : -1)
#define dot(x, y) ((x)[0] * (y)[0] + (x)[1] * (y)[1] + (x)[2] * (y)[2])
force_inline void IntersectTri(const float ro[3], const float rd[3], const tri_accel_t &tri, const uint32_t prim_index,
                               hit_data_t &inter) {
    const float det = dot(rd, tri.n_plane);
    const float dett = tri.n_plane[3] - dot(ro, tri.n_plane);
    if (det == 0.0f || sign_of(dett) != sign_of(det * inter.t - dett)) {
        return;
    }

    const float p[3] = {det * ro[0] + dett * rd[0], det * ro[1] + dett * rd[1], det * ro[2] + dett * rd[2]};

    const float detu = dot(p, tri.u_plane) + det * tri.u_plane[3];
    if (sign_of(detu) != sign_of(det - detu)) {
        return;
    }

    const float detv = dot(p, tri.v_plane) + det * tri.v_plane[3];
    if (sign_of(detv) != sign_of(det - detu - detv)) {
        return;
    }

    const float rdet = (1.0f / det);

    inter.mask = -1;
    inter.prim_index = (det < 0.0f) ? int(prim_index) : -int(prim_index) - 1;
    inter.t = dett * rdet;
    inter.u = detu * rdet;
    inter.v = detv * rdet;
}
#undef dot
#undef sign_of

force_inline void IntersectTri(const float ro[3], const float rd[3], const mtri_accel_t &tri, const uint32_t prim_index,
                               hit_data_t &inter) {
#if VECTORIZE_TRI_INTERSECTION
    simd_ivec4 _mask = 0, _prim_index;
    simd_fvec4 _t = inter.t, _u, _v;
    for (int i = 0; i < 8; i += 4) {
        simd_fvec4 det = rd[0] * simd_fvec4{&tri.n_plane[0][i], simd_mem_aligned} +
                         rd[1] * simd_fvec4{&tri.n_plane[1][i], simd_mem_aligned} +
                         rd[2] * simd_fvec4{&tri.n_plane[2][i], simd_mem_aligned};
        const simd_fvec4 dett = simd_fvec4{&tri.n_plane[3][i], simd_mem_aligned} -
                                ro[0] * simd_fvec4{&tri.n_plane[0][i], simd_mem_aligned} -
                                ro[1] * simd_fvec4{&tri.n_plane[1][i], simd_mem_aligned} -
                                ro[2] * simd_fvec4{&tri.n_plane[2][i], simd_mem_aligned};

        // compare sign bits
        simd_ivec4 is_active_lane = ~srai(simd_cast(dett ^ (det * _t - dett)), 31);
        if (is_active_lane.all_zeros()) {
            continue;
        }

        const simd_fvec4 p[3] = {det * ro[0] + dett * rd[0], det * ro[1] + dett * rd[1], det * ro[2] + dett * rd[2]};

        const simd_fvec4 detu = p[0] * simd_fvec4{&tri.u_plane[0][i], simd_mem_aligned} +
                                p[1] * simd_fvec4{&tri.u_plane[1][i], simd_mem_aligned} +
                                p[2] * simd_fvec4{&tri.u_plane[2][i], simd_mem_aligned} +
                                det * simd_fvec4{&tri.u_plane[3][i], simd_mem_aligned};

        // compare sign bits
        is_active_lane &= ~srai(simd_cast(detu ^ (det - detu)), 31);
        if (is_active_lane.all_zeros()) {
            continue;
        }

        const simd_fvec4 detv = p[0] * simd_fvec4{&tri.v_plane[0][i], simd_mem_aligned} +
                                p[1] * simd_fvec4{&tri.v_plane[1][i], simd_mem_aligned} +
                                p[2] * simd_fvec4{&tri.v_plane[2][i], simd_mem_aligned} +
                                det * simd_fvec4{&tri.v_plane[3][i], simd_mem_aligned};

        // compare sign bits
        is_active_lane &= ~srai(simd_cast(detv ^ (det - detu - detv)), 31);
        if (is_active_lane.all_zeros()) {
            continue;
        }

        where(~is_active_lane, det) = FLT_EPS;
        const simd_fvec4 rdet = (1.0f / det);

        simd_ivec4 prim = -(int(prim_index) + i + simd_ivec4{0, 1, 2, 3}) - 1;
        where(det < 0.0f, prim) = int(prim_index) + i + simd_ivec4{0, 1, 2, 3};

        _mask |= is_active_lane;
        where(is_active_lane, _prim_index) = prim;
        where(is_active_lane, _t) = dett * rdet;
        where(is_active_lane, _u) = detu * rdet;
        where(is_active_lane, _v) = detv * rdet;
    }

    const float min_t = fminf(_t.get<0>(), fminf(_t.get<1>(), fminf(_t.get<2>(), _t.get<3>())));
    _mask &= simd_cast(_t == min_t);

    const long mask = _mask.movemask();
    if (mask) {
        const long i = GetFirstBit(mask);

        inter.mask = -1;
        inter.prim_index = _prim_index[i];
        inter.t = _t[i];
        inter.u = _u[i];
        inter.v = _v[i];
    }
#else
#define _sign_of(f) (((f) >= 0) ? 1 : -1)
    for (int i = 0; i < 8; ++i) {
        const float det = rd[0] * tri.n_plane[0][i] + rd[1] * tri.n_plane[1][i] + rd[2] * tri.n_plane[2][i];
        const float dett =
            tri.n_plane[3][i] - ro[0] * tri.n_plane[0][i] - ro[1] * tri.n_plane[1][i] - ro[2] * tri.n_plane[2][i];
        if (_sign_of(dett) != _sign_of(det * inter.t - dett)) {
            continue;
        }

        const float p[3] = {det * ro[0] + dett * rd[0], det * ro[1] + dett * rd[1], det * ro[2] + dett * rd[2]};

        const float detu =
            p[0] * tri.u_plane[0][i] + p[1] * tri.u_plane[1][i] + p[2] * tri.u_plane[2][i] + det * tri.u_plane[3][i];
        if (_sign_of(detu) != _sign_of(det - detu)) {
            continue;
        }

        const float detv =
            p[0] * tri.v_plane[0][i] + p[1] * tri.v_plane[1][i] + p[2] * tri.v_plane[2][i] + det * tri.v_plane[3][i];
        if (_sign_of(detv) != _sign_of(det - detu - detv)) {
            continue;
        }

        const float rdet = (1.0f / det);

        inter.mask = 0xffffffff;
        inter.prim_index = (det < 0.0f) ? int(prim_index + i) : -int(prim_index + i) - 1;
        inter.t = dett * rdet;
        inter.u = detu * rdet;
        inter.v = detv * rdet;
    }
#undef _sign_of
#endif
}

force_inline uint32_t near_child(const float rd[3], const bvh_node_t &node) {
    return rd[node.prim_count >> 30] < 0 ? (node.right_child & RIGHT_CHILD_BITS) : node.left_child;
}

force_inline uint32_t far_child(const float rd[3], const bvh_node_t &node) {
    return rd[node.prim_count >> 30] < 0 ? node.left_child : (node.right_child & RIGHT_CHILD_BITS);
}

force_inline uint32_t other_child(const bvh_node_t &node, const uint32_t cur_child) {
    return (node.left_child == cur_child) ? (node.right_child & RIGHT_CHILD_BITS) : node.left_child;
}

force_inline bool is_leaf_node(const bvh_node_t &node) { return (node.prim_index & LEAF_NODE_BIT) != 0; }

force_inline bool is_leaf_node(const wbvh_node_t &node) { return (node.child[0] & LEAF_NODE_BIT) != 0; }

force_inline bool bbox_test(const float o[3], const float inv_d[3], const float t, const float bbox_min[3],
                            const float bbox_max[3]) {
    float lo_x = inv_d[0] * (bbox_min[0] - o[0]);
    float hi_x = inv_d[0] * (bbox_max[0] - o[0]);
    if (lo_x > hi_x) {
        const float tmp = lo_x;
        lo_x = hi_x;
        hi_x = tmp;
    }

    float lo_y = inv_d[1] * (bbox_min[1] - o[1]);
    float hi_y = inv_d[1] * (bbox_max[1] - o[1]);
    if (lo_y > hi_y) {
        const float tmp = lo_y;
        lo_y = hi_y;
        hi_y = tmp;
    }

    float lo_z = inv_d[2] * (bbox_min[2] - o[2]);
    float hi_z = inv_d[2] * (bbox_max[2] - o[2]);
    if (lo_z > hi_z) {
        const float tmp = lo_z;
        lo_z = hi_z;
        hi_z = tmp;
    }

    float tmin = lo_x > lo_y ? lo_x : lo_y;
    if (lo_z > tmin) {
        tmin = lo_z;
    }
    float tmax = hi_x < hi_y ? hi_x : hi_y;
    if (hi_z < tmax) {
        tmax = hi_z;
    }
    tmax *= 1.00000024f;

    return tmin <= tmax && tmin <= t && tmax > 0;
}

force_inline bool bbox_test(const float p[3], const float bbox_min[3], const float bbox_max[3]) {
    return p[0] >= bbox_min[0] && p[0] <= bbox_max[0] && p[1] >= bbox_min[1] && p[1] <= bbox_max[1] &&
           p[2] >= bbox_min[2] && p[2] <= bbox_max[2];
}

force_inline bool bbox_test(const float o[3], const float inv_d[3], const float t, const bvh_node_t &node) {
    return bbox_test(o, inv_d, t, node.bbox_min, node.bbox_max);
}

force_inline bool bbox_test(const float p[3], const bvh_node_t &node) {
    return bbox_test(p, node.bbox_min, node.bbox_max);
}

force_inline long bbox_test_oct(const float p[3], const wbvh_node_t &node) {
    long mask = 0;
    UNROLLED_FOR_R(i, 2, { // NOLINT
        const simd_fvec4 fmask = (simd_fvec4{&node.bbox_min[0][4 * i], simd_mem_aligned} <= p[0]) &
                                 (simd_fvec4{&node.bbox_min[1][4 * i], simd_mem_aligned} <= p[1]) &
                                 (simd_fvec4{&node.bbox_min[2][4 * i], simd_mem_aligned} <= p[2]) &
                                 (simd_fvec4{&node.bbox_max[0][4 * i], simd_mem_aligned} >= p[0]) &
                                 (simd_fvec4{&node.bbox_max[1][4 * i], simd_mem_aligned} >= p[1]) &
                                 (simd_fvec4{&node.bbox_max[2][4 * i], simd_mem_aligned} >= p[2]);
        mask <<= 4;
        mask |= simd_cast(fmask).movemask();
    })
    return mask;
}

force_inline void bbox_test_oct(const float o[3], const float inv_d[3], const wbvh_node_t &node, int res[8],
                                float dist[8]){
    UNROLLED_FOR(i, 8,
                 { // NOLINT
                     float lo_x = inv_d[0] * (node.bbox_min[0][i] - o[0]);
                     float hi_x = inv_d[0] * (node.bbox_max[0][i] - o[0]);
                     if (lo_x > hi_x) {
                         const float tmp = lo_x;
                         lo_x = hi_x;
                         hi_x = tmp;
                     }

                     float lo_y = inv_d[1] * (node.bbox_min[1][i] - o[1]);
                     float hi_y = inv_d[1] * (node.bbox_max[1][i] - o[1]);
                     if (lo_y > hi_y) {
                         const float tmp = lo_y;
                         lo_y = hi_y;
                         hi_y = tmp;
                     }

                     float lo_z = inv_d[2] * (node.bbox_min[2][i] - o[2]);
                     float hi_z = inv_d[2] * (node.bbox_max[2][i] - o[2]);
                     if (lo_z > hi_z) {
                         const float tmp = lo_z;
                         lo_z = hi_z;
                         hi_z = tmp;
                     }

                     float tmin = lo_x > lo_y ? lo_x : lo_y;
                     if (lo_z > tmin) {
                         tmin = lo_z;
                     }
                     float tmax = hi_x < hi_y ? hi_x : hi_y;
                     if (hi_z < tmax) {
                         tmax = hi_z;
                     }
                     tmax *= 1.00000024f;

                     dist[i] = tmin;
                     res[i] = (tmin <= tmax && tmax > 0) ? 1 : 0;
                 }) // NOLINT
}

force_inline long bbox_test_oct(const float o[3], const float inv_d[3], const float t, const wbvh_node_t &node,
                                float out_dist[8]) {
    long mask = 0;
#if VECTORIZE_BBOX_INTERSECTION
    simd_fvec4 lo, hi, tmin, tmax;
    UNROLLED_FOR_R(i, 2, { // NOLINT
        lo = inv_d[0] * (simd_fvec4{&node.bbox_min[0][4 * i], simd_mem_aligned} - o[0]);
        hi = inv_d[0] * (simd_fvec4{&node.bbox_max[0][4 * i], simd_mem_aligned} - o[0]);
        tmin = min(lo, hi);
        tmax = max(lo, hi);

        lo = inv_d[1] * (simd_fvec4{&node.bbox_min[1][4 * i], simd_mem_aligned} - o[1]);
        hi = inv_d[1] * (simd_fvec4{&node.bbox_max[1][4 * i], simd_mem_aligned} - o[1]);
        tmin = max(tmin, min(lo, hi));
        tmax = min(tmax, max(lo, hi));

        lo = inv_d[2] * (simd_fvec4{&node.bbox_min[2][4 * i], simd_mem_aligned} - o[2]);
        hi = inv_d[2] * (simd_fvec4{&node.bbox_max[2][4 * i], simd_mem_aligned} - o[2]);
        tmin = max(tmin, min(lo, hi));
        tmax = min(tmax, max(lo, hi));
        tmax *= 1.00000024f;

        const simd_fvec4 fmask = (tmin <= tmax) & (tmin <= t) & (tmax > 0.0f);
        mask <<= 4;
        mask |= simd_cast(fmask).movemask();
        tmin.store_to(&out_dist[4 * i], simd_mem_aligned);
    }) // NOLINT
#else
    UNROLLED_FOR(i, 8, { // NOLINT
        float lo_x = inv_d[0] * (node.bbox_min[0][i] - o[0]);
        float hi_x = inv_d[0] * (node.bbox_max[0][i] - o[0]);
        if (lo_x > hi_x) {
            const float tmp = lo_x;
            lo_x = hi_x;
            hi_x = tmp;
        }

        float lo_y = inv_d[1] * (node.bbox_min[1][i] - o[1]);
        float hi_y = inv_d[1] * (node.bbox_max[1][i] - o[1]);
        if (lo_y > hi_y) {
            const float tmp = lo_y;
            lo_y = hi_y;
            hi_y = tmp;
        }

        float lo_z = inv_d[2] * (node.bbox_min[2][i] - o[2]);
        float hi_z = inv_d[2] * (node.bbox_max[2][i] - o[2]);
        if (lo_z > hi_z) {
            const float tmp = lo_z;
            lo_z = hi_z;
            hi_z = tmp;
        }

        float tmin = lo_x > lo_y ? lo_x : lo_y;
        if (lo_z > tmin) {
            tmin = lo_z;
        }
        float tmax = hi_x < hi_y ? hi_x : hi_y;
        if (hi_z < tmax) {
            tmax = hi_z;
        }
        tmax *= 1.00000024f;

        out_dist[i] = tmin;
        mask |= ((tmin <= tmax && tmin <= t && tmax > 0) ? 1 : 0) << i;
    }) // NOLINT
#endif
    return mask;
}

struct stack_entry_t {
    uint32_t index;
    float dist;
};

struct light_stack_entry_t {
    uint32_t index;
    float dist;
    float factor;
};

template <int StackSize, typename T = stack_entry_t> struct TraversalStack {
    T stack[StackSize];
    uint32_t stack_size = 0;

    template <class... Args> force_inline void push(Args &&...args) {
        stack[stack_size++] = {std::forward<Args>(args)...};
        assert(stack_size < StackSize && "Traversal stack overflow!");
    }

    force_inline T pop() { return stack[--stack_size]; }

    force_inline uint32_t pop_index() { return stack[--stack_size].index; }

    force_inline bool empty() const { return stack_size == 0; }

    void sort_top3() {
        assert(stack_size >= 3);
        const uint32_t i = stack_size - 3;

        if (stack[i].dist > stack[i + 1].dist) {
            if (stack[i + 1].dist > stack[i + 2].dist) {
                return;
            } else if (stack[i].dist > stack[i + 2].dist) {
                std::swap(stack[i + 1], stack[i + 2]);
            } else {
                T tmp = stack[i];
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
                const T tmp = stack[i];
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

        if (stack[i + 0].dist < stack[i + 1].dist) {
            std::swap(stack[i + 0], stack[i + 1]);
        }
        if (stack[i + 2].dist < stack[i + 3].dist) {
            std::swap(stack[i + 2], stack[i + 3]);
        }
        if (stack[i + 0].dist < stack[i + 2].dist) {
            std::swap(stack[i + 0], stack[i + 2]);
        }
        if (stack[i + 1].dist < stack[i + 3].dist) {
            std::swap(stack[i + 1], stack[i + 3]);
        }
        if (stack[i + 1].dist < stack[i + 2].dist) {
            std::swap(stack[i + 1], stack[i + 2]);
        }

        assert(stack[stack_size - 4].dist >= stack[stack_size - 3].dist &&
               stack[stack_size - 3].dist >= stack[stack_size - 2].dist &&
               stack[stack_size - 2].dist >= stack[stack_size - 1].dist);
    }

    void sort_topN(const int count) {
        assert(stack_size >= uint32_t(count));
        const int start = int(stack_size - count);

        for (int i = start + 1; i < int(stack_size); ++i) {
            const T key = stack[i];

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

force_inline void safe_invert(const float v[3], float out_v[3]) {
    if (v[0] <= FLT_EPS && v[0] >= 0) {
        out_v[0] = FLT_MAX;
    } else if (v[0] >= -FLT_EPS && v[0] < 0) {
        out_v[0] = -FLT_MAX;
    } else {
        out_v[0] = 1.0f / v[0];
    }

    if (v[1] <= FLT_EPS && v[1] >= 0) {
        out_v[1] = FLT_MAX;
    } else if (v[1] >= -FLT_EPS && v[1] < 0) {
        out_v[1] = -FLT_MAX;
    } else {
        out_v[1] = 1.0f / v[1];
    }

    if (v[2] <= FLT_EPS && v[2] >= 0) {
        out_v[2] = FLT_MAX;
    } else if (v[2] >= -FLT_EPS && v[2] < 0) {
        out_v[2] = -FLT_MAX;
    } else {
        out_v[2] = 1.0f / v[2];
    }
}

force_inline float clamp(const float val, const float min, const float max) {
    return val < min ? min : (val > max ? max : val);
}

force_inline int clamp(const int val, const int min, const int max) {
    return val < min ? min : (val > max ? max : val);
}

force_inline simd_fvec4 cross(const simd_fvec4 &v1, const simd_fvec4 &v2) {
    return simd_fvec4{v1.get<1>() * v2.get<2>() - v1.get<2>() * v2.get<1>(),
                      v1.get<2>() * v2.get<0>() - v1.get<0>() * v2.get<2>(),
                      v1.get<0>() * v2.get<1>() - v1.get<1>() * v2.get<0>(), 0.0f};
}

force_inline simd_fvec4 reflect(const simd_fvec4 &I, const simd_fvec4 &N, const float dot_N_I) {
    return I - 2 * dot_N_I * N;
}

force_inline float pow5(const float v) { return (v * v) * (v * v) * v; }

force_inline float mix(const float v1, const float v2, const float k) { return (1.0f - k) * v1 + k * v2; }

force_inline uint32_t get_ray_hash(const ray_data_t &r, const float root_min[3], const float cell_size[3]) {
    int x = clamp(int((r.o[0] - root_min[0]) / cell_size[0]), 0, 255),
        y = clamp(int((r.o[1] - root_min[1]) / cell_size[1]), 0, 255),
        z = clamp(int((r.o[2] - root_min[2]) / cell_size[2]), 0, 255);

    // float omega = omega_table[int(r.d[2] / 0.0625f)];
    // float atan2f(r.d[1], r.d[0]);
    // int o = int(16 * omega / (PI)), p = int(16 * (phi + PI) / (2 * PI));

    x = morton_table_256[x];
    y = morton_table_256[y];
    z = morton_table_256[z];

    const int o = morton_table_16[int(omega_table[clamp(int((1.0f + r.d[2]) / omega_step), 0, 32)])];
    const int p = morton_table_16[int(
        phi_table[clamp(int((1.0f + r.d[1]) / phi_step), 0, 16)][clamp(int((1.0f + r.d[0]) / phi_step), 0, 16)])];

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
    static const uint32_t ieeeMantissa = 0x007FFFFFu; // binary32 mantissa bitmask
    static const uint32_t ieeeOne = 0x3F800000u;      // 1.0 in IEEE binary32

    m &= ieeeMantissa; // Keep only mantissa bits (fractional part)
    m |= ieeeOne;      // Add fractional part to 1.0

    union {
        uint32_t i;
        float f;
    } ret = {m};         // Range [1:2]
    return ret.f - 1.0f; // Range [0:1]
}

force_inline simd_fvec4 srgb_to_rgb(const simd_fvec4 &col) {
    simd_fvec4 ret;
    UNROLLED_FOR(i, 3, {
        if (col.get<i>() > 0.04045f) {
            ret.set<i>(powf((col.get<i>() + 0.055f) / 1.055f, 2.4f));
        } else {
            ret.set<i>(col.get<i>() / 12.92f);
        }
    })
    ret.set<3>(col[3]);

    return ret;
}

force_inline simd_fvec4 YCoCg_to_RGB(const simd_fvec4 &col) {
    const float scale = (col.get<2>() * (255.0f / 8.0f)) + 1.0f;
    const float Y = col.get<3>();
    const float Co = (col.get<0>() - (0.5f * 256.0f / 255.0f)) / scale;
    const float Cg = (col.get<1>() - (0.5f * 256.0f / 255.0f)) / scale;

    simd_fvec4 col_rgb = 1.0f;
    col_rgb.set<0>(Y + Co - Cg);
    col_rgb.set<1>(Y + Cg);
    col_rgb.set<2>(Y - Co - Cg);

    return clamp(col_rgb, 0.0f, 1.0f);
}

force_inline float fast_log2(float val) {
    // From https://stackoverflow.com/questions/9411823/fast-log2float-x-implementation-c
    union {
        float val;
        int32_t x;
    } u = {val};
    auto log_2 = float(((u.x >> 23) & 255) - 128);
    u.x &= ~(255 << 23);
    u.x += 127 << 23;
    log_2 += ((-0.34484843f) * u.val + 2.02466578f) * u.val - 0.67487759f;
    return (log_2);
}

force_inline float safe_sqrt(float val) {
#if USE_SAFE_MATH
    return sqrtf(fmaxf(val, 0.0f));
#else
    return sqrtf(val);
#endif
}

force_inline float safe_div_pos(const float a, const float b) {
#if USE_SAFE_MATH
    return a / fmaxf(b, FLT_EPS);
#else
    return (a / b)
#endif
}

force_inline float safe_div_neg(const float a, const float b) {
#if USE_SAFE_MATH
    return a / fminf(b, -FLT_EPS);
#else
    return (a / b)
#endif
}

force_inline simd_fvec4 safe_normalize(const simd_fvec4 &a) {
#if USE_SAFE_MATH
    const float l = length(a);
    return l > 0.0f ? (a / l) : a;
#else
    return normalize(a);
#endif
}

#define sqr(x) ((x) * (x))

force_inline float lum(const simd_fvec3 &color) {
    return 0.212671f * color.get<0>() + 0.715160f * color.get<1>() + 0.072169f * color.get<2>();
}

force_inline float lum(const simd_fvec4 &color) {
    return 0.212671f * color.get<0>() + 0.715160f * color.get<1>() + 0.072169f * color.get<2>();
}

float get_texture_lod(const Cpu::TexStorageBase *const textures[], const uint32_t index, const simd_fvec2 &duv_dx,
                      const simd_fvec2 &duv_dy) {
#ifdef FORCE_TEXTURE_LOD
    const float lod = float(FORCE_TEXTURE_LOD);
#else
    simd_fvec2 sz;
    textures[index >> 28]->GetFRes(index & 0x00ffffff, 0, value_ptr(sz));
    const simd_fvec2 _duv_dx = duv_dx * sz, _duv_dy = duv_dy * sz;
    const simd_fvec2 _diagonal = _duv_dx + _duv_dy;

    // Find minimal dimention of parallelogram
    const float min_length2 = fminf(fminf(_duv_dx.length2(), _duv_dy.length2()), _diagonal.length2());
    // Find lod
    float lod = fast_log2(min_length2);
    // Substruct 1 from lod to always have 4 texels for interpolation
    lod = clamp(0.5f * lod - 1.0f, 0.0f, float(MAX_MIP_LEVEL));
#endif
    return lod;
}

float get_texture_lod(const Cpu::TexStorageBase *const textures[], const uint32_t index, const float lambda) {
#ifdef FORCE_TEXTURE_LOD
    const float lod = float(FORCE_TEXTURE_LOD);
#else
    simd_fvec2 res;
    textures[index >> 28]->GetFRes(index & 0x00ffffff, 0, value_ptr(res));
    // Find lod
    float lod = lambda + 0.5f * fast_log2(res.get<0>() * res.get<1>());
    // Substruct 1 from lod to always have 4 texels for interpolation
    lod = clamp(lod - 1.0f, 0.0f, float(MAX_MIP_LEVEL));
#endif
    return lod;
}

lobe_weights_t get_lobe_weights(const float base_color_lum, const float spec_color_lum, const float specular,
                                const float metallic, const float transmission, const float clearcoat) {
    lobe_weights_t weights;

    // taken from Cycles
    weights.diffuse = base_color_lum * (1.0f - metallic) * (1.0f - transmission);
    const float final_transmission = transmission * (1.0f - metallic);
    weights.specular = (specular != 0.0f || metallic != 0.0f) ? spec_color_lum * (1.0f - final_transmission) : 0.0f;
    weights.clearcoat = 0.25f * clearcoat * (1.0f - metallic);
    weights.refraction = final_transmission * base_color_lum;

    const float total_weight = weights.diffuse + weights.specular + weights.clearcoat + weights.refraction;
    if (total_weight != 0.0f) {
        weights.diffuse /= total_weight;
        weights.specular /= total_weight;
        weights.clearcoat /= total_weight;
        weights.refraction /= total_weight;
    }

    return weights;
}

force_inline float power_heuristic(const float a, const float b) {
    const float t = a * a;
    return t / (b * b + t);
}

force_inline float schlick_weight(const float u) {
    const float m = clamp(1.0f - u, 0.0f, 1.0f);
    return pow5(m);
}

float fresnel_dielectric_cos(float cosi, float eta) {
    // compute fresnel reflectance without explicitly computing the refracted direction
    float c = fabsf(cosi);
    float g = eta * eta - 1 + c * c;
    float result;

    if (g > 0) {
        g = sqrtf(g);
        float A = (g - c) / (g + c);
        float B = (c * (g + c) - 1) / (c * (g - c) + 1);
        result = 0.5f * A * A * (1 + B * B);
    } else {
        result = 1.0f; // TIR (no refracted component)
    }

    return result;
}

//
// From "A Fast and Robust Method for Avoiding Self-Intersection"
//

force_inline int32_t float_as_int(const float v) {
    union {
        float f;
        int32_t i;
    } ret = {v};
    return ret.i;
}
force_inline float int_as_float(const int32_t v) {
    union {
        int32_t i;
        float f;
    } ret = {v};
    return ret.f;
}

simd_fvec4 offset_ray(const simd_fvec4 &p, const simd_fvec4 &n) {
    const float Origin = 1.0f / 32.0f;
    const float FloatScale = 1.0f / 65536.0f;
    const float IntScale = 128.0f; // 256.0f;

    const simd_ivec4 of_i(IntScale * n);

    const simd_fvec4 p_i(
        int_as_float(float_as_int(p.get<0>()) + ((p.get<0>() < 0.0f) ? -of_i.get<0>() : of_i.get<0>())),
        int_as_float(float_as_int(p.get<1>()) + ((p.get<1>() < 0.0f) ? -of_i.get<1>() : of_i.get<1>())),
        int_as_float(float_as_int(p.get<2>()) + ((p.get<2>() < 0.0f) ? -of_i.get<2>() : of_i.get<2>())), 0.0f);

    return simd_fvec4{fabsf(p.get<0>()) < Origin ? (p.get<0>() + FloatScale * n.get<0>()) : p_i.get<0>(),
                      fabsf(p.get<1>()) < Origin ? (p.get<1>() + FloatScale * n.get<1>()) : p_i.get<1>(),
                      fabsf(p.get<2>()) < Origin ? (p.get<2>() + FloatScale * n.get<2>()) : p_i.get<2>(), 0.0f};
}

simd_fvec3 sample_GTR1(const float rgh, const float r1, const float r2) {
    const float a = fmaxf(0.001f, rgh);
    const float a2 = sqr(a);

    const float phi = r1 * (2.0f * PI);

    const float cosTheta = sqrtf(fmaxf(0.0f, 1.0f - powf(a2, 1.0f - r2)) / (1.0f - a2));
    const float sinTheta = sqrtf(fmaxf(0.0f, 1.0f - (cosTheta * cosTheta)));
    const float sinPhi = sinf(phi), cosPhi = cosf(phi);

    return simd_fvec3{sinTheta * cosPhi, sinTheta * sinPhi, cosTheta};
}

simd_fvec3 SampleGGX_NDF(const float rgh, const float r1, const float r2) {
    const float a = fmaxf(0.001f, rgh);

    const float phi = r1 * (2.0f * PI);

    const float cosTheta = sqrtf((1.0f - r2) / (1.0f + (a * a - 1.0f) * r2));
    const float sinTheta = clamp(sqrtf(1.0f - (cosTheta * cosTheta)), 0.0f, 1.0f);
    const float sinPhi = sinf(phi), cosPhi = cosf(phi);

    return simd_fvec3{sinTheta * cosPhi, sinTheta * sinPhi, cosTheta};
}

// http://jcgt.org/published/0007/04/01/paper.pdf
simd_fvec4 SampleVNDF_Hemisphere_CrossSect(const simd_fvec4 &Vh, float U1, float U2) {
    // orthonormal basis (with special case if cross product is zero)
    const float lensq = sqr(Vh.get<0>()) + sqr(Vh.get<1>());
    const simd_fvec4 T1 = lensq > 0.0f ? simd_fvec4(-Vh.get<1>(), Vh.get<0>(), 0.0f, 0.0f) / sqrtf(lensq)
                                       : simd_fvec4(1.0f, 0.0f, 0.0f, 0.0f);
    const simd_fvec4 T2 = cross(Vh, T1);
    // parameterization of the projected area
    const float r = sqrtf(U1);
    const float phi = 2.0f * PI * U2;
    const float t1 = r * cosf(phi);
    float t2 = r * sinf(phi);
    const float s = 0.5f * (1.0f + Vh.get<2>());
    t2 = (1.0f - s) * sqrtf(1.0f - t1 * t1) + s * t2;
    // reprojection onto hemisphere
    const simd_fvec4 Nh = t1 * T1 + t2 * T2 + sqrtf(fmaxf(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;
    // normalization will be done later
    return Nh;
}

// https://arxiv.org/pdf/2306.05044.pdf
simd_fvec4 SampleVNDF_Hemisphere_SphCap(const simd_fvec4 &Vh, float U1, float U2) {
    // sample a spherical cap in (-Vh.z, 1]
    const float phi = 2.0f * PI * U1;
    const float z = fma(1.0f - U2, 1.0f + Vh.get<2>(), -Vh.get<2>());
    const float sin_theta = sqrtf(clamp(1.0f - z * z, 0.0f, 1.0f));
    const float x = sin_theta * cosf(phi);
    const float y = sin_theta * sinf(phi);
    const simd_fvec4 c = simd_fvec4{x, y, z, 0.0f};
    // normalization will be done later
    return c + Vh;
}

// Input Ve: view direction
// Input alpha_x, alpha_y: roughness parameters
// Input U1, U2: uniform random numbers
// Output Ne: normal sampled with PDF D_Ve(Ne) = G1(Ve) * max(0, dot(Ve, Ne)) * D(Ne) / Ve.z
simd_fvec4 SampleGGX_VNDF(const simd_fvec4 &Ve, float alpha_x, float alpha_y, float U1, float U2) {
    // transforming the view direction to the hemisphere configuration
    const simd_fvec4 Vh = normalize(simd_fvec4(alpha_x * Ve.get<0>(), alpha_y * Ve.get<1>(), Ve.get<2>(), 0.0f));
    // sample the hemisphere
    const simd_fvec4 Nh = SampleVNDF_Hemisphere_SphCap(Vh, U1, U2);
    // transforming the normal back to the ellipsoid configuration
    const simd_fvec4 Ne =
        normalize(simd_fvec4(alpha_x * Nh.get<0>(), alpha_y * Nh.get<1>(), fmaxf(0.0f, Nh.get<2>()), 0.0f));
    return Ne;
}

// Smith shadowing function
force_inline float G1(const simd_fvec4 &Ve, float alpha_x, float alpha_y) {
    alpha_x *= alpha_x;
    alpha_y *= alpha_y;
    const float delta = (-1.0f + sqrtf(1.0f + safe_div_pos(alpha_x * sqr(Ve.get<0>()) + alpha_y * sqr(Ve.get<1>()),
                                                           sqr(Ve.get<2>())))) /
                        2.0f;
    return 1.0f / (1.0f + delta);
}

float SmithG_GGX(const float N_dot_V, const float alpha_g) {
    const float a = alpha_g * alpha_g;
    const float b = N_dot_V * N_dot_V;
    return 1.0f / (N_dot_V + sqrtf(a + b - a * b));
}

float D_GTR1(float NDotH, float a) {
    if (a >= 1.0f) {
        return 1.0f / PI;
    }
    const float a2 = sqr(a);
    const float t = 1.0f + (a2 - 1.0f) * NDotH * NDotH;
    return (a2 - 1.0f) / (PI * logf(a2) * t);
}

float D_GTR2(const float N_dot_H, const float a) {
    const float a2 = sqr(a);
    const float t = 1.0f + (a2 - 1.0f) * N_dot_H * N_dot_H;
    return a2 / (PI * t * t);
}

float D_GGX(const simd_fvec4 &H, const float alpha_x, const float alpha_y) {
    if (H.get<2>() == 0.0f) {
        return 0.0f;
    }
    const float sx = -H.get<0>() / (H.get<2>() * alpha_x);
    const float sy = -H.get<1>() / (H.get<2>() * alpha_y);
    const float s1 = 1.0f + sx * sx + sy * sy;
    const float cos_theta_h4 = sqr(sqr(H.get<2>()));
    return 1.0f / (sqr(s1) * PI * alpha_x * alpha_y * cos_theta_h4);
}

void create_tbn_matrix(const simd_fvec4 &N, simd_fvec4 out_TBN[3]) {
    simd_fvec4 U;
    if (fabsf(N.get<1>()) < 0.999f) {
        U = {0.0f, 1.0f, 0.0f, 0.0f};
    } else {
        U = {1.0f, 0.0f, 0.0f, 0.0f};
    }

    simd_fvec4 T = normalize(cross(U, N));
    U = cross(N, T);

    out_TBN[0].set<0>(T.get<0>());
    out_TBN[1].set<0>(T.get<1>());
    out_TBN[2].set<0>(T.get<2>());

    out_TBN[0].set<1>(U.get<0>());
    out_TBN[1].set<1>(U.get<1>());
    out_TBN[2].set<1>(U.get<2>());

    out_TBN[0].set<2>(N.get<0>());
    out_TBN[1].set<2>(N.get<1>());
    out_TBN[2].set<2>(N.get<2>());
}

void create_tbn_matrix(const simd_fvec4 &N, simd_fvec4 &T, simd_fvec4 out_TBN[3]) {
    simd_fvec4 U = normalize(cross(T, N));
    T = cross(N, U);

    out_TBN[0].set<0>(T.get<0>());
    out_TBN[1].set<0>(T.get<1>());
    out_TBN[2].set<0>(T.get<2>());

    out_TBN[0].set<1>(U.get<0>());
    out_TBN[1].set<1>(U.get<1>());
    out_TBN[2].set<1>(U.get<2>());

    out_TBN[0].set<2>(N.get<0>());
    out_TBN[1].set<2>(N.get<1>());
    out_TBN[2].set<2>(N.get<2>());
}

void create_tbn(const simd_fvec4 &N, simd_fvec4 &out_T, simd_fvec4 &out_B) {
    simd_fvec4 U;
    if (fabsf(N.get<1>()) < 0.999f) {
        U = {0.0f, 1.0f, 0.0f, 0.0f};
    } else {
        U = {1.0f, 0.0f, 0.0f, 0.0f};
    }

    out_T = normalize(cross(U, N));
    out_B = cross(N, out_T);
}

simd_fvec4 MapToCone(float r1, float r2, simd_fvec4 N, float radius) {
    const simd_fvec2 offset = 2.0f * simd_fvec2(r1, r2) - simd_fvec2(1.0f);
    if (offset.get<0>() == 0.0f && offset.get<1>() == 0.0f) {
        return N;
    }

    float theta, r;

    if (fabsf(offset.get<0>()) > fabsf(offset.get<1>())) {
        r = offset.get<0>();
        theta = 0.25f * PI * (offset.get<1>() / offset.get<0>());
    } else {
        r = offset.get<1>();
        theta = 0.5f * PI * (1.0f - 0.5f * (offset.get<0>() / offset.get<1>()));
    }

    const simd_fvec2 uv = simd_fvec2(radius * r * cosf(theta), radius * r * sinf(theta));

    simd_fvec4 LT, LB;
    create_tbn(N, LT, LB);

    return N + uv.get<0>() * LT + uv.get<1>() * LB;
}

simd_fvec4 rotate_around_axis(const simd_fvec4 &p, const simd_fvec4 &axis, const float angle) {
    const float costheta = cosf(angle);
    const float sintheta = sinf(angle);
    simd_fvec4 r;

    r.set<0>(((costheta + (1.0f - costheta) * axis.get<0>() * axis.get<0>()) * p.get<0>()) +
             (((1.0f - costheta) * axis.get<0>() * axis.get<1>() - axis.get<2>() * sintheta) * p.get<1>()) +
             (((1.0f - costheta) * axis.get<0>() * axis.get<2>() + axis.get<1>() * sintheta) * p.get<2>()));
    r.set<1>((((1.0f - costheta) * axis.get<0>() * axis.get<1>() + axis.get<2>() * sintheta) * p.get<0>()) +
             ((costheta + (1.0f - costheta) * axis.get<1>() * axis.get<1>()) * p.get<1>()) +
             (((1.0f - costheta) * axis.get<1>() * axis.get<2>() - axis.get<0>() * sintheta) * p.get<2>()));
    r.set<2>((((1.0f - costheta) * axis.get<0>() * axis.get<2>() - axis.get<1>() * sintheta) * p.get<0>()) +
             (((1.0f - costheta) * axis.get<1>() * axis.get<2>() + axis.get<0>() * sintheta) * p.get<1>()) +
             ((costheta + (1.0f - costheta) * axis.get<2>() * axis.get<2>()) * p.get<2>()));
    r.set<3>(0.0f);

    return r;
}

void transpose(const simd_fvec3 in_3x3[3], simd_fvec3 out_3x3[3]) {
    out_3x3[0].set<0>(in_3x3[0].get<0>());
    out_3x3[0].set<1>(in_3x3[1].get<0>());
    out_3x3[0].set<2>(in_3x3[2].get<0>());

    out_3x3[1].set<0>(in_3x3[0].get<1>());
    out_3x3[1].set<1>(in_3x3[1].get<1>());
    out_3x3[1].set<2>(in_3x3[2].get<1>());

    out_3x3[2].set<0>(in_3x3[0].get<2>());
    out_3x3[2].set<1>(in_3x3[1].get<2>());
    out_3x3[2].set<2>(in_3x3[2].get<2>());
}

simd_fvec3 mul(const simd_fvec3 in_mat[3], const simd_fvec3 &in_vec) {
    simd_fvec3 out_vec;
    out_vec.set<0>(in_mat[0].get<0>() * in_vec.get<0>() + in_mat[1].get<0>() * in_vec.get<1>() +
                   in_mat[2].get<0>() * in_vec.get<2>());
    out_vec.set<1>(in_mat[0].get<1>() * in_vec.get<0>() + in_mat[1].get<1>() * in_vec.get<1>() +
                   in_mat[2].get<1>() * in_vec.get<2>());
    out_vec.set<2>(in_mat[0].get<2>() * in_vec.get<0>() + in_mat[1].get<2>() * in_vec.get<1>() +
                   in_mat[2].get<2>() * in_vec.get<2>());
    return out_vec;
}

force_inline float safe_sqrtf(float f) { return sqrtf(fmaxf(f, 0.0f)); }

// Taken from Cycles
simd_fvec4 ensure_valid_reflection(const simd_fvec4 &Ng, const simd_fvec4 &I, const simd_fvec4 &N) {
    const simd_fvec4 R = 2 * dot(N, I) * N - I;

    // Reflection rays may always be at least as shallow as the incoming ray.
    const float threshold = fminf(0.9f * dot(Ng, I), 0.01f);
    if (dot(Ng, R) >= threshold) {
        return N;
    }

    // Form coordinate system with Ng as the Z axis and N inside the X-Z-plane.
    // The X axis is found by normalizing the component of N that's orthogonal to Ng.
    // The Y axis isn't actually needed.
    const float NdotNg = dot(N, Ng);
    const simd_fvec4 X = normalize(N - NdotNg * Ng);

    // Calculate N.z and N.x in the local coordinate system.
    //
    // The goal of this computation is to find a N' that is rotated towards Ng just enough
    // to lift R' above the threshold (here called t), therefore dot(R', Ng) = t.
    //
    // According to the standard reflection equation,
    // this means that we want dot(2*dot(N', I)*N' - I, Ng) = t.
    //
    // Since the Z axis of our local coordinate system is Ng, dot(x, Ng) is just x.z, so we get
    // 2*dot(N', I)*N'.z - I.z = t.
    //
    // The rotation is simple to express in the coordinate system we formed -
    // since N lies in the X-Z-plane, we know that N' will also lie in the X-Z-plane,
    // so N'.y = 0 and therefore dot(N', I) = N'.x*I.x + N'.z*I.z .
    //
    // Furthermore, we want N' to be normalized, so N'.x = sqrt(1 - N'.z^2).
    //
    // With these simplifications,
    // we get the final equation 2*(sqrt(1 - N'.z^2)*I.x + N'.z*I.z)*N'.z - I.z = t.
    //
    // The only unknown here is N'.z, so we can solve for that.
    //
    // The equation has four solutions in general:
    //
    // N'.z = +-sqrt(0.5*(+-sqrt(I.x^2*(I.x^2 + I.z^2 - t^2)) + t*I.z + I.x^2 + I.z^2)/(I.x^2 + I.z^2))
    // We can simplify this expression a bit by grouping terms:
    //
    // a = I.x^2 + I.z^2
    // b = sqrt(I.x^2 * (a - t^2))
    // c = I.z*t + a
    // N'.z = +-sqrt(0.5*(+-b + c)/a)
    //
    // Two solutions can immediately be discarded because they're negative so N' would lie in the
    // lower hemisphere.

    const float Ix = dot(I, X), Iz = dot(I, Ng);
    const float Ix2 = (Ix * Ix), Iz2 = (Iz * Iz);
    const float a = Ix2 + Iz2;

    const float b = safe_sqrtf(Ix2 * (a - (threshold * threshold)));
    const float c = Iz * threshold + a;

    // Evaluate both solutions.
    // In many cases one can be immediately discarded (if N'.z would be imaginary or larger than
    // one), so check for that first. If no option is viable (might happen in extreme cases like N
    // being in the wrong hemisphere), give up and return Ng.
    const float fac = 0.5f / a;
    const float N1_z2 = fac * (b + c), N2_z2 = fac * (-b + c);
    bool valid1 = (N1_z2 > 1e-5f) && (N1_z2 <= (1.0f + 1e-5f));
    bool valid2 = (N2_z2 > 1e-5f) && (N2_z2 <= (1.0f + 1e-5f));

    simd_fvec2 N_new;
    if (valid1 && valid2) {
        // If both are possible, do the expensive reflection-based check.
        const simd_fvec2 N1 = simd_fvec2(safe_sqrtf(1.0f - N1_z2), safe_sqrtf(N1_z2));
        const simd_fvec2 N2 = simd_fvec2(safe_sqrtf(1.0f - N2_z2), safe_sqrtf(N2_z2));

        const float R1 = 2 * (N1.get<0>() * Ix + N1.get<1>() * Iz) * N1.get<1>() - Iz;
        const float R2 = 2 * (N2.get<0>() * Ix + N2.get<1>() * Iz) * N2.get<1>() - Iz;

        valid1 = (R1 >= 1e-5f);
        valid2 = (R2 >= 1e-5f);
        if (valid1 && valid2) {
            // If both solutions are valid, return the one with the shallower reflection since it will be
            // closer to the input (if the original reflection wasn't shallow, we would not be in this
            // part of the function).
            N_new = (R1 < R2) ? N1 : N2;
        } else {
            // If only one reflection is valid (= positive), pick that one.
            N_new = (R1 > R2) ? N1 : N2;
        }
    } else if (valid1 || valid2) {
        // Only one solution passes the N'.z criterium, so pick that one.
        const float Nz2 = valid1 ? N1_z2 : N2_z2;
        N_new = simd_fvec2(safe_sqrtf(1.0f - Nz2), safe_sqrtf(Nz2));
    } else {
        return Ng;
    }

    return N_new.get<0>() * X + N_new.get<1>() * Ng;
}

force_inline simd_fvec4 world_from_tangent(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                                           const simd_fvec4 &V) {
    return V.get<0>() * T + V.get<1>() * B + V.get<2>() * N;
}

force_inline simd_fvec4 tangent_from_world(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                                           const simd_fvec4 &V) {
    return simd_fvec4{dot(V, T), dot(V, B), dot(V, N), 0.0f};
}

force_inline float fract(const float v) {
    float _unused;
    return modff(v, &_unused);
}

force_inline bool quadratic(float a, float b, float c, float &t0, float &t1) {
    const float d = b * b - 4.0f * a * c;
    if (d < 0.0f) {
        return false;
    }
    const float sqrt_d = sqrtf(d);
    float q;
    if (b < 0.0f) {
        q = -0.5f * (b - sqrt_d);
    } else {
        q = -0.5f * (b + sqrt_d);
    }
    t0 = q / a;
    t1 = c / q;
    return true;
}

force_inline float ngon_rad(const float theta, const float n) {
    return cosf(PI / n) / cosf(theta - (2.0f * PI / n) * floorf((n * theta + PI) / (2.0f * PI)));
}

force_inline simd_fvec4 make_fvec3(const float *f) { return simd_fvec4{f[0], f[1], f[2], 0.0f}; }

void push_ior_stack(float stack[4], const float val) {
    UNROLLED_FOR(i, 3, {
        if (stack[i] < 0.0f) {
            stack[i] = val;
            return;
        }
    })
    // replace the last value regardless of sign
    stack[3] = val;
}

float pop_ior_stack(float stack[4], const float default_value = 1.0f) {
    UNROLLED_FOR_R(i, 4, {
        if (stack[i] > 0.0f) {
            return exchange(stack[i], -1.0f);
        }
    })
    return default_value;
}

float peek_ior_stack(const float stack[4], bool skip_first, const float default_value = 1.0f) {
    UNROLLED_FOR_R(i, 4, {
        if (stack[i] > 0.0f && !exchange(skip_first, false)) {
            return stack[i];
        }
    })
    return default_value;
}

float approx_atan2(const float y, const float x) { // max error is 0.000004f
    float t0, t1, t3, t4;

    t3 = fabsf(x);
    t1 = fabsf(y);
    t0 = fmaxf(t3, t1);
    t1 = fminf(t3, t1);
    t3 = 1.0f / t0;
    t3 = t1 * t3;

    t4 = t3 * t3;
    t0 = -0.013480470f;
    t0 = t0 * t4 + 0.057477314f;
    t0 = t0 * t4 - 0.121239071f;
    t0 = t0 * t4 + 0.195635925f;
    t0 = t0 * t4 - 0.332994597f;
    t0 = t0 * t4 + 0.999995630f;
    t3 = t0 * t3;

    t3 = (fabsf(y) > fabsf(x)) ? 1.570796327f - t3 : t3;
    t3 = (x < 0) ? 3.141592654f - t3 : t3;
    t3 = (y < 0) ? -t3 : t3;

    return t3;
}

simd_fvec4 approx_atan2(const simd_fvec4 y, const simd_fvec4 x) {
    simd_fvec4 t0, t1, t3, t4;

    t3 = abs(x);
    t1 = abs(y);
    t0 = max(t3, t1);
    t1 = min(t3, t1);
    t3 = 1.0f / t0;
    t3 = t1 * t3;

    t4 = t3 * t3;
    t0 = -0.013480470f;
    t0 = t0 * t4 + 0.057477314f;
    t0 = t0 * t4 - 0.121239071f;
    t0 = t0 * t4 + 0.195635925f;
    t0 = t0 * t4 - 0.332994597f;
    t0 = t0 * t4 + 0.999995630f;
    t3 = t0 * t3;

    where(abs(y) > abs(x), t3) = 1.570796327f - t3;
    where(x < 0.0f, t3) = 3.141592654f - t3;
    where(y < 0.0f, t3) = -t3;

    return t3;
}

force_inline float approx_cos(float x) { // max error is 0.056010f
    const float tp = 1.0f / (2.0f * PI);
    x *= tp;
    x -= 0.25f + floorf(x + 0.25f);
    x *= 16.0f * (fabsf(x) - 0.5f);
    return x;
}

force_inline simd_fvec4 approx_cos(simd_fvec4 x) {
    const float tp = 1.0f / (2.0f * PI);
    x *= tp;
    x -= 0.25f + floor(x + 0.25f);
    x *= 16.0f * (abs(x) - 0.5f);
    return x;
}

force_inline float approx_acos(float x) { // max error is 0.000068f
    float negate = float(x < 0);
    x = fabsf(x);
    float ret = -0.0187293f;
    ret = ret * x;
    ret = ret + 0.0742610f;
    ret = ret * x;
    ret = ret - 0.2121144f;
    ret = ret * x;
    ret = ret + 1.5707288f;
    ret = ret * sqrtf(1.0f - x);
    ret = ret - 2 * negate * ret;
    return negate * PI + ret;
}

force_inline simd_fvec4 approx_acos(simd_fvec4 x) {
    simd_fvec4 negate = 0.0f;
    where(x < 0, negate) = 1.0f;
    x = abs(x);
    simd_fvec4 ret = -0.0187293f;
    ret = ret * x;
    ret = ret + 0.0742610f;
    ret = ret * x;
    ret = ret - 0.2121144f;
    ret = ret * x;
    ret = ret + 1.5707288f;
    ret = ret * sqrt(1.0f - min(x, 1.0f));
    ret = ret - 2 * negate * ret;
    return negate * PI + ret;
}

float calc_lnode_importance(const light_bvh_node_t &n, const simd_fvec4 &P) {
    float mul = 1.0f, v_len2 = 1.0f;
    if (n.bbox_min[0] > -MAX_DIST) { // check if this is a local light
        simd_fvec4 v = P - 0.5f * (simd_fvec4{n.bbox_min} + simd_fvec4{n.bbox_max});
        v.set<3>(0.0f);

        simd_fvec4 ext = simd_fvec4{n.bbox_max} - simd_fvec4{n.bbox_min};
        ext.set<3>(0.0f);

        const float extent = 0.5f * length(ext);
        v_len2 = dot(v, v);
        const float v_len = sqrtf(v_len2);
        const float omega_u = approx_atan2(extent, v_len) + 0.000005f;

        simd_fvec4 axis = simd_fvec4{n.axis};
        axis.set<3>(0.0f);

        const float omega = approx_acos(fminf(dot(axis, v / v_len), 1.0f)) - 0.00007f;
        const float omega_ = fmaxf(0.0f, omega - n.omega_n - omega_u);
        mul = omega_ < n.omega_e ? approx_cos(omega_) + 0.057f : 0.0f;
    }

    // TODO: account for normal dot product here
    return n.flux * mul / v_len2;
}

force_inline simd_fvec4 dot(const simd_fvec4 v1[3], const simd_fvec4 v2[3]) {
    return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

force_inline simd_fvec4 length(const simd_fvec4 v[3]) { return sqrt(dot(v, v)); }

void calc_lnode_importance(const light_wbvh_node_t &n, const simd_fvec4 &P, float importance[8]) {
    for (int i = 0; i < 8; i += 4) {
        simd_fvec4 mul = 1.0f, v_len2 = 1.0f;

        const simd_ivec4 mask = simd_cast(simd_fvec4{&n.bbox_min[0][i], simd_mem_aligned} > -MAX_DIST);
        if (mask.not_all_zeros()) {
            simd_fvec4 v[3] = {P.get<0>() - 0.5f * (simd_fvec4{&n.bbox_min[0][i], simd_mem_aligned} +
                                                    simd_fvec4{&n.bbox_max[0][i], simd_mem_aligned}),
                               P.get<1>() - 0.5f * (simd_fvec4{&n.bbox_min[1][i], simd_mem_aligned} +
                                                    simd_fvec4{&n.bbox_max[1][i], simd_mem_aligned}),
                               P.get<2>() - 0.5f * (simd_fvec4{&n.bbox_min[2][i], simd_mem_aligned} +
                                                    simd_fvec4{&n.bbox_max[2][i], simd_mem_aligned})};
            const simd_fvec4 ext[3] = {
                simd_fvec4{&n.bbox_max[0][i], simd_mem_aligned} - simd_fvec4{&n.bbox_min[0][i], simd_mem_aligned},
                simd_fvec4{&n.bbox_max[1][i], simd_mem_aligned} - simd_fvec4{&n.bbox_min[1][i], simd_mem_aligned},
                simd_fvec4{&n.bbox_max[2][i], simd_mem_aligned} - simd_fvec4{&n.bbox_min[2][i], simd_mem_aligned}};

            const simd_fvec4 extent = 0.5f * length(ext);
            where(mask, v_len2) = dot(v, v);
            const simd_fvec4 v_len = sqrt(v_len2);
            const simd_fvec4 omega_u = approx_atan2(extent, v_len) + 0.000005f;

            const simd_fvec4 axis[3] = {simd_fvec4{&n.axis[0][i], simd_mem_aligned},
                                        simd_fvec4{&n.axis[1][i], simd_mem_aligned},
                                        simd_fvec4{&n.axis[2][i], simd_mem_aligned}};

            UNROLLED_FOR(j, 3, { v[j] /= v_len; })
            const simd_fvec4 omega = approx_acos(min(dot(axis, v), 1.0f)) - 0.00007f;
            const simd_fvec4 omega_ = max(0.0f, omega - simd_fvec4{&n.omega_n[i], simd_mem_aligned} - omega_u);
            where(mask, mul) = 0.0f;
            where(mask & simd_cast(omega_ < simd_fvec4{&n.omega_e[i], simd_mem_aligned}), mul) =
                approx_cos(omega_) + 0.057f;
        }

        const simd_fvec4 imp = simd_fvec4{&n.flux[i], simd_mem_aligned} * mul / v_len2;
        imp.store_to(&importance[i], simd_mem_aligned);
    }
}

force_inline uint32_t hash_combine(uint32_t seed, uint32_t v) { return seed ^ (v + (seed << 6) + (seed >> 2)); }

force_inline uint32_t reverse_bits(uint32_t x) {
    x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
    x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
    x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
    x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));
    return ((x >> 16) | (x << 16));
}

force_inline uint32_t laine_karras_permutation(uint32_t x, const uint32_t seed) {
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}

uint32_t nested_uniform_scramble_base2(uint32_t x, const uint32_t seed) {
    x = reverse_bits(x);
    x = laine_karras_permutation(x, seed);
    x = reverse_bits(x);
    return x;
}

force_inline float scramble_flt(const uint32_t seed, const float val) {
    uint32_t u = uint32_t(val * 16777216.0f) << 8;
    u = nested_uniform_scramble_base2(u, seed);
    return float(u >> 8) / 16777216.0f;
}

force_inline float scramble_unorm(const uint32_t seed, uint32_t val) {
    val = nested_uniform_scramble_base2(val, seed);
    return float(val >> 8) / 16777216.0f;
}

simd_fvec2 get_scrambled_2d_rand(const uint32_t dim, const uint32_t seed, const int sample, const uint32_t rand_seq[]) {
    const uint32_t shuffled_dim = nested_uniform_scramble_base2(dim, seed) & (RAND_DIMS_COUNT - 1);
    const uint32_t shuffled_i =
        nested_uniform_scramble_base2(sample, hash_combine(seed, dim)) & (RAND_SAMPLES_COUNT - 1);
    return simd_fvec2{scramble_unorm(hash_combine(seed, 2 * dim + 0),
                                     rand_seq[shuffled_dim * 2 * RAND_SAMPLES_COUNT + 2 * shuffled_i + 0]),
                      scramble_unorm(hash_combine(seed, 2 * dim + 1),
                                     rand_seq[shuffled_dim * 2 * RAND_SAMPLES_COUNT + 2 * shuffled_i + 1])};
}

} // namespace Ref
} // namespace Ray

Ray::Ref::hit_data_t::hit_data_t() {
    mask = 0;
    obj_index = -1;
    prim_index = -1;
    t = MAX_DIST;
}

void Ray::Ref::GeneratePrimaryRays(const camera_t &cam, const rect_t &r, const int w, const int h,
                                   const uint32_t rand_seq[], const uint32_t rand_seed, const float filter_table[],
                                   const int iteration, const uint16_t required_samples[],
                                   aligned_vector<ray_data_t> &out_rays, aligned_vector<hit_data_t> &out_inters) {
    const simd_fvec4 cam_origin = make_fvec3(cam.origin), fwd = make_fvec3(cam.fwd), side = make_fvec3(cam.side),
                     up = make_fvec3(cam.up);
    const float focus_distance = cam.focus_distance;

    const float k = float(w) / float(h);
    const float temp = tanf(0.5f * cam.fov * PI / 180.0f);
    const float fov_k = temp * focus_distance;
    const float spread_angle = atanf(2.0f * temp / float(h));

    auto get_pix_dir = [&](const float x, const float y, const simd_fvec4 &origin) {
        simd_fvec4 p(2 * fov_k * (float(x) / float(w) + cam.shift[0] / k) - fov_k,
                     2 * fov_k * (float(-y) / float(h) + cam.shift[1]) + fov_k, focus_distance, 0.0f);
        p = cam_origin + k * p.get<0>() * side + p.get<1>() * up + p.get<2>() * fwd;
        return normalize(p - origin);
    };

    auto lookup_filter_table = [filter_table](float x) {
        x *= (FILTER_TABLE_SIZE - 1);

        const int index = std::min(int(x), FILTER_TABLE_SIZE - 1);
        const int nindex = std::min(index + 1, FILTER_TABLE_SIZE - 1);
        const float t = x - float(index);

        const float data0 = filter_table[index];
        if (t == 0.0f) {
            return data0;
        }

        float data1 = filter_table[nindex];
        return (1.0f - t) * data0 + t * data1;
    };

    size_t i = 0;
    out_rays.resize(size_t(r.w) * r.h);
    out_inters.resize(size_t(r.w) * r.h);

    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            if (required_samples[y * w + x] < iteration) {
                continue;
            }

            auto fx = float(x), fy = float(y);

            const uint32_t px_hash = hash((x << 16) | y);
            const uint32_t rand_hash = hash_combine(px_hash, rand_seed);

            const simd_fvec2 filter_rand = get_scrambled_2d_rand(RAND_DIM_FILTER, rand_hash, iteration - 1, rand_seq);
            float rx = filter_rand.get<0>(), ry = filter_rand.get<1>();

            if (cam.filter != ePixelFilter::Box) {
                rx = lookup_filter_table(rx);
                ry = lookup_filter_table(ry);
            }

            fx += rx;
            fy += ry;

            simd_fvec2 offset = 0.0f;

            if (cam.fstop > 0.0f) {
                const simd_fvec2 lens_rand = get_scrambled_2d_rand(RAND_DIM_LENS, rand_hash, iteration - 1, rand_seq);

                offset = 2.0f * lens_rand - simd_fvec2{1.0f, 1.0f};
                if (offset.get<0>() != 0.0f && offset.get<1>() != 0.0f) {
                    float theta, r;
                    if (fabsf(offset.get<0>()) > fabsf(offset.get<1>())) {
                        r = offset.get<0>();
                        theta = 0.25f * PI * (offset.get<1>() / offset.get<0>());
                    } else {
                        r = offset.get<1>();
                        theta = 0.5f * PI - 0.25f * PI * (offset.get<0>() / offset.get<1>());
                    }

                    if (cam.lens_blades) {
                        r *= ngon_rad(theta, float(cam.lens_blades));
                    }

                    theta += cam.lens_rotation;

                    offset.set<0>(0.5f * r * cosf(theta) / cam.lens_ratio);
                    offset.set<1>(0.5f * r * sinf(theta));
                }

                const float coc = 0.5f * (cam.focal_length / cam.fstop);
                offset *= coc * cam.sensor_height;
            }

            ray_data_t &out_r = out_rays[i];

            const simd_fvec4 _origin = cam_origin + side * offset.get<0>() + up * offset.get<1>();
            const simd_fvec4 _d = get_pix_dir(fx, fy, _origin);
            const float clip_start = cam.clip_start / dot(_d, fwd);

            for (int j = 0; j < 3; j++) {
                out_r.o[j] = _origin[j] + _d[j] * clip_start;
                out_r.d[j] = _d[j];
                out_r.c[j] = 1.0f;
            }

            // air ior is implicit
            out_r.ior[0] = out_r.ior[1] = out_r.ior[2] = out_r.ior[3] = -1.0f;

            out_r.cone_width = 0.0f;
            out_r.cone_spread = spread_angle;

            out_r.pdf = 1e6f;
            out_r.xy = (x << 16) | y;
            out_r.depth = 0;

            hit_data_t &out_i = out_inters[i++];
            out_i = {};
            out_i.t = (cam.clip_end / dot(_d, fwd)) - clip_start;
        }
    }

    out_rays.resize(i);
    out_inters.resize(i);
}

void Ray::Ref::SampleMeshInTextureSpace(const int iteration, const int obj_index, const int uv_layer,
                                        const mesh_t &mesh, const transform_t &tr, const uint32_t *vtx_indices,
                                        const vertex_t *vertices, const rect_t &r, const int width, const int height,
                                        const uint32_t rand_seq[], aligned_vector<ray_data_t> &out_rays,
                                        aligned_vector<hit_data_t> &out_inters) {
    out_rays.resize(size_t(r.w) * r.h);
    out_inters.resize(out_rays.size());

    for (int y = r.y; y < r.y + r.h; ++y) {
        for (int x = r.x; x < r.x + r.w; ++x) {
            const int i = (y - r.y) * r.w + (x - r.x);

            ray_data_t &out_ray = out_rays[i];
            hit_data_t &out_inter = out_inters[i];

            out_ray.xy = (x << 16) | y;
            out_ray.c[0] = out_ray.c[1] = out_ray.c[2] = 1.0f;
            out_inter.mask = 0;
        }
    }

    const simd_ivec2 irect_min = {r.x, r.y}, irect_max = {r.x + r.w - 1, r.y + r.h - 1};
    const simd_fvec2 size = {float(width), float(height)};

    for (uint32_t tri = mesh.tris_index; tri < mesh.tris_index + mesh.tris_count; tri++) {
        const vertex_t &v0 = vertices[vtx_indices[tri * 3 + 0]];
        const vertex_t &v1 = vertices[vtx_indices[tri * 3 + 1]];
        const vertex_t &v2 = vertices[vtx_indices[tri * 3 + 2]];

        // TODO: use uv_layer
        const auto t0 = simd_fvec2{v0.t[0], 1.0f - v0.t[1]} * size;
        const auto t1 = simd_fvec2{v1.t[0], 1.0f - v1.t[1]} * size;
        const auto t2 = simd_fvec2{v2.t[0], 1.0f - v2.t[1]} * size;

        simd_fvec2 bbox_min = t0, bbox_max = t0;

        bbox_min = min(bbox_min, t1);
        bbox_min = min(bbox_min, t2);

        bbox_max = max(bbox_max, t1);
        bbox_max = max(bbox_max, t2);

        simd_ivec2 ibbox_min = simd_ivec2{bbox_min},
                   ibbox_max = simd_ivec2{int(roundf(bbox_max.get<0>())), int(roundf(bbox_max.get<1>()))};

        if (ibbox_max.get<0>() < irect_min.get<0>() || ibbox_max.get<1>() < irect_min.get<1>() ||
            ibbox_min.get<0>() > irect_max.get<0>() || ibbox_min.get<1>() > irect_max.get<1>()) {
            continue;
        }

        ibbox_min = max(ibbox_min, irect_min);
        ibbox_max = min(ibbox_max, irect_max);

        const simd_fvec2 d01 = t0 - t1, d12 = t1 - t2, d20 = t2 - t0;

        const float area = d01.get<0>() * d20.get<1>() - d20.get<0>() * d01.get<1>();
        if (area < FLT_EPS) {
            continue;
        }

        const float inv_area = 1.0f / area;

        for (int y = ibbox_min.get<1>(); y <= ibbox_max.get<1>(); ++y) {
            for (int x = ibbox_min.get<0>(); x <= ibbox_max.get<0>(); ++x) {
                const int i = (y - r.y) * r.w + (x - r.x);
                ray_data_t &out_ray = out_rays[i];
                hit_data_t &out_inter = out_inters[i];

                if (out_inter.mask) {
                    continue;
                }

                const float _x = float(x); // + fract(rand_seq[RAND_DIM_FILTER_U] + construct_float(hash(out_ray.xy)));
                const float _y =
                    float(y); // + fract(rand_seq[RAND_DIM_FILTER_V] + construct_float(hash(hash(out_ray.xy))));

                float u = d01.get<0>() * (_y - t0.get<1>()) - d01.get<1>() * (_x - t0.get<0>()),
                      v = d12.get<0>() * (_y - t1.get<1>()) - d12.get<1>() * (_x - t1.get<0>()),
                      w = d20.get<0>() * (_y - t2.get<1>()) - d20.get<1>() * (_x - t2.get<0>());

                if (u >= -FLT_EPS && v >= -FLT_EPS && w >= -FLT_EPS) {
                    const auto p0 = simd_fvec4{v0.p}, p1 = simd_fvec4{v1.p}, p2 = simd_fvec4{v2.p};
                    const auto n0 = simd_fvec4{v0.n}, n1 = simd_fvec4{v1.n}, n2 = simd_fvec4{v2.n};

                    u *= inv_area;
                    v *= inv_area;
                    w *= inv_area;

                    const simd_fvec4 p = TransformPoint(p0 * v + p1 * w + p2 * u, tr.xform),
                                     n = TransformNormal(n0 * v + n1 * w + n2 * u, tr.inv_xform);

                    const simd_fvec4 o = p + n, d = -n;

                    memcpy(&out_ray.o[0], value_ptr(o), 3 * sizeof(float));
                    memcpy(&out_ray.d[0], value_ptr(d), 3 * sizeof(float));

                    out_ray.cone_width = 0;
                    out_ray.cone_spread = 0;
                    out_ray.depth = 0;

                    out_inter.mask = -1;
                    out_inter.prim_index = int(tri);
                    out_inter.obj_index = obj_index;
                    out_inter.t = 1.0f;
                    out_inter.u = w;
                    out_inter.v = u;
                }
            }
        }
    }
}

int Ray::Ref::SortRays_CPU(Span<ray_data_t> rays, const float root_min[3], const float cell_size[3],
                           uint32_t *hash_values, uint32_t *scan_values, ray_chunk_t *chunks,
                           ray_chunk_t *chunks_temp) {
    // From "Fast Ray Sorting and Breadth-First Packet Traversal for GPU Ray Tracing" [2010]

    // compute ray hash values
    for (uint32_t i = 0; i < uint32_t(rays.size()); ++i) {
        hash_values[i] = get_ray_hash(rays[i], root_min, cell_size);
    }

    size_t chunks_count = 0;

    // compress codes into spans of indentical values (makes sorting stage faster)
    for (uint32_t start = 0, end = 1; end <= uint32_t(rays.size()); end++) {
        if (end == uint32_t(rays.size()) || (hash_values[start] != hash_values[end])) {
            chunks[chunks_count].hash = hash_values[start];
            chunks[chunks_count].base = start;
            chunks[chunks_count++].size = end - start;
            start = end;
        }
    }

    radix_sort(&chunks[0], &chunks[0] + chunks_count, &chunks_temp[0]);

    // decompress sorted spans
    size_t counter = 0;
    for (uint32_t i = 0; i < chunks_count; ++i) {
        for (uint32_t j = 0; j < chunks[i].size; j++) {
            scan_values[counter++] = chunks[i].base + j;
        }
    }

    // reorder rays
    for (uint32_t i = 0; i < uint32_t(rays.size()); ++i) {
        uint32_t j;
        while (i != (j = scan_values[i])) {
            const uint32_t k = scan_values[j];
            std::swap(rays[j], rays[k]);
            std::swap(scan_values[i], scan_values[j]);
        }
    }

    return int(rays.size());
}

int Ray::Ref::SortRays_GPU(Span<ray_data_t> rays, const float root_min[3], const float cell_size[3],
                           uint32_t *hash_values, int *head_flags, uint32_t *scan_values, ray_chunk_t *chunks,
                           ray_chunk_t *chunks_temp, uint32_t *skeleton) {
    // From "Fast Ray Sorting and Breadth-First Packet Traversal for GPU Ray Tracing" [2010]

    // compute ray hash values
    for (uint32_t i = 0; i < uint32_t(rays.size()); ++i) {
        hash_values[i] = get_ray_hash(rays[i], root_min, cell_size);
    }

    // set head flags
    head_flags[0] = 1;
    for (uint32_t i = 1; i < uint32_t(rays.size()); ++i) {
        head_flags[i] = hash_values[i] != hash_values[i - 1];
    }

    size_t chunks_count = 0;

    { // perform exclusive scan on head flags
        uint32_t cur_sum = 0;
        for (size_t i = 0; i < size_t(rays.size()); ++i) {
            scan_values[i] = cur_sum;
            cur_sum += head_flags[i];
        }
        chunks_count = cur_sum;
    }

    // init Ray chunks hash and base index
    for (uint32_t i = 0; i < uint32_t(rays.size()); ++i) {
        if (head_flags[i]) {
            chunks[scan_values[i]].hash = hash_values[i];
            chunks[scan_values[i]].base = uint32_t(i);
        }
    }

    // init ray chunks size
    if (chunks_count) {
        for (size_t i = 0; i < chunks_count - 1; ++i) {
            chunks[i].size = chunks[i + 1].base - chunks[i].base;
        }
        chunks[chunks_count - 1].size = uint32_t(rays.size()) - chunks[chunks_count - 1].base;
    }

    radix_sort(&chunks[0], &chunks[0] + chunks_count, &chunks_temp[0]);

    { // perform exclusive scan on chunks size
        uint32_t cur_sum = 0;
        for (size_t i = 0; i < chunks_count; ++i) {
            scan_values[i] = cur_sum;
            cur_sum += chunks[i].size;
        }
    }

    std::fill(skeleton, skeleton + rays.size(), 1);
    std::fill(head_flags, head_flags + rays.size(), 0);

    // init skeleton and head flags array
    for (size_t i = 0; i < chunks_count; ++i) {
        skeleton[scan_values[i]] = chunks[i].base;
        head_flags[scan_values[i]] = 1;
    }

    { // perform a segmented scan on skeleton array
        uint32_t cur_sum = 0;
        for (uint32_t i = 0; i < uint32_t(rays.size()); ++i) {
            if (head_flags[i]) {
                cur_sum = 0;
            }
            cur_sum += skeleton[i];
            scan_values[i] = cur_sum;
        }
    }

    // reorder rays
    for (uint32_t i = 0; i < uint32_t(rays.size()); ++i) {
        uint32_t j;
        while (i != (j = scan_values[i])) {
            const uint32_t k = scan_values[j];
            std::swap(rays[j], rays[k]);
            std::swap(scan_values[i], scan_values[j]);
        }
    }

    return int(rays.size());
}

bool Ray::Ref::IntersectTris_ClosestHit(const float ro[3], const float rd[3], const tri_accel_t *tris,
                                        const int tri_start, const int tri_end, const int obj_index,
                                        hit_data_t &out_inter) {
    hit_data_t inter{Uninitialize};
    inter.mask = 0;
    inter.obj_index = obj_index;
    inter.t = out_inter.t;

    for (int i = tri_start; i < tri_end; ++i) {
        IntersectTri(ro, rd, tris[i], i, inter);
    }

    out_inter.mask |= inter.mask;
    out_inter.obj_index = inter.mask ? inter.obj_index : out_inter.obj_index;
    out_inter.prim_index = inter.mask ? inter.prim_index : out_inter.prim_index;
    out_inter.t = inter.t; // already contains min value
    out_inter.u = inter.mask ? inter.u : out_inter.u;
    out_inter.v = inter.mask ? inter.v : out_inter.v;

    return inter.mask != 0;
}

bool Ray::Ref::IntersectTris_ClosestHit(const float ro[3], const float rd[3], const mtri_accel_t *mtris,
                                        const int tri_start, const int tri_end, const int obj_index,
                                        hit_data_t &out_inter) {
    hit_data_t inter{Uninitialize};
    inter.mask = 0;
    inter.obj_index = obj_index;
    inter.t = out_inter.t;

    for (int i = tri_start / 8; i < (tri_end + 7) / 8; ++i) {
        IntersectTri(ro, rd, mtris[i], i * 8, inter);
    }

    out_inter.mask |= inter.mask;
    out_inter.obj_index = inter.mask ? inter.obj_index : out_inter.obj_index;
    out_inter.prim_index = inter.mask ? inter.prim_index : out_inter.prim_index;
    out_inter.t = inter.t; // already contains min value
    out_inter.u = inter.mask ? inter.u : out_inter.u;
    out_inter.v = inter.mask ? inter.v : out_inter.v;

    return inter.mask != 0;
}

bool Ray::Ref::IntersectTris_AnyHit(const float ro[3], const float rd[3], const tri_accel_t *tris,
                                    const tri_mat_data_t *materials, const uint32_t *indices, const int tri_start,
                                    const int tri_end, const int obj_index, hit_data_t &out_inter) {
    hit_data_t inter{Uninitialize};
    inter.mask = 0;
    inter.obj_index = obj_index;
    inter.t = out_inter.t;

    for (int i = tri_start; i < tri_end; ++i) {
        IntersectTri(ro, rd, tris[i], i, inter);
        if (inter.mask && ((inter.prim_index > 0 && (materials[indices[i]].front_mi & MATERIAL_SOLID_BIT)) ||
                           (inter.prim_index < 0 && (materials[indices[i]].back_mi & MATERIAL_SOLID_BIT)))) {
            break;
        }
    }

    out_inter.mask |= inter.mask;
    out_inter.obj_index = inter.mask ? inter.obj_index : out_inter.obj_index;
    out_inter.prim_index = inter.mask ? inter.prim_index : out_inter.prim_index;
    out_inter.t = inter.t; // already contains min value
    out_inter.u = inter.mask ? inter.u : out_inter.u;
    out_inter.v = inter.mask ? inter.v : out_inter.v;

    return inter.mask != 0;
}

bool Ray::Ref::IntersectTris_AnyHit(const float ro[3], const float rd[3], const mtri_accel_t *mtris,
                                    const tri_mat_data_t *materials, const uint32_t *indices, const int tri_start,
                                    const int tri_end, const int obj_index, hit_data_t &out_inter) {
    hit_data_t inter{Uninitialize};
    inter.mask = 0;
    inter.obj_index = obj_index;
    inter.t = out_inter.t;

    for (int i = tri_start / 8; i < (tri_end + 7) / 8; ++i) {
        IntersectTri(ro, rd, mtris[i], i * 8, inter);
        if (inter.mask && ((inter.prim_index > 0 && (materials[indices[i]].front_mi & MATERIAL_SOLID_BIT)) ||
                           (inter.prim_index < 0 && (materials[indices[i]].back_mi & MATERIAL_SOLID_BIT)))) {
            break;
        }
    }

    out_inter.mask |= inter.mask;
    out_inter.obj_index = inter.mask ? inter.obj_index : out_inter.obj_index;
    out_inter.prim_index = inter.mask ? inter.prim_index : out_inter.prim_index;
    out_inter.t = inter.t; // already contains min value
    out_inter.u = inter.mask ? inter.u : out_inter.u;
    out_inter.v = inter.mask ? inter.v : out_inter.v;

    return inter.mask != 0;
}

bool Ray::Ref::Traverse_TLAS_WithStack_ClosestHit(const float ro[3], const float rd[3], const bvh_node_t *nodes,
                                                  uint32_t root_index, const mesh_instance_t *mesh_instances,
                                                  const uint32_t *mi_indices, const mesh_t *meshes,
                                                  const transform_t *transforms, const tri_accel_t *tris,
                                                  const uint32_t *tri_indices, hit_data_t &inter) {
    bool res = false;

    float inv_d[3];
    safe_invert(rd, inv_d);

    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack[stack_size++] = root_index;

    while (stack_size) {
        uint32_t cur = stack[--stack_size];

        if (!bbox_test(ro, inv_d, inter.t, nodes[cur])) {
            continue;
        }

        if (!is_leaf_node(nodes[cur])) {
            stack[stack_size++] = far_child(rd, nodes[cur]);
            stack[stack_size++] = near_child(rd, nodes[cur]);
        } else {
            const uint32_t prim_index = (nodes[cur].prim_index & PRIM_INDEX_BITS);
            for (uint32_t i = prim_index; i < prim_index + nodes[cur].prim_count; ++i) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(ro, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) {
                    continue;
                }

                float _ro[3], _rd[3];
                TransformRay(ro, rd, tr.inv_xform, _ro, _rd);

                float _inv_d[3];
                safe_invert(_rd, _inv_d);
                res |= Traverse_BLAS_WithStack_ClosestHit(_ro, _rd, _inv_d, nodes, m.node_index, tris,
                                                          int(mi_indices[i]), inter);
            }
        }
    }

    return res;
}

bool Ray::Ref::Traverse_TLAS_WithStack_ClosestHit(const float ro[3], const float rd[3], const wbvh_node_t *nodes,
                                                  uint32_t root_index, const mesh_instance_t *mesh_instances,
                                                  const uint32_t *mi_indices, const mesh_t *meshes,
                                                  const transform_t *transforms, const mtri_accel_t *mtris,
                                                  const uint32_t *tri_indices, hit_data_t &inter) {
    bool res = false;

    float inv_d[3];
    safe_invert(rd, inv_d);

    TraversalStack<MAX_STACK_SIZE> st;
    st.push(root_index, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > inter.t) {
            continue;
        }

    TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            alignas(16) float dist[8];
            long mask = bbox_test_oct(ro, inv_d, inter.t, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                long i2 = GetFirstBit(mask);
                mask = ClearBit(mask, i2);
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

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                const uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                const int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            const uint32_t prim_index = (nodes[cur.index].child[0] & PRIM_INDEX_BITS);
            for (uint32_t i = prim_index; i < prim_index + nodes[cur.index].child[1]; ++i) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(ro, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) {
                    continue;
                }

                float _ro[3], _rd[3];
                TransformRay(ro, rd, tr.inv_xform, _ro, _rd);

                float _inv_d[3];
                safe_invert(_rd, _inv_d);
                res |= Traverse_BLAS_WithStack_ClosestHit(_ro, _rd, _inv_d, nodes, m.node_index, mtris,
                                                          int(mi_indices[i]), inter);
            }
        }
    }

    // resolve primitive index indirection
    if (inter.prim_index < 0) {
        inter.prim_index = -int(tri_indices[-inter.prim_index - 1]) - 1;
    } else {
        inter.prim_index = int(tri_indices[inter.prim_index]);
    }

    return res;
}

bool Ray::Ref::Traverse_TLAS_WithStack_AnyHit(const float ro[3], const float rd[3], int ray_type,
                                              const bvh_node_t *nodes, const uint32_t root_index,
                                              const mesh_instance_t *mesh_instances, const uint32_t *mi_indices,
                                              const mesh_t *meshes, const transform_t *transforms,
                                              const mtri_accel_t *mtris, const tri_mat_data_t *materials,
                                              const uint32_t *tri_indices, hit_data_t &inter) {
    const uint32_t ray_vismask = (1u << ray_type);

    float inv_d[3];
    safe_invert(rd, inv_d);

    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack[stack_size++] = root_index;

    while (stack_size) {
        const uint32_t cur = stack[--stack_size];

        if (!bbox_test(ro, inv_d, inter.t, nodes[cur])) {
            continue;
        }

        if (!is_leaf_node(nodes[cur])) {
            stack[stack_size++] = far_child(rd, nodes[cur]);
            stack[stack_size++] = near_child(rd, nodes[cur]);
        } else {
            const uint32_t prim_index = (nodes[cur].prim_index & PRIM_INDEX_BITS);
            for (uint32_t i = prim_index; i < prim_index + nodes[cur].prim_count; ++i) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                if ((mi.ray_visibility & ray_vismask) == 0) {
                    continue;
                }

                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(ro, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) {
                    continue;
                }

                float _ro[3], _rd[3];
                TransformRay(ro, rd, tr.inv_xform, _ro, _rd);

                float _inv_d[3];
                safe_invert(_rd, _inv_d);

                const bool solid_hit_found = Traverse_BLAS_WithStack_AnyHit(
                    _ro, _rd, _inv_d, nodes, m.node_index, mtris, materials, tri_indices, int(mi_indices[i]), inter);
                if (solid_hit_found) {
                    return true;
                }
            }
        }
    }

    // resolve primitive index indirection
    if (inter.prim_index < 0) {
        inter.prim_index = -int(tri_indices[-inter.prim_index - 1]) - 1;
    } else {
        inter.prim_index = int(tri_indices[inter.prim_index]);
    }

    return false;
}

bool Ray::Ref::Traverse_TLAS_WithStack_AnyHit(const float ro[3], const float rd[3], int ray_type,
                                              const wbvh_node_t *nodes, const uint32_t root_index,
                                              const mesh_instance_t *mesh_instances, const uint32_t *mi_indices,
                                              const mesh_t *meshes, const transform_t *transforms,
                                              const tri_accel_t *tris, const tri_mat_data_t *materials,
                                              const uint32_t *tri_indices, hit_data_t &inter) {
    const int ray_dir_oct = ((rd[2] > 0.0f) << 2) | ((rd[1] > 0.0f) << 1) | (rd[0] > 0.0f);
    const uint32_t ray_vismask = (1u << ray_type);

    int child_order[8];
    UNROLLED_FOR(i, 8, { child_order[i] = i ^ ray_dir_oct; })

    float inv_d[3];
    safe_invert(rd, inv_d);

    TraversalStack<MAX_STACK_SIZE> st;
    st.push(root_index, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > inter.t) {
            continue;
        }

    TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            alignas(16) float dist[8];
            long mask = bbox_test_oct(ro, inv_d, inter.t, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                long i2 = GetFirstBit(mask);
                mask = ClearBit(mask, i2);
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

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                const uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            const uint32_t prim_index = (nodes[cur.index].child[0] & PRIM_INDEX_BITS);
            for (uint32_t i = prim_index; i < prim_index + nodes[cur.index].child[1]; ++i) {
                const mesh_instance_t &mi = mesh_instances[mi_indices[i]];
                if ((mi.ray_visibility & ray_vismask) == 0) {
                    continue;
                }

                const mesh_t &m = meshes[mi.mesh_index];
                const transform_t &tr = transforms[mi.tr_index];

                if (!bbox_test(ro, inv_d, inter.t, mi.bbox_min, mi.bbox_max)) {
                    continue;
                }

                float _ro[3], _rd[3];
                TransformRay(ro, rd, tr.inv_xform, _ro, _rd);

                float _inv_d[3];
                safe_invert(_rd, _inv_d);
                const bool solid_hit_found = Traverse_BLAS_WithStack_AnyHit(
                    _ro, _rd, _inv_d, nodes, m.node_index, tris, materials, tri_indices, int(mi_indices[i]), inter);
                if (solid_hit_found) {
                    return true;
                }
            }
        }
    }

    // resolve primitive index indirection
    if (inter.prim_index < 0) {
        inter.prim_index = -int(tri_indices[-inter.prim_index - 1]) - 1;
    } else {
        inter.prim_index = int(tri_indices[inter.prim_index]);
    }

    return false;
}

bool Ray::Ref::Traverse_BLAS_WithStack_ClosestHit(const float ro[3], const float rd[3], const float inv_d[3],
                                                  const bvh_node_t *nodes, const uint32_t root_index,
                                                  const tri_accel_t *tris, const int obj_index, hit_data_t &inter) {
    bool res = false;

    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack[stack_size++] = root_index;

    while (stack_size) {
        const uint32_t cur = stack[--stack_size];

        if (!bbox_test(ro, inv_d, inter.t, nodes[cur])) {
            continue;
        }

        if (!is_leaf_node(nodes[cur])) {
            stack[stack_size++] = far_child(rd, nodes[cur]);
            stack[stack_size++] = near_child(rd, nodes[cur]);
        } else {
            const int tri_start = int(nodes[cur].prim_index & PRIM_INDEX_BITS),
                      tri_end = int(tri_start + nodes[cur].prim_count);
            res |= IntersectTris_ClosestHit(ro, rd, tris, tri_start, tri_end, obj_index, inter);
        }
    }

    return res;
}

bool Ray::Ref::Traverse_BLAS_WithStack_ClosestHit(const float ro[3], const float rd[3], const float inv_d[3],
                                                  const wbvh_node_t *nodes, const uint32_t root_index,
                                                  const mtri_accel_t *mtris, int obj_index, hit_data_t &inter) {
    bool res = false;

    TraversalStack<MAX_STACK_SIZE> st;
    st.push(root_index, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > inter.t) {
            continue;
        }

    TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            alignas(16) float dist[8];
            long mask = bbox_test_oct(ro, inv_d, inter.t, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                const long i2 = GetFirstBit(mask);
                mask = ClearBit(mask, i2);
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

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                const int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            const int tri_start = int(nodes[cur.index].child[0] & PRIM_INDEX_BITS),
                      tri_end = int(tri_start + nodes[cur.index].child[1]);
            res |= IntersectTris_ClosestHit(ro, rd, mtris, tri_start, tri_end, obj_index, inter);
        }
    }

    return res;
}

bool Ray::Ref::Traverse_BLAS_WithStack_AnyHit(const float ro[3], const float rd[3], const float inv_d[3],
                                              const bvh_node_t *nodes, uint32_t root_index, const mtri_accel_t *mtris,
                                              const tri_mat_data_t *materials, const uint32_t *tri_indices,
                                              int obj_index, hit_data_t &inter) {
    uint32_t stack[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack[stack_size++] = root_index;

    while (stack_size) {
        const uint32_t cur = stack[--stack_size];

        if (!bbox_test(ro, inv_d, inter.t, nodes[cur])) {
            continue;
        }

        if (!is_leaf_node(nodes[cur])) {
            stack[stack_size++] = far_child(rd, nodes[cur]);
            stack[stack_size++] = near_child(rd, nodes[cur]);
        } else {
            const int tri_start = int(nodes[cur].prim_index & PRIM_INDEX_BITS),
                      tri_end = int(tri_start + nodes[cur].prim_count);
            const bool hit_found =
                IntersectTris_AnyHit(ro, rd, mtris, materials, tri_indices, tri_start, tri_end, obj_index, inter);
            if (hit_found) {
                const bool is_backfacing = inter.prim_index < 0;
                const uint32_t prim_index = is_backfacing ? -inter.prim_index - 1 : inter.prim_index;

                if ((!is_backfacing && (materials[tri_indices[prim_index]].front_mi & MATERIAL_SOLID_BIT)) ||
                    (is_backfacing && (materials[tri_indices[prim_index]].back_mi & MATERIAL_SOLID_BIT))) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool Ray::Ref::Traverse_BLAS_WithStack_AnyHit(const float ro[3], const float rd[3], const float inv_d[3],
                                              const wbvh_node_t *nodes, const uint32_t root_index,
                                              const tri_accel_t *tris, const tri_mat_data_t *materials,
                                              const uint32_t *tri_indices, int obj_index, hit_data_t &inter) {
    TraversalStack<MAX_STACK_SIZE> st;
    st.push(root_index, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > inter.t) {
            continue;
        }

    TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            alignas(16) float dist[8];
            long mask = bbox_test_oct(ro, inv_d, inter.t, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                const long i2 = GetFirstBit(mask);
                mask = ClearBit(mask, i2);
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

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                const int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            const int tri_start = int(nodes[cur.index].child[0] & PRIM_INDEX_BITS),
                      tri_end = int(tri_start + nodes[cur.index].child[1]);
            const bool hit_found =
                IntersectTris_AnyHit(ro, rd, tris, materials, tri_indices, tri_start, tri_end, obj_index, inter);
            if (hit_found) {
                const bool is_backfacing = inter.prim_index < 0;
                const uint32_t prim_index = is_backfacing ? -inter.prim_index - 1 : inter.prim_index;

                if ((!is_backfacing && (materials[tri_indices[prim_index]].front_mi & MATERIAL_SOLID_BIT)) ||
                    (is_backfacing && (materials[tri_indices[prim_index]].back_mi & MATERIAL_SOLID_BIT))) {
                    return true;
                }
            }
        }
    }

    return false;
}

float Ray::Ref::BRDF_PrincipledDiffuse(const simd_fvec4 &V, const simd_fvec4 &N, const simd_fvec4 &L,
                                       const simd_fvec4 &H, const float roughness) {
    const float N_dot_L = dot(N, L);
    const float N_dot_V = dot(N, V);
    if (N_dot_L <= 0.0f /*|| N_dot_V <= 0.0f*/) {
        return 0.0f;
    }

    const float FL = schlick_weight(N_dot_L);
    const float FV = schlick_weight(N_dot_V);

    const float L_dot_H = dot(L, H);
    const float Fd90 = 0.5f + 2.0f * L_dot_H * L_dot_H * roughness;
    const float Fd = mix(1.0f, Fd90, FL) * mix(1.0f, Fd90, FV);

    return Fd;
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_OrenDiffuse_BSDF(const simd_fvec4 &V, const simd_fvec4 &N, const simd_fvec4 &L,
                                                         const float roughness, const simd_fvec4 &base_color) {
    const float sigma = roughness;
    const float div = 1.0f / (PI + ((3.0f * PI - 4.0f) / 6.0f) * sigma);

    const float a = 1.0f * div;
    const float b = sigma * div;

    ////

    const float nl = fmaxf(dot(N, L), 0.0f);
    const float nv = fmaxf(dot(N, V), 0.0f);
    float t = dot(L, V) - nl * nv;

    if (t > 0.0f) {
        t /= fmaxf(nl, nv) + FLT_MIN;
    }
    const float is = nl * (a + b * t);

    simd_fvec4 diff_col = is * base_color;
    diff_col.set<3>(0.5f / PI);

    return diff_col;
}

Ray::Ref::simd_fvec4 Ray::Ref::Sample_OrenDiffuse_BSDF(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                                                       const simd_fvec4 &I, const float roughness,
                                                       const simd_fvec4 &base_color, const float rand_u,
                                                       const float rand_v, simd_fvec4 &out_V) {

    const float phi = 2 * PI * rand_v;
    const float cos_phi = cosf(phi), sin_phi = sinf(phi);

    const float dir = sqrtf(1.0f - rand_u * rand_u);
    auto V = simd_fvec4{dir * cos_phi, dir * sin_phi, rand_u, 0.0f}; // in tangent-space

    out_V = world_from_tangent(T, B, N, V);
    return Evaluate_OrenDiffuse_BSDF(-I, N, out_V, roughness, base_color);
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_PrincipledDiffuse_BSDF(const simd_fvec4 &V, const simd_fvec4 &N,
                                                               const simd_fvec4 &L, const float roughness,
                                                               const simd_fvec4 &base_color,
                                                               const simd_fvec4 &sheen_color,
                                                               const bool uniform_sampling) {
    float weight, pdf;
    if (uniform_sampling) {
        weight = 2 * dot(N, L);
        pdf = 0.5f / PI;
    } else {
        weight = 1.0f;
        pdf = dot(N, L) / PI;
    }

    simd_fvec4 H = normalize(L + V);
    if (dot(V, H) < 0.0f) {
        H = -H;
    }

    simd_fvec4 diff_col = base_color * (weight * BRDF_PrincipledDiffuse(V, N, L, H, roughness));

    const float FH = PI * schlick_weight(dot(L, H));
    diff_col += FH * sheen_color;
    diff_col.set<3>(pdf);

    return diff_col;
}

Ray::Ref::simd_fvec4 Ray::Ref::Sample_PrincipledDiffuse_BSDF(const simd_fvec4 &T, const simd_fvec4 &B,
                                                             const simd_fvec4 &N, const simd_fvec4 &I,
                                                             const float roughness, const simd_fvec4 &base_color,
                                                             const simd_fvec4 &sheen_color, const bool uniform_sampling,
                                                             const float rand_u, const float rand_v,
                                                             simd_fvec4 &out_V) {
    const float phi = 2 * PI * rand_v;
    const float cos_phi = cosf(phi), sin_phi = sinf(phi);

    simd_fvec4 V;
    if (uniform_sampling) {
        const float dir = sqrtf(1.0f - rand_u * rand_u);
        V = simd_fvec4{dir * cos_phi, dir * sin_phi, rand_u, 0.0f}; // in tangent-space
    } else {
        const float dir = sqrtf(rand_u);
        const float k = sqrtf(1.0f - rand_u);
        V = simd_fvec4{dir * cos_phi, dir * sin_phi, k, 0.0f}; // in tangent-space
    }

    out_V = world_from_tangent(T, B, N, V);
    return Evaluate_PrincipledDiffuse_BSDF(-I, N, out_V, roughness, base_color, sheen_color, uniform_sampling);
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_GGXSpecular_BSDF(const simd_fvec4 &view_dir_ts,
                                                         const simd_fvec4 &sampled_normal_ts,
                                                         const simd_fvec4 &reflected_dir_ts, const float alpha_x,
                                                         const float alpha_y, const float spec_ior, const float spec_F0,
                                                         const simd_fvec4 &spec_col) {
#if USE_VNDF_GGX_SAMPLING == 1
    const float D = D_GGX(sampled_normal_ts, alpha_x, alpha_y);
#else
    const float D = D_GTR2(sampled_normal_ts.get<2>(), alpha_x);
#endif

    const float G = G1(view_dir_ts, alpha_x, alpha_y) * G1(reflected_dir_ts, alpha_x, alpha_y);

    const float FH =
        (fresnel_dielectric_cos(dot(view_dir_ts, sampled_normal_ts), spec_ior) - spec_F0) / (1.0f - spec_F0);
    simd_fvec4 F = mix(spec_col, simd_fvec4(1.0f), FH);

    const float denom = 4.0f * fabsf(view_dir_ts.get<2>() * reflected_dir_ts.get<2>());
    F *= (denom != 0.0f) ? (D * G / denom) : 0.0f;

#if USE_VNDF_GGX_SAMPLING == 1
    float pdf = safe_div_pos(D * G1(view_dir_ts, alpha_x, alpha_y) * fmaxf(dot(view_dir_ts, sampled_normal_ts), 0.0f),
                             fabsf(view_dir_ts.get<2>()));
    const float div = 4.0f * dot(view_dir_ts, sampled_normal_ts);
    if (div != 0.0f) {
        pdf /= div;
    }
#else
    const float pdf = D * sampled_normal_ts.get<2>() / (4.0f * dot(view_dir_ts, sampled_normal_ts));
#endif

    F *= fmaxf(reflected_dir_ts.get<2>(), 0.0f);
    F.set<3>(pdf);

    return F;
}

Ray::Ref::simd_fvec4 Ray::Ref::Sample_GGXSpecular_BSDF(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                                                       const simd_fvec4 &I, const float roughness,
                                                       const float anisotropic, const float spec_ior,
                                                       const float spec_F0, const simd_fvec4 &spec_col,
                                                       const float rand_u, const float rand_v, simd_fvec4 &out_V) {
    const float roughness2 = sqr(roughness);
    const float aspect = sqrtf(1.0f - 0.9f * anisotropic);

    const float alpha_x = roughness2 / aspect;
    const float alpha_y = roughness2 * aspect;

    if (alpha_x * alpha_y < 1e-7f) {
        const simd_fvec4 V = reflect(I, N, dot(N, I));
        const float FH = (fresnel_dielectric_cos(dot(V, N), spec_ior) - spec_F0) / (1.0f - spec_F0);
        simd_fvec4 F = mix(spec_col, simd_fvec4(1.0f), FH);
        out_V = V;
        return simd_fvec4{F.get<0>() * 1e6f, F.get<1>() * 1e6f, F.get<2>() * 1e6f, 1e6f};
    }

    const simd_fvec4 view_dir_ts = normalize(tangent_from_world(T, B, N, -I));
#if USE_VNDF_GGX_SAMPLING == 1
    const simd_fvec4 sampled_normal_ts = SampleGGX_VNDF(view_dir_ts, alpha_x, alpha_y, rand_u, rand_v);
#else
    const simd_fvec4 sampled_normal_ts = Sample_GGX_NDF(alpha_x, rand_u, rand_v);
#endif
    const float dot_N_V = -dot(sampled_normal_ts, view_dir_ts);
    const simd_fvec4 reflected_dir_ts = normalize(reflect(-view_dir_ts, sampled_normal_ts, dot_N_V));

    out_V = world_from_tangent(T, B, N, reflected_dir_ts);
    return Evaluate_GGXSpecular_BSDF(view_dir_ts, sampled_normal_ts, reflected_dir_ts, alpha_x, alpha_y, spec_ior,
                                     spec_F0, spec_col);
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_GGXRefraction_BSDF(const simd_fvec4 &view_dir_ts,
                                                           const simd_fvec4 &sampled_normal_ts,
                                                           const simd_fvec4 &refr_dir_ts, float roughness2, float eta,
                                                           const simd_fvec4 &refr_col) {

    if (refr_dir_ts.get<2>() >= 0.0f || view_dir_ts.get<2>() <= 0.0f) {
        return simd_fvec4{0.0f};
    }

#if USE_VNDF_GGX_SAMPLING == 1
    const float D = D_GGX(sampled_normal_ts, roughness2, roughness2);
#else
    const float D = D_GTR2(sampled_normal_ts.get<2>(), roughness2);
#endif

    const float G1o = G1(refr_dir_ts, roughness2, roughness2);
    const float G1i = G1(view_dir_ts, roughness2, roughness2);

    const float denom = dot(refr_dir_ts, sampled_normal_ts) + dot(view_dir_ts, sampled_normal_ts) * eta;
    const float jacobian = denom != 0.0f ? fmaxf(-dot(refr_dir_ts, sampled_normal_ts), 0.0f) / (denom * denom) : 0.0f;

    float F = D * G1i * G1o * fmaxf(dot(view_dir_ts, sampled_normal_ts), 0.0f) * jacobian /
              (/*-refr_dir_ts.get<2>() */ view_dir_ts.get<2>());

#if USE_VNDF_GGX_SAMPLING == 1
    float pdf = D * G1o * fmaxf(dot(view_dir_ts, sampled_normal_ts), 0.0f) * jacobian / view_dir_ts.get<2>();
#else
    // const float pdf = D * fmaxf(sampled_normal_ts.get<2>(), 0.0f) * jacobian;
    const float pdf = D * sampled_normal_ts.get<2>() * fmaxf(-dot(refr_dir_ts, sampled_normal_ts), 0.0f) / denom;
#endif

    simd_fvec4 ret = F * refr_col;
    // ret *= (-refr_dir_ts.get<2>());
    ret.set<3>(pdf);

    return ret;
}

Ray::Ref::simd_fvec4 Ray::Ref::Sample_GGXRefraction_BSDF(const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                                                         const simd_fvec4 &I, float roughness, const float eta,
                                                         const simd_fvec4 &refr_col, const float rand_u,
                                                         const float rand_v, simd_fvec4 &out_V) {
    const float roughness2 = sqr(roughness);
    if (roughness2 * roughness2 < 1e-7f) {
        const float cosi = -dot(I, N);
        const float cost2 = 1.0f - eta * eta * (1.0f - cosi * cosi);
        if (cost2 < 0) {
            return simd_fvec4{0.0f};
        }
        const float m = eta * cosi - sqrtf(cost2);
        const simd_fvec4 V = normalize(eta * I + m * N);

        out_V = simd_fvec4{V.get<0>(), V.get<1>(), V.get<2>(), m};
        return simd_fvec4{refr_col.get<0>() * 1e6f, refr_col.get<1>() * 1e6f, refr_col.get<2>() * 1e6f, 1e6f};
    }

    const simd_fvec4 view_dir_ts = normalize(tangent_from_world(T, B, N, -I));
#if USE_VNDF_GGX_SAMPLING == 1
    const simd_fvec4 sampled_normal_ts = SampleGGX_VNDF(view_dir_ts, roughness2, roughness2, rand_u, rand_v);
#else
    const simd_fvec4 sampled_normal_ts = Sample_GGX_NDF(roughness2, rand_u, rand_v);
#endif

    const float cosi = dot(view_dir_ts, sampled_normal_ts);
    const float cost2 = 1.0f - eta * eta * (1.0f - cosi * cosi);
    if (cost2 < 0) {
        return simd_fvec4{0.0f};
    }
    const float m = eta * cosi - sqrtf(cost2);
    const simd_fvec4 refr_dir_ts = normalize(-eta * view_dir_ts + m * sampled_normal_ts);

    const simd_fvec4 F =
        Evaluate_GGXRefraction_BSDF(view_dir_ts, sampled_normal_ts, refr_dir_ts, roughness2, eta, refr_col);

    const simd_fvec4 V = world_from_tangent(T, B, N, refr_dir_ts);
    out_V = simd_fvec4{V.get<0>(), V.get<1>(), V.get<2>(), m};
    return F;
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_PrincipledClearcoat_BSDF(const simd_fvec4 &view_dir_ts,
                                                                 const simd_fvec4 &sampled_normal_ts,
                                                                 const simd_fvec4 &reflected_dir_ts,
                                                                 const float clearcoat_roughness2,
                                                                 const float clearcoat_ior, const float clearcoat_F0) {
    const float D = D_GTR1(sampled_normal_ts.get<2>(), clearcoat_roughness2);
    // Always assume roughness of 0.25 for clearcoat
    const float clearcoat_alpha = (0.25f * 0.25f);
    const float G =
        G1(view_dir_ts, clearcoat_alpha, clearcoat_alpha) * G1(reflected_dir_ts, clearcoat_alpha, clearcoat_alpha);

    const float FH = (fresnel_dielectric_cos(dot(reflected_dir_ts, sampled_normal_ts), clearcoat_ior) - clearcoat_F0) /
                     (1.0f - clearcoat_F0);
    float F = mix(0.04f, 1.0f, FH);

    const float denom = 4.0f * fabsf(view_dir_ts.get<2>()) * fabsf(reflected_dir_ts.get<2>());
    F *= (denom != 0.0f) ? D * G / denom : 0.0f;

#if USE_VNDF_GGX_SAMPLING == 1
    float pdf = D * G1(view_dir_ts, clearcoat_alpha, clearcoat_alpha) *
                fmaxf(dot(view_dir_ts, sampled_normal_ts), 0.0f) / fabsf(view_dir_ts.get<2>());
    const float div = 4.0f * dot(view_dir_ts, sampled_normal_ts);
    if (div != 0.0f) {
        pdf /= div;
    }
#else
    float pdf = D * sampled_normal_ts.get<2>() / (4.0f * dot(view_dir_ts, sampled_normal_ts));
#endif

    F *= fmaxf(reflected_dir_ts.get<2>(), 0.0f);
    return simd_fvec4{F, F, F, pdf};
}

Ray::Ref::simd_fvec4 Ray::Ref::Sample_PrincipledClearcoat_BSDF(const simd_fvec4 &T, const simd_fvec4 &B,
                                                               const simd_fvec4 &N, const simd_fvec4 &I,
                                                               const float clearcoat_roughness2,
                                                               const float clearcoat_ior, const float clearcoat_F0,
                                                               const float rand_u, const float rand_v,
                                                               simd_fvec4 &out_V) {
    if (sqr(clearcoat_roughness2) < 1e-7f) {
        const simd_fvec4 V = reflect(I, N, dot(N, I));

        const float FH = (fresnel_dielectric_cos(dot(V, N), clearcoat_ior) - clearcoat_F0) / (1.0f - clearcoat_F0);
        const float F = mix(0.04f, 1.0f, FH);

        out_V = V;
        return simd_fvec4{F * 1e6f, F * 1e6f, F * 1e6f, 1e6f};
    }

    const simd_fvec4 view_dir_ts = normalize(tangent_from_world(T, B, N, -I));
    // NOTE: GTR1 distribution is not used for sampling because Cycles does it this way (???!)
#if USE_VNDF_GGX_SAMPLING == 1
    const simd_fvec4 sampled_normal_ts =
        SampleGGX_VNDF(view_dir_ts, clearcoat_roughness2, clearcoat_roughness2, rand_u, rand_v);
#else
    const simd_fvec4 sampled_normal_ts = Sample_GGX_NDF(clearcoat_roughness2, rand_u, rand_v);
#endif
    const float dot_N_V = -dot(sampled_normal_ts, view_dir_ts);
    const simd_fvec4 reflected_dir_ts = normalize(reflect(-view_dir_ts, sampled_normal_ts, dot_N_V));

    out_V = world_from_tangent(T, B, N, reflected_dir_ts);

    return Evaluate_PrincipledClearcoat_BSDF(view_dir_ts, sampled_normal_ts, reflected_dir_ts, clearcoat_roughness2,
                                             clearcoat_ior, clearcoat_F0);
}

float Ray::Ref::Evaluate_EnvQTree(const float y_rotation, const simd_fvec4 *const *qtree_mips, const int qtree_levels,
                                  const simd_fvec4 &L) {
    int res = 2;
    int lod = qtree_levels - 1;

    simd_fvec2 p;
    DirToCanonical(value_ptr(L), -y_rotation, value_ptr(p));
    float factor = 1.0f;

    while (lod >= 0) {
        const int x = clamp(int(p.get<0>() * float(res)), 0, res - 1);
        const int y = clamp(int(p.get<1>() * float(res)), 0, res - 1);

        int index = 0;
        index |= (x & 1) << 0;
        index |= (y & 1) << 1;

        const int qx = x / 2;
        const int qy = y / 2;

        const simd_fvec4 quad = qtree_mips[lod][qy * res / 2 + qx];
        const float total = quad.get<0>() + quad.get<1>() + quad.get<2>() + quad.get<3>();
        if (total <= 0.0f) {
            break;
        }

        factor *= 4.0f * quad[index] / total;

        --lod;
        res *= 2;
    }

    return factor / (4.0f * PI);
}

Ray::Ref::simd_fvec4 Ray::Ref::Sample_EnvQTree(const float y_rotation, const simd_fvec4 *const *qtree_mips,
                                               const int qtree_levels, const float rand, const float rx,
                                               const float ry) {
    int res = 2;
    float step = 1.0f / float(res);

    float sample = rand;
    int lod = qtree_levels - 1;

    simd_fvec2 origin = {0.0f, 0.0f};
    float factor = 1.0f;

    while (lod >= 0) {
        const int qx = int(origin.get<0>() * float(res)) / 2;
        const int qy = int(origin.get<1>() * float(res)) / 2;

        const simd_fvec4 quad = qtree_mips[lod][qy * res / 2 + qx];

        const float top_left = quad.get<0>();
        const float top_right = quad.get<1>();
        float partial = top_left + quad.get<2>();
        const float total = partial + top_right + quad.get<3>();
        if (total <= 0.0f) {
            break;
        }

        float boundary = partial / total;

        int index = 0;
        if (sample < boundary) {
            assert(partial > 0.0f);
            sample /= boundary;
            boundary = top_left / partial;
        } else {
            partial = total - partial;
            assert(partial > 0.0f);
            origin.set<0>(origin.get<0>() + step);
            sample = (sample - boundary) / (1.0f - boundary);
            boundary = top_right / partial;
            index |= (1 << 0);
        }

        if (sample < boundary) {
            sample /= boundary;
        } else {
            origin.set<1>(origin.get<1>() + step);
            sample = (sample - boundary) / (1.0f - boundary);
            index |= (1 << 1);
        }

        factor *= 4.0f * quad[index] / total;

        --lod;
        res *= 2;
        step *= 0.5f;
    }

    origin += 2 * step * simd_fvec2{rx, ry};

    // origin = simd_fvec2{rx, ry};
    // factor = 1.0f;

    simd_fvec4 dir_and_pdf;
    CanonicalToDir(value_ptr(origin), y_rotation, value_ptr(dir_and_pdf));
    dir_and_pdf.set<3>(factor / (4.0f * PI));

    return dir_and_pdf;
}

void Ray::Ref::TransformRay(const float ro[3], const float rd[3], const float *xform, float out_ro[3],
                            float out_rd[3]) {
    out_ro[0] = xform[0] * ro[0] + xform[4] * ro[1] + xform[8] * ro[2] + xform[12];
    out_ro[1] = xform[1] * ro[0] + xform[5] * ro[1] + xform[9] * ro[2] + xform[13];
    out_ro[2] = xform[2] * ro[0] + xform[6] * ro[1] + xform[10] * ro[2] + xform[14];

    out_rd[0] = xform[0] * rd[0] + xform[4] * rd[1] + xform[8] * rd[2];
    out_rd[1] = xform[1] * rd[0] + xform[5] * rd[1] + xform[9] * rd[2];
    out_rd[2] = xform[2] * rd[0] + xform[6] * rd[1] + xform[10] * rd[2];
}

Ray::Ref::simd_fvec4 Ray::Ref::TransformPoint(const simd_fvec4 &p, const float *xform) {
    return simd_fvec4{xform[0] * p.get<0>() + xform[4] * p.get<1>() + xform[8] * p.get<2>() + xform[12],
                      xform[1] * p.get<0>() + xform[5] * p.get<1>() + xform[9] * p.get<2>() + xform[13],
                      xform[2] * p.get<0>() + xform[6] * p.get<1>() + xform[10] * p.get<2>() + xform[14], 0.0f};
}

Ray::Ref::simd_fvec4 Ray::Ref::TransformDirection(const simd_fvec4 &p, const float *xform) {
    return simd_fvec4{xform[0] * p.get<0>() + xform[4] * p.get<1>() + xform[8] * p.get<2>(),
                      xform[1] * p.get<0>() + xform[5] * p.get<1>() + xform[9] * p.get<2>(),
                      xform[2] * p.get<0>() + xform[6] * p.get<1>() + xform[10] * p.get<2>(), 0.0f};
}

Ray::Ref::simd_fvec4 Ray::Ref::TransformNormal(const simd_fvec4 &n, const float *inv_xform) {
    return simd_fvec4{inv_xform[0] * n.get<0>() + inv_xform[1] * n.get<1>() + inv_xform[2] * n.get<2>(),
                      inv_xform[4] * n.get<0>() + inv_xform[5] * n.get<1>() + inv_xform[6] * n.get<2>(),
                      inv_xform[8] * n.get<0>() + inv_xform[9] * n.get<1>() + inv_xform[10] * n.get<2>(), 0.0f};
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleNearest(const Cpu::TexStorageBase *const textures[], const uint32_t index,
                                             const simd_fvec2 &uvs, const int lod) {
    const Cpu::TexStorageBase &storage = *textures[index >> 28];
    const auto &pix = storage.Fetch(int(index & 0x00ffffff), uvs.get<0>(), uvs.get<1>(), lod);
    return simd_fvec4{pix.v[0], pix.v[1], pix.v[2], pix.v[3]};
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleBilinear(const Cpu::TexStorageBase *const textures[], const uint32_t index,
                                              const simd_fvec2 &uvs, const int lod, const simd_fvec2 &rand) {
    const Cpu::TexStorageBase &storage = *textures[index >> 28];

    const int tex = int(index & 0x00ffffff);
    simd_fvec2 img_size;
    storage.GetFRes(tex, lod, value_ptr(img_size));

    simd_fvec2 _uvs = fract(uvs);
    _uvs = _uvs * img_size - 0.5f;

#if USE_STOCH_TEXTURE_FILTERING
    // Jitter UVs
    _uvs += rand;

    const auto &p00 = storage.Fetch(tex, int(_uvs.get<0>()), int(_uvs.get<1>()), lod);
    return simd_fvec4{p00.v};
#else  // USE_STOCH_TEXTURE_FILTERING
    const auto &p00 = storage.Fetch(tex, int(_uvs.get<0>()) + 0, int(_uvs.get<1>()) + 0, lod);
    const auto &p01 = storage.Fetch(tex, int(_uvs.get<0>()) + 1, int(_uvs.get<1>()) + 0, lod);
    const auto &p10 = storage.Fetch(tex, int(_uvs.get<0>()) + 0, int(_uvs.get<1>()) + 1, lod);
    const auto &p11 = storage.Fetch(tex, int(_uvs.get<0>()) + 1, int(_uvs.get<1>()) + 1, lod);

    const float kx = fract(_uvs.get<0>()), ky = fract(_uvs.get<1>());

    const auto p0 = simd_fvec4{p01.v[0] * kx + p00.v[0] * (1 - kx), p01.v[1] * kx + p00.v[1] * (1 - kx),
                               p01.v[2] * kx + p00.v[2] * (1 - kx), p01.v[3] * kx + p00.v[3] * (1 - kx)};

    const auto p1 = simd_fvec4{p11.v[0] * kx + p10.v[0] * (1 - kx), p11.v[1] * kx + p10.v[1] * (1 - kx),
                               p11.v[2] * kx + p10.v[2] * (1 - kx), p11.v[3] * kx + p10.v[3] * (1 - kx)};

    return (p1 * ky + p0 * (1.0f - ky));
#endif // USE_STOCH_TEXTURE_FILTERING
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleBilinear(const Cpu::TexStorageBase &storage, const uint32_t tex,
                                              const simd_fvec2 &iuvs, const int lod, const simd_fvec2 &rand) {
#if USE_STOCH_TEXTURE_FILTERING
    // Jitter UVs
    simd_fvec2 _uvs = iuvs + rand;

    const auto &p00 = storage.Fetch(tex, int(_uvs.get<0>()), int(_uvs.get<1>()), lod);
    return simd_fvec4{p00.v};
#else  // USE_STOCH_TEXTURE_FILTERING
    const auto &p00 = storage.Fetch(int(tex), int(iuvs.get<0>()) + 0, int(iuvs.get<1>()) + 0, lod);
    const auto &p01 = storage.Fetch(int(tex), int(iuvs.get<0>()) + 1, int(iuvs.get<1>()) + 0, lod);
    const auto &p10 = storage.Fetch(int(tex), int(iuvs.get<0>()) + 0, int(iuvs.get<1>()) + 1, lod);
    const auto &p11 = storage.Fetch(int(tex), int(iuvs.get<0>()) + 1, int(iuvs.get<1>()) + 1, lod);

    const simd_fvec2 k = fract(iuvs);

    const auto _p00 = simd_fvec4{p00.v[0], p00.v[1], p00.v[2], p00.v[3]};
    const auto _p01 = simd_fvec4{p01.v[0], p01.v[1], p01.v[2], p01.v[3]};
    const auto _p10 = simd_fvec4{p10.v[0], p10.v[1], p10.v[2], p10.v[3]};
    const auto _p11 = simd_fvec4{p11.v[0], p11.v[1], p11.v[2], p11.v[3]};

    const simd_fvec4 p0X = _p01 * k.get<0>() + _p00 * (1 - k.get<0>());
    const simd_fvec4 p1X = _p11 * k.get<0>() + _p10 * (1 - k.get<0>());

    return (p1X * k.get<1>() + p0X * (1 - k.get<1>()));
#endif // USE_STOCH_TEXTURE_FILTERING
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleTrilinear(const Cpu::TexStorageBase *const textures[], const uint32_t index,
                                               const simd_fvec2 &uvs, const float lod, const simd_fvec2 &rand) {
    const simd_fvec4 col1 = SampleBilinear(textures, index, uvs, int(floorf(lod)), rand);
    const simd_fvec4 col2 = SampleBilinear(textures, index, uvs, int(ceilf(lod)), rand);

    const float k = fract(lod);
    return col1 * (1 - k) + col2 * k;
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleAnisotropic(const Cpu::TexStorageBase *const textures[], const uint32_t index,
                                                 const simd_fvec2 &uvs, const simd_fvec2 &duv_dx,
                                                 const simd_fvec2 &duv_dy) {
    const Cpu::TexStorageBase &storage = *textures[index >> 28];
    const int tex = int(index & 0x00ffffff);

    simd_fvec2 sz;
    storage.GetFRes(tex, 0, value_ptr(sz));

    const simd_fvec2 _duv_dx = abs(duv_dx * sz);
    const simd_fvec2 _duv_dy = abs(duv_dy * sz);

    const float l1 = length(_duv_dx);
    const float l2 = length(_duv_dy);

    float lod, k;
    simd_fvec2 step;

    if (l1 <= l2) {
        lod = fast_log2(fminf(_duv_dx.get<0>(), _duv_dx.get<1>()));
        k = l1 / l2;
        step = duv_dy;
    } else {
        lod = fast_log2(fminf(_duv_dy.get<0>(), _duv_dy.get<1>()));
        k = l2 / l1;
        step = duv_dx;
    }

    lod = clamp(lod, 0.0f, float(MAX_MIP_LEVEL));

    simd_fvec2 _uvs = uvs - step * 0.5f;

    int num = int(2.0f / k);
    num = clamp(num, 1, 4);

    step = step / float(num);

    auto res = simd_fvec4{0.0f};

    const int lod1 = int(floorf(lod));
    const int lod2 = int(ceilf(lod));

    simd_fvec2 size1, size2;
    storage.GetFRes(tex, lod1, value_ptr(size1));
    storage.GetFRes(tex, lod2, value_ptr(size2));

    const float kz = fract(lod);

    for (int i = 0; i < num; ++i) {
        _uvs = fract(_uvs);

        const simd_fvec2 _uvs1 = _uvs * size1;
        res += (1 - kz) * SampleBilinear(storage, tex, _uvs1, lod1, {});

        if (kz > 0.0001f) {
            const simd_fvec2 _uvs2 = _uvs * size2;
            res += kz * SampleBilinear(storage, tex, _uvs2, lod2, {});
        }

        _uvs = _uvs + step;
    }

    return res / float(num);
}

Ray::Ref::simd_fvec4 Ray::Ref::SampleLatlong_RGBE(const Cpu::TexStorageRGBA &storage, const uint32_t index,
                                                  const simd_fvec4 &dir, float y_rotation, const simd_fvec2 &rand) {
    const float theta = acosf(clamp(dir.get<1>(), -1.0f, 1.0f)) / PI;
    float phi = atan2f(dir.get<2>(), dir.get<0>()) + y_rotation;
    if (phi < 0) {
        phi += 2 * PI;
    }
    if (phi > 2 * PI) {
        phi -= 2 * PI;
    }

    const float u = fract(0.5f * phi / PI);

    const int tex = int(index & 0x00ffffff);
    simd_fvec2 size;
    storage.GetFRes(tex, 0, value_ptr(size));

    simd_fvec2 uvs = simd_fvec2{u, theta} * size;

#if USE_STOCH_TEXTURE_FILTERING
    // Jitter UVs
    uvs += rand - 0.5f;
    const simd_ivec2 iuvs = simd_ivec2(uvs);

    const auto &p00 = storage.Get(tex, iuvs.get<0>(), iuvs.get<1>(), 0);
    return rgbe_to_rgb(p00);
#else  // USE_STOCH_TEXTURE_FILTERING
    const simd_ivec2 iuvs = simd_ivec2(uvs);

    const auto &p00 = storage.Get(tex, iuvs.get<0>() + 0, iuvs.get<1>() + 0, 0);
    const auto &p01 = storage.Get(tex, iuvs.get<0>() + 1, iuvs.get<1>() + 0, 0);
    const auto &p10 = storage.Get(tex, iuvs.get<0>() + 0, iuvs.get<1>() + 1, 0);
    const auto &p11 = storage.Get(tex, iuvs.get<0>() + 1, iuvs.get<1>() + 1, 0);

    const simd_fvec2 k = fract(uvs);

    const simd_fvec4 _p00 = rgbe_to_rgb(p00), _p01 = rgbe_to_rgb(p01);
    const simd_fvec4 _p10 = rgbe_to_rgb(p10), _p11 = rgbe_to_rgb(p11);

    const simd_fvec4 p0X = _p01 * k.get<0>() + _p00 * (1 - k.get<0>());
    const simd_fvec4 p1X = _p11 * k.get<0>() + _p10 * (1 - k.get<0>());

    return (p1X * k.get<1>() + p0X * (1 - k.get<1>()));
#endif // USE_STOCH_TEXTURE_FILTERING
}

void Ray::Ref::IntersectScene(Span<ray_data_t> rays, const int min_transp_depth, const int max_transp_depth,
                              const uint32_t rand_seq[], const uint32_t rand_seed, const int iteration,
                              const scene_data_t &sc, const uint32_t root_index,
                              const Cpu::TexStorageBase *const textures[], Span<hit_data_t> out_inter) {
    for (int i = 0; i < rays.size(); ++i) {
        ray_data_t &r = rays[i];
        hit_data_t &inter = out_inter[i];

        const simd_fvec4 rd = make_fvec3(r.d);
        simd_fvec4 ro = make_fvec3(r.o);

        const uint32_t px_hash = hash(r.xy);
        const uint32_t rand_hash = hash_combine(px_hash, rand_seed);

        uint32_t rand_dim = RAND_DIM_BASE_COUNT + total_depth(r) * RAND_DIM_BOUNCE_COUNT;
        while (true) {
            const float t_val = inter.t;

            bool hit_found = false;
            if (sc.wnodes) {
                hit_found = Traverse_TLAS_WithStack_ClosestHit(value_ptr(ro), value_ptr(rd), sc.wnodes, root_index,
                                                               sc.mesh_instances, sc.mi_indices, sc.meshes,
                                                               sc.transforms, sc.mtris, sc.tri_indices, inter);
            } else {
                hit_found = Traverse_TLAS_WithStack_ClosestHit(value_ptr(ro), value_ptr(rd), sc.nodes, root_index,
                                                               sc.mesh_instances, sc.mi_indices, sc.meshes,
                                                               sc.transforms, sc.tris, sc.tri_indices, inter);
            }

            if (!hit_found) {
                break;
            }

            const bool is_backfacing = (inter.prim_index < 0);
            const uint32_t tri_index = is_backfacing ? -inter.prim_index - 1 : inter.prim_index;

            if ((!is_backfacing && (sc.tri_materials[tri_index].front_mi & MATERIAL_SOLID_BIT)) ||
                (is_backfacing && (sc.tri_materials[tri_index].back_mi & MATERIAL_SOLID_BIT))) {
                // solid hit found
                break;
            }

            const material_t *mat = is_backfacing
                                        ? &sc.materials[sc.tri_materials[tri_index].back_mi & MATERIAL_INDEX_BITS]
                                        : &sc.materials[sc.tri_materials[tri_index].front_mi & MATERIAL_INDEX_BITS];

            const vertex_t &v1 = sc.vertices[sc.vtx_indices[tri_index * 3 + 0]];
            const vertex_t &v2 = sc.vertices[sc.vtx_indices[tri_index * 3 + 1]];
            const vertex_t &v3 = sc.vertices[sc.vtx_indices[tri_index * 3 + 2]];

            const float w = 1.0f - inter.u - inter.v;
            const simd_fvec2 uvs = simd_fvec2(v1.t) * w + simd_fvec2(v2.t) * inter.u + simd_fvec2(v3.t) * inter.v;

            const simd_fvec2 mix_term_rand =
                get_scrambled_2d_rand(rand_dim + RAND_DIM_BSDF_PICK, rand_hash, iteration - 1, rand_seq);
            const simd_fvec2 tex_rand =
                get_scrambled_2d_rand(rand_dim + RAND_DIM_TEX, rand_hash, iteration - 1, rand_seq);

            float trans_r = mix_term_rand.get<0>();

            // resolve mix material
            while (mat->type == eShadingNode::Mix) {
                float mix_val = mat->strength;
                const uint32_t base_texture = mat->textures[BASE_TEXTURE];
                if (base_texture != 0xffffffff) {
                    simd_fvec4 tex_color = SampleBilinear(textures, base_texture, uvs, 0, tex_rand);
                    if (base_texture & TEX_YCOCG_BIT) {
                        tex_color = YCoCg_to_RGB(tex_color);
                    }
                    if (base_texture & TEX_SRGB_BIT) {
                        tex_color = srgb_to_rgb(tex_color);
                    }
                    mix_val *= tex_color.get<0>();
                }

                if (trans_r > mix_val) {
                    mat = &sc.materials[mat->textures[MIX_MAT1]];
                    trans_r = safe_div_pos(trans_r - mix_val, 1.0f - mix_val);
                } else {
                    mat = &sc.materials[mat->textures[MIX_MAT2]];
                    trans_r = safe_div_pos(trans_r, mix_val);
                }
            }

            if (mat->type != eShadingNode::Transparent) {
                break;
            }

#if USE_PATH_TERMINATION
            const bool can_terminate_path = (r.depth >> 24) > min_transp_depth;
#else
            const bool can_terminate_path = false;
#endif

            const float lum = fmaxf(r.c[0], fmaxf(r.c[1], r.c[2]));
            const float p = mix_term_rand.get<1>();
            const float q = can_terminate_path ? fmaxf(0.05f, 1.0f - lum) : 0.0f;
            if (p < q || lum == 0.0f || (r.depth >> 24) + 1 >= max_transp_depth) {
                // terminate ray
                r.c[0] = r.c[1] = r.c[2] = 0.0f;
                break;
            }

            r.c[0] *= mat->base_color[0] / (1.0f - q);
            r.c[1] *= mat->base_color[1] / (1.0f - q);
            r.c[2] *= mat->base_color[2] / (1.0f - q);

            const float t = inter.t + HIT_BIAS;
            ro += rd * t;

            // discard current intersection
            inter.mask = 0;
            inter.t = t_val - inter.t;

            r.depth += 0x01000000;
            rand_dim += RAND_DIM_BOUNCE_COUNT;
        }

        inter.t += length(make_fvec3(r.o) - ro);
    }
}

Ray::Ref::simd_fvec4 Ray::Ref::IntersectScene(const shadow_ray_t &r, const int max_transp_depth, const scene_data_t &sc,
                                              const uint32_t root_index, const uint32_t rand_seq[],
                                              const uint32_t rand_seed, const int iteration,
                                              const Cpu::TexStorageBase *const textures[]) {
    const simd_fvec4 rd = make_fvec3(r.d);
    simd_fvec4 ro = make_fvec3(r.o);
    simd_fvec4 rc = make_fvec3(r.c);
    int depth = (r.depth >> 24);

    const uint32_t px_hash = hash(r.xy);
    const uint32_t rand_hash = hash_combine(px_hash, rand_seed);

    uint32_t rand_dim = RAND_DIM_BASE_COUNT + total_depth(r) * RAND_DIM_BOUNCE_COUNT;

    float dist = r.dist > 0.0f ? r.dist : MAX_DIST;
    while (dist > HIT_BIAS) {
        hit_data_t inter;
        inter.t = dist;

        bool solid_hit = false;
        if (sc.wnodes) {
            solid_hit = Traverse_TLAS_WithStack_AnyHit(value_ptr(ro), value_ptr(rd), RAY_TYPE_SHADOW, sc.wnodes,
                                                       root_index, sc.mesh_instances, sc.mi_indices, sc.meshes,
                                                       sc.transforms, sc.tris, sc.tri_materials, sc.tri_indices, inter);
        } else {
            solid_hit = Traverse_TLAS_WithStack_AnyHit(
                value_ptr(ro), value_ptr(rd), RAY_TYPE_SHADOW, sc.nodes, root_index, sc.mesh_instances, sc.mi_indices,
                sc.meshes, sc.transforms, sc.mtris, sc.tri_materials, sc.tri_indices, inter);
        }

        if (solid_hit || depth > max_transp_depth) {
            rc = 0.0f;
        }

        if (solid_hit || depth > max_transp_depth || !inter.mask) {
            break;
        }

        const bool is_backfacing = (inter.prim_index < 0);
        const uint32_t tri_index = is_backfacing ? -inter.prim_index - 1 : inter.prim_index;

        const uint32_t mat_index = is_backfacing ? (sc.tri_materials[tri_index].back_mi & MATERIAL_INDEX_BITS)
                                                 : (sc.tri_materials[tri_index].front_mi & MATERIAL_INDEX_BITS);

        const vertex_t &v1 = sc.vertices[sc.vtx_indices[tri_index * 3 + 0]];
        const vertex_t &v2 = sc.vertices[sc.vtx_indices[tri_index * 3 + 1]];
        const vertex_t &v3 = sc.vertices[sc.vtx_indices[tri_index * 3 + 2]];

        const float w = 1.0f - inter.u - inter.v;
        const simd_fvec2 sh_uvs = simd_fvec2(v1.t) * w + simd_fvec2(v2.t) * inter.u + simd_fvec2(v3.t) * inter.v;

        const simd_fvec2 tex_rand =
            get_scrambled_2d_rand(rand_dim + RAND_DIM_TEX, rand_hash, iteration - 1, rand_seq);

        struct {
            uint32_t index;
            float weight;
        } stack[16];
        int stack_size = 0;

        stack[stack_size++] = {mat_index, 1.0f};

        simd_fvec4 throughput = 0.0f;

        while (stack_size--) {
            const material_t *mat = &sc.materials[stack[stack_size].index];
            const float weight = stack[stack_size].weight;

            // resolve mix material
            if (mat->type == eShadingNode::Mix) {
                float mix_val = mat->strength;
                const uint32_t base_texture = mat->textures[BASE_TEXTURE];
                if (base_texture != 0xffffffff) {
                    simd_fvec4 tex_color = SampleBilinear(textures, base_texture, sh_uvs, 0, tex_rand);
                    if (base_texture & TEX_YCOCG_BIT) {
                        tex_color = YCoCg_to_RGB(tex_color);
                    }
                    if (base_texture & TEX_SRGB_BIT) {
                        tex_color = srgb_to_rgb(tex_color);
                    }
                    mix_val *= tex_color.get<0>();
                }

                stack[stack_size++] = {mat->textures[MIX_MAT1], weight * (1.0f - mix_val)};
                stack[stack_size++] = {mat->textures[MIX_MAT2], weight * mix_val};
            } else if (mat->type == eShadingNode::Transparent) {
                throughput += weight * make_fvec3(mat->base_color);
            }
        }

        rc *= throughput;
        if (lum(rc) < FLT_EPS) {
            break;
        }

        const float t = inter.t + HIT_BIAS;
        ro += rd * t;
        dist -= t;

        ++depth;
        rand_dim += RAND_DIM_BOUNCE_COUNT;
    }

    return rc;
}

void Ray::Ref::SampleLightSource(const simd_fvec4 &P, const simd_fvec4 &T, const simd_fvec4 &B, const simd_fvec4 &N,
                                 const scene_data_t &sc, const Cpu::TexStorageBase *const textures[],
                                 const float rand_pick_light, const simd_fvec2 rand_light_uv,
                                 const simd_fvec2 rand_tex_uv, light_sample_t &ls) {
    float u1 = rand_pick_light;

#if USE_HIERARCHICAL_NEE
    float factor = 1.0f;

    uint32_t i = 0; // start from root
    while (!is_leaf_node(sc.light_wnodes[i])) {
        alignas(16) float importance[8];
        calc_lnode_importance(sc.light_wnodes[i], P, importance);

        const float total_importance =
            hsum(simd_fvec4{&importance[0], simd_mem_aligned} + simd_fvec4{&importance[4], simd_mem_aligned});
        if (total_importance == 0.0f) {
            // failed to find lightsource for sampling
            return;
        }

        alignas(16) float factors[8];
        UNROLLED_FOR(j, 8, { factors[j] = importance[j] / total_importance; })

        float factors_cdf[9] = {};
        UNROLLED_FOR(j, 8, { factors_cdf[j + 1] = factors_cdf[j] + factors[j]; })
        // make sure cdf ends with 1.0
        UNROLLED_FOR(j, 8, {
            if (factors_cdf[j + 1] == factors_cdf[8]) {
                factors_cdf[j + 1] = 1.01f;
            }
        })

        simd_ivec4 less_eq[2] = {};
        where(simd_fvec4{&factors_cdf[1]} <= u1, less_eq[0]) = 1;
        where(simd_fvec4{&factors_cdf[5]} <= u1, less_eq[1]) = 1;

        const int next = hsum(less_eq[0] + less_eq[1]);
        assert(next < 8);

        u1 = fract((u1 - factors_cdf[next]) / factors[next]);
        i = sc.light_wnodes[i].child[next];
        factor *= factors[next];
    }
    const uint32_t light_index = (sc.light_wnodes[i].child[0] & PRIM_INDEX_BITS);
    factor = 1.0f / factor;
#else
    uint32_t light_index = std::min(uint32_t(u1 * float(sc.li_indices.size())), uint32_t(sc.li_indices.size() - 1));
    u1 = u1 * float(sc.li_indices.size()) - float(light_index);
    light_index = sc.li_indices[light_index];
    const float factor = float(sc.li_indices.size());
#endif
    const light_t &l = sc.lights[light_index];

    ls.col = make_fvec3(l.col);
    ls.cast_shadow = l.cast_shadow ? 1 : 0;
    ls.from_env = 0;

    if (l.type == LIGHT_TYPE_SPHERE) {
        const float r1 = rand_light_uv.get<0>(), r2 = rand_light_uv.get<1>();

        simd_fvec4 center_to_surface = P - make_fvec3(l.sph.pos);
        float dist_to_center = length(center_to_surface);

        center_to_surface /= dist_to_center;

        // sample hemisphere
        const float r = sqrtf(fmaxf(0.0f, 1.0f - r1 * r1));
        const float phi = 2.0f * PI * r2;
        auto sampled_dir = simd_fvec4{r * cosf(phi), r * sinf(phi), r1, 0.0f};

        simd_fvec4 LT, LB;
        create_tbn(center_to_surface, LT, LB);

        sampled_dir = LT * sampled_dir.get<0>() + LB * sampled_dir.get<1>() + center_to_surface * sampled_dir.get<2>();

        const simd_fvec4 light_surf_pos = make_fvec3(l.sph.pos) + sampled_dir * l.sph.radius;
        const simd_fvec4 light_forward = normalize(light_surf_pos - make_fvec3(l.sph.pos));

        ls.lp = offset_ray(light_surf_pos, light_forward);
        ls.L = light_surf_pos - P;
        const float ls_dist = length(ls.L);
        ls.L /= ls_dist;
        ls.area = l.sph.area;

        const float cos_theta = fabsf(dot(ls.L, light_forward));
        if (cos_theta > 0.0f) {
            ls.pdf = (ls_dist * ls_dist) / (0.5f * ls.area * cos_theta);
        }

        if (!l.visible) {
            ls.area = 0.0f;
        }

        if (l.sph.spot > 0.0f) {
            const float _dot = -dot(ls.L, simd_fvec4{l.sph.dir});
            if (_dot > 0.0f) {
                const float _angle = acosf(clamp(_dot, 0.0f, 1.0f));
                ls.col *= clamp((l.sph.spot - _angle) / l.sph.blend, 0.0f, 1.0f);
            } else {
                ls.col *= 0.0f;
            }
        }
    } else if (l.type == LIGHT_TYPE_DIR) {
        ls.L = make_fvec3(l.dir.dir);
        ls.area = 0.0f;
        ls.pdf = 1.0f;
        if (l.dir.angle != 0.0f) {
            const float r1 = rand_light_uv.get<0>(), r2 = rand_light_uv.get<1>();

            const float radius = tanf(l.dir.angle);
            ls.L = normalize(MapToCone(r1, r2, ls.L, radius));
            ls.area = PI * radius * radius;

            const float cos_theta = dot(ls.L, make_fvec3(l.dir.dir));
            ls.pdf = 1.0f / (ls.area * cos_theta);
        }
        ls.lp = P + ls.L;
        ls.dist_mul = MAX_DIST;

        if (!l.visible) {
            ls.area = 0.0f;
        }
    } else if (l.type == LIGHT_TYPE_RECT) {
        const simd_fvec4 light_pos = make_fvec3(l.rect.pos);
        const simd_fvec4 light_u = make_fvec3(l.rect.u);
        const simd_fvec4 light_v = make_fvec3(l.rect.v);

        const float r1 = rand_light_uv.get<0>() - 0.5f, r2 = rand_light_uv.get<1>() - 0.5f;

        const simd_fvec4 lp = light_pos + light_u * r1 + light_v * r2;
        const simd_fvec4 light_forward = normalize(cross(light_u, light_v));

        ls.lp = offset_ray(lp, light_forward);
        ls.L = lp - P;
        const float ls_dist = length(ls.L);
        ls.L /= ls_dist;
        ls.area = l.rect.area;

        const float cos_theta = dot(-ls.L, light_forward);
        if (cos_theta > 0.0f) {
            ls.pdf = (ls_dist * ls_dist) / (ls.area * cos_theta);
        }

        if (!l.visible) {
            ls.area = 0.0f;
        }

        if (l.sky_portal != 0) {
            simd_fvec4 env_col = make_fvec3(sc.env.env_col);
            if (sc.env.env_map != 0xffffffff) {
                env_col *= SampleLatlong_RGBE(*static_cast<const Cpu::TexStorageRGBA *>(textures[0]), sc.env.env_map,
                                              ls.L, sc.env.env_map_rotation, rand_tex_uv);
            }
            ls.col *= env_col;
            ls.from_env = 1;
        }
    } else if (l.type == LIGHT_TYPE_DISK) {
        const simd_fvec4 light_pos = make_fvec3(l.disk.pos);
        const simd_fvec4 light_u = make_fvec3(l.disk.u);
        const simd_fvec4 light_v = make_fvec3(l.disk.v);

        const float r1 = rand_light_uv.get<0>(), r2 = rand_light_uv.get<1>();

        simd_fvec2 offset = 2.0f * simd_fvec2{r1, r2} - simd_fvec2{1.0f, 1.0f};
        if (offset.get<0>() != 0.0f && offset.get<1>() != 0.0f) {
            float theta, r;
            if (fabsf(offset.get<0>()) > fabsf(offset.get<1>())) {
                r = offset.get<0>();
                theta = 0.25f * PI * (offset.get<1>() / offset.get<0>());
            } else {
                r = offset.get<1>();
                theta = 0.5f * PI - 0.25f * PI * (offset.get<0>() / offset.get<1>());
            }

            offset.set(0, 0.5f * r * cosf(theta));
            offset.set(1, 0.5f * r * sinf(theta));
        }

        const simd_fvec4 lp = light_pos + light_u * offset.get<0>() + light_v * offset.get<1>();
        const simd_fvec4 light_forward = normalize(cross(light_u, light_v));

        ls.lp = offset_ray(lp, light_forward);
        ls.L = lp - P;
        const float ls_dist = length(ls.L);
        ls.L /= ls_dist;
        ls.area = l.disk.area;

        const float cos_theta = dot(-ls.L, light_forward);
        if (cos_theta > 0.0f) {
            ls.pdf = (ls_dist * ls_dist) / (ls.area * cos_theta);
        }

        if (!l.visible) {
            ls.area = 0.0f;
        }

        if (l.sky_portal != 0) {
            simd_fvec4 env_col = make_fvec3(sc.env.env_col);
            if (sc.env.env_map != 0xffffffff) {
                env_col *= SampleLatlong_RGBE(*static_cast<const Cpu::TexStorageRGBA *>(textures[0]), sc.env.env_map,
                                              ls.L, sc.env.env_map_rotation, rand_tex_uv);
            }
            ls.col *= env_col;
            ls.from_env = 1;
        }
    } else if (l.type == LIGHT_TYPE_LINE) {
        const simd_fvec4 light_pos = make_fvec3(l.line.pos);
        const simd_fvec4 light_dir = make_fvec3(l.line.v);

        const float r1 = rand_light_uv.get<0>(), r2 = rand_light_uv.get<1>();

        const simd_fvec4 center_to_surface = P - light_pos;

        simd_fvec4 light_u = normalize(cross(center_to_surface, light_dir));
        simd_fvec4 light_v = cross(light_u, light_dir);

        const float phi = PI * r1;
        const simd_fvec4 normal = cosf(phi) * light_u + sinf(phi) * light_v;

        const simd_fvec4 lp = light_pos + normal * l.line.radius + (r2 - 0.5f) * light_dir * l.line.height;

        ls.lp = lp;
        ls.L = lp - P;
        const float ls_dist = length(ls.L);
        ls.L /= ls_dist;
        ls.area = l.line.area;

        const float cos_theta = 1.0f - fabsf(dot(ls.L, light_dir));
        if (cos_theta != 0.0f) {
            ls.pdf = (ls_dist * ls_dist) / (ls.area * cos_theta);
        }

        if (!l.visible) {
            ls.area = 0.0f;
        }
    } else if (l.type == LIGHT_TYPE_TRI) {
        const transform_t &ltr = sc.transforms[l.tri.xform_index];
        const uint32_t ltri_index = l.tri.tri_index;

        const vertex_t &v1 = sc.vertices[sc.vtx_indices[ltri_index * 3 + 0]];
        const vertex_t &v2 = sc.vertices[sc.vtx_indices[ltri_index * 3 + 1]];
        const vertex_t &v3 = sc.vertices[sc.vtx_indices[ltri_index * 3 + 2]];

        const simd_fvec4 p1 = TransformPoint(simd_fvec4(v1.p[0], v1.p[1], v1.p[2], 0.0f), ltr.xform),
                         p2 = TransformPoint(simd_fvec4(v2.p[0], v2.p[1], v2.p[2], 0.0f), ltr.xform),
                         p3 = TransformPoint(simd_fvec4(v3.p[0], v3.p[1], v3.p[2], 0.0f), ltr.xform);
        const simd_fvec2 uv1 = simd_fvec2(v1.t), uv2 = simd_fvec2(v2.t), uv3 = simd_fvec2(v3.t);

        const float r1 = sqrtf(rand_light_uv.get<0>()), r2 = rand_light_uv.get<1>();

        const simd_fvec2 luvs = uv1 * (1.0f - r1) + r1 * (uv2 * (1.0f - r2) + uv3 * r2);
        const simd_fvec4 lp = p1 * (1.0f - r1) + r1 * (p2 * (1.0f - r2) + p3 * r2);
        simd_fvec4 light_forward = cross(p2 - p1, p3 - p1);
        ls.area = 0.5f * length(light_forward);
        light_forward = normalize(light_forward);

        ls.L = lp - P;
        const float ls_dist = length(ls.L);
        ls.L /= ls_dist;

        float cos_theta = -dot(ls.L, light_forward);
        ls.lp = offset_ray(lp, cos_theta >= 0.0f ? light_forward : -light_forward);
        if (l.doublesided) {
            cos_theta = fabsf(cos_theta);
        }

        if (cos_theta > 0.0f) {
            ls.pdf = (ls_dist * ls_dist) / (ls.area * cos_theta);
            if (l.tri.tex_index != 0xffffffff) {
                simd_fvec4 tex_color = SampleBilinear(textures, l.tri.tex_index, luvs, 0 /* lod */, rand_tex_uv);
                if (l.tri.tex_index & TEX_YCOCG_BIT) {
                    tex_color = YCoCg_to_RGB(tex_color);
                }
                if (l.tri.tex_index & TEX_SRGB_BIT) {
                    tex_color = srgb_to_rgb(tex_color);
                }
                ls.col *= tex_color;
            }
        }
    } else if (l.type == LIGHT_TYPE_ENV) {
        const float rx = rand_light_uv.get<0>(), ry = rand_light_uv.get<1>();

        simd_fvec4 dir_and_pdf;
        if (sc.env.qtree_levels) {
            // Sample environment using quadtree
            const auto *qtree_mips = reinterpret_cast<const simd_fvec4 *const *>(sc.env.qtree_mips);
            dir_and_pdf = Sample_EnvQTree(sc.env.env_map_rotation, qtree_mips, sc.env.qtree_levels, u1, rx, ry);
        } else {
            // Sample environment as hemishpere
            const float phi = 2 * PI * ry;
            const float cos_phi = cosf(phi), sin_phi = sinf(phi);

            const float dir = sqrtf(1.0f - rx * rx);
            auto V = simd_fvec4{dir * cos_phi, dir * sin_phi, rx, 0.0f}; // in tangent-space

            dir_and_pdf = world_from_tangent(T, B, N, V);
            dir_and_pdf.set<3>(0.5f / PI);
        }

        ls.L = simd_fvec4{dir_and_pdf.get<0>(), dir_and_pdf.get<1>(), dir_and_pdf.get<2>(), 0.0f};
        ls.col *= {sc.env.env_col[0], sc.env.env_col[1], sc.env.env_col[2], 0.0f};

        if (sc.env.env_map != 0xffffffff) {
            ls.col *= SampleLatlong_RGBE(*static_cast<const Cpu::TexStorageRGBA *>(textures[0]), sc.env.env_map, ls.L,
                                         sc.env.env_map_rotation, rand_tex_uv);
        }

        ls.area = 1.0f;
        ls.lp = P + ls.L;
        ls.dist_mul = MAX_DIST;
        ls.pdf = dir_and_pdf.get<3>();
        ls.from_env = 1;
    }

    ls.pdf /= factor;
}

void Ray::Ref::IntersectAreaLights(Span<const ray_data_t> rays, Span<const light_t> lights,
                                   Span<const light_wbvh_node_t> nodes, Span<hit_data_t> inout_inters) {
    for (int i = 0; i < rays.size(); ++i) {
        const ray_data_t &ray = rays[i];
        hit_data_t &inout_inter = inout_inters[i];

        const simd_fvec4 ro = make_fvec3(ray.o);
        const simd_fvec4 rd = make_fvec3(ray.d);

        float inv_d[3];
        safe_invert(value_ptr(rd), inv_d);

        ////

        TraversalStack<MAX_STACK_SIZE, light_stack_entry_t> st;
        st.push(0u /* root_index */, 0.0f /* distance */, 1.0f /* factor */);

        while (!st.empty()) {
            light_stack_entry_t cur = st.pop();

            if (cur.dist > inout_inter.t || cur.factor == 0.0f) {
                continue;
            }

        TRAVERSE:
            if (!is_leaf_node(nodes[cur.index])) {
                alignas(16) float dist[8];
                long mask = bbox_test_oct(value_ptr(ro), inv_d, inout_inter.t, nodes[cur.index], dist);
                if (mask) {
                    simd_fvec4 importance[2];
                    calc_lnode_importance(nodes[cur.index], ro, value_ptr(importance[0]));

                    const float total_importance = hsum(importance[0] + importance[1]);
                    assert(total_importance > 0.0f);

                    importance[0] /= total_importance;
                    importance[1] /= total_importance;

                    alignas(16) float factors[8];
                    importance[0].store_to(&factors[0], simd_mem_aligned);
                    importance[1].store_to(&factors[4], simd_mem_aligned);

                    long i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    if (mask == 0) { // only one box was hit
                        cur.index = nodes[cur.index].child[i];
                        cur.factor *= factors[i];
                        goto TRAVERSE;
                    }

                    const long i2 = GetFirstBit(mask);
                    mask = ClearBit(mask, i2);
                    if (mask == 0) { // two boxes were hit
                        if (dist[i] < dist[i2]) {
                            st.push(nodes[cur.index].child[i2], dist[i2], cur.factor * factors[i2]);
                            cur.index = nodes[cur.index].child[i];
                            cur.factor *= factors[i];
                        } else {
                            st.push(nodes[cur.index].child[i], dist[i], cur.factor * factors[i]);
                            cur.index = nodes[cur.index].child[i2];
                            cur.factor *= factors[i2];
                        }
                        goto TRAVERSE;
                    }

                    st.push(nodes[cur.index].child[i], dist[i], cur.factor * factors[i]);
                    st.push(nodes[cur.index].child[i2], dist[i2], cur.factor * factors[i2]);

                    i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i], cur.factor * factors[i]);
                    if (mask == 0) { // three boxes were hit
                        st.sort_top3();
                        cur = st.pop();
                        goto TRAVERSE;
                    }

                    i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i], cur.factor * factors[i]);
                    if (mask == 0) { // four boxes were hit
                        st.sort_top4();
                        cur = st.pop();
                        goto TRAVERSE;
                    }

                    uint32_t size_before = st.stack_size;

                    // from five to eight boxes were hit
                    do {
                        i = GetFirstBit(mask);
                        mask = ClearBit(mask, i);
                        st.push(nodes[cur.index].child[i], dist[i], cur.factor * factors[i]);
                    } while (mask != 0);

                    const int count = int(st.stack_size - size_before + 4);
                    st.sort_topN(count);
                    cur = st.pop();
                    goto TRAVERSE;
                }
            } else {
                const int light_index = int(nodes[cur.index].child[0] & PRIM_INDEX_BITS);
                assert(nodes[cur.index].child[1] == 1);

                ////

                const light_t &l = lights[light_index];
                if (!l.visible) {
                    continue;
                }
                if (l.sky_portal && inout_inter.mask != 0) {
                    // Portal lights affect only missed rays
                    continue;
                }

                const bool no_shadow = (l.cast_shadow == 0);
                if (l.type == LIGHT_TYPE_SPHERE) {
                    const simd_fvec4 light_pos = make_fvec3(l.sph.pos);
                    const simd_fvec4 op = light_pos - ro;
                    const float b = dot(op, rd);
                    float det = b * b - dot(op, op) + l.sph.radius * l.sph.radius;
                    if (det >= 0.0f) {
                        det = sqrtf(det);
                        const float t1 = b - det, t2 = b + det;
                        if (t1 > HIT_EPS && (t1 < inout_inter.t || no_shadow)) {
                            bool accept = true;
                            if (l.sph.spot > 0.0f) {
                                const float _dot = -dot(rd, simd_fvec4{l.sph.dir});
                                if (_dot > 0.0f) {
                                    const float _angle = acosf(clamp(_dot, 0.0f, 1.0f));
                                    accept &= (_angle <= l.sph.spot);
                                } else {
                                    accept = false;
                                }
                            }
                            if (accept) {
                                inout_inter.mask = -1;
                                inout_inter.obj_index = -int(light_index) - 1;
                                inout_inter.t = t1;
                                inout_inter.u = cur.factor;
                            }
                        } else if (t2 > HIT_EPS && (t2 < inout_inter.t || no_shadow)) {
                            inout_inter.mask = -1;
                            inout_inter.obj_index = -int(light_index) - 1;
                            inout_inter.t = t2;
                            inout_inter.u = cur.factor;
                        }
                    }
                } else if (l.type == LIGHT_TYPE_DIR) {
                    const simd_fvec4 light_dir = make_fvec3(l.dir.dir);
                    const float cos_theta = dot(rd, light_dir);
                    if ((inout_inter.mask == 0 || no_shadow) && cos_theta > cosf(l.dir.angle)) {
                        inout_inter.mask = -1;
                        inout_inter.obj_index = -int(light_index) - 1;
                        inout_inter.t = 1.0f / cos_theta;
                        inout_inter.u = cur.factor;
                    }
                } else if (l.type == LIGHT_TYPE_RECT) {
                    const simd_fvec4 light_pos = make_fvec3(l.rect.pos);
                    simd_fvec4 light_u = make_fvec3(l.rect.u), light_v = make_fvec3(l.rect.v);

                    const simd_fvec4 light_forward = normalize(cross(light_u, light_v));

                    const float plane_dist = dot(light_forward, light_pos);
                    const float cos_theta = dot(rd, light_forward);
                    const float t = (plane_dist - dot(light_forward, ro)) / fminf(cos_theta, -FLT_EPS);

                    if (cos_theta < 0.0f && t > HIT_EPS && (t < inout_inter.t || no_shadow)) {
                        light_u /= dot(light_u, light_u);
                        light_v /= dot(light_v, light_v);

                        const auto p = ro + rd * t;
                        const simd_fvec4 vi = p - light_pos;
                        const float a1 = dot(light_u, vi);
                        if (a1 >= -0.5f && a1 <= 0.5f) {
                            const float a2 = dot(light_v, vi);
                            if (a2 >= -0.5f && a2 <= 0.5f) {
                                inout_inter.mask = -1;
                                inout_inter.obj_index = -int(light_index) - 1;
                                inout_inter.t = t;
                                inout_inter.u = cur.factor;
                            }
                        }
                    }
                } else if (l.type == LIGHT_TYPE_DISK) {
                    const simd_fvec4 light_pos = make_fvec3(l.disk.pos);
                    simd_fvec4 light_u = make_fvec3(l.disk.u), light_v = make_fvec3(l.disk.v);

                    const simd_fvec4 light_forward = normalize(cross(light_u, light_v));

                    const float plane_dist = dot(light_forward, light_pos);
                    const float cos_theta = dot(rd, light_forward);
                    const float t = safe_div_neg(plane_dist - dot(light_forward, ro), cos_theta);

                    if (cos_theta < 0.0f && t > HIT_EPS && (t < inout_inter.t || no_shadow)) {
                        light_u /= dot(light_u, light_u);
                        light_v /= dot(light_v, light_v);

                        const auto p = ro + rd * t;
                        const simd_fvec4 vi = p - light_pos;
                        const float a1 = dot(light_u, vi);
                        const float a2 = dot(light_v, vi);

                        if (sqrtf(a1 * a1 + a2 * a2) <= 0.5f) {
                            inout_inter.mask = -1;
                            inout_inter.obj_index = -int(light_index) - 1;
                            inout_inter.t = t;
                            inout_inter.u = cur.factor;
                        }
                    }
                } else if (l.type == LIGHT_TYPE_LINE) {
                    const simd_fvec4 light_pos = make_fvec3(l.line.pos);
                    const simd_fvec4 light_u = make_fvec3(l.line.u), light_dir = make_fvec3(l.line.v);
                    const simd_fvec4 light_v = cross(light_u, light_dir);

                    simd_fvec4 _ro = ro - light_pos;
                    _ro = simd_fvec4{dot(_ro, light_dir), dot(_ro, light_u), dot(_ro, light_v), 0.0f};

                    simd_fvec4 _rd = rd;
                    _rd = simd_fvec4{dot(_rd, light_dir), dot(_rd, light_u), dot(_rd, light_v), 0.0f};

                    const float A = _rd.get<2>() * _rd.get<2>() + _rd.get<1>() * _rd.get<1>();
                    const float B = 2.0f * (_rd.get<2>() * _ro.get<2>() + _rd.get<1>() * _ro.get<1>());
                    const float C = sqr(_ro.get<2>()) + sqr(_ro.get<1>()) - sqr(l.line.radius);

                    float t0, t1;
                    if (quadratic(A, B, C, t0, t1) && t0 > HIT_EPS && t1 > HIT_EPS) {
                        const float t = fminf(t0, t1);
                        const simd_fvec4 p = _ro + t * _rd;
                        if (fabsf(p.get<0>()) < 0.5f * l.line.height && (t < inout_inter.t || no_shadow)) {
                            inout_inter.mask = -1;
                            inout_inter.obj_index = -int(light_index) - 1;
                            inout_inter.t = t;
                            inout_inter.u = cur.factor;
                        }
                    }
                } else if (l.type == LIGHT_TYPE_ENV && inout_inter.mask == 0) {
                    // NOTE: mask remains empty
                    inout_inter.obj_index = -int(light_index) - 1;
                    inout_inter.u = cur.factor;
                }
            }
        }
    }
}

void Ray::Ref::IntersectAreaLights(Span<const ray_data_t> rays, Span<const light_t> lights,
                                   Span<const light_bvh_node_t> nodes, Span<hit_data_t> inout_inters) {
    for (int i = 0; i < rays.size(); ++i) {
        const ray_data_t &ray = rays[i];
        hit_data_t &inout_inter = inout_inters[i];

        const simd_fvec4 ro = make_fvec3(ray.o);
        const simd_fvec4 rd = make_fvec3(ray.d);

        float inv_d[3];
        safe_invert(value_ptr(rd), inv_d);

        ////

        uint32_t stack[MAX_STACK_SIZE];
        float stack_factors[MAX_STACK_SIZE];
        uint32_t stack_size = 0;

        stack_factors[stack_size] = 1.0f;
        stack[stack_size++] = 0;

        while (stack_size) {
            uint32_t cur = stack[--stack_size];
            float cur_factor = stack_factors[stack_size];

            // if (cur.dist > inout_inter.t) {
            //     continue;
            // }

            if (!bbox_test(value_ptr(ro), inv_d, inout_inter.t, nodes[cur])) {
                continue;
            }

            if (!is_leaf_node(nodes[cur])) {
                const uint32_t far = far_child(value_ptr(rd), nodes[cur]), near = near_child(value_ptr(rd), nodes[cur]);
                const light_bvh_node_t &f = nodes[far], &n = nodes[near];

                const float far_importance = calc_lnode_importance(f, ro),
                            near_importance = calc_lnode_importance(n, ro);
                const float total_importance = far_importance + near_importance;
                assert(total_importance > 0.0f);

                if (far_importance > 0.0f) {
                    stack_factors[stack_size] = cur_factor * far_importance / total_importance;
                    stack[stack_size++] = far;
                }
                if (near_importance > 0.0f) {
                    stack_factors[stack_size] = cur_factor * near_importance / total_importance;
                    stack[stack_size++] = near;
                }
            } else {
                const int light_index = int(nodes[cur].prim_index & PRIM_INDEX_BITS);
                assert((nodes[cur].prim_count & PRIM_COUNT_BITS) == 1);

                ////

                const light_t &l = lights[light_index];
                if (!l.visible) {
                    continue;
                }
                if (l.sky_portal && inout_inter.mask != 0) {
                    // Portal lights affect only missed rays
                    continue;
                }

                const bool no_shadow = (l.cast_shadow == 0);
                if (l.type == LIGHT_TYPE_SPHERE) {
                    const simd_fvec4 light_pos = make_fvec3(l.sph.pos);
                    const simd_fvec4 op = light_pos - ro;
                    const float b = dot(op, rd);
                    float det = b * b - dot(op, op) + l.sph.radius * l.sph.radius;
                    if (det >= 0.0f) {
                        det = sqrtf(det);
                        const float t1 = b - det, t2 = b + det;
                        if (t1 > HIT_EPS && (t1 < inout_inter.t || no_shadow)) {
                            bool accept = true;
                            if (l.sph.spot > 0.0f) {
                                const float _dot = -dot(rd, simd_fvec4{l.sph.dir});
                                if (_dot > 0.0f) {
                                    const float _angle = acosf(clamp(_dot, 0.0f, 1.0f));
                                    accept &= (_angle <= l.sph.spot);
                                } else {
                                    accept = false;
                                }
                            }
                            if (accept) {
                                inout_inter.mask = -1;
                                inout_inter.obj_index = -int(light_index) - 1;
                                inout_inter.t = t1;
                                inout_inter.u = cur_factor;
                            }
                        } else if (t2 > HIT_EPS && (t2 < inout_inter.t || no_shadow)) {
                            inout_inter.mask = -1;
                            inout_inter.obj_index = -int(light_index) - 1;
                            inout_inter.t = t2;
                            inout_inter.u = cur_factor;
                        }
                    }
                } else if (l.type == LIGHT_TYPE_DIR) {
                    const simd_fvec4 light_dir = make_fvec3(l.dir.dir);
                    const float cos_theta = dot(rd, light_dir);
                    if ((inout_inter.mask == 0 || no_shadow) && cos_theta > cosf(l.dir.angle)) {
                        inout_inter.mask = -1;
                        inout_inter.obj_index = -int(light_index) - 1;
                        inout_inter.t = 1.0f / cos_theta;
                        inout_inter.u = cur_factor;
                    }
                } else if (l.type == LIGHT_TYPE_RECT) {
                    const simd_fvec4 light_pos = make_fvec3(l.rect.pos);
                    simd_fvec4 light_u = make_fvec3(l.rect.u), light_v = make_fvec3(l.rect.v);

                    const simd_fvec4 light_forward = normalize(cross(light_u, light_v));

                    const float plane_dist = dot(light_forward, light_pos);
                    const float cos_theta = dot(rd, light_forward);
                    const float t = (plane_dist - dot(light_forward, ro)) / fminf(cos_theta, -FLT_EPS);

                    if (cos_theta < 0.0f && t > HIT_EPS && (t < inout_inter.t || no_shadow)) {
                        light_u /= dot(light_u, light_u);
                        light_v /= dot(light_v, light_v);

                        const auto p = ro + rd * t;
                        const simd_fvec4 vi = p - light_pos;
                        const float a1 = dot(light_u, vi);
                        if (a1 >= -0.5f && a1 <= 0.5f) {
                            const float a2 = dot(light_v, vi);
                            if (a2 >= -0.5f && a2 <= 0.5f) {
                                inout_inter.mask = -1;
                                inout_inter.obj_index = -int(light_index) - 1;
                                inout_inter.t = t;
                                inout_inter.u = cur_factor;
                            }
                        }
                    }
                } else if (l.type == LIGHT_TYPE_DISK) {
                    const simd_fvec4 light_pos = make_fvec3(l.disk.pos);
                    simd_fvec4 light_u = make_fvec3(l.disk.u), light_v = make_fvec3(l.disk.v);

                    const simd_fvec4 light_forward = normalize(cross(light_u, light_v));

                    const float plane_dist = dot(light_forward, light_pos);
                    const float cos_theta = dot(rd, light_forward);
                    const float t = safe_div_neg(plane_dist - dot(light_forward, ro), cos_theta);

                    if (cos_theta < 0.0f && t > HIT_EPS && (t < inout_inter.t || no_shadow)) {
                        light_u /= dot(light_u, light_u);
                        light_v /= dot(light_v, light_v);

                        const auto p = ro + rd * t;
                        const simd_fvec4 vi = p - light_pos;
                        const float a1 = dot(light_u, vi);
                        const float a2 = dot(light_v, vi);

                        if (sqrtf(a1 * a1 + a2 * a2) <= 0.5f) {
                            inout_inter.mask = -1;
                            inout_inter.obj_index = -int(light_index) - 1;
                            inout_inter.t = t;
                            inout_inter.u = cur_factor;
                        }
                    }
                } else if (l.type == LIGHT_TYPE_LINE) {
                    const simd_fvec4 light_pos = make_fvec3(l.line.pos);
                    const simd_fvec4 light_u = make_fvec3(l.line.u), light_dir = make_fvec3(l.line.v);
                    const simd_fvec4 light_v = cross(light_u, light_dir);

                    simd_fvec4 _ro = ro - light_pos;
                    _ro = simd_fvec4{dot(_ro, light_dir), dot(_ro, light_u), dot(_ro, light_v), 0.0f};

                    simd_fvec4 _rd = rd;
                    _rd = simd_fvec4{dot(_rd, light_dir), dot(_rd, light_u), dot(_rd, light_v), 0.0f};

                    const float A = _rd.get<2>() * _rd.get<2>() + _rd.get<1>() * _rd.get<1>();
                    const float B = 2.0f * (_rd.get<2>() * _ro.get<2>() + _rd.get<1>() * _ro.get<1>());
                    const float C = sqr(_ro.get<2>()) + sqr(_ro.get<1>()) - sqr(l.line.radius);

                    float t0, t1;
                    if (quadratic(A, B, C, t0, t1) && t0 > HIT_EPS && t1 > HIT_EPS) {
                        const float t = fminf(t0, t1);
                        const simd_fvec4 p = _ro + t * _rd;
                        if (fabsf(p.get<0>()) < 0.5f * l.line.height && (t < inout_inter.t || no_shadow)) {
                            inout_inter.mask = -1;
                            inout_inter.obj_index = -int(light_index) - 1;
                            inout_inter.t = t;
                            inout_inter.u = cur_factor;
                        }
                    }
                } else if (l.type == LIGHT_TYPE_ENV && inout_inter.mask == 0) {
                    // NOTE: mask remains empty
                    inout_inter.obj_index = -int(light_index) - 1;
                    inout_inter.u = cur_factor;
                }
            }
        }
    }
}

float Ray::Ref::IntersectAreaLights(const shadow_ray_t &ray, Span<const light_t> lights,
                                    Span<const light_wbvh_node_t> nodes) {
    const float rdist = fabsf(ray.dist);

    const simd_fvec4 ro = make_fvec3(ray.o);
    const simd_fvec4 rd = make_fvec3(ray.d);

    float inv_d[3];
    safe_invert(value_ptr(rd), inv_d);

    ////

    TraversalStack<MAX_STACK_SIZE> st;
    st.push(0u /* root_index */, 0.0f);

    while (!st.empty()) {
        stack_entry_t cur = st.pop();

        if (cur.dist > rdist) {
            continue;
        }

    TRAVERSE:
        if (!is_leaf_node(nodes[cur.index])) {
            alignas(16) float dist[8];
            long mask = bbox_test_oct(value_ptr(ro), inv_d, rdist, nodes[cur.index], dist);
            if (mask) {
                long i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                if (mask == 0) { // only one box was hit
                    cur.index = nodes[cur.index].child[i];
                    goto TRAVERSE;
                }

                const long i2 = GetFirstBit(mask);
                mask = ClearBit(mask, i2);
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

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // three boxes were hit
                    st.sort_top3();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                i = GetFirstBit(mask);
                mask = ClearBit(mask, i);
                st.push(nodes[cur.index].child[i], dist[i]);
                if (mask == 0) { // four boxes were hit
                    st.sort_top4();
                    cur.index = st.pop_index();
                    goto TRAVERSE;
                }

                uint32_t size_before = st.stack_size;

                // from five to eight boxes were hit
                do {
                    i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    st.push(nodes[cur.index].child[i], dist[i]);
                } while (mask != 0);

                const int count = int(st.stack_size - size_before + 4);
                st.sort_topN(count);
                cur.index = st.pop_index();
                goto TRAVERSE;
            }
        } else {
            const int light_index = int(nodes[cur.index].child[0] & PRIM_INDEX_BITS);
            assert(nodes[cur.index].child[1] == 1);
            const light_t &l = lights[light_index];
            if (!l.blocking) {
                continue;
            }
            if (l.sky_portal && ray.dist >= 0.0f) {
                continue;
            }
            if (l.type == LIGHT_TYPE_RECT) {
                const simd_fvec4 light_pos = make_fvec3(l.rect.pos);
                simd_fvec4 light_u = make_fvec3(l.rect.u), light_v = make_fvec3(l.rect.v);
                const simd_fvec4 light_forward = normalize(cross(light_u, light_v));

                const float plane_dist = dot(light_forward, light_pos);
                const float cos_theta = dot(rd, light_forward);
                const float t = (plane_dist - dot(light_forward, ro)) / fminf(cos_theta, -FLT_EPS);

                if (cos_theta < 0.0f && t > HIT_EPS && t < rdist) {
                    light_u /= dot(light_u, light_u);
                    light_v /= dot(light_v, light_v);

                    const auto p = ro + rd * t;
                    const simd_fvec4 vi = p - light_pos;
                    const float a1 = dot(light_u, vi);
                    if (a1 >= -0.5f && a1 <= 0.5f) {
                        const float a2 = dot(light_v, vi);
                        if (a2 >= -0.5f && a2 <= 0.5f) {
                            return 0.0f;
                        }
                    }
                }
            } else if (l.type == LIGHT_TYPE_DISK) {
                const simd_fvec4 light_pos = make_fvec3(l.disk.pos);
                simd_fvec4 light_u = make_fvec3(l.disk.u), light_v = make_fvec3(l.disk.v);

                const simd_fvec4 light_forward = normalize(cross(light_u, light_v));

                const float plane_dist = dot(light_forward, light_pos);
                const float cos_theta = dot(rd, light_forward);
                const float t = safe_div_neg(plane_dist - dot(light_forward, ro), cos_theta);

                if (cos_theta < 0.0f && t > HIT_EPS && t < rdist) {
                    light_u /= dot(light_u, light_u);
                    light_v /= dot(light_v, light_v);

                    const auto p = ro + rd * t;
                    const simd_fvec4 vi = p - light_pos;
                    const float a1 = dot(light_u, vi);
                    const float a2 = dot(light_v, vi);

                    if (sqrtf(a1 * a1 + a2 * a2) <= 0.5f) {
                        return 0.0f;
                    }
                }
            }
        }
    }
    return 1.0f;
}

float Ray::Ref::EvalTriLightFactor(const simd_fvec4 &P, const simd_fvec4 &ro, const uint32_t tri_index,
                                   Span<const light_t> lights, Span<const light_bvh_node_t> nodes) {
    uint32_t stack[MAX_STACK_SIZE];
    float stack_factors[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack_factors[stack_size] = 1.0f;
    stack[stack_size++] = 0;

    while (stack_size) {
        const uint32_t cur = stack[--stack_size];
        const float cur_factor = stack_factors[stack_size];

        if (!bbox_test(value_ptr(P), nodes[cur])) {
            continue;
        }

        if (!is_leaf_node(nodes[cur])) {
            const uint32_t left_child = nodes[cur].left_child,
                           right_child = (nodes[cur].right_child & RIGHT_CHILD_BITS);
            const light_bvh_node_t &left = nodes[left_child], &right = nodes[right_child];

            const float left_importance = calc_lnode_importance(left, ro),
                        right_importance = calc_lnode_importance(right, ro);
            const float total_importance = left_importance + right_importance;
            assert(total_importance > 0.0f);

            if (left_importance > 0.0f) {
                stack_factors[stack_size] = cur_factor * left_importance / total_importance;
                stack[stack_size++] = left_child;
            }
            if (right_importance > 0.0f) {
                stack_factors[stack_size] = cur_factor * right_importance / total_importance;
                stack[stack_size++] = right_child;
            }
        } else {
            const int light_index = int(nodes[cur].prim_index & PRIM_INDEX_BITS);
            assert((nodes[cur].prim_count & PRIM_COUNT_BITS) == 1);

            const light_t &l = lights[light_index];
            if (l.type == LIGHT_TYPE_TRI && l.tri.tri_index == tri_index) {
                // needed triangle found
                return 1.0f / cur_factor;
            }
        }
    }

    return 1.0f;
}

float Ray::Ref::EvalTriLightFactor(const simd_fvec4 &P, const simd_fvec4 &ro, uint32_t tri_index,
                                   Span<const light_t> lights, Span<const light_wbvh_node_t> nodes) {
    uint32_t stack[MAX_STACK_SIZE];
    float stack_factors[MAX_STACK_SIZE];
    uint32_t stack_size = 0;

    stack_factors[stack_size] = 1.0f;
    stack[stack_size++] = 0;

    while (stack_size) {
        const uint32_t cur = stack[--stack_size];
        const float cur_factor = stack_factors[stack_size];

        if (!is_leaf_node(nodes[cur])) {
            long mask = bbox_test_oct(value_ptr(P), nodes[cur]);
            if (mask) {
                alignas(16) float importance[8];
                calc_lnode_importance(nodes[cur], ro, importance);

                const float total_importance =
                    hsum(simd_fvec4{&importance[0], simd_mem_aligned} + simd_fvec4{&importance[4], simd_mem_aligned});
                assert(total_importance > 0.0f);

                do {
                    const long i = GetFirstBit(mask);
                    mask = ClearBit(mask, i);
                    if (importance[i] > 0.0f) {
                        stack_factors[stack_size] = cur_factor * importance[i] / total_importance;
                        stack[stack_size++] = nodes[cur].child[i];
                    }
                } while (mask != 0);
            }
        } else {
            const int light_index = int(nodes[cur].child[0] & PRIM_INDEX_BITS);
            assert((nodes[cur].child[1] & PRIM_COUNT_BITS) == 1);

            const light_t &l = lights[light_index];
            if (l.type == LIGHT_TYPE_TRI && l.tri.tri_index == tri_index) {
                // needed triangle found
                return 1.0f / cur_factor;
            }
        }
    }
    return 1.0f;
}

void Ray::Ref::TraceRays(Span<ray_data_t> rays, int min_transp_depth, int max_transp_depth, const scene_data_t &sc,
                         uint32_t node_index, bool trace_lights, const Cpu::TexStorageBase *const textures[],
                         const uint32_t rand_seq[], const uint32_t rand_seed, const int iteration,
                         Span<hit_data_t> out_inter) {
    IntersectScene(rays, min_transp_depth, max_transp_depth, rand_seq, rand_seed, iteration, sc, node_index, textures,
                   out_inter);
    if (trace_lights && !sc.visible_lights.empty()) {
        IntersectAreaLights(rays, sc.lights, sc.light_wnodes, out_inter);
    }
}

void Ray::Ref::TraceShadowRays(Span<const shadow_ray_t> rays, int max_transp_depth, float _clamp_val,
                               const scene_data_t &sc, uint32_t node_index, const uint32_t rand_seq[],
                               const uint32_t rand_seed, const int iteration,
                               const Cpu::TexStorageBase *const textures[], int img_w, color_rgba_t *out_color) {
    simd_fvec4 clamp_val = simd_fvec4{FLT_MAX};
    if (_clamp_val) {
        clamp_val.set<0>(_clamp_val);
        clamp_val.set<1>(_clamp_val);
        clamp_val.set<2>(_clamp_val);
    }

    for (int i = 0; i < int(rays.size()); ++i) {
        const shadow_ray_t &sh_r = rays[i];

        const int x = (sh_r.xy >> 16) & 0x0000ffff;
        const int y = sh_r.xy & 0x0000ffff;

        simd_fvec4 rc =
            IntersectScene(sh_r, max_transp_depth, sc, node_index, rand_seq, rand_seed, iteration, textures);
        if (!sc.blocker_lights.empty()) {
            rc *= IntersectAreaLights(sh_r, sc.lights, sc.light_wnodes);
        }

        auto old_val = simd_fvec4{out_color[y * img_w + x].v, simd_mem_aligned};
        old_val += min(rc, clamp_val);
        old_val.store_to(out_color[y * img_w + x].v, simd_mem_aligned);
    }
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_EnvColor(const ray_data_t &ray, const environment_t &env,
                                                 const Cpu::TexStorageRGBA &tex_storage, const float pdf_factor,
                                                 const simd_fvec2 &rand) {
    const simd_fvec4 I = make_fvec3(ray.d);
    simd_fvec4 env_col = 1.0f;

    const uint32_t env_map = (ray.depth & 0x00ffffff) ? env.env_map : env.back_map;
    const float env_map_rotation = (ray.depth & 0x00ffffff) ? env.env_map_rotation : env.back_map_rotation;
    if (env_map != 0xffffffff) {
        env_col = SampleLatlong_RGBE(tex_storage, env_map, I, env_map_rotation, rand);
    }

#if USE_NEE
    if (env.light_index != 0xffffffff && pdf_factor >= 0.0f) {
        if (env.qtree_levels) {
            const auto *qtree_mips = reinterpret_cast<const simd_fvec4 *const *>(env.qtree_mips);

            const float light_pdf =
                safe_div_pos(Evaluate_EnvQTree(env_map_rotation, qtree_mips, env.qtree_levels, I), pdf_factor);
            const float bsdf_pdf = ray.pdf;

            const float mis_weight = power_heuristic(bsdf_pdf, light_pdf);
            env_col *= mis_weight;
        } else {
            const float light_pdf = safe_div_pos(0.5f, PI * pdf_factor);
            const float bsdf_pdf = ray.pdf;

            const float mis_weight = power_heuristic(bsdf_pdf, light_pdf);
            env_col *= mis_weight;
        }
    }
#endif

    env_col *= (ray.depth & 0x00ffffff) ? simd_fvec4{env.env_col[0], env.env_col[1], env.env_col[2], 1.0f}
                                        : simd_fvec4{env.back_col[0], env.back_col[1], env.back_col[2], 1.0f};
    env_col.set<3>(1.0f);

    return env_col;
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_LightColor(const ray_data_t &ray, const hit_data_t &inter,
                                                   const environment_t &env, const Cpu::TexStorageRGBA &tex_storage,
                                                   Span<const light_t> lights, const uint32_t lights_count,
                                                   const simd_fvec2 &rand) {
    const simd_fvec4 I = make_fvec3(ray.d);
    const simd_fvec4 P = make_fvec3(ray.o) + inter.t * I;

    const light_t &l = lights[-inter.obj_index - 1];
#if USE_HIERARCHICAL_NEE
    const float pdf_factor = 1.0f / inter.u;
#else
    const float pdf_factor = float(lights_count);
#endif

    simd_fvec4 lcol = make_fvec3(l.col);
    if (l.sky_portal != 0) {
        simd_fvec4 env_col = make_fvec3(env.env_col);
        if (env.env_map != 0xffffffff) {
            env_col *= SampleLatlong_RGBE(tex_storage, env.env_map, I, env.env_map_rotation, rand);
        }
        lcol *= env_col;
    }
#if USE_NEE
    if (l.type == LIGHT_TYPE_SPHERE) {
        const simd_fvec4 light_pos = make_fvec3(l.sph.pos);
        const float light_area = l.sph.area;

        const float cos_theta = dot(I, normalize(light_pos - P));

        const float light_pdf = (inter.t * inter.t) / (0.5f * light_area * cos_theta * pdf_factor);
        const float bsdf_pdf = ray.pdf;

        const float mis_weight = power_heuristic(bsdf_pdf, light_pdf);
        lcol *= mis_weight;

        if (l.sph.spot > 0.0f && l.sph.blend > 0.0f) {
            const float _dot = -dot(I, simd_fvec4{l.sph.dir});
            assert(_dot > 0.0f);
            const float _angle = acosf(clamp(_dot, 0.0f, 1.0f));
            assert(_angle <= l.sph.spot);
            if (l.sph.blend > 0.0f) {
                lcol *= clamp((l.sph.spot - _angle) / l.sph.blend, 0.0f, 1.0f);
            }
        }
    } else if (l.type == LIGHT_TYPE_DIR) {
        const float radius = tanf(l.dir.angle);
        const float light_area = PI * radius * radius;

        const float cos_theta = dot(I, make_fvec3(l.dir.dir));

        const float light_pdf = 1.0f / (light_area * cos_theta * pdf_factor);
        const float bsdf_pdf = ray.pdf;

        const float mis_weight = power_heuristic(bsdf_pdf, light_pdf);
        lcol *= mis_weight;
    } else if (l.type == LIGHT_TYPE_RECT) {
        simd_fvec4 light_u = make_fvec3(l.rect.u), light_v = make_fvec3(l.rect.v);

        const simd_fvec4 light_forward = normalize(cross(light_u, light_v));
        const float light_area = l.rect.area;

        const float cos_theta = dot(I, light_forward);

        const float light_pdf = (inter.t * inter.t) / (light_area * cos_theta * pdf_factor);
        const float bsdf_pdf = ray.pdf;

        const float mis_weight = power_heuristic(bsdf_pdf, light_pdf);
        lcol *= mis_weight;
    } else if (l.type == LIGHT_TYPE_DISK) {
        simd_fvec4 light_u = make_fvec3(l.disk.u), light_v = make_fvec3(l.disk.v);

        const simd_fvec4 light_forward = normalize(cross(light_u, light_v));
        const float light_area = l.disk.area;

        const float cos_theta = dot(I, light_forward);

        const float light_pdf = (inter.t * inter.t) / (light_area * cos_theta * pdf_factor);
        const float bsdf_pdf = ray.pdf;

        const float mis_weight = power_heuristic(bsdf_pdf, light_pdf);
        lcol *= mis_weight;
    } else if (l.type == LIGHT_TYPE_LINE) {
        const simd_fvec4 light_dir = make_fvec3(l.line.v);
        const float light_area = l.line.area;

        const float cos_theta = 1.0f - fabsf(dot(I, light_dir));

        const float light_pdf = (inter.t * inter.t) / (light_area * cos_theta * pdf_factor);
        const float bsdf_pdf = ray.pdf;

        const float mis_weight = power_heuristic(bsdf_pdf, light_pdf);
        lcol *= mis_weight;
    }
#endif // USE_NEE
    return lcol;
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_DiffuseNode(const light_sample_t &ls, const ray_data_t &ray,
                                                    const surface_t &surf, const simd_fvec4 &base_color,
                                                    const float roughness, const float mix_weight, const bool use_mis,
                                                    shadow_ray_t &sh_r) {
    const simd_fvec4 I = make_fvec3(ray.d);

    const simd_fvec4 diff_col = Evaluate_OrenDiffuse_BSDF(-I, surf.N, ls.L, roughness, base_color);
    const float bsdf_pdf = diff_col[3];

    float mis_weight = 1.0f;
    if (use_mis && ls.area > 0.0f) {
        mis_weight = power_heuristic(ls.pdf, bsdf_pdf);
    }

    const simd_fvec4 lcol = ls.col * diff_col * (mix_weight * mis_weight / ls.pdf);

    if (!ls.cast_shadow) {
        // apply light immediately
        return lcol;
    }

    // schedule shadow ray
    memcpy(&sh_r.o[0], value_ptr(offset_ray(surf.P, surf.plane_N)), 3 * sizeof(float));
    UNROLLED_FOR(i, 3, { sh_r.c[i] = ray.c[i] * lcol[i]; })
    return simd_fvec4{0.0f};
}

void Ray::Ref::Sample_DiffuseNode(const ray_data_t &ray, const surface_t &surf, const simd_fvec4 &base_color,
                                  const float roughness, const float rand_u, const float rand_v, const float mix_weight,
                                  ray_data_t &new_ray) {
    const simd_fvec4 I = make_fvec3(ray.d);

    simd_fvec4 V;
    const simd_fvec4 F = Sample_OrenDiffuse_BSDF(surf.T, surf.B, surf.N, I, roughness, base_color, rand_u, rand_v, V);

    new_ray.depth = ray.depth + 0x00000001;

    memcpy(&new_ray.o[0], value_ptr(offset_ray(surf.P, surf.plane_N)), 3 * sizeof(float));
    memcpy(&new_ray.d[0], value_ptr(V), 3 * sizeof(float));
    UNROLLED_FOR(i, 3, { new_ray.c[i] = ray.c[i] * F[i] * mix_weight / F[3]; })
    new_ray.pdf = F[3];
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_GlossyNode(const light_sample_t &ls, const ray_data_t &ray,
                                                   const surface_t &surf, const simd_fvec4 &base_color,
                                                   const float roughness, const float spec_ior, const float spec_F0,
                                                   const float mix_weight, const bool use_mis, shadow_ray_t &sh_r) {
    const simd_fvec4 I = make_fvec3(ray.d);
    const simd_fvec4 H = normalize(ls.L - I);

    const simd_fvec4 view_dir_ts = tangent_from_world(surf.T, surf.B, surf.N, -I);
    const simd_fvec4 light_dir_ts = tangent_from_world(surf.T, surf.B, surf.N, ls.L);
    const simd_fvec4 sampled_normal_ts = tangent_from_world(surf.T, surf.B, surf.N, H);

    const simd_fvec4 spec_col = Evaluate_GGXSpecular_BSDF(view_dir_ts, sampled_normal_ts, light_dir_ts, sqr(roughness),
                                                          sqr(roughness), spec_ior, spec_F0, base_color);
    const float bsdf_pdf = spec_col[3];

    float mis_weight = 1.0f;
    if (use_mis && ls.area > 0.0f) {
        mis_weight = power_heuristic(ls.pdf, bsdf_pdf);
    }
    const simd_fvec4 lcol = ls.col * spec_col * (mix_weight * mis_weight / ls.pdf);

    if (!ls.cast_shadow) {
        // apply light immediately
        return lcol;
    }

    // schedule shadow ray
    memcpy(&sh_r.o[0], value_ptr(offset_ray(surf.P, surf.plane_N)), 3 * sizeof(float));
    sh_r.c[0] = ray.c[0] * lcol.get<0>();
    sh_r.c[1] = ray.c[1] * lcol.get<1>();
    sh_r.c[2] = ray.c[2] * lcol.get<2>();
    return simd_fvec4{0.0f};
}

void Ray::Ref::Sample_GlossyNode(const ray_data_t &ray, const surface_t &surf, const simd_fvec4 &base_color,
                                 const float roughness, const float spec_ior, const float spec_F0, const float rand_u,
                                 const float rand_v, const float mix_weight, ray_data_t &new_ray) {
    const simd_fvec4 I = make_fvec3(ray.d);

    simd_fvec4 V;
    const simd_fvec4 F = Sample_GGXSpecular_BSDF(surf.T, surf.B, surf.N, I, roughness, 0.0f, spec_ior, spec_F0,
                                                 base_color, rand_u, rand_v, V);

    new_ray.depth = ray.depth + 0x00000100;

    memcpy(&new_ray.o[0], value_ptr(offset_ray(surf.P, surf.plane_N)), 3 * sizeof(float));
    memcpy(&new_ray.d[0], value_ptr(V), 3 * sizeof(float));

    UNROLLED_FOR(i, 3, { new_ray.c[i] = ray.c[i] * F.get<i>() * safe_div_pos(mix_weight, F.get<3>()); })
    new_ray.pdf = F[3];
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_RefractiveNode(const light_sample_t &ls, const ray_data_t &ray,
                                                       const surface_t &surf, const simd_fvec4 &base_color,
                                                       const float roughness2, const float eta, const float mix_weight,
                                                       const bool use_mis, shadow_ray_t &sh_r) {
    const simd_fvec4 I = make_fvec3(ray.d);

    const simd_fvec4 H = normalize(ls.L - I * eta);
    const simd_fvec4 view_dir_ts = tangent_from_world(surf.T, surf.B, surf.N, -I);
    const simd_fvec4 light_dir_ts = tangent_from_world(surf.T, surf.B, surf.N, ls.L);
    const simd_fvec4 sampled_normal_ts = tangent_from_world(surf.T, surf.B, surf.N, H);

    const simd_fvec4 refr_col =
        Evaluate_GGXRefraction_BSDF(view_dir_ts, sampled_normal_ts, light_dir_ts, roughness2, eta, base_color);
    const float bsdf_pdf = refr_col[3];

    float mis_weight = 1.0f;
    if (use_mis && ls.area > 0.0f) {
        mis_weight = power_heuristic(ls.pdf, bsdf_pdf);
    }
    const simd_fvec4 lcol = ls.col * refr_col * (mix_weight * mis_weight / ls.pdf);

    if (!ls.cast_shadow) {
        // apply light immediately
        return lcol;
    }

    // schedule shadow ray
    memcpy(&sh_r.o[0], value_ptr(offset_ray(surf.P, -surf.plane_N)), 3 * sizeof(float));
    UNROLLED_FOR(i, 3, { sh_r.c[i] = ray.c[i] * lcol.get<i>(); })
    return simd_fvec4{0.0f};
}

void Ray::Ref::Sample_RefractiveNode(const ray_data_t &ray, const surface_t &surf, const simd_fvec4 &base_color,
                                     const float roughness, const bool is_backfacing, const float int_ior,
                                     const float ext_ior, const float rand_u, const float rand_v,
                                     const float mix_weight, ray_data_t &new_ray) {
    const simd_fvec4 I = make_fvec3(ray.d);
    const float eta = is_backfacing ? (int_ior / ext_ior) : (ext_ior / int_ior);

    simd_fvec4 V;
    const simd_fvec4 F =
        Sample_GGXRefraction_BSDF(surf.T, surf.B, surf.N, I, roughness, eta, base_color, rand_u, rand_v, V);

    new_ray.depth = ray.depth + 0x00010000;

    UNROLLED_FOR(i, 3, { new_ray.c[i] = ray.c[i] * F.get<i>() * safe_div_pos(mix_weight, F.get<3>()); })
    new_ray.pdf = F.get<3>();

    if (!is_backfacing) {
        // Entering the surface, push new value
        push_ior_stack(new_ray.ior, int_ior);
    } else {
        // Exiting the surface, pop the last ior value
        pop_ior_stack(new_ray.ior);
    }

    memcpy(&new_ray.o[0], value_ptr(offset_ray(surf.P, -surf.plane_N)), 3 * sizeof(float));
    memcpy(&new_ray.d[0], value_ptr(V), 3 * sizeof(float));
}

Ray::Ref::simd_fvec4 Ray::Ref::Evaluate_PrincipledNode(const light_sample_t &ls, const ray_data_t &ray,
                                                       const surface_t &surf, const lobe_weights_t &lobe_weights,
                                                       const diff_params_t &diff, const spec_params_t &spec,
                                                       const clearcoat_params_t &coat,
                                                       const transmission_params_t &trans, const float metallic,
                                                       const float transmission, const float N_dot_L,
                                                       const float mix_weight, const bool use_mis, shadow_ray_t &sh_r) {
    const simd_fvec4 I = make_fvec3(ray.d);

    simd_fvec4 lcol = 0.0f;
    float bsdf_pdf = 0.0f;

    if (lobe_weights.diffuse > 0.0f && N_dot_L > 0.0f) {
        simd_fvec4 diff_col =
            Evaluate_PrincipledDiffuse_BSDF(-I, surf.N, ls.L, diff.roughness, diff.base_color, diff.sheen_color, false);
        bsdf_pdf += lobe_weights.diffuse * diff_col.get<3>();
        diff_col *= (1.0f - metallic) * (1.0f - transmission);

        lcol += ls.col * N_dot_L * diff_col / (PI * ls.pdf);
    }

    simd_fvec4 H;
    if (N_dot_L > 0.0f) {
        H = normalize(ls.L - I);
    } else {
        H = normalize(ls.L - I * trans.eta);
    }

    const float aspect = sqrtf(1.0f - 0.9f * spec.anisotropy);
    const float roughness2 = sqr(spec.roughness);
    const float alpha_x = roughness2 / aspect;
    const float alpha_y = roughness2 * aspect;

    const simd_fvec4 view_dir_ts = tangent_from_world(surf.T, surf.B, surf.N, -I);
    const simd_fvec4 light_dir_ts = tangent_from_world(surf.T, surf.B, surf.N, ls.L);
    const simd_fvec4 sampled_normal_ts = tangent_from_world(surf.T, surf.B, surf.N, H);

    if (lobe_weights.specular > 0.0f && alpha_x * alpha_y >= 1e-7f && N_dot_L > 0.0f) {
        const simd_fvec4 spec_col = Evaluate_GGXSpecular_BSDF(view_dir_ts, sampled_normal_ts, light_dir_ts, alpha_x,
                                                              alpha_y, spec.ior, spec.F0, spec.tmp_col);
        bsdf_pdf += lobe_weights.specular * spec_col.get<3>();

        lcol += ls.col * spec_col / ls.pdf;
    }

    const float clearcoat_roughness2 = sqr(coat.roughness);

    if (lobe_weights.clearcoat > 0.0f && sqr(clearcoat_roughness2) >= 1e-7f && N_dot_L > 0.0f) {
        const simd_fvec4 clearcoat_col = Evaluate_PrincipledClearcoat_BSDF(view_dir_ts, sampled_normal_ts, light_dir_ts,
                                                                           clearcoat_roughness2, coat.ior, coat.F0);
        bsdf_pdf += lobe_weights.clearcoat * clearcoat_col.get<3>();

        lcol += 0.25f * ls.col * clearcoat_col / ls.pdf;
    }

    if (lobe_weights.refraction > 0.0f) {
        if (trans.fresnel != 0.0f && sqr(roughness2) >= 1e-7f && N_dot_L > 0.0f) {
            const simd_fvec4 spec_col =
                Evaluate_GGXSpecular_BSDF(view_dir_ts, sampled_normal_ts, light_dir_ts, roughness2, roughness2,
                                          1.0f /* ior */, 0.0f /* F0 */, simd_fvec4{1.0f});
            bsdf_pdf += lobe_weights.refraction * trans.fresnel * spec_col.get<3>();

            lcol += ls.col * spec_col * (trans.fresnel / ls.pdf);
        }

        const float transmission_roughness2 = sqr(trans.roughness);

        if (trans.fresnel != 1.0f && sqr(transmission_roughness2) >= 1e-7f && N_dot_L < 0.0f) {
            const simd_fvec4 refr_col = Evaluate_GGXRefraction_BSDF(
                view_dir_ts, sampled_normal_ts, light_dir_ts, transmission_roughness2, trans.eta, diff.base_color);
            bsdf_pdf += lobe_weights.refraction * (1.0f - trans.fresnel) * refr_col.get<3>();

            lcol += ls.col * refr_col * ((1.0f - trans.fresnel) / ls.pdf);
        }
    }

    float mis_weight = 1.0f;
    if (use_mis && ls.area > 0.0f) {
        mis_weight = power_heuristic(ls.pdf, bsdf_pdf);
    }
    lcol *= mix_weight * mis_weight;

    if (!ls.cast_shadow) {
        // apply light immediately
        return lcol;
    }

    // schedule shadow ray
    memcpy(&sh_r.o[0], value_ptr(offset_ray(surf.P, N_dot_L < 0.0f ? -surf.plane_N : surf.plane_N)), 3 * sizeof(float));
    UNROLLED_FOR(i, 3, { sh_r.c[i] = ray.c[i] * lcol.get<i>(); })
    return simd_fvec4{0.0f};
}

void Ray::Ref::Sample_PrincipledNode(const pass_settings_t &ps, const ray_data_t &ray, const surface_t &surf,
                                     const lobe_weights_t &lobe_weights, const diff_params_t &diff,
                                     const spec_params_t &spec, const clearcoat_params_t &coat,
                                     const transmission_params_t &trans, const float metallic, const float transmission,
                                     const float rand_u, const float rand_v, float mix_rand, const float mix_weight,
                                     ray_data_t &new_ray) {
    const simd_fvec4 I = make_fvec3(ray.d);

    const int diff_depth = ray.depth & 0x000000ff;
    const int spec_depth = (ray.depth >> 8) & 0x000000ff;
    const int refr_depth = (ray.depth >> 16) & 0x000000ff;
    // NOTE: transparency depth is not accounted here
    const int total_depth = diff_depth + spec_depth + refr_depth;

    if (mix_rand < lobe_weights.diffuse) {
        //
        // Diffuse lobe
        //
        if (diff_depth < ps.max_diff_depth && total_depth < ps.max_total_depth) {
            simd_fvec4 V;
            simd_fvec4 F = Sample_PrincipledDiffuse_BSDF(surf.T, surf.B, surf.N, I, diff.roughness, diff.base_color,
                                                         diff.sheen_color, false, rand_u, rand_v, V);
            const float pdf = F.get<3>(); // * lobe_weights.diffuse;

            F *= (1.0f - metallic) * (1.0f - transmission);

            new_ray.depth = ray.depth + 0x00000001;

            memcpy(&new_ray.o[0], value_ptr(offset_ray(surf.P, surf.plane_N)), 3 * sizeof(float));
            memcpy(&new_ray.d[0], value_ptr(V), 3 * sizeof(float));

            UNROLLED_FOR(i, 3,
                         { new_ray.c[i] = ray.c[i] * F.get<i>() * safe_div_pos(mix_weight, lobe_weights.diffuse); })
            new_ray.pdf = pdf;
        }
    } else if (mix_rand < lobe_weights.diffuse + lobe_weights.specular) {
        //
        // Main specular lobe
        //
        if (spec_depth < ps.max_spec_depth && total_depth < ps.max_total_depth) {
            simd_fvec4 V;
            simd_fvec4 F = Sample_GGXSpecular_BSDF(surf.T, surf.B, surf.N, I, spec.roughness, spec.anisotropy, spec.ior,
                                                   spec.F0, spec.tmp_col, rand_u, rand_v, V);
            const float pdf = F.get<3>() * lobe_weights.specular;

            new_ray.depth = ray.depth + 0x00000100;

            UNROLLED_FOR(i, 3, { new_ray.c[i] = ray.c[i] * F.get<i>() * safe_div_pos(mix_weight, pdf); })
            new_ray.pdf = pdf;

            memcpy(&new_ray.o[0], value_ptr(offset_ray(surf.P, surf.plane_N)), 3 * sizeof(float));
            memcpy(&new_ray.d[0], value_ptr(V), 3 * sizeof(float));
        }
    } else if (mix_rand < lobe_weights.diffuse + lobe_weights.specular + lobe_weights.clearcoat) {
        //
        // Clearcoat lobe (secondary specular)
        //
        if (spec_depth < ps.max_spec_depth && total_depth < ps.max_total_depth) {
            simd_fvec4 V;
            simd_fvec4 F = Sample_PrincipledClearcoat_BSDF(surf.T, surf.B, surf.N, I, sqr(coat.roughness), coat.ior,
                                                           coat.F0, rand_u, rand_v, V);
            const float pdf = F.get<3>() * lobe_weights.clearcoat;

            new_ray.depth = ray.depth + 0x00000100;

            UNROLLED_FOR(i, 3, { new_ray.c[i] = 0.25f * ray.c[i] * F.get<i>() * safe_div_pos(mix_weight, pdf); })
            new_ray.pdf = pdf;

            memcpy(&new_ray.o[0], value_ptr(offset_ray(surf.P, surf.plane_N)), 3 * sizeof(float));
            memcpy(&new_ray.d[0], value_ptr(V), 3 * sizeof(float));
        }
    } else /*if (mix_rand < lobe_weights.diffuse + lobe_weights.specular + lobe_weights.clearcoat +
              lobe_weights.refraction)*/
    {
        //
        // Refraction/reflection lobes
        //
        mix_rand -= lobe_weights.diffuse + lobe_weights.specular + lobe_weights.clearcoat;
        mix_rand = safe_div_pos(mix_rand, lobe_weights.refraction);
        if (((mix_rand >= trans.fresnel && refr_depth < ps.max_refr_depth) ||
             (mix_rand < trans.fresnel && spec_depth < ps.max_spec_depth)) &&
            total_depth < ps.max_total_depth) {

            simd_fvec4 F, V;
            if (mix_rand < trans.fresnel) {
                F = Sample_GGXSpecular_BSDF(surf.T, surf.B, surf.N, I, spec.roughness, 0.0f /* anisotropic */,
                                            1.0f /* ior */, 0.0f /* F0 */, simd_fvec4{1.0f}, rand_u, rand_v, V);

                new_ray.depth = ray.depth + 0x00000100;
                memcpy(&new_ray.o[0], value_ptr(offset_ray(surf.P, surf.plane_N)), 3 * sizeof(float));
            } else {
                F = Sample_GGXRefraction_BSDF(surf.T, surf.B, surf.N, I, trans.roughness, trans.eta, diff.base_color,
                                              rand_u, rand_v, V);

                new_ray.depth = ray.depth + 0x00010000;
                memcpy(&new_ray.o[0], value_ptr(offset_ray(surf.P, -surf.plane_N)), 3 * sizeof(float));

                if (!trans.backfacing) {
                    // Entering the surface, push new value
                    push_ior_stack(new_ray.ior, trans.int_ior);
                } else {
                    // Exiting the surface, pop the last ior value
                    pop_ior_stack(new_ray.ior);
                }
            }

            const float pdf = F.get<3>() * lobe_weights.refraction;

            UNROLLED_FOR(i, 3, { new_ray.c[i] = ray.c[i] * F.get<i>() * safe_div_pos(mix_weight, pdf); })
            new_ray.pdf = pdf;

            memcpy(&new_ray.d[0], value_ptr(V), 3 * sizeof(float));
        }
    }
}

Ray::color_rgba_t Ray::Ref::ShadeSurface(const pass_settings_t &ps, const hit_data_t &inter, const ray_data_t &ray,
                                         const uint32_t rand_seq[], const uint32_t rand_seed, const int iteration,
                                         const scene_data_t &sc, const uint32_t node_index,
                                         const Cpu::TexStorageBase *const textures[], ray_data_t *out_secondary_rays,
                                         int *out_secondary_rays_count, shadow_ray_t *out_shadow_rays,
                                         int *out_shadow_rays_count, color_rgba_t *out_base_color,
                                         color_rgba_t *out_depth_normal) {
    const simd_fvec4 I = make_fvec3(ray.d);
    const simd_fvec4 ro = make_fvec3(ray.o);

    // used to randomize random sequence among pixels
    const uint32_t px_hash = hash(ray.xy);
    const uint32_t rand_hash = hash_combine(px_hash, rand_seed);
    const uint32_t rand_dim = RAND_DIM_BASE_COUNT + total_depth(ray) * RAND_DIM_BOUNCE_COUNT;

    const simd_fvec2 tex_rand = get_scrambled_2d_rand(rand_dim + RAND_DIM_TEX, rand_hash, iteration - 1, rand_seq);

    if (!inter.mask) {
#if USE_HIERARCHICAL_NEE
        const float pdf_factor = (total_depth(ray) < ps.max_total_depth) ? safe_div_pos(1.0f, inter.u) : -1.0f;
#else
        const float pdf_factor = (total_depth(ray) < ps.max_total_depth) ? float(sc.li_indices.size()) : -1.0f;
#endif

        const simd_fvec4 env_col = Evaluate_EnvColor(
            ray, sc.env, *static_cast<const Cpu::TexStorageRGBA *>(textures[0]), pdf_factor, tex_rand);
        return color_rgba_t{ray.c[0] * env_col[0], ray.c[1] * env_col[1], ray.c[2] * env_col[2], env_col[3]};
    }

    surface_t surf = {};
    surf.P = ro + inter.t * I;

    if (inter.obj_index < 0) { // Area light intersection
        const simd_fvec4 lcol =
            Evaluate_LightColor(ray, inter, sc.env, *static_cast<const Cpu::TexStorageRGBA *>(textures[0]), sc.lights,
                                uint32_t(sc.li_indices.size()), tex_rand);
        return color_rgba_t{ray.c[0] * lcol.get<0>(), ray.c[1] * lcol.get<1>(), ray.c[2] * lcol.get<2>(), 1.0f};
    }

    const bool is_backfacing = (inter.prim_index < 0);
    const uint32_t tri_index = is_backfacing ? -inter.prim_index - 1 : inter.prim_index;

    const material_t *mat = &sc.materials[sc.tri_materials[tri_index].front_mi & MATERIAL_INDEX_BITS];

    const transform_t *tr = &sc.transforms[sc.mesh_instances[inter.obj_index].tr_index];

    const vertex_t &v1 = sc.vertices[sc.vtx_indices[tri_index * 3 + 0]];
    const vertex_t &v2 = sc.vertices[sc.vtx_indices[tri_index * 3 + 1]];
    const vertex_t &v3 = sc.vertices[sc.vtx_indices[tri_index * 3 + 2]];

    const float w = 1.0f - inter.u - inter.v;
    surf.N = normalize(make_fvec3(v1.n) * w + make_fvec3(v2.n) * inter.u + make_fvec3(v3.n) * inter.v);
    surf.uvs = simd_fvec2(v1.t) * w + simd_fvec2(v2.t) * inter.u + simd_fvec2(v3.t) * inter.v;

    surf.plane_N = cross(simd_fvec4{v2.p} - simd_fvec4{v1.p}, simd_fvec4{v3.p} - simd_fvec4{v1.p});
    const float pa = length(surf.plane_N);
    surf.plane_N /= pa;

    surf.B = make_fvec3(v1.b) * w + make_fvec3(v2.b) * inter.u + make_fvec3(v3.b) * inter.v;
    surf.T = cross(surf.B, surf.N);

    if (is_backfacing) {
        if (sc.tri_materials[tri_index].back_mi == 0xffff) {
            return color_rgba_t{0.0f, 0.0f, 0.0f, 0.0f};
        } else {
            mat = &sc.materials[sc.tri_materials[tri_index].back_mi & MATERIAL_INDEX_BITS];
            surf.plane_N = -surf.plane_N;
            surf.N = -surf.N;
            surf.B = -surf.B;
            surf.T = -surf.T;
        }
    }

    surf.plane_N = TransformNormal(surf.plane_N, tr->inv_xform);
    surf.N = TransformNormal(surf.N, tr->inv_xform);
    surf.B = TransformNormal(surf.B, tr->inv_xform);
    surf.T = TransformNormal(surf.T, tr->inv_xform);

    // normalize vectors (scaling might have been applied)
    surf.plane_N = safe_normalize(surf.plane_N);
    surf.N = safe_normalize(surf.N);
    surf.B = safe_normalize(surf.B);
    surf.T = safe_normalize(surf.T);

    const float ta = fabsf((v2.t[0] - v1.t[0]) * (v3.t[1] - v1.t[1]) - (v3.t[0] - v1.t[0]) * (v2.t[1] - v1.t[1]));

    const float cone_width = ray.cone_width + ray.cone_spread * inter.t;

    float lambda = 0.5f * fast_log2(ta / pa);
    lambda += fast_log2(cone_width);
    // lambda += 0.5 * fast_log2(tex_res.x * tex_res.y);
    // lambda -= fast_log2(fabsf(dot(I, plane_N)));

    const float ext_ior = peek_ior_stack(ray.ior, is_backfacing);

    simd_fvec4 col = {0.0f};

    const int diff_depth = ray.depth & 0x000000ff;
    const int spec_depth = (ray.depth >> 8) & 0x000000ff;
    const int refr_depth = (ray.depth >> 16) & 0x000000ff;
    // NOTE: transparency depth is not accounted here
    const int total_depth = diff_depth + spec_depth + refr_depth;

    const simd_fvec2 mix_term_rand =
        get_scrambled_2d_rand(rand_dim + RAND_DIM_BSDF_PICK, rand_hash, iteration - 1, rand_seq);

    float mix_rand = mix_term_rand.get<0>();
    float mix_weight = 1.0f;

    // resolve mix material
    while (mat->type == eShadingNode::Mix) {
        float mix_val = mat->strength;
        const uint32_t base_texture = mat->textures[BASE_TEXTURE];
        if (base_texture != 0xffffffff) {
            simd_fvec4 tex_color = SampleBilinear(textures, base_texture, surf.uvs, 0, tex_rand);
            if (base_texture & TEX_YCOCG_BIT) {
                tex_color = YCoCg_to_RGB(tex_color);
            }
            if (base_texture & TEX_SRGB_BIT) {
                tex_color = srgb_to_rgb(tex_color);
            }
            mix_val *= tex_color.get<0>();
        }

        const float eta = is_backfacing ? safe_div_pos(ext_ior, mat->ior) : safe_div_pos(mat->ior, ext_ior);
        const float RR = mat->ior != 0.0f ? fresnel_dielectric_cos(dot(I, surf.N), eta) : 1.0f;

        mix_val *= clamp(RR, 0.0f, 1.0f);

        if (mix_rand > mix_val) {
            mix_weight *= (mat->flags & MAT_FLAG_MIX_ADD) ? 1.0f / (1.0f - mix_val) : 1.0f;

            mat = &sc.materials[mat->textures[MIX_MAT1]];
            mix_rand = safe_div_pos(mix_rand - mix_val, 1.0f - mix_val);
        } else {
            mix_weight *= (mat->flags & MAT_FLAG_MIX_ADD) ? 1.0f / mix_val : 1.0f;

            mat = &sc.materials[mat->textures[MIX_MAT2]];
            mix_rand = safe_div_pos(mix_rand, mix_val);
        }
    }

    // apply normal map
    if (mat->textures[NORMALS_TEXTURE] != 0xffffffff) {
        simd_fvec4 normals = SampleBilinear(textures, mat->textures[NORMALS_TEXTURE], surf.uvs, 0, tex_rand);
        normals = normals * 2.0f - 1.0f;
        normals.set<2>(1.0f);
        if (mat->textures[NORMALS_TEXTURE] & TEX_RECONSTRUCT_Z_BIT) {
            normals.set<2>(safe_sqrt(1.0f - normals.get<0>() * normals.get<0>() - normals.get<1>() * normals.get<1>()));
        }
        simd_fvec4 in_normal = surf.N;
        surf.N = normalize(normals.get<0>() * surf.T + normals.get<2>() * surf.N + normals.get<1>() * surf.B);
        if (mat->normal_map_strength_unorm != 0xffff) {
            surf.N = normalize(in_normal + (surf.N - in_normal) * unpack_unorm_16(mat->normal_map_strength_unorm));
        }
        surf.N = ensure_valid_reflection(surf.plane_N, -I, surf.N);
    }

#if 0
    create_tbn_matrix(N, _tangent_from_world);
#else
    // Find radial tangent in local space
    const simd_fvec4 P_ls = make_fvec3(v1.p) * w + make_fvec3(v2.p) * inter.u + make_fvec3(v3.p) * inter.v;
    // rotate around Y axis by 90 degrees in 2d
    simd_fvec4 tangent = {-P_ls.get<2>(), 0.0f, P_ls.get<0>(), 0.0f};
    tangent = TransformNormal(tangent, tr->inv_xform);
    if (length2(cross(tangent, surf.N)) == 0.0f) {
        tangent = TransformNormal(P_ls, tr->inv_xform);
    }
    if (mat->tangent_rotation != 0.0f) {
        tangent = rotate_around_axis(tangent, surf.N, mat->tangent_rotation);
    }

    surf.B = normalize(cross(tangent, surf.N));
    surf.T = cross(surf.N, surf.B);
#endif

#if USE_NEE
    light_sample_t ls;
    if (!sc.light_wnodes.empty() && mat->type != eShadingNode::Emissive) {
        const float rand_pick_light =
            get_scrambled_2d_rand(rand_dim + RAND_DIM_LIGHT_PICK, rand_hash, iteration - 1, rand_seq).get<0>();
        const simd_fvec2 rand_light_uv =
            get_scrambled_2d_rand(rand_dim + RAND_DIM_LIGHT, rand_hash, iteration - 1, rand_seq);

        SampleLightSource(surf.P, surf.T, surf.B, surf.N, sc, textures, rand_pick_light, rand_light_uv, tex_rand, ls);
    }
    const float N_dot_L = dot(surf.N, ls.L);
#endif

    // sample base texture
    simd_fvec4 base_color = simd_fvec4{mat->base_color[0], mat->base_color[1], mat->base_color[2], 1.0f};
    if (mat->textures[BASE_TEXTURE] != 0xffffffff) {
        const uint32_t base_texture = mat->textures[BASE_TEXTURE];
        const float base_lod = get_texture_lod(textures, base_texture, lambda);
        simd_fvec4 tex_color = SampleBilinear(textures, base_texture, surf.uvs, int(base_lod), tex_rand);
        if (base_texture & TEX_YCOCG_BIT) {
            tex_color = YCoCg_to_RGB(tex_color);
        }
        if (base_texture & TEX_SRGB_BIT) {
            tex_color = srgb_to_rgb(tex_color);
        }
        base_color *= tex_color;
    }

    if (out_base_color) {
        memcpy(out_base_color->v, value_ptr(base_color), 3 * sizeof(float));
    }
    if (out_depth_normal) {
        memcpy(out_depth_normal->v, value_ptr(surf.N), 3 * sizeof(float));
        out_depth_normal->v[3] = inter.t;
    }

    simd_fvec4 tint_color = {0.0f};

    const float base_color_lum = lum(base_color);
    if (base_color_lum > 0.0f) {
        tint_color = base_color / base_color_lum;
    }

    float roughness = unpack_unorm_16(mat->roughness_unorm);
    if (mat->textures[ROUGH_TEXTURE] != 0xffffffff) {
        const uint32_t roughness_tex = mat->textures[ROUGH_TEXTURE];
        const float roughness_lod = get_texture_lod(textures, roughness_tex, lambda);
        simd_fvec4 roughness_color =
            SampleBilinear(textures, roughness_tex, surf.uvs, int(roughness_lod), tex_rand).get<0>();
        if (roughness_tex & TEX_SRGB_BIT) {
            roughness_color = srgb_to_rgb(roughness_color);
        }
        roughness *= roughness_color.get<0>();
    }

    const simd_fvec2 rand_bsdf = get_scrambled_2d_rand(rand_dim + RAND_DIM_BSDF, rand_hash, iteration - 1, rand_seq);
    const float rand_u = rand_bsdf.get<0>(), rand_v = rand_bsdf.get<1>();

    ray_data_t &new_ray = out_secondary_rays[*out_secondary_rays_count];
    memcpy(new_ray.ior, ray.ior, 4 * sizeof(float));
    new_ray.cone_width = cone_width;
    new_ray.cone_spread = ray.cone_spread;
    new_ray.xy = ray.xy;
    new_ray.pdf = 0.0f;

    shadow_ray_t &sh_r = out_shadow_rays[*out_shadow_rays_count];
    sh_r.c[0] = sh_r.c[1] = sh_r.c[2] = 0.0f;
    sh_r.depth = ray.depth;
    sh_r.xy = ray.xy;

    // Sample materials
    if (mat->type == eShadingNode::Diffuse) {
#if USE_NEE
        if (ls.pdf > 0.0f && N_dot_L > 0.0f) {
            col += Evaluate_DiffuseNode(ls, ray, surf, base_color, roughness, mix_weight,
                                        (total_depth < ps.max_total_depth), sh_r);
        }
#endif
        if (diff_depth < ps.max_diff_depth && total_depth < ps.max_total_depth) {
            Sample_DiffuseNode(ray, surf, base_color, roughness, rand_u, rand_v, mix_weight, new_ray);
        }
    } else if (mat->type == eShadingNode::Glossy) {
        const float specular = 0.5f;
        const float spec_ior = (2.0f / (1.0f - sqrtf(0.08f * specular))) - 1.0f;
        const float spec_F0 = fresnel_dielectric_cos(1.0f, spec_ior);
        const float roughness2 = sqr(roughness);
#if USE_NEE
        if (ls.pdf > 0.0f && sqr(roughness2) >= 1e-7f && N_dot_L > 0.0f) {
            col += Evaluate_GlossyNode(ls, ray, surf, base_color, roughness, spec_ior, spec_F0, mix_weight,
                                       (total_depth < ps.max_total_depth), sh_r);
        }
#endif
        if (spec_depth < ps.max_spec_depth && total_depth < ps.max_total_depth) {
            Sample_GlossyNode(ray, surf, base_color, roughness, spec_ior, spec_F0, rand_u, rand_v, mix_weight, new_ray);
        }
    } else if (mat->type == eShadingNode::Refractive) {
#if USE_NEE
        const float roughness2 = sqr(roughness);
        if (ls.pdf > 0.0f && sqr(roughness2) >= 1e-7f && N_dot_L < 0.0f) {
            const float eta = is_backfacing ? (mat->ior / ext_ior) : (ext_ior / mat->ior);
            col += Evaluate_RefractiveNode(ls, ray, surf, base_color, roughness2, eta, mix_weight,
                                           (total_depth < ps.max_total_depth), sh_r);
        }
#endif
        if (refr_depth < ps.max_refr_depth && total_depth < ps.max_total_depth) {
            Sample_RefractiveNode(ray, surf, base_color, roughness, is_backfacing, mat->ior, ext_ior, rand_u, rand_v,
                                  mix_weight, new_ray);
        }
    } else if (mat->type == eShadingNode::Emissive) {
        float mis_weight = 1.0f;
#if USE_NEE
        if ((ray.depth & 0x00ffffff) != 0 && (mat->flags & MAT_FLAG_MULT_IMPORTANCE)) {
#if USE_HIERARCHICAL_NEE
            // TODO: maybe this can be done more efficiently
            const float pdf_factor = EvalTriLightFactor(surf.P, ro, tri_index, sc.lights, sc.light_wnodes);
#else
            const float pdf_factor = float(sc.li_indices.size());
#endif

            const auto p1 = make_fvec3(v1.p), p2 = make_fvec3(v2.p), p3 = make_fvec3(v3.p);

            simd_fvec4 light_forward = TransformDirection(cross(p2 - p1, p3 - p1), tr->xform);
            const float light_forward_len = length(light_forward);
            light_forward /= light_forward_len;
            const float tri_area = 0.5f * light_forward_len;

            const float cos_theta = fabsf(dot(I, light_forward)); // abs for doublesided light
            if (cos_theta > 0.0f) {
                const float light_pdf = (inter.t * inter.t) / (tri_area * cos_theta * pdf_factor);
                const float bsdf_pdf = ray.pdf;

                mis_weight = power_heuristic(bsdf_pdf, light_pdf);
            }
        }
#endif
        col += mix_weight * mis_weight * mat->strength * base_color;
    } else if (mat->type == eShadingNode::Principled) {
        float metallic = unpack_unorm_16(mat->metallic_unorm);
        if (mat->textures[METALLIC_TEXTURE] != 0xffffffff) {
            const uint32_t metallic_tex = mat->textures[METALLIC_TEXTURE];
            const float metallic_lod = get_texture_lod(textures, metallic_tex, lambda);
            metallic *= SampleBilinear(textures, metallic_tex, surf.uvs, int(metallic_lod), tex_rand).get<0>();
        }

        float specular = unpack_unorm_16(mat->specular_unorm);
        if (mat->textures[SPECULAR_TEXTURE] != 0xffffffff) {
            const uint32_t specular_tex = mat->textures[SPECULAR_TEXTURE];
            const float specular_lod = get_texture_lod(textures, specular_tex, lambda);
            simd_fvec4 specular_color = SampleBilinear(textures, specular_tex, surf.uvs, int(specular_lod), tex_rand);
            if (specular_tex & TEX_SRGB_BIT) {
                specular_color = srgb_to_rgb(specular_color);
            }
            specular *= specular_color.get<0>();
        }

        const float specular_tint = unpack_unorm_16(mat->specular_tint_unorm);
        const float transmission = unpack_unorm_16(mat->transmission_unorm);
        const float clearcoat = unpack_unorm_16(mat->clearcoat_unorm);
        const float clearcoat_roughness = unpack_unorm_16(mat->clearcoat_roughness_unorm);
        const float sheen = 2.0f * unpack_unorm_16(mat->sheen_unorm);
        const float sheen_tint = unpack_unorm_16(mat->sheen_tint_unorm);

        diff_params_t diff = {};
        diff.base_color = base_color;
        diff.sheen_color = sheen * mix(simd_fvec4{1.0f}, tint_color, sheen_tint);
        diff.roughness = roughness;

        spec_params_t spec = {};
        spec.tmp_col = mix(simd_fvec4{1.0f}, tint_color, specular_tint);
        spec.tmp_col = mix(specular * 0.08f * spec.tmp_col, base_color, metallic);
        spec.roughness = roughness;
        spec.ior = (2.0f / (1.0f - sqrtf(0.08f * specular))) - 1.0f;
        spec.F0 = fresnel_dielectric_cos(1.0f, spec.ior);
        spec.anisotropy = unpack_unorm_16(mat->anisotropic_unorm);

        clearcoat_params_t coat = {};
        coat.roughness = clearcoat_roughness;
        coat.ior = (2.0f / (1.0f - sqrtf(0.08f * clearcoat))) - 1.0f;
        coat.F0 = fresnel_dielectric_cos(1.0f, coat.ior);

        transmission_params_t trans = {};
        trans.roughness = 1.0f - (1.0f - roughness) * (1.0f - unpack_unorm_16(mat->transmission_roughness_unorm));
        trans.int_ior = mat->ior;
        trans.eta = is_backfacing ? (mat->ior / ext_ior) : (ext_ior / mat->ior);
        trans.fresnel = fresnel_dielectric_cos(dot(I, surf.N), 1.0f / trans.eta);
        trans.backfacing = is_backfacing;

        // Approximation of FH (using shading normal)
        const float FN = (fresnel_dielectric_cos(dot(I, surf.N), spec.ior) - spec.F0) / (1.0f - spec.F0);

        const simd_fvec4 approx_spec_col = mix(spec.tmp_col, simd_fvec4(1.0f), FN);
        const float spec_color_lum = lum(approx_spec_col);

        const auto lobe_weights = get_lobe_weights(mix(base_color_lum, 1.0f, sheen), spec_color_lum, specular, metallic,
                                                   transmission, clearcoat);

#if USE_NEE
        if (ls.pdf > 0.0f) {
            col += Evaluate_PrincipledNode(ls, ray, surf, lobe_weights, diff, spec, coat, trans, metallic, transmission,
                                           N_dot_L, mix_weight, (total_depth < ps.max_total_depth), sh_r);
        }
#endif
        Sample_PrincipledNode(ps, ray, surf, lobe_weights, diff, spec, coat, trans, metallic, transmission, rand_u,
                              rand_v, mix_rand, mix_weight, new_ray);
    } /*else if (mat->type == TransparentNode) {
        assert(false);
    }*/

#if USE_PATH_TERMINATION
    const bool can_terminate_path = total_depth > ps.min_total_depth;
#else
    const bool can_terminate_path = false;
#endif

    const float lum = fmaxf(new_ray.c[0], fmaxf(new_ray.c[1], new_ray.c[2]));
    const float p = mix_term_rand.get<1>();
    const float q = can_terminate_path ? fmaxf(0.05f, 1.0f - lum) : 0.0f;
    if (p >= q && lum > 0.0f && new_ray.pdf > 0.0f) {
        new_ray.pdf = fminf(new_ray.pdf, 1e6f);
        new_ray.c[0] /= (1.0f - q);
        new_ray.c[1] /= (1.0f - q);
        new_ray.c[2] /= (1.0f - q);
        ++(*out_secondary_rays_count);
    }

#if USE_NEE
    const float sh_lum = fmaxf(sh_r.c[0], fmaxf(sh_r.c[1], sh_r.c[2]));
    if (sh_lum > 0.0f) {
        // actual ray direction accouning for bias from both ends
        const simd_fvec4 to_light = ls.lp - simd_fvec4{sh_r.o[0], sh_r.o[1], sh_r.o[2], 0.0f};
        sh_r.dist = length(to_light);
        memcpy(&sh_r.d[0], value_ptr(to_light / sh_r.dist), 3 * sizeof(float));
        sh_r.dist *= ls.dist_mul;
        if (ls.from_env) {
            // NOTE: hacky way to identify env ray
            sh_r.dist = -sh_r.dist;
        }
        ++(*out_shadow_rays_count);
    }
#endif

    return color_rgba_t{ray.c[0] * col.get<0>(), ray.c[1] * col.get<1>(), ray.c[2] * col.get<2>(), 1.0f};
}

void Ray::Ref::ShadePrimary(const pass_settings_t &ps, Span<const hit_data_t> inters, Span<const ray_data_t> rays,
                            const uint32_t rand_seq[], const uint32_t rand_seed, const int iteration,
                            const scene_data_t &sc, uint32_t node_index, const Cpu::TexStorageBase *const textures[],
                            ray_data_t *out_secondary_rays, int *out_secondary_rays_count,
                            shadow_ray_t *out_shadow_rays, int *out_shadow_rays_count, int img_w, float mix_factor,
                            color_rgba_t *out_color, color_rgba_t *out_base_color, color_rgba_t *out_depth_normal) {
    auto clamp_direct = simd_fvec4{FLT_MAX};
    if (ps.clamp_direct != 0.0f) {
        clamp_direct.set<0>(ps.clamp_direct);
        clamp_direct.set<1>(ps.clamp_direct);
        clamp_direct.set<2>(ps.clamp_direct);
    }

    for (int i = 0; i < int(inters.size()); ++i) {
        const ray_data_t &r = rays[i];
        const hit_data_t &inter = inters[i];

        const int x = (r.xy >> 16) & 0x0000ffff;
        const int y = r.xy & 0x0000ffff;

        color_rgba_t base_color = {}, depth_normal = {};
        const color_rgba_t col =
            ShadeSurface(ps, inter, r, rand_seq, rand_seed, iteration, sc, node_index, textures, out_secondary_rays,
                         out_secondary_rays_count, out_shadow_rays, out_shadow_rays_count, &base_color, &depth_normal);

        const Ref::simd_fvec4 vcol = min(Ref::simd_fvec4{col.v}, clamp_direct);
        vcol.store_to(out_color[y * img_w + x].v, Ref::simd_mem_aligned);
        if (ps.flags & ePassFlags::OutputBaseColor) {
            auto old_val = Ref::simd_fvec4{out_base_color[y * img_w + x].v, Ref::simd_mem_aligned};
            old_val += (Ref::simd_fvec4{base_color.v, Ref::simd_mem_aligned} - old_val) * mix_factor;
            old_val.store_to(out_base_color[y * img_w + x].v, Ref::simd_mem_aligned);
        }
        if (ps.flags & ePassFlags::OutputDepthNormals) {
            auto old_val = Ref::simd_fvec4{out_depth_normal[y * img_w + x].v, Ref::simd_mem_aligned};
            old_val += (Ref::simd_fvec4{depth_normal.v, Ref::simd_mem_aligned} - old_val) * mix_factor;
            old_val.store_to(out_depth_normal[y * img_w + x].v, Ref::simd_mem_aligned);
        }
    }
}

void Ray::Ref::ShadeSecondary(const pass_settings_t &ps, float clamp_val, Span<const hit_data_t> inters,
                              Span<const ray_data_t> rays, const uint32_t rand_seq[], uint32_t rand_seed, int iteration,
                              const scene_data_t &sc, uint32_t node_index, const Cpu::TexStorageBase *const textures[],
                              ray_data_t *out_secondary_rays, int *out_secondary_rays_count,
                              shadow_ray_t *out_shadow_rays, int *out_shadow_rays_count, int img_w,
                              color_rgba_t *out_color) {
    auto clamp_indirect = simd_fvec4{FLT_MAX};
    if (clamp_val != 0.0f) {
        clamp_indirect.set<0>(clamp_val);
        clamp_indirect.set<1>(clamp_val);
        clamp_indirect.set<2>(clamp_val);
    }

    for (int i = 0; i < int(inters.size()); ++i) {
        const ray_data_t &r = rays[i];
        const hit_data_t &inter = inters[i];

        const int x = (r.xy >> 16) & 0x0000ffff;
        const int y = r.xy & 0x0000ffff;

        color_rgba_t col =
            ShadeSurface(ps, inter, r, rand_seq, rand_seed, iteration, sc, node_index, textures, out_secondary_rays,
                         out_secondary_rays_count, out_shadow_rays, out_shadow_rays_count, nullptr, nullptr);
        col.v[3] = 0.0f;

        auto old_val = Ref::simd_fvec4{out_color[y * img_w + x].v, Ref::simd_mem_aligned};
        old_val += min(Ref::simd_fvec4{col.v}, clamp_indirect);
        old_val.store_to(out_color[y * img_w + x].v, Ref::simd_mem_aligned);
    }
}

namespace Ray {
namespace Ref {
template <int WINDOW_SIZE, int NEIGHBORHOOD_SIZE, bool FEATURE0, bool FEATURE1>
void JointNLMFilter(const color_rgba_t *restrict input, const rect_t &rect, const int input_stride, const float alpha,
                    const float damping, const color_rgba_t variance[], const color_rgba_t *restrict feature0,
                    const float feature0_weight, const color_rgba_t *restrict feature1, const float feature1_weight,
                    const rect_t &output_rect, const int output_stride, color_rgba_t *restrict output) {
    const int WindowRadius = (WINDOW_SIZE - 1) / 2;
    const float PatchDistanceNormFactor = NEIGHBORHOOD_SIZE * NEIGHBORHOOD_SIZE;
    const int NeighborRadius = (NEIGHBORHOOD_SIZE - 1) / 2;

    assert(rect.w == output_rect.w);
    assert(rect.h == output_rect.h);

    for (int iy = rect.y; iy < rect.y + rect.h; ++iy) {
        for (int ix = rect.x; ix < rect.x + rect.w; ++ix) {
            simd_fvec4 sum_output = {};
            float sum_weight = 0.0f;

            for (int k = -WindowRadius; k <= WindowRadius; ++k) {
                const int jy = iy + k;

                for (int l = -WindowRadius; l <= WindowRadius; ++l) {
                    const int jx = ix + l;

                    simd_fvec4 color_distance = {};

                    for (int q = -NeighborRadius; q <= NeighborRadius; ++q) {
                        for (int p = -NeighborRadius; p <= NeighborRadius; ++p) {
                            const simd_fvec4 ipx = {input[(iy + q) * input_stride + (ix + p)].v, simd_mem_aligned};
                            const simd_fvec4 jpx = {input[(jy + q) * input_stride + (jx + p)].v, simd_mem_aligned};

                            const simd_fvec4 ivar = {variance[(iy + q) * input_stride + (ix + p)].v, simd_mem_aligned};
                            const simd_fvec4 jvar = {variance[(jy + q) * input_stride + (jx + p)].v, simd_mem_aligned};
                            const simd_fvec4 min_var = min(ivar, jvar);

                            color_distance += ((ipx - jpx) * (ipx - jpx) - alpha * (ivar + min_var)) /
                                              (0.0001f + damping * damping * (ivar + jvar));
                        }
                    }

                    const float patch_distance = 0.25f * PatchDistanceNormFactor *
                                                 (color_distance.get<0>() + color_distance.get<1>() +
                                                  color_distance.get<2>() + color_distance.get<3>());
                    float weight = expf(-fmaxf(0.0f, patch_distance));

                    if (FEATURE0 || FEATURE1) {
                        simd_fvec4 feature_distance = {};
                        if (FEATURE0) {
                            const simd_fvec4 ipx = {feature0[iy * input_stride + ix].v, simd_mem_aligned};
                            const simd_fvec4 jpx = {feature0[jy * input_stride + jx].v, simd_mem_aligned};

                            feature_distance = feature0_weight * (ipx - jpx) * (ipx - jpx);
                        }
                        if (FEATURE1) {
                            const simd_fvec4 ipx = {feature1[iy * input_stride + ix].v, simd_mem_aligned};
                            const simd_fvec4 jpx = {feature1[jy * input_stride + jx].v, simd_mem_aligned};

                            feature_distance = max(feature_distance, feature1_weight * (ipx - jpx) * (ipx - jpx));
                        }

                        const float feature_patch_distance =
                            0.25f * (feature_distance.get<0>() + feature_distance.get<1>() + feature_distance.get<2>() +
                                     feature_distance.get<3>());
                        const float feature_weight = expf(-fmaxf(0.0f, fminf(10000.0f, feature_patch_distance)));

                        weight = fminf(weight, feature_weight);
                    }

                    sum_output += simd_fvec4{input[jy * input_stride + jx].v, simd_mem_aligned} * weight;
                    sum_weight += weight;
                }
            }

            if (sum_weight != 0.0f) {
                sum_output /= sum_weight;
            }

            sum_output.store_to(output[(output_rect.y + iy - rect.y) * output_stride + (output_rect.x + ix - rect.x)].v,
                                simd_mem_aligned);
        }
    }
}
} // namespace Ref
} // namespace Ray

template <int WINDOW_SIZE, int NEIGHBORHOOD_SIZE>
void Ray::Ref::JointNLMFilter(const color_rgba_t input[], const rect_t &rect, const int input_stride, const float alpha,
                              const float damping, const color_rgba_t variance[], const color_rgba_t feature1[],
                              const float feature1_weight, const color_rgba_t feature2[], const float feature2_weight,
                              const rect_t &output_rect, const int output_stride, color_rgba_t output[]) {
    if (feature1 && feature2) {
        JointNLMFilter<WINDOW_SIZE, NEIGHBORHOOD_SIZE, true, true>(input, rect, input_stride, alpha, damping, variance,
                                                                   feature1, feature1_weight, feature2, feature2_weight,
                                                                   output_rect, output_stride, output);
    } else if (feature1) {
        JointNLMFilter<WINDOW_SIZE, NEIGHBORHOOD_SIZE, true, false>(input, rect, input_stride, alpha, damping, variance,
                                                                    feature1, feature1_weight, nullptr, 0.0f,
                                                                    output_rect, output_stride, output);
    } else if (feature2) {
        JointNLMFilter<WINDOW_SIZE, NEIGHBORHOOD_SIZE, true, false>(input, rect, input_stride, alpha, damping, variance,
                                                                    feature2, feature2_weight, nullptr, 0.0f,
                                                                    output_rect, output_stride, output);
    } else {
        JointNLMFilter<WINDOW_SIZE, NEIGHBORHOOD_SIZE, false, false>(input, rect, input_stride, alpha, damping,
                                                                     variance, nullptr, 0.0f, nullptr, 0.0f,
                                                                     output_rect, output_stride, output);
    }
}

template void Ray::Ref::JointNLMFilter<21 /* WINDOW_SIZE */, 5 /* NEIGHBORHOOD_SIZE */>(
    const color_rgba_t input[], const rect_t &rect, int input_stride, float alpha, float damping,
    const color_rgba_t variance[], const color_rgba_t feature0[], float feature0_weight, const color_rgba_t feature1[],
    float feature1_weight, const rect_t &output_rect, int output_stride, color_rgba_t output[]);
template void Ray::Ref::JointNLMFilter<21 /* WINDOW_SIZE */, 3 /* NEIGHBORHOOD_SIZE */>(
    const color_rgba_t input[], const rect_t &rect, int input_stride, float alpha, float damping,
    const color_rgba_t variance[], const color_rgba_t feature0[], float feature0_weight, const color_rgba_t feature1[],
    float feature1_weight, const rect_t &output_rect, int output_stride, color_rgba_t output[]);
template void Ray::Ref::JointNLMFilter<7 /* WINDOW_SIZE */, 3 /* NEIGHBORHOOD_SIZE */>(
    const color_rgba_t input[], const rect_t &rect, int input_stride, float alpha, float damping,
    const color_rgba_t variance[], const color_rgba_t feature0[], float feature0_weight, const color_rgba_t feature1[],
    float feature1_weight, const rect_t &output_rect, int output_stride, color_rgba_t output[]);
template void Ray::Ref::JointNLMFilter<3 /* WINDOW_SIZE */, 1 /* NEIGHBORHOOD_SIZE */>(
    const color_rgba_t input[], const rect_t &rect, int input_stride, float alpha, float damping,
    const color_rgba_t variance[], const color_rgba_t feature0[], float feature0_weight, const color_rgba_t feature1[],
    float feature1_weight, const rect_t &output_rect, int output_stride, color_rgba_t output[]);

#define NS Ref
#include "Convolution.h"
#undef NS

template <int InChannels, int OutChannels, int OutPxPitch, Ray::ePostOp PostOp, Ray::eActivation Activation>
void Ray::Ref::Convolution3x3_Direct(const float data[], const rect_t &rect, int w, int h, int stride,
                                     const float weights[], const float biases[], float output[], int output_stride) {
    static_assert((InChannels % 4) == 0, "!");

    if (!output_stride) {
        if (PostOp == ePostOp::Downscale) {
            output_stride = (w + 1) / 2;
        } else {
            output_stride = w;
        }
    }

    if (PostOp == ePostOp::Downscale) {
        if (OutChannels == OutPxPitch) {
            for (int y = (rect.y / 2); y < (rect.y + rect.h + 1) / 2; ++y) {
                float *ptr = &output[OutChannels * (y * output_stride + (rect.x / 2))];
                std::fill(ptr, ptr + ((rect.w + 1) / 2) * OutChannels, 0.0f);
            }
        } else {
            for (int y = (rect.y / 2); y < (rect.y + rect.h + 1) / 2; ++y) {
                for (int x = (rect.x / 2); x < (rect.x + rect.w + 1) / 2; ++x) {
                    for (int c = 0; c < OutChannels; ++c) {
                        output[OutPxPitch * (y * output_stride + (rect.x / 2)) + c] = 0.0f;
                    }
                }
            }
        }
    }

    int y = rect.y;
    for (; y < rect.y + rect.h - 7; y += 8) {
        Convolution3x3_Direct_ProcessRows<8, 4, InChannels, OutChannels, OutPxPitch, PostOp, Activation>(
            y, data, rect, w, h, stride, weights, biases, output, output_stride);
    }

    for (; y < rect.y + rect.h - 3; y += 4) {
        Convolution3x3_Direct_ProcessRows<4, 4, InChannels, OutChannels, OutPxPitch, PostOp, Activation>(
            y, data, rect, w, h, stride, weights, biases, output, output_stride);
    }

    for (; y < rect.y + rect.h; ++y) {
        Convolution3x3_Direct_ProcessRows<1, 4, InChannels, OutChannels, OutPxPitch, PostOp, Activation>(
            y, data, rect, w, h, stride, weights, biases, output, output_stride);
    }
}

template <int InChannels1, int InChannels2, int InChannels3, int PxPitch, int OutChannels, Ray::ePreOp PreOp1,
          Ray::ePreOp PreOp2, Ray::ePreOp PreOp3, Ray::ePostOp PostOp, Ray::eActivation Activation>
void Ray::Ref::Convolution3x3_GEMM(const float data1[], const float data2[], const float data3[], const rect_t &rect,
                                   int in_w, int in_h, int w, int h, int stride, const float weights[],
                                   const float biases[], float output[], int output_stride) {
    Convolution3x3_GEMM<4, InChannels1, InChannels2, InChannels3, PxPitch, OutChannels, PreOp1, PreOp2, PreOp3, PostOp,
                        Activation>(data1, data2, data3, rect, in_w, in_h, w, h, stride, weights, biases, output,
                                    output_stride);
}

template void Ray::Ref::Convolution3x3_Direct<32, 3, 4, Ray::ePostOp::HDRTransfer, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<32, 32, 32, Ray::ePostOp::Downscale, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<32, 48, 48, Ray::ePostOp::Downscale, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<48, 64, 64, Ray::ePostOp::Downscale, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<64, 32, 32, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<64, 64, 64, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<64, 80, 80, Ray::ePostOp::Downscale, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<80, 96, 96, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<96, 96, 96, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);
template void Ray::Ref::Convolution3x3_Direct<112, 112, 112, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data[], const rect_t &rect, int w, int h, int stride, const float weights[], const float biases[],
    float output[], int output_stride);

template void Ray::Ref::Convolution3x3_GEMM<3, 0, 0, 4, 32, Ray::ePreOp::HDRTransfer, Ray::ePreOp::None,
                                            Ray::ePreOp::None, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data1[], const float data2[], const float data3[], const rect_t &rect, int in_w, int in_h, int w, int h,
    int stride, const float weights[], const float biases[], float output[], int output_stride);
template void Ray::Ref::Convolution3x3_GEMM<3, 3, 0, 4, 32, Ray::ePreOp::HDRTransfer, Ray::ePreOp::None,
                                            Ray::ePreOp::None, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data1[], const float data2[], const float data3[], const rect_t &rect, int in_w, int in_h, int w, int h,
    int stride, const float weights[], const float biases[], float output[], int output_stride);
template void Ray::Ref::Convolution3x3_GEMM<3, 3, 3, 4, 32, Ray::ePreOp::HDRTransfer, Ray::ePreOp::None,
                                            Ray::ePreOp::PositiveNormalize, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data1[], const float data2[], const float data3[], const rect_t &rect, int in_w, int in_h, int w, int h,
    int stride, const float weights[], const float biases[], float output[], int output_stride);

template <int InChannels1, int InChannels2, int OutChannels, Ray::ePreOp PreOp1, Ray::ePostOp PostOp,
          Ray::eActivation Activation>
void Ray::Ref::ConvolutionConcat3x3_Direct(const float data1[], const float data2[], const rect_t &rect, int w, int h,
                                           int stride1, int stride2, const float weights[], const float biases[],
                                           float output[], int output_stride) {
    static_assert((InChannels1 % 4) == 0 && (InChannels2 % 4) == 0, "!");

    int y = rect.y;
    for (; y < rect.y + rect.h - 7; y += 8) {
        ConvolutionConcat3x3_Direct_ProcessRows<8, 4, InChannels1, InChannels2, OutChannels, PreOp1, PostOp,
                                                Activation>(y, data1, data2, rect, w, h, stride1, stride2, weights,
                                                            biases, output, output_stride);
    }

    for (; y < rect.y + rect.h - 3; y += 4) {
        ConvolutionConcat3x3_Direct_ProcessRows<4, 4, InChannels1, InChannels2, OutChannels, PreOp1, PostOp,
                                                Activation>(y, data1, data2, rect, w, h, stride1, stride2, weights,
                                                            biases, output, output_stride);
    }

    for (; y < rect.y + rect.h; ++y) {
        ConvolutionConcat3x3_Direct_ProcessRows<1, 4, InChannels1, InChannels2, OutChannels, PreOp1, PostOp,
                                                Activation>(y, data1, data2, rect, w, h, stride1, stride2, weights,
                                                            biases, output, output_stride);
    }
}

template <int InChannels1, int InChannels2, int OutChannels, Ray::ePreOp PreOp1, Ray::eActivation Activation>
void Ray::Ref::ConvolutionConcat3x3_GEMM(const float data1[], const float data2[], const rect_t &rect, int w, int h,
                                         const float weights[], const float biases[], float output[]) {
    ConvolutionConcat3x3_GEMM<4, InChannels1, InChannels2, OutChannels, PreOp1, Activation>(data1, data2, rect, w, h,
                                                                                            weights, biases, output);
}

template <int InChannels1, int InChannels2, int InChannels3, int InChannels4, int PxPitch2, int OutChannels,
          Ray::ePreOp PreOp1, Ray::ePreOp PreOp2, Ray::ePreOp PreOp3, Ray::ePreOp PreOp4, Ray::ePostOp PostOp,
          Ray::eActivation Activation>
void Ray::Ref::ConvolutionConcat3x3_1Direct_2GEMM(const float data1[], const float data2[], const float data3[],
                                                  const float data4[], const rect_t &rect, int w, int h, int w2, int h2,
                                                  int stride1, int stride2, const float weights[], const float biases[],
                                                  float output[], int output_stride) {
    static_assert((InChannels1 % 4) == 0, "!");

    int y = rect.y;
    for (; y < rect.y + rect.h - 7; y += 8) {
        ConvolutionConcat3x3_1Direct_2GEMM_ProcessRows<8, 4, InChannels1, InChannels2, InChannels3, InChannels4,
                                                       PxPitch2, OutChannels, PreOp1, PreOp2, PreOp3, PreOp4, PostOp,
                                                       Activation>(y, data1, data2, data3, data4, rect, w, h, w2, h2,
                                                                   stride1, stride2, weights, biases, output,
                                                                   output_stride);
    }

    for (; y < rect.y + rect.h - 3; y += 4) {
        ConvolutionConcat3x3_1Direct_2GEMM_ProcessRows<4, 4, InChannels1, InChannels2, InChannels3, InChannels4,
                                                       PxPitch2, OutChannels, PreOp1, PreOp2, PreOp3, PreOp4, PostOp,
                                                       Activation>(y, data1, data2, data3, data4, rect, w, h, w2, h2,
                                                                   stride1, stride2, weights, biases, output,
                                                                   output_stride);
    }

    for (; y < rect.y + rect.h; ++y) {
        ConvolutionConcat3x3_1Direct_2GEMM_ProcessRows<1, 4, InChannels1, InChannels2, InChannels3, InChannels4,
                                                       PxPitch2, OutChannels, PreOp1, PreOp2, PreOp3, PreOp4, PostOp,
                                                       Activation>(y, data1, data2, data3, data4, rect, w, h, w2, h2,
                                                                   stride1, stride2, weights, biases, output,
                                                                   output_stride);
    }
}

template void
Ray::Ref::ConvolutionConcat3x3_Direct<96, 64, 112, Ray::ePreOp::Upscale, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data1[], const float data2[], const rect_t &rect, int w, int h, int stride1, int stride2,
    const float weights[], const float biases[], float output[], int output_stride);
template void
Ray::Ref::ConvolutionConcat3x3_Direct<112, 48, 96, Ray::ePreOp::Upscale, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data1[], const float data2[], const rect_t &rect, int w, int h, int stride1, int stride2,
    const float weights[], const float biases[], float output[], int output_stride);
template void
Ray::Ref::ConvolutionConcat3x3_Direct<96, 32, 64, Ray::ePreOp::Upscale, Ray::ePostOp::None, Ray::eActivation::ReLU>(
    const float data1[], const float data2[], const rect_t &rect, int w, int h, int stride1, int stride2,
    const float weights[], const float biases[], float output[], int output_stride);

template void Ray::Ref::ConvolutionConcat3x3_1Direct_2GEMM<
    64, 3, 0, 0, 4, 64, Ray::ePreOp::Upscale, Ray::ePreOp::HDRTransfer, Ray::ePreOp::None, Ray::ePreOp::None,
    Ray::ePostOp::None, Ray::eActivation::ReLU>(const float data1[], const float data2[], const float data3[],
                                                const float data4[], const rect_t &rect, int w, int h, int w2, int h2,
                                                int stride1, int stride2, const float weights[], const float biases[],
                                                float output[], int output_stride);
template void Ray::Ref::ConvolutionConcat3x3_1Direct_2GEMM<
    64, 3, 3, 0, 4, 64, Ray::ePreOp::Upscale, Ray::ePreOp::HDRTransfer, Ray::ePreOp::None, Ray::ePreOp::None,
    Ray::ePostOp::None, Ray::eActivation::ReLU>(const float data1[], const float data2[], const float data3[],
                                                const float data4[], const rect_t &rect, int w, int h, int w2, int h2,
                                                int stride1, int stride2, const float weights[], const float biases[],
                                                float output[], int output_stride);
template void
Ray::Ref::ConvolutionConcat3x3_1Direct_2GEMM<64, 3, 3, 3, 4, 64, Ray::ePreOp::Upscale, Ray::ePreOp::HDRTransfer,
                                             Ray::ePreOp::None, Ray::ePreOp::PositiveNormalize, Ray::ePostOp::None,
                                             Ray::eActivation::ReLU>(const float data1[], const float data2[],
                                                                     const float data3[], const float data4[],
                                                                     const rect_t &rect, int w, int h, int w2, int h2,
                                                                     int stride1, int stride2, const float weights[],
                                                                     const float biases[], float output[],
                                                                     int output_stride);

namespace Ray {
extern const int LUT_DIMS = 48;
#include "luts/__filmic_high_contrast.inl"
#include "luts/__filmic_low_contrast.inl"
#include "luts/__filmic_med_contrast.inl"
#include "luts/__filmic_med_high_contrast.inl"
#include "luts/__filmic_med_low_contrast.inl"
#include "luts/__filmic_very_high_contrast.inl"
#include "luts/__filmic_very_low_contrast.inl"

const uint32_t *transform_luts[] = {
    nullptr,                    // Standard
    __filmic_very_low_contrast, // Filmic_VeryLowContrast
    __filmic_low_contrast,      // Filmic_LowContrast
    __filmic_med_low_contrast,  // Filmic_MediumLowContrast
    __filmic_med_contrast,      // Filmic_MediumContrast
    __filmic_med_high_contrast, // Filmic_MediumHighContrast
    __filmic_high_contrast,     // Filmic_HighContrast
    __filmic_very_high_contrast // Filmic_VeryHighContrast
};
static_assert(sizeof(transform_luts) / sizeof(transform_luts[0]) == int(eViewTransform::_Count), "!");

namespace Ref {
force_inline simd_fvec4 FetchLUT(const eViewTransform view_transform, const int ix, const int iy, const int iz) {
    const uint32_t packed_val = transform_luts[int(view_transform)][iz * LUT_DIMS * LUT_DIMS + iy * LUT_DIMS + ix];
    const simd_ivec4 ret = simd_ivec4{int((packed_val >> 0) & 0x3ff), int((packed_val >> 10) & 0x3ff),
                                      int((packed_val >> 20) & 0x3ff), int((packed_val >> 30) & 0x3)};
    return simd_fvec4(ret) * simd_fvec4{1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 1023.0f, 1.0f / 3.0f};
}
} // namespace Ref
} // namespace Ray

Ray::Ref::simd_fvec4 vectorcall Ray::Ref::TonemapFilmic(const eViewTransform view_transform, simd_fvec4 color) {
    const simd_fvec4 encoded = color / (color + 1.0f);
    const simd_fvec4 uv = encoded * float(LUT_DIMS - 1);
    const simd_ivec4 xyz = simd_ivec4(uv);
    const simd_fvec4 f = fract(uv);
    const simd_ivec4 xyz_next = min(xyz + 1, simd_ivec4{LUT_DIMS - 1});

    const int ix = xyz.get<0>(), iy = xyz.get<1>(), iz = xyz.get<2>();
    const int jx = xyz_next.get<0>(), jy = xyz_next.get<1>(), jz = xyz_next.get<2>();
    const float fx = f.get<0>(), fy = f.get<1>(), fz = f.get<2>();

    const simd_fvec4 c000 = FetchLUT(view_transform, ix, iy, iz);
    const simd_fvec4 c001 = FetchLUT(view_transform, jx, iy, iz);
    const simd_fvec4 c010 = FetchLUT(view_transform, ix, jy, iz);
    const simd_fvec4 c011 = FetchLUT(view_transform, jx, jy, iz);
    const simd_fvec4 c100 = FetchLUT(view_transform, ix, iy, jz);
    const simd_fvec4 c101 = FetchLUT(view_transform, jx, iy, jz);
    const simd_fvec4 c110 = FetchLUT(view_transform, ix, jy, jz);
    const simd_fvec4 c111 = FetchLUT(view_transform, jx, jy, jz);

    const simd_fvec4 c00x = (1.0f - fx) * c000 + fx * c001;
    const simd_fvec4 c01x = (1.0f - fx) * c010 + fx * c011;
    const simd_fvec4 c10x = (1.0f - fx) * c100 + fx * c101;
    const simd_fvec4 c11x = (1.0f - fx) * c110 + fx * c111;

    const simd_fvec4 c0xx = (1.0f - fy) * c00x + fy * c01x;
    const simd_fvec4 c1xx = (1.0f - fy) * c10x + fy * c11x;

    simd_fvec4 cxxx = (1.0f - fz) * c0xx + fz * c1xx;
    cxxx.set<3>(color.get<3>());

    return cxxx;
}

#undef sqr

#undef USE_VNDF_GGX_SAMPLING
#undef USE_NEE
#undef USE_PATH_TERMINATION
#undef VECTORIZE_BBOX_INTERSECTION
#undef FORCE_TEXTURE_LOD
#undef USE_STOCH_TEXTURE_FILTERING
#undef USE_SAFE_MATH