#pragma once

#include "Anim.h"
#include "Buffer.h"
#include "Material.h"
#include "Mesh.h"
#include "Program.h"
#include "Texture.h"

struct SWcontext;

namespace Ren {
class Context {
protected:
    int w_ = 0, h_ = 0;

    MeshStorage      meshes_;
    MaterialStorage  materials_;
    ProgramStorage   programs_;
    Texture2DStorage textures_;
    AnimSeqStorage   anims_;
    BufferStorage    buffers_;

    BufferRef       default_vertex_buf_, default_indices_buf_;

    SWcontext       *sw_ctx_;

public:
    ~Context();

    void Init(int w, int h);

    int w() const {
        return w_;
    }
    int h() const {
        return h_;
    }

    BufferRef default_vertex_buf() const { return default_vertex_buf_; }
    BufferRef default_indices_buf() const { return default_indices_buf_; }

    void Resize(int w, int h);

    /*** Mesh ***/
    MeshRef LoadMesh(const char *name, std::istream &data, material_load_callback on_mat_load);
    MeshRef LoadMesh(const char *name, std::istream &data, material_load_callback on_mat_load,
                     const BufferRef &vertex_buf, const BufferRef &index_buf);

    /*** Material ***/
    MaterialRef LoadMaterial(const char *name, const char *mat_src, eMatLoadStatus *status, const program_load_callback &on_prog_load,
                             const texture_load_callback &on_tex_load);
    int NumMaterialsNotReady();
    void ReleaseMaterials();

    /*** Program ***/
    ProgramRef LoadProgramSW(const char *name, void *vs_shader, void *fs_shader, int num_fvars,
                             const Attribute *attrs, const Uniform *unifs, eProgLoadStatus *load_status);
    int NumProgramsNotReady();
    void ReleasePrograms();

    /*** Texture ***/
    Texture2DRef LoadTexture2D(const char *name, const void *data, int size, const Texture2DParams &p, eTexLoadStatus *load_status);
    Texture2DRef LoadTextureCube(const char *name, const void *data[6], const int size[6], const Texture2DParams &p, eTexLoadStatus *load_status);

    int NumTexturesNotReady();
    void ReleaseTextures();

    /*** Anims ***/
    AnimSeqRef LoadAnimSequence(const char *name, std::istream &data);
    int NumAnimsNotReady();
    void ReleaseAnims();

    /*** Buffers ***/
    BufferRef CreateBuffer(uint32_t initial_size);
    void ReleaseBuffers();

    void ReleaseAll();

    int max_uniform_vec4 = 0;
};

}