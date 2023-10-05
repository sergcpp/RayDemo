#ifndef SHADE_INTERFACE_H
#define SHADE_INTERFACE_H

#include "_interface_common.h"

INTERFACE_START(Shade)

struct Params {
    UVEC4_TYPE rect;
    VEC4_TYPE env_col;
    VEC4_TYPE back_col;
    //
    int iteration;
    int li_count;
    int max_diff_depth;
    int max_spec_depth;
    //
    int max_refr_depth;
    int max_transp_depth;
    int max_total_depth;
    int min_total_depth;
    //
    UINT_TYPE rand_seed;
    int env_qtree_levels;
    float env_rotation;
    float back_rotation;
    //
    int env_light_index;
    float clamp_val;
    UINT_TYPE env_map_res;
    UINT_TYPE back_map_res;
};

const int LOCAL_GROUP_SIZE_X = 8;
const int LOCAL_GROUP_SIZE_Y = 8;

const int HITS_BUF_SLOT = 6;
const int RAYS_BUF_SLOT = 7;
const int LIGHTS_BUF_SLOT = 8;
const int LI_INDICES_BUF_SLOT = 9;
const int TRIS_BUF_SLOT = 10;
const int TRI_MATERIALS_BUF_SLOT = 11;
const int MATERIALS_BUF_SLOT = 12;
const int TRANSFORMS_BUF_SLOT = 13;
const int MESH_INSTANCES_BUF_SLOT = 14;
const int VERTICES_BUF_SLOT = 15;
const int VTX_INDICES_BUF_SLOT = 16;
const int RANDOM_SEQ_BUF_SLOT = 17;
const int LIGHT_WNODES_BUF_SLOT = 18;
const int ENV_QTREE_TEX_SLOT = 19;

const int OUT_IMG_SLOT = 0;
const int OUT_RAYS_BUF_SLOT = 1;
const int OUT_SH_RAYS_BUF_SLOT = 2;
const int INOUT_COUNTERS_BUF_SLOT = 3;

const int OUT_BASE_COLOR_IMG_SLOT = 4;
const int OUT_DEPTH_NORMALS_IMG_SLOT = 5;

INTERFACE_END

#endif // SHADE_INTERFACE_H
