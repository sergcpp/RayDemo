#include "GSSamplingTest.h"

#include <fstream>

#if defined(USE_SW_RENDER)
#include <Ren/SW/SW.h>
#endif

#include <Eng/GameStateManager.h>
#include <Eng/Random.h>
#include <Ren/Context.h>
#include <Sys/Json.h>
#include <Sys/Log.h>
#include <Gui/Renderer.h>

#include <Ray/internal/Core.h>
#include <Ray/internal/Halton.h>

#include "../Viewer.h"
#include "../ui/FontStorage.h"

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

    m &= ieeeMantissa;                     // Keep only mantissa bits (fractional part)
    m |= ieeeOne;                          // Add fractional part to 1.0

    float  f = reinterpret_cast<float &>(m);                // Range [1:2]
    return f - 1.0f;                        // Range [0:1]
}

std::vector<uint16_t> radical_inv_perms;
}

GSSamplingTest::GSSamplingTest(GameBase *game) : game_(game) {
    state_manager_	= game->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    ctx_			= game->GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    renderer_		= game->GetComponent<Renderer>(RENDERER_KEY);

    random_         = game->GetComponent<Random>(RANDOM_KEY);

    ui_renderer_ = game->GetComponent<Gui::Renderer>(UI_RENDERER_KEY);
    ui_root_ = game->GetComponent<Gui::BaseElement>(UI_ROOT_KEY);

    const auto fonts = game->GetComponent<FontStorage>(UI_FONTS_KEY);
    font_ = fonts->FindFont("main_font");
}

void GSSamplingTest::Enter() {
#if defined(USE_SW_RENDER)
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);
#endif
    using namespace GSSamplingTestInternal;

    radical_inv_perms = Ray::ComputeRadicalInversePermutations(Ray::g_primes, Ray::PrimesCount, ::rand);
}

void GSSamplingTest::Exit() {

}

void GSSamplingTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSSamplingTestInternal;

    //renderer_->set_current_cam(&cam_);
    //renderer_->ClearColorAndDepth(0, 0, 0, 1);

    uint32_t width = (uint32_t)game_->width, height = (uint32_t)game_->height;

    int sample_limit = 32;
    if (++iteration_ > sample_limit) {
        //return;
    }

    pixels_.resize(width * height * 4);

    if (iteration_ == 1) {
        std::fill(pixels_.begin(), pixels_.end(), 0.0f);
    }

    LOGI("%i", int(iteration_));

    uint32_t nsamplesx = 4, nsamplesy = 1;

#if 1
    for (uint32_t y = 0; y < height; ++y) {
        if (y < height / 4) {
            for (uint32_t x = 0; x < width; ++x) {
                float sum = 0;

                for (uint32_t ny = 0; ny < nsamplesy; ++ny) {
                    for (uint32_t nx = 0; nx < nsamplesx; ++nx) {
                        sum += EvalFunc(float(x) + (iteration_ - 1) / float(sample_limit) + (float(nx) + 0.5f) / float(nsamplesx * sample_limit),
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
                        sum += EvalFunc(x + random_->GetNormalizedFloat(),
                                        y + random_->GetNormalizedFloat(), float(width), float(height));
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
                                        (y + (ny + random_->GetNormalizedFloat()) / nsamplesy), float(width), float(height));
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
                        int last_ndx = ndx;
                        //ndx = ((y - 3 * (height / 4)) * width + x) * nsamplesx * sample_limit * 31 + i * nsamplesx + nx;
                        ndx = ((y - 3 * (height / 4)) * width + x) * 31 + i * nsamplesx + nx;
                        //ndx = (i * (width + height) + x) * nsamplesx + nx;
                        //if (!(last_ndx == 0 || last_ndx + 1 == ndx)) {
                        //__debugbreak();
                        //}

                        if (x == 0 && (y - 3 * (height / 4)) == 0) {
                            volatile int ii = 0;
                        }

                        float ff = construct_float(hash(x));

                        //float rx = RadicalInverse<3>(ndx);
                        float _unused;
                        float rx = std::modf(Ray::ScrambledRadicalInverse(29, &radical_inv_perms[100], ndx) + ff, &_unused);
                        float ry = 0;//RadicalInverse<2>(i * nsamplesx + nx);

                        //sum += EvalFunc(x + (nx + rx) / nsamplesx, y + (ny + ry) / nsamplesy, width, height);
                        sum += EvalFunc(x + rx, y + ry, float(width), float(height));
                    }
                }

                float val = sum / (nsamplesx * nsamplesy);
                float prev = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 0] += (val - prev) / iteration_;
                pixels_[4 * (y * width + x) + 1] = pixels_[4 * (y * width + x) + 2] = pixels_[4 * (y * width + x) + 0];
                pixels_[4 * (y * width + x) + 3] = 1.0f;
            }
            volatile int ii = 0;
        }
    }
#endif

#if 0
    int i = (int)iteration_ * nsamplesx;
    for (int j = 0; j < nsamplesx; j++) {
        //float rx = RadicalInverse<3>(i + j);
        float rx = Ray::ScrambledRadicalInverse<3>(&radical_inv_perms[2], (3000 + i + j) % 4096);
        //float ry = RadicalInverse<5>(i + j);
        float ry = Ray::ScrambledRadicalInverse<5>(&radical_inv_perms[5], (3000 + i + j) % 4096);

        int x = rx * (width - 0);
        int y = height - 1 - ry * (height - 0);

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

#if defined(USE_SW_RENDER)
    swBlitPixels(0, 0, 0, SW_FLOAT, SW_FRGBA, width, height, &pixels_[0], 1);
#endif

#if 0
    {
        // ui draw
        ui_renderer_->BeginDraw();

        float font_height = font_->height(ui_root_.get());
        font_->DrawText(ui_renderer_.get(), "regular", { 0.25f, 1 - font_height }, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), "random", { 0.25f, 1 - 2 * 0.25f - font_height }, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), "jittered", { 0.25f, 1 - 2 * 0.5f - font_height }, ui_root_.get());
        font_->DrawText(ui_renderer_.get(), "halton", { 0.25f, 1 - 2 * 0.75f - font_height }, ui_root_.get());

        ui_renderer_->EndDraw();
    }
#endif

    ctx_->ProcessTasks();
}

void GSSamplingTest::Update(uint64_t dt_us) {

}

void GSSamplingTest::HandleInput(const InputManager::Event &evt) {
    switch (evt.type) {
    case InputManager::RAW_INPUT_P1_DOWN: {

    } break;
    case InputManager::RAW_INPUT_P1_UP:

        break;
    case InputManager::RAW_INPUT_P1_MOVE: {

    } break;
    case InputManager::RAW_INPUT_KEY_UP: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_SPACE) {
            iteration_ = 0;
        }
    }
    break;
    case InputManager::RAW_INPUT_RESIZE:
        iteration_ = 0;
        break;
    default:
        break;
    }
}
