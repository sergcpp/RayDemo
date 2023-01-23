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
    std::string device_name;
    int samples = -1;
    double psnr = 0.0;
    int threshold = -1;
    int diff_depth = 4;
    int spec_depth = 4;
    int refr_depth = 8;
    int transp_depth = 8;
    int total_depth = 8;
    int max_tex_res = -1;
};

class Viewer : public GameBase {
  public:
    Viewer(int w, int h, const char *local_dir, const AppParams &app_params, int gpu_mode, bool nobindless,
           bool nocompression);
};
