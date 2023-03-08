#include "Mesh.h"

#include <ctime>

#if defined(USE_GL_RENDER)
#include "GL.h"
#elif defined(USE_SW_RENDER)
#include "SW/SW.h"
#endif

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif


int Ren::Mesh::max_gpu_bones = 16;

Ren::Mesh::Mesh(const char *name, std::istream &data, const material_load_callback &on_mat_load,
                const BufferRef &vertex_buf, const BufferRef &index_buf) {
    Init(name, data, on_mat_load, vertex_buf, index_buf);
}

void Ren::Mesh::Init(const char *name, std::istream &data, const material_load_callback &on_mat_load,
                     const BufferRef &vertex_buf, const BufferRef &index_buf) {
    strcpy(name_, name);

    char mesh_type_str[12];
    auto pos = data.tellg();
    data.read(mesh_type_str, 12);
    data.seekg(pos, std::ios::beg);

    if (strcmp(mesh_type_str, "STATIC_MESH\0") == 0) {
        InitMeshSimple(data, on_mat_load, vertex_buf, index_buf);
    } else if (strcmp(mesh_type_str, "TERRAI_MESH\0") == 0) {
        InitMeshTerrain(data, on_mat_load, vertex_buf, index_buf);
    } else if (strcmp(mesh_type_str, "SKELET_MESH\0") == 0) {
        InitMeshSkeletal(data, on_mat_load, vertex_buf, index_buf);
    }
}

void Ren::Mesh::InitMeshSimple(std::istream &data, const material_load_callback &on_mat_load,
                               const BufferRef &vertex_buf, const BufferRef &index_buf) {
    char mesh_type_str[12];
    data.read(mesh_type_str, 12);
    assert(strcmp(mesh_type_str, "STATIC_MESH\0") == 0);

    type_ = MeshSimple;

    enum {
        MESH_INFO_CHUNK = 0,
        VTX_ATTR_CHUNK,
        VTX_NDX_CHUNK,
        MATERIALS_CHUNK,
        STRIPS_CHUNK
    };

    struct ChunkPos {
        int offset;
        int length;
    };

    struct Header {
        int num_chunks;
        ChunkPos p[5];
    } file_header;

    data.read((char *)&file_header, sizeof(file_header));

    // Skip name, cant remember why i put it there
    data.seekg(32, std::ios::cur);

    float temp_f[3];
    data.read((char *)&temp_f[0], sizeof(float) * 3);
    bbox_min_ = MakeVec3(temp_f);
    data.read((char *)&temp_f[0], sizeof(float) * 3);
    bbox_max_ = MakeVec3(temp_f);

    attribs_size_ = (uint32_t)file_header.p[VTX_ATTR_CHUNK].length;
    attribs_.reset(new char[attribs_size_], std::default_delete<char[]>());
    data.read((char *)attribs_.get(), attribs_size_);

    indices_size_ = (uint32_t)file_header.p[VTX_NDX_CHUNK].length;
    indices_.reset(new char[indices_size_], std::default_delete<char[]>());
    data.read((char *)indices_.get(), indices_size_);

    std::vector<std::array<char, 64>> material_names((size_t)file_header.p[MATERIALS_CHUNK].length / 64);
    for (auto &n : material_names) {
        data.read(&n[0], 64);
    }

    flags_ = 0;

    int num_strips = file_header.p[STRIPS_CHUNK].length / 12;
    for (int i = 0; i < num_strips; i++) {
        int index, num_indices, alpha;
        data.read((char *)&index, 4);
        data.read((char *)&num_indices, 4);
        data.read((char *)&alpha, 4);

        groups_[i].offset = (int)(index * sizeof(uint32_t));
        groups_[i].num_indices = (int)num_indices;
        groups_[i].flags = 0;

        if (alpha) {
            groups_[i].flags |= MeshHasAlpha;
            flags_ |= MeshHasAlpha;
        }

        groups_[i].mat = on_mat_load(&material_names[i][0]);
    }

    if (num_strips < (int)groups_.size()) {
        groups_[num_strips].offset = -1;
    }

    attribs_buf_ = vertex_buf;
    attribs_offset_ = attribs_buf_->Alloc(attribs_size_, attribs_.get());

    if (attribs_offset_ != 0) {
        uint32_t offset = attribs_offset_ / (13 * sizeof(float));

        uint32_t *_indices = (uint32_t *)indices_.get();
        for (uint32_t i = 0; i < indices_size_ / sizeof(uint32_t); i++) {
            _indices[i] += offset;
        }
    }

    indices_buf_ = index_buf;
    indices_offset_ = indices_buf_->Alloc(indices_size_, indices_.get());
}

void Ren::Mesh::InitMeshTerrain(std::istream &data, const material_load_callback &on_mat_load,
                                const BufferRef &vertex_buf, const BufferRef &index_buf) {
    char mesh_type_str[12];
    data.read(mesh_type_str, 12);
    assert(strcmp(mesh_type_str, "TERRAI_MESH\0") == 0);

    type_ = MeshTerrain;

    enum {
        MESH_INFO_CHUNK = 0,
        VTX_ATTR_CHUNK,
        VTX_NDX_CHUNK,
        MATERIALS_CHUNK,
        STRIPS_CHUNK
    };

    struct ChunkPos {
        int offset;
        int length;
    };

    struct Header {
        int num_chunks;
        ChunkPos p[5];
    } file_header;

    data.read((char *)&file_header, sizeof(file_header));

    // Skip name, cant remember why i put it there
    data.seekg(32, std::ios::cur);

    float temp_f[3];
    data.read((char *)&temp_f[0], sizeof(float) * 3);
    bbox_min_ = MakeVec3(temp_f);
    data.read((char *)&temp_f[0], sizeof(float) * 3);
    bbox_max_ = MakeVec3(temp_f);

    attribs_size_ = file_header.p[VTX_ATTR_CHUNK].length + file_header.p[VTX_NDX_CHUNK].length * sizeof(float);
    attribs_.reset(new char[attribs_size_], std::default_delete<char[]>());

    float *p_fattrs = (float *)attribs_.get();
    for (int i = 0; i < file_header.p[VTX_NDX_CHUNK].length; i++) {
        data.read((char *)&p_fattrs[i * 9], 8 * sizeof(float));
    }

    for (int i = 0; i < file_header.p[VTX_NDX_CHUNK].length; i++) {
        unsigned char c;
        data.read((char *)&c, 1);
        p_fattrs[i * 9 + 8] = float(c);
    }

    indices_size_ = (size_t)file_header.p[VTX_NDX_CHUNK].length;
    indices_.reset(new char[indices_size_], std::default_delete<char[]>());
    data.read((char *)indices_.get(), indices_size_);

    std::vector<std::array<char, 64>> material_names((size_t)file_header.p[MATERIALS_CHUNK].length / 64);
    for (auto &n : material_names) {
        data.read(&n[0], 64);
    }

    flags_ = 0;

    int num_strips = file_header.p[STRIPS_CHUNK].length / 12;
    for (int i = 0; i < num_strips; i++) {
        int index, num_indices, alpha;
        data.read((char *)&index, 4);
        data.read((char *)&num_indices, 4);
        data.read((char *)&alpha, 4);

        groups_[i].offset = (int)index * sizeof(unsigned short);
        groups_[i].num_indices = (int)num_indices;
        groups_[i].flags = 0;

        if (alpha) {
            groups_[i].flags |= MeshHasAlpha;
            flags_ |= MeshHasAlpha;
        }

        groups_[i].mat = on_mat_load(&material_names[i][0]);
    }

    if (num_strips < (int)groups_.size()) {
        groups_[num_strips].offset = -1;
    }

    attribs_buf_ = vertex_buf;
    attribs_offset_ = attribs_buf_->Alloc(attribs_size_, attribs_.get());

    indices_buf_ = index_buf;
    indices_offset_ = indices_buf_->Alloc(indices_size_, indices_.get());
}

void Ren::Mesh::InitMeshSkeletal(std::istream &data, const material_load_callback &on_mat_load,
                                 const BufferRef &vertex_buf, const BufferRef &index_buf) {
    char mesh_type_str[12];
    data.read(mesh_type_str, 12);
    assert(strcmp(mesh_type_str, "SKELET_MESH\0") == 0);

    type_ = MeshSkeletal;

    enum {
        MESH_INFO_CHUNK = 0,
        VTX_ATTR_CHUNK,
        VTX_NDX_CHUNK,
        MATERIALS_CHUNK,
        STRIPS_CHUNK,
        BONES_CHUNK
    };

    struct ChunkPos {
        int offset;
        int length;
    };

    struct Header {
        int num_chunks;
        ChunkPos p[6];
    } file_header;

    data.read((char *)&file_header, sizeof(file_header));

    // Skip name, cant remember why i put it there
    data.seekg(32, std::ios::cur);

    {   // Read bounding box
        float temp_f[3];
        data.read((char *)&temp_f[0], sizeof(float) * 3);
        bbox_min_ = MakeVec3(temp_f);
        data.read((char *)&temp_f[0], sizeof(float) * 3);
        bbox_max_ = MakeVec3(temp_f);
    }

    attribs_size_ = (uint32_t)file_header.p[VTX_ATTR_CHUNK].length;
    attribs_.reset(new char[attribs_size_], std::default_delete<char[]>());
    data.read((char *)attribs_.get(), attribs_size_);

    indices_size_ = (uint32_t)file_header.p[VTX_NDX_CHUNK].length;
    indices_.reset(new char[indices_size_], std::default_delete<char[]>());
    data.read((char *)indices_.get(), indices_size_);

    std::vector<std::array<char, 64>> material_names((size_t)file_header.p[MATERIALS_CHUNK].length / 64);
    for (auto &n : material_names) {
        data.read(&n[0], 64);
    }

    flags_ = 0;

    int num_strips = file_header.p[STRIPS_CHUNK].length / 12;
    for (int i = 0; i < num_strips; i++) {
        int index, num_indices, alpha;
        data.read((char *)&index, 4);
        data.read((char *)&num_indices, 4);
        data.read((char *)&alpha, 4);

        groups_[i].offset = (int)index * sizeof(unsigned short);
        groups_[i].num_indices = (int)num_indices;
        groups_[i].flags = 0;

        if (alpha) {
            groups_[i].flags |= MeshHasAlpha;
            flags_ |= MeshHasAlpha;
        }

        groups_[i].mat = on_mat_load(&material_names[i][0]);
    }

    if (num_strips < (int)groups_.size()) {
        groups_[num_strips].offset = -1;
    }

    auto &bones = skel_.bones;

    int num_bones = file_header.p[BONES_CHUNK].length / (64 + 8 + 12 + 16);
    bones.resize((size_t)num_bones);
    for (int i = 0; i < num_bones; i++) {
        float temp_f[4];
        Vec3f temp_v;
        Quatf temp_q;
        data.read(bones[i].name, 64);
        data.read((char *)&bones[i].id, sizeof(int));
        data.read((char *)&bones[i].parent_id, sizeof(int));

        data.read((char *)&temp_f[0], sizeof(float) * 3);
        temp_v = MakeVec3(&temp_f[0]);
        bones[i].bind_matrix = Translate(bones[i].bind_matrix, temp_v);
        data.read((char *)&temp_f[0], sizeof(float) * 4);
        temp_q = MakeQuat(&temp_f[0]);
        bones[i].bind_matrix *= ToMat4(temp_q);
        bones[i].inv_bind_matrix = Inverse(bones[i].bind_matrix);

        if (bones[i].parent_id != -1) {
            bones[i].cur_matrix = bones[bones[i].parent_id].inv_bind_matrix * bones[i].bind_matrix;
            Vec4f pos = bones[bones[i].parent_id].inv_bind_matrix * bones[i].bind_matrix[3];
            bones[i].head_pos = MakeVec3(&pos[0]);
        } else {
            bones[i].cur_matrix = bones[i].bind_matrix;
            bones[i].head_pos = MakeVec3(&bones[i].bind_matrix[3][0]);
        }
        bones[i].cur_comb_matrix = bones[i].cur_matrix;
        bones[i].dirty = true;
    }

    assert(max_gpu_bones);
    if (bones.size() <= (size_t)max_gpu_bones) {
        for (size_t s = 0; s < groups_.size(); s++) {
            if (groups_[s].offset == -1) break;
            BoneGroup grp;
            for (size_t i = 0; i < bones.size(); i++) {
                grp.bone_ids.push_back((uint32_t)i);
            }
            grp.strip_ids.push_back((uint32_t)s);
            grp.strip_ids.push_back(groups_[s].offset / 2);
            grp.strip_ids.push_back(groups_[s].num_indices);
            skel_.bone_groups.push_back(grp);
        }
    } else {
        SplitMesh(max_gpu_bones);
    }

    skel_.matr_palette.resize(skel_.bones.size());

    attribs_buf_ = vertex_buf;
    attribs_offset_ = attribs_buf_->Alloc(attribs_size_, attribs_.get());

    indices_buf_ = index_buf;
    indices_offset_ = indices_buf_->Alloc(indices_size_, indices_.get());
}

#undef max

void Ren::Mesh::SplitMesh(int bones_limit) {
    assert(type_ == MeshSkeletal);

    std::vector<int> bone_ids;
    bone_ids.reserve(12);

    float *vtx_attribs = (float *)attribs_.get();
    size_t num_vtx_attribs = attribs_size_ / sizeof(float);
    unsigned short *vtx_indices = (unsigned short *)indices_.get();
    size_t num_vtx_indices = indices_size_ / sizeof(unsigned short);

    auto t1 = clock();

    for (size_t s = 0; s < groups_.size(); s++) {
        if (groups_[s].offset == -1) break;
        for (int i = (int)groups_[s].offset / 2; i < (int)(groups_[s].offset / 2 + groups_[s].num_indices - 2); i += 1) {
            bone_ids.clear();
            if (vtx_indices[i] == vtx_indices[i + 1] || vtx_indices[i + 1] == vtx_indices[i + 2]) {
                continue;
            }
            for (int j = i; j < i + 3; j++) {
                for (int k = 8; k < 12; k += 1) {
                    if (vtx_attribs[vtx_indices[j] * 16 + k + 4] > 0.00001f) {
                        if (std::find(bone_ids.begin(), bone_ids.end(), (int)vtx_attribs[vtx_indices[j] * 16 + k]) ==
                                bone_ids.end()) {
                            bone_ids.push_back((int)vtx_attribs[vtx_indices[j] * 16 + k]);
                        }
                    }
                }
            }
            Ren::BoneGroup *best_fit = nullptr;
            int best_k = std::numeric_limits<int>::max();
            for (auto &g : skel_.bone_groups) {
                bool b = true;
                int k = 0;
                for (size_t j = 0; j < bone_ids.size(); j++) {
                    if (std::find(g.bone_ids.begin(), g.bone_ids.end(), bone_ids[j]) == g.bone_ids.end()) {
                        k++;
                        if (g.bone_ids.size() + k > (size_t) bones_limit) {
                            b = false;
                            break;
                        }
                    }
                }
                if (b && k < best_k) {
                    best_k = k;
                    best_fit = &g;
                }
            }

            if (!best_fit) {
                skel_.bone_groups.emplace_back();
                best_fit = &skel_.bone_groups[skel_.bone_groups.size() - 1];
            }
            for (size_t j = 0; j < bone_ids.size(); j++) {
                if (std::find(best_fit->bone_ids.begin(), best_fit->bone_ids.end(), bone_ids[j]) ==
                        best_fit->bone_ids.end()) {
                    best_fit->bone_ids.push_back(bone_ids[j]);
                }
            }
            if (!best_fit->strip_ids.empty() && s == best_fit->strip_ids[best_fit->strip_ids.size() - 3] &&
                    best_fit->strip_ids[best_fit->strip_ids.size() - 2] +
                    best_fit->strip_ids[best_fit->strip_ids.size() - 1] - 0 == i) {
                best_fit->strip_ids[best_fit->strip_ids.size() - 1]++;
            } else {
                best_fit->strip_ids.push_back((int)s);
                if ((i - groups_[s].offset / 2) % 2 == 0) {
                    best_fit->strip_ids.push_back(i);
                } else {
                    best_fit->strip_ids.push_back(-i);
                }
                best_fit->strip_ids.push_back(1);
            }
        }
    }

    printf("%li\n", clock() - t1);
    t1 = clock();

    std::vector<unsigned short> new_indices;
    new_indices.reserve((size_t)(num_vtx_indices * 1.2f));
    for (auto &g : skel_.bone_groups) {
        std::sort(g.bone_ids.begin(), g.bone_ids.end());
        int cur_s = g.strip_ids[0];

        for (size_t i = 0; i < g.strip_ids.size(); i += 3) {
            bool sign = g.strip_ids[i + 1] >= 0;
            if (!sign) {
                g.strip_ids[i + 1] = -g.strip_ids[i + 1];
            }
            int beg = g.strip_ids[i + 1];
            int end = g.strip_ids[i + 1] + g.strip_ids[i + 2] + 2;

            if (g.strip_ids[i] == cur_s && i > 0) {
                new_indices.push_back(new_indices.back());
                new_indices.push_back(vtx_indices[beg]);
                g.strip_ids[i + 2 - 3] += 2;
                if ((!sign && (g.strip_ids[i + 2 - 3] % 2 == 0)) || (sign && (g.strip_ids[i + 2 - 3] % 2 != 0))) {
                    g.strip_ids[i + 2 - 3] += 1;
                    new_indices.push_back(vtx_indices[beg]);

                }
                g.strip_ids[i + 2 - 3] += g.strip_ids[i + 2] + 2;
                g.strip_ids.erase(g.strip_ids.begin() + i, g.strip_ids.begin() + i + 3);
                i -= 3;
            } else {
                cur_s = g.strip_ids[i];
                g.strip_ids[i + 2] += 2;
                g.strip_ids[i + 1] = (int)new_indices.size();
                if (!sign) {
                    g.strip_ids[i + 2] += 1;
                    new_indices.push_back(vtx_indices[beg]);
                }
            }
            new_indices.insert(new_indices.end(), &vtx_indices[beg], &vtx_indices[end]);
        }

    }
    new_indices.shrink_to_fit();
    printf("%li\n", clock() - t1);
    t1 = clock();

    clock_t find_time = 0;

    std::vector<bool> done_bools(num_vtx_attribs);
    std::vector<float> new_attribs(vtx_attribs, vtx_attribs + num_vtx_attribs);
    for (auto &g : skel_.bone_groups) {
        std::vector<int> moved_points;
        for (size_t i = 0; i < g.strip_ids.size(); i += 3) {
            for (int j = g.strip_ids[i + 1]; j < g.strip_ids[i + 1] + g.strip_ids[i + 2]; j++) {
                if (done_bools[new_indices[j]]) {
                    if (&g - &skel_.bone_groups[0] != 0) {
                        bool b = false;
                        for (size_t k = 0; k < moved_points.size(); k += 2) {
                            if (new_indices[j] == moved_points[k]) {
                                new_indices[j] = (unsigned short)moved_points[k + 1];
                                b = true;
                                break;
                            }
                        }
                        if (!b) {
                            new_attribs.insert(new_attribs.end(), &vtx_attribs[new_indices[j] * 16],
                                               &vtx_attribs[new_indices[j] * 16] + 16);
                            moved_points.push_back(new_indices[j]);
                            moved_points.push_back((int)new_attribs.size() / 16 - 1);
                            new_indices[j] = (unsigned short)(new_attribs.size() / 16 - 1);
                        } else {
                            continue;
                        }
                    } else {
                        continue;
                    }
                } else {
                    done_bools[new_indices[j]] = true;
                }
                for (int k = 8; k < 12; k += 1) {
                    if (new_attribs[new_indices[j] * 16 + k + 4] > 0.0f) {
                        int bone_ndx = (int)(std::find(g.bone_ids.begin(), g.bone_ids.end(),
                                                       (int)new_attribs[new_indices[j] * 16 + k]) - g.bone_ids.begin());
                        new_attribs[new_indices[j] * 16 + k] = (float)bone_ndx;
                    }
                }
            }
        }
    }

    printf("---------------------------\n");
    for (auto &g : skel_.bone_groups) {
        printf("%u\n", (unsigned int)g.strip_ids.size() / 3);
    }
    printf("---------------------------\n");

    printf("%li\n", clock() - t1);
    printf("find_time = %li\n", find_time);
    printf("after bone broups2\n");

    indices_size_ = (uint32_t)(new_indices.size() * sizeof(unsigned short));
    indices_.reset(new char[indices_size_], std::default_delete<char[]>());
    memcpy(indices_.get(), &new_indices[0], indices_size_);

    attribs_size_ = (uint32_t)(new_attribs.size() * sizeof(float));
    attribs_.reset(new char[attribs_size_], std::default_delete<char[]>());
    memcpy(attribs_.get(), &new_attribs[0], attribs_size_);
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif