#pragma once

#include "../CoreVK.h"
#include "../TextureSplitter.h"
#include "Sampler.h"

namespace Ray {
namespace Vk {
class Context;

enum class eTexFormat : uint8_t;

class TextureAtlas {
    Context *ctx_;

    eTexFormat format_, real_format_;
    const int res_[2];

    VkImage img_ = VK_NULL_HANDLE;
    VkDeviceMemory mem_ = VK_NULL_HANDLE;
    VkImageView img_view_ = VK_NULL_HANDLE;
    Sampler sampler_;

    std::vector<TextureSplitter> splitters_;

    void WritePageData(int page, int posx, int posy, int sizex, int sizey, const void *data);

  public:
    TextureAtlas(Context *ctx, eTexFormat format, int resx, int resy, int page_count = 1);
    ~TextureAtlas();

    eTexFormat format() const { return format_; }
    VkImage vk_image() const { return img_; }
    VkImageView vk_imgage_view() const { return img_view_; }
    VkSampler vk_sampler() const { return sampler_.vk_handle(); }

    int page_count() const { return int(splitters_.size()); }

    template <typename T, int N> int Allocate(const color_t<T, N> *data, const int res[2], int pos[2]);
    int Allocate(const int res[2], int pos[2]);
    bool Free(int page, const int pos[2]);

    bool Resize(int pages_count);

    int DownsampleRegion(int src_page, const int src_pos[2], const int src_res[2], int dst_pos[2]);

    mutable eResState resource_state = eResState::Undefined;
};
} // namespace Vk
} // namespace Ray