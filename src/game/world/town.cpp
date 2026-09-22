#include "game/world/town.h"

#include <bx/math.h>
#include <algorithm>
#include <bx/timer.h>

#include "content/placement.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// The six planes of a frustum out of a row-vector view-projection, each as ax+by+cz+d.
// Standard Gribb-Hartmann: the rows of the matrix added to and subtracted from the w row.
struct Frustum {
    float plane[6][4];

    explicit Frustum(const float* m) {
        // Row-vector layout, so m[i*4 + j] is row i column j and the w column is j = 3.
        auto set = [&](int index, int column, float sign) {
            for (int row = 0; row < 4; ++row) {
                plane[index][row] = m[row * 4 + 3] + sign * m[row * 4 + column];
            }
        };
        set(0, 0, 1.0f);   // left
        set(1, 0, -1.0f);  // right
        set(2, 1, 1.0f);   // bottom
        set(3, 1, -1.0f);  // top
        // Metal's clip space is 0..1 in z, not -1..1, and bgfx says which at run time. The
        // near plane is therefore the w row alone rather than w + z; getting this wrong
        // culls everything in front of the eye, or nothing at all.
        set(4, 2, 1.0f);   // near (homogeneousDepth false: this is the z row itself)
        set(5, 2, -1.0f);  // far
        if (!bgfx::getCaps()->homogeneousDepth) {
            for (int row = 0; row < 4; ++row) plane[4][row] = m[row * 4 + 2];
        }
    }

    // A box is outside only when it is wholly behind one plane. The positive vertex test:
    // the corner furthest along the plane's normal is the last one to fall behind it.
    bool holds(const float* low, const float* high) const {
        for (const float* p : plane) {
            const float x = p[0] >= 0.0f ? high[0] : low[0];
            const float y = p[1] >= 0.0f ? high[1] : low[1];
            const float z = p[2] >= 0.0f ? high[2] : low[2];
            if (p[0] * x + p[1] * y + p[2] * z + p[3] < 0.0f) return false;
        }
        return true;
    }
};

}  // namespace

bool Town::readTable(const std::string& assetDir, const std::string& world) {
    const std::string dir = core::join(assetDir, "cooked/" + world);
    const std::string townPath = core::join(dir, world + ".mut");

    std::vector<uint8_t> bytes = core::readFile(townPath);
    if (bytes.empty()) {
        core::logf("no cooked town at %s -- the land will stand empty. tools/cook.py writes it",
                   townPath.c_str());
        return false;
    }
    std::string error;
    if (!content::parseCookedTown(bytes, town_, error)) {
        core::logError("%s: %s", townPath.c_str(), error.c_str());
        return false;
    }
    return true;
}

size_t Town::loadMeshes(const std::string& assetDir, const std::vector<bool>& wanted,
                        content::Textures& textures) {
    glowLevels_.assign(town_.instances.size(), 1.0f);
    paletteRows_.assign(town_.instances.size(), -1);
    meshes_.resize(town_.models.size());
    size_t failed = 0;
    std::string error;
    for (size_t i = 0; i < town_.models.size(); ++i) {
        if (!wanted.empty() && !wanted[i]) continue;
        const content::TownModel& model = town_.models[i];
        std::vector<uint8_t> meshBytes = core::readFile(core::join(assetDir, model.mesh));
        content::CookedMesh cooked;
        if (meshBytes.empty() || !content::parseCookedMesh(meshBytes, cooked, error)) {
            core::logError("%s: %s", model.mesh.c_str(),
                           meshBytes.empty() ? "is not there" : error.c_str());
            ++failed;
            continue;
        }
        if (!meshes_[i].buildFromCooked(cooked, model.name, assetDir, textures)) {
            ++failed;
            continue;
        }
        triangles_ += meshes_[i].triangleCount() * model.instances;
    }
    return failed;
}

bool Town::open(const std::string& assetDir, const std::string& world,
                content::Textures& textures) {
    const int64_t started = bx::getHPCounter();
    if (!readTable(assetDir, world)) return false;
    const size_t failed = loadMeshes(assetDir, {}, textures);

    loadSeconds_ = double(bx::getHPCounter() - started) / double(bx::getHPFrequency());
    core::logf("town %s: %zu placements of %zu models in %zu chunks of %u tiles, "
               "%u triangles if all of it is drawn, %zu models failed, %.2f s",
               world.c_str(), town_.instances.size(), town_.models.size(), town_.chunks.size(),
               town_.chunkTiles, triangles_, failed, loadSeconds_);
    return failed == 0;
}

bool Town::openStage(const std::string& assetDir, const std::string& world,
                     const std::vector<StagePlacement>& placements,
                     content::Textures& textures) {
    if (!readTable(assetDir, world)) return false;
    // The map's placements go; its models, emitters and glows stay, because those are what a
    // placement is resolved through.
    town_.instances.clear();
    std::vector<bool> wanted(town_.models.size(), false);
    content::TownChunk chunk;
    for (int i = 0; i < 3; ++i) {
        chunk.min[i] = 1e30f;
        chunk.max[i] = -1e30f;
    }
    for (content::TownModel& model : town_.models) model.instances = 0;
    for (const StagePlacement& one : placements) {
        size_t found = town_.models.size();
        for (size_t i = 0; i < town_.models.size(); ++i) {
            if (town_.models[i].name == one.model) found = i;
        }
        if (found == town_.models.size()) {
            core::logError("stage: %s has no cooked model named %s", world.c_str(),
                           one.model.c_str());
            continue;
        }
        content::TownInstance instance{};
        for (int i = 0; i < 3; ++i) instance.position[i] = one.position[i];
        instance.yaw = one.yaw;
        instance.scale = one.scale;
        instance.model = uint16_t(found);
        instance.light[0] = instance.light[1] = instance.light[2] = 255;
        town_.instances.push_back(instance);
        town_.models[found].instances += 1;
        wanted[found] = true;
        // A box generous enough for anything a stage stands up; the stage is one chunk and
        // is never culled against anything that matters.
        for (int i = 0; i < 3; ++i) {
            chunk.min[i] = std::min(chunk.min[i], one.position[i] - 10.0f);
            chunk.max[i] = std::max(chunk.max[i], one.position[i] + 10.0f);
        }
    }
    chunk.instanceCount = uint32_t(town_.instances.size());
    town_.chunks.assign(1, chunk);
    const size_t failed = loadMeshes(assetDir, wanted, textures);
    core::logf("stage: %zu placements of %s's models, %zu failed", town_.instances.size(),
               world.c_str(), failed);
    return !town_.instances.empty() && failed == 0;
}

void Town::shutdown() {
    for (content::Mesh& mesh : meshes_) mesh.shutdown();
    meshes_.clear();
    town_ = content::CookedTown();
    glowLevels_.clear();
    paletteRows_.clear();
    triangles_ = 0;
}

void Town::append(const content::TownInstance& instance, std::vector<gfx::Drawable>& out) {
    if (instance.model >= meshes_.size()) return;
    // Bit 1 is a roof. tools/cook.py.
    if (roofsHidden_ && (instance.flags & 2) != 0) return;
    const content::Mesh& mesh = meshes_[instance.model];
    if (!bgfx::isValid(mesh.vertexBuffer())) return;

    gfx::Drawable drawable;
    drawable.mesh = &mesh;
    // content/placement.cpp, which is MU's own (Z * Y) * X in our axes, with a test
    // against MuMain's AngleMatrix beside it. Not bx::mtxSRT: that composes the three the
    // other way round and turns each of them the opposite way.
    content::placementTransform(instance.pitch, instance.yaw, instance.roll, instance.scale,
                                instance.position, drawable.transform);

    // The cook stored MU's light as bytes; the shader wants it linear. It is a lit result
    // and not an albedo, so it does NOT go through the sRGB curve -- the same rule
    // docs/conventions.md states for light.png on the ground.
    for (int i = 0; i < 3; ++i) drawable.light[i] = float(instance.light[i]) / 255.0f;
    const size_t index = size_t(&instance - town_.instances.data());
    drawable.light[3] = glowLevels_[index];
    // -1 (the default) draws the bind pose, which is right for every placement Sway does not
    // reach -- the town's rigged models that do not sway yet, and everything else, drew this
    // way from the day the cook started writing them skinned. See Renderer::kBindRow.
    drawable.paletteRow = paletteRows_[index];
    out.push_back(drawable);
}

void Town::gatherAll(std::vector<gfx::Drawable>& out, bool asCasters) {
    TownCounts& counts = asCasters ? casterCounts_ : counts_;
    counts = TownCounts();
    out.reserve(out.size() + town_.instances.size());
    for (const content::TownInstance& instance : town_.instances) append(instance, out);
    counts.chunksDrawn = uint32_t(town_.chunks.size());
    counts.instancesDrawn = uint32_t(town_.instances.size());
}

void Town::gatherVisible(const float* viewProj, std::vector<gfx::Drawable>& out) {
    counts_ = TownCounts();
    const Frustum frustum(viewProj);
    for (const content::TownChunk& chunk : town_.chunks) {
        if (!frustum.holds(chunk.min, chunk.max)) {
            ++counts_.chunksCulled;
            counts_.instancesCulled += chunk.instanceCount;
            continue;
        }
        ++counts_.chunksDrawn;
        counts_.instancesDrawn += chunk.instanceCount;
        for (uint32_t i = 0; i < chunk.instanceCount; ++i) {
            append(town_.instances[chunk.firstInstance + i], out);
        }
    }
}

}  // namespace mu::game
