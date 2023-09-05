#pragma once

#include "../eng/GameState.h"
#include "../ren/Camera.h"
#include "../ren/Program.h"
#include "../ren/Texture.h"

class GameStateManager;
class Viewer;

namespace Gui {
class BaseElement;
class BitmapFont;
class Renderer;
}

class GSVNDFTest : public GameState {
    Viewer *viewer_ = nullptr;

    std::weak_ptr<GameStateManager> state_manager_;

    Ren::ProgramRef vtx_color_prog_;
    Ren::Camera cam_;

    bool input_grabbed_ = false;
    int mode_ = 0;
    float cam_angle_ = 0.0f;

public:
    explicit GSVNDFTest(Viewer *viewer);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};
