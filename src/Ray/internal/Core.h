#pragma once

#include <cmath>
#include <cstdint>

#include "../SceneBase.h"
#include "../Types.h"
#include "Span.h"

#ifdef __GNUC__
#define force_inline __attribute__((always_inline)) inline
#define assume_aligned(ptr, sz) (__builtin_assume_aligned((const void *)ptr, sz))
#define vectorcall
#endif
#ifdef _MSC_VER
#define force_inline __forceinline
#define vectorcall __vectorcall
#define assume_aligned(ptr, sz) (__assume((((const char *)ptr) - ((const char *)0)) % (sz) == 0), (ptr))

#include <intrin.h>

#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_bittestandcomplement)

#ifdef _M_IX86
// Win32 doesn't have _BitScanForward64 so emulate it with two 32 bit calls
force_inline unsigned char _BitScanForward64(unsigned long *Index, unsigned __int64 Mask) {
    // Scan the Low Word
    if (_BitScanForward(Index, static_cast<unsigned long>(Mask))) {
        return 1;
    }
    // Scan the High Word
    if (_BitScanForward(Index, static_cast<unsigned long>(Mask >> 32))) {
        *Index += 32; // Create a bit offset from the LSB
        return 1;
    }
    return 0;
}
#endif

#endif

#define unused(x) ((void)x)
#define countof(x) (sizeof(x) / sizeof(x[0]))

#include "simd/aligned_allocator.h"

#define pack_unorm_16(x) uint16_t(x * 65535.0f)
#define unpack_unorm_16(x) (float(x) / 65535.0f)

namespace Ray {
enum eUninitialize { Uninitialize };

struct alignas(16) tri_accel_t {
    float n_plane[4];
    float u_plane[4];
    float v_plane[4];
};
static_assert(sizeof(tri_accel_t) == 48, "!");

struct alignas(32) mtri_accel_t {
    float n_plane[4][8];
    float u_plane[4][8];
    float v_plane[4][8];
};
static_assert(sizeof(mtri_accel_t) == 384, "!");

const float HIT_BIAS = 0.00001f;
const float HIT_EPS = 0.000001f;
const float FLT_EPS = 0.0000001f;

const float PI = 3.141592653589793238463f;

const float MAX_DIST = 3.402823466e+30F; // 3.402823466e+38F

const int MAX_BOUNCES = 32;

const float LIGHT_ATTEN_CUTOFF = 0.001f;

const uint32_t LEAF_NODE_BIT = (1u << 31);
const uint32_t PRIM_INDEX_BITS = ~LEAF_NODE_BIT;
const uint32_t LEFT_CHILD_BITS = ~LEAF_NODE_BIT;

const uint32_t SEP_AXIS_BITS = (0b11u << 30);
const uint32_t PRIM_COUNT_BITS = ~SEP_AXIS_BITS;
const uint32_t RIGHT_CHILD_BITS = ~SEP_AXIS_BITS;

struct bvh_node_t {
    float bbox_min[3];
    union {
        uint32_t prim_index; // First bit is used to identify leaf node
        uint32_t left_child;
    };
    float bbox_max[3];
    union {
        uint32_t prim_count; // First two bits are used for separation axis (0, 1 or 2 - x, y or z)
        uint32_t right_child;
    };
};
static_assert(sizeof(bvh_node_t) == 32, "!");

struct alignas(32) mbvh_node_t {
    float bbox_min[3][8];
    float bbox_max[3][8];
    uint32_t child[8];
};
static_assert(sizeof(mbvh_node_t) == 224, "!");

const int NUM_MIP_LEVELS = 14;
const int MAX_MIP_LEVEL = NUM_MIP_LEVELS - 1;
const int MAX_TEXTURE_SIZE = (1 << MAX_MIP_LEVEL);

const int TEXTURE_ATLAS_SIZE = 8192 + 256; // small margin to account for borders

const int ATLAS_TEX_SRGB_BIT = 0b1000000000000000;
const int ATLAS_TEX_RECONSTRUCT_Z_BIT = 0b0100000000000000;
const int ATLAS_TEX_WIDTH_BITS = 0b0011111111111111;
const int ATLAS_TEX_MIPS_BIT = 0b1000000000000000;
const int ATLAS_TEX_HEIGHT_BITS = 0b0011111111111111;

struct atlas_texture_t {
    uint16_t width;
    uint16_t height;
    uint32_t atlas;
    uint8_t page[NUM_MIP_LEVELS + 2];
    uint16_t pos[NUM_MIP_LEVELS][2];
};
static_assert(sizeof(atlas_texture_t) == 80, "!");

const uint32_t TEX_SRGB_BIT = (0b00000001u << 24);
const uint32_t TEX_RECONSTRUCT_Z_BIT = (0b00000010u << 24);
const uint32_t TEX_YCOCG_BIT = (0b00000100u << 24);

const int MAX_MATERIAL_TEXTURES = 5;

const int NORMALS_TEXTURE = 0;
const int BASE_TEXTURE = 1;
const int ROUGH_TEXTURE = 2;
const int METALLIC_TEXTURE = 3;
const int SPECULAR_TEXTURE = 4;

const int MIX_MAT1 = 3;
const int MIX_MAT2 = 4;

const int MAX_STACK_SIZE = 48;

struct tri_mat_data_t {
    uint16_t front_mi, back_mi;
};

const int MATERIAL_SOLID_BIT = 0b1000000000000000;
const int MATERIAL_INDEX_BITS = 0b0011111111111111;

const uint32_t MAT_FLAG_MULT_IMPORTANCE = (1u << 0u);
const uint32_t MAT_FLAG_MIX_ADD = (1u << 1u);

struct material_t {
    uint32_t textures[MAX_MATERIAL_TEXTURES];
    float base_color[3];
    uint32_t flags;
    eShadingNode type;
    union {
        struct {
            float tangent_rotation;
        };
        struct {
            float strength;
        };
    };
    uint16_t roughness_unorm;
    uint16_t anisotropic_unorm;
    float ior;
    uint16_t sheen_unorm;
    uint16_t sheen_tint_unorm;
    uint16_t tint_unorm;
    uint16_t metallic_unorm;
    uint16_t transmission_unorm;
    uint16_t transmission_roughness_unorm;
    uint16_t specular_unorm;
    uint16_t specular_tint_unorm;
    uint16_t clearcoat_unorm;
    uint16_t clearcoat_roughness_unorm;
    uint16_t normal_map_strength_unorm;
    uint16_t _pad;
};
static_assert(sizeof(material_t) == 76, "!");

const int LIGHT_TYPE_SPHERE = 0;
const int LIGHT_TYPE_SPOT = 1;
const int LIGHT_TYPE_DIR = 2;
const int LIGHT_TYPE_LINE = 3;
const int LIGHT_TYPE_RECT = 4;
const int LIGHT_TYPE_DISK = 5;
const int LIGHT_TYPE_TRI = 6;
const int LIGHT_TYPE_ENV = 7;

struct light_t {
    uint32_t type : 5;
    uint32_t cast_shadow : 1;
    uint32_t visible : 1;
    uint32_t sky_portal : 1;
    uint32_t _unused : 24;
    float col[3];
    union {
        struct {
            float pos[3], area;
            float dir[3], radius;
            float spot, blend, _unused[2];
        } sph;
        struct {
            float pos[3], area;
            float u[3], _unused0;
            float v[3], _unused1;
        } rect;
        struct {
            float pos[3], area;
            float u[3], _unused0;
            float v[3], _unused1;
        } disk;
        struct {
            float pos[3], area;
            float u[3], radius;
            float v[3], height;
        } line;
        struct {
            uint32_t tri_index;
            uint32_t xform_index;
            float _unused[10];
        } tri;
        struct {
            float dir[3], angle;
            float _unused[8];
        } dir;
    };
};
static_assert(sizeof(light_t) == 64, "!");

struct prim_t;

struct bvh_settings_t {
    float oversplit_threshold = 0.95f;
    bool allow_spatial_splits = false;
    bool use_fast_bvh_build = false;
    int min_primitives_in_leaf = 8;
};

template <typename T, size_t Alignment = alignof(T)>
using aligned_vector = std::vector<T, aligned_allocator<T, Alignment>>;

// bit scan forward
force_inline long GetFirstBit(long mask) {
#ifdef _MSC_VER
    unsigned long ret;
    _BitScanForward(&ret, (unsigned long)mask);
    return long(ret);
#else
    return long(__builtin_ffsl(mask) - 1);
#endif
}

force_inline bool GetFirstBit(const uint64_t mask, unsigned long *bit_index) {
#ifdef _MSC_VER
    return _BitScanForward64(bit_index, mask);
#else
    const int ret = __builtin_ffsll((long long)mask);
    (*bit_index) = ret - 1;
    return ret != 0;
#endif
}

force_inline int CountTrailingZeroes(const uint64_t mask) {
#ifdef _MSC_VER
    // return int(_tzcnt_u64(mask));
    if (mask == 0) {
        return 64;
    }
    unsigned long r = 0;
    _BitScanForward64(&r, mask);
    return r;
#else
    return (mask == 0) ? 64 : __builtin_ctzll(mask);
#endif
}

// bit test and complement
force_inline long ClearBit(long mask, long index) {
#ifdef _MSC_VER
    _bittestandcomplement(&mask, index);
    return mask;
#else
    return (mask & ~(1 << index));
#endif
}

// Creates struct of precomputed triangle data for faster Plucker intersection test
bool PreprocessTri(const float *p, int stride, tri_accel_t *out_acc);

// Builds BVH for mesh and precomputes triangle data
uint32_t PreprocessMesh(const float *attrs, Span<const uint32_t> vtx_indices, eVertexLayout layout, int base_vertex,
                        const bvh_settings_t &s, std::vector<bvh_node_t> &out_nodes, aligned_vector<tri_accel_t> &out_tris,
                        std::vector<uint32_t> &out_indices, aligned_vector<mtri_accel_t> &out_tris2);

// Recursively builds linear bvh for a set of primitives
uint32_t EmitLBVH_Recursive(const prim_t *prims, const uint32_t *indices, const uint32_t *morton_codes,
                            uint32_t prim_index, uint32_t prim_count, uint32_t index_offset, int bit_index,
                            std::vector<bvh_node_t> &out_nodes);
// Iteratively builds linear bvh for a set of primitives
uint32_t EmitLBVH_NonRecursive(const prim_t *prims, const uint32_t *indices, const uint32_t *morton_codes,
                               uint32_t prim_index, uint32_t prim_count, uint32_t index_offset, int bit_index,
                               std::vector<bvh_node_t> &out_nodes);

// Builds SAH-based BVH for a set of primitives, slow
uint32_t PreprocessPrims_SAH(Span<const prim_t> prims, const float *positions, size_t stride, const bvh_settings_t &s,
                             std::vector<bvh_node_t> &out_nodes, std::vector<uint32_t> &out_indices);

// Builds linear BVH for a set of primitives, fast
uint32_t PreprocessPrims_HLBVH(Span<const prim_t> prims, std::vector<bvh_node_t> &out_nodes,
                               std::vector<uint32_t> &out_indices);

uint32_t FlattenBVH_Recursive(const bvh_node_t *nodes, uint32_t node_index, uint32_t parent_index,
                              aligned_vector<mbvh_node_t> &out_nodes);

bool NaiivePluckerTest(const float p[9], const float o[3], const float d[3]);

void ConstructCamera(eCamType type, eFilterType filter, eDeviceType dtype, const float origin[3], const float fwd[3],
                     const float up[3], const float shift[2], float fov, float sensor_height, float exposure,
                     float gamma, float focus_distance, float fstop, float lens_rotation, float lens_ratio,
                     int lens_blades, float clip_start, float clip_end, camera_t *cam);

// Applies 4x4 matrix matrix transform to bounding box
void TransformBoundingBox(const float bbox_min[3], const float bbox_max[3], const float *xform, float out_bbox_min[3],
                          float out_bbox_max[3]);

void InverseMatrix(const float mat[16], float out_mat[16]);

// Arrays of prime numbers, used to generate halton sequence for sampling
const int PrimesCount = 228;
extern const int g_primes[];

const int HALTON_COUNT = PrimesCount;
const int HALTON_SEQ_LEN = 256;

const int RAND_DIM_FILTER_U = 0;
const int RAND_DIM_FILTER_V = 1;
const int RAND_DIM_LENS_U = 2;
const int RAND_DIM_LENS_V = 3;
const int RAND_DIM_BASE_COUNT = 4; // independent from bounce count

const int RAND_DIM_BSDF_PICK = 0;
const int RAND_DIM_BSDF_U = 1;
const int RAND_DIM_BSDF_V = 2;
const int RAND_DIM_LIGHT_PICK = 3;
const int RAND_DIM_LIGHT_U = 4;
const int RAND_DIM_LIGHT_V = 5;
const int RAND_DIM_TERMINATE = 6;
const int RAND_DIM_BOUNCE_COUNT = 7; // separate for each bounce

// Sampling stages must be independent from each other (otherwise it may lead to artifacts), so different halton
// sequences at each ray bounce must be used. This leads to limited number of bounces. Can be easily fixed by generating
// huge primes table above
static_assert(RAND_DIM_BASE_COUNT + MAX_BOUNCES * RAND_DIM_BOUNCE_COUNT <= HALTON_COUNT, "!");

struct vertex_t {
    float p[3], n[3], b[3], t[2][2];
};
static_assert(sizeof(vertex_t) == 52, "!");

struct mesh_t {
    float bbox_min[3], bbox_max[3];
    uint32_t node_index, node_count;
    uint32_t tris_index, tris_count;
    uint32_t vert_index, vert_count;
};
static_assert(sizeof(mesh_t) == 48, "!");

struct transform_t {
    float xform[16], inv_xform[16];
};
static_assert(sizeof(transform_t) == 128, "!");

struct mesh_instance_t {
    float bbox_min[3];
    uint32_t tr_index;
    float bbox_max[3];
    uint32_t mesh_index;
};
static_assert(sizeof(mesh_instance_t) == 32, "!");

struct environment_t {
    float env_col[3];
    uint32_t env_map;
    float back_col[3];
    uint32_t back_map;
    float env_map_rotation;
    float back_map_rotation;
    const float *qtree_mips[16];
    int qtree_levels;
    bool multiple_importance;
};

force_inline float to_norm_float(uint8_t v) {
    uint32_t val = 0x3f800000 + v * 0x8080 + (v + 1) / 2;
    union {
        uint32_t i;
        float f;
    } ret = {val};
    return ret.f - 1.0f;
}

force_inline void rgbe_to_rgb(const uint8_t rgbe[4], float out_rgb[3]) {
    const float f = std::exp2(float(rgbe[3]) - 128.0f);
    out_rgb[0] = to_norm_float(rgbe[0]) * f;
    out_rgb[1] = to_norm_float(rgbe[1]) * f;
    out_rgb[2] = to_norm_float(rgbe[2]) * f;
}

void CanonicalToDir(const float p[2], float y_rotation, float out_d[3]);
void DirToCanonical(const float d[3], float y_rotation, float out_p[2]);

extern const uint8_t morton_table_16[];
extern const int morton_table_256[];

extern const float omega_step;
extern const char omega_table[];

extern const float phi_step;
extern const char phi_table[][17];

struct ray_chunk_t {
    uint32_t hash, base, size;
};

/*struct pass_info_t {
    int iteration, bounce;
    pass_settings_t settings;

    force_inline bool should_add_direct_light() const {
        // skip for primary bounce if we want only indirect light contribution
        return !(settings.flags & SkipDirectLight) || bounce > 2;
    }

    force_inline bool should_add_environment() const { return !(settings.flags & NoBackground) || bounce > 2; }

    force_inline bool should_consider_albedo() const {
        // do not use albedo in lightmap mode for primary bounce
        return !(settings.flags & LightingOnly) || bounce > 2;
    }

    force_inline bool use_uniform_sampling() const {
        // do not use diffuse-specific sampling
        return ((settings.flags & OutputSH) && bounce <= 2);
    }
};
static_assert(sizeof(pass_info_t) == 20, "!");*/

struct scene_data_t {
    const environment_t &env;
    const mesh_instance_t *mesh_instances;
    const uint32_t *mi_indices;
    const mesh_t *meshes;
    const transform_t *transforms;
    const uint32_t *vtx_indices;
    const vertex_t *vertices;
    const bvh_node_t *nodes;
    const mbvh_node_t *mnodes;
    const tri_accel_t *tris;
    const uint32_t *tri_indices;
    const mtri_accel_t *mtris;
    const tri_mat_data_t *tri_materials;
    const material_t *materials;
    const light_t *lights;
    Span<const uint32_t> li_indices;
    Span<const uint32_t> visible_lights;
    Span<const uint32_t> blocker_lights;
};

} // namespace Ray
