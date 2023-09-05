#include "Context.h"

#include <algorithm>
#include <istream>

int Ren::Context::NumProgramsNotReady() {
    return (int)std::count_if(programs_.begin(), programs_.end(), [](const Program &p) {
        return !p.ready();
    });
}

void Ren::Context::ReleasePrograms() {
    if (!programs_.Size()) return;
    fprintf(stderr, "---------REMAINING PROGRAMS--------\n");
    for (const auto &p : programs_) {
        fprintf(stderr, "%s %i\n", p.name(), (int)p.prog_id());
    }
    fprintf(stderr, "-----------------------------------\n");
    programs_.Clear();
}

Ren::Texture2DRef Ren::Context::LoadTexture2D(const char *name, const void *data, int size,
        const Texture2DParams &p, eTexLoadStatus *load_status) {
    Texture2DRef ref;
    for (auto it = textures_.begin(); it != textures_.end(); ++it) {
        if (strcmp(it->name(), name) == 0) {
            ref = { &textures_, it.index() };
            break;
        }
    }

    if (!ref) {
        ref = textures_.Add(name, data, size, p, load_status);
    } else {
        if (load_status) *load_status = TexFound;
        if (!ref->ready() && data) {
            ref->Init(name, data, size, p, load_status);
        }
    }

    return ref;
}

Ren::Texture2DRef Ren::Context::LoadTextureCube(const char *name, const void *data[6], const int size[6],
        const Texture2DParams &p, eTexLoadStatus *load_status) {
    Texture2DRef ref;
    for (auto it = textures_.begin(); it != textures_.end(); ++it) {
        if (strcmp(it->name(), name) == 0) {
            ref = { &textures_, it.index() };
            break;
        }
    }

    if (!ref) {
        ref = textures_.Add(name, data, size, p, load_status);
    } else {
        if (ref->ready()) {
            if (load_status) *load_status = TexFound;
        } else if (!ref->ready() && data) {
            ref->Init(name, data, size, p, load_status);
        }
    }

    return ref;
}

int Ren::Context::NumTexturesNotReady() {
    return (int)std::count_if(textures_.begin(), textures_.end(), [](const Texture2D &t) {
        return !t.ready();
    });
}

void Ren::Context::ReleaseTextures() {
    if (!textures_.Size()) return;
    fprintf(stderr, "---------REMAINING TEXTURES--------\n");
    for (const auto &t : textures_) {
        fprintf(stderr, "%s\n", t.name());
    }
    fprintf(stderr, "-----------------------------------\n");
    textures_.Clear();
}

Ren::BufferRef Ren::Context::CreateBuffer(uint32_t initial_size) {
    return buffers_.Add(initial_size);
}

void Ren::Context::ReleaseBuffers() {
    if (!buffers_.Size()) return;
    fprintf(stderr, "---------REMAINING BUFFERS--------\n");
    for (const auto &b : buffers_) {
        fprintf(stderr, "%u\n", b.size());
    }
    fprintf(stderr, "-----------------------------------\n");
    buffers_.Clear();
}

void Ren::Context::ReleaseAll() {
    default_vertex_buf_ = {};
    default_indices_buf_ = {};

    ReleaseTextures();
    ReleaseBuffers();
}