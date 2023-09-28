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

class GSFilterTest : public GameState {
    Viewer *viewer_ = nullptr;
    std::weak_ptr<GameStateManager> state_manager_;

    Random *random_ = nullptr;

    std::vector<Ray::color_rgba8_t> img_;
    float mul_ = 0.001f;

    std::vector<Ren::Vec4f> quadtree_[16];

    std::vector<float> pixels_;
    float iteration_ = 0;

    std::vector<float> filter_table_;

public:
    explicit GSFilterTest(Viewer *viewer);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};
