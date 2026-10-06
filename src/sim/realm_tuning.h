// The realm's own tuning: every constant the rules turn on, and the five small helpers that
// read them. Nothing outside sim/ may include this.
//
// `realm.cpp` was 1514 lines. It is four files now -- the spine (raising, the tick, accept,
// press, step), the items and the trade, the moving and the thinking, and the fighting -- all
// implementing the one `Realm` declared in `realm.h`. This was its anonymous namespace, and it
// had to become something all four can see.
//
// It is a better place for them than the top of one .cpp. docs/conventions.md's rule for this
// layer is that "every constant is traced to OpenMU's Version075, MuMain's own C++ or mu.db,
// with the source named in a comment, and a departure is marked `invention` on the line that
// makes it" -- and a reader checking that rule now has one page to read rather than a file to
// scroll. The comments below are the originals, unchanged.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "sim/realm.h"
#include "sim/recovery.h"

namespace mu::sim {

// The mana maximum after anything that moves it, and the pool raised by what the maximum
// gained -- the rule the health beside it follows, so a point in energy arrives full rather
// than as a bigger empty gem. A new hero starts at zero of zero and so starts full.
// Mana and the shield, re-reckoned after `reckon` (the shield reads the defence it just set).
// What a maximum gains arrives full; what it loses is clipped.
inline void restoreMana(Body& hero) {
    const int was = hero.maxMana;
    // And the excellent armour's +8% a piece (Excellence::manaRate).
    hero.maxMana = int(double(maximumMana(hero.kin, hero.level, hero.totalPoints()) + hero.excel.moreMana) *
                       hero.excel.manaRate);
    hero.mana = std::min(hero.maxMana, hero.mana + std::max(0, hero.maxMana - was));
    const int wasSd = hero.maxSd;
    hero.maxSd = maximumShield(hero.level, hero.totalPoints(), hero.stats.defense);
    hero.sd = std::min(hero.maxSd, hero.sd + std::max(0, hero.maxSd - wasSd));
}



// How far past its view range something can be and still keep a monster awake. Realm.cs:63.
constexpr int kMargin = 8;
// And never nearer than this, so whatever stands on the screen is awake and wandering: at sight
// 3 and the margin, Blood Castle's road stood frozen a dozen tiles from him, in plain view (the
// user, 2026-10-03: 'when BCmonsters is spawned theay are not wondering / moving'). Its own sight
// still decides what it attacks (Realm::think); this only lets it move. Ours, and Blood Castle's
// alone: everywhere else the seeded fights are as they were.
constexpr int kWakeAtLeast = 16;
// And how much further again it has to get before the monster goes back to sleep. **invention**,
// and it is here for the picture: a character standing exactly on the margin crosses it twice a
// second as he shuffles, and a monster that falls asleep is HALTED where it stands -- so the
// animal stopped and set off again every time, in the middle of a stride, which is one of the
// things "monsters get stuck" was. Two tiles of slack is the smallest thing that cannot be
// crossed by a walk between two ticks (a tile takes eight).
constexpr int kSleepSlack = 2;
// No leash any more (the user, 2026-09-30, WebZen's way, where MU2's bench had one of 10 and
// 20). A hit sets WebZen's chase count to ten steps of 400 ms (gObjMonster.cpp:998-1165), and
// past them the beast keeps him only while he is in its eyes; nor past the view box of fifteen
// tiles at all (:620-621). That is what lets a hero outrun what walks as fast as he does. And
// a beast wanders only within its MonsterSetBase row's Dis of home -- 30 on a box nest, every
// nest of these three maps but Devias's points (10 there, taken as 30).
constexpr int64_t kGrudgeTicks = 80;
constexpr int kLoseSight = 15;
// The span inside which a body is never walled off from him (Realm::seen): the eight tiles
// round his own, so a swing at a body on the far side of a wall's corner still lands. Ours.
constexpr float kArmsLength = 1.5f;
constexpr int kWanderReach = 30;
// And the leash back on top of them (the user, 2026-10-01: "monster leash is to far"): past
// kLeash tiles of home a beast lets go and walks back, twice that once it has been hit -- MU2's
// bench numbers, ours, as WebZen has none. A beast walking home notices nobody until it is
// within kHomeAgain of its nest, so it cannot turn on the hero at the leash's edge and turn
// back again; a blow still turns it.
constexpr int kLeash = 10;
constexpr int kGrudge = 20;
constexpr int kHomeAgain = 3;
// How often a chase re-plans, in ticks. Realm.cs:1918.
// How long a beast stands over what it has just killed before it turns away. **invention**, and
// the only number in this file put here for the sake of what the SCREEN shows rather than for
// the rules -- so it is worth saying exactly why it had to be.
//
// The sim resolves a death on the tick the blow lands, and `worth` drops a dead quarry at once,
// so a beast used to turn and wander on the very next tick. The DRAWING does not put the body
// down then: a fall waits for the killing blow to be seen landing, which is half a swing later
// (game/fx/showing.h, kLandingPoint). So the killer walked away while its victim was still on
// its feet, which is what a player reported seeing on 2026-09-22 and is plainly wrong however
// right each half is on its own.
//
// Twelve ticks is six tenths of a second: longer than the half-swing the cue waits, so the body
// is always down before anything moves, and short enough that it reads as a beast looking at
// what it did rather than as one that has frozen. It does NOT stop it defending itself -- the
// hold only applies where nothing else is worth attacking, so a second target still takes it.
//
// MU has no such number, because MU has nothing to be out of step with: its client kills a body
// on the packet (`SetPlayerDie`) with no animation gate at all. The deferral this covers for is
// MU2's invention and so, therefore, is this.
constexpr int kStandOverTicks = 12;
constexpr int kRepath = 3;
// A player's swing, in ticks, WHEN THE TABLES CANNOT SAY. Things.cs:249's 1000 ms, and it is
// now only a fallback: the swing is the length of the clip he swings with, and sim/swings.cpp
// works it out from what is in his hands. A cooked file with no attack actions in it -- an old
// one -- keeps this rather than being handed an interval invented out of no data.
constexpr int kHeroSwingTicks = 20;
// A player walks a tile in 400 ms, as every Lorencia monster does. The monster's 400 is traced
// (Lorencia.cs:87); the player's is MU2's derivation from MU's walk clip (Things.cs:583) and is
// borrowed from the monster row rather than invented separately.
constexpr int kHeroMoveTicks = 8;
// How fast a body comes round to where it is going, in degrees a second. MU snaps its facing;
// MU2 turned at 900 (Walker.cs:85, a bench number and marked as one). Invention: 1440, which is
// 72 degrees a tick, so that a click straight behind costs ONE tick on the spot rather than
// three -- the pivot is the one wait between a click and the first step, and at 900 it was the
// thing being waited for. The drawing interpolates the facing between ticks, so 72 degrees a
// tick still reads as a turn and not a snap.
constexpr float kTurnDegrees = 1440.0f;
// How far off its heading a body may be and still walk, in degrees. Under it, it sets off and
// finishes coming round as it goes, which is what walking round a corner is. Over it -- a click
// behind the character, a monster turning onto somebody who hit it from behind -- it turns on
// the spot first. Walker.cs:98-107 had 120, and that was the moonwalk: a body 119 degrees off
// covers ground for the ticks it takes to come round, which is gliding sideways and backwards
// under a walk clip that faces the other way. Invention: 60, less than one tick's turn, so the
// most a body ever travels off its facing is 60 degrees for one tick, and the drawn facing has
// already swung most of that by the time the step is drawn.
constexpr float kPivotDegrees = 60.0f;

// A player reaches one tile. Sprint 7's weapons have their own reach.
constexpr int kHeroAttackRange = 1;
// And six with any bow type drawn: MuMain's `Action()`, `Range = 6.f` whenever
// GetEquipedBowType is not BOWTYPE_NONE (ZzzInterface.cpp:1245-1261).
constexpr int kArcherReach = 6;
// How many tiles a breed's body stands past its own tile, which his reach takes as well, so he
// strikes it from its edge and does not walk into it. Ours (the user, 2026-10-04: 'attack
// distance is incorrect for char because monster is big'): MU reaches the tile alone and
// lets him stand inside a Hydra, whose body runs 2.8 m out from its tile, front and back. At 2
// he stood clear of it; 1 keeps him close against it ('little but closer').
constexpr int bulkOf(int32_t number) {
    return number == 49 ? 1 : 0;  // Hydra: a tile in, close against its body
}
// An arrow's flight: MU's `Direction[1] = -70` units a reference frame at 25, 1750 units -- 17.5
// tiles -- a second, stopping a tile short of the body as a spell does (MU2 Realm.cs:79).
constexpr float kArrowTilesPerSecond = 17.5f;
// The two ammunition rows in the bow group: Arrows for a bow, the Bolt for a crossbow.
constexpr int kArrowsNumber = 15;
constexpr int kBoltNumber = 7;
// How long a dead character lies there before he stands up in town. MU2's Player.cs:1687, three
// seconds, which is also when a summon is taken off him.
constexpr int kRiseTicks = 60;
// What comes back on its own -- the recovery rates and the shield's share -- is in
// sim/recovery.h, public so the HUD's cards can print the same numbers the realm spends.

// An angle folded into a half turn either side of nothing, so that a body facing just west of
// north turns a few degrees to face just east of it rather than most of the way round the other
// way. Walker.cs:288-309.
inline float wrapped(float angle) {
    constexpr float kTurnabout = 6.28318530718f;
    angle = std::fmod(angle, kTurnabout);
    if (angle > 3.14159265359f) angle -= kTurnabout;
    if (angle < -3.14159265359f) angle += kTurnabout;
    return angle;
}

// MU's own reckoning of distance: the larger of the two axis distances, not a Euclidean
// length. Things.cs:607.
inline float reach(const Body& one, const Body& other) {
    return std::max(std::fabs(one.x - other.x), std::fabs(one.y - other.y));
}

inline bool within(const Body& one, const Body& other, float range) {
    return reach(one, other) <= range;
}

// WebZen's gObjCalDistance (user.cpp:7042-7053, 1.00.93): the straight line between two tiles,
// cut to whole tiles. A monster notices a hero strictly nearer than its view
// (gObjMonster.cpp:899-903) and swings at one no farther than its reach (:2852) -- a disc,
// where the larger axis above makes a square, 1.75 times the ground at a view of 5 and a pull
// from seven tiles off on the diagonal. Asked by the monsters' eyes and arms only.
inline int apart(const Body& one, const Body& other) {
    const int dx = one.column() - other.column(), dy = one.row() - other.row();
    return int(std::sqrt(double(dx * dx + dy * dy)));
}

inline int strayed(const Body& beast) {
    return std::max(std::abs(beast.column() - beast.homeColumn),
                    std::abs(beast.row() - beast.homeRow));
}

// ---- the town's guards -----------------------------------------------------------------------
// The two townsfolk 0.75 gives a fighting row: OpenMU's Version075 NpcInitialization declares
// 247 and 249 `NpcObjectKind.Guard` with `GuardIntelligence` and these numbers, transcribed in
// MU2's shared/Folk.cs `Townsfolk.Watch`. Every field is theirs, the delays turned into ticks
// at 20 Hz: a 400 ms tile is 8, a 1500 ms swing 30. They differ in their reach alone -- five
// tiles for the crossbow, two for the berdysh.
struct WardenRow {
    int32_t number, level, health, minimumDamage, maximumDamage, defense;
    int32_t attackRange, viewRange, moveTicks, attackTicks, attackRate, defenseRate;
    // A guard of his own rules, ours: how many tiles he may step off his post (-1: the town
    // guards' kWardenLeash, and then he takes on anything within it), and his blow's share of
    // what it hits (0: kWardenShare). With a leash of his own he takes on only what comes within
    // his reach of the post plus that leash.
    int32_t leash = -1;
    float share = 0.0f;
};
constexpr WardenRow kWardens[] = {
    {247, 90, 10000, 180, 195, 70, 5, 7, 8, 30, 300, 100},  // Crossbow Guard
    {249, 90, 10000, 180, 195, 70, 2, 7, 8, 30, 300, 100},  // Berdysh Guard
    // The Golden Archer at the Dungeon's arch (docs/golden-archer.md), ours, the user's of
    // 2026-09-30: "has to protect the gate, by one shoting monster which come close ... dont
    // move to much just 1". A crossbow's six tiles, a step off his post at most, and a blow of
    // twice what it hits -- the band's spread and the defence cannot save it -- at an attack rate
    // no Dungeon monster's defence rate comes near, so he does not miss.
    {236, 90, 10000, 180, 195, 70, 6, 7, 8, 30, 1000, 100, 1, 2.0f},  // Golden Archer
};
inline const WardenRow* wardenRow(int32_t number) {
    for (const WardenRow& row : kWardens) {
        if (row.number == number) return &row;
    }
    return nullptr;
}
// How far from his post a guard will follow a monster before he lets it go and walks back.
// **invention**: OpenMU's guard stands where it is put, and its sight is all its reach. A guard
// who cannot take a step never reaches a monster that stops two tiles off, which is where one
// led to the gate ends up; eight is his sight and one more, so he meets what he has seen.
constexpr int kWardenLeash = 8;
// Round each of Noria's guard posts no monster is placed, in tiles on MU's measure, so its
// roads are not a fight on every tick. **invention**, the user's of 2026-09-29: "there is too
// much monsters near entrances ... so its not making problems for guard scenes". Noria's nests
// are 0.75's quadrant-wide scatters and reach right to the town's edge. A monster drawn inside
// draws again, so every breed keeps its count; a respawn rises on its own home tile, which is
// outside. Nine is his leash and one more: at twelve a seeded run's guards struck 3 blows in
// 20000 ticks, at nine 57, where the whole scatter gave 986. Lorencia's gates keep MU's own.
constexpr uint32_t kClearedMap = 3;
constexpr int kPostClearing = 9;
// What a guard's blow takes, as a share of the monster's whole health. **invention**: at
// OpenMU's 180 to 195 a blow he killed everything in Lorencia with one swing, which is no fight
// to watch and none to join (the user, 2026-09-28). The row's own band still rolls -- the hit
// chance and the spread are its -- and is scaled to this share of what it hits, so a fight is
// about four of his 1.5-second swings.
constexpr float kWardenShare = 0.25f;
// How long he stands looking where he pointed the hero before he goes back to his post, in
// ticks. **invention**, for the picture: long enough to read the line he said.
constexpr int kPointTicks = 60;

// ---- a townsperson's rounds (realm_folk.cpp) -------------------------------------------------
// **invention**, all of it, on the user's word of 2026-09-29: "make Marlon do some walking to the
// bar and sit, check on guards and go back to his spot", and the guards salute him. MU's NPCs
// stand where they are put. A row is a loop of stops by tile, in Lorencia's grid: stand at his
// own spot, sit at the tavern's bench by Lumen's bar, visit the Berdysh Guard at the south gate
// and the Crossbow Guard at the west gate, and home. A Visit names the guard by his post.
enum class StopKind : uint8_t { Stand, Sit, Visit };
struct StrollStop {
    StopKind kind;
    int32_t column, row;
    int32_t seconds;  // how long he stays, from the tick he arrives
    int32_t with = 0; // at a Sit or Stand, the NPC number he talks with there (kChatLines), or 0
};
constexpr int kStrollStops = 6;
struct StrollRow {
    int32_t number;  // MU's NPC number
    int32_t count;
    StrollStop stops[kStrollStops];
};
constexpr StrollRow kStrollers[] = {
    {229, 4, {{StopKind::Stand, 130, 127, 40},     // Marlon: his own spot, his table's tile
              {StopKind::Sit, 124, 133, 38, 255},  // the bench before Lumen's bar, talking
              {StopKind::Visit, 131, 148, 6},      // the Berdysh Guard at the south gate
              {StopKind::Visit, 114, 125, 6}}},    // the Crossbow Guard at the west gate
    // Peia, the user's of 2026-09-29: "elf quest giver patrol to guards", and "has to talk with
    // elf lala at some point". Her flower bed, the watch at each of Noria's three roads, and a
    // talk with Lala standing under her harp -- a Stand with someone to talk to faces them.
    {257, 5, {{StopKind::Stand, 171, 118, 40},     // Peia: her flower bed under the great tree
              {StopKind::Visit, 157, 112, 6},      // the Elf Archer at the west road
              {StopKind::Visit, 166, 80, 6},       // the Elf Archer at the north road
              {StopKind::Visit, 208, 128, 6},      // the Elf Archer at the south-east road
              {StopKind::Stand, 173, 123, 38, 242}}},  // before Elf Lala, talking
};
inline const StrollRow* strollRow(int32_t number) {
    for (const StrollRow& row : kStrollers) {
        if (row.number == number) return &row;
    }
    return nullptr;
}
// A walker's pace, ticks a tile: an unhurried man, slower than a guard's eight.
constexpr int kStrollTicks = 10;
// After the hero is done with him -- the talk over, the window shut -- he stands this long
// before he goes on with his rounds, so the goodbye is not his back.
constexpr int kStrollResumeTicks = 40;
// How long a visited guard holds his salute and his turn toward him.
constexpr int kSaluteTicks = 50;
// His talk at the bar (the user's, 2026-09-29: "make some dialog with Marlon and barmaid"): this
// many lines, she first and then turn about, one every kChatTicks from a second after he sits.
// The words are the drawing's (game/play.cpp, kBarTalk), which must have exactly this many. The
// hero talking to either of them pauses it, and it goes on from the line it stopped at.
constexpr int kChatLines = 10;
constexpr int kChatTicks = 70;

// ---- the monsters whose blow poisons -----------------------------------------------------------
// 0.75's own, by MU's number: the Dungeon's Poison Bull (8) and Larva (12) and Lost Tower's Poison
// Shadow (39), each `AttackSkill = Poison` (Version075/Maps/Dungeon.cs:666, :791; LostTower.cs:834).
// Nothing in Lorencia poisons -- not its Spider (3), which was ours for a day and taken out (the
// user, 2026-09-28).
constexpr int32_t kPoisoners[] = {8, 12, 39};
// A poison on him: 0.75's twenty seconds, a pulse every three at `PoisonDamageMultiplier` 0.03 of
// the health left (Dungeon.cs:677), never the last point. One at a time: a bite while one is on
// neither adds to it nor starts the twenty seconds again (ObjBaseAttack.cpp:770-777).
constexpr int32_t kHeroPoisonTicks = 400;
// A Thunder Lich's or Devil's Lightning pushes him when its bolt lands, not when the realm
// strikes: the drawing calls the Lich's bolt fifteen of MU's frames into the swing (0.6 s) and
// shows the blow with it (play.cpp, thunderCasts_), so a push at the strike slid him away before
// there was any bolt (the user, 2026-10-01: "lich monster push back ... is not synced").
constexpr int32_t kBeastPushDelay = 12;
constexpr float kHeroPoisonShare = 0.03f;

// ---- the monsters whose blow ices -------------------------------------------------------------
// Devias's Ice Monster (22), `AttackSkill = Ice` (Version075/Maps/Devias.cs:178). OpenMU's
// Monster.AttackAsync hits with the plain blow and then, hit or miss, tries the skill's element on
// him (Monster.cs:113-123, AttackableExtensions.cs:451-489): 1/(IceResistance+1), which is every
// swing on a hero with none, and not again while it is on. Iced is ten seconds at half his speed
// (SkillsInitializerBase.cs:205-207, :274-295; MovementSpeedConstants.cs:50) -- the wizard's own
// chill, whose ticks and factor are Ice's row. A Ring of Ice's resistance is not carried.
// Atlans's Silver Valkyrie (52), `AttackSkill = Ice` (Version075/Maps/Atlans.cs:630-660; WebZen's
// A.Type 7), the same chill -- but **ours**, one shot in `odds` (the user, 2026-10-04: 'Silver
// Valkyrie has a chance to use ice oon char whwn shoot'), rolled off its own stream (chillDice_)
// before his resistance is asked. 1 is every blow, MU's.
struct Chiller {
    int32_t number;
    int odds;
};
constexpr Chiller kChillers[] = {{22, 1}, {52, 4}};
constexpr int chillOdds(int32_t number) {
    for (const Chiller& one : kChillers) {
        if (one.number == number) return one.odds;
    }
    return 0;
}

// ---- the bosses' Flame of Evil -----------------------------------------------------------------
// The Lost Tower's Death Gorgon (35) and Balrog (38), A.Type 150 in WebZen's Monster.txt: one blow
// in five is skill 50, Flame of Evil, on everyone within five tiles (gObjMonster.cpp:1849-1925,
// 1738-1752) -- with one player, the blow on him, at the monster's own damage band. OpenMU 075
// never creates its skill 150, so there they only melee; WebZen's, docs/lost-tower-port.md.
// And Atlans's Hydra (49), A.Type 150 the same (Monster.txt:72; docs/atlans-port.md 4.2).
// And Tarkan's Tantallos (58), Zaikan (59) and Death Beam Knight (63), A.Type 150 (WebZen
// Monster.txt:61, :73, :74; docs/tarkan-port.md 4.2): all 63 Tantallos, not only the bosses.
constexpr int32_t kBosses[] = {35, 38, 49, 58, 59, 63};

// ---- blows split into parts --------------------------------------------------------------------
// **Ours.** MU and WebZen strike once a swing; these breeds' one blow is split into `parts`, each
// its own roll at a parts'th of the blow -- several numbers where one stood, the same damage.
// The first is the swing's own; the rest come `every` ticks apart from `after` ticks after the
// swing, each only while he is still in the breed's reach (Realm::beamOn), and each is lightning,
// which pushes him (Realm::strikeAt).
//   * Atlans's Hydra (49): a bolt from each of its four heads (the user, 2026-10-04: 'each laser
//     from head has to do some damage', 'from heads we shoot red lightiing'), the first a bolt
//     too. Its swing is 1.4 s and its blow shows at half of it, tick 14.
//   * The Lizard King (48): its staff's blow and then its red lightning (the user, 2026-10-04:
//     'lizards has to do melee and lihgting damage, not onyl melee') -- the first part melee,
//     which does not push. Its swing is 1.6 s, its blow at tick 16, the bolt just after.
struct SplitBlow {
    int32_t number;
    int parts;
    bool meleeFirst;
    int64_t after, every;
};
constexpr SplitBlow kSplitBlows[] = {
    {49, 4, false, 18, 4},  // Hydra
    {48, 2, true, 18, 4},   // Lizard King
};
constexpr const SplitBlow* splitOf(int32_t number) {
    for (const SplitBlow& one : kSplitBlows) {
        if (one.number == number) return &one;
    }
    return nullptr;
}

// ---- a spot of many -----------------------------------------------------------------------------
// How far a one-tile nest with a count scatters its members (Realm's raise): **ours**, a
// reconstruction. MonsterSetBase's point rows carry a scatter distance that OpenMU's parser drops
// (BaseMapInitializer.cs:188-192) and nothing on disk recovers it; 3 is the Elite Yeti's own
// MoveRange (Devias.cs:110), which keeps each of Devias's two camps of ten a camp.
constexpr int kPointScatter = 3;
constexpr int32_t kHeroChillTicks = 200;

// ---- elemental resistance ---------------------------------------------------------------------
// Only the two elements that do something in 0.75 (the Ice slow and the Poison), and only the
// breeds that have one. The rest of the three maps' breeds have none: Lorencia's Lich carries
// only Fire, which nothing in 0.75 acts on, and Noria's Goblin writes 0 to every element.
// WebZen's word, not OpenMU's: gObjCheckResistance takes the raw number and turns the element
// aside when rand()%(r+1) != 0, so r of every r+1 (user.cpp:8710-8711, 1.00.93) -- 3 is 75%,
// the Ice Queen's 5 is 83%. OpenMU read it as r/255 and came to ~2%. Monster.txt's columns are
// cold, poison, lightning, fire (public.h:37-40, MonsterAttr.cpp:233-236), which OpenMU's ice
// and poison had swapped: Devias's own beasts take Ice and shrug off Poison. It never lessens
// the damage. The Yeti (19) is here though it is spawned nowhere, for the day it is.
struct Resistance {
    int32_t number;
    int32_t ice, poison;
};
constexpr Resistance kResistances[] = {
    {19, 0, 3},  // Yeti
    {20, 1, 4},  // Elite Yeti
    {22, 0, 3},  // Ice Monster
    {23, 0, 3},  // Hommerd
    {24, 0, 2},  // Worm
    {25, 4, 5},  // Ice Queen
    // The Lost Tower's eight (Version075/Maps/LostTower.cs, every breed's own rows, OpenMU's
    // poison and ice read the other way about as above; WZD Monster.txt agrees).
    {34, 5, 5},    // Cursed Wizard
    {35, 6, 6},    // Death Gorgon
    {36, 3, 3},    // Shadow
    {37, 5, 5},    // Devil
    {38, 10, 10},  // Balrog
    {39, 6, 4},    // Poison Shadow
    {40, 6, 6},    // Death Knight
    {41, 5, 5},    // Death Cow
    // Atlans's seven (Version075/Maps/Atlans.cs, read the same way; WZO Monster.txt agrees).
    {45, 1, 1},    // Bahamut
    {46, 2, 2},    // Vepar
    {47, 6, 2},    // Valkyrie
    {48, 7, 7},    // Lizard King
    {49, 12, 12},  // Hydra
    {51, 6, 6},    // Great Bahamut
    {52, 7, 7},    // Silver Valkyrie
    // Tarkan's seven (WZO Monster.txt:55-74, = OpenMU Version095d Maps/Tarkan.cs read the same
    // way; docs/tarkan-port.md A 4.2). The two bosses' fire 15 and 17 is not carried.
    {57, 9, 9},    // Iron Wheel
    {58, 9, 9},    // Tantalos
    {59, 13, 13},  // Zaikan
    {60, 8, 8},    // Bloody Wolf
    {61, 10, 10},  // Beam Knight
    {62, 8, 8},    // Mutant
    {63, 13, 13},  // Death Beam Knight
};

// ---- what each breed leaves -------------------------------------------------------------------
// Monster.txt's MoneyRate and MaxItemLevel (WebZen 1.00.93, revision 2008-08-22), which OpenMU
// does not carry. A kill that leaves no item leaves Zen when rand()%moneyRate < 10
// (gObjMonster.cpp:4762): every kill at 10, five in six at 12, five in seven at 14. And a
// breed drops nothing whose plus would pass its maxPlus (MonsterItemMng.cpp:626) -- the
// Dungeon's and the Ice Queen's 3 and 4 keep their drops low. ItemRate is not here: the item
// chance is the user's (realm_items.cpp, Realm::leave). A breed missing from the list takes
// {10, 6}.
// And its RegTime, in seconds: the respawn is that and one more (realm_fight.cpp, kill).
struct DropRate {
    int32_t number;
    int32_t moneyRate, maxPlus;
    int32_t regen = 5;
};
constexpr DropRate kDropRates[] = {
    {0, 10, 6},  {1, 10, 6},  {2, 10, 6},  {3, 10, 6},  {4, 12, 6},  {6, 12, 6},
    {7, 12, 6},  {10, 14, 3}, {11, 14, 4}, {12, 14, 4}, {13, 14, 4}, {14, 12, 6},
    {17, 14, 4}, {18, 14, 3}, {19, 14, 6}, {20, 14, 6}, {21, 14, 6}, {22, 14, 6},
    {23, 14, 6}, {24, 14, 6}, {25, 14, 3, 10}, {26, 10, 6}, {27, 10, 6}, {28, 10, 6},
    {29, 12, 6}, {30, 12, 6}, {31, 12, 6}, {32, 12, 6}, {33, 10, 6},
    // The Lost Tower's: MoneyRate 14 and MaxItemLevel 3 for all eight (WZD Monster.txt). The
    // Balrog's RegTime 10 is WebZen's, 11 s, against OpenMU's 150 s (docs/lost-tower-port.md,
    // Decision 3: the user kept WebZen's, 2026-10-01).
    {34, 14, 3}, {35, 14, 3}, {36, 14, 3}, {37, 14, 3}, {38, 14, 3, 10}, {39, 14, 3},
    {40, 14, 3}, {41, 14, 3},
    // Atlans's: MoneyRate 14 and MaxItemLevel 3 for all seven, RegTime 8 for the first three,
    // 15 for the elite and 150 for the Hydra (WZO Monster.txt:39-72; docs/atlans-port.md 4.2).
    {45, 14, 3, 8}, {46, 14, 3, 8}, {47, 14, 3, 8}, {48, 14, 3, 15}, {49, 14, 3, 150},
    {51, 14, 3, 15}, {52, 14, 3, 15},
    // Tarkan's: MoneyRate 30 for all seven, twice the others', and MaxItemLevel 3; RegTime 10
    // for the three weakest, 20 for the Tantalos and the Beam Knight, 150 for the two bosses
    // (WZO Monster.txt:55-74; docs/tarkan-port.md A 4.2).
    {57, 30, 3, 10}, {58, 30, 3, 20}, {59, 30, 3, 150}, {60, 30, 3, 10}, {61, 30, 3, 20},
    {62, 30, 3, 10}, {63, 30, 3, 150},
};
constexpr DropRate dropRateOf(int32_t number) {
    for (const DropRate& one : kDropRates) {
        if (one.number == number) return one;
    }
    return DropRate{number, 10, 6, 5};
}
// A risen beast's five idle seconds (gObjMonster.cpp:185), in ticks.
constexpr int64_t kRiseIdleTicks = 100;

// A skill's element for the element runes (sim::kElementRuneDamage): 0.75's elementalModifier
// where it has one, and ours past it -- Meteorite fire (OpenMU's Earth), and the knight's two
// whirls wind, as Twister is (0.75's Wind).
inline Element skillElement(int32_t number) {
    switch (number) {
        case skill::kFireBall:
        case skill::kFlame:
        case skill::kHellfire:
        case skill::kInferno:
        case skill::kMeteorite: return Element::Fire;
        case skill::kIce: return Element::Ice;
        case skill::kPoison: return Element::Poison;
        case skill::kLightning:
        case skill::kCometfall: return Element::Lightning;  // OpenMU's ElementalType.Lightning
        case skill::kCyclone:
        case skill::kTwister:
        case skill::kTwistingSlash:
        case skill::kPenetration: return Element::Wind;  // OpenMU's ElementalType.Wind
        default: return Element::None;
    }
}

// The family on his arm as a self-cast asks it (`SkillRow::suits`): the shield's, and a
// shield's for a knight with a Bulwark in his hands whatever he holds there (sim/items.h).
inline uint32_t armFamily(const Body& hero, const content::Arm* onArm) {
    return familyOf(onArm) | (hero.excel.bulwark ? arms::kShield : arms::kNone);
}

// What his element runes multiply a blow of `element` by: 1 for none worn, or for a monster's.
inline float elementForce(const Body& hero, Element element) {
    if (!hero.player || element == Element::None) return 1.0f;
    return float(1.0 + kElementRuneDamage * hero.excel.elementRunes[int(element)]);
}

// What his Wraths multiply every blow of his by (sim::kWrathDamage): 1 for none, or a monster's.
inline float wrathForce(const Body& hero) {
    return hero.player ? float(1.0 + kWrathDamage * hero.excel.wraths) : 1.0f;
}

}  // namespace mu::sim
