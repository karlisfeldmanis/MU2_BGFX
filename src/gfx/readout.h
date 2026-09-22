// The frame rate in the top right of a played frame, and nothing else.
//
// It is drawn through `Overlay` and not through `Interface`, deliberately. The interface is
// retained -- a Canvas a window, rebuilt when what it mirrors moves -- and it carries MU2's
// plate art, so a two-word readout would mean a canvas, an owner and a staleness rule for a
// string that changes four times a second. The overlay is immediate, one font on one atlas,
// one draw for everything it is given, and it is ALREADY up in a played run for the tile over
// the character's head (app/modes/play_mode.cpp). So this costs one more draw call in the hud view and no
// new texture, no new program and no new state. See the two files' own headers: the overlay
// is deliberately not the HUD, and this is exactly the bench-note kind of text it is for.
//
// Nothing here allocates per frame: the ring is a fixed array, the string is a member that is
// rewritten in place, and the median is taken only on the quarter-second the string is
// rebuilt on.
#pragma once

#include <cstdint>
#include <string>

namespace mu::gfx {

class Overlay;

class Readout {
public:
    // Called once a frame, at the same point of the loop, after the world is drawn. The wall
    // time between two calls IS the frame, so this needs nothing from the Application's own clock --
    // which is capped to one 20 Hz tick for the animation's sake and overridden whole by
    // --fixed-dt, neither of which is the frame rate anybody wants to read.
    void draw(Overlay& overlay, int width, int height);

private:
    // Half a second at 60 fps. The MEDIAN of these is what is shown, not the mean: a frame
    // that writes a screenshot stalls to about 250 ms (app/application.cpp says so on the line that
    // drops it from the statistics), and one such frame moves a mean of 31 by eight
    // milliseconds and moves a median by nothing. The instantaneous number is unreadable at
    // any rate -- it changes every frame and the eye gets a blur -- which is why there is a
    // window at all.
    static constexpr int kWindow = 31;
    double samples_[kWindow] = {};
    int filled_ = 0;
    int next_ = 0;
    int64_t last_ = 0;
    // MU2's own habit, kept: a readout is recomputed only when it changes, not every frame.
    // A quarter of a second is the slowest refresh that still reads as live -- four numbers a
    // second -- and it is what MU2's Hud.Stale settles at for the same reason.
    static constexpr double kRefreshMs = 250.0;
    double sinceText_ = kRefreshMs;  // so the first frame with a sample prints one
    std::string text_;
};

}  // namespace mu::gfx
