#include "Viewer.h"

#include <regex>
#include <sstream>

#include <Ray/RendererFactory.h>
#include <Ren/Context.h>
#include <Sys/AssetFile.h>
#include <Sys/Json.h>

#include "eng/GameStateManager.h"
#include "states/GSCreate.h"
#include "gui/FontStorage.h"

Viewer::Viewer(const int w, const int h, const char *local_dir, const AppParams &_app_params, const int gpu_mode,
               const bool nobindless, const bool nocompression)
    : GameBase(w, h, local_dir) {
    auto ctx = GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    auto log = GetComponent<Ray::ILog>(LOG_KEY);

    JsObject main_config;

    { // load config
        Sys::AssetFile config_file("assets/config.json");
        size_t config_file_size = config_file.size();
        std::unique_ptr<char[]> buf(new char[config_file_size]);
        config_file.Read(buf.get(), config_file_size);

        std::stringstream ss;
        ss.write(buf.get(), config_file_size);

        if (!main_config.Read(ss)) {
            throw std::runtime_error("Unable to load main config!");
        }
    }

    const JsObject &ui_settings = main_config.at("ui_settings").as_obj();

    { // load fonts
        auto font_storage = std::make_shared<FontStorage>();
        AddComponent(UI_FONTS_KEY, font_storage);

        const JsObject &fonts = ui_settings.at("fonts").as_obj();
        for (auto &el : fonts.elements) {
            const std::string &name = el.first;
            const JsString &file_name = el.second.as_str();

            font_storage->LoadFont(name, file_name.val, ctx.get());
        }
    }

    {
        auto test_result = std::make_shared<double>(0.0);
        AddComponent(TEST_RESULT_KEY, test_result);
    }

    {
        auto app_params = std::make_shared<AppParams>(_app_params);
        AddComponent(APP_PARAMS_KEY, app_params);
    }

    { // create ray renderer
        Ray::settings_t s;
        s.w = w;
        s.h = h;
#ifdef ENABLE_GPU_IMPL
        if (!_app_params.device_name.empty()) {
            s.preferred_device = _app_params.device_name.c_str();
        }
#endif

        std::shared_ptr<Ray::RendererBase> ray_renderer;

        if (gpu_mode == 0) {
            ray_renderer = std::shared_ptr<Ray::RendererBase>(Ray::CreateRenderer(s, log.get(), Ray::RendererCPU));
        } else {
            s.use_hwrt = (gpu_mode == 2);
            s.use_bindless = !nobindless;
#ifdef ENABLE_GPU_IMPL
            s.use_tex_compression = !nocompression;
#endif
            ray_renderer = std::shared_ptr<Ray::RendererBase>(Ray::CreateRenderer(s, log.get()));
        }

        if (!_app_params.device_name.empty() && !_app_params.ref_name.empty()) {
            // make sure we use requested device during reference tests
            std::regex match_name(_app_params.device_name);
            if (!std::regex_search(ray_renderer->device_name(), match_name)) {
                throw std::runtime_error("Requested device not found!");
            }
        }

        AddComponent(RAY_RENDERER_KEY, ray_renderer);
    }

    auto input_manager = GetComponent<InputManager>(INPUT_MANAGER_KEY);
    input_manager->SetConverter(InputManager::RAW_INPUT_P1_MOVE, nullptr);
    input_manager->SetConverter(InputManager::RAW_INPUT_P2_MOVE, nullptr);

    auto state_manager = GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    state_manager->Push(GSCreate(GS_RAY_TEST, this));
}
