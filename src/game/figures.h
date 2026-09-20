// What a figure is made of, read from the cook: meshes, clip libraries and the table that
// says which parts and which clips belong to which breed.
//
// The one structural fact this file exists to hold is that a figure is NOT a model. A Dark
// Knight is five worn meshes and two things in his hands drawn against ONE set of bone rows,
// and a Bull Fighter is one mesh and an axe against another. Wearing armour is swapping
// which meshes draw; the skeleton, the clip and the grip do not move. Every part of a set
// carries the identical joint list in the identical order, checked here by name rather than
// assumed, because a rig re-exported with one bone inserted would otherwise animate a helmet
// with a thigh.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "content/cooked.h"
#include "content/mesh.h"
#include "content/texture.h"

namespace mu::game {

// One library of clips, and the ways a clip is asked for: by MU's own action number, which
// is what index.json's tables are keyed by, or by name.
//
// A monster's clip numbers are NOT a player's. `action4` is "Stop sword" on a character and
// "Attack 2" on a spider, and both files call the clip `action4`; the cook has already put
// the right label on each, out of `actions` for a player library and `monster_actions` for a
// monster's own. Reading a monster's slots out of the player's table labels its second swing
// "Stop sword" and looks, at a glance, exactly like it is working.
struct ClipLibrary {
    std::string name;
    content::CookedClips clips;
    std::unordered_map<int, int> bySlot;  // MU's action number -> index into clips.clips
    std::unordered_map<std::string, int> byName;

    int find(int slot) const {
        auto found = bySlot.find(slot);
        return found == bySlot.end() ? -1 : found->second;
    }
    int find(const std::string& name) const {
        auto found = byName.find(name);
        return found == byName.end() ? -1 : found->second;
    }
};

// A thing held in a hand: a rigid mesh riding one named bone.
//
// A weapon is not skinned, except when it is. Swords, axes, maces, spears and shields have
// no skin and hang off a bone; bows and crossbows carry a 12-bone rig of their own for the
// string; staffs are skinned to the full player rig and are worn rather than held. Sending a
// rigid weapon down the skinning path binds it to every bone whose name happens to match and
// draws it stretched across the character.
struct HeldItem {
    const content::Mesh* mesh = nullptr;
    int bone = -1;              // into the body's own skeleton
    std::string boneName;
    std::string kind;    // "weapon", "shield", from index.json's own rows
    std::string stance;  // "crossbow", "bow", "sword", ... -- the item's own

    // **In the hand there is nothing to correct**: the rig's grip bones sit where a grip
    // belongs, so a held item hangs off one with an identity transform. That is MU2's own
    // rule (`Model.cs`, "Identity in the hand, and MU's own numbers on the back"), and it is
    // what makes the swords, axes and staffs sit right without a table of corrections.
    //
    // **On the back there is everything to correct**, because `Bone05` is a bare attachment
    // point between the shoulders rather than something shaped for a grip. These are MU's
    // own numbers out of `RenderCharacterBackItem`, already carried into this engine's axes
    // by MU2's `Model.cs`: a weapon reared over the shoulder, a shield laid flat against the
    // back, a crossbow turned upright and flat -- given the sword's numbers a crossbow lies
    // across the back with a limb past each shoulder.
    float backRotation[3] = {0, 0, 0};  // degrees, MU's own angles about our axes
    float backOffset[3] = {0, 0, 0};    // metres
    // A shield is placed by its middle rather than by its origin: MU places one by a point
    // inside its mesh and the disc then sinks into the armour. MU2 centres it instead.
    bool centred = false;
};

// One breed or character: the parts, what is in its hands, its rig and its clips.
struct FigureBody {
    std::string name;
    std::string label;
    std::vector<const content::Mesh*> parts;   // skinned, all against one skeleton
    std::vector<HeldItem> held;
    const content::Mesh* skeletonMesh = nullptr;  // whose bone table the palette is built on
    const ClipLibrary* library = nullptr;
    // Mesh bone -> library bone, by name. Identity in all of MU2's content, and built rather
    // than assumed so that it stays right when it is not.
    std::vector<int32_t> clipBoneOf;
    // The bind box of ALL the parts together, and the radius of it. One part's bounds are
    // not a figure's: parts[0] of a Dark Knight is his helmet, and a figure framed or culled
    // by a helmet is framed on the helmet and culled when the head leaves the frame.
    float min[3] = {0, 0, 0};
    float max[3] = {0, 0, 0};
    float radius = 1.0f;
    float height = 1.0f;
    float scale = 1.0f;
    int idleClip = -1;
    int walkClip = -1;
    bool female = false;
    // Which way this figure holds what is in its hands: "sword", "two_hand_sword", "spear",
    // "scythe", "bow", "crossbow", "wand", or empty for bare hands. Read from the weapon's
    // own index.json row by the cook, never guessed from a name.
    std::string stance;
    // Where a slung item hangs: `Bone05`, between the shoulders, a child of Bip01 Spine --
    // `w->LinkBone = 47` in the old client's RenderCharacterBackItem. -1 on a rig that has
    // none, which is every monster's.
    int backBone = -1;
    // What this figure stands in with its weapon put away. Inside a safe zone MU carries the
    // weapon on the back and stands in the UNARMED idle, and steps out of the zone with the
    // weapon drawn: the client's own rule, from RenderCharacterBackItem and the safe-zone
    // branch of the stance code. An NPC with an idle named in index.json keeps it either way
    // -- that is MU's own table for that figure and not a stance this engine picks.
    int idleSafeClip = -1;

    size_t boneCount() const { return skeletonMesh ? skeletonMesh->bones().size() : 0; }
};

// Where one of MU's own figures stands in the town: the fourteen the placement list carries
// and sprint 3 had to drop for want of a mesh.
struct FigurePlacement {
    std::string figure;
    float position[3] = {0, 0, 0};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float scale = 1.0f;
};

// One breed that spawns on this map, with the rectangles it spawns in. Sprint 5 owns the
// spawning; sprint 4 uses the weights to pick a representative crowd.
struct Breed {
    std::string name;
    const FigureBody* body = nullptr;
    struct Rect { int x1, x2, y1, y2, count; };
    std::vector<Rect> rects;
    int total = 0;
};

class Figures {
public:
    bool open(const std::string& assetDir, const std::string& world,
              content::Textures& textures);
    void shutdown();

    bool isOpen() const { return !bodies_.empty(); }
    const FigureBody* body(const std::string& name) const;
    const std::vector<FigurePlacement>& placements() const { return placements_; }
    const std::vector<Breed>& breeds() const { return breeds_; }
    const ClipLibrary* library(const std::string& name) const;
    size_t meshCount() const { return meshes_.size(); }
    size_t clipCount() const;
    double loadSeconds() const { return loadSeconds_; }

private:
    const content::Mesh* mesh(const std::string& name) const;
    void bind(FigureBody& body);

    std::unordered_map<std::string, size_t> meshIndex_;
    std::vector<std::unique_ptr<content::Mesh>> meshes_;  // stable addresses: a body points here
    std::unordered_map<std::string, std::unique_ptr<ClipLibrary>> libraries_;
    std::unordered_map<std::string, std::string> clipOf_;  // mesh name -> library name
    std::unordered_map<std::string, std::unique_ptr<FigureBody>> bodies_;
    std::vector<FigurePlacement> placements_;
    std::vector<Breed> breeds_;
    double loadSeconds_ = 0.0;
};

}  // namespace mu::game
