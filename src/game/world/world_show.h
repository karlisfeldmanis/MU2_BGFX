// How a world is shown, played and heard: its manifest's `show` block (source/world/<w>/world.json,
// copied to assets/world/<w>/world.json by tools/sync.sh). The order plan's MapShow, sprint D
// (docs/order-plan.md): what used to be `world == "atlans"` in twenty files is read here once, and
// a world system opens from its own field -- a world that names no bubbles has none -- so a new
// map is its json and its art, with no system taught its name.
//
// Only the choosing is data. How a bat flies, how sand blows and how a trap strikes stay each
// system's own code; the manifest says which of them a world has and with what numbers.
#pragma once

#include <string>
#include <vector>

#include "game/world/boids.h"
#include "game/world/scurry.h"

namespace mu::game {

struct WorldShow {
    std::string world;  // the folder, which is also --world's
    std::string name;   // as the arrival, the minimap and the Move list say it: "Lost Tower"

    // A region of the map with a name of its own: the Dungeon's three floors and the Lost
    // Tower's seven, columns and rows inclusive. The first whose box holds the tile names it.
    struct Place {
        std::string name;
        int box[4] = {0, 0, 0, 0};  // first column, first row, last column, last row
    };
    std::vector<Place> places;

    // ---- Heard (Play::openSound, Play::hear) ----
    // The loop on the wind's slot: MU's wind, or the map's own air (aDungeon, aWater...); empty
    // for none, as Noria's jungle bed is the whole of its air.
    std::string air = "world_wind";
    bool airEverywhere = false;  // played under a roof too, the whole map
    std::string room;            // the reverb: "stone", "water", "open"; empty is by the roof
    std::string bed;             // a looped bed while the map's event runs (Blood Castle's)
    std::string steps;           // "grass" where slot 0 is grass, "snow"; empty is soil and stone
    bool swims = false;          // off the safe zone a player swims (Atlans)
    bool mountsFly = false;      // the Dinorant flies 90 over the ground (game/pets.h)

    // ---- Music (PlayMode::frame) ----
    std::string music;       // the track under assets, "/music/Noria.mp3"; empty for none
    std::string musicWhere;  // "safe", "indoors", "safe_indoors", "everywhere"
    bool musicLoops = false; // round and round while there, rather than once and rested

    // ---- Weather (game/world/weather.cpp) ----
    std::string weather;  // "rain", "snow", "sand", "sky", "square"; empty for none
    float weatherPeak = -1.0f;  // the pool's share at the spell's height; < 0 is the kind's own
    bool jungle = false;        // Noria's jungle bed under its weather

    // ---- On the land (game/world/world.cpp) ----
    std::string leaves;      // "leaf", "snow", "motes", "sand", "square", "sky"; empty none
    bool indoorsEverywhere = false;          // no open sky on any tile
    // The tile slots under a roof: by default Lorencia's 4, which it floors its interiors with
    // and uses nowhere outdoors (MuMain's HeroTile == 4, MU2's World.IndoorTile).
    std::vector<int> indoorFloors = {4};
    int indoorFloorsFrom = -1;               // and every slot from this one; < 0 none
    bool openBridges = false;                // plank tiles under a bridge model are open sky
    std::string caustic;     // the showing's effect drawn over the water tiles; empty none
    std::vector<std::string> roads;  // the tile sheets townsfolk keep to (Play::open)

    // ---- What flies and runs round the hero (game/world/boids.h, scurry.h) ----
    std::string boid;  // the cooked model; empty for none
    Airs airs;
    std::string crawl;  // the cooked model; empty for none
    CrawlRow crawls;

    // ---- The world's own systems, each opened only where its field is set ----
    bool bubbles = false;      // Atlans's (game/world/bubbles.h)
    bool doors = false;        // Devias's (doors.h)
    bool portal = false;       // Noria's warp (portal.h)
    bool skulls = false;       // the Lost Tower's (skulls.h)
    bool skyClouds = false;    // Icarus's cloud road (sky_clouds.h)
    bool desertVents = false;  // Tarkan's (desert_vents.h)
    bool waterfallSpray = false;  // Aida's (waterfall_spray.h)
    bool smallFires = false;      // Aida's (small_fires.h)
    bool castleSparks = false; // Blood Castle's (castle_sparks.h)
    bool drawbridge = false;   // Blood Castle's (drawbridge.h)
    bool beacon = false;       // Devias's Lost Tower beacon, its type 100 (ornaments.h)
    std::string traps;         // "dungeon" or "tower" (trap_show.h); empty none
    // The clouds over the void (void_clouds.h): on where `voidClouds` is.
    bool voidClouds = false;
    float voidColour[3] = {0.30f, 0.32f, 0.38f};
    bool voidShallow = false;
    // The haze over the eye (sand_haze.h): on where `haze` is.
    bool haze = false;
    float hazeCloud[3] = {0.0f, 0.0f, 0.0f}, hazeCloudSlide = 0.0f;
    float hazeSpecks[3] = {0.0f, 0.0f, 0.0f}, hazeSpecksSlide = 0.0f;
    float hazeLean = 0.0f;

    // The name for a tile: its place's, or the world's own.
    const std::string& placeAt(int column, int row) const;
    bool indoorFloor(int floor) const;
};

// Where the manifests are read from: `assetDir`/world/<w>/world.json. Set once at start.
void openWorldShows(const std::string& assetDir);

// The world's show, read on first asking and kept. A world with no manifest gets the defaults
// above -- MU's wind, a soil step, nothing of its own -- under its folder's name, first letter
// up, and the log says so once.
const WorldShow& worldShow(const std::string& world);

}  // namespace mu::game
