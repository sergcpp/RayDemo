#include "GSFibTest.h"

#include <fstream>

#include <Ray/Log.h>
#include <Ray/internal/Core.h>
#include <SW/SW.h>
#include <Sys/Json.h>

#include "../Viewer.h"
#include "../eng/GameStateManager.h"
#include "../eng/Halton.h"
#include "../eng/Random.h"
#include "../gui/FontStorage.h"
#include "../gui/Renderer.h"
#include "../ren/Context.h"
#include "../ren/MQuat.h"
#include "../ren/Program.h"

namespace GSSamplingTestInternal {
extern const int g_primes[];
extern const int g_primes_count;
} // namespace GSSamplingTestInternal

namespace GSFibTestInternal {
enum { A_POS, A_COL };
enum { V_COL = 0 }; // offset in floats
enum { U_MVP };

Ren::Vec3f spherical_fibonacci(const float sample_index, const float sample_count) {
    const float b = (sqrtf(5.0f) * 0.5f + 0.5f) - 1.0f;
    float _unused;
    const float phi = 2.0f * Ren::Pi<float>() * std::modf(sample_index * b, &_unused);
    const float cos_theta = 1.0f - (2.0f * sample_index + 1.0f) / sample_count;
    const float sin_theta = sqrtf(std::min(std::max(1.0f - (cos_theta * cos_theta), 0.0f), 1.0f));
    return Ren::Vec3f(cosf(phi) * sin_theta, sinf(phi) * sin_theta, cos_theta);
}

Ren::Vec3f quat_rotate(const Ren::Vec3f v, const Ren::Quatf q) {
    const auto b = Ren::Vec3f{q.x, q.y, q.z};
    const float b2 = Dot(b, b);
    return (v * (q.w * q.w - b2) + b * (Dot(v, b) * 2.0f) + Cross(b, v) * (q.w * 2.0f));
}

const int PROBE_FIXED_RAYS_COUNT = 32;
const int PROBE_TOTAL_RAYS_COUNT = 128;

Ren::Vec3f get_probe_ray_dir(const int ray_index, Ren::Quatf rot_quat) {
    const bool is_fixed_ray = (ray_index < PROBE_FIXED_RAYS_COUNT);
    const int rays_count = is_fixed_ray ? PROBE_FIXED_RAYS_COUNT : (PROBE_TOTAL_RAYS_COUNT - PROBE_FIXED_RAYS_COUNT);
    const int sample_index = is_fixed_ray ? ray_index : (ray_index - PROBE_FIXED_RAYS_COUNT);

    Ren::Vec3f dir = spherical_fibonacci(float(sample_index), float(rays_count));
    if (!is_fixed_ray) {
        dir = quat_rotate(dir, rot_quat);
    }
    return Normalize(dir);
}

} // namespace GSFibTestInternal

extern "C" {
VSHADER vtx_color_vs2(VS_IN, VS_OUT) {
    using namespace GSFibTestInternal;

    memcpy(V_FVARYING(V_COL), V_FATTR(A_COL), 3 * sizeof(float));

    const float(&in_mat)[4][4] = *(float(*)[4][4])F_UNIFORM(U_MVP);
    const float *in_vec = V_FATTR(A_POS);

    V_POS_OUT[0] = in_mat[0][0] * in_vec[0] + in_mat[1][0] * in_vec[1] + in_mat[2][0] * in_vec[2] + in_mat[3][0];
    V_POS_OUT[1] = in_mat[0][1] * in_vec[0] + in_mat[1][1] * in_vec[1] + in_mat[2][1] * in_vec[2] + in_mat[3][1];
    V_POS_OUT[2] = in_mat[0][2] * in_vec[0] + in_mat[1][2] * in_vec[1] + in_mat[2][2] * in_vec[2] + in_mat[3][2];
    V_POS_OUT[3] = in_mat[0][3] * in_vec[0] + in_mat[1][3] * in_vec[1] + in_mat[2][3] * in_vec[2] + in_mat[3][3];
}

FSHADER vtx_color_fs2(FS_IN, FS_OUT) {
    using namespace GSFibTestInternal;

    const float *in_color = F_FVARYING_IN(V_COL);

    F_COL_OUT[0] = in_color[0];
    F_COL_OUT[1] = in_color[1];
    F_COL_OUT[2] = in_color[2];
    F_COL_OUT[3] = 1.0f;
}
}

GSFibTest::GSFibTest(Viewer *viewer) : viewer_(viewer) {
    using namespace GSFibTestInternal;

    state_manager_ = viewer->GetComponent<GameStateManager>(STATE_MANAGER_KEY);

    random_ = viewer_->random.get();

    {
        const Ren::Attribute attrs[] = {{"pos", A_POS, SW_VEC3, 1}, {"col", A_COL, SW_VEC3, 1}, {}};
        const Ren::Attribute unifs[] = {{"mvp", U_MVP, SW_MAT4, 1}, {}};
        Ren::eProgLoadStatus status;
        vtx_color_prog_ = viewer->ren_ctx->LoadProgramSW("vtx_color", (void *)vtx_color_vs2, (void *)vtx_color_fs2, 3,
                                                         attrs, unifs, &status);
    }
}

void GSFibTest::Enter() {
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);

    using namespace GSFibTestInternal;

    for (int i = 0; i < PROBE_TOTAL_RAYS_COUNT; ++i) {
        const Ren::Vec3f p = get_probe_ray_dir(i, Ren::Quatf{});
        unrotated_dirs_.push_back(p);
    }
}

void GSFibTest::Exit() {}

void GSFibTest::Draw(uint64_t dt_us) {
    using namespace Ren;
    using namespace GSFibTestInternal;

    swClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    swClearDepth(1.0f);

    swBindBuffer(SW_ARRAY_BUFFER, 0);
    swBindBuffer(SW_INDEX_BUFFER, 0);

    swUseProgram(vtx_color_prog_->prog_id());

    const auto cam_center = Ren::Vec3f{2.5f * std::cos(cam_angle_), 0.0f, 2.5f * std::sin(cam_angle_)};
    const auto cam_target = Ren::Vec3f{0.0f, 0.0f, 0.0f};
    const auto cam_up = Ren::Vec3f{0.0f, 1.0f, 0.0f};

    const float k = float(viewer_->ren_ctx->w()) / viewer_->ren_ctx->h();
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

    for (int i = PROBE_FIXED_RAYS_COUNT; i < PROBE_TOTAL_RAYS_COUNT; ++i) {
        const Ren::Vec3f p = get_probe_ray_dir(i, Ren::Quatf{});

        const float lines_pos[][3] = {{0.5f * p[0], 0.5f * p[1], 0.5f * p[2]}, //
                                      {0.45f * p[0], 0.45f * p[1], 0.45f * p[2]}};
        const float lines_col[][3] = {{1, 0, 0}, {1, 0, 0}};

        swVertexAttribPointer(A_POS, 3 * sizeof(float), 0, (void *)&lines_pos[0][0]);
        swVertexAttribPointer(A_COL, 3 * sizeof(float), 0, (void *)&lines_col[0][0]);
        swDrawArrays(SW_LINE_STRIP, 0, 2);
    }

    static float best_yaw = 0.3098f, best_pitch = 0.5f, best_roll = 0.5f;
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    // yaw = pitch = roll = 0.125f;

    static double best_distance = 12289.915618;
    double curr_distance = 0.0;

    static uint64_t frame_index;
    ++frame_index;

    const int PerDimCount = 100;

    uint64_t sample_index = frame_index;
    yaw = (sample_index % PerDimCount) / float(PerDimCount);
    sample_index /= PerDimCount;
    pitch = (sample_index % PerDimCount) / float(PerDimCount);
    sample_index /= PerDimCount;
    roll = (sample_index % PerDimCount) / float(PerDimCount);

    yaw *= 2.0f * Ren::Pi<float>();
    pitch *= 2.0f * Ren::Pi<float>();
    roll *= 2.0f * Ren::Pi<float>();

    Ren::Quatf q2 = Ren::ToQuat(yaw, pitch, roll);
    for (int i = PROBE_FIXED_RAYS_COUNT; i < PROBE_TOTAL_RAYS_COUNT; ++i) {
        const Ren::Vec3f p = get_probe_ray_dir(i, q2);

        double total_distance = 0.0;
        for (int i = PROBE_FIXED_RAYS_COUNT; i < PROBE_TOTAL_RAYS_COUNT; ++i) {
            const Ren::Vec3f unrotated_p = unrotated_dirs_[i];
            total_distance += Distance(p, unrotated_p);
        }
        curr_distance += total_distance;

        const float lines_pos[][3] = {{0.5f * p[0], 0.5f * p[1], 0.5f * p[2]}, //
                                      {0.45f * p[0], 0.45f * p[1], 0.45f * p[2]}};
        const float lines_col[][3] = {{0, 1, 0}, {0, 1, 0}};

        swVertexAttribPointer(A_POS, 3 * sizeof(float), 0, (void *)&lines_pos[0][0]);
        swVertexAttribPointer(A_COL, 3 * sizeof(float), 0, (void *)&lines_col[0][0]);
        //swDrawArrays(SW_LINE_STRIP, 0, 2);
    }

    if (curr_distance > best_distance) {
        best_distance = curr_distance;

        best_yaw = yaw / (2.0f * Ren::Pi<float>());
        best_pitch = pitch / (2.0f * Ren::Pi<float>());
        best_roll = roll / (2.0f * Ren::Pi<float>());

        viewer_->log->Info("%f (%f %f %f)", best_distance, best_yaw, best_pitch, best_roll);
    }

    static int percent = -1;

    const int percent_done = 100 * int(frame_index) / (PerDimCount * PerDimCount * PerDimCount);
    if (percent_done != percent) {
        viewer_->log->Info("%i%% done", percent_done);
        percent = percent_done;
    }

    Ren::Quatf q3 = Ren::ToQuat(best_yaw, best_pitch, best_roll);
    for (int i = PROBE_FIXED_RAYS_COUNT; i < PROBE_TOTAL_RAYS_COUNT; ++i) {
        const Ren::Vec3f p = get_probe_ray_dir(i, q3);

        const float lines_pos[][3] = {{0.5f * p[0], 0.5f * p[1], 0.5f * p[2]}, //
                                      {0.45f * p[0], 0.45f * p[1], 0.45f * p[2]}};
        const float lines_col[][3] = {{0, 0, 1}, {0, 0, 1}};

        swVertexAttribPointer(A_POS, 3 * sizeof(float), 0, (void *)&lines_pos[0][0]);
        swVertexAttribPointer(A_COL, 3 * sizeof(float), 0, (void *)&lines_col[0][0]);
        swDrawArrays(SW_LINE_STRIP, 0, 2);
    }
}

void GSFibTest::Update(uint64_t dt_us) {}

void GSFibTest::HandleInput(const InputManager::Event &evt) {
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
