#pragma once

#include <cstdint>

#include "SamplingParams.h"

namespace Ray {
enum class eTexFormat : uint8_t {
    Undefined,
    RawRGB888,
    RawRGBA8888,
    RawRGBA8888Snorm,
    RawBGRA8888,
    RawR32F,
    RawR16F,
    RawR8,
    RawR16UI,
    RawR32UI,
    RawRG88,
    RawRGB32F,
    RawRGBA32F,
    RawRGBE8888,
    RawRGB16F,
    RawRGBA16F,
    RawRG16Snorm,
    RawRG16,
    RawRG16F,
    RawRG32F,
    RawRG32UI,
    RawRGB10_A2,
    RawRG11F_B10F,
    Depth16,
    Depth24Stencil8,
    Depth32Stencil8,
#ifndef __ANDROID__
    Depth32,
#endif
    BC1,
    BC2,
    BC3,
    BC4,
    BC5,
    ASTC,
    None,
    _Count
};

inline bool IsDepthFormat(const eTexFormat format) {
    static_assert(int(eTexFormat::_Count) == 34, "Update the list below!");
    return format == eTexFormat::Depth16 || format == eTexFormat::Depth24Stencil8 ||
           format == eTexFormat::Depth32Stencil8
#ifndef __ANDROID__
           || format == eTexFormat::Depth32;
#else
        ;
#endif
}

inline bool IsDepthStencilFormat(const eTexFormat format) {
    static_assert(int(eTexFormat::_Count) == 34, "Update the list below!");
    return format == eTexFormat::Depth24Stencil8 || format == eTexFormat::Depth32Stencil8;
}

inline bool IsCompressedFormat(const eTexFormat format) {
    static_assert(int(eTexFormat::_Count) == 34, "Update the list below!");
    switch (format) {
    case eTexFormat::BC1:
    case eTexFormat::BC2:
    case eTexFormat::BC3:
    case eTexFormat::BC4:
    case eTexFormat::BC5:
    case eTexFormat::ASTC:
        return true;
    default:
        return false;
    }
    return false;
}

inline bool IsUintFormat(const eTexFormat format) {
    static_assert(int(eTexFormat::_Count) == 34, "Update the list below!");
    if (format == eTexFormat::RawR16UI || format == eTexFormat::RawR32UI || format == eTexFormat::RawRG32UI) {
        return true;
    }
    return false;
}

enum class eTexBlock : uint8_t {
    _4x4,
    _5x4,
    _5x5,
    _6x5,
    _6x6,
    _8x5,
    _8x6,
    _8x8,
    _10x5,
    _10x6,
    _10x8,
    _10x10,
    _12x10,
    _12x12,
    _None
};

enum class eTexFlagBits : uint16_t {
    NoOwnership = (1u << 0u),
    Mutable = (1u << 1u), // TODO: remove this
    Signed = (1u << 2u),
    SRGB = (1u << 3u),
    NoRepeat = (1u << 4u),
    MIPMin = (1u << 5u),
    MIPMax = (1u << 6u),
    NoBias = (1u << 7u),
    UsageScene = (1u << 8u),
    UsageUI = (1u << 9u)
};
using eTexFlags = eTexFlagBits;
inline eTexFlags operator|(eTexFlags a, eTexFlags b) { return eTexFlags(uint16_t(a) | uint16_t(b)); }
inline eTexFlags &operator|=(eTexFlags &a, eTexFlags b) { return a = eTexFlags(uint16_t(a) | uint16_t(b)); }
inline eTexFlags operator&(eTexFlags a, eTexFlags b) { return eTexFlags(uint16_t(a) & uint16_t(b)); }
inline eTexFlags &operator&=(eTexFlags &a, eTexFlags b) { return a = eTexFlags(uint16_t(a) & uint16_t(b)); }
inline eTexFlags operator~(eTexFlags a) { return eTexFlags(~uint16_t(a)); }

enum class eTexUsageBits : uint8_t {
    Transfer = (1u << 0u),
    Sampled = (1u << 1u),
    Storage = (1u << 2u),
    RenderTarget = (1u << 3u)
};
using eTexUsage = eTexUsageBits;

inline eTexUsage operator|(eTexUsage a, eTexUsage b) { return eTexUsage(uint8_t(a) | uint8_t(b)); }
inline eTexUsage &operator|=(eTexUsage &a, eTexUsage b) { return a = eTexUsage(uint8_t(a) | uint8_t(b)); }
inline eTexUsage operator&(eTexUsage a, eTexUsage b) { return eTexUsage(uint8_t(a) & uint8_t(b)); }
inline eTexUsage &operator&=(eTexUsage &a, eTexUsage b) { return a = eTexUsage(uint8_t(a) & uint8_t(b)); }

struct Tex2DParams {
    uint16_t w = 0, h = 0;
    eTexFlags flags = {};
    uint8_t mip_count = 1;
    eTexUsage usage = {};
    uint8_t cube = 0;
    uint8_t samples = 1;
    uint8_t fallback_color[4] = {0, 255, 255, 255};
    eTexFormat format = eTexFormat::Undefined;
    eTexBlock block = eTexBlock::_None;
    SamplingParams sampling;
};
static_assert(sizeof(Tex2DParams) == 22, "!");

inline bool operator==(const Tex2DParams &lhs, const Tex2DParams &rhs) {
    return lhs.w == rhs.w && lhs.h == rhs.h && lhs.flags == rhs.flags && lhs.mip_count == rhs.mip_count &&
           lhs.usage == rhs.usage && lhs.cube == rhs.cube && lhs.samples == rhs.samples &&
           lhs.fallback_color[0] == rhs.fallback_color[0] && lhs.fallback_color[1] == rhs.fallback_color[1] &&
           lhs.fallback_color[2] == rhs.fallback_color[2] && lhs.fallback_color[3] == rhs.fallback_color[3] &&
           lhs.format == rhs.format && lhs.sampling == rhs.sampling;
}
inline bool operator!=(const Tex2DParams &lhs, const Tex2DParams &rhs) { return !operator==(lhs, rhs); }

enum class eTexLoadStatus { Found, Reinitialized, CreatedDefault, CreatedFromData };

int GetColorChannelCount(eTexFormat format);
int GetPerPixelDataLen(eTexFormat format);
int GetBlockLenBytes(eTexFormat format, eTexBlock block);
int GetBlockCount(int w, int h, eTexBlock block);
inline int GetMipDataLenBytes(const int w, const int h, const eTexFormat format, const eTexBlock block) {
    return GetBlockCount(w, h, block) * GetBlockLenBytes(format, block);
}
uint32_t EstimateMemory(const Tex2DParams &params);

eTexFormat FormatFromGLInternalFormat(uint32_t gl_internal_format, eTexBlock *block, bool *is_srgb);
int BlockLenFromGLInternalFormat(uint32_t gl_internal_format);
} // namespace Ray