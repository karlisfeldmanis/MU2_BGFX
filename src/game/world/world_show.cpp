#include "game/world/world_show.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <memory>

#include "core/files.h"
#include "core/json.h"
#include "core/log.h"

namespace mu::game {

namespace {

std::string& showDir() {
    static std::string dir;
    return dir;
}

std::map<std::string, std::unique_ptr<WorldShow>>& shows() {
    static std::map<std::string, std::unique_ptr<WorldShow>> kept;
    return kept;
}

void readFloat(const core::Json& from, const char* key, float* out) { from.readInto(key, out); }

void readString(const core::Json& from, const char* key, std::string* out) {
    const core::Json& value = from[key];
    if (value.type == core::Json::Type::String) *out = value.string;
}

void readInts(const core::Json& from, const char* key, std::vector<int>* out) {
    const core::Json& value = from[key];
    if (value.type != core::Json::Type::Array) return;
    out->clear();
    for (const core::Json& item : value.items) out->push_back(int(item.numberOr(0.0)));
}

void readStrings(const core::Json& from, const char* key, std::vector<std::string>* out) {
    const core::Json& value = from[key];
    if (value.type != core::Json::Type::Array) return;
    out->clear();
    for (const core::Json& item : value.items) out->push_back(item.stringOr(""));
}

// A block that names a system turns it on: `"bubbles": {}` is enough.
bool named(const core::Json& show, const char* key) { return !show[key].isNull(); }

void readShow(const core::Json& show, WorldShow& out) {
    for (const core::Json& place : show["places"].items) {
        WorldShow::Place row;
        row.name = place["name"].stringOr("");
        for (int k = 0; k < 4; ++k) row.box[k] = int(place["box"].at(size_t(k)).numberOr(0.0));
        out.places.push_back(row);
    }

    const core::Json& air = show["air"];
    readString(air, "loop", &out.air);
    air.readInto("everywhere", &out.airEverywhere);
    readString(air, "room", &out.room);
    for (const core::Json& one : show["object_loops"].items) {
        WorldShow::ObjectLoop row;
        row.model = one["model"].stringOr("");
        row.loop = one["loop"].stringOr("");
        row.full = float(one["full"].numberOr(row.full));
        row.reach = float(one["reach"].numberOr(row.reach));
        if (!row.model.empty() && !row.loop.empty()) out.objectLoops.push_back(row);
    }
    readString(air, "bed", &out.bed);
    readString(show, "steps", &out.steps);
    show.readInto("swims", &out.swims);
    show.readInto("mounts_fly", &out.mountsFly);

    const core::Json& music = show["music"];
    readString(music, "track", &out.music);
    readString(music, "where", &out.musicWhere);
    readString(music, "rain", &out.musicRain);
    readString(music, "wild", &out.musicWild);
    music.readInto("loops", &out.musicLoops);
    readFloat(music, "rest", &out.musicRest);

    const core::Json& weather = show["weather"];
    readString(weather, "kind", &out.weather);
    readFloat(weather, "peak", &out.weatherPeak);
    weather.readInto("jungle", &out.jungle);
    weather.readInto("thunder", &out.thunder);
    readString(weather, "bed", &out.jungleBed);

    readString(show, "leaves", &out.leaves);
    const core::Json& field = show["leaf_field"];
    field.readInto("count", &out.leafCount);
    readFloat(field, "size", &out.leafSize);
    const core::Json& indoors = show["indoors"];
    indoors.readInto("everywhere", &out.indoorsEverywhere);
    readInts(indoors, "floors", &out.indoorFloors);
    indoors.readInto("floors_from", &out.indoorFloorsFrom);
    indoors.readInto("open_bridges", &out.openBridges);
    readString(show, "caustic", &out.caustic);
    readStrings(show, "roads", &out.roads);

    const core::Json& boid = show["boids"];
    readString(boid, "model", &out.boid);
    Airs& airs = out.airs;
    readFloat(boid, "speed", &airs.speed);
    boid.readInto("lit", &airs.lit);
    boid.readVec3Into("tint", airs.tint);
    boid.readInto("calls", &airs.calls);
    readFloat(boid, "flap", &airs.flap);
    if (boid["call"].type == core::Json::Type::Array) {
        for (int k = 0; k < 2; ++k) airs.call[k] = boid["call"].at(size_t(k)).stringOr("");
    }
    readFloat(boid, "call_every", &airs.callEvery);
    boid.readInto("call_rolls", &airs.callRolls);
    boid.readInto("calls_over_safe", &airs.callsOverSafe);

    const core::Json& crawl = show["crawls"];
    readString(crawl, "model", &out.crawl);
    CrawlRow& row = out.crawls;
    crawl.readInto("most", &row.most);
    crawl.readInto("scale_least", &row.scaleLeast);
    readFloat(crawl, "speed", &row.speed);
    readFloat(crawl, "turn", &row.turn);
    crawl.readInto("life", &row.life);
    crawl.readInto("squeaks", &row.squeaks);
    crawl.readInto("trail", &row.trail);
    readFloat(crawl, "drawn", &row.drawn);
    crawl.readInto("keeps_out", &row.keepsOut);

    out.bubbles = named(show, "bubbles");
    out.doors = named(show, "doors");
    out.portal = named(show, "portal");
    out.skulls = named(show, "skulls");
    out.skyClouds = named(show, "sky_clouds");
    out.desertVents = named(show, "desert_vents");
    out.waterfallSpray = named(show, "waterfall_spray");
    readFloat(show["waterfall_spray"], "burst_scale", &out.sprayBurstScale);
    show["waterfall_spray"].readInto("mist", &out.sprayMist);
    show["waterfall_spray"].readInto("steam", &out.spraySteam);
    out.smallFires = named(show, "small_fires");
    out.skullGhosts = named(show, "skull_ghosts");
    out.castleSparks = named(show, "castle_sparks");
    out.smokeVents = named(show, "smoke_vents");
    out.crystalSparks = named(show, "crystal_sparks");
    readString(show["crystal_chrome"], "model", &out.crystalChrome);
    readStrings(show["soft_glow"], "models", &out.softGlow);
    out.ruinEffects = named(show, "ruin_effects");
    out.chasmMist = named(show, "chasm_mist");
    readFloat(show["chasm_mist"], "strength", &out.chasmMistStrength);
    out.drawbridge = named(show, "drawbridge");
    out.beacon = named(show, "beacon");
    readString(show, "traps", &out.traps);

    const core::Json& clouds = show["void_clouds"];
    out.voidClouds = !clouds.isNull();
    clouds.readVec3Into("colour", out.voidColour);
    clouds.readInto("shallow", &out.voidShallow);

    const core::Json& haze = show["haze"];
    out.haze = !haze.isNull();
    haze.readVec3Into("cloud", out.hazeCloud);
    readFloat(haze, "cloud_slide", &out.hazeCloudSlide);
    haze.readVec3Into("specks", out.hazeSpecks);
    readFloat(haze, "specks_slide", &out.hazeSpecksSlide);
    readFloat(haze, "lean", &out.hazeLean);
}

}  // namespace

const std::string& WorldShow::placeAt(int column, int row) const {
    for (const Place& place : places) {
        if (column >= place.box[0] && row >= place.box[1] && column <= place.box[2] &&
            row <= place.box[3])
            return place.name;
    }
    return name;
}

bool WorldShow::indoorFloor(int floor) const {
    if (indoorFloorsFrom >= 0 && floor >= indoorFloorsFrom) return true;
    return std::find(indoorFloors.begin(), indoorFloors.end(), floor) != indoorFloors.end();
}

void openWorldShows(const std::string& assetDir) {
    showDir() = assetDir;
    shows().clear();
}

const WorldShow& worldShow(const std::string& world) {
    auto found = shows().find(world);
    if (found != shows().end()) return *found->second;
    auto show = std::make_unique<WorldShow>();
    show->world = world;
    show->name = world;
    if (!show->name.empty()) show->name[0] = char(std::toupper(static_cast<unsigned char>(show->name[0])));
    if (!world.empty() && !showDir().empty()) {
        const std::string path = core::join(showDir(), "world/" + world + "/world.json");
        const core::Json manifest = core::parseJsonFile(path);
        if (manifest.isNull()) {
            core::logf("world %s: no manifest at %s, so MU's wind and nothing of its own "
                       "(content/world/%s/world.json, then pipeline/sync.sh)",
                       world.c_str(), path.c_str(), world.c_str());
        } else {
            readString(manifest, "name", &show->name);
            readShow(manifest["show"], *show);
        }
    }
    return *shows().emplace(world, std::move(show)).first->second;
}

}  // namespace mu::game
