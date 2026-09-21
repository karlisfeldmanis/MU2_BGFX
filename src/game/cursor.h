// MU's own mouse pointer, drawn into the picture: a hand over the ground, a claw over
// something to kill, a grasp over something to pick up, a talking mouth over a townsperson.
//
// MU2's `client/core/Pointer.cs`, number for number, minus the two it drew over a bench --
// this game has no pose to sit or lean against, so `onPerch` never arrives and the plain hand
// is what MU2 itself falls back to where a build is missing one of its cursors.
//
// Drawn, not handed to the window manager: MU2's own remark applies unchanged. The hardware
// cursor is hidden once at the window (`Window::open`) and this is blitted over it instead, so
// what a screenshot shows is what a player aimed with.
#pragma once

#include "game/panel.h"
#include "gfx/interface.h"

namespace mu::game {

class Cursor {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);

    // Called once a frame, whether or not a fight is in view: MU2's Show and Step in one call.
    // `onLoot`, `onFolk` and `onMonster` are RenderCursor's own ladder, item above NPC above
    // monster; passing all three false draws the plain hand.
    void update(float seconds, float x, float y, bool onMonster, bool onLoot, bool onFolk);

    const gfx::Canvas& canvas() const { return canvas_; }

private:
    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    double elapsed_ = 0.0;  // the talk cursor's own clock, running always
};

}  // namespace mu::game
