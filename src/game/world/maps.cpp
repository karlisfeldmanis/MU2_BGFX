#include "game/world/maps.h"

#include <algorithm>
#include <cstdio>

#include "content/tables.h"
#include "core/files.h"

namespace mu::game {

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
    if (world == "losttower") {
        // Seven floors, each a region of the one grid whose box overlaps no other's
        // (docs/lost-tower-port.md): 1 and 2 in the east, 3 to 5 down the middle, 6 and 7 west.
        if (column >= 160) return row < 150 ? "Lost Tower 1" : "Lost Tower 2";
        if (column >= 70) return row >= 150 ? "Lost Tower 3" : row >= 70 ? "Lost Tower 4" : "Lost Tower 5";
        return row < 70 ? "Lost Tower 6" : "Lost Tower 7";
    }
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
