#pragma once

#include <cstdarg>
#include <cstdio>

#include <atomic>

#include "../Log.h"

enum class eTestScene {
    Standard,
    Standard_Filmic,
    Standard_SphereLight,
    Standard_SpotLight,
    Standard_MeshLights,
    Standard_SunLight,
    Standard_HDRLight,
    Standard_NoLight,
    Standard_DOF0,
    Standard_DOF1,
    Standard_GlassBall0,
    Standard_GlassBall1,
    Refraction_Plane
};

namespace Ray {
class RendererBase;
class SceneBase;

struct settings_t;
} // namespace Ray

extern std::atomic_bool g_log_contains_errors;
extern bool g_catch_flt_exceptions;

class LogErr final : public Ray::ILog {
    FILE *err_out_ = nullptr;

  public:
    LogErr() {
#pragma warning(suppress : 4996)
        err_out_ = fopen("test_data/errors.txt", "w");
    }
    ~LogErr() override { fclose(err_out_); }

    void Info(const char *fmt, ...) override {}
    void Warning(const char *fmt, ...) override {}
    void Error(const char *fmt, ...) override {
        va_list vl;
        va_start(vl, fmt);
        vfprintf(err_out_, fmt, vl);
        va_end(vl);
        putc('\n', err_out_);
        fflush(err_out_);
        g_log_contains_errors = true;
    }
};

extern LogErr g_log_err;

template <typename MatDesc>
void setup_test_scene(Ray::SceneBase &scene, bool output_sh, int min_samples, float variance_threshold,
                      const MatDesc &main_mat_desc, const char *textures[], eTestScene test_scene);

void schedule_render_jobs(Ray::RendererBase &renderer, const Ray::SceneBase *scene, const Ray::settings_t &settings,
                          int max_samples, bool denoise, bool partial, const char *log_str);