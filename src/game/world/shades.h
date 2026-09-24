// The shade under Lorencia's bridges: the one mesh in the town MU draws blended.
//
// Bridge01 and BridgeStone01 each carry a second mesh, `bridge_shadow01`: one quad hanging
// from just under the deck to a metre and a half below it, the length of the span, painted
// black at the top and fading to nothing at the bottom. MU draws it like any other mesh
// whose sheet has alpha -- EnableAlphaTest's source-alpha blend, two-sided, alpha kept above
// 0.25 -- and from MU's camera it is what stands between the eye and the water seen through
// under the span. Without it the river runs on under the deck as a lit wall of water.
//
// The recipes skip it out of the model (source/world/lorencia/Bridge01.json, `skip_sheets`),
// because the opaque pass has no blend and the material model is kept closed. So it is laid
// here instead, in the transparent pass, at every placement of either model, from the quad's
// own corners in the .bmd. Nothing here reaches the sim.
#pragma once

#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Town;

class Shades {
public:
    // Finds every Bridge01 and BridgeStone01 among `town`'s placements and lays its quad in
    // the world. The sheet is the showing's `bridge_shadow`; without it nothing is drawn and
    // the log says so.
    bool open(const std::string& assetDir, const Town& town, content::Textures& textures);
    void shutdown();

    // The quads within `kMetres` of `near`, into the transparent pass.
    void gather(gfx::Effects& effects, const float near[3]) const;

    size_t count() const { return quads_.size(); }

    static constexpr float kMetres = 60.0f;

private:
    struct Quad {
        float centre[3] = {0, 0, 0};
        float corner[4][3] = {};
    };
    std::vector<Quad> quads_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
