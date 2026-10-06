#pragma once

// The Golden Invasion, one map at a time: when it comes, how long its entrance takes, and how
// long what lands stays. MU2/docs/golden-invasion.md is the research.
//
// OpenMU's is one server-wide clock -- every four hours, twenty Golden Budge Dragons scattered
// over Lorencia and the Golden Dragon drawn to one of the three towns (GoldenInvasionPlugIn,
// InvasionConfigurationDefaults.Golden), out of 0.75's period. **Ours, the user's (2026-10-06):
// each map its own clock, Lorencia's first, a dragon's entrance in the rain** -- MU's flying
// dragons (GOBoid.cpp:1205-1305) crossing the screen, and one of them coming down where the
// Golden Dragon then stands. MU has no landing: OpenMU drops the monster on a random walkable
// tile (CreateMonstersAsync) and the client roars it in (AppearMonster, WSclient.cpp:2767-2772).
// The dive between the two is ours.

#include <cstdint>

namespace mu::sim {

// The map this clock is Lorencia's, and the breed that lands: OpenMU's Golden Dragon (79,
// VersionSeasonSix InvasionMobsInitialization.cs:74-101), MuMain's MONSTER_MODEL_DRAGON body.
constexpr uint32_t kInvasionMap = 0;
constexpr int32_t kGoldenDragonNumber = 79;

// **When**: not on a clock. OpenMU's is a timetable (GenerateTimeSequence(4 hours)); **ours, the
// user's (2026-10-06): an invasion comes with the rain, at random** -- each wet spell that
// begins over the map has kInvasionChance in 100 of bringing the dragons. Lorencia's spells
// (game/world/weather.cpp) come about every seventeen minutes, so one in three is an invasion
// about every fifty. --invasion starts one at once for a review.
constexpr int kInvasionChance = 33;

// **The entrance**, in ticks (20 a second), from the call to the landing. Ours: four seconds
// for the rain to come in (MU's own walk, a hundred steps at 25 a second, weather.h), then the
// dragons cross for about twenty-five (the user, 2026-10-06: 'entrance has to happen longer with
// more dragons'), and the last of them comes down. The drawing paces its sky off these two.
constexpr int64_t kInvasionRainTicks = 4 * 20;
constexpr int64_t kInvasionLandTicks = 30 * 20;

// **How long it stays** once landed: OpenMU's TaskDuration, thirty minutes. Then it goes, out
// of the picture without a fall (CleanUpMonstersAsync), as a summon is dismissed.
constexpr int64_t kInvasionStandTicks = 30 * 60 * 20;

// Where it comes down: a standable tile out of the safe zone, kInvasionNear to kInvasionFar
// tiles from the hero, so he sees it land, the ring widened a ring's breadth at a time, up to
// kInvasionRings, when he is in town (Lorencia's is forty tiles across and walled). Ours;
// OpenMU's is anywhere walkable on the map, which is the fallback.
constexpr int kInvasionNear = 5;
constexpr int kInvasionFar = 9;
constexpr int kInvasionRings = 6;

enum class InvasionPhase : uint8_t {
    Quiet,     // nothing in the sky
    Entering,  // the rain and the dragons, until the landing tick
    Standing,  // it has landed, and stands until killed or its thirty minutes are up
};

}  // namespace mu::sim
