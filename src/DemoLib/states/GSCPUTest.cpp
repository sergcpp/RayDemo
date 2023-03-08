#include "GSCPUTest.h"

#include <fstream>
#include <iostream>

#if defined(USE_SW_RENDER)
#include <Ren/SW/SW.h>
#endif

#include <Ren/Context.h>
#include <Sys/Json.h>
#include <Sys/ThreadPool.h>

#include <Ray/internal/Core.h>
#include <Ray/internal/Halton.h>

#include "GSCreate.h"
#include "../Viewer.h"
#include "../eng/GameStateManager.h"
#include "../eng/Random.h"
#include "../gui/FontStorage.h"
#include "../gui/Renderer.h"

namespace GSCPUTestInternal {
}

GSCPUTest::GSCPUTest(GameBase *game) : game_(game) {
    state_manager_	= game->GetComponent<GameStateManager>(STATE_MANAGER_KEY);
    ctx_			= game->GetComponent<Ren::Context>(REN_CONTEXT_KEY);
    renderer_		= game->GetComponent<Renderer>(RENDERER_KEY);

    random_         = game->GetComponent<Random>(RANDOM_KEY);

    ui_renderer_ = game->GetComponent<Gui::Renderer>(UI_RENDERER_KEY);
    ui_root_ = game->GetComponent<Gui::BaseElement>(UI_ROOT_KEY);

    const auto fonts = game->GetComponent<FontStorage>(UI_FONTS_KEY);
    font_ = fonts->FindFont("main_font");

    threads_        = game->GetComponent<Sys::ThreadPool>(THREAD_POOL_KEY);
}

GSCPUTest::~GSCPUTest() = default;

void GSCPUTest::Enter() {
#if defined(USE_SW_RENDER)
    swEnable(SW_FAST_PERSPECTIVE_CORRECTION);
    swEnable(SW_DEPTH_TEST);
#endif
    using namespace GSCPUTestInternal;

    if (state_ == eWarmup) {
        auto warmup_job = [this]() {
            double mean_us = 0, deviation = 999999;
            int iterations = 0;

            bool signaled_ready = false;

            auto warmup_start = std::chrono::high_resolution_clock::now();

            while (!warmup_done_) {
                iterations++;

                auto t1 = std::chrono::high_resolution_clock::now();

                volatile double f = 12;
                for (size_t i = 0; i < 100000000; i++) f += std::sqrt(f);

                auto t2 = std::chrono::high_resolution_clock::now();

                auto dt = std::chrono::duration_cast<std::chrono::duration<double, std::micro>>(t2 - t1);

                double k = 1.0/iterations;

                deviation = dt.count() - mean_us;
                mean_us += deviation * k;
                
                std::chrono::duration<double, std::milli> time_since_start = std::chrono::high_resolution_clock::now() - warmup_start;
                if ((deviation < 1000.0 || time_since_start.count() > 100.0) && !signaled_ready) {
                    ++num_ready_;
                    signaled_ready = true;
                }

                //std::cout << std::this_thread::get_id() << " : " << mean_us << " us, deviation " << std::abs(deviation) << " us" << std::endl;
            }
        };

        for (size_t i = 0; i < threads_->workers_count(); i++) {
            threads_->Enqueue(warmup_job);
        }
    } else if (state_ == eStarted) {
        state_ = eFinished;
    }
}

void GSCPUTest::Exit() {

}

void GSCPUTest::Draw(uint64_t dt_us) {
    using namespace GSCPUTestInternal;

#if defined(USE_SW_RENDER)
    swClearColor(0, 0, 0, 0);
#endif

    if (state_ == eWarmup) {
        int num_ready = num_ready_;
        if (num_ready == threads_->workers_count()) {
            warmup_done_ = true;
            state_ = eStarted;

            auto sm = state_manager_.lock();
            sm->Push(GSCreate(GS_RAY_BUCK_TEST, game_));
        }
    } else if (state_ == eStarted) {
        
    } else if (state_ == eFinished) {

    }

#if 1
    {
        // ui draw
        ui_renderer_->BeginDraw();

        if (state_ == eWarmup) {
            font_->set_scale(4.0f);
            std::string text = "WARMUP";
            float len = font_->GetWidth(text.c_str(), ui_root_.get());
            for (int i = 50; i < counter_; i += 100) text += ".";
            font_->DrawText(ui_renderer_.get(), text.c_str(), { -0.5f * len, -0.5f * font_->height(ui_root_.get()) }, ui_root_.get());
            font_->set_scale(1.0f);

            counter_++;
            if (counter_ > 300) counter_ = 0;
        } else if (state_ == eStarted) {
            //font_->DrawText(ui_renderer_.get(), "STARTING", { 0.0f, 0.0f }, ui_root_.get());
        } else if (state_ == eFinished) {
            auto result = game_->GetComponent<double>(TEST_RESULT_KEY);

            font_->set_scale(4.0f);
            std::string text = "RESULT: ";
            text += std::to_string((unsigned)std::round(1000000.0 / *result));
            float len = font_->GetWidth(text.c_str(), ui_root_.get());
            font_->DrawText(ui_renderer_.get(), text.c_str(), { -0.5f * len, -0.5f * font_->height(ui_root_.get()) }, ui_root_.get());
            font_->set_scale(1.0f);
        }

        //float font_height = font_->height(ui_root_.get());
        //font_->DrawText(ui_renderer_.get(), "regular", { 0.25f, 1 - font_height }, ui_root_.get());
        //font_->DrawText(ui_renderer_.get(), "random", { 0.25f, 1 - 2 * 0.25f - font_height }, ui_root_.get());
        //font_->DrawText(ui_renderer_.get(), "jittered", { 0.25f, 1 - 2 * 0.5f - font_height }, ui_root_.get());
        //font_->DrawText(ui_renderer_.get(), "halton", { 0.25f, 1 - 2 * 0.75f - font_height }, ui_root_.get());

        ui_renderer_->EndDraw();
    }
#endif

    ctx_->ProcessTasks();
}

void GSCPUTest::Update(uint64_t dt_us) {

}

void GSCPUTest::HandleInput(const InputManager::Event &evt) {
    switch (evt.type) {
    case InputManager::RAW_INPUT_P1_DOWN: {

    } break;
    case InputManager::RAW_INPUT_P1_UP:

        break;
    case InputManager::RAW_INPUT_P1_MOVE: {

    } break;
    case InputManager::RAW_INPUT_KEY_UP: {
        if (evt.key == InputManager::RAW_INPUT_BUTTON_SPACE) {
            
        }
    }
    break;
    case InputManager::RAW_INPUT_RESIZE:
        
        break;
    default:
        break;
    }
}
