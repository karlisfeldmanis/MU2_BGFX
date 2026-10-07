// The worlds a character can stand in, one row each: what the rest of the game used to assume
// was Lorencia by writing 142,126 and "lorencia" wherever it needed a map.
//
// A row is only what no cooked file says. The land, the town and the rules are each world's
// own cook (assets/world/<name>, cooked/<name>/<name>.mut and .mur) and are found by the name;
// the lighting laid over the base sheet is sheets/worlds/<name>.json when there is one. What is
// left is where a character comes in, which is MU's spawn gate for the map.
#pragma once

#include <string>

#include "sim/maps.h"

namespace mu::content {
struct Tables;
}

namespace mu::game {

// The rows and their lookups are the rules' (sim/maps.h), so the server lands a player by the
// same table the client draws him by.
using sim::MapRow;
using sim::mapAfter;
using sim::mapNumbered;
using sim::mapOf;

// The line under the map's name as he comes in: "Level 2-38", the lowest and the highest level
// of the monsters the map's nests raise. Empty on a map with no nests. Invention: MU's
// ShowMapName is the map's picture alone.
std::string zoneLevels(const content::Tables& tables);

// The name to show for a tile of `world`: "Dungeon 1", "Dungeon 2" or "Dungeon 3" by the floor
// the tile is on -- three disconnected regions of one map, the Move list's Dungeon, Dungeon2 and
// Dungeon3 (OpenMU Version075 Gates.cs:48-50; docs/dungeon-port.md §1.2) -- and `world` itself
// on every other map. The user's, 2026-09-30.
std::string placeName(const std::string& world, int column, int row);

// The lighting overlay for a world, laid over sheets/lighting.json by TimeOfDay::setScene:
// `sheetsDir`/worlds/<world>.json when the file exists, and empty -- the base sheet alone --
// when it does not.
std::string mapSheet(const std::string& sheetsDir, const std::string& world);

}  // namespace mu::game
