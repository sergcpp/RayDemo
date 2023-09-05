#pragma once

#include "Buffer.h"
#include "Program.h"
#include "Texture.h"

struct SWcontext;

namespace Ren {
class Context {
protected:
    int w_ = 0, h_ = 0;

    ProgramStorage   programs_;
    Texture2DStorage textures_;
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

    /*** Buffers ***/
    BufferRef CreateBuffer(uint32_t initial_size);
    void ReleaseBuffers();

    void ReleaseAll();

    int max_uniform_vec4 = 0;
};

}