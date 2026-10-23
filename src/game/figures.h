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

#include "core/json.h"
#include <algorithm>
#include <array>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "content/cooked.h"
#include "content/mesh.h"
#include "content/texture.h"
#include "game/shine.h"

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
    // On the back wherever he stands, not only in a safe zone: the arrows and the bolt, which
    // `RenderCharacterBackItem` sends to the back unconditionally (ZzzCharacter.cpp:15294).
    bool alwaysSlung = false;

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
    // A knight's second weapon is slung as the mirror of the first across his spine: the axis
    // of the back socket that points across him, or -1 for anything else.
    int mirrorAxis = -1;
    // How its plus shows. A body Figures::dress made carries one, and so does a townsperson
    // whose recipe names a plus (Figures::shine).
    ShineLook shine;
    // That plus as the recipe gives it, before the item table turns it into `shine`.
    int plus = 0;
    // The weapon's own clip, where its model carries a rig of its own: the bows' and the
    // crossbows' string (pipeline/cook.py bakes it to cooked/wardrobe/clips/<mesh>.muc). Null on
    // everything rigid. See Figure::poseHeld.
    const content::CookedClips* clip = nullptr;
    // Whether it plays on the shot, as MU plays a bow's or a crossbow's action 0 only while
    // its archer is in PLAYER_ATTACK_BOW..FLY_CROSSBOW and holds it on key 0 otherwise
    // (ZzzCharacter.cpp:10095-10106, 10160-10166).
    bool onShot = false;
    // Where its missile leaves, MU2's Model.Muzzle: bone indices into the item's own rig, -1
    // where it marks none, and the offset off the first in metres in that bone's frame. See
    // Figure::muzzle.
    int muzzleBone = -1;
    int muzzleAxis = -1;
    float muzzleOffset[3] = {0.0f, 0.0f, 0.0f};
    // The material of the arrow or bolt drawn sitting on it ("nocked"), -1 for none. Left out
    // from the loose until the next draw (Figure::nock).
    int nockedMaterial = -1;
    // The fraction of its own clip at which the string goes (its asset's `release`), 0 for none.
    float release = 0.0f;
    // Times a second its own clip runs through while it is in hand (its asset's `held_loop`), 0
    // for none: the Staff of Resurrection's swirl, which MuMain spins at fifteen times the
    // stand's speed (ZzzCharacter.cpp:10125-10128). Slung it stands on its first clip's first key,
    // as MU's back does. See Figure::poseHeld.
    float heldLoop = 0.0f;
    // Which of its own clips that is (its asset's `held_clip`), and the one a sword swing plays
    // instead, keyed to the swing (`swing_clip`, -1 for none): MuMain's Flail, action 1 held
    // and action 2 in PLAYER_ATTACK_SWORD_RIGHT1..2 (ZzzCharacter.cpp:10107-10118).
    int heldClip = 0;
    int swingClip = -1;
};

// Whether it hangs off the right grip: MU's `Weapon[0]`, what a skill that draws the weapon
// itself draws (Twisting Slash's wheel, fx/wheel.h).
bool inRightHand(const HeldItem& item);

// The most bones a held item's own rig may have and still play its own clip: pipeline/cook.py's
// wardrobe bakes a clip for no more. 24 since the Staff of Resurrection's 17 (2026-10-03).
constexpr size_t kHeldBones = 24;

// Which of the manifest's three lists a body came out of. It is not the rig and not the
// behaviour -- the Skeleton Warrior is a monster on the player's rig -- it is the row that
// made it, which is what the viewer's categories are cut along: a list of monsters is what
// somebody browsing monsters asked for, and the fact that one of them borrows a character's
// clips is a detail of that monster and not a reason to file it with the characters.
// Armour and Weapon are not the manifest's lists: they are bodies the WARDROBE makes, a suit
// worn on the bare body of the class that may wear it and a weapon held by the class that may
// hold it. A suit of armour has no other way to be looked at -- five pieces on a rig is what
// it is, and a helmet lying on the grass is not it.
enum class BodyKind { Character, Monster, Townsfolk, Armour, Weapon, Wing };

// One breed or character: the parts, what is in its hands, its rig and its clips.
struct FigureBody {
    std::string name;
    std::string label;
    // What the client shows for it beyond its mesh and clips, by tag: its recipe's show.tags,
    // cooked into cooked/figures/shows.json (sprint E4a). Play asks `has("hydra")`, never a name.
    std::vector<std::string> tags;
    bool has(std::string_view tag) const {
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }
    // Its faint light (show.aura): the bone it hangs on, empty for none, and its colour; with
    // `tiles`, MU's AddTerrainLight radius, it lies on the ground under the bone that far round.
    std::string auraBone;
    float auraColour[3] = {0.0f, 0.0f, 0.0f};
    float auraTiles = 0.0f;
    // Its two eye trails' bones, right and left (show.eye_trail), empty for none; their light,
    // width in units and tails, right then left. Tarkan's by default: subtypes 3 and 2.
    std::string eyeTrail[2];
    float eyeTrailColour[3] = {0.5f, 0.1f, 1.0f};
    float eyeTrailWidth = 30.0f;
    int eyeTrailTails[2] = {8, 20};
    // The bones its shows hang on, by role (show.bones, sprint E4b): "hand_flame", "stars",
    // "shade", "eyes"... each a list of MU's bone names, found once by Play::fit.
    std::vector<std::pair<std::string, std::vector<std::string>>> boneRoles;
    const std::vector<std::string>& bonesFor(std::string_view role) const {
        static const std::vector<std::string> kNone;
        for (const auto& [name, list] : boneRoles) {
            if (name == role) return list;
        }
        return kNone;
    }
    // Whether `bone` is the `i`th name of `role`.
    bool named(std::string_view role, size_t i, const std::string& bone) const {
        const std::vector<std::string>& list = bonesFor(role);
        return i < list.size() && list[i] == bone;
    }
    // Its star bones' colour (show.star_tint), or null for the starlight's own.
    float tint[3] = {0.0f, 0.0f, 0.0f};
    bool tinted = false;
    const float* starTint() const { return tinted ? tint : nullptr; }
    // The structured shows (sprint E4c): the Queen's and the Drakan's arcs, bone pairs with how
    // many a reference frame and how wide; the Hydra's heads, each a bone and its mouth in bind
    // space, and the material its beams were; the claws (the Forest Orc's, the Death Rider's
    // blades), [first, last] bone pairs with the ribbon's reach and light and the key it stops at;
    // the Death Beam Knight's burn, segments as indices into its show.bones.burning.
    std::vector<std::array<std::string, 2>> arcPairs;
    int arcsAFrame = 0;
    float arcHalf = 0.0f;
    struct Head {
        std::string bone;
        float mouth[3] = {0.0f, 0.0f, 0.0f};
    };
    std::vector<Head> heads;
    std::string beamMaterial;
    struct Claws {
        std::vector<std::array<std::string, 2>> pairs;
        float from = 0.0f, to = 0.0f;  // metres along the first bone and along the last
        float light[3] = {0.0f, 0.0f, 0.0f};
        float until = -1.0f;  // the last key of a swing that lays one; -1, the whole swing
        bool firstOnly = false;  // swing 1: on Attack 1 alone (the Death Tree's; its second is roots)
        // Which of the bone's own axes in this engine, 0 x, 1 y, 2 z ("x", "y", "z"); the Death
        // Tree's "z" as the user passed it. 3, "through": from the first bone's point along the
        // line to the second's, `from` and `to` metres at scale 1 -- for a blade whose bone's
        // axes the Blender rebuild has rolled (the Satyros' axe, MU's 200 down bone 44's z).
        int axis = 0;
    };
    Claws claws;
    // show.lands: the key of its swing its blow lands on, the hit's number and sound with it;
    // -1 for the half of the clip every other breed's lands on (Showing::kLandingPoint).
    float landsAt = -1.0f;
    struct BurnSegment {
        int from = 0, to = 0;
        float scale = 1.0f;
    };
    std::vector<BurnSegment> burnSegments;
    int burnBody[2] = {0, 0};
    int burnHead = 0;
    // Its blow's Inferno (show.inferno, sprint E4d): whether it throws one, CreateInferno's bombs
    // and MODEL_SKILL_INFERNO's mesh, the CheckAttackTime it waits for (0, at once), and whether
    // on Attack 1 alone.
    struct Inferno {
        bool throws = false, bombs = false, mesh = false, first = false;
        int frame = 0;
    };
    Inferno inferno;
    // Its Flame of Evil's show (show.blow): "gorgon_ring", "staff_ring", "none"...; empty, the
    // Balrog's.
    std::string blow;
    BodyKind kind = BodyKind::Character;
    std::vector<const content::Mesh*> parts;   // skinned, all against one skeleton
    // How each part's plus shows, beside `parts`; empty, or shorter, is +0. A body
    // Figures::dress made has them, and so does a townsperson whose recipe names a plus.
    std::vector<ShineLook> partShine;
    // The plus every worn part is at by its recipe -- Marlon's plate is +7 -- until
    // Figures::shine reads the item table and turns it into partShine.
    int wornPlus = 0;
    // And the colour that plus shines in on a body that is one model and no item, where its
    // recipe names one (`plus.colour`): the Silver Valkyrie's white chrome. Below 0, MU's gold.
    float wornColour[3] = {-1.0f, -1.0f, -1.0f};
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
    // A wardrobe's Wings tab body: the wing it wears on its back (game/wings.h), else null.
    const FigureBody* wing = nullptr;
    // Where a slung item hangs: `Bone05`, between the shoulders, a child of Bip01 Spine --
    // `w->LinkBone = 47` in the old client's RenderCharacterBackItem. -1 on a rig that has
    // none, which is every monster's.
    int backBone = -1;
    // `Bip01 Pelvis` and `Bip01 Neck`: the two ends of the line a refined suit lights from, so
    // the body is the light (fx/gleam.h). -1 on a rig without them.
    int pelvisBone = -1;
    int neckBone = -1;
    // `Bip01 Head` and its joint in the model as bound: what a monster's bar is held down to
    // (Play::follow's crown, kBarOverHead). -1 on a rig without one.
    int headBone = -1;
    float headJoint[3] = {};
    // A complete set's six lights (game/fx/set_shine.h): each hand's grip and the two bones up
    // that arm, MU's Weapon[i].LinkBone and LinkBone - 6 and - 7 (ZzzCharacter.cpp:10988) --
    // `knife_gdf`, R Forearm, R UpperArm, `hand_bofdgne01`, L Forearm, L UpperArm. -1 where a
    // rig has none.
    int armBones[6] = {-1, -1, -1, -1, -1, -1};
    // Bip01, the root at the pelvis: what a Kanturu set's puffs are thrown off (set_shine.h,
    // MU's BoneTransform[0]). -1 where a rig has none.
    int rootBone = -1;
    // What this figure stands in with its weapon put away. Inside a safe zone MU carries the
    // weapon on the back and stands in the UNARMED idle, and steps out of the zone with the
    // weapon drawn: the client's own rule, from RenderCharacterBackItem and the safe-zone
    // branch of the stance code. An NPC with an idle named in index.json keeps it either way
    // -- that is MU's own table for that figure and not a stance this engine picks.
    int idleSafeClip = -1;
    // A player's stand off a safe zone, whatever he holds: the Mixamo combat idle, action285.
    // -1 where the library lacks it, and then idleClip stands in.
    int combatIdleClip = -1;
    // Whether he carries his weapon on the back, and stands in the safe idle, whenever he is not
    // fighting, safe tile or not: the Golden Archer, who draws his crossbow only to shoot.
    bool slungAtRest = false;
    // The wing a townsperson wears on her back, by its body's name (the recipe's "wings"): the
    // Atlans envoy's Wing04. Empty for none; drawn by Play's WingLook as the hero's.
    std::string wings;
    // And what it WALKS in there. The same chain, one row over: `c->SafeZone` gives
    // PLAYER_WALK_MALE (action 15, 16 for a woman) whatever is carried, so a knight crossing the
    // town square walks empty-handed with the axe on his back rather than in the axe's own
    // stride. It was computed from the first and never used, which is why he marched through
    // town in combat stance. Its own plant speed, because it is its own clip with its own feet.
    int walkSafeClip = -1;
    float plantSpeedSafe = 0.0f;
    // A bare-handed player's walk in a fight: the sword's, MU's 17, as MU has no unarmed combat
    // walk. -1 on an armed body, which walks its weapon's own; and its plant speed.
    int combatWalkClip = -1;
    float plantSpeedCombat = 0.0f;
    // And what it runs in, once the realm says it is running (sim::Body::running): action284,
    // one clip for every stance, -1 on a rig without it. Its own plant speed for the same reason.
    int runClip = -1;
    float plantSpeedRun = 0.0f;
    // And on a horse (sim::Body::riding, docs/game/mount.md): MU's PLAYER_STOP_RIDE 13 and _WEAPON 14,
    // PLAYER_RUN_RIDE 36 and _WEAPON 37 (ZzzCharacter.cpp:283-291, 567-583). A rider never walks:
    // the run ride is his only gait. Bare while the weapon is slung, armed while it is drawn.
    int rideIdleClip = -1, rideIdleArmedClip = -1;
    int rideRunClip = -1, rideRunArmedClip = -1;
    // And on wings, off a safe zone (docs/game/wings.md): MU's PLAYER_STOP_FLY 11 where he stands
    // (PLAYER_STOP_FLY_CROSSBOW 12 with a crossbow) and PLAYER_FLY 34 where he goes
    // (PLAYER_FLY_CROSSBOW 35), ZzzCharacter.cpp:298-326, 615-621. Played at MU's 0.34, the
    // run ride's (ZzzOpenData.cpp:511-516): nothing plants in the air.
    int flyIdleClip = -1, flyIdleCrossbowClip = -1;
    int flyClip = -1, flyCrossbowClip = -1;
    // And under the sea, off its safe zone (Atlans): MU's Fly stance, PLAYER_STOP_FLY 11
    // treading water (PLAYER_STOP_FLY_CROSSBOW 12 with a crossbow), and PLAYER_WALK_SWIM 24,
    // or PLAYER_RUN_SWIM 33 once he runs (ZzzCharacter.cpp:298-326, 624-630). Played at MU's
    // own 0.35 (ZzzOpenData.cpp:332-333): nothing plants on the sea floor.
    int swimIdleClip = -1, swimIdleCrossbowClip = -1;
    int swimWalkClip = -1, swimRunClip = -1;
    // The keys its two feet land on, in cycle order (measureStrikes), -1 when unmeasured: the
    // run's footsteps, where a MU walk has PlayWalkSound's 1.5 and 4.5.
    float runFeet[2] = {-1.0f, -1.0f};

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
    // A bow's or a crossbow's shot, off its asset through the cook (pipeline/cook.py shot_of): the
    // fraction of its own clip the string goes at, the bone the missile leaves from, the bone
    // behind it on the rail, and where off the first the missile sits (MU units). Empty bones
    // on everything that does not shoot, and on the three whose rig marks none.
    float release = 0.0f;
    float heldLoop = 0.0f;  // its asset's `held_loop`: its own clip run always, cycles a second
    int heldClip = 0;       // its asset's `held_clip`: which clip that is
    int swingClip = -1;     // its asset's `swing_clip`: the clip a sword swing plays, -1 none
    std::string muzzleBone;
    std::string muzzleAxis;
    float muzzleOffset[3] = {0.0f, 0.0f, 0.0f};
};

// One out of a manifest's item or arm entry.
ItemRow itemRowOf(const core::Json& entry);

class Figures {
public:
    Figures() = default;
    ~Figures();
    Figures(const Figures&) = delete;
    Figures& operator=(const Figures&) = delete;
    bool open(const std::string& assetDir, const std::string& world,
              content::Textures& textures);
    // A monster figure this world's table does not carry, borrowed from whichever world's table
    // does -- their meshes and clips share one cooked folder. For the elf's summon (sprint 15),
    // which is Noria's Goblin wherever she raises it. True when the figure is here after.
    bool borrow(const std::string& name);
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
    //
    // `wornShine` is how each worn piece's plus shows, in `worn`'s order, and the two after it
    // the hands' (game::shineOf); the body carries them (FigureBody::partShine,
    // HeldItem::shine). Short or empty is +0.
    const FigureBody* dress(const std::string& name, const std::string& base,
                            const std::string& weapon, const std::string& shield,
                            const std::vector<std::string>& worn = {},
                            const std::vector<ShineLook>& wornShine = {},
                            const ShineLook& weaponShine = {}, const ShineLook& shieldShine = {},
                            const std::string& quiver = {});

    // The wardrobe: every suit of armour and every weapon index.json carries, worn and held
    // rather than laid out. It is a SECOND manifest and a second directory on purpose --
    // `pipeline/cook.py --only wardrobe` writes it and `--only all` does not -- because these
    // are ninety item files the game never loads and the viewer shows one at a time. Called
    // by the viewer only, and after open(), whose bare class bodies it dresses.
    bool openWardrobe(const std::string& assetDir, content::Textures& textures);

    // **Gear off the frame** (the user, 2026-10-11, after the bots' hitches were traced to it):
    // while streaming, dress() never reads the wardrobe itself. A piece nobody has loaded is
    // asked of a worker, which reads its mesh and every texture it names and parses the mesh;
    // stream(), once a frame on this thread, makes one finished piece from those bytes -- no
    // disk -- and the figure goes on without it till then. A full suit was 55 files and 43 MB
    // read, parsed and uploaded on the frame a bot in it came into view. Off (the default), as
    // under the loading screen, dress() loads what it needs then and there.
    void setStreaming(bool on);
    bool streaming() const { return streaming_; }
    // At most one piece the worker has read made here, on the calling thread; how many.
    int stream();
    // How many pieces the last dress() went without, waiting on the worker.
    int lastMissed() const { return lastMissed_; }

    bool isOpen() const { return !bodies_.empty(); }
    // Goes up each time the bodies are built again (open, openWardrobe, the shutdown's clear),
    // which frees the ones before: whoever keeps a FigureBody* stands its figures again on a
    // change (game/studio_folk.cpp; the studio crashed when its browser refilled under them).
    uint32_t generation() const { return generation_; }
    const FigureBody* body(const std::string& name) const;
    // Every body of one kind, sorted by the label a reader sees. The bodies themselves live
    // in a hash map, whose order is neither stable between runs nor anything a person could
    // walk, so a list to be stepped through by hand is built here rather than there.
    std::vector<const FigureBody*> bodiesOf(BodyKind kind) const;
    // Turns the plus a townsperson's recipe names into the shine it is drawn with, now that
    // the item table is known: the chrome's level and colour are the item's (game::shineOf),
    // and the figures open before the tables do. Idempotent; a body with no plus is left
    // alone, so a dressed hero is not undressed.
    void shine(const std::vector<content::ItemRow>& items);

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
    // A held weapon's own clip, by its mesh's name, read once from cooked/wardrobe/clips.
    // Null where the cook wrote none -- everything but the bows and crossbows today.
    const content::CookedClips* heldClip(const std::string& name);
    // Reads the wardrobe manifest's mesh paths and which helms keep the head, once.
    void readWardrobe();
    // A piece read into a mesh and put in the store under its name, as wearable() does.
    const content::Mesh* keep(const std::string& name, const content::CookedMesh& cooked);
    // The worker (setStreaming): what it is asked, what it has read, and its thread.
    struct Streamed {
        std::string name;
        content::CookedMesh cooked;
        struct Texture {
            std::string path;
            content::TextureRole role;
            std::vector<uint8_t> bytes;
        };
        std::vector<Texture> textures;
        bool ok = false;
    };
    void want(const std::string& name);
    void work();
    void stopWorker();
    bool streaming_ = false;
    int lastMissed_ = 0;
    std::thread worker_;
    std::mutex lock_;
    std::condition_variable wake_;
    std::deque<std::string> asked_;
    std::deque<Streamed> read_;
    std::unordered_set<std::string> pending_;  // asked and not yet made, this thread's
    bool stop_ = false;
    std::string assetDir_;
    content::Textures* textures_ = nullptr;
    bool wardrobeRead_ = false;
    std::unordered_map<std::string, std::string> wardrobePaths_;  // mesh name -> cooked file
    std::unordered_map<std::string, bool> keepsHead_;             // helm name -> worn over the head
    void bind(FigureBody& body);
    // One monster row of a figure table made into a body: open() for every breed the world
    // carries, and borrow() for one it does not.
    void addMonster(const core::Json& entry);
    // Every figure's show tags, cooked/figures/shows.json, read the first time a monster is added.
    void readShows();
    // A body made, given its recipe's show (tags, bones and the rest) and kept
    // (game/figure_shows.cpp).
    void addShown(std::unique_ptr<FigureBody> made);
    bool showsRead_ = false;
    struct Show {
        std::vector<std::string> tags;
        std::string auraBone;
        float auraColour[3] = {0.0f, 0.0f, 0.0f};
        float auraTiles = 0.0f;
        std::string eyeTrail[2];
        float eyeTrailColour[3] = {0.5f, 0.1f, 1.0f};
        float eyeTrailWidth = 30.0f;
        int eyeTrailTails[2] = {8, 20};
        std::vector<std::pair<std::string, std::vector<std::string>>> boneRoles;
        float tint[3] = {0.0f, 0.0f, 0.0f};
        bool tinted = false;
        // E4c's structured shows, as FigureBody holds them.
        std::vector<std::array<std::string, 2>> arcPairs;
        int arcsAFrame = 0;
        float arcHalf = 0.0f;
        std::vector<FigureBody::Head> heads;
        std::string beamMaterial;
        FigureBody::Claws claws;
        float landsAt = -1.0f;
        std::vector<FigureBody::BurnSegment> burnSegments;
        int burnBody[2] = {0, 0};
        int burnHead = 0;
        FigureBody::Inferno inferno;
        std::string blow;
    };
    std::unordered_map<std::string, Show> shows_;
    // Which clips a finished body stands and walks in: the stance's own row, or the idle
    // index.json names for this figure when it names one. Called by open() and by dress(), so
    // that a hero and a guard pick their idle by one rule.
    void posture(FigureBody& body, const std::string& namedIdle);

    std::unordered_map<std::string, size_t> meshIndex_;
    std::vector<std::unique_ptr<content::Mesh>> meshes_;  // stable addresses: a body points here
    std::unordered_map<std::string, std::unique_ptr<ClipLibrary>> libraries_;
    std::unordered_map<std::string, std::string> clipOf_;  // mesh name -> library name
    // Held weapons' own clips, by mesh name; a null entry is a weapon asked for and found
    // to have none, so it is not asked for again.
    std::unordered_map<std::string, std::unique_ptr<content::CookedClips>> heldClips_;
    std::unordered_map<std::string, ItemRow> items_;       // mesh name -> what it is
    std::unordered_map<std::string, std::unique_ptr<FigureBody>> bodies_;
    uint32_t generation_ = 0;
    std::vector<FigurePlacement> placements_;
    std::vector<Breed> breeds_;
    double loadSeconds_ = 0.0;
};

}  // namespace mu::game
