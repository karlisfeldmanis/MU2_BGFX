#pragma once

// **The rejoin's badge**, top left: a turning arc and "Reconnecting" while the line is joined
// again by itself (PlayMode::rejoin), in place of the Disconnected window. The user, 2026-10-23:
// "we need those reconnection on client side be more less vissible with nice elegenat preloader
// in top left corner so user is not spooked". The line chip's plate and type (line_chip.h), so
// the two corners read as one. The window still comes when the game cannot get back by itself
// (Desk::update, kQuietMost). Ours.

#include "gfx/interface.h"

namespace mu::game {

class RejoinBadge {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    void close() {
        canvas_.clear();
        alpha_ = 0.0f;
        up_ = false;
    }
    // One frame: `up` while the rejoin goes on; `count` the seconds to the next try, or -1. It
    // fades in and out, and turns while it shows. Its top left at `left`, `top`, in pixels.
    void update(float seconds, bool up, int count, float left, float top);
    bool showing() const { return alpha_ > 0.0f; }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    gfx::Canvas canvas_;
    bool up_ = false;
    float alpha_ = 0.0f;
    float turn_ = 0.0f;  // the arc's head, in turns
};

}  // namespace mu::game
