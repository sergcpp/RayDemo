#include "DemoApp.h"

#if defined(USE_GL_RENDER)
#include <Ren/GL.h>
#elif defined(USE_SW_RENDER)

#endif

#if !defined(__ANDROID__)
#include <SDL2/SDL.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_video.h>
#endif

#include <Eng/GameBase.h>
#include <Eng/TimedInput.h>
#include <Ren/SW/SW.h>
#include <Sys/DynLib.h>
#include <Sys/Log.h>
#include <Sys/Time_.h>

#include <SDL2/SDL_events.h>

#include <cassert>
#include <cfloat>

#ifdef _WIN32
#include <renderdoc/renderdoc_app.h>
#endif

#include "../DemoLib/Viewer.h"

#pragma warning(disable : 4996)

namespace {
DemoApp *g_app = nullptr;
#ifdef _WIN32
RENDERDOC_API_1_1_2 *rdoc_api = NULL;
#endif
} // namespace

extern "C" {
// Enable High Performance Graphics while using Integrated Graphics
DLL_EXPORT int32_t NvOptimusEnablement = 0x00000001;     // Nvidia
DLL_EXPORT int AmdPowerXpressRequestHighPerformance = 1; // AMD

#ifdef _WIN32
DLL_IMPORT int __stdcall SetProcessDPIAware();
#endif
}

#ifdef _WIN32
namespace Ray {
namespace Vk {
extern RENDERDOC_DevicePointer rdoc_device;
} // namespace Vk
} // namespace Ray
#endif

DemoApp::DemoApp() : quit_(false) { g_app = this; }

DemoApp::~DemoApp() {}

int DemoApp::Init(int w, int h, const char *scene_name, const char *ref_name, const char *device_name, bool nogpu,
                  bool nohwrt, bool nobindless, bool nocompression, int samples, double min_psnr, int threshold) {
#if !defined(__ANDROID__)
#ifdef _WIN32
    int dpi_result = SetProcessDPIAware();
    (void)dpi_result;
#endif

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        const char *s = SDL_GetError();
        LOGE("%s\n", s);
        return -1;
    }

    window_ = SDL_CreateWindow("View", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, w, h,
                               SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        const char *s = SDL_GetError();
        LOGE("%s\n", s);
        return -1;
    }

#if defined(USE_GL_RENDER)
    gl_ctx_ = SDL_GL_CreateContext(window_);
#elif defined(USE_SW_RENDER)
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        const char *s = SDL_GetError();
        LOGE("%s\n", s);
        return -1;
    }
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
    if (!texture_) {
        const char *s = SDL_GetError();
        LOGE("%s\n", s);
        return -1;
    }
#endif
#endif

    putenv("MVK_CONFIG_FULL_IMAGE_VIEW_SWIZZLE=1");

#if !defined(NDEBUG) && defined(_WIN32)
    _controlfp(_EM_INEXACT, _MCW_EM);
#endif

    try {
        CreateViewer(w, h, scene_name, ref_name, device_name, nogpu, nohwrt, nobindless, nocompression, samples,
                     min_psnr, threshold);
    } catch (std::exception &e) {
        fprintf(stderr, "%s", e.what());
        return -1;
    }

    return 0;
}

void DemoApp::Destroy() {
    viewer_.reset();

#if !defined(__ANDROID__)
#if defined(USE_GL_RENDER)
    SDL_GL_DeleteContext(gl_ctx_);
#elif defined(USE_SW_RENDER)
    SDL_DestroyTexture(texture_);
    SDL_DestroyRenderer(renderer_);
#endif
    SDL_DestroyWindow(window_);
    SDL_Quit();
#endif
}

void DemoApp::Frame() {
#ifdef _WIN32
    if (capture_frame_ && rdoc_api) {
        rdoc_api->StartFrameCapture(Ray::Vk::rdoc_device, NULL);
    }
#endif
    viewer_->Frame();
#ifdef _WIN32
    if (capture_frame_ && rdoc_api) {
        const uint32_t ret = rdoc_api->EndFrameCapture(Ray::Vk::rdoc_device, NULL);
        assert(ret == 1);
    }
#endif
    capture_frame_ = false;
}

#if !defined(__ANDROID__)
int DemoApp::Run(int argc, char *argv[]) {
    int w = 640, h = 360;
    scene_name_ = "assets/scenes/mat_test.json";
    nogpu_ = false;
    nohwrt_ = false;
    nobindless_ = false;
    nocompression_ = false;
    samples_ = -1;
    min_psnr_ = 0.0;
    threshold_ = -1;
    diff_depth_ = 4;
    const char *device_name = nullptr;

    for (size_t i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--width") == 0 || strcmp(argv[i], "-w") == 0) && (++i != argc)) {
            w = atoi(argv[i]);
        } else if ((strcmp(argv[i], "--height") == 0 || strcmp(argv[i], "-h") == 0) && (++i != argc)) {
            h = atoi(argv[i]);
        } else if ((strcmp(argv[i], "--scene") == 0 || strcmp(argv[i], "-s") == 0) && (++i != argc)) {
            scene_name_ = argv[i];
        } else if ((strcmp(argv[i], "--reference") == 0 || strcmp(argv[i], "-ref") == 0) && (++i != argc)) {
            ref_name_ = argv[i];
        } else if (strcmp(argv[i], "--nogpu") == 0) {
            nogpu_ = true;
        } else if (strcmp(argv[i], "--nohwrt") == 0) {
            nohwrt_ = true;
        } else if (strcmp(argv[i], "--nobindless") == 0) {
            nobindless_ = true;
        } else if (strcmp(argv[i], "--nocompression") == 0) {
            nocompression_ = true;
        } else if (strcmp(argv[i], "--samples") == 0 && (++i != argc)) {
            samples_ = atoi(argv[i]);
        } else if (strcmp(argv[i], "--psnr") == 0 && (++i != argc)) {
            min_psnr_ = atof(argv[i]);
        } else if (strcmp(argv[i], "--threshold") == 0 && (++i != argc)) {
            threshold_ = atoi(argv[i]);
        } else if (strcmp(argv[i], "--diff_depth") == 0 && (++i != argc)) {
            diff_depth_ = atoi(argv[i]);
        } else if (strcmp(argv[i], "--spec_depth") == 0 && (++i != argc)) {
            spec_depth_ = atoi(argv[i]);
        } else if (strcmp(argv[i], "--refr_depth") == 0 && (++i != argc)) {
            refr_depth_ = atoi(argv[i]);
        } else if (strcmp(argv[i], "--transp_depth") == 0 && (++i != argc)) {
            transp_depth_ = atoi(argv[i]);
        } else if (strcmp(argv[i], "--total_depth") == 0 && (++i != argc)) {
            total_depth_ = atoi(argv[i]);
        } else if ((strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-d") == 0) && (++i != argc)) {
            device_name = argv[i];
        }
    }

    if (Init(w, h, scene_name_.c_str(), ref_name_.c_str(), device_name, nogpu_, nohwrt_, nobindless_, nocompression_,
             samples_, min_psnr_, threshold_) < 0) {
        return -1;
    }

#ifdef _WIN32
    Sys::DynLib rdoc{"renderdoc.dll"};
    if (rdoc) {
        pRENDERDOC_GetAPI RENDERDOC_GetAPI = (pRENDERDOC_GetAPI)rdoc.GetProcAddress("RENDERDOC_GetAPI");
        const int ret = RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_1_2, (void **)&rdoc_api);
        assert(ret == 1);
    }
#endif

    while (!terminated() && !viewer_->terminated) {
        this->PollEvents();
        this->Frame();

#if defined(USE_GL_RENDER)
        SDL_GL_SwapWindow(window_);
#elif defined(USE_SW_RENDER)
        const void *_pixels = swGetPixelDataRef(swGetCurFramebuffer());
        SDL_UpdateTexture(texture_, NULL, _pixels, viewer_->width * sizeof(Uint32));

        // SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, NULL, NULL);
        SDL_RenderPresent(renderer_);
#endif
    }

    const int return_code = viewer_->return_code;

    this->Destroy();
#endif
    return return_code;
}

bool DemoApp::ConvertToRawButton(int32_t key, InputManager::RawInputButton &button) {
    switch (key) {
    case SDLK_UP:
        button = InputManager::RAW_INPUT_BUTTON_UP;
        break;
    case SDLK_DOWN:
        button = InputManager::RAW_INPUT_BUTTON_DOWN;
        break;
    case SDLK_LEFT:
        button = InputManager::RAW_INPUT_BUTTON_LEFT;
        break;
    case SDLK_RIGHT:
        button = InputManager::RAW_INPUT_BUTTON_RIGHT;
        break;
    case SDLK_ESCAPE:
        button = InputManager::RAW_INPUT_BUTTON_EXIT;
        break;
    case SDLK_TAB:
        button = InputManager::RAW_INPUT_BUTTON_TAB;
        break;
    case SDLK_BACKSPACE:
        button = InputManager::RAW_INPUT_BUTTON_BACKSPACE;
        break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        button = InputManager::RAW_INPUT_BUTTON_SHIFT;
        break;
    case SDLK_DELETE:
        button = InputManager::RAW_INPUT_BUTTON_DELETE;
        break;
    case SDLK_SPACE:
        button = InputManager::RAW_INPUT_BUTTON_SPACE;
        break;
    default:
        button = InputManager::RAW_INPUT_BUTTON_OTHER;
        break;
    }
    return true;
}

void DemoApp::PollEvents() {
    SDL_Event e = {};
    InputManager::RawInputButton button;
    InputManager::Event evt = {};
    while (SDL_PollEvent(&e)) {
        evt.type = InputManager::RAW_INPUT_NONE;
        switch (e.type) {
        case SDL_KEYDOWN: {
            if (e.key.keysym.sym == SDLK_ESCAPE) {
                quit_ = true;
                return;
            } else if (e.key.keysym.sym == SDLK_F12) {
                capture_frame_ = true;
                return;
            } /*else if (e.key.keysym.sym == SDLK_TAB) {
            bool is_fullscreen = bool(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN);
            SDL_SetWindowFullscreen(window_, is_fullscreen ? 0 : SDL_WINDOW_FULLSCREEN);
            return;
        }*/ else if (ConvertToRawButton(e.key.keysym.sym, button)) {
                evt.type = InputManager::RAW_INPUT_KEY_DOWN;
                evt.key = button;
                evt.raw_key = e.key.keysym.sym;
            }
        } break;
        case SDL_KEYUP:
            if (ConvertToRawButton(e.key.keysym.sym, button)) {
                evt.type = InputManager::RAW_INPUT_KEY_UP;
                evt.key = button;
                evt.raw_key = e.key.keysym.sym;
            }
            break;
        case SDL_FINGERDOWN:
            evt.type = e.tfinger.fingerId == 0 ? InputManager::RAW_INPUT_P1_DOWN : InputManager::RAW_INPUT_P2_DOWN;
            evt.point.x = e.tfinger.x * viewer_->width;
            evt.point.y = e.tfinger.y * viewer_->height;
            break;
        case SDL_MOUSEBUTTONDOWN:
            evt.type = InputManager::RAW_INPUT_P1_DOWN;
            evt.point.x = float(e.motion.x);
            evt.point.y = float(e.motion.y);
            break;
        case SDL_FINGERUP:
            evt.type = e.tfinger.fingerId == 0 ? InputManager::RAW_INPUT_P1_UP : InputManager::RAW_INPUT_P2_UP;
            evt.point.x = e.tfinger.x * viewer_->width;
            evt.point.y = e.tfinger.y * viewer_->height;
            break;
        case SDL_MOUSEBUTTONUP:
            evt.type = InputManager::RAW_INPUT_P1_UP;
            evt.point.x = float(e.motion.x);
            evt.point.y = float(e.motion.y);
            break;
        case SDL_QUIT: {
            quit_ = true;
            return;
        }
        case SDL_FINGERMOTION:
            evt.type = e.tfinger.fingerId == 0 ? InputManager::RAW_INPUT_P1_MOVE : InputManager::RAW_INPUT_P2_MOVE;
            evt.point.x = e.tfinger.x * viewer_->width;
            evt.point.y = e.tfinger.y * viewer_->height;
            evt.move.dx = e.tfinger.dx * viewer_->width;
            evt.move.dy = e.tfinger.dy * viewer_->height;
            break;
        case SDL_MOUSEMOTION:
            evt.type = InputManager::RAW_INPUT_P1_MOVE;
            evt.point.x = float(e.motion.x);
            evt.point.y = float(e.motion.y);
            evt.move.dx = float(e.motion.xrel);
            evt.move.dy = float(e.motion.yrel);
            break;
        case SDL_MOUSEWHEEL:
            evt.type = InputManager::RAW_INPUT_MOUSE_WHEEL;
            evt.move.dx = float(e.wheel.x);
            evt.move.dy = float(e.wheel.y);
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                evt.type = InputManager::RAW_INPUT_RESIZE;
                evt.point.x = float(e.window.data1);
                evt.point.y = float(e.window.data2);

                viewer_->Resize(e.window.data1, e.window.data2);
#if defined(USE_SW_RENDER)
                SDL_RenderPresent(renderer_);
                SDL_DestroyTexture(texture_);
                texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             e.window.data1, e.window.data2);
#endif
            }
            break;
        default:
            return;
        }
        if (evt.type != InputManager::RAW_INPUT_NONE) {
            evt.time_stamp = Sys::GetTimeMs() - (SDL_GetTicks() - e.common.timestamp);
            p_input_manager_->AddRawInputEvent(evt);
        }
    }
}

void DemoApp::CreateViewer(int w, int h, const char *scene_name, const char *ref_name, const char *device_name,
                           bool nogpu, bool nohwrt, bool nobindless, bool nocompression, int samples, double psnr,
                           int threshold) {
    if (viewer_) {
        w = viewer_->width;
        h = viewer_->height;
    }

    viewer_ = {};

    AppParams app_params;
    app_params.scene_name = scene_name;
    if (ref_name) {
        app_params.ref_name = ref_name;
    }
    if (device_name) {
        app_params.device_name = device_name;
    }
    app_params.samples = samples;
    app_params.psnr = psnr;
    app_params.threshold = threshold;
    app_params.diff_depth = diff_depth_;
    app_params.spec_depth = spec_depth_;
    app_params.refr_depth = refr_depth_;
    app_params.transp_depth = transp_depth_;
    app_params.total_depth = total_depth_;

    viewer_.reset(new Viewer(w, h, "./", app_params, nogpu ? 0 : (nohwrt ? 1 : 2), nobindless, nocompression));
    p_input_manager_ = viewer_->GetComponent<InputManager>(INPUT_MANAGER_KEY);
}
