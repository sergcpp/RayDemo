#include "GSHDRTest.h"

#include <fstream>
#include <random>

#if defined(USE_SW_RENDER)
#include <Ren/SW/SW.h>
#endif

#include <Eng/GameStateManager.h>
#include <Eng/Random.h>
#include <Gui/Renderer.h>
#include <Ren/Context.h>
#include <Ren/MMat.h>
#include <Sys/AssetFile.h>
#include <Sys/Json.h>

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
    if (l == m)
        return pmm;
    double pmmp1 = x * (2.0 * m + 1.0) * pmm;
    if (l == m + 1)
        return pmmp1;
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

template <int bands_count, typename T> void SH_Project(T &&fn, int sample_count, double result[]) {
    const int coeff_count = bands_count * bands_count;

    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_real_distribution<> dist_pos_norm{0.0, 1.0};

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
            Ren::Vec3d vec = {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
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

template <typename T> void SH_ProjectDiffuseL1(T &&fn, int sample_count, double result[4]) {
    std::random_device rd;
    std::mt19937 gen{rd()};
    std::uniform_real_distribution<> dist_pos_norm{0.0, 1.0};

    for (int n = 0; n < 4; n++) {
        result[n] = 0.0;
    }

    for (int i = 0; i < sample_count; i++) {
        double x = dist_pos_norm(gen);
        double y = dist_pos_norm(gen);

        double theta = 2.0 * std::acos(std::sqrt(1.0 - x));
        double phi = 2.0 * Pi * y;

        Ren::Vec3d vec = {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};

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

float clamp(float x, float _min, float _max) { return std::min(std::max(x, _min), _max); }

Ren::Vec3f canonical_to_dir(const Ren::Vec2f p) {
    const float cos_theta = 2 * p[0] - 1;
    const float phi = 2 * Ren::Pi<float>() * p[1];

    const float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);

    const float sin_phi = std::sin(phi);
    const float cos_phi = std::cos(phi);

    return {sin_theta * cos_phi, cos_theta, -sin_theta * sin_phi};
}

Ren::Vec2f dir_to_canonical(const Ren::Vec3f &d) {
    const float cosTheta = clamp(d[1], -1.0f, 1.0f);
    float phi = -std::atan2(d[2], d[0]);
    while (phi < 0) {
        phi += 2.0f * Ren::Pi<float>();
    }

    return Ren::Vec2f{(cosTheta + 1.0f) / 2.0f, phi / (2.0f * Ren::Pi<float>())};
}

} // namespace GSHDRTestInternal

GSHDRTest::GSHDRTest(GameBase *game) : game_(game) {
    state_manager_ = game->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    ctx_ = game->GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    renderer_ = game->GetComponent<Renderer>(RENDERER_KEY);

    random_ = game->GetComponent<Random>(RANDOM_KEY);

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

    img_ = LoadHDR("src/Ray/tests/test_data/textures/studio_small_03_2k.hdr", img_w_, img_h_);
    //img_ = LoadHDR("assets/textures/env/dancing_hall_4k.hdr", img_w_, img_h_);

    auto lum = [](const Ren::Vec3f &c) -> float { return (c[0] + c[1] + c[2]); };

    quadtree_res_ = 1;
    while (2 * quadtree_res_ < img_h_) {
        quadtree_res_ *= 2;
    }
    quadtree_count_ = 0;

    int cur_res = quadtree_res_;

    float total_lum = 0.0f;

    { // initialize the first quadtree level
        quadtree_[quadtree_count_].resize(cur_res * cur_res / 4, Ren::Vec4f{0.0f});

        for (int y = 0; y < img_h_; ++y) {
            const float theta = Ren::Pi<float>() * float(y) / img_h_;
            for (int x = 0; x < img_w_; ++x) {
                const float phi = 2.0f * Ren::Pi<float>() * float(x) / img_w_;

                const float cur_lum = lum(sample_env(x, y));

                Ren::Vec3f dir;
                dir[0] = std::sin(theta) * std::cos(phi);
                dir[1] = std::cos(theta);
                dir[2] = std::sin(theta) * std::sin(phi);

                Ren::Vec2f q = dir_to_canonical(dir);

                int qx = std::min(std::max(int(cur_res * q[0]), 0), cur_res - 1);
                int qy = std::min(std::max(int(cur_res * q[1]), 0), cur_res - 1);

                int index = 0;
                index |= (qx & 1) << 0;
                index |= (qy & 1) << 1;

                qx /= 2;
                qy /= 2;

                float &q_lum = quadtree_[quadtree_count_][qy * cur_res / 2 + qx][index];
                q_lum = std::max(q_lum, cur_lum);
            }
        }

        for (const Ren::Vec4f &v : quadtree_[quadtree_count_]) {
            total_lum += (v[0] + v[1] + v[2] + v[3]);
        }

        ++quadtree_count_;
        cur_res /= 2;
    }

    while (cur_res > 1) {
        quadtree_[quadtree_count_].resize(cur_res * cur_res / 4, Ren::Vec4f{0.0f});

        for (int y = 0; y < cur_res; ++y) {
            for (int x = 0; x < cur_res; ++x) {
                const float res_lum = sample_lum(2 * x + 0, 2 * y + 0, quadtree_count_ - 1) +
                                      sample_lum(2 * x + 1, 2 * y + 0, quadtree_count_ - 1) +
                                      sample_lum(2 * x + 0, 2 * y + 1, quadtree_count_ - 1) +
                                      sample_lum(2 * x + 1, 2 * y + 1, quadtree_count_ - 1);

                int index = 0;
                index |= (x & 1) << 0;
                index |= (y & 1) << 1;

                const int qx = (x / 2);
                const int qy = (y / 2);

                quadtree_[quadtree_count_][qy * cur_res / 2 + qx][index] = res_lum;
            }
        }

        ++quadtree_count_;
        cur_res /= 2;
    }

    //
    // Determine how many levels was actually required
    //

    const float LumFractThreshold = 0.01f;

    cur_res = 2;
    int the_last_required_lod;
    for (int lod = quadtree_count_ - 1; lod >= 0; --lod) {
        the_last_required_lod = lod;

        bool subdivision_required = false;
        for (int y = 0; y < cur_res && !subdivision_required; y += 2) {
            for (int x = 0; x < cur_res && !subdivision_required; x += 2) {
                const float l00 = sample_lum(x + 0, y + 0, lod);
                const float l01 = sample_lum(x + 1, y + 0, lod);
                const float l10 = sample_lum(x + 0, y + 1, lod);
                const float l11 = sample_lum(x + 1, y + 1, lod);

                subdivision_required |= (l00 > LumFractThreshold * total_lum || l01 > LumFractThreshold * total_lum ||
                                         l10 > LumFractThreshold * total_lum || l11 > LumFractThreshold * total_lum);
            }
        }

        if (!subdivision_required) {
            break;
        }

        cur_res *= 2;
    }

    //
    // Drop not needed levels
    //

    while (the_last_required_lod != 0) {
        for (int i = 1; i < quadtree_count_; ++i) {
            quadtree_[i - 1] = std::move(quadtree_[i]);
        }
        quadtree_res_ /= 2;
        --quadtree_count_;
        --the_last_required_lod;
    }
}

void GSHDRTest::Exit() {}

void GSHDRTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSHDRTestInternal;

    // renderer_->set_current_cam(&cam_);
    // renderer_->ClearColorAndDepth(0, 0, 0, 1);

    int width = game_->width, height = game_->height;

    int sample_limit = 32;
    if (++iteration_ > sample_limit) {
        // return;
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

        //auto sample_env_r = [&](float theta, float phi) -> double { return sample_env(theta, phi)[0]; };

        //auto sample_env_g = [&](float theta, float phi) -> double { return sample_env(theta, phi)[1]; };

        //auto sample_env_b = [&](float theta, float phi) -> double { return sample_env(theta, phi)[2]; };

        //auto sample_env2 = [](float theta, float phi) -> double {
        //    return std::max(0.0, 5.0 * std::cos(theta) - 4.0) +
        //           std::max(0.0, -4.0 * std::sin(theta - Ren::Pi<float>()) * std::cos(phi - 2.5) - 3.0);
        //};

        //const int bands_count = 2, coeff_count = bands_count * bands_count;

        //const int sample_count = 1024 * 128;

        //double sh_result[3][coeff_count];

        // SH_Project<bands_count>(sample_env, sample_count, sh_result);

#if 0
        SH_Project<bands_count>(sample_env_r, sample_count, sh_result[0]);
        SH_Project<bands_count>(sample_env_g, sample_count, sh_result[1]);
        SH_Project<bands_count>(sample_env_b, sample_count, sh_result[2]);

        SH_ApplyDiffuseConvolutionL1(sh_result[0]);
        SH_ApplyDiffuseConvolutionL1(sh_result[1]);
        SH_ApplyDiffuseConvolutionL1(sh_result[2]);
#elif 0
        static_assert(bands_count == 2, "!");

        SH_ProjectDiffuseL1(sample_env_r, sample_count, sh_result[0]);
        SH_ProjectDiffuseL1(sample_env_g, sample_count, sh_result[1]);
        SH_ProjectDiffuseL1(sample_env_b, sample_count, sh_result[2]);
#endif

        for (int j = 0; j < height; j++) {
            const float v = float(j) / height;
            const float theta = Ren::Pi<float>() * v;
            for (int i = 0; i < width; i++) {
                const float u = float(i) / width;
                const float phi = 2.0f * Ren::Pi<float>() * u;

#if 0
                auto res = sample_env(theta, phi);

                pixels_[4 * (j * width + i) + 0] = std::min(std::max(res[0] * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 1] = std::min(std::max(res[1] * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 2] = std::min(std::max(res[2] * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 3] = 1.0f;
#elif 1
                Ren::Vec3f dir;
                dir[0] = std::sin(theta) * std::cos(phi);
                dir[1] = std::cos(theta);
                dir[2] = std::sin(theta) * std::sin(phi);

                Ren::Vec2f q = dir_to_canonical(dir);

                const int lod = 0;
                const float res = sample_lum(q[0], q[1], lod) / std::pow(4.0f, float(lod + 1));

                pixels_[4 * (j * width + i) + 0] = std::min(std::max(res * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 1] = std::min(std::max(res * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 2] = std::min(std::max(res * mul_, 0.0f), 1.0f);
                pixels_[4 * (j * width + i) + 3] = 1.0f;
#else
                auto dres = Vec3d{0.0};

                if (bands_count == 2) {
                    Ren::Vec3d vec = {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi),
                                      std::cos(theta)};

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
            }
        }
    }

    std::function<Vec2f(int, Vec2f)> sample_quadtree;
    sample_quadtree = [this](int lod, Vec2f origin) -> Vec2f {
        int res = (quadtree_res_ >> lod);
        float step = 1.0f / float(res);

        float sample = random_->GetNormalizedFloat();

        while (lod >= 0) {
            const int qx = int(origin[0] * res) / 2;
            const int qy = int(origin[1] * res) / 2;

            const float top_left = quadtree_[lod][qy * res / 2 + qx][0];
            const float top_right = quadtree_[lod][qy * res / 2 + qx][1];
            float partial = top_left + quadtree_[lod][qy * res / 2 + qx][2];
            const float total = partial + top_right + quadtree_[lod][qy * res / 2 + qx][3];

            if (total <= 0.0f) {
                break;
            }

            float boundary = partial / total;

            if (sample < boundary) {
                assert(partial > 0.0f);
                sample /= boundary;
                boundary = top_left / partial;
            } else {
                partial = total - partial;
                assert(partial > 0.0f);
                origin[0] += step;
                sample = (sample - boundary) / (1.0f - boundary);
                boundary = top_right / partial;
            }

            if (sample < boundary) {
                sample /= boundary;
            } else {
                origin[1] += step;
                sample = (sample - boundary) / (1.0f - boundary);
            }

            --lod;
            res *= 2;
            step *= 0.5f;
        }

        return origin + 2.0f * step * Vec2f{random_->GetNormalizedFloat(), random_->GetNormalizedFloat()};
    };

    for (int i = 0; i < 1; ++i) {
        //auto r = Vec2f{random_->GetNormalizedFloat(), random_->GetNormalizedFloat()};

#if 0
        const float theta = std::acos(1.0f - 2.0f * r[1]);
        const float phi = 2 * Ren::Pi<float>() * r[0];
        
        Ren::Vec3f dir;
        dir[0] = std::sin(theta) * std::cos(phi);
        dir[1] = std::cos(theta);
        dir[2] = std::sin(theta) * std::sin(phi);
#elif 0
        Ren::Vec3f dir = canonical_to_dir(r);
#else
        auto origin = Vec2f{0.0f, 0.0f};
        Ren::Vec3f dir = canonical_to_dir(sample_quadtree(quadtree_count_ - 1, origin));
#endif

        const float v = std::acos(clamp(dir[1], -1.0f, 1.0f)) / Ren::Pi<float>();
        const float rr = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
        float u = 0.5f * std::acos(rr > 0.00001f ? clamp(dir[0] / rr, -1.0f, 1.0f) : 0.0f) / Ren::Pi<float>();
        if (dir[2] < 0.0f) {
            u = 1.0f - u;
        }

        const int px = std::min(std::max(int(u * width), 0), width - 1);
        const int py = std::min(std::max(int(v * height), 0), height - 1);

        pixels_[4 * (py * width + px) + 0] = 1.0f;
        pixels_[4 * (py * width + px) + 1] = 0.0f;
        pixels_[4 * (py * width + px) + 2] = 0.0f;
        pixels_[4 * (py * width + px) + 3] = 1.0f;
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

void GSHDRTest::Update(uint64_t dt_us) {}

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

Ren::Vec2i GSHDRTest::latlong_to_xy(const float theta, const float phi, const int lod) const {
    const float u = 0.5f * phi / Ren::Pi<float>();
    const float v = theta / Ren::Pi<float>();

    const int w = std::max(img_w_ >> lod, 1);
    const int h = std::max(img_h_ >> lod, 1);

    return Ren::Vec2i{std::min(std::max(int(u * w), 0), w - 1), std::min(std::max(int(v * h), 0), h - 1)};
}

Ren::Vec3f GSHDRTest::sample_env(const float theta, const float phi) const {
    const Ren::Vec2i xy = latlong_to_xy(theta, phi, 0);

    return sample_env(xy[0], xy[1]);
}

Ren::Vec3f GSHDRTest::sample_env(const int x, const int y) const {
    float r = img_[y * img_w_ + x].v[0] / 255.0f;
    float g = img_[y * img_w_ + x].v[1] / 255.0f;
    float b = img_[y * img_w_ + x].v[2] / 255.0f;
    float e = float(img_[y * img_w_ + x].v[3]);

    float f = std::pow(2.0f, e - 128.0f);

    r *= f;
    g *= f;
    b *= f;

    return Ren::Vec3f{r, g, b};
}

float GSHDRTest::sample_lum(const float u, const float v, const int lod) const {
    const int res = std::max(quadtree_res_ >> lod, 1);
    return sample_lum(int(u * res), int(v * res), lod);
}

float GSHDRTest::sample_lum(int x, int y, const int lod) const {
    const int res = std::max(quadtree_res_ >> lod, 1);

    int index = 0;
    index |= (x & 1) << 0;
    index |= (y & 1) << 1;

    x = std::min(std::max(x, 0), res - 1);
    y = std::min(std::max(y, 0), res - 1);

    x /= 2;
    y /= 2;

    return quadtree_[lod][y * res / 2 + x][index];
}