#include "game/quest_auto.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>

#include "content/tables.h"
#include "core/log.h"
#include "core/files.h"
#include "game/play.h"
#include "game/ui/tracker.h"
#include "game/world/world_show.h"
#include "sim/gates.h"
#include "sim/market.h"
#include "sim/items.h"
#include "sim/maps.h"
#include "sim/quests.h"
#include "sim/realm.h"
#include "sim/realm_tuning.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/travel.h"

namespace mu::game {
namespace {

// MU's map numbers run below this (sim/gates.cpp keeps the same bound).
constexpr int kMapNumbers = 128;

// Each built map's breeds, how many of each its nests hold and where those nests are.
struct Living {
    int map = -1;
    std::map<int32_t, int> population;
    std::map<int32_t, std::vector<std::pair<int, int>>> places;
    // On a map of floors (the Dungeon's, the Lost Tower's), how many of each breed stand on each
    // floor, by the Move row landing on it; -1 for a nest on none.
    std::map<int32_t, std::map<int, int>> floors;
};
std::vector<Living>& worlds() {
    static std::vector<Living> all;
    return all;
}
bool opened = false;

const Living* livingOn(int map) {
    for (const Living& one : worlds()) {
        if (one.map == map) return &one;
    }
    return nullptr;
}

// How many of a breed stand on the floor a Move row lands on.
int onFloor(const Living* living, int32_t breed, int floor) {
    if (living == nullptr) return 0;
    const auto found = living->floors.find(breed);
    if (found == living->floors.end()) return 0;
    const auto many = found->second.find(floor);
    return many == found->second.end() ? 0 : many->second;
}

// Each tile's floor on a map of several Move rows, as Realm::settleFound fills them: every open
// tile walkable from a row's landing is that row's. Empty for a map of one row or none.
std::vector<int8_t> floorsOf(const content::Tables& tables, int map) {
    const uint32_t rows = sim::travelRowsOf(int32_t(map));
    const content::Grid& grid = tables.grid;
    if ((rows & (rows - 1)) == 0 || grid.empty()) return {};
    const int size = grid.size();
    std::vector<int8_t> floors(size_t(size) * size_t(size), int8_t(-1));
    std::vector<int> queue;
    for (int i = 0; i < sim::kTravels; ++i) {
        if (((rows >> i) & 1u) == 0) continue;
        int column = -1, row = -1;
        const sim::TravelRow& trip = sim::travelAt(i);
        for (int ring = 0; ring <= 4 && column < 0; ++ring) {
            for (int dr = -ring; dr <= ring && column < 0; ++dr) {
                for (int dc = -ring; dc <= ring && column < 0; ++dc) {
                    if (grid.open(trip.column + dc, trip.row + dr)) column = trip.column + dc, row = trip.row + dr;
                }
            }
        }
        if (column < 0 || floors[size_t(row * size + column)] >= 0) continue;
        queue.assign(1, row * size + column);
        floors[size_t(queue[0])] = int8_t(i);
        for (size_t at = 0; at < queue.size(); ++at) {
            const int c = queue[at] % size, r = queue[at] / size;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    const int nc = c + dc, nr = r + dr;
                    if (!grid.open(nc, nr)) continue;
                    const size_t k = size_t(nr) * size_t(size) + size_t(nc);
                    if (floors[k] >= 0) continue;
                    floors[k] = int8_t(i);
                    queue.push_back(int(k));
                }
            }
        }
    }
    return floors;
}

// The floor of a tile, or of the nearest open tile a few steps round it (a nest's middle can be
// a wall); -1 for none.
int floorOf(const std::vector<int8_t>& floors, const content::Grid& grid, int column, int row) {
    if (floors.empty()) return -1;
    for (int ring = 0; ring <= 4; ++ring) {
        for (int dr = -ring; dr <= ring; ++dr) {
            for (int dc = -ring; dc <= ring; ++dc) {
                const int c = column + dc, r = row + dr;
                if (!grid.inside(c, r)) continue;
                const int8_t floor = floors[size_t(r) * size_t(grid.size()) + size_t(c)];
                if (floor >= 0) return floor;
            }
        }
    }
    return -1;
}

// How often the hand looks again, and how long an order is left to work before it is asked again.
constexpr double kThink = 0.25;
constexpr double kReask = 2.5;
constexpr double kTravelWait = 6.0;
// A giver answered this many times without the quest moving: refused (his bag, his level).
constexpr int kTries = 3;
// Stairs walked in a row toward a nest he cannot walk to, at the most.
constexpr int kStairs = 4;
// How near, in tiles, a monster he is going for must be before he is set on it. An attack order
// is a fight to the realm (Realm::advance, "his attack"), and a fight walks him with his weapon
// drawn, so one set on from across the field walked him the whole way there -- one that has
// only seen him too, which the realm does not yet count (kCombatReach). The user,
// 2026-10-19: "when auto-play is going, char is constanyl walking and not runing to monster when
// not in combat". Farther than this he is walked to it, which runs.
constexpr float kSetOn = 6.0f;
// And how far it may move from the tile he was walked to before the walk is asked again.
constexpr int kSetOnDrift = 2;
// How near, in tiles, a monster that has him as its quarry must be to be answered first.
constexpr float kAttackerReach = 15.0f;

// The first of this map's own stairs (an enter gate letting out on the same map) to walk into on
// the way from (column, row) to a tile he cannot walk to from there: the Dungeon's floors join
// only by them. A few stairs deep, nearest first; null when none leads there.
const sim::EnterGate* stairToward(const sim::Realm& realm, const sim::Body& hero, int column, int row,
                                  int toColumn, int toRow) {
    const uint32_t map = realm.tables()->map;
    int count = 0;
    const sim::EnterGate* gates = sim::enterGates(&count);
    struct Foot {
        int column, row;
        const sim::EnterGate* first;
    };
    std::vector<Foot> now{{column, row, nullptr}}, next;
    std::vector<int32_t> taken;
    for (int depth = 0; depth < kStairs && !now.empty(); ++depth) {
        next.clear();
        for (const Foot& foot : now) {
            const sim::EnterGate* best = nullptr;
            int bestFar = 0;
            for (int g = 0; g < count; ++g) {
                const sim::EnterGate& gate = gates[g];
                if (gate.map != map || gate.target < 0 || gate.box.x1 < 0) continue;
                if (hero.level < sim::moveLevel(gate.level, hero.kin)) continue;
                if (std::find(taken.begin(), taken.end(), gate.number) != taken.end()) continue;
                const sim::ExitGate* out = sim::exitGate(gate.target);
                if (out == nullptr || out->map != map) continue;
                const int c = (gate.box.x1 + gate.box.x2) / 2, r = (gate.box.y1 + gate.box.y2) / 2;
                if (!realm.reaches(hero, foot.column, foot.row, c, r)) continue;
                taken.push_back(gate.number);
                const int outColumn = (out->box.x1 + out->box.x2) / 2, outRow = (out->box.y1 + out->box.y2) / 2;
                const sim::EnterGate* first = foot.first ? foot.first : &gate;
                if (realm.reaches(hero, outColumn, outRow, toColumn, toRow)) {
                    const int far = (c - foot.column) * (c - foot.column) + (r - foot.row) * (r - foot.row);
                    if (best == nullptr || far < bestFar) {
                        best = first;
                        bestFar = far;
                    }
                }
                next.push_back({outColumn, outRow, first});
            }
            if (best) return best;
        }
        now.swap(next);
    }
    return nullptr;
}

}  // namespace

namespace quest_world {

void open(const std::string& assetDir) {
    if (opened) return;
    opened = true;
    for (int number = 0; number < kMapNumbers; ++number) {
        const sim::MapRow* row = sim::mapNumbered(number);
        if (row == nullptr) continue;
        content::Tables tables;
        std::string error;
        const std::string path =
            core::join(assetDir, std::string("cooked/") + row->world + "/" + row->world + ".mur");
        if (!content::loadTables(path, tables, error)) continue;
        Living living;
        living.map = number;
        const std::vector<int8_t> floors = floorsOf(tables, number);
        for (const content::MonsterNest& nest : tables.nests) {
            if (nest.kind >= tables.kinds.size()) continue;
            const int32_t breed = tables.kinds[nest.kind].number;
            const int column = (nest.x1 + nest.x2) / 2, row = (nest.y1 + nest.y2) / 2;
            living.population[breed] += int(nest.count);
            living.places[breed].push_back({column, row});
            if (!floors.empty()) living.floors[breed][floorOf(floors, tables.grid, column, row)] += int(nest.count);
        }
        worlds().push_back(std::move(living));
    }
    core::logf("quest world: %zu maps read for their breeds", worlds().size());
}

int huntMap(int32_t breed, int here) {
    if (const Living* now = livingOn(here); now && now->population.count(breed)) return here;
    for (const Living& one : worlds()) {
        if (one.population.count(breed)) return one.map;
    }
    return -1;
}

int questMap(int quest, int here) {
    const sim::QuestRow& row = sim::questAt(quest);
    for (int s = 0; s < row.stepCount; ++s) {
        if (row.steps[s].kind != sim::QuestStepKind::Clear) continue;
        const int map = huntMap(row.steps[s].target, here);
        if (map >= 0) return map;
    }
    return sim::questFolkMap(row.giver);
}

int goal(const sim::Realm& realm, int quest, int step) {
    const sim::QuestStepRow& want = sim::questAt(quest).steps[step];
    if (want.count > 0 || want.kind != sim::QuestStepKind::Clear) return realm.questGoal(quest, step);
    const int here = realm.tables() ? int(realm.tables()->map) : -1;
    const int map = huntMap(want.target, here);
    if (map == here || map < 0) return realm.questGoal(quest, step);
    if (const Living* there = livingOn(map)) {
        const auto found = there->population.find(want.target);
        if (found != there->population.end()) return found->second;
    }
    return realm.questGoal(quest, step);
}

std::string mapName(int map) {
    const sim::MapRow* row = sim::mapNumbered(map);
    return row ? worldShow(row->world).name : std::string("Elsewhere");
}

}  // namespace quest_world

bool autoPlayableEver(const sim::Realm& realm, int quest) {
    if (quest < 0 || quest >= sim::kQuests) return false;
    const sim::QuestRow& row = sim::questAt(quest);
    if (row.repeatSeconds <= 0 || realm.quest(quest).completions == 0) return false;
    for (int s = 0; s < row.stepCount; ++s) {
        if (row.steps[s].kind == sim::QuestStepKind::Find) return false;
    }
    return true;
}

bool autoPlayable(const sim::Realm& realm, int quest) {
    if (!autoPlayableEver(realm, quest)) return false;
    const sim::QuestProgress& progress = realm.quest(quest);
    return progress.state != sim::QuestState::Resting || realm.questOffered(quest);
}

void QuestAuto::reset() {
    quest_ = -1;
    target_ = 0;
    nest_ = -1;
    askedAt_ = travelAt_ = -1e9;
    tries_ = 0;
    lastState_ = -1;
    handedIn_ = false;
    walkedColumn_ = walkedRow_ = -1;
    still_ = 0;
    hunting_ = -1;
    lootTried_.clear();
    stocking_ = false;
}

std::string QuestAuto::update(Play& play, int* quest, double seconds, const int32_t* bar, int barCount) {
    clock_ += seconds;
    barCount_ = bar ? std::min(barCount, 8) : 0;
    for (int i = 0; i < barCount_; ++i) bar_[i] = bar[i];
    if (*quest < 0) {
        if (quest_ >= 0) reset();
        return {};
    }
    if (*quest != quest_) {
        reset();
        quest_ = *quest;
    }
    since_ += seconds;
    if (since_ < kThink || !play.isOpen()) return {};
    since_ = 0.0;
    const sim::Realm& realm = play.realm();
    const content::Tables* tables = realm.tables();
    const sim::Body& hero = realm.hero();
    // Dead, he waits to rise in town and goes on from there; held by a cast, he waits it out.
    if (tables == nullptr || !hero.alive() || realm.held()) return {};
    const int q = quest_;
    const sim::QuestRow& row = sim::questAt(q);
    const sim::QuestProgress& progress = realm.quest(q);
    const int here = int(tables->map);
    const std::function<std::string(const std::string&)> stop = [&](const std::string& why) {
        *quest = -1;
        reset();
        if (realm.questing() >= 0) play.closeQuest();
        core::logf("auto-play: %s", why.c_str());
        return why;
    };
    if (int(progress.state) != lastState_) {
        lastState_ = int(progress.state);
        tries_ = 0;
        target_ = 0;
        nest_ = -1;
    }

    // An archer out of shots buys more before anything else.
    if (std::string said; resupply(play, stop, &said)) return said;

    switch (progress.state) {
        case sim::QuestState::Resting:
            if (!realm.questOffered(q)) {
                return stop(handedIn_ ? std::string("Auto-play: ") + row.title + " complete."
                                      : std::string("Auto-play: ") + row.title + " is not ready yet.");
            }
            [[fallthrough]];
        case sim::QuestState::Untaken: {
            if (tries_ >= kTries) return stop(std::string("Auto-play: ") + row.giverName + " will not give " + row.title + ".");
            const int map = sim::questFolkMap(row.giver);
            if (map != here) return goTo(play, map, stop);
            for (size_t f = 0; f < tables->folk.size(); ++f) {
                if (tables->folk[f].number == row.giver) {
                    visit(play, int(f), false);
                    return {};
                }
            }
            return stop(std::string("Auto-play: ") + row.giverName + " is not here.");
        }
        case sim::QuestState::Ready: {
            if (tries_ >= kTries) return stop("Auto-play: the reward does not fit in your bag.");
            const int32_t whom = sim::questReceiver(row);
            const int map = sim::questFolkMap(whom);
            if (map != here) return goTo(play, map, stop);
            for (size_t f = 0; f < tables->folk.size(); ++f) {
                if (tables->folk[f].number == whom) {
                    visit(play, int(f), true);
                    return {};
                }
            }
            return stop("Auto-play: whom to hand it to is not here.");
        }
        case sim::QuestState::Active: {
            // The step to hunt: the weakest breed not yet counted out, as the log lists them.
            int order[sim::kQuestSteps];
            const int listed = quest_marks::byLevel(row, tables, order);
            int32_t breed = -1;
            int step = -1;
            for (int i = 0; i < listed; ++i) {
                const int s = order[i];
                if (row.steps[s].kind != sim::QuestStepKind::Clear) continue;
                if (progress.counts[s] >= quest_world::goal(realm, q, s)) continue;
                breed = row.steps[s].target;
                step = s;
                break;
            }
            if (breed < 0) return {};  // every step counted: the realm settles it Ready
            if (breed != hunting_) {
                hunting_ = breed;
                core::logf("auto-play: %s -- hunting monster %d", row.title, int(breed));
            }
            const int map = quest_world::huntMap(breed, here);
            if (map < 0) return stop("Auto-play: nowhere to hunt for this quest.");
            if (map != here) return goTo(play, map, stop, breed);
            if (!hunt(play, breed)) {
                return stop(std::string("Auto-play: the ") + row.steps[step].line +
                            " left cannot be reached from here.");
            }
            return {};
        }
    }
    return {};
}

// The realm's own trip: a Move row to the map, paid, when one is open to him -- for a hunt, the
// one landing on the floor most of the breed stand on, then nearest its nests (the Dungeon's
// third floor by "Dungeon 3", not the first floor and every stair down; the user, 2026-10-20:
// "auto-play is not going to dungeon 3 for clear quest but goes long way"; and the Lost Tower's
// Death Gorgons by its seventh floor, 50 there, not the sixth's 11: "we need smarter auto-play
// also in LT, char is no using fast-travel smartly") -- else on foot to the gate toward it.
std::string QuestAuto::goTo(Play& play, int map, const std::function<std::string(const std::string&)>& stop,
                            int32_t breed) {
    const sim::Realm& realm = play.realm();
    const int here = int(realm.tables()->map);
    if (realm.questing() >= 0) play.closeQuest();
    if (clock_ - travelAt_ < kTravelWait) return {};
    const std::vector<std::pair<int, int>>* places = nullptr;
    const Living* there = livingOn(map);
    if (there && breed >= 0) {
        const auto found = there->places.find(breed);
        if (found != there->places.end()) places = &found->second;
    }
    int bestRow = -1, bestMany = 0;
    float bestAway = 1e9f;
    for (int i = 0; i < sim::kTravels; ++i) {
        const sim::TravelRow& trip = sim::travelAt(i);
        if (trip.map != map || realm.travelRefusal(i) != sim::TravelRefusal::None) continue;
        const int many = breed >= 0 ? onFloor(there, breed, i) : 0;
        float away = 0.0f;
        if (places) {
            away = 1e9f;
            for (const auto& [c, r] : *places) away = std::min(away, std::hypot(float(trip.column - c), float(trip.row - r)));
        }
        if (bestRow < 0 || many > bestMany || (many == bestMany && away < bestAway)) {
            bestRow = i;
            bestMany = many;
            bestAway = away;
        }
    }
    if (bestRow >= 0) {
        travelAt_ = clock_;
        core::logf("auto-play: by the Move list to %s", sim::travelAt(bestRow).name);
        play.travel(bestRow);
        return {};
    }
    const sim::Body& hero = realm.hero();
    if (const sim::EnterGate* gate = sim::gateToward(uint32_t(here), uint32_t(map), hero.column(), hero.row())) {
        const int column = (gate->box.x1 + gate->box.x2) / 2, row = (gate->box.y1 + gate->box.y2) / 2;
        if (column != walkedColumn_ || row != walkedRow_ || (!hero.walking && clock_ - askedAt_ > kReask)) {
            walkedColumn_ = column;
            walkedRow_ = row;
            askedAt_ = clock_;
            play.walkTo(column, row);
        }
        return {};
    }
    return stop("Auto-play: no way to " + quest_world::mapName(map) + " from here.");
}

// Walked to the giver or the one who takes it back, and once his window is open the quest taken
// or handed in, and the window shut.
void QuestAuto::visit(Play& play, int folk, bool hand) {
    const sim::Realm& realm = play.realm();
    if (realm.questing() == folk) {
        if (clock_ - askedAt_ < 0.6) return;  // the last answer's step first
        askedAt_ = clock_;
        ++tries_;
        if (hand) {
            int choice = -1;
            const sim::QuestRow& row = sim::questAt(quest_);
            for (int c = 0; c < row.choiceCount && choice < 0; ++c) {
                if (realm.questChoiceFits(quest_, c)) choice = c;
            }
            handedIn_ = true;
            play.completeQuest(quest_, choice);
        } else {
            play.acceptQuest(quest_);
        }
        play.closeQuest();
        return;
    }
    if (realm.questing() >= 0) play.closeQuest();
    // On another floor of this map (Tersia in the Lost Tower's hall, the quest hunted on its
    // seventh): the free Move row landing where he can walk to her.
    const sim::Body& hero = realm.hero();
    const content::Townsperson& one = realm.tables()->folk[size_t(folk)];
    if (!realm.reaches(hero, hero.column(), hero.row(), one.x, one.y)) {
        if (clock_ - travelAt_ < kTravelWait) return;
        int bestRow = -1;
        float bestAway = 1e9f;
        for (int i = 0; i < sim::kTravels; ++i) {
            const sim::TravelRow& trip = sim::travelAt(i);
            if (trip.map != int32_t(realm.tables()->map) || realm.travelRefusal(i) != sim::TravelRefusal::None) continue;
            if (!realm.reaches(hero, trip.column, trip.row, one.x, one.y)) continue;
            const float away = std::hypot(float(trip.column - one.x), float(trip.row - one.y));
            if (away < bestAway) {
                bestAway = away;
                bestRow = i;
            }
        }
        if (bestRow >= 0) {
            travelAt_ = clock_;
            core::logf("auto-play: by the Move list to %s, to %s", sim::travelAt(bestRow).name, one.name.c_str());
            play.travel(bestRow);
            return;
        }
    }
    const sim::Request& order = realm.order();
    const bool asked = order.kind == sim::Request::Kind::Talk && order.target == uint32_t(folk);
    if (asked && (hero.walking || clock_ - askedAt_ < kReask)) return;
    askedAt_ = clock_;
    sim::Request request;
    request.kind = sim::Request::Kind::Talk;
    request.target = uint32_t(folk);
    play.orderHero(request);
}

// **His defensive auras** (the user, 2026-10-17: "if char is shield aura user and cooldown ends
// he should recast shield auras also", then "use right click and def auras"): a guard on his bar
// -- the knight's Defense, the wizard's Soul Barrier, the elf's Greater Defense and her Greater
// Damage -- cast on himself
// once its boon has lapsed, it is ready, paid for and his hand may throw it -- so a guard behind a
// shield is never asked with two hands on one weapon or a blade in each (SkillRow::suits off the
// left hand; the user, the same day: "if char cant use def aura (dual wield, two-hands) weapon
// than we dont use it"). The bot's guard()
// (tools/bot/brain.h), for the boons only and only those he put on his bar. A cast the realm
// refuses starts no cooldown, so one is not asked again for a few seconds.
bool QuestAuto::guard(Play& play) {
    const sim::Realm& realm = play.realm();
    const sim::Body& hero = realm.hero();
    if (realm.casting()) return false;
    // Nothing is thrown on a safe tile (Realm::invoke): in town it waits for the field.
    if (realm.tables()->grid.safe(hero.column(), hero.row())) return false;
    const uint32_t offHand = realm.wearer().offHand;
    for (int i = 0; i < barCount_; ++i) {
        const int32_t skill = bar_[i];
        const sim::SkillRow* row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || !realm.knows(skill) || realm.cooling(skill) > 0) continue;
        // A guard while his boon has lapsed, and the elf's Greater Damage while her might has,
        // each on its own clock (the user, 2026-10-19: "elf on auto-play have to use also
        // greater damage buff its inside QWERT").
        if (row->boonTicks > 0) {
            if (hero.boonUntil > realm.tick()) continue;
        } else if (row->mightTicks > 0) {
            if (hero.mightUntil > realm.tick()) continue;
        } else {
            continue;
        }
        if (hero.mana < sim::manaCostOf(hero, *row) || !row->suits(offHand)) continue;
        if (const auto tried = guardTried_.find(skill); tried != guardTried_.end() && tried->second > clock_) continue;
        guardTried_[skill] = clock_ + 4.0;
        core::logf("auto-play: %s raised", row->name);
        play.castHero(skill, hero.id);
        return true;
    }
    return false;
}

// **What he picks up** (the user, 2026-10-19: "on auto-play character has to auto-pickup jewel
// drops and rune drops and exc drops, if inventiry is full it does not pickup anything"): a
// jewel, a Rune of Creation, an excellent piece or a box he may take, lying within kLootReach tiles he
// can walk, and only when it fits his bag as it stands -- with no room it is left where it
// lies, not walked to and refused. The nearest first, by the same Pick a Space press asks. One
// he has been walking to for kLootGiveUp seconds without it going is left for good.
constexpr float kLootReach = 8.0f;
constexpr double kLootGiveUp = 6.0;

bool QuestAuto::loot(Play& play) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();
    const sim::Lying* nearest = nullptr;
    float best = kLootReach + 0.5f;
    for (const sim::Lying& one : realm.lying()) {
        if (one.what.empty() || one.what.item < 0 || size_t(one.what.item) >= tables.items.size()) continue;
        const content::ItemRow& row = tables.items[size_t(one.what.item)];
        // And a Firecracker, a Box of Luck or a Box of Kundun (the user, 2026-10-21), while the
        // Helper window's Boxes tick is on.
        const bool box = sim::firecracker(row) || sim::boxOfLuck(row) || sim::boxOfKundun(row);
        if (box && !boxes_) continue;
        if (!row.jewel() && !sim::creation(row) && !sim::refiningJewel(row) && one.what.excellent == 0 && !box) continue;
        if (!realm.mayTake(one)) continue;
        if (const auto tried = lootTried_.find(one.id); tried != lootTried_.end() && tried->second < clock_) continue;
        const float away = std::max(std::fabs(float(one.column) - hero.x), std::fabs(float(one.row) - hero.y));
        if (away >= best || !realm.reaches(hero, hero.column(), hero.row(), one.column, one.row)) continue;
        sim::Satchel bag = realm.satchel();
        if (sim::pour(tables, bag, sim::kWorn, sim::kSlots, one.what) < 0) continue;
        best = away;
        nearest = &one;
    }
    if (nearest == nullptr) return false;
    if (lootTried_.find(nearest->id) == lootTried_.end()) {
        lootTried_[nearest->id] = clock_ + kLootGiveUp;
        core::logf("auto-play: picking up %s at %d,%d", tables.items[size_t(nearest->what.item)].label.c_str(),
                   nearest->column, nearest->row);
    }
    const sim::Request& order = realm.order();
    if (order.kind == sim::Request::Kind::Pick && order.target == nearest->id) return true;
    sim::Request request;
    request.kind = sim::Request::Kind::Pick;
    request.target = nearest->id;
    play.orderHero(request);
    target_ = 0;
    return true;
}

// **Her arrows** (the user, 2026-10-19: "on auto-play elf has to buy more arrows/bolts if its
// out of ammo"): an archer with fewer than kAmmoLow shots in her free hand and her bag walks to
// a merchant who has the quiver her weapon shoots -- one of this map's, else Amy in Lorencia,
// Eo in Noria or Zienna in Devias -- buys until she has kAmmoStacks quivers' worth or her Zen or
// her bag runs out, puts one in her free hand, and goes back to the quest. With no Zen for one
// quiver auto-play stops and says so.
constexpr int kAmmoLow = 20;
constexpr int kAmmoStacks = 3;
constexpr double kBuyWait = 0.5;  // seconds between purchases, for the last one's answer

bool QuestAuto::resupply(Play& play, const std::function<std::string(const std::string&)>& stop,
                         std::string* said) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();
    if (hero.archer == 0) {
        stocking_ = false;
        return false;
    }
    const int wanted = hero.archer == 1 ? 15 : 7;  // Arrows, the Bolt, as the HUD counts them
    const sim::Satchel& bag = realm.satchel();
    int shots = 0, stack = 0, carried = -1;
    for (int slot = sim::kWeaponLeft; slot < sim::kSlots; ++slot) {
        if (slot != sim::kWeaponLeft && slot < sim::kWorn) continue;
        const sim::Held& h = bag[slot];
        if (h.empty() || h.durability <= 0 || size_t(h.item) >= tables.items.size()) continue;
        const content::ItemRow& row = tables.items[size_t(h.item)];
        if (row.group != sim::kGroupBows || row.number != wanted) continue;
        shots += h.durability;
        stack = std::max(stack, int(row.durability));
        if (slot != sim::kWeaponLeft && carried < 0) carried = slot;
    }
    // A quiver in the bag and her hand empty: into the hand, wherever she is.
    if (carried >= 0 && bag[sim::kWeaponLeft].empty()) {
        play.moveItem(carried, sim::kWeaponLeft);
        return true;
    }
    if (!stocking_ && shots >= kAmmoLow) return false;
    stocking_ = true;
    // The shelf line that is her quiver, for a merchant by NPC number; -1 when he has none.
    const auto line = [&](int npc) -> const sim::Offer* {
        int count = 0;
        const sim::Offer* shelf = sim::stockOf(npc, &count);
        for (int i = 0; i < count; ++i) {
            if (shelf[i].refinement != 0 || shelf[i].group != sim::kGroupBows || shelf[i].number != wanted) continue;
            if (tables.itemAt(shelf[i].group, shelf[i].number) >= 0) return &shelf[i];
        }
        return nullptr;
    };
    // At the counter: one quiver a half second while there is Zen, room and a reason.
    if (const int at = realm.trading(); at >= 0 && size_t(at) < tables.folk.size()) {
        if (const sim::Offer* offer = line(tables.folk[size_t(at)].number)) {
            const content::ItemRow& row = tables.items[size_t(tables.itemAt(offer->group, offer->number))];
            if (stack == 0) stack = int(row.durability);
            // As Realm::buy charges it: a full quiver.
            const int64_t price = sim::buyingPrice(row, 0, offer->pieces > 0 ? offer->pieces : 1, offer->skill,
                                                   row.durability, row.durability);
            const bool room = realm.satchel().free(tables, row.width, row.height) >= 0;
            if (shots < stack * kAmmoStacks && realm.money() >= price && room) {
                if (clock_ - boughtAt_ >= kBuyWait) {
                    boughtAt_ = clock_;
                    play.buy(offer->slot);
                }
                return true;
            }
            play.closeTrade();
            stocking_ = false;
            if (shots == 0) {
                *said = stop(realm.money() < price ? "Auto-play: no Zen for more arrows."
                                                   : "Auto-play: no room in your bag for arrows.");
            } else {
                core::logf("auto-play: stocked up, %d shots", shots);
            }
            return true;
        }
    }
    // A merchant of this map who has it.
    for (size_t f = 0; f < tables.folk.size(); ++f) {
        if (line(tables.folk[f].number) == nullptr) continue;
        if (realm.questing() >= 0) play.closeQuest();
        const sim::Request& order = realm.order();
        const bool asked = order.kind == sim::Request::Kind::Talk && order.target == uint32_t(f);
        if (asked && (hero.walking || clock_ - askedAt_ < kReask)) return true;
        askedAt_ = clock_;
        core::logf("auto-play: out of shots, to %s", tables.folk[f].name.c_str());
        sim::Request request;
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(f);
        play.orderHero(request);
        return true;
    }
    // Else the nearest town that sells it: Lorencia's Amy, Noria's Eo, Devias's Zienna.
    constexpr struct { int map, npc; } kTowns[] = {{0, 253}, {3, 243}, {2, 246}};
    for (const auto& town : kTowns) {
        if (line(town.npc) == nullptr) continue;
        *said = goTo(play, town.map, stop);
        return true;
    }
    stocking_ = false;
    return false;
}

// **What he fights with.** The right button's skill when the slot holds one; with it empty, the
// first attack on his bar he knows and his hand may throw -- a wizard with Energy Ball on a key
// and nothing on the right button swung his staff (bug #26, 2026-10-19: "it does physical with
// staff if you do not have equipped any Main Skill. Could it use any or first registered in
// QWERT?"). A primary first, as it is cast over and over; else any blow, and the realm swings
// while its cooldown runs (Realm::press). No aura, summon, charge, channel or Teleport. -1 keeps
// the quick slot's own. Asked of the hand that swings, as the bar's plate asks it (the user,
// 2026-10-22: "helper is not using DK spells" -- asked of the off hand, a knight with an axe and a
// shield had his Falling Slash refused for the shield and swung bare).
int32_t QuestAuto::striker(Play& play) const {
    if (play.quickSkill() != 0) return -1;
    const sim::Realm& realm = play.realm();
    const uint32_t hand = realm.wearer().hand;
    int32_t any = -1;
    for (int i = 0; i < barCount_; ++i) {
        const int32_t skill = bar_[i];
        const sim::SkillRow* row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || !realm.knows(skill) || !row->suits(hand)) continue;
        if (row->onSelf() || row->chargeTicks > 0 || row->channelled() || row->blinks) continue;
        if (row->primary()) return skill;
        if (any < 0) any = skill;
    }
    return any;
}

// The nearest of the breed in sight that he can walk to -- whatever is fighting him first -- and
// with nobody in sight, its nests he can walk to one after another. A nest behind a wall of this
// map (the Dungeon's lower floors, 2026-10-17: muclassic stood at 97,145 asking for an archer's
// nest at 68,125 for five minutes, a Ghost's fight putting him back on it each time) is left
// out; with none left, a stair of this map toward the nearest, or a Move row landing where he
// can walk to one.
bool QuestAuto::hunt(Play& play, int32_t breed) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();
    if (realm.questing() >= 0) play.closeQuest();
    const auto walkable = [&](int column, int row) {
        return realm.reaches(hero, hero.column(), hero.row(), column, row);
    };
    const auto ofBreed = [&](const sim::Body& body) {
        return body.monster() && body.alive() && !body.gone && body.kind >= 0 &&
               size_t(body.kind) < tables.kinds.size() && tables.kinds[size_t(body.kind)].number == breed;
    };
    const sim::Body* held = target_ ? realm.find(target_) : nullptr;
    const sim::Body* attacker = nullptr;
    const sim::Body* nearest = nullptr;
    float attackerAway = 1e9f, best = 1e9f;
    for (const sim::Body& body : realm.bodies()) {
        if (!body.monster() || !body.alive() || body.gone) continue;
        const float dx = body.x - hero.x, dy = body.y - hero.y, away = dx * dx + dy * dy;
        // One on him: near and on his floor. A monster keeps him as its quarry past a death or a
        // trip -- a Death Knight of the Lost Tower's seventh floor held him in its hall, walking
        // to it for good.
        if (body.quarry == hero.id && away < attackerAway && away < kAttackerReach * kAttackerReach &&
            walkable(body.column(), body.row())) {
            attackerAway = away;
            attacker = &body;
        }
        if (ofBreed(body) && away < best && walkable(body.column(), body.row())) {
            best = away;
            nearest = &body;
        }
    }
    // The one he was on, while he can still walk to it: not one left on another floor.
    const bool holding = held && held->alive() && ofBreed(*held) && walkable(held->column(), held->row());
    // One he is walking to is kept over one that has only seen him: an attacker takes over only
    // when it is on him (kSetOn), else he went back and forth between two (Crylos, Discord,
    // 2026-10-23: Atlans' Hydras, two killed and risen again as he was halfway to the other two,
    // "your character is stuck going back and forth because it switches to the closest").
    const bool onHim = attacker != nullptr && attackerAway <= kSetOn * kSetOn;
    const sim::Body* fightNow = holding && !onHim ? held : attacker ? attacker : holding ? held : nearest;
    // A jewel, rune or excellent piece first, while nothing is on him.
    if (attacker == nullptr && loot(play)) return true;
    // His guard first, when it has lapsed and something is to be fought.
    if (fightNow && guard(play)) return true;
    if (fightNow) {
        const sim::Request& order = realm.order();
        const bool onIt = order.kind == sim::Request::Kind::Attack && order.target == fightNow->id;
        const float away = std::hypot(fightNow->x - hero.x, fightNow->y - hero.y);
        if (!onIt && away > kSetOn) {
            // Run to it first (kSetOn): out of a fight a walk is a run.
            target_ = fightNow->id;
            const int column = fightNow->column(), row = fightNow->row();
            const bool drifted = std::abs(column - walkedColumn_) >= kSetOnDrift ||
                                 std::abs(row - walkedRow_) >= kSetOnDrift;
            if (drifted || (!hero.walking && clock_ - askedAt_ > kReask)) {
                walkedColumn_ = column;
                walkedRow_ = row;
                askedAt_ = clock_;
                play.walkTo(column, row);
            }
            nest_ = -1;
            still_ = 0;
            return true;
        }
        if (!onIt || fightNow->id != target_) {
            target_ = fightNow->id;
            play.fight(target_, striker(play));
        }
        nest_ = -1;
        still_ = 0;
        return true;
    }
    target_ = 0;
    // Nobody about, on a map of floors: another floor open to him with at least twice the breed
    // his own has is a free trip away (Realm::travelZen), where its nests are walked to one by one
    // -- the Lost Tower's second floor keeps one stray Shadow and its first 71, and Shadows were
    // hunted at the stray's nest.
    if (clock_ - travelAt_ >= kTravelWait) {
        const Living* living = livingOn(int(tables.map));
        const int floor = realm.floorAt(hero.column(), hero.row());
        int bestRow = -1, bestMany = std::max(1, 2 * onFloor(living, breed, floor));
        for (int i = 0; i < sim::kTravels; ++i) {
            if (i == floor || sim::travelAt(i).map != int32_t(tables.map)) continue;
            if (realm.travelRefusal(i) != sim::TravelRefusal::None) continue;
            const int many = onFloor(living, breed, i);
            if (many >= bestMany) {
                bestMany = many;
                bestRow = i;
            }
        }
        if (bestRow >= 0) {
            travelAt_ = clock_;
            core::logf("auto-play: by the Move list to %s, %d of monster %d there", sim::travelAt(bestRow).name,
                       bestMany, int(breed));
            play.travel(bestRow);
            return true;
        }
    }
    const auto middle = [](const content::MonsterNest& nest, int* column, int* row) {
        *column = (nest.x1 + nest.x2) / 2;
        *row = (nest.y1 + nest.y2) / 2;
    };
    // Its nests on this map he can walk to, and the nearest of those he cannot.
    std::vector<const content::MonsterNest*> nests;
    const content::MonsterNest* behind = nullptr;
    float behindAway = 1e9f;
    for (const content::MonsterNest& nest : tables.nests) {
        if (nest.kind >= tables.kinds.size() || tables.kinds[nest.kind].number != breed) continue;
        int c = 0, r = 0;
        middle(nest, &c, &r);
        if (walkable(c, r)) {
            nests.push_back(&nest);
            continue;
        }
        const float away = std::hypot(float(c) - hero.x, float(r) - hero.y);
        if (away < behindAway) {
            behindAway = away;
            behind = &nest;
        }
    }
    int column = 0, row = 0;
    if (nests.empty()) {
        if (behind == nullptr) return false;
        int toColumn = 0, toRow = 0;
        middle(*behind, &toColumn, &toRow);
        // A Move row of this map landing where he can walk to it first -- free on the map he is
        // on (Realm::travelZen) -- and only then the stairs, floor by floor.
        if (clock_ - travelAt_ < kTravelWait) return true;
        int bestRow = -1;
        {
            float bestAway = 1e9f;
            for (int i = 0; i < sim::kTravels; ++i) {
                const sim::TravelRow& trip = sim::travelAt(i);
                if (trip.map != int32_t(tables.map) || realm.travelRefusal(i) != sim::TravelRefusal::None) continue;
                if (!realm.reaches(hero, trip.column, trip.row, toColumn, toRow)) continue;
                const float there = std::hypot(float(trip.column - toColumn), float(trip.row - toRow));
                if (there < bestAway) {
                    bestAway = there;
                    bestRow = i;
                }
            }
        }
        if (bestRow >= 0) {
            travelAt_ = clock_;
            core::logf("auto-play: by the Move list to %s, toward a nest at %d,%d", sim::travelAt(bestRow).name,
                       toColumn, toRow);
            play.travel(bestRow);
            return true;
        }
        const sim::EnterGate* stair = stairToward(realm, hero, hero.column(), hero.row(), toColumn, toRow);
        if (stair == nullptr) return false;
        column = (stair->box.x1 + stair->box.x2) / 2;
        row = (stair->box.y1 + stair->box.y2) / 2;
    } else {
        // The nearest first and then round them.
        if (nest_ < 0 || nest_ >= int(nests.size())) {
            float near = 1e9f;
            for (size_t i = 0; i < nests.size(); ++i) {
                int c = 0, r = 0;
                middle(*nests[i], &c, &r);
                const float away = std::hypot(float(c) - hero.x, float(r) - hero.y);
                if (away < near) {
                    near = away;
                    nest_ = int(i);
                }
            }
        }
        middle(*nests[size_t(nest_)], &column, &row);
        const float away = std::hypot(float(column) - hero.x, float(row) - hero.y);
        // There and nobody of its breed about: the next nest.
        if (away < 4.0f || still_ >= 2) {
            nest_ = (nest_ + 1) % int(nests.size());
            still_ = 0;
            middle(*nests[size_t(nest_)], &column, &row);
        }
        // A long way off on a map of floors (the Dungeon's, the Lost Tower's): a Move row of this
        // map that lands nearer, when one is open to him.
        if (away > 40.0f && clock_ - travelAt_ > kTravelWait) {
            int bestRow = -1;
            float bestAway = away - 15.0f;
            for (int i = 0; i < sim::kTravels; ++i) {
                const sim::TravelRow& trip = sim::travelAt(i);
                if (trip.map != int32_t(tables.map) || realm.travelRefusal(i) != sim::TravelRefusal::None) continue;
                // On this floor: another's landing is near as the crow flies only.
                if (!realm.reaches(hero, trip.column, trip.row, column, row)) continue;
                const float there = std::hypot(float(trip.column - column), float(trip.row - row));
                if (there < bestAway) {
                    bestAway = there;
                    bestRow = i;
                }
            }
            if (bestRow >= 0) {
                travelAt_ = clock_;
                play.travel(bestRow);
                return true;
            }
        }
    }
    const bool asked = column == walkedColumn_ && row == walkedRow_;
    if (asked && (hero.walking || clock_ - askedAt_ < kReask)) return true;
    if (asked && !hero.walking) ++still_;
    walkedColumn_ = column;
    walkedRow_ = row;
    askedAt_ = clock_;
    play.walkTo(column, row);
    return true;
}

}  // namespace mu::game
