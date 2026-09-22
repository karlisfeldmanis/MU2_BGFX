// The viewer's list, down the left, and the hit test that goes with it.
//
// A window onto the list rather than the whole of it: 156 names do not fit at a size anybody
// can read, and a list that scrolls past what is selected is no use for choosing. The
// selection is held in the middle of the window where it can be, so the eye stays in one
// place while the names move past it.
//
// Drawing and picking are one function on purpose. They share the same arithmetic -- where a
// row starts, how tall it is, which slice of the list is on screen -- and two copies of that
// drift the moment either changes, which is a list that highlights one name and selects
// another. `pointer` is where the mouse is; the row under it comes back in `hovered`, and
// what the panel covers in `bounds`, so the caller can tell a click on the list from a drag
// on the model.
#pragma once

#include "game/bench.h"
#include "gfx/overlay.h"

namespace mu::game {

struct ListHit {
    long long hovered = -1;   // the row the pointer is over, or -1
    long long tab = -1;       // the category tab the pointer is over, or -1
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool over = false;        // the pointer is somewhere on the panel
};

ListHit drawBrowserList(gfx::Overlay& overlay, const ModelBench& bench, int width, int height,
                        float pointerX, float pointerY);

}  // namespace mu::game
