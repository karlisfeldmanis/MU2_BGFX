// The town's own animation: MU's MoveObject plays a rig-and-clip animation on twenty of
// Lorencia's placed models -- the trees, the street lamp's arm, the candles, the hanging inn
// sign, curtains, the tents, the carriages and the merchants' animals, two houses and two
// stretches of wall, the fountain's spout -- at the per-type `o->Velocity` CreateObject gives
// them in ZzzObject.cpp. Each carries its one clip in the same glb as its mesh, and
// tools/cook.py's cook_world_clip bakes every one of them into cooked/<world>/clips.json.
//
// The rate is already in the clip: MU2's exporter wrote each at its own velocity (0.16 by
// default, 0.3 for a lamp, a candle and a sign -- `play_speed` in its recipe), so every model
// plays at its baked rate but two. Tree01 and Tree02 take `1/Scale * 0.4`, the one velocity
// that is not a constant, and their clip was baked at scale 1: they play at 1/scale of it.
// And the treasure chest takes `o->Velocity = 0.f` -- MU draws it on its first key and never
// runs the rest (a lid opening and shutting); clips.json's `still` names it.
//
// Reuses `game::Figure` exactly as Crowd does: one FigureBody per animated MODEL (not per
// placement -- the mesh, the clip library and the bone mapping are the model's, and every
// placement of it just plays the same clip at its own clock), one Figure per PLACEMENT.
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
    // placements, and stands up one Figure for every placement of a model the cook found a
    // clip for. A model with no clip is left exactly as Town already draws it -- bind pose,
    // not an error.
    bool open(const std::string& assetDir, const std::string& world, const Town& town);
    void shutdown();

    // Steps every placement's clock by its own rate and poses the ones `viewProj` can see,
    // writing each row into `town`; a placement out of sight keeps its clock running and
    // stands in the bind row, as MU only moves the objects in the blocks it draws. Called
    // once a frame, before the town is gathered, the same order Lamps' flicker runs in.
    // A null `viewProj` poses everything.
    void update(float seconds, const float* viewProj, gfx::Renderer& renderer, Town& town);

    size_t swayingCount() const { return instances_.size(); }
    // The figure standing at town instance `townIndex` if it was posed on the last update --
    // in sight, so its bones are where they are drawn -- or null. What rides a bone (the
    // fountain's spray, a lantern's glow: game/ornaments.h) asks here, and is only thrown
    // where MU would have drawn the object at all.
    const Figure* posedAt(uint32_t townIndex) const {
        if (townIndex >= slotOf_.size() || slotOf_[townIndex] < 0) return nullptr;
        const Instance& instance = instances_[size_t(slotOf_[townIndex])];
        return instance.posed ? &instance.figure : nullptr;
    }
    // How many were posed on the last update -- the palette rows this spent.
    size_t posedCount() const { return posed_; }

private:
    // One rig this file knows how to play: the model's own mesh and clip library, and the
    // bone mapping between them -- identity, because a world object's clip is baked from the
    // very same glb as its mesh, in the very same joint order. Kept behind unique_ptr because
    // FigureBody keeps a raw ClipLibrary* and a map rehashing out from under either would
    // dangle it.
    struct Model {
        std::unique_ptr<ClipLibrary> library;
        std::unique_ptr<FigureBody> body;
    };
    // One placement playing one of the above, and which town instance it writes its row into.
    struct Instance {
        Figure figure;
        uint32_t townIndex = 0;
        // The clip seconds a real second is worth: 1/scale for Tree01 and Tree02, 0 for a
        // still model, 1 for everything else. Decided once at open().
        float clipRate = 1.0f;
        // What the frustum is asked about: the bind box's centre, placed, and a radius wide
        // enough for the shadow it throws into the frame from just outside it.
        float centre[3] = {0, 0, 0};
        float radius = 1.0f;
        bool posed = false;  // on the last update
    };

    std::unordered_map<std::string, Model> models_;
    std::vector<Instance> instances_;
    std::vector<int32_t> slotOf_;  // town instance -> index into instances_, or -1
    std::vector<float> scratch_;  // kMaxBones x 12, reused every pose so nothing allocates
    size_t posed_ = 0;
};

}  // namespace mu::game
