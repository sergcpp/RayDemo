#include "test_common.h"

#include "../internal/TextureAtlasRef.h"

#include <memory>
#include <random>

void test_atlas() {
    { // Test two types of atlas
        const int AtlasRes = 8192;
        Ray::Ref::TextureAtlasLinear<uint8_t, 4> atlas_linear = {AtlasRes, AtlasRes};
        Ray::Ref::TextureAtlasTiled<uint8_t, 4> atlas_tiled = {AtlasRes, AtlasRes};

        const int TextureRes = 4096;
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

        int res[2] = {TextureRes, TextureRes}, pos_linear[2], pos_tiled[2];
        require(atlas_linear.Allocate(test_pixels.get(), res, pos_linear) == 0);
        require(atlas_tiled.Allocate(test_pixels.get(), res, pos_tiled) == 0);

        for (int y = 0; y < TextureRes; y++) {
            for (int x = 0; x < TextureRes; x++) {
                const Ray::color_t<uint8_t, 4> sampled_color1 =
                                                   atlas_linear.Get(0, pos_linear[0] + x + 1, pos_linear[1] + y + 1),
                                               sampled_color2 =
                                                   atlas_tiled.Get(0, pos_tiled[0] + x + 1, pos_tiled[1] + y + 1);

                const Ray::color_t<uint8_t, 4> &test_color = test_pixels[y * TextureRes + x];
                require(sampled_color1.v[0] == test_color.v[0]);
                require(sampled_color1.v[1] == test_color.v[1]);
                require(sampled_color1.v[2] == test_color.v[2]);
                require(sampled_color1.v[3] == test_color.v[3]);

                require(sampled_color2.v[0] == test_color.v[0]);
                require(sampled_color2.v[1] == test_color.v[1]);
                require(sampled_color2.v[2] == test_color.v[2]);
                require(sampled_color2.v[3] == test_color.v[3]);
            }
        }
    }
}