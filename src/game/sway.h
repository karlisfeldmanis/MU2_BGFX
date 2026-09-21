// The town's trees, in the wind: MU's MoveObject plays a rig-and-clip animation on twenty of
// Lorencia's placed models -- trees, the street lamp's arm, the hanging inn sign, curtains,
// carriages -- traced to ZzzObject.cpp's per-type `o->Velocity`. Sway is the first and, for
// now, the only reader of that: trees alone, the biggest count (273 placements across five
// models) and the simplest case, one clip apiece and no crossfade. The other nineteen models
// already cook a clip (tools/cook.py's cook_world_clip runs on any world object with one) and
// stand today in their bind pose, same as they did before this file existed -- Town's own
// note on kBindRow says why that is correct rather than broken.
//
// Reuses `game::Figure` exactly as Crowd does: one FigureBody per animated MODEL (not per
// placement -- the mesh, the clip library and the bone mapping are the model's, and every
// placement of it just plays the same clip at its own clock), one Figure per PLACEMENT,
// because a tree's own scale changes how fast it plays and 273 trees do not share a scale.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/crowd.h"
#include "game/figures.h"
#include "game/town.h"
#include "gfx/renderer.h"

namespace mu::game {

class Sway {
public:
    // Reads `clips.json` beside the world's cooked town, matches it against `town`'s own
    // placements, and stands up one Figure for every placement of a model both this file
    // knows how to play (today: the five tree models) and the cook found a clip for. A
    // model with no clip, or a clip this build does not yet play, is left exactly as Town
    // already draws it -- bind pose, not an error.
    bool open(const std::string& assetDir, const std::string& world, const Town& town);
    void shutdown();

    // Steps every standing tree's clock by its own rate -- MU's `o->Velocity`, scale-
    // dependent for Tree01 and Tree02 and traced to ZzzObject.cpp's switch on WD_0LORENCIA
    // -- poses it, and writes the row into `town`. Called once a frame, before the town is
    // gathered, the same order Lamps' flicker runs in.
    void update(float seconds, gfx::Renderer& renderer, Town& town);

    size_t swayingCount() const { return instances_.size(); }

private:
    // One rig this file knows how to play: the model's own mesh and clip library, and the
    // bone mapping between them -- identity, because a world object's clip is baked from the
    // very same glb as its mesh, in the very same joint order. Kept in a deque-like stable
    // container (unique_ptr) because FigureBody keeps a raw ClipLibrary* and a std::vector
    // resizing out from under either would dangle it.
    struct Model {
        std::unique_ptr<ClipLibrary> library;
        std::unique_ptr<FigureBody> body;
    };
    // One placement playing one of the above, and which town instance it writes its row into.
    struct Instance {
        Figure figure;
        uint32_t townIndex = 0;
        // MU's `o->Scale`, kept here rather than re-read from the instance every frame: the
        // rate a placement plays at is a property of ITS OWN scale, decided once at open().
        float scale = 1.0f;
        bool scaleDependent = false;  // Tree01, Tree02: ZzzObject.cpp's 1/Scale * 0.4
    };

    std::unordered_map<std::string, Model> models_;
    std::vector<Instance> instances_;
    std::vector<float> scratch_;  // kMaxBones x 12, reused every pose so nothing allocates
};

}  // namespace mu::game
