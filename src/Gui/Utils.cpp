#include "Utils.h"

Ren::Vec2f Gui::MapPointToScreen(const Ren::Vec2i &p, const Ren::Vec2i &res) {
    return (2.0f * Ren::Vec2f((float)p[0], (float)res[1] - p[1])) / (Ren::Vec2f)res + Ren::Vec2f(-1, -1);
}

