// The monster under the pointer: its name, and under it a rounded, lit, shadowed bar with the
// two health figures printed small inside it, floated over its head.
//
// MU2's `client/core/Vitals.cs`, and ours rather than MU's. MU's own readout is
// `CNewUINameWindow::RenderName`'s monster branch -- a name and twenty segments pinned to the
// top edge of the screen -- and MU2 built that first and replaced it, because a top-edge banner
// cannot say WHICH of the six around you it describes. Vitals.cs carries the long reasons for
// each of its numbers; they are copied here without the essays.
//
// What is kept: the fixed width (the length is the reading, so it cannot depend on the name or
// the monster), the pale trail that hangs a moment and then closes on the health, the quick
// fade up and the short linger after the pointer leaves, the longer eased fade on a kill, and
// the health as SHOWN rather than as the realm has it, so the red never drops before the number
// that took it goes up.
//
// What is not: the wait for the fall. MU2's realm killed a monster on the tick and its drawing
// held the collapse for the landing cue, and the bar waited for the collapse. Here the body
// goes down on the tick, so the bar goes with it, and there is nothing to wait for. Nor is the
// elf's summon's green Escort -- there is no summon.
//
// Godot drew the rounded pieces with StyleBoxFlat; the canvas has no such thing, so every
// shape is laid down one pixel row at a time, each row cut to the rounded outline and its two
// ends antialiased by coverage. A bar is fifteen-odd rows, so a layer is fifteen-odd quads.
#pragma once

#include <cstdint>

#include "gfx/interface.h"

namespace mu::game {

class Play;

class Vitals {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }

    // A frame. `pointed` is the body under the pointer or 0, polled every frame as MU2's Show
    // was -- what is on screen after the pointer leaves is the last monster's bar living out its
    // linger. `paneled` is whether a window has the pointer, which hides a new reading but lets
    // one already up run out. Rebuilds the canvas only when what it draws moved.
    void update(float seconds, const Play& play, uint32_t pointed, bool paneled,
                const float* viewProj, int width, int height);
    // Takes it down now, without the linger or the fade: leaving the world, not the monster.
    void dismiss();

    bool showing() const { return on_ != 0 && shown_ > 0.0f; }
    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    // Everything the canvas draws, compared whole to decide a rebuild.
    struct Readout {
        uint32_t on = 0;
        float x = 0, y = 0;
        float shown = 0, lag = 0, health = 0;
        int reading = 0, maximum = 0;
        float unit = 0;
        bool operator==(const Readout& o) const {
            return on == o.on && x == o.x && y == o.y && shown == o.shown && lag == o.lag &&
                   health == o.health && reading == o.reading && maximum == o.maximum &&
                   unit == o.unit;
        }
    };

    void trail(float now, float seconds);
    void rebuild(const Play& play, const Readout& r);

    gfx::Canvas canvas_;
    uint32_t on_ = 0;
    bool alive_ = false;   // whether it was alive last frame, so the kill can be caught
    float left_ = 0.0f;    // seconds of the linger left
    float shown_ = 0.0f;   // the fade, 0 to 1
    float lag_ = 0.0f;     // the trail's edge, as a fraction of the bar
    float was_ = 0.0f;     // the fraction last frame, so a drop can be noticed
    float holding_ = 0.0f; // seconds before the trail starts closing
    Readout drawn_;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
