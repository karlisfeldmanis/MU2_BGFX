// The studio's Stage (docs/plans/order-plan.md §9c, F3b): a neutral place to work on a thing, in
// place of the town round the bonfire.
//
// The user, 2026-10-12, picked "a stage and the world": the world to judge a thing in the
// game's own surroundings, and this to work on it. So nothing here moves or flickers -- no
// fire, no wind, no grass -- and a before and after shows only what the cook changed. It is
// lit by the game's own sheets, as everything is, so noon, dusk and night still read true.
//
// What stands on it: a dark floor out past the edge of any frame the bench's camera takes, a
// lighter plinth under the subject, a grey ball (a rough 18% grey: what the light itself is)
// and a chrome ball (what the light and the sky reflect), and a 1.8 m post for scale. Not
// white anything: the bench's white plane read brighter than any ground in the game
// (game/bench.cpp, raiseWorldGround), and a plinth that bright would do the same.
#pragma once

#include <vector>

#include <bgfx/bgfx.h>

#include "content/mesh.h"
#include "content/texture.h"
#include "gfx/renderer.h"

namespace mu::game {

class StudioStage {
public:
    bool open(content::Textures& textures);
    bool isOpen() const { return open_; }
    // Lays the set round `stand`, where the subject's feet are, the balls and the post to
    // its sides as the studio's camera first looks at it.
    void place(const float stand[3]);
    const std::vector<gfx::Drawable>& drawables() const { return drawables_; }
    void shutdown();

private:
    bgfx::TextureHandle grey(uint8_t value);

    bool open_ = false;
    content::Mesh floor_, plinth_, ball_, chrome_, post_;
    std::vector<bgfx::TextureHandle> owned_;
    std::vector<gfx::Drawable> drawables_;
};

}  // namespace mu::game
