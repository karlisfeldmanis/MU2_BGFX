// The studio's ground painting (docs/plans/order-plan.md §9c, sprint G): a world's tiles.png painted
// in Place, a brush of tiles at a time.
//
// tiles.png is MU's terrain mapping (pipeline/terrain.py): red the base slot, green the
// overlay slot, blue how far the overlay is painted over the base. A slot is an index into the
// world's `tile_slots` (<w>.json). The brush sets the base (and clears the overlay off it) or
// paints the overlay over the base at a strength, in a round of `radius` tiles; a left drag on
// the land paints, and the right drag orbits the camera in its place.
//
// The ground the game draws is a mesh cooked from the grid, its blend baked into the vertex
// colours, so a stroke cannot look real until `mu make ground <w>` -- the card's target, a
// heavy cook (the tile sheets, the ground, the index, the world's textures and placements and
// every world's tables). Until then each painted tile is tinted over the land in its slot's
// colour, the legend in the window. A stroke is written when let go (tools/mu/paint.py through
// mu serve), one journal line with every tile's old and new values, and Undo takes the last
// stroke back exactly.
#pragma once

#include <array>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "gfx/renderer.h"

namespace mu::game {
class World;
}
namespace mu::studio {
class Cooker;
}

namespace mu::app {

struct Context;

class StudioPaint {
public:
    void world(Context& ctx, const std::string& name);
    std::string target() const { return "ground " + world_; }
    // A left drag paints the brush's tiles; true while it does, so the camera keeps still.
    bool pointer(Context& ctx, game::World& world, const gfx::Camera& camera, bool overPanels);
    void panel(Context& ctx, game::World& world, studio::Cooker& cooker, const gfx::Camera& camera,
               float x, float y, float w, float h);
    // The ground was cooked and read again: the tint goes, the land shows it.
    void cooked() { strokes_.clear(), stroke_.clear(); }

    // For the control words: the brush (`slot`, overlay or base, alpha, radius), a stroke at a
    // tile written at once, and the last stroke taken back.
    void brush(int slot, bool overlay, int alpha, int radius);
    bool stroke(studio::Cooker& cooker, int column, int row, std::string* answer);
    bool undo(studio::Cooker& cooker, std::string* answer);

private:
    using Tile = std::pair<int, int>;
    using Value = std::array<int, 3>;   // layer1, layer2, alpha; -1 keeps
    void dab(int column, int row);
    bool write(studio::Cooker& cooker, std::string* answer);
    void draw(Context& ctx, game::World& world, const gfx::Camera& camera) const;

    std::string world_;
    std::vector<std::string> slotNames_;   // by slot
    int slot_ = 0, alpha_ = 160, radius_ = 1;
    bool overlay_ = false;
    bool painting_ = false, released_ = false;
    int hover_[2] = {-1, -1};
    std::map<Tile, Value> stroke_;                  // the one being dragged
    std::vector<std::map<Tile, Value>> strokes_;    // written, until the ground is cooked
    std::string said_;
    bool saidOk_ = true;
};

}  // namespace mu::app
