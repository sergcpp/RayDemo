#include "GSVNDFTest.h"

#include <fstream>

#if defined(USE_SW_RENDER)
#include <Ren/SW/SW.h>
#endif

#include <Gui/Renderer.h>
#include <Ren/Context.h>
#include <Ren/Program.h>
#include <Sys/Json.h>

#include <Ray/internal/Core.h>
#include <Ray/internal/Halton.h>

#include "../Viewer.h"
#include "../eng/GameStateManager.h"
#include "../eng/Random.h"
#include "../ui/FontStorage.h"

namespace GSVNDFTestInternal {
enum { A_POS, A_COL };
enum { V_COL = 0 }; // offset in floats
enum { U_MVP };

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

Ren::Vec3f reflect(const Ren::Vec3f &I, const Ren::Vec3f &N) { return I - 2 * Ren::Dot(N, I) * N; }

Ren::Vec3f SampleGGX_NDF(const float rgh, const float r1, const float r2) {
    const float a = std::max(0.001f, rgh);

    const float phi = r1 * (2.0f * Ren::Pi<float>());

    const float cosTheta = std::sqrt((1.0f - r2) / (1.0f + (a * a - 1.0f) * r2));
    const float sinTheta = std::min(std::max(std::sqrt(1.0f - (cosTheta * cosTheta)), 0.0f), 1.0f);
    const float sinPhi = std::sin(phi);
    const float cosPhi = std::cos(phi);

    return Ren::Vec3f(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}

Ren::Vec3f SampleGGX_VNDF(const Ren::Vec3f &Ve, float alpha_x, float alpha_y, float U1, float U2) {
    // Section 3.2: transforming the view direction to the hemisphere configuration
    const Ren::Vec3f Vh = Ren::Normalize(Ren::Vec3f(alpha_x * Ve[0], alpha_y * Ve[1], Ve[2]));
    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    const float lensq = Vh[0] * Vh[0] + Vh[1] * Vh[1];
    const Ren::Vec3f T1 =
        lensq > 0.0f ? Ren::Vec3f(-Vh[1], Vh[0], 0.0f) / std::sqrt(lensq) : Ren::Vec3f(1.0f, 0.0f, 0.0f);
    const Ren::Vec3f T2 = Ren::Cross(Vh, T1);
    // Section 4.2: parameterization of the projected area
    const float r = std::sqrt(U1);
    const float phi = 2.0f * Ren::Pi<float>() * U2;
    const float t1 = r * std::cos(phi);
    float t2 = r * std::sin(phi);
    const float s = 0.5f * (1.0f + Vh[2]);
    t2 = (1.0f - s) * std::sqrt(1.0f - t1 * t1) + s * t2;
    // Section 4.3: reprojection onto hemisphere
    const Ren::Vec3f Nh = t1 * T1 + t2 * T2 + std::sqrt(std::max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;
    // Section 3.4: transforming the normal back to the ellipsoid configuration
    const Ren::Vec3f Ne = Ren::Normalize(Ren::Vec3f(alpha_x * Nh[0], alpha_y * Nh[1], std::max(0.0f, Nh[2])));
    return Ne;
}

Ren::Vec3f sample_GTR1(float rgh, float r1, float r2) {
    float a = std::max(0.001f, rgh);
    float a2 = a * a;

    float phi = r1 * (2.0f * Ren::Pi<float>());

    float cosTheta = std::sqrt((1.0f - std::pow(a2, 1.0f - r2)) / (1.0f - a2));
    float sinTheta = std::sqrt(1.0f - (cosTheta * cosTheta));
    float sinPhi = std::sin(phi);
    float cosPhi = std::cos(phi);

    return Ren::Vec3f(sinTheta * cosPhi, sinTheta * sinPhi, cosTheta);
}

float G1(const Ren::Vec3f &Ve, float alpha_x, float alpha_y) {
    if (Ve[2] == 0.0f) {
        return 0.0f;
    }
    alpha_x *= alpha_x;
    alpha_y *= alpha_y;
    const float delta =
        (-1.0f + std::sqrt(1.0f + (alpha_x * Ve[0] * Ve[0] + alpha_y * Ve[1] * Ve[1]) / (Ve[2] * Ve[2]))) / 2.0f;
    return 1.0f / (1.0f + delta);
}

} // namespace GSVNDFTestInternal

extern "C" {
VSHADER vtx_color_vs(VS_IN, VS_OUT) {
    using namespace GSVNDFTestInternal;

    memcpy(V_FVARYING(V_COL), V_FATTR(A_COL), 3 * sizeof(float));

    const float(&in_mat)[4][4] = *(float(*)[4][4])F_UNIFORM(U_MVP);
    const float *in_vec = V_FATTR(A_POS);

    V_POS_OUT[0] = in_mat[0][0] * in_vec[0] + in_mat[1][0] * in_vec[1] + in_mat[2][0] * in_vec[2] + in_mat[3][0];
    V_POS_OUT[1] = in_mat[0][1] * in_vec[0] + in_mat[1][1] * in_vec[1] + in_mat[2][1] * in_vec[2] + in_mat[3][1];
    V_POS_OUT[2] = in_mat[0][2] * in_vec[0] + in_mat[1][2] * in_vec[1] + in_mat[2][2] * in_vec[2] + in_mat[3][2];
    V_POS_OUT[3] = in_mat[0][3] * in_vec[0] + in_mat[1][3] * in_vec[1] + in_mat[2][3] * in_vec[2] + in_mat[3][3];
}

FSHADER vtx_color_fs(FS_IN, FS_OUT) {
    using namespace GSVNDFTestInternal;

    const float *in_color = F_FVARYING_IN(V_COL);

    F_COL_OUT[0] = in_color[0];
    F_COL_OUT[1] = in_color[1];
    F_COL_OUT[2] = in_color[2];
    F_COL_OUT[3] = 1.0f;
}
}

GSVNDFTest::GSVNDFTest(GameBase *game) : game_(game) {
    using namespace GSVNDFTestInternal;

    state_manager_ = game->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    ctx_ = game->GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    renderer_ = game->GetComponent<Renderer>(RENDERER_KEY);

    random_ = game->GetComponent<Random>(RANDOM_KEY);

    ui_renderer_ = game->GetComponent<Gui::Renderer>(UI_RENDERER_KEY);
    ui_root_ = game->GetComponent<Gui::BaseElement>(UI_ROOT_KEY);

    const auto fonts = game->GetComponent<FontStorage>(UI_FONTS_KEY);
    font_ = fonts->FindFont("main_font");

    {
        const Ren::Attribute attrs[] = {{"pos", A_POS, SW_VEC3, 1}, {"col", A_COL, SW_VEC3, 1}, {}};
        const Ren::Attribute unifs[] = {{"mvp", U_MVP, SW_MAT4, 1}, {}};
        Ren::eProgLoadStatus status;
        vtx_color_prog_ =
            ctx_->LoadProgramSW("vtx_color", (void *)vtx_color_vs, (void *)vtx_color_fs, 3, attrs, unifs, &status);
    }
}

void GSVNDFTest::Enter() {
#if defined(USE_SW_RENDER)
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);
#endif
    using namespace GSVNDFTestInternal;

    radical_inv_perms = Ray::ComputeRadicalInversePermutations(Ray::g_primes, Ray::PrimesCount, ::rand);
}

void GSVNDFTest::Exit() {}

void GSVNDFTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSVNDFTestInternal;

    swClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    swClearDepth(1.0f);

    swBindBuffer(SW_ARRAY_BUFFER, 0);
    swBindBuffer(SW_INDEX_BUFFER, 0);

    swUseProgram(vtx_color_prog_->prog_id());

    const auto cam_center = Ren::Vec3f{2.5f * std::cos(cam_angle_), 0.0f, 2.5f * std::sin(cam_angle_)};
    const auto cam_target = Ren::Vec3f{0.0f, 0.0f, 0.0f};
    const auto cam_up = Ren::Vec3f{0.0f, 1.0f, 0.0f};

    const float k = float(ctx_->w()) / ctx_->h();
    cam_.Perspective(45.0f, k, 0.1f, 100.0f);
    cam_.SetupView(cam_center, cam_target, cam_up);

    const Ren::Mat4f clip_from_world = cam_.projection_matrix() * cam_.view_matrix();

    swSetUniform(U_MVP, SW_MAT4, Ren::ValuePtr(clip_from_world));

    { // draw plane
        const float lines_pos[][3] = {{-1.0f, 0.0f, -1.0f},
                                      {+1.0f, 0.0f, -1.0f},
                                      {+1.0f, 0.0f, +1.0f},
                                      {-1.0f, 0.0f, +1.0f},
                                      {-1.0f, 0.0f, -1.0f}};
        const float lines_col[][3] = {{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}};

        swVertexAttribPointer(A_POS, 3 * sizeof(float), 0, (void *)&lines_pos[0][0]);
        swVertexAttribPointer(A_COL, 3 * sizeof(float), 0, (void *)&lines_col[0][0]);
        swDrawArrays(SW_LINE_STRIP, 0, 5);
    }

    static float kk = 0.05f;
    // kk += 0.002f;
    float _unused;
    kk = std::modf(kk, &_unused);

    const auto I = Ren::Normalize(Ren::Vec3f{1.0f, 2.0f * kk, 0.0f});
    const float Roughness = 0.25f;
    const float r2 = Roughness * Roughness;

    { // draw incident ray
        const float lines_pos[][3] = {{I[0], I[1], I[2]}, {0.0f, 0.0f, 0.0f}};
        const float lines_col[][3] = {{1, 1, 1}, {1, 1, 1}};

        swVertexAttribPointer(A_POS, 3 * sizeof(float), 0, (void *)&lines_pos[0][0]);
        swVertexAttribPointer(A_COL, 3 * sizeof(float), 0, (void *)&lines_col[0][0]);
        swDrawArrays(SW_LINE_STRIP, 0, 2);
    }

    Ren::Vec3f I_ts = I;
    std::swap(I_ts[1], I_ts[2]);

    { // draw reflected rays
        for (int i = 0; i < 256; ++i) {
            const float rx = Ray::ScrambledRadicalInverse(3, &radical_inv_perms[2], i);
            const float ry = Ray::ScrambledRadicalInverse(5, &radical_inv_perms[5], i);

            float lines_col[2][3] = {0.0f};

            Ren::Vec3f H_ts;
            if (mode_ == 0) {
                H_ts = SampleGGX_NDF(r2, rx, ry);

                lines_col[0][0] = 1.0f;
                lines_col[1][0] = 1.0f;
            } else if (mode_ == 1) {
                H_ts = SampleGGX_VNDF(I_ts, r2, r2, rx, ry);

                lines_col[0][1] = 1.0f;
                lines_col[1][1] = 1.0f;
            } else if (mode_ == 2) {
                H_ts = sample_GTR1(r2, rx, ry);

                lines_col[0][2] = 1.0f;
                lines_col[1][2] = 1.0f;
            }
            Ren::Vec3f R = reflect(-I_ts, H_ts);
            float G = 1.0f;
            // float G = use_vndf_ ? 1.0f : G1(I_ts, r2, r2);
            // G *= G1(R, r2, r2);

            std::swap(R[1], R[2]);
            const float lines_pos[][3] = {{0.0f, 0.0f, 0.0f}, {G * R[0], G * R[1], G * R[2]}};

            swVertexAttribPointer(A_POS, 3 * sizeof(float), 0, (void *)&lines_pos[0][0]);
            swVertexAttribPointer(A_COL, 3 * sizeof(float), 0, (void *)&lines_col[0][0]);
            swDrawArrays(SW_LINE_STRIP, 0, 2);
        }
    }

    ctx_->ProcessTasks();
}

void GSVNDFTest::Update(uint64_t dt_us) {}

void GSVNDFTest::HandleInput(const InputManager::Event &evt) {
    switch (evt.type) {
    case InputManager::RAW_INPUT_P1_DOWN: {
        input_grabbed_ = true;
    } break;
    case InputManager::RAW_INPUT_P1_UP: {
        input_grabbed_ = false;
    } break;
    case InputManager::RAW_INPUT_P1_MOVE: {
        if (input_grabbed_) {
            cam_angle_ += 0.01f * evt.move.dx;
        }
    } break;
    case InputManager::RAW_INPUT_KEY_UP: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_SPACE) {
            mode_ = (mode_ + 1) % 3;
        }
    } break;
    case InputManager::RAW_INPUT_RESIZE:
        // iteration_ = 0;
        break;
    default:
        break;
    }
}
