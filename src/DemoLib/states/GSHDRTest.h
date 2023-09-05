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

class GSHDRTest : public GameState {
    Viewer *viewer_ = nullptr;
    std::weak_ptr<GameStateManager> state_manager_;

    Random *random_ = nullptr;

    std::vector<Ray::color_rgba8_t> img_;
    int img_w_, img_h_;
    float mul_ = 0.001f;

    std::vector<Ren::Vec4f> quadtree_[16];
    int quadtree_res_ = 0;
    int quadtree_count_ = 0;

    std::vector<float> pixels_;
    float iteration_ = 0;

    Ren::Vec2i latlong_to_xy(float theta, float phi, int lod = 0) const;

    Ren::Vec3f sample_env(float theta, float phi) const;
    Ren::Vec3f sample_env(int x, int y) const;

    float sample_lum(float u, float v, int lod) const;
    float sample_lum(int x, int y, int lod) const;

public:
    explicit GSHDRTest(Viewer *viewer);

    void Enter() override;
    void Exit() override;

    void Draw(uint64_t dt_us) override;

    void Update(uint64_t dt_us) override;

    void HandleInput(const InputManager::Event &evt) override;
};
