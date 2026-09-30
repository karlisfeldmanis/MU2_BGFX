#include "game/world/maps.h"

#include <algorithm>
#include <cstdio>

#include "content/tables.h"
#include "core/files.h"

namespace mu::game {
namespace {

// mu.db's gates table, the spawn rows:
//   17  map 0  133,118 to 151,135   Lorencia
//   27  map 3  171,108 to 177,117   Noria
//   22  map 2  197,35 to 218,50     Devias
// Lorencia's 142,126 is the tile the world has always opened on, and it is inside gate 17; kept
// so a character made before this table comes in where he always did. Devias is last so the M
// key still goes Lorencia to Noria first.
constexpr MapRow kMaps[] = {
    {"lorencia", 0, {142, 126}},
    {"noria", 3, {174, 112}},
    {"devias", 2, {207, 42}},
    // The Dungeon has no spawn gate (Gates.cs:119-125, none marked), so it opens where Lorencia's
    // stair lets out: the middle of exit gate 2, 107,247 to 110,247, on the first floor.
    {"dungeon", 1, {108, 247}},
};

}  // namespace

const MapRow* mapOf(const std::string& world) {
    for (const MapRow& row : kMaps) {
        if (world == row.world) return &row;
    }
    return nullptr;
}

const MapRow* mapNumbered(int number) {
    for (const MapRow& row : kMaps) {
        if (row.number == number) return &row;
    }
    return nullptr;
}

const MapRow* mapAfter(const std::string& world) {
    constexpr int count = int(sizeof(kMaps) / sizeof(kMaps[0]));
    for (int i = 0; i < count; ++i) {
        if (world == kMaps[i].world) return &kMaps[(i + 1) % count];
    }
    return &kMaps[0];
}

std::string zoneLevels(const content::Tables& tables) {
    int low = 0, high = 0;
    for (const content::MonsterNest& nest : tables.nests) {
        if (nest.kind >= tables.kinds.size()) continue;
        const int level = tables.kinds[nest.kind].level;
        low = low == 0 ? level : std::min(low, level);
        high = std::max(high, level);
    }
    if (high == 0) return {};
    char line[48];
    if (low == high) std::snprintf(line, sizeof(line), "Level %d", low);
    else std::snprintf(line, sizeof(line), "Level %d-%d", low, high);
    return line;
}

std::string placeName(const std::string& world, int column, int row) {
    if (world != "dungeon") return world;
    // Flood-filled off the attribute grid, every walkable tile: Dungeon 1 is rows 141-250,
    // Dungeon 2 rows 2-129, and Dungeon 3 the pocket at columns 1-46, rows 50-132, which holds
    // no tile of Dungeon 2's.
    if (row >= 135) return "Dungeon 1";
    if (column <= 46 && row >= 50) return "Dungeon 3";
    return "Dungeon 2";
}

std::string mapSheet(const std::string& sheetsDir, const std::string& world) {
    const std::string path = core::join(sheetsDir, "worlds/" + world + ".json");
    return core::fileExists(path) ? path : std::string();
}

}  // namespace mu::game
