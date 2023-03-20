#pragma once

#include <memory>
#include <string>
#include <vector>

#include <DemoLib/eng/TimedInput.h>
#include <Sys/DynLib.h>

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;

struct AppParams;
class GameBase;

class DemoApp {
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *texture_ = nullptr;
    SDL_Window *window_ = nullptr;

    std::shared_ptr<InputManager> p_input_manager_;

    bool quit_, capture_frame_ = false;

#if !defined(__ANDROID__)
    static bool ConvertToRawButton(int32_t key, InputManager::RawInputButton &button);
    void PollEvents();
#endif

    std::unique_ptr<GameBase> viewer_;

    void CreateViewer(int w, int h, const AppParams &app_params, bool nogpu, bool nohwrt, bool nobindless,
                      bool nocompression);

  public:
    DemoApp();
    ~DemoApp();

    int Init(int w, int h, const AppParams &app_params, bool nogpu, bool nohwrt, bool nobindless, bool nocompression);
    void Destroy();

    void Frame();

#if !defined(__ANDROID__)
    int Run(int argc, char *argv[]);
#endif

    bool terminated() const { return quit_; }
};
