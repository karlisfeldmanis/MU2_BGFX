// The engine's own reader, against the cook's own output, with no window in the way.
//
// This is the first thing in tests/ and it is here because it can be: parsing was kept
// apart from uploading precisely so that the half which decides whether the town is right
// can be run in a tenth of a second without a device. cookcheck reads the files with a
// second implementation, which catches a cook that wrote nonsense; this reads them with
// the engine's, which catches a reader that disagrees with the cook. Neither one alone
// would have caught both.
//
//     cmake --build build --target cooked_test && build/cooked_test
//
// Exits non-zero on the first thing that is not so.

#include "content/cooked.h"
#include "core/files.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("  FAIL %s\n", what.c_str());
        ++g_failures;
    }
}

}  // namespace

int main() {
    const std::string cooked = std::string(MU2_ASSET_DIR) + "/cooked/lorencia";

    // --- the town ----------------------------------------------------------------------
    std::vector<uint8_t> bytes = mu::core::readFile(cooked + "/lorencia.mut");
    if (bytes.empty()) {
        std::printf("cooked_test: no %s/lorencia.mut -- run tools/cook.py first\n",
                    cooked.c_str());
        return 2;
    }

    mu::content::CookedTown town;
    std::string error;
    if (!mu::content::parseCookedTown(bytes, town, error)) {
        std::printf("  FAIL the town did not parse: %s\n", error.c_str());
        return 1;
    }
    std::printf("cooked_test: %zu placements, %zu chunks of %u tiles, %zu models, %u tiles a side\n",
                town.instances.size(), town.chunks.size(), town.chunkTiles, town.models.size(),
                town.size);

    check(town.metresPerTile == 1.0f, "one tile is one metre, as docs/conventions.md says");
    check(town.size % town.chunkTiles == 0 || town.chunks.size() > 0, "the chunk grid covers the map");

    // Every model's own count agrees with the instances that name it, which is the table a
    // loader will size its buffers from.
    std::vector<uint32_t> counted(town.models.size(), 0);
    for (const mu::content::TownInstance& instance : town.instances) ++counted[instance.model];
    for (size_t i = 0; i < town.models.size(); ++i) {
        check(counted[i] == town.models[i].instances,
              town.models[i].name + " is placed " + std::to_string(counted[i]) +
                  " times and its table says " + std::to_string(town.models[i].instances));
    }

    // Sorted by model inside a chunk: the property that makes an instanced draw a range.
    for (const mu::content::TownChunk& chunk : town.chunks) {
        uint16_t previous = 0;
        for (uint32_t i = 0; i < chunk.instanceCount; ++i) {
            const uint16_t model = town.instances[chunk.firstInstance + i].model;
            check(model >= previous, "the models in a chunk are sorted");
            previous = model;
            if (g_failures) break;
        }
        if (g_failures) break;
    }

    // Every placement stands inside the box its own chunk claims, or the culling will drop
    // something it can see.
    size_t outside = 0;
    for (const mu::content::TownChunk& chunk : town.chunks) {
        for (uint32_t i = 0; i < chunk.instanceCount; ++i) {
            const mu::content::TownInstance& instance = town.instances[chunk.firstInstance + i];
            for (int axis = 0; axis < 3; ++axis) {
                if (instance.position[axis] < chunk.min[axis] ||
                    instance.position[axis] > chunk.max[axis]) {
                    ++outside;
                    break;
                }
            }
        }
    }
    check(outside == 0, std::to_string(outside) + " placements stand outside their chunk's box");

    // --- every model the town names ------------------------------------------------------
    size_t triangles = 0;
    size_t meshes = 0;
    for (const mu::content::TownModel& model : town.models) {
        std::vector<uint8_t> meshBytes =
            mu::core::readFile(std::string(MU2_ASSET_DIR) + "/" + model.mesh);
        if (meshBytes.empty()) {
            check(false, model.name + "'s mesh is missing: " + model.mesh);
            continue;
        }
        mu::content::CookedMesh mesh;
        if (!mu::content::parseCookedMesh(meshBytes, mesh, error)) {
            check(false, model.name + " did not parse: " + error);
            continue;
        }
        ++meshes;
        triangles += mesh.indices.size() / 3;
        // The bounds the town carries for culling are the mesh's own, or a chunk's box is
        // a fiction.
        for (int axis = 0; axis < 3; ++axis) {
            check(mesh.min[axis] == model.min[axis] && mesh.max[axis] == model.max[axis],
                  model.name + "'s bounds differ between its .mum and the town's table");
        }
        // Every texture a material names is a file that exists.
        for (const mu::content::CookedMaterial& material : mesh.materials) {
            for (const std::string* path : {&material.albedo, &material.normal, &material.orm,
                                            &material.emissive}) {
                if (path->empty()) continue;
                check(mu::core::fileExists(std::string(MU2_ASSET_DIR) + "/" + *path),
                      model.name + " names a texture that is not there: " + *path);
            }
        }
    }
    std::printf("  %zu meshes parsed, %zu triangles\n", meshes, triangles);

    // --- a file that is not to be trusted --------------------------------------------------
    // Half a town and a bent version must both be refused with a reason rather than read.
    {
        mu::content::CookedTown ignored;
        std::vector<uint8_t> half(bytes.begin(), bytes.begin() + bytes.size() / 2);
        check(!mu::content::parseCookedTown(half, ignored, error), "half a .mut is refused");

        std::vector<uint8_t> wrongVersion = bytes;
        wrongVersion[4] = 99;
        check(!mu::content::parseCookedTown(wrongVersion, ignored, error),
              "a .mut of the wrong version is refused");

        std::vector<uint8_t> notATown = bytes;
        std::memcpy(notATown.data(), "XXXX", 4);
        check(!mu::content::parseCookedTown(notATown, ignored, error),
              "a file that is not a .mut is refused");

        // A count far larger than the file, which is what would make a reader allocate
        // gigabytes before discovering it had been lied to.
        std::vector<uint8_t> hugeCount = bytes;
        const uint32_t enormous = 0xFFFFFF00u;
        std::memcpy(hugeCount.data() + 16, &enormous, 4);
        check(!mu::content::parseCookedTown(hugeCount, ignored, error),
              "a .mut claiming more placements than it holds is refused");
    }

    std::printf("cooked_test: %d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
