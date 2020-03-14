#include "FramebufferRef.h"

#include <cassert>
#include <cstring>

Ray::Ref::Framebuffer::Framebuffer(int w, int h) : w_(0), h_(0) {
    Resize(w, h, false);
}

void Ray::Ref::Framebuffer::Resize(int w, int h, bool alloc_sh) {
    assert(w > 0 && h > 0);

    if (alloc_sh) {
        size_t buf_size = (size_t)w * h;
        if (sh_data_.size() != buf_size) {
            sh_data_.resize(buf_size, {});
        }
    } else {
        sh_data_.clear();
    }

    if (w_ != w || h_ != h) {
        w_ = w;
        h_ = h;
        size_t buf_size = (size_t)w * h;
        pixels_.resize(buf_size, pixel_color_t{ 0.0f, 0.0f, 0.0f, 0.0f });
    }
}

void Ray::Ref::Framebuffer::Clear(const pixel_color_t &p) {
    for (int i = 0; i < w_; i++) {
        pixels_[i] = p;
    }
    for (int i = 1; i < h_; i++) {
        memcpy(&pixels_[i * w_], &pixels_[0], w_ * sizeof(pixel_color_t));
    }

    if (!sh_data_.empty()) {
        for (int i = 0; i < w_; i++) {
            sh_data_[i] = {};
        }
        for (int i = 1; i < h_; i++) {
            memcpy(&sh_data_[i * w_], &sh_data_[0], w_ * sizeof(shl1_data_t));
        }
    }
}

void Ray::Ref::Framebuffer::ResetSampleData(const rect_t &rect) {
    for (int y = rect.y; y < rect.y + rect.h; y++) {
        for (int x = rect.x; x < rect.x + rect.w; x++) {
            int i = y * w_ + x;
            sh_data_[i].coeff_g[0] = 0.0f;
        }
    }
}

void Ray::Ref::Framebuffer::ComputeSHData(const rect_t &rect) {
    for (int y = rect.y; y < rect.y + rect.h; y++) {
        for (int x = rect.x; x < rect.x + rect.w; x++) {
            int i = y * w_ + x;

            shl1_data_t &sh_data = sh_data_[i];
            const float *sh_coeff = sh_data.coeff_r;
            const float inv_weight = sh_data.coeff_g[0] > FLT_EPS ? (2.0f * PI / sh_data.coeff_g[0]) : 0.0f;

            pixel_color_t p = pixels_[i];
            p.r *= inv_weight;
            p.g *= inv_weight;
            p.b *= inv_weight;

            for (int j = 0; j < 4; j++) {
                sh_data.coeff_g[j] = sh_coeff[j] * p.g;
                sh_data.coeff_b[j] = sh_coeff[j] * p.b;
                sh_data.coeff_r[j] = sh_coeff[j] * p.r;
            }
        }
    }
}

void Ray::Ref::Framebuffer::MixWith(const Framebuffer &f2, const rect_t &rect, float k) {
    for (int y = rect.y; y < rect.y + rect.h; y++) {
        for (int x = rect.x; x < rect.x + rect.w; x++) {
            this->MixPixel(x, y, f2.GetPixel(x, y), k);
        }
    }
}

void Ray::Ref::Framebuffer::MixWith_SH(const Framebuffer &f2, const rect_t &rect, float k) {
    for (int y = rect.y; y < rect.y + rect.h; y++) {
        for (int x = rect.x; x < rect.x + rect.w; x++) {
            this->MixSHData(x, y, f2.GetSHData(x, y), k);
        }
    }
}
