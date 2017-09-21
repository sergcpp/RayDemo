#pragma once

#include <Eng/GameBase.h>

const char UI_FONTS_KEY[] = "ui_fonts";

const char RENDERER_KEY[] = "renderer";
const char RAY_RENDERER_KEY[] = "ray_renderer";

const char TEST_RESULT_KEY[] = "test_result";

const char APP_PARAMS_KEY[] = "app_params";

struct AppParams {
    std::string scene_name;
    std::string ref_name;
    int samples = -1;
    double psnr = 0.0;
    int threshold = -1;
};

class Viewer : public GameBase {
  public:
    Viewer(int w, int h, const char *local_dir, const char *scene_name, const char *ref_name, const char *device_name,
           int samples, double psnr, int threshold, int gpu_mode);
};
