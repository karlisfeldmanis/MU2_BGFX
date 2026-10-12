// The studio's Place (docs/order-plan.md §9c, F6): a world's objects picked, moved, turned and
// scaled where they stand, in a world scene (not the Stage).
//
// A click on the world picks the object under the pointer -- the nearest whose box's sphere
// the ray passes through -- and a drag on it walks it over the land, keeping its height above
// the ground as it was. The Place window holds its numbers: where, how high, its yaw, its
// scale. Every change shows on the next frame, because the studio draws the town from its
// cooked instances each frame (Town::gatherAll) and the instance is moved in memory; it is
// written to source/world/<w>/<w>.json when let go, through mu serve's edit (a line in the
// journal), and the card's Cook -- `mu make placements <w>` -- makes it the cooked truth, after
// which the world is read again. Lamps and fires move with their object only then.
//
// An object is found in the source by its model and where it stands. The index adds objects
// of its own (assets/ holds more than source/), and those, which no source line holds, are
// shown but not moved. A `scale` the source does not write is the 1.0 the cook assumes, and
// is left as it is: an edit only changes a value that is there.
#pragma once

#include <string>
#include <vector>

#include "core/json.h"
#include "gfx/renderer.h"

namespace mu::game {
class World;
}
namespace mu::studio {
class Cooker;
}

namespace mu::app {

struct Context;

class StudioPlace {
public:
    // The world the source belongs to, read when it changes. Called each frame with the scene.
    void world(Context& ctx, const std::string& name);
    // The pointer: a click picks, a drag on the picked object moves it. True when the pointer
    // was the Place's this frame, so the camera does not orbit under it.
    bool pointer(Context& ctx, game::World& world, const gfx::Camera& camera, bool overPanels);
    // The window, in the inspector's place, and the mark over the picked object.
    void panel(Context& ctx, game::World& world, studio::Cooker& cooker, const gfx::Camera& camera,
               float x, float y, float w, float h);
    void drop() { picked_ = -1; }
    // For a run with nobody at the mouse (MU_STUDIO_DO): the `model` nearest `near` picked, and
    // the picked one moved by (dx, dz) metres and written, as a drag would.
    bool pickModel(game::World& world, const std::string& model, const float near[3]);
    void nudge(Context& ctx, game::World& world, studio::Cooker& cooker, float dx, float dz);

private:
    bool ray(Context& ctx, const gfx::Camera& camera, float origin[3], float direction[3]) const;
    bool groundUnder(Context& ctx, game::World& world, const gfx::Camera& camera, float at[3]) const;
    void match(game::World& world);
    void write(Context& ctx, game::World& world, studio::Cooker& cooker);
public:
    int picked() const { return picked_; }
    int line() const { return line_; }

private:

    std::string world_, file_;
    double perTile_ = 100.0;
    core::Json source_;
    int picked_ = -1;   // the town's instance
    int line_ = -1;     // its object in the source, or -1 when the source has none
    bool dragging_ = false;
    float grab_[3] = {0, 0, 0};   // the pointer's ground point less the object's, at the press
    float heightOver_ = 0.0f;     // the object's height above the land, kept while it walks
    float storedY_ = 0.0f;        // its height when its source line was last read or written
    std::string said_;
    bool saidOk_ = true;
};

}  // namespace mu::app
