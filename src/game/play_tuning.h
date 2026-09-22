// What the drawing of a realm turns on: the clip slots MU numbers its actions by, the timings
// of a death, a spawn and a gait, and the four small helpers that read them. Nothing outside
// game/ may include this.
//
// `play.cpp` was 2073 lines. It is six files now -- the spine (the tick and what it remembers),
// the opening, the noises, the showing, the pointer and the windows' requests -- all
// implementing the one `Play` declared in `play.h`. This was its anonymous namespace, and it
// had to become something all six can see.
//
// Everything here is PRESENTATION and none of it is a rule. That is the whole of play.h's
// contract restated at the level of its constants: a number in this file may change what a
// blow LOOKS like and may never change whether it lands. Anything that decides is in sim/,
// and sim/realm_tuning.h is the page this one is the counterpart of.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "content/tables.h"
#include "game/play.h"

namespace mu::game {

// The sim's own rate. docs/conventions.md, "Time": the tick never reads the frame's delta and
// the frame never decides how many ticks have passed except by this number.
constexpr double kTickSeconds = 1.0 / 20.0;
// The most ticks one frame may run. A window that was away -- dragged, or stalled on a
// screenshot's readback -- comes back owing seconds of simulation, and stepping all of it in
// one frame is a stall that makes the next frame owe more. MU2 called this the mirror clock's
// debt and repaid it a little at a time; here the debt is simply forgiven, because a single
// player game has nobody to be out of step with.
constexpr int kMostTicks = 5;
// The least time between two ticks taken early for a click, in seconds. See Play::update.
constexpr float kEarlyApart = 0.5f;
// How long the character takes to dissolve in when the game has loaded. Invention.
constexpr float kAppearSeconds = 1.1f;

// How long a skill's clip blends in over, against the 0.18 s every other clip uses
// (`kBlendSeconds`, crowd.cpp). **invention**: a skill is a wind-up rather than a jab, and at the
// swing's own blend the body is standing in the pose before the arm has begun to move, which is
// what "the animation looks instant" means. Long enough to read as a body gathering itself and
// short enough that the blow still lands in the clip's own second half.
constexpr float kCastBlend = 0.28f;

// A breed's name as the command line may spell it: lowered, with everything that is not a
// letter or a digit dropped, so "Skeleton Warrior", "skeletonwarrior" and "Skeleton_Warrior"
// are one word. The cook writes two names for a breed -- the figure ("BudgeDragon01") and the
// label ("Budge Dragon") -- and the log prints the label, so both are accepted.
inline std::string plainName(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(char(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

// Which breed `name` means, or -1. An exact label or figure wins over a prefix of a figure, so
// that a figure whose name is another's prefix cannot shadow the exact one; the prefix pass is
// what lets `--arena BudgeDragon` find `BudgeDragon01`, which is the figure name the cook
// writes and nobody types.
inline int32_t arenaBreed(const content::Tables& tables, const std::string& name) {
    const std::string want = plainName(name);
    if (want.empty()) return -1;
    for (size_t i = 0; i < tables.kinds.size(); ++i) {
        if (plainName(tables.kinds[i].label) == want) return int32_t(i);
        if (plainName(tables.kinds[i].figure) == want) return int32_t(i);
    }
    for (size_t i = 0; i < tables.kinds.size(); ++i) {
        const std::string figure = plainName(tables.kinds[i].figure);
        if (!figure.empty() && figure.compare(0, want.size(), want) == 0) return int32_t(i);
    }
    return -1;
}

// MONSTER01_DIE: every monster model shares this twelve-slot layout in this one order
// (source/monsters/actions.json), so slot 6 is the death whether the model is a spider or a
// giant, and the cook already marks it `hold` -- it plays once and holds its last frame, a
// corpse pose, rather than looping.
constexpr int kMonsterDieSlot = 6;
// MONSTER01_SHOCK, played on the Lich's meteor quake and nowhere else (sprint 11 step 5).
constexpr int kMonsterShockSlot = 5;
// How far the quake reaches: MU's `Distance <= 200`, a hundred units to the tile.
constexpr float kShockTiles = 2.0f;
// PLAYER_SHOCK, for a body on the player rig that is not the hero -- the Skeleton Warrior.
// 231 is what index.json's own `actions` table calls "Shock"; MuMain's enum numbers it 238,
// and the two tables are not the same enumeration, which is why this reads ours.
constexpr int kPlayerShockSlot = 231;
// The HERO has none, and that is a decision rather than an omission. MU's quake
// loop excludes the hero outright, so he never flinches for a meteor. MU *does* flinch him on
// an ordinary blow -- `SetPlayerShock`, ZzzCharacter.cpp:1392, fed by the damage packets --
// and this engine does not, for anybody: see game/showing.h, where the same question was
// answered the same way for the blood. That is the fight's feel and belongs to the sprint
// that owns it, not to a meteor.
// PLAYER_DIE1, which the cook also holds (source/players/rig/actions.json, hold_at_end). The
// hero plays it and lies there until the realm revives him at the gate -- no fade: MU leaves
// the player's body on the ground for the whole wait.
constexpr int kPlayerDieSlot = 232;
// How long a corpse holds its pose before fading, and how long the fade itself takes.
// Invention: MU removes a dead monster from the world the instant OpenMU's own
// DeathAction handler fires, with nothing standing in for either number. This one held
// visibly through its own death clip (typically under a second at a monster's authored
// play speed) plus a beat, then eased out over a fade quick enough that a second monster
// dying nearby is never waiting on it, and short enough beside any monster's own respawn
// timer that nothing here needs to race it.
constexpr float kDeathHold = 1.2f;
constexpr float kDeathFade = 0.6f;
constexpr float kDeathTotal = kDeathHold + kDeathFade;
// How long after the killing blow lands -- the frame the bar reads nought and the fall starts
// -- what the monster left comes out. The user's call (2026-09-22): the drop belongs to the
// health reaching nought, not to the end of the death clip. Invention; a beat so it reads as
// coming out of the blow rather than being there already.
constexpr float kDropDelay = 0.12f;
// A respawn's own fade-in: fast enough to read as "arriving" rather than "loading", and the
// same smoothstep the hero's own door-opening fade uses (kAppearSeconds), just shorter --
// invention, the user asked for it not to pop.
constexpr float kSpawnFadeSeconds = 0.35f;

// How far from the camera a body is drawn at all, in tiles. MU's camera is fixed and close and
// sees about twenty tiles; posing all 290 of Lorencia's bodies every frame would spend the
// crowd's whole account on figures nobody can see. Foundation 7's "ranges per kind", applied to
// the one kind this sprint draws.
constexpr float kDrawRange = 32.0f;

// The three lengths a walk is made of, all three MU2's own (`client/core/Crowd.cs`) and all
// three marked there as not MU's: MU has no fade and no wait and stands a man up on the frame
// he arrives.
//
// Setting off and stopping are not the same change, which is why they are not the same number.
constexpr float kGaiting = 0.1f;    // standing into a walk: MU2's 0.15 slid for its length
constexpr float kHalting = 0.08f;   // a walk into standing: an arrival, already late
constexpr float kCoasting = 0.1f;   // a monster's patience before a still body is a stopped one
// The most a clip may be hurried. MU2 clamps the rate to [0.25, 4]; the floor is not kept here
// because zero is a rate this engine means -- a body covering no ground has its feet stop, and
// a quarter-speed walk under a body that is not moving is the slide the floor was hiding.
constexpr float kFastestClip = 4.0f;

// MU's own action numbers for a swing, by the stance the figure holds its weapon in
// (index.json's `actions` table: 38 "Attack fist", 39 "Attack sword right 1", 43 "Attack two
// hand sword 1", 46 "Attack spear 1", 47 "Attack scythe 1"). A monster's numbers are its OWN
// table and not these -- `monster_actions` 3 is "Attack 1" -- which is the trap figures.h
// names, so the two are looked up separately below and never out of one table.
inline int attackSlotFor(const std::string& stance) {
    if (stance == "sword") return 39;
    if (stance == "two_hand_sword") return 43;
    if (stance == "spear") return 46;
    if (stance == "scythe") return 47;
    if (stance == "bow") return 50;
    if (stance == "crossbow") return 51;
    return 38;  // bare hands
}

// An angle folded into a half turn either side of nothing, so that a body a few degrees the
// other side of due north turns the short way. The sim has its own copy (realm.cpp's `wrapped`)
// and this is deliberately not shared with it: the sim must not grow a dependency on the
// drawing, and an angle is four lines.
inline float wrapped(float angle) {
    constexpr float kTurnabout = 6.28318530718f;
    angle = std::fmod(angle, kTurnabout);
    if (angle > 3.14159265359f) angle -= kTurnabout;
    if (angle < -3.14159265359f) angle += kTurnabout;
    return angle;
}

// What the character is drawn as when nobody dressed him: the cook's own armoured Dark Knight,
// which is what every run before there was a game used. A game hands `heroLook` in instead --
// the naked class body with the cradle's weapon in its hand -- and sprint 9's character select
// is what decides which one that is.
inline constexpr const char* kHeroFigure = "DarkKnight";

// Figures::dress's own `name` for the hero, matching World::play's first dress -- `redress`
// asks for the same key so it replaces that same FigureBody rather than piling up another one.
inline constexpr const char* kHeroDressName = "Hero";

// Hanzo the smith's model: the one townsperson in Lorencia who makes a noise. MU2's
// Scenery.Noise; MixNpc01 and ElfWizard01 are the other two and are Noria's.
inline constexpr const char* kSmithFigure = "Smith01";

// The one breed in Lorencia that breathes fire and raises dust. MU2's BudgeDragon01.json
// `effects` block, which the cook does not carry; stated here once.
inline constexpr const char* kBreathingFigure = "BudgeDragon01";
// And the one that does not fall: MU's SetPlayerDie tests the sub-type of a body on the player
// rig (MODEL_SKELETON1..3) and makes eleven bones of it instead of playing a death
// (ZzzCharacter.cpp:1465-1471).
//
// MU keys that on the MODEL and this keys it on the cooked BODY, which is not the same thing
// and is worth saying out loud: the figure's `name` is "SkeletonWarrior" and its `mesh` is
// "Skeleton01". The dragon's case above gets away with `name` because its two are the same
// word. What this costs is that another breed sharing this model -- MU's Death Cow takes the
// identical eleven-bone branch, and the Stone Golem the same shape with big stones -- would
// need its own row here. Neither stands in Lorencia, and the day one does this becomes a
// field in the cook rather than a name in a list.
inline constexpr const char* kBurstingFigure = "SkeletonWarrior";
// MODEL_GIANT's own case in the same effect switch, and the whole of it is one call:
//
//     case MODEL_GIANT:
//         MonsterDieSandSmoke(o);
//         break;                                    -- ZzzCharacter.cpp:6178
//
//     void MonsterDieSandSmoke(OBJECT* o) {
//         if (o->CurrentAction == MONSTER01_DIE &&
//             o->AnimationFrame >= 8.f && o->AnimationFrame < 9.f)
//             for (int i = 0; i < 20; i++)
//                 if (rand_fps_check(1))
//                     CreateParticle(BITMAP_SMOKE + 1, <within 32 units>, o->Angle, white, 1);
//     }                                             -- ZzzCharacter.cpp:5552
//
// It is a different SHAPE from the skeleton's burst above, and that is the thing to hold on to:
// the burst happens on the instruction that kills the body, and the sand happens on the death
// clip's own clock, a third of the way into it. So this is read per frame off `keyOf`, like the
// dragon's fire and the smith's hammer, and not from `fall`.
//
// MU's other three callers -- Bloody Wolf, Tantallos, Golden Wheel -- stand nowhere near
// Lorencia, so the Giant is the only one of them this tree can draw. Same reasoning as the
// bursting figure above, and the same answer if that ever stops being true: a field in the cook.
inline constexpr const char* kSandingFigure = "Giant01";
constexpr float kSandFrom = 8.0f, kSandTo = 9.0f;
// ONCE, on the frame the death clip crosses key 8, and not a rate. MU's window is one key wide
// and MU advances a key a reference frame, so its twenty attempts at `rand_fps_check(1)` come
// to twenty in total; reading it as the dragon's dust is read -- a rate of twenty a reference
// frame -- turned the death into a sandstorm. Play::sandOnDeath.
//
// TEN rather than MU's twenty, and thrown round a ring of this radius in tiles at the animal's
// own size. Invention, the user's call (2026-09-22): "elegant and nice", "nothing too much".
// Twenty at MU's own size and alpha is a solid tan blob; the count, the ring and the dressing
// in Breath::sand are all part of one answer and are tuned together.
constexpr int kSandPuffs = 10;
constexpr float kSandReach = 0.62f;

// MONSTER01_ATTACK1, and the key its fire stops on: `AnimationFrame <= 4.f`.
constexpr int kBreathSlot = 3;
constexpr float kBreathThrough = 4.0f;
// How far out of the head a spark is born: MU's 32 to 64 units.
constexpr float kBreathNear = 0.32f, kBreathFar = 0.64f;

// Where a foot lands in MU's walk, in the clip's own keys: `AnimationFrame >= 1.5f` and
// `>= 4.5f` in ZzzCharacter.cpp's PlayWalkSound, each latched by its own c->Foot[n] so a mark
// several frames wide sounds once. Two a cycle, which is a pair of legs. MU2's Crowd.FirstFoot.
constexpr float kFirstFoot = 1.5f, kSecondFoot = 4.5f;

// The window of the smith's first action his hammer lands in: `CurrentAction == 0 &&
// AnimationFrame >= 5.f && <= 10.f`, in keys and not seconds. MU2's Scenery.HammerFrom.
constexpr float kHammerFrom = 5.0f, kHammerTo = 10.0f;

// Lorencia's grass: the tile texture MU tests for `HeroTile == 0` under a footstep; every
// other floor on the map is soil.
constexpr int kGrassFloor = 0;

// What counts as in the shot for a sound: a sphere this far round a point this high over the
// ground where it was made -- a body's middle, and enough slack that a monster half off the
// edge of the frame is still heard. MU2's Scenery reach was a tile.
constexpr float kHeardHeight = 1.0f;
constexpr float kHeardReach = 1.5f;

// Where a figure's clock stands in its clip's own keys -- MU's AnimationFrame, which is what
// every one of its sound tests reads. A looping clip is cooked with one closing key, so its
// duration spans frames - 1 intervals and this runs 0 to the key count MU authored.
inline float keyOf(const Figure& figure) {
    const FigureBody* body = figure.body();
    if (!body || !body->library || figure.clip() < 0) return -1.0f;
    const content::CookedClip& clip = body->library->clips.clips[size_t(figure.clip())];
    if (clip.duration <= 0.0f || clip.frames < 2) return -1.0f;
    return figure.clock() / clip.duration * float(clip.frames - 1);
}

// The MU action a figure is playing, or -1.
inline int slotOf(const Figure& figure) {
    const FigureBody* body = figure.body();
    if (!body || !body->library || figure.clip() < 0) return -1;
    return body->library->clips.clips[size_t(figure.clip())].slot;
}

}  // namespace mu::game
