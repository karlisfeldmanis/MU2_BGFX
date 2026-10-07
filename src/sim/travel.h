// The travel list: where the Tab window (game/ui/travel.h) can take him, what each trip asks, and
// which of them he has opened.
//
// **The rows are 0.75's warp list** (`OM/Version075/Gates.cs:45-50`, the `/move` command, which is
// all 0.75 had: no Move window): each a map, a level, a price in Zen and the exit gate it lands on.
// Only the rows for worlds this game has are here, in the list's own order. Dungeon 2 and 3 are
// the same map as Dungeon 1 (map 1), each floor its own row and its own gate.
//
// **Opening a row is ours** (the user, 2026-09-30, claude.ai/artifact/CeRcbNmeWq31yAA2WUL15X): a
// town's row opens the first time he speaks to its quest giver, and a map with no giver -- the
// Dungeon -- opens every row of itself the first time he stands in it. Lorencia and Noria are open
// from the start, at any level (the user, 2026-10-04). Atlans opens as he meets Lirien, its giver,
// which Peia's Drowned Song sends him to do (the user, 2026-10-05). 0.75 asks only the level and
// the Zen, of anybody, anywhere.
#pragma once

#include <cstdint>
#include <string>

namespace mu::sim {

struct TravelRow {
    const char* name;      // as the window prints it
    int32_t map;           // MU's map number (Tables::map)
    int32_t level;         // CreateWarpInfo's level
    int64_t zen;           // and its price
    int32_t column, row;   // the middle of the exit gate it lands on
    int8_t dx, dy;         // that gate's facing, as sim/gates.h stores one
    const char* key;       // what a save calls it (game/save.cpp), never renamed: not its index
};

inline constexpr int kTravels = 15;
const TravelRow& travelAt(int index);
// The row a save's key names, or -1.
int travelIndexOf(const std::string& key);

// Why a trip is refused, first reason first. `Here` is a row landing in a safe zone of the map he
// is on, asked from inside one; from its field the row sets him down at its landing (the user,
// 2026-10-02).
// `Quest` is ours (the user, 2026-09-30: "dont allow to teleport to dungeon 1,2,3 if quest line is
// not started"): a Dungeon floor's row asks its own link of the Golden Archer's chain taken once
// -- the first floor The Catacombs, the second the next, the third the last (travelQuest).
enum class TravelRefusal : uint8_t { None, Unknown, Here, Dead, Level, Zen, Quest };

// The rows of `map`, a bit a row.
uint32_t travelRowsOf(int32_t map);

}  // namespace mu::sim
