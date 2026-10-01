// Go Back!: the way back to the field, over the middle of the HUD, for the five minutes after a
// Town Portal Scroll or a Tab trip took him out of it (app/context.h holds the spot and the clock).
//
// The proposal of 2026-10-01 (claude.ai/artifact/JL4Y6qWZV6YaNt3vjHPDHp), option B2 as the user
// tuned it: no filled box -- the words on a soft scrim, in an unbroken gold hairline frame with
// round corners ("dont cut outline", "use some border-radius"). "Go Back!" with the place it
// returns to under it, and the clock on the right behind a thin upright rule. Gold, red for the
// last thirty seconds, no pulse. Clicked, never keyed (the user: "dont use key, have to click").
// When the five minutes run out it says "Go Back! has closed" in a dark frame for three seconds.
//
// A mirror, as every window is: told what to show each frame, and it rebuilds only when that
// moved -- the second, the hover, the fade.
//
// Ours: 0.75 has no way back from town but the walk.
#pragma once

#include <string>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class GoBackPlate {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    void close() { canvas_.clear(); built_ = false; alpha_ = 0.0f; }
    // One frame. `secondsLeft` above 0 shows the clock; 0 with `closed` shows the closed line;
    // `shown` false takes it down. `plateTop` is the HUD's top edge in pixels, which it stands
    // over. Returns true on the frame it was clicked.
    bool update(float seconds, bool shown, int secondsLeft, bool closed, const std::string& where,
                const Pointer& pointer, int width, int height, float plateTop);
    bool covers(float x, float y) const { return alpha_ > 0.0f && box_.has(x, y); }
    bool showing() const { return alpha_ > 0.0f; }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    struct Drawn {
        int width = 0, height = 0;
        int plateTop = 0;
        int secondsLeft = 0;
        bool closed = false;
        bool hover = false;
        int alpha = 0;  // in 64ths
        std::string where;
        bool operator==(const Drawn& o) const {
            return width == o.width && height == o.height && plateTop == o.plateTop &&
                   secondsLeft == o.secondsLeft && closed == o.closed && hover == o.hover &&
                   alpha == o.alpha && where == o.where;
        }
    };
    void rebuild(const Drawn& now);

    gfx::Canvas canvas_;
    Drawn drawn_;
    bool built_ = false;
    float alpha_ = 0.0f;
    gfx::Box box_;
};

}  // namespace mu::game
