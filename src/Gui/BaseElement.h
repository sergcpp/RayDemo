#pragma once

#include <bitset>

#include <Ren/MVec.h>

namespace Gui {
// forward declare everything
class BitmapFont;
class ButtonBase;
class ButtonImage;
class ButtonText;
class Cursor;
class EditBox;
class Frame;
class Image;
class LinearLayout;
class Renderer;
class TypeMesh;

using Ren::Vec2f;
using Ren::Vec2i;

enum eFlags {
    Visible,
    Resizable
};

class BaseElement {
protected:
    Vec2f           rel_dims_[2];

    Vec2f           dims_[2];
    Vec2i           dims_px_[2];
    std::bitset<32>     flags_;
public:
    BaseElement(const Vec2f &pos, const Vec2f &size, const BaseElement *parent);
    ~BaseElement() {}

    bool visible() const {
        return flags_[Visible];
    }
    bool resizable() const {
        return flags_[Resizable];
    }

    void set_visible(bool v) {
        flags_[Visible] = v;
    }
    void set_resizable(bool v) {
        flags_[Resizable] = v;
    }

    const Vec2f &rel_pos() const {
        return rel_dims_[0];
    }
    const Vec2f &rel_size() const {
        return rel_dims_[1];
    }

    const Vec2f &pos() const {
        return dims_[0];
    }
    const Vec2f &size() const {
        return dims_[1];
    }

    const Vec2i &pos_px() const {
        return dims_px_[0];
    }
    const Vec2i &size_px() const {
        return dims_px_[1];
    }

    virtual void Resize(const BaseElement *parent);
    virtual void Resize(const Vec2f &pos, const Vec2f &size, const BaseElement *parent);

    virtual bool Check(const Vec2i &p) const;
    virtual bool Check(const Vec2f &p) const;

    virtual void Focus(const Vec2i &/*p*/) {}
    virtual void Focus(const Vec2f &/*p*/) {}

    virtual void Press(const Vec2i &/*p*/, bool /*push*/) {}
    virtual void Press(const Vec2f &/*p*/, bool /*push*/) {}

    virtual void Draw(Renderer * /*r*/) {}
};

class RootElement : public BaseElement {
public:
    explicit RootElement(const Vec2i &zone_size) : BaseElement({ -1, -1 }, { 2, 2 }, nullptr) {
        set_zone(zone_size);
        Resize(Vec2f{ -1, -1 }, Vec2f{ 2, 2 }, nullptr);
    }

    void set_zone(const Vec2i &zone_size) {
        dims_px_[1] = zone_size;
    }

    void Resize(const BaseElement *parent) override {
        Resize(dims_[0], dims_[1], parent);
    }

    void Resize(const Vec2f &pos, const Vec2f &size, const BaseElement * /*parent*/) override {
        dims_[0] = pos;
        dims_[1] = size;
    }
};
}
