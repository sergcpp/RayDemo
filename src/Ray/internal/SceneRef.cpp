#include "SceneRef.h"

#include <cassert>
#include <cstring>

#include "../Log.h"
#include "TextureUtilsRef.h"
#include "Time_.h"

#define _CLAMP(val, min, max) (val < min ? min : (val > max ? max : val))

Ray::Ref::Scene::Scene(ILog *log, const bool use_wide_bvh)
    : log_(log), use_wide_bvh_(use_wide_bvh), tex_atlas_rgba_(TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE),
      tex_atlas_rgb_(TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE), tex_atlas_rg_(TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE),
      tex_atlas_r_(TEXTURE_ATLAS_SIZE, TEXTURE_ATLAS_SIZE) {}

Ray::Ref::Scene::~Scene() {
    while (!mesh_instances_.empty()) {
        RemoveMeshInstance(mesh_instances_.begin().index());
    }
    while (!meshes_.empty()) {
        RemoveMesh(meshes_.begin().index());
    }
    while (!lights_.empty()) {
        RemoveLight(lights_.begin().index());
    }
    materials_.clear();
    textures_.clear();
    lights_.clear();
}

void Ray::Ref::Scene::GetEnvironment(environment_desc_t &env) {
    memcpy(&env.env_col[0], &env_.env_col, 3 * sizeof(float));
    env.env_clamp = env_.env_clamp;
    env.env_map = env_.env_map;
}

void Ray::Ref::Scene::SetEnvironment(const environment_desc_t &env) {
    memcpy(&env_.env_col, &env.env_col[0], 3 * sizeof(float));
    env_.env_clamp = env.env_clamp;
    env_.env_map = env.env_map;
}

uint32_t Ray::Ref::Scene::AddTexture(const tex_desc_t &_t) {
    const auto tex_index = uint32_t(textures_.size());

    texture_t t;
    t.width = uint16_t(_t.w);
    t.height = uint16_t(_t.h);

    if (_t.is_srgb) {
        t.width |= TEXTURE_SRGB_BIT;
    }

    if (_t.generate_mipmaps) {
        t.height |= TEXTURE_MIPS_BIT;
    }

    int res[2] = {_t.w, _t.h};

    std::vector<color_rgba8_t> tex_data_rgba8;
    std::vector<color_rgb8_t> tex_data_rgb8;
    std::vector<color_rg8_t> tex_data_rg8;
    std::vector<color_r8_t> tex_data_r8;

    if (_t.format == eTextureFormat::RGBA8888) {
        t.atlas = 0;
        tex_data_rgba8.assign(reinterpret_cast<const color_rgba8_t *>(_t.data),
                              reinterpret_cast<const color_rgba8_t *>(_t.data) + _t.w * _t.h);
    } else if (_t.format == eTextureFormat::RGB888) {
        t.atlas = 1;
        tex_data_rgb8.assign(reinterpret_cast<const color_rgb8_t *>(_t.data),
                             reinterpret_cast<const color_rgb8_t *>(_t.data) + _t.w * _t.h);
    } else if (_t.format == eTextureFormat::RG88) {
        t.atlas = 2;
        tex_data_rg8.assign(reinterpret_cast<const color_rg8_t *>(_t.data),
                            reinterpret_cast<const color_rg8_t *>(_t.data) + _t.w * _t.h);
    } else if (_t.format == eTextureFormat::R8) {
        t.atlas = 3;
        tex_data_r8.assign(reinterpret_cast<const color_r8_t *>(_t.data),
                           reinterpret_cast<const color_r8_t *>(_t.data) + _t.w * _t.h);
    }

    { // Allocate initial mip level
        int page = -1, pos[2];
        if (_t.format == eTextureFormat::RGBA8888) {
            page = tex_atlas_rgba_.Allocate(&tex_data_rgba8[0], res, pos);
        } else if (_t.format == eTextureFormat::RGB888) {
            page = tex_atlas_rgb_.Allocate(&tex_data_rgb8[0], res, pos);
        } else if (_t.format == eTextureFormat::RG88) {
            page = tex_atlas_rg_.Allocate(&tex_data_rg8[0], res, pos);
        } else if (_t.format == eTextureFormat::R8) {
            page = tex_atlas_r_.Allocate(&tex_data_r8[0], res, pos);
        }
        if (page == -1) {
            return 0xffffffff;
        }

        t.page[0] = uint8_t(page);
        t.pos[0][0] = uint16_t(pos[0]);
        t.pos[0][1] = uint16_t(pos[1]);
    }

    // temporarily fill remaining mip levels with the last one (mips will be added later)
    for (int i = 1; i < NUM_MIP_LEVELS; i++) {
        t.page[i] = t.page[0];
        t.pos[i][0] = t.pos[0][0];
        t.pos[i][1] = t.pos[0][1];
    }

    log_->Info("Ray: Texture loaded (atlas = %i, %ix%i)", int(t.atlas), _t.w, _t.h);
    log_->Info("Ray: Atlasses are (RGBA[%i], RGB[%i], RG[%i], R[%i])", tex_atlas_rgba_.page_count(),
               tex_atlas_rgb_.page_count(), tex_atlas_rg_.page_count(), tex_atlas_r_.page_count());

    return textures_.push(t);
}

uint32_t Ray::Ref::Scene::AddMaterial(const shading_node_desc_t &m) {
    material_t mat;

    mat.type = m.type;
    mat.textures[BASE_TEXTURE] = m.base_texture;
    mat.roughness_unorm = pack_unorm_16(m.roughness);
    mat.textures[ROUGH_TEXTURE] = m.roughness_texture;
    memcpy(&mat.base_color[0], &m.base_color[0], 3 * sizeof(float));
    mat.int_ior = m.int_ior;
    mat.ext_ior = m.ext_ior;
    mat.tangent_rotation = 0.0f;
    mat.flags = 0;

    if (m.type == DiffuseNode) {
        mat.sheen_unorm = pack_unorm_16(_CLAMP(m.sheen, 0.0f, 1.0f));
        mat.sheen_tint_unorm = pack_unorm_16(_CLAMP(m.tint, 0.0f, 1.0f));
        mat.textures[METALLIC_TEXTURE] = m.metallic_texture;
    } else if (m.type == GlossyNode) {
        mat.tangent_rotation = 2.0f * PI * m.anisotropic_rotation;
        mat.textures[METALLIC_TEXTURE] = m.metallic_texture;
        mat.tint_unorm = pack_unorm_16(_CLAMP(m.tint, 0.0f, 1.0f));
    } else if (m.type == RefractiveNode) {
    } else if (m.type == EmissiveNode) {
        mat.strength = m.strength;
        if (m.multiple_importance) {
            mat.flags |= MAT_FLAG_MULT_IMPORTANCE;
        }
        if (m.sky_portal) {
            mat.flags |= MAT_FLAG_SKY_PORTAL;
        }
    } else if (m.type == MixNode) {
        mat.strength = m.strength;
        mat.textures[MIX_MAT1] = m.mix_materials[0];
        mat.textures[MIX_MAT2] = m.mix_materials[1];
        if (m.mix_add) {
            mat.flags |= MAT_FLAG_MIX_ADD;
        }
    } else if (m.type == TransparentNode) {
    }

    mat.textures[NORMALS_TEXTURE] = m.normal_map;
    mat.normal_map_strength_unorm = pack_unorm_16(_CLAMP(m.normal_map_intensity, 0.0f, 1.0f));

    return materials_.push(mat);
}

uint32_t Ray::Ref::Scene::AddMaterial(const principled_mat_desc_t &m) {
    material_t main_mat;

    main_mat.type = PrincipledNode;
    main_mat.textures[BASE_TEXTURE] = m.base_texture;
    memcpy(&main_mat.base_color[0], &m.base_color[0], 3 * sizeof(float));
    main_mat.sheen_unorm = pack_unorm_16(_CLAMP(m.sheen, 0.0f, 1.0f));
    main_mat.sheen_tint_unorm = pack_unorm_16(_CLAMP(m.sheen_tint, 0.0f, 1.0f));
    main_mat.roughness_unorm = pack_unorm_16(_CLAMP(m.roughness, 0.0f, 1.0f));
    main_mat.tangent_rotation = 2.0f * PI * _CLAMP(m.anisotropic_rotation, 0.0f, 1.0f);
    main_mat.textures[ROUGH_TEXTURE] = m.roughness_texture;
    main_mat.metallic_unorm = pack_unorm_16(_CLAMP(m.metallic, 0.0f, 1.0f));
    main_mat.textures[METALLIC_TEXTURE] = m.metallic_texture;
    main_mat.int_ior = m.ior;
    main_mat.ext_ior = 1.0f;
    main_mat.flags = 0;
    main_mat.transmission_unorm = pack_unorm_16(_CLAMP(m.transmission, 0.0f, 1.0f));
    main_mat.transmission_roughness_unorm = pack_unorm_16(_CLAMP(m.transmission_roughness, 0.0f, 1.0f));
    main_mat.textures[NORMALS_TEXTURE] = m.normal_map;
    main_mat.normal_map_strength_unorm = pack_unorm_16(_CLAMP(m.normal_map_intensity, 0.0f, 1.0f));
    main_mat.anisotropic_unorm = pack_unorm_16(_CLAMP(m.anisotropic, 0.0f, 1.0f));
    main_mat.specular_unorm = pack_unorm_16(_CLAMP(m.specular, 0.0f, 1.0f));
    main_mat.specular_tint_unorm = pack_unorm_16(_CLAMP(m.specular_tint, 0.0f, 1.0f));
    main_mat.clearcoat_unorm = pack_unorm_16(_CLAMP(m.clearcoat, 0.0f, 1.0f));
    main_mat.clearcoat_roughness_unorm = pack_unorm_16(_CLAMP(m.clearcoat_roughness, 0.0f, 1.0f));

    uint32_t root_node = materials_.push(main_mat);
    uint32_t emissive_node = 0xffffffff, transparent_node = 0xffffffff;

    if (m.emission_strength > 0.0f &&
        (m.emission_color[0] > 0.0f || m.emission_color[1] > 0.0f || m.emission_color[2] > 0.0f)) {
        shading_node_desc_t emissive_desc;
        emissive_desc.type = EmissiveNode;

        memcpy(emissive_desc.base_color, m.emission_color, 3 * sizeof(float));
        emissive_desc.base_texture = m.emission_texture;
        emissive_desc.strength = m.emission_strength;

        emissive_node = AddMaterial(emissive_desc);
    }

    if (m.alpha != 1.0f || m.alpha_texture != 0xffffffff) {
        shading_node_desc_t transparent_desc;
        transparent_desc.type = TransparentNode;

        transparent_node = AddMaterial(transparent_desc);
    }

    if (emissive_node != 0xffffffff) {
        if (root_node == 0xffffffff) {
            root_node = emissive_node;
        } else {
            shading_node_desc_t mix_node;
            mix_node.type = MixNode;
            mix_node.base_texture = 0xffffffff;
            mix_node.strength = 0.5f;
            mix_node.int_ior = mix_node.ext_ior = 0.0f;
            mix_node.mix_add = true;

            mix_node.mix_materials[0] = root_node;
            mix_node.mix_materials[1] = emissive_node;

            root_node = AddMaterial(mix_node);
        }
    }

    if (transparent_node != 0xffffffff) {
        if (root_node == 0xffffffff || m.alpha == 0.0f) {
            root_node = transparent_node;
        } else {
            shading_node_desc_t mix_node;
            mix_node.type = MixNode;
            mix_node.base_texture = m.alpha_texture;
            mix_node.strength = m.alpha;
            mix_node.int_ior = mix_node.ext_ior = 0.0f;

            mix_node.mix_materials[0] = transparent_node;
            mix_node.mix_materials[1] = root_node;

            root_node = AddMaterial(mix_node);
        }
    }

    return root_node;
}

uint32_t Ray::Ref::Scene::AddMesh(const mesh_desc_t &_m) {
    const uint32_t mesh_index = meshes_.emplace();
    mesh_t &m = meshes_.at(mesh_index);

    const auto tris_start = uint32_t(tris_.size());
    // const auto tri_index_start = uint32_t(tri_indices_.size());

    bvh_settings_t s;
    s.oversplit_threshold = 0.95f;
    s.allow_spatial_splits = _m.allow_spatial_splits;
    s.use_fast_bvh_build = _m.use_fast_bvh_build;

    const uint64_t t1 = Ray::GetTimeMs();

    m.node_index = uint32_t(nodes_.size());
    m.node_count = PreprocessMesh(_m.vtx_attrs, {_m.vtx_indices, _m.vtx_indices_count}, _m.layout, _m.base_vertex,
                                  uint32_t(tri_materials_.size()), s, nodes_, tris_, tri_indices_, mtris_);

    memcpy(m.bbox_min, nodes_[m.node_index].bbox_min, 3 * sizeof(float));
    memcpy(m.bbox_max, nodes_[m.node_index].bbox_max, 3 * sizeof(float));

    log_->Info("Ray: Mesh preprocessed in %lldms", (Ray::GetTimeMs() - t1));

    if (use_wide_bvh_) {
        const uint64_t t2 = Ray::GetTimeMs();

        const auto before_count = uint32_t(mnodes_.size());
        const uint32_t new_root = FlattenBVH_Recursive(nodes_.data(), m.node_index, 0xffffffff, mnodes_);

        m.node_index = new_root;
        m.node_count = uint32_t(mnodes_.size() - before_count);

        // nodes_ variable is treated as temporary storage
        nodes_.clear();

        log_->Info("Ray: BVH flattened in %lldms", (Ray::GetTimeMs() - t2));
    }

    const auto tri_materials_start = uint32_t(tri_materials_.size());
    tri_materials_.resize(tri_materials_start + (_m.vtx_indices_count / 3));

    // init triangle materials
    for (const shape_desc_t &s : _m.shapes) {
        bool is_front_solid = true, is_back_solid = true;

        uint32_t material_stack[32];
        material_stack[0] = s.mat_index;
        uint32_t material_count = 1;

        while (material_count) {
            const material_t &mat = materials_[material_stack[--material_count]];

            if (mat.type == MixNode) {
                material_stack[material_count++] = mat.textures[MIX_MAT1];
                material_stack[material_count++] = mat.textures[MIX_MAT2];
            } else if (mat.type == TransparentNode) {
                is_front_solid = false;
                break;
            }
        }

        material_stack[0] = s.back_mat_index;
        material_count = 1;

        while (material_count) {
            const material_t &mat = materials_[material_stack[--material_count]];

            if (mat.type == MixNode) {
                material_stack[material_count++] = mat.textures[MIX_MAT1];
                material_stack[material_count++] = mat.textures[MIX_MAT2];
            } else if (mat.type == TransparentNode) {
                is_back_solid = false;
                break;
            }
        }

        for (size_t i = s.vtx_start; i < s.vtx_start + s.vtx_count; i += 3) {
            tri_mat_data_t &tri_mat = tri_materials_[tri_materials_start + (i / 3)];

            assert(s.mat_index < (1 << 14) && "Not enough bits to reference material!");
            assert(s.back_mat_index < (1 << 14) && "Not enough bits to reference material!");

            tri_mat.front_mi = uint16_t(s.mat_index);
            if (is_front_solid) {
                tri_mat.front_mi |= MATERIAL_SOLID_BIT;
            }

            tri_mat.back_mi = uint16_t(s.back_mat_index);
            if (is_back_solid) {
                tri_mat.back_mi |= MATERIAL_SOLID_BIT;
            }
        }
    }

    m.tris_index = tris_start;
    m.tris_count = uint32_t(tris_.size() - tris_start);

    std::vector<uint32_t> new_vtx_indices;
    new_vtx_indices.reserve(_m.vtx_indices_count);
    for (size_t i = 0; i < _m.vtx_indices_count; i++) {
        new_vtx_indices.push_back(_m.vtx_indices[i] + _m.base_vertex + uint32_t(vertices_.size()));
    }

    const size_t stride = AttrStrides[_m.layout];

    // add attributes
    const size_t new_vertices_start = vertices_.size();
    vertices_.resize(new_vertices_start + _m.vtx_attrs_count);
    for (size_t i = 0; i < _m.vtx_attrs_count; ++i) {
        vertex_t &v = vertices_[new_vertices_start + i];

        memcpy(&v.p[0], (_m.vtx_attrs + i * stride), 3 * sizeof(float));
        memcpy(&v.n[0], (_m.vtx_attrs + i * stride + 3), 3 * sizeof(float));

        if (_m.layout == PxyzNxyzTuv) {
            memcpy(&v.t[0][0], (_m.vtx_attrs + i * stride + 6), 2 * sizeof(float));
            v.t[1][0] = v.t[1][1] = 0.0f;
            v.b[0] = v.b[1] = v.b[2] = 0.0f;
        } else if (_m.layout == PxyzNxyzTuvTuv) {
            memcpy(&v.t[0][0], (_m.vtx_attrs + i * stride + 6), 2 * sizeof(float));
            memcpy(&v.t[1][0], (_m.vtx_attrs + i * stride + 8), 2 * sizeof(float));
            v.b[0] = v.b[1] = v.b[2] = 0.0f;
        } else if (_m.layout == PxyzNxyzBxyzTuv) {
            memcpy(&v.b[0], (_m.vtx_attrs + i * stride + 6), 3 * sizeof(float));
            memcpy(&v.t[0][0], (_m.vtx_attrs + i * stride + 9), 2 * sizeof(float));
            v.t[1][0] = v.t[1][1] = 0.0f;
        } else if (_m.layout == PxyzNxyzBxyzTuvTuv) {
            memcpy(&v.b[0], (_m.vtx_attrs + i * stride + 6), 3 * sizeof(float));
            memcpy(&v.t[0][0], (_m.vtx_attrs + i * stride + 9), 2 * sizeof(float));
            memcpy(&v.t[1][0], (_m.vtx_attrs + i * stride + 11), 2 * sizeof(float));
        }
    }

    if (_m.layout == PxyzNxyzTuv || _m.layout == PxyzNxyzTuvTuv) {
        ComputeTangentBasis(0, new_vertices_start, vertices_, new_vtx_indices, &new_vtx_indices[0],
                            new_vtx_indices.size());
    }

    m.vert_index = uint32_t(vtx_indices_.size());
    m.vert_count = uint32_t(new_vtx_indices.size());

    vtx_indices_.insert(vtx_indices_.end(), new_vtx_indices.begin(), new_vtx_indices.end());

    return mesh_index;
}

void Ray::Ref::Scene::RemoveMesh(const uint32_t i) {
    if (!meshes_.exists(i)) {
        return;
    }

    const mesh_t &m = meshes_[i];

    const uint32_t node_index = m.node_index, node_count = m.node_count;
    const uint32_t tris_index = m.tris_index, tris_count = m.tris_count;

    meshes_.erase(i);

    bool rebuild_needed = false;
    for (auto it = mesh_instances_.begin(); it != mesh_instances_.end();) {
        mesh_instance_t &mi = *it;
        if (mi.mesh_index == i) {
            it = mesh_instances_.erase(it);
            rebuild_needed = true;
        } else {
            ++it;
        }
    }

    RemoveTris(tris_index, tris_count);
    RemoveNodes(node_index, node_count);

    if (rebuild_needed) {
        RebuildTLAS();
    }
}

uint32_t Ray::Ref::Scene::AddLight(const directional_light_desc_t &_l) {
    light_t l;

    l.type = LIGHT_TYPE_DIR;
    l.visible = false;
    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));
    l.dir.dir[0] = -_l.direction[0];
    l.dir.dir[1] = -_l.direction[1];
    l.dir.dir[2] = -_l.direction[2];
    l.dir.angle = _l.angle * PI / 360.0f;

    const uint32_t light_index = lights_.push(l);
    li_indices_.push_back(light_index);
    return light_index;
}

uint32_t Ray::Ref::Scene::AddLight(const sphere_light_desc_t &_l) {
    light_t l;

    l.type = LIGHT_TYPE_SPHERE;
    l.visible = _l.visible;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));
    memcpy(&l.sph.pos[0], &_l.position[0], 3 * sizeof(float));

    l.sph.area = 4.0f * PI * _l.radius * _l.radius;
    l.sph.radius = _l.radius;

    const uint32_t light_index = lights_.push(l);
    li_indices_.push_back(light_index);
    if (_l.visible) {
        visible_lights_.push_back(light_index);
    }
    return light_index;
}

uint32_t Ray::Ref::Scene::AddLight(const rect_light_desc_t &_l, const float *xform) {
    light_t l;

    l.type = LIGHT_TYPE_RECT;
    l.visible = _l.visible;
    l.sky_portal = _l.sky_portal;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));

    l.rect.pos[0] = xform[12];
    l.rect.pos[1] = xform[13];
    l.rect.pos[2] = xform[14];

    l.rect.area = _l.width * _l.height;

    const simd_fvec4 uvec = _l.width * TransformDirection(simd_fvec4{1.0f, 0.0f, 0.0f, 0.0f}, xform);
    const simd_fvec4 vvec = _l.height * TransformDirection(simd_fvec4{0.0f, 0.0f, 1.0f, 0.0f}, xform);

    memcpy(l.rect.u, value_ptr(uvec), 3 * sizeof(float));
    memcpy(l.rect.v, value_ptr(vvec), 3 * sizeof(float));

    const uint32_t light_index = lights_.push(l);
    li_indices_.push_back(light_index);
    if (_l.visible) {
        visible_lights_.push_back(light_index);
    }
    return light_index;
}

uint32_t Ray::Ref::Scene::AddLight(const disk_light_desc_t &_l, const float *xform) {
    light_t l;

    l.type = LIGHT_TYPE_DISK;
    l.visible = _l.visible;
    l.sky_portal = _l.sky_portal;

    memcpy(&l.col[0], &_l.color[0], 3 * sizeof(float));

    l.disk.pos[0] = xform[12];
    l.disk.pos[1] = xform[13];
    l.disk.pos[2] = xform[14];

    l.disk.area = 0.25f * PI * _l.size_x * _l.size_y;

    const simd_fvec4 uvec = _l.size_x * TransformDirection(simd_fvec4{1.0f, 0.0f, 0.0f, 0.0f}, xform);
    const simd_fvec4 vvec = _l.size_y * TransformDirection(simd_fvec4{0.0f, 0.0f, 1.0f, 0.0f}, xform);

    memcpy(l.disk.u, value_ptr(uvec), 3 * sizeof(float));
    memcpy(l.disk.v, value_ptr(vvec), 3 * sizeof(float));

    const uint32_t light_index = lights_.push(l);
    li_indices_.push_back(light_index);
    if (_l.visible) {
        visible_lights_.push_back(light_index);
    }
    return light_index;
}

void Ray::Ref::Scene::RemoveLight(const uint32_t i) {
    if (!lights_.exists(i)) {
        return;
    }

    { // remove from compacted list
        auto it = find(begin(li_indices_), end(li_indices_), i);
        assert(it != end(li_indices_));
        li_indices_.erase(it);
    }

    if (lights_[i].visible) {
        auto it = find(begin(visible_lights_), end(visible_lights_), i);
        assert(it != end(visible_lights_));
        visible_lights_.erase(it);
    }

    lights_.erase(i);
}

uint32_t Ray::Ref::Scene::AddMeshInstance(const uint32_t mesh_index, const float *xform) {
    const uint32_t mi_index = mesh_instances_.emplace();

    mesh_instance_t &mi = mesh_instances_.at(mi_index);
    mi.mesh_index = mesh_index;
    mi.tr_index = transforms_.emplace();

    { // find emissive triangles and add them as emitters
        const mesh_t &m = meshes_[mesh_index];
        for (uint32_t tri = (m.vert_index / 3); tri < (m.vert_index + m.vert_count) / 3; ++tri) {
            const tri_mat_data_t &tri_mat = tri_materials_[tri];

            const material_t &front_mat = materials_[tri_mat.front_mi & MATERIAL_INDEX_BITS];
            if (front_mat.type == EmissiveNode &&
                (front_mat.flags & (MAT_FLAG_MULT_IMPORTANCE | MAT_FLAG_SKY_PORTAL))) {
                light_t new_light;
                new_light.type = LIGHT_TYPE_TRI;
                new_light.visible = 0;
                new_light.sky_portal = 0;
                new_light.tri.tri_index = tri;
                new_light.tri.xform_index = mi.tr_index;
                new_light.col[0] = front_mat.base_color[0] * front_mat.strength;
                new_light.col[1] = front_mat.base_color[1] * front_mat.strength;
                new_light.col[2] = front_mat.base_color[2] * front_mat.strength;
                const uint32_t index = lights_.push(new_light);
                li_indices_.push_back(index);
            }
        }
    }

    SetMeshInstanceTransform(mi_index, xform);

    return mi_index;
}

void Ray::Ref::Scene::SetMeshInstanceTransform(uint32_t mi_index, const float *xform) {
    mesh_instance_t &mi = mesh_instances_[mi_index];
    transform_t &tr = transforms_[mi.tr_index];

    memcpy(tr.xform, xform, 16 * sizeof(float));
    InverseMatrix(tr.xform, tr.inv_xform);

    const mesh_t &m = meshes_[mi.mesh_index];
    TransformBoundingBox(m.bbox_min, m.bbox_max, xform, mi.bbox_min, mi.bbox_max);

    RebuildTLAS();
}

void Ray::Ref::Scene::RemoveMeshInstance(uint32_t i) {
    transforms_.erase(mesh_instances_[i].tr_index);
    mesh_instances_.erase(i);

    RebuildTLAS();
}

void Ray::Ref::Scene::Finalize() { GenerateTextureMips(); }

void Ray::Ref::Scene::RemoveTris(uint32_t tris_index, uint32_t tris_count) {
    if (!tris_count) {
        return;
    }

    tris_.erase(std::next(tris_.begin(), tris_index), std::next(tris_.begin(), tris_index + tris_count));

    if (tris_index != tris_.size()) {
        for (mesh_t &m : meshes_) {
            if (m.tris_index > tris_index) {
                m.tris_index -= tris_count;
            }
        }
    }
}

void Ray::Ref::Scene::RemoveNodes(uint32_t node_index, uint32_t node_count) {
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

void Ray::Ref::Scene::RebuildTLAS() {
    RemoveNodes(macro_nodes_root_, macro_nodes_count_);
    mi_indices_.clear();

    macro_nodes_root_ = 0xffffffff;
    macro_nodes_count_ = 0;

    if (mesh_instances_.empty()) {
        return;
    }

    std::vector<prim_t> primitives;
    primitives.reserve(mesh_instances_.size());

    for (const mesh_instance_t &mi : mesh_instances_) {
        primitives.push_back({0, 0, 0, Ref::simd_fvec4{mi.bbox_min}, Ref::simd_fvec4{mi.bbox_max}});
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

void Ray::Ref::Scene::GenerateTextureMips() {
    struct mip_gen_info {
        uint32_t texture_index;
        uint16_t size; // used for sorting
        uint8_t dst_mip;
        uint8_t atlas_index; // used for sorting
    };

    std::vector<mip_gen_info> mips_to_generate;
    mips_to_generate.reserve(textures_.size());

    for (uint32_t i = 0; i < textures_.capacity(); ++i) {
        if (!textures_.exists(i)) {
            continue;
        }

        const texture_t &t = textures_[i];
        if ((t.height & TEXTURE_MIPS_BIT) == 0) {
            continue;
        }

        int mip = 0;
        int res[2] = {(t.width & TEXTURE_WIDTH_BITS), (t.height & TEXTURE_HEIGHT_BITS)};

        res[0] /= 2;
        res[1] /= 2;
        ++mip;

        while (res[0] >= 1 && res[1] >= 1) {
            const bool requires_generation =
                t.page[mip] == t.page[0] && t.pos[mip][0] == t.pos[0][0] && t.pos[mip][1] == t.pos[0][1];
            if (requires_generation) {
                mips_to_generate.emplace_back();
                auto &m = mips_to_generate.back();
                m.texture_index = i;
                m.size = std::max(res[0], res[1]);
                m.dst_mip = mip;
                m.atlas_index = t.atlas;
            }

            res[0] /= 2;
            res[1] /= 2;
            ++mip;
        }
    }

    // Sort for more optimal allocation
    sort(begin(mips_to_generate), end(mips_to_generate), [](const mip_gen_info &lhs, const mip_gen_info &rhs) {
        if (lhs.atlas_index == rhs.atlas_index) {
            return lhs.size > rhs.size;
        }
        return lhs.atlas_index < rhs.atlas_index;
    });

    for (const mip_gen_info &info : mips_to_generate) {
        texture_t &t = textures_[info.texture_index];

        const int dst_mip = info.dst_mip;
        const int src_mip = dst_mip - 1;
        const int src_res[2] = {(t.width & TEXTURE_WIDTH_BITS) >> src_mip, (t.height & TEXTURE_HEIGHT_BITS) >> src_mip};
        assert(src_res[0] != 0 && src_res[1] != 0);

        const int src_pos[2] = {t.pos[src_mip][0] + 1, t.pos[src_mip][1] + 1};

        int page = -1, pos[2];
        if (t.atlas == 0) {
            page = tex_atlas_rgba_.DownsampleRegion(t.page[src_mip], src_pos, src_res, pos);
        } else if (t.atlas == 1) {
            page = tex_atlas_rgb_.DownsampleRegion(t.page[src_mip], src_pos, src_res, pos);
        } else if (t.atlas == 2) {
            page = tex_atlas_rg_.DownsampleRegion(t.page[src_mip], src_pos, src_res, pos);
        } else if (t.atlas == 3) {
            page = tex_atlas_r_.DownsampleRegion(t.page[src_mip], src_pos, src_res, pos);
        }

        if (page == -1) {
            log_->Error("Failed to allocate texture!");
            break;
        }

        t.page[dst_mip] = uint8_t(page);
        t.pos[dst_mip][0] = uint16_t(pos[0]);
        t.pos[dst_mip][1] = uint16_t(pos[1]);

        if (src_res[0] == 1 || src_res[1] == 1) {
            // fill remaining mip levels with the last one
            for (int i = dst_mip + 1; i < NUM_MIP_LEVELS; i++) {
                t.page[i] = t.page[dst_mip];
                t.pos[i][0] = t.pos[dst_mip][0];
                t.pos[i][1] = t.pos[dst_mip][1];
            }
        }
    }

    log_->Info("Ray: Atlasses are (RGBA[%i], RGB[%i], RG[%i], R[%i])", tex_atlas_rgba_.page_count(),
               tex_atlas_rgb_.page_count(), tex_atlas_rg_.page_count(), tex_atlas_r_.page_count());
}

#undef _CLAMP