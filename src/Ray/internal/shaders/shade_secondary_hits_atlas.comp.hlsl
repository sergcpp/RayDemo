struct atlas_texture_t
{
    uint size;
    uint atlas;
    uint page[4];
    uint pos[14];
};

struct light_sample_t
{
    float3 col;
    float3 L;
    float area;
    float dist;
    float pdf;
    float cast_shadow;
};

struct hit_data_t
{
    int mask;
    int obj_index;
    int prim_index;
    float t;
    float u;
    float v;
};

struct ray_data_t
{
    float o[3];
    float d[3];
    float pdf;
    float c[3];
    float cone_width;
    float cone_spread;
    int xy;
    int ray_depth;
};

struct Params
{
    uint2 img_size;
    int hi;
    int li_count;
    int max_diff_depth;
    int max_spec_depth;
    int max_refr_depth;
    int max_transp_depth;
    int max_total_depth;
    int termination_start_depth;
    float env_rotation;
    int env_qtree_levels;
    float4 env_col;
};

struct light_t
{
    uint4 type_and_param0;
    float4 param1;
    float4 param2;
    float4 param3;
};

struct transform_t
{
    row_major float4x4 xform;
    row_major float4x4 inv_xform;
};

struct vertex_t
{
    float p[3];
    float n[3];
    float b[3];
    float t[2][2];
};

struct material_t
{
    uint textures[5];
    float base_color[3];
    uint flags;
    uint type;
    float tangent_rotation_or_strength;
    uint roughness_and_anisotropic;
    float int_ior;
    float ext_ior;
    uint sheen_and_sheen_tint;
    uint tint_and_metallic;
    uint transmission_and_transmission_roughness;
    uint specular_and_specular_tint;
    uint clearcoat_and_clearcoat_roughness;
    uint normal_map_strength_unorm;
};

struct mesh_instance_t
{
    float4 bbox_min;
    float4 bbox_max;
};

struct shadow_ray_t
{
    float o[3];
    float d[3];
    float dist;
    float c[3];
    int xy;
};

struct tri_accel_t
{
    float4 n_plane;
    float4 u_plane;
    float4 v_plane;
};

static const uint3 gl_WorkGroupSize = uint3(8u, 8u, 1u);

ByteAddressBuffer _855 : register(t20, space0);
ByteAddressBuffer _3219 : register(t15, space0);
ByteAddressBuffer _3256 : register(t6, space0);
ByteAddressBuffer _3260 : register(t7, space0);
ByteAddressBuffer _4173 : register(t11, space0);
ByteAddressBuffer _4198 : register(t13, space0);
ByteAddressBuffer _4202 : register(t14, space0);
ByteAddressBuffer _4513 : register(t10, space0);
ByteAddressBuffer _4517 : register(t9, space0);
ByteAddressBuffer _5300 : register(t12, space0);
RWByteAddressBuffer _6376 : register(u3, space0);
RWByteAddressBuffer _6386 : register(u2, space0);
RWByteAddressBuffer _8685 : register(u1, space0);
ByteAddressBuffer _8768 : register(t5, space0);
ByteAddressBuffer _8794 : register(t4, space0);
ByteAddressBuffer _8898 : register(t8, space0);
cbuffer UniformParams
{
    Params _3224_g_params : packoffset(c0);
};

Texture2DArray<float4> g_atlases[7] : register(t21, space0);
SamplerState _g_atlases_sampler[7] : register(s21, space0);
Texture2D<float4> g_env_qtree : register(t16, space0);
SamplerState _g_env_qtree_sampler : register(s16, space0);
RWTexture2D<float4> g_out_img : register(u0, space0);

static uint3 gl_WorkGroupID;
static uint gl_LocalInvocationIndex;
struct SPIRV_Cross_Input
{
    uint3 gl_WorkGroupID : SV_GroupID;
    uint gl_LocalInvocationIndex : SV_GroupIndex;
};

float2 TransformUV(float2 _uv, atlas_texture_t t, int mip_level)
{
    uint _9029[14] = t.pos;
    uint _9032[14] = t.pos;
    uint _948 = t.size & 16383u;
    uint _951 = t.size >> uint(16);
    uint _952 = _951 & 16383u;
    float2 size = float2(float(_948), float(_952));
    if ((_951 & 32768u) != 0u)
    {
        size = float2(float(_948 >> uint(mip_level)), float(_952 >> uint(mip_level)));
    }
    return mad(frac(_uv), size, float2(float(_9029[mip_level] & 65535u), float((_9032[mip_level] >> uint(16)) & 65535u))) + 1.0f.xx;
}

float3 rgbe_to_rgb(float4 rgbe)
{
    return rgbe.xyz * exp2(mad(255.0f, rgbe.w, -128.0f));
}

float3 SampleLatlong_RGBE(atlas_texture_t t, float3 dir, float y_rotation)
{
    float _1123 = sqrt(mad(dir.x, dir.x, dir.z * dir.z));
    float _1128;
    if (_1123 > 1.0000000116860974230803549289703e-07f)
    {
        _1128 = clamp(dir.x / _1123, -1.0f, 1.0f);
    }
    else
    {
        _1128 = 0.0f;
    }
    float _1139 = acos(_1128) + y_rotation;
    float phi = _1139;
    if (_1139 < 0.0f)
    {
        phi += 6.283185482025146484375f;
    }
    if (phi > 6.283185482025146484375f)
    {
        phi -= 6.283185482025146484375f;
    }
    float u = phi * 0.15915493667125701904296875f;
    [flatten]
    if (dir.z < 0.0f)
    {
        u = 1.0f - u;
    }
    float2 _1168 = TransformUV(float2(u, acos(clamp(dir.y, -1.0f, 1.0f)) * 0.3183098733425140380859375f), t, 0) + 1.0f.xx;
    uint _1175 = t.atlas;
    int3 _1184 = int3(int2(_1168), int(t.page[0] & 255u));
    float2 _1231 = frac(_1168);
    float4 param = g_atlases[NonUniformResourceIndex(_1175)].Load(int4(_1184, 0), int2(0, 0));
    float4 param_1 = g_atlases[NonUniformResourceIndex(_1175)].Load(int4(_1184, 0), int2(1, 0));
    float4 param_2 = g_atlases[NonUniformResourceIndex(_1175)].Load(int4(_1184, 0), int2(0, 1));
    float4 param_3 = g_atlases[NonUniformResourceIndex(_1175)].Load(int4(_1184, 0), int2(1, 1));
    float _1251 = _1231.x;
    float _1256 = 1.0f - _1251;
    float _1272 = _1231.y;
    return (((rgbe_to_rgb(param_3) * _1251) + (rgbe_to_rgb(param_2) * _1256)) * _1272) + (((rgbe_to_rgb(param_1) * _1251) + (rgbe_to_rgb(param) * _1256)) * (1.0f - _1272));
}

float2 DirToCanonical(float3 d, float y_rotation)
{
    float _585 = (-atan2(d.z, d.x)) + y_rotation;
    float phi = _585;
    if (_585 < 0.0f)
    {
        phi += 6.283185482025146484375f;
    }
    if (phi > 6.283185482025146484375f)
    {
        phi -= 6.283185482025146484375f;
    }
    return float2((clamp(d.y, -1.0f, 1.0f) + 1.0f) * 0.5f, phi * 0.15915493667125701904296875f);
}

float Evaluate_EnvQTree(float y_rotation, Texture2D<float4> qtree_tex, SamplerState _qtree_tex_sampler, int qtree_levels, float3 L)
{
    int res = 2;
    int lod = qtree_levels - 1;
    float2 _611 = DirToCanonical(L, y_rotation);
    float factor = 1.0f;
    while (lod >= 0)
    {
        int2 _631 = clamp(int2(_611 * float(res)), int2(0, 0), (res - 1).xx);
        float4 quad = qtree_tex.Load(int3(_631 / int2(2, 2), lod));
        float _666 = ((quad.x + quad.y) + quad.z) + quad.w;
        if (_666 <= 0.0f)
        {
            break;
        }
        factor *= ((4.0f * quad[(0 | ((_631.x & 1) << 0)) | ((_631.y & 1) << 1)]) / _666);
        lod--;
        res *= 2;
    }
    return factor * 0.079577468335628509521484375f;
}

float power_heuristic(float a, float b)
{
    float _1285 = a * a;
    return _1285 / mad(b, b, _1285);
}

float3 TransformNormal(float3 n, float4x4 inv_xform)
{
    return mul(float4(n, 0.0f), transpose(inv_xform)).xyz;
}

int hash(int x)
{
    uint _362 = uint(x);
    uint _369 = ((_362 >> uint(16)) ^ _362) * 73244475u;
    uint _374 = ((_369 >> uint(16)) ^ _369) * 73244475u;
    return int((_374 >> uint(16)) ^ _374);
}

float construct_float(inout uint m)
{
    m &= 8388607u;
    m |= 1065353216u;
    return asfloat(m) - 1.0f;
}

float3 YCoCg_to_RGB(float4 col)
{
    float _461 = mad(col.z, 31.875f, 1.0f);
    float _471 = (col.x - 0.501960813999176025390625f) / _461;
    float _477 = (col.y - 0.501960813999176025390625f) / _461;
    return float3((col.w + _471) - _477, col.w + _477, (col.w - _471) - _477);
}

float3 srgb_to_rgb(float3 col)
{
    float3 ret;
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        [flatten]
        if (col[i] > 0.040449999272823333740234375f)
        {
            ret[i] = pow((col[i] + 0.054999999701976776123046875f) * 0.947867333889007568359375f, 2.400000095367431640625f);
        }
        else
        {
            ret[i] = col[i] * 0.077399380505084991455078125f;
        }
    }
    return ret;
}

float4 SampleBilinear(uint index, float2 uvs, int lod, bool maybe_YCoCg, bool maybe_SRGB)
{
    atlas_texture_t _985;
    _985.size = _855.Load(index * 80 + 0);
    _985.atlas = _855.Load(index * 80 + 4);
    [unroll]
    for (int _61ident = 0; _61ident < 4; _61ident++)
    {
        _985.page[_61ident] = _855.Load(_61ident * 4 + index * 80 + 8);
    }
    [unroll]
    for (int _62ident = 0; _62ident < 14; _62ident++)
    {
        _985.pos[_62ident] = _855.Load(_62ident * 4 + index * 80 + 24);
    }
    uint _9037[4];
    _9037[0] = _985.page[0];
    _9037[1] = _985.page[1];
    _9037[2] = _985.page[2];
    _9037[3] = _985.page[3];
    uint _9073[14] = { _985.pos[0], _985.pos[1], _985.pos[2], _985.pos[3], _985.pos[4], _985.pos[5], _985.pos[6], _985.pos[7], _985.pos[8], _985.pos[9], _985.pos[10], _985.pos[11], _985.pos[12], _985.pos[13] };
    atlas_texture_t _9043 = { _985.size, _985.atlas, _9037, _9073 };
    uint _1055 = _985.atlas;
    float4 res = g_atlases[NonUniformResourceIndex(_1055)].SampleLevel(_g_atlases_sampler[NonUniformResourceIndex(_1055)], float3(TransformUV(uvs, _9043, lod) * 0.000118371215648949146270751953125f.xx, float((_9037[lod / 4] >> uint((lod % 4) * 8)) & 255u)), 0.0f);
    bool _1070;
    if (maybe_YCoCg)
    {
        _1070 = _985.atlas == 4u;
    }
    else
    {
        _1070 = maybe_YCoCg;
    }
    if (_1070)
    {
        float4 param = res;
        res = float4(YCoCg_to_RGB(param), 1.0f);
    }
    bool _1089;
    if (maybe_SRGB)
    {
        _1089 = (_985.size & 32768u) != 0u;
    }
    else
    {
        _1089 = maybe_SRGB;
    }
    if (_1089)
    {
        float3 param_1 = res.xyz;
        float3 _1095 = srgb_to_rgb(param_1);
        float4 _10058 = res;
        _10058.x = _1095.x;
        float4 _10060 = _10058;
        _10060.y = _1095.y;
        float4 _10062 = _10060;
        _10062.z = _1095.z;
        res = _10062;
    }
    return res;
}

float4 SampleBilinear(uint index, float2 uvs, int lod)
{
    return SampleBilinear(index, uvs, lod, false, false);
}

float fresnel_dielectric_cos(float cosi, float eta)
{
    float _1317 = abs(cosi);
    float _1326 = mad(_1317, _1317, mad(eta, eta, -1.0f));
    float g = _1326;
    float result;
    if (_1326 > 0.0f)
    {
        float _1331 = g;
        float _1332 = sqrt(_1331);
        g = _1332;
        float _1336 = _1332 - _1317;
        float _1339 = _1332 + _1317;
        float _1340 = _1336 / _1339;
        float _1354 = mad(_1317, _1339, -1.0f) / mad(_1317, _1336, 1.0f);
        result = ((0.5f * _1340) * _1340) * mad(_1354, _1354, 1.0f);
    }
    else
    {
        result = 1.0f;
    }
    return result;
}

float safe_sqrtf(float f)
{
    return sqrt(max(f, 0.0f));
}

float3 ensure_valid_reflection(float3 Ng, float3 I, float3 N)
{
    float3 _8910;
    do
    {
        float _1390 = min(0.89999997615814208984375f * dot(Ng, I), 0.00999999977648258209228515625f);
        if (dot(Ng, (N * (2.0f * dot(N, I))) - I) >= _1390)
        {
            _8910 = N;
            break;
        }
        float3 _1410 = normalize(N - (Ng * dot(N, Ng)));
        float _1414 = dot(I, _1410);
        float _1418 = dot(I, Ng);
        float _1430 = mad(_1414, _1414, _1418 * _1418);
        float param = (_1414 * _1414) * mad(-_1390, _1390, _1430);
        float _1440 = safe_sqrtf(param);
        float _1446 = mad(_1418, _1390, _1430);
        float _1449 = 0.5f / _1430;
        float _1454 = _1440 + _1446;
        float _1455 = _1449 * _1454;
        float _1461 = (-_1440) + _1446;
        float _1462 = _1449 * _1461;
        bool _1470 = (_1455 > 9.9999997473787516355514526367188e-06f) && (_1455 <= 1.000010013580322265625f);
        bool valid1 = _1470;
        bool _1476 = (_1462 > 9.9999997473787516355514526367188e-06f) && (_1462 <= 1.000010013580322265625f);
        bool valid2 = _1476;
        float2 N_new;
        if (_1470 && _1476)
        {
            float _10366 = (-0.5f) / _1430;
            float param_1 = mad(_10366, _1454, 1.0f);
            float _1486 = safe_sqrtf(param_1);
            float param_2 = _1455;
            float _1489 = safe_sqrtf(param_2);
            float2 _1490 = float2(_1486, _1489);
            float param_3 = mad(_10366, _1461, 1.0f);
            float _1495 = safe_sqrtf(param_3);
            float param_4 = _1462;
            float _1498 = safe_sqrtf(param_4);
            float2 _1499 = float2(_1495, _1498);
            float _10368 = -_1418;
            float _1515 = mad(2.0f * mad(_1486, _1414, _1489 * _1418), _1489, _10368);
            float _1531 = mad(2.0f * mad(_1495, _1414, _1498 * _1418), _1498, _10368);
            bool _1533 = _1515 >= 9.9999997473787516355514526367188e-06f;
            valid1 = _1533;
            bool _1535 = _1531 >= 9.9999997473787516355514526367188e-06f;
            valid2 = _1535;
            if (_1533 && _1535)
            {
                bool2 _1548 = (_1515 < _1531).xx;
                N_new = float2(_1548.x ? _1490.x : _1499.x, _1548.y ? _1490.y : _1499.y);
            }
            else
            {
                bool2 _1556 = (_1515 > _1531).xx;
                N_new = float2(_1556.x ? _1490.x : _1499.x, _1556.y ? _1490.y : _1499.y);
            }
        }
        else
        {
            if (!(valid1 || valid2))
            {
                _8910 = Ng;
                break;
            }
            float _1568 = valid1 ? _1455 : _1462;
            float param_5 = 1.0f - _1568;
            float param_6 = _1568;
            N_new = float2(safe_sqrtf(param_5), safe_sqrtf(param_6));
        }
        _8910 = (_1410 * N_new.x) + (Ng * N_new.y);
        break;
    } while(false);
    return _8910;
}

float3 rotate_around_axis(float3 p, float3 axis, float angle)
{
    float _1641 = cos(angle);
    float _1644 = sin(angle);
    float _1648 = 1.0f - _1641;
    return float3(mad(mad(_1648 * axis.x, axis.z, axis.y * _1644), p.z, mad(mad(_1648 * axis.x, axis.x, _1641), p.x, mad(_1648 * axis.x, axis.y, -(axis.z * _1644)) * p.y)), mad(mad(_1648 * axis.y, axis.z, -(axis.x * _1644)), p.z, mad(mad(_1648 * axis.x, axis.y, axis.z * _1644), p.x, mad(_1648 * axis.y, axis.y, _1641) * p.y)), mad(mad(_1648 * axis.z, axis.z, _1641), p.z, mad(mad(_1648 * axis.x, axis.z, -(axis.y * _1644)), p.x, mad(_1648 * axis.y, axis.z, axis.x * _1644) * p.y)));
}

void create_tbn(float3 N, inout float3 out_T, out float3 out_B)
{
    float3 U;
    [flatten]
    if (abs(N.y) < 0.999000012874603271484375f)
    {
        U = float3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        U = float3(1.0f, 0.0f, 0.0f);
    }
    out_T = normalize(cross(U, N));
    out_B = cross(N, out_T);
}

float3 MapToCone(float r1, float r2, float3 N, float radius)
{
    float3 _8935;
    do
    {
        float2 _3134 = (float2(r1, r2) * 2.0f) - 1.0f.xx;
        float _3136 = _3134.x;
        bool _3137 = _3136 == 0.0f;
        bool _3143;
        if (_3137)
        {
            _3143 = _3134.y == 0.0f;
        }
        else
        {
            _3143 = _3137;
        }
        if (_3143)
        {
            _8935 = N;
            break;
        }
        float _3152 = _3134.y;
        float r;
        float theta;
        if (abs(_3136) > abs(_3152))
        {
            r = _3136;
            theta = 0.785398185253143310546875f * (_3152 / _3136);
        }
        else
        {
            r = _3152;
            theta = 1.57079637050628662109375f * mad(-0.5f, _3136 / _3152, 1.0f);
        }
        float3 param;
        float3 param_1;
        create_tbn(N, param, param_1);
        _8935 = (N + (param * ((radius * r) * cos(theta)))) + (param_1 * ((radius * r) * sin(theta)));
        break;
    } while(false);
    return _8935;
}

float3 CanonicalToDir(float2 p, float y_rotation)
{
    float _535 = mad(2.0f, p.x, -1.0f);
    float _540 = mad(6.283185482025146484375f, p.y, y_rotation);
    float phi = _540;
    if (_540 < 0.0f)
    {
        phi += 6.283185482025146484375f;
    }
    if (phi > 6.283185482025146484375f)
    {
        phi -= 6.283185482025146484375f;
    }
    float _558 = sqrt(mad(-_535, _535, 1.0f));
    return float3(_558 * cos(phi), _535, (-_558) * sin(phi));
}

float4 Sample_EnvQTree(float y_rotation, Texture2D<float4> qtree_tex, SamplerState _qtree_tex_sampler, int qtree_levels, float rand, float rx, float ry)
{
    int res = 2;
    float _step = 0.5f;
    float _sample = rand;
    int lod = qtree_levels - 1;
    float2 origin = 0.0f.xx;
    float factor = 1.0f;
    while (lod >= 0)
    {
        float4 quad = qtree_tex.Load(int3(int2(origin * float(res)) / int2(2, 2), lod));
        float _732 = quad.x + quad.z;
        float partial = _732;
        float _739 = (_732 + quad.y) + quad.w;
        if (_739 <= 0.0f)
        {
            break;
        }
        float _748 = partial / _739;
        float boundary = _748;
        int index = 0;
        if (_sample < _748)
        {
            _sample /= boundary;
            boundary = quad.x / partial;
        }
        else
        {
            float _763 = partial;
            float _764 = _739 - _763;
            partial = _764;
            float2 _10045 = origin;
            _10045.x = origin.x + _step;
            origin = _10045;
            _sample = (_sample - boundary) / (1.0f - boundary);
            boundary = quad.y / _764;
            index |= 1;
        }
        if (_sample < boundary)
        {
            _sample /= boundary;
        }
        else
        {
            float2 _10048 = origin;
            _10048.y = origin.y + _step;
            origin = _10048;
            _sample = (_sample - boundary) / (1.0f - boundary);
            index |= 2;
        }
        factor *= ((4.0f * quad[index]) / _739);
        lod--;
        res *= 2;
        _step *= 0.5f;
    }
    float2 _821 = origin;
    float2 _822 = _821 + (float2(rx, ry) * (2.0f * _step));
    origin = _822;
    return float4(CanonicalToDir(_822, y_rotation), factor * 0.079577468335628509521484375f);
}

void SampleLightSource(float3 P, float2 sample_off, inout light_sample_t ls)
{
    float _3235 = frac(asfloat(_3219.Load((_3224_g_params.hi + 3) * 4 + 0)) + sample_off.x);
    float _3240 = float(_3224_g_params.li_count);
    uint _3247 = min(uint(_3235 * _3240), uint(_3224_g_params.li_count - 1));
    light_t _3267;
    _3267.type_and_param0 = _3256.Load4(_3260.Load(_3247 * 4 + 0) * 64 + 0);
    _3267.param1 = asfloat(_3256.Load4(_3260.Load(_3247 * 4 + 0) * 64 + 16));
    _3267.param2 = asfloat(_3256.Load4(_3260.Load(_3247 * 4 + 0) * 64 + 32));
    _3267.param3 = asfloat(_3256.Load4(_3260.Load(_3247 * 4 + 0) * 64 + 48));
    ls.col = asfloat(_3267.type_and_param0.yzw);
    ls.col *= _3240;
    ls.cast_shadow = float((_3267.type_and_param0.x & 32u) != 0u);
    uint _3301 = _3267.type_and_param0.x & 31u;
    [branch]
    if (_3301 == 0u)
    {
        float _3315 = frac(asfloat(_3219.Load((_3224_g_params.hi + 4) * 4 + 0)) + sample_off.x);
        float3 _3331 = P - _3267.param1.xyz;
        float3 _3338 = _3331 / length(_3331).xxx;
        float _3345 = sqrt(clamp(mad(-_3315, _3315, 1.0f), 0.0f, 1.0f));
        float _3348 = 6.283185482025146484375f * frac(asfloat(_3219.Load((_3224_g_params.hi + 5) * 4 + 0)) + sample_off.y);
        float3 sampled_dir = float3(_3345 * cos(_3348), _3345 * sin(_3348), _3315);
        float3 param;
        float3 param_1;
        create_tbn(_3338, param, param_1);
        float3 _10125 = sampled_dir;
        float3 _3381 = ((param * _10125.x) + (param_1 * _10125.y)) + (_3338 * _10125.z);
        sampled_dir = _3381;
        float3 _3390 = _3267.param1.xyz + (_3381 * _3267.param2.w);
        ls.L = _3390 - P;
        ls.dist = length(ls.L);
        ls.L /= ls.dist.xxx;
        ls.area = _3267.param1.w;
        float _3421 = abs(dot(ls.L, normalize(_3390 - _3267.param1.xyz)));
        [flatten]
        if (_3421 > 0.0f)
        {
            ls.pdf = (ls.dist * ls.dist) / ((0.5f * ls.area) * _3421);
        }
        [branch]
        if (_3267.param3.x > 0.0f)
        {
            float _3450 = -dot(ls.L, _3267.param2.xyz);
            if (_3450 > 0.0f)
            {
                ls.col *= clamp((_3267.param3.x - acos(clamp(_3450, 0.0f, 1.0f))) / _3267.param3.y, 0.0f, 1.0f);
            }
            else
            {
                ls.col = 0.0f.xxx;
            }
        }
    }
    else
    {
        [branch]
        if (_3301 == 2u)
        {
            ls.L = _3267.param1.xyz;
            if (_3267.param1.w != 0.0f)
            {
                float param_2 = frac(asfloat(_3219.Load((_3224_g_params.hi + 4) * 4 + 0)) + sample_off.x);
                float param_3 = frac(asfloat(_3219.Load((_3224_g_params.hi + 5) * 4 + 0)) + sample_off.y);
                float3 param_4 = ls.L;
                float param_5 = tan(_3267.param1.w);
                ls.L = normalize(MapToCone(param_2, param_3, param_4, param_5));
            }
            ls.area = 0.0f;
            ls.dist = 3402823346297367662189621542912.0f;
            ls.pdf = 1.0f;
            if ((_3267.type_and_param0.x & 64u) == 0u)
            {
                ls.area = 0.0f;
            }
        }
        else
        {
            [branch]
            if (_3301 == 4u)
            {
                float3 _3590 = ((_3267.param1.xyz + (_3267.param2.xyz * (frac(asfloat(_3219.Load((_3224_g_params.hi + 4) * 4 + 0)) + sample_off.x) - 0.5f))) + (_3267.param3.xyz * (frac(asfloat(_3219.Load((_3224_g_params.hi + 5) * 4 + 0)) + sample_off.y) - 0.5f))) - P;
                ls.dist = length(_3590);
                ls.L = _3590 / ls.dist.xxx;
                ls.area = _3267.param1.w;
                float _3613 = dot(-ls.L, normalize(cross(_3267.param2.xyz, _3267.param3.xyz)));
                if (_3613 > 0.0f)
                {
                    ls.pdf = (ls.dist * ls.dist) / (ls.area * _3613);
                }
                if ((_3267.type_and_param0.x & 64u) == 0u)
                {
                    ls.area = 0.0f;
                }
                [branch]
                if ((_3267.type_and_param0.w & 128u) != 0u)
                {
                    float3 env_col = _3224_g_params.env_col.xyz;
                    uint _3652 = asuint(_3224_g_params.env_col.w);
                    if (_3652 != 4294967295u)
                    {
                        atlas_texture_t _3660;
                        _3660.size = _855.Load(_3652 * 80 + 0);
                        _3660.atlas = _855.Load(_3652 * 80 + 4);
                        [unroll]
                        for (int _63ident = 0; _63ident < 4; _63ident++)
                        {
                            _3660.page[_63ident] = _855.Load(_63ident * 4 + _3652 * 80 + 8);
                        }
                        [unroll]
                        for (int _64ident = 0; _64ident < 14; _64ident++)
                        {
                            _3660.pos[_64ident] = _855.Load(_64ident * 4 + _3652 * 80 + 24);
                        }
                        uint _9228[14] = { _3660.pos[0], _3660.pos[1], _3660.pos[2], _3660.pos[3], _3660.pos[4], _3660.pos[5], _3660.pos[6], _3660.pos[7], _3660.pos[8], _3660.pos[9], _3660.pos[10], _3660.pos[11], _3660.pos[12], _3660.pos[13] };
                        uint _9199[4] = { _3660.page[0], _3660.page[1], _3660.page[2], _3660.page[3] };
                        atlas_texture_t _9109 = { _3660.size, _3660.atlas, _9199, _9228 };
                        float param_6 = _3224_g_params.env_rotation;
                        env_col *= SampleLatlong_RGBE(_9109, ls.L, param_6);
                    }
                    ls.col *= env_col;
                }
            }
            else
            {
                [branch]
                if (_3301 == 5u)
                {
                    float2 _3763 = (float2(frac(asfloat(_3219.Load((_3224_g_params.hi + 4) * 4 + 0)) + sample_off.x), frac(asfloat(_3219.Load((_3224_g_params.hi + 5) * 4 + 0)) + sample_off.y)) * 2.0f) - 1.0f.xx;
                    float2 offset = _3763;
                    bool _3766 = _3763.x != 0.0f;
                    bool _3772;
                    if (_3766)
                    {
                        _3772 = offset.y != 0.0f;
                    }
                    else
                    {
                        _3772 = _3766;
                    }
                    if (_3772)
                    {
                        float r;
                        float theta;
                        if (abs(offset.x) > abs(offset.y))
                        {
                            r = offset.x;
                            theta = 0.785398185253143310546875f * (offset.y / offset.x);
                        }
                        else
                        {
                            r = offset.y;
                            theta = mad(-0.785398185253143310546875f, offset.x / offset.y, 1.57079637050628662109375f);
                        }
                        float _3805 = 0.5f * r;
                        offset = float2(_3805 * cos(theta), _3805 * sin(theta));
                    }
                    float3 _3831 = ((_3267.param1.xyz + (_3267.param2.xyz * offset.x)) + (_3267.param3.xyz * offset.y)) - P;
                    ls.dist = length(_3831);
                    ls.L = _3831 / ls.dist.xxx;
                    ls.area = _3267.param1.w;
                    float _3854 = dot(-ls.L, normalize(cross(_3267.param2.xyz, _3267.param3.xyz)));
                    [flatten]
                    if (_3854 > 0.0f)
                    {
                        ls.pdf = (ls.dist * ls.dist) / (ls.area * _3854);
                    }
                    if ((_3267.type_and_param0.x & 64u) == 0u)
                    {
                        ls.area = 0.0f;
                    }
                    [branch]
                    if ((_3267.type_and_param0.w & 128u) != 0u)
                    {
                        float3 env_col_1 = _3224_g_params.env_col.xyz;
                        uint _3890 = asuint(_3224_g_params.env_col.w);
                        if (_3890 != 4294967295u)
                        {
                            atlas_texture_t _3897;
                            _3897.size = _855.Load(_3890 * 80 + 0);
                            _3897.atlas = _855.Load(_3890 * 80 + 4);
                            [unroll]
                            for (int _65ident = 0; _65ident < 4; _65ident++)
                            {
                                _3897.page[_65ident] = _855.Load(_65ident * 4 + _3890 * 80 + 8);
                            }
                            [unroll]
                            for (int _66ident = 0; _66ident < 14; _66ident++)
                            {
                                _3897.pos[_66ident] = _855.Load(_66ident * 4 + _3890 * 80 + 24);
                            }
                            uint _9266[14] = { _3897.pos[0], _3897.pos[1], _3897.pos[2], _3897.pos[3], _3897.pos[4], _3897.pos[5], _3897.pos[6], _3897.pos[7], _3897.pos[8], _3897.pos[9], _3897.pos[10], _3897.pos[11], _3897.pos[12], _3897.pos[13] };
                            uint _9237[4] = { _3897.page[0], _3897.page[1], _3897.page[2], _3897.page[3] };
                            atlas_texture_t _9118 = { _3897.size, _3897.atlas, _9237, _9266 };
                            float param_7 = _3224_g_params.env_rotation;
                            env_col_1 *= SampleLatlong_RGBE(_9118, ls.L, param_7);
                        }
                        ls.col *= env_col_1;
                    }
                }
                else
                {
                    [branch]
                    if (_3301 == 3u)
                    {
                        float3 _3998 = normalize(cross(P - _3267.param1.xyz, _3267.param3.xyz));
                        float _4005 = 3.1415927410125732421875f * frac(asfloat(_3219.Load((_3224_g_params.hi + 4) * 4 + 0)) + sample_off.x);
                        float3 _4034 = ((_3267.param1.xyz + (((_3998 * cos(_4005)) + (cross(_3998, _3267.param3.xyz) * sin(_4005))) * _3267.param2.w)) + ((_3267.param3.xyz * (frac(asfloat(_3219.Load((_3224_g_params.hi + 5) * 4 + 0)) + sample_off.y) - 0.5f)) * _3267.param3.w)) - P;
                        ls.dist = length(_4034);
                        ls.L = _4034 / ls.dist.xxx;
                        ls.area = _3267.param1.w;
                        float _4053 = 1.0f - abs(dot(ls.L, _3267.param3.xyz));
                        [flatten]
                        if (_4053 != 0.0f)
                        {
                            ls.pdf = (ls.dist * ls.dist) / (ls.area * _4053);
                        }
                        if ((_3267.type_and_param0.x & 64u) == 0u)
                        {
                            ls.area = 0.0f;
                        }
                        [branch]
                        if ((_3267.type_and_param0.w & 128u) != 0u)
                        {
                            float3 env_col_2 = _3224_g_params.env_col.xyz;
                            uint _4089 = asuint(_3224_g_params.env_col.w);
                            if (_4089 != 4294967295u)
                            {
                                atlas_texture_t _4096;
                                _4096.size = _855.Load(_4089 * 80 + 0);
                                _4096.atlas = _855.Load(_4089 * 80 + 4);
                                [unroll]
                                for (int _67ident = 0; _67ident < 4; _67ident++)
                                {
                                    _4096.page[_67ident] = _855.Load(_67ident * 4 + _4089 * 80 + 8);
                                }
                                [unroll]
                                for (int _68ident = 0; _68ident < 14; _68ident++)
                                {
                                    _4096.pos[_68ident] = _855.Load(_68ident * 4 + _4089 * 80 + 24);
                                }
                                uint _9304[14] = { _4096.pos[0], _4096.pos[1], _4096.pos[2], _4096.pos[3], _4096.pos[4], _4096.pos[5], _4096.pos[6], _4096.pos[7], _4096.pos[8], _4096.pos[9], _4096.pos[10], _4096.pos[11], _4096.pos[12], _4096.pos[13] };
                                uint _9275[4] = { _4096.page[0], _4096.page[1], _4096.page[2], _4096.page[3] };
                                atlas_texture_t _9127 = { _4096.size, _4096.atlas, _9275, _9304 };
                                float param_8 = _3224_g_params.env_rotation;
                                env_col_2 *= SampleLatlong_RGBE(_9127, ls.L, param_8);
                            }
                            ls.col *= env_col_2;
                        }
                    }
                    else
                    {
                        [branch]
                        if (_3301 == 6u)
                        {
                            uint _4165 = asuint(_3267.param1.x);
                            transform_t _4179;
                            _4179.xform = asfloat(uint4x4(_4173.Load4(asuint(_3267.param1.y) * 128 + 0), _4173.Load4(asuint(_3267.param1.y) * 128 + 16), _4173.Load4(asuint(_3267.param1.y) * 128 + 32), _4173.Load4(asuint(_3267.param1.y) * 128 + 48)));
                            _4179.inv_xform = asfloat(uint4x4(_4173.Load4(asuint(_3267.param1.y) * 128 + 64), _4173.Load4(asuint(_3267.param1.y) * 128 + 80), _4173.Load4(asuint(_3267.param1.y) * 128 + 96), _4173.Load4(asuint(_3267.param1.y) * 128 + 112)));
                            uint _4204 = _4165 * 3u;
                            vertex_t _4210;
                            [unroll]
                            for (int _69ident = 0; _69ident < 3; _69ident++)
                            {
                                _4210.p[_69ident] = asfloat(_4198.Load(_69ident * 4 + _4202.Load(_4204 * 4 + 0) * 52 + 0));
                            }
                            [unroll]
                            for (int _70ident = 0; _70ident < 3; _70ident++)
                            {
                                _4210.n[_70ident] = asfloat(_4198.Load(_70ident * 4 + _4202.Load(_4204 * 4 + 0) * 52 + 12));
                            }
                            [unroll]
                            for (int _71ident = 0; _71ident < 3; _71ident++)
                            {
                                _4210.b[_71ident] = asfloat(_4198.Load(_71ident * 4 + _4202.Load(_4204 * 4 + 0) * 52 + 24));
                            }
                            [unroll]
                            for (int _72ident = 0; _72ident < 2; _72ident++)
                            {
                                [unroll]
                                for (int _73ident = 0; _73ident < 2; _73ident++)
                                {
                                    _4210.t[_72ident][_73ident] = asfloat(_4198.Load(_73ident * 4 + _72ident * 8 + _4202.Load(_4204 * 4 + 0) * 52 + 36));
                                }
                            }
                            vertex_t _4259;
                            [unroll]
                            for (int _74ident = 0; _74ident < 3; _74ident++)
                            {
                                _4259.p[_74ident] = asfloat(_4198.Load(_74ident * 4 + _4202.Load((_4204 + 1u) * 4 + 0) * 52 + 0));
                            }
                            [unroll]
                            for (int _75ident = 0; _75ident < 3; _75ident++)
                            {
                                _4259.n[_75ident] = asfloat(_4198.Load(_75ident * 4 + _4202.Load((_4204 + 1u) * 4 + 0) * 52 + 12));
                            }
                            [unroll]
                            for (int _76ident = 0; _76ident < 3; _76ident++)
                            {
                                _4259.b[_76ident] = asfloat(_4198.Load(_76ident * 4 + _4202.Load((_4204 + 1u) * 4 + 0) * 52 + 24));
                            }
                            [unroll]
                            for (int _77ident = 0; _77ident < 2; _77ident++)
                            {
                                [unroll]
                                for (int _78ident = 0; _78ident < 2; _78ident++)
                                {
                                    _4259.t[_77ident][_78ident] = asfloat(_4198.Load(_78ident * 4 + _77ident * 8 + _4202.Load((_4204 + 1u) * 4 + 0) * 52 + 36));
                                }
                            }
                            vertex_t _4305;
                            [unroll]
                            for (int _79ident = 0; _79ident < 3; _79ident++)
                            {
                                _4305.p[_79ident] = asfloat(_4198.Load(_79ident * 4 + _4202.Load((_4204 + 2u) * 4 + 0) * 52 + 0));
                            }
                            [unroll]
                            for (int _80ident = 0; _80ident < 3; _80ident++)
                            {
                                _4305.n[_80ident] = asfloat(_4198.Load(_80ident * 4 + _4202.Load((_4204 + 2u) * 4 + 0) * 52 + 12));
                            }
                            [unroll]
                            for (int _81ident = 0; _81ident < 3; _81ident++)
                            {
                                _4305.b[_81ident] = asfloat(_4198.Load(_81ident * 4 + _4202.Load((_4204 + 2u) * 4 + 0) * 52 + 24));
                            }
                            [unroll]
                            for (int _82ident = 0; _82ident < 2; _82ident++)
                            {
                                [unroll]
                                for (int _83ident = 0; _83ident < 2; _83ident++)
                                {
                                    _4305.t[_82ident][_83ident] = asfloat(_4198.Load(_83ident * 4 + _82ident * 8 + _4202.Load((_4204 + 2u) * 4 + 0) * 52 + 36));
                                }
                            }
                            float3 _4351 = float3(_4210.p[0], _4210.p[1], _4210.p[2]);
                            float3 _4359 = float3(_4259.p[0], _4259.p[1], _4259.p[2]);
                            float3 _4367 = float3(_4305.p[0], _4305.p[1], _4305.p[2]);
                            float _4396 = sqrt(frac(asfloat(_3219.Load((_3224_g_params.hi + 4) * 4 + 0)) + sample_off.x));
                            float _4406 = frac(asfloat(_3219.Load((_3224_g_params.hi + 5) * 4 + 0)) + sample_off.y);
                            float _4410 = 1.0f - _4396;
                            float _4415 = 1.0f - _4406;
                            float3 _4462 = mul(float4(cross(_4359 - _4351, _4367 - _4351), 0.0f), _4179.xform).xyz;
                            ls.area = 0.5f * length(_4462);
                            float3 _4472 = mul(float4((_4351 * _4410) + (((_4359 * _4415) + (_4367 * _4406)) * _4396), 1.0f), _4179.xform).xyz - P;
                            ls.dist = length(_4472);
                            ls.L = _4472 / ls.dist.xxx;
                            float _4487 = abs(dot(ls.L, normalize(_4462)));
                            [flatten]
                            if (_4487 > 0.0f)
                            {
                                ls.pdf = (ls.dist * ls.dist) / (ls.area * _4487);
                            }
                            material_t _4526;
                            [unroll]
                            for (int _84ident = 0; _84ident < 5; _84ident++)
                            {
                                _4526.textures[_84ident] = _4513.Load(_84ident * 4 + ((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 0);
                            }
                            [unroll]
                            for (int _85ident = 0; _85ident < 3; _85ident++)
                            {
                                _4526.base_color[_85ident] = asfloat(_4513.Load(_85ident * 4 + ((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 20));
                            }
                            _4526.flags = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 32);
                            _4526.type = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 36);
                            _4526.tangent_rotation_or_strength = asfloat(_4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 40));
                            _4526.roughness_and_anisotropic = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 44);
                            _4526.int_ior = asfloat(_4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 48));
                            _4526.ext_ior = asfloat(_4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 52));
                            _4526.sheen_and_sheen_tint = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 56);
                            _4526.tint_and_metallic = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 60);
                            _4526.transmission_and_transmission_roughness = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 64);
                            _4526.specular_and_specular_tint = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 68);
                            _4526.clearcoat_and_clearcoat_roughness = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 72);
                            _4526.normal_map_strength_unorm = _4513.Load(((_4517.Load(_4165 * 4 + 0) >> 16u) & 16383u) * 80 + 76);
                            if ((_4526.flags & 4u) == 0u)
                            {
                                if (_4526.textures[1] != 4294967295u)
                                {
                                    ls.col *= SampleBilinear(_4526.textures[1], (float2(_4210.t[0][0], _4210.t[0][1]) * _4410) + (((float2(_4259.t[0][0], _4259.t[0][1]) * _4415) + (float2(_4305.t[0][0], _4305.t[0][1]) * _4406)) * _4396), 0).xyz;
                                }
                            }
                            else
                            {
                                float3 env_col_3 = _3224_g_params.env_col.xyz;
                                uint _4600 = asuint(_3224_g_params.env_col.w);
                                if (_4600 != 4294967295u)
                                {
                                    atlas_texture_t _4607;
                                    _4607.size = _855.Load(_4600 * 80 + 0);
                                    _4607.atlas = _855.Load(_4600 * 80 + 4);
                                    [unroll]
                                    for (int _86ident = 0; _86ident < 4; _86ident++)
                                    {
                                        _4607.page[_86ident] = _855.Load(_86ident * 4 + _4600 * 80 + 8);
                                    }
                                    [unroll]
                                    for (int _87ident = 0; _87ident < 14; _87ident++)
                                    {
                                        _4607.pos[_87ident] = _855.Load(_87ident * 4 + _4600 * 80 + 24);
                                    }
                                    uint _9389[14] = { _4607.pos[0], _4607.pos[1], _4607.pos[2], _4607.pos[3], _4607.pos[4], _4607.pos[5], _4607.pos[6], _4607.pos[7], _4607.pos[8], _4607.pos[9], _4607.pos[10], _4607.pos[11], _4607.pos[12], _4607.pos[13] };
                                    uint _9360[4] = { _4607.page[0], _4607.page[1], _4607.page[2], _4607.page[3] };
                                    atlas_texture_t _9181 = { _4607.size, _4607.atlas, _9360, _9389 };
                                    float param_9 = _3224_g_params.env_rotation;
                                    env_col_3 *= SampleLatlong_RGBE(_9181, ls.L, param_9);
                                }
                                ls.col *= env_col_3;
                            }
                        }
                        else
                        {
                            [branch]
                            if (_3301 == 7u)
                            {
                                float4 _4710 = Sample_EnvQTree(_3224_g_params.env_rotation, g_env_qtree, _g_env_qtree_sampler, _3224_g_params.env_qtree_levels, mad(_3235, _3240, -float(_3247)), frac(asfloat(_3219.Load((_3224_g_params.hi + 4) * 4 + 0)) + sample_off.x), frac(asfloat(_3219.Load((_3224_g_params.hi + 5) * 4 + 0)) + sample_off.y));
                                ls.L = _4710.xyz;
                                ls.col *= _3224_g_params.env_col.xyz;
                                atlas_texture_t _4727;
                                _4727.size = _855.Load(asuint(_3224_g_params.env_col.w) * 80 + 0);
                                _4727.atlas = _855.Load(asuint(_3224_g_params.env_col.w) * 80 + 4);
                                [unroll]
                                for (int _88ident = 0; _88ident < 4; _88ident++)
                                {
                                    _4727.page[_88ident] = _855.Load(_88ident * 4 + asuint(_3224_g_params.env_col.w) * 80 + 8);
                                }
                                [unroll]
                                for (int _89ident = 0; _89ident < 14; _89ident++)
                                {
                                    _4727.pos[_89ident] = _855.Load(_89ident * 4 + asuint(_3224_g_params.env_col.w) * 80 + 24);
                                }
                                uint _9427[14] = { _4727.pos[0], _4727.pos[1], _4727.pos[2], _4727.pos[3], _4727.pos[4], _4727.pos[5], _4727.pos[6], _4727.pos[7], _4727.pos[8], _4727.pos[9], _4727.pos[10], _4727.pos[11], _4727.pos[12], _4727.pos[13] };
                                uint _9398[4] = { _4727.page[0], _4727.page[1], _4727.page[2], _4727.page[3] };
                                atlas_texture_t _9190 = { _4727.size, _4727.atlas, _9398, _9427 };
                                float param_10 = _3224_g_params.env_rotation;
                                ls.col *= SampleLatlong_RGBE(_9190, ls.L, param_10);
                                ls.area = 1.0f;
                                ls.dist = 3402823346297367662189621542912.0f;
                                ls.pdf = _4710.w;
                            }
                        }
                    }
                }
            }
        }
    }
}

int2 texSize(uint index)
{
    atlas_texture_t _858;
    _858.size = _855.Load(index * 80 + 0);
    _858.atlas = _855.Load(index * 80 + 4);
    [unroll]
    for (int _90ident = 0; _90ident < 4; _90ident++)
    {
        _858.page[_90ident] = _855.Load(_90ident * 4 + index * 80 + 8);
    }
    [unroll]
    for (int _91ident = 0; _91ident < 14; _91ident++)
    {
        _858.pos[_91ident] = _855.Load(_91ident * 4 + index * 80 + 24);
    }
    return int2(int(_858.size & 16383u), int((_858.size >> uint(16)) & 16383u));
}

float get_texture_lod(int2 res, float lambda)
{
    return clamp(mad(0.5f, log2(float(res.x) * float(res.y)), lambda) - 1.0f, 0.0f, 13.0f);
}

float lum(float3 color)
{
    return mad(0.072168998420238494873046875f, color.z, mad(0.21267099678516387939453125f, color.x, 0.71516001224517822265625f * color.y));
}

float4 Evaluate_OrenDiffuse_BSDF(float3 V, float3 N, float3 L, float roughness, float3 base_color)
{
    float _2215 = 1.0f / mad(0.904129683971405029296875f, roughness, 3.1415927410125732421875f);
    float _2227 = max(dot(N, L), 0.0f);
    float _2232 = max(dot(N, V), 0.0f);
    float _2240 = mad(-_2227, _2232, dot(L, V));
    float t = _2240;
    if (_2240 > 0.0f)
    {
        t /= (max(_2227, _2232) + 1.1754943508222875079687365372222e-38f);
    }
    return float4(base_color * (_2227 * mad(roughness * _2215, t, _2215)), 0.15915493667125701904296875f);
}

float3 offset_ray(float3 p, float3 n)
{
    int3 _1797 = int3(n * 128.0f);
    int _1805;
    if (p.x < 0.0f)
    {
        _1805 = -_1797.x;
    }
    else
    {
        _1805 = _1797.x;
    }
    int _1823;
    if (p.y < 0.0f)
    {
        _1823 = -_1797.y;
    }
    else
    {
        _1823 = _1797.y;
    }
    int _1841;
    if (p.z < 0.0f)
    {
        _1841 = -_1797.z;
    }
    else
    {
        _1841 = _1797.z;
    }
    float _1859;
    if (abs(p.x) < 0.03125f)
    {
        _1859 = mad(1.52587890625e-05f, n.x, p.x);
    }
    else
    {
        _1859 = asfloat(asint(p.x) + _1805);
    }
    float _1877;
    if (abs(p.y) < 0.03125f)
    {
        _1877 = mad(1.52587890625e-05f, n.y, p.y);
    }
    else
    {
        _1877 = asfloat(asint(p.y) + _1823);
    }
    float _1894;
    if (abs(p.z) < 0.03125f)
    {
        _1894 = mad(1.52587890625e-05f, n.z, p.z);
    }
    else
    {
        _1894 = asfloat(asint(p.z) + _1841);
    }
    return float3(_1859, _1877, _1894);
}

float3 world_from_tangent(float3 T, float3 B, float3 N, float3 V)
{
    return ((T * V.x) + (B * V.y)) + (N * V.z);
}

float4 Sample_OrenDiffuse_BSDF(float3 T, float3 B, float3 N, float3 I, float roughness, float3 base_color, float rand_u, float rand_v, inout float3 out_V)
{
    float _2274 = 6.283185482025146484375f * rand_v;
    float _2286 = sqrt(mad(-rand_u, rand_u, 1.0f));
    float3 param = T;
    float3 param_1 = B;
    float3 param_2 = N;
    float3 param_3 = float3(_2286 * cos(_2274), _2286 * sin(_2274), rand_u);
    out_V = world_from_tangent(param, param_1, param_2, param_3);
    float3 param_4 = -I;
    float3 param_5 = N;
    float3 param_6 = out_V;
    float param_7 = roughness;
    float3 param_8 = base_color;
    return Evaluate_OrenDiffuse_BSDF(param_4, param_5, param_6, param_7, param_8);
}

float3 tangent_from_world(float3 T, float3 B, float3 N, float3 V)
{
    return float3(dot(V, T), dot(V, B), dot(V, N));
}

float D_GGX(float3 H, float alpha_x, float alpha_y)
{
    float _8940;
    do
    {
        if (H.z == 0.0f)
        {
            _8940 = 0.0f;
            break;
        }
        float _2101 = (-H.x) / (H.z * alpha_x);
        float _2107 = (-H.y) / (H.z * alpha_y);
        float _2116 = mad(_2107, _2107, mad(_2101, _2101, 1.0f));
        _8940 = 1.0f / (((((_2116 * _2116) * 3.1415927410125732421875f) * alpha_x) * alpha_y) * (((H.z * H.z) * H.z) * H.z));
        break;
    } while(false);
    return _8940;
}

float G1(float3 Ve, inout float alpha_x, inout float alpha_y)
{
    alpha_x *= alpha_x;
    alpha_y *= alpha_y;
    return 1.0f / mad((-1.0f) + sqrt(1.0f + (mad(alpha_x * Ve.x, Ve.x, (alpha_y * Ve.y) * Ve.y) / (Ve.z * Ve.z))), 0.5f, 1.0f);
}

float4 Evaluate_GGXSpecular_BSDF(float3 view_dir_ts, float3 sampled_normal_ts, float3 reflected_dir_ts, float alpha_x, float alpha_y, float spec_ior, float spec_F0, float3 spec_col)
{
    float _2456 = D_GGX(sampled_normal_ts, alpha_x, alpha_y);
    float3 param = view_dir_ts;
    float param_1 = alpha_x;
    float param_2 = alpha_y;
    float _2464 = G1(param, param_1, param_2);
    float3 param_3 = reflected_dir_ts;
    float param_4 = alpha_x;
    float param_5 = alpha_y;
    float _2471 = G1(param_3, param_4, param_5);
    float param_6 = dot(view_dir_ts, sampled_normal_ts);
    float param_7 = spec_ior;
    float3 F = lerp(spec_col, 1.0f.xxx, ((fresnel_dielectric_cos(param_6, param_7) - spec_F0) / (1.0f - spec_F0)).xxx);
    float _2499 = 4.0f * abs(view_dir_ts.z * reflected_dir_ts.z);
    float _2502;
    if (_2499 != 0.0f)
    {
        _2502 = (_2456 * (_2464 * _2471)) / _2499;
    }
    else
    {
        _2502 = 0.0f;
    }
    F *= _2502;
    float3 param_8 = view_dir_ts;
    float param_9 = alpha_x;
    float param_10 = alpha_y;
    float _2522 = G1(param_8, param_9, param_10);
    float pdf = ((_2456 * _2522) * clamp(dot(view_dir_ts, sampled_normal_ts), 0.0f, 1.0f)) / abs(view_dir_ts.z);
    float _2537 = 4.0f * dot(view_dir_ts, sampled_normal_ts);
    if (_2537 != 0.0f)
    {
        pdf /= _2537;
    }
    float3 _2548 = F;
    float3 _2549 = _2548 * max(reflected_dir_ts.z, 0.0f);
    F = _2549;
    return float4(_2549, pdf);
}

float3 SampleGGX_VNDF(float3 Ve, float alpha_x, float alpha_y, float U1, float U2)
{
    float3 _1919 = normalize(float3(alpha_x * Ve.x, alpha_y * Ve.y, Ve.z));
    float _1922 = _1919.x;
    float _1927 = _1919.y;
    float _1931 = mad(_1922, _1922, _1927 * _1927);
    float3 _1935;
    if (_1931 > 0.0f)
    {
        _1935 = float3(-_1927, _1922, 0.0f) / sqrt(_1931).xxx;
    }
    else
    {
        _1935 = float3(1.0f, 0.0f, 0.0f);
    }
    float _1957 = sqrt(U1);
    float _1960 = 6.283185482025146484375f * U2;
    float _1965 = _1957 * cos(_1960);
    float _1974 = 1.0f + _1919.z;
    float _1981 = mad(-_1965, _1965, 1.0f);
    float _1987 = mad(mad(-0.5f, _1974, 1.0f), sqrt(_1981), (0.5f * _1974) * (_1957 * sin(_1960)));
    float3 _2008 = ((_1935 * _1965) + (cross(_1919, _1935) * _1987)) + (_1919 * sqrt(max(0.0f, mad(-_1987, _1987, _1981))));
    return normalize(float3(alpha_x * _2008.x, alpha_y * _2008.y, max(0.0f, _2008.z)));
}

float4 Sample_GGXSpecular_BSDF(float3 T, float3 B, float3 N, float3 I, float roughness, float anisotropic, float spec_ior, float spec_F0, float3 spec_col, float rand_u, float rand_v, inout float3 out_V)
{
    float4 _8915;
    do
    {
        float _2559 = roughness * roughness;
        float _2563 = sqrt(mad(-0.89999997615814208984375f, anisotropic, 1.0f));
        float _2567 = _2559 / _2563;
        float _2571 = _2559 * _2563;
        [branch]
        if ((_2567 * _2571) < 1.0000000116860974230803549289703e-07f)
        {
            float3 _2581 = reflect(I, N);
            float param = dot(_2581, N);
            float param_1 = spec_ior;
            float3 _2595 = lerp(spec_col, 1.0f.xxx, ((fresnel_dielectric_cos(param, param_1) - spec_F0) / (1.0f - spec_F0)).xxx);
            out_V = _2581;
            _8915 = float4(_2595.x * 1000000.0f, _2595.y * 1000000.0f, _2595.z * 1000000.0f, 1000000.0f);
            break;
        }
        float3 param_2 = T;
        float3 param_3 = B;
        float3 param_4 = N;
        float3 param_5 = -I;
        float3 _2620 = normalize(tangent_from_world(param_2, param_3, param_4, param_5));
        float param_6 = _2567;
        float param_7 = _2571;
        float param_8 = rand_u;
        float param_9 = rand_v;
        float3 _2629 = SampleGGX_VNDF(_2620, param_6, param_7, param_8, param_9);
        float3 _2640 = normalize(reflect(-_2620, _2629));
        float3 param_10 = T;
        float3 param_11 = B;
        float3 param_12 = N;
        float3 param_13 = _2640;
        out_V = world_from_tangent(param_10, param_11, param_12, param_13);
        float3 param_14 = _2620;
        float3 param_15 = _2629;
        float3 param_16 = _2640;
        float param_17 = _2567;
        float param_18 = _2571;
        float param_19 = spec_ior;
        float param_20 = spec_F0;
        float3 param_21 = spec_col;
        _8915 = Evaluate_GGXSpecular_BSDF(param_14, param_15, param_16, param_17, param_18, param_19, param_20, param_21);
        break;
    } while(false);
    return _8915;
}

float4 Evaluate_GGXRefraction_BSDF(float3 view_dir_ts, float3 sampled_normal_ts, float3 refr_dir_ts, float roughness2, float eta, float3 refr_col)
{
    float4 _8920;
    do
    {
        bool _2862 = refr_dir_ts.z >= 0.0f;
        bool _2869;
        if (!_2862)
        {
            _2869 = view_dir_ts.z <= 0.0f;
        }
        else
        {
            _2869 = _2862;
        }
        if (_2869)
        {
            _8920 = 0.0f.xxxx;
            break;
        }
        float _2878 = D_GGX(sampled_normal_ts, roughness2, roughness2);
        float3 param = refr_dir_ts;
        float param_1 = roughness2;
        float param_2 = roughness2;
        float _2886 = G1(param, param_1, param_2);
        float3 param_3 = view_dir_ts;
        float param_4 = roughness2;
        float param_5 = roughness2;
        float _2894 = G1(param_3, param_4, param_5);
        float _2904 = mad(dot(view_dir_ts, sampled_normal_ts), eta, dot(refr_dir_ts, sampled_normal_ts));
        float _2914 = clamp(-dot(refr_dir_ts, sampled_normal_ts), 0.0f, 1.0f) / (_2904 * _2904);
        _8920 = float4(refr_col * (((((_2878 * _2894) * _2886) * clamp(dot(view_dir_ts, sampled_normal_ts), 0.0f, 1.0f)) * _2914) / view_dir_ts.z), (((_2878 * _2886) * clamp(dot(view_dir_ts, sampled_normal_ts), 0.0f, 1.0f)) * _2914) / view_dir_ts.z);
        break;
    } while(false);
    return _8920;
}

float4 Sample_GGXRefraction_BSDF(float3 T, float3 B, float3 N, float3 I, float roughness, float eta, float3 refr_col, float rand_u, float rand_v, inout float4 out_V)
{
    float4 _8925;
    do
    {
        float _2958 = roughness * roughness;
        [branch]
        if ((_2958 * _2958) < 1.0000000116860974230803549289703e-07f)
        {
            float _2968 = dot(I, N);
            float _2969 = -_2968;
            float _2979 = mad(-(eta * eta), mad(_2968, _2969, 1.0f), 1.0f);
            if (_2979 < 0.0f)
            {
                _8925 = 0.0f.xxxx;
                break;
            }
            float _2991 = mad(eta, _2969, -sqrt(_2979));
            out_V = float4(normalize((I * eta) + (N * _2991)), _2991);
            _8925 = float4(refr_col.x * 1000000.0f, refr_col.y * 1000000.0f, refr_col.z * 1000000.0f, 1000000.0f);
            break;
        }
        float3 param = T;
        float3 param_1 = B;
        float3 param_2 = N;
        float3 param_3 = -I;
        float3 _3031 = normalize(tangent_from_world(param, param_1, param_2, param_3));
        float param_4 = _2958;
        float param_5 = _2958;
        float param_6 = rand_u;
        float param_7 = rand_v;
        float3 _3042 = SampleGGX_VNDF(_3031, param_4, param_5, param_6, param_7);
        float _3046 = dot(_3031, _3042);
        float _3056 = mad(-(eta * eta), mad(-_3046, _3046, 1.0f), 1.0f);
        if (_3056 < 0.0f)
        {
            _8925 = 0.0f.xxxx;
            break;
        }
        float _3068 = mad(eta, _3046, -sqrt(_3056));
        float3 _3078 = normalize((_3031 * (-eta)) + (_3042 * _3068));
        float3 param_8 = _3031;
        float3 param_9 = _3042;
        float3 param_10 = _3078;
        float param_11 = _2958;
        float param_12 = eta;
        float3 param_13 = refr_col;
        float3 param_14 = T;
        float3 param_15 = B;
        float3 param_16 = N;
        float3 param_17 = _3078;
        out_V = float4(world_from_tangent(param_14, param_15, param_16, param_17), _3068);
        _8925 = Evaluate_GGXRefraction_BSDF(param_8, param_9, param_10, param_11, param_12, param_13);
        break;
    } while(false);
    return _8925;
}

void get_lobe_weights(float base_color_lum, float spec_color_lum, float specular, float metallic, float transmission, float clearcoat, inout float out_diffuse_weight, inout float out_specular_weight, inout float out_clearcoat_weight, inout float out_refraction_weight)
{
    float _1591 = 1.0f - metallic;
    out_diffuse_weight = (base_color_lum * _1591) * (1.0f - transmission);
    float _1601;
    if ((specular != 0.0f) || (metallic != 0.0f))
    {
        _1601 = spec_color_lum * mad(-transmission, _1591, 1.0f);
    }
    else
    {
        _1601 = 0.0f;
    }
    out_specular_weight = _1601;
    out_clearcoat_weight = (0.25f * clearcoat) * _1591;
    out_refraction_weight = (transmission * _1591) * base_color_lum;
    float _1616 = out_diffuse_weight;
    float _1617 = out_specular_weight;
    float _1619 = out_clearcoat_weight;
    float _1622 = ((_1616 + _1617) + _1619) + out_refraction_weight;
    if (_1622 != 0.0f)
    {
        out_diffuse_weight /= _1622;
        out_specular_weight /= _1622;
        out_clearcoat_weight /= _1622;
        out_refraction_weight /= _1622;
    }
}

float pow5(float v)
{
    return ((v * v) * (v * v)) * v;
}

float schlick_weight(float u)
{
    float param = clamp(1.0f - u, 0.0f, 1.0f);
    return pow5(param);
}

float BRDF_PrincipledDiffuse(float3 V, float3 N, float3 L, float3 H, float roughness)
{
    float _8945;
    do
    {
        float _2167 = dot(N, L);
        if (_2167 <= 0.0f)
        {
            _8945 = 0.0f;
            break;
        }
        float param = _2167;
        float param_1 = dot(N, V);
        float _2188 = dot(L, H);
        float _2196 = mad((2.0f * _2188) * _2188, roughness, 0.5f);
        _8945 = lerp(1.0f, _2196, schlick_weight(param)) * lerp(1.0f, _2196, schlick_weight(param_1));
        break;
    } while(false);
    return _8945;
}

float4 Evaluate_PrincipledDiffuse_BSDF(float3 V, float3 N, float3 L, float roughness, float3 base_color, float3 sheen_color, bool uniform_sampling)
{
    float weight;
    float pdf;
    if (uniform_sampling)
    {
        weight = 2.0f * dot(N, L);
        pdf = 0.15915493667125701904296875f;
    }
    else
    {
        weight = 1.0f;
        pdf = dot(N, L) * 0.3183098733425140380859375f;
    }
    float3 _2337 = normalize(L + V);
    float3 H = _2337;
    if (dot(V, _2337) < 0.0f)
    {
        H = -H;
    }
    float3 param = V;
    float3 param_1 = N;
    float3 param_2 = L;
    float3 param_3 = H;
    float param_4 = roughness;
    float3 diff_col = base_color * (weight * BRDF_PrincipledDiffuse(param, param_1, param_2, param_3, param_4));
    float param_5 = dot(L, H);
    float3 _2372 = diff_col;
    float3 _2373 = _2372 + (sheen_color * (3.1415927410125732421875f * schlick_weight(param_5)));
    diff_col = _2373;
    return float4(_2373, pdf);
}

float D_GTR1(float NDotH, float a)
{
    float _8950;
    do
    {
        if (a >= 1.0f)
        {
            _8950 = 0.3183098733425140380859375f;
            break;
        }
        float _2075 = mad(a, a, -1.0f);
        _8950 = _2075 / ((3.1415927410125732421875f * log(a * a)) * mad(_2075 * NDotH, NDotH, 1.0f));
        break;
    } while(false);
    return _8950;
}

float4 Evaluate_PrincipledClearcoat_BSDF(float3 view_dir_ts, float3 sampled_normal_ts, float3 reflected_dir_ts, float clearcoat_roughness2, float clearcoat_ior, float clearcoat_F0)
{
    float param = sampled_normal_ts.z;
    float param_1 = clearcoat_roughness2;
    float _2672 = D_GTR1(param, param_1);
    float3 param_2 = view_dir_ts;
    float param_3 = 0.0625f;
    float param_4 = 0.0625f;
    float _2679 = G1(param_2, param_3, param_4);
    float3 param_5 = reflected_dir_ts;
    float param_6 = 0.0625f;
    float param_7 = 0.0625f;
    float _2684 = G1(param_5, param_6, param_7);
    float param_8 = dot(reflected_dir_ts, sampled_normal_ts);
    float param_9 = clearcoat_ior;
    float F = lerp(0.039999999105930328369140625f, 1.0f, (fresnel_dielectric_cos(param_8, param_9) - clearcoat_F0) / (1.0f - clearcoat_F0));
    float _2711 = (4.0f * abs(view_dir_ts.z)) * abs(reflected_dir_ts.z);
    float _2714;
    if (_2711 != 0.0f)
    {
        _2714 = (_2672 * (_2679 * _2684)) / _2711;
    }
    else
    {
        _2714 = 0.0f;
    }
    F *= _2714;
    float3 param_10 = view_dir_ts;
    float param_11 = 0.0625f;
    float param_12 = 0.0625f;
    float _2732 = G1(param_10, param_11, param_12);
    float pdf = ((_2672 * _2732) * clamp(dot(view_dir_ts, sampled_normal_ts), 0.0f, 1.0f)) / abs(view_dir_ts.z);
    float _2747 = 4.0f * dot(view_dir_ts, sampled_normal_ts);
    if (_2747 != 0.0f)
    {
        pdf /= _2747;
    }
    float _2758 = F;
    float _2759 = _2758 * clamp(reflected_dir_ts.z, 0.0f, 1.0f);
    F = _2759;
    return float4(_2759, _2759, _2759, pdf);
}

float4 Sample_PrincipledDiffuse_BSDF(float3 T, float3 B, float3 N, float3 I, float roughness, float3 base_color, float3 sheen_color, bool uniform_sampling, float rand_u, float rand_v, inout float3 out_V)
{
    float _2384 = 6.283185482025146484375f * rand_v;
    float _2387 = cos(_2384);
    float _2390 = sin(_2384);
    float3 V;
    if (uniform_sampling)
    {
        float _2399 = sqrt(mad(-rand_u, rand_u, 1.0f));
        V = float3(_2399 * _2387, _2399 * _2390, rand_u);
    }
    else
    {
        float _2412 = sqrt(rand_u);
        V = float3(_2412 * _2387, _2412 * _2390, sqrt(1.0f - rand_u));
    }
    float3 param = T;
    float3 param_1 = B;
    float3 param_2 = N;
    float3 param_3 = V;
    out_V = world_from_tangent(param, param_1, param_2, param_3);
    float3 param_4 = -I;
    float3 param_5 = N;
    float3 param_6 = out_V;
    float param_7 = roughness;
    float3 param_8 = base_color;
    float3 param_9 = sheen_color;
    bool param_10 = uniform_sampling;
    return Evaluate_PrincipledDiffuse_BSDF(param_4, param_5, param_6, param_7, param_8, param_9, param_10);
}

float4 Sample_PrincipledClearcoat_BSDF(float3 T, float3 B, float3 N, float3 I, float clearcoat_roughness2, float clearcoat_ior, float clearcoat_F0, float rand_u, float rand_v, inout float3 out_V)
{
    float4 _8930;
    do
    {
        [branch]
        if ((clearcoat_roughness2 * clearcoat_roughness2) < 1.0000000116860974230803549289703e-07f)
        {
            float3 _2776 = reflect(I, N);
            float param = dot(_2776, N);
            float param_1 = clearcoat_ior;
            out_V = _2776;
            float _2795 = lerp(0.039999999105930328369140625f, 1.0f, (fresnel_dielectric_cos(param, param_1) - clearcoat_F0) / (1.0f - clearcoat_F0)) * 1000000.0f;
            _8930 = float4(_2795, _2795, _2795, 1000000.0f);
            break;
        }
        float3 param_2 = T;
        float3 param_3 = B;
        float3 param_4 = N;
        float3 param_5 = -I;
        float3 _2813 = normalize(tangent_from_world(param_2, param_3, param_4, param_5));
        float param_6 = clearcoat_roughness2;
        float param_7 = clearcoat_roughness2;
        float param_8 = rand_u;
        float param_9 = rand_v;
        float3 _2824 = SampleGGX_VNDF(_2813, param_6, param_7, param_8, param_9);
        float3 _2835 = normalize(reflect(-_2813, _2824));
        float3 param_10 = T;
        float3 param_11 = B;
        float3 param_12 = N;
        float3 param_13 = _2835;
        out_V = world_from_tangent(param_10, param_11, param_12, param_13);
        float3 param_14 = _2813;
        float3 param_15 = _2824;
        float3 param_16 = _2835;
        float param_17 = clearcoat_roughness2;
        float param_18 = clearcoat_ior;
        float param_19 = clearcoat_F0;
        _8930 = Evaluate_PrincipledClearcoat_BSDF(param_14, param_15, param_16, param_17, param_18, param_19);
        break;
    } while(false);
    return _8930;
}

float3 ShadeSurface(int px_index, hit_data_t inter, ray_data_t ray)
{
    float3 _8905;
    do
    {
        float3 _4804 = float3(ray.d[0], ray.d[1], ray.d[2]);
        [branch]
        if (inter.mask == 0)
        {
            float3 env_col = _3224_g_params.env_col.xyz;
            uint _4817 = asuint(_3224_g_params.env_col.w);
            if (_4817 != 4294967295u)
            {
                atlas_texture_t _4824;
                _4824.size = _855.Load(_4817 * 80 + 0);
                _4824.atlas = _855.Load(_4817 * 80 + 4);
                [unroll]
                for (int _92ident = 0; _92ident < 4; _92ident++)
                {
                    _4824.page[_92ident] = _855.Load(_92ident * 4 + _4817 * 80 + 8);
                }
                [unroll]
                for (int _93ident = 0; _93ident < 14; _93ident++)
                {
                    _4824.pos[_93ident] = _855.Load(_93ident * 4 + _4817 * 80 + 24);
                }
                uint _9824[14] = { _4824.pos[0], _4824.pos[1], _4824.pos[2], _4824.pos[3], _4824.pos[4], _4824.pos[5], _4824.pos[6], _4824.pos[7], _4824.pos[8], _4824.pos[9], _4824.pos[10], _4824.pos[11], _4824.pos[12], _4824.pos[13] };
                uint _9795[4] = { _4824.page[0], _4824.page[1], _4824.page[2], _4824.page[3] };
                atlas_texture_t _9448 = { _4824.size, _4824.atlas, _9795, _9824 };
                float param = _3224_g_params.env_rotation;
                env_col *= SampleLatlong_RGBE(_9448, _4804, param);
                if (_3224_g_params.env_qtree_levels > 0)
                {
                    float param_1 = ray.pdf;
                    float param_2 = Evaluate_EnvQTree(_3224_g_params.env_rotation, g_env_qtree, _g_env_qtree_sampler, _3224_g_params.env_qtree_levels, _4804);
                    env_col *= power_heuristic(param_1, param_2);
                }
            }
            _8905 = float3(ray.c[0] * env_col.x, ray.c[1] * env_col.y, ray.c[2] * env_col.z);
            break;
        }
        float3 _4927 = float3(ray.o[0], ray.o[1], ray.o[2]) + (_4804 * inter.t);
        [branch]
        if (inter.obj_index < 0)
        {
            light_t _4939;
            _4939.type_and_param0 = _3256.Load4(((-1) - inter.obj_index) * 64 + 0);
            _4939.param1 = asfloat(_3256.Load4(((-1) - inter.obj_index) * 64 + 16));
            _4939.param2 = asfloat(_3256.Load4(((-1) - inter.obj_index) * 64 + 32));
            _4939.param3 = asfloat(_3256.Load4(((-1) - inter.obj_index) * 64 + 48));
            float3 lcol = asfloat(_4939.type_and_param0.yzw);
            uint _4956 = _4939.type_and_param0.x & 31u;
            if (_4956 == 0u)
            {
                float param_3 = ray.pdf;
                float param_4 = (inter.t * inter.t) / ((0.5f * _4939.param1.w) * dot(_4804, normalize(_4939.param1.xyz - _4927)));
                lcol *= power_heuristic(param_3, param_4);
                bool _5023 = _4939.param3.x > 0.0f;
                bool _5029;
                if (_5023)
                {
                    _5029 = _4939.param3.y > 0.0f;
                }
                else
                {
                    _5029 = _5023;
                }
                [branch]
                if (_5029)
                {
                    [flatten]
                    if (_4939.param3.y > 0.0f)
                    {
                        lcol *= clamp((_4939.param3.x - acos(clamp(-dot(_4804, _4939.param2.xyz), 0.0f, 1.0f))) / _4939.param3.y, 0.0f, 1.0f);
                    }
                }
            }
            else
            {
                if (_4956 == 4u)
                {
                    float param_5 = ray.pdf;
                    float param_6 = (inter.t * inter.t) / (_4939.param1.w * dot(_4804, normalize(cross(_4939.param2.xyz, _4939.param3.xyz))));
                    lcol *= power_heuristic(param_5, param_6);
                }
                else
                {
                    if (_4956 == 5u)
                    {
                        float param_7 = ray.pdf;
                        float param_8 = (inter.t * inter.t) / (_4939.param1.w * dot(_4804, normalize(cross(_4939.param2.xyz, _4939.param3.xyz))));
                        lcol *= power_heuristic(param_7, param_8);
                    }
                    else
                    {
                        if (_4956 == 3u)
                        {
                            float param_9 = ray.pdf;
                            float param_10 = (inter.t * inter.t) / (_4939.param1.w * (1.0f - abs(dot(_4804, _4939.param3.xyz))));
                            lcol *= power_heuristic(param_9, param_10);
                        }
                    }
                }
            }
            _8905 = float3(ray.c[0] * lcol.x, ray.c[1] * lcol.y, ray.c[2] * lcol.z);
            break;
        }
        bool _5228 = inter.prim_index < 0;
        int _5231;
        if (_5228)
        {
            _5231 = (-1) - inter.prim_index;
        }
        else
        {
            _5231 = inter.prim_index;
        }
        uint _5242 = uint(_5231);
        material_t _5250;
        [unroll]
        for (int _94ident = 0; _94ident < 5; _94ident++)
        {
            _5250.textures[_94ident] = _4513.Load(_94ident * 4 + ((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 0);
        }
        [unroll]
        for (int _95ident = 0; _95ident < 3; _95ident++)
        {
            _5250.base_color[_95ident] = asfloat(_4513.Load(_95ident * 4 + ((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 20));
        }
        _5250.flags = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 32);
        _5250.type = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 36);
        _5250.tangent_rotation_or_strength = asfloat(_4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 40));
        _5250.roughness_and_anisotropic = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 44);
        _5250.int_ior = asfloat(_4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 48));
        _5250.ext_ior = asfloat(_4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 52));
        _5250.sheen_and_sheen_tint = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 56);
        _5250.tint_and_metallic = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 60);
        _5250.transmission_and_transmission_roughness = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 64);
        _5250.specular_and_specular_tint = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 68);
        _5250.clearcoat_and_clearcoat_roughness = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 72);
        _5250.normal_map_strength_unorm = _4513.Load(((_4517.Load(_5242 * 4 + 0) >> 16u) & 16383u) * 80 + 76);
        uint _9825 = _5250.textures[0];
        uint _9826 = _5250.textures[1];
        uint _9827 = _5250.textures[2];
        uint _9828 = _5250.textures[3];
        uint _9829 = _5250.textures[4];
        float _9830 = _5250.base_color[0];
        float _9831 = _5250.base_color[1];
        float _9832 = _5250.base_color[2];
        uint _9465 = _5250.flags;
        uint _9466 = _5250.type;
        float _9467 = _5250.tangent_rotation_or_strength;
        uint _9468 = _5250.roughness_and_anisotropic;
        float _9469 = _5250.int_ior;
        float _9470 = _5250.ext_ior;
        uint _9471 = _5250.sheen_and_sheen_tint;
        uint _9472 = _5250.tint_and_metallic;
        uint _9473 = _5250.transmission_and_transmission_roughness;
        uint _9474 = _5250.specular_and_specular_tint;
        uint _9475 = _5250.clearcoat_and_clearcoat_roughness;
        uint _9476 = _5250.normal_map_strength_unorm;
        transform_t _5307;
        _5307.xform = asfloat(uint4x4(_4173.Load4(asuint(asfloat(_5300.Load(inter.obj_index * 32 + 12))) * 128 + 0), _4173.Load4(asuint(asfloat(_5300.Load(inter.obj_index * 32 + 12))) * 128 + 16), _4173.Load4(asuint(asfloat(_5300.Load(inter.obj_index * 32 + 12))) * 128 + 32), _4173.Load4(asuint(asfloat(_5300.Load(inter.obj_index * 32 + 12))) * 128 + 48)));
        _5307.inv_xform = asfloat(uint4x4(_4173.Load4(asuint(asfloat(_5300.Load(inter.obj_index * 32 + 12))) * 128 + 64), _4173.Load4(asuint(asfloat(_5300.Load(inter.obj_index * 32 + 12))) * 128 + 80), _4173.Load4(asuint(asfloat(_5300.Load(inter.obj_index * 32 + 12))) * 128 + 96), _4173.Load4(asuint(asfloat(_5300.Load(inter.obj_index * 32 + 12))) * 128 + 112)));
        uint _5314 = _5242 * 3u;
        vertex_t _5319;
        [unroll]
        for (int _96ident = 0; _96ident < 3; _96ident++)
        {
            _5319.p[_96ident] = asfloat(_4198.Load(_96ident * 4 + _4202.Load(_5314 * 4 + 0) * 52 + 0));
        }
        [unroll]
        for (int _97ident = 0; _97ident < 3; _97ident++)
        {
            _5319.n[_97ident] = asfloat(_4198.Load(_97ident * 4 + _4202.Load(_5314 * 4 + 0) * 52 + 12));
        }
        [unroll]
        for (int _98ident = 0; _98ident < 3; _98ident++)
        {
            _5319.b[_98ident] = asfloat(_4198.Load(_98ident * 4 + _4202.Load(_5314 * 4 + 0) * 52 + 24));
        }
        [unroll]
        for (int _99ident = 0; _99ident < 2; _99ident++)
        {
            [unroll]
            for (int _100ident = 0; _100ident < 2; _100ident++)
            {
                _5319.t[_99ident][_100ident] = asfloat(_4198.Load(_100ident * 4 + _99ident * 8 + _4202.Load(_5314 * 4 + 0) * 52 + 36));
            }
        }
        vertex_t _5365;
        [unroll]
        for (int _101ident = 0; _101ident < 3; _101ident++)
        {
            _5365.p[_101ident] = asfloat(_4198.Load(_101ident * 4 + _4202.Load((_5314 + 1u) * 4 + 0) * 52 + 0));
        }
        [unroll]
        for (int _102ident = 0; _102ident < 3; _102ident++)
        {
            _5365.n[_102ident] = asfloat(_4198.Load(_102ident * 4 + _4202.Load((_5314 + 1u) * 4 + 0) * 52 + 12));
        }
        [unroll]
        for (int _103ident = 0; _103ident < 3; _103ident++)
        {
            _5365.b[_103ident] = asfloat(_4198.Load(_103ident * 4 + _4202.Load((_5314 + 1u) * 4 + 0) * 52 + 24));
        }
        [unroll]
        for (int _104ident = 0; _104ident < 2; _104ident++)
        {
            [unroll]
            for (int _105ident = 0; _105ident < 2; _105ident++)
            {
                _5365.t[_104ident][_105ident] = asfloat(_4198.Load(_105ident * 4 + _104ident * 8 + _4202.Load((_5314 + 1u) * 4 + 0) * 52 + 36));
            }
        }
        vertex_t _5411;
        [unroll]
        for (int _106ident = 0; _106ident < 3; _106ident++)
        {
            _5411.p[_106ident] = asfloat(_4198.Load(_106ident * 4 + _4202.Load((_5314 + 2u) * 4 + 0) * 52 + 0));
        }
        [unroll]
        for (int _107ident = 0; _107ident < 3; _107ident++)
        {
            _5411.n[_107ident] = asfloat(_4198.Load(_107ident * 4 + _4202.Load((_5314 + 2u) * 4 + 0) * 52 + 12));
        }
        [unroll]
        for (int _108ident = 0; _108ident < 3; _108ident++)
        {
            _5411.b[_108ident] = asfloat(_4198.Load(_108ident * 4 + _4202.Load((_5314 + 2u) * 4 + 0) * 52 + 24));
        }
        [unroll]
        for (int _109ident = 0; _109ident < 2; _109ident++)
        {
            [unroll]
            for (int _110ident = 0; _110ident < 2; _110ident++)
            {
                _5411.t[_109ident][_110ident] = asfloat(_4198.Load(_110ident * 4 + _109ident * 8 + _4202.Load((_5314 + 2u) * 4 + 0) * 52 + 36));
            }
        }
        float3 _5457 = float3(_5319.p[0], _5319.p[1], _5319.p[2]);
        float3 _5465 = float3(_5365.p[0], _5365.p[1], _5365.p[2]);
        float3 _5473 = float3(_5411.p[0], _5411.p[1], _5411.p[2]);
        float _5480 = (1.0f - inter.u) - inter.v;
        float3 _5513 = normalize(((float3(_5319.n[0], _5319.n[1], _5319.n[2]) * _5480) + (float3(_5365.n[0], _5365.n[1], _5365.n[2]) * inter.u)) + (float3(_5411.n[0], _5411.n[1], _5411.n[2]) * inter.v));
        float3 N = _5513;
        float2 _5539 = ((float2(_5319.t[0][0], _5319.t[0][1]) * _5480) + (float2(_5365.t[0][0], _5365.t[0][1]) * inter.u)) + (float2(_5411.t[0][0], _5411.t[0][1]) * inter.v);
        float3 _5555 = cross(_5465 - _5457, _5473 - _5457);
        float _5558 = length(_5555);
        float3 plane_N = _5555 / _5558.xxx;
        float3 _5594 = ((float3(_5319.b[0], _5319.b[1], _5319.b[2]) * _5480) + (float3(_5365.b[0], _5365.b[1], _5365.b[2]) * inter.u)) + (float3(_5411.b[0], _5411.b[1], _5411.b[2]) * inter.v);
        float3 B = _5594;
        float3 T = cross(_5594, _5513);
        if (_5228)
        {
            if ((_4517.Load(_5242 * 4 + 0) & 65535u) == 65535u)
            {
                _8905 = 0.0f.xxx;
                break;
            }
            material_t _5617;
            [unroll]
            for (int _111ident = 0; _111ident < 5; _111ident++)
            {
                _5617.textures[_111ident] = _4513.Load(_111ident * 4 + (_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 0);
            }
            [unroll]
            for (int _112ident = 0; _112ident < 3; _112ident++)
            {
                _5617.base_color[_112ident] = asfloat(_4513.Load(_112ident * 4 + (_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 20));
            }
            _5617.flags = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 32);
            _5617.type = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 36);
            _5617.tangent_rotation_or_strength = asfloat(_4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 40));
            _5617.roughness_and_anisotropic = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 44);
            _5617.int_ior = asfloat(_4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 48));
            _5617.ext_ior = asfloat(_4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 52));
            _5617.sheen_and_sheen_tint = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 56);
            _5617.tint_and_metallic = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 60);
            _5617.transmission_and_transmission_roughness = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 64);
            _5617.specular_and_specular_tint = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 68);
            _5617.clearcoat_and_clearcoat_roughness = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 72);
            _5617.normal_map_strength_unorm = _4513.Load((_4517.Load(_5242 * 4 + 0) & 16383u) * 80 + 76);
            _9825 = _5617.textures[0];
            _9826 = _5617.textures[1];
            _9827 = _5617.textures[2];
            _9828 = _5617.textures[3];
            _9829 = _5617.textures[4];
            _9830 = _5617.base_color[0];
            _9831 = _5617.base_color[1];
            _9832 = _5617.base_color[2];
            _9465 = _5617.flags;
            _9466 = _5617.type;
            _9467 = _5617.tangent_rotation_or_strength;
            _9468 = _5617.roughness_and_anisotropic;
            _9469 = _5617.int_ior;
            _9470 = _5617.ext_ior;
            _9471 = _5617.sheen_and_sheen_tint;
            _9472 = _5617.tint_and_metallic;
            _9473 = _5617.transmission_and_transmission_roughness;
            _9474 = _5617.specular_and_specular_tint;
            _9475 = _5617.clearcoat_and_clearcoat_roughness;
            _9476 = _5617.normal_map_strength_unorm;
            plane_N = -plane_N;
            N = -N;
            B = -B;
            T = -T;
        }
        float3 param_11 = plane_N;
        float4x4 param_12 = _5307.inv_xform;
        plane_N = TransformNormal(param_11, param_12);
        float3 param_13 = N;
        float4x4 param_14 = _5307.inv_xform;
        N = TransformNormal(param_13, param_14);
        float3 param_15 = B;
        float4x4 param_16 = _5307.inv_xform;
        B = TransformNormal(param_15, param_16);
        float3 param_17 = T;
        float4x4 param_18 = _5307.inv_xform;
        T = TransformNormal(param_17, param_18);
        float _5727 = mad(ray.cone_spread, inter.t, ray.cone_width);
        float _5737 = mad(0.5f, log2(abs(mad(_5365.t[0][0] - _5319.t[0][0], _5411.t[0][1] - _5319.t[0][1], -((_5411.t[0][0] - _5319.t[0][0]) * (_5365.t[0][1] - _5319.t[0][1])))) / _5558), log2(_5727));
        uint param_19 = uint(hash(px_index));
        float _5743 = construct_float(param_19);
        uint param_20 = uint(hash(hash(px_index)));
        float _5749 = construct_float(param_20);
        float3 col = 0.0f.xxx;
        int _5756 = ray.ray_depth & 255;
        int _5761 = (ray.ray_depth >> 8) & 255;
        int _5766 = (ray.ray_depth >> 16) & 255;
        int _5772 = (ray.ray_depth >> 24) & 255;
        int _5780 = ((_5756 + _5761) + _5766) + _5772;
        float mix_rand = frac(asfloat(_3219.Load(_3224_g_params.hi * 4 + 0)) + _5743);
        float mix_weight = 1.0f;
        float _5817;
        float _5836;
        float _5861;
        float _5930;
        while (_9466 == 4u)
        {
            float mix_val = _9467;
            if (_9826 != 4294967295u)
            {
                mix_val *= SampleBilinear(_9826, _5539, 0).x;
            }
            if (_5228)
            {
                _5817 = _9470 / _9469;
            }
            else
            {
                _5817 = _9469 / _9470;
            }
            if (_9469 != 0.0f)
            {
                float param_21 = dot(_4804, N);
                float param_22 = _5817;
                _5836 = fresnel_dielectric_cos(param_21, param_22);
            }
            else
            {
                _5836 = 1.0f;
            }
            float _5850 = mix_val;
            float _5851 = _5850 * clamp(_5836, 0.0f, 1.0f);
            mix_val = _5851;
            if (mix_rand > _5851)
            {
                if ((_9465 & 2u) != 0u)
                {
                    _5861 = 1.0f / (1.0f - mix_val);
                }
                else
                {
                    _5861 = 1.0f;
                }
                mix_weight *= _5861;
                material_t _5874;
                [unroll]
                for (int _113ident = 0; _113ident < 5; _113ident++)
                {
                    _5874.textures[_113ident] = _4513.Load(_113ident * 4 + _9828 * 80 + 0);
                }
                [unroll]
                for (int _114ident = 0; _114ident < 3; _114ident++)
                {
                    _5874.base_color[_114ident] = asfloat(_4513.Load(_114ident * 4 + _9828 * 80 + 20));
                }
                _5874.flags = _4513.Load(_9828 * 80 + 32);
                _5874.type = _4513.Load(_9828 * 80 + 36);
                _5874.tangent_rotation_or_strength = asfloat(_4513.Load(_9828 * 80 + 40));
                _5874.roughness_and_anisotropic = _4513.Load(_9828 * 80 + 44);
                _5874.int_ior = asfloat(_4513.Load(_9828 * 80 + 48));
                _5874.ext_ior = asfloat(_4513.Load(_9828 * 80 + 52));
                _5874.sheen_and_sheen_tint = _4513.Load(_9828 * 80 + 56);
                _5874.tint_and_metallic = _4513.Load(_9828 * 80 + 60);
                _5874.transmission_and_transmission_roughness = _4513.Load(_9828 * 80 + 64);
                _5874.specular_and_specular_tint = _4513.Load(_9828 * 80 + 68);
                _5874.clearcoat_and_clearcoat_roughness = _4513.Load(_9828 * 80 + 72);
                _5874.normal_map_strength_unorm = _4513.Load(_9828 * 80 + 76);
                _9825 = _5874.textures[0];
                _9826 = _5874.textures[1];
                _9827 = _5874.textures[2];
                _9828 = _5874.textures[3];
                _9829 = _5874.textures[4];
                _9830 = _5874.base_color[0];
                _9831 = _5874.base_color[1];
                _9832 = _5874.base_color[2];
                _9465 = _5874.flags;
                _9466 = _5874.type;
                _9467 = _5874.tangent_rotation_or_strength;
                _9468 = _5874.roughness_and_anisotropic;
                _9469 = _5874.int_ior;
                _9470 = _5874.ext_ior;
                _9471 = _5874.sheen_and_sheen_tint;
                _9472 = _5874.tint_and_metallic;
                _9473 = _5874.transmission_and_transmission_roughness;
                _9474 = _5874.specular_and_specular_tint;
                _9475 = _5874.clearcoat_and_clearcoat_roughness;
                _9476 = _5874.normal_map_strength_unorm;
                mix_rand = (mix_rand - mix_val) / (1.0f - mix_val);
            }
            else
            {
                if ((_9465 & 2u) != 0u)
                {
                    _5930 = 1.0f / mix_val;
                }
                else
                {
                    _5930 = 1.0f;
                }
                mix_weight *= _5930;
                material_t _5942;
                [unroll]
                for (int _115ident = 0; _115ident < 5; _115ident++)
                {
                    _5942.textures[_115ident] = _4513.Load(_115ident * 4 + _9829 * 80 + 0);
                }
                [unroll]
                for (int _116ident = 0; _116ident < 3; _116ident++)
                {
                    _5942.base_color[_116ident] = asfloat(_4513.Load(_116ident * 4 + _9829 * 80 + 20));
                }
                _5942.flags = _4513.Load(_9829 * 80 + 32);
                _5942.type = _4513.Load(_9829 * 80 + 36);
                _5942.tangent_rotation_or_strength = asfloat(_4513.Load(_9829 * 80 + 40));
                _5942.roughness_and_anisotropic = _4513.Load(_9829 * 80 + 44);
                _5942.int_ior = asfloat(_4513.Load(_9829 * 80 + 48));
                _5942.ext_ior = asfloat(_4513.Load(_9829 * 80 + 52));
                _5942.sheen_and_sheen_tint = _4513.Load(_9829 * 80 + 56);
                _5942.tint_and_metallic = _4513.Load(_9829 * 80 + 60);
                _5942.transmission_and_transmission_roughness = _4513.Load(_9829 * 80 + 64);
                _5942.specular_and_specular_tint = _4513.Load(_9829 * 80 + 68);
                _5942.clearcoat_and_clearcoat_roughness = _4513.Load(_9829 * 80 + 72);
                _5942.normal_map_strength_unorm = _4513.Load(_9829 * 80 + 76);
                _9825 = _5942.textures[0];
                _9826 = _5942.textures[1];
                _9827 = _5942.textures[2];
                _9828 = _5942.textures[3];
                _9829 = _5942.textures[4];
                _9830 = _5942.base_color[0];
                _9831 = _5942.base_color[1];
                _9832 = _5942.base_color[2];
                _9465 = _5942.flags;
                _9466 = _5942.type;
                _9467 = _5942.tangent_rotation_or_strength;
                _9468 = _5942.roughness_and_anisotropic;
                _9469 = _5942.int_ior;
                _9470 = _5942.ext_ior;
                _9471 = _5942.sheen_and_sheen_tint;
                _9472 = _5942.tint_and_metallic;
                _9473 = _5942.transmission_and_transmission_roughness;
                _9474 = _5942.specular_and_specular_tint;
                _9475 = _5942.clearcoat_and_clearcoat_roughness;
                _9476 = _5942.normal_map_strength_unorm;
                mix_rand /= mix_val;
            }
        }
        [branch]
        if (_9825 != 4294967295u)
        {
            float3 normals = (float3(SampleBilinear(_9825, _5539, 0).xy, 1.0f) * 2.0f) - 1.0f.xxx;
            if ((_855.Load(_9825 * 80 + 0) & 16384u) != 0u)
            {
                float3 _10184 = normals;
                _10184.z = sqrt(1.0f - dot(normals.xy, normals.xy));
                normals = _10184;
            }
            float3 _6026 = N;
            N = normalize(((T * normals.x) + (_6026 * normals.z)) + (B * normals.y));
            if ((_9476 & 65535u) != 65535u)
            {
                N = normalize(_6026 + ((N - _6026) * clamp(float(_9476 & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f)));
            }
            float3 param_23 = plane_N;
            float3 param_24 = -_4804;
            float3 param_25 = N;
            N = ensure_valid_reflection(param_23, param_24, param_25);
        }
        float3 _6083 = ((_5457 * _5480) + (_5465 * inter.u)) + (_5473 * inter.v);
        float3 _6090 = float3(-_6083.z, 0.0f, _6083.x);
        float3 tangent = _6090;
        float3 param_26 = _6090;
        float4x4 param_27 = _5307.inv_xform;
        tangent = TransformNormal(param_26, param_27);
        if (_9467 != 0.0f)
        {
            float3 param_28 = tangent;
            float3 param_29 = N;
            float param_30 = _9467;
            tangent = rotate_around_axis(param_28, param_29, param_30);
        }
        float3 _6113 = normalize(cross(tangent, N));
        B = _6113;
        T = cross(N, _6113);
        float3 _9564 = 0.0f.xxx;
        float3 _9563 = 0.0f.xxx;
        float _9566 = 0.0f;
        float _9567 = 0.0f;
        float _9565 = 0.0f;
        bool _6125 = _3224_g_params.li_count != 0;
        bool _6131;
        if (_6125)
        {
            _6131 = _9466 != 3u;
        }
        else
        {
            _6131 = _6125;
        }
        float _9568;
        if (_6131)
        {
            float3 param_31 = _4927;
            float2 param_32 = float2(_5743, _5749);
            light_sample_t _9575 = { _9563, _9564, _9565, _9566, _9567, _9568 };
            light_sample_t param_33 = _9575;
            SampleLightSource(param_31, param_32, param_33);
            _9563 = param_33.col;
            _9564 = param_33.L;
            _9565 = param_33.area;
            _9566 = param_33.dist;
            _9567 = param_33.pdf;
            _9568 = param_33.cast_shadow;
        }
        float _6146 = dot(N, _9564);
        float3 base_color = float3(_9830, _9831, _9832);
        [branch]
        if (_9826 != 4294967295u)
        {
            base_color *= SampleBilinear(_9826, _5539, int(get_texture_lod(texSize(_9826), _5737)), true, true).xyz;
        }
        float3 tint_color = 0.0f.xxx;
        float _6190 = lum(base_color);
        [flatten]
        if (_6190 > 0.0f)
        {
            tint_color = base_color / _6190.xxx;
        }
        float roughness = clamp(float(_9468 & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f);
        [branch]
        if (_9827 != 4294967295u)
        {
            roughness *= SampleBilinear(_9827, _5539, int(get_texture_lod(texSize(_9827), _5737)), false, true).x;
        }
        float _6231 = asfloat(_3219.Load((_3224_g_params.hi + 1) * 4 + 0));
        float _6235 = frac(_6231 + _5743);
        float _6241 = asfloat(_3219.Load((_3224_g_params.hi + 2) * 4 + 0));
        float _6245 = frac(_6241 + _5749);
        float _9896 = 0.0f;
        float _9895 = 0.0f;
        float _9894 = 0.0f;
        float _9596 = 0.0f;
        int _9601;
        float _9880;
        float _9881;
        float _9882;
        float _9887;
        float _9888;
        float _9889;
        [branch]
        if (_9466 == 0u)
        {
            [branch]
            if ((_9567 > 0.0f) && (_6146 > 0.0f))
            {
                float3 param_34 = -_4804;
                float3 param_35 = N;
                float3 param_36 = _9564;
                float param_37 = roughness;
                float3 param_38 = base_color;
                float4 _6285 = Evaluate_OrenDiffuse_BSDF(param_34, param_35, param_36, param_37, param_38);
                float mis_weight = 1.0f;
                if (_9565 > 0.0f)
                {
                    float param_39 = _9567;
                    float param_40 = _6285.w;
                    mis_weight = power_heuristic(param_39, param_40);
                }
                float3 _6313 = (_9563 * _6285.xyz) * ((mix_weight * mis_weight) / _9567);
                [branch]
                if (_9568 > 0.5f)
                {
                    float3 param_41 = _4927;
                    float3 param_42 = plane_N;
                    float3 _6324 = offset_ray(param_41, param_42);
                    uint _6378;
                    _6376.InterlockedAdd(8, 1u, _6378);
                    _6386.Store(_6378 * 44 + 0, asuint(_6324.x));
                    _6386.Store(_6378 * 44 + 4, asuint(_6324.y));
                    _6386.Store(_6378 * 44 + 8, asuint(_6324.z));
                    _6386.Store(_6378 * 44 + 12, asuint(_9564.x));
                    _6386.Store(_6378 * 44 + 16, asuint(_9564.y));
                    _6386.Store(_6378 * 44 + 20, asuint(_9564.z));
                    _6386.Store(_6378 * 44 + 24, asuint(_9566 - 9.9999997473787516355514526367188e-05f));
                    _6386.Store(_6378 * 44 + 28, asuint(ray.c[0] * _6313.x));
                    _6386.Store(_6378 * 44 + 32, asuint(ray.c[1] * _6313.y));
                    _6386.Store(_6378 * 44 + 36, asuint(ray.c[2] * _6313.z));
                    _6386.Store(_6378 * 44 + 40, uint(ray.xy));
                }
                else
                {
                    col += _6313;
                }
            }
            bool _6430 = _5756 < _3224_g_params.max_diff_depth;
            bool _6437;
            if (_6430)
            {
                _6437 = _5780 < _3224_g_params.max_total_depth;
            }
            else
            {
                _6437 = _6430;
            }
            [branch]
            if (_6437)
            {
                float3 param_43 = T;
                float3 param_44 = B;
                float3 param_45 = N;
                float3 param_46 = _4804;
                float param_47 = roughness;
                float3 param_48 = base_color;
                float param_49 = _6235;
                float param_50 = _6245;
                float3 param_51;
                float4 _6459 = Sample_OrenDiffuse_BSDF(param_43, param_44, param_45, param_46, param_47, param_48, param_49, param_50, param_51);
                _9601 = ray.ray_depth + 1;
                float3 param_52 = _4927;
                float3 param_53 = plane_N;
                float3 _6470 = offset_ray(param_52, param_53);
                _9880 = _6470.x;
                _9881 = _6470.y;
                _9882 = _6470.z;
                _9887 = param_51.x;
                _9888 = param_51.y;
                _9889 = param_51.z;
                _9894 = ((ray.c[0] * _6459.x) * mix_weight) / _6459.w;
                _9895 = ((ray.c[1] * _6459.y) * mix_weight) / _6459.w;
                _9896 = ((ray.c[2] * _6459.z) * mix_weight) / _6459.w;
                _9596 = _6459.w;
            }
        }
        else
        {
            [branch]
            if (_9466 == 1u)
            {
                float param_54 = 1.0f;
                float param_55 = 1.5f;
                float _6535 = fresnel_dielectric_cos(param_54, param_55);
                float _6539 = roughness * roughness;
                bool _6542 = _9567 > 0.0f;
                bool _6549;
                if (_6542)
                {
                    _6549 = (_6539 * _6539) >= 1.0000000116860974230803549289703e-07f;
                }
                else
                {
                    _6549 = _6542;
                }
                [branch]
                if (_6549 && (_6146 > 0.0f))
                {
                    float3 param_56 = T;
                    float3 param_57 = B;
                    float3 param_58 = N;
                    float3 param_59 = -_4804;
                    float3 param_60 = T;
                    float3 param_61 = B;
                    float3 param_62 = N;
                    float3 param_63 = _9564;
                    float3 param_64 = T;
                    float3 param_65 = B;
                    float3 param_66 = N;
                    float3 param_67 = normalize(_9564 - _4804);
                    float3 param_68 = tangent_from_world(param_56, param_57, param_58, param_59);
                    float3 param_69 = tangent_from_world(param_64, param_65, param_66, param_67);
                    float3 param_70 = tangent_from_world(param_60, param_61, param_62, param_63);
                    float param_71 = _6539;
                    float param_72 = _6539;
                    float param_73 = 1.5f;
                    float param_74 = _6535;
                    float3 param_75 = base_color;
                    float4 _6609 = Evaluate_GGXSpecular_BSDF(param_68, param_69, param_70, param_71, param_72, param_73, param_74, param_75);
                    float mis_weight_1 = 1.0f;
                    if (_9565 > 0.0f)
                    {
                        float param_76 = _9567;
                        float param_77 = _6609.w;
                        mis_weight_1 = power_heuristic(param_76, param_77);
                    }
                    float3 _6637 = (_9563 * _6609.xyz) * ((mix_weight * mis_weight_1) / _9567);
                    [branch]
                    if (_9568 > 0.5f)
                    {
                        float3 param_78 = _4927;
                        float3 param_79 = plane_N;
                        float3 _6648 = offset_ray(param_78, param_79);
                        uint _6695;
                        _6376.InterlockedAdd(8, 1u, _6695);
                        _6386.Store(_6695 * 44 + 0, asuint(_6648.x));
                        _6386.Store(_6695 * 44 + 4, asuint(_6648.y));
                        _6386.Store(_6695 * 44 + 8, asuint(_6648.z));
                        _6386.Store(_6695 * 44 + 12, asuint(_9564.x));
                        _6386.Store(_6695 * 44 + 16, asuint(_9564.y));
                        _6386.Store(_6695 * 44 + 20, asuint(_9564.z));
                        _6386.Store(_6695 * 44 + 24, asuint(_9566 - 9.9999997473787516355514526367188e-05f));
                        _6386.Store(_6695 * 44 + 28, asuint(ray.c[0] * _6637.x));
                        _6386.Store(_6695 * 44 + 32, asuint(ray.c[1] * _6637.y));
                        _6386.Store(_6695 * 44 + 36, asuint(ray.c[2] * _6637.z));
                        _6386.Store(_6695 * 44 + 40, uint(ray.xy));
                    }
                    else
                    {
                        col += _6637;
                    }
                }
                bool _6734 = _5761 < _3224_g_params.max_spec_depth;
                bool _6741;
                if (_6734)
                {
                    _6741 = _5780 < _3224_g_params.max_total_depth;
                }
                else
                {
                    _6741 = _6734;
                }
                [branch]
                if (_6741)
                {
                    float3 param_80 = T;
                    float3 param_81 = B;
                    float3 param_82 = N;
                    float3 param_83 = _4804;
                    float3 param_84;
                    float4 _6760 = Sample_GGXSpecular_BSDF(param_80, param_81, param_82, param_83, roughness, 0.0f, 1.5f, _6535, base_color, _6235, _6245, param_84);
                    _9601 = ray.ray_depth + 256;
                    float3 param_85 = _4927;
                    float3 param_86 = plane_N;
                    float3 _6772 = offset_ray(param_85, param_86);
                    _9880 = _6772.x;
                    _9881 = _6772.y;
                    _9882 = _6772.z;
                    _9887 = param_84.x;
                    _9888 = param_84.y;
                    _9889 = param_84.z;
                    _9894 = ((ray.c[0] * _6760.x) * mix_weight) / _6760.w;
                    _9895 = ((ray.c[1] * _6760.y) * mix_weight) / _6760.w;
                    _9896 = ((ray.c[2] * _6760.z) * mix_weight) / _6760.w;
                    _9596 = _6760.w;
                }
            }
            else
            {
                [branch]
                if (_9466 == 2u)
                {
                    float _6835;
                    if (_5228)
                    {
                        _6835 = _9469 / _9470;
                    }
                    else
                    {
                        _6835 = _9470 / _9469;
                    }
                    float _6853 = roughness * roughness;
                    bool _6856 = _9567 > 0.0f;
                    bool _6863;
                    if (_6856)
                    {
                        _6863 = (_6853 * _6853) >= 1.0000000116860974230803549289703e-07f;
                    }
                    else
                    {
                        _6863 = _6856;
                    }
                    [branch]
                    if (_6863 && (_6146 < 0.0f))
                    {
                        float3 param_87 = T;
                        float3 param_88 = B;
                        float3 param_89 = N;
                        float3 param_90 = -_4804;
                        float3 param_91 = T;
                        float3 param_92 = B;
                        float3 param_93 = N;
                        float3 param_94 = _9564;
                        float3 param_95 = T;
                        float3 param_96 = B;
                        float3 param_97 = N;
                        float3 param_98 = normalize(_9564 - (_4804 * _6835));
                        float3 param_99 = tangent_from_world(param_87, param_88, param_89, param_90);
                        float3 param_100 = tangent_from_world(param_95, param_96, param_97, param_98);
                        float3 param_101 = tangent_from_world(param_91, param_92, param_93, param_94);
                        float param_102 = _6853;
                        float param_103 = _6835;
                        float3 param_104 = base_color;
                        float4 _6922 = Evaluate_GGXRefraction_BSDF(param_99, param_100, param_101, param_102, param_103, param_104);
                        float mis_weight_2 = 1.0f;
                        if (_9565 > 0.0f)
                        {
                            float param_105 = _9567;
                            float param_106 = _6922.w;
                            mis_weight_2 = power_heuristic(param_105, param_106);
                        }
                        float3 _6950 = (_9563 * _6922.xyz) * ((mix_weight * mis_weight_2) / _9567);
                        [branch]
                        if (_9568 > 0.5f)
                        {
                            float3 param_107 = _4927;
                            float3 param_108 = -plane_N;
                            float3 _6962 = offset_ray(param_107, param_108);
                            uint _7009;
                            _6376.InterlockedAdd(8, 1u, _7009);
                            _6386.Store(_7009 * 44 + 0, asuint(_6962.x));
                            _6386.Store(_7009 * 44 + 4, asuint(_6962.y));
                            _6386.Store(_7009 * 44 + 8, asuint(_6962.z));
                            _6386.Store(_7009 * 44 + 12, asuint(_9564.x));
                            _6386.Store(_7009 * 44 + 16, asuint(_9564.y));
                            _6386.Store(_7009 * 44 + 20, asuint(_9564.z));
                            _6386.Store(_7009 * 44 + 24, asuint(_9566 - 9.9999997473787516355514526367188e-05f));
                            _6386.Store(_7009 * 44 + 28, asuint(ray.c[0] * _6950.x));
                            _6386.Store(_7009 * 44 + 32, asuint(ray.c[1] * _6950.y));
                            _6386.Store(_7009 * 44 + 36, asuint(ray.c[2] * _6950.z));
                            _6386.Store(_7009 * 44 + 40, uint(ray.xy));
                        }
                        else
                        {
                            col += _6950;
                        }
                    }
                    bool _7048 = _5766 < _3224_g_params.max_refr_depth;
                    bool _7055;
                    if (_7048)
                    {
                        _7055 = _5780 < _3224_g_params.max_total_depth;
                    }
                    else
                    {
                        _7055 = _7048;
                    }
                    [branch]
                    if (_7055)
                    {
                        float3 param_109 = T;
                        float3 param_110 = B;
                        float3 param_111 = N;
                        float3 param_112 = _4804;
                        float param_113 = roughness;
                        float param_114 = _6835;
                        float3 param_115 = base_color;
                        float param_116 = _6235;
                        float param_117 = _6245;
                        float4 param_118;
                        float4 _7079 = Sample_GGXRefraction_BSDF(param_109, param_110, param_111, param_112, param_113, param_114, param_115, param_116, param_117, param_118);
                        _9601 = ray.ray_depth + 65536;
                        _9894 = ((ray.c[0] * _7079.x) * mix_weight) / _7079.w;
                        _9895 = ((ray.c[1] * _7079.y) * mix_weight) / _7079.w;
                        _9896 = ((ray.c[2] * _7079.z) * mix_weight) / _7079.w;
                        _9596 = _7079.w;
                        float3 param_119 = _4927;
                        float3 param_120 = -plane_N;
                        float3 _7134 = offset_ray(param_119, param_120);
                        _9880 = _7134.x;
                        _9881 = _7134.y;
                        _9882 = _7134.z;
                        _9887 = param_118.x;
                        _9888 = param_118.y;
                        _9889 = param_118.z;
                    }
                }
                else
                {
                    [branch]
                    if (_9466 == 3u)
                    {
                        float mis_weight_3 = 1.0f;
                        if ((_9465 & 4u) != 0u)
                        {
                            float3 env_col_1 = _3224_g_params.env_col.xyz;
                            uint _7173 = asuint(_3224_g_params.env_col.w);
                            if (_7173 != 4294967295u)
                            {
                                atlas_texture_t _7180;
                                _7180.size = _855.Load(_7173 * 80 + 0);
                                _7180.atlas = _855.Load(_7173 * 80 + 4);
                                [unroll]
                                for (int _117ident = 0; _117ident < 4; _117ident++)
                                {
                                    _7180.page[_117ident] = _855.Load(_117ident * 4 + _7173 * 80 + 8);
                                }
                                [unroll]
                                for (int _118ident = 0; _118ident < 14; _118ident++)
                                {
                                    _7180.pos[_118ident] = _855.Load(_118ident * 4 + _7173 * 80 + 24);
                                }
                                uint _10001[14] = { _7180.pos[0], _7180.pos[1], _7180.pos[2], _7180.pos[3], _7180.pos[4], _7180.pos[5], _7180.pos[6], _7180.pos[7], _7180.pos[8], _7180.pos[9], _7180.pos[10], _7180.pos[11], _7180.pos[12], _7180.pos[13] };
                                uint _9972[4] = { _7180.page[0], _7180.page[1], _7180.page[2], _7180.page[3] };
                                atlas_texture_t _9766 = { _7180.size, _7180.atlas, _9972, _10001 };
                                float param_121 = _3224_g_params.env_rotation;
                                env_col_1 *= SampleLatlong_RGBE(_9766, _4804, param_121);
                            }
                            base_color *= env_col_1;
                        }
                        [branch]
                        if ((_9465 & 5u) != 0u)
                        {
                            float3 _7282 = mul(float4(_5555, 0.0f), _5307.xform).xyz;
                            float _7285 = length(_7282);
                            float _7297 = abs(dot(_4804, _7282 / _7285.xxx));
                            if (_7297 > 0.0f)
                            {
                                float param_122 = ray.pdf;
                                float param_123 = (inter.t * inter.t) / ((0.5f * _7285) * _7297);
                                mis_weight_3 = power_heuristic(param_122, param_123);
                            }
                        }
                        col += (base_color * ((mix_weight * mis_weight_3) * _9467));
                    }
                    else
                    {
                        [branch]
                        if (_9466 == 5u)
                        {
                            bool _7339 = _5772 < _3224_g_params.max_transp_depth;
                            bool _7346;
                            if (_7339)
                            {
                                _7346 = _5780 < _3224_g_params.max_total_depth;
                            }
                            else
                            {
                                _7346 = _7339;
                            }
                            [branch]
                            if (_7346)
                            {
                                _9601 = ray.ray_depth + 16777216;
                                _9596 = ray.pdf;
                                float3 param_124 = _4927;
                                float3 param_125 = -plane_N;
                                float3 _7363 = offset_ray(param_124, param_125);
                                _9880 = _7363.x;
                                _9881 = _7363.y;
                                _9882 = _7363.z;
                                _9887 = ray.d[0];
                                _9888 = ray.d[1];
                                _9889 = ray.d[2];
                                _9894 = ray.c[0];
                                _9895 = ray.c[1];
                                _9896 = ray.c[2];
                            }
                        }
                        else
                        {
                            if (_9466 == 6u)
                            {
                                float metallic = clamp(float((_9472 >> uint(16)) & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f);
                                [branch]
                                if (_9828 != 4294967295u)
                                {
                                    metallic *= SampleBilinear(_9828, _5539, int(get_texture_lod(texSize(_9828), _5737))).x;
                                }
                                float specular = clamp(float(_9474 & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f);
                                [branch]
                                if (_9829 != 4294967295u)
                                {
                                    specular *= SampleBilinear(_9829, _5539, int(get_texture_lod(texSize(_9829), _5737))).x;
                                }
                                float _7473 = clamp(float(_9475 & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f);
                                float _7481 = clamp(float((_9475 >> uint(16)) & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f);
                                float _7488 = clamp(float(_9471 & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f);
                                float3 _7510 = lerp(lerp(1.0f.xxx, tint_color, clamp(float((_9474 >> uint(16)) & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f).xxx) * (specular * 0.07999999821186065673828125f), base_color, metallic.xxx);
                                float _7517 = (2.0f / (1.0f - sqrt(0.07999999821186065673828125f * specular))) - 1.0f;
                                float param_126 = 1.0f;
                                float param_127 = _7517;
                                float _7522 = fresnel_dielectric_cos(param_126, param_127);
                                float param_128 = dot(_4804, N);
                                float param_129 = _7517;
                                float param_130;
                                float param_131;
                                float param_132;
                                float param_133;
                                get_lobe_weights(lerp(_6190, 1.0f, _7488), lum(lerp(_7510, 1.0f.xxx, ((fresnel_dielectric_cos(param_128, param_129) - _7522) / (1.0f - _7522)).xxx)), specular, metallic, clamp(float(_9473 & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f), _7473, param_130, param_131, param_132, param_133);
                                float3 _7576 = lerp(1.0f.xxx, tint_color, clamp(float((_9471 >> uint(16)) & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f).xxx) * _7488;
                                float _7579;
                                if (_5228)
                                {
                                    _7579 = _9469 / _9470;
                                }
                                else
                                {
                                    _7579 = _9470 / _9469;
                                }
                                float param_134 = dot(_4804, N);
                                float param_135 = 1.0f / _7579;
                                float _7602 = fresnel_dielectric_cos(param_134, param_135);
                                float _7609 = (2.0f / (1.0f - sqrt(0.07999999821186065673828125f * _7473))) - 1.0f;
                                float param_136 = 1.0f;
                                float param_137 = _7609;
                                float _7614 = fresnel_dielectric_cos(param_136, param_137);
                                float _7618 = _7481 * _7481;
                                float _7631 = mad(roughness - 1.0f, 1.0f - clamp(float((_9473 >> uint(16)) & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f), 1.0f);
                                float _7635 = _7631 * _7631;
                                [branch]
                                if (_9567 > 0.0f)
                                {
                                    float3 lcol_1 = 0.0f.xxx;
                                    float bsdf_pdf = 0.0f;
                                    bool _7646 = _6146 > 0.0f;
                                    [branch]
                                    if ((param_130 > 1.0000000116860974230803549289703e-07f) && _7646)
                                    {
                                        float3 param_138 = -_4804;
                                        float3 param_139 = N;
                                        float3 param_140 = _9564;
                                        float param_141 = roughness;
                                        float3 param_142 = base_color.xyz;
                                        float3 param_143 = _7576;
                                        bool param_144 = false;
                                        float4 _7666 = Evaluate_PrincipledDiffuse_BSDF(param_138, param_139, param_140, param_141, param_142, param_143, param_144);
                                        bsdf_pdf = mad(param_130, _7666.w, bsdf_pdf);
                                        lcol_1 += (((_9563 * _6146) * (_7666 * (1.0f - metallic)).xyz) / (3.1415927410125732421875f * _9567).xxx);
                                    }
                                    float3 H;
                                    [flatten]
                                    if (_7646)
                                    {
                                        H = normalize(_9564 - _4804);
                                    }
                                    else
                                    {
                                        H = normalize(_9564 - (_4804 * _7579));
                                    }
                                    float _7712 = roughness * roughness;
                                    float _7723 = sqrt(mad(-0.89999997615814208984375f, clamp(float((_9468 >> uint(16)) & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f), 1.0f));
                                    float _7727 = _7712 / _7723;
                                    float _7731 = _7712 * _7723;
                                    float3 param_145 = T;
                                    float3 param_146 = B;
                                    float3 param_147 = N;
                                    float3 param_148 = -_4804;
                                    float3 _7742 = tangent_from_world(param_145, param_146, param_147, param_148);
                                    float3 param_149 = T;
                                    float3 param_150 = B;
                                    float3 param_151 = N;
                                    float3 param_152 = _9564;
                                    float3 _7753 = tangent_from_world(param_149, param_150, param_151, param_152);
                                    float3 param_153 = T;
                                    float3 param_154 = B;
                                    float3 param_155 = N;
                                    float3 param_156 = H;
                                    float3 _7763 = tangent_from_world(param_153, param_154, param_155, param_156);
                                    bool _7765 = param_131 > 0.0f;
                                    bool _7772;
                                    if (_7765)
                                    {
                                        _7772 = (_7727 * _7731) >= 1.0000000116860974230803549289703e-07f;
                                    }
                                    else
                                    {
                                        _7772 = _7765;
                                    }
                                    [branch]
                                    if (_7772 && _7646)
                                    {
                                        float3 param_157 = _7742;
                                        float3 param_158 = _7763;
                                        float3 param_159 = _7753;
                                        float param_160 = _7727;
                                        float param_161 = _7731;
                                        float param_162 = _7517;
                                        float param_163 = _7522;
                                        float3 param_164 = _7510;
                                        float4 _7795 = Evaluate_GGXSpecular_BSDF(param_157, param_158, param_159, param_160, param_161, param_162, param_163, param_164);
                                        bsdf_pdf = mad(param_131, _7795.w, bsdf_pdf);
                                        lcol_1 += ((_9563 * _7795.xyz) / _9567.xxx);
                                    }
                                    bool _7814 = param_132 > 0.0f;
                                    bool _7821;
                                    if (_7814)
                                    {
                                        _7821 = (_7618 * _7618) >= 1.0000000116860974230803549289703e-07f;
                                    }
                                    else
                                    {
                                        _7821 = _7814;
                                    }
                                    [branch]
                                    if (_7821 && _7646)
                                    {
                                        float3 param_165 = _7742;
                                        float3 param_166 = _7763;
                                        float3 param_167 = _7753;
                                        float param_168 = _7618;
                                        float param_169 = _7609;
                                        float param_170 = _7614;
                                        float4 _7840 = Evaluate_PrincipledClearcoat_BSDF(param_165, param_166, param_167, param_168, param_169, param_170);
                                        bsdf_pdf = mad(param_132, _7840.w, bsdf_pdf);
                                        lcol_1 += (((_9563 * 0.25f) * _7840.xyz) / _9567.xxx);
                                    }
                                    [branch]
                                    if (param_133 > 0.0f)
                                    {
                                        bool _7864 = _7602 != 0.0f;
                                        bool _7871;
                                        if (_7864)
                                        {
                                            _7871 = (_7712 * _7712) >= 1.0000000116860974230803549289703e-07f;
                                        }
                                        else
                                        {
                                            _7871 = _7864;
                                        }
                                        [branch]
                                        if (_7871 && _7646)
                                        {
                                            float3 param_171 = _7742;
                                            float3 param_172 = _7763;
                                            float3 param_173 = _7753;
                                            float param_174 = _7712;
                                            float param_175 = _7712;
                                            float param_176 = 1.0f;
                                            float param_177 = 0.0f;
                                            float3 param_178 = 1.0f.xxx;
                                            float4 _7891 = Evaluate_GGXSpecular_BSDF(param_171, param_172, param_173, param_174, param_175, param_176, param_177, param_178);
                                            bsdf_pdf = mad(param_133 * _7602, _7891.w, bsdf_pdf);
                                            lcol_1 += ((_9563 * _7891.xyz) * (_7602 / _9567));
                                        }
                                        bool _7913 = _7602 != 1.0f;
                                        bool _7920;
                                        if (_7913)
                                        {
                                            _7920 = (_7635 * _7635) >= 1.0000000116860974230803549289703e-07f;
                                        }
                                        else
                                        {
                                            _7920 = _7913;
                                        }
                                        [branch]
                                        if (_7920 && (_6146 < 0.0f))
                                        {
                                            float3 param_179 = _7742;
                                            float3 param_180 = _7763;
                                            float3 param_181 = _7753;
                                            float param_182 = _7635;
                                            float param_183 = _7579;
                                            float3 param_184 = base_color;
                                            float4 _7939 = Evaluate_GGXRefraction_BSDF(param_179, param_180, param_181, param_182, param_183, param_184);
                                            float _7942 = 1.0f - _7602;
                                            bsdf_pdf = mad(param_133 * _7942, _7939.w, bsdf_pdf);
                                            lcol_1 += ((_9563 * _7939.xyz) * (_7942 / _9567));
                                        }
                                    }
                                    float mis_weight_4 = 1.0f;
                                    [flatten]
                                    if (_9565 > 0.0f)
                                    {
                                        float param_185 = _9567;
                                        float param_186 = bsdf_pdf;
                                        mis_weight_4 = power_heuristic(param_185, param_186);
                                    }
                                    lcol_1 *= (mix_weight * mis_weight_4);
                                    [branch]
                                    if (_9568 > 0.5f)
                                    {
                                        float3 _7987;
                                        if (_6146 < 0.0f)
                                        {
                                            _7987 = -plane_N;
                                        }
                                        else
                                        {
                                            _7987 = plane_N;
                                        }
                                        float3 param_187 = _4927;
                                        float3 param_188 = _7987;
                                        float3 _7998 = offset_ray(param_187, param_188);
                                        uint _8045;
                                        _6376.InterlockedAdd(8, 1u, _8045);
                                        _6386.Store(_8045 * 44 + 0, asuint(_7998.x));
                                        _6386.Store(_8045 * 44 + 4, asuint(_7998.y));
                                        _6386.Store(_8045 * 44 + 8, asuint(_7998.z));
                                        _6386.Store(_8045 * 44 + 12, asuint(_9564.x));
                                        _6386.Store(_8045 * 44 + 16, asuint(_9564.y));
                                        _6386.Store(_8045 * 44 + 20, asuint(_9564.z));
                                        _6386.Store(_8045 * 44 + 24, asuint(_9566 - 9.9999997473787516355514526367188e-05f));
                                        _6386.Store(_8045 * 44 + 28, asuint(ray.c[0] * lcol_1.x));
                                        _6386.Store(_8045 * 44 + 32, asuint(ray.c[1] * lcol_1.y));
                                        _6386.Store(_8045 * 44 + 36, asuint(ray.c[2] * lcol_1.z));
                                        _6386.Store(_8045 * 44 + 40, uint(ray.xy));
                                    }
                                    else
                                    {
                                        col += lcol_1;
                                    }
                                }
                                [branch]
                                if (mix_rand < param_130)
                                {
                                    bool _8089 = _5756 < _3224_g_params.max_diff_depth;
                                    bool _8096;
                                    if (_8089)
                                    {
                                        _8096 = _5780 < _3224_g_params.max_total_depth;
                                    }
                                    else
                                    {
                                        _8096 = _8089;
                                    }
                                    if (_8096)
                                    {
                                        float3 param_189 = T;
                                        float3 param_190 = B;
                                        float3 param_191 = N;
                                        float3 param_192 = _4804;
                                        float param_193 = roughness;
                                        float3 param_194 = base_color.xyz;
                                        float3 param_195 = _7576;
                                        bool param_196 = false;
                                        float param_197 = _6235;
                                        float param_198 = _6245;
                                        float3 param_199;
                                        float4 _8121 = Sample_PrincipledDiffuse_BSDF(param_189, param_190, param_191, param_192, param_193, param_194, param_195, param_196, param_197, param_198, param_199);
                                        float3 _8127 = _8121.xyz * (1.0f - metallic);
                                        _9601 = ray.ray_depth + 1;
                                        float3 param_200 = _4927;
                                        float3 param_201 = plane_N;
                                        float3 _8143 = offset_ray(param_200, param_201);
                                        _9880 = _8143.x;
                                        _9881 = _8143.y;
                                        _9882 = _8143.z;
                                        _9887 = param_199.x;
                                        _9888 = param_199.y;
                                        _9889 = param_199.z;
                                        _9894 = ((ray.c[0] * _8127.x) * mix_weight) / param_130;
                                        _9895 = ((ray.c[1] * _8127.y) * mix_weight) / param_130;
                                        _9896 = ((ray.c[2] * _8127.z) * mix_weight) / param_130;
                                        _9596 = _8121.w;
                                    }
                                }
                                else
                                {
                                    float _8199 = param_130 + param_131;
                                    [branch]
                                    if (mix_rand < _8199)
                                    {
                                        bool _8206 = _5761 < _3224_g_params.max_spec_depth;
                                        bool _8213;
                                        if (_8206)
                                        {
                                            _8213 = _5780 < _3224_g_params.max_total_depth;
                                        }
                                        else
                                        {
                                            _8213 = _8206;
                                        }
                                        if (_8213)
                                        {
                                            float3 param_202 = T;
                                            float3 param_203 = B;
                                            float3 param_204 = N;
                                            float3 param_205 = _4804;
                                            float3 param_206;
                                            float4 _8240 = Sample_GGXSpecular_BSDF(param_202, param_203, param_204, param_205, roughness, clamp(float((_9468 >> uint(16)) & 65535u) * 1.525902189314365386962890625e-05f, 0.0f, 1.0f), _7517, _7522, _7510, _6235, _6245, param_206);
                                            float _8245 = _8240.w * param_131;
                                            _9601 = ray.ray_depth + 256;
                                            _9894 = ((ray.c[0] * _8240.x) * mix_weight) / _8245;
                                            _9895 = ((ray.c[1] * _8240.y) * mix_weight) / _8245;
                                            _9896 = ((ray.c[2] * _8240.z) * mix_weight) / _8245;
                                            _9596 = _8245;
                                            float3 param_207 = _4927;
                                            float3 param_208 = plane_N;
                                            float3 _8292 = offset_ray(param_207, param_208);
                                            _9880 = _8292.x;
                                            _9881 = _8292.y;
                                            _9882 = _8292.z;
                                            _9887 = param_206.x;
                                            _9888 = param_206.y;
                                            _9889 = param_206.z;
                                        }
                                    }
                                    else
                                    {
                                        float _8317 = _8199 + param_132;
                                        [branch]
                                        if (mix_rand < _8317)
                                        {
                                            bool _8324 = _5761 < _3224_g_params.max_spec_depth;
                                            bool _8331;
                                            if (_8324)
                                            {
                                                _8331 = _5780 < _3224_g_params.max_total_depth;
                                            }
                                            else
                                            {
                                                _8331 = _8324;
                                            }
                                            if (_8331)
                                            {
                                                float3 param_209 = T;
                                                float3 param_210 = B;
                                                float3 param_211 = N;
                                                float3 param_212 = _4804;
                                                float param_213 = _7618;
                                                float param_214 = _7609;
                                                float param_215 = _7614;
                                                float param_216 = _6235;
                                                float param_217 = _6245;
                                                float3 param_218;
                                                float4 _8355 = Sample_PrincipledClearcoat_BSDF(param_209, param_210, param_211, param_212, param_213, param_214, param_215, param_216, param_217, param_218);
                                                float _8360 = _8355.w * param_132;
                                                _9601 = ray.ray_depth + 256;
                                                _9894 = (((0.25f * ray.c[0]) * _8355.x) * mix_weight) / _8360;
                                                _9895 = (((0.25f * ray.c[1]) * _8355.y) * mix_weight) / _8360;
                                                _9896 = (((0.25f * ray.c[2]) * _8355.z) * mix_weight) / _8360;
                                                _9596 = _8360;
                                                float3 param_219 = _4927;
                                                float3 param_220 = plane_N;
                                                float3 _8410 = offset_ray(param_219, param_220);
                                                _9880 = _8410.x;
                                                _9881 = _8410.y;
                                                _9882 = _8410.z;
                                                _9887 = param_218.x;
                                                _9888 = param_218.y;
                                                _9889 = param_218.z;
                                            }
                                        }
                                        else
                                        {
                                            bool _8432 = mix_rand >= _7602;
                                            bool _8439;
                                            if (_8432)
                                            {
                                                _8439 = _5766 < _3224_g_params.max_refr_depth;
                                            }
                                            else
                                            {
                                                _8439 = _8432;
                                            }
                                            bool _8453;
                                            if (!_8439)
                                            {
                                                bool _8445 = mix_rand < _7602;
                                                bool _8452;
                                                if (_8445)
                                                {
                                                    _8452 = _5761 < _3224_g_params.max_spec_depth;
                                                }
                                                else
                                                {
                                                    _8452 = _8445;
                                                }
                                                _8453 = _8452;
                                            }
                                            else
                                            {
                                                _8453 = _8439;
                                            }
                                            bool _8460;
                                            if (_8453)
                                            {
                                                _8460 = _5780 < _3224_g_params.max_total_depth;
                                            }
                                            else
                                            {
                                                _8460 = _8453;
                                            }
                                            [branch]
                                            if (_8460)
                                            {
                                                float _8468 = mix_rand;
                                                float _8472 = (_8468 - _8317) / param_133;
                                                mix_rand = _8472;
                                                float4 F;
                                                float3 V;
                                                [branch]
                                                if (_8472 < _7602)
                                                {
                                                    float3 param_221 = T;
                                                    float3 param_222 = B;
                                                    float3 param_223 = N;
                                                    float3 param_224 = _4804;
                                                    float3 param_225;
                                                    float4 _8492 = Sample_GGXSpecular_BSDF(param_221, param_222, param_223, param_224, roughness, 0.0f, 1.0f, 0.0f, 1.0f.xxx, _6235, _6245, param_225);
                                                    V = param_225;
                                                    F = _8492;
                                                    _9601 = ray.ray_depth + 256;
                                                    float3 param_226 = _4927;
                                                    float3 param_227 = plane_N;
                                                    float3 _8503 = offset_ray(param_226, param_227);
                                                    _9880 = _8503.x;
                                                    _9881 = _8503.y;
                                                    _9882 = _8503.z;
                                                }
                                                else
                                                {
                                                    float3 param_228 = T;
                                                    float3 param_229 = B;
                                                    float3 param_230 = N;
                                                    float3 param_231 = _4804;
                                                    float param_232 = _7631;
                                                    float param_233 = _7579;
                                                    float3 param_234 = base_color;
                                                    float param_235 = _6235;
                                                    float param_236 = _6245;
                                                    float4 param_237;
                                                    float4 _8534 = Sample_GGXRefraction_BSDF(param_228, param_229, param_230, param_231, param_232, param_233, param_234, param_235, param_236, param_237);
                                                    F = _8534;
                                                    V = param_237.xyz;
                                                    _9601 = ray.ray_depth + 65536;
                                                    float3 param_238 = _4927;
                                                    float3 param_239 = -plane_N;
                                                    float3 _8548 = offset_ray(param_238, param_239);
                                                    _9880 = _8548.x;
                                                    _9881 = _8548.y;
                                                    _9882 = _8548.z;
                                                }
                                                float4 _10332 = F;
                                                float _8561 = _10332.w * param_133;
                                                float4 _10334 = _10332;
                                                _10334.w = _8561;
                                                F = _10334;
                                                _9894 = ((ray.c[0] * _10332.x) * mix_weight) / _8561;
                                                _9895 = ((ray.c[1] * _10332.y) * mix_weight) / _8561;
                                                _9896 = ((ray.c[2] * _10332.z) * mix_weight) / _8561;
                                                _9596 = _8561;
                                                _9887 = V.x;
                                                _9888 = V.y;
                                                _9889 = V.z;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        float _8621 = max(_9894, max(_9895, _9896));
        float _8634;
        if (_5780 >= _3224_g_params.termination_start_depth)
        {
            _8634 = max(0.0500000007450580596923828125f, 1.0f - _8621);
        }
        else
        {
            _8634 = 0.0f;
        }
        bool _8648 = (frac(asfloat(_3219.Load((_3224_g_params.hi + 6) * 4 + 0)) + _5743) >= _8634) && (_8621 > 0.0f);
        bool _8654;
        if (_8648)
        {
            _8654 = _9596 > 0.0f;
        }
        else
        {
            _8654 = _8648;
        }
        [branch]
        if (_8654)
        {
            float _8658 = 1.0f - _8634;
            float _8660 = _9894;
            float _8661 = _8660 / _8658;
            _9894 = _8661;
            float _8666 = _9895;
            float _8667 = _8666 / _8658;
            _9895 = _8667;
            float _8672 = _9896;
            float _8673 = _8672 / _8658;
            _9896 = _8673;
            uint _8677;
            _6376.InterlockedAdd(0, 1u, _8677);
            _8685.Store(_8677 * 56 + 0, asuint(_9880));
            _8685.Store(_8677 * 56 + 4, asuint(_9881));
            _8685.Store(_8677 * 56 + 8, asuint(_9882));
            _8685.Store(_8677 * 56 + 12, asuint(_9887));
            _8685.Store(_8677 * 56 + 16, asuint(_9888));
            _8685.Store(_8677 * 56 + 20, asuint(_9889));
            _8685.Store(_8677 * 56 + 24, asuint(_9596));
            _8685.Store(_8677 * 56 + 28, asuint(_8661));
            _8685.Store(_8677 * 56 + 32, asuint(_8667));
            _8685.Store(_8677 * 56 + 36, asuint(_8673));
            _8685.Store(_8677 * 56 + 40, asuint(_5727));
            _8685.Store(_8677 * 56 + 44, asuint(ray.cone_spread));
            _8685.Store(_8677 * 56 + 48, uint(ray.xy));
            _8685.Store(_8677 * 56 + 52, uint(_9601));
        }
        _8905 = float3(ray.c[0] * col.x, ray.c[1] * col.y, ray.c[2] * col.z);
        break;
    } while(false);
    return _8905;
}

void comp_main()
{
    do
    {
        int _8755 = int((gl_WorkGroupID.x * 64u) + gl_LocalInvocationIndex);
        if (uint(_8755) >= _6376.Load(4))
        {
            break;
        }
        int _8771 = int(_8768.Load(_8755 * 56 + 48));
        int _8774 = (_8771 >> 16) & 65535;
        int _8778 = int(_8768.Load(_8755 * 56 + 48));
        int _8779 = _8778 & 65535;
        hit_data_t _8798;
        _8798.mask = int(_8794.Load(_8755 * 24 + 0));
        _8798.obj_index = int(_8794.Load(_8755 * 24 + 4));
        _8798.prim_index = int(_8794.Load(_8755 * 24 + 8));
        _8798.t = asfloat(_8794.Load(_8755 * 24 + 12));
        _8798.u = asfloat(_8794.Load(_8755 * 24 + 16));
        _8798.v = asfloat(_8794.Load(_8755 * 24 + 20));
        ray_data_t _8814;
        [unroll]
        for (int _119ident = 0; _119ident < 3; _119ident++)
        {
            _8814.o[_119ident] = asfloat(_8768.Load(_119ident * 4 + _8755 * 56 + 0));
        }
        [unroll]
        for (int _120ident = 0; _120ident < 3; _120ident++)
        {
            _8814.d[_120ident] = asfloat(_8768.Load(_120ident * 4 + _8755 * 56 + 12));
        }
        _8814.pdf = asfloat(_8768.Load(_8755 * 56 + 24));
        [unroll]
        for (int _121ident = 0; _121ident < 3; _121ident++)
        {
            _8814.c[_121ident] = asfloat(_8768.Load(_121ident * 4 + _8755 * 56 + 28));
        }
        _8814.cone_width = asfloat(_8768.Load(_8755 * 56 + 40));
        _8814.cone_spread = asfloat(_8768.Load(_8755 * 56 + 44));
        _8814.xy = int(_8768.Load(_8755 * 56 + 48));
        _8814.ray_depth = int(_8768.Load(_8755 * 56 + 52));
        int param = (_8779 * int(_3224_g_params.img_size.x)) + _8774;
        hit_data_t _8966 = { _8798.mask, _8798.obj_index, _8798.prim_index, _8798.t, _8798.u, _8798.v };
        hit_data_t param_1 = _8966;
        float _9004[3] = { _8814.c[0], _8814.c[1], _8814.c[2] };
        float _8997[3] = { _8814.d[0], _8814.d[1], _8814.d[2] };
        float _8990[3] = { _8814.o[0], _8814.o[1], _8814.o[2] };
        ray_data_t _8983 = { _8990, _8997, _8814.pdf, _9004, _8814.cone_width, _8814.cone_spread, _8814.xy, _8814.ray_depth };
        ray_data_t param_2 = _8983;
        float3 _8856 = ShadeSurface(param, param_1, param_2);
        int2 _8863 = int2(_8774, _8779);
        g_out_img[_8863] = float4(_8856 + g_out_img[_8863].xyz, 1.0f);
        break;
    } while(false);
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
    gl_WorkGroupID = stage_input.gl_WorkGroupID;
    gl_LocalInvocationIndex = stage_input.gl_LocalInvocationIndex;
    comp_main();
}