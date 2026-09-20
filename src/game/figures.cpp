#include "game/figures.h"

#include <bx/timer.h>

#include <algorithm>
#include <cmath>
#include <memory>

#include "core/files.h"
#include "core/json.h"
#include "core/log.h"

namespace mu::game {
namespace {

// MU's own grips, from MU2's Model.cs. They are named bones on the player rig and they sit
// where a grip belongs rather than at the wrist, so there is no correction to derive -- and
// MU2 records three different derivations of the correction that the WRONG bone needs.
// index.json names the bone for every monster and for no character, which is why these two
// are written down here instead.
constexpr const char* kRightGrip = "knife_gdf";
constexpr const char* kLeftGrip = "hand_bofdgne01";

int boneNamed(const content::Mesh& mesh, const std::string& name) {
    if (name.empty()) return -1;
    const std::vector<content::Bone>& bones = mesh.bones();
    for (size_t i = 0; i < bones.size(); ++i) {
        if (bones[i].name == name) return int(i);
    }
    return -1;
}

}  // namespace

const content::Mesh* Figures::mesh(const std::string& name) const {
    auto found = meshIndex_.find(name);
    return found == meshIndex_.end() ? nullptr : meshes_[found->second].get();
}

const FigureBody* Figures::body(const std::string& name) const {
    auto found = bodies_.find(name);
    return found == bodies_.end() ? nullptr : found->second.get();
}

const ClipLibrary* Figures::library(const std::string& name) const {
    auto found = libraries_.find(name);
    return found == libraries_.end() ? nullptr : found->second.get();
}

size_t Figures::clipCount() const {
    size_t total = 0;
    for (const auto& [name, library] : libraries_) total += library->clips.clips.size();
    return total;
}

void Figures::bind(FigureBody& body) {
    if (body.parts.empty()) return;
    body.skeletonMesh = body.parts.front();
    const std::vector<content::Bone>& bones = body.skeletonMesh->bones();

    // Every part is checked against the first, by name and in order. This is the one place
    // that would catch a set whose parts do not share a rig, and it costs a string compare
    // per bone once at load.
    for (const content::Mesh* part : body.parts) {
        if (part->bones().size() != bones.size()) {
            core::logError("%s: %s has %zu bones and %s has %zu -- they are not one rig",
                           body.name.c_str(), part->name().c_str(), part->bones().size(),
                           body.skeletonMesh->name().c_str(), bones.size());
            continue;
        }
        for (size_t i = 0; i < bones.size(); ++i) {
            if (part->bones()[i].name != bones[i].name) {
                core::logError("%s: %s names bone %zu %s where %s names it %s",
                               body.name.c_str(), part->name().c_str(), i,
                               part->bones()[i].name.c_str(),
                               body.skeletonMesh->name().c_str(), bones[i].name.c_str());
                break;
            }
        }
    }

    // And the rig against the clip library, the same way. A figure whose clips are keyed to
    // a different bone order animates plausibly and wrongly.
    body.clipBoneOf.assign(bones.size(), -1);
    if (body.library) {
        const std::vector<std::string>& clipBones = body.library->clips.boneNames;
        size_t missing = 0;
        for (size_t i = 0; i < bones.size(); ++i) {
            if (i < clipBones.size() && clipBones[i] == bones[i].name) {
                body.clipBoneOf[i] = int32_t(i);
                continue;
            }
            for (size_t j = 0; j < clipBones.size(); ++j) {
                if (clipBones[j] == bones[i].name) {
                    body.clipBoneOf[i] = int32_t(j);
                    break;
                }
            }
            if (body.clipBoneOf[i] < 0) ++missing;
        }
        if (missing) {
            core::logf("  %s: %zu of %zu bones are not in clip library %s and hold their bind "
                       "pose", body.name.c_str(), missing, bones.size(),
                       body.library->name.c_str());
        }
    }

    // The box the whole figure was bound in, over every part it wears.
    for (int axis = 0; axis < 3; ++axis) {
        body.min[axis] = 1e30f;
        body.max[axis] = -1e30f;
    }
    for (const content::Mesh* part : body.parts) {
        for (int axis = 0; axis < 3; ++axis) {
            body.min[axis] = std::min(body.min[axis], part->bounds().min[axis]);
            body.max[axis] = std::max(body.max[axis], part->bounds().max[axis]);
        }
    }
    float extent = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        const float half = (body.max[axis] - body.min[axis]) * 0.5f;
        extent += half * half;
    }
    body.radius = std::sqrt(extent);
    body.height = body.max[1] - body.min[1];

    for (HeldItem& item : body.held) {
        item.bone = boneNamed(*body.skeletonMesh, item.boneName);
        if (item.bone < 0 && !item.boneName.empty()) {
            core::logError("%s: no bone named %s to hang %s on", body.name.c_str(),
                           item.boneName.c_str(),
                           item.mesh ? item.mesh->name().c_str() : "an item");
        }
    }
}

bool Figures::open(const std::string& assetDir, const std::string& world,
                   content::Textures& textures) {
    const int64_t started = bx::getHPCounter();
    const std::string dir = core::join(assetDir, "cooked/figures");
    const std::string path = core::join(dir, "figures.json");
    core::Json manifest = core::parseJsonFile(path);
    if (manifest.isNull()) {
        core::logf("no cooked figures at %s -- the town will stand empty of people. "
                   "tools/cook.py --only figures writes it", path.c_str());
        return false;
    }

    // --- the meshes ----------------------------------------------------------------
    const core::Json& meshes = manifest["meshes"];
    meshes_.reserve(meshes.members.size());
    size_t failed = 0;
    uint32_t triangles = 0;
    for (const auto& [name, entry] : meshes.members) {
        std::vector<uint8_t> bytes = core::readFile(core::join(assetDir, entry["mesh"].string));
        content::CookedMesh cooked;
        std::string error;
        if (bytes.empty() || !content::parseCookedMesh(bytes, cooked, error)) {
            core::logError("%s: %s", entry["mesh"].string.c_str(),
                           bytes.empty() ? "is not there" : error.c_str());
            ++failed;
            continue;
        }
        auto made = std::make_unique<content::Mesh>();
        if (!made->buildFromCooked(cooked, name, assetDir, textures)) {
            ++failed;
            continue;
        }
        triangles += made->triangleCount();
        meshIndex_[name] = meshes_.size();
        meshes_.push_back(std::move(made));
    }

    // --- the clip libraries -----------------------------------------------------------
    for (const auto& [name, entry] : manifest["clips"].members) {
        std::vector<uint8_t> bytes = core::readFile(core::join(assetDir, entry.string));
        auto library = std::make_unique<ClipLibrary>();
        library->name = name;
        std::string error;
        if (bytes.empty() || !content::parseCookedClips(bytes, library->clips, error)) {
            core::logError("%s: %s", entry.string.c_str(),
                           bytes.empty() ? "is not there" : error.c_str());
            ++failed;
            continue;
        }
        for (size_t i = 0; i < library->clips.clips.size(); ++i) {
            const content::CookedClip& clip = library->clips.clips[i];
            library->byName[clip.name] = int(i);
            if (clip.slot >= 0) library->bySlot[clip.slot] = int(i);
        }
        libraries_[name] = std::move(library);
    }
    for (const auto& [name, entry] : manifest["clip_of"].members) clipOf_[name] = entry.string;

    auto libraryFor = [&](const std::string& meshName) -> const ClipLibrary* {
        auto found = clipOf_.find(meshName);
        if (found == clipOf_.end()) return nullptr;
        return library(found->second);
    };

    // --- the characters: parts on a shared rig, with things in their hands -------------
    for (const core::Json& entry : manifest["characters"].items) {
        auto made = std::make_unique<FigureBody>();
        made->name = entry["name"].string;
        made->label = entry["label"].stringOr(made->name.c_str());
        made->female = entry["female"].boolOr(false);
        for (const core::Json& part : entry["parts"].items) {
            if (const content::Mesh* found = mesh(part.string)) {
                made->parts.push_back(found);
            }
        }
        if (made->parts.empty()) continue;
        made->library = libraryFor(made->parts.front()->name());
        for (const auto& [field, grip] : {std::pair<const char*, const char*>{"right_hand",
                                                                              kRightGrip},
                                          {"left_hand", kLeftGrip}}) {
            const std::string name = entry[field].stringOr("");
            if (name.empty()) continue;
            const content::Mesh* found = mesh(name);
            if (!found) continue;
            // A staff is skinned to the full player rig: it is a worn part and not a held
            // one, and hanging it off a grip would draw it in the fist as a stick.
            if (found->isSkinned() && found->bones().size() == made->parts.front()->bones().size()) {
                made->parts.push_back(found);
                continue;
            }
            made->held.push_back(HeldItem{found, -1, grip});
        }
        bind(*made);

        // The stances are a table, and one row asks who is standing in it: empty hands are
        // (1, 15) for a man and (2, 16) for a woman, and every armed row is shared. This
        // sprint stands the figures still, so only the stand half is read; the walk is here
        // because the bench asks for it by name.
        const std::string idle = entry["idle"].stringOr("");
        if (made->library) {
            made->idleClip = idle.empty() ? made->library->find(made->female ? 2 : 1)
                                          : made->library->find(idle);
            made->walkClip = made->library->find(made->female ? 16 : 15);
            if (made->idleClip < 0) made->idleClip = made->library->find(0);
        }
        bodies_[made->name] = std::move(made);
    }

    // --- the monsters: one mesh, its own clips, and its gear on a named bone -----------
    for (const core::Json& entry : manifest["monsters"].items) {
        auto made = std::make_unique<FigureBody>();
        made->name = entry["name"].string;
        made->label = entry["label"].stringOr(made->name.c_str());
        made->scale = float(entry["scale"].numberOr(1.0));
        const content::Mesh* body = mesh(entry["mesh"].string);
        if (!body) continue;
        made->parts.push_back(body);
        made->library = libraryFor(body->name());
        for (const char* side : {"right_hand", "left_hand"}) {
            const std::string name = entry[side].stringOr("");
            if (name.empty()) continue;
            const content::Mesh* found = mesh(name);
            if (!found) continue;
            const std::string boneField = std::string(side) + "_bone";
            std::string bone = entry[boneField.c_str()].stringOr("");
            // The Skeleton Warrior names no bone and is on the player rig: it takes the
            // player's own grips, like the characters above.
            if (bone.empty()) bone = std::string(side) == "right_hand" ? kRightGrip : kLeftGrip;
            made->held.push_back(HeldItem{found, -1, bone});
        }
        bind(*made);
        if (made->library) {
            // A monster's slots are its own table: 0 idle, 2 walk, 6 die.
            made->idleClip = made->library->find(0);
            made->walkClip = made->library->find(2);
        }
        bodies_[made->name] = std::move(made);
    }

    // --- the standalone townsfolk: a whole model with its clips inside it --------------
    for (const core::Json& entry : manifest["standalone"].items) {
        auto made = std::make_unique<FigureBody>();
        made->name = entry["name"].string;
        made->label = made->name;
        const content::Mesh* body = mesh(entry["mesh"].string);
        if (!body) continue;
        made->parts.push_back(body);
        made->library = libraryFor(body->name());
        bind(*made);
        if (made->library && !made->library->clips.clips.empty()) made->idleClip = 0;
        bodies_[made->name] = std::move(made);
    }

    // --- where the town's own figures stand, and what spawns on this map ---------------
    for (const core::Json& entry : manifest["placements"].items) {
        FigurePlacement one;
        one.figure = entry["figure"].string;
        for (int i = 0; i < 3; ++i) one.position[i] = float(entry["at"].at(size_t(i)).number);
        one.yaw = float(entry["yaw"].numberOr(0.0));
        one.pitch = float(entry["pitch"].numberOr(0.0));
        one.scale = float(entry["scale"].numberOr(1.0));
        placements_.push_back(one);
    }
    for (const core::Json& entry : manifest["monsters"].items) {
        Breed breed;
        breed.name = entry["name"].string;
        breed.body = body(breed.name);
        if (!breed.body) continue;
        for (const core::Json& rect : entry["spawns"].items) {
            Breed::Rect one{int(rect["x1"].number), int(rect["x2"].number),
                            int(rect["y1"].number), int(rect["y2"].number),
                            int(rect["count"].number)};
            breed.rects.push_back(one);
            breed.total += one.count;
        }
        breeds_.push_back(breed);
    }

    loadSeconds_ = double(bx::getHPCounter() - started) / double(bx::getHPFrequency());
    size_t frames = 0;
    for (const auto& [name, one] : libraries_) frames += one->clips.rows.size() /
                                                          (one->clips.bones * 7);
    core::logf("figures %s: %zu meshes (%u triangles), %zu bodies, %zu clip libraries with "
               "%zu clips over %zu frames, %zu placed in the town, %zu breeds spawn here, "
               "%zu failed, %.2f s",
               world.c_str(), meshes_.size(), triangles, bodies_.size(), libraries_.size(),
               clipCount(), frames, placements_.size(), breeds_.size(), failed, loadSeconds_);
    // Said on the line that says it loaded, and not left to be discovered: a body whose
    // clips never arrived stands in bind pose, animates nothing, and sends the first hour
    // of the hunt into the skinning.
    for (const auto& [name, one] : bodies_) {
        core::logf("  %s: %zu parts, %zu held, %zu bones, %zu clips", name.c_str(),
                   one->parts.size(), one->held.size(), one->boneCount(),
                   one->library ? one->library->clips.clips.size() : 0);
    }
    return failed == 0;
}

void Figures::shutdown() {
    for (auto& one : meshes_) one->shutdown();
    meshes_.clear();
    meshIndex_.clear();
    libraries_.clear();
    bodies_.clear();
    placements_.clear();
    breeds_.clear();
}

}  // namespace mu::game
