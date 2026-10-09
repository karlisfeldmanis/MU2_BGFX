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
#include "gfx/casters.h"
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

// One thing a stage stands up, by the cooked model's name. Metres and radians.
struct StagePlacement {
    std::string model;
    float position[3] = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    float scale = 1.0f;
};

class Town {
public:
    bool open(const std::string& assetDir, const std::string& world, content::Textures& textures);
    // A town of a few placements rather than a map's worth: the world's own cooked models,
    // emitters and glows, standing where `placements` says and nowhere else. The viewer's
    // stage is this -- a bonfire, a lamp, a wall -- and because it is a Town, the lamps, the
    // glows, the flicker and the fire are built from it by exactly the code the game uses.
    // Lit white: the stage's plot has no baked light to give them. Only the models named are
    // loaded.
    bool openStage(const std::string& assetDir, const std::string& world,
                   const std::vector<StagePlacement>& placements, content::Textures& textures);
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
    void setRoofsHidden(bool hidden) {
        if (hidden == roofsHidden_) return;
        roofsHidden_ = hidden;
        for (uint32_t i = 0; i < town_.instances.size(); ++i) {
            if ((town_.instances[i].flags & 2) != 0) markDirty(i);
        }
    }
    bool roofsHidden() const { return roofsHidden_; }

    // The cooked table itself, for what reads the placements without drawing them: the
    // lamps resolve each light through the placement that carries it.
    const content::CookedTown& cooked() const { return town_; }
    // How bright a placement's glow parts draw this frame, MU's BlendMeshLight: 1 unless the
    // lamps flicker it. Rides in the instance's fifth vec4 .w, which fs_glow reads.
    void setGlowLevel(uint32_t instance, float level) {
        if (instance < glowLevels_.size() && glowLevels_[instance] != level) {
            glowLevels_[instance] = level;
            markDirty(instance);
        }
    }
    // Which row of the bone palette a placement poses against this frame, or -1 for its bind
    // pose -- the same idea as setGlowLevel, and set the same way: something outside Town
    // computes it (Sway, for now) and writes it in before the town is gathered. Town knows
    // nothing about clips or clocks, only that a row was asked for.
    void setPaletteRow(uint32_t instance, int row) {
        if (instance >= paletteRows_.size() || paletteRows_[instance] == row) return;
        // The run's `posed` is a count of its posed placements, kept as they change.
        if (instance < residentBatchOf_.size() && residentBatchOf_[instance] != kNoSlot) {
            uint32_t& posed = posedIn_[residentBatchOf_[instance]];
            if (paletteRows_[instance] >= 0) --posed;
            if (row >= 0) ++posed;
        }
        paletteRows_[instance] = row;
        markDirty(instance);
    }
    // A placement stood somewhere else this frame: Devias's doors (game/world/doors.h). The
    // chunk bounds are the cook's and stay; a door moves under a metre, a gate under four.
    void movePlacement(uint32_t instance, float yaw, const float position[3]) {
        if (instance >= town_.instances.size()) return;
        town_.instances[instance].yaw = yaw;
        for (int a = 0; a < 3; ++a) town_.instances[instance].position[a] = position[a];
        markDirty(instance);
    }
    // A placement left out of the frame and the sun's list, or put back: Blood Castle's
    // drawbridge, whose door goes as its lowered deck comes (game/world/drawbridge.h). Bit 3 of
    // the flags, which the cook sets on what it holds back for later (tools/cook.py SHOWN_LATER).
    void setHidden(uint32_t instance, bool hidden) {
        if (instance >= town_.instances.size()) return;
        uint16_t& flags = town_.instances[instance].flags;
        const uint16_t was = flags;
        flags = hidden ? uint16_t(flags | 8) : uint16_t(flags & ~8);
        if (flags != was) markDirty(instance);
    }
    // The same with the piece tumbling as well: the Lost Tower's kicked skulls
    // (game/world/skulls.h). Radians, the cook's pitch and roll.
    void posePlacement(uint32_t instance, float pitch, float yaw, float roll,
                       const float position[3]) {
        if (instance >= town_.instances.size()) return;
        town_.instances[instance].pitch = pitch;
        town_.instances[instance].roll = roll;
        movePlacement(instance, yaw, position);
    }
    // The mesh a model index owns, for whoever poses a placement against it: Sway reads a
    // rig's bones off this the same way `append` reads its vertex and index buffers.
    const content::Mesh* meshAt(size_t model) const {
        return model < meshes_.size() ? &meshes_[model] : nullptr;
    }

    // **The sun's casters, kept on the GPU** (2026-10-04, the user: "lets continue to use GPU
    // on processes where we can"). Every placement's instance lives in one buffer, grouped by
    // model, built on the first call; this uploads only the placements a setter above changed
    // since (a lamp's flicker, a sway's row, a door, a kicked skull), and hands back the runs
    // for Renderer::setResidentCasters. gatherAll(out, true) is the same list rebuilt on the
    // CPU every frame, which the lobby and the benches still use. A placement hidden (a roof,
    // a held-back piece) is a zero matrix in its slot, which draws nothing.
    const std::vector<gfx::Renderer::ResidentBatch>& residentCasters();
    // And their solid parts culled and drawn by the GPU (gfx/casters.h), built with them on
    // residentCasters()'s first call. For Renderer::setGpuCasters, after residentCasters().
    const gfx::GpuCasters& gpuCasters() const { return casters_; }

    const TownCounts& counts() const { return counts_; }
    const TownCounts& casterCounts() const { return casterCounts_; }
    size_t modelCount() const { return meshes_.size(); }
    size_t instanceCount() const { return town_.instances.size(); }
    size_t chunkCount() const { return town_.chunks.size(); }
    uint32_t triangleCount() const { return triangles_; }
    double loadSeconds() const { return loadSeconds_; }

private:
    bool readTable(const std::string& assetDir, const std::string& world);
    // Builds the meshes of the models `wanted` marks, or of every model when it is empty.
    // `forGpu` hands each static model's geometry to casters_ as well: the map's town, and
    // not a stage's.
    size_t loadMeshes(const std::string& assetDir, const std::vector<bool>& wanted,
                      content::Textures& textures, bool forGpu = false);
    void append(const content::TownInstance& instance, std::vector<gfx::Drawable>& out);
    // The drawable a placement is this frame, or false when it is not drawn at all.
    bool drawableOf(const content::TownInstance& instance, gfx::Drawable& out) const;
    void buildResident();
    void markDirty(uint32_t instance) {
        if (instance >= isDirty_.size() || isDirty_[instance]) return;
        isDirty_[instance] = 1;
        dirty_.push_back(instance);
    }
    static constexpr uint32_t kNoSlot = 0xffffffffu;

    content::CookedTown town_;
    std::vector<content::Mesh> meshes_;
    TownCounts counts_;
    TownCounts casterCounts_;
    uint32_t triangles_ = 0;
    bool roofsHidden_ = false;
    std::vector<float> glowLevels_;
    std::vector<int> paletteRows_;
    double loadSeconds_ = 0.0;
    // residentCasters()'s: the buffer, each placement's slot in it and run, the runs, how
    // many of each run are posed, and the placements changed since the last upload.
    bgfx::DynamicVertexBufferHandle resident_ = BGFX_INVALID_HANDLE;
    std::vector<uint32_t> residentSlot_;
    std::vector<uint32_t> residentBatchOf_;
    std::vector<gfx::Renderer::ResidentBatch> residentBatches_;
    std::vector<uint32_t> posedIn_;
    std::vector<float> records_;
    std::vector<uint32_t> dirty_;
    std::vector<uint8_t> isDirty_;
    gfx::GpuCasters casters_;
};

}  // namespace mu::game
