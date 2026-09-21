#include "game/town.h"

#include <bx/math.h>
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

bool Town::open(const std::string& assetDir, const std::string& world,
                content::Textures& textures) {
    const int64_t started = bx::getHPCounter();
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

    meshes_.resize(town_.models.size());
    size_t failed = 0;
    for (size_t i = 0; i < town_.models.size(); ++i) {
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

    loadSeconds_ = double(bx::getHPCounter() - started) / double(bx::getHPFrequency());
    core::logf("town %s: %zu placements of %zu models in %zu chunks of %u tiles, "
               "%u triangles if all of it is drawn, %zu models failed, %.2f s",
               world.c_str(), town_.instances.size(), town_.models.size(), town_.chunks.size(),
               town_.chunkTiles, triangles_, failed, loadSeconds_);
    return failed == 0;
}

void Town::shutdown() {
    for (content::Mesh& mesh : meshes_) mesh.shutdown();
    meshes_.clear();
    town_ = content::CookedTown();
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
    drawable.light[3] = 1.0f;
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
