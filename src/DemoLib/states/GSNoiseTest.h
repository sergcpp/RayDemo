#pragma once

#include "../eng/GameState.h"
#include "../ren/Camera.h"
#include "../ren/Program.h"
#include "../ren/Texture.h"

#include <Ray/Types.h>

class GameStateManager;
class Random;
class Viewer;

namespace Gui {
class BaseElement;
class BitmapFont;
class Renderer;
}

class GSNoiseTest : public GameState {
    Viewer *viewer_ = nullptr;
    std::weak_ptr<GameStateManager> state_manager_;

public:
    explicit GSNoiseTest(Viewer *viewer);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};
