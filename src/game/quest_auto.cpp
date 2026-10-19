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
#include "sim/maps.h"
#include "sim/quests.h"
#include "sim/realm.h"
#include "sim/rules.h"
#include "sim/skills.h"
#include "sim/travel.h"

namespace mu::game {
namespace {

// MU's map numbers run below this (sim/gates.cpp keeps the same bound).
constexpr int kMapNumbers = 128;

// Each built map's breeds and how many of each its nests hold.
struct Living {
    int map = -1;
    std::map<int32_t, int> population;
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

// How often the hand looks again, and how long an order is left to work before it is asked again.
constexpr double kThink = 0.25;
constexpr double kReask = 2.5;
constexpr double kTravelWait = 6.0;
// A giver answered this many times without the quest moving: refused (his bag, his level).
constexpr int kTries = 3;
// Stairs walked in a row toward a nest he cannot walk to, at the most.
constexpr int kStairs = 4;

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
        for (const content::MonsterNest& nest : tables.nests) {
            if (nest.kind >= tables.kinds.size()) continue;
            living.population[tables.kinds[nest.kind].number] += int(nest.count);
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
            if (map != here) return goTo(play, map, stop);
            if (!hunt(play, breed)) {
                return stop(std::string("Auto-play: the ") + row.steps[step].line +
                            " left cannot be reached from here.");
            }
            return {};
        }
    }
    return {};
}

// The realm's own trip: a Move row to the map, paid, when one is open to him; else on foot to
// the gate toward it.
std::string QuestAuto::goTo(Play& play, int map, const std::function<std::string(const std::string&)>& stop) {
    const sim::Realm& realm = play.realm();
    const int here = int(realm.tables()->map);
    if (realm.questing() >= 0) play.closeQuest();
    if (clock_ - travelAt_ < kTravelWait) return {};
    for (int i = 0; i < sim::kTravels; ++i) {
        if (sim::travelAt(i).map != map || realm.travelRefusal(i) != sim::TravelRefusal::None) continue;
        travelAt_ = clock_;
        core::logf("auto-play: by the Move list to %s", sim::travelAt(i).name);
        play.travel(i);
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
    const sim::Request& order = realm.order();
    const bool asked = order.kind == sim::Request::Kind::Talk && order.target == uint32_t(folk);
    if (asked && (realm.hero().walking || clock_ - askedAt_ < kReask)) return;
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
        if (hero.mana < row->mana || !row->suits(offHand)) continue;
        if (const auto tried = guardTried_.find(skill); tried != guardTried_.end() && tried->second > clock_) continue;
        guardTried_[skill] = clock_ + 4.0;
        core::logf("auto-play: %s raised", row->name);
        play.castHero(skill, hero.id);
        return true;
    }
    return false;
}

// **What he fights with.** The right button's skill when the slot holds one; with it empty, the
// first attack on his bar he knows and his hand may throw -- a wizard with Energy Ball on a key
// and nothing on the right button swung his staff (bug #26, 2026-10-19: "it does physical with
// staff if you do not have equipped any Main Skill. Could it use any or first registered in
// QWERT?"). A primary first, as it is cast over and over; else any blow, and the realm swings
// while its cooldown runs (Realm::press). No aura, summon, charge, channel or Teleport. -1 keeps
// the quick slot's own.
int32_t QuestAuto::striker(Play& play) const {
    if (play.quickSkill() != 0) return -1;
    const sim::Realm& realm = play.realm();
    const uint32_t offHand = realm.wearer().offHand;
    int32_t any = -1;
    for (int i = 0; i < barCount_; ++i) {
        const int32_t skill = bar_[i];
        const sim::SkillRow* row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || !realm.knows(skill) || !row->suits(offHand)) continue;
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
        if (body.quarry == hero.id && away < attackerAway) {
            attackerAway = away;
            attacker = &body;
        }
        if (ofBreed(body) && away < best && walkable(body.column(), body.row())) {
            best = away;
            nearest = &body;
        }
    }
    const sim::Body* fightNow = attacker ? attacker : (held && held->alive() && ofBreed(*held)) ? held : nearest;
    // His guard first, when it has lapsed and something is to be fought.
    if (fightNow && guard(play)) return true;
    if (fightNow) {
        const sim::Request& order = realm.order();
        const bool onIt = order.kind == sim::Request::Kind::Attack && order.target == fightNow->id;
        if (!onIt || fightNow->id != target_) {
            target_ = fightNow->id;
            play.fight(target_, striker(play));
        }
        nest_ = -1;
        still_ = 0;
        return true;
    }
    target_ = 0;
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
        if (const sim::EnterGate* stair = stairToward(realm, hero, hero.column(), hero.row(), toColumn, toRow)) {
            column = (stair->box.x1 + stair->box.x2) / 2;
            row = (stair->box.y1 + stair->box.y2) / 2;
        } else {
            if (clock_ - travelAt_ < kTravelWait) return true;
            int bestRow = -1;
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
            if (bestRow < 0) return false;
            travelAt_ = clock_;
            core::logf("auto-play: by the Move list to %s, toward a nest at %d,%d", sim::travelAt(bestRow).name,
                       toColumn, toRow);
            play.travel(bestRow);
            return true;
        }
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
