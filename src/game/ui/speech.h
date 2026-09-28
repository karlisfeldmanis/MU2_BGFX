// What the town's guards say, over their heads: Play::said's lines, each in a speech bubble --
// paper with an ink outline and ink words, a tail down to the speaker -- that pops up out of him
// and shrinks away again.
//
// Ours: MU's guards say nothing. MU's own chat over a head (`CreateChat`) is a plain block of type;
// the user asked for a bubble that looks exactly like a speech bubble (2026-09-28), so this is the
// comic's shape rather than the interface's dark glass.
#pragma once

#include <cstdint>

#include "gfx/interface.h"

namespace mu::game {

class Play;

class Speech {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    // A frame. Rebuilt whenever anything is up, since a slip follows its speaker as he walks and
    // as the camera turns; empty and not rebuilt when nobody is speaking.
    void update(const Play& play, const float* viewProj, int width, int height);
    void dismiss() { canvas_.clear(); showing_ = false; }

    bool showing() const { return showing_; }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    gfx::Canvas canvas_;
    bool showing_ = false;
};

}  // namespace mu::game
