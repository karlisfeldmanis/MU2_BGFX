// The bot: a character played from level 1 with no window, the way a player would -- hunting
// what his level can take, drinking potions, resting in town when he is low, picking up what
// falls, wearing what is better, learning the orbs he can read, walking to town to sell, repair,
// restock and buy gear, and doing the quests: Marlon's in Lorencia, Peia's in Noria, Apostle
// Devin's in Devias and the Golden Archer's three floors of the Dungeon. It writes what happened,
// and when.
//
// The hand is the bot's and not the sim's: it only asks the realm what a player may ask --
// requests, potion right-clicks, drags, counters, a giver's dialog, a gate walked into or a trip
// paid for -- so whatever it finds (a jewel's wait, a breed that kills him, a quest he cannot
// finish) is the game's answer and not a shortcut's. A map change is the mode's in the game
// (PlayMode::travel): the hero's record is taken and laid on a realm raised on the next world,
// and the bot does exactly that. The quests' twelve hours run on a wall clock the bot keeps off
// its own ticks, so a long run sees them come back.
//
// Not the seeded hunt: `mu2 --headless` is the log the tests compare, and this changes nothing
// in it. The realm's own log is silenced for the hours of play; BOT_LOG=1 lets it through, which
// is where a skill pressed and never thrown says why.
//
//   build/bot [--kin dk|dw|elf] [--seed N] [--runs N] [--hours H] [--until-jewel]
//             [--no-quests] [--fights] [--build s,a,v,e] [--no-shop-skills] [--quiet]

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "content/tables.h"
#include "core/log.h"
#include "sim/gates.h"
#include "sim/items.h"
#include "sim/market.h"
#include "sim/quests.h"
#include "sim/random.h"
#include "sim/realm.h"
#include "sim/skills.h"
#include "sim/travel.h"
#include "sim/wear.h"

using namespace mu;

namespace {

constexpr float kSight = 12.0f;       // MU's InfoRange, as the headless hand looks
constexpr float kLootReach = 10.0f;
constexpr int kThink = 5;             // ticks between decisions: four a second
constexpr int64_t kGiveUp = 30 * 20;  // a target not won in thirty seconds is out of reach
constexpr int64_t kForget = 5 * 60 * 20;
constexpr int64_t kFear = 15 * 60 * 20;  // a breed that killed him twice is left this long
constexpr int64_t kEpoch = 1800000000;
constexpr int kElfEnergyFrom = 70;
constexpr int kRuneWeaponLevel = 40;  // a weapon rune is set only in a weapon of this drop level up     // the level the elf starts keeping energy for her orbs   // the wall clock's start, unix seconds


// The worlds a quest takes him to: MU's map number, the cooked world, where one arrives.
struct WorldRow {
    int map;
    const char* name;
    int arrive[2];
};
constexpr WorldRow kWorlds[] = {
    {0, "lorencia", {138, 124}},  // the birth tile every seeded run starts from
    {1, "dungeon", {108, 247}},
    {2, "devias", {207, 42}},
    {3, "noria", {174, 112}},
    {4, "losttower", {208, 75}},
};
const WorldRow* worldOf(int map) {
    for (const WorldRow& w : kWorlds) {
        if (w.map == map) return &w;
    }
    return nullptr;
}

struct Options {
    sim::Kin kin = sim::Kin::DarkKnight;
    uint64_t seed = 1;
    int runs = 1;
    double hours = 4.0;
    bool untilJewel = false;
    bool quests = true;
    bool quiet = false;
    bool fights = false;  // --fights: a line every half hour on how he fights
    int build[4] = {};    // --build s,a,v,e: the weights his points are spent by, else his class's
    bool noShopSkills = false;  // --no-shop-skills: buys no orb or scroll, reads only what drops
};

std::string clock(int64_t tick) {
    const int64_t s = tick / 20;
    char line[32];
    std::snprintf(line, sizeof(line), "%lld:%02lld:%02lld", (long long)(s / 3600),
                  (long long)(s / 60 % 60), (long long)(s % 60));
    return line;
}

const char* kinName(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return "Dark Wizard";
        case sim::Kin::FairyElf: return "Fairy Elf";
        case sim::Kin::DarkKnight: return "Dark Knight";
    }
    return "?";
}

const char* cradleWeapon(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return "Staff01";
        case sim::Kin::FairyElf: return "Bow01";
        case sim::Kin::DarkKnight: return "Axe01";
    }
    return "";
}

// What a run ends with, for the summary across seeds.
struct Outcome {
    int64_t ticks = 0;
    int level = 1, kills = 0, deaths = 0;
    int64_t zen = 0, firstJewelTick = -1;
    int firstJewelKills = 0, firstJewelLevel = 0;
    std::string firstJewel;
    int jewels = 0, runes = 0, trips = 0, bought = 0, sold = 0, drunk = 0, maps = 0;
    int handedIn[sim::kQuests] = {};
    int64_t firstHandIn[sim::kQuests];
    Outcome() { std::fill(std::begin(firstHandIn), std::end(firstHandIn), int64_t(-1)); }
};

class Bot {
public:
    Bot(const Options& options, uint64_t seed) : options_(options), seed_(seed) {}

    bool start() {
        const WorldRow& home = *worldOf(homeMap());
        tables_ = world(home.map);
        if (!tables_) return false;
        realm_ = std::make_unique<sim::Realm>();
        if (!realm_->raise(tables_, seed_, home.arrive[0], home.arrive[1], options_.kin, 1)) return false;
        const int32_t weapon = tables_->armNamed(cradleWeapon(options_.kin));
        if (!realm_->equip(weapon, -1)) realm_->equip(weapon, -1, true);
        settle();
        return true;
    }

    int64_t now() const { return clock_; }
    bool foundJewel() const { return out_.firstJewelTick >= 0; }

    // One tick: a decision when one is due, the realm's step, what came of it, and a map change
    // when one is owed.
    void tick() {
        // The share of a blow his guard lets through, the least seen while one was up.
        if (realm_->hero().boonUntil > realm_->tick()) {
            guardSeen_ = std::min(guardSeen_, double(realm_->hero().stats.damageTaken));
        }
        if (clock_ % kThink == 0) play();
        if (realm_->hero().alive()) {
            ++tally_.alive;
            if (realm_->hero().mana < 3) ++tally_.dry;
            if (realm_->hero().walking) ++tally_.walking;
            if (mode_ == Mode::Hunt) ++tally_.hunt;
            else if (mode_ == Mode::Rest) ++tally_.rest;
            else ++tally_.town;
            if (owedMap_ >= 0 || awayTo_ >= 0 || tripOwed_ || restOwed_ ||
                (mode_ == Mode::Hunt && aimMap_ != map())) ++tally_.travel;
        }
        if (options_.fights && clock_ > 0 && clock_ % (30 * 60 * 20) == 0) fights();
        realm_->step();
        ++clock_;
        heard();
        if (owedMap_ >= 0) {
            const int map = owedMap_;
            owedMap_ = -1;
            enter(map, owedColumn_, owedRow_);
        }
    }

    Outcome finish() {
        out_.ticks = clock_;
        out_.level = realm_->hero().level;
        out_.zen = realm_->money();
        return out_;
    }

    void printKills() const {
        std::vector<std::pair<int, std::string>> rows;
        for (const auto& [name, n] : killsOf_) rows.push_back({n, name});
        std::sort(rows.rbegin(), rows.rend());
        std::printf("  kills:");
        for (const auto& [n, name] : rows) std::printf(" %s %d,", name.c_str(), n);
        std::printf("\n  casts:");
        for (const auto& [name, n] : castsOf_) std::printf(" %s %d,", name.c_str(), n);
        std::printf("\n  quests:");
        for (int q = 0; q < sim::kQuests; ++q) {
            const sim::QuestProgress& p = realm_->quest(q);
            const char* state = p.state == sim::QuestState::Active    ? "under way"
                                : p.state == sim::QuestState::Ready   ? "ready"
                                : p.state == sim::QuestState::Resting ? "resting"
                                                                      : "not taken";
            std::printf(" %s x%d (%s);", sim::questAt(q).title, out_.handedIn[q], state);
        }
        // What is left of each quest under way, and what a kill of each breed costs him now.
        for (int q = 0; q < sim::kQuests; ++q) {
            if (realm_->quest(q).state != sim::QuestState::Active) continue;
            const sim::QuestRow& row = sim::questAt(q);
            std::printf("\n    %s left:", row.title);
            for (int s = 0; s < row.stepCount; ++s) {
                const sim::QuestStepRow& step = row.steps[s];
                if (step.kind != sim::QuestStepKind::Clear || realm_->quest(q).counts[s] >= step.count) continue;
                const int home = const_cast<Bot*>(this)->homeOf(step.target);
                const content::MonsterKind* kind = home >= 0 ? const_cast<Bot*>(this)->kindOf(home, step.target) : nullptr;
                std::printf(" %s %d/%d (a kill costs %.0f of %d)", step.line, realm_->quest(q).counts[s], step.count,
                            kind ? costOf(*kind) : -1.0, realm_->hero().maxHealth);
            }
        }
        std::printf("\n  worn:");
        for (int slot = 0; slot < sim::kWorn; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            std::printf(" %s", tables_->items[size_t(one.item)].label.c_str());
            if (one.refinement) std::printf(" +%d", one.refinement);
            if (one.excellent) std::printf(" (excellent)");
            // Its sockets: each rune by name, an empty one as `-`.
            if (one.sockets > 0) {
                std::printf(" [");
                for (int k = 0; k < std::min<int>(one.sockets, 3); ++k) {
                    const sim::PowerRow* power = one.powers[k] ? sim::powerOf(one.powers[k]) : nullptr;
                    std::printf("%s%s", k ? ", " : "", power ? power->name : "-");
                }
                std::printf("]");
            }
            std::printf(",");
        }
        // How deep his gear is (the user, 2026-10-03: "its wierd that bots are not ending with
        // good gears"): the drop levels of his weapon, shield and five armour pieces, their mean
        // and their plus's, against his own level.
        {
            int pieces = 0, levels = 0, pluses = 0;
            for (int slot = sim::kWeaponRight; slot <= sim::kBoots; ++slot) {
                const sim::Held& one = realm_->satchel()[slot];
                if (one.empty() || sim::ammunition(tables_->items[size_t(one.item)])) continue;
                ++pieces;
                levels += tables_->items[size_t(one.item)].dropLevel;
                pluses += one.refinement;
            }
            if (pieces > 0) {
                std::printf("\n  gear: drop level %.0f, plus %.1f over %d pieces", double(levels) / pieces,
                            double(pluses) / pieces, pieces);
            }
        }
        const sim::Body& hero = realm_->hero();
        std::printf("\n  damage %d-%d, defence %d, health %d, points %d/%d/%d/%d, in %s\n",
                    hero.stats.minimumDamage, hero.stats.maximumDamage, hero.stats.defense,
                    hero.maxHealth, hero.points.strength, hero.points.agility,
                    hero.points.vitality, hero.points.energy, worldOf(map())->name);
    }

private:
    enum class Mode { Hunt, Rest, Town };
    // What he is after, chosen every two seconds: a quest to hand in, one to hunt for, one to
    // take, or none -- the grind, on the map whose breeds suit him best.
    enum class Aim { Grind, HandIn, Hunt, Accept };

    // ---- worlds ---------------------------------------------------------------------------
    // A world's tables, loaded the first time it is asked for and kept: a realm holds a pointer.
    const content::Tables* world(int map) {
        auto it = worlds_.find(map);
        if (it != worlds_.end()) return it->second.get();
        const WorldRow* row = worldOf(map);
        if (!row) return nullptr;
        auto tables = std::make_unique<content::Tables>();
        const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/" + row->name + "/" + row->name + ".mur";
        std::string error;
        if (!content::loadTables(path, *tables, error)) {
            std::printf("bot: %s: %s\n", path.c_str(), error.c_str());
            worlds_[map] = nullptr;
            return nullptr;
        }
        return (worlds_[map] = std::move(tables)).get();
    }
    int map() const { return int(tables_->map); }

    // Onto another map, as PlayMode::travel takes him: his record laid on a realm raised there.
    void enter(int map, int column, int row) {
        const content::Tables* next = world(map);
        if (!next) return;
        const sim::HeroRecord record = realm_->record();
        auto realm = std::make_unique<sim::Realm>();
        if (!realm->raise(next, seed_ + uint64_t(++out_.maps) * 7919u, column, row, options_.kin, record.level)) return;
        realm->restore(record);
        realm_ = std::move(realm);
        tables_ = next;
        settle();
        say("-> %s", worldOf(map)->name);
    }

    // What hangs off a realm, set again after each one: the clock, the merchants, the targets.
    void settle() {
        realm_->setWallClock(kEpoch + clock_ / 20);
        banned_.clear();
        chasing_ = 0;
        errands_.clear();
        mode_ = Mode::Hunt;
        sellers_.clear();
        for (size_t i = 0; i < tables_->folk.size(); ++i) {
            if (sim::sells(tables_->folk[i].number)) sellers_.push_back(int(i));
        }
        // Gear first, potions last: what is left after gear buys the potions.
        std::stable_sort(sellers_.begin(), sellers_.end(), [&](int a, int b) {
            return potionShelf(a) < potionShelf(b);
        });
        const int32_t* box = tables_->safeGate;
        safe_ = box[2] > box[0] && box[3] > box[1];
        restAt_[0] = (box[0] + box[2]) / 2;
        restAt_[1] = (box[1] + box[3]) / 2;
        // Home for the counters or a rest: what he came for, now he is here.
        if (tripOwed_ && !sellers_.empty()) {
            tripOwed_ = false;
            startTrip(false);
        } else if (restOwed_ && safe_) {
            restOwed_ = false;
            mode_ = Mode::Rest;
        }
    }

    bool potionShelf(int folk) const {
        int count = 0;
        const sim::Offer* shelf = sim::stockOf(tables_->folk[size_t(folk)].number, &count);
        for (int i = 0; i < count; ++i) {
            const int item = tables_->itemAt(shelf[i].group, shelf[i].number);
            if (item >= 0 && sim::heals(tables_->items[size_t(item)])) return true;
        }
        return false;
    }

    // The way to another map from this one: a paid trip when its row is open and he may take
    // it, else the nearest gate that leads there (or toward it, one map at a time). False with
    // neither. `gate` gets the enter gate to walk into, or -1 for the trip in `travel`.
    bool way(int to, int* gate, int* travel) const {
        *gate = -1;
        *travel = -1;
        for (int i = 0; i < sim::kTravels; ++i) {
            const sim::TravelRow& row = sim::travelAt(i);
            if (row.map != to) continue;
            if (realm_->travelRefusal(i) == sim::TravelRefusal::None && realm_->money() >= row.zen + 500) {
                *travel = i;
                return true;
            }
        }
        // Breadth first over the gates, from this map, each a hop he has the level for.
        std::map<int, int> firstGate;  // map -> the gate on this map that starts the way there
        std::vector<int> frontier{map()};
        firstGate[map()] = -1;
        for (size_t at = 0; at < frontier.size(); ++at) {
            const int from = frontier[at];
            for (int n = 0; n < 512; ++n) {
                const sim::EnterGate* in = sim::enterGateNumbered(n);
                if (!in || int(in->map) != from || in->target < 0) continue;
                if (in->level > realm_->hero().level) continue;
                const sim::ExitGate* out = sim::exitGate(in->target);
                if (!out || !worldOf(int(out->map)) || firstGate.count(int(out->map))) continue;
                firstGate[int(out->map)] = from == map() ? n : firstGate[from];
                frontier.push_back(int(out->map));
            }
        }
        const auto it = firstGate.find(to);
        if (it == firstGate.end() || it->second < 0) return false;
        *gate = it->second;
        return true;
    }

    bool reachable(int to) const {
        int gate, travel;
        return to == map() || way(to, &gate, &travel);
    }

    // A step toward another map: pays for the trip, or walks into the gate. True while he is
    // on his way.
    bool goTo(int to) {
        if (to == map()) return false;
        int gate, travel;
        if (!way(to, &gate, &travel)) return false;
        if (travel >= 0) {
            const sim::TravelRow& row = sim::travelAt(travel);
            if (realm_->travel(travel)) {
                say("pays %lld zen to travel to %s", (long long)row.zen, row.name);
                if (row.map != map()) {
                    owedMap_ = row.map;
                    owedColumn_ = row.column;
                    owedRow_ = row.row;
                }
            }
            return true;
        }
        const sim::EnterGate* in = sim::enterGateNumbered(gate);
        const sim::Body& hero = realm_->hero();
        if (!hero.walking || walkingTo_ != gate) {
            sim::Request request;
            request.kind = sim::Request::Kind::WalkTo;
            request.column = (in->box.x1 + in->box.x2) / 2;
            request.row = (in->box.y1 + in->box.y2) / 2;
            realm_->ask(request);
            walkingTo_ = gate;
        }
        return true;
    }

    // ---- how he fights (--fights) ---------------------------------------------------------
    static bool isSpell(int skill) {
        const sim::SkillRow* row = sim::skillNumbered(skill);
        return row && row->wizardry;
    }
    struct Tally {
        int landed = 0, missed = 0, kills = 0, drunk = 0;
        int64_t dealt = 0, taken = 0, alive = 0, dry = 0, walking = 0;
        // Where the time went: hunting, resting, at the counters; and, hunting, the share spent
        // getting to another map (an aim's or a trip's).
        int64_t hunt = 0, rest = 0, town = 0, travel = 0;
        std::map<int, int> cast;
    } tally_;

    void fights() {
        const Tally& t = tally_;
        const double minutes = 30.0;
        const sim::Body& hero = realm_->hero();
        std::printf("  [%s] level %d, %.1f kills/min, %d blows landed and %d missed (%.0f%%), "
                    "%.0f a blow, %.0f dealt and %.0f taken a minute, %.0f potions a minute; "
                    "out of mana %.0f%% and walking %.0f%% of the time; health %d mana %d\n",
                    clock(clock_).c_str(), hero.level, (out_.kills - t.kills) / minutes, t.landed,
                    t.missed, 100.0 * t.landed / std::max(1, t.landed + t.missed),
                    double(t.dealt) / std::max(1, t.landed), t.dealt / minutes, t.taken / minutes,
                    (out_.drunk - t.drunk) / minutes, 100.0 * t.dry / std::max<int64_t>(1, t.alive),
                    100.0 * t.walking / std::max<int64_t>(1, t.alive), hero.maxHealth, hero.maxMana);
        const double a = double(std::max<int64_t>(1, t.alive));
        std::printf("             time: hunting %.0f%%, resting %.0f%%, in town %.0f%%, on the way to "
                    "another map %.0f%%\n", 100.0 * t.hunt / a, 100.0 * t.rest / a, 100.0 * t.town / a,
                    100.0 * t.travel / a);
        if (!t.cast.empty()) {
            std::printf("             casts:");
            for (const auto& [skill, n] : t.cast) {
                const sim::SkillRow* row = sim::skillNumbered(skill);
                std::printf(" %s %d,", row ? row->name : "?", n);
            }
            std::printf("\n");
        }
        tally_ = Tally{};
        tally_.kills = out_.kills;
        tally_.drunk = out_.drunk;
    }

    // ---- what happened --------------------------------------------------------------------
    void heard() {
        const uint32_t me = realm_->hero().id;
        for (const sim::Happening& h : realm_->happenings()) {
            // Every skill he threw, by name, for the report's `casts:` line -- what the realm
            // let go, not what the bot asked for.
            if (h.what == sim::What::Cast && h.who == me) {
                if (const sim::SkillRow* row = sim::skillNumbered(h.a)) ++castsOf_[row->name];
            }
            switch (h.what) {
                case sim::What::Died:
                    if (h.who == me) {
                        died(h.whom);
                    } else if (h.whom == me) {
                        ++out_.kills;
                        const sim::Body* dead = bodyOf(h.who);
                        if (dead && dead->kind >= 0) ++killsOf_[tables_->kinds[size_t(dead->kind)].label];
                    }
                    break;
                case sim::What::Dropped:
                    if (h.b >= 0 && size_t(h.b) < tables_->items.size()) dropped(tables_->items[size_t(h.b)]);
                    break;
                case sim::What::Gated:
                    // Through a gate to another map: the exit gate's, at the tile the realm chose.
                    if (h.who == me) {
                        const sim::EnterGate* in = sim::enterGateNumbered(h.a);
                        const sim::ExitGate* out = in ? sim::exitGate(in->target) : nullptr;
                        if (out) {
                            owedMap_ = int(out->map);
                            owedColumn_ = h.b;
                            owedRow_ = h.c;
                        }
                    }
                    break;
                case sim::What::Rose:
                    // Risen on a map with no safe zone: owed Lorencia, as the mode takes him.
                    if (h.who == me && h.c == 1) {
                        owedMap_ = 0;
                        owedColumn_ = kWorlds[0].arrive[0];
                        owedRow_ = kWorlds[0].arrive[1];
                    }
                    break;
                case sim::What::Hit:
                    if (h.who == me) {
                        ++tally_.landed;
                        tally_.dealt += h.a;
                    } else if (h.whom == me) {
                        tally_.taken += h.a;
                    }
                    break;
                case sim::What::Missed:
                    if (h.who == me) ++tally_.missed;
                    break;
                case sim::What::Loosed:
                case sim::What::Cast:
                    // A spell says Loosed when it leaves his hands; a knight's skill says Cast.
                    if (h.who == me && (h.what == sim::What::Loosed || !isSpell(h.a))) ++tally_.cast[h.a];
                    break;
                case sim::What::QuestStep:
                    if (h.a >= 0 && h.a < sim::kQuests && h.c >= 0 && h.c < sim::questAt(h.a).stepCount &&
                        h.b == sim::questAt(h.a).steps[h.c].count) {
                        say("  %s: %s done", sim::questAt(h.a).title, sim::questAt(h.a).steps[h.c].line);
                    }
                    break;
                case sim::What::QuestReady:
                    say("quest done: %s -- back to %s", sim::questAt(h.a).title, sim::questAt(h.a).giverName);
                    break;
                default:
                    break;
            }
        }
        const int level = realm_->hero().level;
        if (level > lastLevel_) {
            if (level / 10 > lastLevel_ / 10 || level <= 5) {
                say("level %d (%d kills, %lld zen)", level, out_.kills, (long long)realm_->money());
            }
            lastLevel_ = level;
        }
    }

    void died(uint32_t by) {
        ++out_.deaths;
        const sim::Body* killer = bodyOf(by);
        const int breed = killer ? killer->kind : -1;
        if (breed < 0) {
            say("dies");
            return;
        }
        const content::MonsterKind& kind = tables_->kinds[size_t(breed)];
        if (++deathsTo_[kind.number] >= 2) {
            fearUntil_[kind.number] = clock_ + kFear;
            deathsTo_[kind.number] = 0;
            say("killed by %s; leaves them for 15 min", kind.label.c_str());
        } else {
            say("killed by %s", kind.label.c_str());
        }
    }

    void dropped(const content::ItemRow& row) {
        if (!sim::refiningJewel(row) && !sim::creation(row)) return;
        sim::creation(row) ? ++out_.runes : ++out_.jewels;
        if (out_.firstJewelTick < 0) {
            out_.firstJewelTick = clock_;
            out_.firstJewel = row.label;
            out_.firstJewelKills = out_.kills;
            out_.firstJewelLevel = realm_->hero().level;
        }
        say("** %s dropped (kill %d, level %d)", row.label.c_str(), out_.kills, realm_->hero().level);
    }

    __attribute__((format(printf, 2, 3))) void say(const char* format, ...) {
        if (options_.quiet) return;
        std::printf("  [%s] ", clock(clock_).c_str());
        va_list args;
        va_start(args, format);
        std::vprintf(format, args);
        va_end(args);
        std::printf("\n");
    }

    const sim::Body* bodyOf(uint32_t id) const {
        for (const sim::Body& b : realm_->bodies()) {
            if (b.id == id) return &b;
        }
        return nullptr;
    }

    const content::ItemRow& rowOf(const sim::Held& held) const { return tables_->items[size_t(held.item)]; }

    // ---- a decision -----------------------------------------------------------------------
    void play() {
        realm_->setWallClock(kEpoch + clock_ / 20);
        const sim::Body& hero = realm_->hero();
        if (!hero.alive()) {
            mode_ = Mode::Hunt;
            errands_.clear();
            return;
        }
        spend();
        drink();
        mend();
        learnCaution();
        if (clock_ >= nextSort_) {
            sortBag();
            nextSort_ = clock_ + 100;
        }
        if (clock_ >= nextAim_) {
            choose();
            nextAim_ = clock_ + 40;
        }
        switch (mode_) {
            case Mode::Hunt: hunt(); break;
            case Mode::Rest: rest(); break;
            case Mode::Town: town(); break;
        }
    }

    // ---- points ---------------------------------------------------------------------------
    // Each class's usual build, or --build's: the knight strength and vitality, the elf agility,
    // the wizard energy -- six tenths of it, since every spell is energy/9 to energy/4 with the
    // monster's whole defence off it; at four tenths he dealt a third of the damage and killed
    // half as much (the bot's runs, 2026-10-01).
    void spend() {
        int points = realm_->hero().pointsInHand;
        if (points <= 0) return;
        // First what a carried piece of his class asks that he is a few points short of (the
        // user, 2026-10-03: a socketed drop or reward has to be read as a replacement): the
        // Catacombs' Double Blade asks 120 agility, a knight of level 107 had 113 and carried it
        // into the next sale. Within thirty points, strength and agility both.
        {
            const sim::Body& me = realm_->hero();
            for (int slot = sim::kWorn; slot < sim::kSlots && points > 0; ++slot) {
                const sim::Held& one = realm_->satchel()[slot];
                if (!nearlyFits(one)) continue;
                const auto a = sim::asks(rowOf(one), one.refinement, one.excellent != 0);
                const int str = std::max(0, int(a.strength) - me.points.strength);
                const int agi = std::max(0, int(a.agility) - me.points.agility);
                const int s1 = std::min(points, str);
                const int a1 = std::min(points - s1, agi);
                realm_->spend(s1, a1, 0, 0);
                points -= s1 + a1;
            }
            if (points <= 0) return;
        }
        // First the strength his class's best armour at his level asks (Needs: 3 x drop level x
        // raw / 100 + 20), as a player keeps up with his set: a wizard of level 114 with all of it
        // in energy had 21 and could not wear even the Pad Armor, which asks 29.
        const sim::Body& hero = realm_->hero();
        int asked = 0;
        for (const content::ItemRow& row : tables_->items) {
            if (!row.armour() || row.dropLevel > hero.level) continue;
            if (row.classes != 0 && (row.classes & (1 << int(options_.kin))) == 0) continue;
            asked = std::max(asked, sim::asks(row, 0).strength);
        }
        if (const int short_ = std::min(points, asked - hero.points.strength); short_ > 0) {
            realm_->spend(short_, 0, 0, 0);
            points -= short_;
            if (points <= 0) return;
        }
        // Then, for the elf from kElfEnergyFrom, the energy her orbs at this level ask --
        // Summoning 30, Healing 52, Greater Damage 92 -- as a player keeps enough to read them:
        // on 2/5/2/1 she had 18 at level 118 and read none of the three Lala sells (2026-10-03).
        // Not before 70: from the start it took her agility while she needed it most and her
        // first two quests came 30-40 min later; from 70 they are as quick as without, and over
        // three seeds she ends higher (115-122 against 116-118) on half the potions.
        if (options_.kin == sim::Kin::FairyElf && hero.level >= kElfEnergyFrom) {
            int wants = 0;
            for (const content::ItemRow& row : tables_->items) {
                if (!row.teaches || row.teachesLevel > hero.level) continue;
                if (row.classes != 0 && (row.classes & (1 << int(options_.kin))) == 0) continue;
                wants = std::max(wants, int(row.teachesEnergy));
            }
            if (const int short_ = std::min(points, wants - hero.points.energy); short_ > 0) {
                realm_->spend(0, 0, 0, short_);
                points -= short_;
                if (points <= 0) return;
            }
        }
        // The knight 4/3/2/0 since 2026-10-03: of five builds over three seeds of eight hours it
        // handed in all three quests soonest (50, 79, 161 min against 63, 86, 171 at 4/2/3/0) and
        // ended highest (156-161) -- agility's defence rate and pace over vitality's health.
        int w[4] = {4, 3, 2, 0};  // strength, agility, vitality, energy
        // The wizard 1/1/5/4 since 2026-10-03 ("improve DW bot"): over three seeds his quests
        // came in ~20 minutes sooner than at 1/1/2/6 and his health 574 against 368 -- a wizard
        // whose spells reach the screen is held back by what one blow costs him, not by damage.
        if (options_.kin == sim::Kin::DarkWizard) { w[0] = 1; w[1] = 1; w[2] = 5; w[3] = 4; }
        if (options_.kin == sim::Kin::FairyElf) { w[0] = 2; w[1] = 5; w[2] = 2; w[3] = 1; }
        if (options_.build[0] + options_.build[1] + options_.build[2] + options_.build[3] > 0) {
            std::copy(std::begin(options_.build), std::end(options_.build), w);
        }
        const int sum = w[0] + w[1] + w[2] + w[3];
        int give[4];
        int left = points;
        for (int i = 0; i < 4; ++i) {
            give[i] = points * w[i] / sum;
            left -= give[i];
        }
        give[options_.kin == sim::Kin::DarkWizard ? 3 : options_.kin == sim::Kin::FairyElf ? 1 : 0] += left;
        realm_->spend(give[0], give[1], give[2], give[3]);
    }

    // ---- potions --------------------------------------------------------------------------
    int countOf(bool (*kind)(const content::ItemRow&)) const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && kind(rowOf(one))) n += std::max<int>(1, one.durability);
        }
        return n;
    }

    bool drinkOne(bool (*kind)(const content::ItemRow&)) {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && kind(rowOf(one)) && realm_->useItem(slot)) {
                ++out_.drunk;  // counted here: the next step clears what useItem says
                if (kind == sim::heals) ++healed_;
                return true;
            }
        }
        return false;
    }

    // What his own potions say about the estimate: more than twenty drunk in five minutes and
    // every fight is reckoned half as dear again; under five and it eases back toward the band.
    void learnCaution() {
        if (clock_ < cautionAt_) return;
        cautionAt_ = clock_ + 5 * 60 * 20;
        // Healing potions only (2026-10-03): a knight's mana potions for Uppercut and Lunge were
        // read as danger, and at caution x8 he ground monsters far under his level while taking
        // 10-40 health a minute.
        const int drunk = healed_ - drunkAt_;
        drunkAt_ = healed_;
        const double was = caution_;
        if (drunk > 20) caution_ = std::min(8.0, caution_ * 1.5);
        else if (drunk < 5) caution_ = std::max(1.0, caution_ / 1.2);
        if (caution_ > was) say("%d potions in 5 min: fights reckoned x%.1f", drunk, caution_);
        // **And a grinding ground that drinks him dry whatever he picks there is left** (the
        // user, 2026-10-03: "keep working on weak bots"): a knight grinding the Dungeon's weak
        // Skeleton Warriors drank 30-60 potions every five minutes at caution x8 -- the floor's
        // other breeds were on him the whole time, which no breed's own cost can see. Twenty
        // minutes away from it, grinding wherever is next best.
        if (drunk > 20 && caution_ >= 4.0 && aim_ == Aim::Grind) {
            shunned_[map()] = clock_ + 20 * 60 * 20;
            say("leaves %s for 20 min: %d potions in 5 min", worldOf(map())->name, drunk);
        }
    }

    bool casts() const {
        if (options_.kin != sim::Kin::DarkKnight) return true;
        for (int i = 0; i < sim::skillCount(); ++i) {
            if (realm_->knows(sim::skillAt(i).number)) return true;
        }
        return false;
    }

    void drink() {
        const sim::Body& hero = realm_->hero();
        if (hero.health * 2 < hero.maxHealth) drinkOne(sim::heals);
        if (casts() && hero.mana * 10 < hero.maxMana * 3) drinkOne(sim::restores);
    }

    // ---- the bag --------------------------------------------------------------------------
    int freeCells() const {
        bool taken[sim::kBagRows][sim::kBagColumns] = {};
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            const int at = slot - sim::kWorn, r0 = at / sim::kBagColumns, c0 = at % sim::kBagColumns;
            const content::ItemRow& row = rowOf(one);
            for (int r = r0; r < std::min<int>(sim::kBagRows, r0 + row.height); ++r)
                for (int c = c0; c < std::min<int>(sim::kBagColumns, c0 + row.width); ++c) taken[r][c] = true;
        }
        int n = 0;
        for (auto& r : taken)
            for (bool t : r) n += t ? 0 : 1;
        return n;
    }

    double blow() const {
        const sim::Body& hero = realm_->hero();
        if (options_.kin == sim::Kin::DarkWizard) {
            const sim::Wearer w = realm_->wearer();
            // The staff's rise is a percentage: the band times 1 + rise/100 (rules.cpp).
            return (w.wizardMinimum + w.wizardMaximum) / 2.0 * w.wizardryRate;
        }
        return (hero.stats.minimumDamage + hero.stats.maximumDamage) / 2.0 +
               (hero.stats.offhandMinimumDamage + hero.stats.offhandMaximumDamage) / 2.0;
    }

    // How good he is now: his blow and his guard, as the realm has re-reckoned them.
    double score() const {
        const sim::Body& hero = realm_->hero();
        // **And what his sockets are worth** (the user, 2026-10-03: "if armor or weapon drop with
        // sockets he has to understand that it's a good replacement"): each socket a share of the
        // rest, a set one twice that -- a rune is power the band and the guard do not show, and an
        // empty socket is one waiting for the next. A Double Blade +0 with two lost to a Blade +2
        // with none, and a knight wore the Blade.
        double sockets = 0.0;
        for (int slot = 0; slot < sim::kWorn; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            for (int k = 0; k < std::min<int>(one.sockets, 3); ++k) sockets += one.powers[k] ? 2.0 : 1.0;
        }
        const double base = blow() * 2.0 + hero.stats.defense + hero.stats.defenseRate * 0.5;
        return base * (1.0 + 0.04 * sockets);
    }

    // The same piece: what a trial took off is found again by this, wherever it went.
    static bool same(const sim::Held& a, const sim::Held& b) {
        return a.item == b.item && a.refinement == b.refinement && a.durability == b.durability &&
               a.skill == b.skill && a.luck == b.luck && a.option == b.option &&
               a.excellent == b.excellent && a.sockets == b.sockets &&
               a.powers[0] == b.powers[0] && a.powers[1] == b.powers[1] && a.powers[2] == b.powers[2] &&
               a.affixes[0] == b.affixes[0] && a.affixes[1] == b.affixes[1] &&
               a.affixes[2] == b.affixes[2];
    }
    // Puts every worn slot back to `worn`: a trial that put a weapon on can take the other hand
    // down too, into whatever bag cell was free, and the old one-move undo left that hand bare
    // -- so the next look found a "better" swap again, and a knight traded Small Axe +1 and
    // Hand Axe every five seconds for hours (seed 1, 2026-10-03).
    void restoreWorn(const sim::Held (&worn)[sim::kWorn]) {
        for (int pass = 0; pass < 2; ++pass) {
            for (int w = 0; w < sim::kWorn; ++w) {
                const sim::Held now = realm_->satchel()[w];
                if (same(now, worn[w])) continue;
                if (worn[w].empty()) {
                    for (int to = sim::kWorn; to < sim::kSlots; ++to) {
                        if (realm_->moveItem(w, to)) break;
                    }
                    continue;
                }
                for (int at = sim::kWorn; at < sim::kSlots; ++at) {
                    if (same(realm_->satchel()[at], worn[w])) {
                        realm_->moveItem(at, w);
                        break;
                    }
                }
            }
        }
    }

    // Tries each bag piece in each place it may go, and keeps it there only if he is better.
    // True when anything went on.
    bool wearBest() {
        bool any = false;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held held = realm_->satchel()[slot];
            if (held.empty()) continue;
            const content::ItemRow& row = rowOf(held);
            if (sim::placeOf(row) < 0 || sim::ammunition(row)) continue;
            // Rings and the pendant are wearJewellery's, on its own worth: scored here by their
            // sockets alone, a socketed Ring of Ice and a Ring of Fortune traded places every
            // sort, 1,600 times in six hours (knight, seed 2, 2026-10-03).
            if (sim::jewellery(row)) continue;
            // An elf keeps to the bow: her skills and her arrows are its. And to the one she can
            // feed: a crossbow shoots bolts where a bow shoots arrows, and a switch with no
            // quiver for it and no Zen for one leaves her nothing to shoot.
            if (options_.kin == sim::Kin::FairyElf &&
                (row.shield() || (row.weapon() && row.group != sim::kGroupBows))) continue;
            // And a wizard to his staff: his spells are its rise.
            if (options_.kin == sim::Kin::DarkWizard && row.weapon() && row.magicPower <= 0) continue;
            if (options_.kin == sim::Kin::FairyElf && row.weapon() && archer() &&
                sim::placeOf(row) != (ammoHand() == sim::kWeaponRight ? int(sim::kWeaponLeft) : int(sim::kWeaponRight)) &&
                realm_->money() < 1000) continue;
            for (const int place : {int(sim::kWeaponRight), int(sim::kWeaponLeft), sim::placeOf(row)}) {
                if (!sim::placesIn(row, options_.kin, place)) continue;
                // **The left hand is the shield's** for a knight and a wizard (the user,
                // 2026-10-03: "teach DK to use shield and defense skill", "same with DW"): a
                // second weapon there scored its offhand blow over any shield, and Defense and
                // Soul Barrier, drawn up behind one, were never cast.
                if ((options_.kin == sim::Kin::DarkKnight || options_.kin == sim::Kin::DarkWizard) &&
                    place == int(sim::kWeaponLeft) && row.weapon() && !row.shield()) continue;
                // And a knight's weapon one-handed, so the shield always has its hand: a
                // two-handed Berdysh outscored sword and shield and Defense went uncast again.
                if (options_.kin == sim::Kin::DarkKnight && row.weapon() && !row.shield() &&
                    row.twoHanded()) continue;
                sim::Held worn[sim::kWorn];
                for (int w = 0; w < sim::kWorn; ++w) worn[w] = realm_->satchel()[w];
                // A two-hander wants the other hand empty: that hand's thing into the bag first,
                // and back if the trade is not better.
                int freed = -1;
                const double beforeFreed = score();
                if (row.twoHanded() && !realm_->satchel()[sim::kWeaponLeft].empty() && place == sim::kWeaponRight) {
                    for (int to = sim::kWorn; to < sim::kSlots && freed < 0; ++to) {
                        if (to != slot && sim::movable(*tables_, realm_->wearer(), realm_->satchel(), sim::kWeaponLeft, to) &&
                            realm_->moveItem(sim::kWeaponLeft, to)) freed = to;
                    }
                    if (freed < 0) continue;
                }
                const auto putBack = [&] {
                    if (freed >= 0) realm_->moveItem(freed, sim::kWeaponLeft);
                };
                if (!sim::movable(*tables_, realm_->wearer(), realm_->satchel(), slot, place)) {
                    putBack();
                    continue;
                }
                const bool wasEmpty = realm_->satchel()[place].empty();
                const double before = freed >= 0 ? beforeFreed : score();
                if (!realm_->moveItem(slot, place)) {
                    putBack();
                    continue;
                }
                if (score() > before + 0.5) {
                    if (held.refinement) say("wears %s +%d", row.label.c_str(), held.refinement);
                    else say("wears %s", row.label.c_str());
                    any = true;
                    break;
                }
                // Back as it was: the moved piece first, then every worn slot to what it held.
                if (wasEmpty) realm_->moveItem(place, slot);
                else realm_->moveItem(slot, place);
                putBack();
                restoreWorn(worn);
            }
        }
        return any;
    }

    // Reads the orbs and scrolls he can; what he cannot is sold with the rest.
    void readOrbs() {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty() || rowOf(one).teaches == 0) continue;
            const std::string label = rowOf(one).label;
            if (realm_->useItem(slot)) say("learns from %s", label.c_str());
        }
    }

    // **Jewels and runes put to use** (the user, 2026-10-03: "does bots upgrade items with
    // runes?" -- they did not; they banked them). Each Bless, Soul and Rune of Creation he
    // carries goes onto what he wears: a rune into the first free socket of his weapon, then his
    // shield; a Bless or Soul onto the worn piece with the lowest plus, the weapon first on a
    // tie -- a Soul only below +7, where a failed one would reset it to +0 (kSoulResetFrom).
    // At the Goblin's box: for each carried piece whose runes are worth it, each rune in turn --
    // the piece and a Chaos in, Remove Rune on that socket, the box emptied back into the bag.
    void unrune() {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            if (!unruneWorth(realm_->satchel()[slot])) continue;
            const std::string label = rowOf(realm_->satchel()[slot]).label;
            int piece = slot;
            for (int k = 0; k < 3; ++k) {
                const sim::Held thing = realm_->satchel()[piece];
                if (thing.empty() || k >= thing.sockets || !thing.powers[k]) continue;
                int chaosSlot = -1;
                for (int c = sim::kWorn; c < sim::kSlots && chaosSlot < 0; ++c) {
                    const sim::Held& one = realm_->satchel()[c];
                    if (!one.empty() && chaos(rowOf(one))) chaosSlot = c;
                }
                if (chaosSlot < 0) break;
                const int pieceCell = realm_->putIn(piece);
                const int chaosCell = realm_->putIn(chaosSlot);
                const bool done = pieceCell >= 0 && chaosCell >= 0 && realm_->mix(sim::Service::RemoveRune, k);
                // Everything back, the piece first so it finds a slot.
                piece = -1;
                for (int cell = 0; cell < sim::kMachineCells; ++cell) {
                    if (realm_->machine()[cell].empty()) continue;
                    const bool isPiece = rowOf(realm_->machine()[cell]).label == label;
                    const int back = realm_->takeOut(cell);
                    if (isPiece) piece = back;
                }
                if (done) say("takes a rune out of his %s at the Chaos Goblin", label.c_str());
                if (!done || piece < 0) break;
            }
        }
    }

    void useJewels() {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held jewel = realm_->satchel()[slot];
            if (jewel.empty()) continue;
            const content::ItemRow& row = rowOf(jewel);
            if (sim::creation(row)) {
                // Any worn piece it may be set in, the weapon and shield first: a Keen Eye,
                // a Bloodwell, an Undying go in armour and never in a weapon (sim::settable).
                for (const int to : {int(sim::kWeaponRight), int(sim::kWeaponLeft), 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) {
                    if (to >= sim::kWorn || realm_->satchel()[to].empty()) continue;
                    if (!sim::settable(*tables_, jewel, realm_->satchel()[to], options_.kin)) continue;
                    // **A weapon rune waits for a weapon worth keeping** (the user, 2026-10-03:
                    // "bot saves weapon runes"): Marlon's Stormcall went into the Falchion at
                    // level 40, was sold with it, and the Double Blade's sockets stayed empty.
                    const content::ItemRow& target = rowOf(realm_->satchel()[to]);
                    // The knight's alone: the wizard's Serpent Staff and the elf's Battle Bow are the
                    // weapons they keep, and holding Arcane Echo and Frost Arrow off them cost both
                    // the Knights' Halls on two seeds of three.
                    if (options_.kin == sim::Kin::DarkKnight && target.weapon() && !target.shield() &&
                        target.dropLevel < kRuneWeaponLevel) continue;
                    const std::string on = rowOf(realm_->satchel()[to]).label;
                    if (realm_->refine(slot, to)) {
                        say("sets %s into his %s", row.label.c_str(), on.c_str());
                        break;
                    }
                }
                continue;
            }
            if (!sim::refiningJewel(row)) continue;
            const bool soul = sim::jewelOf(row) == sim::Jewel::Soul;
            int best = -1;
            for (int to = 0; to < sim::kWorn; ++to) {
                const sim::Held& worn = realm_->satchel()[to];
                if (worn.empty() || !sim::refinable(*tables_, jewel, worn)) continue;
                if (soul && worn.refinement >= 7) continue;
                if (best < 0 || worn.refinement < realm_->satchel()[best].refinement) best = to;
            }
            if (best < 0) continue;
            const std::string on = rowOf(realm_->satchel()[best]).label;
            const int was = realm_->satchel()[best].refinement;
            if (realm_->refine(slot, best)) {
                say("%s on %s +%d: +%d", row.label.c_str(), on.c_str(), was,
                    int(realm_->satchel()[best].refinement));
            }
        }
    }

    // What a ring or pendant is worth to him: each power at its plus (sim::affixValue), a point
    // of resistance, the option, and luck's crit. A rough sum; it only has to rank them.
    double jewelleryWorth(const sim::Held& one) const {
        const content::ItemRow& row = tables_->items[size_t(one.item)];
        // Resistance only where something casts it on him: Ice and Poison in 0.75; a Pendant of
        // Fire's or Lightning's turns nothing aside (docs/jewellery.md).
        const sim::Element element = sim::elementOf(row);
        const bool resists = element == sim::Element::Ice || element == sim::Element::Poison;
        double worth = (resists ? sim::resistanceOf(row, one.refinement) : 0) + one.option * 2.0 +
                       (one.luck ? 5.0 : 0.0);
        // A rune set is worth a power; an empty socket a little, for the rune he may set later.
        for (int k = 0; k < std::min<int>(one.sockets, 3); ++k) worth += one.powers[k] ? 6.0 : 1.0;
        if (sim::powered(row)) {
            worth += powerWorth(sim::signatureOf(row), one.refinement);
            for (uint8_t a : one.affixes) {
                if (a != 0) worth += powerWorth(sim::Affix(a), one.refinement);
            }
        }
        return worth + one.refinement;
    }

    // One power to his class (the user, 2026-10-03, "yes" to weighting them so): the Leech most
    // to the knight, who stands in the blows and drinks the most, less to the elf at range and
    // least to the wizard; Fury by how often he crits at all, from half with no luck or Keen Eye
    // to double at 15%. Experience, Zen and item find serve every class the same.
    double powerWorth(sim::Affix affix, int refinement) const {
        double weight = 1.0;
        if (affix == sim::Affix::Leech) {
            weight = options_.kin == sim::Kin::DarkKnight ? 2.5
                     : options_.kin == sim::Kin::FairyElf ? 1.5
                                                          : 1.0;
        }
        if (affix == sim::Affix::Fury) weight = 0.5 + 10.0 * realm_->hero().stats.criticalChance;
        return 4.0 + weight * sim::affixValue(affix, refinement);
    }

    // Puts his best rings and pendant on: each slot takes the bag's best that is worth more than
    // what it holds, the old one going back into the bag. He wore none before 2026-10-03.
    void wearJewellery() {
        for (int place : {int(sim::kAmulet), int(sim::kRingRight), int(sim::kRingLeft)}) {
            const sim::Held& on = realm_->satchel()[place];
            double best = on.empty() ? -1.0 : jewelleryWorth(on);
            int from = -1;
            for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                const sim::Held& one = realm_->satchel()[slot];
                if (one.empty() || !sim::jewellery(tables_->items[size_t(one.item)])) continue;
                if (!sim::placesIn(tables_->items[size_t(one.item)], realm_->hero().kin, place)) {
                    continue;
                }
                if (!sim::fits(*tables_, realm_->wearer(), one)) continue;
                const double worth = jewelleryWorth(one);
                if (worth > best) best = worth, from = slot;
            }
            if (from < 0) continue;
            const std::string label = tables_->items[size_t(realm_->satchel()[from].item)].label;
            if (!on.empty()) {
                const content::ItemRow& old = tables_->items[size_t(on.item)];
                const int spare = realm_->satchel().free(*tables_, old.width, old.height);
                if (spare < 0 || !realm_->moveItem(place, spare)) continue;
            }
            if (realm_->moveItem(from, place)) say("wears %s", label.c_str());
        }
    }

    void sortBag() {
        readOrbs();
        wearBest();
        wearJewellery();
        useJewels();
    }

    // What he would sell, and what he would store: jewels and runes, which a sale keeps.
    int sellable() const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && !keepsAt(slot)) ++n;
        }
        return n;
    }
    bool stores(const sim::Held& one) const {
        const content::ItemRow& row = rowOf(one);
        // A weapon rune of his class is carried for the weapon it waits for (savesRune), and
        // the Jewels of Chaos for the machine's Remove Rune, three of them.
        if (sim::creation(row)) return options_.kin != sim::Kin::DarkKnight || !weaponRune(one);
        if (chaos(row)) return chaosCount() > 3;
        return sim::refiningJewel(row);
    }
    static bool chaos(const content::ItemRow& row) { return row.group == 12 && row.number == 15; }
    int chaosCount() const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && chaos(rowOf(one))) ++n;
        }
        return n;
    }
    // A Rune of Creation whose power goes in a weapon and is his class's.
    bool weaponRune(const sim::Held& one) const {
        if (one.empty() || !sim::creation(rowOf(one))) return false;
        const sim::PowerRow* power = sim::powerOf(one.powers[0]);
        return power != nullptr && (power->slots & sim::kInWeapon) != 0 && power->takenBy(options_.kin);
    }
    // What taking the runes out of a carried piece would cost, or -1 when it has none: the
    // machine's Remove Rune is one Chaos and Zen by the rune's rarity, each.
    int64_t unruneCost(const sim::Held& one) const {
        if (one.empty() || sim::creation(rowOf(one))) return -1;
        int64_t zen = 0;
        int runes = 0;
        for (int k = 0; k < std::min<int>(one.sockets, 3); ++k) {
            const sim::PowerRow* power = one.powers[k] ? sim::powerOf(one.powers[k]) : nullptr;
            if (!power) continue;
            zen += sim::kRemoveRuneZen[int(power->rarity)];
            ++runes;
        }
        return runes > 0 ? zen : -1;
    }
    // Whether a carried, unworn piece with runes set is worth the machine now: the Zen over his
    // potions, a Chaos for each rune, and a Chaos Goblin in this town (the user, 2026-10-03: "bot
    // removes runes when rich").
    bool unruneWorth(const sim::Held& one) const {
        const int64_t zen = unruneCost(one);
        if (zen < 0 || realm_->money() < zen + potionReserve()) return false;
        int runes = 0;
        for (int k = 0; k < std::min<int>(one.sockets, 3); ++k) runes += one.powers[k] ? 1 : 0;
        return chaosCount() >= runes;
    }
    int goblinHere() const {
        for (size_t i = 0; i < tables_->folk.size(); ++i) {
            if (tables_->folk[i].number == sim::kChaosGoblin) return int(i);
        }
        return -1;
    }
    int stash() const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && stores(one)) ++n;
        }
        return n;
    }
    // Whether this trip should call on the vault: eight jewels and runes, or any once the bag is
    // full -- one rule for the errand and the trip, which disagreed in Devias (six in a full bag
    // started a trip every minute that the vault never answered).
    bool wantsVault() const {
        return vaultHere() >= 0 && (stash() >= 8 || (freeCells() < 6 && stash() > 0));
    }
    // The vault keeper in this town (Baz, NPC 240), or -1.
    int vaultHere() const {
        for (size_t i = 0; i < tables_->folk.size(); ++i) {
            if (tables_->folk[i].number == 240) return int(i);
        }
        return -1;
    }

    // What he keeps at this slot: `keeps`, less the ammunition his weapon does not shoot and a
    // second of a pet (the user, 2026-10-03, "continue improving bots"): an elf gone over to a
    // crossbow carried six quivers of arrows, three Guardian Angels, three Imps and two Horns of
    // Uniria, and her bag had no room for a quiver of bolts -- 148 trips for arrows she never
    // bought in eight hours.
    // Whether he is within thirty points of strength and agility of wearing a carried piece of
    // his class he cannot wear yet: kept through a sale, and his points go there first (spend).
    bool nearlyFits(const sim::Held& one) const {
        if (one.empty()) return false;
        const content::ItemRow& row = rowOf(one);
        if (sim::placeOf(row) < 0 || sim::ammunition(row)) return false;
        if (row.classes != 0 && (row.classes & (1 << int(options_.kin))) == 0) return false;
        if (sim::fits(*tables_, realm_->wearer(), one)) return false;
        const sim::Body& me = realm_->hero();
        const auto a = sim::asks(row, one.refinement, one.excellent != 0);
        const int str = std::max(0, int(a.strength) - me.points.strength);
        const int agi = std::max(0, int(a.agility) - me.points.agility);
        return a.level <= me.level && a.energy <= me.points.energy && str + agi > 0 && str + agi <= 30;
    }
    bool keepsAt(int slot) const {
        const sim::Held& one = realm_->satchel()[slot];
        if (slot >= sim::kWorn && nearlyFits(one)) return true;
        if (slot >= sim::kWorn && unruneWorth(one)) return true;
        if (one.empty() || !keeps(one)) return false;
        const content::ItemRow& row = rowOf(one);
        if (sim::ammunition(row)) return archer() && feeds(row);
        if (row.group == sim::kGroupPets) {
            for (int other = 0; other < slot; ++other) {
                const sim::Held& was = realm_->satchel()[other];
                if (!was.empty() && was.item == one.item) return false;
            }
        }
        return true;
    }
    // What he keeps through a sale: jewels, runes, potions, ammunition and pets.
    bool keeps(const sim::Held& one) const {
        const content::ItemRow& row = rowOf(one);
        return sim::refiningJewel(row) || sim::creation(row) || sim::heals(row) ||
               sim::restores(row) || sim::ammunition(row) || row.group == sim::kGroupPets;
    }

    // ---- what a fight costs ---------------------------------------------------------------
    // His band and guard against the breed's, 0.75's hit chance (1 - defence rate / attack
    // rate, floored at 3%): the health a kill of it costs him.
    static double hitChance(double attackRate, double defenseRate) {
        if (attackRate <= 0.0) return 0.03;
        return std::clamp(1.0 - defenseRate / attackRate, 0.03, 1.0);
    }
    // **His real attack, not his bare hand** (the user, 2026-10-03: "lets continue to improve
    // bots"): the blow of the strongest primary he knows and may throw -- Energy Ball or Fire
    // Ball, Twisting Slash, Skillshot -- and the ticks it takes, as `press` reckons a skill. The
    // swing alone said a Larva cost a wizard more than half his health, and he never went down
    // into the Dungeon.
    void attack(double* hit, int32_t* ticks) const {
        const sim::Body& hero = realm_->hero();
        const sim::Wearer w = realm_->wearer();
        *hit = blow();
        *ticks = std::max(1, hero.swingTicks);
        const auto arm = [&](int32_t at) -> const content::Arm* {
            return at >= 0 && size_t(at) < tables_->arms.size() ? &tables_->arms[size_t(at)] : nullptr;
        };
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            if (!row.primary() || !realm_->knows(row.number) || !row.suits(w.hand)) continue;
            const double base = row.wizardry
                                    ? ((w.wizardMinimum + w.wizardMaximum) / 2.0 + row.damage * 1.25) * w.wizardryRate
                                    : blow();
            const double one = base * sim::force(row, hero.points);
            const int32_t cast = sim::castTicks(*tables_, hero.kin, hero.points.agility, arm(hero.weapon),
                                                arm(hero.shield), row);
            const int32_t t = std::max<int32_t>(1, row.wizardry ? cast : std::max(cast, hero.swingTicks));
            if (one / t > *hit / *ticks) {
                *hit = one;
                *ticks = t;
            }
        }
    }
    double costOf(const content::MonsterKind& kind) const {
        const sim::Body& hero = realm_->hero();
        double hit = 0.0;
        int32_t ticks = 1;
        attack(&hit, &ticks);
        const double landed = std::max(1.0, hit - kind.defense) *
                              hitChance(hero.stats.attackRate, float(kind.defenseRate));
        const double seconds = kind.health / landed * ticks / 20.0;
        // And what his guard takes off a blow, as he keeps it up (guardSeen_): Defense, Soul
        // Barrier, Greater Defense.
        const double taken =
            std::max(0.0, (kind.minimumDamage + kind.maximumDamage) / 2.0 - hero.stats.defense) *
            hitChance(float(kind.attackRate), hero.stats.defenseRate) * guardSeen_;
        return caution_ * taken * seconds * 20.0 / std::max(1, kind.attackTicks);
    }
    // Whether he takes this breed on: a kill costs him under a third of his health (a half to
    // keep at a quest already under way, or while he can pay for the potions: riskShare), and it
    // has not killed him twice lately.
    bool takes(const content::MonsterKind& kind, double share = 0.0) const {
        if (share <= 0.0) share = riskShare();
        const auto fear = fearUntil_.find(kind.number);
        if (fear != fearUntil_.end() && fear->second > clock_) return false;
        return costOf(kind) * share <= realm_->hero().maxHealth;
    }
    // **What share of his health a kill may cost** (the user, 2026-10-03: "its wierd that bots
    // are not ending with good gears", then "push to harder maps"): a third, and for the wizard
    // a half while his purse holds twenty potions of his tier -- he drinks his way down, as a
    // player does. Three seeds of 8 h: the wizard's gear went from drop level 26-30 to 31-39 with
    // it; the knight, who drinks through melee, lost 6-10 levels and gained nothing, and the elf
    // was the same either way, so theirs stays a third. Off the purse alone, so the choice does
    // not flicker as he drinks his bag down.
    double riskShare() const {
        const bool rich = realm_->money() >= 20 * potionPrice(healTier());
        return options_.kin == sim::Kin::DarkWizard && rich ? 2.0 : 3.0;
    }
    // The strongest breed level on a map he takes, or -1.
    int bestOn(const content::Tables& tables) const {
        int best = -1;
        for (const content::MonsterNest& nest : tables.nests) {
            const content::MonsterKind& kind = tables.kinds[nest.kind];
            if (takes(kind) && kind.level > best) best = kind.level;
        }
        return best;
    }

    // ---- the aim --------------------------------------------------------------------------
    // The Clear steps of a quest still short of their goal, as breed numbers.
    std::vector<int> wanted(int q) const {
        std::vector<int> out;
        const sim::QuestRow& row = sim::questAt(q);
        const sim::QuestProgress& p = realm_->quest(q);
        for (int s = 0; s < row.stepCount; ++s) {
            if (row.steps[s].kind != sim::QuestStepKind::Clear) continue;
            if (p.counts[s] < row.steps[s].count) out.push_back(row.steps[s].target);
        }
        return out;
    }
    // Where a breed lives, among the worlds: the first map with a nest of it, or -1.
    int homeOf(int breed) {
        for (const WorldRow& w : kWorlds) {
            const content::Tables* t = world(w.map);
            if (!t) continue;
            for (const content::MonsterNest& nest : t->nests) {
                if (t->kinds[nest.kind].number == breed) return w.map;
            }
        }
        return -1;
    }
    const content::MonsterKind* kindOf(int map, int breed) {
        const content::Tables* t = world(map);
        if (!t) return nullptr;
        for (const content::MonsterKind& k : t->kinds) {
            if (k.number == breed) return &k;
        }
        return nullptr;
    }
    // Which map a giver stands on, or -1.
    int giverMap(int q) {
        for (const WorldRow& w : kWorlds) {
            const content::Tables* t = world(w.map);
            if (!t) continue;
            for (const content::Townsperson& f : t->folk) {
                if (f.number == sim::questAt(q).giver) return w.map;
            }
        }
        return -1;
    }
    // The quest's breeds he can take now, on the map they live on: the map, or -1 for none.
    int huntable(int q, std::vector<int>* breeds, double share = 3.0) {
        breeds->clear();
        if (aside_[q] > clock_) return -1;
        int where = -1;
        for (const int breed : wanted(q)) {
            const int home = homeOf(breed);
            const content::MonsterKind* kind = home >= 0 ? kindOf(home, breed) : nullptr;
            if (!kind || !takes(*kind, share) || !reachable(home)) continue;
            if (where < 0) where = home;
            if (home == where) breeds->push_back(breed);
        }
        return where;
    }

    // Where his class is born, as the realm has it (realm_travel.cpp homeMap): an elf in Noria,
    // the others in Lorencia.
    int homeMap() const { return options_.kin == sim::Kin::FairyElf ? 3 : 0; }
    // A town's own quest: the first in the table whose giver stands there and waits on nothing --
    // Marlon's in Lorencia, Peia's in Noria (the Golden Archer stands in Lorencia too, later).
    int townQuest(int map) {
        for (int q = 0; q < sim::kQuests; ++q) {
            if (sim::questAt(q).afterAny == 0 && giverMap(q) == map) return q;
        }
        return -1;
    }
    bool handedIn(int q) const { return q >= 0 && realm_->quest(q).completions > 0; }
    // The user's order (2026-10-01): his own town's quest, then the other town's, and only once
    // both are handed in anything in Devias or the Dungeon -- quests and the grind both.
    bool townsDone() {
        return handedIn(townQuest(homeMap())) && handedIn(townQuest(homeMap() == 0 ? 3 : 0));
    }
    bool allowed(int q) {
        const int home = townQuest(homeMap()), other = townQuest(homeMap() == 0 ? 3 : 0);
        if (q == home) return true;
        if (q == other) return handedIn(home);
        return townsDone();
    }
    // The quests in the order he takes them: his town's, the other town's, then the table's.
    std::vector<int> questOrder() {
        const int home = townQuest(homeMap()), other = townQuest(homeMap() == 0 ? 3 : 0);
        std::vector<int> out;
        for (const int q : {home, other}) if (q >= 0) out.push_back(q);
        for (int q = 0; q < sim::kQuests; ++q) {
            if (q != home && q != other) out.push_back(q);
        }
        return out;
    }

    void choose() {
        const Aim was = aim_;
        const int wasQuest = aimQuest_;
        aim_ = Aim::Grind;
        aimQuest_ = -1;
        quarry_.clear();
        if (options_.quests) {
            // The quest he is hunting for keeps him while it still can, on a looser test: a
            // breed on the line between two would have him paying to cross the map both ways.
            if (was == Aim::Hunt && wasQuest >= 0 && allowed(wasQuest) &&
                realm_->quest(wasQuest).state == sim::QuestState::Active) {
                std::vector<int> breeds;
                const int where = huntable(wasQuest, &breeds, 2.0);
                if (where >= 0) {
                    aim_ = Aim::Hunt;
                    aimQuest_ = wasQuest;
                    aimMap_ = where;
                    quarry_ = breeds;
                }
            }
            // Hand in first, then hunt what is under way, then take what is offered.
            const std::vector<int> order = questOrder();
            for (const int q : order) {
                if (aim_ == Aim::HandIn) break;
                if (realm_->quest(q).state == sim::QuestState::Ready && reachable(giverMap(q))) {
                    aim_ = Aim::HandIn;
                    aimQuest_ = q;
                    aimMap_ = giverMap(q);
                }
            }
            for (const int q : order) {
                if (aim_ != Aim::Grind) break;
                if (!allowed(q) || realm_->quest(q).state != sim::QuestState::Active) continue;
                std::vector<int> breeds;
                const int where = huntable(q, &breeds);
                if (where < 0) continue;
                aim_ = Aim::Hunt;
                aimQuest_ = q;
                aimMap_ = where;
                quarry_ = breeds;
            }
            for (const int q : order) {
                if (aim_ != Aim::Grind) break;
                if (!allowed(q) || !realm_->questOffered(q)) continue;
                const int at = giverMap(q);
                std::vector<int> breeds;
                if (at < 0 || !reachable(at) || huntable(q, &breeds) < 0) continue;
                aim_ = Aim::Accept;
                aimQuest_ = q;
                aimMap_ = at;
            }
        }
        if (aim_ == Aim::Grind) {
            // The map with the strongest breed he takes, where he is on a tie.
            aimMap_ = map();
            int best = shunned_[map()] > clock_ ? -1 : bestOn(*tables_);
            // Lorencia and Noria only, until both their quests are in.
            const bool open = !options_.quests || townsDone();
            if (!open && map() != 0 && map() != 3) {
                aimMap_ = homeMap();
                best = -1;
            }
            for (const WorldRow& w : kWorlds) {
                const content::Tables* t = world(w.map);
                if (!t || !reachable(w.map) || (!open && w.map != 0 && w.map != 3)) continue;
                if (shunned_[w.map] > clock_) continue;
                const int b = bestOn(*t);
                if (b > best) {
                    best = b;
                    aimMap_ = w.map;
                }
            }
        }
        if (aim_ != lastAim_ || aimQuest_ != lastQuest_ || aimMap_ != lastMap_) {
            const char* names[] = {"grinds", "hands in", "hunts for", "takes"};
            if (aimQuest_ >= 0) {
                say("%s %s (%s)", names[int(aim_)], sim::questAt(aimQuest_).title, worldOf(aimMap_)->name);
            } else {
                say("grinds in %s", worldOf(aimMap_)->name);
            }
            lastAim_ = aim_;
            lastQuest_ = aimQuest_;
            lastMap_ = aimMap_;
        }
    }

    // ---- hunting --------------------------------------------------------------------------
    bool barred(uint32_t id) const {
        const auto it = banned_.find(id);
        return it != banned_.end() && it->second > clock_;
    }

    void ask(sim::Request request) {
        if (request.kind == sim::Request::Kind::Attack || request.kind == sim::Request::Kind::Pick) {
            if (request.target != chasing_) {
                chasing_ = request.target;
                chasedSince_ = clock_;
            } else if (clock_ - chasedSince_ > kGiveUp) {
                banned_[chasing_] = clock_ + kForget;
                chasing_ = 0;
                return;
            }
        }
        if (options_.kin == sim::Kin::DarkWizard && request.kind == sim::Request::Kind::Attack) {
            request.skill = sim::skill::kEnergyBall;
        }
        realm_->ask(request);
    }

    // Raises his guard when it has lapsed and something is on him: the knight's Defense or the
    // wizard's Soul Barrier, each drawn up behind a shield (SkillRow::suits off the left hand).
    //
    // And every class's own self-casts besides (the user, 2026-10-03: "teach DK to use shield and
    // defense skill", "same with DW and elf"): a might -- the elf's Greater Damage -- when it has
    // lapsed, a mend -- her Heal -- under half his health, and her summon when none of hers
    // stands. Each only when it is ready, paid for and his hand may throw it.
    bool guard() {
        const sim::Body& hero = realm_->hero();
        const sim::Wearer w = realm_->wearer();
        const int64_t now = realm_->tick();
        bool summoned = false;
        for (const sim::Body& b : realm_->bodies()) {
            if (b.summoner == hero.id && b.alive()) summoned = true;
        }
        int summon = -1;
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            if (!row.onSelf() || !realm_->knows(row.number) || realm_->cooling(row.number) > 0) continue;
            // Tried in the last five seconds: a cast the realm refused starts no cooldown, and
            // asked again every think it would hold him out of the fight. On the bot's own
            // clock: a map change raises a new realm whose tick starts again at nought, and a
            // realm tick kept here barred every self-cast for the rest of the run.
            if (guardTried_[row.number] > clock_) continue;
            if (hero.mana < row.mana || !row.suits(w.offHand)) continue;
            const bool wanted = (row.boonTicks > 0 && hero.boonUntil <= now) ||
                                (row.mightTicks > 0 && hero.mightUntil <= now) ||
                                (row.mends && hero.health * 2 < hero.maxHealth);
            if (wanted) {
                guardTried_[row.number] = clock_ + 100;
                realm_->invoke(row.number, hero.id);
                return true;
            }
            // The last summon in the table she knows: the later breeds are the stronger.
            if (row.summons > 0 && !summoned) summon = i;
        }
        if (summon >= 0) {
            guardTried_[sim::skillAt(summon).number] = clock_ + 100;
            realm_->invoke(sim::skillAt(summon).number, hero.id);
            return true;
        }
        return false;
    }
    std::map<int32_t, int64_t> guardTried_;

    // Presses the strongest skill that is ready on the body he fights: a blow's force times
    // what it rolls on (a spell adds its own damage to the band), the guards and buffs aside.
    void press(uint32_t at) {
        const sim::Body& hero = realm_->hero();
        const sim::Wearer w = realm_->wearer();
        int best = -1;
        double strongest = 0.0;
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            if (!realm_->knows(row.number) || realm_->cooling(row.number) > 0) continue;
            if (hero.mana < row.mana || row.onSelf() || !row.suits(w.hand)) continue;
            const double base = row.wizardry
                                    ? ((w.wizardMinimum + w.wizardMaximum) / 2.0 + row.damage * 1.25) * w.wizardryRate
                                    : blow();
            const double hit = base * sim::force(row, hero.points);
            if (hit > strongest) {
                strongest = hit;
                best = i;
            }
        }
        if (best < 0) return;
        realm_->invoke(sim::skillAt(best).number, at);
        pressed_ = best;
    }

    // Whether he needs the town: potions out, the bag full, the gear worn down, or money enough
    // that a shelf may hold something better.
    const char* needsTown() const {
        if (clock_ < noTripUntil_) return nullptr;
        const int64_t money = realm_->money();
        const int64_t bundle = potionPrice(healTier());
        if (countOf(sim::heals) < 3 && money >= bundle) return "out of potions";
        // The wizard's alone: a knight sent home for mana potions too (2026-10-03) ended fifteen
        // levels lower in eight hours -- the trips cost more than Uppercut and Lunge gave back.
        if (options_.kin == sim::Kin::DarkWizard && countOf(sim::restores) < 3 && money >= bundle) {
            return "out of mana potions";
        }
        // Only with something to sell or store: a bag full of what he keeps -- jewels, runes,
        // potions, quivers -- sent an elf back to town every thirty-six seconds for three hours.
        if (freeCells() < 6 && (sellable() > 0 || wantsVault())) return "bag full";
        if (wornDown() >= 0 && !realm_->selfMending()) return "gear worn down";
        if (archer() && ammo() < 30 && money >= 70) return "out of arrows";
        if (clock_ - lastTrip_ > 30 * 60 * 20 && money >= 5000 && const_cast<Bot*>(this)->shopWorth()) {
            return "something on a shelf";
        }
        return nullptr;
    }

    // The first worn piece down to a quarter, or -1.
    int wornDown() const {
        for (int slot = 0; slot < sim::kWorn; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty() || sim::ammunition(rowOf(one))) continue;
            const int full = sim::maximumDurability(rowOf(one), one);
            if (full > 0 && one.durability * 4 < full) return slot;
        }
        return -1;
    }
    // Mends what is worn down by his own hand where he stands, once he may (selfMending).
    void mend() {
        if (!realm_->selfMending()) return;
        for (int slot = wornDown(); slot >= 0; slot = wornDown()) {
            const int64_t cost = realm_->repairCost(slot);
            if (!realm_->repair(slot)) return;
            say("mends his %s for %lld zen", rowOf(realm_->satchel()[slot]).label.c_str(), (long long)cost);
        }
    }

    // An elf's: the hand her bow or crossbow leaves for its ammunition (a bow is held left and
    // its arrows right, a crossbow right and its bolts left -- placeOf), or -1 with neither.
    int ammoHand() const {
        for (const int slot : {int(sim::kWeaponRight), int(sim::kWeaponLeft)}) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && rowOf(one).group == sim::kGroupBows && !sim::ammunition(rowOf(one))) {
                return slot == sim::kWeaponRight ? sim::kWeaponLeft : sim::kWeaponRight;
            }
        }
        return -1;
    }
    bool archer() const { return ammoHand() >= 0; }
    // Whether a quiver is the one her weapon shoots: arrows (15) for a bow, numbers 0-6, and
    // bolts (7) for a crossbow -- sim's `together`, MuMain's CheckArrow. Since 2026-10-02 every
    // quiver goes in the left hand, so the hand no longer tells them apart, and the bot bought
    // bolts for a Short Bow and stood three hours with nothing to shoot.
    bool feeds(const content::ItemRow& quiver) const {
        if (!sim::ammunition(quiver) || sim::placeOf(quiver) != ammoHand()) return false;
        for (const int slot : {int(sim::kWeaponRight), int(sim::kWeaponLeft)}) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            const content::ItemRow& bow = rowOf(one);
            if (bow.group != sim::kGroupBows || sim::ammunition(bow)) continue;
            return quiver.number == (bow.number <= 6 ? 15 : 7);
        }
        return false;
    }
    // The shots she has for that hand, in it and in the bag.
    int ammo() const {
        const int hand = ammoHand();
        int n = 0;
        for (int slot = 0; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && hand >= 0 && feeds(rowOf(one))) n += one.durability;
        }
        return n;
    }

    // Whether a body is one he would go after now: a quest's breed while hunting for one, else
    // whatever he takes.
    bool quarry(const sim::Body& body, bool anyFloor = false) const {
        if (!body.monster() || !body.alive() || barred(body.id) || body.kind < 0) return false;
        // On a map of floors, only what stands on his.
        const int floor = realm_->travelFloor();
        if (!anyFloor && floor >= 0 && realm_->floorAt(body.column(), body.row()) != floor) return false;
        const content::MonsterKind& kind = tables_->kinds[size_t(body.kind)];
        if (aim_ == Aim::Hunt && aimMap_ == map()) {
            return std::find(quarry_.begin(), quarry_.end(), kind.number) != quarry_.end();
        }
        // **Grinding, nothing far under the best he takes here**, so a floor of weaklings sends
        // him down (nextFloor) -- what is on him he answers all the same. The map was chosen by
        // its strongest breed he takes, but he fought whatever stood on the floor he landed on:
        // a level-175 wizard killed 1,481 of the Dungeon's first-floor Skeleton Warriors in 8 h
        // and wore Pad and Bone, as nothing there drops better (the user, 2026-10-03: "its
        // wierd that bots are not ending with good gears").
        if (aim_ == Aim::Grind && body.quarry != realm_->hero().id) {
            const int best = bestHere();
            if (best >= 0 && kind.level < best - kGrindBand) return false;
        }
        return takes(kind);
    }
    static constexpr int kGrindBand = 12;
    // bestOn for the map he stands on, once a second: quarry asks it of every body every tick.
    int bestHere() const {
        if (bestHereAt_ != clock_ / 20 || bestHereMap_ != map()) {
            bestHereAt_ = clock_ / 20;
            bestHereMap_ = map();
            bestHere_ = bestOn(*tables_);
        }
        return bestHere_;
    }
    mutable int64_t bestHereAt_ = -1;
    mutable int bestHereMap_ = -1;
    mutable int bestHere_ = -1;

    void hunt() {
        const sim::Body& hero = realm_->hero();
        // Low, and nothing to drink: to a safe tile to sit it out.
        if (hero.health * 10 < hero.maxHealth * 3 && countOf(sim::heals) == 0) {
            say("retreats at %d/%d health, no potions", hero.health, hero.maxHealth);
            mode_ = Mode::Rest;
            return;
        }
        sim::Request request;
        // Whatever is on him first.
        float closest = 6.0f * 6.0f;
        bool engaged = false;
        for (const sim::Body& body : realm_->bodies()) {
            if (!body.monster() || !body.alive() || body.quarry != hero.id || barred(body.id)) continue;
            const float dx = body.x - hero.x, dy = body.y - hero.y;
            if (dx * dx + dy * dy < closest) {
                closest = dx * dx + dy * dy;
                request.kind = sim::Request::Kind::Attack;
                request.target = body.id;
                engaged = true;
            }
        }
        // Town, if it is time: when nothing is on him, or after a minute of never being free
        // while he is still well -- a crowded nest never lets him go otherwise.
        if (!engaged) freeSince_ = clock_;
        const bool pressed = clock_ - freeSince_ > 60 * 20 && hero.health * 10 > hero.maxHealth * 7;
        if (!engaged || pressed) {
            if (const char* why = needsTown()) {
                say("goes to town: %s", why);
                startTrip();
                return;
            }
        }
        // A giver to see, or another map: there first, when nothing is on him.
        if (!engaged) {
            if ((aim_ == Aim::HandIn || aim_ == Aim::Accept) && aimMap_ == map()) {
                visitGiver();
                return;
            }
            if (aimMap_ != map() && goTo(aimMap_)) return;
        }
        // Then what fell, while the bag has room.
        if (!engaged) {
            closest = kLootReach * kLootReach;
            const int room = freeCells();
            for (const sim::Lying& one : realm_->lying()) {
                if (one.what.empty() || barred(one.id)) continue;
                const content::ItemRow& row = rowOf(one.what);
                if (row.width * row.height > room) continue;
                const float dx = float(one.column) - hero.x, dy = float(one.row) - hero.y;
                if (dx * dx + dy * dy < closest) {
                    closest = dx * dx + dy * dy;
                    request.kind = sim::Request::Kind::Pick;
                    request.target = one.id;
                }
            }
        }
        // Then the nearest he would go after, in sight, and then anywhere on the map: the
        // strongest such breed while grinding.
        if (request.kind == sim::Request::Kind::None) {
            const int cap = aim_ == Aim::Hunt ? 1 << 30 : bestOn(*tables_);
            for (const float reach : {kSight, 1e15f}) {
                closest = reach * reach;
                for (const sim::Body& body : realm_->bodies()) {
                    if (!quarry(body)) continue;
                    if (reach > kSight && aim_ != Aim::Hunt && body.level != cap) continue;
                    const float dx = body.x - hero.x, dy = body.y - hero.y;
                    if (dx * dx + dy * dy < closest) {
                        closest = dx * dx + dy * dy;
                        request.kind = sim::Request::Kind::Attack;
                        request.target = body.id;
                    }
                }
                if (request.kind != sim::Request::Kind::None) break;
            }
        }
        // Nothing he can reach here: on a map of floors, the next floor he may go to.
        if (request.kind == sim::Request::Kind::None) {
            nextFloor();
            return;
        }
        ask(request);
        // Guarded and buffed whenever he goes to fight, not only once something is on him: the
        // wizard's nine tiles kill most things before they close, and Defense was raised six
        // times in four hours.
        if ((engaged || request.kind == sim::Request::Kind::Attack) && guard()) return;
        if (request.kind == sim::Request::Kind::Attack) press(request.target);
    }

    // The Dungeon's floors are one map the router cannot cross: what lives on another is out of
    // reach until he pays to be put down there (this map's travel rows) or walks its stairs (an
    // enter gate whose exit is on this same map). Toward the floor of the nearest he would go
    // after, when his own has none; a quest whose breeds he cannot reach at all is set aside.
    void nextFloor() {
        const int here = realm_->travelFloor();
        const sim::Body& hero = realm_->hero();
        float closest = 1e30f;
        int to = -1;
        // Grinding, only the level he would grind here, as the hunt's own pick does: without it
        // a knight paid down to Dungeon 2 and back up to Dungeon 1 every thirty seconds for four
        // hours (seed 2, 2026-10-03), each floor's bodies the other's quarry and neither his.
        // Not the elf's: over three seeds she ended 15 levels lower with it (172-174 against
        // 187-190), the knight 30 lower without it (140-145 against 175-180).
        const int cap = (aim_ == Aim::Hunt || options_.kin == sim::Kin::FairyElf) ? -1 : bestOn(*tables_);
        for (const sim::Body& body : realm_->bodies()) {
            if (here < 0 || !quarry(body, true)) continue;
            if (cap >= 0 && body.level != cap) continue;
            const int floor = realm_->floorAt(body.column(), body.row());
            if (floor < 0 || floor == here) continue;
            const float dx = body.x - hero.x, dy = body.y - hero.y;
            if (dx * dx + dy * dy < closest) {
                closest = dx * dx + dy * dy;
                to = floor;
            }
        }
        // And never straight back to the floor he has just left.
        if (to >= 0 && to == floorLeft_ && clock_ < floorLeftAt_ + 5 * 60 * 20) to = -1;
        if (to < 0) {
            setAside();
            return;
        }
        const sim::TravelRow& row = sim::travelAt(to);
        if (realm_->travelRefusal(to) == sim::TravelRefusal::None && realm_->money() >= row.zen + potionReserve()) {
            if (clock_ >= nextFloorAt_ && realm_->travel(to)) {
                floorLeft_ = here;
                floorLeftAt_ = clock_;
                say("pays %lld zen down to %s", (long long)row.zen, row.name);
                banned_.clear();
                nextFloorAt_ = clock_ + 30 * 20;
            }
            return;
        }
        // The stairs: a gate on his floor to this same map, the one to that floor if there is
        // one, else to any other.
        int stair = -1;
        for (int n = 0; n < 512; ++n) {
            const sim::EnterGate* in = sim::enterGateNumbered(n);
            const sim::ExitGate* out = in && in->target >= 0 ? sim::exitGate(in->target) : nullptr;
            if (!out || int(in->map) != map() || int(out->map) != map() || in->level > hero.level) continue;
            if (realm_->floorAt((in->box.x1 + in->box.x2) / 2, (in->box.y1 + in->box.y2) / 2) != here) continue;
            const int lands = realm_->floorAt((out->box.x1 + out->box.x2) / 2, (out->box.y1 + out->box.y2) / 2);
            if (lands == here) continue;
            if (stair < 0 || lands == to) stair = n;
            if (lands == to) break;
        }
        if (stair < 0) {
            setAside();
            return;
        }
        if (!hero.walking) {
            const sim::EnterGate* in = sim::enterGateNumbered(stair);
            sim::Request request;
            request.kind = sim::Request::Kind::WalkTo;
            request.column = (in->box.x1 + in->box.x2) / 2;
            request.row = (in->box.y1 + in->box.y2) / 2;
            realm_->ask(request);
        }
    }

    void setAside() {
        if (aim_ != Aim::Hunt || aimQuest_ < 0) return;
        aside_[aimQuest_] = clock_ + 10 * 60 * 20;
        say("sets %s aside for 10 min: its breeds are out of reach", sim::questAt(aimQuest_).title);
        nextAim_ = clock_;
    }

    // The giver's dialog: walked to and opened by a Talk, and then the quest taken or handed in.
    void visitGiver() {
        const sim::QuestRow& row = sim::questAt(aimQuest_);
        int folk = -1;
        for (size_t i = 0; i < tables_->folk.size(); ++i) {
            if (tables_->folk[i].number == row.giver) folk = int(i);
        }
        if (folk < 0) return;
        if (realm_->questing() != folk) {
            sim::Request request;
            request.kind = sim::Request::Kind::Talk;
            request.target = uint32_t(folk);
            realm_->ask(request);
            return;
        }
        if (aim_ == Aim::Accept) {
            if (realm_->acceptQuest(aimQuest_)) say("takes %s from %s", row.title, row.giverName);
        } else {
            int choice = -1;
            for (int c = 0; c < row.choiceCount && choice < 0; ++c) {
                if (realm_->questChoiceFits(aimQuest_, c)) choice = c;
            }
            const int level = realm_->hero().level;
            const int64_t zen = realm_->money();
            if (realm_->completeQuest(aimQuest_, choice)) {
                ++out_.handedIn[aimQuest_];
                if (out_.firstHandIn[aimQuest_] < 0) out_.firstHandIn[aimQuest_] = clock_;
                say("** hands in %s to %s: +%lld zen, level %d -> %d%s%s", row.title, row.giverName,
                    (long long)(realm_->money() - zen), level, realm_->hero().level,
                    choice >= 0 ? ", chose " : "", choice >= 0 ? row.choices[choice].item : "");
                sortBag();
            } else {
                // Refused when it is ready: the room, which a bag of scattered cells can lack
                // with plenty free. To the counters to sell, and back.
                say("no room for %s's reward: to the counters", row.giverName);
                realm_->closeQuest();
                startTrip();
                return;
            }
        }
        realm_->closeQuest();
        nextAim_ = clock_;  // choose again at once
    }

    // Sits in the safe zone until he is nearly whole, then goes shopping if he must. A map with
    // no safe zone sends him home.
    void rest() {
        const sim::Body& hero = realm_->hero();
        if (!safe_) {
            restOwed_ = true;
            if (!goTo(0)) {
                restOwed_ = false;
                mode_ = Mode::Hunt;
            }
            return;
        }
        if (!tables_->grid.safe(hero.column(), hero.row())) {
            if (!hero.walking) {
                sim::Request request;
                request.kind = sim::Request::Kind::WalkTo;
                request.column = restAt_[0];
                request.row = restAt_[1];
                realm_->ask(request);
            }
            return;
        }
        if (hero.health * 10 >= hero.maxHealth * 9) {
            if (const char* why = needsTown()) {
                say("rested; to the counters: %s", why);
                startTrip();
            } else {
                mode_ = Mode::Hunt;
            }
        }
    }

    // ---- town -----------------------------------------------------------------------------
    void startTrip(bool fresh = true) {
        errands_.clear();
        // Every counter in town; each after the first is asked again on the way whether it is
        // worth the walk (`town`), since the first one's sales are what pay for the rest.
        errands_ = sellers_;
        // **And the vault, when the jewels and runes he keeps fill eight cells** (the user,
        // 2026-10-03: "lets continue to improve bots"): Baz takes them in, as a player banks
        // what he keeps, so the bag has room for what falls.
        vaultOwed_ = wantsVault();
        machineOwed_ = false;
        if (goblinHere() >= 0) {
            for (int slot = sim::kWorn; slot < sim::kSlots && !machineOwed_; ++slot) {
                machineOwed_ = unruneWorth(realm_->satchel()[slot]);
            }
        }
        machineSince_ = clock_;
        vaultSince_ = clock_;
        mode_ = Mode::Town;
        tripSince_ = clock_;
        lastTrip_ = clock_;
        if (fresh) ++out_.trips;
    }

    // Whether a counter in this town mends (sim::repairsAt: Hanzo's, Eo's).
    bool smithHere() const {
        for (const int folk : sellers_) {
            if (sim::repairsAt(tables_->folk[size_t(folk)].number)) return true;
        }
        return false;
    }
    // Whether a counter in this town sells ammunition for her hand.
    bool ammoHere() const {
        const int hand = ammoHand();
        for (const int folk : sellers_) {
            int count = 0;
            const sim::Offer* shelf = sim::stockOf(tables_->folk[size_t(folk)].number, &count);
            for (int i = 0; i < count; ++i) {
                const int item = tables_->itemAt(shelf[i].group, shelf[i].number);
                if (item >= 0 && hand >= 0 && feeds(tables_->items[size_t(item)])) return true;
            }
        }
        return false;
    }

    void town() {
        // No counter on this map (the Dungeon), or none with the arrows she is out of: home to
        // Lorencia's, where Amy sells both quivers.
        if (sellers_.empty() || (map() != 0 && archer() && ammo() < 30 && !ammoHere()) ||
            (map() != 0 && wornDown() >= 0 && !realm_->selfMending() && !smithHere())) {
            tripOwed_ = true;
            if (!goTo(0)) {
                say("cannot get to Lorencia's counters; tries again in 10 min");
                tripOwed_ = false;
                noTripUntil_ = clock_ + 10 * 60 * 20;
                mode_ = Mode::Hunt;
            }
            return;
        }
        // **A better piece on another town's shelf** (the user, 2026-10-03: "improve DW bot that he
        // can better results"): at the start of a trip, when nothing here is worth his Zen and
        // another town he can reach has something, he goes there to shop -- the wizard sat in
        // Pad armour at level 119 with 200,000 Zen while Izabel in Devias sold Sphinx and Bone.
        // Not again for half an hour, so a piece that will not go on cannot bounce him.
        // The Chaos Goblin first, when a carried piece's runes are worth taking out: into the box
        // with a Chaos, Remove Rune, everything back out; the rune goes into his weapon at the
        // next sort of the bag (useJewels).
        if (machineOwed_) {
            const int goblin = goblinHere();
            if (goblin < 0 || clock_ - machineSince_ > 90 * 20) {
                machineOwed_ = false;
            } else if (realm_->mixing() == goblin) {
                unrune();
                realm_->closeMachine();
                machineOwed_ = false;
                tripSince_ = clock_;
                sortBag();
                return;
            } else {
                sim::Request request;
                request.kind = sim::Request::Kind::Talk;
                request.target = uint32_t(goblin);
                realm_->ask(request);
                return;
            }
        }
        // The vault first, when it is owed: deposit every jewel and rune, then the counters.
        if (vaultOwed_) {
            const int keeper = vaultHere();
            if (keeper < 0 || clock_ - vaultSince_ > 90 * 20) {
                vaultOwed_ = false;
            } else if (realm_->banking() == keeper) {
                int stored = 0;
                for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                    const sim::Held one = realm_->satchel()[slot];
                    if (!one.empty() && stores(one) && realm_->deposit(slot) >= 0) ++stored;
                }
                say("stores %d jewels and runes with %s", stored, tables_->folk[size_t(keeper)].name.c_str());
                realm_->closeVault();
                vaultOwed_ = false;
                tripSince_ = clock_;
                return;
            } else {
                sim::Request request;
                request.kind = sim::Request::Kind::Talk;
                request.target = uint32_t(keeper);
                realm_->ask(request);
                return;
            }
        }
        // On his way there: keep going -- a gate is a walk of many thinks, and the trip's own
        // errands here would take him off it.
        if (awayTo_ >= 0) {
            if (map() == awayTo_) {
                awayTo_ = -1;
            } else {
                if (goTo(awayTo_)) return;
                awayTo_ = -1;
            }
        }
        if (errands_.size() == sellers_.size() && clock_ >= awayShopUntil_ && !shopWorth()) {
            for (const int there : {0, 3, 2}) {
                if (there == map() || !reachable(there)) continue;
                const content::Tables* town = world(there);
                if (!town) continue;
                bool worth = false;
                for (const content::Townsperson& f : town->folk) worth = worth || shelfWorth(*town, f.number);
                if (!worth) continue;
                awayShopUntil_ = clock_ + 30 * 60 * 20;
                say("goes to %s's counters: something there is worth his Zen", worldOf(there)->name);
                tripOwed_ = true;
                if (goTo(there)) {
                    awayTo_ = there;
                    return;
                }
                tripOwed_ = false;
            }
        }
        if (errands_.empty()) {
            mode_ = Mode::Hunt;
            return;
        }
        const int folk = errands_.front();
        // Past the first counter: only one with something worth trying, or the potions.
        if (errands_.size() < sellers_.size() && realm_->trading() != folk && !potionShelf(folk) &&
            !shelfWorth(*tables_, tables_->folk[size_t(folk)].number) && !(archer() && ammo() < 500)) {
            errands_.erase(errands_.begin());
            return;
        }
        if (realm_->trading() == folk) {
            serve(folk);
            realm_->closeTrade();
            errands_.erase(errands_.begin());
            tripSince_ = clock_;
            return;
        }
        if (clock_ - tripSince_ > 90 * 20) {  // could not reach him: the next one
            say("could not reach %s", tables_->folk[size_t(folk)].name.c_str());
            errands_.erase(errands_.begin());
            tripSince_ = clock_;
            return;
        }
        sim::Request request;
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(folk);
        realm_->ask(request);
    }

    // What the potions he needs will cost, kept back from gear.
    int64_t potionReserve() const {
        const int64_t price = potionPrice(healTier());
        return price * 7 + (casts() ? price * 4 : 0);
    }
    // Small, medium or large: by how much a small one would mend of him.
    int healTier() const {
        const int maxHealth = realm_->hero().maxHealth;
        return maxHealth < 400 ? 0 : maxHealth < 1200 ? 1 : 2;
    }
    static int64_t potionPrice(int tier) { return tier == 0 ? 240 : tier == 1 ? 990 : 2200; }

    void serve(int folk) {
        const int npc = tables_->folk[size_t(folk)].number;
        const std::string& name = tables_->folk[size_t(folk)].name;
        // Sell what is not kept and not worn better.
        wearBest();
        int sold = 0;
        int64_t got = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty() || keepsAt(slot)) continue;
            const int64_t paid = realm_->sellItem(slot);
            if (paid >= 0) {
                ++sold;
                got += paid;
            }
        }
        if (sold) {
            out_.sold += sold;
            say("sells %d things to %s for %lld zen", sold, name.c_str(), (long long)got);
        }
        if (realm_->mending()) {
            const int64_t cost = realm_->repairAllCost();
            if (cost > 0 && realm_->repairAll() > 0) say("repairs for %lld zen", (long long)cost);
        }
        int count = 0;
        const sim::Offer* shelf = sim::stockOf(npc, &count);
        // An archer's quiver before anything: without it she cannot earn the rest.
        buyAmmo(shelf, count);
        buyGear(shelf, count);
        if (potionShelf(folk)) buyPotions(shelf, count);
    }

    int buyOne(const sim::Offer& offer, const char* why) {
        const int item = tables_->itemAt(offer.group, offer.number);
        if (item < 0) return -1;
        const int64_t before = realm_->money();
        const int slot = realm_->buy(offer.slot);
        if (slot >= 0) {
            ++out_.bought;
            if (why) say("buys %s for %lld zen%s", tables_->items[size_t(item)].label.c_str(),
                         (long long)(before - realm_->money()), why);
        }
        return slot;
    }

    void buyPotions(const sim::Offer* shelf, int count) {
        const int tier = healTier();
        const auto find = [&](bool (*kind)(const content::ItemRow&), int pieces, int want) -> const sim::Offer* {
            int seen = 0;
            for (int i = 0; i < count; ++i) {
                const int item = tables_->itemAt(shelf[i].group, shelf[i].number);
                if (item < 0 || !kind(tables_->items[size_t(item)]) || shelf[i].pieces != pieces) continue;
                if (tables_->items[size_t(item)].number == 0 && kind == sim::heals) continue;  // the apple
                if (seen++ == want) return &shelf[i];
            }
            return nullptr;
        };
        // Mana first for a wizard, whose every blow is a spell; health first for the others.
        int bought = 0, mana = 0;
        const auto heal = [&](int upTo) {
            if (const sim::Offer* offer = find(sim::heals, 3, tier)) {
                while (countOf(sim::heals) < upTo && realm_->money() >= potionPrice(tier) &&
                       buyOne(*offer, nullptr) >= 0) bought += 3;
            }
        };
        const auto restore = [&](int upTo) {
            if (!casts()) return;
            if (const sim::Offer* offer = find(sim::restores, 3, tier)) {
                while (countOf(sim::restores) < upTo && realm_->money() >= potionPrice(tier) &&
                       buyOne(*offer, nullptr) >= 0) mana += 3;
            }
        };
        // Twice the stock when the purse can stand forty bundles (2026-10-03, "continue
        // improving bots"): a wizard with 250,000 Zen bought twenty-four, drank them in nine
        // minutes in the Dungeon and paid two trips back to Lorencia for the next. Twenty a cell,
        // so it is three cells more.
        const int deep = realm_->money() > potionPrice(tier) * 40 ? 2 : 1;
        if (options_.kin == sim::Kin::DarkWizard) {
            heal(6 * deep);
            restore(30 * deep);
            heal(24 * deep);
        } else {
            heal(30 * deep);
            restore(30 * deep);
        }
        if (bought || mana) say("buys %d healing and %d mana potions (%lld zen left)", bought, mana,
                                (long long)realm_->money());
    }

    // An archer's quiver, from whichever counter has the one for her hand.
    void buyAmmo(const sim::Offer* shelf, int count) {
        const int hand = ammoHand();
        for (int i = 0; hand >= 0 && i < count; ++i) {
            const int item = tables_->itemAt(shelf[i].group, shelf[i].number);
            if (item < 0 || shelf[i].refinement) continue;
            const content::ItemRow& row = tables_->items[size_t(item)];
            if (!feeds(row)) continue;
            const int64_t price = sim::buyingPrice(row, 0, shelf[i].pieces, shelf[i].skill);
            // Fifteen hundred when she can spare it: Skillshot looses three a cast, and five
            // hundred sent her home every three minutes (126 trips in eight hours, 2026-10-03).
            const int quiver = realm_->money() > 20000 ? 1500 : 500;
            while (ammo() < quiver && realm_->money() >= price) {
                const int slot = buyOne(shelf[i], " (ammunition)");
                if (slot < 0) break;
                if (realm_->satchel()[hand].empty()) realm_->moveItem(slot, hand);
            }
        }
    }

    // Gear and orbs from a shelf: an orb he can read, then any piece that makes him better,
    // within what is left over the potions he will need. A purchase that does not go on or
    // cannot be read is bought back at once.
    // Whether an offer is worth trying: an orb his class can read now, or a piece he can wear
    // that promises more than what he wears in its place, within the Zen over his potions.
    bool worthTrying(const content::Tables& tables, const sim::Offer& offer) const {
        const int item = tables.itemAt(offer.group, offer.number);
        if (item < 0) return false;
        const content::ItemRow& row = tables.items[size_t(item)];
        // Tried on lately and handed back: not again for a while.
        const auto tried = triedOn_.find(row.name);
        if (tried != triedOn_.end() && tried->second > clock_) return false;
        const bool orb = row.teaches != 0;
        if (orb && options_.noShopSkills) return false;
        if (!orb && (sim::placeOf(row) < 0 || sim::ammunition(row))) return false;
        if (orb && realm_->knows(row.teaches)) return false;
        const sim::Held held{item, int16_t(offer.refinement), int16_t(1)};
        // `fits` is for what is worn; an orb is asked its class here and the rest by useItem,
        // which says no to what he cannot read yet.
        if (orb) {
            if (row.classes != 0 && (row.classes & (1 << int(options_.kin))) == 0) return false;
            // A knight's skill is thrown with a family of weapons: one his hand cannot throw is
            // not worth the Zen yet (a guard is read off the shield, and is his either way).
            const sim::SkillRow* teaches = sim::skillNumbered(row.teaches);
            if (teaches && !teaches->onSelf() && !teaches->suits(realm_->wearer().hand)) return false;
            if (realm_->hero().level < row.teachesLevel || realm_->hero().points.energy < row.teachesEnergy) {
                return false;
            }
        } else if (!sim::fits(tables, realm_->wearer(), held)) {
            return false;
        }
        const int64_t price = sim::buyingPrice(row, offer.refinement, offer.pieces, offer.skill);
        if (price > realm_->money() - potionReserve()) return false;
        return orb || promising(row, offer.refinement);
    }
    bool shelfWorth(const content::Tables& tables, int npc) const {
        int count = 0;
        const sim::Offer* shelf = sim::stockOf(npc, &count);
        for (int i = 0; i < count; ++i) {
            if (worthTrying(tables, shelf[i])) return true;
        }
        return false;
    }
    // Whether any shelf in this town, or Lorencia's from a map with none, holds such a thing.
    bool shopWorth() {
        const content::Tables* town = sellers_.empty() ? world(0) : tables_;
        if (!town) return false;
        for (const content::Townsperson& f : town->folk) {
            if (shelfWorth(*town, f.number)) return true;
        }
        return false;
    }

    void buyGear(const sim::Offer* shelf, int count) {
        // Orbs and scrolls first, then gear: a skill his weapon throws outlasts any piece.
        for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < count; ++i) {
            const int at = tables_->itemAt(shelf[i].group, shelf[i].number);
            if (at < 0 || (tables_->items[size_t(at)].teaches != 0) != (pass == 0)) continue;
            if (!worthTrying(*tables_, shelf[i])) continue;
            const int item = tables_->itemAt(shelf[i].group, shelf[i].number);
            const content::ItemRow& row = tables_->items[size_t(item)];
            const bool orb = row.teaches != 0;
            const int64_t price = sim::buyingPrice(row, shelf[i].refinement, shelf[i].pieces, shelf[i].skill);
            const int slot = buyOne(shelf[i], nullptr);
            if (slot < 0) continue;
            if (orb) {
                if (realm_->useItem(slot)) {
                    say("buys %s for %lld zen and learns it", row.label.c_str(), (long long)price);
                } else {
                    realm_->buyBack();
                    --out_.bought;
                    triedOn_[row.name] = clock_ + 2 * 3600 * 20;
                }
                continue;
            }
            if (wearBest() && realm_->satchel()[slot].item != item) {
                say("  (bought %s for %lld zen)", row.label.c_str(), (long long)price);
            } else {
                realm_->buyBack();
                --out_.bought;
                triedOn_[row.name] = clock_ + 2 * 3600 * 20;
            }
        }
    }

    // Worth buying to try on: a weapon with a bigger band, or armour with more defence, than
    // what he wears in its place.
    bool promising(const content::ItemRow& row, int plus) const {
        const int place = sim::placeOf(row);
        const sim::Held& worn = realm_->satchel()[place];
        if (options_.kin == sim::Kin::DarkWizard && row.weapon()) {
            return worn.empty() || row.magicPower > rowOf(worn).magicPower;
        }
        if (worn.empty()) return true;
        const content::ItemRow& has = rowOf(worn);
        // A shield against a second weapon in that hand is not a like-for-like trade.
        if (row.shield() != has.shield()) return false;
        if (row.weapon()) {
            // A two-handed one takes both hands: against what both hold.
            int held = has.minimumDamage + has.maximumDamage + 2 * sim::damageBonus(worn.refinement);
            const sim::Held& left = realm_->satchel()[sim::kWeaponLeft];
            if (row.twoHanded() && place == sim::kWeaponRight && !left.empty() && rowOf(left).weapon()) {
                held += rowOf(left).minimumDamage + rowOf(left).maximumDamage + 2 * sim::damageBonus(left.refinement);
            }
            return row.minimumDamage + row.maximumDamage + 2 * sim::damageBonus(plus) > held;
        }
        return row.defense + sim::defenseBonus(row.shield(), plus) >
               has.defense + sim::defenseBonus(has.shield(), worn.refinement);
    }

    Options options_;
    uint64_t seed_;
    std::map<int, std::unique_ptr<content::Tables>> worlds_;
    const content::Tables* tables_ = nullptr;
    std::unique_ptr<sim::Realm> realm_;
    int64_t clock_ = 0;
    Outcome out_;
    Mode mode_ = Mode::Hunt;
    Aim aim_ = Aim::Grind, lastAim_ = Aim::Grind;
    int aimQuest_ = -1, aimMap_ = 0, lastQuest_ = -2, lastMap_ = -1;
    std::vector<int> quarry_;
    int64_t aside_[sim::kQuests] = {};
    int owedMap_ = -1, owedColumn_ = 0, owedRow_ = 0, walkingTo_ = -1;
    std::vector<int> errands_, sellers_;
    bool safe_ = true, tripOwed_ = false, restOwed_ = false;
    int64_t awayShopUntil_ = 0;
    int awayTo_ = -1;
    int floorLeft_ = -1;       // the floor he last paid to leave, and when
    int64_t floorLeftAt_ = 0;
    bool vaultOwed_ = false;   // this trip calls on the vault keeper first
    bool machineOwed_ = false;  // and on the Chaos Goblin, to take runes out
    int64_t machineSince_ = 0;
    int64_t vaultSince_ = 0;
    double guardSeen_ = 1.0;  // the share of a blow his guard lets through, once he has one  // the town he is on his way to shop in, -1 for none  // the next trip that may go to another town's counters
    int restAt_[2] = {0, 0};
    int64_t tripSince_ = 0, lastTrip_ = 0, nextSort_ = 0, freeSince_ = 0, nextAim_ = 0, nextFloorAt_ = 0, noTripUntil_ = 0;
    std::unordered_map<uint32_t, int64_t> banned_;
    std::map<int, int64_t> fearUntil_;  // by breed number
    std::map<int, int> deathsTo_;
    std::map<std::string, int> killsOf_;
    std::map<std::string, int> castsOf_;
    std::map<std::string, int64_t> triedOn_;  // an item's name -> when it may be tried again
    uint32_t chasing_ = 0;
    int64_t chasedSince_ = 0;
    int pressed_ = 0, lastLevel_ = 1, drunkAt_ = 0, healed_ = 0;
    double caution_ = 1.0;
    std::map<int, int64_t> shunned_;  // a grinding ground left until this tick (learnCaution)
    int64_t cautionAt_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--kin") {
            const std::string k = next();
            options.kin = k == "dw" ? sim::Kin::DarkWizard : k == "elf" ? sim::Kin::FairyElf : sim::Kin::DarkKnight;
        } else if (a == "--seed") options.seed = std::strtoull(next(), nullptr, 10);
        else if (a == "--runs") options.runs = std::max(1, std::atoi(next()));
        else if (a == "--hours") options.hours = std::atof(next());
        else if (a == "--until-jewel") options.untilJewel = true;
        else if (a == "--no-quests") options.quests = false;
        else if (a == "--fights") options.fights = true;
        else if (a == "--no-shop-skills") options.noShopSkills = true;
        else if (a == "--build") {
            std::sscanf(next(), "%d,%d,%d,%d", &options.build[0], &options.build[1], &options.build[2],
                        &options.build[3]);
        }
        else if (a == "--quiet") options.quiet = true;
        else {
            std::printf("usage: bot [--kin dk|dw|elf] [--seed N] [--runs N] [--hours H] "
                        "[--until-jewel] [--no-quests] [--fights] [--build s,a,v,e] [--no-shop-skills] [--quiet]\n");
            return 2;
        }
    }
    if (!std::getenv("BOT_LOG")) core::logSilence(true);

    std::vector<Outcome> outcomes;
    for (int run = 0; run < options.runs; ++run) {
        const uint64_t seed = options.seed + uint64_t(run);
        std::printf("%s, seed %llu\n", kinName(options.kin), (unsigned long long)seed);
        Bot bot(options, seed);
        if (!bot.start()) {
            std::printf("bot: the realm did not raise\n");
            return 1;
        }
        const int64_t cap = int64_t(options.hours * 3600.0 * 20.0);
        while (bot.now() < cap && !(options.untilJewel && bot.foundJewel())) bot.tick();
        const Outcome o = bot.finish();
        std::printf("  after %s: level %d, %d kills, %d deaths, %lld zen, %d trips to town, "
                    "%d bought, %d sold, %d potions drunk, %d jewels, %d runes, %d map changes\n",
                    clock(o.ticks).c_str(), o.level, o.kills, o.deaths, (long long)o.zen, o.trips,
                    o.bought, o.sold, o.drunk, o.jewels, o.runes, o.maps);
        if (o.firstJewelTick >= 0) {
            std::printf("  first jewel: %s at %s, kill %d, level %d\n", o.firstJewel.c_str(),
                        clock(o.firstJewelTick).c_str(), o.firstJewelKills, o.firstJewelLevel);
        } else {
            std::printf("  no jewel\n");
        }
        for (int q = 0; q < sim::kQuests; ++q) {
            if (o.firstHandIn[q] >= 0) {
                std::printf("  first hand-in: %s at %s\n", sim::questAt(q).title,
                            clock(o.firstHandIn[q]).c_str());
            }
        }
        bot.printKills();
        outcomes.push_back(o);
    }

    if (outcomes.size() > 1) {
        std::vector<double> minutes, kills;
        int none = 0;
        for (const Outcome& o : outcomes) {
            if (o.firstJewelTick < 0) {
                ++none;
                continue;
            }
            minutes.push_back(double(o.firstJewelTick) / 1200.0);
            kills.push_back(o.firstJewelKills);
        }
        std::sort(minutes.begin(), minutes.end());
        std::sort(kills.begin(), kills.end());
        std::printf("\n%zu runs: first jewel in %zu", outcomes.size(), minutes.size());
        if (!minutes.empty()) {
            std::printf(" -- median %.0f min (%.0f-%.0f), median %.0f kills",
                        minutes[minutes.size() / 2], minutes.front(), minutes.back(),
                        kills[kills.size() / 2]);
        }
        std::printf("; none in %d\n", none);
        for (int q = 0; q < sim::kQuests; ++q) {
            std::vector<double> at;
            for (const Outcome& o : outcomes) {
                if (o.firstHandIn[q] >= 0) at.push_back(double(o.firstHandIn[q]) / 1200.0);
            }
            if (at.empty()) continue;
            std::sort(at.begin(), at.end());
            std::printf("%s: first handed in by %zu of %zu, median %.0f min (%.0f-%.0f)\n",
                        sim::questAt(q).title, at.size(), outcomes.size(), at[at.size() / 2],
                        at.front(), at.back());
        }
    }
    return 0;
}
