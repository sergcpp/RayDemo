#include "test_common.h"

#include "../internal/TextureStorageCPU.h"

#include <memory>
#include <random>

void test_tex_storage() {
    { // Test three storage layouts
        Ray::Cpu::TexStorageLinear<uint8_t, 4> storage_linear;
        Ray::Cpu::TexStorageTiled<uint8_t, 4> storage_tiled;
        Ray::Cpu::TexStorageSwizzled<uint8_t, 4> storage_swizzled;

        const int TextureRes = 4093;
        auto test_pixels =
            std::unique_ptr<Ray::color_t<uint8_t, 4>[]> { new Ray::color_t<uint8_t, 4>[ TextureRes * TextureRes * 4 ] };

        { // Fill test pixels
            std::uniform_int_distribution<int> dist(0, 255);
            std::mt19937 gen(42);

            for (int j = 0; j < TextureRes; j++) {
                for (int i = 0; i < TextureRes; i++) {
                    test_pixels[j * TextureRes + i].v[0] = static_cast<uint8_t>(dist(gen));
                    test_pixels[j * TextureRes + i].v[1] = static_cast<uint8_t>(dist(gen));
                    test_pixels[j * TextureRes + i].v[2] = static_cast<uint8_t>(dist(gen));
                    test_pixels[j * TextureRes + i].v[3] = static_cast<uint8_t>(dist(gen));
                }
            }
        }

        int res[2] = {TextureRes, TextureRes};
        require_fatal(storage_linear.Allocate(test_pixels.get(), res, false) == 0);
        require_fatal(storage_tiled.Allocate(test_pixels.get(), res, false) == 0);
        require_fatal(storage_swizzled.Allocate(test_pixels.get(), res, false) == 0);

        for (int y = 0; y < TextureRes; ++y) {
            for (int x = 0; x < TextureRes; ++x) {
                const Ray::color_t<uint8_t, 4> sampled_color1 = storage_linear.Get(0, x, y, 0),
                                               sampled_color2 = storage_tiled.Get(0, x, y, 0),
                                               sampled_color3 = storage_swizzled.Get(0, x, y, 0);

                const Ray::color_t<uint8_t, 4> &test_color = test_pixels[y * TextureRes + x];
                require_fatal(sampled_color1.v[0] == test_color.v[0]);
                require_fatal(sampled_color1.v[1] == test_color.v[1]);
                require_fatal(sampled_color1.v[2] == test_color.v[2]);
                require_fatal(sampled_color1.v[3] == test_color.v[3]);

                require_fatal(sampled_color2.v[0] == test_color.v[0]);
                require_fatal(sampled_color2.v[1] == test_color.v[1]);
                require_fatal(sampled_color2.v[2] == test_color.v[2]);
                require_fatal(sampled_color2.v[3] == test_color.v[3]);

                require(sampled_color3.v[0] == test_color.v[0]);
                require(sampled_color3.v[1] == test_color.v[1]);
                require(sampled_color3.v[2] == test_color.v[2]);
                require(sampled_color3.v[3] == test_color.v[3]);
            }
        }
    }
}
