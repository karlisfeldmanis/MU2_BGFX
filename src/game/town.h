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
    double loadSeconds_ = 0.0;
};

}  // namespace mu::game
