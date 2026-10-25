// What a player can get to: the maps the travel window lists (sim/travel.h), and every map a gate
// chain leads to from them (sim/gates.h: an enter gate on a map it reaches, whose exit is on
// another). The event doors are gates too (Blood Castle's 1066 in Devias, Devil Square's 1058 in
// Noria). A map nothing leads into is not live, whatever its packs hold: Kanturu at 4b0ad87e.

#include <set>

#include "dump.h"
#include "sim/gates.h"
#include "sim/travel.h"

namespace armory {

const std::set<uint32_t>& reachableMaps() {
    static std::set<uint32_t> reach;
    if (!reach.empty()) return reach;
    for (int i = 0; i < sim::kTravels; ++i) reach.insert(uint32_t(sim::travelAt(i).map));
    int count = 0;
    const sim::EnterGate* gates = sim::enterGates(&count);
    for (bool grew = true; grew;) {
        grew = false;
        for (int i = 0; i < count; ++i) {
            const sim::EnterGate& g = gates[i];
            if (g.target < 0 || !reach.count(g.map)) continue;
            const sim::ExitGate* out = sim::exitGate(g.target);
            if (out && reach.insert(out->map).second) grew = true;
        }
    }
    return reach;
}

bool reachable(uint32_t map) { return reachableMaps().count(map) != 0; }

bool live(const Dump& d, const World& w) { return d.packs && reachable(w.tables->map); }

}  // namespace armory
