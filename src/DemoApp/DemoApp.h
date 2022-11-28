#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Eng/TimedInput.h>
#include <Sys/DynLib.h>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

class GameBase;

class DemoApp {
#if defined(USE_GL_RENDER)
    void *gl_ctx_ = nullptr;
#elif defined(USE_SW_RENDER)
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
#endif
    SDL_Window *window_ = nullptr;

    std::string scene_name_, ref_name_;
    int samples_ = -1, threshold_ = -1;
    double min_psnr_ = 0.0;
    int diff_depth_ = 4, spec_depth_ = 4, refr_depth_ = 8, transp_depth_ = 8, total_depth_ = 8;
    bool nogpu_, nohwrt_, nobindless_;

    std::shared_ptr<InputManager> p_input_manager_;

    bool quit_, capture_frame_ = false;

#if !defined(__ANDROID__)
    bool ConvertToRawButton(int32_t key, InputManager::RawInputButton &button);
    void PollEvents();
#endif

    std::unique_ptr<GameBase> viewer_;

    void CreateViewer(int w, int h, const char *scene_name, const char *ref_name, const char *device_name, bool nogpu,
                      bool nohwrt, bool nobindless, int samples, double psnr, int threshold);

  public:
    DemoApp();
    ~DemoApp();

    int Init(int w, int h, const char *scene_name, const char *ref_name, const char *device_name, bool nogpu,
             bool nohwrt, bool nobindless, int samples, double psnr, int threshold);
    void Destroy();

    void Frame();

#if !defined(__ANDROID__)
    int Run(int argc, char *argv[]);
#endif

    bool terminated() const { return quit_; }
};
