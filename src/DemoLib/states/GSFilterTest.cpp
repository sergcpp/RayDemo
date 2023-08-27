#include "GSFilterTest.h"

#include <fstream>
#include <random>

#include <SW/SW.h>
#include <Sys/AssetFile.h>
#include <Sys/Json.h>

#include "../Viewer.h"
#include "../eng/GameStateManager.h"
#include "../eng/Random.h"
#include "../gui/FontStorage.h"
#include "../gui/Renderer.h"
#include "../load/Load.h"
#include "../ren/Context.h"
#include "../ren/MMat.h"

#include <Ray/internal/CDFUtils.h>

namespace GSFilterTestInternal {
const int FilterTableSize = 1024;
const float FilterWidth = 1.5f;
} // namespace GSFilterTestInternal

GSFilterTest::GSFilterTest(GameBase *game) : game_(game) {
    state_manager_ = game->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    ctx_ = game->GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    renderer_ = game->GetComponent<Renderer>(RENDERER_KEY);

    random_ = game->GetComponent<Random>(RANDOM_KEY);

    ui_renderer_ = game->GetComponent<Gui::Renderer>(UI_RENDERER_KEY);
    ui_root_ = game->GetComponent<Gui::BaseElement>(UI_ROOT_KEY);

    const auto fonts = game->GetComponent<FontStorage>(UI_FONTS_KEY);
    font_ = fonts->FindFont("main_font");
}

void GSFilterTest::Enter() {
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);

    using namespace GSFilterTestInternal;

    auto filter_box = [](float /*v*/, float /*width*/) { return 1.0f; };
    auto filter_gaussian = [](float v, float width) {
        v *= 6.0f / width;
        return expf(-2.0f * v * v);
    };
    auto filter_blackman_harris = [](float v, float width) {
        v = 2.0f * Ren::Pi<float>() * (v / width + 0.5f);
        return 0.35875f - 0.48829f * cosf(v) + 0.14128f * cosf(2.0f * v) - 0.01168f * cosf(3.0f * v);
    };

    filter_table_ = Ray::CDFInverted(FilterTableSize, 0.0f, FilterWidth * 0.5f,
                                     std::bind(filter_blackman_harris, std::placeholders::_1, FilterWidth),
                                     true /* make_symmetric */);
}

void GSFilterTest::Exit() {}

void GSFilterTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSFilterTestInternal;

    int width = game_->width, height = game_->height;

    int sample_limit = 32;
    if (++iteration_ > sample_limit) {
        // return;
    }

    if (iteration_ == 1) {
        pixels_.clear();
        pixels_.resize(width * height * 4, 0.0f);

        int y = (height / 3);
        for (int x = 0; x < width; ++x) {
            pixels_[4 * (y * width + x) + 0] = 0.25f;
            pixels_[4 * (y * width + x) + 1] = 0.25f;
            pixels_[4 * (y * width + x) + 2] = 0.25f;
            pixels_[4 * (y * width + x) + 3] = 1.0f;
        }
        y = (2 * height / 3);
        for (int x = 0; x < width; ++x) {
            pixels_[4 * (y * width + x) + 0] = 0.25f;
            pixels_[4 * (y * width + x) + 1] = 0.25f;
            pixels_[4 * (y * width + x) + 2] = 0.25f;
            pixels_[4 * (y * width + x) + 3] = 1.0f;
        }

        int x = (width / 3);
        for (int y = 0; y < height; ++y) {
            pixels_[4 * (y * width + x) + 0] = 0.25f;
            pixels_[4 * (y * width + x) + 1] = 0.25f;
            pixels_[4 * (y * width + x) + 2] = 0.25f;
            pixels_[4 * (y * width + x) + 3] = 1.0f;
        }
        x = (2 * width / 3);
        for (int y = 0; y < height; ++y) {
            pixels_[4 * (y * width + x) + 0] = 0.25f;
            pixels_[4 * (y * width + x) + 1] = 0.25f;
            pixels_[4 * (y * width + x) + 2] = 0.25f;
            pixels_[4 * (y * width + x) + 3] = 1.0f;
        }
    }

    auto lookup_table_read = [this](float x, int size) {
        x = std::min(std::max(x, 0.0f), 1.0f) * (size - 1);

        int index = std::min(int(x), size - 1);
        int nindex = std::min(index + 1, size - 1);
        float t = x - float(index);

        float data0 = filter_table_[index];
        if (t == 0.0f) {
            return data0;
        }

        float data1 = filter_table_[nindex];
        return (1.0f - t) * data0 + t * data1;
    };

    { //
        float rx = random_->GetNormalizedFloat(), ry = random_->GetNormalizedFloat();

        rx = lookup_table_read(rx, FilterTableSize);
        ry = lookup_table_read(ry, FilterTableSize);

        rx = FilterWidth * (rx - 0.5f);
        ry = FilterWidth * (ry - 0.5f);

        const int x = std::min(int((0.5f + rx / 3.0f) * float(width)), width - 1),
                  y = std::min(int((0.5f + ry / 3.0f) * float(height)), height - 1);

        pixels_[4 * (y * width + x) + 0] = 1.0f;
        pixels_[4 * (y * width + x) + 1] = 1.0f;
        pixels_[4 * (y * width + x) + 2] = 1.0f;
        pixels_[4 * (y * width + x) + 3] = 1.0f;
    }

    swBlitPixels(0, 0, 0, SW_FLOAT, SW_FRGBA, width, height, &pixels_[0], 1);
}

void GSFilterTest::Update(uint64_t dt_us) {}

void GSFilterTest::HandleInput(const InputManager::Event &evt) {
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
    } break;
    case InputManager::RAW_INPUT_MOUSE_WHEEL: {
        mul_ += evt.move.dy * 0.025f;
        iteration_ = 0;
    } break;
    case InputManager::RAW_INPUT_RESIZE:
        iteration_ = 0;
        break;
    default:
        break;
    }
}
