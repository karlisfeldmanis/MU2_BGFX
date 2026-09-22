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

namespace mu::sim {

// The mana maximum after anything that moves it, and the pool raised by what the maximum
// gained -- the rule the health beside it follows, so a point in energy arrives full rather
// than as a bigger empty gem. A new hero starts at zero of zero and so starts full.
// Mana and the shield, re-reckoned after `reckon` (the shield reads the defence it just set).
// What a maximum gains arrives full; what it loses is clipped.
inline void restoreMana(Body& hero) {
    const int was = hero.maxMana;
    hero.maxMana = maximumMana(hero.kin, hero.level, hero.points);
    hero.mana = std::min(hero.maxMana, hero.mana + std::max(0, hero.maxMana - was));
    const int wasSd = hero.maxSd;
    hero.maxSd = maximumShield(hero.level, hero.points, hero.stats.defense);
    hero.sd = std::min(hero.maxSd, hero.sd + std::max(0, hero.maxSd - wasSd));
}



// How far past its view range something can be and still keep a monster awake. Realm.cs:63.
constexpr int kMargin = 8;
// How far a monster will be led from where it was put down before it gives up and walks back,
// and twice that for one that has been hit. Both are MU2's bench inventions and are marked as
// such where they are defined: OpenMU has no leash at all, and without one nothing that has
// seen you can ever be outrun, because a monster walks exactly as fast as a character.
constexpr int kLeash = 10;       // invention (MU2's Realm.cs Leash, and it says so itself)
constexpr int kGrudge = 20;      // invention (MU2's Grudge = Leash * 2)
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
// How long a dead character lies there before he stands up in town. MU2's Player.cs:1687, three
// seconds, which is also when a summon is taken off him.
constexpr int kRiseTicks = 60;
// ---- what comes back on its own -----------------------------------------------------------
//
// **The passive is 0.75's own and the attack's return is ours.** OpenMU regenerates on a timer:
// `GameConfiguration.RecoveryInterval = 3000` ms (GameConfigurationInitializerBase.cs:51) and
// `current += maximum * multiplier` (Player.RegenerateAsync), with the multiplier `1/27.5` for
// every class (CharacterClassInitialization.cs:163). That is 3.6% of the pool every three
// seconds, and it is what a character standing still gets back.
//
// It is also nowhere near enough to pay for a skill, which is the fault the player found: a
// level-30 knight holds 35 mana, Falling Slash costs 9, and the passive alone is one cast every
// twenty-seven seconds against a cooldown of under four. 0.75's answer was potions, because 0.75
// had no cooldowns and no reason to press anything but the attack.
//
// So the knight takes mana off what he hits. **invention**, and it is Diablo 3's shape, which is
// what PLAN.md chose for this bar: the basic attack is the generator and the skills are the
// spenders, so a fight has a rhythm of its own -- swing, swing, spend -- rather than a bar that
// empties in three presses and then plays like a game with no skills in it. A LANDED swing only,
// and never a skill's own blow: hitting is what pays, and a skill does not pay for the next one.
constexpr int32_t kRecoverEveryTicks = 60;       // 3000 ms at 20 Hz
constexpr float kManaRecoveryShare = 1.0f / 27.5f;
constexpr float kAttackManaShare = 0.05f;        // invention: a twentieth of the pool a blow

// The shield: its share of a blow and its safe-zone recovery, every three seconds. Rates.cs.
constexpr float kShieldShare = 0.9f;
constexpr float kShieldRecovery = 0.02f;
constexpr int kRecoveryTicks = 60;

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

inline int strayed(const Body& beast) {
    return std::max(std::abs(beast.column() - beast.homeColumn),
                    std::abs(beast.row() - beast.homeRow));
}

}  // namespace mu::sim
