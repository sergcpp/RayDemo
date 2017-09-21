#include "GSHDRTest.h"

#include <fstream>
#include <random>

#if defined(USE_SW_RENDER)
#include <Ren/SW/SW.h>
#endif

#include <Eng/GameStateManager.h>
#include <Eng/Random.h>
#include <Ren/Context.h>
#include <Ren/MMat.h>
#include <Sys/AssetFile.h>
#include <Sys/Json.h>
#include <Sys/Log.h>
#include <Gui/Renderer.h>

#include "../Viewer.h"
#include "../load/Load.h"
#include "../ui/FontStorage.h"

namespace GSHDRTestInternal {
    const double Pi = 3.1415926535897932384626433832795;

    double LegendrePolynomial(int l, int m, double x) {
        double pmm = 1.0;
        if (m > 0) {
            double somx2 = std::sqrt((1.0 - x) * (1.0 + x));
            double fact = 1.0;
            for (int i = 1; i <= m; i++) {
                pmm *= (-fact) * somx2;
                fact += 2.0;
            }
        }
        if (l == m) return pmm;
        double pmmp1 = x * (2.0 * m + 1.0) * pmm;
        if (l == m + 1) return pmmp1;
        double pll = 0.0;
        for (int ll = m + 2; ll <= l; ll++) {
            pll = ((2.0 * ll - 1.0) * x * pmmp1 - (ll + m - 1.0) * pmm) / (ll - m);
            pmm = pmmp1;
            pmmp1 = pll;
        }
        return pll;
    }

    int factorial(int x) {
        int ret = 1;
        for (int i = 2; i <= x; i++) {
            ret *= i;
        }
        return ret;
    }

    double SH_RenormConstant(int l, int m) {
        double temp = ((2.0 * l + 1.0) * factorial(l - m)) / (4.0 * Pi * factorial(l + m));
        return std::sqrt(temp);
    }

    // l - band in range [0..N]
    // m in range [-l..l]
    // theta in range [0..Pi]
    // phi in range [0..2 * Pi]
    double SH_Evaluate(int l, int m, double theta, double phi) {
        const double sqrt2 = std::sqrt(2.0);
        if (m == 0) {
            return SH_RenormConstant(l, 0) * LegendrePolynomial(l, m, std::cos(theta));
        } else if (m > 0) {
            return sqrt2 * SH_RenormConstant(l, m) * std::cos(m * phi) * LegendrePolynomial(l, m, std::cos(theta));
        } else {
            return sqrt2 * SH_RenormConstant(l, -m) * std::sin(-m * phi) * LegendrePolynomial(l, -m, std::cos(theta));
        }
    }

    template <int bands_count, typename T>
    void SH_Project(T &&fn, int sample_count, double result[]) {
        const int coeff_count = bands_count * bands_count;
        
        std::random_device rd;
        std::mt19937 gen{ rd() };
        std::uniform_real_distribution<> dist_pos_norm{ 0.0, 1.0 };

        for (int n = 0; n < coeff_count; n++) {
            result[n] = 0.0;
        }

        for (int i = 0; i < sample_count; i++) {
            double x = dist_pos_norm(gen);
            double y = dist_pos_norm(gen);
            
            double theta = 2.0 * std::acos(std::sqrt(1.0 - x));
            double phi = 2.0 * Pi * y;

            double coeff[coeff_count];

            if (bands_count == 2) {
                Ren::Vec3d vec = { std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) };
                SH_EvaluateL1(vec, coeff);
            } else {
                for (int l = 0; l < bands_count; l++) {
                    for (int m = -l; m <= l; m++) {
                        int index = l * (l + 1) + m;
                        coeff[index] = SH_Evaluate(l, m, theta, phi);
                    }
                }
            }

            for (int n = 0; n < coeff_count; n++) {
                result[n] += fn(theta, phi) * coeff[n];
            }
        }

        const double weight = 4.0 * Pi;
        const double factor = weight / sample_count;

        for (int n = 0; n < coeff_count; n++) {
            result[n] *= factor;
        }
    }

    void SH_EvaluateL1(const Ren::Vec3d &v, double coeff[4]) {
        const double Y0 = std::sqrt(1.0 / (4.0 * Pi));
        const double Y1 = std::sqrt(3.0 / (4.0 * Pi));

        coeff[0] = Y0;
        coeff[1] = Y1 * v[1];
        coeff[2] = Y1 * v[2];
        coeff[3] = Y1 * v[0];
    }

    void SH_ApplyDiffuseConvolutionL1(double coeff[4]) {
        const double A0 = Pi / std::sqrt(4 * Pi);
        const double A1 = std::sqrt(Pi / 3);

        coeff[0] *= A0;
        coeff[1] *= A1;
        coeff[2] *= A1;
        coeff[3] *= A1;
    }

    void SH_EvaluateDiffuseL1(const Ren::Vec3d &v, double coeff[4]) {
        const double AY0 = 0.25;
        const double AY1 = 0.5;

        coeff[0] = AY0;
        coeff[1] = AY1 * v[1];
        coeff[2] = AY1 * v[2];
        coeff[3] = AY1 * v[0];
    }

    template <typename T>
    void SH_ProjectDiffuseL1(T &&fn, int sample_count, double result[4]) {
        std::random_device rd;
        std::mt19937 gen{ rd() };
        std::uniform_real_distribution<> dist_pos_norm{ 0.0, 1.0 };

        for (int n = 0; n < 4; n++) {
            result[n] = 0.0;
        }

        for (int i = 0; i < sample_count; i++) {
            double x = dist_pos_norm(gen);
            double y = dist_pos_norm(gen);

            double theta = 2.0 * std::acos(std::sqrt(1.0 - x));
            double phi = 2.0 * Pi * y;

            Ren::Vec3d vec = { std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) };

            double coeff[4];
            SH_EvaluateDiffuseL1(vec, coeff);

            for (int n = 0; n < 4; n++) {
                result[n] += fn(theta, phi) * coeff[n];
            }
        }

        const double weight = 4.0 * Pi;
        const double factor = weight / sample_count;

        for (int n = 0; n < 4; n++) {
            result[n] *= factor;
        }
    }
}

GSHDRTest::GSHDRTest(GameBase *game) : game_(game) {
    state_manager_	= game->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    ctx_			= game->GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    renderer_		= game->GetComponent<Renderer>(RENDERER_KEY);

    random_         = game->GetComponent<Random>(RANDOM_KEY);

    ui_renderer_ = game->GetComponent<Gui::Renderer>(UI_RENDERER_KEY);
    ui_root_ = game->GetComponent<Gui::BaseElement>(UI_ROOT_KEY);

    const auto fonts = game->GetComponent<FontStorage>(UI_FONTS_KEY);
    font_ = fonts->FindFont("main_font");
}

void GSHDRTest::Enter() {
#if defined(USE_SW_RENDER)
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);
#endif
    using namespace GSHDRTestInternal;

    img_ = LoadHDR("assets/textures/wells6_hd_low.hdr", img_w_, img_h_);
}

void GSHDRTest::Exit() {

}

void GSHDRTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSHDRTestInternal;

    //renderer_->set_current_cam(&cam_);
    //renderer_->ClearColorAndDepth(0, 0, 0, 1);

    uint32_t width = (uint32_t)game_->width, height = (uint32_t)game_->height;

    int sample_limit = 32;
    if (++iteration_ > sample_limit) {
        //return;
    }

    pixels_.resize(width * height * 4);

    if (iteration_ == 1) {
        /*for (uint32_t j = 0; j < height; j++) {
            float v = float(j) / height;
            for (uint32_t i = 0; i < width; i++) {
                float u = float(i) / width;

                int ii = int(u * img_w_);
                int jj = int(v * img_h_);

                if (ii > img_w_ - 1) ii = img_w_ - 1;
                if (jj > img_h_ - 1) jj = img_h_ - 1;

                float r = img_[jj * img_w_ + ii].r / 255.0f;
                float g = img_[jj * img_w_ + ii].g / 255.0f;
                float b = img_[jj * img_w_ + ii].b / 255.0f;
                float e = (float)img_[jj * img_w_ + ii].a;

                float f = std::pow(2.0f, e - 128.0f);

                r *= f;
                g *= f;
                b *= f;

                pixels_[4 * (j * width + i) + 0] = std::min(r * mul_, 1.0f);
                pixels_[4 * (j * width + i) + 1] = std::min(g * mul_, 1.0f);
                pixels_[4 * (j * width + i) + 2] = std::min(b * mul_, 1.0f);
                pixels_[4 * (j * width + i) + 3] = 1.0f;
            }
        }*/

        auto sample_env = [this](double theta, double phi) {
            const double Pi = 3.1415926535897932384626433832795;

            double u = 0.5 * phi / Pi;
            double v = theta / Pi;
            
            int ii = int(u * img_w_);
            int jj = int(v * img_h_);

            if (ii > img_w_ - 1) ii = img_w_ - 1;
            if (jj > img_h_ - 1) jj = img_h_ - 1;

            float r = img_[jj * img_w_ + ii].v[0] / 255.0f;
            float g = img_[jj * img_w_ + ii].v[1] / 255.0f;
            float b = img_[jj * img_w_ + ii].v[2] / 255.0f;
            float e = (float)img_[jj * img_w_ + ii].v[3];

            float f = std::pow(2.0f, e - 128.0f);

            r *= f;
            g *= f;
            b *= f;

            return Vec3f{ r, g, b };
        };

        auto sample_env_r = [&](double theta, double phi) -> double {
            return sample_env(theta, phi)[0];
        };

        auto sample_env_g = [&](double theta, double phi) -> double {
            return sample_env(theta, phi)[1];
        };

        auto sample_env_b = [&](double theta, double phi) -> double {
            return sample_env(theta, phi)[2];
        };

        auto sample_env2 = [this](double theta, double phi) -> double {
            const double Pi = 3.1415926535897932384626433832795;

            return std::max(0.0, 5.0 * std::cos(theta) - 4.0) +
                   std::max(0.0, -4.0 * std::sin(theta - Pi) * std::cos(phi - 2.5) - 3.0);
        };

        const int bands_count = 2,
                  coeff_count = bands_count * bands_count;

        const int sample_count = 1024 * 128;

        double sh_result[3][coeff_count];

        //SH_Project<bands_count>(sample_env, sample_count, sh_result);

#if 0
        SH_Project<bands_count>(sample_env_r, sample_count, sh_result[0]);
        SH_Project<bands_count>(sample_env_g, sample_count, sh_result[1]);
        SH_Project<bands_count>(sample_env_b, sample_count, sh_result[2]);

        SH_ApplyDiffuseConvolutionL1(sh_result[0]);
        SH_ApplyDiffuseConvolutionL1(sh_result[1]);
        SH_ApplyDiffuseConvolutionL1(sh_result[2]);
#else
        static_assert(bands_count == 2, "!");

        SH_ProjectDiffuseL1(sample_env_r, sample_count, sh_result[0]);
        SH_ProjectDiffuseL1(sample_env_g, sample_count, sh_result[1]);
        SH_ProjectDiffuseL1(sample_env_b, sample_count, sh_result[2]);
#endif

        const double Pi = 3.1415926535897932384626433832795;

        for (uint32_t j = 0; j < height; j++) {
            double theta = Pi * double(j) / height;
            for (uint32_t i = 0; i < width; i++) {
                double phi = 2.0 * Pi * double(i) / width;

#if 0
                float res = (float)sample_env(theta, phi);
#else
                auto dres = Vec3d{ 0.0 };

                if (bands_count == 2) {
                    Ren::Vec3d vec = { std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta) };

                    double coeff[4];
                    SH_EvaluateL1(vec, coeff);

                    for (int n = 0; n < 4; n++) {
                        dres[0] += sh_result[0][n] * coeff[n];
                        dres[1] += sh_result[1][n] * coeff[n];
                        dres[2] += sh_result[2][n] * coeff[n];
                    }
                } else {
                    for (int l = 0; l < bands_count; l++) {
                        for (int m = -l; m <= l; m++) {
                            int index = l * (l + 1) + m;
                            dres[0] += sh_result[0][index] * SH_Evaluate(l, m, theta, phi);
                            dres[1] += sh_result[1][index] * SH_Evaluate(l, m, theta, phi);
                            dres[2] += sh_result[2][index] * SH_Evaluate(l, m, theta, phi);
                        }
                    }
                }

                auto res = (Vec3f)dres;
#endif

                pixels_[4 * (j * width + i) + 0] = std::min(std::max(res[0] * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 1] = std::min(std::max(res[1] * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 2] = std::min(std::max(res[2] * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 3] = 1.0f;
            }
        }
    }

    

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

void GSHDRTest::Update(uint64_t dt_us) {

}

void GSHDRTest::HandleInput(const InputManager::Event &evt) {
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
        mul_ += evt.move.dy * 0.1f;
        LOGI("%f", mul_);
        iteration_ = 0;
    } break;
    case InputManager::RAW_INPUT_RESIZE:
        iteration_ = 0;
        break;
    default:
        break;
    }
}
