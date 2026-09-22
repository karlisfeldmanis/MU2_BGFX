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

// Which of the manifest's three lists a body came out of. It is not the rig and not the
// behaviour -- the Skeleton Warrior is a monster on the player's rig -- it is the row that
// made it, which is what the viewer's categories are cut along: a list of monsters is what
// somebody browsing monsters asked for, and the fact that one of them borrows a character's
// clips is a detail of that monster and not a reason to file it with the characters.
// Armour and Weapon are not the manifest's lists: they are bodies the WARDROBE makes, a suit
// worn on the bare body of the class that may wear it and a weapon held by the class that may
// hold it. A suit of armour has no other way to be looked at -- five pieces on a rig is what
// it is, and a helmet lying on the grass is not it.
enum class BodyKind { Character, Monster, Townsfolk, Armour, Weapon };

// One breed or character: the parts, what is in its hands, its rig and its clips.
struct FigureBody {
    std::string name;
    std::string label;
    BodyKind kind = BodyKind::Character;
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
    // How fast the ground goes past a foot that is standing on it while the walk clip plays at
    // its authored speed, in metres a second, at scale 1. It is what the walk should be paced
    // by: play the clip at `gait / plantSpeed` and the planted foot is still.
    //
    // Measured at load from the clip itself (`measurePlant`) rather than taken from the cook's
    // `travel`, and the difference is not academic. `travel` is the whole cycle's foot
    // movement, which includes the swinging foot going the other way at twice the speed, and
    // the swing is not the mirror of the stance: for MU's walk the two disagree by 4%, and the
    // planted frames measurably slide less at the stance's own rate. See tools/stride.py.
    // Zero for a body with nothing that plants -- a clip in the air, a rig with no foot -- and
    // then the cook's travel decides, as it did before this existed.
    float plantSpeed = 0.0f;
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
    // And what it WALKS in there. The same chain, one row over: `c->SafeZone` gives
    // PLAYER_WALK_MALE (action 15, 16 for a woman) whatever is carried, so a knight crossing the
    // town square walks empty-handed with the axe on his back rather than in the axe's own
    // stride. It was computed from the first and never used, which is why he marched through
    // town in combat stance. Its own plant speed, because it is its own clip with its own feet.
    int walkSafeClip = -1;
    float plantSpeedSafe = 0.0f;

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

// What one item IS, out of index.json's own row and carried through the cook: which kind it is
// and which stance it is held in. Never guessed from a name, and kept after load so a body
// dressed later slings and stands exactly as one built at load does.
struct ItemRow {
    std::string kind;
    std::string stance;
};

class Figures {
public:
    bool open(const std::string& assetDir, const std::string& world,
              content::Textures& textures);
    void shutdown();

    // A body index.json does not describe: `base`'s parts -- which for a new character is the
    // naked class body, HelmClass02 and its four fellows -- with whatever the game has decided
    // he is holding. `weapon` and `shield` are cooked mesh names and either may be empty, which
    // is bare hands and is a state a character really starts in.
    //
    // This exists because what a character wears is the GAME's answer and not the cook's. The
    // cook knows the town's people, each drawn once in one set of armour; a hero changes what
    // he is holding, and by sprint 7 what he is wearing, and neither is a row anybody can write
    // down in advance. The body is owned here and lives as long as the rest, so a pointer to it
    // is as good as a pointer to any other.
    //
    // `worn` is armour on him -- a helm, a cuirass, pants, gloves, boots, by their asset names
    // -- each put in place of the bare part it covers, as a suit's pieces are. A piece nobody
    // has loaded yet is loaded here, out of the wardrobe (see wearable).
    const FigureBody* dress(const std::string& name, const std::string& base,
                            const std::string& weapon, const std::string& shield,
                            const std::vector<std::string>& worn = {});

    // The wardrobe: every suit of armour and every weapon index.json carries, worn and held
    // rather than laid out. It is a SECOND manifest and a second directory on purpose --
    // `tools/cook.py --only wardrobe` writes it and `--only all` does not -- because these
    // are ninety item files the game never loads and the viewer shows one at a time. Called
    // by the viewer only, and after open(), whose bare class bodies it dresses.
    bool openWardrobe(const std::string& assetDir, content::Textures& textures);

    bool isOpen() const { return !bodies_.empty(); }
    const FigureBody* body(const std::string& name) const;
    // Every body of one kind, sorted by the label a reader sees. The bodies themselves live
    // in a hash map, whose order is neither stable between runs nor anything a person could
    // walk, so a list to be stepped through by hand is built here rather than there.
    std::vector<const FigureBody*> bodiesOf(BodyKind kind) const;
    const std::vector<FigurePlacement>& placements() const { return placements_; }
    const std::vector<Breed>& breeds() const { return breeds_; }
    const ClipLibrary* library(const std::string& name) const;
    size_t meshCount() const { return meshes_.size(); }
    size_t clipCount() const;
    double loadSeconds() const { return loadSeconds_; }

private:
    const content::Mesh* mesh(const std::string& name) const;
    // A mesh by name, loaded out of the wardrobe the first time it is asked for. The game
    // never opens the whole wardrobe -- ninety item files, one of which a hero wears -- so a
    // piece he puts on is read then, once, into the same store. Null when the wardrobe has no
    // such piece or it will not load.
    const content::Mesh* wearable(const std::string& name);
    // Reads the wardrobe manifest's mesh paths and which helms keep the head, once.
    void readWardrobe();
    std::string assetDir_;
    content::Textures* textures_ = nullptr;
    bool wardrobeRead_ = false;
    std::unordered_map<std::string, std::string> wardrobePaths_;  // mesh name -> cooked file
    std::unordered_map<std::string, bool> keepsHead_;             // helm name -> worn over the head
    void bind(FigureBody& body);
    // Which clips a finished body stands and walks in: the stance's own row, or the idle
    // index.json names for this figure when it names one. Called by open() and by dress(), so
    // that a hero and a guard pick their idle by one rule.
    void posture(FigureBody& body, const std::string& namedIdle);

    std::unordered_map<std::string, size_t> meshIndex_;
    std::vector<std::unique_ptr<content::Mesh>> meshes_;  // stable addresses: a body points here
    std::unordered_map<std::string, std::unique_ptr<ClipLibrary>> libraries_;
    std::unordered_map<std::string, std::string> clipOf_;  // mesh name -> library name
    std::unordered_map<std::string, ItemRow> items_;       // mesh name -> what it is
    std::unordered_map<std::string, std::unique_ptr<FigureBody>> bodies_;
    std::vector<FigurePlacement> placements_;
    std::vector<Breed> breeds_;
    double loadSeconds_ = 0.0;
};

}  // namespace mu::game
