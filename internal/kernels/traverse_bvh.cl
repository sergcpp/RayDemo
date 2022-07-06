R"(

bool __bbox_test(const float3 o, const float3 inv_d, const float t, const float3 bbox_min, const float3 bbox_max) {
    float low = inv_d.x * (bbox_min.x - o.x);
    float high = inv_d.x * (bbox_max.x - o.x);
    float tmin = fmin(low, high);
    float tmax = fmax(low, high);

    low = inv_d.y * (bbox_min.y - o.y);
    high = inv_d.y * (bbox_max.y - o.y);
    tmin = fmax(tmin, fmin(low, high));
    tmax = fmin(tmax, fmax(low, high));

    low = inv_d.z * (bbox_min.z - o.z);
    high = inv_d.z * (bbox_max.z - o.z);
    tmin = fmax(tmin, fmin(low, high));
    tmax = fmin(tmax, fmax(low, high));
    tmax *= 1.00000024f;

    return tmin <= tmax && tmin <= t && tmax > 0;
}

bool __bbox_test_fma(const float3 inv_d, const float3 neg_inv_d_o, const float t, const float3 bbox_min, const float3 bbox_max) {
    float low = fmadd(inv_d.x, bbox_min.x, neg_inv_d_o.x);
    float high = fmadd(inv_d.x, bbox_max.x, neg_inv_d_o.x);
    float tmin = fmin(low, high);
    float tmax = fmax(low, high);

    low = fmadd(inv_d.y, bbox_min.y, neg_inv_d_o.y);
    high = fmadd(inv_d.y, bbox_max.y, neg_inv_d_o.y);
    tmin = fmax(tmin, fmin(low, high));
    tmax = fmin(tmax, fmax(low, high));

    low = fmadd(inv_d.z, bbox_min.z, neg_inv_d_o.z);
    high = fmadd(inv_d.z, bbox_max.z, neg_inv_d_o.z);
    tmin = fmax(tmin, fmin(low, high));
    tmax = fmin(tmax, fmax(low, high));
    tmax *= 1.00000024f;

    return tmin <= tmax && tmin <= t && tmax > 0;
}

bool _bbox_test(const float3 o, const float3 inv_d, const float t, __global const float *bbox_min, __global const float *bbox_max) {
    float low = inv_d.x * (bbox_min[0] - o.x);
    float high = inv_d.x * (bbox_max[0] - o.x);
    float tmin = fmin(low, high);
    float tmax = fmax(low, high);

    low = inv_d.y * (bbox_min[1] - o.y);
    high = inv_d.y * (bbox_max[1] - o.y);
    tmin = fmax(tmin, fmin(low, high));
    tmax = fmin(tmax, fmax(low, high));

    low = inv_d.z * (bbox_min[2] - o.z);
    high = inv_d.z * (bbox_max[2] - o.z);
    tmin = fmax(tmin, fmin(low, high));
    tmax = fmin(tmax, fmax(low, high));
    tmax *= 1.00000024f;

    return tmin <= tmax && tmin <= t && tmax > 0;
}

bool _bbox_test_fma(const float3 inv_d, const float3 neg_inv_d_o, const float t, __global const float *bbox_min, __global const float *bbox_max) {
    float low = fmadd(inv_d.x, bbox_min[0], neg_inv_d_o.x);
    float high = fmadd(inv_d.x, bbox_max[0], neg_inv_d_o.x);
    float tmin = fmin(low, high);
    float tmax = fmax(low, high);

    low = fmadd(inv_d.y, bbox_min[1], neg_inv_d_o.y);
    high = fmadd(inv_d.y, bbox_max[1], neg_inv_d_o.y);
    tmin = fmax(tmin, fmin(low, high));
    tmax = fmin(tmax, fmax(low, high));

    low = fmadd(inv_d.z, bbox_min[2], neg_inv_d_o.z);
    high = fmadd(inv_d.z, bbox_max[2], neg_inv_d_o.z);
    tmin = fmax(tmin, fmin(low, high));
    tmax = fmin(tmax, fmax(low, high));
    tmax *= 1.00000024f;

    return tmin <= tmax && tmin <= t && tmax > 0;
}

bool _is_point_inside(const float3 p, __global const float *bbox_min, __global const float *bbox_max) {
    return p.x > bbox_min[0] && p.x < bbox_max[0] &&
           p.y > bbox_min[1] && p.y < bbox_max[1] &&
           p.z > bbox_min[2] && p.z < bbox_max[2];
}

bool bbox_test(const float3 o, const float3 inv_d, const float t, __global const bvh_node_t *node) {
    return _bbox_test(o, inv_d, t, node->bbox_min, node->bbox_max);
}

bool bbox_test_fma(const float3 o, const float3 inv_d, const float t, __global const bvh_node_t *node) {
    return _bbox_test_fma(o, inv_d, t, node->bbox_min, node->bbox_max);
}

bool is_point_inside(const float3 p, __global const bvh_node_t *node) {
    return _is_point_inside(p, node->bbox_min, node->bbox_max);
}

float3 safe_invert(const float3 v) {
    float3 inv_v = 1.0f / v;

    if (v.x <= FLT_EPS && v.x >= 0) {
        inv_v.x = FLT_MAX;
    } else if (v.x >= -FLT_EPS && v.x < 0) {
        inv_v.x = -FLT_MAX;
    }

    if (v.y <= FLT_EPS && v.y >= 0) {
        inv_v.y = FLT_MAX;
    } else if (v.y >= -FLT_EPS && v.y < 0) {
        inv_v.y = -FLT_MAX;
    }

    if (v.z <= FLT_EPS && v.z >= 0) {
        inv_v.z = FLT_MAX;
    } else if (v.z >= -FLT_EPS && v.z < 0) {
        inv_v.z = -FLT_MAX;
    }

    return inv_v;
}

#define near_child(rd, n)   \
    (rd)[(n)->prim_count >> 30] < 0 ? ((n)->right_child & RIGHT_CHILD_BITS) : (n)->left_child

#define far_child(rd, n)    \
    (rd)[(n)->prim_count >> 30] < 0 ? (n)->left_child : ((n)->right_child & RIGHT_CHILD_BITS)

void Traverse_MicroTree_WithLocalStack(const float3 r_o, const float3 r_d, const float3 inv_d, uint obj_index,
                                       __global const bvh_node_t *nodes, uint node_index,
                                       __global const tri_accel_t *tris, __global const uint *tri_indices, 
                                       __local uint *stack, hit_data_t *inter) {
    const float *rd = (const float *)&r_d;
    const float3 neg_inv_d_o = -inv_d * r_o;

    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        __global const bvh_node_t *n = &nodes[cur];

        if (!bbox_test_fma(inv_d, neg_inv_d_o, inter->t, n)) continue;

        if ((n->prim_index & LEAF_NODE_BIT) == 0) {
            stack[stack_size++] = far_child(rd, n);
            stack[stack_size++] = near_child(rd, n);
        } else {
            IntersectTris(r_o, r_d, tris, tri_indices, (n->prim_index & PRIM_INDEX_BITS), n->prim_count, obj_index, inter);
        }
    }
}

void Traverse_MicroTree_WithPrivateStack(const float3 r_o, const float3 r_d, const float3 inv_d, uint obj_index,
                                         __global const bvh_node_t *nodes, uint node_index,
                                         __global const tri_accel_t *tris, __global const uint *tri_indices, 
                                         uint *stack, hit_data_t *inter) {
    const float *rd = (const float *)&r_d;
    const float3 neg_inv_d_o = -inv_d * r_o;

    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        __global const bvh_node_t *n = &nodes[cur];

        if (!bbox_test_fma(inv_d, neg_inv_d_o, inter->t, n)) continue;

        if ((n->prim_index & LEAF_NODE_BIT) == 0) {
            stack[stack_size++] = far_child(rd, n);
            stack[stack_size++] = near_child(rd, n);
        } else {
            IntersectTris(r_o, r_d, tris, tri_indices, (n->prim_index & PRIM_INDEX_BITS), n->prim_count, obj_index, inter);
        }
    }
}

void Traverse_MicroTreeImg_WithLocalStack(const float3 r_o, const float3 r_d, const float3 inv_d, uint obj_index,
                                          __read_only image1d_buffer_t nodes, uint node_index,
                                          __global const tri_accel_t *tris, __global const uint *tri_indices, 
                                          __local uint *stack, float4 *node_data1, float4 *node_data2, hit_data_t *inter) {
    const float *rd = (const float *)&r_d;
    const float3 neg_inv_d_o = -inv_d * r_o;

    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        /*
            struct bvh_node_t {
                xxx4 node_data1;  // { bbox_min[0], bbox_min[1], bbox_min[2], prim_index/left_child  }
                xxx4 node_data2;  // { bbox_max[0], bbox_max[1], bbox_max[2], prim_count/right_child }
            };
        */

        *node_data1 = read_imagef(nodes, cur * 2 + 0);
        *node_data2 = read_imagef(nodes, cur * 2 + 1);

        if (!__bbox_test_fma(inv_d, neg_inv_d_o, inter->t, (*node_data1).xyz, (*node_data2).xyz)) continue;

        if ((as_uint((*node_data1).w) & LEAF_NODE_BIT) == 0) {
            uint space_axis = as_uint((*node_data2).w) >> 30;
            stack[stack_size++] = rd[space_axis] < 0 ? as_uint((*node_data1).w) : (as_uint((*node_data2).w) & RIGHT_CHILD_BITS);
            stack[stack_size++] = rd[space_axis] < 0 ? (as_uint((*node_data2).w) & RIGHT_CHILD_BITS) : as_uint((*node_data1).w);
        } else {
            IntersectTris(r_o, r_d, tris, tri_indices, (as_uint((*node_data1).w) & PRIM_INDEX_BITS), as_uint((*node_data2).w), obj_index, inter);
        }
    }
}

void Traverse_MicroTreeImg_WithPrivateStack(const float3 r_o, const float3 r_d, const float3 inv_d, uint obj_index,
                                            __read_only image1d_buffer_t nodes, uint node_index,
                                            __global const tri_accel_t *tris, __global const uint *tri_indices, 
                                            uint *stack, float4 *node_data1, float4 *node_data2, hit_data_t *inter) {
    const float *rd = (const float *)&r_d;
    const float3 neg_inv_d_o = -inv_d * r_o;

    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        /*
            struct bvh_node_t {
                xxx4 node_data1;  // { bbox_min[0], bbox_min[1], bbox_min[2], prim_index/left_child  }
                xxx4 node_data2;  // { bbox_max[0], bbox_max[1], bbox_max[2], prim_count/right_child }
            };
        */

        *node_data1 = read_imagef(nodes, cur * 2 + 0);
        *node_data2 = read_imagef(nodes, cur * 2 + 1);

        if (!__bbox_test_fma(inv_d, neg_inv_d_o, inter->t, (*node_data1).xyz, (*node_data2).xyz)) continue;

        if ((as_uint((*node_data1).w) & LEAF_NODE_BIT) == 0) {
            uint space_axis = as_uint((*node_data2).w) >> 30;
            stack[stack_size++] = rd[space_axis] < 0 ? as_uint((*node_data1).w) : (as_uint((*node_data2).w) & RIGHT_CHILD_BITS);
            stack[stack_size++] = rd[space_axis] < 0 ? (as_uint((*node_data2).w) & RIGHT_CHILD_BITS) : as_uint((*node_data1).w);
        } else {
            IntersectTris(r_o, r_d, tris, tri_indices, (as_uint((*node_data1).w) & PRIM_INDEX_BITS), as_uint((*node_data2).w), obj_index, inter);
        }
    }
}

float Traverse_MicroTree_Occlusion_WithLocalStack(const float3 r_o, const float3 r_d, const float3 inv_d, float max_dist,
                                             __global const bvh_node_t *nodes, uint node_index,
                                             __global const tri_accel_t *tris, __global const uint *tri_indices,
                                             __local uint *stack) {
    const float *rd = (const float *)&r_d;
    const float3 neg_inv_d_o = -inv_d * r_o;

    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        __global const bvh_node_t *n = &nodes[cur];

        if (!bbox_test_fma(inv_d, neg_inv_d_o, max_dist, n)) continue;

        if ((n->prim_index & LEAF_NODE_BIT) == 0) {
            stack[stack_size++] = far_child(rd, n);
            stack[stack_size++] = near_child(rd, n);
        } else {
            if (IntersectTris_Occlusion(r_o, r_d, max_dist, tris, tri_indices, (n->prim_index & PRIM_INDEX_BITS), n->prim_count) < 1)  {
                return 0;
            }
        }
    }

    return 1;
}

float Traverse_MicroTree_Occlusion_WithPrivateStack(const float3 r_o, const float3 r_d, const float3 inv_d, float max_dist,
                                                    __global const bvh_node_t *nodes, uint node_index,
                                                    __global const tri_accel_t *tris, __global const uint *tri_indices,
                                                    uint *stack) {
    const float *rd = (const float *)&r_d;
    const float3 neg_inv_d_o = -inv_d * r_o;

    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        __global const bvh_node_t *n = &nodes[cur];

        if (!bbox_test_fma(inv_d, neg_inv_d_o, max_dist, n)) continue;

        if ((n->prim_index & LEAF_NODE_BIT) == 0) {
            stack[stack_size++] = far_child(rd, n);
            stack[stack_size++] = near_child(rd, n);
        } else {
            if (IntersectTris_Occlusion(r_o, r_d, max_dist, tris, tri_indices, (n->prim_index & PRIM_INDEX_BITS), n->prim_count) < 1)  {
                return 0;
            }
        }
    }

    return 1;
}

)" // workaround for 16k string literal limitation on msvc
R"(

void Traverse_MacroTree_WithLocalStack(const float3 orig_r_o, const float3 orig_r_d, const float3 orig_r_inv_d, 
                                       __global const mesh_instance_t *mesh_instances, __global const uint *mi_indices, 
                                       __global const mesh_t *meshes, __global const transform_t *transforms, 
                                       __global const bvh_node_t *nodes, uint node_index, 
                                       __global const tri_accel_t *tris, __global const uint *tri_indices,
                                       __local uint *stack, hit_data_t *inter) {
    const float *orig_rd = (const float *)&orig_r_d;
    const float3 orig_neg_inv_d_o = -orig_r_inv_d * orig_r_o;
    
    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        __global const bvh_node_t *n = &nodes[cur];

        if (!bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, inter->t, n)) continue;

        if ((n->prim_index & LEAF_NODE_BIT) == 0) {
            stack[stack_size++] = far_child(orig_rd, n);
            stack[stack_size++] = near_child(orig_rd, n);
        } else {
            uint prim_index = (n->prim_index & PRIM_INDEX_BITS);
            for (uint i = prim_index; i < prim_index + n->prim_count; i++) {
                __global const mesh_instance_t *mi = &mesh_instances[mi_indices[i]];
                __global const mesh_t *m = &meshes[mi->mesh_index];
                __global const transform_t *tr = &transforms[mi->tr_index];

                if (!_bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, inter->t, mi->bbox_min, mi->bbox_max)) continue;

                const float3 r_o = TransformPoint(orig_r_o, &tr->inv_xform);
                const float3 r_d = TransformDirection(orig_r_d, &tr->inv_xform);
                const float3 inv_d = safe_invert(r_d);
                
                Traverse_MicroTree_WithLocalStack(r_o, r_d, inv_d, mi_indices[i], nodes, m->node_index, tris, tri_indices, &stack[stack_size], inter);
            }
        }
    }
}

void Traverse_MacroTree_WithPrivateStack(const float3 orig_r_o, const float3 orig_r_d, const float3 orig_r_inv_d, 
                                         __global const mesh_instance_t *mesh_instances, __global const uint *mi_indices, 
                                         __global const mesh_t *meshes, __global const transform_t *transforms, 
                                         __global const bvh_node_t *nodes, uint node_index, 
                                         __global const tri_accel_t *tris, __global const uint *tri_indices,
                                         uint *stack, hit_data_t *inter) {
    const float *orig_rd = (const float *)&orig_r_d;
    const float3 orig_neg_inv_d_o = -orig_r_inv_d * orig_r_o;
    
    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        __global const bvh_node_t *n = &nodes[cur];

        if (!bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, inter->t, n)) continue;

        if ((n->prim_index & LEAF_NODE_BIT) == 0) {
            stack[stack_size++] = far_child(orig_rd, n);
            stack[stack_size++] = near_child(orig_rd, n);
        } else {
            uint prim_index = n->prim_index & PRIM_INDEX_BITS;
            for (uint i = prim_index; i < prim_index + n->prim_count; i++) {
                __global const mesh_instance_t *mi = &mesh_instances[mi_indices[i]];
                __global const mesh_t *m = &meshes[mi->mesh_index];
                __global const transform_t *tr = &transforms[mi->tr_index];

                if (!_bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, inter->t, mi->bbox_min, mi->bbox_max)) continue;

                const float3 r_o = TransformPoint(orig_r_o, &tr->inv_xform);
                const float3 r_d = TransformDirection(orig_r_d, &tr->inv_xform);
                const float3 inv_d = safe_invert(r_d);
                
                Traverse_MicroTree_WithPrivateStack(r_o, r_d, inv_d, mi_indices[i], nodes, m->node_index, tris, tri_indices, &stack[stack_size], inter);
            }
        }
    }
}

void Traverse_MacroTreeImg_WithLocalStack(const float3 orig_r_o, const float3 orig_r_d, const float3 orig_r_inv_d, 
                                          __global const mesh_instance_t *mesh_instances, __global const uint *mi_indices, 
                                          __global const mesh_t *meshes, __global const transform_t *transforms, 
                                          __read_only image1d_buffer_t nodes, uint node_index, 
                                          __global const tri_accel_t *tris, __global const uint *tri_indices,
                                          __local uint *stack, hit_data_t *inter) {
    const float *orig_rd = (const float *)&orig_r_d;
    const float3 orig_neg_inv_d_o = -orig_r_inv_d * orig_r_o;

    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        /*
            struct bvh_node_t {
                xxx4 node_data1;  // { bbox_min[0], bbox_min[1], bbox_min[2], prim_index/left_child  }
                xxx4 node_data2;  // { bbox_max[0], bbox_max[1], bbox_max[2], prim_count/right_child }
            };
        */

        float4 node_data1 = read_imagef(nodes, cur * 2 + 0);
        float4 node_data2 = read_imagef(nodes, cur * 2 + 1);

        if (!__bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, inter->t, node_data1.xyz, node_data2.xyz)) continue;

        if ((as_uint(node_data1.w) & LEAF_NODE_BIT) == 0) {
            uint space_axis = as_uint(node_data2.w) >> 30;
            stack[stack_size++] = orig_rd[space_axis] < 0 ? as_uint(node_data1.w) : (as_uint(node_data2.w) & RIGHT_CHILD_BITS);
            stack[stack_size++] = orig_rd[space_axis] < 0 ? (as_uint(node_data2.w) & RIGHT_CHILD_BITS) : as_uint(node_data1.w);
        } else {
            const uint index_start = as_uint(node_data1.w) & PRIM_INDEX_BITS;
            const uint index_end = index_start + as_uint(node_data2.w);
            for (uint i = index_start; i < index_end; i++) {
                __global const mesh_instance_t *mi = &mesh_instances[mi_indices[i]];
                __global const mesh_t *m = &meshes[mi->mesh_index];
                __global const transform_t *tr = &transforms[mi->tr_index];

                if (!_bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, inter->t, mi->bbox_min, mi->bbox_max)) continue;

                const float3 r_o = TransformPoint(orig_r_o, &tr->inv_xform);
                const float3 r_d = TransformDirection(orig_r_d, &tr->inv_xform);
                const float3 inv_d = safe_invert(r_d);
                
                Traverse_MicroTreeImg_WithLocalStack(r_o, r_d, inv_d, mi_indices[i], nodes, m->node_index, tris, tri_indices,
                                                     &stack[stack_size], &node_data1, &node_data2, inter);
            }
        }
    }
}

)" // workaround for 16k string literal limitation on msvc
R"(

void Traverse_MacroTreeImg_WithPrivateStack(const float3 orig_r_o, const float3 orig_r_d, const float3 orig_r_inv_d, 
                                            __global const mesh_instance_t *mesh_instances, __global const uint *mi_indices, 
                                            __global const mesh_t *meshes, __global const transform_t *transforms, 
                                            __read_only image1d_buffer_t nodes, uint node_index, 
                                            __global const tri_accel_t *tris, __global const uint *tri_indices,
                                            uint *stack, hit_data_t *inter) {
    const float *orig_rd = (const float *)&orig_r_d;
    const float3 orig_neg_inv_d_o = -orig_r_inv_d * orig_r_o;

    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        /*
            struct bvh_node_t {
                xxx4 node_data1;  // { bbox_min[0], bbox_min[1], bbox_min[2], prim_index/left_child  }
                xxx4 node_data2;  // { bbox_max[0], bbox_max[1], bbox_max[2], prim_count/right_child }
            };
        */

        float4 node_data1 = read_imagef(nodes, cur * 2 + 0);
        float4 node_data2 = read_imagef(nodes, cur * 2 + 1);

        if (!__bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, inter->t, node_data1.xyz, node_data2.xyz)) continue;

        if ((as_uint(node_data1.w) & LEAF_NODE_BIT) == 0) {
            uint space_axis = as_uint(node_data2.w) >> 30;
            stack[stack_size++] = orig_rd[space_axis] < 0 ? as_uint(node_data1.w) : (as_uint(node_data2.w) & RIGHT_CHILD_BITS);
            stack[stack_size++] = orig_rd[space_axis] < 0 ? (as_uint(node_data2.w) & RIGHT_CHILD_BITS) : as_uint(node_data1.w);
        } else {
            const uint index_start = as_uint(node_data1.w) & PRIM_INDEX_BITS;
            const uint index_end = index_start + as_uint(node_data2.w);
            for (uint i = index_start; i < index_end; i++) {
                __global const mesh_instance_t *mi = &mesh_instances[mi_indices[i]];
                __global const mesh_t *m = &meshes[mi->mesh_index];
                __global const transform_t *tr = &transforms[mi->tr_index];

                if (!_bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, inter->t, mi->bbox_min, mi->bbox_max)) continue;

                const float3 r_o = TransformPoint(orig_r_o, &tr->inv_xform);
                const float3 r_d = TransformDirection(orig_r_d, &tr->inv_xform);
                const float3 inv_d = safe_invert(r_d);
                
                Traverse_MicroTreeImg_WithPrivateStack(r_o, r_d, inv_d, mi_indices[i], nodes, m->node_index, tris, tri_indices,
                                                       &stack[stack_size], &node_data1, &node_data2, inter);
            }
        }
    }
}

float Traverse_MacroTree_Occlusion_WithLocalStack(const float3 orig_r_o, const float3 orig_r_d, const float3 orig_r_inv_d, float max_dist,
                                                  __global const mesh_instance_t *mesh_instances, __global const uint *mi_indices, 
                                                  __global const mesh_t *meshes, __global const transform_t *transforms, 
                                                  __global const bvh_node_t *nodes, uint node_index, 
                                                  __global const tri_accel_t *tris, __global const uint *tri_indices,
                                                  __local uint *stack) {
    const float *orig_rd = (const float *)&orig_r_d;
    const float3 orig_neg_inv_d_o = -orig_r_inv_d * orig_r_o;
    
    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        __global const bvh_node_t *n = &nodes[cur];

        if (!bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, max_dist, n)) continue;

        if ((n->prim_index & LEAF_NODE_BIT) == 0) {
            stack[stack_size++] = far_child(orig_rd, n);
            stack[stack_size++] = near_child(orig_rd, n);
        } else {
            uint prim_index = n->prim_index & PRIM_INDEX_BITS;
            for (uint i = prim_index; i < prim_index + n->prim_count; i++) {
                __global const mesh_instance_t *mi = &mesh_instances[mi_indices[i]];
                __global const mesh_t *m = &meshes[mi->mesh_index];
                __global const transform_t *tr = &transforms[mi->tr_index];

                if (!_bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, max_dist, mi->bbox_min, mi->bbox_max)) continue;

                const float3 r_o = TransformPoint(orig_r_o, &tr->inv_xform);
                const float3 r_d = TransformDirection(orig_r_d, &tr->inv_xform);
                const float3 inv_d = safe_invert(r_d);
                
                if (Traverse_MicroTree_Occlusion_WithLocalStack(r_o, r_d, inv_d, max_dist, nodes, m->node_index, tris, tri_indices, &stack[stack_size]) < 1) {
                    return 0;
                }
            }
        }
    }

    return 1;
}

float Traverse_MacroTree_Occlusion_WithPrivateStack(const float3 orig_r_o, const float3 orig_r_d, const float3 orig_r_inv_d, float max_dist,
                                                    __global const mesh_instance_t *mesh_instances, __global const uint *mi_indices, 
                                                    __global const mesh_t *meshes, __global const transform_t *transforms, 
                                                    __global const bvh_node_t *nodes, uint node_index, 
                                                    __global const tri_accel_t *tris, __global const uint *tri_indices, uint *stack) {
    const float *orig_rd = (const float *)&orig_r_d;
    const float3 orig_neg_inv_d_o = -orig_r_inv_d * orig_r_o;
    
    uint stack_size = 0;
    stack[stack_size++] = node_index;

    while (stack_size) {
        uint cur = stack[--stack_size];

        __global const bvh_node_t *n = &nodes[cur];

        if (!bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, max_dist, n)) continue;

        if ((n->prim_index & LEAF_NODE_BIT) == 0) {
            stack[stack_size++] = far_child(orig_rd, n);
            stack[stack_size++] = near_child(orig_rd, n);
        } else {
            uint prim_index = n->prim_index & PRIM_INDEX_BITS;
            for (uint i = prim_index; i < prim_index + n->prim_count; i++) {
                __global const mesh_instance_t *mi = &mesh_instances[mi_indices[i]];
                __global const mesh_t *m = &meshes[mi->mesh_index];
                __global const transform_t *tr = &transforms[mi->tr_index];

                if (!_bbox_test_fma(orig_r_inv_d, orig_neg_inv_d_o, max_dist, mi->bbox_min, mi->bbox_max)) continue;

                const float3 r_o = TransformPoint(orig_r_o, &tr->inv_xform);
                const float3 r_d = TransformDirection(orig_r_d, &tr->inv_xform);
                const float3 inv_d = safe_invert(r_d);
                
                if (Traverse_MicroTree_Occlusion_WithPrivateStack(r_o, r_d, inv_d, max_dist, nodes, m->node_index, tris, tri_indices, &stack[stack_size]) < 1) {
                    return 0;
                }
            }
        }
    }

    return 1;
}

#undef near_child
#undef far_child

)"