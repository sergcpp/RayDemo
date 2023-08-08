#include "SceneCPU.h"

#include <cassert>
#include <cstring>

#include <functional>

#include "../Log.h"
#include "Atmosphere.h"
#include "BVHSplit.h"
#include "CoreRef.h"
#include "TextureUtils.h"
#include "Time_.h"

#define CLAMP(val, min, max) (val < min ? min : (val > max ? max : val))

namespace Ray {
namespace Cpu {
Ref::simd_fvec4 rgb_to_rgbe(const Ref::simd_fvec4 &rgb) {
    float max_component = std::max(std::max(rgb.get<0>(), rgb.get<1>()), rgb.get<2>());
    if (max_component < 1e-32) {
        return Ref::simd_fvec4{0.0f};
    }

    int exponent;
    const float factor = std::frexp(max_component, &exponent) * 256.0f / max_component;

    return Ref::simd_fvec4{rgb.get<0>() * factor, rgb.get<1>() * factor, rgb.get<2>() * factor, float(exponent + 128)};
}
} // namespace Cpu
} // namespace Ray

Ray::Cpu::Scene::Scene(ILog *log, const bool use_wide_bvh) : use_wide_bvh_(use_wide_bvh) {
    SceneBase::log_ = log;
    SetEnvironment({});
}

Ray::Cpu::Scene::~Scene() {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    for (auto it = mesh_instances_.begin(); it != mesh_instances_.end();) {
        MeshInstanceHandle to_delete = {it.index(), it.block()};
        ++it;
        Scene::RemoveMeshInstance_nolock(to_delete);
    }
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        MeshHandle to_delete = {it.index(), it.block()};
        ++it;
        Scene::RemoveMesh_nolock(to_delete);
    }
    for (auto it = lights_.begin(); it != lights_.end();) {
        LightHandle to_delete = {it.index(), it.block()};
        ++it;
        Scene::RemoveLight_nolock(to_delete);
    }

    materials_.clear();
    lights_.clear();
}

void Ray::Cpu::Scene::GetEnvironment(environment_desc_t &env) {
    std::shared_lock<std::shared_timed_mutex> lock(mtx_);

    memcpy(env.env_col, env_.env_col, 3 * sizeof(float));
    env.env_map = TextureHandle{env_.env_map};
    memcpy(env.back_col, env_.back_col, 3 * sizeof(float));
    env.back_map = TextureHandle{env_.back_map};
    env.env_map_rotation = env_.env_map_rotation;
    env.back_map_rotation = env_.back_map_rotation;
    env.multiple_importance = env_.multiple_importance;
}

void Ray::Cpu::Scene::SetEnvironment(const environment_desc_t &env) {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    memcpy(env_.env_col, env.env_col, 3 * sizeof(float));
    env_.env_map = env.env_map._index;
    memcpy(env_.back_col, env.back_col, 3 * sizeof(float));
    env_.back_map = env.back_map._index;
    env_.env_map_rotation = env.env_map_rotation;
    env_.back_map_rotation = env.back_map_rotation;
    env_.multiple_importance = env.multiple_importance;
}

Ray::TextureHandle Ray::Cpu::Scene::AddTexture(const tex_desc_t &_t) {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const int res[2] = {_t.w, _t.h};

    bool recostruct_z = false;

    int storage = -1, index = -1;
    if (_t.format == eTextureFormat::RGBA8888) {
        const auto *rgba_data = reinterpret_cast<const color_rgba8_t *>(_t.data);
        if (!_t.is_normalmap) {
            storage = 0;
            index = tex_storage_rgba_.Allocate(rgba_data, res, _t.generate_mipmaps);
        } else {
            // TODO: get rid of this allocation
            std::unique_ptr<color_rg8_t[]> repacked_data(new color_rg8_t[res[0] * res[1]]);
            for (int i = 0; i < res[0] * res[1]; ++i) {
                repacked_data[i].v[0] = rgba_data[i].v[0];
                repacked_data[i].v[1] = rgba_data[i].v[1];
                recostruct_z |= (rgba_data[i].v[2] < 250);
            }
            storage = 2;
            index = tex_storage_rg_.Allocate(repacked_data.get(), res, _t.generate_mipmaps);
        }
    } else if (_t.format == eTextureFormat::RGB888) {
        const auto *rgb_data = reinterpret_cast<const color_rgb8_t *>(_t.data);
        if (!_t.is_normalmap) {
            storage = 1;
            index = tex_storage_rgb_.Allocate(rgb_data, res, _t.generate_mipmaps);
        } else {
            // TODO: get rid of this allocation
            std::unique_ptr<color_rg8_t[]> repacked_data(new color_rg8_t[res[0] * res[1]]);
            for (int i = 0; i < res[0] * res[1]; ++i) {
                repacked_data[i].v[0] = rgb_data[i].v[0];
                repacked_data[i].v[1] = rgb_data[i].v[1];
                recostruct_z |= (rgb_data[i].v[2] < 250);
            }
            storage = 2;
            index = tex_storage_rg_.Allocate(repacked_data.get(), res, _t.generate_mipmaps);
        }
    } else if (_t.format == eTextureFormat::RG88) {
        storage = 2;
        index = tex_storage_rg_.Allocate(reinterpret_cast<const color_rg8_t *>(_t.data), res, _t.generate_mipmaps);
    } else if (_t.format == eTextureFormat::R8) {
        storage = 3;
        index = tex_storage_r_.Allocate(reinterpret_cast<const color_r8_t *>(_t.data), res, _t.generate_mipmaps);
    }

    if (storage == -1) {
        return InvalidTextureHandle;
    }

    log_->Info("Ray: Texture '%s' loaded (storage = %i, %ix%i)", _t.name, storage, _t.w, _t.h);
    log_->Info("Ray: Storages are (RGBA[%i], RGB[%i], RG[%i], R[%i])", tex_storage_rgba_.img_count(),
               tex_storage_rgb_.img_count(), tex_storage_rg_.img_count(), tex_storage_r_.img_count());

    uint32_t ret = 0;

    ret |= uint32_t(storage) << 28;
    if (_t.is_srgb) {
        ret |= TEX_SRGB_BIT;
    }
    if (recostruct_z) {
        ret |= TEX_RECONSTRUCT_Z_BIT;
    }
    ret |= index;

    return TextureHandle{ret};
}

Ray::MaterialHandle Ray::Cpu::Scene::AddMaterial_nolock(const shading_node_desc_t &m) {
    material_t mat = {};

    mat.type = m.type;
    mat.textures[BASE_TEXTURE] = m.base_texture._index;
    mat.roughness_unorm = pack_unorm_16(CLAMP(m.roughness, 0.0f, 1.0f));
    mat.textures[ROUGH_TEXTURE] = m.roughness_texture._index;
    memcpy(&mat.base_color[0], &m.base_color[0], 3 * sizeof(float));
    mat.ior = m.ior;
    mat.tangent_rotation = 0.0f;
    mat.flags = 0;

    if (m.type == eShadingNode::Diffuse) {
        mat.sheen_unorm = pack_unorm_16(CLAMP(0.5f * m.sheen, 0.0f, 1.0f));
        mat.sheen_tint_unorm = pack_unorm_16(CLAMP(m.tint, 0.0f, 1.0f));
        mat.textures[METALLIC_TEXTURE] = m.metallic_texture._index;
    } else if (m.type == eShadingNode::Glossy) {
        mat.tangent_rotation = 2.0f * PI * m.anisotropic_rotation;
        mat.textures[METALLIC_TEXTURE] = m.metallic_texture._index;
        mat.tint_unorm = pack_unorm_16(CLAMP(m.tint, 0.0f, 1.0f));
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
    mat.normal_map_strength_unorm = pack_unorm_16(CLAMP(m.normal_map_intensity, 0.0f, 1.0f));

    const std::pair<uint32_t, uint32_t> ret = materials_.push(mat);
    return MaterialHandle{ret.first, ret.second};
}

Ray::MaterialHandle Ray::Cpu::Scene::AddMaterial(const principled_mat_desc_t &m) {
    material_t main_mat = {};

    main_mat.type = eShadingNode::Principled;
    main_mat.textures[BASE_TEXTURE] = m.base_texture._index;
    memcpy(&main_mat.base_color[0], &m.base_color[0], 3 * sizeof(float));
    main_mat.sheen_unorm = pack_unorm_16(CLAMP(0.5f * m.sheen, 0.0f, 1.0f));
    main_mat.sheen_tint_unorm = pack_unorm_16(CLAMP(m.sheen_tint, 0.0f, 1.0f));
    main_mat.roughness_unorm = pack_unorm_16(CLAMP(m.roughness, 0.0f, 1.0f));
    main_mat.tangent_rotation = 2.0f * PI * CLAMP(m.anisotropic_rotation, 0.0f, 1.0f);
    main_mat.textures[ROUGH_TEXTURE] = m.roughness_texture._index;
    main_mat.metallic_unorm = pack_unorm_16(CLAMP(m.metallic, 0.0f, 1.0f));
    main_mat.textures[METALLIC_TEXTURE] = m.metallic_texture._index;
    main_mat.ior = m.ior;
    main_mat.flags = 0;
    main_mat.transmission_unorm = pack_unorm_16(CLAMP(m.transmission, 0.0f, 1.0f));
    main_mat.transmission_roughness_unorm = pack_unorm_16(CLAMP(m.transmission_roughness, 0.0f, 1.0f));
    main_mat.textures[NORMALS_TEXTURE] = m.normal_map._index;
    main_mat.normal_map_strength_unorm = pack_unorm_16(CLAMP(m.normal_map_intensity, 0.0f, 1.0f));
    main_mat.anisotropic_unorm = pack_unorm_16(CLAMP(m.anisotropic, 0.0f, 1.0f));
    main_mat.specular_unorm = pack_unorm_16(CLAMP(m.specular, 0.0f, 1.0f));
    main_mat.textures[SPECULAR_TEXTURE] = m.specular_texture._index;
    main_mat.specular_tint_unorm = pack_unorm_16(CLAMP(m.specular_tint, 0.0f, 1.0f));
    main_mat.clearcoat_unorm = pack_unorm_16(CLAMP(m.clearcoat, 0.0f, 1.0f));
    main_mat.clearcoat_roughness_unorm = pack_unorm_16(CLAMP(m.clearcoat_roughness, 0.0f, 1.0f));

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> rn = materials_.push(main_mat);
    auto root_node = MaterialHandle{rn.first, rn.second};
    MaterialHandle emissive_node = InvalidMaterialHandle, transparent_node = InvalidMaterialHandle;

    if (m.emission_strength > 0.0f &&
        (m.emission_color[0] > 0.0f || m.emission_color[1] > 0.0f || m.emission_color[2] > 0.0f)) {
        shading_node_desc_t emissive_desc;
        emissive_desc.type = eShadingNode::Emissive;

        memcpy(emissive_desc.base_color, m.emission_color, 3 * sizeof(float));
        emissive_desc.base_texture = m.emission_texture;
        emissive_desc.strength = m.emission_strength;

        emissive_node = AddMaterial_nolock(emissive_desc);
    }

    if (m.alpha != 1.0f || m.alpha_texture != InvalidTextureHandle) {
        shading_node_desc_t transparent_desc;
        transparent_desc.type = eShadingNode::Transparent;

        transparent_node = AddMaterial_nolock(transparent_desc);
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

            root_node = AddMaterial_nolock(mix_node);
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

            root_node = AddMaterial_nolock(mix_node);
        }
    }

    return MaterialHandle{root_node};
}

Ray::MeshHandle Ray::Cpu::Scene::AddMesh(const mesh_desc_t &_m) {
    bvh_settings_t s;
    s.oversplit_threshold = 0.95f;
    s.allow_spatial_splits = _m.allow_spatial_splits;
    s.use_fast_bvh_build = _m.use_fast_bvh_build;

    const uint64_t t1 = Ray::GetTimeMs();

    std::vector<bvh_node_t> temp_nodes;
    aligned_vector<tri_accel_t> temp_tris;
    aligned_vector<mtri_accel_t> temp_mtris;
    std::vector<uint32_t> temp_tri_indices;

    PreprocessMesh(_m.vtx_attrs, {_m.vtx_indices, _m.vtx_indices_count}, _m.layout, _m.base_vertex, s, temp_nodes,
                   temp_tris, temp_tri_indices, temp_mtris);

    log_->Info("Ray: Mesh \'%s\' preprocessed in %lldms", _m.name ? _m.name : "(unknown)", (Ray::GetTimeMs() - t1));

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    mesh_t m = {};
    m.node_index = uint32_t(nodes_.size());
    m.node_count = uint32_t(temp_nodes.size());

    const std::pair<uint32_t, uint32_t> tris_index = tris_.Allocate(uint32_t(temp_tris.size()));

    m.tris_index = tris_index.first;
    m.tris_block = tris_index.second;
    m.tris_count = uint32_t(temp_tris.size());
    for (uint32_t i = 0; i < uint32_t(temp_tris.size()); ++i) {
        tris_[m.tris_index + i] = temp_tris[i];
    }

    const std::pair<uint32_t, uint32_t> mtris_index = mtris_.Allocate(uint32_t(temp_mtris.size()));
    assert(mtris_index.first == m.tris_index / 8);
    for (uint32_t i = 0; i < uint32_t(temp_mtris.size()); ++i) {
        mtris_[mtris_index.first + i] = temp_mtris[i];
    }

    const auto tris_offset = uint32_t(tri_materials_.size());

    const std::pair<uint32_t, uint32_t> tri_indices_index = tri_indices_.Allocate(uint32_t(temp_tri_indices.size()));
    assert(tri_indices_index.first == m.tris_index);
    assert(tri_indices_index.second == m.tris_block);
    for (uint32_t i = 0; i < uint32_t(temp_tri_indices.size()); ++i) {
        tri_indices_[tri_indices_index.first + i] = tris_offset + temp_tri_indices[i];
    }

    { // Apply required offsets
        const auto nodes_offset = uint32_t(nodes_.size());

        for (bvh_node_t &n : temp_nodes) {
            if (n.prim_index & LEAF_NODE_BIT) {
                n.prim_index += tri_indices_index.first;
            } else {
                n.left_child += nodes_offset;
                n.right_child += nodes_offset;
            }
        }
    }

    memcpy(m.bbox_min, temp_nodes[0].bbox_min, 3 * sizeof(float));
    memcpy(m.bbox_max, temp_nodes[0].bbox_max, 3 * sizeof(float));

    if (use_wide_bvh_) {
        const uint64_t t2 = Ray::GetTimeMs();

        const auto before_count = uint32_t(mnodes_.size());
        const uint32_t new_root = FlattenBVH_Recursive(temp_nodes.data(), m.node_index, 0xffffffff, mnodes_);

        m.node_index = new_root;
        m.node_count = uint32_t(mnodes_.size() - before_count);

        log_->Info("Ray: Mesh \'%s\' BVH flattened in %lldms", _m.name ? _m.name : "(unknown)",
                   (Ray::GetTimeMs() - t2));
    } else {
        nodes_.insert(nodes_.end(), temp_nodes.begin(), temp_nodes.end());
    }

    const auto tri_materials_start = uint32_t(tri_materials_.size());
    tri_materials_.resize(tri_materials_start + (_m.vtx_indices_count / 3));

    // init triangle materials
    for (const shape_desc_t &sh : _m.shapes) {
        bool is_front_solid = true, is_back_solid = true;

        uint32_t material_stack[32];
        material_stack[0] = sh.front_mat._index;
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

        material_stack[0] = sh.back_mat._index;
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

        for (size_t i = sh.vtx_start; i < sh.vtx_start + sh.vtx_count; i += 3) {
            tri_mat_data_t &tri_mat = tri_materials_[tri_materials_start + (i / 3)];

            assert(sh.front_mat._index < (1 << 14) && "Not enough bits to reference material!");
            assert(sh.back_mat._index < (1 << 14) && "Not enough bits to reference material!");

            tri_mat.front_mi = uint16_t(sh.front_mat._index);
            if (is_front_solid) {
                tri_mat.front_mi |= MATERIAL_SOLID_BIT;
            }

            tri_mat.back_mi = uint16_t(sh.back_mat._index);
            if (is_back_solid) {
                tri_mat.back_mi |= MATERIAL_SOLID_BIT;
            }
        }
    }

    std::vector<uint32_t> new_vtx_indices;
    new_vtx_indices.reserve(_m.vtx_indices_count);
    for (size_t i = 0; i < _m.vtx_indices_count; i++) {
        new_vtx_indices.push_back(_m.vtx_indices[i] + _m.base_vertex + uint32_t(vertices_.size()));
    }

    const size_t stride = AttrStrides[int(_m.layout)];

    // add attributes
    const size_t new_vertices_start = vertices_.size();
    vertices_.resize(new_vertices_start + _m.vtx_attrs_count);
    for (size_t i = 0; i < _m.vtx_attrs_count; ++i) {
        vertex_t &v = vertices_[new_vertices_start + i];

        memcpy(&v.p[0], (_m.vtx_attrs + i * stride), 3 * sizeof(float));
        memcpy(&v.n[0], (_m.vtx_attrs + i * stride + 3), 3 * sizeof(float));

        if (_m.layout == eVertexLayout::PxyzNxyzTuv) {
            memcpy(&v.t[0], (_m.vtx_attrs + i * stride + 6), 2 * sizeof(float));
            // v.t[1][0] = v.t[1][1] = 0.0f;
            v.b[0] = v.b[1] = v.b[2] = 0.0f;
        } else if (_m.layout == eVertexLayout::PxyzNxyzTuvTuv) {
            memcpy(&v.t[0], (_m.vtx_attrs + i * stride + 6), 2 * sizeof(float));
            // memcpy(&v.t[1][0], (_m.vtx_attrs + i * stride + 8), 2 * sizeof(float));
            v.b[0] = v.b[1] = v.b[2] = 0.0f;
        } else if (_m.layout == eVertexLayout::PxyzNxyzBxyzTuv) {
            memcpy(&v.b[0], (_m.vtx_attrs + i * stride + 6), 3 * sizeof(float));
            memcpy(&v.t[0], (_m.vtx_attrs + i * stride + 9), 2 * sizeof(float));
            // v.t[1][0] = v.t[1][1] = 0.0f;
        } else if (_m.layout == eVertexLayout::PxyzNxyzBxyzTuvTuv) {
            memcpy(&v.b[0], (_m.vtx_attrs + i * stride + 6), 3 * sizeof(float));
            memcpy(&v.t[0], (_m.vtx_attrs + i * stride + 9), 2 * sizeof(float));
            // memcpy(&v.t[1][0], (_m.vtx_attrs + i * stride + 11), 2 * sizeof(float));
        }
    }

    if (_m.layout == eVertexLayout::PxyzNxyzTuv || _m.layout == eVertexLayout::PxyzNxyzTuvTuv) {
        ComputeTangentBasis(0, new_vertices_start, vertices_, new_vtx_indices, &new_vtx_indices[0],
                            new_vtx_indices.size());
    }

    m.vert_index = uint32_t(vtx_indices_.size());
    m.vert_count = uint32_t(new_vtx_indices.size());

    vtx_indices_.insert(vtx_indices_.end(), new_vtx_indices.begin(), new_vtx_indices.end());

    const std::pair<uint32_t, uint32_t> ret = meshes_.emplace(m);
    return MeshHandle{ret.first, ret.second};
}

void Ray::Cpu::Scene::RemoveMesh_nolock(const MeshHandle i) {
    const mesh_t &m = meshes_[i._index];

    const uint32_t node_index = m.node_index, node_count = m.node_count;
    const uint32_t tris_block = m.tris_block;

    meshes_.Erase(i._block);

    bool rebuild_needed = false;
    for (auto it = mesh_instances_.begin(); it != mesh_instances_.end();) {
        mesh_instance_t &mi = *it;
        if (mi.mesh_index == i._index) {
            it = mesh_instances_.erase(it);
            rebuild_needed = true;
        } else {
            ++it;
        }
    }

    tris_.Erase(tris_block);
    mtris_.Erase(tris_block);
    tri_indices_.Erase(tris_block);
    RemoveNodes_nolock(node_index, node_count);

    if (rebuild_needed) {
        RebuildTLAS_nolock();
    }
}

Ray::LightHandle Ray::Cpu::Scene::AddLight(const directional_light_desc_t &_l) {
    light_t l = {};

    l.type = LIGHT_TYPE_DIR;
    l.cast_shadow = _l.cast_shadow;
    l.visible = false;

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
    li_indices_.push_back(light_index.first);
    if (_l.visible) {
        visible_lights_.push_back(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

Ray::LightHandle Ray::Cpu::Scene::AddLight(const sphere_light_desc_t &_l) {
    light_t l = {};

    l.type = LIGHT_TYPE_SPHERE;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));
    memcpy(&l.sph.pos[0], &_l.position[0], 3 * sizeof(float));

    l.sph.area = 4.0f * PI * _l.radius * _l.radius;
    l.sph.radius = _l.radius;
    l.sph.spot = l.sph.blend = -1.0f;

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.push_back(light_index.first);
    if (_l.visible) {
        visible_lights_.push_back(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

Ray::LightHandle Ray::Cpu::Scene::AddLight(const spot_light_desc_t &_l) {
    light_t l = {};

    l.type = LIGHT_TYPE_SPHERE;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));
    memcpy(&l.sph.pos[0], &_l.position[0], 3 * sizeof(float));
    memcpy(&l.sph.dir[0], &_l.direction[0], 3 * sizeof(float));

    l.sph.area = 4.0f * PI * _l.radius * _l.radius;
    l.sph.radius = _l.radius;
    l.sph.spot = 0.5f * PI * _l.spot_size / 180.0f;
    l.sph.blend = _l.spot_blend * _l.spot_blend;

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.push_back(light_index.first);
    if (_l.visible) {
        visible_lights_.push_back(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

Ray::LightHandle Ray::Cpu::Scene::AddLight(const rect_light_desc_t &_l, const float *xform) {
    light_t l = {};

    l.type = LIGHT_TYPE_RECT;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.sky_portal = _l.sky_portal;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));

    l.rect.pos[0] = xform[12];
    l.rect.pos[1] = xform[13];
    l.rect.pos[2] = xform[14];

    l.rect.area = _l.width * _l.height;

    const Ref::simd_fvec4 uvec = _l.width * TransformDirection(Ref::simd_fvec4{1.0f, 0.0f, 0.0f, 0.0f}, xform);
    const Ref::simd_fvec4 vvec = _l.height * TransformDirection(Ref::simd_fvec4{0.0f, 0.0f, 1.0f, 0.0f}, xform);

    memcpy(l.rect.u, value_ptr(uvec), 3 * sizeof(float));
    memcpy(l.rect.v, value_ptr(vvec), 3 * sizeof(float));

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.push_back(light_index.first);
    if (_l.visible) {
        visible_lights_.push_back(light_index.first);
    }
    if (_l.sky_portal) {
        blocker_lights_.push_back(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

Ray::LightHandle Ray::Cpu::Scene::AddLight(const disk_light_desc_t &_l, const float *xform) {
    light_t l = {};

    l.type = LIGHT_TYPE_DISK;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.sky_portal = _l.sky_portal;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));

    l.disk.pos[0] = xform[12];
    l.disk.pos[1] = xform[13];
    l.disk.pos[2] = xform[14];

    l.disk.area = 0.25f * PI * _l.size_x * _l.size_y;

    const Ref::simd_fvec4 uvec = _l.size_x * TransformDirection(Ref::simd_fvec4{1.0f, 0.0f, 0.0f, 0.0f}, xform);
    const Ref::simd_fvec4 vvec = _l.size_y * TransformDirection(Ref::simd_fvec4{0.0f, 0.0f, 1.0f, 0.0f}, xform);

    memcpy(l.disk.u, value_ptr(uvec), 3 * sizeof(float));
    memcpy(l.disk.v, value_ptr(vvec), 3 * sizeof(float));

    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> light_index = lights_.push(l);
    li_indices_.push_back(light_index.first);
    if (_l.visible) {
        visible_lights_.push_back(light_index.first);
    }
    if (_l.sky_portal) {
        blocker_lights_.push_back(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

Ray::LightHandle Ray::Cpu::Scene::AddLight(const line_light_desc_t &_l, const float *xform) {
    light_t l = {};

    l.type = LIGHT_TYPE_LINE;
    l.cast_shadow = _l.cast_shadow;
    l.visible = _l.visible;
    l.sky_portal = _l.sky_portal;

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
    li_indices_.push_back(light_index.first);
    if (_l.visible) {
        visible_lights_.push_back(light_index.first);
    }
    return LightHandle{light_index.first, light_index.second};
}

void Ray::Cpu::Scene::RemoveLight_nolock(const LightHandle i) {
    { // remove from compacted list
        auto it = find(begin(li_indices_), end(li_indices_), i._index);
        assert(it != end(li_indices_));
        li_indices_.erase(it);
    }

    if (lights_[i._index].visible) {
        auto it = find(begin(visible_lights_), end(visible_lights_), i._index);
        assert(it != end(visible_lights_));
        visible_lights_.erase(it);
    }

    if (lights_[i._index].sky_portal) {
        auto it = find(begin(blocker_lights_), end(blocker_lights_), i._index);
        assert(it != end(blocker_lights_));
        blocker_lights_.erase(it);
    }

    lights_.Erase(i._block);
}

Ray::MeshInstanceHandle Ray::Cpu::Scene::AddMeshInstance(const MeshHandle mesh, const float *xform) {
    std::unique_lock<std::shared_timed_mutex> lock(mtx_);

    const std::pair<uint32_t, uint32_t> mi_index = mesh_instances_.emplace();
    const std::pair<uint32_t, uint32_t> tr_index = transforms_.emplace();

    mesh_instance_t &mi = mesh_instances_.at(mi_index.first);
    mi.mesh_index = mesh._index;
    mi.mesh_block = mesh._block;
    mi.tr_index = tr_index.first;
    mi.tr_block = tr_index.second;

    { // find emissive triangles and add them as emitters
        const mesh_t &m = meshes_[mesh._index];
        for (uint32_t tri = (m.vert_index / 3); tri < (m.vert_index + m.vert_count) / 3; ++tri) {
            const tri_mat_data_t &tri_mat = tri_materials_[tri];

            const material_t &front_mat = materials_[tri_mat.front_mi & MATERIAL_INDEX_BITS];
            if (front_mat.type == eShadingNode::Emissive && (front_mat.flags & MAT_FLAG_MULT_IMPORTANCE)) {
                light_t new_light = {};
                new_light.cast_shadow = 1;
                new_light.type = LIGHT_TYPE_TRI;
                new_light.visible = 0;
                new_light.sky_portal = 0;
                new_light.tri.tri_index = tri;
                new_light.tri.xform_index = mi.tr_index;
                new_light.col[0] = front_mat.base_color[0] * front_mat.strength;
                new_light.col[1] = front_mat.base_color[1] * front_mat.strength;
                new_light.col[2] = front_mat.base_color[2] * front_mat.strength;
                const uint32_t index = lights_.push(new_light).first;
                li_indices_.push_back(index);
            }
        }
    }

    const MeshInstanceHandle ret = {mi_index.first, mi_index.second};
    SetMeshInstanceTransform_nolock(ret, xform);

    return ret;
}

void Ray::Cpu::Scene::SetMeshInstanceTransform_nolock(const MeshInstanceHandle mi_handle, const float *xform) {
    mesh_instance_t &mi = mesh_instances_[mi_handle._index];
    transform_t &tr = transforms_[mi.tr_index];

    memcpy(tr.xform, xform, 16 * sizeof(float));
    InverseMatrix(tr.xform, tr.inv_xform);

    const mesh_t &m = meshes_[mi.mesh_index];
    TransformBoundingBox(m.bbox_min, m.bbox_max, xform, mi.bbox_min, mi.bbox_max);

    RebuildTLAS_nolock();
}

void Ray::Cpu::Scene::RemoveMeshInstance_nolock(const MeshInstanceHandle i) {
    mesh_instance_t &mi = mesh_instances_[i._index];

    transforms_.Erase(mi.tr_block);
    mesh_instances_.Erase(i._block);

    RebuildTLAS_nolock();
}

void Ray::Cpu::Scene::Finalize() {
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
        }
        { // add env light source
            light_t l = {};

            l.type = LIGHT_TYPE_ENV;
            l.cast_shadow = 1;
            l.col[0] = l.col[1] = l.col[2] = 1.0f;

            const std::pair<uint32_t, uint32_t> li = lights_.push(l);
            env_map_light_ = LightHandle{li.first, li.second};
            li_indices_.push_back(env_map_light_._index);
        }
    }
}

void Ray::Cpu::Scene::RemoveNodes_nolock(uint32_t node_index, uint32_t node_count) {
    if (!node_count) {
        return;
    }

    if (!use_wide_bvh_) {
        nodes_.erase(std::next(nodes_.begin(), node_index), std::next(nodes_.begin(), node_index + node_count));
    } else {
        mnodes_.erase(std::next(mnodes_.begin(), node_index), std::next(mnodes_.begin(), node_index + node_count));
    }

    if ((!use_wide_bvh_ && node_index != nodes_.size()) || (use_wide_bvh_ && node_index != mnodes_.size())) {
        for (mesh_t &m : meshes_) {
            if (m.node_index > node_index) {
                m.node_index -= node_count;
            }
        }

        for (uint32_t i = node_index; i < nodes_.size(); i++) {
            bvh_node_t &n = nodes_[i];
            if ((n.prim_index & LEAF_NODE_BIT) == 0) {
                if (n.left_child > node_index) {
                    n.left_child -= node_count;
                }
                if ((n.right_child & RIGHT_CHILD_BITS) > node_index) {
                    n.right_child -= node_count;
                }
            }
        }

        for (uint32_t i = node_index; i < mnodes_.size(); i++) {
            mbvh_node_t &n = mnodes_[i];

            if ((n.child[0] & LEAF_NODE_BIT) == 0) {
                if (n.child[0] > node_index) {
                    n.child[0] -= node_count;
                }
                if (n.child[1] > node_index) {
                    n.child[1] -= node_count;
                }
                if (n.child[2] > node_index) {
                    n.child[2] -= node_count;
                }
                if (n.child[3] > node_index) {
                    n.child[3] -= node_count;
                }
                if (n.child[4] > node_index) {
                    n.child[4] -= node_count;
                }
                if (n.child[5] > node_index) {
                    n.child[5] -= node_count;
                }
                if (n.child[6] > node_index) {
                    n.child[6] -= node_count;
                }
                if (n.child[7] > node_index) {
                    n.child[7] -= node_count;
                }
            }
        }

        if (macro_nodes_root_ > node_index) {
            macro_nodes_root_ -= node_count;
        }
    }
}

void Ray::Cpu::Scene::RebuildTLAS_nolock() {
    RemoveNodes_nolock(macro_nodes_root_, macro_nodes_count_);
    mi_indices_.clear();

    macro_nodes_root_ = 0xffffffff;
    macro_nodes_count_ = 0;

    if (mesh_instances_.empty()) {
        return;
    }

    aligned_vector<prim_t> primitives;
    primitives.reserve(mesh_instances_.size());

    for (const mesh_instance_t &mi : mesh_instances_) {
        primitives.push_back({0, 0, 0, Ref::simd_fvec4{mi.bbox_min[0], mi.bbox_min[1], mi.bbox_min[2], 0.0f},
                              Ref::simd_fvec4{mi.bbox_max[0], mi.bbox_max[1], mi.bbox_max[2], 0.0f}});
    }

    macro_nodes_root_ = uint32_t(nodes_.size());
    macro_nodes_count_ = PreprocessPrims_SAH(primitives, nullptr, 0, {}, nodes_, mi_indices_);

    if (use_wide_bvh_) {
        const auto before_count = uint32_t(mnodes_.size());
        const uint32_t new_root = FlattenBVH_Recursive(nodes_.data(), macro_nodes_root_, 0xffffffff, mnodes_);

        macro_nodes_root_ = new_root;
        macro_nodes_count_ = static_cast<uint32_t>(mnodes_.size() - before_count);

        // nodes_ is temporary storage when wide BVH is used
        nodes_.clear();
    }
}

void Ray::Cpu::Scene::PrepareSkyEnvMap_nolock() {
    if (physical_sky_texture_ != InvalidTextureHandle) {
        tex_storages_[physical_sky_texture_._index >> 24]->Free(physical_sky_texture_._index & 0x00ffffff);
    }

    // Find directional light sources
    std::vector<uint32_t> dir_lights;
    for (const uint32_t li_index : li_indices_) {
        const light_t &l = lights_[li_index];
        if (l.type == LIGHT_TYPE_DIR) {
            dir_lights.push_back(li_index);
        }
    }

    if (dir_lights.empty()) {
        env_.env_map = InvalidTextureHandle._index;
        if (env_.back_map == PhysicalSkyTexture._index) {
            env_.back_map = InvalidTextureHandle._index;
        }
        return;
    }

    static const int SkyEnvRes[] = {512, 256};
    std::unique_ptr<color_rgba8_t[]> rgbe_pixels(new color_rgba8_t[SkyEnvRes[0] * SkyEnvRes[1]]);

    std::unique_ptr<color_rgb_t[]> rgb_pixels(new color_rgb_t[SkyEnvRes[0] * SkyEnvRes[1]]);

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

            rgb_pixels[y * SkyEnvRes[0] + x].v[0] = color.get<0>();
            rgb_pixels[y * SkyEnvRes[0] + x].v[1] = color.get<1>();
            rgb_pixels[y * SkyEnvRes[0] + x].v[2] = color.get<2>();

            color = rgb_to_rgbe(color);

            rgbe_pixels[y * SkyEnvRes[0] + x].v[0] = uint8_t(color.get<0>());
            rgbe_pixels[y * SkyEnvRes[0] + x].v[1] = uint8_t(color.get<1>());
            rgbe_pixels[y * SkyEnvRes[0] + x].v[2] = uint8_t(color.get<2>());
            rgbe_pixels[y * SkyEnvRes[0] + x].v[3] = uint8_t(color.get<3>());
        }
    }

    const int storage = 0;
    const int index = tex_storage_rgba_.Allocate(rgbe_pixels.get(), SkyEnvRes, false);

    physical_sky_texture_._index = (uint32_t(storage) << 28) | index;

    env_.env_map = physical_sky_texture_._index;
    if (env_.back_map == PhysicalSkyTexture._index) {
        env_.back_map = physical_sky_texture_._index;
    }
}

void Ray::Cpu::Scene::PrepareEnvMapQTree_nolock() {
    const int tex = int(env_.env_map & 0x00ffffff);
    Ref::simd_ivec2 size;
    tex_storage_rgba_.GetIRes(tex, 0, value_ptr(size));

    const int lowest_dim = std::min(size[0], size[1]);

    env_map_qtree_.res = 1;
    while (2 * env_map_qtree_.res < lowest_dim) {
        env_map_qtree_.res *= 2;
    }

    assert(env_map_qtree_.mips.empty());

    int cur_res = env_map_qtree_.res;
    float total_lum = 0.0f;

    { // initialize the first quadtree level
        env_map_qtree_.mips.emplace_back(cur_res * cur_res, 0.0f);

        for (int y = 0; y < size[1]; ++y) {
            const float theta = PI * float(y) / float(size[1]);
            for (int x = 0; x < size[0]; ++x) {
                const float phi = 2.0f * PI * float(x) / float(size[0]);

                const color_rgba8_t col_rgbe = tex_storage_rgba_.Get(tex, x, y, 0);
                const Ref::simd_fvec4 col_rgb = Ref::rgbe_to_rgb(col_rgbe);

                const float cur_lum = (col_rgb[0] + col_rgb[1] + col_rgb[2]);

                auto dir = Ref::simd_fvec4{std::sin(theta) * std::cos(phi), std::cos(theta),
                                           std::sin(theta) * std::sin(phi), 0.0f};

                Ref::simd_fvec2 q;
                DirToCanonical(value_ptr(dir), 0.0f, value_ptr(q));

                int qx = CLAMP(int(cur_res * q[0]), 0, cur_res - 1);
                int qy = CLAMP(int(cur_res * q[1]), 0, cur_res - 1);

                int index = 0;
                index |= (qx & 1) << 0;
                index |= (qy & 1) << 1;

                qx /= 2;
                qy /= 2;

                auto &qvec = reinterpret_cast<Ref::simd_fvec4 &>(env_map_qtree_.mips[0][4 * (qy * cur_res / 2 + qx)]);
                qvec.set(index, std::max(qvec[index], cur_lum));
            }
        }

        for (const float v : env_map_qtree_.mips[0]) {
            total_lum += v;
        }

        cur_res /= 2;
    }

    while (cur_res > 1) {
        env_map_qtree_.mips.emplace_back(cur_res * cur_res, 0.0f);
        const auto *prev_mip =
            reinterpret_cast<const Ref::simd_fvec4 *>(env_map_qtree_.mips[env_map_qtree_.mips.size() - 2].data());

        for (int y = 0; y < cur_res; ++y) {
            for (int x = 0; x < cur_res; ++x) {
                const float res_lum = prev_mip[y * cur_res + x][0] + prev_mip[y * cur_res + x][1] +
                                      prev_mip[y * cur_res + x][2] + prev_mip[y * cur_res + x][3];

                int index = 0;
                index |= (x & 1) << 0;
                index |= (y & 1) << 1;

                const int qx = (x / 2);
                const int qy = (y / 2);

                env_map_qtree_.mips.back()[4 * (qy * cur_res / 2 + qx) + index] = res_lum;
            }
        }

        cur_res /= 2;
    }

    //
    // Determine how many levels was actually required
    //

    static const float LumFractThreshold = 0.01f;

    cur_res = 2;
    int the_last_required_lod;
    for (int lod = int(env_map_qtree_.mips.size()) - 1; lod >= 0; --lod) {
        the_last_required_lod = lod;
        const auto *cur_mip = reinterpret_cast<const Ref::simd_fvec4 *>(env_map_qtree_.mips[lod].data());

        bool subdivision_required = false;
        for (int y = 0; y < (cur_res / 2) && !subdivision_required; ++y) {
            for (int x = 0; x < (cur_res / 2) && !subdivision_required; ++x) {
                const Ref::simd_ivec4 mask = simd_cast(cur_mip[y * cur_res / 2 + x] > LumFractThreshold * total_lum);
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
        env_.qtree_mips[i] = env_map_qtree_.mips[i].data();
    }
    for (int i = env_.qtree_levels; i < countof(env_.qtree_mips); ++i) {
        env_.qtree_mips[i] = nullptr;
    }

    log_->Info("Env map qtree res is %i", env_map_qtree_.res);
}

#undef CLAMP
