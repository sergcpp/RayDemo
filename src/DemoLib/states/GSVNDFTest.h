#pragma once

#include "../eng/GameState.h"
#include "../ren/Camera.h"
#include "../ren/Program.h"
#include "../ren/Texture.h"

class GameBase;
class GameStateManager;
class FontStorage;
class Random;
class Renderer;

namespace Gui {
class BaseElement;
class BitmapFont;
class Renderer;
}

class GSVNDFTest : public GameState {
    std::weak_ptr<GameStateManager> state_manager_;
    std::shared_ptr<Ren::Context> ctx_;
    std::shared_ptr<Renderer> renderer_;

    std::shared_ptr<Random> random_;

    std::shared_ptr<Gui::Renderer> ui_renderer_;
    std::shared_ptr<Gui::BaseElement> ui_root_;
    std::shared_ptr<Gui::BitmapFont> font_;

    Ren::ProgramRef vtx_color_prog_;
    Ren::Camera cam_;

    bool input_grabbed_ = false;
    int mode_ = 0;
    float cam_angle_ = 0.0f;

public:
    explicit GSVNDFTest(GameBase *game);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};
