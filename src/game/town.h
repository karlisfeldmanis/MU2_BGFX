// What stands on the land: 2753 placements of 105 models, read from the cook's .mut.
//
// The town owns no rendering. It hands the renderer a list of drawables, which the renderer
// already turns into one instanced draw per model. What this class exists to decide is
// *which* placements go into that list, and foundation 7 of PLAN.md says how: by chunk, with
// the camera and the sun tested separately, against bounds settled at cook time rather than
// per frame.
#pragma once

#include <string>
#include <vector>

#include "content/cooked.h"
#include "content/mesh.h"
#include "content/texture.h"
#include "gfx/renderer.h"

namespace mu::game {

// What a gather did, for the log and for the sprint file. Both passes are counted because
// culling the shadow pass with the camera's frustum is the bug foundation 7 names, and the
// only way to see it is that the two numbers move together when they should not.
struct TownCounts {
    uint32_t chunksDrawn = 0;
    uint32_t chunksCulled = 0;
    uint32_t instancesDrawn = 0;
    uint32_t instancesCulled = 0;
};

class Town {
public:
    bool open(const std::string& assetDir, const std::string& world, content::Textures& textures);
    void shutdown();

    bool isOpen() const { return !meshes_.empty(); }

    // Everything, in cook order: the baseline the culling is measured against, and what
    // the sun's split draws while its own culling is not built. `asCasters` keeps the two
    // tallies apart -- gathering the casters after the camera used to overwrite the
    // camera's counts, and the log then reported that nothing had been culled while 82% of
    // the town was being left out of the frame.
    void gatherAll(std::vector<gfx::Drawable>& out, bool asCasters = false);

    // Only the chunks whose box survives the camera's frustum.
    void gatherVisible(const float* viewProj, std::vector<gfx::Drawable>& out);

    // Whether the roofs are drawn: gone or there, never between, and from the sun's list as
    // well as the camera's, so a hidden roof takes its shadow with it. MU2's World.Step,
    // which tried a tenth-of-a-second fade first and found a half-transparent roof worse than
    // none -- the room and the roof both seen through each other, in the very frames the
    // doorway matters.
    void setRoofsHidden(bool hidden) { roofsHidden_ = hidden; }
    bool roofsHidden() const { return roofsHidden_; }

    // The cooked table itself, for what reads the placements without drawing them: the
    // lamps resolve each light through the placement that carries it.
    const content::CookedTown& cooked() const { return town_; }
    // How bright a placement's glow parts draw this frame, MU's BlendMeshLight: 1 unless the
    // lamps flicker it. Rides in the instance's fifth vec4 .w, which fs_glow reads.
    void setGlowLevel(uint32_t instance, float level) {
        if (instance < glowLevels_.size()) glowLevels_[instance] = level;
    }

    const TownCounts& counts() const { return counts_; }
    const TownCounts& casterCounts() const { return casterCounts_; }
    size_t modelCount() const { return meshes_.size(); }
    size_t instanceCount() const { return town_.instances.size(); }
    size_t chunkCount() const { return town_.chunks.size(); }
    uint32_t triangleCount() const { return triangles_; }
    double loadSeconds() const { return loadSeconds_; }

private:
    void append(const content::TownInstance& instance, std::vector<gfx::Drawable>& out);

    content::CookedTown town_;
    std::vector<content::Mesh> meshes_;
    TownCounts counts_;
    TownCounts casterCounts_;
    uint32_t triangles_ = 0;
    bool roofsHidden_ = false;
    std::vector<float> glowLevels_;
    double loadSeconds_ = 0.0;
};

}  // namespace mu::game
