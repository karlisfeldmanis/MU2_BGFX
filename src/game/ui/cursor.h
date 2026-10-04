// MU's own mouse pointer, drawn into the picture: a hand over the ground, a claw over
// something to kill, a grasp over something to pick up, a talking mouth over a townsperson.
//
// MU2's `client/core/Pointer.cs`, number for number, the two over a bench included: the sit
// pointer and the lean one, picked by RenderCursor's own list rather than by the pose (see
// content::Perch::leans).
//
// Drawn, not handed to the window manager: MU2's own remark applies unchanged. The hardware
// cursor is hidden once at the window (`Window::open`) and this is blitted over it instead, so
// what a screenshot shows is what a player aimed with.
#pragma once

#include "game/ui/panel.h"
#include "gfx/interface.h"

namespace mu::game {

class Cursor {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // Called once a frame, whether or not a fight is in view: MU2's Show and Step in one call.
    // `onLoot`, `onFolk`, `perch` and `onMonster` are RenderCursor's own ladder, item above NPC
    // above operable above monster; passing them all empty draws the plain hand. `mending` is
    // the repair mode, above the whole ladder: MU's hammer, tipped into a blow while `pressed`.
    enum class Perch { None, Sit, Lean };
    void update(float seconds, float x, float y, bool onMonster, bool onLoot, bool onFolk,
                Perch perch = Perch::None, bool mending = false, bool pressed = false);

    const gfx::Canvas& canvas() const { return canvas_; }

private:
    void drawHammer(const gfx::Art& art, float x, float y, bool pressed);

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    double elapsed_ = 0.0;  // the talk cursor's own clock, running always
};

}  // namespace mu::game
