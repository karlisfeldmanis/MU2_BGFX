// The Sanctuary bench: every control in game/ui/controls.h drawn over the world in the real
// interface, for the user to judge from a shot before any window is changed to it.
//
// Raised by `--windows sanctuary`. Two sheets: "Sanctuary", every button at every size, kind
// and state (the frozen ones say which state they hold) with the icon squares and the meters;
// and "Options", the settings sheet the game menu will become -- rows, chevrons, switches, the
// volume slider, a field with its caret and one refused, and Back. The live ones answer the
// pointer, so `--ui-hover` parks it on one and the shot shows the real hover.
//
// It asks nothing of the realm and changes nothing: a click on a switch flips the switch, and
// that is all. It is a bench, and goes when the windows it stands for are drawn in it.
#pragma once

#include <cstdint>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Specimen {
public:
    void open(const gfx::Interface& interface);
    void close();
    void update(float seconds, float width, float height, const Pointer& pointer);
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    void rebuild();
    int hitAt(float x, float y) const;

    gfx::Canvas canvas_;
    float width_ = 0.0f, height_ = 0.0f;
    float clock_ = 0.0f;
    // What the live controls are doing, and what the switches say.
    static constexpr int kTargets = 32;
    float lift_[kTargets] = {};
    int over_ = -1, pressing_ = -1;
    bool vsync_ = true, counter_ = false;
    // Where each live control was drawn last, for the pointer.
    std::vector<std::pair<int, gfx::Box>> targets_;
    gfx::Box sheets_[2];
    // What the last rebuild drew for.
    struct Drawn {
        float width = -1, height = -1;
        int over = -2, pressing = -2;
        float lift[kTargets] = {};
        bool caret = false, vsync = false, counter = false;
        bool operator==(const Drawn& o) const;
    };
    Drawn drawn_;
};

}  // namespace mu::game
