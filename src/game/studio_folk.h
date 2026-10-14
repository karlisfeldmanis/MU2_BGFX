// The townsfolk standing in the studio's world (docs/plans/order-plan.md §9c, sprint G): each
// townsperson of the world's manifest, content/world/<w>/world.json `npcs`, stood where it says
// and turned the way it looks, by the lines Play stands them with (game/play_open.cpp): the
// tile's middle, the look's yaw, the body's scale (Tersia at 0.93), the idle `stand` chooses.
//
// The manifest, not the cooked tables: Place moves a townsperson in the source, and the figure
// follows it at once, before `mu make tables <w>` puts it in the game. The file's date is
// looked at twice a second, so an edit from the agent's `mu edit` moves them too.
//
// Not the realm's: a guard here stands at its post rather than walking its round, and nobody
// speaks or is clicked. One with no figure ("" -- Hanzo, Pasi, Baz) is a placement of the town
// and already drawn by it.
#pragma once

#include <string>
#include <vector>

#include "game/crowd.h"
#include "gfx/renderer.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

class Figures;

class StudioFolk {
public:
    // Reads `root`/source/world/`world`/world.json again when it is another world or the file
    // has changed since, and stands them all anew. `seconds` is the frame's clock.
    void follow(const std::string& root, const std::string& world, const Figures& figures,
                const content::Ground& ground, double seconds);
    void update(float deltaSeconds);
    // Poses each into the renderer's palette and appends its parts.
    void gather(gfx::Renderer& renderer, std::vector<gfx::Drawable>& out);
    void clear();
    size_t count() const { return folk_.size(); }

private:
    std::string world_;
    long long stamp_ = -1;    // the manifest's date when it was last read
    uint32_t generation_ = 0; // the figures' generation the folk were stood in
    double lookedAt_ = -1.0;  // when the date was last looked at
    std::vector<Figure> folk_;
    std::vector<float> scratch_;
};

}  // namespace mu::game
