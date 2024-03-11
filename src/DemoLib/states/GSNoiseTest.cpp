#include "GSNoiseTest.h"

#include <fstream>
#include <random>

#include <Ray/internal/CoreRef.h>
#include <SW/SW.h>
#include <Sys/AssetFile.h>
#include <Sys/Json.h>

#include <tinyexr/tinyexr.h>

#include "../Viewer.h"
#include "../eng/GameStateManager.h"
#include "../eng/Random.h"
#include "../gui/FontStorage.h"
#include "../gui/Renderer.h"
#include "../load/Load.h"
#include "../ren/Context.h"
#include "../ren/MMat.h"

#define float_to_byte(val)                                                                                             \
    (((val) <= 0.0f) ? 0 : (((val) > (1.0f - 0.5f / 255.0f)) ? 255 : uint8_t((255.0f * (val)) + 0.5f)))

namespace GSNoiseTestInternal {
using namespace Ray;

Ref::fvec4 permute(const Ref::fvec4 &x) { return mod(((x * 34.0f) + 1.0f) * x, Ref::fvec4{289.0f}); }

Ref::fvec4 taylor_inv_sqrt(const Ref::fvec4 &r) { return 1.79284291400159f - 0.85373472095314f * r; }

Ref::fvec4 fade(const Ref::fvec4 &t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

Ref::fvec4 step(const Ref::fvec4 &x, const Ref::fvec4 &edge) {
    Ref::fvec4 ret = 0.0f;
    where(x >= edge, ret) = 1.0f;
    return ret;
}

// Classic Perlin noise, periodic version
float PerlinNoise(const Ref::fvec4 &P, const Ref::fvec4 &rep) {
    const Ref::fvec4 Pi0 = mod(floor(P), rep);   // Integer part modulo rep
    const Ref::fvec4 Pi1 = mod(Pi0 + 1.0f, rep); // Integer part + 1 mod rep
    const Ref::fvec4 Pf0 = fract(P);             // Fractional part for interpolation
    const Ref::fvec4 Pf1 = Pf0 - 1.0f;           // Fractional part - 1.0
    const Ref::fvec4 ix = {Pi0[0], Pi1[0], Pi0[0], Pi1[0]};
    const Ref::fvec4 iy = {Pi0[1], Pi0[1], Pi1[1], Pi1[1]};
    const Ref::fvec4 iz0 = Pi0[2];
    const Ref::fvec4 iz1 = Pi1[2];
    const Ref::fvec4 iw0 = Pi0[3];
    const Ref::fvec4 iw1 = Pi1[3];

    const Ref::fvec4 ixy = permute(permute(ix) + iy);
    const Ref::fvec4 ixy0 = permute(ixy + iz0);
    const Ref::fvec4 ixy1 = permute(ixy + iz1);
    const Ref::fvec4 ixy00 = permute(ixy0 + iw0);
    const Ref::fvec4 ixy01 = permute(ixy0 + iw1);
    const Ref::fvec4 ixy10 = permute(ixy1 + iw0);
    const Ref::fvec4 ixy11 = permute(ixy1 + iw1);

    Ref::fvec4 gx00 = ixy00 / 7.0f;
    Ref::fvec4 gy00 = floor(gx00) / 7.0f;
    Ref::fvec4 gz00 = floor(gy00) / 6.0f;
    gx00 = fract(gx00) - 0.5f;
    gy00 = fract(gy00) - 0.5f;
    gz00 = fract(gz00) - 0.5f;
    Ref::fvec4 gw00 = 0.75f - abs(gx00) - abs(gy00) - abs(gz00);
    Ref::fvec4 sw00 = step(gw00, 0.0f);
    gx00 -= sw00 * (step(0.0f, gx00) - 0.5f);
    gy00 -= sw00 * (step(0.0f, gy00) - 0.5f);

    Ref::fvec4 gx01 = ixy01 / 7.0f;
    Ref::fvec4 gy01 = floor(gx01) / 7.0f;
    Ref::fvec4 gz01 = floor(gy01) / 6.0f;
    gx01 = fract(gx01) - 0.5f;
    gy01 = fract(gy01) - 0.5f;
    gz01 = fract(gz01) - 0.5f;
    Ref::fvec4 gw01 = 0.75f - abs(gx01) - abs(gy01) - abs(gz01);
    Ref::fvec4 sw01 = step(gw01, 0.0f);
    gx01 -= sw01 * (step(0.0f, gx01) - 0.5f);
    gy01 -= sw01 * (step(0.0f, gy01) - 0.5f);

    Ref::fvec4 gx10 = ixy10 / 7.0f;
    Ref::fvec4 gy10 = floor(gx10) / 7.0f;
    Ref::fvec4 gz10 = floor(gy10) / 6.0f;
    gx10 = fract(gx10) - 0.5f;
    gy10 = fract(gy10) - 0.5f;
    gz10 = fract(gz10) - 0.5f;
    Ref::fvec4 gw10 = 0.75f - abs(gx10) - abs(gy10) - abs(gz10);
    Ref::fvec4 sw10 = step(gw10, 0.0f);
    gx10 -= sw10 * (step(0.0f, gx10) - 0.5f);
    gy10 -= sw10 * (step(0.0f, gy10) - 0.5f);

    Ref::fvec4 gx11 = ixy11 / 7.0f;
    Ref::fvec4 gy11 = floor(gx11) / 7.0f;
    Ref::fvec4 gz11 = floor(gy11) / 6.0f;
    gx11 = fract(gx11) - 0.5f;
    gy11 = fract(gy11) - 0.5f;
    gz11 = fract(gz11) - 0.5f;
    Ref::fvec4 gw11 = 0.75f - abs(gx11) - abs(gy11) - abs(gz11);
    Ref::fvec4 sw11 = step(gw11, 0.0f);
    gx11 -= sw11 * (step(0.0f, gx11) - 0.5f);
    gy11 -= sw11 * (step(0.0f, gy11) - 0.5f);

    auto g0000 = Ref::fvec4(gx00[0], gy00[0], gz00[0], gw00[0]);
    auto g1000 = Ref::fvec4(gx00[1], gy00[1], gz00[1], gw00[1]);
    auto g0100 = Ref::fvec4(gx00[2], gy00[2], gz00[2], gw00[2]);
    auto g1100 = Ref::fvec4(gx00[3], gy00[3], gz00[3], gw00[3]);
    auto g0010 = Ref::fvec4(gx10[0], gy10[0], gz10[0], gw10[0]);
    auto g1010 = Ref::fvec4(gx10[1], gy10[1], gz10[1], gw10[1]);
    auto g0110 = Ref::fvec4(gx10[2], gy10[2], gz10[2], gw10[2]);
    auto g1110 = Ref::fvec4(gx10[3], gy10[3], gz10[3], gw10[3]);
    auto g0001 = Ref::fvec4(gx01[0], gy01[0], gz01[0], gw01[0]);
    auto g1001 = Ref::fvec4(gx01[1], gy01[1], gz01[1], gw01[1]);
    auto g0101 = Ref::fvec4(gx01[2], gy01[2], gz01[2], gw01[2]);
    auto g1101 = Ref::fvec4(gx01[3], gy01[3], gz01[3], gw01[3]);
    auto g0011 = Ref::fvec4(gx11[0], gy11[0], gz11[0], gw11[0]);
    auto g1011 = Ref::fvec4(gx11[1], gy11[1], gz11[1], gw11[1]);
    auto g0111 = Ref::fvec4(gx11[2], gy11[2], gz11[2], gw11[2]);
    auto g1111 = Ref::fvec4(gx11[3], gy11[3], gz11[3], gw11[3]);

    const Ref::fvec4 norm00 =
        taylor_inv_sqrt(Ref::fvec4{dot(g0000, g0000), dot(g0100, g0100), dot(g1000, g1000), dot(g1100, g1100)});
    g0000 *= norm00[0];
    g0100 *= norm00[1];
    g1000 *= norm00[2];
    g1100 *= norm00[3];

    const Ref::fvec4 norm01 =
        taylor_inv_sqrt(Ref::fvec4{dot(g0001, g0001), dot(g0101, g0101), dot(g1001, g1001), dot(g1101, g1101)});
    g0001 *= norm01[0];
    g0101 *= norm01[1];
    g1001 *= norm01[2];
    g1101 *= norm01[3];

    const Ref::fvec4 norm10 =
        taylor_inv_sqrt(Ref::fvec4{dot(g0010, g0010), dot(g0110, g0110), dot(g1010, g1010), dot(g1110, g1110)});
    g0010 *= norm10[0];
    g0110 *= norm10[1];
    g1010 *= norm10[2];
    g1110 *= norm10[3];

    const Ref::fvec4 norm11 =
        taylor_inv_sqrt(Ref::fvec4{dot(g0011, g0011), dot(g0111, g0111), dot(g1011, g1011), dot(g1111, g1111)});
    g0011 *= norm11[0];
    g0111 *= norm11[1];
    g1011 *= norm11[2];
    g1111 *= norm11[3];

    const float n0000 = dot(g0000, Pf0);
    const float n1000 = dot(g1000, Ref::fvec4{Pf1[0], Pf0[1], Pf0[2], Pf0[3]});
    const float n0100 = dot(g0100, Ref::fvec4{Pf0[0], Pf1[1], Pf0[2], Pf0[3]});
    const float n1100 = dot(g1100, Ref::fvec4{Pf1[0], Pf1[1], Pf0[2], Pf0[3]});
    const float n0010 = dot(g0010, Ref::fvec4{Pf0[0], Pf0[1], Pf1[2], Pf0[3]});
    const float n1010 = dot(g1010, Ref::fvec4{Pf1[0], Pf0[1], Pf1[2], Pf0[3]});
    const float n0110 = dot(g0110, Ref::fvec4{Pf0[0], Pf1[1], Pf1[2], Pf0[3]});
    const float n1110 = dot(g1110, Ref::fvec4{Pf1[0], Pf1[1], Pf1[2], Pf0[3]});
    const float n0001 = dot(g0001, Ref::fvec4{Pf0[0], Pf0[1], Pf0[2], Pf1[3]});
    const float n1001 = dot(g1001, Ref::fvec4{Pf1[0], Pf0[1], Pf0[2], Pf1[3]});
    const float n0101 = dot(g0101, Ref::fvec4{Pf0[0], Pf1[1], Pf0[2], Pf1[3]});
    const float n1101 = dot(g1101, Ref::fvec4{Pf1[0], Pf1[1], Pf0[2], Pf1[3]});
    const float n0011 = dot(g0011, Ref::fvec4{Pf0[0], Pf0[1], Pf1[2], Pf1[3]});
    const float n1011 = dot(g1011, Ref::fvec4{Pf1[0], Pf0[1], Pf1[2], Pf1[3]});
    const float n0111 = dot(g0111, Ref::fvec4{Pf0[0], Pf1[1], Pf1[2], Pf1[3]});
    const float n1111 = dot(g1111, Pf1);

    const Ref::fvec4 fade_xyzw = fade(Pf0);
    const Ref::fvec4 n_0w =
        mix(Ref::fvec4{n0000, n1000, n0100, n1100}, Ref::fvec4{n0001, n1001, n0101, n1101}, fade_xyzw[3]);
    const Ref::fvec4 n_1w =
        mix(Ref::fvec4{n0010, n1010, n0110, n1110}, Ref::fvec4{n0011, n1011, n0111, n1111}, fade_xyzw[3]);
    const Ref::fvec4 n_zw = mix(n_0w, n_1w, fade_xyzw[2]);
    const Ref::fvec2 n_yzw =
        mix(Ref::fvec2{n_zw[0], n_zw[1]}, Ref::fvec2{n_zw[2], n_zw[3]}, fade_xyzw[1]);
    const float n_xyzw = mix(n_yzw[0], n_yzw[1], fade_xyzw[0]);
    return 2.2f * n_xyzw;
}

// Worley noise based on https://www.shadertoy.com/view/Xl2XRR by Marc-Andre Loyer

float hash(float n) { return Ref::fract(sinf(n + 1.951f) * 43758.5453f); }

// hash based 3d value noise
float noise(const Ref::fvec4 &x) {
    Ref::fvec4 p = floor(x);
    Ref::fvec4 f = fract(x);

    f = f * f * (3.0f - 2.0f * f);
    float n = p[0] + p[1] * 57.0f + 113.0f * p[2];
    return mix(mix(mix(hash(n + 0.0f), hash(n + 1.0f), f[0]), mix(hash(n + 57.0f), hash(n + 58.0f), f[0]), f[1]),
               mix(mix(hash(n + 113.0f), hash(n + 114.0f), f[0]), mix(hash(n + 170.0f), hash(n + 171.0f), f[0]), f[1]),
               f[2]);
}

float Cells(const Ref::fvec4 &p, float cellCount) {
    const Ref::fvec4 pCell = p * cellCount;
    float d = 1.0e10f;
    for (int xo = -1; xo <= 1; xo++) {
        for (int yo = -1; yo <= 1; yo++) {
            for (int zo = -1; zo <= 1; zo++) {
                Ref::fvec4 tp = floor(pCell) + Ref::fvec4(xo, yo, zo, 0);

                tp = pCell - tp - noise(mod(tp, Ref::fvec4{cellCount / 1}));
                tp.set<3>(0.0f);

                d = fminf(d, dot(tp, tp));
            }
        }
    }
    d = std::fminf(d, 1.0f);
    d = std::fmaxf(d, 0.0f);
    return d;
}

float WorleyNoise(const Ref::fvec4 &p, float cellCount) { return Cells(p, cellCount); }

} // namespace GSNoiseTestInternal

GSNoiseTest::GSNoiseTest(Viewer *viewer) : viewer_(viewer) {
    state_manager_ = viewer->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
}

void GSNoiseTest::Enter() {
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);

    using namespace GSNoiseTestInternal;

    { // Generate weather texture
        const int WeatherRes = 512;
        const float BaseFrequencies[3] = {4.0f, 16.0f, 4.0f}, YOffsets[3] = {0.0f, 0.25f, 0.5f};
        const int OctavesCount = 10;

        std::unique_ptr<float[]> img_data(new float[WeatherRes * WeatherRes * 3]);

        for (int y = 0; y < WeatherRes; ++y) {
            for (int x = 0; x < WeatherRes; ++x) {
                const float norm_x = float(x) / float(WeatherRes), norm_y = float(y) / float(WeatherRes);

                for (int i = 0; i < 3; ++i) {
                    auto coord = Ref::fvec4{norm_x, YOffsets[i], norm_y, 1.0f};

                    float weight = 1.0f, weight_sum = 0.0f;
                    float fval = 0.0f, frequency = BaseFrequencies[i];

                    if (i == 1) {
                        fval += 1.5f * weight *
                                (0.05f + PerlinNoise(0.125f * frequency *
                                                         fract(coord + Ref::fvec4{0.5f, 0.0f, 0.0f, 0.0f}),
                                                     0.125f * frequency));
                        weight_sum += weight;
                    }

                    for (int j = 0; j < OctavesCount; ++j) {
                        fval += weight * PerlinNoise(frequency * coord, frequency);

                        weight_sum += weight;
                        weight *= 0.5f;
                        frequency *= 2;
                    }

                    img_data[3 * (y * WeatherRes + x) + i] = (fval / weight_sum) * 0.5f + 0.5f;
                }
            }
        }

        // Increase contrast
        for (int i = 0; i < WeatherRes * WeatherRes; ++i) {
            img_data[3 * i + 0] = saturate((img_data[3 * i + 0] - 0.3f) / (0.7f - 0.3f));
            img_data[3 * i + 1] = saturate((img_data[3 * i + 1] - 0.35f) / (0.7f - 0.3f));
            img_data[3 * i + 2] = saturate((img_data[3 * i + 2] - 0.3f) / (0.7f - 0.3f));
        }

        // const char *err = nullptr;
        // SaveEXR(&img_data[0], WeatherRes, WeatherRes, 3, 0, "noise.exr", &err);

        //

        std::ofstream out_file("src/Ray/internal/precomputed/__weather_tex.inl", std::ios::binary);

        out_file << "extern const int WEATHER_TEX_RES = " << WeatherRes << ";\n";

        out_file << "extern const uint8_t __weather_tex[" << WeatherRes * WeatherRes * 3 << "] = {\n    ";
        for (int i = 0; i < WeatherRes * WeatherRes * 3; ++i) {
            out_file << float_to_byte(img_data[i]) << ", ";
        }
        out_file << "\n};\n";
    }

    { // Generate cloud detail texture
        const int NoiseRes = 128;

        std::unique_ptr<float[]> img_data(new float[NoiseRes * NoiseRes * NoiseRes]);

        for (int z = 0; z < NoiseRes; ++z) {
            const float w = float(z) / float(NoiseRes);
            for (int y = 0; y < NoiseRes; ++y) {
                const float v = float(y) / float(NoiseRes);
                for (int x = 0; x < NoiseRes; ++x) {
                    const float u = float(x) / float(NoiseRes);

                    float weight = 1.0f, weight_sum = 0.0f;
                    float val = weight * (2.0f * WorleyNoise(Ref::fvec4{u, v, w, 0.0f}, 8.0f) - 1.0f);
                    weight_sum += weight;

                    weight = 0.5f;
                    val += weight * (2.0f * WorleyNoise(Ref::fvec4{u, v, w, 0.0f}, 16.0f) - 1.0f);
                    weight_sum += weight;

                    weight = 0.5f;
                    val += weight * (2.0f * WorleyNoise(Ref::fvec4{u, v, w, 0.0f}, 32.0f) - 1.0f);
                    weight_sum += weight;

                    weight = 0.25f;
                    val += weight * (2.0f * WorleyNoise(Ref::fvec4{u, v, w, 0.0f}, 64.0f) - 1.0f);
                    weight_sum += weight;

                    val = (val / weight_sum) * 0.5f + 0.5f;
                    img_data[z * NoiseRes * NoiseRes + y * NoiseRes + x] = saturate(val);
                }
            }
        }

        const char *err = nullptr;
        SaveEXR(&img_data[0], NoiseRes, NoiseRes, 1, 0, "noise.exr", &err);

        //

        std::ofstream out_file("src/Ray/internal/precomputed/__3d_noise_tex.inl", std::ios::binary);

        out_file << "extern const int NOISE_3D_RES = " << NoiseRes << ";\n";

        out_file << "extern const uint8_t __3d_noise_tex[" << NoiseRes * NoiseRes * NoiseRes << "] = {\n    ";
        for (int i = 0; i < NoiseRes * NoiseRes * NoiseRes; ++i) {
            out_file << float_to_byte(img_data[i]) << ", ";
        }
        out_file << "\n};\n";
    }
}

void GSNoiseTest::Exit() {}

void GSNoiseTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSNoiseTestInternal;

    int width = viewer_->width, height = viewer_->height;
}

void GSNoiseTest::Update(uint64_t dt_us) {}

void GSNoiseTest::HandleInput(const InputManager::Event &evt) {
    switch (evt.type) {
    case InputManager::RAW_INPUT_P1_DOWN: {

    } break;
    case InputManager::RAW_INPUT_P1_UP:

        break;
    case InputManager::RAW_INPUT_P1_MOVE: {

    } break;
    case InputManager::RAW_INPUT_KEY_UP: {
    } break;
    case InputManager::RAW_INPUT_MOUSE_WHEEL: {
    } break;
    case InputManager::RAW_INPUT_RESIZE:
        break;
    default:
        break;
    }
}
