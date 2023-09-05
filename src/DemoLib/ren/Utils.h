#pragma once

#include <memory>
#include <vector>

namespace Ren {
enum eTexColorFormat : int;
std::unique_ptr<uint8_t[]> ReadTGAFile(const void *data, int &w, int &h, eTexColorFormat &format);
}