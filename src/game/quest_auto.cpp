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
            for (int i = 0; i < listed; ++i) {
                const int s = order[i];
                if (row.steps[s].kind != sim::QuestStepKind::Clear) continue;
                if (progress.counts[s] >= quest_world::goal(realm, q, s)) continue;
                breed = row.steps[s].target;
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
            hunt(play, breed);
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
// -- the knight's Defense, the wizard's Soul Barrier, the elf's Greater Defense -- cast on himself
// once its boon has lapsed, it is ready, paid for and his hand may throw it -- so a guard behind a
// shield is never asked with two hands on one weapon or a blade in each (SkillRow::suits off the
// left hand; the user, the same day: "if char cant use def aura (dual wield, two-hands) weapon
// than we dont use it"). The bot's guard()
// (tools/bot/brain.h), for the boons only and only those he put on his bar. A cast the realm
// refuses starts no cooldown, so one is not asked again for a few seconds.
bool QuestAuto::guard(Play& play) {
    const sim::Realm& realm = play.realm();
    const sim::Body& hero = realm.hero();
    if (realm.casting() || hero.boonUntil > realm.tick()) return false;
    // Nothing is thrown on a safe tile (Realm::invoke): in town it waits for the field.
    if (realm.tables()->grid.safe(hero.column(), hero.row())) return false;
    const uint32_t offHand = realm.wearer().offHand;
    for (int i = 0; i < barCount_; ++i) {
        const int32_t skill = bar_[i];
        const sim::SkillRow* row = skill > 0 ? sim::skillNumbered(skill) : nullptr;
        if (row == nullptr || row->boonTicks <= 0 || !realm.knows(skill) || realm.cooling(skill) > 0) continue;
        if (hero.mana < row->mana || !row->suits(offHand)) continue;
        if (const auto tried = guardTried_.find(skill); tried != guardTried_.end() && tried->second > clock_) continue;
        guardTried_[skill] = clock_ + 4.0;
        core::logf("auto-play: %s raised", row->name);
        play.castHero(skill, hero.id);
        return true;
    }
    return false;
}

// The nearest of the breed in sight -- whatever is fighting him first -- and with nobody in
// sight, its nests walked one after another.
void QuestAuto::hunt(Play& play, int32_t breed) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Body& hero = realm.hero();
    if (realm.questing() >= 0) play.closeQuest();
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
        if (ofBreed(body) && away < best) {
            best = away;
            nearest = &body;
        }
    }
    const sim::Body* fightNow = attacker ? attacker : (held && held->alive() && ofBreed(*held)) ? held : nearest;
    // His guard first, when it has lapsed and something is to be fought.
    if (fightNow && guard(play)) return;
    if (fightNow) {
        const sim::Request& order = realm.order();
        const bool onIt = order.kind == sim::Request::Kind::Attack && order.target == fightNow->id;
        if (!onIt || fightNow->id != target_) {
            target_ = fightNow->id;
            play.fight(target_);
        }
        nest_ = -1;
        still_ = 0;
        return;
    }
    target_ = 0;
    // Its nests on this map, the nearest first and then round them.
    std::vector<const content::MonsterNest*> nests;
    for (const content::MonsterNest& nest : tables.nests) {
        if (nest.kind < tables.kinds.size() && tables.kinds[nest.kind].number == breed) nests.push_back(&nest);
    }
    if (nests.empty()) return;
    const auto middle = [](const content::MonsterNest& nest, int* column, int* row) {
        *column = (nest.x1 + nest.x2) / 2;
        *row = (nest.y1 + nest.y2) / 2;
    };
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
    int column = 0, row = 0;
    middle(*nests[size_t(nest_)], &column, &row);
    const float away = std::hypot(float(column) - hero.x, float(row) - hero.y);
    // There and nobody of its breed about: the next nest.
    if (away < 4.0f || still_ >= 2) {
        nest_ = (nest_ + 1) % int(nests.size());
        still_ = 0;
        middle(*nests[size_t(nest_)], &column, &row);
    }
    // A long way off on a map of floors (the Dungeon's, the Lost Tower's): a Move row of this map
    // that lands nearer, when one is open to him.
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
            return;
        }
    }
    const bool asked = column == walkedColumn_ && row == walkedRow_;
    if (asked && (hero.walking || clock_ - askedAt_ < kReask)) return;
    if (asked && !hero.walking) ++still_;
    walkedColumn_ = column;
    walkedRow_ = row;
    askedAt_ = clock_;
    play.walkTo(column, row);
}

}  // namespace mu::game
