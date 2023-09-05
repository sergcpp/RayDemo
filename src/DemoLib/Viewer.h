#pragma once

#include "eng/GameBase.h"

struct AppParams {
    std::string scene_name;
    std::string ref_name;
    std::string device_name;
    int min_samples = 128;
    int max_samples = -1;
    double psnr = 0.0;
    int threshold = -1;
    int diff_depth = 4;
    int spec_depth = 4;
    int refr_depth = 8;
    int transp_depth = 8;
    int total_depth = 8;
    int max_tex_res = -1;
    int denoise_after = -1;
    int denoise_method = 1; // 0 - NLM, 1 - UNet
    int iteration_steps = 1;
    bool output_exr = false;
    bool output_aux = false;
    float clamp_direct = 0.0f;
    float clamp_indirect = 10.0f;
    float variance_threshold = 0.0f;
};

class FontStorage;

class Viewer : public GameBase {
  public:
    Viewer(int w, int h, const char *local_dir, const AppParams &app_params, int gpu_mode, bool nobindless,
           bool nocompression);
    ~Viewer();

    AppParams app_params = {};
    std::unique_ptr<FontStorage> ui_fonts;
};
