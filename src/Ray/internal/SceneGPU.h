#pragma once

#include <cfloat>
#include <climits>

#include "../Log.h"
#include "Atmosphere.h"
#include "BVHSplit.h"
#include "SceneCommon.h"
#include "SparseStorageCPU.h"
#include "TextureUtils.h"

namespace Ray {
namespace NS {
class Context;
class Renderer;

template <class T> force_inline T clamp(const T &val, const T &min_val, const T &max_val) {
    return std::min(std::max(val, min_val), max_val);
}

inline Ref::simd_fvec4 cross(const Ref::simd_fvec4 &v1, const Ref::simd_fvec4 &v2) {
    return Ref::simd_fvec4{v1.get<1>() * v2.get<2>() - v1.get<2>() * v2.get<1>(),
                           v1.get<2>() * v2.get<0>() - v1.get<0>() * v2.get<2>(),
                           v1.get<0>() * v2.get<1>() - v1.get<1>() * v2.get<0>(), 0.0f};
}

inline Ref::simd_fvec4 rgb_to_rgbe(const Ref::simd_fvec4 &rgb) {
    float max_component = std::max(std::max(rgb.get<0>(), rgb.get<1>()), rgb.get<2>());
    if (max_component < 1e-32) {
        return Ref::simd_fvec4{0.0f};
    }

    int exponent;
    const float factor = std::frexp(max_component, &exponent) * 256.0f / max_component;

    return Ref::simd_fvec4{rgb.get<0>() * factor, rgb.get<1>() * factor, rgb.get<2>() * factor, float(exponent + 128)};
}

const eTexFormat g_to_internal_format[] = {
    eTexFormat::Undefined,   // Undefined
    eTexFormat::RawRGBA8888, // RGBA8888
    eTexFormat::RawRGB888,   // RGB888
    eTexFormat::RawRG88,     // RG88
    eTexFormat::RawR8,       // R8
    eTexFormat::BC1,         // BC1
    eTexFormat::BC3,         // BC3
    eTexFormat::BC4,         // BC4
    eTexFormat::BC5          // BC5
};

class Scene : public SceneCommon {
  protected:
    friend class NS::Renderer;

    Context *ctx_;
    bool use_hwrt_ = false, use_bindless_ = false, use_tex_compression_ = false;

    Vector<bvh_node_t> nodes_;
    Vector<tri_accel_t> tris_;
    Vector<uint32_t> tri_indices_;
    Vector<tri_mat_data_t> tri_materials_;
    std::vector<tri_mat_data_t> tri_materials_cpu_;
    SparseStorage<transform_t> transforms_;
    SparseStorage<mesh_t> meshes_;
    SparseStorage<mesh_instance_t> mesh_instances_;
    Vector<uint32_t> mi_indices_;
    Vector<vertex_t> vertices_;
    std::vector<vertex_t> vertices_cpu_;
    Vector<uint32_t> vtx_indices_;
    std::vector<uint32_t> vtx_indices_cpu_;

    SparseStorage<material_t> materials_;
    SparseStorage<atlas_texture_t> atlas_textures_;
    Cpu::SparseStorage<Texture2D> bindless_textures_;

    BindlessTexData bindless_tex_data_;

    TextureAtlas tex_atlases_[8];

    SparseStorage<light_t> lights_;
    Vector<uint32_t> li_indices_;
    Vector<uint32_t> visible_lights_;
    Vector<uint32_t> blocker_lights_;
    Vector<light_wbvh_node_t> light_wnodes_;

    environment_t env_;
    LightHandle env_map_light_ = InvalidLightHandle;
    TextureHandle physical_sky_texture_ = InvalidTextureHandle;
    struct {
        int res = -1;
        SmallVector<aligned_vector<simd_fvec4>, 16> mips;
        Texture2D tex;
    } env_map_qtree_;

    uint32_t macro_nodes_start_ = 0xffffffff, macro_nodes_count_ = 0;

    bvh_node_t tlas_root_node_ = {};

    Buffer rt_blas_buf_, rt_geo_data_buf_, rt_instance_buf_, rt_tlas_buf_;

    struct MeshBlas {
        AccStructure acc;
        uint32_t geo_index, geo_count;
    };
    std::vector<MeshBlas> rt_mesh_blases_;
    AccStructure rt_tlas_;

    MaterialHandle AddMaterial_nolock(const shading_node_desc_t &m);
    void SetMeshInstanceTransform_nolock(MeshInstanceHandle mi_handle, const float *xform);

    void RemoveLight_nolock(LightHandle i);
    void RemoveNodes_nolock(uint32_t node_index, uint32_t node_count);
    void RebuildTLAS_nolock();
    void RebuildLightTree_nolock();

    void PrepareSkyEnvMap_nolock();
    void PrepareEnvMapQTree_nolock();
    void GenerateTextureMips_nolock();
    void PrepareBindlessTextures_nolock();
    void RebuildHWAccStructures_nolock();

    TextureHandle AddAtlasTexture_nolock(const tex_desc_t &t);
    TextureHandle AddBindlessTexture_nolock(const tex_desc_t &t);

    template <typename T, int N>
    static void WriteTextureMips(const color_t<T, N> data[], const int _res[2], int mip_count, bool compress,
                                 uint8_t out_data[], uint32_t out_size[16]);

  public:
    Scene(Context *ctx, bool use_hwrt, bool use_bindless, bool use_tex_compression);
    ~Scene() override;

    void GetEnvironment(environment_desc_t &env) override;
    void SetEnvironment(const environment_desc_t &env) override;

    TextureHandle AddTexture(const tex_desc_t &t) override {
        std::unique_lock<std::shared_timed_mutex> lock(mtx_);
        if (use_bindless_) {
            return AddBindlessTexture_nolock(t);
        } else {
            return AddAtlasTexture_nolock(t);
        }
    }
    void RemoveTexture(const TextureHandle t) override {
        std::unique_lock<std::shared_timed_mutex> lock(mtx_);
        if (use_bindless_) {
            bindless_textures_.Erase(t._block);
        } else {
            atlas_textures_.Erase(t._block);
        }
    }

    MaterialHandle AddMaterial(const shading_node_desc_t &m) override {
        std::unique_lock<std::shared_timed_mutex> lock(mtx_);
        return AddMaterial_nolock(m);
    }
    MaterialHandle AddMaterial(const principled_mat_desc_t &m) override;
    void RemoveMaterial(const MaterialHandle m) override {
        std::unique_lock<std::shared_timed_mutex> lock(mtx_);
        materials_.Erase(m._block);
    }

    MeshHandle AddMesh(const mesh_desc_t &m) override;
    void RemoveMesh(MeshHandle) override;

    LightHandle AddLight(const directional_light_desc_t &l) override;
    LightHandle AddLight(const sphere_light_desc_t &l) override;
    LightHandle AddLight(const spot_light_desc_t &l) override;
    LightHandle AddLight(const rect_light_desc_t &l, const float *xform) override;
    LightHandle AddLight(const disk_light_desc_t &l, const float *xform) override;
    LightHandle AddLight(const line_light_desc_t &l, const float *xform) override;
    void RemoveLight(const LightHandle i) override {
        std::unique_lock<std::shared_timed_mutex> lock(mtx_);
        RemoveLight_nolock(i);
    }

    MeshInstanceHandle AddMeshInstance(const mesh_instance_desc_t &mi) override;
    void SetMeshInstanceTransform(MeshInstanceHandle mi_handle, const float *xform) override {
        std::unique_lock<std::shared_timed_mutex> lock(mtx_);
        SetMeshInstanceTransform_nolock(mi_handle, xform);
    }
    void RemoveMeshInstance(MeshInstanceHandle) override;

    void Finalize() override;

    uint32_t triangle_count() const override {
        std::shared_lock<std::shared_timed_mutex> lock(mtx_);
        return uint32_t(vtx_indices_.size() / 3);
    }
    uint32_t node_count() const override {
        std::shared_lock<std::shared_timed_mutex> lock(mtx_);
        return uint32_t(nodes_.size());
    }
};
} // namespace NS
} // namespace Ray

namespace Ray {
int round_up(int v, int align);
}

inline Ray::NS::Scene::Scene(Context *ctx, const bool use_hwrt, const bool use_bindless, const bool use_tex_compression)
    : ctx_(ctx), use_hwrt_(use_hwrt), use_bindless_(use_bindless), use_tex_compression_(use_tex_compression),
      nodes_(ctx, "Nodes"), tris_(ctx, "Tris"), tri_indices_(ctx, "Tri Indices"), tri_materials_(ctx, "Tri Materials"),
      transforms_(ctx, "Transforms"), meshes_(ctx, "Meshes"), mesh_instances_(ctx, "Mesh Instances"),
      mi_indices_(ctx, "MI Indices"), vertices_(ctx, "Vertices"), vtx_indices_(ctx, "Vtx Indices"),
      materials_(ctx, "Materials"), atlas_textures_(ctx, "Atlas Textures"), bindless_tex_data_{ctx},
      tex_atlases_{
          {ctx, "Atlas RGBA", eTexFormat::RawRGBA8888, eTexFilter::Nearest, TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE},
          {ctx, "Atlas RGB", eTexFormat::RawRGB888, eTexFilter::Nearest, TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE},
          {ctx, "Atlas RG", eTexFormat::RawRG88, eTexFilter::Nearest, TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE},
          {ctx, "Atlas R", eTexFormat::RawR8, eTexFilter::Nearest, TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE},
          {ctx, "Atlas BC1", eTexFormat::BC1, eTexFilter::Nearest, TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE},
          {ctx, "Atlas BC3", eTexFormat::BC3, eTexFilter::Nearest, TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE},
          {ctx, "Atlas BC4", eTexFormat::BC4, eTexFilter::Nearest, TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE},
          {ctx, "Atlas BC5", eTexFormat::BC5, eTexFilter::Nearest, TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE}},
      lights_(ctx, "Lights"), li_indices_(ctx, "LI Indices"), visible_lights_(ctx, "Visible Lights"),
      blocker_lights_(ctx, "Blocker Lights"), light_wnodes_(ctx, "Light WNodes") {
    SceneBase::log_ = ctx->log();
    SetEnvironment({});
}

inline void Ray::NS::Scene::GetEnvironment(environment_desc_t &env) {
    std::shared_lock<std::shared_timed_mutex> lock(mtx_);

    memcpy(env.env_col, env_.env_col, 3 * sizeof(float));
    env.env_map = TextureHandle{env_.env_map};
    memcpy(env.back_col, env_.back_col, 3 * sizeof(float));
    env.back_map = TextureHandle{env_.back_map};
    env.env_map_rotation = env_.env_map_rotation;
    env.back_map_rotation = env_.back_map_rotation;
    env.multiple_importance = env_.multiple_importance;
}

inline void Ray::NS::Scene::SetEnvironment(const environment_desc_t &env) {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    memcpy(env_.env_col, env.env_col, 3 * sizeof(float));
    env_.env_map = env.env_map._index;
    memcpy(env_.back_col, env.back_col, 3 * sizeof(float));
    env_.back_map = env.back_map._index;
    env_.env_map_rotation = env.env_map_rotation;
    env_.back_map_rotation = env.back_map_rotation;
    env_.multiple_importance = env.multiple_importance;
}

inline Ray::TextureHandle Ray::NS::Scene::AddAtlasTexture_nolock(const tex_desc_t &_t) {
    atlas_texture_t t;
    t.width = uint16_t(_t.w);
    t.height = uint16_t(_t.h);

    if (_t.is_srgb) {
        t.width |= ATLAS_TEX_SRGB_BIT;
    }

    if (((_t.generate_mipmaps && !IsCompressedFormat(_t.format)) || _t.mips_count > 1) &&
        _t.w > MIN_ATLAS_TEXTURE_SIZE && _t.h > MIN_ATLAS_TEXTURE_SIZE) {
        t.height |= ATLAS_TEX_MIPS_BIT;
    }

    int res[2] = {_t.w, _t.h};

    const bool use_compression = use_tex_compression_ && !_t.force_no_compression;

    std::unique_ptr<color_rg8_t[]> repacked_normalmap;
    bool reconstruct_z = false;

    const void *tex_data = _t.data.data();

    if (_t.format == eTextureFormat::RGBA8888) {
        if (!_t.is_normalmap) {
            t.atlas = 0;
        } else {
            // TODO: get rid of this allocation
            repacked_normalmap = std::make_unique<color_rg8_t[]>(res[0] * res[1]);
            const bool invert_y = (_t.convention == eTextureConvention::DX);
            const auto *rgba_data = reinterpret_cast<const color_rgba8_t *>(_t.data.data());
            for (int i = 0; i < res[0] * res[1]; ++i) {
                repacked_normalmap[i].v[0] = rgba_data[i].v[0];
                repacked_normalmap[i].v[1] = invert_y ? (255 - rgba_data[i].v[1]) : rgba_data[i].v[1];
                reconstruct_z |= (rgba_data[i].v[2] < 250);
            }

            tex_data = repacked_normalmap.get();
            t.atlas = use_compression ? 7 : 2;
        }
    } else if (_t.format == eTextureFormat::RGB888) {
        if (!_t.is_normalmap) {
            t.atlas = use_compression ? 5 : 1;
            t.height |= ATLAS_TEX_YCOCG_BIT;
        } else {
            // TODO: get rid of this allocation
            repacked_normalmap = std::make_unique<color_rg8_t[]>(res[0] * res[1]);
            const bool invert_y = (_t.convention == eTextureConvention::DX);
            const auto *rgb_data = reinterpret_cast<const color_rgb8_t *>(_t.data.data());
            for (int i = 0; i < res[0] * res[1]; ++i) {
                repacked_normalmap[i].v[0] = rgb_data[i].v[0];
                repacked_normalmap[i].v[1] = invert_y ? (255 - rgb_data[i].v[1]) : rgb_data[i].v[1];
                reconstruct_z |= (rgb_data[i].v[2] < 250);
            }

            tex_data = repacked_normalmap.get();
            t.atlas = use_compression ? 7 : 2;
        }
    } else if (_t.format == eTextureFormat::RG88) {
        t.atlas = use_compression ? 7 : 2;
        reconstruct_z = _t.is_normalmap;

        const bool invert_y = _t.is_normalmap && (_t.convention == eTextureConvention::DX);
        if (invert_y) {
            // TODO: get rid of this allocation
            repacked_normalmap = std::make_unique<color_rg8_t[]>(res[0] * res[1]);
            const auto *rg_data = reinterpret_cast<const color_rg8_t *>(_t.data.data());
            for (int i = 0; i < res[0] * res[1]; ++i) {
                repacked_normalmap[i].v[0] = rg_data[i].v[0];
                repacked_normalmap[i].v[1] = 255 - rg_data[i].v[1];
            }
            tex_data = repacked_normalmap.get();
        }
    } else if (_t.format == eTextureFormat::R8) {
        t.atlas = use_compression ? 6 : 3;
    } else {
        const bool flip_vertical = (_t.convention == eTextureConvention::DX);
        const bool invert_green = (_t.convention == eTextureConvention::DX) && _t.is_normalmap;
        reconstruct_z = _t.is_normalmap && (_t.format == eTextureFormat::BC5);

        int read_offset = 0;
        int res[2] = {_t.w, _t.h};
        // TODO: Get rid of allocation
        std::vector<uint8_t> temp_data;
        for (int i = 0; i < std::min(_t.mips_count, NUM_MIP_LEVELS) && res[0] >= 4 && res[1] >= 4; ++i) {
            if (_t.format == eTextureFormat::BC1) {
                t.atlas = 4;
                temp_data.resize(GetRequiredMemory_BCn<3>(res[0], res[1], 1));
                Preprocess_BCn<3>(&_t.data[read_offset], (res[0] + 3) / 4, (res[1] + 3) / 4, flip_vertical,
                                  invert_green, temp_data.data());
            } else if (_t.format == eTextureFormat::BC3) {
                t.atlas = 5;
                temp_data.resize(GetRequiredMemory_BCn<4>(res[0], res[1], 1));
                Preprocess_BCn<4>(&_t.data[read_offset], (res[0] + 3) / 4, (res[1] + 3) / 4, flip_vertical,
                                  invert_green, temp_data.data());
            } else if (_t.format == eTextureFormat::BC4) {
                t.atlas = 6;
                temp_data.resize(GetRequiredMemory_BCn<1>(res[0], res[1], 1));
                Preprocess_BCn<1>(&_t.data[read_offset], (res[0] + 3) / 4, (res[1] + 3) / 4, flip_vertical,
                                  invert_green, temp_data.data());
            } else if (_t.format == eTextureFormat::BC5) {
                t.atlas = 7;
                temp_data.resize(GetRequiredMemory_BCn<2>(res[0], res[1], 1));
                Preprocess_BCn<2>(&_t.data[read_offset], (res[0] + 3) / 4, (res[1] + 3) / 4, flip_vertical,
                                  invert_green, temp_data.data());
            }

            int pos[2] = {};
            const int page = tex_atlases_[t.atlas].AllocateRaw(temp_data.data(), res, pos);

            t.page[i] = uint8_t(page);
            t.pos[i][0] = uint16_t(pos[0]);
            t.pos[i][1] = uint16_t(pos[1]);

            read_offset += int(temp_data.size());

            res[0] /= 2;
            res[1] /= 2;
        }

        // Fill remaining mip levels
        for (int i = _t.mips_count; i < NUM_MIP_LEVELS; i++) {
            t.page[i] = t.page[_t.mips_count - 1];
            t.pos[i][0] = t.pos[_t.mips_count - 1][0];
            t.pos[i][1] = t.pos[_t.mips_count - 1][1];
        }
    }

    if (reconstruct_z) {
        t.width |= uint32_t(ATLAS_TEX_RECONSTRUCT_Z_BIT);
    }

    if (!IsCompressedFormat(_t.format)) { // Allocate initial mip level
        int page = -1, pos[2] = {};
        if (t.atlas == 0) {
            page = tex_atlases_[0].Allocate<uint8_t, 4>(reinterpret_cast<const color_rgba8_t *>(tex_data), res, pos);
        } else if (t.atlas == 1 || t.atlas == 5) {
            page =
                tex_atlases_[t.atlas].Allocate<uint8_t, 3>(reinterpret_cast<const color_rgb8_t *>(tex_data), res, pos);
        } else if (t.atlas == 2 || t.atlas == 7) {
            page =
                tex_atlases_[t.atlas].Allocate<uint8_t, 2>(reinterpret_cast<const color_rg8_t *>(tex_data), res, pos);
        } else if (t.atlas == 3 || t.atlas == 6) {
            page = tex_atlases_[t.atlas].Allocate<uint8_t, 1>(reinterpret_cast<const color_r8_t *>(tex_data), res, pos);
        }

        if (page == -1) {
            return InvalidTextureHandle;
        }

        t.page[0] = uint8_t(page);
        t.pos[0][0] = uint16_t(pos[0]);
        t.pos[0][1] = uint16_t(pos[1]);
    }

    // Temporarily fill remaining mip levels with the last one (mips will be added later)
    for (int i = 1; i < NUM_MIP_LEVELS && !IsCompressedFormat(_t.format); i++) {
        t.page[i] = t.page[0];
        t.pos[i][0] = t.pos[0][0];
        t.pos[i][1] = t.pos[0][1];
    }

    if (_t.generate_mipmaps && (use_compression || !ctx_->image_blit_supported()) && !IsCompressedFormat(_t.format)) {
        // We have to generate mips here as uncompressed data will be lost

        int pages[16], positions[16][2];
        if (_t.format == eTextureFormat::RGBA8888) {
            tex_atlases_[t.atlas].AllocateMips<uint8_t, 4>(reinterpret_cast<const color_rgba8_t *>(_t.data.data()), res,
                                                           NUM_MIP_LEVELS - 1, pages, positions);
        } else if (_t.format == eTextureFormat::RGB888) {
            tex_atlases_[t.atlas].AllocateMips<uint8_t, 3>(reinterpret_cast<const color_rgb8_t *>(_t.data.data()), res,
                                                           NUM_MIP_LEVELS - 1, pages, positions);
        } else if (_t.format == eTextureFormat::RG88) {
            tex_atlases_[t.atlas].AllocateMips<uint8_t, 2>(reinterpret_cast<const color_rg8_t *>(_t.data.data()), res,
                                                           NUM_MIP_LEVELS - 1, pages, positions);
        } else if (_t.format == eTextureFormat::R8) {
            tex_atlases_[t.atlas].AllocateMips<uint8_t, 1>(reinterpret_cast<const color_r8_t *>(_t.data.data()), res,
                                                           NUM_MIP_LEVELS - 1, pages, positions);
        } else {
            return InvalidTextureHandle;
        }

        for (int i = 1; i < NUM_MIP_LEVELS; i++) {
            t.page[i] = uint8_t(pages[i - 1]);
            t.pos[i][0] = uint16_t(positions[i - 1][0]);
            t.pos[i][1] = uint16_t(positions[i - 1][1]);
        }
    }

    log_->Info("Ray: Texture '%s' loaded (atlas = %i, %ix%i)", _t.name, int(t.atlas), _t.w, _t.h);
    log_->Info("Ray: Atlasses are (RGBA[%i], RGB[%i], RG[%i], R[%i], BC1[%i], BC3[%i], BC4[%i], BC5[%i])",
               tex_atlases_[0].page_count(), tex_atlases_[1].page_count(), tex_atlases_[2].page_count(),
               tex_atlases_[3].page_count(), tex_atlases_[4].page_count(), tex_atlases_[5].page_count(),
               tex_atlases_[6].page_count(), tex_atlases_[7].page_count());

    const std::pair<uint32_t, uint32_t> at = atlas_textures_.push(t);
    return TextureHandle{at.first, at.second};
}

inline Ray::TextureHandle Ray::NS::Scene::AddBindlessTexture_nolock(const tex_desc_t &_t) {
    eTexFormat src_fmt = eTexFormat::Undefined, fmt = eTexFormat::Undefined;
    eTexBlock block = eTexBlock::_None;

    const int expected_mip_count = CalcMipCount(_t.w, _t.h, 4, eTexFilter::Bilinear);
    const int mip_count = (_t.generate_mipmaps && !Ray::IsCompressedFormat(_t.format))
                              ? expected_mip_count
                              : std::min(_t.mips_count, expected_mip_count);

    Buffer temp_stage_buf("Temp stage buf", ctx_, eBufType::Upload,
                          3 * _t.w * _t.h * 4 + 4096 * mip_count); // allocate for worst case
    uint8_t *stage_data = temp_stage_buf.Map();

    bool use_compression = use_tex_compression_ && !_t.force_no_compression;
    use_compression &= CanBeBlockCompressed(_t.w, _t.h, mip_count, eTexBlock::_4x4);

    uint32_t data_size[16] = {};

    std::unique_ptr<uint8_t[]> repacked_data;
    bool reconstruct_z = false, is_YCoCg = false;

    if (_t.format == eTextureFormat::RGBA8888) {
        if (!_t.is_normalmap) {
            src_fmt = fmt = eTexFormat::RawRGBA8888;
            data_size[0] = round_up(_t.w * 4, TextureDataPitchAlignment) * _t.h;

            const auto *rgba_data = reinterpret_cast<const color_rgba8_t *>(_t.data.data());

            int j = 0;
            for (int y = 0; y < _t.h; ++y) {
                memcpy(&stage_data[j], &rgba_data[y * _t.w], _t.w * 4);
                j += round_up(_t.w * 4, TextureDataPitchAlignment);
            }
        } else {
            // TODO: get rid of this allocation
            repacked_data = std::make_unique<uint8_t[]>(2 * _t.w * _t.h);

            const bool invert_y = (_t.convention == Ray::eTextureConvention::DX);
            const auto *rgba_data = reinterpret_cast<const color_rgba8_t *>(_t.data.data());
            for (int i = 0; i < _t.w * _t.h; ++i) {
                repacked_data[i * 2 + 0] = rgba_data[i].v[0];
                repacked_data[i * 2 + 1] = invert_y ? (255 - rgba_data[i].v[1]) : rgba_data[i].v[1];
                reconstruct_z |= (rgba_data[i].v[2] < 250);
            }

            if (use_compression) {
                src_fmt = eTexFormat::RawRG88;
                fmt = eTexFormat::BC5;
                block = eTexBlock::_4x4;
                data_size[0] = GetRequiredMemory_BC5(_t.w, _t.h, TextureDataPitchAlignment);
                CompressImage_BC5<2>(&repacked_data[0], _t.w, _t.h, stage_data,
                                     GetRequiredMemory_BC5(_t.w, 1, TextureDataPitchAlignment));
            } else {
                src_fmt = fmt = eTexFormat::RawRG88;
                data_size[0] = round_up(_t.w * 2, TextureDataPitchAlignment) * _t.h;

                int j = 0;
                for (int y = 0; y < _t.h; ++y) {
                    memcpy(&stage_data[j], &repacked_data[y * _t.w * 2], _t.w * 2);
                    j += round_up(_t.w * 2, TextureDataPitchAlignment);
                }
            }
        }
    } else if (_t.format == eTextureFormat::RGB888) {
        if (!_t.is_normalmap) {
            if (use_compression) {
                auto temp_YCoCg = ConvertRGB_to_CoCgxY(_t.data.data(), _t.w, _t.h);
                is_YCoCg = true;
                src_fmt = eTexFormat::RawRGB888;
                fmt = eTexFormat::BC3;
                block = eTexBlock::_4x4;
                data_size[0] = GetRequiredMemory_BC3(_t.w, _t.h, TextureDataPitchAlignment);
                CompressImage_BC3<true /* Is_YCoCg */>(temp_YCoCg.get(), _t.w, _t.h, stage_data,
                                                       GetRequiredMemory_BC3(_t.w, 1, TextureDataPitchAlignment));
            } else if (ctx_->rgb8_unorm_is_supported()) {
                src_fmt = fmt = eTexFormat::RawRGB888;
                data_size[0] = round_up(_t.w * 3, TextureDataPitchAlignment) * _t.h;

                const auto *rgb_data = reinterpret_cast<const color_rgb8_t *>(_t.data.data());

                int j = 0;
                for (int y = 0; y < _t.h; ++y) {
                    memcpy(&stage_data[j], &rgb_data[y * _t.w], _t.w * 3);
                    j += round_up(_t.w * 3, TextureDataPitchAlignment);
                }
            } else {
                // Fallback to 4-component texture
                src_fmt = fmt = eTexFormat::RawRGBA8888;
                data_size[0] = round_up(_t.w * 4, TextureDataPitchAlignment) * _t.h;

                // TODO: get rid of this allocation
                repacked_data = std::make_unique<uint8_t[]>(4 * _t.w * _t.h);

                const auto *rgb_data = _t.data.data();

                for (int i = 0; i < _t.w * _t.h; ++i) {
                    repacked_data[i * 4 + 0] = rgb_data[i * 3 + 0];
                    repacked_data[i * 4 + 1] = rgb_data[i * 3 + 1];
                    repacked_data[i * 4 + 2] = rgb_data[i * 3 + 2];
                    repacked_data[i * 4 + 3] = 255;
                }

                int j = 0;
                for (int y = 0; y < _t.h; ++y) {
                    memcpy(&stage_data[j], &repacked_data[y * _t.w * 4], _t.w * 4);
                    j += round_up(_t.w * 4, TextureDataPitchAlignment);
                }
            }
        } else {
            // TODO: get rid of this allocation
            repacked_data = std::make_unique<uint8_t[]>(2 * _t.w * _t.h);

            const bool invert_y = (_t.convention == Ray::eTextureConvention::DX);
            const auto *rgb_data = reinterpret_cast<const color_rgb8_t *>(_t.data.data());
            for (int i = 0; i < _t.w * _t.h; ++i) {
                repacked_data[i * 2 + 0] = rgb_data[i].v[0];
                repacked_data[i * 2 + 1] = invert_y ? (255 - rgb_data[i].v[1]) : rgb_data[i].v[1];
                reconstruct_z |= (rgb_data[i].v[2] < 250);
            }

            if (use_compression) {
                src_fmt = eTexFormat::RawRG88;
                fmt = eTexFormat::BC5;
                block = eTexBlock::_4x4;
                data_size[0] = GetRequiredMemory_BC5(_t.w, _t.h, TextureDataPitchAlignment);
                CompressImage_BC5<2>(&repacked_data[0], _t.w, _t.h, stage_data,
                                     GetRequiredMemory_BC5(_t.w, 1, TextureDataPitchAlignment));
            } else {
                src_fmt = fmt = eTexFormat::RawRG88;
                data_size[0] = round_up(_t.w * 2, TextureDataPitchAlignment) * _t.h;

                int j = 0;
                for (int y = 0; y < _t.h; ++y) {
                    memcpy(&stage_data[j], &repacked_data[y * _t.w * 2], _t.w * 2);
                    j += round_up(_t.w * 2, TextureDataPitchAlignment);
                }
            }
        }
    } else if (_t.format == eTextureFormat::RG88) {
        src_fmt = fmt = eTexFormat::RawRG88;
        data_size[0] = round_up(_t.w * 2, TextureDataPitchAlignment) * _t.h;

        const bool invert_y = _t.is_normalmap && (_t.convention == Ray::eTextureConvention::DX);
        const auto *rg_data = reinterpret_cast<const color_rg8_t *>(_t.data.data());

        int j = 0;
        for (int y = 0; y < _t.h; ++y) {
            auto *dst = reinterpret_cast<color_rg8_t *>(&stage_data[j]);
            for (int x = 0; x < _t.w; ++x) {
                dst[x].v[0] = rg_data[y * _t.w + x].v[0];
                dst[x].v[1] = invert_y ? (255 - rg_data[y * _t.w + x].v[1]) : rg_data[y * _t.w + x].v[1];
            }
            j += round_up(_t.w * 2, TextureDataPitchAlignment);
        }

        reconstruct_z = _t.is_normalmap;
    } else if (_t.format == eTextureFormat::R8) {
        if (use_compression) {
            src_fmt = eTexFormat::RawR8;
            fmt = eTexFormat::BC4;
            block = eTexBlock::_4x4;
            data_size[0] = GetRequiredMemory_BC4(_t.w, _t.h, TextureDataPitchAlignment);
            CompressImage_BC4<1>(_t.data.data(), _t.w, _t.h, stage_data,
                                 GetRequiredMemory_BC4(_t.w, 1, TextureDataPitchAlignment));
        } else {
            src_fmt = fmt = eTexFormat::RawR8;
            data_size[0] = round_up(_t.w, TextureDataPitchAlignment) * _t.h;

            const auto *r_data = reinterpret_cast<const color_r8_t *>(_t.data.data());

            int j = 0;
            for (int y = 0; y < _t.h; ++y) {
                memcpy(&stage_data[j], &r_data[y * _t.w], _t.w);
                j += round_up(_t.w, TextureDataPitchAlignment);
            }
        }
    } else {
        //
        // Compressed formats
        //
        src_fmt = fmt = g_to_internal_format[int(_t.format)];
        block = eTexBlock::_4x4;

        const bool flip_vertical = (_t.convention == eTextureConvention::DX);
        const bool invert_green = (_t.convention == eTextureConvention::DX) && _t.is_normalmap;
        reconstruct_z = _t.is_normalmap && (_t.format == eTextureFormat::BC5);

        int read_offset = 0, write_offset = 0;
        int w = _t.w, h = _t.h;
        for (int i = 0; i < mip_count; ++i) {
            if (_t.format == eTextureFormat::BC1) {
                data_size[i] = Preprocess_BCn<3>(&_t.data[read_offset], (w + 3) / 4, (h + 3) / 4, flip_vertical,
                                                 invert_green, &stage_data[write_offset],
                                                 GetRequiredMemory_BC1(w, 1, TextureDataPitchAlignment));
            } else if (_t.format == eTextureFormat::BC3) {
                data_size[i] = Preprocess_BCn<4>(&_t.data[read_offset], (w + 3) / 4, (h + 3) / 4, flip_vertical,
                                                 invert_green, &stage_data[write_offset],
                                                 GetRequiredMemory_BC3(w, 1, TextureDataPitchAlignment));
            } else if (_t.format == eTextureFormat::BC4) {
                data_size[i] = Preprocess_BCn<1>(&_t.data[read_offset], (w + 3) / 4, (h + 3) / 4, flip_vertical,
                                                 invert_green, &stage_data[write_offset],
                                                 GetRequiredMemory_BC4(w, 1, TextureDataPitchAlignment));
            } else if (_t.format == eTextureFormat::BC5) {
                data_size[i] = Preprocess_BCn<2>(&_t.data[read_offset], (w + 3) / 4, (h + 3) / 4, flip_vertical,
                                                 invert_green, &stage_data[write_offset],
                                                 GetRequiredMemory_BC5(w, 1, TextureDataPitchAlignment));
            }

            read_offset += data_size[i];
            write_offset += round_up(data_size[i], 4096);

            w /= 2;
            h /= 2;
        }
    }

    if (_t.generate_mipmaps && !IsCompressedFormat(src_fmt)) {
        const int res[2] = {_t.w, _t.h};
        if (src_fmt == eTexFormat::RawRGBA8888) {
            const auto *rgba_data =
                reinterpret_cast<const color_rgba8_t *>(repacked_data ? repacked_data.get() : _t.data.data());
            WriteTextureMips(rgba_data, res, mip_count, use_compression, stage_data, data_size);
        } else if (src_fmt == eTexFormat::RawRGB888) {
            const auto *rgb_data =
                reinterpret_cast<const color_rgb8_t *>(repacked_data ? repacked_data.get() : _t.data.data());
            WriteTextureMips(rgb_data, res, mip_count, use_compression, stage_data, data_size);
        } else if (src_fmt == eTexFormat::RawRG88) {
            const auto *rg_data =
                reinterpret_cast<const color_rg8_t *>(repacked_data ? repacked_data.get() : _t.data.data());
            WriteTextureMips(rg_data, res, mip_count, use_compression, stage_data, data_size);
        } else if (src_fmt == eTexFormat::RawR8) {
            const auto *r_data =
                reinterpret_cast<const color_r8_t *>(repacked_data ? repacked_data.get() : _t.data.data());
            WriteTextureMips(r_data, res, mip_count, use_compression, stage_data, data_size);
        }
    }

    temp_stage_buf.FlushMappedRange(0, temp_stage_buf.size(), true);
    temp_stage_buf.Unmap();

    Tex2DParams p = {};
    p.w = _t.w;
    p.h = _t.h;
    if (_t.is_srgb && !is_YCoCg && !RequiresManualSRGBConversion(fmt)) {
        p.flags |= eTexFlagBits::SRGB;
    }
    p.mip_count = mip_count;
    p.usage = eTexUsageBits::Transfer | eTexUsageBits::Sampled;
    p.format = fmt;
    p.block = block;
    p.sampling.filter = eTexFilter::NearestMipmap;

    std::pair<uint32_t, uint32_t> ret =
        bindless_textures_.emplace(_t.name ? _t.name : "Bindless Tex", ctx_, p, ctx_->default_memory_allocs(), log_);

    { // Submit GPU commands
        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

        int res[2] = {_t.w, _t.h};
        uint32_t data_offset = 0;
        for (int i = 0; i < p.mip_count; ++i) {
            bindless_textures_[ret.first].SetSubImage(i, 0, 0, res[0], res[1], fmt, temp_stage_buf, cmd_buf,
                                                      data_offset, data_size[i]);
            res[0] = std::max(res[0] / 2, 1);
            res[1] = std::max(res[1] / 2, 1);
            data_offset += round_up(data_size[i], 4096);
        }

        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
    }

    temp_stage_buf.FreeImmediate();

    log_->Info("Ray: Texture '%s' loaded (%ix%i)", _t.name, _t.w, _t.h);

    assert(ret.first <= 0x00ffffff);

    if (_t.is_srgb && (is_YCoCg || RequiresManualSRGBConversion(fmt))) {
        ret.first |= TEX_SRGB_BIT;
    }
    if (reconstruct_z) {
        ret.first |= TEX_RECONSTRUCT_Z_BIT;
    }
    if (is_YCoCg) {
        ret.first |= TEX_YCOCG_BIT;
    }

    return TextureHandle{ret.first, ret.second};
}

template <typename T, int N>
void Ray::NS::Scene::WriteTextureMips(const color_t<T, N> data[], const int _res[2], const int mip_count,
                                      const bool compress, uint8_t out_data[], uint32_t out_size[16]) {
    int src_res[2] = {_res[0], _res[1]};

    // TODO: try to get rid of these allocations
    std::vector<color_t<T, N>> _src_data, dst_data;
    for (int i = 1; i < mip_count; ++i) {
        const int dst_res[2] = {std::max(src_res[0] / 2, 1), std::max(src_res[1] / 2, 1)};

        dst_data.clear();
        dst_data.reserve(dst_res[0] * dst_res[1]);

        const color_t<T, N> *src_data = (i == 1) ? data : _src_data.data();

        for (int y = 0; y < dst_res[1]; ++y) {
            for (int x = 0; x < dst_res[0]; ++x) {
                const color_t<T, N> c00 = src_data[(2 * y + 0) * src_res[0] + (2 * x + 0)];
                const color_t<T, N> c10 = src_data[(2 * y + 0) * src_res[0] + std::min(2 * x + 1, src_res[0] - 1)];
                const color_t<T, N> c11 =
                    src_data[std::min(2 * y + 1, src_res[1] - 1) * src_res[0] + std::min(2 * x + 1, src_res[0] - 1)];
                const color_t<T, N> c01 = src_data[std::min(2 * y + 1, src_res[1] - 1) * src_res[0] + (2 * x + 0)];

                color_t<T, N> res;
                for (int j = 0; j < N; ++j) {
                    res.v[j] = (c00.v[j] + c10.v[j] + c11.v[j] + c01.v[j]) / 4;
                }

                dst_data.push_back(res);
            }
        }

        assert(dst_data.size() == (dst_res[0] * dst_res[1]));

        out_data += round_up(out_size[i - 1], 4096);
        if (compress && N <= 3) {
            if (N == 3) {
                auto temp_YCoCg = ConvertRGB_to_CoCgxY(&dst_data[0].v[0], dst_res[0], dst_res[1]);

                out_size[i] = GetRequiredMemory_BC3(dst_res[0], dst_res[1], TextureDataPitchAlignment);
                CompressImage_BC3<true /* Is_YCoCg */>(temp_YCoCg.get(), dst_res[0], dst_res[1], out_data,
                                                       GetRequiredMemory_BC3(dst_res[0], 1, TextureDataPitchAlignment));
            } else if (N == 1) {
                out_size[i] = GetRequiredMemory_BC4(dst_res[0], dst_res[1], TextureDataPitchAlignment);
                CompressImage_BC4<N>(&dst_data[0].v[0], dst_res[0], dst_res[1], out_data,
                                     GetRequiredMemory_BC4(dst_res[0], 1, TextureDataPitchAlignment));
            } else if (N == 2) {
                out_size[i] = GetRequiredMemory_BC5(dst_res[0], dst_res[1], TextureDataPitchAlignment);
                CompressImage_BC5<2>(&dst_data[0].v[0], dst_res[0], dst_res[1], out_data,
                                     GetRequiredMemory_BC5(dst_res[0], 1, TextureDataPitchAlignment));
            }
        } else {
            out_size[i] = int(dst_res[1] * round_up(dst_res[0] * sizeof(color_t<T, N>), TextureDataPitchAlignment));
            int j = 0;
            for (int y = 0; y < dst_res[1]; ++y) {
                memcpy(&out_data[j], &dst_data[y * dst_res[0]], dst_res[0] * sizeof(color_t<T, N>));
                j += round_up(dst_res[0] * sizeof(color_t<T, N>), TextureDataPitchAlignment);
            }
        }

        src_res[0] = dst_res[0];
        src_res[1] = dst_res[1];
        std::swap(_src_data, dst_data);
    }
}

inline Ray::MaterialHandle Ray::NS::Scene::AddMaterial_nolock(const shading_node_desc_t &m) {
    material_t mat = {};

    mat.type = m.type;
    mat.textures[BASE_TEXTURE] = m.base_texture._index;
    mat.roughness_unorm = pack_unorm_16(m.roughness);
    mat.textures[ROUGH_TEXTURE] = m.roughness_texture._index;
    memcpy(&mat.base_color[0], &m.base_color[0], 3 * sizeof(float));
    mat.ior = m.ior;
    mat.tangent_rotation = 0.0f;
    mat.flags = 0;

    if (m.type == eShadingNode::Diffuse) {
        mat.sheen_unorm = pack_unorm_16(clamp(0.5f * m.sheen, 0.0f, 1.0f));
        mat.sheen_tint_unorm = pack_unorm_16(clamp(m.tint, 0.0f, 1.0f));
        mat.textures[METALLIC_TEXTURE] = m.metallic_texture._index;
    } else if (m.type == eShadingNode::Glossy) {
        mat.tangent_rotation = 2.0f * PI * m.anisotropic_rotation;
        mat.textures[METALLIC_TEXTURE] = m.metallic_texture._index;
        mat.tint_unorm = pack_unorm_16(clamp(m.tint, 0.0f, 1.0f));
    } else if (m.type == eShadingNode::Refractive) {
    } else if (m.type == eShadingNode::Emissive) {
        mat.strength = m.strength;
        if (m.multiple_importance) {
            mat.flags |= MAT_FLAG_MULT_IMPORTANCE;
        }
    } else if (m.type == eShadingNode::Mix) {
        mat.strength = m.strength;
        mat.textures[MIX_MAT1] = m.mix_materials[0]._index;
        mat.textures[MIX_MAT2] = m.mix_materials[1]._index;
        if (m.mix_add) {
            mat.flags |= MAT_FLAG_MIX_ADD;
        }
    } else if (m.type == eShadingNode::Transparent) {
    }

    mat.textures[NORMALS_TEXTURE] = m.normal_map._index;
    mat.normal_map_strength_unorm = pack_unorm_16(clamp(m.normal_map_intensity, 0.0f, 1.0f));

    const std::pair<uint32_t, uint32_t> mi = materials_.push(mat);
    return MaterialHandle{mi.first, mi.second};
}

inline Ray::MaterialHandle Ray::NS::Scene::AddMaterial(const principled_mat_desc_t &m) {
    material_t main_mat = {};

    main_mat.type = eShadingNode::Principled;
    main_mat.textures[BASE_TEXTURE] = m.base_texture._index;
    memcpy(&main_mat.base_color[0], &m.base_color[0], 3 * sizeof(float));
    main_mat.sheen_unorm = pack_unorm_16(clamp(0.5f * m.sheen, 0.0f, 1.0f));
    main_mat.sheen_tint_unorm = pack_unorm_16(clamp(m.sheen_tint, 0.0f, 1.0f));
    main_mat.roughness_unorm = pack_unorm_16(clamp(m.roughness, 0.0f, 1.0f));
    main_mat.tangent_rotation = 2.0f * PI * clamp(m.anisotropic_rotation, 0.0f, 1.0f);
    main_mat.textures[ROUGH_TEXTURE] = m.roughness_texture._index;
    main_mat.metallic_unorm = pack_unorm_16(clamp(m.metallic, 0.0f, 1.0f));
    main_mat.textures[METALLIC_TEXTURE] = m.metallic_texture._index;
    main_mat.ior = m.ior;
    main_mat.flags = 0;
    main_mat.transmission_unorm = pack_unorm_16(clamp(m.transmission, 0.0f, 1.0f));
    main_mat.transmission_roughness_unorm = pack_unorm_16(clamp(m.transmission_roughness, 0.0f, 1.0f));
    main_mat.textures[NORMALS_TEXTURE] = m.normal_map._index;
    main_mat.normal_map_strength_unorm = pack_unorm_16(clamp(m.normal_map_intensity, 0.0f, 1.0f));
    main_mat.anisotropic_unorm = pack_unorm_16(clamp(m.anisotropic, 0.0f, 1.0f));
    main_mat.specular_unorm = pack_unorm_16(clamp(m.specular, 0.0f, 1.0f));
    main_mat.textures[SPECULAR_TEXTURE] = m.specular_texture._index;
    main_mat.specular_tint_unorm = pack_unorm_16(clamp(m.specular_tint, 0.0f, 1.0f));
    main_mat.clearcoat_unorm = pack_unorm_16(clamp(m.clearcoat, 0.0f, 1.0f));
    main_mat.clearcoat_roughness_unorm = pack_unorm_16(clamp(m.clearcoat_roughness, 0.0f, 1.0f));

    const std::pair<uint32_t, uint32_t> mi = materials_.push(main_mat);
    auto root_node = MaterialHandle{mi.first, mi.second};
    MaterialHandle emissive_node = InvalidMaterialHandle, transparent_node = InvalidMaterialHandle;

    if (m.emission_strength > 0.0f &&
        (m.emission_color[0] > 0.0f || m.emission_color[1] > 0.0f || m.emission_color[2] > 0.0f)) {
        shading_node_desc_t emissive_desc;
        emissive_desc.type = eShadingNode::Emissive;

        memcpy(emissive_desc.base_color, m.emission_color, 3 * sizeof(float));
        emissive_desc.base_texture = m.emission_texture;
        emissive_desc.strength = m.emission_strength;
        emissive_desc.multiple_importance = m.multiple_importance;

        emissive_node = AddMaterial(emissive_desc);
    }

    if (m.alpha != 1.0f || m.alpha_texture != InvalidTextureHandle) {
        shading_node_desc_t transparent_desc;
        transparent_desc.type = eShadingNode::Transparent;

        transparent_node = AddMaterial(transparent_desc);
    }

    if (emissive_node != InvalidMaterialHandle) {
        if (root_node == InvalidMaterialHandle) {
            root_node = emissive_node;
        } else {
            shading_node_desc_t mix_node;
            mix_node.type = eShadingNode::Mix;
            mix_node.base_texture = InvalidTextureHandle;
            mix_node.strength = 0.5f;
            mix_node.ior = 0.0f;
            mix_node.mix_add = true;

            mix_node.mix_materials[0] = root_node;
            mix_node.mix_materials[1] = emissive_node;

            root_node = AddMaterial(mix_node);
        }
    }

    if (transparent_node != InvalidMaterialHandle) {
        if (root_node == InvalidMaterialHandle || m.alpha == 0.0f) {
            root_node = transparent_node;
        } else {
            shading_node_desc_t mix_node;
            mix_node.type = eShadingNode::Mix;
            mix_node.base_texture = m.alpha_texture;
            mix_node.strength = m.alpha;
            mix_node.ior = 0.0f;

            mix_node.mix_materials[0] = transparent_node;
            mix_node.mix_materials[1] = root_node;

            root_node = AddMaterial(mix_node);
        }
    }

    return root_node;
}

inline Ray::MeshHandle Ray::NS::Scene::AddMesh(const mesh_desc_t &_m) {
    std::vector<bvh_node_t> new_nodes;
    aligned_vector<tri_accel_t> new_tris;
    std::vector<uint32_t> new_tri_indices;
    std::vector<uint32_t> new_vtx_indices;

    bvh_settings_t s;
    s.allow_spatial_splits = _m.allow_spatial_splits;
    s.use_fast_bvh_build = _m.use_fast_bvh_build;

    simd_fvec4 bbox_min{FLT_MAX}, bbox_max{-FLT_MAX};

    const size_t attr_stride = AttrStrides[int(_m.layout)];
    if (use_hwrt_) {
        for (int j = 0; j < int(_m.vtx_indices.size()); j += 3) {
            simd_fvec4 p[3];

            const uint32_t i0 = _m.vtx_indices[j + 0], i1 = _m.vtx_indices[j + 1], i2 = _m.vtx_indices[j + 2];

            memcpy(value_ptr(p[0]), &_m.vtx_attrs[i0 * attr_stride], 3 * sizeof(float));
            memcpy(value_ptr(p[1]), &_m.vtx_attrs[i1 * attr_stride], 3 * sizeof(float));
            memcpy(value_ptr(p[2]), &_m.vtx_attrs[i2 * attr_stride], 3 * sizeof(float));

            bbox_min = min(bbox_min, min(p[0], min(p[1], p[2])));
            bbox_max = max(bbox_max, max(p[0], max(p[1], p[2])));
        }
    } else {
        aligned_vector<mtri_accel_t> _unused;
        PreprocessMesh(_m.vtx_attrs.data(), _m.vtx_indices, _m.layout, _m.base_vertex, s, new_nodes, new_tris,
                       new_tri_indices, _unused);

        memcpy(value_ptr(bbox_min), new_nodes[0].bbox_min, 3 * sizeof(float));
        memcpy(value_ptr(bbox_max), new_nodes[0].bbox_max, 3 * sizeof(float));
    }

    std::vector<tri_mat_data_t> new_tri_materials(_m.vtx_indices.size() / 3);

    // init triangle materials
    for (const mat_group_desc_t &grp : _m.groups) {
        bool is_front_solid = true, is_back_solid = true;

        uint32_t material_stack[32];
        material_stack[0] = grp.front_mat._index;
        uint32_t material_count = 1;

        while (material_count) {
            const material_t &mat = materials_[material_stack[--material_count]];

            if (mat.type == eShadingNode::Mix) {
                material_stack[material_count++] = mat.textures[MIX_MAT1];
                material_stack[material_count++] = mat.textures[MIX_MAT2];
            } else if (mat.type == eShadingNode::Transparent) {
                is_front_solid = false;
                break;
            }
        }

        material_stack[0] = grp.back_mat._index;
        material_count = 1;

        while (material_count) {
            const material_t &mat = materials_[material_stack[--material_count]];

            if (mat.type == eShadingNode::Mix) {
                material_stack[material_count++] = mat.textures[MIX_MAT1];
                material_stack[material_count++] = mat.textures[MIX_MAT2];
            } else if (mat.type == eShadingNode::Transparent) {
                is_back_solid = false;
                break;
            }
        }

        for (size_t i = grp.vtx_start; i < grp.vtx_start + grp.vtx_count; i += 3) {
            tri_mat_data_t &tri_mat = new_tri_materials[i / 3];

            assert(grp.front_mat._index < (1 << 14) && "Not enough bits to reference material!");
            assert(grp.back_mat._index < (1 << 14) && "Not enough bits to reference material!");

            tri_mat.front_mi = uint16_t(grp.front_mat._index);
            if (is_front_solid) {
                tri_mat.front_mi |= MATERIAL_SOLID_BIT;
            }

            tri_mat.back_mi = uint16_t(grp.back_mat._index);
            if (is_back_solid) {
                tri_mat.back_mi |= MATERIAL_SOLID_BIT;
            }
        }
    }

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    for (int i = 0; i < _m.vtx_indices.size(); ++i) {
        new_vtx_indices.push_back(_m.vtx_indices[i] + _m.base_vertex + uint32_t(vertices_.size()));
    }

    // offset nodes and primitives
    for (bvh_node_t &n : new_nodes) {
        if (n.prim_index & LEAF_NODE_BIT) {
            n.prim_index += uint32_t(tri_indices_.size());
        } else {
            n.left_child += uint32_t(nodes_.size());
            n.right_child += uint32_t(nodes_.size());
        }
    }

    // offset triangle indices
    for (uint32_t &i : new_tri_indices) {
        i += uint32_t(tri_materials_.size());
    }

    tri_materials_.Append(&new_tri_materials[0], new_tri_materials.size());
    tri_materials_cpu_.insert(tri_materials_cpu_.end(), new_tri_materials.data(),
                              new_tri_materials.data() + new_tri_materials.size());
    assert(tri_materials_.size() == tri_materials_cpu_.size());

    // add mesh
    mesh_t m = {};
    memcpy(m.bbox_min, value_ptr(bbox_min), 3 * sizeof(float));
    memcpy(m.bbox_max, value_ptr(bbox_max), 3 * sizeof(float));
    m.node_index = uint32_t(nodes_.size());
    m.node_count = uint32_t(new_nodes.size());
    m.tris_index = uint32_t(tris_.size());
    m.tris_count = uint32_t(new_tris.size());
    m.vert_index = uint32_t(vtx_indices_.size());
    m.vert_count = uint32_t(new_vtx_indices.size());

    const std::pair<uint32_t, uint32_t> mesh_index = meshes_.push(m);

    if (!use_hwrt_) {
        // add nodes
        nodes_.Append(&new_nodes[0], new_nodes.size());
    }

    const size_t stride = AttrStrides[int(_m.layout)];

    // add attributes
    std::vector<vertex_t> new_vertices(_m.vtx_attrs.size() / stride);
    for (int i = 0; i < _m.vtx_attrs.size() / stride; ++i) {
        vertex_t &v = new_vertices[i];

        memcpy(&v.p[0], (_m.vtx_attrs.data() + i * stride), 3 * sizeof(float));
        memcpy(&v.n[0], (_m.vtx_attrs.data() + i * stride + 3), 3 * sizeof(float));

        if (_m.layout == eVertexLayout::PxyzNxyzTuv) {
            memcpy(&v.t[0], (_m.vtx_attrs.data() + i * stride + 6), 2 * sizeof(float));
            // v.t[1][0] = v.t[1][1] = 0.0f;
            v.b[0] = v.b[1] = v.b[2] = 0.0f;
        } else if (_m.layout == eVertexLayout::PxyzNxyzTuvTuv) {
            memcpy(&v.t[0], (_m.vtx_attrs.data() + i * stride + 6), 2 * sizeof(float));
            // memcpy(&v.t[1][0], (_m.vtx_attrs.data() + i * stride + 8), 2 * sizeof(float));
            v.b[0] = v.b[1] = v.b[2] = 0.0f;
        } else if (_m.layout == eVertexLayout::PxyzNxyzBxyzTuv) {
            memcpy(&v.b[0], (_m.vtx_attrs.data() + i * stride + 6), 3 * sizeof(float));
            memcpy(&v.t[0], (_m.vtx_attrs.data() + i * stride + 9), 2 * sizeof(float));
            // v.t[1][0] = v.t[1][1] = 0.0f;
        } else if (_m.layout == eVertexLayout::PxyzNxyzBxyzTuvTuv) {
            memcpy(&v.b[0], (_m.vtx_attrs.data() + i * stride + 6), 3 * sizeof(float));
            memcpy(&v.t[0], (_m.vtx_attrs.data() + i * stride + 9), 2 * sizeof(float));
            // memcpy(&v.t[1][0], (_m.vtx_attrs.data() + i * stride + 11), 2 * sizeof(float));
        }
    }

    if (_m.layout == eVertexLayout::PxyzNxyzTuv || _m.layout == eVertexLayout::PxyzNxyzTuvTuv) {
        ComputeTangentBasis(vertices_.size(), 0, new_vertices, new_vtx_indices, _m.vtx_indices);
    }

    vertices_.Append(&new_vertices[0], new_vertices.size());
    vertices_cpu_.insert(std::end(vertices_cpu_), std::begin(new_vertices), std::end(new_vertices));
    assert(vertices_.size() == vertices_cpu_.size());

    // add vertex indices
    vtx_indices_.Append(&new_vtx_indices[0], new_vtx_indices.size());
    vtx_indices_cpu_.insert(vtx_indices_cpu_.end(), std::begin(new_vtx_indices), std::end(new_vtx_indices));
    assert(vtx_indices_.size() == vtx_indices_cpu_.size());

    if (!use_hwrt_) {
        // add triangles
        tris_.Append(&new_tris[0], new_tris.size());
        // add triangle indices
        tri_indices_.Append(&new_tri_indices[0], new_tri_indices.size());
    }

    return MeshHandle{mesh_index.first, mesh_index.second};
}

inline void Ray::NS::Scene::RemoveMesh(MeshHandle) {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);
    // TODO!!!
}

inline Ray::LightHandle Ray::NS::Scene::AddLight(const directional_light_desc_t &_l) {
    light_t l = {};

    l.type = LIGHT_TYPE_DIR;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.blocking = false;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));
    l.dir.dir[0] = -_l.direction[0];
    l.dir.dir[1] = -_l.direction[1];
    l.dir.dir[2] = -_l.direction[2];
    l.dir.angle = _l.angle * PI / 360.0f;
    if (l.dir.angle != 0.0f) {
        const float radius = std::tan(l.dir.angle);
        const float mul = 1.0f / (PI * radius * radius);
        l.col[0] *= mul;
        l.col[1] *= mul;
        l.col[2] *= mul;
    }

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.PushBack(light_index.first);
    if (_l.visible) {
        visible_lights_.PushBack(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

inline Ray::LightHandle Ray::NS::Scene::AddLight(const sphere_light_desc_t &_l) {
    light_t l = {};

    l.type = LIGHT_TYPE_SPHERE;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.blocking = false;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));
    memcpy(&l.sph.pos[0], &_l.position[0], 3 * sizeof(float));

    l.sph.area = 4.0f * PI * _l.radius * _l.radius;
    l.sph.radius = _l.radius;
    l.sph.spot = l.sph.blend = -1.0f;

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.PushBack(light_index.first);
    if (_l.visible) {
        visible_lights_.PushBack(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

inline Ray::LightHandle Ray::NS::Scene::AddLight(const spot_light_desc_t &_l) {
    light_t l = {};

    l.type = LIGHT_TYPE_SPHERE;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.blocking = false;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));
    memcpy(&l.sph.pos[0], &_l.position[0], 3 * sizeof(float));
    memcpy(&l.sph.dir[0], &_l.direction[0], 3 * sizeof(float));

    l.sph.area = 4.0f * PI * _l.radius * _l.radius;
    l.sph.radius = _l.radius;
    l.sph.spot = 0.5f * PI * _l.spot_size / 180.0f;
    l.sph.blend = _l.spot_blend * _l.spot_blend;

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.PushBack(light_index.first);
    if (_l.visible) {
        visible_lights_.PushBack(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

inline Ray::LightHandle Ray::NS::Scene::AddLight(const rect_light_desc_t &_l, const float *xform) {
    light_t l = {};

    l.type = LIGHT_TYPE_RECT;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.sky_portal = _l.sky_portal;
    l.blocking = _l.sky_portal;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));

    l.rect.pos[0] = xform[12];
    l.rect.pos[1] = xform[13];
    l.rect.pos[2] = xform[14];

    l.rect.area = _l.width * _l.height;

    const Ref::simd_fvec4 uvec = _l.width * Ref::TransformDirection(Ref::simd_fvec4{1.0f, 0.0f, 0.0f, 0.0f}, xform);
    const Ref::simd_fvec4 vvec = _l.height * Ref::TransformDirection(Ref::simd_fvec4{0.0f, 0.0f, 1.0f, 0.0f}, xform);

    memcpy(l.rect.u, value_ptr(uvec), 3 * sizeof(float));
    memcpy(l.rect.v, value_ptr(vvec), 3 * sizeof(float));

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.PushBack(light_index.first);
    if (_l.visible) {
        visible_lights_.PushBack(light_index.first);
    }
    if (_l.sky_portal) {
        blocker_lights_.PushBack(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

inline Ray::LightHandle Ray::NS::Scene::AddLight(const disk_light_desc_t &_l, const float *xform) {
    light_t l = {};

    l.type = LIGHT_TYPE_DISK;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.sky_portal = _l.sky_portal;
    l.blocking = _l.sky_portal;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));

    l.disk.pos[0] = xform[12];
    l.disk.pos[1] = xform[13];
    l.disk.pos[2] = xform[14];

    l.disk.area = 0.25f * PI * _l.size_x * _l.size_y;

    const Ref::simd_fvec4 uvec = _l.size_x * Ref::TransformDirection(Ref::simd_fvec4{1.0f, 0.0f, 0.0f, 0.0f}, xform);
    const Ref::simd_fvec4 vvec = _l.size_y * Ref::TransformDirection(Ref::simd_fvec4{0.0f, 0.0f, 1.0f, 0.0f}, xform);

    memcpy(l.disk.u, value_ptr(uvec), 3 * sizeof(float));
    memcpy(l.disk.v, value_ptr(vvec), 3 * sizeof(float));

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.PushBack(light_index.first);
    if (_l.visible) {
        visible_lights_.PushBack(light_index.first);
    }
    if (_l.sky_portal) {
        blocker_lights_.PushBack(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

inline Ray::LightHandle Ray::NS::Scene::AddLight(const line_light_desc_t &_l, const float *xform) {
    light_t l = {};

    l.type = LIGHT_TYPE_LINE;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.sky_portal = _l.sky_portal;
    l.blocking = false;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));

    l.line.pos[0] = xform[12];
    l.line.pos[1] = xform[13];
    l.line.pos[2] = xform[14];

    l.line.area = 2.0f * PI * _l.radius * _l.height;

    const Ref::simd_fvec4 uvec = TransformDirection(Ref::simd_fvec4{1.0f, 0.0f, 0.0f, 0.0f}, xform);
    const Ref::simd_fvec4 vvec = TransformDirection(Ref::simd_fvec4{0.0f, 1.0f, 0.0f, 0.0f}, xform);

    memcpy(l.line.u, value_ptr(uvec), 3 * sizeof(float));
    l.line.radius = _l.radius;
    memcpy(l.line.v, value_ptr(vvec), 3 * sizeof(float));
    l.line.height = _l.height;

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.PushBack(light_index.first);
    if (_l.visible) {
        visible_lights_.PushBack(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

inline void Ray::NS::Scene::RemoveLight_nolock(const LightHandle i) {
    //{ // remove from compacted list
    //    auto it = find(begin(li_indices_), end(li_indices_), i);
    //    assert(it != end(li_indices_));
    //    li_indices_.erase(it);
    //}

    // if (lights_[i].visible) {
    //     auto it = find(begin(visible_lights_), end(visible_lights_), i);
    //     assert(it != end(visible_lights_));
    //     visible_lights_.erase(it);
    // }

    // if (lights_[i].sky_portal) {
    //     auto it = find(begin(blocker_lights_), end(blocker_lights_), i);
    //     assert(it != end(blocker_lights_));
    //     blocker_lights_.erase(it);
    // }

    lights_.Erase(i._block);
}

inline Ray::MeshInstanceHandle Ray::NS::Scene::AddMeshInstance(const mesh_instance_desc_t &mi_desc) {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> tr_index = transforms_.emplace();

    mesh_instance_t mi = {};
    mi.mesh_index = mi_desc.mesh._index;
    mi.tr_index = tr_index.first;
    mi.tr_block = tr_index.second;
    mi.ray_visibility = 0x000000ff;

    if (!mi_desc.camera_visibility) {
        mi.ray_visibility &= ~(1u << RAY_TYPE_CAMERA);
    }
    if (!mi_desc.diffuse_visibility) {
        mi.ray_visibility &= ~(1u << RAY_TYPE_DIFFUSE);
    }
    if (!mi_desc.specular_visibility) {
        mi.ray_visibility &= ~(1u << RAY_TYPE_SPECULAR);
    }
    if (!mi_desc.refraction_visibility) {
        mi.ray_visibility &= ~(1u << RAY_TYPE_REFR);
    }
    if (!mi_desc.shadow_visibility) {
        mi.ray_visibility &= ~(1u << RAY_TYPE_SHADOW);
    }

    const std::pair<uint32_t, uint32_t> mi_index = mesh_instances_.push(mi);

    std::vector<uint32_t> new_li_indices;

    { // find emissive triangles and add them as light emitters
        const mesh_t &m = meshes_[mi_desc.mesh._index];
        for (uint32_t tri = (m.vert_index / 3); tri < (m.vert_index + m.vert_count) / 3; ++tri) {
            const tri_mat_data_t &tri_mat = tri_materials_cpu_[tri];

            SmallVector<uint16_t, 64> mat_indices;
            mat_indices.push_back(tri_mat.front_mi & MATERIAL_INDEX_BITS);

            uint16_t front_emissive = 0xffff;
            for (int i = 0; i < int(mat_indices.size()); ++i) {
                const material_t &mat = materials_[mat_indices[i]];
                if (mat.type == eShadingNode::Emissive && (mat.flags & MAT_FLAG_MULT_IMPORTANCE)) {
                    front_emissive = mat_indices[i];
                    break;
                } else if (mat.type == eShadingNode::Mix) {
                    mat_indices.push_back(mat.textures[MIX_MAT1]);
                    mat_indices.push_back(mat.textures[MIX_MAT2]);
                }
            }

            mat_indices.clear();
            mat_indices.push_back(tri_mat.back_mi & MATERIAL_INDEX_BITS);

            uint16_t back_emissive = 0xffff;
            for (int i = 0; i < int(mat_indices.size()); ++i) {
                const material_t &mat = materials_[mat_indices[i]];
                if (mat.type == eShadingNode::Emissive && (mat.flags & MAT_FLAG_MULT_IMPORTANCE)) {
                    back_emissive = mat_indices[i];
                    break;
                } else if (mat.type == eShadingNode::Mix) {
                    mat_indices.push_back(mat.textures[MIX_MAT1]);
                    mat_indices.push_back(mat.textures[MIX_MAT2]);
                }
            }

            if (front_emissive != 0xffff) {
                const material_t &mat = materials_[front_emissive];

                light_t new_light = {};
                new_light.type = LIGHT_TYPE_TRI;
                new_light.doublesided = (back_emissive != 0xffff) ? 1 : 0;
                new_light.cast_shadow = 1;
                new_light.visible = 0;
                new_light.sky_portal = 0;
                new_light.blocking = 0;
                new_light.tri.tri_index = tri;
                new_light.tri.xform_index = mi.tr_index;
                new_light.tri.tex_index = mat.textures[BASE_TEXTURE];
                new_light.col[0] = mat.base_color[0] * mat.strength;
                new_light.col[1] = mat.base_color[1] * mat.strength;
                new_light.col[2] = mat.base_color[2] * mat.strength;
                const std::pair<uint32_t, uint32_t> index = lights_.push(new_light);
                new_li_indices.push_back(index.first);
            }
        }
    }

    if (!new_li_indices.empty()) {
        li_indices_.Append(new_li_indices.data(), new_li_indices.size());
    }

    auto ret = MeshInstanceHandle{mi_index.first, mi_index.second};

    SetMeshInstanceTransform_nolock(ret, mi_desc.xform);

    return ret;
}

inline void Ray::NS::Scene::SetMeshInstanceTransform_nolock(const MeshInstanceHandle mi_handle, const float *xform) {
    transform_t tr = {};

    memcpy(tr.xform, xform, 16 * sizeof(float));
    InverseMatrix(tr.xform, tr.inv_xform);

    mesh_instance_t mi = mesh_instances_[mi_handle._index];

    const mesh_t &m = meshes_[mi.mesh_index];
    TransformBoundingBox(m.bbox_min, m.bbox_max, xform, mi.bbox_min, mi.bbox_max);

    mesh_instances_.Set(mi_handle._index, mi);
    transforms_.Set(mi.tr_index, tr);

    RebuildTLAS_nolock();
}

inline void Ray::NS::Scene::RemoveMeshInstance(MeshInstanceHandle) {
    // TODO!!
}

inline void Ray::NS::Scene::Finalize() {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    if (env_map_light_ != InvalidLightHandle) {
        RemoveLight_nolock(env_map_light_);
    }
    env_map_qtree_ = {};
    env_.qtree_levels = 0;

    if (env_.env_map != InvalidTextureHandle._index &&
        (env_.env_map == PhysicalSkyTexture._index || env_.env_map == physical_sky_texture_._index)) {
        PrepareSkyEnvMap_nolock();
    }

    if (env_.multiple_importance && env_.env_col[0] > 0.0f && env_.env_col[1] > 0.0f && env_.env_col[2] > 0.0f) {
        if (env_.env_map != InvalidTextureHandle._index) {
            PrepareEnvMapQTree_nolock();
        } else {
            // Dummy
            Tex2DParams p;
            p.w = p.h = 1;
            p.format = eTexFormat::RawRGBA32F;
            p.mip_count = 1;
            p.usage = eTexUsageBits::Sampled | eTexUsageBits::Transfer;

            env_map_qtree_.tex = Texture2D("Env map qtree", ctx_, p, ctx_->default_memory_allocs(), log_);
        }
        { // add env light source
            light_t l = {};

            l.type = LIGHT_TYPE_ENV;
            l.cast_shadow = 1;
            l.col[0] = l.col[1] = l.col[2] = 1.0f;

            const std::pair<uint32_t, uint32_t> li = lights_.push(l);
            env_map_light_ = LightHandle{li.first, li.second};
            li_indices_.PushBack(env_map_light_._index);
        }
    } else {
        // Dummy
        Tex2DParams p;
        p.w = p.h = 1;
        p.format = eTexFormat::RawRGBA32F;
        p.mip_count = 1;
        p.usage = eTexUsageBits::Sampled | eTexUsageBits::Transfer;

        env_map_qtree_.tex = Texture2D("Env map qtree", ctx_, p, ctx_->default_memory_allocs(), log_);
    }

    if (use_bindless_ && env_.env_map != InvalidTextureHandle._index) {
        const auto &env_map_params = bindless_textures_[env_.env_map].params;
        env_.env_map_res = (env_map_params.w << 16) | env_map_params.h;
    } else {
        env_.env_map_res = 0;
    }

    if (use_bindless_ && env_.back_map != InvalidTextureHandle._index) {
        const auto &back_map_params = bindless_textures_[env_.back_map].params;
        env_.back_map_res = (back_map_params.w << 16) | back_map_params.h;
    } else {
        env_.back_map_res = 0;
    }

    GenerateTextureMips_nolock();
    PrepareBindlessTextures_nolock();
    RebuildHWAccStructures_nolock();
    RebuildLightTree_nolock();
}

inline void Ray::NS::Scene::RemoveNodes_nolock(uint32_t node_index, uint32_t node_count) {
    if (!node_count) {
        return;
    }

    /*nodes_.Erase(node_index, node_count);

    if (node_index != nodes_.size()) {
        size_t meshes_count = meshes_.size();
        std::vector<mesh_t> meshes(meshes_count);
        meshes_.Get(&meshes[0], 0, meshes_.size());

        for (mesh_t &m : meshes) {
            if (m.node_index > node_index) {
                m.node_index -= node_count;
            }
        }
        meshes_.Set(&meshes[0], 0, meshes_count);

        size_t nodes_count = nodes_.size();
        std::vector<bvh_node_t> nodes(nodes_count);
        nodes_.Get(&nodes[0], 0, nodes_count);

        for (uint32_t i = node_index; i < nodes.size(); i++) {
            bvh_node_t &n = nodes[i];
            if ((n.prim_index & LEAF_NODE_BIT) == 0) {
                if (n.left_child > node_index) {
                    n.left_child -= node_count;
                }
                if ((n.right_child & RIGHT_CHILD_BITS) > node_index) {
                    n.right_child -= node_count;
                }
            }
        }
        nodes_.Set(&nodes[0], 0, nodes_count);

        if (macro_nodes_start_ > node_index) {
            macro_nodes_start_ -= node_count;
        }
    }*/
}

inline void Ray::NS::Scene::RebuildTLAS_nolock() {
    RemoveNodes_nolock(macro_nodes_start_, macro_nodes_count_);
    mi_indices_.Clear();

    const size_t mi_count = mesh_instances_.size();

    aligned_vector<prim_t> primitives;
    primitives.reserve(mi_count);

    for (auto it = mesh_instances_.cbegin(); it != mesh_instances_.cend(); ++it) {
        primitives.push_back({0, 0, 0, Ref::simd_fvec4{it->bbox_min[0], it->bbox_min[1], it->bbox_min[2], 0.0f},
                              Ref::simd_fvec4{it->bbox_max[0], it->bbox_max[1], it->bbox_max[2], 0.0f}});
    }

    std::vector<bvh_node_t> bvh_nodes;
    std::vector<uint32_t> mi_indices;

    macro_nodes_start_ = uint32_t(nodes_.size());
    macro_nodes_count_ = PreprocessPrims_SAH(primitives, nullptr, 0, {}, bvh_nodes, mi_indices);

    // offset nodes
    for (bvh_node_t &n : bvh_nodes) {
        if ((n.prim_index & LEAF_NODE_BIT) == 0) {
            n.left_child += uint32_t(nodes_.size());
            n.right_child += uint32_t(nodes_.size());
        }
    }

    nodes_.Append(&bvh_nodes[0], bvh_nodes.size());
    mi_indices_.Append(&mi_indices[0], mi_indices.size());

    // store root node
    tlas_root_node_ = bvh_nodes[0];
}

inline void Ray::NS::Scene::PrepareSkyEnvMap_nolock() {
    if (physical_sky_texture_ != InvalidTextureHandle) {
        if (use_bindless_) {
            bindless_textures_.Erase(physical_sky_texture_._block);
        } else {
            atlas_textures_.Erase(physical_sky_texture_._block);
        }
    }

    // Find directional light sources
    std::vector<uint32_t> dir_lights;
    for (auto it = lights_.begin(); it != lights_.end(); ++it) {
        const light_t &l = lights_[it.index()];
        if (l.type == LIGHT_TYPE_DIR) {
            dir_lights.push_back(it.index());
        }
    }

    // if (dir_lights.empty()) {
    //     env_.env_map = InvalidTextureHandle._index;
    //     if (env_.back_map == PhysicalSkyTexture._index) {
    //         env_.back_map = InvalidTextureHandle._index;
    //     }
    //     return;
    // }

    static const int SkyEnvRes[] = {512, 256};
    std::vector<uint8_t> rgbe_pixels(4 * SkyEnvRes[0] * SkyEnvRes[1]);

    for (int y = 0; y < SkyEnvRes[1]; ++y) {
        const float theta = PI * float(y) / float(SkyEnvRes[1]);
        for (int x = 0; x < SkyEnvRes[0]; ++x) {
            const float phi = 2.0f * PI * float(x) / float(SkyEnvRes[0]);

            auto ray_dir = Ref::simd_fvec4{std::sin(theta) * std::cos(phi), std::cos(theta),
                                           std::sin(theta) * std::sin(phi), 0.0f};

            Ref::simd_fvec4 color = 0.0f;

            // Evaluate light sources
            for (const uint32_t li_index : dir_lights) {
                const light_t &l = lights_[li_index];

                const Ref::simd_fvec4 light_dir = {l.dir.dir[0], l.dir.dir[1], l.dir.dir[2], 0.0f};
                Ref::simd_fvec4 light_col = {l.col[0], l.col[1], l.col[2], 0.0f};
                if (l.dir.angle != 0.0f) {
                    const float radius = std::tan(l.dir.angle);
                    light_col *= (PI * radius * radius);
                }

                Ref::simd_fvec4 transmittance;
                color +=
                    IntegrateScattering(Ref::simd_fvec4{0.0f}, ray_dir, MAX_DIST, light_dir, light_col, transmittance);
            }

            color = rgb_to_rgbe(color);

            rgbe_pixels[4 * (y * SkyEnvRes[0] + x) + 0] = uint8_t(color.get<0>());
            rgbe_pixels[4 * (y * SkyEnvRes[0] + x) + 1] = uint8_t(color.get<1>());
            rgbe_pixels[4 * (y * SkyEnvRes[0] + x) + 2] = uint8_t(color.get<2>());
            rgbe_pixels[4 * (y * SkyEnvRes[0] + x) + 3] = uint8_t(color.get<3>());
        }
    }

    tex_desc_t desc = {};
    desc.format = eTextureFormat::RGBA8888;
    desc.name = "Physical Sky Texture";
    desc.data = rgbe_pixels;
    desc.w = SkyEnvRes[0];
    desc.h = SkyEnvRes[1];
    desc.is_srgb = false;
    desc.force_no_compression = true;

    if (use_bindless_) {
        physical_sky_texture_ = AddBindlessTexture_nolock(desc);
    } else {
        physical_sky_texture_ = AddAtlasTexture_nolock(desc);
    }

    env_.env_map = physical_sky_texture_._index;
    if (env_.back_map == PhysicalSkyTexture._index) {
        env_.back_map = physical_sky_texture_._index;
    }
}

inline void Ray::NS::Scene::PrepareEnvMapQTree_nolock() {
    const int tex = int(env_.env_map & 0x00ffffff);

    Buffer temp_stage_buf;
    simd_ivec2 size;
    int pitch = 0;

    if (use_bindless_) {
        const Texture2D &t = bindless_textures_[tex];
        size.template set<0>(t.params.w);
        size.template set<1>(t.params.h);

        assert(t.params.format == eTexFormat::RawRGBA8888);
        pitch = round_up(t.params.w * GetPerPixelDataLen(eTexFormat::RawRGBA8888), TextureDataPitchAlignment);
        const uint32_t data_size = pitch * t.params.h;

        temp_stage_buf = Buffer("Temp stage buf", ctx_, eBufType::Readback, data_size);

        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

        CopyImageToBuffer(t, 0, 0, 0, t.params.w, t.params.h, temp_stage_buf, cmd_buf, 0);

        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
    } else {
        const atlas_texture_t &t = atlas_textures_[tex];
        size.template set<0>(t.width & ATLAS_TEX_WIDTH_BITS);
        size.template set<1>(t.height & ATLAS_TEX_HEIGHT_BITS);

        const TextureAtlas &atlas = tex_atlases_[t.atlas];

        assert(atlas.format() == eTexFormat::RawRGBA8888);
        pitch = round_up(size.get<0>() * GetPerPixelDataLen(atlas.real_format()), TextureDataPitchAlignment);
        const uint32_t data_size = pitch * size.get<1>();

        temp_stage_buf = Buffer("Temp stage buf", ctx_, eBufType::Readback, data_size);

        CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

        atlas.CopyRegionTo(t.page[0], t.pos[0][0], t.pos[0][1], (t.width & ATLAS_TEX_WIDTH_BITS),
                           (t.height & ATLAS_TEX_HEIGHT_BITS), temp_stage_buf, cmd_buf, 0);

        EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());
    }

    pitch /= 4;

    const uint8_t *rgbe_data = temp_stage_buf.Map();

    const int lowest_dim = std::min(size[0], size[1]);

    env_map_qtree_.res = 1;
    while (2 * env_map_qtree_.res < lowest_dim) {
        env_map_qtree_.res *= 2;
    }

    assert(env_map_qtree_.mips.empty());

    int cur_res = env_map_qtree_.res;
    float total_lum = 0.0f;

    { // initialize the first quadtree level
        env_map_qtree_.mips.emplace_back(cur_res * cur_res / 4, 0.0f);

        for (int y = 0; y < size[1]; ++y) {
            for (int x = 0; x < size[0]; ++x) {
                const uint8_t *col_rgbe = &rgbe_data[4 * (y * pitch + x)];
                simd_fvec4 col_rgb;
                rgbe_to_rgb(col_rgbe, value_ptr(col_rgb));

                const float cur_lum = (col_rgb[0] + col_rgb[1] + col_rgb[2]);

                for (int jj = -1; jj <= 1; ++jj) {
                    const float theta = PI * float(y + jj) / float(size[1]);
                    for (int ii = -1; ii <= 1; ++ii) {
                        const float phi = 2.0f * PI * float(x + ii) / float(size[0]);
                        auto dir = simd_fvec4{std::sin(theta) * std::cos(phi), std::cos(theta),
                                              std::sin(theta) * std::sin(phi), 0.0f};

                        simd_fvec2 q;
                        DirToCanonical(value_ptr(dir), 0.0f, value_ptr(q));

                        int qx = clamp(int(cur_res * q[0]), 0, cur_res - 1);
                        int qy = clamp(int(cur_res * q[1]), 0, cur_res - 1);

                        int index = 0;
                        index |= (qx & 1) << 0;
                        index |= (qy & 1) << 1;

                        qx /= 2;
                        qy /= 2;

                        simd_fvec4 &qvec = env_map_qtree_.mips[0][qy * cur_res / 2 + qx];
                        qvec.set(index, std::max(qvec[index], cur_lum));
                    }
                }
            }
        }

        for (const simd_fvec4 &v : env_map_qtree_.mips[0]) {
            total_lum += (v[0] + v[1] + v[2] + v[3]);
        }

        cur_res /= 2;
    }

    temp_stage_buf.Unmap();
    temp_stage_buf.FreeImmediate();

    while (cur_res > 1) {
        env_map_qtree_.mips.emplace_back(cur_res * cur_res / 4, 0.0f);
        const auto &prev_mip = env_map_qtree_.mips[env_map_qtree_.mips.size() - 2];

        for (int y = 0; y < cur_res; ++y) {
            for (int x = 0; x < cur_res; ++x) {
                const float res_lum = prev_mip[y * cur_res + x][0] + prev_mip[y * cur_res + x][1] +
                                      prev_mip[y * cur_res + x][2] + prev_mip[y * cur_res + x][3];

                int index = 0;
                index |= (x & 1) << 0;
                index |= (y & 1) << 1;

                const int qx = (x / 2);
                const int qy = (y / 2);

                env_map_qtree_.mips.back()[qy * cur_res / 2 + qx].set(index, res_lum);
            }
        }

        cur_res /= 2;
    }

    //
    // Determine how many levels was actually required
    //

    const float LumFractThreshold = 0.01f;

    cur_res = 2;
    int the_last_required_lod = 0;
    for (int lod = int(env_map_qtree_.mips.size()) - 1; lod >= 0; --lod) {
        the_last_required_lod = lod;
        const auto &cur_mip = env_map_qtree_.mips[lod];

        bool subdivision_required = false;
        for (int y = 0; y < (cur_res / 2) && !subdivision_required; ++y) {
            for (int x = 0; x < (cur_res / 2) && !subdivision_required; ++x) {
                const simd_ivec4 mask = simd_cast(cur_mip[y * cur_res / 2 + x] > LumFractThreshold * total_lum);
                subdivision_required |= mask.not_all_zeros();
            }
        }

        if (!subdivision_required) {
            break;
        }

        cur_res *= 2;
    }

    //
    // Drop not needed levels
    //

    while (the_last_required_lod != 0) {
        for (int i = 1; i < int(env_map_qtree_.mips.size()); ++i) {
            env_map_qtree_.mips[i - 1] = std::move(env_map_qtree_.mips[i]);
        }
        env_map_qtree_.res /= 2;
        env_map_qtree_.mips.pop_back();
        --the_last_required_lod;
    }

    env_.qtree_levels = int(env_map_qtree_.mips.size());
    for (int i = 0; i < env_.qtree_levels; ++i) {
        env_.qtree_mips[i] = value_ptr(env_map_qtree_.mips[i][0]);
    }
    for (int i = env_.qtree_levels; i < countof(env_.qtree_mips); ++i) {
        env_.qtree_mips[i] = nullptr;
    }

    //
    // Upload texture
    //

    int req_size = 0, mip_offsets[16] = {};
    for (int i = 0; i < env_.qtree_levels; ++i) {
        mip_offsets[i] = req_size;
        req_size += 4096 * int((env_map_qtree_.mips[i].size() * sizeof(simd_fvec4) + 4096 - 1) / 4096);
    }

    temp_stage_buf = Buffer("Temp upload buf", ctx_, eBufType::Upload, req_size);
    uint8_t *stage_data = temp_stage_buf.Map();

    for (int i = 0; i < env_.qtree_levels; ++i) {
        const int res = (env_map_qtree_.res >> i) / 2;
        assert(res * res == env_map_qtree_.mips[i].size());

        int j = mip_offsets[i];
        for (int y = 0; y < res; ++y) {
            memcpy(&stage_data[j], &env_map_qtree_.mips[i][y * res], res * sizeof(simd_fvec4));
            j += round_up(res * sizeof(simd_fvec4), TextureDataPitchAlignment);
        }
    }

    Tex2DParams p;
    p.w = p.h = (env_map_qtree_.res / 2);
    p.format = eTexFormat::RawRGBA32F;
    p.mip_count = env_.qtree_levels;
    p.usage = eTexUsageBits::Sampled | eTexUsageBits::Transfer;

    env_map_qtree_.tex = Texture2D("Env map qtree", ctx_, p, ctx_->default_memory_allocs(), log_);

    CommandBuffer cmd_buf = BegSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->temp_command_pool());

    for (int i = 0; i < env_.qtree_levels; ++i) {
        env_map_qtree_.tex.SetSubImage(i, 0, 0, (env_map_qtree_.res >> i) / 2, (env_map_qtree_.res >> i) / 2,
                                       eTexFormat::RawRGBA32F, temp_stage_buf, cmd_buf, mip_offsets[i],
                                       int(env_map_qtree_.mips[i].size() * sizeof(simd_fvec4)));
    }

    EndSingleTimeCommands(ctx_->api(), ctx_->device(), ctx_->graphics_queue(), cmd_buf, ctx_->temp_command_pool());

    temp_stage_buf.Unmap();
    temp_stage_buf.FreeImmediate();

    log_->Info("Env map qtree res is %i", env_map_qtree_.res);
}

inline void Ray::NS::Scene::RebuildLightTree_nolock() {
    aligned_vector<prim_t> primitives;
    primitives.reserve(lights_.size());

    struct additional_data_t {
        Ref::simd_fvec4 axis;
        float flux, omega_n, omega_e;
    };
    aligned_vector<additional_data_t> additional_data;
    additional_data.reserve(lights_.size());

    for (auto it = lights_.cbegin(); it != lights_.cend(); ++it) {
        const light_t &l = *it;
        Ref::simd_fvec4 bbox_min = 0.0f, bbox_max = 0.0f, axis = {0.0f, 1.0f, 0.0f, 0.0f};
        float area = 1.0f, omega_n = 0.0f, omega_e = 0.0f;

        switch (l.type) {
        case LIGHT_TYPE_SPHERE: {
            const auto pos = Ref::simd_fvec4{l.sph.pos[0], l.sph.pos[1], l.sph.pos[2], 0.0f};

            bbox_min = pos - Ref::simd_fvec4{l.sph.radius, l.sph.radius, l.sph.radius, 0.0f};
            bbox_max = pos + Ref::simd_fvec4{l.sph.radius, l.sph.radius, l.sph.radius, 0.0f};
            if (l.sph.area != 0.0f) {
                area = l.sph.area;
            }
            omega_n = PI; // normals in all directions
            omega_e = PI / 2.0f;
        } break;
        case LIGHT_TYPE_DIR: {
            bbox_min = Ref::simd_fvec4{-MAX_DIST, -MAX_DIST, -MAX_DIST, 0.0f};
            bbox_max = Ref::simd_fvec4{MAX_DIST, MAX_DIST, MAX_DIST, 0.0f};
            axis = Ref::simd_fvec4{l.dir.dir[0], l.dir.dir[1], l.dir.dir[2], 0.0f};
            omega_n = 0.0f; // single normal
            omega_e = l.dir.angle;
            if (l.dir.angle != 0.0f) {
                const float radius = tanf(l.dir.angle);
                area = (PI * radius * radius);
            }
        } break;
        case LIGHT_TYPE_LINE: {
            const auto pos = Ref::simd_fvec4{l.line.pos[0], l.line.pos[1], l.line.pos[2], 0.0f};
            auto light_u = Ref::simd_fvec4{l.line.u[0], l.line.u[1], l.line.u[2], 0.0f},
                 light_dir = Ref::simd_fvec4{l.line.v[0], l.line.v[1], l.line.v[2], 0.0f};
            Ref::simd_fvec4 light_v = NS::cross(light_u, light_dir);

            light_u *= l.line.radius;
            light_v *= l.line.radius;
            light_dir *= 0.5f * l.line.height;

            const Ref::simd_fvec4 p0 = pos + light_dir + light_u + light_v, p1 = pos + light_dir + light_u - light_v,
                                  p2 = pos + light_dir - light_u + light_v, p3 = pos + light_dir - light_u - light_v,
                                  p4 = pos - light_dir + light_u + light_v, p5 = pos - light_dir + light_u - light_v,
                                  p6 = pos - light_dir - light_u + light_v, p7 = pos - light_dir - light_u - light_v;

            bbox_min = min(min(min(p0, p1), min(p2, p3)), min(min(p4, p5), min(p6, p7)));
            bbox_max = max(max(max(p0, p1), max(p2, p3)), max(max(p4, p5), max(p6, p7)));
            area = l.line.area;
            omega_n = PI; // normals in all directions
            omega_e = PI / 2.0f;
        } break;
        case LIGHT_TYPE_RECT: {
            const auto pos = Ref::simd_fvec4{l.rect.pos[0], l.rect.pos[1], l.rect.pos[2], 0.0f};
            const auto u = 0.5f * Ref::simd_fvec4{l.rect.u[0], l.rect.u[1], l.rect.u[2], 0.0f};
            const auto v = 0.5f * Ref::simd_fvec4{l.rect.v[0], l.rect.v[1], l.rect.v[2], 0.0f};

            const Ref::simd_fvec4 p0 = pos + u + v, p1 = pos + u - v, p2 = pos - u + v, p3 = pos - u - v;
            bbox_min = min(min(p0, p1), min(p2, p3));
            bbox_max = max(max(p0, p1), max(p2, p3));
            area = l.rect.area;

            axis = normalize(NS::cross(u, v));
            omega_n = 0.0f; // single normal
            omega_e = PI / 2.0f;
        } break;
        case LIGHT_TYPE_DISK: {
            const auto pos = Ref::simd_fvec4{l.disk.pos[0], l.disk.pos[1], l.disk.pos[2], 0.0f};
            const auto u = 0.5f * Ref::simd_fvec4{l.disk.u[0], l.disk.u[1], l.disk.u[2], 0.0f};
            const auto v = 0.5f * Ref::simd_fvec4{l.disk.v[0], l.disk.v[1], l.disk.v[2], 0.0f};

            const Ref::simd_fvec4 p0 = pos + u + v, p1 = pos + u - v, p2 = pos - u + v, p3 = pos - u - v;
            bbox_min = min(min(p0, p1), min(p2, p3));
            bbox_max = max(max(p0, p1), max(p2, p3));
            area = l.disk.area;

            axis = normalize(NS::cross(u, v));
            omega_n = 0.0f; // single normal
            omega_e = PI / 2.0f;
        } break;
        case LIGHT_TYPE_TRI: {
            const transform_t &ltr = transforms_[l.tri.xform_index];
            const uint32_t ltri_index = l.tri.tri_index;

            const vertex_t &v1 = vertices_cpu_[vtx_indices_cpu_[ltri_index * 3 + 0]];
            const vertex_t &v2 = vertices_cpu_[vtx_indices_cpu_[ltri_index * 3 + 1]];
            const vertex_t &v3 = vertices_cpu_[vtx_indices_cpu_[ltri_index * 3 + 2]];

            auto p1 = Ref::simd_fvec4(v1.p[0], v1.p[1], v1.p[2], 0.0f),
                 p2 = Ref::simd_fvec4(v2.p[0], v2.p[1], v2.p[2], 0.0f),
                 p3 = Ref::simd_fvec4(v3.p[0], v3.p[1], v3.p[2], 0.0f);

            p1 = TransformPoint(p1, ltr.xform);
            p2 = TransformPoint(p2, ltr.xform);
            p3 = TransformPoint(p3, ltr.xform);

            bbox_min = min(p1, min(p2, p3));
            bbox_max = max(p1, max(p2, p3));

            Ref::simd_fvec4 light_forward = NS::cross(p2 - p1, p3 - p1);
            area = 0.5f * length(light_forward);

            axis = normalize(light_forward);
            omega_n = PI; // normals in all directions (triangle lights are double-sided)
            omega_e = PI / 2.0f;
        } break;
        case LIGHT_TYPE_ENV: {
            bbox_min = Ref::simd_fvec4{-MAX_DIST, -MAX_DIST, -MAX_DIST, 0.0f};
            bbox_max = Ref::simd_fvec4{MAX_DIST, MAX_DIST, MAX_DIST, 0.0f};
            omega_n = PI; // normals in all directions
            omega_e = PI / 2.0f;
        } break;
        }

        primitives.push_back({0, 0, 0, bbox_min, bbox_max});

        const float flux = (l.col[0] + l.col[1] + l.col[2]) * area;
        additional_data.push_back({axis, flux, omega_n, omega_e});
    }

    light_wnodes_.Clear();

    if (primitives.empty()) {
        return;
    }

    std::vector<bvh_node_t> temp_nodes;

    std::vector<uint32_t> li_indices;
    li_indices.reserve(primitives.size());

    bvh_settings_t s;
    s.oversplit_threshold = -1.0f;
    s.allow_spatial_splits = false;
    s.min_primitives_in_leaf = 1;
    PreprocessPrims_SAH(primitives, nullptr, 0, s, temp_nodes, li_indices);

    std::vector<light_bvh_node_t> temp_lnodes(temp_nodes.size(), light_bvh_node_t{});
    for (uint32_t i = 0; i < temp_nodes.size(); ++i) {
        static_cast<bvh_node_t &>(temp_lnodes[i]) = temp_nodes[i];
        if ((temp_nodes[i].prim_index & LEAF_NODE_BIT) != 0) {
            const uint32_t li_index = li_indices[temp_nodes[i].prim_index & PRIM_INDEX_BITS];
            memcpy(temp_lnodes[i].axis, value_ptr(additional_data[li_index].axis), 3 * sizeof(float));
            temp_lnodes[i].flux = additional_data[li_index].flux;
            temp_lnodes[i].omega_n = additional_data[li_index].omega_n;
            temp_lnodes[i].omega_e = additional_data[li_index].omega_e;
        }
    }

    std::vector<uint32_t> parent_indices(temp_lnodes.size());
    parent_indices[0] = 0xffffffff; // root node has no parent

    std::vector<uint32_t> leaf_indices;
    leaf_indices.reserve(primitives.size());

    SmallVector<uint32_t, 128> stack;
    stack.push_back(0);
    while (!stack.empty()) {
        const uint32_t i = stack.back();
        stack.pop_back();

        if ((temp_lnodes[i].prim_index & LEAF_NODE_BIT) == 0) {
            const uint32_t left_child = temp_lnodes[i].left_child,
                           right_child = (temp_lnodes[i].right_child & RIGHT_CHILD_BITS);
            parent_indices[left_child] = parent_indices[right_child] = i;

            stack.push_back(left_child);
            stack.push_back(right_child);
        } else {
            leaf_indices.push_back(i);
        }
    }

    // Propagate flux and cone up the hierarchy
    std::vector<uint32_t> to_process;
    to_process.reserve(temp_lnodes.size());
    to_process.insert(end(to_process), begin(leaf_indices), end(leaf_indices));
    for (uint32_t i = 0; i < uint32_t(to_process.size()); ++i) {
        const uint32_t n = to_process[i];
        const uint32_t parent = parent_indices[n];
        if (parent == 0xffffffff) {
            continue;
        }

        temp_lnodes[parent].flux += temp_lnodes[n].flux;
        if (temp_lnodes[parent].axis[0] == 0.0f && temp_lnodes[parent].axis[1] == 0.0f &&
            temp_lnodes[parent].axis[2] == 0.0f) {
            memcpy(temp_lnodes[parent].axis, temp_lnodes[n].axis, 3 * sizeof(float));
            temp_lnodes[parent].omega_n = temp_lnodes[n].omega_n;
        } else {
            auto axis1 = Ref::simd_fvec4{temp_lnodes[parent].axis}, axis2 = Ref::simd_fvec4{temp_lnodes[n].axis};
            axis1.set<3>(0.0f);
            axis2.set<3>(0.0f);

            const float angle_between = acosf(clamp(dot(axis1, axis2), -1.0f, 1.0f));

            axis1 += axis2;
            const float axis_length = length(axis1);
            if (axis_length != 0.0f) {
                axis1 /= axis_length;
            } else {
                axis1 = Ref::simd_fvec4{0.0f, 1.0f, 0.0f, 0.0f};
            }

            memcpy(temp_lnodes[parent].axis, value_ptr(axis1), 3 * sizeof(float));

            temp_lnodes[parent].omega_n =
                fmin(0.5f * (temp_lnodes[parent].omega_n +
                             fmax(temp_lnodes[parent].omega_n, angle_between + temp_lnodes[n].omega_n)),
                     PI);
        }
        temp_lnodes[parent].omega_e = fmax(temp_lnodes[parent].omega_e, temp_lnodes[n].omega_e);
        if ((temp_lnodes[parent].left_child & LEFT_CHILD_BITS) == n) {
            to_process.push_back(parent);
        }
    }

    // Remove indices indirection
    for (uint32_t i = 0; i < leaf_indices.size(); ++i) {
        light_bvh_node_t &n = temp_lnodes[leaf_indices[i]];
        assert((n.prim_index & LEAF_NODE_BIT) != 0);
        {
            const uint32_t li_index = li_indices[n.prim_index & PRIM_INDEX_BITS];
            n.prim_index &= ~PRIM_INDEX_BITS;
            n.prim_index |= li_index;
        }
    }

    aligned_vector<light_wbvh_node_t> temp_light_wnodes;
    const uint32_t root_node = FlattenBVH_r(temp_lnodes.data(), 0, 0xffffffff, temp_light_wnodes);
    assert(root_node == 0);

    light_wnodes_.Append(temp_light_wnodes.data(), temp_light_wnodes.size());
}
