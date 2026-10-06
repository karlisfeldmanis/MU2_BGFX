// The realm: one map, its bodies, and the fixed 20 Hz tick that moves them.
//
// This layer has never heard of the renderer (PLAN.md foundation 8). It speaks in two lists:
// requests in -- walk here, attack that -- and happenings out -- spawned, stepped, hit, died,
// gained, levelled. The game draws the happenings; windows raise the requests. That is the
// mirror rule kept without a wire, and it is what lets the whole thing run with no window at
// all under `--headless`.
//
// Three rules hold inside a step, and each is a thing that has gone wrong in a sim before:
//
//   * **No allocation, once each vector has seen its worst case.** Every vector here is sized
//     when the realm is raised: the bodies, the router's whole scratch, and a route's worth of
//     tiles on every body. What is NOT a hard bound is a route longer than any that body has
//     yet walked, and `happenings` in a tick busier than any before it -- both grow once and
//     then never again, and neither has been seen to grow after the first few hundred ticks of
//     a run. The claim used to be "and to nothing else", which was simply untrue.
//   * **No clock but the tick.** Nothing reads a wall clock, nothing reads a frame's delta, and
//     every delay was converted to ticks once, in the cook.
//   * **A fixed order, and it is written down.** The player walks and swings, then monsters are
//     roused, think and move in index order, then the dead are considered for respawn. No
//     hash container is ever walked to produce a happening, and every id comes from one
//     monotonic counter.
#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "content/tables.h"
#include "sim/quests.h"
#include "sim/items.h"
#include "sim/market.h"
#include "sim/random.h"
#include "sim/route.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/travel.h"
#include "sim/vault.h"
#include "sim/machine.h"
#include "sim/gates.h"
#include "sim/event.h"
#include "sim/invasion.h"
#include "sim/raid.h"

namespace mu::sim {

// What a body is doing about a target, as one field and a switch rather than an object. MU2
// wrote down that a per-monster intelligence object cost an allocation and a virtual call per
// monster per tick for a state machine with one implementation, and at 290 monsters that is
// the whole budget.
enum class Temper : uint8_t {
    Asleep,    // nobody near: not thinking at all
    Wandering, // awake, nothing worth attacking
    Chasing,
    Fighting,
    Homing,    // led too far from its nest, walking back
    Dead,
};

enum class What : uint8_t {
    Spawned,   // a: level, b: health
    Rose,      // respawned
    Roused,    // a: awake, b: the distance that woke it
    Walked,    // a route was planned. a: goal column, b: goal row, c: steps
    Refused,   // and a route that could not be. a: goal column, b: goal row
    Stepped,   // a tile crossed. a: column, b: row
    Halted,
    Missed,    // whom
    Hit,       // a: damage, b: the roll, c: what is left of the defender
    Died,      // whom killed it
    Gained,    // a: experience, b: the total
    Levelled,  // a: the new level, b: points in hand
    Drank,     // a potion's whole worth begun: a: the total, b: 1 for mana, 0 for health
    Served,    // a merchant's counter or the vault opened: a: the townsperson's index, b: MU's
               // NPC number
    Bought,    // a: the item row, b: the price, c: the bag slot
    Sold,      // a: the item row, b: what was paid, c: the bag slot it left
    Dropped,   // something left on the ground: a: its id, b: the item row or -1 for Zen,
               // c: the Zen or the plus
    Picked,    // a: its id (for Zen, the body it came off), b: the bag slot or -1 for Zen, c: the Zen
    Vanished,  // a: its id: it lay too long
    Swung,     // a blow BEGUN, at the top of the swing: a: the skill's number or 0, whom: at whom
    Cast,      // a skill thrown: a: its number, b: the cooldown it set in ticks, whom: at whom
    Shoved,    // the knock: a: the column it was put on, b: the row
    Learned,   // a: the skill's number
    Posed,     // a: the Pose now held (Standing when he gets up), b: the perch's index or -1
    Worn,      // a worn piece lost a whole point: a: its slot, b: what is left, c: its maximum
    Repaired,  // a: the slot, or -1 for all of them, b: the Zen paid, c: pieces put right
    Refined,   // a jewel spent on a thing: a: its slot, b: the plus it had, c: the plus it has
    Soused,    // an Ale gone down: a: the ticks it lasts, b: the swing's ticks now
    Warped,    // a Town Portal Scroll read: a: the column he stands on, b: the row; c: 1 when
               // the map has no safe zone and he is owed Lorencia (the Dungeon)
    Loosed,    // a spell let go at the bottom of its clip: a: its number, b: the ticks it
               // will be in the air, whom: at whom. The `Hit` follows when it arrives.
    Blinked,   // a Teleport put him down: a: the column, b: the row
    Cured,     // an Antidote drunk: the poison on him is gone
    Offered,   // a quest giver spoke (sim/quests.h): a: the quest, b: his folk index, c: the state
    QuestTaken,  // a: the quest
    QuestStep,   // a live step's count moved: a: the quest, b: the count, c: the step
    QuestReady,  // every counted step done: back to the giver. a: the quest
    QuestDone,   // handed in and paid: a: the quest, b: the chosen item row, c: its bag slot
    Set,       // a Rune of Creation set in a thing's socket: a: the thing's slot, b: the power
               // (sim::Power), c: which socket. invention, see sim/items.h
    Shouted,   // a guard's line: a: a `Shout`, b and c: for a pointing, the tile he points the
               // hero to (-1 for nowhere), whom: the monster it is about. What is SAID is the
               // drawing's to choose; the realm only says that he spoke and why.
    Arrowless, // she drew and found no ammunition in hand or bag, and the attack stopped:
               // MuMain's "no more arrows" (CheckArrow). a: 1 for arrows, 2 for bolts
    Dismissed, // her summon gone without a blow: recast, or her death (Realm::dismiss)
    Gated,     // he stepped into an enter gate and goes through it (sim/gates.h): a: the enter
               // gate's number, b: the column he comes out on, c: the row. The realm stops him
               // there; changing the map is the game's.
    Barred,    // an enter gate he is too low for: a: its number, b: the level it asks (0 sealed,
               // -1 he cannot fly: Icarus's door).
               // MuMain's "Only characters over level %d can enter".
    PetLost,   // his pet's or mount's life ran out and it is gone from its slot (Player.cs:1991-2001):
               // a: the item row
    Climbed,   // through an enter gate to another floor of this same map (the Dungeon's
               // stairs): a: the enter gate's number, b: the column he is put down on, c: the row
    Trapped,   // a Dungeon trap fired at him (sim/traps.h): a: the damage, 0 on a miss, b: the
               // trap's index in traps(), c: his health left. `who` is the hero.
    Spirits,   // his shield's Evil Spirit let go round him (sim/items.h kSpiritChance): a: the
               // ticks until its last pulse, whom: the monster whose miss let it go
    Mixed,     // the Chaos Machine ran (sim/machine.h): a: the sim::Recipe for a Combine, else
               // 100 + the sim::Service, b: 1 made, 0 failed, c: the rate it ran at
    Cracked,   // a Firecracker thrown and opened (Realm::crack): a: the id of what it left on
               // the ground, or -1 for Zen, b: that thing's item row or -1, c: the Zen or its plus
    Enlivened,  // a Jewel of Life spent on a thing: a: its slot, b: the option level it had,
                // c: the one it has (0 when it failed)
    Invasion,  // the map's Golden Invasion (sim/invasion.h): a: 1 begun, its dragons coming in
               // the rain, landing kInvasionLandTicks later; 0 over, killed or gone. b and c:
               // the column and row it lands on. `who` is the dragon's body.
    Raid,      // the Golden Dragon's raid (sim/raid.h): a: a RaidEvent, b and c as it says, x and
               // y its place. `who` is the dragon, or the raider for RaidEvent::Raider.
};

struct StrollRow;  // a townsperson's rounds (realm_tuning.h)

// Why a guard spoke. See Realm::watch.
enum class Shout : int32_t {
    Challenge = 0,  // he has seen a monster and is going for it
    Pointing = 1,   // one he fought died with the hero's help: he points him on to the rest
    Salute = 2,     // a townsperson on his rounds came to his post (realm_folk.cpp); whom: him
    Chat = 3,       // his talk at a stop (realm_folk.cpp): b: the line, c: the folk row that says
                    // it, or -1 for him. Said by him, whoever speaks.
    Greet = 4,      // the hero spoke to a townsperson with nothing to sell, keep or give -- the
                    // Guild Master, who has no guild window alone: c: his folk row. Said by the hero.
};

// Devias's Guild Master, MU's NPC 241 (OpenMU's NpcWindow.GuildMaster). Shout::Greet.
constexpr int kGuildMaster = 241;
// Sevina the Priestess, MU's NPC 235: the class change's giver (sim/quests.cpp). Below level 200
// she wears a grey "!" and tells the hero he is not ready (the user, 2026-09-30 and 2026-10-04).
constexpr int kSevina = 235;
// The Messenger of Archangel, MU's NPC 233: Blood Castle's gatekeeper in Devias. Spoken to, he
// opens his page of the quest window (Realm::gating, QuestDialog::kGate), which asks for the
// Invisibility Cloak.
constexpr int kMessenger = 233;
// Thompson the Merchant, MU's NPC 231, a Devias trader who once supplied the Lost Tower's shrine
// and has only lines about it; and Tersia, MU's 566 (OpenMU's Mercenary Guild Felicia), its last guard and the tower's quest giver
// (sim/quests.cpp, docs/lost-tower-quest.md).
constexpr int kThompson = 231;
constexpr int kTersia = 566;
// Charon, MU's NPC 237: Devil Square's gatekeeper in Noria, likewise (the user, 2026-09-30).
constexpr int kCharon = 237;

// What the hero is doing with his body when he is doing nothing: OpenMU's CharacterPose,
// numbers included. These persist -- MU's StopAnimationSetting re-picks the idle only up to
// PLAYER_SHOCK and the sit block sits above it -- so a pose lasts until a walk, a swing, a cast
// or a death takes it away (MU2's Realm.Rise). Nothing else in the rules reads it.
enum class Pose : uint8_t { Standing = 0, Sitting = 2, Leaning = 3, Hanging = 4 };

// A thing on the ground: an item or a pile of Zen, where a death left it, until it is picked
// up or it has lain its minute. MU2's `Lying`: an id the drawing knows it by, a tile, and a
// tick it vanishes on -- none of which survive being picked up, which is why it is not a Held.
struct Lying {
    uint32_t id = 0;
    Held what;            // empty for Zen
    int64_t zen = 0;
    int32_t column = 0, row = 0;
    int64_t vanishesAt = 0;
};

// What a thrown Firecracker gave (Realm::crack): an item lying at his feet, or Zen in the purse.
// `id` is 0 when it was refused and nothing was thrown.
struct Cracked {
    uint32_t id = 0;       // the thing it left on the ground, 0 for Zen or a refusal
    int32_t item = -1;     // its row, -1 for Zen
    int64_t zen = 0;       // what went into the purse, 0 for an item
    int32_t column = 0, row = 0;  // where it burst: the thing's tile, or his for Zen
    bool opened = false;
};

// One thing that happened, flat and copyable. The numbers mean what the enum above says they
// mean; a happening with a body in it carries its id and not a pointer, because ids survive a
// vector that grows and pointers do not.
struct Happening {
    uint32_t tick = 0;
    What what = What::Spawned;
    uint32_t who = 0;
    uint32_t whom = 0;
    int32_t a = 0, b = 0, c = 0;
    // A `Hit`'s own bit, and nothing else reads it: the blow was the top of its band.
    // Unreachable in this content version -- 0.75 grants criticalChance from the luck option
    // alone and nothing rolls one yet -- and said anyway, so the top step of the drawing's
    // ramp is wired the day an item does. It takes no draw, so no seeded log moves.
    bool critical = false;
    // And an excellent hit's: 1.2 x the top of the band, off an excellent weapon's sixth option.
    bool excellent = false;
    // A `Hit` that is his armour's reflect, not a blow he threw: no swing, no clip.
    bool reflected = false;
    // A `Hit` or `Missed` that FLEW -- a spell that left his hand ticks ago and has arrived. It
    // belongs to no swing still playing, so the drawing shows it whatever the body is doing now.
    bool thrown = false;
    // A `Hit` that is a poison's pulse and not a blow: drawn green, as MU's DT_POISON is.
    bool poisoned = false;
    // A `Hit` that is an Immolate burn's pulse: drawn in its ember with the word BURN.
    bool burned = false;
    // A `Hit` a Rune of Creation's power dealt -- Stormcall's lightning, Meteor's rock, Frost
    // Arrow's second wound -- drawn in the rune's own colour (the user, 2026-10-01).
    bool rune = false;
    // A fan's `Loosed`: which of its lanes, a bit each, Plague Arrows poisoned -- drawn green.
    uint8_t plagueLanes = 0;
    // A `Hit` Evil Spirit's spirits dealt, the wizard's spell's or the shield rune's: the drawing
    // spins what it struck (Play, kSpiritStormTime).
    bool spirit = false;
    // A chiller's blow that iced him (kChillers): the drawing shows a one-in-some chiller's ice
    // on this blow alone.
    bool iced = false;
    // A split blow's part after the first (kSplitBlows) -- a Hydra's head bolt, a Lizard King's
    // lightning: no swing, a number and its blood where it lands.
    bool beamed = false;
    // A boss's Flame of Evil: the Death Gorgon's and the Balrog's one blow in five (WebZen's
    // `rand() % 5 == 0` on A.Type 150, gObjMonster.cpp:1849-1925), its damage the monster's own
    // band as WebZen sends it -- the drawing's cue, nothing else changes.
    bool boss = false;
    // Where it happened, in tiles. Written for everything that has a place, because a log line
    // with a position in it is the one that catches a sim drifting apart from itself.
    float x = 0.0f, y = 0.0f;
};

// Running. The user's rule (2026-09-30), and not MU's: out of combat and off a safe tile every
// class runs, at once, with the weapon on his back (a Mixamo run, action284); in combat he
// walks with it drawn, and in town he walks. The speed is a notch under MU's: CharacterMoveSpeed
// gives 15 against the walk's 12 (ZzzCharacter.cpp:6337-6343), a quarter faster over the
// ground, and the user found that 'little bit to fast'. **ours**: 14.
constexpr float kRunFactor = 14.0f / 12.0f;
// Riding the Horn of Uniria. MU's mount runs at CharacterMoveSpeed's 15 off a safe tile, at once
// (ZzzCharacter.cpp:6320-6335; OpenMU's BasicMountMovementSpeed), which is our run's 14 plus a
// notch, and the user, 2026-10-02: "mount has to be faster that runing". 17, MU's own number for
// the later mounts (OpenMU's HorseOrFenrirMovementSpeed), was then 'litttle bit to fast'.
// **ours**: 16. In a fight too: MU's rider never walks (docs/mount.md).
constexpr float kRideFactor = 16.0f / 12.0f;
// Flying on the 1st level wings: CharacterMoveSpeed's 15 off a safe tile, in a fight or out
// (ZzzCharacter.cpp:6320-6335, the wing beside the horn). MU's own number, between our run's
// 14 and our ride's 16 (docs/wings.md).
constexpr float kFlyFactor = 15.0f / 12.0f;
// And the Wings of Dragon's 16 (ZzzCharacter.cpp:6324-6331), our ride's.
constexpr float kFastFlyFactor = 16.0f / 12.0f;
struct Body;
struct SplitBlow;  // realm_tuning.h
// How much ground a walk covers against the breed's own pace: riding, running or neither.
float strideFactor(const Body& one);
// How long a fight holds him in it after the last thing that says he is in one: a monster
// chasing or fighting him, an attack order, a blow or a cast in the air. **invention**, so a
// pause between two blows does not flip him into the run and back. The user, 2026-10-03: 'lets
// go to runing faster after the combat'. **ours**: one second, from three; a blow, a cast, an
// attack order or a monster on him holds it, so the hold only spans the gaps between those.
// Then, the same day: 'lets go instantly to runing if char can after the combat'. **ours**: one
// tick -- the fight lets go on the first tick nothing holds him, and his next step is a run.
constexpr int kCombatTicks = 1;
// On a horse the hold guards nothing -- the rider never walks, so there is no run to flip --
// and it only kept the weapon drawn three seconds over a dead monster. The user, 2026-10-02:
// 'char has to go back to not combat ASAP'. **ours**: a quarter of a second.
constexpr int kRideCombatTicks = 5;
// And how near a chasing monster must be to count, in tiles. **invention**: one that saw him
// across the field is not yet a fight, and he may outrun it.
constexpr float kCombatReach = 4.0f;

// A body on the map: the player, or one monster. One struct for both, because the fight reads
// the same six numbers off either side and a second body type is a second damage path.
struct Body {
    uint32_t id = 0;
    int32_t kind = -1;  // index into Tables::kinds; -1 is the player
    bool player = false;

    Kin kin = Kin::DarkKnight;  // the player's class; meaningless on a monster
    // And whether he is its second -- Blade Knight, Soul Master, Muse Elf -- Sevina's treasure
    // handed in (sim::promoted, kept in step by Realm::restore and completeQuest).
    bool second = false;
    HeroPoints points;          // likewise: the points he spent, which an item's asks are met by
    // The stat runes worn, percent of each (sim::statShareOf summed, Realm::rearm), and the points
    // with them in, which is what every point buys -- damage, defence, life, mana, spells.
    HeroPoints runeShare;
    HeroPoints totalPoints() const {
        return {points.strength + points.strength * runeShare.strength / 100,
                points.agility + points.agility * runeShare.agility / 100,
                points.vitality + points.vitality * runeShare.vitality / 100,
                points.energy + points.energy * runeShare.energy / 100};
    }
    // What is in his hands, as indices into Tables::arms, or -1. A monster's weapon is part of
    // its row and not an item: `monster_kinds` carries the damage band whole. `shield` is the
    // LEFT hand's arm: a shield, or a Dark Knight's second weapon (`dual`), which the swing
    // chain alternates and the skill gates read as the sword family it is.
    int32_t weapon = -1;
    int32_t shield = -1;
    bool dual = false;
    int32_t level = 1;
    int32_t health = 0;
    int32_t maxHealth = 0;
    // The player's mana. Zero on a monster, which casts nothing in 0.75's Lorencia.
    int32_t mana = 0;
    int32_t maxMana = 0;
    // SD, the shield: Season 3's pool in front of health, MU2's Player.Shield. Zero on a
    // monster, so a blow on one lands whole. 0.75 has none; this is the hybrid MU2 chose.
    int32_t sd = 0;
    int32_t maxSd = 0;
    float sdCarry = 0.0f;  // the fraction of a point a recovery tick owes and did not give
    float manaCarry = 0.0f;  // and the same for mana, whose share of a small pool is under one
    float healthCarry = 0.0f;  // and for health, which comes back in a safe zone alone
    // And Bloodwell's, apart: recovery lays healthCarry at nought every three seconds off a
    // safe tile, which ate a rune's share of every small wound before it made a point.
    float stealCarry = 0.0f;
    Fighter stats;
    // What the player's worn pieces add, off the satchel at the last rearm: the armour and
    // shield's defence with their plus counted, and the weapon's plus on its damage band.
    int32_t wornDefense = 0;
    int32_t wornDefenseRate = 0;
    // And the shield's own share of `wornDefense`, which is what Defense's guard is raised on.
    int32_t shieldDefense = 0;
    int32_t weaponBonus = 0;
    // His staff's rise, in percent, off the right hand at the last rearm (Arms::staffRise).
    float staffRise = 0.0f;
    // A bow (+1) or a crossbow (+2) in hand at the last rearm, 0 for anything else: she shoots
    // from `kArcherReach`, spends ammunition from the other hand, and reckons her archery band.
    int8_t archer = 0;
    // The plus of the quiver in that other hand, 0 when it holds none (Arms::quiverPlus).
    int8_t quiverPlus = 0;
    // How many lucky things he wears, each 5% of critical chance (sim::kLuckCritical).
    int32_t luckyWorn = 0;
    // And what his excellent pieces come to (sim::Excellence), summed in rearm.
    Excellence excel;
    // The share of the weapon's band its wear takes, 0 to 0.5, and 1 broken (sim/wear.h). The
    // defence's cut is taken piece by piece inside `wornDefense`.
    float weaponCut = 0.0f;
    // And the second weapon's plus and wear, while `dual`: the left hand's own band.
    int32_t offhandBonus = 0;
    float offhandCut = 0.0f;
    // What his pet does while its life lasts (sim::PetPower), read off slot 8 in rearm.
    PetPower pet;

    // Tiles, and a tile's centre is its integer coordinate -- MU2's own reckoning
    // (Things.cs:71-82, `Column => (int)MathF.Round(X)`). The world's metres and the negation
    // of the row belong to whoever draws this, not here.
    float x = 0.0f, y = 0.0f;
    // Where it is looking and where it wants to look. Two fields and not one, because MU2
    // found that a body which snaps its facing reads as cheap and a body which turns at the
    // DRAWING's own rate is worse: the movement depends on the turn -- a thing does not set off
    // until it has roughly come round -- so the turn belongs down here and the viewer reads it.
    // Walker.cs:70-97.
    float facing = 0.0f;  // radians, where it is looking now
    float aim = 0.0f;     // radians, where it means to look
    bool turning = false; // too far off its aim to cover ground this tick

    // Where it was put down, which is what a leash measures from. Not the nest rectangle:
    // Lorencia's spiders share one 47 by 155 tiles across, and a rectangle leash would let a
    // spider be led from one end of the field to the other and still count as home.
    int32_t homeColumn = 0, homeRow = 0;
    // Its nest in the tables, -1 for none: a respawn draws its tile there again. And the tick a
    // risen beast first thinks, WebZen's five idle seconds (kRiseIdleTicks).
    int32_t nest = -1;
    int64_t wakesAt = 0;

    // The walk. `route` is the tiles left to cross and keeps its capacity between plans.
    std::vector<Step> route;
    size_t onStep = 0;
    bool walking = false;
    float speed = 0.125f;  // tiles a tick: one over the breed's own moveTicks
    // The player's only: whether this tick's walk is a run (see kRunFactor), and the tick his
    // fight lets go of him (kCombatTicks).
    bool running = false;
    int64_t combatUntil = 0;
    // And whether he is on his horse this tick: a mount worn with life left (PetPower::mount),
    // off a safe tile, walking or standing, in a fight or out. In town MU hides it and he walks
    // (GOBoid.cpp:498-502, every ride branch gated `!c->SafeZone`).
    bool riding = false;
    // And whether he flies this tick: a wing worn with life left, off a safe tile, not riding
    // (MU's horn and wing share one branch, the horn first). MuMain's PLAYER_FLY: he never
    // walks or runs then, in a fight or out (ZzzCharacter.cpp:615-621).
    bool flying = false;
    // And on the Wings of Dragon, at 16 where the others fly at 15 (sim::kFastFlyFactor).
    bool flyFast = false;
    // His wing's option, while it has life (sim::Arms::wingDamage, wingWizardry).
    int wingDamage = 0, wingWizardry = 0;

    Temper temper = Temper::Asleep;
    uint32_t quarry = 0;  // an id, 0 for nobody
    bool provoked = false;
    int64_t provokedUntil = 0;  // how long a hit's chase holds past its eyesight (kGrudgeTicks)
    int64_t swingsAt = 0;
    int64_t thinksAt = 0;
    int64_t repathsAt = 0;
    // A summon walking back to her: where it stood, and on what tick, so one that has not got
    // anywhere since is put down behind her (Realm::tend, kSummonStuckTicks).
    float stuckX = 0.0f, stuckY = 0.0f;
    int64_t stuckAt = 0;
    int64_t risesAt = 0;
    // Until when it stands over what it has just killed instead of turning away. See
    // Realm::think and kStandOverTicks; it is the one thing in this layer put there for the
    // sake of what the screen shows.
    int64_t standsUntil = 0;
    int32_t swingTicks = 20;  // how often he may swing: the length of the clip he swings with
    int32_t swingMs = 0;      // the same before it was rounded to ticks, for the log
    // Where the quarry was when this chase was last planned, in tiles and NOT in whole tiles.
    // See Realm::drifted.
    float chaseX = 0.0f, chaseY = 0.0f;

    uint64_t experience = 0;
    int32_t pointsInHand = 0;  // won by levelling and not yet spent

    // ---- skills (docs/skills-dk.md) --------------------------------------------------------
    // What he has learned, as a bit per row of the skill table and NOT per skill number: the
    // mask is six wide and the save writes it whole. Learned permanently -- a skill is not a
    // property of what is in his hands, which is 0.75's shape and is the one the design leaves
    // on purpose. Sixty-four wide since Inferno, the thirty-third row (2026-10-02).
    uint64_t learned = 0;
    // When each learned skill may be thrown again, by the same index. The cooldown is the one
    // thing 0.75 has no equivalent of, so it lives beside the swing clock rather than through
    // it -- a cast pays BOTH, which is what stops haste outrunning the animation.
    int64_t cools[kSkills] = {};
    // A boon: what it multiplies incoming damage by and when it lapses. One at a time, because
    // 0.75 has exactly one skill that grants one and the elf's two are the same shape when they
    // arrive.
    // While a skill's own clip is running he does not turn and is not moved: the blow is thrown
    // where he was standing and facing when he threw it. The user's rule, 2026-09-22 -- a body
    // that swivels or slides mid-skill reads as a teleport, which is the same objection that took
    // the gap-closers out.
    // A blow in the air. The swing is DECIDED on one tick and LANDS on another, half a clip
    // later, which is where the drawing has always shown it (`Showing::kLandingPoint`). Until
    // 2026-09-23 the sim resolved it at the top of the swing instead, so a click that cancelled
    // the attack still did its damage -- the player's own report, and it was the two clocks
    // disagreeing rather than anything about skills.
    int64_t blowAt = 0;        // 0 for nothing in the air
    uint32_t blowTarget = 0;
    float blowForce = 1.0f;    // a skill's multiplier, 1 for a swing
    int32_t blowSkill = 0;     // which skill it belongs to, 0 for a swing
    // A skill aimed at the pointer's ground keeps that way until it is let go
    // (SkillRow::aimsAtPointer): a primary does not hold him, and the fight he is in would turn
    // him back to its body before the release.
    bool blowAimed = false;
    float blowAim = 0.0f;      // radians, as `aim`
    // And a shower's ground (SkillRow::showers), in tiles: where its rocks fall round when the
    // blow is let go, whatever stands there by then.
    bool blowGround = false;
    float blowX = 0.0f, blowY = 0.0f;
    int64_t castUntil = 0;
    // Whether a click to move may end that hold: every skill's but Teleport's, whose fade and
    // settle are the move (the user, 2026-10-02: "any spell ahs to be cancelable").
    bool castBreaks = false;
    int64_t boonUntil = 0;
    float boonDamageTaken = 1.0f;
    int32_t boonSkill = 0;
    // Until when an Ale stands on him: kAleSpeed more attack speed, read by `reswing`. Beside
    // the boon and not through it, because the two are different effects in OpenMU (subtypes
    // 54 and the skill's own) and a guard raised with an Ale in him keeps both.
    int64_t aleUntil = 0;
    // Until when a Frenzy rune's stacks stand on him, and how many: kFrenzyStackSpeed more attack
    // and casting speed each, read by `reswing` and `clipTicksOf` through frenzySpeed, beside the
    // Ale's.
    int64_t frenzyUntil = 0;
    int frenzyStacks = 0;
    int frenzySpeed(int64_t tick) const {
        return frenzyUntil > tick ? frenzyStacks * kFrenzyStackSpeed : 0;
    }
    // Greater Damage on her: the bonus reckoned at the cast, and the tick it lapses.
    int32_t might = 0;
    int64_t mightUntil = 0;
    // Being pushed (`Realm::push`): tiles a tick to slide, and how many ticks are left. While it
    // runs the body neither thinks nor walks.
    float pushX = 0.0f, pushY = 0.0f;
    int32_t pushTicks = 0;
    // Pulled in by a Whirlwind (`Realm::whirl`): who pulled it and the force his slash struck
    // with, laid on it when the slide ends beside him, so the pull is seen before the blow. 0 none.
    uint32_t whirledBy = 0;
    float whirlForce = 0.0f;
    // A beast's Lightning push on him, held until its bolt lands (kBeastPushDelay): the tick it
    // goes, 0 for none, and where the beast stood when it struck.
    int64_t pushAt = 0;
    float pushFromX = 0.0f, pushFromY = 0.0f;
    // The parts of a split blow still to land this swing (kSplitBlows), the tick the next lands
    // and whom they are aimed at. 0 none.
    int32_t beamsLeft = 0;
    int64_t beamAt = 0;
    uint32_t beamOn = 0;
    // A channel running (`Realm::channel`): which skill, when it began and ends, and the tick of
    // its next pulse. 0 for none. The interface reads the first three for its bar.
    int32_t channelSkill = 0;
    int64_t channelFrom = 0, channelUntil = 0, channelNext = 0;
    // Its strike window in ticks from `channelFrom`, the row's quickened by his casting speed as
    // the clip is (Realm::throwSkill): the hand and the bolts keep together.
    int32_t channelStrikeFrom = 0, channelStrikeUntil = 0;
    // Where the last strike went, as a bearing from him in radians: the next goes to the body
    // clockwise from it, so the channel sweeps round.
    float channelTurn = 0.0f;
    // Lightning's chain: the body it was cast at, struck first from his hand, and the last body
    // struck and where it stood, which the next strike leaps from. 0 before the first strike.
    uint32_t channelAim = 0, channelLast = 0;
    float channelLastX = 0.0f, channelLastY = 0.0f;
    // Whom this channel has struck and how often, for `SkillRow::strikesEach`. Emptied at the cast.
    uint32_t channelStruck[kVictims] = {};
    uint8_t channelTimes[kVictims] = {};
    int32_t channelStruckCount = 0;
    // This channel is an Arcane Echo's second sweep (sim/items.h), which echoes no further.
    bool channelEcho = false;
    // A Teleport cast and not yet landed (`Realm::blink`): the tick he is put down, 0 for none,
    // and where.
    int64_t blinkAt = 0;
    // Iced (`SkillRow::chillTicks`): walks at `kChillFactor` until this tick. 0 for never.
    int64_t chilledUntil = 0;
    // Frozen (a Frost Arrow rune, sim/items.h): neither walks nor swings until this tick.
    int64_t frozenUntil = 0;
    // Poisoned (`SkillRow::poisonTicks`): until this tick, its next pulse, how much a pulse takes,
    // and who poisoned it. 0 for never.
    int64_t poisonUntil = 0, poisonNext = 0;
    int32_t poisonDamage = 0;
    uint32_t poisonBy = 0;
    // Burning (a knight's Immolate rune, sim/items.h): until this tick, its next pulse, how much
    // a pulse takes, and whose rune lit it. 0 for never.
    int64_t burnUntil = 0, burnNext = 0;
    int32_t burnDamage = 0;
    uint32_t burnBy = 0;
    int32_t blinkColumn = 0, blinkRow = 0;
    // Sitting, leaning or hanging, and off which perch (an index into Tables::perches, -1 for
    // none). The player's only; a monster never poses.
    Pose pose = Pose::Standing;
    int32_t perch = -1;

    // ---- the town's guards (Realm::watch) ----------------------------------------------------
    // A guard's row in Tables::folk, -1 on everybody else. A guard is a body like a monster, at
    // the end of `bodies_`, so the fight and the drawing read him as they read anything else;
    // he is not a monster, and every "which of these can be attacked" loop asks `monster()`.
    int32_t warden = -1;
    // Where he looks when he is at his post, in the sim's radians.
    float post = 0.0f;
    // The last monster he challenged, so one that steps out of his leash and back in is not
    // challenged twice. Forgotten when he is back at his post.
    uint32_t challenged = 0;
    // On a monster: the guard who last swung at it, 0 for none, and whether the hero has landed
    // a blow on it. Both cleared when it rises. Together they are "the hero helped a guard".
    uint32_t guardedBy = 0;
    bool heroStruck = false;

    // ---- the elf's summon (Realm::tend, sprint 15) --------------------------------------------
    // Whose it is: the owner's id, 0 on everybody else. One body a realm, raised dormant at the
    // end of `bodies_` and reused, so no pointer into `bodies_` moves when she casts. Not a
    // monster: nothing that asks `monster()` attacks it, and it attacks only monsters.
    uint32_t summoner = 0;
    // The summon skill that raised it (30 Goblin ... 35 Bali), for the name and the recast.
    int32_t summonedBy = 0;

    // ---- the Golden Dragon's raid (sim/raid.h, realm_raid.cpp) ------------------------------
    // A raider's place in the party, -1 on everybody else: one of the end-game characters who
    // fight the dragon beside him. Not a monster, and not the player: its blows roll off its own
    // reckoned stats, as a guard's do, and a dead one stays down for the fight.
    int32_t raider = -1;

    bool alive() const { return health > 0; }
    bool monster() const { return !player && warden < 0 && summoner == 0 && raider < 0; }    int column() const { return int(x + (x < 0.0f ? -0.5f : 0.5f)); }
    int row() const { return int(y + (y < 0.0f ? -0.5f : 0.5f)); }
};

// What a save keeps of the hero: everything the player earned and chose, and nothing the sim
// works out again from it. Health, stats and swing speed are reckoned from level, points and
// what is worn (Realm::rearm), so they are not here; health and mana ARE, because a hero who
// quits hurt comes back hurt. Where he stands and his class go to raise(), which already
// finds a free tile and dresses the class -- this is the rest, laid on top.
struct HeroRecord {
    Kin kin = Kin::DarkKnight;
    int32_t column = 0, row = 0;
    float facing = 0.0f;
    int32_t level = 1;
    uint64_t experience = 0;
    int32_t pointsInHand = 0;
    HeroPoints points;
    int32_t health = 0, mana = 0;
    int64_t money = 0;
    // What he has learned, by the skill table's own index. Saved because learning is permanent
    // in this design and is the one thing about a skill that is his rather than his weapon's.
    uint64_t learned = 0;
    // How many ticks each skill had left to cool, by the same index, 0 for ready. Saved so a
    // restart is not a way round a wait (the user, 2026-09-28); the time away is not counted
    // against it, as it is not against the boon below.
    int64_t coolsLeft[kSkills] = {};
    // The buff standing on him when he was saved: which skill, the damage factor it was cast
    // at, and how many ticks of it were left. Saved so a guard raised before a restart is still
    // up after it (the user, 2026-09-25); the time away is not counted against it. 0 for none.
    int32_t boonSkill = 0;
    float boonDamageTaken = 1.0f;
    int64_t boonTicksLeft = 0;
    // And an Ale's ticks left, 0 for none, saved for the same reason as the boon.
    int64_t aleTicksLeft = 0;
    // And her Greater Damage: the bonus it was cast at and its ticks left, 0 for none.
    int32_t might = 0;
    int64_t mightTicksLeft = 0;
    Held slots[kSlots];
    // Every quest's progress, by sim/quests.h's index.
    QuestProgress quests[kQuests];
    // The travel list's rows he has opened, a bit a row (sim/travel.h).
    uint32_t found = 0;
    // Her summon standing when she was saved: the skill that raised it and its health, 0 for
    // none. Saved so a restart does not take it from her (the user, 2026-10-02: "remember it so
    // after restart it still there how it was before"); a map change writes none, as the realm
    // she lands in raises it dormant.
    int32_t summonSkill = 0;
    int32_t summonHealth = 0;
};

// What the game asks the sim for. Nothing here is a skill, and that is on purpose: PLAN.md
// decided the skill system is Diablo 3's shape -- learned permanently, four keys, real
// cooldowns -- and the one thing this sprint owes it is not baking in MU's assumptions. An
// attack is a request to fight a body, not a swing fired from an item, and the swing clock it
// runs on is per-body and already separate from the thinking clock, so a per-skill cooldown
// goes beside it rather than through it.
struct Request {
    // Talk: walk to a townsperson (`target` is his index in Tables::folk) and, within the
    // counter's reach, be served. Any other order closes the counter.
    // Pick: walk to a thing on the ground (`target` is its id) and take it on arrival.
    // Perch: walk to something to sit on or lean against (`target` is its index in
    // Tables::perches) and take the pose once the walk is over. MU's MOVEMENT_OPERATE.
    enum class Kind : uint8_t { None, WalkTo, Attack, Stop, Talk, Pick, Perch } kind = Kind::None;
    int32_t column = 0, row = 0;
    uint32_t target = 0;
    // An Attack's quick-slot skill: the one the right mouse button carries, thrown at the target
    // whenever it can be and the weapon swung whenever it cannot -- short of mana, the wrong
    // hand, a knight's skill cooling. 0 is a plain attack, which is the left button. The user,
    // 2026-09-28: *"right click is quick slot but for right click"*.
    int32_t skill = 0;
};

struct RealmCounts {
    uint32_t monsters = 0;
    uint32_t alive = 0;
    uint32_t roused = 0;
    uint32_t walking = 0;
};

class Realm {
public:
    // Raises the map: every nest placed, the player put down on a standable tile near
    // (column, row), the router opened. Allocates here and, apart from happenings and the
    // route vectors growing once to their high-water mark, nowhere else.
    bool raise(const content::Tables* tables, uint64_t seed, int playerColumn, int playerRow,
               Kin kin = Kin::DarkKnight, int level = 1);

    void ask(const Request& request) { pending_ = request; }

    // ---- skills (docs/skills-dk.md) --------------------------------------------------------
    // A key pressed: throw this skill at this body. NOT a Request, and that is the design and
    // not a shortcut -- a press does not replace the standing attack order, it spends the next
    // swing on the skill and leaves the order standing, so the knight goes on fighting
    // afterwards without a second click. MU2's `Realm.Cast` keeps the ask in `Player.Wants` for
    // the same reason and its remark is worth reading.
    //
    // Remembered rather than dropped when it arrives mid-swing: the tick rate is 20 and a key is
    // pressed on a frame, so refusing an ask that lands inside the swing it is waiting for would
    // lose most presses. It is held for `kWishTicks` and thrown by the first tick that can.
    // Every refusal is silent, as `Swing`'s and `Move`'s are: the interface asks, and a no is a
    // message that does not come back.
    void invoke(int32_t skill, uint32_t at);
    // The same wish aimed at the ground rather than a body: Teleport's, at a tile.
    void invokeAt(int32_t skill, int column, int row);
    // Learning, which in this design is permanent and saved: an orb consumed sets a bit. Nothing
    // in 0.75 does this -- the knight's skills were carried by the weapon in his hand -- so it
    // is `invention`, argued in the doc's §3.3.
    bool learn(int32_t skill);
    // The bench's (`--arena-undying`): a blow that would fell the hero fills his health instead,
    // so a fight runs as long as it is watched. Never set in play.
    void undying(bool on) { undying_ = on; }
    // Whether he may throw it at all: learned, or a mount's skill and that mount worn
    // (`SkillRow::mounted`). What the bar and the list ask, and `throwSkill` before it spends.
    bool knows(int32_t skill) const;
    // A Horn of Dinorant in the mount's slot with life left: Fire Breath is known while it is.
    bool dinorantWorn() const;
    // Ticks left on a skill's cooldown, and the whole cooldown it was set to, which is what the
    // frame needs to draw a sweep. Zero and zero when it is ready.
    int64_t cooling(int32_t skill) const;
    int32_t coolsFor(int32_t skill) const;

    // Points the player has won and not yet spent, put into his four stats. A real action --
    // sprint 7's stat window raises it, and sprint 9's save writes what came of it -- and it
    // is here rather than in raise() because spending a point is a choice and the sim does not
    // make choices. Refused, whole, when it asks for more points than are in hand.
    bool spend(int strength, int agility, int vitality, int energy);

    // The hero as a save keeps him, and the same laid back on a hero just raised at that
    // record's tile and class. restore() empties the satchel first, puts back what was carried
    // and worn, re-reckons him off it, and only then sets health and mana, clamped to what he
    // can now hold. A record from a hero who died is brought back full, as reviveHero would.
    HeroRecord record() const;
    void restore(const HeroRecord& saved);

    // Puts a weapon and a shield in the character's hands, by index into the cooked arms, -1
    // for an empty hand. Refused, whole and with a reason in the log, when his class may not
    // hold it or his strength and agility do not meet what it asks. There is no bag and no
    // durability behind this -- both are sprint 7's -- and no swing speed either: MU paces a
    // swing by the attack clip's authored length, and inventing a mapping from a weapon's
    // `attack_speed` is the one thing this sprint will not do.
    // `given` is the cradle's exemption and nothing else: a character is CREATED holding what
    // his class is given, and the strength and agility the item asks are not tested then. That
    // is OpenMU's own shape -- AddSmallAxeForDarkKnight writes the axe straight into the hand
    // slot, and the requirement check lives in whatever moves an item into a slot afterwards --
    // and it is load-bearing rather than a convenience: a level-one Dark Knight has 28 strength
    // and the Small Axe wants 50, so a knight made the strict way starts the game punching.
    // MU2's `Beast.Wield` records the same decision. Everything a player does later goes
    // through the check.
    bool equip(int32_t weapon, int32_t shield, bool given = false);
    // Why the last equip was refused, or empty.
    const std::string& refusal() const { return refusal_; }

    // ---- the satchel (sprint 7) -----------------------------------------------------------
    // What the player carries and wears. The satchel is the truth and his hands are read off
    // it: every change to a worn slot re-reckons him (Beast.Rearm).
    const Satchel& satchel() const { return bag_; }
    int64_t money() const { return money_; }
    Wearer wearer() const;
    // A thing put straight into a slot, with no gate but the slot being free: the cradle's
    // axe, and a purchase into the first place it fits. -1 for anywhere in the bag. The slot
    // it went to, or -1 when there was nowhere. A durability of -1 is whole at its plus.
    int give(int32_t item, int slot = -1, int refinement = 0, int durability = -1,
             bool luck = false, int option = 0, uint8_t excellent = 0, uint8_t sockets = 0,
             const uint8_t* powers = nullptr, const uint8_t* affixes = nullptr);
    // A drag from one slot to another, equipping and unequipping included. Refused, whole,
    // where `movable` says no -- the same answer the window colours the cell by.
    bool moveItem(int from, int to);
    // A right-click on a carried thing: drink it, read it or learn from it. A potion is refused
    // while the half-second cooldown has not run (RecoverConsumeHandler's CooldownTime) and its
    // heal arrives over the next second in three instalments. The Ale and the Town Portal
    // Scroll take no cooldown -- OpenMU's handlers for both ask none -- and say What::Soused
    // and What::Warped.
    bool useItem(int slot);
    // Whether an Ale stands on him, and the ticks it has left.
    int64_t aleLeft() const { return std::max<int64_t>(0, bodies_[0].aleUntil - tick_); }
    // And a Frenzy's, the Dungeon's rune (sim::kFrenzyTicks).
    int64_t frenzyLeft() const { return std::max<int64_t>(0, bodies_[0].frenzyUntil - tick_); }
    // A potion still pouring in, health or mana: the ticks to its last instalment (at most
    // kPourTicks) and what is still to come of it. For the buff strip's cell; nothing to the sim.
    static constexpr int64_t kPourTicks = 20;
    struct Pouring {
        int64_t left = 0;
        int32_t amount = 0;
    };
    Pouring pouring(bool mana) const {
        Pouring out;
        for (int i = 0; i < sipCount_; ++i) {
            const Sip& one = sips_[i];
            if (!one.drunk || one.mana != mana) continue;
            out.left = std::max(out.left, one.due - tick_);
            out.amount += one.amount;
        }
        return out;
    }
    // A jewel let go over a thing: the Bless or the Soul, from a bag slot, onto a thing carried
    // or worn. Refused, whole and silent, where `refinable` says no. Otherwise the jewel is
    // spent whatever the roll gives, and the thing comes back at its new plus: OpenMU's
    // UpgradeItemLevelJewelConsumeHandlerPlugIn. Says What::Refined. A Life works the option
    // instead (kLifeChance) and says What::Enlivened.
    bool refine(int jewelSlot, int targetSlot);
    // Zen in and out, for the merchants. `pay` refuses, whole, what he cannot afford.
    void earn(int64_t zen) { money_ += zen; }
    bool pay(int64_t zen);
    // Takes a carried thing out of the bag and hands it back: a sale. Worn things are not
    // sold (Shelf.Offer refuses a source outside the bag, and so does this).
    Held sell(int slot);
    // Throws a carried thing on the ground at his feet: the drag out of the window. MU's
    // SendRequestDropItem and OpenMU's DropItemAction, as MU2's Realm.Discard wrote them --
    // the thing leaves the slot, lands on the tile he stands on or the nearest clear one
    // (the same `clearing` a kill's drop takes) and lingers as long as a kill's drop does.
    // Worn things may be thrown too, and the hands are re-reckoned when they are. Money is
    // not here, because money is not in a slot.
    //
    // The dropped thing's id, or 0 refused -- and the id is the answer rather than a bool
    // because a discard is asked BETWEEN ticks, like a purchase and a sale, and the next
    // step clears the What::Dropped it says before the showing could read it. What rings the
    // thing landing is the caller, off this id (Play::discard).
    uint32_t discard(int slot);
    // Whether the thing in `slot` opens when it is thrown rather than lying where it falls: a
    // Firecracker (sim/items.h). The window's drag out asks this before it asks `discard`.
    bool cracks(int slot) const;
    // Throws a Firecracker: spent from the slot, and WebZen's roll -- two in ten an item off
    // eventitembag5, +5 to +9 with luck and the option rolled, lying at his feet as a kill's
    // drop lies; else 2,004 Zen into the purse. Asked between ticks as `discard` is, so the
    // answer carries what the showing needs; says What::Cracked for the log.
    Cracked crack(int slot);
    // The bench's, and no rule of MU's: a thing laid on the ground beside him as a kill's drop
    // lies, from nobody's bag, for looking at a drop Lorencia never leaves. Its id, or 0.
    uint32_t lay(int32_t item, int refinement = 0, bool luck = false, int option = 0,
                 uint8_t excellent = 0, uint8_t sockets = 0);
    // The tests', and no rule of MU's: a death's drop as `leave` rolls it, for a monster of
    // `level` slain by him beside him, with no hunt. What falls is in lying() and happenings().
    void dropFor(int level);

    // ---- the merchants (sprint 7) ---------------------------------------------------------
    // The townsperson whose counter is open, as an index into Tables::folk, or -1. Opened by a
    // Talk order arriving within `kCounter` of a merchant, closed by any other order.
    int trading() const { return trading_; }
    void closeTrade() { trading_ = -1; }
    // Buys whatever sits in one of the open shop's slots: priced, the room looked for at its
    // own footprint BEFORE anything is taken, then paid and placed together. The bag slot, or
    // -1 refused. Realm.Buy.
    int buy(int shelfSlot);
    // Sells a carried thing to the open shop: bag slots only, never what is worn. What was
    // paid, or -1 refused. Realm.Sell.
    int64_t sellItem(int slot);
    // The undo on a sale, and no rule of MU's (the user's, 2026-09-28): the last few sales stay
    // at the counter for `kBuybackSeconds`, and one bought back costs exactly what it fetched.
    // Newest first, at any merchant, while a counter is open and he is in reach of it.
    struct Sale {
        Held what;
        int64_t paid = 0;
        int slot = -1;     // the bag slot it left, which it goes back to when that is free
        int64_t at = 0;    // the tick it was sold on
    };
    static constexpr int kBuybackSeconds = 60;
    static constexpr int kBuybacks = 5;
    // The newest sale still in its window, or nullptr. Ticks left beside it, if asked.
    const Sale* lastSale(int64_t* ticksLeft = nullptr) const;
    // Takes the newest sale back: paid for, then put where it was or the first place it fits.
    // The bag slot, or -1 refused (nothing to undo, no counter, no room, no Zen).
    int buyBack();
    // What `sellItem` would pay for the thing in this slot at any counter, or -1 when it cannot
    // be sold at all: a worn slot, an empty one, or a thing worth nothing. The item card's.
    int64_t sellValue(int slot) const;
    // Whether he is close enough to be served by this townsperson right now. Asked again on
    // every purchase and sale, not once when the counter opened.
    bool serving(int folk) const;

    // ---- wear and the repair (sim/wear.h) -------------------------------------------------
    // Whether the open counter mends: Hanzo's or Eo's, and he is still in reach of it.
    bool mending() const;
    // Whether he may mend his own gear where he stands: alive and at kSelfRepairLevel or above.
    bool selfMending() const;
    // What putting one carried or worn thing back to full costs, or 0 for a thing that wears
    // not or is whole: the counter's price at a counter that mends, else his own, two and a half
    // times it. What `repair` would take, to the Zen.
    int64_t repairCost(int slot) const;
    // And every piece at once, worn and in the bag: the strip under Hanzo's shelf. MuMain's
    // RepairAllGold sums both, and so does this, so the figure drawn is the figure paid --
    // OpenMU mends the worn ones only and says the client is wrong to show the bag's; the
    // user's rule is that the window is a mirror, so the two cannot disagree here.
    int64_t repairAllCost() const;
    // Put one back to full: at a counter that mends for its price, anywhere else by his own hand
    // from kSelfRepairLevel (MuMain's inventory repair). Refused, whole and silent, with neither,
    // nothing to mend or not the Zen for it.
    bool repair(int slot);
    // Every piece, in slot order -- worn, then the bag -- as RepairAllItems walks, stopping at
    // the first he cannot pay for. How many were put right. At a counter only: the inventory's
    // repair in MuMain is one piece a click.
    int repairAll();
    // The fraction of a point a worn slot has lost and not yet shown: what the rate is proved
    // by, since a whole point is two thousand health away.
    double wearOwed(int slot) const { return slot >= 0 && slot < kWorn ? wearCarry_[slot] : 0.0; }

    // ---- the vault ------------------------------------------------------------------------
    // Baz's, opened by a Talk order arriving within `kCounter` of a vault keeper (NPC 240) and
    // closed by any other order, as a counter is. The townsperson's index, or -1.
    //
    // The moves are asked between ticks, as a purchase is, and each is refused whole while the
    // vault is shut, he has walked out of reach or he is dead. No swaps: a thing lands on a clear
    // rectangle or not at all, which is what a drop between two windows in MU does too
    // (CNewUIInventoryCtrl refuses a target that is not free).
    int banking() const { return banking_; }
    void closeVault() { banking_ = -1; }
    const Vault& vault() const { return vault_; }

    // ---- the Chaos Machine (sim/machine.h) -------------------------------------------------
    // The Chaos Goblin's box, opened by a Talk order arriving within `kCounter` of him and
    // closed by any other order, as the vault is. The townsperson's index, or -1.
    //
    // Closing it puts what is in it back in the bag, as MU's machine never keeps anything
    // (OpenMU's temporary storage goes back on close). What will not fit stays in the box and
    // is there the next time he opens it, and the save keeps it.
    int mixing() const { return mixing_; }
    // The Messenger of Archangel's window (sim/event.h): his folk row while it is open, or -1.
    // Opened by a Talk reaching him, shut by any other order, as a counter is.
    int gating() const { return gating_; }
    // Why he would not let him into `castle` (1 to kCastles) now, or None: what the window shows
    // and Enter asks again.
    CastleRefusal castleRefusal(int castle) const;
    // The cloak in the bag the Messenger would take for `castle`, one of its level, or -1 -- or
    // with `castle` 0 any cloak, the first.
    int cloakSlot(int castle = 0) const;
    // Enter: checked now, and at the next tick's start the cloak spent and the castle's gate
    // passed (Gated); false and nothing done when he would refuse.
    bool enterCastle(int castle);
    // Blood Castle's run, while he is in one (sim/event.h), and the whole seconds left of its
    // wait or of its time.
    const CastleRun& castleRun() const { return run_; }
    int castleSecondsLeft() const;
    // The Archangel's page (sim/event.h): his folk row while it is open, what it shows, and Give
    // -- checked now, the staff taken and the win paid at the next tick's start.
    int angeling() const { return angeling_; }
    AngelState angelState() const;
    int staffSlot() const;
    bool handInStaff();
    // Complete, on his thanks: the win paid into the bag and he is sent to Devias at the next
    // tick's start (the user, 2026-10-05: 'when char clicks it we close window and take items
    // and teleport to devias'). False before the weapon is given or once paid.
    bool claimCastle();
    void closeAngel() { angeling_ = -1; }
    // Farewell: his window shut, as walking away shuts it.
    void closeGate() { gating_ = -1; }
    void closeMachine();
    const Machine& machine() const { return machine_; }
    // Bag to box: a bag slot, or a worn one straight off him (2026-10-05), to a cell, or -1 for
    // the first it fits in. A jewel let go on a thing in the box it works is applied there. The
    // cell, or -1 refused. Refused while the last mix's answer is still in it -- MuMain locks
    // the box at MIX_FINISHED until it is reopened, and here until it is emptied.
    int putIn(int bagSlot, int cell = -1);
    // Box to bag: a cell to a bag slot, or -1 for the first slot it fits. The slot, or -1.
    int takeOut(int cell, int bagSlot = -1);
    // Inside the box, from one cell to another.
    bool shuffle(int from, int to);
    // What the box is to a service as it stands: sim::judge, for his class.
    Judged judged(Service service = Service::Combine, int socket = -1) const;
    // Runs a service on the box: refused, whole, when it is not ready or he has not the Zen.
    // Pays, rolls off the machine's own dice, and says What::Mixed. The answer is left in the box.
    bool mix(Service service = Service::Combine, int socket = -1);
    // Whether the box holds the last mix's answer, untouched since.
    bool mixed() const { return mixed_; }
    // Laid on the realm from the save.
    void restoreMachine(const Machine& saved) { machine_ = saved; }

    // ---- the quests (sim/quests.h) ------------------------------------------------------------
    // A giver's dialog, opened by a Talk order arriving within `kCounter` of him and closed by
    // any other order, as a counter is: his index in Tables::folk, or -1.
    int questing() const { return questing_; }
    void closeQuest() { questing_ = -1; }
    // Where a townsperson stands now: his body's tile when he walks rounds (realm_folk.cpp) or
    // guards a post, else his table's. False for an index off the table.
    bool folkTile(int folk, int* column, int* row) const;
    // Which tiles are road, a byte a tile in the grid's order, for the townsfolk's rounds to
    // keep to (Router::plan's `byRoad`). Handed over by the drawing, which has the ground's
    // painted slots; a realm never given one -- a headless run -- walks them as before.
    void setRoads(std::vector<uint8_t> roads) {
        roads_ = std::move(roads);
        router_.setRoads(&roads_);
    }
    const QuestProgress& quest(int index) const { return quests_[index]; }
    // A step's goal: its row's count, or for a Clear with none the breed's population here.
    int questGoal(int index, int step) const;
    // Whether the giver would offer it now: never taken, or resting and its time has come.
    bool questOffered(int index) const;
    // Whether it waits on another quest (QuestRow::afterAny): never taken, and none of those
    // handed in yet.
    bool questLocked(int index) const;
    // The quest a giver stands behind now, or -1. One for most; a chain for the Golden Archer
    // (the Dungeon's three floors, quests.cpp), which shows the link under way or ready, else
    // the first one offered and not handed in, else one come back round, else the last handed in.
    int questHere(int32_t giver) const;
    // Whether it waits on his level alone: one his class may take, its chain open, never handed
    // in, and he below its QuestRow::minLevel. The list shows it greyed with the level it asks.
    bool questUnderLevel(int index) const;
    // What a giver's window lists, in table order (QuestDialog::kList): each quest of his this
    // class may take that is under way or ready, offered, waiting on his level alone, or a
    // repeat resting. Into `out` (kQuests long); how many. Ours, as WoW's gossip list.
    int questsAt(int32_t giver, int* out) const;
    // Whether his window opens on that list rather than one quest's page: more than one, or one
    // waiting on his level (its page would only refuse).
    bool questListed(int32_t giver) const;
    // The wall clock, in unix seconds, which a repeating quest waits on. Handed in by the game;
    // a run that never sets it (the headless hunt) never sees a quest come back.
    void setWallClock(int64_t unixSeconds) { wall_ = unixSeconds; }
    // The Golden Invasion (realm_invasion.cpp). The game says each frame whether it is raining
    // over the map, and as a wet spell begins the realm rolls kInvasionChance for the dragons;
    // --invasion's `invade` begins one at once. Nothing on a map with no invasion, or one whose
    // dragon is not cooked.
    void invasionRain(bool raining);
    bool invade();
    InvasionPhase invasionPhase() const { return invasion_.phase; }
    // Ticks until the dragon lands, while it is coming; 0 otherwise.
    int64_t invasionLandsIn() const {
        return invasion_.phase == InvasionPhase::Entering ? std::max<int64_t>(0, invasion_.landsAt - tick_)
                                                          : 0;
    }
    // The dragon's body, or null on a map with no invasion.
    const Body* invader() const {
        return invaderSlot_ >= 0 ? &bodies_[size_t(invaderSlot_)] : nullptr;
    }
    // ---- the Golden Dragon's raid (sim/raid.h, realm_raid.cpp) ------------------------------
    // Set BEFORE raise or not at all: the raid itself (never set, the invasion is the plain
    // one), how many players the dragon is tough for, and the party that fights it -- its first kit is the hero's own, laid on
    // him at the raise; the rest are raised as raiders. `hand` lets the raiders' mind drive the
    // hero too, for the headless runs (tools/raid, sim_test); in play he is the player's.
    void setRaid(int players, std::vector<RaiderKit> party = {}, bool hand = false) {
        raidPlayers_ = players;
        party_ = std::move(party);
        raidHand_ = hand;
        raidAsked_ = true;
    }
    // Why this kit could not be worn as it stands -- a piece unknown, refused by its class or
    // its points, or in the wrong slot -- or empty. The real gates (sim::movable), on a scratch
    // satchel. Asked by the raise for every kit, and by the tests.
    std::string kitRefusal(const RaiderKit& kit) const;
    RaidStage raidStage() const { return raid_.stage; }
    bool raidAloft() const { return raid_.aloft; }
    int64_t raidLandedAt() const { return raid_.landedAt; }
    const Hazard* hazards() const { return hazards_; }
    // The raiders, alive or dead, in party order after the hero (empty with no party).
    int raiderCount() const { return int(raiderSlots_.size()); }
    const Body* raiderAt(int index) const {
        return index >= 0 && index < raiderCount() ? &bodies_[size_t(raiderSlots_[size_t(index)])]
                                                   : nullptr;
    }
    // The minions' bodies, up or down.
    int minionCount() const { return int(minionSlots_.size()); }
    const Body* minionAt(int index) const {
        return index >= 0 && index < minionCount() ? &bodies_[size_t(minionSlots_[size_t(index)])]
                                                   : nullptr;
    }
    // Potions a body of the party has left: the hero's at [0], the raiders' after.
    int raidPotions(int index) const {
        return index >= 0 && index <= kRaidersMost ? raidPotions_[index] : 0;
    }
    // A test's and the demo's (--raid-stage): once it stands, its health laid at the top of
    // `stage`'s band, so that stage begins on the next tick.
    void raidSkipTo(RaidStage stage);
    // --castle-open's: the Messenger's entry open at any hour, for a test (Realm::castleRefusal).
    void openCastleDoor() { castleOpen_ = true; }
    bool castleDoorHeld() const { return castleOpen_; }
    // --castle-free's: no run, and the entrance, the bridge's gap and the door all open, to walk
    // the castle (a test).
    void freeCastle();
    // --castle-bridge's: the run started now if it waits, its first quota met, and the
    // drawbridge falling `seconds` from now, to be watched (a test).
    void dropCastleBridge(int seconds);
    // Which castle this run is, 1 to 6, as the Messenger let him into it: the garrison raised
    // on castle 1's nests becomes that castle's own breeds (sim/event.h kCastleBreeds), and the
    // Statue of Saint one of the three at random, at that castle's health. Asked once, after
    // the raise and before the run starts (app/modes/play_mode.cpp).
    void setCastle(int castle);
    // Which castle the Messenger last passed him into, or 0: carried by the mode to the
    // castle's own realm (Realm::setCastle).
    int castlePassed() const { return castlePassed_; }
    // The Archangel weapon this run's Statue of Saint holds -- the staff, the sword or the
    // crossbow -- as an item index, or -1: what his page asks for and pictures.
    int32_t castleWeaponItem() const;
    // A test's: the monster `id` felled by the hero's hand, through the kill every blow ends in
    // -- its quota, its drop, its rise -- without the walk and the swings (tests/sim_test.cpp).
    void smite(uint32_t id) {
        const Body* found = find(id);
        if (found == nullptr || !found->monster() || !found->alive()) return;
        Body& beast = *const_cast<Body*>(found);
        beast.health = 0;
        kill(beast, bodies_[0]);
    }
    int64_t wallClock() const { return wall_; }
    // Whether his class may be paid this choice: the item's own class bits, as a purchase asks.
    bool questChoiceFits(int index, int choice) const;
    // Accept, at the giver: offered, and his dialog open. Refused whole and silent otherwise.
    bool acceptQuest(int index);
    // Hand in, at the giver: ready, the choice his class may take (or -1 when none is offered
    // him), and room in the bag for all of it before anything is given. Pays and rests it.
    bool completeQuest(int index, int choice);
    // Whether he is his class's second, Sevina's treasure handed in (sim::promoted).
    bool promoted() const { return bodies_[0].second; }
    // Whether a kill on this tile may leave a class's treasure: the Lost Tower's last floor, or
    // Atlans (Realm::treasure).
    bool treasureGround(int column, int row) const;
    // ---- travel (sim/travel.h) ---------------------------------------------------------------
    // The rows he has opened, a bit a row: his birth town's, each town whose giver he has spoken
    // to, each giverless map he has stood in.
    uint32_t found() const { return found_; }
    // Why a trip on that row would be refused now, or None.
    TravelRefusal travelRefusal(int index) const;
    // The quest a row waits on (TravelRefusal::Quest): a Dungeon floor's link of the Golden
    // Archer's chain, taken; a Lost Tower floor's (2-7) of Tersia's, handed in; the n-th
    // floor the n-th link; -1 for a row that waits on none.
    int travelQuest(int index) const;
    // The row he is standing in on a map of several (which of the Dungeon's floors), or -1.
    int travelFloor() const;
    // And the row a tile is on, or -1: the bot's (tools/bot), which hunts the floor it stands on.
    int floorAt(int column, int row) const;
    // Pays for it and puts down whatever he had open; the map change is the game's. Refused whole
    // for any reason travelRefusal gives.
    bool travel(int index);
    // Put him down on another floor of the map he is on -- the Dungeon's stairs, and any trip
    // that lands on this same map -- at once, as a Town Portal does: stopped, nothing selected,
    // the monsters on him losing him, her summon dismissed, facing (dx, dy) as sim/gates.h
    // stores one, on the nearest standable tile to (column, row). Says What::Climbed with
    // `gate` (-1 when no gate took him), which the game draws as a warp's landing.
    void setHeroDown(int column, int row, int dx, int dy, int gate = -1);

    // Laid on the realm from the save, or emptied. Never refused: it is the account's.
    void restoreVault(const Vault& saved) { vault_ = saved; }
    // Bag to vault: a bag slot -- or a worn one, straight off him (the user, 2026-10-05: 'allow
    // to put items from equipment to warehouse and reverse') -- to a vault cell, or -1 for the
    // first cell it fits. A jewel let go on a thing it works is applied there instead
    // (refineAcross). The cell, or -1 refused.
    int deposit(int bagSlot, int cell = -1);
    // Vault to bag: a cell to a bag slot, or -1 for the first slot it fits; or onto a worn slot,
    // put on as the bag puts it on (`move`'s gates), what it takes off going back to the vault.
    // A jewel onto a thing it works is applied. The slot, or -1.
    int withdraw(int cell, int bagSlot = -1);
    // **A jewel across the windows** (the user, 2026-10-05: 'allow me to upgrade item from
    // warehouse to inventory and reverse', 'allow to upgrade items with jewels while item is
    // inside chaos machine'): a Bless, Soul, Life or Rune of Creation in the bag, the vault or the
    // Goblin's box, onto a thing carried, worn, in the vault or in the box -- `refine`'s own roll
    // and rules, run on a stage of the bag and written back where each came from. False refused.
    enum class Store : uint8_t { Bag, Vault, Machine };
    bool refineAcross(Store jewelIn, int jewelAt, Store thingIn, int thingAt);
    // Whether this jewel would work on this thing: set, refined or enlivened.
    bool worksOn(const Held& jewel, const Held& thing) const;
    int wearFromVault(int cell, int worn);
    // Whether the last vault or box move was a jewel applied (refineAcross), once: the game rings
    // the jewel's sound off it, as Play::refine rings its own.
    bool takeJeweled() {
        const bool was = jeweled_;
        jeweled_ = false;
        return was;
    }
    // Inside the vault: from one cell to another, onto a clear rectangle.
    bool rearrange(int from, int to);
    // Zen across the counter, refused whole where there is not that much to move.
    bool depositZen(int64_t zen);
    bool withdrawZen(int64_t zen);

    // ---- the ground (sprint 7) ------------------------------------------------------------
    const std::vector<Lying>& lying() const { return lying_; }
    void step();

    int64_t tick() const { return tick_; }
    const std::vector<Happening>& happenings() const { return happenings_; }
    const std::vector<Body>& bodies() const { return bodies_; }
    // The map's traps (sim/traps.h), raised with the realm. Not bodies: nothing targets them,
    // they never move and never die, and they fire on their own clock (realm_traps.cpp).
    struct Trap {
        int32_t number = 0;
        int32_t column = 0, row = 0;
        int32_t dx = 0, dy = 0;
        int64_t firesAt = 0;
    };
    const std::vector<Trap>& traps() const { return traps_; }
    // Her summon's body, alive or dormant, or null before `raise` (realm_summon.cpp).
    const Body* summoned() const {
        return summonSlot_ >= 0 ? &bodies_[size_t(summonSlot_)] : nullptr;
    }
    const Body* find(uint32_t id) const;
    const Body& hero() const { return bodies_[0]; }
    // A skill's clip is still running, so he is locked where he stands: no step, no re-path.
    // Asked by `accept`, which drops the orders that would move him, and by the pointer, which
    // does not draw a destination marker for a walk that is not going to happen.
    bool casting() const { return tick_ < bodies_[0].castUntil; }
    // A hold a click to move cannot end -- Teleport's. Every other cast is broken by the click
    // (Realm::accept), so the pointer lets the walk through.
    bool held() const { return casting() && !bodies_[0].castBreaks; }
    // What he was last told and is still doing: the drawing's flinch halts a walk and not a
    // chase, and reads which it is here.
    const Request& order() const { return order_; }
    const content::Tables* tables() const { return tables_; }
    // Sets or clears `bits` on a box of this realm's grid: Blood Castle's run opening its
    // entrance, bridge and door (sim/event.h). Only on a map whose tables the realm copied at
    // raise (kBloodCastleMap); anywhere else the cooked tables are shared and nothing changes.
    // The router reads the grid live, so the next plan sees it.
    bool changeGrid(int x1, int y1, int x2, int y2, uint16_t bits, bool set);
    const Router& router() const { return router_; }
    uint64_t draws() const { return dice_.draws(); }
    RealmCounts counts() const;

private:
    Body* body(uint32_t id);
    Arms armsOf(const Body& one) const;
    void reswing(Body& hero);
    // The boon's own field put back over what `reckon` just wrote: reckon starts a fighter from
    // his class and his points, so it clears `damageTaken`, and a guard raised before a level-up
    // or a change of armour would lapse silently in the middle of its four seconds.
    void keepBoon(Body& hero);
    void advance(Body& one);
    bool turn(Body& one);
    void rouse(Body& beast);
    void think(Body& beast);
    void accept();  // the pending order becomes the order, before the hero moves
    void press();   // and what the order does once he has
    void wander(Body& beast);
    void retreat(Body& beast);
    void engage(Body& one, const Body& target);
    // `force` is the skill multiplier on the blow, 1 for an ordinary swing. It multiplies the
    // damage after the roll, the defence and the level floor, which is where OpenMU's own
    // `SkillMultiplier` falls (AttackableExtensions.cs:226-247).
    // `row` is the skill the blow belongs to, null for a swing: a spell rolls the wizardry sum,
    // and only a swing or a primary pays mana back. `thrown` marks a blow that flew.
    void strikeAt(Body& attacker, Body& target, float force = 1.0f,
                  const SkillRow* row = nullptr, bool thrown = false, bool pays = true);
    // The player's blow: begun now, landing half a swing from now, and dropped whole if he is
    // given another order before it lands. `land` is what the tick calls when it is due.
    void begin(Body& hero, uint32_t at, float force, int32_t skill, int32_t overTicks);
    void land(Body& hero);
    // What `land` does once the blow is due: the skill let go, flown, rained or swept, or the
    // swing struck. An Arcane Echo's second throw comes through here too, without the landing.
    // `spot`, a shower's ground in tiles (x, y), or null to fall round the body `at`.
    void release(Body& hero, uint32_t at, float force, int32_t skill,
                 const float* spot = nullptr);
    // Whether his staff's Arcane Echo answers this cast: each socket carrying it rolls, off the
    // runes' own dice. Draws nothing unless one is worn, so the seeded log does not move.
    bool echoes(Body& hero);
    // How many Pyroblasters his hands carry (sim/items.h), and the chain one rolls for off a Fire
    // Ball that landed on the body `struck`, which stood at (x, y).
    int pyroblasts(const Body& hero) const;
    void pyroblast(Body& hero, uint32_t struck, float x, float y, float force);
    // A spell let go: into the air for as long as it takes to cross the gap, and landed by
    // `arrive` on the tick it gets there. Past his hand, a new order no longer takes it back.
    // `announce` says `Loosed` (one wave is drawn per cast, so a line says it once); `pays` is
    // whether the landing pays mana back (a line pays for the body it was aimed at only).
    // `delay`: ticks the rock waits in the sky before its fall begins -- a Meteorite's rain is
    // not one volley (Realm::rain).
    void loose(Body& hero, const SkillRow& row, uint32_t at, float force, bool announce = true,
               bool pays = true, int32_t delay = 0);
    // Power Wave: one `Loosed` for the cast, and a flight to every body in the line.
    void looseLine(Body& hero, const SkillRow& row, uint32_t aimedAt, float force);
    void arrive();
    // An archer's shot: one piece of ammunition off the hand her bow leaves free, reloaded from
    // the bag first when that hand is empty. False, and nothing spent, when there is none.
    bool nock(Body& hero);
    // Whether `nock` would find one, spending nothing: what Skillshot asks before it is cast.
    bool quivered(const Body& hero) const;
    // Whether an attack order would only draw her empty bow: an archer with nothing to nock
    // and no skill on the order she can throw instead. Refused before the fight begins.
    bool arrowless(const Body& hero, const Request& order) const;
    // The plus of the quiver in the hand her bow leaves free, 0 when it holds none of hers.
    int quiverPlusOf(const Body& hero) const;
    // Skillshot let go: `arrows` lanes fanned round the body it was aimed at, an arrow into
    // every body in each lane, each paid for as it is loosed.
    void looseFan(Body& hero, const SkillRow& row, uint32_t aimedAt, float force);
    // And the arrow let go at the bottom of the swing, into `flights_` like a spell.
    void looseArrow(Body& hero, uint32_t at, float force);
    // Meteorite: a rock let go at every body within its splash of the one it was called on.
    void rain(Body& hero, const SkillRow& row, uint32_t aimedAt, float force);
    // A shower (SkillRow::showers): `kShowerRocks` rocks at random within the splash of `spot`
    // (tiles), or of the body `aimedAt` when there is none, each said as a `Loosed` with no body
    // and its ground in `x`/`y`; every monster within `kRockBlast` of one, in its sight, takes
    // one rock's blow when they land.
    void shower(Body& hero, const SkillRow& row, uint32_t aimedAt, float force,
                const float* spot);
    // Flame: a fire lit on the tile of the body it was thrown at, and each tick's due strikes.
    void light(Body& hero, const SkillRow& row, uint32_t aimedAt, float force);
    void burn();
    // A poisoned body's pulse, when it is due -- a monster's or the hero's.
    void poisonPulse(Body& beast);
    // Immolate's or Scorch's roll, and the burn it lights on `struck` off a blow of `wound`;
    // whether it lit.
    bool ignite(Body& hero, Body& struck, int wound);
    // A burning monster's pulse (the Immolate rune), when it is due.
    void burnPulse(Body& beast);
    // Whether this monster's blow poisons the hero (realm_tuning.h, kPoisoners).
    bool poisons(const Body& monster) const;
    // Whether a tile is within a guard post's clearing on the cleared map, where no nest puts a
    // monster down (raise, raiseBeast).
    bool nearPost(int column, int row) const;
    // Whether a poison is on it still, pulses to come.
    bool poisoned(const Body& one) const { return one.poisonUntil != 0 && one.poisonUntil >= tick_; }
    // Whether this monster's blow ices the hero (realm_tuning.h, kChillers), and icing him when
    // it does and he is not iced already -- on a hit and on a miss alike.
    bool chills(const Body& monster) const;
    // Whether a monster's blow is fire, for his Fire resistance (sim::Affix).
    bool fireBlow(const Body& monster, bool flame) const;
    // A breed whose blow comes in parts (kSplitBlows), and its parts after the first landing on
    // their ticks.
    const SplitBlow* splitOf(const Body& monster) const;
    void beamOn(Body& beast);
    // Whether this blow is a boss's Flame of Evil (realm_tuning.h kBosses): one in five, off
    // bossDice_; false and no draw for every other breed.
    bool bossBlow(const Body& monster);
    void chillHero(const Body& attacker, Body& target);
    // Whether a monster turns Ice's or Poison's element aside: its OpenMU resistance, rolled on
    // `dice` only when it has one, so a breed with none takes no draw (kResistances).
    bool resists(const Body& target, bool ice, Random& dice) const;
    // Whether his worn resistance `resistance` turns an element aside (docs/jewellery.md).
    bool heroResists(int resistance);
    // Teleport: where a blink toward `column, row` lands -- pulled back to its reach, and off a
    // wall toward him -- or false when nowhere on the line will take him.
    bool blinkTo(const Body& hero, const SkillRow& row, int column, int row_, int* outColumn,
                 int* outRow) const;
    // And putting him down there, on the tick the fade-out ends.
    void blink(Body& hero);
    // Walks him to within `radius` of what he is fighting, on the chase's own re-plan clock --
    // and with `sight`, to a tile from which nothing walls it off (`seen`).
    void approach(Body& hero, const Body& target, int radius, bool sight = false);
    // **Nothing is thrown through a wall** (the user, 2026-10-02: "dont allow to cast multi-shot
    // or other class skills throught walls"): whether a straight line from one body to the other
    // crosses only tiles a body could stand on (Route::sees, kWallNoMove). A body beside him is
    // always seen -- an arm's length is not through anything. Ours: 0.75 asks no wall of a skill.
    bool seen(const Body& from, const Body& to) const;
    bool seen(float fromX, float fromY, const Body& to) const;
    // Whether the quick slot's skill could be thrown now but for the cooldown and the reach:
    // learned, his class's, the right hand, the mana. What a right-click falls back to the
    // weapon on.
    bool armed(const Body& hero, const SkillRow& row) const;
    void dropBlow(Body& hero) { hero.blowAt = 0; hero.blowTarget = 0; hero.blowGround = false; }
    // A skill thrown, with the refusals in OpenMU's own order. False and silent for each.
    bool throwSkill(Body& hero, const SkillRow& row, uint32_t at);
    // Whom an area skill catches, in the order it strikes them: nearest first, then clockwise
    // from north, then by id. Written into `victims` and the count returned, never more than
    // `room` -- a fixed array on the caller's stack, because this runs inside a tick.
    int gather(const Body& hero, const SkillRow& row, uint32_t* victims, int room) const;
    // And the blow itself, once the arm comes down: one roll a target, in that order.
    void strikeAround(Body& hero, const SkillRow& row, float force);
    // The knock: one tile at random, onto something standable. 0.75's `movesTarget`.
    void shove(Body& target);
    // His weapon's socketed powers (sim/items.h), after his swing lands on `struck` for `wound`:
    // each one rolls, Stormcall's lightning or Meteor's rock on another monster near him, Ice's
    // chill or Poison's pulses on `struck` itself. Draws nothing unless one is worn.
    void stormcall(Body& hero, Body& struck, int wound);
    // One power's roll and, when it answers, its lightning, rock, chill or poison.
    void callDown(Body& hero, Body& struck, const PowerRow& power, int wound);
    // A magic rune's blow: his swing's roll with his energy's band on top (sim::kRuneEnergyLow
    // and High), at `force`, unpaid. Stormcall's lightning and the knight's fire runes.
    void runeStrike(Body& hero, Body& target, float force);
    // Spirit Plague's and Plague Arrows' poison on what a blow of `blowDamage` struck.
    void plague(Body& hero, Body& target, int blowDamage, int worn);
    // That poison laid on, unrolled: a Plague Arrows lane's arrow.
    void envenom(Body& hero, Body& target, int blowDamage);
    // Set while a Plague Arrows lane's arrows are loosed, so their flights carry the poison.
    bool loosingPlague_ = false;
    // The Lightning push: one tile straight away from `from`, slid over `kPushTicks`, onto
    // something standable or not at all.
    void push(Body& target, const Body& from) { push(target, from.x, from.y); }
    void push(Body& target, float fromX, float fromY);
    // A knight's Whirlwind on a Twisting Slash (sim/items.h): the force the slash strikes with,
    // and on its chance the monsters beyond the slash pulled in and struck.
    float whirl(Body& hero, const SkillRow& row, float force);
    // The channel's tick: a pulse when one is due, and the end when it is over.
    void channel(Body& hero);
    // How long the clip this skill plays takes, and so what its cooldown cannot go under.
    int32_t clipTicksOf(const Body& hero, const SkillRow& row) const;
    // A body's breed number, MU's monster index; -1 for the player and anything without a row.
    int32_t numberOf(const Body& one) const {
        return one.kind >= 0 && size_t(one.kind) < tables_->kinds.size()
                   ? tables_->kinds[size_t(one.kind)].number
                   : -1;
    }
    void kill(Body& beast, Body& killer);
    // The town's guards: raised off the folk table once the monsters are placed, and each tick
    // looking for a monster near his post, going for it, and walking back when it is dead.
    void raiseWardens();
    void raiseTraps();
    void fireTraps();
    void watch(Body& guard);
    // A townsperson's rounds (realm_folk.cpp, kStrollers): raised beside the guards, as a body
    // with `warden` naming his folk row, and walked stop to stop -- standing still and turning
    // to the hero while the hero talks to him.
    void raiseStrollers();
    void stroll(Body& walker);
    // The summon's turn: guard her, peel what is on her, follow her, fight (realm_summon.cpp).
    void tend(Body& summon);
    // Put down on an open tile behind her that sees her, and said as a Teleport's `Blinked`.
    // False when there is none, and it walks.
    bool blinkSummon(Body& summon, const Body& owner);
    // Its level and row off its breed and her level and points (skills.h): at the cast, and on
    // every tick it stands, keeping its share of health when the maximum moves.
    void fitSummon(Body& summon, const Body& hero);
    // Raised beside her off the row's breed, scaled by her energy; or false with no breed cooked.
    bool conjure(Body& hero, const SkillRow& row);
    // Gone: dismissed, or with her death. Nothing drops and nothing rises.
    void dismiss(Body& summon);
    // A monster a guard fought has died with the hero's help: the guard points him on and turns to
    // look toward the nearest of its kind still standing.
    void pointOn(Body& guard, const Body& dead);
    void gain(Body& hero, int32_t award);
    void raiseBeast(Body& beast);
    void reviveHero();
    // A standable tile in the map's spawn box, drawn off the dice: where a death rises and where
    // a Town Portal Scroll lands, which in OpenMU are the same SafezoneSpawnGate. His own tile
    // when the map has no box.
    std::pair<int, int> haven();
    // Puts him down on a tile with nothing in hand: no walk, no order, no blow, no counter.
    void setDown(Body& hero, int column, int row);
    // MuMain's CheckGate on the tile he has just stepped onto: through an enter gate when his
    // level allows (`Gated`, and true), told he is too low when it does not (`Barred`).
    bool throughGate(Body& hero);
    // The rest of a gate once it lets him through: the landing in the target's box, and the
    // map change said (Gated) or the floor of this map he is put down on.
    bool passGate(Body& hero, const EnterGate& gate);
    void rearm(Body& hero) { rearm(hero, bag_); }
    // And off another satchel: a raider's own kit (realm_raid.cpp), by the same rules.
    void rearm(Body& hero, const Satchel& kit);
    // A blow's wear on the player's gear: `took` the health a blow took off him, which wears one
    // defending piece; `landed` a blow of his that did harm, which wears the weapon.
    // Player.DecreaseItemDurabilityAfterHitAsync and DecreaseWeaponDurabilityAfterHitAsync.
    void wearOnTaken(int took);
    void wearOnLanded(int defense);
    // Takes `amount` off one worn slot, the fraction kept in `wearCarry_`, and re-reckons him
    // when a whole point goes.
    void wearDown(int slot, double amount);
    void leave(const Body& dead, const Body& killer);
    std::pair<int, int> clearing(int column, int row) const;
    bool bare(int column, int row) const;
    bool take(size_t index);
    void sip();
    void recover(Body& hero);
    // `byRoad`: kept to the roads where it can be (setRoads), for a townsperson's rounds.
    bool send(Body& one, int column, int row, bool byRoad = false);
    // The pass a body plans on: a monster's walls the safe zone off (content::kWallMonster),
    // everybody else's -- the hero, his summons, the guards and the town -- is the strict one.
    static uint16_t wallOf(const Body& one) {
        return one.monster() ? content::kWallMonster : content::kWallCharacter;
    }
    void halt(Body& one);
    // The Perch order's arrival, and the four things that end a pose. See Pose.
    void perch(Body& hero);
    void rise(Body& one);
    void settle(Body& one);
    bool beside(const Body& target, int radius, const Body& walker, int* column, int* row,
                bool sight = false, bool disc = false);
    bool drifted(const Body& chaser, const Body& target) const;
    bool worth(const Body& beast, const Body& target, int range) const;
    void say(What what, const Body& who, int32_t a = 0, int32_t b = 0, int32_t c = 0,
             uint32_t whom = 0);

    const content::Tables* tables_ = nullptr;
    // An event map's tables, copied at raise so its grid can change under the run and a raise
    // again starts from the cooked words. Null elsewhere. On the heap, so `tables_` survives
    // the realm being moved.
    std::unique_ptr<content::Tables> own_;
    Random dice_{0};
    Router router_;
    std::vector<uint8_t> roads_;  // see setRoads
    // [0] is the player; then the monsters, in spawn order; then the town's guards.
    std::vector<Body> bodies_;
    // Who is a player, by index, and where an id lives. Both are lists and not maps: an
    // unordered_map walked to produce a happening is the first thing the census warns about,
    // and ids here are handed out by one counter from 1, so the second is a plain lookup.
    // Without them, rousing 1005 monsters scanned 1005 bodies apiece -- a million comparisons
    // a tick, measured at 0.33 ms on Noria against Lorencia's 0.03, which is the whole budget
    // for a map with nothing happening on it.
    std::vector<uint32_t> players_;
    std::vector<uint32_t> indexOfId_;
    std::vector<Happening> happenings_;
    std::vector<Step> scratch_;
    Request pending_;
    Request order_;  // what the player is doing until told otherwise
    // The skill a key asked for and whom it was aimed at, held for a few ticks so a press inside
    // the swing it waits for is not lost. Cleared the moment it is thrown or it goes stale.
    int32_t wants_ = skill::kNone;
    uint32_t wantsAt_ = 0;
    int wantsColumn_ = -1, wantsRow_ = -1;  // a wish aimed at the ground; -1 for none
    int64_t wantsUntil_ = 0;
    // Spells in the air. A fixed handful, because a wizard at speed lets the next one go before
    // the last has landed, and this runs inside a tick; one that finds no room lands at once.
    struct Flight {
        int64_t at = 0;  // 0 for an empty place
        uint32_t target = 0;
        int32_t skill = 0;
        float force = 1.0f;
        bool pays = true;
        // A Pyroblaster's chain (sim/items.h): how many hops it has made, 0 for a plain flight,
        // and the monsters it has struck so far, the first his own Fire Ball's.
        int8_t hops = 0;
        uint32_t chained[kPyroblastChain + 1] = {};
        // A knight's Fireburst chain: each hop lands as his rune's blow (`runeStrike`), not as
        // the wizard's spell.
        bool swung = false;
        // A Plague Arrows lane's arrow: it poisons what it lands in (Realm::envenom).
        bool plague = false;
    };
    // A Pyroblaster's chain flying on from `off`, which it struck at (x, y): to the nearest
    // monster it has not struck yet, while it has hops left.
    void hop(Body& hero, const Flight& from, uint32_t off, float x, float y);
    // Room for a line's worth of bodies and the bolts around it.
    static constexpr int kFlights = 48;  // 32 until a Meteorite rained on 24 (2026-10-03)
    Flight flights_[kFlights] = {};
    // An Arcane Echo waiting to be let go: the spell again, at `at`. One at a time; a cast that
    // echoes while one waits does not.
    struct Echo {
        int64_t at = 0;  // 0 for none
        uint32_t target = 0;
        int32_t skill = 0;
        float force = 1.0f;
        // A shower's ground, fallen on again (Realm::shower).
        bool ground = false;
        float spot[2] = {};
    };
    Echo echo_;
    // Evil Spirit's blows held, his spell's or his shield's rune's (WebZen's SkillEvil): each on
    // one monster at its own tick, at the cast's force. Room for a crowd in ten tiles and a cast
    // or two over it, as the spell has no cooldown; a blow that finds no room is not held.
    struct SpiritBlow {
        int64_t at = 0;  // 0 for none
        uint32_t target = 0;
        float force = 1.0f;
        bool rune = false;  // his shield's, drawn in the rune's colour
    };
    static constexpr int kSpiritBlowsMost = 96;
    SpiritBlow spiritBlows_[kSpiritBlowsMost] = {};
    bool spiritsGoing() const;
    bool letSpiritsGo(Body& hero, float force, bool rune);
    void spiritStrike(Body& hero, const SpiritBlow& blow);
    // Flames burning on the ground (`SkillRow::burns`): where, when each strikes next and how
    // many strikes are left. A fixed handful -- one cast every five seconds and an echo lights
    // two -- and a fire that finds no room is not lit.
    struct Fire {
        int64_t next = 0;  // 0 for an empty place
        float x = 0.0f, y = 0.0f;
        int32_t skill = 0;
        int32_t left = 0;
        float force = 1.0f;
        uint32_t aimed = 0;  // the body it was thrown at, whose first strike pays back
        float dx = 0.0f, dy = 0.0f;  // tiles a tick it walks: Twister's storm (`SkillRow::walks`)
        bool rune = false;  // a knight's Twister rune's: each strike his rune's blow (`runeStrike`)
    };
    static constexpr int kFires = 8;
    Fire fires_[kFires] = {};
    bool undying_ = false;  // `undying`
    int64_t tick_ = 0;
    std::string refusal_;
    uint32_t nextId_ = 1;

    Satchel bag_;
    // Wear's own dice, off the realm's seed: which piece a blow wears is a draw, and taking it
    // from `dice_` would move every roll after it, so a seeded fight would change for a rule
    // that decides nothing in it.
    Random wearDice_{0};
    // And the guards' fights, for the same reason: a guard at a far gate killing what spawns on
    // his post rolls every few seconds, and off `dice_` that moved every roll in the run after
    // it -- a seeded hunt twenty tiles away fought different fights because of it.
    Random wardenDice_{0};
    // The summon's fights, either way round, roll off their own stream, as a guard's do: a run
    // without a summon is not moved by one existing.
    Random summonDice_{0};
    // The sockets' own (sim/items.h): whether a drop rolls them, and a power's chance and pick.
    // Off `dice_`, the one extra draw a drop moved every roll after it.
    Random runeDice_{0};
    // The traps' own, as the guards' are: a run on another map is not moved by the Dungeon's.
    Random trapDice_{0};
    // The bosses' own, for their one blow in five: a run with none of them is not moved.
    Random bossDice_{0};
    // A chiller's that ices one blow in some (kChillers): a run with none of them is not moved.
    Random chillDice_{0};
    // Whether a dungeon's kill leaves a Firecracker (sim/items.h), so a seeded hunt there rolls
    // its loot as it always did.
    Random crackerDice_{0};
    // Whether a kill in Atlans or the Lost Tower leaves a Loch's Feather (sim::kFeatherOdds).
    Random featherDice_{0};
    // And whether Atlans's strongest leave a piece of the second class's gear
    // (sim::kAtlansGearOdds), the piece and its options off the same stream.
    Random gearDice_{0};
    // Blood Castle's scroll and bone, rolled on every kill outside a castle (Realm::leave), off
    // a stream of their own so the kill's own drops draw as they did.
    Random ticketDice_{0};
    // Sevina's treasures (Realm::treasure), drawn only while one is sought.
    Random treasureDice_{0};
    // The Orb of Summoning's own roll on every kill (sim/items.h), so the rest draw as they did.
    Random orbDice_{0};
    // Where a Meteorite's rocks fall (Realm::shower), so a wizard's showers move no other roll.
    Random showerDice_{0};
    std::vector<Trap> traps_;
    // Where the one summon body sits in `bodies_`, or -1 before `raise`.
    int summonSlot_ = -1;
    // The Golden Invasion's dragon: one body, raised down at the end of `bodies_` on a map with
    // an invasion, and risen where it lands (realm_invasion.cpp); -1 elsewhere.
    int invaderSlot_ = -1;
    struct Invading {
        InvasionPhase phase = InvasionPhase::Quiet;
        int64_t landsAt = 0;
        int64_t endsAt = 0;
        bool raining = false;  // the weather the game last said, to see a spell begin
    } invasion_;
    bool invasionOwed_ = false;  // invade()'s, begun inside the next tick
    // Where the dragon comes down, so a landing moves no other roll.
    Random invasionDice_{0};
    void raiseInvader();
    void invasionTick();
    // Laid down for good: killed (Realm::kill) or its thirty minutes up.
    void endInvasion();
    // ---- the Golden Dragon's raid (realm_raid.cpp) ---------------------------------------------
    struct RaidState {
        RaidStage stage = RaidStage::None;
        bool aloft = false;
        int players = kRaidPlayers;
        int64_t landedAt = 0;
        int64_t nextMove = 0;      // the next Breath or Shock
        int64_t busyUntil = 0;     // a move in hand: it neither walks nor swings
        int64_t aloftSince = 0;
        int64_t nextStrafe = 0;
        int64_t nextStorm = 0;
        int64_t nextInferno = 0;
        bool secondWave = false;
        bool wiped = false;        // the hard enrage's Inferno told
        bool heroDown = false;     // the headless hand's hero has fallen: out, as a raider is
        uint32_t serial = 0;       // tells said, for the raiders' reactions
        // The threat each fighter holds on it, by the party's index (0 the hero, then the
        // raiders), and the summon's after them.
        float threat[kRaidersMost + 2] = {};
    } raid_;
    int raidPlayers_ = kRaidPlayers;
    // Whether a raid was asked for (setRaid). Until the drawing shows the raid (sprint 2 of
    // docs/golden-dragon-raid.md), an invasion with none is the plain one: no raid health, no
    // minions, no stages.
    bool raidAsked_ = false;
    std::vector<RaiderKit> party_;
    bool raidHand_ = false;
    std::vector<int> raiderSlots_;
    std::vector<int> minionSlots_;
    std::vector<Satchel> raiderBags_;
    Hazard hazards_[kHazards] = {};
    // Each fighter's reaction to the last tell: when it steps out, or never (-1).
    int64_t reactAt_[kRaidersMost + 1] = {};
    uint32_t reactSerial_[kRaidersMost + 1] = {};
    int raidPotions_[kRaidersMost + 1] = {};
    int64_t drinkAt_[kRaidersMost + 1] = {};
    // The dragon's own rolls, and the raiders': so a run with no raid is not moved by one.
    Random raidDice_{0};
    Random raiderDice_{0};
    void raiseRaid();
    void dressHero(const RaiderKit& kit);
    void fitRaider(Body& one, const RaiderKit& kit, const Satchel& bag);
    void raidTick();
    void raidAfter();
    void beginStage(Body& dragon, RaidStage stage);
    void bossThink(Body& dragon);
    void raidMove(Body& dragon);
    void strafe(Body& dragon);
    void storm(Body& dragon);
    void inferno(Body& dragon, bool shadows);
    void minionWave(Body& dragon);
    void hazardTick(Body& dragon);
    Hazard* layHazard(const Hazard& one);
    void scorch(Body& dragon, Body& one, float share);
    bool inHazard(const Hazard& h, float x, float y) const;
    bool isBoss(const Body& one) const {
        return invaderSlot_ >= 0 && &one == &bodies_[size_t(invaderSlot_)] &&
               raid_.stage != RaidStage::None;
    }
    // Whether a body fights on the party's side: the hero, a raider, her summon.
    bool partisan(const Body& one) const {
        return one.player || one.raider >= 0 || one.summoner != 0;
    }
    int partyIndex(const Body& one) const;
    void raid(Body& one, int index);
    bool dodge(Body& one, int index);
    void raiderStrike(Body& one, Body& target, const SkillRow* row);
    // The kit's satchel a party member is reckoned from: the hero's bag, or the raider's own.
    const Satchel& kitOf(const Body& one) const;
    // The Town Portal's warp, shared with Icarus's sending home (realm_items.cpp).
    void warpHome(Body& hero);
    // Set once Icarus has sent him home for want of wings, so the warp is said once while the
    // mode carries out the map change.
    bool grounded_ = false;
    // A summon a restored record carried, raised on the next tick rather than in restore(), so
    // its `Spawned` reaches the drawing (step() clears the happenings first). 0 for none.
    int32_t summonOwed_ = 0;
    int32_t summonOwedHealth_ = 0;
    // The fraction of a point each worn slot has lost and not yet shown, beside the item it
    // was lost by: a piece moved out and back starts its fraction again, which is under a point.
    double wearCarry_[kWorn] = {};
    int32_t wearItem_[kWorn] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    // The tick his weapon last wore on a landed blow (sim::kWeaponWearTicks).
    int64_t weaponWornAt_ = -1000000;
    int64_t money_ = 0;
    int trading_ = -1;
    std::vector<Sale> sold_;  // oldest first, at most kBuybacks
    int banking_ = -1;
    Vault vault_;
    bool jeweled_ = false;
    int mixing_ = -1;
    int gating_ = -1;  // see gating()
    // A castle Enter asked for, passed at the next tick's start (Realm::enterCastle), or 0.
    int castleOwed_ = 0;
    int castlePassed_ = 0;  // see castlePassed()
    CastleRun run_;  // see castleRun()
    int angeling_ = -1;     // see angeling()
    bool staffOwed_ = false;
    bool claimOwed_ = false;
    // The win's pay (CastleRun::paid*), into the bag, once.
    void payCastle();
    // The run's clock, once a tick (Realm::step), and a kill counted against its quotas.
    void castleTick();
    void castleKill(const Body& dead);
    // Blood Castle's Statue of Saint: never wakes, walks, turns or is pushed (WebZen
    // gObjMonster.cpp:1524-1529, ObjAttack.cpp:2713-2719; MuMain NotRotateOnMagicHit).
    bool fixed(const Body& body) const;
    void passCastle(int castle);
    Machine machine_;
    bool mixed_ = false;
    // The machine's own dice, off the realm's seed: a run that never mixes is not moved.
    Random mixDice_{0};
    QuestProgress quests_[kQuests];
    int questing_ = -1;
    // The quests' inner steps (realm_quests.cpp): Ready once every count is met; a thing come
    // into the bag counted; the bag slot holding an item; a kill's treasure.
    void questSettle(int index);
    void questMet(int32_t number);
    void questFound(int32_t item);
    int carried(int32_t item) const;
    void treasure(const Body& dead);
    uint32_t found_ = 0;  // the travel rows he has opened (sim/travel.h)
    bool byFloor_ = false;  // this map's rows open floor by floor (reachFloor)
    // On a map of several rows, which row's floor each tile is on (row-major, -1 for none): the
    // tiles walkable from that row's landing, flood-filled once as the map is raised.
    std::vector<int8_t> floors_;
    // Opens every row of `map`; and the rows a character is raised or restored with -- his birth
    // town's, and this map's when nobody here gives a quest (realm_travel.cpp).
    void discover(int32_t map);
    void settleFound(uint32_t saved);
    // A floor-split map's row, opened as he stands on its floor (settleFound's byFloor_).
    void reachFloor();
    // The walkers on their rounds, by body id: which stop, and when he leaves it.
    struct Stroller {
        uint32_t id = 0;
        const StrollRow* row = nullptr;
        int stop = 0;
        bool there = false;     // arrived at the stop and doing what it asks
        int64_t leaves = 0;     // the tick he goes on to the next stop
        bool held = false;      // the hero is talking to him
        int64_t freeAt = 0;     // after a talk, the tick he takes up his rounds again
        int perch = -1;         // the bench he sits on, while he sits
        int chatLine = 0;       // the next line of his talk at this stop
        int64_t chatAt = 0;     // the tick it is said
    };
    std::vector<Stroller> strollers_;
    int64_t wall_ = 0;
    bool castleOpen_ = false;  // see openCastleDoor
    // A monster the hero killed, counted against every live Clear of its breed.
    void countKill(const Body& dead);
    bool banked() const { return banking_ >= 0 && serving(banking_); }
    bool atMachine() const { return mixing_ >= 0 && serving(mixing_); }
    std::vector<Lying> lying_;
    int64_t potionUntil_ = 0;
    // A potion's worth arrives in three instalments, 20% 60% 20% at 200, 600 and 200 ms
    // (MU2's Realm.Consume, off OpenMU's handler). A fixed ring: a potion every half second
    // and three instalments a potion is at most six in flight.
    struct Sip {
        int64_t due = 0;
        int32_t amount = 0;
        bool mana = false;
        bool drunk = false;  // a potion's, and not a kill's life (kKillLifeTicks)
    };
    Sip sips_[8];
    int sipCount_ = 0;
};

// The one line a happening becomes in the seeded log. Fixed precision throughout: a `%g` of a
// float is a byte difference waiting for a compiler change, and the log's own formatting is
// part of the contract the two runs are compared under.
std::string describe(const Happening& happening, const Realm& realm);

}  // namespace mu::sim
