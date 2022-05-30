#pragma once

#include <vector>

#include "../Types.h"
#include "Core.h"

namespace Ray {
namespace Ref {
std::vector<pixel_color8_t> DownsampleTexture(const pixel_color8_t tex[], const int res[2]);

void ComputeTangentBasis(size_t vtx_offset, size_t vtx_start, std::vector<vertex_t> &vertices,
                         std::vector<uint32_t> &new_vtx_indices, const uint32_t *indices, size_t indices_count);
} // namespace Ref
} // namespace Ray