// The worlds a character can stand in, one row each (the server's as much as the client's: the
// server lands a player by it, sprint 20): what the rest of the game used to assume
// was Lorencia by writing 142,126 and "lorencia" wherever it needed a map.
//
// A row is only what no cooked file says. The land, the town and the rules are each world's
// own cook (assets/world/<name>, cooked/<name>/<name>.mut and .mur) and are found by the name;
// the lighting laid over the base sheet is content/sheets/worlds/<name>.json when there is one. What is
// left is where a character comes in, which is MU's spawn gate for the map. The rows are each
// world's manifest's `rules` (content/world/<w>/world.json), compiled in through
// pipeline/realm_tables.py's sim/maps_table.inc: `./mu make realm`. How a world looks, sounds
// and blows is its manifest's `show`, which the client reads (game/world/world_show.h).
#pragma once

#include <cstdint>
#include <string>

namespace mu::sim {

// Where a kill may leave a class's treasure (Realm::treasureGround): nowhere, the whole map, or
// the last floor of a dungeon of floors.
enum class Treasure : uint8_t { None, Map, LastFloor };

struct MapRow {
    const char* world;  // the folder name, which is also --world's
    int number;         // MU's map number: 0 Lorencia, 3 Noria (mu.db gates.map)
    // The tile a character comes in on: the middle of mu.db's spawn gate for the map (gates
    // 17 and 27, both spawn = 1). The realm moves him to the nearest tile he may stand on.
    int arrive[2];
    // MU's map number of the town a death or a Town Portal sends him to from a map with no
    // safe zone of its own: Lorencia (OpenMU's SafezoneMap fallback, BaseMapInitializer.cs:91),
    // or Devias from Blood Castle (WebZen user.cpp:22084-22089, gate 22).
    int home = 0;
    // An event map: a save never resumes inside it. He is written standing in `home`'s town, as
    // WebZen logs a player on a Blood Castle map in at Devias (user.cpp:3147-3150).
    bool event = false;
    // A dungeon of floors -- the Dungeon's three, the Lost Tower's seven -- and the only maps
    // Go Back! opens from (app/context.h). Left by magic from any other map, the field of a
    // town's map, Atlans or Tarkan among them, there is no way back (the user, 2026-10-07: 'go
    // back! only works for dungeons with floors').
    bool floors = false;
    // The map's rules, the manifest's `rules` too. Whether a summon may be raised here: not in
    // Icarus (WebZen user.cpp:29567-29571).
    bool summons = true;
    // What a kill here may leave besides its own drops: Loch's Feather (featherMap) and the
    // Scroll of Nova (novaScrollMap), sim/items.h.
    bool feather = false;
    bool novaScroll = false;
    // Whether the Golden Dragon invades it (sim/invasion.h).
    bool invasion = false;
    // The firecracker (firecrackerMap, sim/items.h) and a class's treasure (treasureGround).
    bool firecracker = false;
    Treasure treasure = Treasure::None;
    // Icarus: entered and stood in only by one who can fly (sim::canFly, sim/items.h kIcarusMap).
    bool flight = false;
    // The first wings (kIcarusWingOdds) and Atlans's second-class gear roll (kAtlansGearOdds), and
    // whether the second class's gear is in the ordinary pools (Realm::leave).
    bool firstWings = false;
    bool secondGear = false;
    bool secondClass = false;
    // Ours: every kill's experience here times this, on top of the game's rate and the rings
    // (the manifest's `rules.exp_bonus`; the user, 2026-10-25: the Ruins pay "more than Aida",
    // x1.25). 1 on every other map.
    double expBonus = 1.0;
    // Kanturu's gear (sim::kanturuGear) in the pools here: the manifest's drops `kanturu_gear`.
    bool kanturuGear = false;
    // Ancient runes in a kill's rune draw (sim::drawRunePower): the manifest's drops
    // `ancient_runes`, the Relics and the boss's map, never the Ruins.
    bool ancientRunes = false;
    // The Gemstone (sim::kGemstoneOdds) on its own roll: the manifest's drops `gemstone`, the Ruins
    // and the Relics.
    bool gemstone = false;
};

// The rules of MU's map `number`: its row, or a row of the defaults for a map not in the table
// (summons allowed, nothing extra dropped, no invasion).
const MapRow& rulesOf(uint32_t number);

// The row for `world`, or nullptr for one not in the table.
const MapRow* mapOf(const std::string& world);

// The row for MU's map number, or nullptr: where a gate's target map is found.
const MapRow* mapNumbered(int number);

// The next world in the table after `world`, wrapping: the stand-in for a Move window until
// there is one (app/modes/play_mode.cpp, the M key).
const MapRow* mapAfter(const std::string& world);

}  // namespace mu::sim
