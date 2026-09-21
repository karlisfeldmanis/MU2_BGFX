// A little world of its own, for drawing item models flat against a window.
//
// MU2's `Panel.Stage`, and MU's `RenderObjectScreen` before it: MU's item pictures are the
// real models rendered through the same path the world uses, turned to face the viewer and
// photographed into a texture the window draws like any other image. One stage per window --
// the bag's, the shop's -- covering the whole window, with each model standing where its cell
// is, so one pass draws every picture and they line up with the cells for free.
//
// Redrawn when what stands on it changes, and every frame only while something on it turns:
// MU's hover feedback is the item under the pointer spinning (`Angle[1] = WorldTime * 0.45`).
// A stage at rest costs its texture and nothing else.
#pragma once

#include <cstdint>
#include <vector>

#include "gfx/interface.h"

namespace mu::game {

// One item standing on a stage: which row, where in the window's own MU units, its plus, and
// whether it is the one turning under the pointer.
struct Standing {
    int32_t item = -1;
    gfx::Box box;
    int refinement = 0;
    bool spinning = false;
    bool operator==(const Standing& o) const {
        return item == o.item && box.x == o.box.x && box.y == o.box.y && box.w == o.box.w &&
               box.h == o.box.h && refinement == o.refinement && spinning == o.spinning;
    }
};

class Stage {
public:
    virtual ~Stage() = default;
    // What stands on it now, in the window's MU units across `unitsW` by `unitsH`. The same list
    // twice is no change and no redraw.
    virtual void stand(const std::vector<Standing>& items, float unitsW, float unitsH) = 0;
    // The picture, the window's size, drawn over the cells. Invalid until the first render.
    virtual gfx::Art picture() const = 0;
};

}  // namespace mu::game
