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

// The maps it invades, each on its own clock, and the breed that lands: OpenMU's Golden Dragon
// (79, VersionSeasonSix InvasionMobsInitialization.cs:74-101), MuMain's MONSTER_MODEL_DRAGON body.
// Lorencia's first; Devias and Noria since 2026-10-08 (the user: "dragon can land on
// devias,lorencia,noria") -- OpenMU's three towns too.
constexpr bool invasionMap(uint32_t map) { return map == 0 || map == 2 || map == 3; }
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

// **The storm holds** while it is in the sky or on the ground (the user, 2026-10-08: "dragons
// only land on storms and storm does not stop till dragon is killed or fly away"): the server's
// spell is not turned dry while Realm::invasionPhase is not Quiet (server/src/main.cpp), nor a
// client's own (Weather::summon).
//
// **How long it stays** once landed: OpenMU's TaskDuration, thirty minutes. Then it goes, out
// of the picture without a fall (CleanUpMonstersAsync), as a summon is dismissed.
constexpr int64_t kInvasionStandTicks = 30 * 60 * 20;

// **Where it comes down**: one of its map's three fields, drawn at random (the user, 2026-10-08:
// "each for 3 random spots"), on the nearest standable tile out of the safe zone within
// kInvasionFieldReach of it. Ours: OpenMU's is anywhere walkable on the map, which is the
// fallback. Each field is an open clearing at least nine tiles across, 35 to 60 tiles from its
// town and reached on foot from it (read off each map's attribute grid, 2026-10-08); Lorencia's
// are the raid's west, east and south fields (sim::kRaidLandings E, C and D). The minimap shows
// the dragon where it stands (game/ui/minimap.cpp).
struct InvasionField {
    uint32_t map;
    int column, row;
};
inline constexpr InvasionField kInvasionFields[] = {
    {0, 86, 111},  {0, 183, 111}, {0, 145, 175},  // Lorencia: west, east, south
    {2, 165, 42},  {2, 241, 87},  {2, 200, 84},   // Devias
    {3, 211, 92},  {3, 128, 138}, {3, 199, 168},  // Noria
};
constexpr int kInvasionFieldCount = int(sizeof(kInvasionFields) / sizeof(kInvasionFields[0]));
constexpr int kInvasionFieldReach = 3;

enum class InvasionPhase : uint8_t {
    Quiet,     // nothing in the sky
    Entering,  // the rain and the dragons, until the landing tick
    Standing,  // it has landed, and stands until killed or its thirty minutes are up
};

}  // namespace mu::sim
