// The Realm's side of the travel list (sim/travel.h): which rows he has opened, and a trip asked
// for, checked and paid. The map change itself is the game's (app/modes/play_mode.cpp), as a
// gate's is. None of it draws a die.
#include "core/log.h"
#include "sim/quests.h"
#include "sim/realm.h"
#include "sim/fmath.h"
#include "sim/maps.h"

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
    {"Lorencia", 0, 1, 2000, 142, 126, 0, 0, "lorencia"},
    {"Noria", 3, 1, 2000, 174, 112, 0, 0, "noria"},
    {"Devias", 2, 40, 2000, 207, 42, 0, 0, "devias"},
    // The first floor at the stair's 40, not 60 (the user, 2026-10-09): the Catacombs are offered
    // from 40 (2026-10-08), and a link asks at least its floor's trip (Realm::questLevel).
    {"Dungeon", 1, 40, 3000, 108, 247, -1, -1, "dungeon"},
    {"Dungeon 2", 1, 80, 3500, 232, 126, -1, -1, "dungeon_2"},
    {"Dungeon 3", 1, 100, 4000, 3, 84, 1, -1, "dungeon_3"},
    // `Gates.cs:51-57` (docs/lost-tower-port.md §2.3): LostTower lands in the safe hall, on spawn
    // gate 42, facing nowhere; LostTower2-7 on each floor's arrival gate, 31-41, as those face.
    // The hall at the Devias door's 80, not 100 (the user, 2026-10-09): Devin's errand sends the
    // hero to Tersia from 80, and speaking to her opens this row.
    {"Lost Tower", 4, 80, 5000, 208, 75, 0, 0, "lost_tower"},
    {"Lost Tower 2", 4, 100, 5500, 242, 237, -1, -1, "lost_tower_2"},
    {"Lost Tower 3", 4, 100, 6000, 86, 167, 1, -1, "lost_tower_3"},
    {"Lost Tower 4", 4, 120, 6500, 87, 87, 1, -1, "lost_tower_4"},
    {"Lost Tower 5", 4, 120, 7000, 129, 53, -1, -1, "lost_tower_5"},
    {"Lost Tower 6", 4, 140, 7500, 53, 53, -1, -1, "lost_tower_6"},
    {"Lost Tower 7", 4, 140, 8000, 8, 86, -1, -1, "lost_tower_7"},
    // Atlans, last so a save's bits keep their rows. 0.75's list has none; ours (the user,
    // 2026-10-05: 'if char already did meet the quest giver quest from noria allow to use fast
    // travel to atlans'): Season Six's warp, level 70 as the Noria gate asks and 4,000 Zen, on the
    // safe basin's spawn gate 49, facing nowhere (game/world/maps.cpp's arrival tile).
    {"Atlans", 7, 70, 4000, 21, 17, 0, 0, "atlans"},
    // Tarkan, after it for the same reason. 0.95d's list has none; Season Six's "Tarkan" is 8,000
    // Zen at 140 on spawn gate 57 (Gates.cs:67). Ours: level 100 as the Atlans door asks, landing
    // on the town at 195,65 (WZ's middle, 195,61, is a closed statue block), facing nowhere. It
    // opens when he speaks to the Keeper of Kantur there, a quest giver since 2026-10-06
    // (docs/tarkan-quest.md), or with The Road of Kantur handed in. docs/tarkan-port.md.
    {"Tarkan", 8, 100, 8000, 195, 65, 0, 0, "tarkan"},
    // Icarus, after it for the same reason (docs/icarus-port.md step 8, decision 1): Season Six's
    // 10,000 Zen, landing at the door, 15,13, facing nowhere; level 160 as the door asks (gate
    // 62), and refused without wings or a Dinorant worn, as the door refuses (Wings). It opens
    // when Tersia's The Sky Door is taken (docs/icarus-quest.md), or as he stands in Icarus.
    {"Icarus", 10, 160, 10000, 15, 13, 0, 0, "icarus"},
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

int travelIndexOf(const std::string& key) {
    for (int i = 0; i < kTravels; ++i) {
        if (key == kRows[i].key) return i;
    }
    return -1;
}

uint32_t travelRowsOf(int32_t map) {
    uint32_t bits = 0;
    for (int i = 0; i < kTravels; ++i) {
        if (kRows[i].map == map) bits |= uint32_t(1) << i;
    }
    return bits;
}

void Realm::discover(int32_t map) {
    const uint32_t rows = travelRowsOf(map);
    if ((me().found & rows) == rows) return;
    me().found |= rows;
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

    me().found = saved & ((uint32_t(1) << kTravels) - 1);
    // Both towns from the start, whichever he was born in (the user, 2026-10-04).
    me().found |= travelRowsOf(0) | travelRowsOf(3);
    // Atlans once he has met Lirien: speaking to her opens it (Realm::discover), and a save from
    // before her row was here has the Drowned Song handed in to show for it.
    if (me().quests[kDrownedSong].completions > 0) me().found |= travelRowsOf(int32_t(kAtlansMap));
    // And Tarkan once he has met the Keeper, the same way (The Road of Kantur).
    if (me().quests[kRoadOfKantur].completions > 0) me().found |= travelRowsOf(int32_t(kTarkanMap));
    // And Icarus once The Sky Door is taken (Realm::acceptQuest opens it then).
    if (me().quests[kSkyDoor].state != QuestState::Untaken || me().quests[kSkyDoor].completions > 0)
        me().found |= travelRowsOf(int32_t(kIcarusMap));
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
    if (!giver && !byFloor_) me().found |= rows;
    reachFloor();
    openChained();
}

void Realm::openChained() {
    // **A chain's floor opens with its link** (the user, 2026-10-08: "if char picks catacombs
    // quest 2, then also map travel has to be avaible at dungeon 2"): taken at the Golden Archer,
    // not waiting for him to have stood in the Dungeon first. Ours.
    for (int i = 0; i < kTravels; ++i) {
        const int q = travelQuest(i);
        if (q < 0 || ((me().found >> i) & 1u) != 0) continue;
        const QuestProgress& link = me().quests[q];
        if (link.state == QuestState::Untaken && link.completions == 0) continue;
        me().found |= uint32_t(1) << i;
        core::logf("travel: %s opened", kRows[i].name);
    }
}

void Realm::reachFloor() {
    if (!byFloor_) return;
    const int floor = travelFloor();
    if (floor < 0 || ((me().found >> floor) & 1u) != 0) return;
    me().found |= uint32_t(1) << floor;
    core::logf("travel: %s opened", travelAt(floor).name);
}

int Realm::travelFloor() const {
    return floorAt(mine().column(), mine().row());
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
        if (questGiver(q, config_.questDemo) != chain->giver) continue;
        if (link++ == floor) return q;
    }
    return -1;
}

TravelRefusal Realm::travelRefusal(int index) const {
    if (index < 0 || index >= kTravels || !tables_) return TravelRefusal::Unknown;
    const TravelRow& to = kRows[index];
    if (((me().found >> index) & 1u) == 0) return TravelRefusal::Unknown;
    // The map he is on is a trip too (the user, 2026-10-02: 'allow to travel to current map'):
    // set down at its landing in place, as another of the Dungeon's floors is. Not to a safe zone
    // from inside it, where he already is (the user: 'dont allow to use fast travel to safezone
    // to same map wher he already is'); the Lost Tower's floors from its hall still go.
    const Body& standing = mine();
    if (to.map == int32_t(tables_->map) && tables_->grid.safe(to.column, to.row) &&
        tables_->grid.safe(standing.column(), standing.row()))
        return TravelRefusal::Here;
    // Its link of the chain, taken at least once: under way, ready, resting or ever handed in.
    if (const int q = travelQuest(index); q >= 0) {
        const QuestProgress& link = me().quests[q];
        if (link.state == QuestState::Untaken && link.completions == 0) return TravelRefusal::Quest;
    }
    const Body& hero = mine();
    if (!hero.alive()) return TravelRefusal::Dead;
    if (hero.level < moveLevel(to.level, hero.kin)) return TravelRefusal::Level;
    if (me().money < to.zen) return TravelRefusal::Zen;
    if (to.map == int32_t(kIcarusMap) && !canFly(*tables_, me().bag)) return TravelRefusal::Wings;
    return TravelRefusal::None;
}

bool Realm::travel(int index) {
    if (travelRefusal(index) != TravelRefusal::None) return false;
    const TravelRow& to = kRows[index];
    me().money -= to.zen;
    // Whatever he had open or was doing stays behind with the map.
    me().trading = me().banking = me().questing = -1;
    closeMachine();
    me().gating = -1;
    me().angeling = -1;
    halt(mine());
    core::logf("travel: to %s for %lld zen", to.name, static_cast<long long>(to.zen));
    // Another floor of the map he is on (the Dungeon's): set down there in place, as a same-map
    // gate does, with no map change.
    if (to.map == int32_t(tables_->map)) {
        setHeroDown(to.column, to.row, to.dx, to.dy);
        return true;
    }
    // To another map. Left from the field of a dungeon of floors, the way back opens there; left
    // from anywhere else -- a town, or the field of Lorencia, Devias, Noria, Atlans or Tarkan --
    // one already open is given up: he chose to go elsewhere.
    // Her summon stays behind and is gone (the user, 2026-10-08: "we need to dismiss pet when
    // use map travel"), as a gate and a Town Portal dismiss it.
    if (me().summonSlot >= 0) dismiss(bodies_[size_t(me().summonSlot)]);
    const Body& hero = mine();
    const MapRow* here = mapNumbered(int(tables_->map));
    if (here != nullptr && here->floors && !tables_->grid.safe(hero.column(), hero.row())) {
        me().wayBack = {int32_t(tables_->map), hero.column(), hero.row(), hero.facing, kGoBackTicks, 0};
    } else {
        me().wayBack = WayBack{};
    }
    return true;
}

bool Realm::goBack() {
    Body& hero = mine();
    const WayBack to = me().wayBack;
    if (!hero.alive() || !to.open()) return false;
    me().wayBack = WayBack{};
    const int dx = int(std::lround(fm::cos(to.facing) * 100.0f));
    const int dy = int(std::lround(fm::sin(to.facing) * 100.0f));
    if (to.map == int32_t(tables_->map)) {
        setHeroDown(to.column, to.row, dx, dy);
        return true;
    }
    // To another map: whatever he had open stays behind, and he stops where he is, as a gate
    // stops him; the map change is the game's and the server's (What::WentBack).
    me().trading = me().banking = me().questing = -1;
    closeMachine();
    me().gating = -1;
    me().angeling = -1;
    halt(hero);
    if (me().summonSlot >= 0) dismiss(bodies_[size_t(me().summonSlot)]);
    say(What::WentBack, hero, to.map, to.column, to.row);
    return true;
}

}  // namespace mu::sim
