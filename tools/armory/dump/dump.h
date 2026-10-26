// The Armory's dump (docs/plans/armory.md): every number the site shows that a C++ rule decides,
// from the game's own code linked in, and the release's own tables read from its packs.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "content/tables.h"
#include "json.h"
#include "sim/rules.h"

namespace armory {

using namespace mu;

// One world's cooked tables, as the game reads them (core::readFile, through the packs when
// they are mounted).
struct World {
    std::string name;  // the folder: "lorencia"
    std::unique_ptr<content::Tables> tables;
};

struct Dump {
    std::string commit, protocol;
    std::vector<World> worlds;  // by name
    bool packs = false;         // read from the release's packs (every world in them is live)
    const World* world(const std::string& name) const {
        for (const World& w : worlds) {
            if (w.name == name) return &w;
        }
        return nullptr;
    }
    // The items table every world shares (Lorencia's, else the first).
    const content::Tables& items() const {
        const World* w = world("lorencia");
        return *(w ? w : &worlds.front())->tables;
    }
    // The stamp every file carries.
    J stamped() const {
        J o = J::obj();
        o.set("commit", commit).set("protocol", protocol);
        return o;
    }
};

const char* kinName(sim::Kin kin);  // "DK", "DW", "Elf", "MG", "DL"
inline constexpr sim::Kin kKins[4] = {sim::Kin::DarkKnight, sim::Kin::DarkWizard, sim::Kin::FairyElf,
                                      sim::Kin::MagicGladiator};

// Each writer returns its file's tree; main saves it.
J dumpCards(const Dump& d);
J dumpSkills(const Dump& d);
J dumpTalents(const Dump& d);
J dumpGearPowers(const Dump& d);
J dumpDrops(const Dump& d);
J dumpRules(const Dump& d);
J dumpBoxes(const Dump& d);
J lookJson();
// spawn.cpp: a monster's numbers as the realm stands it up in that place (castle 0 for a map,
// -n for Devil Square n),
// or nullptr when no body of it stands there.
const J* spawned(const World& w, int castle, int32_t number, bool elite);
// reach.cpp: whether a player can get to a map (travel rows, then gate chains), and so whether a
// world of the release is live: in its packs and reachable.
bool reachable(uint32_t map);
bool live(const Dump& d, const World& w);
// spawn.cpp: whether any nest of `kind` lies where Realm::treasureGround says a class's treasure
// may fall.
bool onTreasureGround(const World& w, uint32_t kind);                // look.cpp: the game's own look numbers the site draws with  // drops.cpp: what each box opens into, for rules.json

// The analytic per-kill odds for one monster, for --check-odds to hold the real roll against:
// item index -> the chance one kill leaves at least one, for a killer of `kin`.
struct Ground {
    const World* world = nullptr;
    uint32_t kind = 0;   // index into the world's kinds
    int castle = 0;      // Blood Castle's 1..6, else 0
    int square = 0;      // Devil Square's 1..4, else 0
    bool elite = false;
    bool raid = false;   // the Golden Dragon raid's own spoils too
};
std::vector<double> perKill(const Dump& d, const Ground& g, sim::Kin kin);

// --check-odds: the real Realm::leave against perKill. Non-zero on drift.
int checkOdds(const Dump& d, int kills, bool verbose);

}  // namespace armory
