#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../gui/BitmapFont.h"

class FontStorage {
    std::vector<std::pair<std::string, std::unique_ptr<Gui::BitmapFont>>> fonts_;
public:

    Gui::BitmapFont *FindFont(const std::string &name) const {
        for (auto &f : fonts_) {
            if (f.first == name) {
                return f.second.get();
            }
        }
        return nullptr;
    }

    Gui::BitmapFont *LoadFont(const std::string &name, const std::string &file_name, Ren::Context *ctx) {
        auto font = FindFont(name);
        if (!font) {
            auto new_font = std::make_unique<Gui::BitmapFont>(file_name.c_str(), ctx);
            font = new_font.get();
            fonts_.push_back(std::make_pair(name, std::move(new_font)));
        }
        return font;
    }

    void EraseFont(const char *name) {
        for (auto it = fonts_.begin(); it != fonts_.end(); ++it) {
            if (it->first == name) {
                fonts_.erase(it);
                return;
            }
        }
    }

    void Clear() {
        fonts_.clear();
    }
};
