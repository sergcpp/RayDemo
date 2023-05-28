#pragma once

#include <memory>

#include "Config.h"
#include "SceneBase.h"
#include "Types.h"

/**
  @file RendererBase.h
*/

namespace Ray {
/// Renderer flags used to choose backend
enum class eRendererType : uint32_t {
    // Reference renderer, slightly vectorized, the easiest to modify and debug
    Reference,
    // SIMD renderers, heavily vectorized
    SIMD_SSE2,
    SIMD_SSE41,
    SIMD_AVX,
    SIMD_AVX2,
    SIMD_AVX512,
    SIMD_NEON,
    // GPU renderers
    Vulkan,
    DirectX12
};

// All CPU renderers
const Bitmask<eRendererType> RendererCPU = Bitmask<eRendererType>{eRendererType::Reference} | eRendererType::SIMD_SSE2 |
                                           eRendererType::SIMD_SSE41 | eRendererType::SIMD_NEON |
                                           eRendererType::SIMD_AVX | eRendererType::SIMD_AVX2;
// All GPU renderers
const Bitmask<eRendererType> RendererGPU = Bitmask<eRendererType>{eRendererType::Vulkan} | eRendererType::DirectX12;

const char *RendererTypeName(eRendererType rt);
eRendererType RendererTypeFromName(const char *name);

/// Returns whether it is safe to call Render function for non-overlapping regions from different threads
bool RendererSupportsMultithreading(eRendererType rt);

/// Renderer settings
struct settings_t {
    int w = 0, h = 0;
    const char *preferred_device = nullptr;
    bool use_tex_compression = true; // temporarily GPU only
    bool use_hwrt = true;
    bool use_bindless = true;
    bool use_wide_bvh = true;
};

/** Render region context,
    holds information for specific rectangle on image
*/
class RegionContext {
    /// Rectangle on image
    rect_t rect_;

  public:
    int iteration = 0;                   ///< Number of rendered samples per pixel
    std::unique_ptr<float[]> halton_seq; ///< Sequence of random 2D points

    explicit RegionContext(const rect_t &rect) : rect_(rect) {}

    const rect_t &rect() const { return rect_; }

    /// Clear region context (used to start again)
    void Clear() {
        iteration = 0;
        halton_seq = nullptr;
    }
};

class ILog;

/** Base class for all renderer backends
 */
class RendererBase {
  public:
    virtual ~RendererBase() = default;

    /// Type of renderer
    virtual eRendererType type() const = 0;

    /// Log
    virtual ILog *log() const { return nullptr; }

    /// Name of the device
    virtual const char *device_name() const = 0;

    virtual bool is_hwrt() const { return false; }

    /// Returns size of rendered image
    virtual std::pair<int, int> size() const = 0;

    /// Returns pointer to rendered image
    virtual const color_rgba_t *get_pixels_ref() const = 0;

    /// Returns pointer to 'raw' untonemapped image
    virtual const color_rgba_t *get_raw_pixels_ref() const = 0;

    /// Returns pointer to auxiliary image buffers
    virtual const color_rgba_t *get_aux_pixels_ref(eAUXBuffer buf) const = 0;

    /// Returns pointer to SH data
    virtual const shl1_data_t *get_sh_data_ref() const = 0;

    /** @brief Resize framebuffer
        @param w new image width
        @param h new image height
    */
    virtual void Resize(int w, int h) = 0;

    /** @brief Clear framebuffer
        @param c color used to fill image
    */
    virtual void Clear(const color_rgba_t &c) = 0;

    /** @brief Create new scene
        @return pointer to new scene for specific backend
    */
    virtual SceneBase *CreateScene() = 0;

    /** @brief Render image region
        @param scene reference to a scene
        @param region image region to render
    */
    virtual void RenderScene(const SceneBase *scene, RegionContext &region) = 0;

    /** @brief Denoise image region
        @param region image region to denoise
    */
    virtual void DenoiseImage(const RegionContext &region) = 0;

    struct stats_t {
        unsigned long long time_primary_ray_gen_us;
        unsigned long long time_primary_trace_us;
        unsigned long long time_primary_shade_us;
        unsigned long long time_primary_shadow_us;
        unsigned long long time_secondary_sort_us;
        unsigned long long time_secondary_trace_us;
        unsigned long long time_secondary_shade_us;
        unsigned long long time_secondary_shadow_us;
        unsigned long long time_denoise_us;
    };
    virtual void GetStats(stats_t &st) = 0;
    virtual void ResetStats() = 0;
};
} // namespace Ray