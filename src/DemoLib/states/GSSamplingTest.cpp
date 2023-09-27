#include "GSSamplingTest.h"

#include <fstream>
#include <random>

#include <Ray/internal/Core.h>
#include <Ray/internal/Halton.h>
#include <Ray/internal/SmallVector.h>
#include <SW/SW.h>
#include <Sys/Json.h>

#include "../Viewer.h"
#include "../eng/GameStateManager.h"
#include "../eng/Random.h"
#include "../gui/FontStorage.h"
#include "../gui/Renderer.h"
#include "../ren/Context.h"

namespace Ray {
extern const int __pmj02_sample_count;
extern const float __pmj02_samples[];
} // namespace Ray

namespace GSSamplingTestInternal {
float EvalFunc(const float x, const float y, const float xmax, const float ymax) {
    const float Pi = 3.14159265358979323846f;
    return 0.5f + 0.5f * std::sin(2.0f * Pi * (x / xmax) * std::exp(8.0f * (x / xmax)));
}

uint32_t hash(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

float construct_float(uint32_t m) {
    const uint32_t ieeeMantissa = 0x007FFFFFu; // binary32 mantissa bitmask
    const uint32_t ieeeOne = 0x3F800000u;      // 1.0 in IEEE binary32

    m &= ieeeMantissa; // Keep only mantissa bits (fractional part)
    m |= ieeeOne;      // Add fractional part to 1.0

    float f = reinterpret_cast<float &>(m); // Range [1:2]
    return f - 1.0f;                        // Range [0:1]
}

std::vector<uint16_t> radical_inv_perms;

std::mt19937 gen;
std::uniform_real_distribution<float> n_float_distr(0, 1);

// clang-format off
unsigned permute(unsigned i, unsigned l, unsigned p) {
    unsigned w = l - 1;
    w |= w >> 1;
    w |= w >> 2;
    w |= w >> 4;
    w |= w >> 8;
    w |= w >> 16;
    do {
        i ^= p;             i *= 0xe170893d;
        i ^= p       >> 16;
        i ^= (i & w) >> 4;
        i ^= p       >> 8;  i *= 0x0929eb3f;
        i ^= p       >> 23;
        i ^= (i & w) >> 1;  i *= 1 | p >> 27;
                            i *= 0x6935fa69;
        i ^= (i & w) >> 11; i *= 0x74dcb303;
        i ^= (i & w) >> 2;  i *= 0x9e501cc3;
        i ^= (i & w) >> 2;  i *= 0xc860a3df;
        i &= w;
        i ^= i       >> 5;
    } while (i >= l);
    return (i + p) % l;
}

float randfloat(unsigned i, unsigned p) {
    i ^= p;
    i ^= i >> 17;
    i ^= i >> 10; i *= 0xb36534e5;
    i ^= i >> 12;
    i ^= i >> 21; i *= 0x93fc4795;
    i ^= 0xdf6e307f;
    i ^= i >> 17; i *= 1 | p >> 18;
    return i * (1.0f / 4294967808.0f);
}

std::pair<float, float> cmj_ordered(int s, int m, int n, int p) {
    int sx = permute(s % m, m, p * 0xa511e9b3);
    int sy = permute(s / m, n, p * 0x63d83595);
    float jx = randfloat(s, p * 0xa399d265);
    float jy = randfloat(s, p * 0x711ad6a5);
    std::pair<float, float> r = {(s % m + (sy + jx) / n) / m,
                                 (s / m + (sx + jy) / m) / n};
    return r;
}

std::pair<float, float> cmj(int s, int N, int p, float a = 1.0f) {
    int m = static_cast<int>(sqrtf(N * a));
    int n = (N + m - 1) / m;
    s = permute(s, N, p * 0x51633e2d);
    int sx = permute(s % m, m, p * 0x68bc21eb);
    int sy = permute(s / m, n, p * 0x02e5be93);
    float jx = randfloat(s, p * 0x967a889b);
    float jy = randfloat(s, p * 0x368cc8b7);
    std::pair<float, float> r = {(sx + (sy + jx) / n) / m,
                                 (s + jy) / N};
    return r;
}
// clang-format on

uint32_t hash_combine(uint32_t seed, uint32_t v) { return seed ^ (v + (seed << 6) + (seed >> 2)); }

inline uint32_t reverse_bits(uint32_t x) {
    x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
    x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
    x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
    x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));
    return ((x >> 16) | (x << 16));
}

inline uint32_t laine_karras_permutation(uint32_t x, uint32_t seed) {
    x += seed;
    x ^= x * 0x6c50b47cu;
    x ^= x * 0xb82f1e52u;
    x ^= x * 0xc7afe638u;
    x ^= x * 0x8d22f6e6u;
    return x;
}

inline uint32_t nested_uniform_scramble_base2(uint32_t x, uint32_t seed) {
    x = reverse_bits(x);
    x = laine_karras_permutation(x, seed);
    x = reverse_bits(x);
    return x;
}
} // namespace GSSamplingTestInternal

GSSamplingTest::GSSamplingTest(Viewer *viewer) : viewer_(viewer) {
    state_manager_ = viewer->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    random_ = viewer->random.get();
}

void GSSamplingTest::Enter() {
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);

    using namespace GSSamplingTestInternal;

    radical_inv_perms = Ray::ComputeRadicalInversePermutations(Ray::g_primes, Ray::PrimesCount, ::rand);
}

void GSSamplingTest::Exit() {}

void GSSamplingTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSSamplingTestInternal;

    const int width = viewer_->width, height = viewer_->height;

    int sample_limit = 1;
    if (++iteration_ > sample_limit) {
        // return;
    }

    pixels_.resize(width * height * 4);

    static int loop_index = 0;
    if (iteration_ == 1) {
        std::fill(pixels_.begin(), pixels_.end(), 0.0f);
        loop_index++;
    }

    const int nsamplesx = 8, nsamplesy = 8;

#if 1
    // grid
    for (int y = 0; y < height; ++y) {
        for (int i = 1; i < nsamplesx * nsamplesy; ++i) {
            const int x = int((float(i) / float(nsamplesx * nsamplesy)) * width);

            pixels_[4 * (y * width + x) + 0] =
                std::max(pixels_[4 * (y * width + x) + 0], (i % nsamplesx == 0) ? 1.0f : 0.25f);
            pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
            pixels_[4 * (y * width + x) + 3] = 1.0f;
        }
    }
    for (int j = 1; j < nsamplesx * nsamplesy; ++j) {
        for (int x = 0; x < width; ++x) {
            const int y = int((float(j) / float(nsamplesx * nsamplesy)) * height);

            pixels_[4 * (y * width + x) + 0] =
                std::max(pixels_[4 * (y * width + x) + 0], (j % nsamplesy == 0) ? 1.0f : 0.25f);
            pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
            pixels_[4 * (y * width + x) + 3] = 1.0f;
        }
    }

    // canonical arrangement
    /*Ray::SmallVector<std::pair<float, float>, 128> points;
    for (int j = 0; j < nsamplesy; ++j) {
        for (int i = 0; i < nsamplesx; ++i) {
            const float rx = (i + (j + n_float_distr(gen)) / nsamplesy) / nsamplesx;
            const float ry = (j + (i + n_float_distr(gen)) / nsamplesx) / nsamplesy;

            points.emplace_back(rx, ry);
        }
    }

    // shuffle
    for (int j = 0; j < nsamplesy; ++j) {
        const int k = int(j + n_float_distr(gen) * (nsamplesy - j));
        for (int i = 0; i < nsamplesx; ++i) {
            // const int k = int(j + n_float_distr(gen) * (nsamplesy - j));
            std::swap(points[j * nsamplesx + i].first, points[k * nsamplesx + i].first);
        }
    }
    for (int i = 0; i < nsamplesx; ++i) {
        const int k = int(i + n_float_distr(gen) * (nsamplesx - i));
        for (int j = 0; j < nsamplesy; ++j) {
            // const int k = int(i + n_float_distr(gen) * (nsamplesx - i));
            std::swap(points[j * nsamplesx + i].second, points[j * nsamplesx + k].second);
        }
    }

    for (const auto &p : points) {
        const int x = int(p.first * float(width));
        const int y = height - 1 - int(p.second * float(height));

        for (int offy = -1; offy <= 1; ++offy) {
            const int yy = std::min(std::max(y + offy, 0), height - 1);
            for (int offx = -1; offx <= 1; ++offx) {
                const int xx = std::min(std::max(x + offx, 0), width - 1);

                pixels_[4 * (yy * width + xx) + 0] = pixels_[4 * (yy * width + xx) + 1] =
                    pixels_[4 * (yy * width + xx) + 2] = 1.0f;
                pixels_[4 * (yy * width + xx) + 3] = 1.0f;
            }
        }
    }*/

    // for (int i = 0; i < nsamplesx * nsamplesy; ++i) {
    /*{
        int i = iteration_;

        float rx, ry;
        // std::tie(rx, ry) = cmj_ordered(i, nsamplesx, nsamplesy, 0);
        std::tie(rx, ry) = cmj(i, nsamplesx * nsamplesy, 0);

        const int x = int(rx * float(width));
        const int y = height - 1 - int(ry * float(height));

        for (int offy = -1; offy <= 1; ++offy) {
            const int yy = std::min(std::max(y + offy, 0), height - 1);
            for (int offx = -1; offx <= 1; ++offx) {
                const int xx = std::min(std::max(x + offx, 0), width - 1);

                pixels_[4 * (yy * width + xx) + 0] = pixels_[4 * (yy * width + xx) + 1] =
                    pixels_[4 * (yy * width + xx) + 2] = 1.0f;
                pixels_[4 * (yy * width + xx) + 3] = 1.0f;
            }
        }
    }*/

    {
        const float S = float(1.0f / (1ull << 32));
        const float temp = (1ull << 32);

        const int i = std::min(iteration_, Ray::__pmj02_sample_count - 1);

        uint32_t x_seed = hash_combine(0x98fc82u, loop_index);
        uint32_t y_seed = hash_combine(0xab773au, loop_index);
        uint32_t shuffle_seed = hash_combine(0xacc75au, loop_index);

        uint32_t shuffled_i = nested_uniform_scramble_base2(i, shuffle_seed) % Ray::__pmj02_sample_count;

        float rx = Ray::__pmj02_samples[2 * shuffled_i + 0];
        float ry = Ray::__pmj02_samples[2 * shuffled_i + 1];

        uint32_t ux = uint32_t(rx * 16777216.0f) << 8;
        uint32_t uy = uint32_t(ry * 16777216.0f) << 8;

        ux = nested_uniform_scramble_base2(ux, x_seed);
        uy = nested_uniform_scramble_base2(uy, y_seed);

        rx = float(ux >> 8) / 16777216.0f;
        ry = float(uy >> 8) / 16777216.0f;

        const int x = int(rx * float(width));
        const int y = height - 1 - int(ry * float(height));

        for (int offy = -1; offy <= 1; ++offy) {
            const int yy = std::min(std::max(y + offy, 0), height - 1);
            for (int offx = -1; offx <= 1; ++offx) {
                const int xx = std::min(std::max(x + offx, 0), width - 1);

                pixels_[4 * (yy * width + xx) + 0] = pixels_[4 * (yy * width + xx) + 1] =
                    pixels_[4 * (yy * width + xx) + 2] = 1.0f;
                pixels_[4 * (yy * width + xx) + 3] = 1.0f;
            }
        }
    }
#endif

#if 0
    for (uint32_t y = 0; y < height; ++y) {
        if (y < height / 4) {
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        sum += EvalFunc(float(x) + (iteration_ - 1) / float(sample_limit) +
                                            (float(nx) + 0.5f) / float(nsamplesx * sample_limit),
                                        float(y) + (float(ny) + 0.5f) / float(nsamplesy), float(width), float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
        } else if (y < 2 * (height / 4)) {
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        sum += EvalFunc(x + random_->GetNormalizedFloat(), y + random_->GetNormalizedFloat(),
                                        float(width), float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
        } else if (y < 3 * (height / 4)) {
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        sum += EvalFunc((x + (nx + random_->GetNormalizedFloat()) / nsamplesx),
                                        (y + (ny + random_->GetNormalizedFloat()) / nsamplesy), float(width),
                                        float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
        } else {
            uint32_t ndx = 0;
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                int i = int(iteration_) - 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        // int last_ndx = ndx;
                        // ndx = ((y - 3 * (height / 4)) * width + x) * nsamplesx * sample_limit * 31 + i * nsamplesx +
                        // nx;
                        ndx = ((y - 3 * (height / 4)) * width + x) * 31 + i * nsamplesx + nx;
                        // ndx = (i * (width + height) + x) * nsamplesx + nx;

                        float ff = construct_float(hash(x));

                        // float rx = RadicalInverse<3>(ndx);
                        float _unused;
                        float rx =
                            std::modf(Ray::ScrambledRadicalInverse(29, &radical_inv_perms[100], ndx) + ff, &_unused);
                        float ry = 0; // RadicalInverse<2>(i * nsamplesx + nx);

                        // sum += EvalFunc(x + (nx + rx) / nsamplesx, y + (ny + ry) / nsamplesy, width, height);
                        sum += EvalFunc(x + rx, y + ry, float(width), float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
        }
    }
#endif

#if 0
    int i = (int)iteration_ * nsamplesx;
    for (int j = 0; j < nsamplesx; j++) {
        const float rx = Ray::ScrambledRadicalInverse(3, &radical_inv_perms[2], (3000 + i + j) % 4096);
        const float ry = Ray::ScrambledRadicalInverse(5, &radical_inv_perms[5], (3000 + i + j) % 4096);

        const int x = int(rx * float(width - 0));
        const int y = height - 1 - int(ry * float(height - 0));

        pixels_[4 * (y * width + x) + 0] = 1.0f;
        pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
        pixels_[4 * (y * width + x) + 3] = 1.0f;

        /*for (int y = 0; y < height; y++) {
        	pixels_[4 * (y * width + x) + 0] = 1.0f;
        	pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
        	pixels_[4 * (y * width + x) + 3] = 1.0f;
        }*/
    }
#endif

#if 0
    int i = (int)iteration_ * nsamplesx;
    for (int j = 0; j < (int)nsamplesx; j++) {
        //float rx = RadicalInverse<3>(i + j);
        float rx = Ray::ScrambledRadicalInverse(3, &radical_inv_perms[2], (i + j));
        //float ry = RadicalInverse<5>(i + j);
        float ry = Ray::ScrambledRadicalInverse(5, &radical_inv_perms[5], (i + j));


        if (true) {
            if (rx < 0.5f) {
                rx = std::sqrt(2.0f * rx) - 1.0f;
            } else {
                rx = 1.0f - std::sqrt(2.0f - 2.0f * rx);
            }

            if (ry < 0.5f) {
                ry = std::sqrt(2.0f * ry) - 1.0f;
            } else {
                ry = 1.0f - std::sqrt(2.0f - 2.0f * ry);
            }

            rx = rx * 0.5f + 0.5f;
            ry = ry * 0.5f + 0.5f;
        }

        int x = (int)(rx * (width - 0));
        int y = (int)(height - 1 - ry * (height - 0));

        pixels_[4 * (y * width + x) + 0] = 1.0f;
        pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
        pixels_[4 * (y * width + x) + 3] = 1.0f;
    }
#endif

    swBlitPixels(0, 0, 0, SW_FLOAT, SW_FRGBA, width, height, &pixels_[0], 1);
}

void GSSamplingTest::Update(uint64_t dt_us) {}

void GSSamplingTest::HandleInput(const InputManager::Event &evt) {
    switch (evt.type) {
    case InputManager::RAW_INPUT_P1_DOWN: {

    } break;
    case InputManager::RAW_INPUT_P1_UP: {

    } break;
    case InputManager::RAW_INPUT_P1_MOVE: {

    } break;
    case InputManager::RAW_INPUT_KEY_UP: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_SPACE) {
            iteration_ = 0;
        }
    } break;
    case InputManager::RAW_INPUT_RESIZE: {
        iteration_ = 0;
    } break;
    default:
        break;
    }
}
