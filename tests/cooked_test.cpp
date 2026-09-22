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
#include "content/missiles.h"
#include "content/showing.h"
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

    // The lamps, sprint 8a. Eleven of Lorencia's models carry thirteen lights between them
    // (Bridge01 and DoungeonGate01 two each, the rest one), and the map hides 25 anchors:
    // Light01 twice, Light02 eighteen times, Light03 five.
    {
        size_t carried = 0, anchors = 0, fires = 0;
        for (const mu::content::TownEmitter& one : town.emitters) {
            if (one.model == mu::content::TownEmitter::kWorld) {
                ++anchors;
            } else {
                ++carried;
            }
            if (one.kind == mu::content::EmitterKind::Fire) ++fires;
            check(one.kind == mu::content::EmitterKind::Smoke || one.reach > 0.0f,
                  "every light reaches somewhere");
        }
        std::printf("  %zu lights carried by models, %zu hidden anchors, %zu fires, %zu glows\n",
                    carried, anchors, fires, town.glows.size());
        check(carried == 13, "thirteen lights across Lorencia's eleven lit models");
        check(anchors == 25, "the 25 hidden Light01-03 anchors");
    }

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

    // --- the figures: skinned meshes and their clips -------------------------------------
    // Not fatal when they are missing: the figure cook is its own pass, and a tree with no
    // figures cooked yet still has a town worth checking.
    {
        const std::string dir = std::string(MU2_ASSET_DIR) + "/cooked/figures";
        size_t skinned = 0, rigid = 0, bones = 0;
        for (const char* name : {"ArmorMale10", "HelmMale10", "BullFighter01", "Skeleton01",
                                 "Sword01", "Shield10"}) {
            std::vector<uint8_t> meshBytes =
                mu::core::readFile(dir + "/meshes/" + name + ".mum");
            if (meshBytes.empty()) continue;
            mu::content::CookedMesh mesh;
            if (!mu::content::parseCookedMesh(meshBytes, mesh, error)) {
                check(false, std::string(name) + " did not parse: " + error);
                continue;
            }
            if (mesh.isSkinned()) {
                ++skinned;
                bones += mesh.bones.size();
                check(mesh.vertices.empty(), std::string(name) + " is skinned and static at once");
                // The weights the cook promised: four bytes summing to 255, so no shader has
                // to renormalise and no vertex is quietly darkened by a dropped remainder.
                for (const mu::content::CookedSkinnedVertex& vertex : mesh.skinned) {
                    const int sum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2] +
                                    vertex.weights[3];
                    if (sum != 255) {
                        check(false, std::string(name) + " has a vertex whose weights sum to " +
                                         std::to_string(sum));
                        break;
                    }
                }
                // One root at most per rig, and every other bone hanging off one that
                // precedes it -- which is what makes a pose one walk of a flat array.
                for (size_t i = 0; i < mesh.bones.size(); ++i) {
                    check(mesh.bones[i].parent < int32_t(i),
                          std::string(name) + " has a bone before its own parent");
                }
            } else {
                ++rigid;
            }
        }
        std::printf("  %zu skinned meshes (%zu bones), %zu rigid\n", skinned, bones, rigid);

        std::vector<uint8_t> clipBytes = mu::core::readFile(dir + "/clips/player.muc");
        if (!clipBytes.empty()) {
            mu::content::CookedClips clips;
            if (!mu::content::parseCookedClips(clipBytes, clips, error)) {
                check(false, "the player clip library did not parse: " + error);
            } else {
                std::printf("  player.muc: %zu clips, %u bones, %zu frames\n",
                            clips.clips.size(), clips.bones,
                            clips.rows.size() / (clips.bones * 7));
                check(clips.bones == 60, "the player rig is 60 bones");
                check(clips.clips.size() == 283, "the player library is 283 clips");
                // Every clip's frames lie inside the pose array, which is the one thing a
                // player will index with without asking again.
                for (const mu::content::CookedClip& clip : clips.clips) {
                    const size_t last =
                        (size_t(clip.firstRow) + size_t(clip.frames) * clips.bones) * 7;
                    if (last > clips.rows.size()) {
                        check(false, clip.name + " runs past the end of the poses");
                        break;
                    }
                }
                // MU's own rule, measured in the census: a clip's duration is
                // (samples - 1) / (action_speeds x 25). Here only the weaker half is
                // checked -- that every clip has a length and a rotation that is a
                // rotation -- because the speeds table is not in this file.
                for (const mu::content::CookedClip& clip : clips.clips) {
                    if (clip.frames > 1 && !(clip.duration > 0.0f)) {
                        check(false, clip.name + " has frames and no duration");
                        break;
                    }
                }
                for (size_t row = 0; row + 7 <= clips.rows.size(); row += 7 * 97) {
                    const float* q = &clips.rows[row];
                    const float length = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
                    if (length < 0.99f || length > 1.01f) {
                        check(false, "a baked rotation is not a unit quaternion");
                        break;
                    }
                }

                mu::content::CookedClips ignored;
                std::vector<uint8_t> half(clipBytes.begin(),
                                          clipBytes.begin() + clipBytes.size() / 2);
                check(!mu::content::parseCookedClips(half, ignored, error),
                      "half a .muc is refused");
            }
        }
    }

    // --- the showing: sprint 6's effect sheets and sound events -------------------------
    {
        const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/showing/showing.mus";
        std::vector<uint8_t> showingBytes = mu::core::readFile(path);
        if (showingBytes.empty()) {
            std::printf("cooked_test: no showing.mus -- run tools/cook.py --only showing\n");
            ++g_failures;
        } else {
            mu::content::Showing showing;
            std::string error;
            check(mu::content::parseShowing(showingBytes, showing, error),
                  "showing.mus parses: " + error);

            // Mono at 22050 is what the cook promises and what a mixer is allowed to assume.
            // 47 of MU's 104 sound files are stereo and cannot be panned to a place, so this
            // is a correctness property of the sprint's positioned audio, not a size one.
            check(showing.sampleRate == 22050 && showing.channels == 1,
                  "every cooked sound is mono at 22050 Hz");
            check(!showing.effects.empty(), "the showing carries effect sheets");
            check(!showing.events.empty(), "the showing carries sound events");

            // Every event names at least one file, or it is an event that can never sound.
            for (const mu::content::SoundEvent& one : showing.events) {
                if (one.files.empty()) {
                    check(false, one.name + " is a sound event with no files");
                    break;
                }
                // A negative onset would mean a cue that fires before its own file.
                if (!(one.onset >= 0.0f)) {
                    check(false, one.name + " has a negative onset");
                    break;
                }
            }

            // The onset is only load-bearing for a ONE-SHOT: it is what stops a blow's sound
            // landing up to a quarter of a second after the blow. This bound was first
            // written as a second over every event and the ambiences failed it --
            // world_forest leads by 3.15 s and world_wind by 1.53 s, which is not a fault but
            // a track that fades in, and an ambience is looped rather than cued anyway. So
            // the tight bound belongs on the events a cue actually fires.
            for (const char* name : {"melee_hit", "agon_attack", "beetlemonster_attack"}) {
                const mu::content::SoundEvent* one = showing.event(name);
                if (one == nullptr) {
                    check(false, std::string(name) + " is in the cooked events");
                    continue;
                }
                check(one->onset < 0.3f,
                      std::string(name) + " is a cued one-shot and leads by under 0.3 s");
            }

            // The one event the census names by its measurement, so the number is checked
            // and not merely present: agon_attack's lead is 0.167 s.
            if (const mu::content::SoundEvent* agon = showing.event("agon_attack")) {
                check(agon->onset > 0.166f && agon->onset < 0.168f,
                      "agon_attack keeps its measured 0.167 s lead");
            } else {
                check(false, "agon_attack is in the cooked events");
            }

            mu::content::Showing ignored;
            std::vector<uint8_t> half(showingBytes.begin(),
                                      showingBytes.begin() + showingBytes.size() / 2);
            check(!mu::content::parseShowing(half, ignored, error), "half a .mus is refused");
        }
    }

    // --- the missiles: the thing a blow throws, as an ordinary cooked model --------------
    //
    // The point of the pass this checks is that a missile is no longer special: its mesh is
    // a .mum read by the same parseCookedMesh the town's models and the wardrobe's items go
    // through, so the only thing between here and a lit gfx::Drawable is
    // content::Mesh::buildFromCooked, which is the one step that wants a device. Everything
    // that can say "no" about it can therefore say so here, with no window.
    {
        const std::string assets = std::string(MU2_ASSET_DIR);
        mu::content::Missiles missiles;
        if (!missiles.read(assets + "/cooked/showing/missiles.mup")) {
            std::printf("cooked_test: no missiles.mup -- run tools/cook.py --only missiles\n");
            ++g_failures;
        } else {
            check(missiles.rows.size() >= 18, "index.json's eighteen missiles are cooked");
            check(missiles.find("Bone01") != nullptr, "Bone01 is among them");
            check(missiles.find("nothing_throws_this") == nullptr,
                  "a name nothing cooked finds nothing");

            size_t meshes = 0, missileTriangles = 0, additive = 0;
            for (const mu::content::MissileRow& one : missiles.rows) {
                check(one.scale > 0.0f, one.name + " is thrown at some size");
                check(!one.parts.empty(), one.name + " has a part to draw");
                std::vector<uint8_t> meshBytes = mu::core::readFile(assets + "/" + one.mesh);
                if (meshBytes.empty()) {
                    check(false, one.name + " names a .mum that is on disk (" + one.mesh + ")");
                    continue;
                }
                mu::content::CookedMesh mesh;
                std::string meshError;
                if (!mu::content::parseCookedMesh(meshBytes, mesh, meshError)) {
                    check(false, one.name + "'s mesh parses: " + meshError);
                    continue;
                }
                ++meshes;
                missileTriangles += mesh.indices.size() / 3;
                check(mesh.parts.size() == one.parts.size(),
                      one.name + "'s mesh has one part per row part");
                check(!mesh.isSkinned(), one.name + " is rigid: MU animates these by keys");
                // Metres, not MU units. The whole conversion happens in the cook, so the
                // largest thing thrown here is a meteor a metre and a half across and not a
                // rock 150 m wide -- which is the failure this bound exists to catch.
                for (int axis = 0; axis < 3; ++axis) {
                    check(mesh.max[axis] - mesh.min[axis] < 10.0f,
                          one.name + " is in metres, as docs/conventions.md asks of a mesh");
                }
                for (size_t p = 0; p < mesh.parts.size() && p < one.parts.size(); ++p) {
                    const mu::content::CookedMaterial& material =
                        mesh.materials[mesh.parts[p].material];
                    // The blend the table states and the flag the renderer reads are the
                    // same fact written twice, and they must not drift apart.
                    check(material.glow == one.parts[p].additive,
                          one.name + "'s part " + std::to_string(p) +
                              " agrees about being additive");
                    check(material.albedo == one.parts[p].sheet,
                          one.name + "'s part " + std::to_string(p) + " wears the row's sheet");
                    check(!mu::core::readFile(assets + "/" + material.albedo).empty(),
                          one.name + "'s sheet is cooked (" + material.albedo + ")");
                    if (one.parts[p].additive) ++additive;
                }
            }
            std::printf("  %zu missiles, %zu meshes parsed, %zu triangles, %zu additive parts\n",
                        missiles.rows.size(), meshes, missileTriangles, additive);
            check(additive > 0, "some part of some missile is drawn added");

            // Bone01, the skeleton's throw: MU lifts it 150 units over the flight, which the
            // table keeps in MU's units exactly as index.json states it.
            if (const mu::content::MissileRow* bone = missiles.find("Bone01")) {
                check(bone->lift > 149.0f && bone->lift < 151.0f,
                      "Bone01 keeps index.json's lift of 150 MU units");
                check(bone->parts.size() == 1 && !bone->parts[0].additive,
                      "a bone lying on the grass is lit and not added");
            }
        }
    }

    std::printf("cooked_test: %d failures\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
