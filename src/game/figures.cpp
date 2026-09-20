#include "game/figures.h"

#include <bx/timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <utility>

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
// And the bare point between the shoulders that everything slung hangs from.
constexpr const char* kBackBone = "Bone05";

// MU has no single idle. It has one per way of holding a weapon, and the weapon picks
// between them: a two-handed axe is not a sword held differently, it is actions 5 and 18
// where a sword is 4 and 17. Numbered from the client's own PLAYER_ enum and taken from
// MU2's `Clips.cs:155` (`StanceActions`), which holds the same table.
//
// Empty hands is the one row that asks WHO is standing in it -- (1, 15) for a man and
// (2, 16) for a woman -- because every armed row is shared: a crossbow is held one way
// whoever is holding it.
struct Stance {
    const char* name;
    int idle;
    int walk;
};
// How a slung thing sits on `Bone05`, in MU's own angles about our axes and MU's own units
// carried to metres. Every one of these is out of `RenderCharacterBackItem` by way of MU2's
// `Model.cs`, which did the axis arithmetic and checked it against two constants that were
// already in the file.
struct OnBack {
    float rotation[3];
    float offset[3];
    bool centred;
};
// A sword reared up over the shoulder, hilt clear of the head.
constexpr OnBack kWeaponOnBack = {{70.0f, 90.0f, 0.0f}, {-0.20f, 0.40f, -0.05f}, false};
// A shield laid flat, pushed 14 MU units clear of the back: MU places one by a point inside
// its mesh and the disc sinks into a plate cuirass until its rim disappears. Centred, so the
// middle of the disc lands on the spine rather than the model's origin.
constexpr OnBack kShieldOnBack = {{70.0f, 90.0f, 0.0f}, {0.0f, 0.0f, -0.14f}, true};
// A crossbow is a third arrangement and not the sword's: given the sword's numbers it lies
// across the back diagonally with a limb sticking out past each shoulder. MU's own angles
// turn it upright and flat, which is what makes it read as carried rather than as impaled.
constexpr OnBack kCrossbowOnBack = {{0.0f, 180.0f, -20.0f}, {-0.10f, 0.40f, -0.08f}, false};
// A bow, and the quiver of either kind: MU sends everything in the bow group that is not a
// crossbow to the same translation.
constexpr OnBack kQuiverOnBack = {{70.0f, 90.0f, 0.0f}, {-0.10f, 0.10f, -0.05f}, false};

const OnBack& onBack(const HeldItem& item, bool leftHand) {
    if (item.stance == "crossbow") return kCrossbowOnBack;
    if (item.stance == "bow") return kQuiverOnBack;
    if (item.kind == "shield" || leftHand) return kShieldOnBack;
    return kWeaponOnBack;
}

constexpr Stance kStances[] = {
    {"sword", 4, 17},  {"two_hand_sword", 5, 18}, {"spear", 6, 19}, {"scythe", 7, 20},
    {"bow", 8, 21},    {"crossbow", 9, 22},       {"wand", 10, 23},
};

// The two action numbers a stance stands and walks in.
std::pair<int, int> stanceActions(const std::string& stance, bool female) {
    for (const Stance& one : kStances) {
        if (stance == one.name) return {one.idle, one.walk};
    }
    return female ? std::pair<int, int>{2, 16} : std::pair<int, int>{1, 15};
}

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

std::vector<const FigureBody*> Figures::bodiesOf(BodyKind kind) const {
    std::vector<const FigureBody*> found;
    for (const auto& [name, one] : bodies_) {
        if (one->kind == kind) found.push_back(one.get());
    }
    std::sort(found.begin(), found.end(), [](const FigureBody* a, const FigureBody* b) {
        return a->label < b->label;
    });
    return found;
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

    body.backBone = boneNamed(*body.skeletonMesh, kBackBone);
    for (HeldItem& item : body.held) {
        item.bone = boneNamed(*body.skeletonMesh, item.boneName);
        const OnBack& slung = onBack(item, item.boneName == kLeftGrip);
        std::memcpy(item.backRotation, slung.rotation, sizeof(item.backRotation));
        std::memcpy(item.backOffset, slung.offset, sizeof(item.backOffset));
        item.centred = slung.centred;
        if (item.bone < 0 && !item.boneName.empty()) {
            core::logError("%s: no bone named %s to hang %s on", body.name.c_str(),
                           item.boneName.c_str(),
                           item.mesh ? item.mesh->name().c_str() : "an item");
        }
    }
}

void Figures::posture(FigureBody& body, const std::string& namedIdle) {
    // The stances are a table, and one row asks who is standing in it: empty hands are (1, 15)
    // for a man and (2, 16) for a woman, and every armed row is shared.
    if (!body.library) return;
    const auto [stand, walk] = stanceActions(body.stance, body.female);
    const auto [bare, bareWalk] = stanceActions("", body.female);

    // **With a weapon drawn, the weapon decides the stance and nothing else does.** This read
    // "a named idle wins ... whatever is in its hands", and that was wrong in a way that is
    // only visible with a crossbow. MuMain says it plainly in `ZzzCharacter.cpp`, in the one
    // chain that sets a standing player's action: no weapon at all OR a safe tile gives
    // PLAYER_STOP_MALE, and otherwise the weapon's own type picks the action -- sword, spear,
    // scythe, wand, and `GetEquipedBowType` giving PLAYER_STOP_BOW or PLAYER_STOP_CROSSBOW.
    // The named idle in index.json is the FIRST branch of that chain, not an override of it.
    //
    // What it cost: the Crossbow Guard carries `idle: action1`, which is the unarmed male
    // stop, and a crossbow held in `knife_gdf` under an unarmed idle hangs down the outside of
    // his thigh with the prod across his shin. The mesh, the grip and the bone were all right
    // and were all suspected first. In action9 -- PLAYER_STOP_CROSSBOW -- the same crossbow
    // comes up level in both hands, which is the pose the asset was authored for.
    const bool armed = !body.stance.empty();
    body.idleClip = (armed || namedIdle.empty()) ? body.library->find(stand)
                                                 : body.library->find(namedIdle);
    // Inside a safe zone the weapon goes on the back and the body stands unarmed, which is
    // the same chain's first branch: `c->SafeZone` reaches PLAYER_STOP_MALE whatever is
    // carried. A figure whose index.json names an idle stands in THAT, because for the
    // townsfolk and the guards it is the pose MU's own town stands them in.
    body.idleSafeClip = namedIdle.empty() ? body.library->find(bare)
                                          : body.library->find(namedIdle);
    body.walkClip = body.library->find(walk);
    if (body.idleClip < 0) body.idleClip = body.library->find(0);
    if (body.idleSafeClip < 0) body.idleSafeClip = body.idleClip;
}

const FigureBody* Figures::dress(const std::string& name, const std::string& base,
                                 const std::string& weapon, const std::string& shield) {
    const FigureBody* wearing = body(base);
    if (!wearing) {
        core::logError("nothing cooked called %s to dress %s in", base.c_str(), name.c_str());
        return nullptr;
    }
    auto made = std::make_unique<FigureBody>();
    made->name = name;
    made->label = wearing->label;
    made->kind = wearing->kind;
    made->female = wearing->female;
    made->scale = wearing->scale;
    made->parts = wearing->parts;
    made->library = wearing->library;

    // The right hand first, because the weapon in it decides the stance the whole body stands
    // and walks in -- and an empty right hand is the fist, which is a stance like any other.
    for (int hand = 0; hand < 2; ++hand) {
        const bool right = hand == 0;
        const std::string& wanted = right ? weapon : shield;
        const char* grip = right ? kRightGrip : kLeftGrip;
        if (wanted.empty()) continue;
        const content::Mesh* found = mesh(wanted);
        if (!found) {
            // Said rather than shrugged off: a starter weapon nobody cooked is a character who
            // silently punches, and the reason is in the cook and not in the game.
            core::logError("%s has no cooked mesh, so %s holds nothing in that hand "
                           "(tools/cook.py --only figures)", wanted.c_str(), name.c_str());
            continue;
        }
        HeldItem item;
        item.mesh = found;
        item.boneName = grip;
        auto row = items_.find(found->name());
        if (row != items_.end()) {
            item.kind = row->second.kind;
            item.stance = row->second.stance;
        }
        if (right) made->stance = item.stance;
        made->held.push_back(item);
    }

    bind(*made);
    posture(*made, "");
    core::logf("dressed %s: %s with %zu parts, holding %s%s%s -- stands in %s", name.c_str(),
               base.c_str(), made->parts.size(),
               weapon.empty() ? "nothing" : weapon.c_str(), shield.empty() ? "" : " and ",
               shield.c_str(), made->stance.empty() ? "bare hands" : made->stance.c_str());
    FigureBody* kept = made.get();
    bodies_[name] = std::move(made);
    return kept;
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

    // What each item is, from index.json's own rows: a crossbow is slung one way, a bow's
    // quiver another, a shield a third. Never guessed from a name. Kept past open() because
    // dress() builds held items of its own out of the same table.
    for (const auto& [name, entry] : manifest["items"].members) {
        items_[name] = ItemRow{entry["kind"].stringOr(""), entry["stance"].stringOr("")};
    }
    auto describe = [&](HeldItem& item) {
        auto found = items_.find(item.mesh ? item.mesh->name() : std::string());
        if (found == items_.end()) return;
        item.kind = found->second.kind;
        item.stance = found->second.stance;
    };

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
        made->kind = BodyKind::Character;
        made->female = entry["female"].boolOr(false);
        made->stance = entry["stance"].stringOr("");
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
            HeldItem item;
            item.mesh = found;
            item.boneName = grip;
            describe(item);
            made->held.push_back(item);
        }
        bind(*made);
        posture(*made, entry["idle"].stringOr(""));
        bodies_[made->name] = std::move(made);
    }

    // --- the monsters: one mesh, its own clips, and its gear on a named bone -----------
    for (const core::Json& entry : manifest["monsters"].items) {
        auto made = std::make_unique<FigureBody>();
        made->name = entry["name"].string;
        made->label = entry["label"].stringOr(made->name.c_str());
        made->kind = BodyKind::Monster;
        made->scale = float(entry["scale"].numberOr(1.0));
        made->stance = entry["stance"].stringOr("");
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
            HeldItem item;
            item.mesh = found;
            item.boneName = bone;
            describe(item);
            made->held.push_back(item);
        }
        bind(*made);
        if (made->library) {
            // A monster's slots are its own table: 0 idle, 2 walk, 6 die. But `Skeleton01`
            // has no clips of its own and is animated out of the PLAYER library, where slot
            // 0 is "Set" -- a three-frame character-creation pose -- and slot 2 is "Stop
            // female". Its index.json row says which stance it stands in, and that is the
            // table to read: the trap this file's own header names, which the first draft of
            // these two lines then walked into.
            const bool playerRig = made->library->name == "player";
            const auto [stand, walk] = stanceActions(made->stance, false);
            made->idleClip = made->library->find(playerRig ? stand : 0);
            made->walkClip = made->library->find(playerRig ? walk : 2);
            // A monster never puts its weapon away: MU's safe-zone rule is the player's, and
            // nothing hostile stands in one.
            made->idleSafeClip = made->idleClip;
        }
        bodies_[made->name] = std::move(made);
    }

    // --- the standalone townsfolk: a whole model with its clips inside it --------------
    for (const core::Json& entry : manifest["standalone"].items) {
        auto made = std::make_unique<FigureBody>();
        made->name = entry["name"].string;
        made->label = made->name;
        made->kind = BodyKind::Townsfolk;
        const content::Mesh* body = mesh(entry["mesh"].string);
        if (!body) continue;
        made->parts.push_back(body);
        made->library = libraryFor(body->name());
        bind(*made);
        if (made->library && !made->library->clips.clips.empty()) made->idleClip = 0;
        made->idleSafeClip = made->idleClip;
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
