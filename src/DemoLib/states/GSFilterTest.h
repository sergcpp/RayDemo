#pragma once

#include "../eng/GameState.h"
#include "../ren/Camera.h"
#include "../ren/Program.h"
#include "../ren/Texture.h"

#include <Ray/Types.h>

class GameBase;
class GameStateManager;
class GCursor;
class FontStorage;
class Random;
class Renderer;

namespace Gui {
class BaseElement;
class BitmapFont;
class Renderer;
}

class GSFilterTest : public GameState {
    GameBase *game_;
    std::weak_ptr<GameStateManager> state_manager_;
    std::shared_ptr<Ren::Context> ctx_;
    std::shared_ptr<Renderer> renderer_;

    std::shared_ptr<Random> random_;

    std::shared_ptr<Gui::Renderer> ui_renderer_;
    std::shared_ptr<Gui::BaseElement> ui_root_;
    std::shared_ptr<Gui::BitmapFont> font_;

    std::vector<Ray::color_rgba8_t> img_;
    int img_w_, img_h_;
    float mul_ = 0.001f;

    std::vector<Ren::Vec4f> quadtree_[16];
    int quadtree_res_ = 0;
    int quadtree_count_ = 0;

    std::vector<float> pixels_;
    float iteration_ = 0;

    std::vector<float> filter_table_;

public:
    explicit GSFilterTest(GameBase *game);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};