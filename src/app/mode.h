// A mode is one way of running this binary, and there are three of them.
//
// They share this lifecycle and nothing else. What is common to every frame -- the resize, the
// lighting sheet's reload, the palette reset, the effects pool, the screenshot, bgfx::frame()
// and the statistics -- belongs to the Application and happens either side of `frame()`. What
// is common to no frame belongs here.
//
// The rule that keeps this honest: a mode never calls bgfx::frame() and never reads the
// clock for its own delta. It is handed the frame it is drawing and the seconds the last one
// took, and everything it does is inside that. A mode that times itself would be measuring
// something the budget accounts cannot see.
#pragma once

#include "app/context.h"

namespace mu::app {

// Where the loop stands. `deltaSeconds` is the PREVIOUS frame's own length, which is what this
// frame advances a clip by: the frame's own is not known until it has been drawn, and a clip
// advanced by a delta measured after the draw is a clip one frame behind what is on screen.
struct Frame {
    int index = 0;            // within the segment, so --repeat restarts it at 0
    double deltaSeconds = 0.0;
    double elapsed = 0.0;     // seconds since the loop began, across segments
};

class Mode {
public:
    virtual ~Mode() = default;

    // Opened after the window, the renderer and the textures are up, and before the first
    // frame. False is a run that cannot start: the Application tears down what it owns and
    // returns 1, so a mode that fails half-open must close its own half first.
    virtual bool open(Context& ctx) = 0;

    // True when the window was closed while the mode was still opening -- the preloader's
    // spinner is the only thing that takes long enough for it to happen. It is a quit and not
    // a failure, so the Application leaves without running a frame and without an error.
    virtual bool quitEarly() const { return false; }

    // One frame of this mode's own work: its input, its update, its gather and its draw.
    virtual void frame(Context& ctx, const Frame& at) = 0;

    // The camera this frame was drawn from. The effects probe (--effects) strings its sprites
    // along the line from it to what it looks at, and that is the one thing outside a mode
    // that needs to know where the eye is.
    virtual const gfx::Camera& camera() const = 0;

    // The once-a-second lines, under the Application's own frame-rate line. Nothing by default.
    virtual void report(Context& ctx) {}

    // Before the renderer, the textures and the window go. Each mode tears down in its own
    // order -- the order is load-bearing and differs between them, which is why this is not
    // one sequence in the Application.
    virtual void shutdown(Context& ctx) {}
};

}  // namespace mu::app
