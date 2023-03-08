#include "GameBase.h"

#include <random>
#include <thread>

#include <Sys/AssetFileIO.h>
#include <Sys/Json.h>
#include <Sys/Time_.h>
#include <Sys/ThreadPool.h>

#include "GameStateManager.h"
#include "Log.h"
#include "Random.h"
#include "../gui/BaseElement.h"
#include "../gui/Renderer.h"
#include "../ren/Context.h"

GameBase::GameBase(const int w, const int h, const char * /*local_dir*/) : width(w), height(h) {
    terminated = false;

    //Sys::InitWorker();

    auto log =
#if !defined(__ANDROID__)
        std::make_shared<LogStdout>();
#else
        std::make_shared<LogAndroid>("APP_JNI");
#endif
    AddComponent(LOG_KEY, log);

    auto ctx = std::make_shared<Ren::Context>();
    ctx->Init(w, h);
    AddComponent(REN_CONTEXT_KEY, ctx);

#if !defined(__EMSCRIPTEN__)
    auto num_threads = std::max(std::thread::hardware_concurrency(), 1u);
    auto pool = std::make_shared<Sys::ThreadPool>(num_threads);
    AddComponent(THREAD_POOL_KEY, pool);
#endif

    auto state_manager = std::make_shared<GameStateManager>();
    AddComponent(STATE_MANAGER_KEY, state_manager);

    auto input_manager = std::make_shared<InputManager>();
    AddComponent(INPUT_MANAGER_KEY, input_manager);

    auto random_engine = std::make_shared<Random>(std::random_device{}());
    AddComponent(RANDOM_KEY, random_engine);

    JsObject config;
    config[Gui::GL_DEFINES_KEY] = JsString{ "" };
    auto ui_renderer = std::make_shared<Gui::Renderer>(*ctx.get(), config);
    AddComponent(UI_RENDERER_KEY, ui_renderer);

    auto ui_root = std::make_shared<Gui::RootElement>(Gui::Vec2i(w, h));
    AddComponent(UI_ROOT_KEY, ui_root);
}

GameBase::~GameBase() {
    // context should be deleted last
    auto ctx = GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    components_.clear();

    //Sys::StopWorker();
}

void GameBase::Resize(const int w, const int h) {
    width = w;
    height = h;

    auto ctx = GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    ctx->Resize(width, height);

    auto ui_root = GetComponent<Gui::RootElement>(UI_ROOT_KEY);
    ui_root->set_zone({ width, height });
    ui_root->Resize(nullptr);
}

void GameBase::Start() {

}

void GameBase::Frame() {
    auto state_manager = GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    auto input_manager = GetComponent<InputManager>(INPUT_MANAGER_KEY);

    //PROFILE_FUNC();

    FrameInfo &fr = fr_info_;

    fr.cur_time_us = Sys::GetTimeUs();
    if (fr.cur_time_us < fr.prev_time_us) fr.prev_time_us = 0;
    fr.delta_time_us = fr.cur_time_us - fr.prev_time_us;
    if (fr.delta_time_us > 200000) {
        fr.delta_time_us = 200000;
    }
    fr.prev_time_us = fr.cur_time_us;
    fr.time_acc_us += fr.delta_time_us;

    uint64_t poll_time_point = fr.cur_time_us - fr.time_acc_us;

    {
        //PROFILE_BLOCK(Update);
        while (fr.time_acc_us >= UPDATE_DELTA) {
            InputManager::Event evt;
            while (input_manager->PollEvent(poll_time_point, evt)) {
                state_manager->HandleInput(evt);
            }

            state_manager->Update(UPDATE_DELTA);
            fr.time_acc_us -= UPDATE_DELTA;

            poll_time_point += UPDATE_DELTA;
        }
    }

    fr.time_fract = double(fr.time_acc_us) / UPDATE_DELTA;

    {
        //PROFILE_BLOCK(Draw);
        state_manager->Draw(fr_info_.delta_time_us);
    }
}

void GameBase::Quit() {
    terminated = true;
}
