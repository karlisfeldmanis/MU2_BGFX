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
// shown but not moved. A `scale` the source does not write is the 1.0 the cook assumes; the
// window's "add a scale" writes one in, after which it is dragged like the rest.
//
// Sprint G: an object added (a model of this world's source, at the view's middle), one
// duplicated (beside itself) and one deleted -- each a list operation of tools/mu/edit.py,
// written in its neighbours' style, journalled. An added object is a town instance at once,
// lit as its template; a deleted one is hidden. An add appends to the source's list and a
// delete takes its line out, so every line after it moves up one, here as in the file.
#pragma once

#include <string>
#include <vector>

#include "content/cooked.h"
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

// The pointer's ray, where it meets the land, and a point back on the screen: Place's, shared
// with its marks (app/modes/studio_marks.h).
bool pointerRay(Context& ctx, const gfx::Camera& camera, float origin[3], float direction[3]);
bool groundUnderPointer(Context& ctx, game::World& world, const gfx::Camera& camera, float at[3]);
bool toScreen(Context& ctx, const gfx::Camera& camera, const float point[3], float* x, float* y);
// A value of a source file back to JSON text, for an insert (studio_place.cpp says how whole
// numbers are written).
std::string jsonText(const core::Json& j, bool pointed, bool floats = false);

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
    // Sprint G's three, the window's buttons and the control words alike; each answers what it
    // did or why not. `add` stands `model` on the land at (x, z), picked; `duplicate` puts a
    // copy of the picked object a metre east of it, picked; `remove` takes the picked out.
    bool add(game::World& world, studio::Cooker& cooker, const std::string& model, float x, float z,
             std::string* answer);
    bool duplicate(game::World& world, studio::Cooker& cooker, std::string* answer);
    bool remove(game::World& world, studio::Cooker& cooker, std::string* answer);

private:
    void match(game::World& world);
    int lineOf(game::World& world, size_t instance) const;
    void write(Context& ctx, game::World& world, studio::Cooker& cooker);
    // The source object `o` appended to the source's list, and the town instance `one` with it.
    bool append(game::World& world, studio::Cooker& cooker, const core::Json& o,
                const content::TownInstance& one, std::string* answer);
    void addPanel(game::World& world, studio::Cooker& cooker, const gfx::Camera& camera);
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
    std::vector<std::string> models_;   // the source's models, each once: what Add offers
    int adding_ = 0;                    // the one chosen
};

}  // namespace mu::app
