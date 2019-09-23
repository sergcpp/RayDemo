R"(

/* This file should be passed to cl program first */

typedef struct _ray_packet_t {
    float4 o, d;
    float4 c;
    float4 do_dx, dd_dx, do_dy, dd_dy;
} ray_packet_t;

typedef struct _camera_t {
    float4 origin, fwd;
    float4 side, up;
    int flags;
} camera_t;

typedef struct _tri_accel_t {
    float nu, nv;
    float np;
    float pu, pv;
    int ci;
    float e0u, e0v;
    float e1u, e1v;
    uint mi, back_mi;
} tri_accel_t;

typedef struct _hit_data_t {
    int mask, obj_index, prim_index;
    float t, u, v;
    float2 ray_id;
} hit_data_t;

typedef struct _bvh_node_t {
    float bbox_min[3];
    union {
        uint prim_index;
        uint left_child;
    };
    float bbox_max[3];
    union {
        uint prim_count;
        uint right_child;
    };
#ifdef USE_STACKLESS_BVH_TRAVERSAL
    uint parent;
#endif
} bvh_node_t;

typedef struct _vertex_t {
    float p[3], n[3], b[3], t[2][2];
} vertex_t;

typedef struct _mesh_t {
    uint node_index, node_count;
    uint tris_index, tris_count;
} mesh_t;

typedef struct _transform_t {
    float16 xform, inv_xform;
} transform_t;

typedef struct _mesh_instance_t {
    float bbox_min[3];
    uint tr_index;
    float bbox_max[3];
    uint mesh_index;
} mesh_instance_t;

typedef struct _texture_t {
    ushort width;   // First bit is used as srgb flag
    ushort height;
    uchar page[NUM_MIP_LEVELS];
    ushort pos[NUM_MIP_LEVELS][2];
} texture_t;

typedef struct _material_t {
    uint textures[MAX_MATERIAL_TEXTURES];
    float3 main_color;
    uint type;
    union {
        float roughness;
        float strength;
    };
    float int_ior, ext_ior;
} material_t;

typedef struct _light_t {
    float4 pos_and_radius;
    float4 col_and_brightness;
    float4 dir_and_spot;
} light_t;

typedef struct _environment_t {
    float4 env_col_and_clamp;
    uint env_map;
    float pad[3];
} environment_t;

typedef struct _ray_chunk_t {
    uint hash, base, size;
} ray_chunk_t;

typedef struct _pass_settings_t {
    uchar max_diff_depth,
          max_glossy_depth,
          max_refr_depth,
          max_transp_depth,
          max_total_depth;
    uchar termination_start_depth;
    uchar pad[2];
    uint flags;
} pass_settings_t;

typedef struct _pass_info_t {
    int index, rand_index;
    int iteration, bounce;
    pass_settings_t settings;
} pass_info_t;

typedef struct _derivatives_t {
    float3 do_dx, do_dy, dd_dx, dd_dy;
    float2 duv_dx, duv_dy;
    float3 dndx, dndy;
    float ddn_dx, ddn_dy;
} derivatives_t;

typedef struct _shl1_data_t {
    float4 coeff_r, coeff_g, coeff_b;
} shl1_data_t;

__kernel void TypesCheck(ray_packet_t r, camera_t c, tri_accel_t t, hit_data_t i,
                         bvh_node_t b, vertex_t v, mesh_t m, mesh_instance_t mi, transform_t tr,
                         texture_t tex, material_t mat, light_t l, environment_t env, ray_chunk_t ch,
                         pass_settings_t ps, pass_info_t pi, shl1_data_t sh) {}

)"