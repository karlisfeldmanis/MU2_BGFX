// The Realm's side of the travel list (sim/travel.h): which rows he has opened, and a trip asked
// for, checked and paid. The map change itself is the game's (app/modes/play_mode.cpp), as a
// gate's is. None of it draws a die.
#include "core/log.h"
#include "sim/quests.h"
#include "sim/realm.h"

namespace mu::sim {
namespace {

// `OM/Version075/Gates.cs:45-50`. The towns land on their spawn gates, whose middles are
// game/world/maps.cpp's arrival tiles (gates 17, 27, 22), and face nowhere in particular; the
// Dungeon's three on exit gates 2, 6 and 10 (docs/dungeon-port.md §1.2-1.3), facing as those do.
// Every level here is MU's doubled, ours, as the gates' are (sim/gates.cpp; the user, 2026-10-04:
// 'fix tab travels also'): the experience is 100x. The prices are MU's. The two towns ask no
// level, ours (the user, 2026-10-04: 'allow to fast travel to noria and lorencia from the
// begining').
constexpr TravelRow kRows[kTravels] = {
    {"Lorencia", 0, 1, 2000, 142, 126, 0, 0},
    {"Noria", 3, 1, 2000, 174, 112, 0, 0},
    {"Devias", 2, 40, 2000, 207, 42, 0, 0},
    {"Dungeon", 1, 60, 3000, 108, 247, -1, -1},
    {"Dungeon 2", 1, 80, 3500, 232, 126, -1, -1},
    {"Dungeon 3", 1, 100, 4000, 3, 84, 1, -1},
    // `Gates.cs:51-57` (docs/lost-tower-port.md §2.3): LostTower lands in the safe hall, on spawn
    // gate 42, facing nowhere; LostTower2-7 on each floor's arrival gate, 31-41, as those face.
    {"Lost Tower", 4, 100, 5000, 208, 75, 0, 0},
    {"Lost Tower 2", 4, 100, 5500, 242, 237, -1, -1},
    {"Lost Tower 3", 4, 100, 6000, 86, 167, 1, -1},
    {"Lost Tower 4", 4, 120, 6500, 87, 87, 1, -1},
    {"Lost Tower 5", 4, 120, 7000, 129, 53, -1, -1},
    {"Lost Tower 6", 4, 140, 7500, 53, 53, -1, -1},
    {"Lost Tower 7", 4, 140, 8000, 8, 86, -1, -1},
};

// A chained map's floors, every floor a link and its row open once the link is taken: the
// Dungeon's are the Golden Archer's; the Lost Tower's Tersia's (the user, 2026-10-01), its hall
// opening as she is spoken to and floors 2-7 with their links -- at first only handed in, since
// 2026-10-03 taken ('if char accept quest which has to go to lost tower 2, unlock LT2 fast travel
// also'). The stairs ask only their levels (sim/gates.cpp).
struct Chain {
    int32_t map, giver;
    int fromFloor;
};
constexpr Chain kChains[] = {{1, 236, 0}, {4, 566, 1}};

const Chain* chainOf(int32_t map) {
    for (const Chain& chain : kChains) {
        if (chain.map == map) return &chain;
    }
    return nullptr;
}

}  // namespace

const TravelRow& travelAt(int index) { return kRows[index < 0 || index >= kTravels ? 0 : index]; }

uint32_t travelRowsOf(int32_t map) {
    uint32_t bits = 0;
    for (int i = 0; i < kTravels; ++i) {
        if (kRows[i].map == map) bits |= uint32_t(1) << i;
    }
    return bits;
}

void Realm::discover(int32_t map) {
    const uint32_t rows = travelRowsOf(map);
    if ((found_ & rows) == rows) return;
    found_ |= rows;
    core::logf("travel: %s opened", travelAt(__builtin_ctz(rows ? rows : 1)).name);
}

void Realm::settleFound(uint32_t saved) {
    // The floors, once: a Dungeon's are separate walkable regions of the one map, and two of them
    // share a bounding box (docs/dungeon-port.md §1.2), so each is filled from its landing.
    const uint32_t rows = travelRowsOf(int32_t(tables_->map));
    floors_.clear();
    if (rows & (rows - 1)) {
        const content::Grid& grid = tables_->grid;
        const int size = grid.size();
        floors_.assign(size_t(size) * size_t(size), int8_t(-1));
        std::vector<int> queue;
        for (int i = 0; i < kTravels; ++i) {
            if (((rows >> i) & 1u) == 0) continue;
            int column = kRows[i].column, row = kRows[i].row;
            if (!router_.nearestOpen(column, row, content::kWallCharacter, 4, &column, &row)) continue;
            queue.assign(1, row * size + column);
            floors_[size_t(queue[0])] = int8_t(i);
            for (size_t at = 0; at < queue.size(); ++at) {
                const int c = queue[at] % size, r = queue[at] / size;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        const int nc = c + dc, nr = r + dr;
                        if (!grid.open(nc, nr)) continue;
                        const size_t k = size_t(nr) * size_t(size) + size_t(nc);
                        if (floors_[k] >= 0) continue;
                        floors_[k] = int8_t(i);
                        queue.push_back(int(k));
                    }
                }
            }
        }
    }

    found_ = saved & ((uint32_t(1) << kTravels) - 1);
    // Both towns from the start, whichever he was born in (the user, 2026-10-04).
    found_ |= travelRowsOf(0) | travelRowsOf(3);
    // A map nobody gives a quest on opens as he stands in it. Silently: a raise logs the same
    // lines on every run.
    bool giver = false;
    for (const content::Townsperson& one : tables_->folk) {
        if (questOf(one.number) >= 0) giver = true;
    }
    // Unless its floors are its own: a map split into floors with no quest chain over them -- the
    // Lost Tower -- opens each floor's row as he stands on it (reachFloor), so the hall does not
    // open the warp to the seventh floor (docs/lost-tower-port.md, Decision 1). The Dungeon's
    // floors are the Golden Archer's chain's, and open with the map as before.
    bool chained = false;
    for (int i = 0; i < kTravels; ++i) {
        if (((rows >> i) & 1u) != 0 && travelQuest(i) >= 0) chained = true;
    }
    byFloor_ = !giver && (rows & (rows - 1)) != 0 && !chained;
    if (!giver && !byFloor_) found_ |= rows;
    reachFloor();
}

void Realm::reachFloor() {
    if (!byFloor_) return;
    const int floor = travelFloor();
    if (floor < 0 || ((found_ >> floor) & 1u) != 0) return;
    found_ |= uint32_t(1) << floor;
    core::logf("travel: %s opened", travelAt(floor).name);
}

int Realm::travelFloor() const {
    return floorAt(bodies_[0].column(), bodies_[0].row());
}

int Realm::floorAt(int column, int row) const {
    if (floors_.empty() || !tables_->grid.inside(column, row)) return -1;
    return floors_[size_t(row) * size_t(tables_->grid.size()) + size_t(column)];
}

int Realm::travelQuest(int index) const {
    if (index < 0 || index >= kTravels) return -1;
    const TravelRow& to = kRows[index];
    const Chain* chain = chainOf(to.map);
    if (!chain) return -1;
    // Which of its rows this is, in the list's order.
    int floor = 0;
    for (int i = 0; i < index; ++i) floor += kRows[i].map == chain->map ? 1 : 0;
    if (floor < chain->fromFloor) return -1;
    // And the chain's links, in the quest table's order.
    int link = 0;
    for (int q = 0; q < kQuests; ++q) {
        if (questAt(q).giver != chain->giver) continue;
        if (link++ == floor) return q;
    }
    return -1;
}

TravelRefusal Realm::travelRefusal(int index) const {
    if (index < 0 || index >= kTravels || !tables_) return TravelRefusal::Unknown;
    const TravelRow& to = kRows[index];
    if (((found_ >> index) & 1u) == 0) return TravelRefusal::Unknown;
    // The map he is on is a trip too (the user, 2026-10-02: 'allow to travel to current map'):
    // set down at its landing in place, as another of the Dungeon's floors is. Not to a safe zone
    // from inside it, where he already is (the user: 'dont allow to use fast travel to safezone
    // to same map wher he already is'); the Lost Tower's floors from its hall still go.
    const Body& standing = bodies_[0];
    if (to.map == int32_t(tables_->map) && tables_->grid.safe(to.column, to.row) &&
        tables_->grid.safe(standing.column(), standing.row()))
        return TravelRefusal::Here;
    // Its link of the chain, taken at least once: under way, ready, resting or ever handed in.
    if (const int q = travelQuest(index); q >= 0) {
        const QuestProgress& link = quests_[q];
        if (link.state == QuestState::Untaken && link.completions == 0) return TravelRefusal::Quest;
    }
    const Body& hero = bodies_[0];
    if (!hero.alive()) return TravelRefusal::Dead;
    if (hero.level < to.level) return TravelRefusal::Level;
    if (money_ < to.zen) return TravelRefusal::Zen;
    return TravelRefusal::None;
}

bool Realm::travel(int index) {
    if (travelRefusal(index) != TravelRefusal::None) return false;
    const TravelRow& to = kRows[index];
    money_ -= to.zen;
    // Whatever he had open or was doing stays behind with the map.
    trading_ = banking_ = questing_ = -1;
    closeMachine();
    gating_ = -1;
    angeling_ = -1;
    halt(bodies_[0]);
    core::logf("travel: to %s for %lld zen", to.name, static_cast<long long>(to.zen));
    // Another floor of the map he is on (the Dungeon's): set down there in place, as a same-map
    // gate does, with no map change.
    if (to.map == int32_t(tables_->map)) setHeroDown(to.column, to.row, to.dx, to.dy);
    return true;
}

}  // namespace mu::sim
