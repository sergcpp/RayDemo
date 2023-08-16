#include "TextureStorageCPU.h"

#include <cstring>

#include <algorithm> // for std::max

template <typename T, int N>
int Ray::Cpu::TexStorageLinear<T, N>::Allocate(Span<const ColorType> data, const int res[2], const bool mips) {
    int index = -1;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        index = int(images_.size());
        images_.resize(images_.size() + 1);
    }

    ImgData &p = images_[index];

    p.lod_offsets[0] = 0;
    p.res[0][0] = res[0];
    p.res[0][1] = res[1];

    int total_size = (res[0] * res[1]);

    for (int i = 1; i < NUM_MIP_LEVELS; ++i) {
        if (mips && (p.res[i - 1][0] > 1 || p.res[i - 1][1] > 1)) {
            p.lod_offsets[i] = total_size;
            p.res[i][0] = p.res[i - 1][0] / 2;
            p.res[i][1] = p.res[i - 1][1] / 2;
            total_size += (p.res[i][0] * p.res[i][1]);
        } else {
            p.lod_offsets[i] = p.lod_offsets[i - 1];
            p.res[i][0] = p.res[i - 1][0];
            p.res[i][1] = p.res[i - 1][1];
        }
    }

    p.pixels.reset(new ColorType[total_size]);
    memcpy(p.pixels.get(), data.data(), total_size * sizeof(ColorType));

    p.lod_offsets[0] = 0;
    p.res[0][0] = res[0];
    p.res[0][1] = res[1];

    for (int i = 1; i < NUM_MIP_LEVELS && mips; ++i) {
        ColorType *out_pixels = &p.pixels[p.lod_offsets[i]];

        for (int y = 0; y < p.res[i - 1][1] - 1; y += 2) {
            for (int x = 0; x < p.res[i - 1][0] - 1; x += 2) {
                const ColorType c00 = Get(index, x + 0, y + 0, i - 1);
                const ColorType c10 = Get(index, x + 1, y + 0, i - 1);
                const ColorType c11 = Get(index, x + 1, y + 1, i - 1);
                const ColorType c01 = Get(index, x + 0, y + 1, i - 1);

                ColorType res;
                for (int j = 0; j < N; ++j) {
                    res.v[j] = (c00.v[j] + c10.v[j] + c11.v[j] + c01.v[j]) / 4;
                }

                out_pixels[(y / 2) * p.res[i][0] + (x / 2)] = res;
            }
        }

        for (int y = 0; y < p.res[i][1]; ++y) {
            out_pixels[y * p.res[i][0] + p.res[i][0] - 1] = out_pixels[y * p.res[i][0]];
        }

        memcpy(&out_pixels[(p.res[i][1] - 1) * p.res[i][0]], out_pixels, p.res[i][0] * sizeof(ColorType));
        out_pixels[(p.res[i][1] - 1) * p.res[i][0] + p.res[i][0] - 1] = out_pixels[0];
    }

    return index;
}

template <typename T, int N> bool Ray::Cpu::TexStorageLinear<T, N>::Free(const int index) {
    if (index < 0 || index > int(images_.size())) {
        return false;
    }

#ifndef NDEBUG
    memset(images_[index].res, 0, sizeof(images_[index].res));
    memset(images_[index].lod_offsets, 0, sizeof(images_[index].lod_offsets));
#endif

    images_[index].pixels.reset();
    free_slots_.push_back(index);

    return true;
}

template <typename T, int N>
void Ray::Cpu::TexStorageLinear<T, N>::WriteImageData(const int index, const int lod, const ColorType data[]) {
    const ImgData &p = images_[index];
    const int w = p.res[lod][0], h = p.res[lod][1];
    memcpy(&p.pixels[p.lod_offsets[lod]], data, w * h * sizeof(ColorType));
}

template class Ray::Cpu::TexStorageLinear<uint8_t, 4>;
template class Ray::Cpu::TexStorageLinear<uint8_t, 3>;
template class Ray::Cpu::TexStorageLinear<uint8_t, 2>;
template class Ray::Cpu::TexStorageLinear<uint8_t, 1>;

////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename T, int N>
int Ray::Cpu::TexStorageTiled<T, N>::Allocate(Span<const ColorType> data, const int res[2], bool mips) {
    int index = -1;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        index = int(images_.size());
        images_.resize(images_.size() + 1);
    }

    ImgData &p = images_[index];

    p.lod_offsets[0] = 0;
    p.res[0][0] = res[0];
    p.res[0][1] = res[1];
    p.res_in_tiles[0][0] = (p.res[0][0] + TileSize - 1) / TileSize;
    p.res_in_tiles[0][1] = (p.res[0][1] + TileSize - 1) / TileSize;

    int total_size = (p.res_in_tiles[0][0] * p.res_in_tiles[0][1]) * (TileSize * TileSize);

    for (int i = 1; i < NUM_MIP_LEVELS; ++i) {
        if (mips && (p.res[i - 1][0] > 1 || p.res[i - 1][1] > 1)) {
            p.lod_offsets[i] = total_size;

            p.res[i][0] = p.res[i - 1][0] / 2;
            p.res[i][1] = p.res[i - 1][1] / 2;

            p.res_in_tiles[i][0] = (p.res[i][0] + TileSize - 1) / TileSize;
            p.res_in_tiles[i][1] = (p.res[i][1] + TileSize - 1) / TileSize;

            total_size += (p.res_in_tiles[i][0] * p.res_in_tiles[i][1]) * (TileSize * TileSize);
        } else {
            p.lod_offsets[i] = p.lod_offsets[i - 1];
            p.res[i][0] = p.res[i - 1][0];
            p.res[i][1] = p.res[i - 1][1];
            p.res_in_tiles[i][0] = p.res_in_tiles[i - 1][0];
            p.res_in_tiles[i][1] = p.res_in_tiles[i - 1][1];
        }
    }

    p.pixels.reset(new ColorType[total_size]);

    for (int y = 0; y < res[1]; ++y) {
        const int tiley = y / TileSize, in_tiley = y % TileSize;

        for (int x = 0; x < res[0]; ++x) {
            const int tilex = x / TileSize, in_tilex = x % TileSize;

            p.pixels[(tiley * p.res_in_tiles[0][0] + tilex) * TileSize * TileSize + in_tiley * TileSize + in_tilex] =
                data[y * res[0] + x];
        }
    }

    for (int i = 1; i < NUM_MIP_LEVELS && mips; ++i) {
        ColorType *out_pixels = &p.pixels[p.lod_offsets[i]];

        for (int y = 0; y < p.res[i - 1][1] - 1; y += 2) {
            const int tiley = (y / 2) / TileSize, in_tiley = (y / 2) % TileSize;

            for (int x = 0; x < p.res[i - 1][0] - 1; x += 2) {
                const ColorType c00 = Get(index, x + 0, y + 0, i - 1);
                const ColorType c10 = Get(index, x + 1, y + 0, i - 1);
                const ColorType c11 = Get(index, x + 1, y + 1, i - 1);
                const ColorType c01 = Get(index, x + 0, y + 1, i - 1);

                ColorType res;
                for (int i = 0; i < N; ++i) {
                    res.v[i] = (c00.v[i] + c10.v[i] + c11.v[i] + c01.v[i]) / 4;
                }

                const int tilex = (x / 2) / TileSize, in_tilex = (x / 2) % TileSize;

                out_pixels[(tiley * p.res_in_tiles[i][0] + tilex) * TileSize * TileSize + in_tiley * TileSize +
                           in_tilex] = res;
            }
        }
    }

    return index;
}

template <typename T, int N> bool Ray::Cpu::TexStorageTiled<T, N>::Free(const int index) {
    if (index < 0 || index > int(images_.size())) {
        return false;
    }

#ifndef NDEBUG
    memset(images_[index].res, 0, sizeof(images_[index].res));
    memset(images_[index].res_in_tiles, 0, sizeof(images_[index].res_in_tiles));
    memset(images_[index].lod_offsets, 0, sizeof(images_[index].lod_offsets));
#endif

    images_[index].pixels.reset();
    free_slots_.push_back(index);

    return true;
}

template class Ray::Cpu::TexStorageTiled<uint8_t, 4>;
template class Ray::Cpu::TexStorageTiled<uint8_t, 3>;
template class Ray::Cpu::TexStorageTiled<uint8_t, 2>;
template class Ray::Cpu::TexStorageTiled<uint8_t, 1>;

////////////////////////////////////////////////////////////////////////////////////////////////////

template <typename T, int N>
int Ray::Cpu::TexStorageSwizzled<T, N>::Allocate(Span<const ColorType> data, const int res[2], bool mips) {
    int index = -1;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        index = int(images_.size());
        images_.resize(images_.size() + 1);
    }

    ImgData &p = images_[index];

    p.lod_offsets[0] = 0;
    p.res[0][0] = res[0];
    p.res[0][1] = res[1];
    p.tile_y_stride[0] = swizzle_x_tile(OuterTileW * ((p.res[0][0] + OuterTileW - 1) / OuterTileW));

    int total_size = p.tile_y_stride[0] * ((p.res[0][1] + OuterTileH - 1) / OuterTileH);

    mips = false;

    for (int i = 1; i < NUM_MIP_LEVELS; ++i) {
        if (mips && (p.res[i - 1][0] > 1 || p.res[i - 1][1] > 1)) {
            p.lod_offsets[i] = total_size;

            p.res[i][0] = (p.res[i - 1][0] / 2);
            p.res[i][1] = (p.res[i - 1][1] / 2);

            p.tile_y_stride[i] = swizzle_x_tile(OuterTileW * ((p.res[i][0] + OuterTileW - 1) / OuterTileW));

            total_size += p.tile_y_stride[i] * ((p.res[i][1] + OuterTileH - 1) / OuterTileH);
        } else {
            p.lod_offsets[i] = p.lod_offsets[i - 1];
            p.res[i][0] = p.res[i - 1][0];
            p.res[i][1] = p.res[i - 1][1];
            p.tile_y_stride[i] = p.tile_y_stride[i - 1];
        }
    }

    p.pixels.reset(new ColorType[total_size]);

    for (int y = 0; y < res[1]; ++y) {
        const uint32_t y_off = (y / OuterTileH) * p.tile_y_stride[0] + swizzle_y(y);
        for (int x = 0; x < res[0]; ++x) {
            const uint32_t x_off = swizzle_x_tile(x);
            p.pixels[y_off + x_off] = data[y * res[0] + x];
        }
    }

    for (int i = 1; i < NUM_MIP_LEVELS && mips; ++i) {
        ColorType *out_pixels = &p.pixels[p.lod_offsets[i]];

        for (int y = 0; y < p.res[i - 1][1] - 1; y += 2) {
            const uint32_t y_off = ((y / 2) / OuterTileH) * p.tile_y_stride[i] + swizzle_y(y / 2);

            for (int x = 0; x < p.res[i - 1][0] - 1; x += 2) {
                const ColorType c00 = Get(index, x + 0, y + 0, i - 1);
                const ColorType c10 = Get(index, x + 1, y + 0, i - 1);
                const ColorType c11 = Get(index, x + 1, y + 1, i - 1);
                const ColorType c01 = Get(index, x + 0, y + 1, i - 1);

                ColorType res;
                for (int i = 0; i < N; ++i) {
                    res.v[i] = (c00.v[i] + c10.v[i] + c11.v[i] + c01.v[i]) / 4;
                }

                const uint32_t x_off = swizzle_x_tile(x / 2);

                out_pixels[y_off + x_off] = res;
            }
        }
    }

    return index;
}

template <typename T, int N> bool Ray::Cpu::TexStorageSwizzled<T, N>::Free(const int index) {
    if (index < 0 || index > int(images_.size())) {
        return false;
    }

#ifndef NDEBUG
    memset(images_[index].res, 0, sizeof(images_[index].res));
    memset(images_[index].tile_y_stride, 0, sizeof(images_[index].tile_y_stride));
    memset(images_[index].lod_offsets, 0, sizeof(images_[index].lod_offsets));
#endif

    images_[index].pixels.reset();
    free_slots_.push_back(index);

    return true;
}

template class Ray::Cpu::TexStorageSwizzled<uint8_t, 4>;
template class Ray::Cpu::TexStorageSwizzled<uint8_t, 3>;
template class Ray::Cpu::TexStorageSwizzled<uint8_t, 2>;
template class Ray::Cpu::TexStorageSwizzled<uint8_t, 1>;

////////////////////////////////////////////////////////////////////////////////////////////////////

template <int N> int Ray::Cpu::TexStorageBCn<N>::Allocate(Span<const InColorType> data, const int res[2], bool mips) {
    int index = -1;
    if (!free_slots_.empty()) {
        index = free_slots_.back();
        free_slots_.pop_back();
    } else {
        index = int(images_.size());
        images_.resize(images_.size() + 1);
    }

    ImgData &p = images_[index];

    p.lod_offsets[0] = 0;
    p.res[0][0] = res[0];
    p.res[0][1] = res[1];
    p.res_in_tiles[0][0] = (p.res[0][0] + TileSize - 1) / TileSize;
    p.res_in_tiles[0][1] = (p.res[0][1] + TileSize - 1) / TileSize;

    int total_size = 0;
    if (N == 4) {
        total_size = GetRequiredMemory_BC3(res[0], res[1], 1);
    } else if (N == 2) {
        total_size = GetRequiredMemory_BC5(res[0], res[1], 1);
    } else if (N == 1) {
        total_size = GetRequiredMemory_BC4(res[0], res[1], 1);
    }

    for (int i = 1; i < NUM_MIP_LEVELS; ++i) {
        if (mips && (p.res[i - 1][0] > 4 || p.res[i - 1][1] > 4)) {
            p.lod_offsets[i] = total_size;

            p.res[i][0] = p.res[i - 1][0] / 2;
            p.res[i][1] = p.res[i - 1][1] / 2;

            p.res_in_tiles[i][0] = (p.res[i][0] + TileSize - 1) / TileSize;
            p.res_in_tiles[i][1] = (p.res[i][1] + TileSize - 1) / TileSize;

            if (N == 4) {
                total_size += GetRequiredMemory_BC3(p.res[i][0], p.res[i][1], 1);
            } else if (N == 2) {
                total_size += GetRequiredMemory_BC5(p.res[i][0], p.res[i][1], 1);
            } else if (N == 1) {
                total_size += GetRequiredMemory_BC4(p.res[i][0], p.res[i][1], 1);
            }
        } else {
            p.lod_offsets[i] = p.lod_offsets[i - 1];
            p.res[i][0] = p.res[i - 1][0];
            p.res[i][1] = p.res[i - 1][1];
            p.res_in_tiles[i][0] = p.res_in_tiles[i - 1][0];
            p.res_in_tiles[i][1] = p.res_in_tiles[i - 1][1];
        }
    }

    p.pixels.reset(new uint8_t[total_size]);
    if (N == 4) {
        // TODO: get rid of this allocation
        auto temp_YCoCg = ConvertRGB_to_CoCgxY(&data[0].v[0], res[0], res[1]);
        CompressImage_BC3<true /* Is_YCoCg */>(temp_YCoCg.get(), res[0], res[1], p.pixels.get());
    } else if (N == 2) {
        CompressImage_BC5(&data[0].v[0], res[0], res[1], p.pixels.get());
    } else if (N == 1) {
        CompressImage_BC4(&data[0].v[0], res[0], res[1], p.pixels.get());
    }

    // TODO: try to get rid of these allocations
    std::vector<InColorType> _src_data, dst_data;
    for (int i = 1; i < NUM_MIP_LEVELS && mips; ++i) {
        if (p.res[i][0] < 4 || p.res[i][1] < 4) {
            break;
        }

        dst_data.clear();
        dst_data.reserve(p.res[i][0] * p.res[i][1]);

        const InColorType *src_data = (i == 1) ? data.data() : _src_data.data();

        for (int y = 0; y < p.res[i][1]; ++y) {
            for (int x = 0; x < p.res[i][0]; ++x) {
                const InColorType c00 = src_data[(2 * y + 0) * p.res[i - 1][0] + (2 * x + 0)];
                const InColorType c10 =
                    src_data[(2 * y + 0) * p.res[i - 1][0] + std::min(2 * x + 1, p.res[i - 1][0] - 1)];
                const InColorType c11 = src_data[std::min(2 * y + 1, p.res[i - 1][1] - 1) * p.res[i - 1][0] +
                                                 std::min(2 * x + 1, p.res[i - 1][0] - 1)];
                const InColorType c01 =
                    src_data[std::min(2 * y + 1, p.res[i - 1][1] - 1) * p.res[i - 1][0] + (2 * x + 0)];

                InColorType res;
                for (int j = 0; j < N; ++j) {
                    res.v[j] = (c00.v[j] + c10.v[j] + c11.v[j] + c01.v[j]) / 4;
                }

                dst_data.push_back(res);
            }
        }

        assert(dst_data.size() == (p.res[i][0] * p.res[i][1]));

        if (N == 4) {
            // TODO: get rid of this allocation
            auto temp_YCoCg = ConvertRGB_to_CoCgxY(&dst_data[0].v[0], p.res[i][0], p.res[i][1]);
            CompressImage_BC3<true /* Is_YCoCg */>(temp_YCoCg.get(), p.res[i][0], p.res[i][1],
                                                   p.pixels.get() + p.lod_offsets[i]);
        } else if (N == 2) {
            CompressImage_BC5(&dst_data[0].v[0], p.res[i][0], p.res[i][1], p.pixels.get() + p.lod_offsets[i]);
        } else if (N == 1) {
            CompressImage_BC4(&dst_data[0].v[0], p.res[i][0], p.res[i][1], p.pixels.get() + p.lod_offsets[i]);
        }

        std::swap(_src_data, dst_data);
    }

    return index;
}

template class Ray::Cpu::TexStorageBCn<1>;
template class Ray::Cpu::TexStorageBCn<2>;
template class Ray::Cpu::TexStorageBCn<4>;
