#pragma once

// The bot's brain (tools/bot/bot.cpp, tools/netbot/netbot.cpp): how a character plays from level
// 1 -- hunting, potions, loot, gear, the town's counters, the quests and the road between maps.
// bot.cpp plays it on a realm of its own for hours in seconds; netbot plays it on the server,
// where every ask goes through its Hand as a command.

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <type_traits>

#include "content/tables.h"
#include "core/log.h"
#include "sim/gates.h"
#include "sim/items.h"
#include "sim/market.h"
#include "sim/quests.h"
#include "sim/random.h"
#include "sim/swings.h"
#include "sim/realm.h"
#include "sim/skills.h"
#include "sim/travel.h"
#include "sim/wear.h"
#include "sim/command.h"
#include "sim/event.h"
#include "sim/machine.h"
#include "sim/maps.h"

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
    // The deeper maps (the user, 2026-10-08: "keep pushing DW" -- a wizard of level 150 still
    // ground the Dungeon and Devias, the only grounds he knew): Atlans, Tarkan, and Icarus for
    // one who can fly (canFly), each where sim/maps.cpp puts a newcomer.
    {7, "atlans", {21, 17}},
    {8, "tarkan", {195, 65}},
    {10, "icarus", {15, 13}},
    // Blood Castle: reached only by the Messenger's door (sim/event.h), never chosen to grind.
    {11, "bloodcastle", {13, 8}},
    // Devil Square: reached only by Charon's door (sim/square.h), never chosen to grind.
    {9, "devilsquare", {137, 95}},
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
    // --pet angel|imp|none: the pet he keeps in slot 8, or -1 for his class's (wantedPet).
    int pet = -1;
    // --drink-at N: the tenths of his health a wizard drinks under (65% unless asked).
    double drinkAt = 6.5;
    // --path magic: the Magic Gladiator on the wizard's ways -- energy, a staff, spells and the
    // wizard's potions -- where melee, the default, is the knight's. His quests pay that path.
    bool magic = false;
    // **The energy elf** (the user, 2026-10-09: "maybe create a elf which is energy elf which can
    // give best buffs and heals"): her points to energy -- Heal is 5 + energy/5, Greater Damage
    // 3 + energy/7 -- and enough agility and vitality to stand beside a party. netbot's `eelf`.
    bool support = false;
    // Said before every line: the netbot's name for him, so two bots' lines tell apart.
    std::string tag;
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
        case sim::Kin::MagicGladiator: return "Magic Gladiator";
    }
    return "?";
}

const char* cradleWeapon(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return "Staff01";
        case sim::Kin::FairyElf: return "Bow01";
        case sim::Kin::DarkKnight: return "Axe01";
        case sim::Kin::MagicGladiator: return "Sword02";  // the lobby's (roster.cpp)
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
    // Strong ground (the user, 2026-10-10: "improve DW brains so he can farm 50+ and get to his
    // first wings"): kills of breeds from kLifeFromLevel, the Jewels of Life that fell, and when
    // his wings were made.
    int strongKills = 0, lives = 0, castles = 0, castleWins = 0;
    int squares = 0, squareBestRank = 0;  // Devil Square runs, and his best rank in one
    int64_t squarePoints = 0;             // over all of them
    int64_t wingsTick = -1, firstCastleTick = -1;
    int handedIn[sim::kQuests] = {};
    int64_t firstHandIn[sim::kQuests];
    Outcome() { std::fill(std::begin(firstHandIn), std::end(firstHandIn), int64_t(-1)); }
};

// The bot's hand: every ask he makes of the realm, made on it at once -- what the brain reads
// next is its answer -- and, on the server (tools/netbot), written down as the command a
// client's click becomes (sim/command.h), to be sent once the decision is made. There the
// realm is a scratch copy of the mirror, so the server decides what really happens, and the next
// decision starts again from the world as it is.
class Hand {
public:
    sim::Realm* realm = nullptr;
    std::vector<sim::Command>* record = nullptr;
    // The last ticket written: every command but an order or a cast asks for an answer.
    uint32_t ticket = 0;

    void ask(const sim::Request& r) {
        realm->ask(r);
        note(sim::Command::Kind::Order, int(r.kind), r.column, r.row, r.skill, r.target);
    }
    void invoke(int32_t skill, uint32_t at) {
        realm->invoke(skill, at);
        note(sim::Command::Kind::Cast, skill, -1, -1, -1, at);
    }
    bool spend(int s, int a, int v, int e) {
        const bool ok = realm->spend(s, a, v, e);
        if (ok) {
            const int each[4] = {s, a, v, e};
            for (int k = 0; k < 4; ++k)
                for (int n = 0; n < each[k]; ++n) note(sim::Command::Kind::Spend, k);
        }
        return ok;
    }
    bool useItem(int slot) { return was(realm->useItem(slot), sim::Command::Kind::Use, slot); }
    bool moveItem(int from, int to) {
        if (std::getenv("BOT_MOVES") && (from == sim::kWeaponRight || to == sim::kWeaponRight)) {
            std::printf("MOVE %d -> %d (%s)\n", from, to, std::getenv("BOT_MOVES"));
        }
        return was(realm->moveItem(from, to), sim::Command::Kind::Move, from, to);
    }
    int buy(int shelf) { return was(realm->buy(shelf), sim::Command::Kind::Buy, shelf); }
    int64_t sellItem(int slot) { return was(realm->sellItem(slot), sim::Command::Kind::Sell, slot); }
    uint32_t discard(int slot) { return was(realm->discard(slot), sim::Command::Kind::Discard, slot, 0); }
    int buyBack() { return was(realm->buyBack(), sim::Command::Kind::BuyBack); }
    bool repair(int slot) { return was(realm->repair(slot), sim::Command::Kind::Repair, slot); }
    int repairAll() { return was(realm->repairAll(), sim::Command::Kind::RepairAll); }
    int deposit(int slot, int cell = -1) { return was(realm->deposit(slot, cell), sim::Command::Kind::Deposit, slot, cell); }
    // A Firecracker or a box thrown and opened (Realm::cracks).
    // Opened as the server's Discard command opens it (Realm::crack), not merely dropped.
    bool crack(int slot) {
        if (!realm->cracks(slot)) return false;
        const bool opened = realm->crack(slot).opened;
        if (opened) note(sim::Command::Kind::Discard, slot);
        return opened;
    }
    int withdraw(int cell, int slot = -1) { return was(realm->withdraw(cell, slot), sim::Command::Kind::Withdraw, cell, slot); }
    int putIn(int slot, int cell = -1) { return was(realm->putIn(slot, cell), sim::Command::Kind::PutIn, slot, cell); }
    int takeOut(int cell, int slot = -1) { return was(realm->takeOut(cell, slot), sim::Command::Kind::TakeOut, cell, slot); }
    bool mix(sim::Service service, int socket = -1) {
        const bool ok = realm->mix(service, socket);
        if (record) {
            note(sim::Command::Kind::Mix, socket);
            record->back().service = service;
        }
        return ok;
    }
    bool refine(int jewel, int thing) { return was(realm->refine(jewel, thing), sim::Command::Kind::Refine, jewel, thing); }
    bool acceptQuest(int q) { return was(realm->acceptQuest(q), sim::Command::Kind::AcceptQuest, q); }
    bool completeQuest(int q, int choice, sim::QuestPath path) {
        return was(realm->completeQuest(q, choice, path), sim::Command::Kind::CompleteQuest, q, choice, int(path));
    }
    bool travel(int row) { return was(realm->travel(row), sim::Command::Kind::Travel, row); }
    bool enterCastle(int castle) { return was(realm->enterCastle(castle), sim::Command::Kind::EnterCastle, castle); }
    bool handInStaff() { return was(realm->handInStaff(), sim::Command::Kind::HandInStaff); }
    bool claimCastle() { return was(realm->claimCastle(), sim::Command::Kind::ClaimCastle); }
    bool enterSquare(int square) { return was(realm->enterSquare(square), sim::Command::Kind::EnterSquare, square); }
    bool claimSquare() { return was(realm->claimSquare(), sim::Command::Kind::ClaimSquare); }
    void closeTrade() { realm->closeTrade(); close(sim::Command::Window::Trade); }
    void closeVault() { realm->closeVault(); close(sim::Command::Window::Vault); }
    void closeMachine() { realm->closeMachine(); close(sim::Command::Window::Machine); }
    void closeQuest() { realm->closeQuest(); close(sim::Command::Window::Quest); }

private:
    // Written whatever the scratch answered: the server's realm may answer otherwise, and a
    // refusal there costs nothing. Except what the scratch refused, which would be refused again.
    template <typename T>
    T was(T answer, sim::Command::Kind kind, int a = -1, int b = -1, int c = -1) {
        bool ok;
        if constexpr (std::is_same_v<T, bool>) ok = answer;
        else ok = answer >= 0;
        if (ok) note(kind, a, b, c);
        return answer;
    }
    void close(sim::Command::Window window) { note(sim::Command::Kind::Close, int(window)); }
    void note(sim::Command::Kind kind, int a = -1, int b = -1, int c = -1, int d = -1, uint32_t target = 0) {
        if (!record) return;
        sim::Command one;
        one.kind = kind;
        one.player = realm->hero().id;
        one.a = a;
        one.b = b;
        one.c = c;
        one.d = d;
        one.target = target;
        if (kind != sim::Command::Kind::Order && kind != sim::Command::Kind::Cast) one.ticket = ++ticket;
        record->push_back(one);
    }
};

class Bot {
public:
    Bot(const Options& options, uint64_t seed) : options_(options), seed_(seed) {}

    bool start() {
        const WorldRow& home = *worldOf(homeMap());
        tables_ = world(home.map);
        if (!tables_) return false;
        own_ = std::make_unique<sim::Realm>();
        realm_ = own_.get();
        hand_.realm = realm_;
        if (!realm_->raise(tables_, seed_, home.arrive[0], home.arrive[1], options_.kin, 1)) return false;
        const int32_t weapon = tables_->armNamed(cradleWeapon(options_.kin));
        if (!realm_->equip(weapon, -1)) realm_->equip(weapon, -1, true);
        settle();
        return true;
    }

    int64_t now() const { return clock_; }
    bool foundJewel() const { return out_.firstJewelTick >= 0; }

    // ---- on the server (tools/netbot) -----------------------------------------------------
    // Into a world the server raised, as the mirror stands: what hangs off it set again.
    void attach(sim::Realm& mirror, const content::Tables* tables) {
        tables_ = tables;
        look(mirror);
        settle();
    }
    // What the mirror's last tick said, and his clock on.
    void heardOn(sim::Realm& mirror) {
        look(mirror);
        heard();
        owedMap_ = -1;  // a map change is the server's: the line follows it (netbot)
        ++clock_;
    }
    // A decision, made on `scratch` -- the mirror's copy, which it may change as it likes -- and
    // what it asked written into `out`, in order.
    void thinkOn(sim::Realm& scratch, std::vector<sim::Command>& out) {
        realm_ = &scratch;
        hand_.realm = &scratch;
        hand_.record = &out;
        play();
        hand_.record = nullptr;
        owedMap_ = -1;
    }
    void look(sim::Realm& mirror) {
        realm_ = &mirror;
        hand_.realm = nullptr;
    }
    uint32_t lastTicket() const { return hand_.ticket; }
    int64_t clockNow() const { return clock_; }
    int mapNow() const { return map(); }
    // **Ordered** (tools/netbot, --claude): the map he grinds on whatever his own choice, or -1
    // for his own again. A map he cannot reach yet is not taken.
    bool order(int map) {
        if (map >= 0 && !reachable(map)) return false;
        ordered_ = map;
        nextAim_ = 0;
        return true;
    }
    int ordered() const { return ordered_; }

    // ---- the party (docs/party.md; tools/netbot's board) ------------------------------------
    // The user, 2026-10-09: "lets implement party system, so bots can form 5-man party when they
    // can kill some quest monsters, elfs can heal, dk can tank, and 2 dps", and "we need to be
    // smart about how they will fill quests, because some will need more kill some already will
    // be finished some kills". The board (netbot) forms the party and says what it hunts; the
    // brain plays his part in it: the tank holds what the party fights, the damage dealers strike
    // what the tank holds, the elf heals and buffs them, and all of them keep together.
    enum class Role : uint8_t { None, Tank, Damage, Healer };
    struct Lead {
        int map = -1;             // where the party hunts; -1, no party to play in
        int quest = -1;           // his own quest it hunts for there, or -1 (he is its support)
        std::vector<int> breeds;  // what it hunts there: every breed a member still wants
        Role role = Role::None;
        uint32_t leader = 0;      // bodies on his map, 0 when elsewhere (or himself)
        uint32_t tank = 0;
        int size = 1;             // members, all maps
        bool healer = false;      // an elf among them
        int castle = 0;           // the Blood Castle they go into together, 0 for their own
        int square = 0;           // and the Devil Square, 0 for their own
    };
    void lead(Lead l) {
        if (l.map != lead_.map || l.quest != lead_.quest || l.breeds != lead_.breeds || l.castle != lead_.castle ||
            l.square != lead_.square) {
            nextAim_ = 0;
        }
        lead_ = std::move(l);
    }
    const Lead& led() const { return lead_; }
    static Role roleOf(sim::Kin kin, bool tanks) {
        if (tanks) return Role::Tank;
        return kin == sim::Kin::FairyElf ? Role::Healer : Role::Damage;
    }
    // What his quests still want, breed by breed: a quest under way, or one offered him he may
    // take; `alone` whether he takes that breed by himself, `together` whether he would with a
    // party about him (kTogether of a kill's cost to him).
    struct Need {
        int quest = -1, breed = -1, map = -1, left = 0, level = 0;
        bool taken = false, alone = false, together = false;
        bool town = false;  // a town's own quest, Marlon's or Peia's
        std::string label;  // the breed's name
    };
    std::vector<Need> needs() {
        std::vector<Need> out;
        if (!options_.quests) return out;
        const double was = partyScale_;
        for (int q = 0; q < sim::kQuests; ++q) {
            if (!allowed(q) || aside_[q] > clock_) continue;
            const sim::QuestState state = realm_->quest(q).state;
            const bool taken = state == sim::QuestState::Active;
            if (!taken) {
                const int at = giverMap(q);
                if (!realm_->questOffered(q) || at < 0 || !reachable(at)) continue;
            }
            const sim::QuestRow& row = sim::questAt(q);
            for (int s = 0; s < row.stepCount; ++s) {
                const sim::QuestStepRow& step = row.steps[s];
                if (step.kind != sim::QuestStepKind::Clear) continue;
                const int left = step.count - (taken ? realm_->quest(q).counts[s] : 0);
                if (left <= 0) continue;
                const int home = homeOf(step.target);
                const content::MonsterKind* kind = home >= 0 ? kindOf(home, step.target) : nullptr;
                if (!kind || !reachable(home)) continue;
                Need one;
                one.quest = q;
                one.breed = step.target;
                one.map = home;
                one.left = left;
                one.level = kind->level;
                one.taken = taken;
                one.town = row.afterAny == 0;
                one.label = kind->label;
                partyScale_ = 1.0;
                one.alone = soloTakes(q, *kind, home);
                partyScale_ = kTogether;
                one.together = one.alone || soloTakes(q, *kind, home);
                out.push_back(one);
            }
        }
        partyScale_ = was;
        return out;
    }
    // Whether he can stand beside a party on this breed: he reaches its map and, with the party
    // about him, a kill of it is one he would take.
    bool helps(int map, int breed) {
        const content::MonsterKind* kind = reachable(map) ? kindOf(map, breed) : nullptr;
        if (!kind) return false;
        const double was = partyScale_;
        partyScale_ = kTogether;
        const bool ok = takes(*kind, 1.0) || drinksThrough(*kind);
        partyScale_ = was;
        return ok;
    }
    // **Farming for wings together** (the user, 2026-10-10: "can party get to the icarus?", "and
    // get wings?"): no bot made wings in 28 game hours, party or solo -- the Jewel of Life falls
    // only from breeds of level 50 and up, and alone they kill what is easy. A bot without wings
    // from kWingFarmFrom wants them; the board makes a party of such bots and leads it to the
    // strongest breed they can stand together -- Icarus's, when enough of them fly.
    static constexpr int kWingFarmFrom = 100;
    bool wantsWings() const { return !hasWings() && realm_->hero().level >= kWingFarmFrom; }
    // The breeds he could stand beside a party, on every map he reaches: map, breed, its level.
    // Kept ten game minutes, as the board asks it each second.
    struct Ground {
        int map = -1, breed = -1, level = 0;
        std::string label;
    };
    const std::vector<Ground>& grounds() {
        if (groundsAt_ >= 0 && clock_ - groundsAt_ < 10 * 60 * 20) return grounds_;
        groundsAt_ = clock_;
        grounds_.clear();
        for (const WorldRow& w : kWorlds) {
            if (w.map == int(sim::kBloodCastleMap) || w.map == int(sim::kDevilSquareMap)) continue;
            const content::Tables* t = world(w.map);
            if (!t || !reachable(w.map)) continue;
            std::set<int> seen;
            for (const content::MonsterNest& nest : t->nests) {
                const content::MonsterKind& kind = t->kinds[nest.kind];
                if (!seen.insert(kind.number).second) continue;
                if (helps(w.map, kind.number)) grounds_.push_back({w.map, kind.number, kind.level, kind.label});
            }
        }
        return grounds_;
    }
    // **Blood Castle together** (the user, 2026-10-09: "and maybe go to bc together?"): the
    // castles he could go into, a bit each (1 << c) -- its cloak held and its band's floor
    // reached -- and whether it is his time at the Messenger now.
    int castlesOpen() const {
        int out = 0;
        for (int c = 1; c <= sim::kCastlesBuilt; ++c) {
            if (realm_->cloakSlot(c) >= 0 && realm_->hero().level >= sim::kCastleBands[c - 1][0]) out |= 1 << c;
        }
        return out;
    }
    bool castleTime() const { return castleDue(); }
    bool inCastle() const { return map() == int(sim::kBloodCastleMap); }
    // **Devil Square together**, the same way (docs/devil-square-port.md C 4.6): the squares he
    // could go into (his invitation and his band, a bit each), whether Charon's gate is due, and
    // whether he is in one.
    int squaresOpen() const {
        int out = 0;
        for (int s = 1; s <= sim::kSquaresBuilt; ++s) {
            if (realm_->invitationSlot(s) >= 0 && inSquareBand(s)) out |= 1 << s;
        }
        return out;
    }
    bool squareTime() const { return squareGateSoon(); }
    bool inSquare() const { return map() == int(sim::kDevilSquareMap); }
    // What he is after, in a line: "hunts for The Broken Sword (devias)".
    std::string aimLine() const {
        const char* names[] = {"grinds", "hands in", "hunts for", "takes", "goes to", "goes to"};
        const char* modes[] = {"hunting", "resting", "in town"};
        const WorldRow* at = worldOf(aimMap_);
        std::string out = names[int(aim_)];
        if (aim_ == Aim::Castle) out += " Blood Castle";
        else if (aim_ == Aim::Square) out += " Devil Square";
        else if (aimQuest_ >= 0) out += std::string(" ") + sim::questAt(aimQuest_).title;
        out += std::string(" in ") + (at ? at->name : "?") + ", " + modes[int(mode_)];
        return out;
    }
    int kills() const { return out_.kills; }
    int deaths() const { return out_.deaths; }
    // One line on how he stands, for the netbot's minute report.
    void status() {
        const sim::Body& hero = realm_->hero();
        const char* modes[] = {"hunting", "resting", "in town"};
        say("level %d, %d/%d hp, %lld zen, %d kills, %d deaths, %d potions -- %s in %s at %d,%d", hero.level,
            hero.health, hero.maxHealth, (long long)realm_->money(), out_.kills, out_.deaths, countOf(sim::heals),
            modes[int(mode_)], worldOf(map()) ? worldOf(map())->name : "?", hero.column(), hero.row());
        // The user, 2026-10-09: "also check if elf is buffing and healing party memebers, also
        // check if DW is using magic shield".
        std::string aids;
        for (const auto& [name, n] : aidsOf_) aids += (aids.empty() ? "" : ", ") + name + " " + std::to_string(n);
        if (!aids.empty()) say("  on her party: %s", aids.c_str());
        if (const auto shield = castsOf_.find("Magic Shield"); shield != castsOf_.end()) {
            say("  Magic Shield %d times", shield->second);
        }
    }

    // One tick: a decision when one is due, the realm's step, what came of it, and a map change
    // when one is owed.
    void tick() {
        // The share of a blow his guard lets through, the least seen while one was up.
        if (realm_->hero().boonUntil > realm_->tick()) {
            guardSeen_ = std::min(guardSeen_, double(realm_->hero().stats.damageTaken));
        }
        if (clock_ % kThink == 0) play();
        if (std::getenv("BOT_LADDER") && clock_ % (3600 * 20) == 0) ladderTrace();
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
        // What killed him, and where (the plan of 2026-10-10: "name what kills him").
        {
            std::vector<std::pair<int, std::string>> deaths;
            for (const auto& [name, n] : deathsOf_) deaths.push_back({n, name});
            std::sort(deaths.rbegin(), deaths.rend());
            std::printf("\n  deaths:");
            for (const auto& [n, name] : deaths) std::printf(" %s %d,", name.c_str(), n);
        }
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
        std::printf("\n  strong ground: %d kills of level %d+ (%.0f%%), %d Jewels of Life, ", out_.strongKills,
                    sim::kLifeFromLevel, 100.0 * out_.strongKills / std::max(1, out_.kills), out_.lives);
        if (out_.wingsTick >= 0) std::printf("wings at %s", clock(out_.wingsTick).c_str());
        else std::printf("no wings");
        std::printf("; Blood Castle %d runs, %d won", out_.castles, out_.castleWins);
        if (out_.firstCastleTick >= 0) std::printf(", the first at %s", clock(out_.firstCastleTick).c_str());
        if (out_.squares > 0) {
            std::printf("; Devil Square %d runs, best rank %d, %lld points", out_.squares, out_.squareBestRank,
                        (long long)out_.squarePoints);
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
    enum class Aim { Grind, HandIn, Hunt, Accept, Castle, Square };

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
        // Into Blood Castle: which one his cloak opened, before anything else, as the server sets it.
        if (map == int(sim::kBloodCastleMap)) realm->setCastle(owedCastle_);
        // Into Devil Square: which one, and its fight on the gate's hour, as the server sets it.
        if (map == int(sim::kDevilSquareMap)) realm->setSquare(owedSquare_, owedSquareWait_);
        // And his vault and the Goblin's box, the account's as a server keeps them (sim::Kept):
        // the record alone lost every jewel he banked at the next gate.
        sim::Kept kept;
        kept.hero = record;
        kept.vault = realm_->vault();
        kept.machine = realm_->machine();
        realm->restoreKept(kept);
        own_ = std::move(realm);
        realm_ = own_.get();
        hand_.realm = realm_;
        tables_ = next;
        settle();
        say("-> %s", worldOf(map)->name);
    }

    // What hangs off a realm, set again after each one: the clock, the merchants, the targets.
    void settle() {
        if (own_) realm_->setWallClock(kEpoch + clock_ / 20);
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
        // **The map travel first, then the gates** (the user, 2026-10-08: "bots has to use map
        // travel with M, fast travel to noria then go to" Atlans): with no trip to the map
        // itself, the trip to the map whose gates reach it in the fewest hops, when that is
        // fewer than walking from here -- a knight walked Devias, Lorencia, Noria and Atlans's
        // gates, nine minutes, where the trip to Noria and its one gate would do.
        int hopsHere = -1;
        const int gateHere = firstGateTo(map(), to, &hopsHere);
        int bestTrip = -1, bestHops = hopsHere < 0 ? 1 << 20 : hopsHere;
        for (int i = 0; i < sim::kTravels; ++i) {
            const sim::TravelRow& row = sim::travelAt(i);
            if (row.map == map() || !worldOf(row.map)) continue;
            if (realm_->travelRefusal(i) != sim::TravelRefusal::None || realm_->money() < row.zen + 500) continue;
            int hops = -1;
            firstGateTo(row.map, to, &hops);
            if (hops >= 0 && hops < bestHops) {
                bestHops = hops;
                bestTrip = i;
            }
        }
        if (bestTrip >= 0) {
            *travel = bestTrip;
            return true;
        }
        if (gateHere < 0) return false;
        *gate = gateHere;
        return true;
    }
    // Breadth first over the gates from `from` to `to`, each a hop he has the level for: the gate
    // on `from` that starts the way, or -1 (none, or `to` is `from`); `hops` the gates walked.
    int firstGateTo(int from, int to, int* hops) const {
        *hops = from == to ? 0 : -1;
        std::map<int, std::pair<int, int>> first;  // map -> (the gate on `from` starting the way, hops)
        std::vector<int> frontier{from};
        first[from] = {-1, 0};
        for (size_t at = 0; at < frontier.size(); ++at) {
            const int here = frontier[at];
            for (int n = 0; n < 512; ++n) {
                const sim::EnterGate* in = sim::enterGateNumbered(n);
                if (!in || int(in->map) != here || in->target < 0) continue;
                if (in->level > realm_->hero().level) continue;
                const sim::ExitGate* out = sim::exitGate(in->target);
                if (!out || !worldOf(int(out->map)) || first.count(int(out->map))) continue;
                first[int(out->map)] = {here == from ? n : first[here].first, first[here].second + 1};
                frontier.push_back(int(out->map));
            }
        }
        const auto it = first.find(to);
        if (it == first.end() || it->second.first < 0) return -1;
        *hops = it->second.second;
        return it->second.first;
    }

    bool reachable(int to) const {
        // Icarus's gate refuses a hero without wings or a Dinorant (sim::canFly): never routed to.
        if (to == 10 && !sim::canFly(*tables_, realm_->satchel())) return false;
        if (to == map()) return true;
        // Kept for the tick, on the map he stands on: the party's board asks it many times a
        // second, and his way off a map changes only with his level, purse or map.
        if (reachTick_ != clock_ || reachMap_ != map()) {
            reachTick_ = clock_;
            reachMap_ = map();
            reach_.clear();
        }
        if (const auto known = reach_.find(to); known != reach_.end()) return known->second;
        int gate, travel;
        return reach_[to] = way(to, &gate, &travel);
    }
    mutable int64_t reachTick_ = -1;
    mutable int reachMap_ = -1;
    mutable std::map<int, bool> reach_;

    // A step toward another map: pays for the trip, or walks into the gate. True while he is
    // on his way.
    bool goTo(int to) {
        if (to == map()) return false;
        int gate, travel;
        if (!way(to, &gate, &travel)) return false;
        if (travel >= 0) {
            const sim::TravelRow& row = sim::travelAt(travel);
            if (hand_.travel(travel)) {
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
            hand_.ask(request);
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
                        if (dead && dead->kind >= 0) {
                            const content::MonsterKind& kind = tables_->kinds[size_t(dead->kind)];
                            ++killsOf_[kind.label];
                            if (kind.level >= sim::kLifeFromLevel) ++out_.strongKills;
                        }
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
                            if (h.a == sim::kCastleEnterGate) owedCastle_ = realm_->castlePassed();
                            if (sim::squareEnterGateNumbered(h.a)) {
                                owedSquare_ = realm_->squarePassed();
                                owedSquareWait_ = realm_->squarePassedWait();
                            }
                            owedMap_ = int(out->map);
                            owedColumn_ = h.b;
                            owedRow_ = h.c;
                        }
                    }
                    break;
                case sim::What::Rose:
                    // Risen on a map with no safe zone: owed its town -- Lorencia, or Devias from Blood
                    // Castle (sim::MapRow::home) -- as the mode takes him.
                    if (h.who == me && h.c == 1) homeOwed();
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
        // Blood Castle's run over and its rest out, or his win claimed: to Devias.
        if (map() == int(sim::kBloodCastleMap) && realm_->castleRun().sentOut && owedMap_ < 0) homeOwed();
        // Devil Square's: paid (Claim) or the rest out, to Noria -- the realm's warp home, which a
        // bot carries out as the castle's.
        if (map() == int(sim::kDevilSquareMap) && (realm_->squarePaid() || realm_->squareRun().sentOut) &&
            owedMap_ < 0) {
            homeOwed();
        }
        const int level = realm_->hero().level;
        if (level > lastLevel_) {
            if (level / 10 > lastLevel_ / 10 || level <= 5) {
                say("level %d (%d kills, %lld zen)", level, out_.kills, (long long)realm_->money());
            }
            lastLevel_ = level;
        }
    }

    void homeOwed() {
        const sim::MapRow* here = sim::mapNumbered(map());
        const sim::MapRow* town = sim::mapNumbered(here ? here->home : 0);
        if (!town) return;
        owedMap_ = town->number;
        owedColumn_ = town->arrive[0];
        owedRow_ = town->arrive[1];
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
        ++deathsOf_[kind.label + " (" + worldOf(map())->name + ")"];
        if (++deathsTo_[kind.number] >= 2) {
            fearUntil_[kind.number] = clock_ + kFear;
            deathsTo_[kind.number] = 0;
            say("killed by %s; leaves them for 15 min", kind.label.c_str());
        } else {
            say("killed by %s", kind.label.c_str());
        }
        // **And a map that kills him three times in fifteen minutes is left** (2026-10-10, the energy
        // elf): the fear above is a breed's, and what killed her was never what she hunted --
        // Noria's Song's Beetle Monsters cost her 94 of 145 health, the Stone Golems and Agons
        // that roam the same fields 1,681 and 5,644, and she died to them 70 times in six game
        // hours while the quest stayed huntable. Twenty minutes off the map: its grind shunned
        // (learnCaution's shunned_), and the hunt's breeds there set aside (setAside's asideOn_),
        // so she goes where the next quest or grind is and comes back stronger.
        // Three, not two: at two the agility elf, who dies to a Golem twice a day and shoots on,
        // left Noria for twenty minutes each time and ended a day 68 levels lower (seed 2). And
        // hers alone (Options::support): at three the agility elf still ended 191 against 260 on
        // that seed with three times the deaths, while the energy elf's deaths went 101/34 ->
        // 33/16 -- the rule is for one who cannot shoot her way out, and she is the only one.
        std::vector<int64_t>& deaths = deathsOn_[map()];
        deaths.push_back(clock_);
        while (!deaths.empty() && clock_ - deaths.front() >= kFear) deaths.erase(deaths.begin());
        if (options_.support && deaths.size() >= 3) {
            deaths.clear();
            shunned_[map()] = clock_ + kLeavesMap;
            if (aim_ == Aim::Hunt && aimQuest_ >= 0 && aimMap_ == map()) asideOn_[{aimQuest_, map()}] = clock_ + kLeavesMap;
            nextAim_ = clock_;
            say("killed three times in %s within 15 min: leaves it for 20 min", worldOf(map())->name);
        }
    }

    void dropped(const content::ItemRow& row) {
        if (!sim::refiningJewel(row) && !sim::creation(row)) return;
        sim::creation(row) ? ++out_.runes : ++out_.jewels;
        if (sim::jewelOf(row) == sim::Jewel::Life) ++out_.lives;
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
        std::printf("  [%s] %s", clock(clock_).c_str(), options_.tag.c_str());
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
        if (own_) realm_->setWallClock(kEpoch + clock_ / 20);
        const sim::Body& hero = realm_->hero();
        if (!hero.alive()) {
            mode_ = Mode::Hunt;
            errands_.clear();
            return;
        }
        partyScale_ = partyScale();
        spend();
        drink();
        mend();
        learnCaution();
        if (clock_ >= nextSort_) {
            sortBag();
            nextSort_ = clock_ + 100;
        }
        if (map() == int(sim::kBloodCastleMap)) {
            castle();
            return;
        }
        if (map() == int(sim::kDevilSquareMap)) {
            square();
            return;
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
    // The Magic Gladiator plays one of the two he stands between (--path): the wizard's rules on
    // the magic path, the knight's on the melee one.
    bool wizardly() const {
        return options_.kin == sim::Kin::DarkWizard || (options_.kin == sim::Kin::MagicGladiator && options_.magic);
    }
    bool knightly() const {
        return options_.kin == sim::Kin::DarkKnight || (options_.kin == sim::Kin::MagicGladiator && !options_.magic);
    }

    bool gladiatorMage() const { return options_.kin == sim::Kin::MagicGladiator && options_.magic; }

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
                hand_.spend(s1, a1, 0, 0);
                points -= s1 + a1;
            }
            if (points <= 0) return;
        }
        // First the strength his class's best armour at his level asks (Needs: 3 x drop level x
        // raw / 100 + 20), as a player keeps up with his set: a wizard of level 114 with all of it
        // in energy had 21 and could not wear even the Pad Armor, which asks 29.
        // **At the level that can fall for him, not his own** (the user, 2026-10-10: "improve DW
        // brains so he can farm 50+ and get to his first wings"): a thing falls only from a breed
        // of its drop level or over (Realm's drop roll), and every wizard of level 160-180 had
        // 221 strength -- the Dark Soul set's ask, a set that falls from level-90 breeds he never
        // fought -- and 218-260 health, wearing Sphinx and Bone of drop level 23-31. Two health
        // a point in vitality, that strength was his health twice over. So the armour whose
        // drop level a breed he takes, anywhere he can go, or a shelf he can walk to, can give him.
        // Four seeds of 24 h, all else the same: the wizard 153/164/170/161 -> 188/222/245/207,
        // his kills on level-50+ breeds 0% -> 2-29%, wings on one seed (none before); the knight
        // over 12 h 140/133/124/99 -> 148/121/130/161. Not the elf: her levels the same
        // (130/154/136/128 against 117/152/130/148) and her deaths 31 -> 69 -- her strength
        // is her blow (ClassRow), and the old ask was a better build for her than her own weights.
        // BOT_STRENGTH_OLD: the rule as it was, for an A/B.
        const sim::Body& hero = realm_->hero();
        const int findable = std::getenv("BOT_STRENGTH_OLD") || options_.kin == sim::Kin::FairyElf
                                 ? hero.level
                                 : findableLevel();
        int asked = 0;
        for (const content::ItemRow& row : tables_->items) {
            if (!row.armour() || row.dropLevel > findable) continue;
            if (row.classes != 0 && (row.classes & (1 << int(options_.kin))) == 0) continue;
            // The magic Magic Gladiator stops at the knight's Scale set, the rest to energy (the
            // user, 2026-10-07: 'fully to energy and just spent enought for scale set').
            if (gladiatorMage() && row.label.rfind("Scale ", 0) != 0) continue;
            asked = std::max(asked, sim::asks(row, 0).strength);
        }
        if (const int short_ = std::min(points, asked - hero.points.strength); short_ > 0) {
            hand_.spend(short_, 0, 0, 0);
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
                hand_.spend(0, 0, 0, short_);
                points -= short_;
                if (points <= 0) return;
            }
        }
        // The knight 4/3/2/0 since 2026-10-03: of five builds over three seeds of eight hours it
        // handed in all three quests soonest (50, 79, 161 min against 63, 86, 171 at 4/2/3/0) and
        // ended highest (156-161) -- agility's defence rate and pace over vitality's health.
        int w[4] = {4, 3, 2, 0};  // strength, agility, vitality, energy
        // The wizard 1/1/5/4 from 2026-10-03 ("improve DW bot"), when he threw Lightning at 40
        // mana a cast and every point of health saved a potion. **1/1/4/5 since 2026-10-08**, his
        // spells chosen by their mana (spell): over two seeds of 12 h, levels 116 and 120 against
        // 113 and 110 at 1/1/5/4; 1/1/3/6 swung 108-125 with more deaths, 1/1/6/3 fell to 83.
        // **All of it energy since 2026-10-09**, after the strength his armour asks (above): the
        // user, "dw should prioritise put points on energy becuase it give damage and defense,
        // but he also needs to think about strength to wear better items". 1/1/4/5 was tuned
        // while he could not afford his dear spells (drink: mana at three tenths) and threw Fire
        // Ball; with his mana kept over half, four seeds of 24 h: 1/1/4/5 153/154/177/186,
        // 0/0/3/7 666 summed, 0/0/2/8 679, 0/0/1/9 187/179/188/181, 0/0/0/1 188/179/188/192
        // with 42 quests handed in against 39.
        // **0/0/1/3 since 2026-10-10** (the user: "improve DW brains so he can farm 50+ and get
        // to his first wings"), once his strength stopped at what can fall for him (findableLevel)
        // and so 200-odd points came back to the build. Eight seeds of 24 h: all energy
        // 188/222/245/207/173/206/172/190, deaths 17-64, wings on two of eight; 0/0/1/3
        // 216/209/197/216/188/216/185/180, deaths 17-27, wings on four of eight; 0/0/1/1
        // 210/199/197/191/149/225/197/167, deaths 5-15, wings on two. Blood Castle 1, which
        // pays a Jewel of Life, is where most of his Life comes from: 26 wins in 55 runs on 0/0/1/3.
        if (wizardly()) { w[0] = 0; w[1] = 0; w[2] = 1; w[3] = 3; }
        // **The magic gladiator 0/0/1/3 too since 2026-10-10**, as the wizard: all energy left
        // him 230-440 health at level 230, and a castle's garrison caught him at its door nine
        // times a day. Four seeds of 24 h: levels 1,021 -> 1,088 summed, deaths 134 -> 45, castles
        // won 24/51 -> 45/59, wings on two seeds against one.
        if (gladiatorMage()) { w[0] = 0; w[1] = 0; w[2] = 1; w[3] = 3; }
        if (options_.kin == sim::Kin::FairyElf) { w[0] = 2; w[1] = 5; w[2] = 2; w[3] = 1; }
        if (options_.kin == sim::Kin::FairyElf && options_.support) { w[0] = 1; w[1] = 2; w[2] = 2; w[3] = 5; }
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
        give[wizardly() || options_.support ? 3 : options_.kin == sim::Kin::FairyElf ? 1 : 0] += left;
        hand_.spend(give[0], give[1], give[2], give[3]);
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
            if (!one.empty() && kind(rowOf(one)) && hand_.useItem(slot)) {
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
        if (!knightly()) return true;
        for (int i = 0; i < sim::skillCount(); ++i) {
            if (realm_->knows(sim::skillAt(i).number)) return true;
        }
        return false;
    }

    void drink() {
        const sim::Body& hero = realm_->hero();
        // In Blood Castle at 70%: no town to fall back on, and what follows him in is many. And a
        // spellcaster at --drink-at (65%): a pack of Elite Yetis or Ghosts outran his half and
        // killed a wizard of 400 health 43 times in three runs of a day.
        const double at = map() == int(sim::kBloodCastleMap) || map() == int(sim::kDevilSquareMap) ? 7.0
                          : wizardly() ? options_.drinkAt : 5.0;
        if (hero.health * 10.0 < hero.maxHealth * at) drinkOne(sim::heals);
        // A wizard under half, where spell() throws his dear spells (`flush`): at three tenths
        // his mana sat between the two on Fire Ball's five, never drank and never cast -- a day's
        // wizard bought 3,360 healing potions and 39 mana, and threw Fire Ball 24,103 times
        // against Meteorite, Ice and Flame's none at level 153 (the Claude bot's runs, 2026-10-09).
        const int manaAt = wizardly() ? 5 : 3;
        if (casts() && hero.mana * 10 < hero.maxMana * manaAt) drinkOne(sim::restores);
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
        if (wizardly()) {
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
                        if (hand_.moveItem(w, to)) break;
                    }
                    continue;
                }
                for (int at = sim::kWorn; at < sim::kSlots; ++at) {
                    if (same(realm_->satchel()[at], worn[w])) {
                        hand_.moveItem(at, w);
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
            // **Not the Archangel's weapon while he is in the castle** (2026-10-10): the statue's
            // Divine Sword outscored a knight's own, he wore it at 23:31 of a run, and the hand-in
            // looks in the bag alone (Realm::staffSlot) -- the run timed out with the weapon on his
            // hip. Out of the castle a won one is his to wear.
            if (inCastle() && sim::archangelWeapon(row)) continue;
            // Rings and the pendant are wearJewellery's, on its own worth: scored here by their
            // sockets alone, a socketed Ring of Ice and a Ring of Fortune traded places every
            // sort, 1,600 times in six hours (knight, seed 2, 2026-10-03).
            if (sim::jewellery(row) || anyHelper(row)) continue;
            // An elf keeps to the bow: her skills and her arrows are its. And to the one she can
            // feed: a crossbow shoots bolts where a bow shoots arrows, and a switch with no
            // quiver for it and no Zen for one leaves her nothing to shoot.
            if (options_.kin == sim::Kin::FairyElf &&
                (row.shield() || (row.weapon() && row.group != sim::kGroupBows))) continue;
            // And a wizard to his staff: his spells are its rise.
            if (wizardly() && row.weapon() && row.magicPower <= 0) continue;
            // No shield while a two-hander is in his right hand: a wizard's Chaos Lightning Staff
            // put his Small Shield in the bag, the shield took the staff off again, and the two
            // traded places a hundred times a minute.
            if (row.shield() && !realm_->satchel()[sim::kWeaponRight].empty() &&
                rowOf(realm_->satchel()[sim::kWeaponRight]).twoHanded()) continue;
            if (options_.kin == sim::Kin::FairyElf && row.weapon() && archer() &&
                sim::placeOf(row) != (ammoHand() == sim::kWeaponRight ? int(sim::kWeaponLeft) : int(sim::kWeaponRight)) &&
                realm_->money() < 1000) continue;
            for (const int place : {int(sim::kWeaponRight), int(sim::kWeaponLeft), sim::placeOf(row)}) {
                if (!sim::placesIn(row, options_.kin, place)) continue;
                // **The left hand is the shield's** for a knight and a wizard (the user,
                // 2026-10-03: "teach DK to use shield and defense skill", "same with DW"): a
                // second weapon there scored its offhand blow over any shield, and Defense and
                // Soul Barrier, drawn up behind one, were never cast.
                if (options_.kin != sim::Kin::FairyElf &&
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
                            hand_.moveItem(sim::kWeaponLeft, to)) freed = to;
                    }
                    if (freed < 0) continue;
                }
                const auto putBack = [&] {
                    if (freed >= 0) hand_.moveItem(freed, sim::kWeaponLeft);
                };
                if (!sim::movable(*tables_, realm_->wearer(), realm_->satchel(), slot, place)) {
                    putBack();
                    continue;
                }
                const bool wasEmpty = realm_->satchel()[place].empty();
                const double before = freed >= 0 ? beforeFreed : score();
                if (!hand_.moveItem(slot, place)) {
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
                if (wasEmpty) hand_.moveItem(place, slot);
                else hand_.moveItem(slot, place);
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
            if (hand_.useItem(slot)) say("learns from %s", label.c_str());
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
                const int pieceCell = hand_.putIn(piece);
                const int chaosCell = hand_.putIn(chaosSlot);
                const bool done = pieceCell >= 0 && chaosCell >= 0 && hand_.mix(sim::Service::RemoveRune, k);
                // Everything back, the piece first so it finds a slot.
                piece = -1;
                for (int cell = 0; cell < sim::kMachineCells; ++cell) {
                    if (realm_->machine()[cell].empty()) continue;
                    const bool isPiece = rowOf(realm_->machine()[cell]).label == label;
                    const int back = hand_.takeOut(cell);
                    if (isPiece) piece = back;
                }
                if (done) say("takes a rune out of his %s at the Chaos Goblin", label.c_str());
                if (!done || piece < 0) break;
            }
        }
    }

    void useJewels() {
        raiseChaosWeapon();
        // **The runes rarest first** (the quest ladder, 2026-10-09): Devias pays the knight a
        // two-socket Double Blade and two Epics while Marlon's Common Gust is still in his bag, and
        // in bag order the Gust could take a socket an Epic was paid for. A Legendary, then an
        // Epic, a Rare, a Common.
        const auto rank = [&](const sim::Held& one) {
            const sim::PowerRow* power = sim::powerOf(one.powers[0]);
            if (!power) return 0;
            switch (power->rarity) {
                case sim::Rarity::Legendary: return 4;
                case sim::Rarity::Epic: return 3;
                case sim::Rarity::Rare: return 2;
                case sim::Rarity::Common: return 1;
            }
            return 0;
        };
        std::vector<int> runes;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && sim::creation(rowOf(one))) runes.push_back(slot);
        }
        std::stable_sort(runes.begin(), runes.end(), [&](int a, int b) {
            return rank(realm_->satchel()[a]) > rank(realm_->satchel()[b]);
        });
        for (const int slot : runes) {
            const sim::Held jewel = realm_->satchel()[slot];
            if (jewel.empty() || !sim::creation(rowOf(jewel))) continue;
            const content::ItemRow& row = rowOf(jewel);
            // Any worn piece it may be set in, the weapon and shield first: a Keen Eye,
            // a Bloodwell, an Undying go in armour and never in a weapon (sim::settable).
            for (const int to : {int(sim::kWeaponRight), int(sim::kWeaponLeft), 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) {
                if (to >= sim::kWorn || realm_->satchel()[to].empty()) continue;
                if (!sim::settable(*tables_, jewel, realm_->satchel()[to], options_.kin, realm_->hero().second)) continue;
                // **A weapon rune waits for a weapon worth keeping** (the user, 2026-10-03:
                // "bot saves weapon runes"): Marlon's rune went into the Falchion at level 40,
                // was sold with it, and the Double Blade's sockets stayed empty. The knight's
                // alone: the wizard's and the elf's home weapons are the ones they keep.
                const content::ItemRow& target = rowOf(realm_->satchel()[to]);
                if (knightly() && target.weapon() && !target.shield() &&
                    target.dropLevel < kRuneWeaponLevel) continue;
                const std::string on = rowOf(realm_->satchel()[to]).label;
                if (hand_.refine(slot, to)) {
                    say("sets %s into his %s", row.label.c_str(), on.c_str());
                    break;
                }
            }
        }
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held jewel = realm_->satchel()[slot];
            if (jewel.empty()) continue;
            const content::ItemRow& row = rowOf(jewel);
            if (sim::creation(row)) continue;
            if (!sim::refiningJewel(row)) continue;
            // Held back for the wing ladder until it is climbed (ladderJewels).
            if (ladderOn() && countJewel(sim::jewelOf(row)) <= ladderJewels(sim::jewelOf(row))) continue;
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
            if (hand_.refine(slot, best)) {
                say("%s on %s +%d: +%d", row.label.c_str(), on.c_str(), was,
                    int(realm_->satchel()[best].refinement));
            }
        }
    }

    // What a ring or pendant is worth to him: each power at its plus (sim::affixValue), a point
    // of resistance, the option, and luck's crit. A rough sum; it only has to rank them.
    double jewelleryWorth(const sim::Held& one) const {
        const content::ItemRow& row = tables_->items[size_t(one.item)];
        // A point of resistance, every element's: Ice, Poison and Lightning turn the chill, the
        // poison and the push aside, Fire cuts fire blows (docs/jewellery.md).
        double worth = sim::resistanceOf(row, one.refinement) + one.option * 2.0 +
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
            weight = knightly() ? 2.5
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
                if (spare < 0 || !hand_.moveItem(place, spare)) continue;
            }
            if (hand_.moveItem(from, place)) say("wears %s", label.c_str());
        }
    }

    // Firecrackers and boxes opened where he hunts, what they leave picked up as a kill's drop
    // (the user, 2026-10-08: "does bots open firecrackers to get better item or chaos machine
    // materials?" -- they sold them).
    void openBoxes() {
        if (mode_ != Mode::Hunt) return;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            if (!realm_->cracks(slot)) continue;
            const std::string label = rowOf(realm_->satchel()[slot]).label;
            if (hand_.crack(slot)) {
                ++opened_;
                say("opens a %s", label.c_str());
            }
            return;  // one a sort: what it leaves is picked up before the next
        }
    }
    void sortBag() {
        openBoxes();
        readOrbs();
        wearBest();
        wearJewellery();
        wearHelpers();
        wearWings();
        useJewels();
    }

    // At the Goblin: what the last box left taken out, then one mix an opening -- the machine
    // locks after one until it is reopened -- the window shut and opened again while more is owed.
    // True while he is busy there.
    bool goblinVisit() {
        const int goblin = goblinHere();
        if (std::getenv("BOT_MIX") && clock_ % 100 == 0) {
            std::printf("MIX visit: goblin %d since %lld mixing %d\n", goblin, (long long)(clock_ - machineSince_), realm_->mixing());
        }
        if (goblin < 0 || clock_ - machineSince_ > 90 * 20) {
            machineOwed_ = false;
            return false;
        }
        if (realm_->mixing() != goblin) {
            talkTo(goblin);
            return true;
        }
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm_->machine()[cell].empty()) hand_.takeOut(cell);
        }
        // What the bag has no room for stays in the box, and nothing new goes in beside it: a
        // wizard's cloak +1 stayed there and he put a scroll, a bone and a Chaos in beside it
        // every 90 seconds for 60 hours. Short of room (boxStuck_), so the counters sell.
        boxStuck_ = boxHeld();
        if (boxStuck_) {
            say("the Goblin's box holds what his bag has no room for");
            hand_.closeMachine();
            machineOwed_ = false;
            return false;
        }
        bool owed = false;
        for (int slot = sim::kWorn; slot < sim::kSlots && !owed; ++slot) owed = unruneWorth(realm_->satchel()[slot]);
        if (owed) unrune();
        else if (cloakWorth() > 0) makeCloak();
        else if (invitationWorth() >= 0) makeInvitation();
        else mixWings();
        hand_.closeMachine();
        sortBag();
        machineOwed_ = cloakWorth() > 0 || invitationWorth() >= 0 || wingsOwed();
        for (int slot = sim::kWorn; slot < sim::kSlots && !machineOwed_; ++slot) {
            machineOwed_ = unruneWorth(realm_->satchel()[slot]);
        }
        tripSince_ = clock_;
        return machineOwed_;
    }

    // ---- the wing ladder (docs/wings.md, sim/machine.h) -------------------------------------
    // The user, 2026-10-08: "chaos machine". A thing at +4 or better with an option (or a socket,
    // ours) and a Chaos make a Chaos weapon at the Goblin; that weapon raised to +4 with Bless and
    // given an option with Life, and a Chaos, make his class's 1st wings. The rate is the box's
    // worth over 20,000 and each percent costs 10,000 Zen (sim::judge says both); Bless and Soul
    // in the wing box raise it.
    static bool chaosWeaponRow(const content::ItemRow& row) {
        return (row.group == 2 && row.number == 6) || (row.group == sim::kGroupBows && row.number == 6) ||
               (row.group == 5 && row.number == 7);
    }
    bool hasWings() const {
        for (int slot = 0; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            const content::ItemRow& row = rowOf(one);
            if ((sim::firstWing(row) || sim::secondWing(row)) && sim::placesIn(row, options_.kin, sim::kWings)) return true;
        }
        return false;
    }
    void wearWings() {
        if (!realm_->satchel()[sim::kWings].empty()) return;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            const content::ItemRow& row = rowOf(one);
            if (!(sim::firstWing(row) || sim::secondWing(row)) || !sim::placesIn(row, options_.kin, sim::kWings)) continue;
            const std::string label = row.label;
            if (hand_.moveItem(slot, sim::kWings)) say("** wears %s", label.c_str());
            return;
        }
    }
    // A thing the Chaos Weapon's box takes: +4 or better with an option or a socket.
    bool optioned(const sim::Held& one) const {
        if (one.empty()) return false;
        const content::ItemRow& row = rowOf(one);
        return row.group <= sim::kGroupBoots && !sim::ammunition(row) && one.refinement >= 4 &&
               (one.option > 0 || one.sockets > 0);
    }
    // The Chaos weapon he has, bag or hands, the highest plus first, or -1.
    int chaosWeaponSlot() const {
        int best = -1;
        for (int slot = 0; slot < sim::kSlots; ++slot) {
            if (slot >= sim::kWeaponLeft + 1 && slot < sim::kWorn) continue;
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty() || !chaosWeaponRow(rowOf(one))) continue;
            if (best < 0 || one.refinement > realm_->satchel()[best].refinement) best = slot;
        }
        return best;
    }
    // The bag's fodder he keeps through a sale while he has no wings and the bag has room: the
    // three worth the most to the machine, never what he wears.
    // A fixed share of the bag, so what he keeps does not change with how full it is (a rule
    // by free room sold the fodder at the counter and took a 10% box to the Goblin as a 2%).
    // Held whatever his purse: a knight below 400,000 Zen most of the day sold every +4 piece
    // with an option and was never once ready to mix; the box's own Zen is asked at the Goblin.
    bool fodder(int slot) const {
        if (hasWings() || crowded_ || slot < sim::kWorn) return false;
        const sim::Held& one = realm_->satchel()[slot];
        if (!optioned(one) || chaosWeaponRow(rowOf(one))) return false;
        const int64_t mine = sim::mixValue(*tables_, one);
        if (rowOf(one).width * rowOf(one).height > 6) return false;  // three at most 12 cells, near enough
        int better = 0;
        for (int other = sim::kWorn; other < sim::kSlots; ++other) {
            if (other == slot) continue;
            const sim::Held& o = realm_->satchel()[other];
            if (!optioned(o) || chaosWeaponRow(rowOf(o)) || rowOf(o).width * rowOf(o).height > 6) continue;
            const int64_t theirs = sim::mixValue(*tables_, o);
            if (theirs > mine || (theirs == mine && other < slot)) ++better;
        }
        return better < 3;
    }
    // A box of these bag slots, judged as the Goblin would for this service.
    // `fits` false when a thing found no room in the 8 x 4 box: what fits is judged, but a box
    // that does not all go in is never mixed (a wizard's staff and forty jewels went in and came
    // back out twice a second for hours).
    sim::Judged judgeBox(const std::vector<int>& slots, sim::Service service, bool* fits = nullptr) const {
        sim::Machine box;
        if (fits) *fits = true;
        for (const int slot : slots) {
            const sim::Held& one = realm_->satchel()[slot];
            const content::ItemRow& row = rowOf(one);
            const int cell = box.free(*tables_, row.width, row.height);
            if (cell < 0) {
                if (fits) *fits = false;
                break;
            }
            box.put(cell, one);
        }
        return sim::judge(*tables_, box, service, -1, options_.kin);
    }
    // The box he would mix now, and its service, or empty: the wings' when his Chaos weapon is
    // ready, else a Chaos weapon's from his fodder -- each only at 10% or more and with the Zen
    // over his potions twice.
    std::vector<int> wingBox(sim::Service* service, sim::Judged* judged) const {
        std::vector<int> box;
        if (hasWings()) return box;
        int jewel = -1;
        std::vector<int> things, raisers;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            const content::ItemRow& row = rowOf(one);
            if (chaos(row) && jewel < 0) jewel = slot;
            if (fodder(slot)) things.push_back(slot);
            if (sim::refiningJewel(row) && (sim::jewelOf(row) == sim::Jewel::Bless || sim::jewelOf(row) == sim::Jewel::Soul)) {
                raisers.push_back(slot);
            }
        }
        if (jewel < 0) return box;
        const int weapon = chaosWeaponSlot();
        if (weapon >= 0 && optioned(realm_->satchel()[weapon])) {
            *service = sim::Service::FirstWings;
            box = {weapon, jewel};
            box.insert(box.end(), things.begin(), things.end());
        } else if (weapon < 0 && !things.empty()) {
            *service = sim::Service::ChaosWeapon;
            box = {jewel};
            box.insert(box.end(), things.begin(), things.end());
            // Four Bless kept back for the weapon it makes, which goes to the wings' box at +4.
            int keep = 4;
            for (auto it = raisers.begin(); it != raisers.end() && keep > 0;) {
                if (sim::jewelOf(rowOf(realm_->satchel()[*it])) == sim::Jewel::Bless) {
                    it = raisers.erase(it);
                    --keep;
                } else {
                    ++it;
                }
            }
        } else {
            return {};
        }
        // Then the jewels, while his Zen runs to the rate they make: a Chaos is 40,000 of the
        // box's worth, 2% (sim::mixValue), and only jewels are lost when it fails, so a full box
        // is the cheaper road to wings -- an elf mixed four boxes of one Chaos at 12-26% with
        // seventeen more in the vault, and failed all four. One Chaos is held back for a cloak
        // whose Scroll and Bone he carries.
        int spare = cloakPairHeld() ? 1 : 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (slot == jewel || one.empty() || !chaos(rowOf(one))) continue;
            if (spare > 0) {
                --spare;
                continue;
            }
            raisers.push_back(slot);
        }
        // A hundred potions of his size kept back: a knight who drinks ~190 an hour spent all but
        // seven on a 30% box and spent the next 18 hours short of potions, 22 levels behind.
        const int64_t purse = realm_->money() - 100 * potionPrice(healTier());
        *judged = judgeBox(box, *service);
        for (const int slot : raisers) {
            if (!judged->ready || judged->rate >= 100) break;
            box.push_back(slot);
            bool fits = true;
            const sim::Judged more = judgeBox(box, *service, &fits);
            if (!fits || more.zen > purse) {
                box.pop_back();
                break;
            }
            *judged = more;
        }
        bool fits = true;
        judgeBox(box, *service, &fits);
        if (!fits || !judged->ready || judged->rate < kMixAt || purse < judged->zen) return {};
        return box;
    }
    // The rate a wing-ladder box is mixed at: below it he waits for more jewels and Zen.
    static constexpr int kMixAt = 30;
    // Whether he carries a Scroll and Bone of one level he would make a cloak of.
    bool cloakPairHeld() const {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && sim::scrollOfArchangel(rowOf(one)) && cloakHalfWanted(one, true)) return true;
        }
        return false;
    }
    void ladderTrace() const {
        int optionedBag = 0, fod = 0, bless = countJewel(sim::Jewel::Bless), soul = countJewel(sim::Jewel::Soul);
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            optionedBag += optioned(realm_->satchel()[slot]);
            fod += fodder(slot);
        }
        sim::Service service = sim::Service::ChaosWeapon;
        sim::Judged judged;
        const std::vector<int> box = wingBox(&service, &judged);
        std::printf("LADDER level %d zen %lld on %d crowded %d wings %d chaos %d bless %d soul %d optioned %d fodder %d "
                    "weapon %d box %zu rate %d free %d\n",
                    realm_->hero().level, (long long)realm_->money(), ladderOn(), crowded_, hasWings(), chaosCount(),
                    bless, soul, optionedBag, fod, chaosWeaponSlot(), box.size(), box.empty() ? -1 : judged.rate,
                    realm_->satchel().free(*tables_, 2, 2));
        int vb = 0, vs = 0, vc = 0, vf = 0;
        for (int cell = 0; cell < sim::kVaultCells; ++cell) {
            const sim::Held& one = realm_->vault()[cell];
            if (one.empty()) continue;
            const content::ItemRow& row = rowOf(one);
            if (chaos(row)) ++vc;
            else if (sim::refiningJewel(row) && sim::jewelOf(row) == sim::Jewel::Bless) ++vb;
            else if (sim::refiningJewel(row) && sim::jewelOf(row) == sim::Jewel::Soul) ++vs;
            vf += fetches(one);
        }
        std::printf("VAULT bless %d soul %d chaos %d fetch %d owed %d freecells %d\n", vb, vs, vc, vf, fetchOwed(), freeCells());
        std::map<std::string, int> in;
        for (int cell = 0; cell < sim::kVaultCells; ++cell) {
            if (!realm_->vault()[cell].empty()) ++in[rowOf(realm_->vault()[cell]).label];
        }
        std::string list;
        for (const auto& [name, n] : in) list += name + " " + std::to_string(n) + ", ";
        std::printf("VAULTED %s\n", list.c_str());
        std::string bag;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty()) bag += rowOf(one).label + "+" + std::to_string(one.refinement) + (keepsAt(slot) ? "* " : " ");
        }
        std::printf("BAGNOW %s\n", bag.c_str());
        const sim::Body& me = realm_->hero();
        std::printf("STATE mode %d aim %d quest %d aimMap %d map %d at %d,%d walking %d kills %d chasing %u awayTo %d owedMap %d trip %d rest %d\n",
                    int(mode_), int(aim_), aimQuest_, aimMap_, map(), me.column(), me.row(), me.walking,
                    out_.kills, chasing_, awayTo_, owedMap_, tripOwed_, restOwed_);
        if (const content::Tables* sky = const_cast<Bot*>(this)->world(10)) {
            std::string costs;
            std::set<int> seen;
            for (const content::MonsterNest& nest : sky->nests) {
                const content::MonsterKind& kind = sky->kinds[nest.kind];
                if (!seen.insert(kind.number).second) continue;
                char one[96];
                std::snprintf(one, sizeof one, "%s(%d) %.0f, ", kind.label.c_str(), kind.level, costOf(kind));
                costs += one;
                if (kind.level == 75 || kind.level == 96) {
                    double hit = 0.0;
                    int32_t ticks = 1;
                    attack(&hit, &ticks, kind.defense);
                    const sim::Body& h = realm_->hero();
                    std::printf("PARTS %s hit %.0f ticks %d landed %.0f myHit%% %.0f | blow %.0f theirHit%% %.0f guard %.2f\n",
                                kind.label.c_str(), hit, ticks, std::max(1.0, hit - kind.defense),
                                100 * hitChance(h.stats.attackRate, float(kind.defenseRate)),
                                std::max(0.0, (kind.minimumDamage + kind.maximumDamage) / 2.0 - h.stats.defense),
                                100 * hitChance(float(kind.attackRate), h.stats.defenseRate), guardSeen_);
                }
            }
            const sim::Body& hero = realm_->hero();
            std::printf("ICARUS hp %d def %.0f rate %.0f dmg %.0f-%.0f fly %d | %s\n", hero.maxHealth,
                        double(hero.stats.defense), double(hero.stats.defenseRate), double(hero.stats.minimumDamage),
                        double(hero.stats.maximumDamage), sim::canFly(*tables_, realm_->satchel()), costs.c_str());
        }
    }
    bool wingsOwed() const {
        if (runesWaiting() > 0 && chaosCount() <= runesWaiting()) return false;
        sim::Service service;
        sim::Judged judged;
        return !wingBox(&service, &judged).empty();
    }
    // At the Goblin's box: the box in, mixed, everything out.
    void mixWings() {
        if (runesWaiting() > 0 && chaosCount() <= runesWaiting()) return;
        sim::Service service;
        sim::Judged judged;
        std::vector<int> box = wingBox(&service, &judged);
        if (std::getenv("BOT_MIX")) {
            std::printf("MIX wings: box %zu service %d ready %d rate %d zen %lld money %lld machine empty %d weapon %d\n",
                        box.size(), int(service), judged.ready, judged.rate, (long long)judged.zen,
                        (long long)realm_->money(), realm_->machine().empty(), chaosWeaponSlot());
        }
        if (box.empty()) return;
        if (!realm_->machine().empty()) return;  // what a full bag left in it last time
        bool in = true;
        for (const int slot : box) in = in && hand_.putIn(slot) >= 0;
        const bool mixed = in && hand_.mix(service);
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm_->machine()[cell].empty()) hand_.takeOut(cell);
        }
        if (!mixed) return;
        const bool wings = service == sim::Service::FirstWings;
        const bool made = wings ? hasWings() : chaosWeaponSlot() >= 0;
        if (wings && made && out_.wingsTick < 0) out_.wingsTick = clock_;
        say("%s at the Chaos Goblin: %s (%d%%, %lld zen)", wings ? "the 1st wings' box" : "the Chaos Weapon's box",
            made ? "** made" : "failed", judged.rate, (long long)judged.zen);
        wearWings();
    }
    // His Chaos weapon brought up to the wings' box: Bless to +4, then Life for an option.
    void raiseChaosWeapon() {
        if (hasWings()) return;
        const int weapon = chaosWeaponSlot();
        if (weapon < 0 || optioned(realm_->satchel()[weapon])) return;
        const sim::Held& it = realm_->satchel()[weapon];
        const sim::Jewel want = it.refinement < 4 ? sim::Jewel::Bless : sim::Jewel::Life;
        if (want == sim::Jewel::Life && it.sockets > 0) return;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty() || !sim::refiningJewel(rowOf(one)) || sim::jewelOf(rowOf(one)) != want) continue;
            if (!sim::refinable(*tables_, one, it)) return;
            // Both names copied first: the jewel's slot is empty once it is spent.
            const std::string label = rowOf(it).label, jewel = rowOf(one).label;
            const int was = it.refinement, option = it.option;
            if (hand_.refine(slot, weapon)) {
                const sim::Held& now = realm_->satchel()[weapon];
                say("%s on his %s: +%d -> +%d, option %d -> %d", jewel.c_str(), label.c_str(), was,
                    int(now.refinement), option, int(now.option));
            }
            return;
        }
    }

    // ---- pets and mounts (docs/pets.md, docs/mount.md) -------------------------------------
    // The user, 2026-10-08: "satan, guardian angle use", "mounts". The Guardian Angel (13/0) takes
    // 30% off every blow on him and adds 50 health, and his own blows lose a fifth; the Imp (13/1)
    // adds 30% to his blows for 3 of his life each; both wear out under blows taken and are gone
    // at nought. A mount rides its own slot beside the pet: Uniria (13/2) is speed alone, Dinorant
    // (13/3) adds 15% to his blows and takes 10% off theirs. All four are on Lumen's shelf in
    // Lorencia's tavern. None of it moves his blow or guard as `score` reads them, so they are
    // chosen here and not tried on.
    static constexpr int kAngel = 0, kImp = 1, kUniria = 2, kDinorant = 3;
    static bool helper(const content::ItemRow& row, int number) {
        return row.group == sim::kGroupPets && row.number == number;
    }
    static bool anyHelper(const content::ItemRow& row) {
        return row.group == sim::kGroupPets && row.number >= kAngel && row.number <= kDinorant;
    }
    // The pet his class keeps: the Imp for every class, measured (--pet), or -1 for none.
    int wantedPet() const {
        if (options_.pet != -1) return options_.pet;
        return kImp;
    }
    int heldHelper(int number, bool bagOnly = false) const {
        for (int slot = bagOnly ? int(sim::kWorn) : 0; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && helper(rowOf(one), number) && one.durability > 0) return slot;
        }
        return -1;
    }
    // Puts on the pet he keeps and the best mount he carries; the old one back into the bag.
    void wearHelpers() {
        const auto wear = [&](int place, int number) {
            const int from = heldHelper(number, true);
            if (from < 0) return;
            const sim::Held& on = realm_->satchel()[place];
            if (!on.empty()) {
                const content::ItemRow& old = rowOf(on);
                const int spare = realm_->satchel().free(*tables_, old.width, old.height);
                if (spare < 0 || !hand_.moveItem(place, spare)) return;
            }
            const std::string label = rowOf(realm_->satchel()[from]).label;
            if (hand_.moveItem(from, place)) say("wears %s", label.c_str());
        };
        const int pet = wantedPet();
        const sim::Held& onPet = realm_->satchel()[sim::kPet];
        if (pet >= 0 && (onPet.empty() || !helper(rowOf(onPet), pet))) wear(sim::kPet, pet);
        const sim::Held& onMount = realm_->satchel()[sim::kMount];
        const bool dinorant = !onMount.empty() && helper(rowOf(onMount), kDinorant);
        if (!dinorant) {
            if (heldHelper(kDinorant, true) >= 0) wear(sim::kMount, kDinorant);
            else if (onMount.empty()) wear(sim::kMount, kUniria);
        }
    }
    int64_t helperPrice(int number) const {
        const int item = tables_->itemAt(sim::kGroupPets, number);
        return item < 0 ? -1 : sim::buyingPrice(tables_->items[size_t(item)], 0, 0, false);
    }
    // What a helper he lacks would cost now, if his purse runs to it over his potions, or -1.
    int helperOwed() const {
        const int64_t spare = realm_->money() - potionReserve();
        const int pet = wantedPet();
        if (pet >= 0 && heldHelper(pet) < 0) {
            const int64_t price = helperPrice(pet);
            if (price > 0 && spare >= price) return pet;
        }
        if (heldHelper(kDinorant) < 0) {
            const int64_t price = helperPrice(kDinorant);
            if (price > 0 && spare >= price * 2) return kDinorant;  // never his last Zen on it
            if (heldHelper(kUniria) < 0 && helperPrice(kUniria) > 0 && spare >= helperPrice(kUniria)) return kUniria;
        }
        return -1;
    }
    void buyHelpers(const sim::Offer* shelf, int count) {
        for (int pass = 0; pass < 3; ++pass) {
            const int want = helperOwed();
            if (want < 0) return;
            bool bought = false;
            for (int i = 0; i < count && !bought; ++i) {
                if (shelf[i].group != sim::kGroupPets || shelf[i].number != want) continue;
                bought = buyOne(shelf[i], " (a helper)") >= 0;
            }
            if (!bought) return;
            wearHelpers();
        }
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
        if (jewelKind(row) && vaultJewels(one) >= jewelCap(row)) return false;
        // A weapon rune of his class is carried for the weapon it waits for (savesRune), and
        // the Jewels of Chaos for the machine's Remove Rune, three of them.
        if (sim::creation(row)) return !knightly() || !weaponRune(one) || weaponRunes() > kWeaponRunes;
        // All of them while he climbs the wing ladder: each is 2% in its box (wingBox).
        if (chaos(row)) return ladderOn() ? false : chaosCount() > 3;
        // The wing ladder's, carried while he has none: Bless to raise the Chaos weapon, Life for
        // its option, Soul for the wing box's rate.
        // Not once a reward has found no room (crowded_): an elf who kept them, and her fodder,
        // had no room for the Golden Archer's reward and asked for it for twelve hours.
        if (ladderOn() && sim::refiningJewel(row) && countJewel(sim::jewelOf(row)) <= ladderJewels(sim::jewelOf(row))) {
            return false;
        }
        // **The warehouse** (the user, 2026-10-08: "teach bots also to use warehouse"): what he
        // carries for no use yet goes to the vault -- the pet he does not keep, and a Scroll of
        // Archangel or Blood Bone whose other half of that level is neither in the bag nor the
        // vault, or whose castle he may not enter yet. `fetches` brings each back when it is wanted.
        if (helper(row, kAngel) && wantedPet() != kAngel) return vaultHolds(one) < 1;
        if (sim::scrollOfArchangel(row) || sim::bloodBone(row)) {
            int carried = 0;
            for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                const sim::Held& o = realm_->satchel()[slot];
                carried += !o.empty() && o.item == one.item && o.refinement == one.refinement;
            }
            return (carried > 1 || !cloakHalfWanted(one, false)) && vaultHolds(one) < 2;
        }
        return sim::refiningJewel(row);
    }
    // How many of this thing at this plus the vault holds.
    int vaultHolds(const sim::Held& one) const {
        int n = 0;
        for (int cell = 0; cell < sim::kVaultCells; ++cell) {
            const sim::Held& o = realm_->vault()[cell];
            n += !o.empty() && o.item == one.item && o.refinement == one.refinement;
        }
        return n;
    }
    // Whether a Scroll or Bone is half of a cloak he would make: a castle of its level he may
    // enter, no such cloak already, and the other half of that level in the bag or (`bagOnly`
    // false) the vault.
    bool cloakHalfWanted(const sim::Held& one, bool bagOnly) const {
        const content::ItemRow& row = rowOf(one);
        const int level = one.refinement;
        if (level < 1 || level > sim::kCastlesBuilt || realm_->hero().level < sim::kCastleBands[level - 1][0]) return false;
        if (realm_->cloakSlot(level) >= 0) return false;
        const bool scroll = sim::scrollOfArchangel(row);
        const auto partner = [&](const sim::Held& o) {
            if (o.empty() || o.refinement != level) return false;
            const content::ItemRow& r = rowOf(o);
            return scroll ? sim::bloodBone(r) : sim::scrollOfArchangel(r);
        };
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            if (partner(realm_->satchel()[slot])) return true;
        }
        if (bagOnly) return false;
        for (int cell = 0; cell < sim::kVaultCells; ++cell) {
            if (partner(realm_->vault()[cell])) return true;
        }
        return false;
    }
    // Whether a thing in the vault is wanted back now: the wing ladder's jewels to its count,
    // Chaos to three (the machine's), a Scroll or Bone whose other half he has, or the castle's
    // pair both in the vault once he may make it.
    bool fetches(const sim::Held& one) const {
        if (one.empty()) return false;
        const content::ItemRow& row = rowOf(one);
        // Past what he keeps: out, for the counter.
        if (jewelKind(row) && vaultJewels(one) > jewelCap(row)) return true;
        if (chaos(row)) return ladderOn() || chaosCount() < 3;
        if (sim::refiningJewel(row)) {
            const sim::Jewel kind = sim::jewelOf(row);
            return ladderOn() && (kind == sim::Jewel::Bless || kind == sim::Jewel::Soul || kind == sim::Jewel::Life) &&
                   countJewel(kind) < ladderJewels(kind);
        }
        if (sim::scrollOfArchangel(row) || sim::bloodBone(row)) {
            if (!cloakHalfWanted(one, false)) return false;
            // Not a second of the same half and level.
            for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                const sim::Held& o = realm_->satchel()[slot];
                if (!o.empty() && o.item == one.item && o.refinement == one.refinement) return false;
            }
            return true;
        }
        return false;
    }
    // **Jewels sold** (the user, 2026-10-08: "if bots will sell jewels they can earn a lot of
    // zen"): a merchant pays a third of WebZen's value -- 3M a Bless, 2M a Soul, 15M a Life, 12M a
    // Rune of Creation, 270k a Chaos (sim::sellingPrice) -- and a bot banked every one he had no
    // use for. What he keeps of each, bag and vault together; the rest goes to the counter.
    int jewelCap(const content::ItemRow& row) const {
        if (chaos(row)) return ladderOn() ? 1000 : 4;
        if (sim::creation(row)) return 4;
        switch (sim::jewelOf(row)) {
            case sim::Jewel::Bless: return 10;
            case sim::Jewel::Soul: return 10;
            case sim::Jewel::Life: return 4;
            default: return 1000;
        }
    }
    static bool jewelKind(const content::ItemRow& row) { return sim::refiningJewel(row) || sim::creation(row); }
    // How many of this jewel's kind the bag (slots before `upTo`) and the vault hold.
    int jewelsHeld(const sim::Held& one, int upTo = sim::kSlots) const {
        int n = vaultJewels(one);
        for (int slot = sim::kWorn; slot < upTo; ++slot) {
            const sim::Held& o = realm_->satchel()[slot];
            n += !o.empty() && sameJewel(o, one);
        }
        return n;
    }
    int vaultJewels(const sim::Held& one) const {
        int n = 0;
        for (int cell = 0; cell < sim::kVaultCells; ++cell) {
            const sim::Held& o = realm_->vault()[cell];
            n += !o.empty() && sameJewel(o, one);
        }
        return n;
    }
    bool sameJewel(const sim::Held& a, const sim::Held& b) const {
        const content::ItemRow& x = rowOf(a);
        const content::ItemRow& y = rowOf(b);
        if (sim::creation(x) || sim::creation(y)) return sim::creation(x) && sim::creation(y);
        return a.item == b.item;
    }
    // A jewel in the bag at `slot` past what he keeps: the vault's and those before it count first.
    bool surplusAt(int slot) const {
        const sim::Held& one = realm_->satchel()[slot];
        if (one.empty() || !jewelKind(rowOf(one))) return false;
        // The knight's weapon rune waits for its weapon (stores).
        if (sim::creation(rowOf(one)) && knightly() && weaponRune(one) && weaponRunes(slot) < kWeaponRunes) return false;
        return jewelsHeld(one, slot) >= jewelCap(rowOf(one));
    }
    bool fetchOwed() const {
        if (vaultHere() < 0 || freeCells() < 6) return false;
        for (int cell = 0; cell < sim::kVaultCells; ++cell) {
            if (fetches(realm_->vault()[cell])) return true;
        }
        return false;
    }
    // What the ladder holds back while he has no wings: the Bless and Soul raise a box's rate
    // (about 5 and 3.5 a jewel), and the Life gives the Chaos weapon its option. Three +4 things
    // of his level make a 2-3% box alone.
    // Whether he climbs it now: no wings, a bag with room, and the Zen for the boxes -- a knight
    // and a wizard who held their jewels back on 50,000 Zen never mixed and fell 6-10 levels
    // behind, their gear unrefined.
    // Nor while the Goblin's box holds what the bag has no room for (boxStuck_): an elf whose
    // bag was 13 Chaos held for the ladder could neither empty the box nor buy a quiver, and went
    // to town for arrows every second for forty hours at level 142.
    bool ladderOn() const { return !hasWings() && !crowded_ && !boxStuck_ && realm_->money() >= kLadderZen; }
    static constexpr int64_t kLadderZen = 400000;
    static int ladderJewels(sim::Jewel kind) {
        return kind == sim::Jewel::Bless ? 6 : kind == sim::Jewel::Soul ? 4 : 2;
    }
    int countJewel(sim::Jewel kind) const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && sim::refiningJewel(rowOf(one)) && sim::jewelOf(rowOf(one)) == kind) ++n;
        }
        return n;
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
    // The knight's weapon runes he carries for it, bag slots before `upTo`: three, a weapon's
    // sockets. Past them the vault and the counter take them as any rune (stores, surplusAt): a
    // knight carried ten into Tersia's desk, and The Scythe's Bill of Balrog found no room for
    // eight hours.
    static constexpr int kWeaponRunes = 3;
    int weaponRunes(int upTo = sim::kSlots) const {
        int n = 0;
        for (int slot = sim::kWorn; slot < upTo; ++slot) n += weaponRune(realm_->satchel()[slot]);
        return n;
    }
    // A Rune of Creation whose power goes in a weapon and is his class's.
    bool weaponRune(const sim::Held& one) const {
        if (one.empty() || !sim::creation(rowOf(one))) return false;
        const sim::PowerRow* power = sim::powerOf(one.powers[0]);
        return power != nullptr && (power->slots & sim::kInWeapon) != 0 && power->takenBy(options_.kin, realm_->hero().second);
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
    // **A good rune is saved** (the user, 2026-10-09: "frenzy is very good rune, which needs to
    // save it, remove that rune and attach to next weapon when there is upgrade", then "basically
    // any good rune has to be saved and attach to upgrade"): a carried piece with a Rare, Epic or
    // Legendary rune of his class set -- the weapon or boots he has just bettered -- is kept until
    // the Chaos Goblin takes the rune out, whatever his purse or Chaos say now; a Chaos is held
    // back from cloaks and wings for it; and the rune, back in the bag, goes into the upgrade
    // (useJewels, the weapon first). Sold with the old piece before, whenever he was short of a
    // Chaos that day. A Common is not kept for: the quests and the drops replace it.
    bool holdsGoodRune(const sim::Held& one) const {
        if (one.empty() || sim::creation(rowOf(one))) return false;
        for (int k = 0; k < std::min<int>(one.sockets, 3); ++k) {
            const sim::PowerRow* power = one.powers[k] ? sim::powerOf(one.powers[k]) : nullptr;
            if (power && power->rarity != sim::Rarity::Common && power->takenBy(options_.kin, realm_->hero().second)) {
                return true;
            }
        }
        return false;
    }
    int runesWaiting() const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) n += holdsGoodRune(realm_->satchel()[slot]);
        return n;
    }
    // A good rune he can take out now, at the Goblin.
    bool runeOwed() const {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            if (holdsGoodRune(realm_->satchel()[slot]) && unruneWorth(realm_->satchel()[slot])) return true;
        }
        return false;
    }
    bool unruneWorth(const sim::Held& one) const {
        const int64_t zen = unruneCost(one);
        if (zen < 0 || realm_->money() < zen + potionReserve()) return false;
        int runes = 0;
        for (int k = 0; k < std::min<int>(one.sockets, 3); ++k) runes += one.powers[k] ? 1 : 0;
        return chaosCount() >= runes;
    }
    // Whether the Goblin's box still holds something of his (sim::Kept keeps it between visits).
    bool boxHeld() const { return !realm_->machine().empty(); }
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
        // And whenever a reward waits on room (crowded_) and anything is his to store.
        return vaultHere() >= 0 &&
               (stash() >= 8 || ((freeCells() < 6 || crowded_) && stash() > 0) || fetchOwed());
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
        if (slot >= sim::kWorn && questItem(one)) return true;
        if (slot >= sim::kWorn && surplusAt(slot)) return false;
        if (slot >= sim::kWorn && nearlyFits(one)) return true;
        if (slot >= sim::kWorn && unruneWorth(one)) return true;
        if (slot >= sim::kWorn && holdsGoodRune(one)) return true;
        if (fodder(slot)) return true;
        if (slot >= sim::kWorn && !one.empty() && chaosWeaponRow(rowOf(one)) && !hasWings()) return true;
        // His class's wings, until he is level enough to wear them: an elf of 166 mixed the
        // Wings of Elf at 43% and sold them to the blacksmith for 18 million Zen.
        if (slot >= sim::kWorn && !one.empty() && (sim::firstWing(rowOf(one)) || sim::secondWing(rowOf(one))) &&
            sim::placesIn(rowOf(one), options_.kin, sim::kWings)) {
            return true;
        }
        if (one.empty() || !keeps(one)) return false;
        const content::ItemRow& row = rowOf(one);
        if (sim::ammunition(row)) return archer() && feeds(row);
        // One of each half a level in the bag: an elf whose pets' rule no longer sold the second
        // carried twelve of each at +1 and bought no arrows for hours.
        if (sim::scrollOfArchangel(row) || sim::bloodBone(row)) {
            for (int other = sim::kWorn; other < slot; ++other) {
                const sim::Held& was = realm_->satchel()[other];
                if (!was.empty() && was.item == one.item && was.refinement == one.refinement) return false;
            }
        }
        // Devil Square's Eye or Key he has no use for, or the vault holds two of: sold.
        if ((sim::devilsEye(row) || sim::devilsKey(row)) && (!invitationHalfWanted(one) || vaultHolds(one) >= 2)) {
            return false;
        }
        // A castle's Scroll or Bone the vault already holds two of: sold. A wizard's vault filled
        // with 28 Bones and 23 Scrolls and he went to it 313 times.
        if ((sim::scrollOfArchangel(row) || sim::bloodBone(row)) && !cloakHalfWanted(one, false) &&
            vaultHolds(one) >= 2) {
            return false;
        }
        // The pet he does not keep, once the vault holds one: sold (nine Angels piled up there).
        if (helper(row, kAngel) && wantedPet() != kAngel && vaultHolds(one) >= 1) return false;
        // A reward waiting on room: the healing potions smaller than he drinks go to the counter.
        if ((crowded_ || boxStuck_) && potionTier(row) >= 0 && potionTier(row) < healTier()) return false;
        // A second of a pet or mount is sold -- the helpers alone: a cloak, scroll or bone is
        // group 13 too, and a Cloak +1 was sold beside a +2 four times in a day, a Chaos each.
        if (anyHelper(row)) {
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
               sim::restores(row) || sim::ammunition(row) || (row.group == sim::kGroupPets && !sim::jewellery(row)) ||
               castleTicket(row) || squareTicket(row);
    }

    // ---- Blood Castle (sim/event.h, docs/blood-castle-port.md) -----------------------------
    // The user, 2026-10-08: "ideas is that bots are doing quests and BC". A Scroll of Archangel
    // and a Blood Bone of one level, with a Chaos, make the Invisibility Cloak of that level at the
    // Chaos Goblin; with a cloak he may use, he is at the Messenger in Devias when the hourly door
    // opens, and inside he plays the run: the court's wait, the road's kills until the bridge
    // falls, the Spirit Sorcerers, the Statue of Saint, its weapon picked up and carried to the
    // Archangel, and the win claimed.
    static constexpr int kDevias = 2;
    static constexpr int kMessenger = 233;
    static bool castleTicket(const content::ItemRow& row) {
        return sim::scrollOfArchangel(row) || sim::bloodBone(row) || sim::invisibilityCloak(row) ||
               sim::archangelWeapon(row);
    }
    int folkNumbered(int number) const {
        for (size_t i = 0; i < tables_->folk.size(); ++i) {
            if (tables_->folk[i].number == number) return int(i);
        }
        return -1;
    }
    // The castle he would go into: the highest his cloaks open whose floor he has reached and
    // whose garrison he takes on as he would out in the world (takes), or 0. A knight let in at
    // level 54 killed three Chief Skeletons in three minutes and died there.
    int castleChoice() const {
        // His party's castle, when he can go in: they fight its garrison together.
        if (const int c = lead_.castle; c > 0 && (castlesOpen() & (1 << c))) return c;
        for (int c = sim::kCastlesBuilt; c >= 1; --c) {
            if (realm_->cloakSlot(c) >= 0 && realm_->hero().level >= sim::kCastleBands[c - 1][0] &&
                castleTaken(c)) {
                return c;
            }
        }
        return 0;
    }
    bool castleTaken(int c) const {
        const content::Tables* t = const_cast<Bot*>(this)->world(int(sim::kBloodCastleMap));
        if (!t) return false;
        // BOT_CASTLE_SHARE: what share of his health a garrison kill may cost (1.0 = all of it),
        // for the castle-2-and-up A/B of 2026-10-10; unset, his own riskShare.
        const double share = std::getenv("BOT_CASTLE_SHARE") ? std::atof(std::getenv("BOT_CASTLE_SHARE")) : 0.0;
        for (int k = 0; k < 5; ++k) {  // the garrison, the sorcerer aside (kCastleBreeds)
            for (const content::MonsterKind& kind : t->kinds) {
                if (kind.number == sim::kCastleBreeds[c - 1][k] && !takes(kind, share)) return false;
            }
        }
        return true;
    }
    // Whether it is time to be at the Messenger: the door open now, or opening within the walk.
    bool castleDue() const {
        if (castleChoice() <= 0 || realm_->wallClock() <= 0) return false;
        // The day's second as the realm reckons it (Realm::castleRefusal).
        const time_t at = time_t(realm_->wallClock());
        struct tm local {};
        localtime_r(&at, &local);
        const int day = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
        const sim::GateClock& door = realm_->config().castle;
        if (door.entryLeft(day) > 0) return true;
        return door.period - door.phase(day) <= 8 * 60;
    }
    // In Devias: by the Messenger until the door opens, then through it.
    void messenger() {
        const int c = castleChoice();
        const int folk = folkNumbered(kMessenger);
        if (c <= 0 || folk < 0) {
            nextAim_ = clock_;
            return;
        }
        if (realm_->castleRefusal(c) == sim::CastleRefusal::None) {
            if (realm_->gating() == folk) {
                if (hand_.enterCastle(c)) {
                    say("** gives the Messenger his cloak: into Blood Castle %d", c);
                    ++out_.castles;
                    if (out_.firstCastleTick < 0) out_.firstCastleTick = clock_;
                }
                return;
            }
            sim::Request request;
            request.kind = sim::Request::Kind::Talk;
            request.target = uint32_t(folk);
            hand_.ask(request);
            return;
        }
        int column = 0, row = 0;
        const sim::Body& hero = realm_->hero();
        if (!hero.walking && realm_->folkTile(folk, &column, &row) &&
            std::hypot(float(column) - hero.x, float(row) - hero.y) > 4.0f) {
            sim::Request request;
            request.kind = sim::Request::Kind::WalkTo;
            request.column = column;
            request.row = row + 2;
            hand_.ask(request);
        }
    }
    // The level of a Scroll and Bone pair he can make a cloak of now -- a castle he may enter, a
    // Chaos to spend and the Zen over his potions, no such cloak already -- or 0.
    int cloakWorth() const {
        if (realm_->hero().level < sim::kCloakFromLevel || boxHeld()) return 0;
        bool chaosHeld = false;
        int scrolls = 0, bones = 0;  // a bit a level
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            const content::ItemRow& row = rowOf(one);
            if (chaos(row)) chaosHeld = true;
            if (one.refinement < 1 || one.refinement > sim::kCloakMostLevel) continue;
            if (sim::scrollOfArchangel(row)) scrolls |= 1 << one.refinement;
            if (sim::bloodBone(row)) bones |= 1 << one.refinement;
        }
        if (!chaosHeld) return 0;
        // A Chaos held back for a rune to save (holdsGoodRune).
        if (chaosCount() <= runesWaiting()) return 0;
        for (int level = sim::kCloakMostLevel; level >= 1; --level) {
            if (!(scrolls & bones & (1 << level))) continue;
            if (level > sim::kCastlesBuilt || realm_->hero().level < sim::kCastleBands[level - 1][0]) continue;
            // **Only for a castle he would go into** (2026-10-10): a wizard of seed 1 made a +2
            // cloak at 9:48 for a castle whose garrison he did not reckon he could take, held it,
            // and went to no castle until 18:08 -- castleChoice looks for the cloak of the castle
            // it picks. The +2 parts wait; the +1 make the cloak he will use.
            if (!castleTaken(level)) continue;
            if (realm_->cloakSlot(level) >= 0) continue;
            if (realm_->money() < sim::kCloakZen[level] + potionReserve()) continue;
            return level;
        }
        return 0;
    }
    // At the Goblin's box: the scroll, the bone and a Chaos in, the Cloak mixed, everything out.
    void makeCloak() {
        const int level = cloakWorth();
        if (level <= 0) return;
        int scroll = -1, bone = -1, jewel = -1;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            const content::ItemRow& row = rowOf(one);
            if (scroll < 0 && sim::scrollOfArchangel(row) && one.refinement == level) scroll = slot;
            if (bone < 0 && sim::bloodBone(row) && one.refinement == level) bone = slot;
            if (jewel < 0 && chaos(row)) jewel = slot;
        }
        if (scroll < 0 || bone < 0 || jewel < 0) return;
        if (std::getenv("BOT_MIX")) {
            std::printf("MIX cloak puts: scroll %d bone %d jewel %d atMachine-mixing %d machine-empty %d\n", scroll, bone, jewel,
                        realm_->mixing(), realm_->machine().empty());
        }
        const bool in = hand_.putIn(scroll) >= 0 && hand_.putIn(bone) >= 0 && hand_.putIn(jewel) >= 0;
        if (std::getenv("BOT_MIX")) {
            const sim::Judged j = realm_->judged(sim::Service::Cloak);
            std::printf("MIX cloak +%d: in %d ready %d rate %d zen %lld recipe %d money %lld\n", level, in, j.ready, j.rate,
                        (long long)j.zen, int(j.recipe), (long long)realm_->money());
            for (int cell = 0; cell < sim::kMachineCells; ++cell) {
                const sim::Held& c = realm_->machine()[cell];
                if (!c.empty()) std::printf("   cell %d: %s +%d\n", cell, rowOf(c).label.c_str(), int(c.refinement));
            }
        }
        const bool mixed = in && hand_.mix(sim::Service::Cloak);
        if (std::getenv("BOT_MIX")) {
            for (int cell = 0; cell < sim::kMachineCells; ++cell) {
                const sim::Held& c = realm_->machine()[cell];
                if (!c.empty()) std::printf("   after: cell %d: %s +%d\n", cell, rowOf(c).label.c_str(), int(c.refinement));
            }
        }
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm_->machine()[cell].empty()) hand_.takeOut(cell);
        }
        if (mixed) {
            say("%s an Invisibility Cloak +%d at the Chaos Goblin", realm_->cloakSlot(level) >= 0 ? "** makes" : "fails to make",
                level);
        }
    }
    void talkTo(int folk) {
        sim::Request request;
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(folk);
        hand_.ask(request);
    }
    // Inside: the run, as a player plays it alone.
    void castle() {
        const sim::CastleRun& run = realm_->castleRun();
        const sim::Body& hero = realm_->hero();
        mode_ = Mode::Hunt;
        if (run.sentOut) return;
        const int angel = folkNumbered(sim::kArchangel);
        if (run.phase != lastPhase_) {
            const char* phases[] = {"", "waits in the court", "the run begins", "the run is over", "** Blood Castle won"};
            say("Blood Castle %d: %s (%d kills, %d sorcerers)", run.castle, phases[int(run.phase)], run.kills,
                run.sorcerers);
            if (run.phase == sim::CastlePhase::Won) ++out_.castleWins;
            lastPhase_ = run.phase;
        }
        if (run.phase == sim::CastlePhase::Won) {
            if (run.claimed || angel < 0) return;
            if (realm_->angeling() == angel) {
                if (hand_.claimCastle()) say("claims the win: to Devias");
            } else {
                talkTo(angel);
            }
            return;
        }
        if (run.phase != sim::CastlePhase::Running) return;
        if (std::getenv("BOT_CASTLE") && clock_ % (30 * 20) < kThink) {
            std::printf("CASTLE %s c%d kills %d/%d sorcerers %d bridge %d hp %d/%d at %.0f,%.0f on %u staff %d\n",
                        clock(clock_).c_str(), run.castle, run.kills, run.killsWanted, run.sorcerers, int(run.bridgeDown),
                        hero.health, hero.maxHealth, hero.x, hero.y, castleOn_, realm_->staffSlot());
        }
        // One worn before this rule: into the bag, where the hand-in finds it.
        for (int slot : {int(sim::kWeaponRight), int(sim::kWeaponLeft)}) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty() || !sim::archangelWeapon(rowOf(one))) continue;
            for (int to = sim::kWorn; to < sim::kSlots; ++to) {
                if (realm_->satchel()[to].empty() && hand_.moveItem(slot, to)) break;
            }
            return;
        }
        // The weapon in his bag: to the Archangel with it.
        if (realm_->staffSlot() >= 0 && angel >= 0) {
            if (realm_->angeling() == angel) {
                if (hand_.handInStaff()) say("** gives the Archangel his weapon back");
            } else {
                talkTo(angel);
            }
            return;
        }
        // The weapon on the ground: his, or anyone's. **Room made for it first** (2026-10-10):
        // the take is refused with no room at its footprint (Realm::take), the realm then drops
        // the pick, and a knight of level 210 stood on the Divine Sword at 14,95 for seven
        // minutes with a full bag while the run timed out -- six of his ten castle runs went so
        // (the seed-2 trace), and every class's. No counter in the castle, so the cheapest block
        // of the bag it would cover is thrown on the ground (clearFor's discard).
        for (const sim::Lying& one : realm_->lying()) {
            if (one.what.empty() || !sim::archangelWeapon(rowOf(one.what))) continue;
            if (one.owner != 0 && one.owner != hero.id) continue;
            if (const content::ItemRow& row = rowOf(one.what); realm_->satchel().free(*tables_, row.width, row.height) < 0) {
                if (clearFor(row, true) > 0) say("throws things out of the bag for the %s", row.label.c_str());
            }
            if (std::getenv("BOT_CASTLE") && clock_ % (15 * 20) < kThink) {
                std::printf("CASTLE %s weapon lying at %d,%d id %u barred %d hero %.1f,%.1f walking %d order %d\n",
                            clock(clock_).c_str(), one.column, one.row, one.id, int(barred(one.id)), hero.x, hero.y,
                            int(hero.walking), int(realm_->order().kind));
            }
            sim::Request request;
            request.kind = sim::Request::Kind::Pick;
            request.target = one.id;
            ask(request);
            return;
        }
        // Then the fight: what is on him; else the statue, the sorcerers once the bridge is down,
        // and the garrison -- nearest first within each. Once the bridge is down the statue and
        // the sorcerers come before what is on him: the garrison rises again, so something always
        // is, and a knight stood at the court's mouth with the bridge down for seven minutes.
        uint32_t target = 0;
        int rank = 9;
        float closest = 1e30f;
        // Whether the statue stands: the run's last fight, far from the court the garrison rises in.
        bool statueUp = false;
        for (const sim::Body& body : realm_->bodies()) {
            statueUp |= body.monster() && body.alive() && body.kind >= 0 &&
                        sim::castleStatue(tables_->kinds[size_t(body.kind)].number);
        }
        for (const sim::Body& body : realm_->bodies()) {
            if (!body.monster() || !body.alive() || barred(body.id) || body.kind < 0) continue;
            // **Not one on safe ground** (2026-10-10): the realm refuses a blow at a body on a
            // safe tile (Realm's `sheltered`), and the court he waits in is one. A Chief Skeleton
            // Warrior stood on the wizard's own tile there, struck him, and was answered for the
            // whole fifteen minutes with nothing landing: 0 kills, the run lost.
            if (tables_->grid.safe(body.column(), body.row())) continue;
            const int number = tables_->kinds[size_t(body.kind)].number;
            const float d = (body.x - hero.x) * (body.x - hero.x) + (body.y - hero.y) * (body.y - hero.y);
            int r = 4;
            if (sim::castleStatue(number)) r = 0;
            else if (sim::castleSorcerer(number)) {
                if (!run.bridgeDown) continue;
                r = 1;
            } else if (body.quarry == hero.id && d < 36.0f) {
                // Under 60% he answers it first even past the bridge: a knight whose back was to
                // the Red Skeleton Knights while he cut at a sorcerer died in fifteen seconds.
                // **A caster at four fifths, while the statue stands** (2026-10-10): a wizard of 426
                // health cut at it from nine tiles with the Red Skeleton Knights on him, went from
                // 63% to dead in five seconds, and lost the run. Not before the statue: in the court
                // the garrison rises without end, and a caster who answered it there stood until
                // the run timed out.
                const int hold = wizardly() && statueUp ? 8 : 6;
                r = run.bridgeDown && hero.health * 10 >= hero.maxHealth * hold ? 2 : -2;
            }
            // Of one rank, the one he is on, then the nearest: a sorcerer has 3,700 health, and
            // turning to the nearest each think left every one of them half dead.
            const float near = body.id == castleOn_ ? -1.0f : d;
            if (r < rank || (r == rank && near < closest)) {
                rank = r;
                closest = near;
                target = body.id;
            }
        }
        if (std::getenv("BC_TRACE") && clock_ % (20 * 15) < kThink) {
            const sim::Body* t = bodyOf(target);
            std::printf("BC %s at %.1f,%.1f hp %d/%d kills %d sorc %d bridge %d target %u rank %d %s at %.1f,%.1f walking %d\n",
                        clock(clock_).c_str(), hero.x, hero.y, hero.health, hero.maxHealth, run.kills, run.sorcerers,
                        run.bridgeDown, target, rank, t ? tables_->kinds[size_t(t->kind)].label.c_str() : "-",
                        t ? t->x : 0.f, t ? t->y : 0.f, hero.walking);
            if (t) std::printf("   landed %d missed %d target hp %d/%d quarry %u order %d floor %d/%d\n", tally_.landed,
                               tally_.missed, t->health, t->maxHealth, hero.quarry, int(realm_->order().kind),
                               realm_->floorAt(hero.column(), hero.row()), realm_->floorAt(t->column(), t->row()));
        }
        if (target == 0) return;
        sim::Request request;
        request.kind = sim::Request::Kind::Attack;
        request.target = target;
        castleOn_ = target;
        if (rank <= 1) {
            // The run's own: never given up for taking long (ask's kGiveUp), only for dying.
            if (wizardly()) request.skill = quickSpell();
            hand_.ask(request);
        } else {
            ask(request);
        }
        if (guard()) return;
        press(target);
    }
    sim::CastlePhase lastPhase_ = sim::CastlePhase::None;
    uint32_t castleOn_ = 0;  // the statue or sorcerer he is on

    // ---- Devil Square (sim/square.h, docs/devil-square-port.md) ----------------------------
    // The handoff's step 4 (the user, 2026-10-10: 'All, in order'): a Devil's Eye and Key of one
    // level, with a Chaos, make the Devil's Invitation at the Goblin; with one he may use, he is at
    // Charon in Noria when the two-hourly gate opens, and inside he fights the waves to the end,
    // then claims his row.
    static constexpr int kNoria = 3;
    static bool squareTicket(const content::ItemRow& row) {
        return sim::devilsEye(row) || sim::devilsKey(row) || sim::devilsInvitation(row);
    }
    int squareBandLevel() const {
        return sim::squareLevel(realm_->hero().level, realm_->hero().kin == sim::Kin::MagicGladiator);
    }
    bool inSquareBand(int s) const {
        const int level = squareBandLevel();
        const int* band = sim::kSquareBands[s - 1];
        return level >= band[0] && (band[1] == 0 || level <= band[1]);
    }
    // The square he would go into: the one his band holds, built, whose waves he takes on as he
    // would out in the world, with an invitation for it -- or 0.
    int squareChoice() const {
        // His party's square, when he can go in: MU's numbers are a party's (decision 8).
        if (const int s = lead_.square; s > 0 && (squaresOpen() & (1 << s))) return s;
        const int s = sim::squareFor(squareBandLevel());
        if (s > sim::kSquaresBuilt || !inSquareBand(s) || realm_->invitationSlot(s) < 0 || !squareTaken(s)) return 0;
        return s;
    }
    // Every wave's breeds at his whole health a kill (his party's scaling in takes): the square
    // keeps MU's numbers whoever is in it (the user, 2026-10-10: 'Keep MU's numbers', decision 8),
    // and a knight of 77 and 1,000 health, let in alone on the first wave's alone, died 79 seconds
    // into it. BOT_SQUARE_SHARE moves the line.
    bool squareTaken(int s) const {
        const content::Tables* t = const_cast<Bot*>(this)->world(int(sim::kDevilSquareMap));
        if (!t) return false;
        const double share = std::getenv("BOT_SQUARE_SHARE") ? std::atof(std::getenv("BOT_SQUARE_SHARE")) : 1.0;
        for (int w = 0; w < sim::kSquareWaves; ++w) {
            for (int k = 0; k < 2; ++k) {
                for (const content::MonsterKind& kind : t->kinds) {
                    if (kind.number == sim::kSquareBreeds[s - 1][w][k] && !takes(kind, share)) return false;
                }
            }
        }
        return true;
    }
    // Whether it is time to be at Charon: the gate open now, or opening within the walk.
    bool squareDue() const {
        if (std::getenv("BOT_SQUARE") && clock_ - squareTraced_ > 20 * 60 * 20) {
            const_cast<Bot*>(this)->squareTraced_ = clock_;
            const int s = sim::squareFor(squareBandLevel());
            std::printf("SQUARE %s level %d band %d in %d slot %d taken %d:", clock(clock_).c_str(), realm_->hero().level, s,
                        int(inSquareBand(s)), realm_->invitationSlot(s), int(squareTaken(s)));
            const content::Tables* t = const_cast<Bot*>(this)->world(int(sim::kDevilSquareMap));
            for (int w = 0; t && w < sim::kSquareWaves; ++w) {
                for (int k = 0; k < 2; ++k) {
                    for (const content::MonsterKind& kind : t->kinds) {
                        if (kind.number == sim::kSquareBreeds[s - 1][w][k] && !takes(kind, 0.0)) {
                            std::printf(" %s(L%d cost %.0f fear %d)", kind.label.c_str(), kind.level, costOf(kind),
                                        int(fearUntil_.count(kind.number) && fearUntil_.at(kind.number) > clock_));
                        }
                    }
                }
            }
            std::printf("\n");
        }
        return squareChoice() > 0 && squareGateSoon();
    }
    // Whether Charon's gate is open now, or opens within the walk.
    bool squareGateSoon() const {
        if (realm_->wallClock() <= 0) return false;
        const time_t at = time_t(realm_->wallClock());
        struct tm local {};
        localtime_r(&at, &local);
        const int day = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
        const sim::GateClock& gate = realm_->config().square;
        if (gate.entryLeft(day) > 0) return true;
        return gate.period - gate.phase(day) <= 8 * 60;
    }
    // In Noria: by Charon until the gate opens, then through it.
    void charon() {
        const int s = squareChoice();
        const int folk = folkNumbered(sim::kCharon);
        if (s <= 0 || folk < 0) {
            nextAim_ = clock_;
            return;
        }
        if (realm_->squareRefusal(s) == sim::SquareRefusal::None) {
            if (realm_->gating() == folk) {
                if (hand_.enterSquare(s)) say("** gives Charon his invitation: into Devil Square %d", s);
                return;
            }
            talkTo(folk);
            return;
        }
        int column = 0, row = 0;
        const sim::Body& hero = realm_->hero();
        if (!hero.walking && realm_->folkTile(folk, &column, &row) &&
            std::hypot(float(column) - hero.x, float(row) - hero.y) > 4.0f) {
            sim::Request request;
            request.kind = sim::Request::Kind::WalkTo;
            request.column = column;
            request.row = row + 2;
            hand_.ask(request);
        }
    }
    // Whether an Eye or Key is half of an invitation he would make: a square of its level is one
    // he may enter (or will, at +4), and no such invitation already.
    bool invitationHalfWanted(const sim::Held& one) const {
        const int level = one.refinement;
        if (level < 1 || level > sim::kInvitationMostLevel) return false;
        if (realm_->invitationSlot(level) >= 0) return false;
        return level <= sim::kSquaresBuilt && squareBandLevel() <= (sim::kSquareBands[level - 1][1] == 0
                                                                       ? 100000
                                                                       : sim::kSquareBands[level - 1][1]);
    }
    // The level of an Eye and Key pair he can make an invitation of now -- his square's, a Chaos
    // to spend and the Zen over his potions, no invitation for it already -- or -1.
    int invitationWorth() const {
        if (realm_->hero().level < sim::kInvitationFromLevel || boxHeld()) return -1;
        const int s = sim::squareFor(squareBandLevel());
        if (s > sim::kSquaresBuilt || !inSquareBand(s) || realm_->invitationSlot(s) >= 0) return -1;
        bool eye = false, key = false;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty() || one.refinement != s) continue;
            const content::ItemRow& row = rowOf(one);
            eye = eye || sim::devilsEye(row);
            key = key || sim::devilsKey(row);
        }
        if (!eye || !key || chaosCount() <= runesWaiting()) return -1;
        if (realm_->money() < sim::kInvitationZen[s] + potionReserve()) return -1;
        return s;
    }
    // At the Goblin's box: the eye, the key and a Chaos in, the invitation mixed, everything out.
    void makeInvitation() {
        const int level = invitationWorth();
        if (level < 0) return;
        int eye = -1, key = -1, jewel = -1;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (one.empty()) continue;
            const content::ItemRow& row = rowOf(one);
            if (eye < 0 && sim::devilsEye(row) && one.refinement == level) eye = slot;
            if (key < 0 && sim::devilsKey(row) && one.refinement == level) key = slot;
            if (jewel < 0 && chaos(row)) jewel = slot;
        }
        if (eye < 0 || key < 0 || jewel < 0) return;
        const bool in = hand_.putIn(eye) >= 0 && hand_.putIn(key) >= 0 && hand_.putIn(jewel) >= 0;
        const bool mixed = in && hand_.mix(sim::Service::Invitation);
        for (int cell = 0; cell < sim::kMachineCells; ++cell) {
            if (!realm_->machine()[cell].empty()) hand_.takeOut(cell);
        }
        if (mixed) {
            say("%s a Devil's Invitation +%d at the Chaos Goblin",
                realm_->invitationSlot(level) >= 0 ? "** makes" : "fails to make", level);
        }
    }
    // Inside: the waves, the bosses first once up, what is on him first when he is low; never
    // after anything outside his square's arena. At the end, his rank said and his row claimed.
    void square() {
        const sim::SquareRun& run = realm_->squareRun();
        const sim::Body& hero = realm_->hero();
        mode_ = Mode::Hunt;
        if (run.phase != lastSquarePhase_) {
            const char* phases[] = {"", "waits for the fight", "the fight begins", "time is up"};
            say("Devil Square %d: %s (%lld points)", run.square, phases[int(run.phase)],
                (long long)realm_->squarePoints());
            if (run.phase == sim::SquarePhase::Running) ++out_.squares;
            lastSquarePhase_ = run.phase;
        }
        if (run.phase == sim::SquarePhase::Ended) {
            if (realm_->squarePaid() || run.sentOut) return;
            std::vector<sim::Realm::SquareResult> rows;
            realm_->squareResults(&rows);
            for (const auto& one : rows) {
                if (!one.mine) continue;
                if (hand_.claimSquare()) {
                    say("** Devil Square %d: rank %d of %zu, %lld points, %lld experience and %lld Zen", run.square,
                        one.rank, rows.size(), (long long)one.points, (long long)one.experience, (long long)one.zen);
                    out_.squarePoints += one.points;
                    if (out_.squareBestRank == 0 || one.rank < out_.squareBestRank) out_.squareBestRank = one.rank;
                }
            }
            return;
        }
        if (run.phase != sim::SquarePhase::Running) return;
        uint32_t target = 0;
        int rank = 9;
        float closest = 1e30f;
        const bool low = hero.health * 10 < hero.maxHealth * 6;
        for (const sim::Body& body : realm_->bodies()) {
            if (!body.monster() || !body.alive() || barred(body.id) || body.kind < 0) continue;
            if (!sim::inSquareArena(run.square, body.column(), body.row())) continue;
            const int number = tables_->kinds[size_t(body.kind)].number;
            const float d = (body.x - hero.x) * (body.x - hero.x) + (body.y - hero.y) * (body.y - hero.y);
            int r = 4;
            if (body.quarry == hero.id && d < 36.0f) r = low ? -2 : 2;
            else if (sim::squareWaveOf(run.square, number) == sim::kSquareWaves + 1) r = 1;
            const float near = body.id == squareOn_ ? -1.0f : d;
            if (r < rank || (r == rank && near < closest)) {
                rank = r;
                closest = near;
                target = body.id;
            }
        }
        if (target == 0) return;
        sim::Request request;
        request.kind = sim::Request::Kind::Attack;
        request.target = target;
        squareOn_ = target;
        ask(request);
        if (guard()) return;
        press(target);
    }
    sim::SquarePhase lastSquarePhase_ = sim::SquarePhase::None;
    uint32_t squareOn_ = 0;
    int64_t squareTraced_ = -1000000;

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
    // **Against the breed's defence** (`defense`): a knight's quick swing of 302 every half
    // second outran his Rageful Blow and Twisting Slash of 780 on a bare count, and against a
    // Phantom Knight's 425 it lands 1 -- every Icarus breed read as costing him millions.
    void attack(double* hit, int32_t* ticks, double defense = 0.0) const {
        const sim::Body& hero = realm_->hero();
        const sim::Wearer w = realm_->wearer();
        *hit = blow();
        const int32_t swing = steadySwing();
        *ticks = std::max(1, swing);
        const auto landed = [&](double h) { return std::max(1.0, h - defense); };
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
            const int32_t t = std::max<int32_t>(1, row.wizardry ? cast : std::max(cast, swing));
            if (landed(one) / t > landed(*hit) / *ticks) {
                *hit = one;
                *ticks = t;
            }
        }
    }
    // **His swing as it stays** (the Claude bot's runs, 2026-10-09): what his arms, agility and
    // excellent options give, without a Frenzy rune's stacks or an Ale, which a fight builds and
    // a trip or a rest lets go. Read as it stood, an elf's swing went 10 ticks to 14 with her
    // stacks on landing, The Last Contract's breed cost 429 then 600 of her 465, and she paid
    // the Lost Tower and the Dungeon back and forth every thirty seconds: 1,022 trips and 3.6
    // million Zen in a day, ending with 906.
    int32_t steadySwing() const {
        const sim::Body& hero = realm_->hero();
        const auto arm = [&](int32_t at) -> const content::Arm* {
            return at >= 0 && size_t(at) < tables_->arms.size() ? &tables_->arms[size_t(at)] : nullptr;
        };
        const int32_t ticks = sim::swingTicks(sim::swingMilliseconds(
            *tables_, hero.kin, hero.totalPoints().agility, arm(hero.weapon), arm(hero.shield), hero.excel.speed));
        return ticks > 0 ? ticks : hero.swingTicks;
    }
    // The ticks a kill of this breed takes him, alone.
    double killTicks(const content::MonsterKind& kind) const {
        double hit = 0.0;
        int32_t ticks = 1;
        attack(&hit, &ticks, kind.defense);
        const double landed = std::max(1.0, hit - kind.defense) *
                              hitChance(realm_->hero().stats.attackRate, float(kind.defenseRate));
        return kind.health / landed * ticks;
    }
    double costOf(const content::MonsterKind& kind) const {
        const sim::Body& hero = realm_->hero();
        const double seconds = killTicks(kind) / 20.0;
        // And what his guard takes off a blow, as he keeps it up (guardSeen_): Defense, Soul
        // Barrier, Greater Defense.
        const double taken =
            std::max(0.0, (kind.minimumDamage + kind.maximumDamage) / 2.0 - hero.stats.defense) *
            hitChance(float(kind.attackRate), hero.stats.defenseRate) * guardSeen_;
        return reckoned() * caution_ * taken * seconds * 20.0 / std::max(1, kind.attackTicks);
    }
    // **Half of it for the elf and the gladiator** (the user, 2026-10-09: "we need to teach bots
    // to farm better monsters"): costOf counts the breed on him for the whole kill, one at a time,
    // with no first shots from range and no summon taking the blows -- so at level 150-230 they
    // made 3-21% of their kills on breeds of level 50 and over, where the Jewel of Life falls,
    // and none made wings in a day. Halved, four seeds of 24 h offline each: the elf
    // 166/172/194/178 -> 221/230/196/252, wings on all four (none before); the gladiator
    // 157/220/219/175 -> 222/230/221/229, deaths 139 -> 79. Not the knight (213/202/213/210 ->
    // 225/212/191/212, deaths 44 -> 79) nor the wizard (died three times as often, no higher):
    // theirs stays whole.
    // Not the energy elf (Options::support): her bow is an energy build's, and halved she took
    // Noria's Agons and Lorencia's Skeleton Warriors alone and died 255 times in 18 game hours.
    double reckoned() const {
        if (options_.support) return 1.0;
        return options_.kin == sim::Kin::FairyElf || (options_.kin == sim::Kin::MagicGladiator && !wizardly()) ? 0.5
                                                                                                                 : 1.0;
    }
    // Whether he takes this breed on: a kill costs him under a third of his health (a half to
    // keep at a quest already under way, or while he can pay for the potions: riskShare), and it
    // has not killed him twice lately.
    bool takes(const content::MonsterKind& kind, double share = 0.0) const {
        if (share <= 0.0) share = riskShare();
        const auto fear = fearUntil_.find(kind.number);
        if (fear != fearUntil_.end() && fear->second > clock_) return false;
        return costOf(kind) * partyScale_ * share <= realm_->hero().maxHealth;
    }
    // **What share of his health a kill may cost** (the user, 2026-10-03: "its wierd that bots
    // are not ending with good gears", then "push to harder maps"): a third, and for the wizard
    // a half while his purse holds twenty potions of his tier -- he drinks his way down, as a
    // player does. Three seeds of 8 h: the wizard's gear went from drop level 26-30 to 31-39 with
    // it; the knight, who drinks through melee, lost 6-10 levels and gained nothing, and the elf
    // was the same either way, so theirs stays a third. Off the purse alone, so the choice does
    // not flicker as he drinks his bag down.
    double riskShare() const {
        const bool rich = realm_->money() - spent_ >= 20 * potionPrice(healTier());
        return wizardly() && rich ? 2.0 : 3.0;
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
    // The highest drop level of a piece his class wears that can come to him: what falls from the
    // strongest breed he takes on any world he can reach, or stands on a shelf there (spend's
    // strength). Kept a game minute, and never lower than it was: his strength is spent for good.
    int findableLevel() {
        if (findableAt_ >= 0 && clock_ - findableAt_ < 60 * 20) return findable_;
        findableAt_ = clock_;
        int found = findable_;
        for (const WorldRow& w : kWorlds) {
            if (w.map == int(sim::kBloodCastleMap) || w.map == int(sim::kDevilSquareMap)) continue;
            const content::Tables* t = world(w.map);
            if (!t || !reachable(w.map)) continue;
            found = std::max(found, bestOn(*t));
            for (const content::Townsperson& f : t->folk) {
                int count = 0;
                const sim::Offer* shelf = sim::stockOf(f.number, &count);
                for (int i = 0; i < count; ++i) {
                    for (const content::ItemRow& row : t->items) {
                        if (row.group != shelf[i].group || row.number != shelf[i].number || !row.armour()) continue;
                        if (row.classes != 0 && (row.classes & (1 << int(options_.kin))) == 0) continue;
                        found = std::max(found, row.dropLevel + 3 * shelf[i].refinement);
                    }
                }
            }
        }
        findable_ = found;
        return findable_;
    }
    int findable_ = 0;
    int64_t findableAt_ = -1;

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
    // **A treasure to find** (Sevina's second part: the Broken Sword, the Soul of Wizard, the Tear
    // of Elf): a Find step not yet done. It falls on 2 kills in 100 anywhere in Atlans or on the
    // Lost Tower's top floor (Realm::treasureGround), so he hunts Atlans for it.
    bool seeks(int q) const {
        const sim::QuestRow& row = sim::questAt(q);
        for (int s = 0; s < row.stepCount; ++s) {
            if (row.steps[s].kind == sim::QuestStepKind::Find && row.steps[s].item &&
                realm_->quest(q).counts[s] < row.steps[s].count) return true;
        }
        return false;
    }
    bool hasFind(int q) const {
        const sim::QuestRow& row = sim::questAt(q);
        for (int s = 0; s < row.stepCount; ++s) {
            if (row.steps[s].kind == sim::QuestStepKind::Find && row.steps[s].item) return true;
        }
        return false;
    }
    // Where he looks for it: Atlans, when he takes something there.
    int seekMap() {
        const content::Tables* t = world(7);
        return t && reachable(7) && bestOn(*t) >= 0 ? 7 : -1;
    }
    // An item a quest under way asks him to find: kept whatever a counter would give.
    bool questItem(const sim::Held& one) const {
        if (one.empty()) return false;
        for (int q = 0; q < sim::kQuests; ++q) {
            const sim::QuestState state = realm_->quest(q).state;
            if (state != sim::QuestState::Active && state != sim::QuestState::Ready) continue;
            const sim::QuestRow& row = sim::questAt(q);
            for (int s = 0; s < row.stepCount; ++s) {
                if (row.steps[s].kind == sim::QuestStepKind::Find && row.steps[s].item &&
                    tables_->itemNamed(row.steps[s].item) == one.item) return true;
            }
        }
        return false;
    }
    // Where a breed lives, among the worlds: the first map with a nest of it, or -1.
    // Kept once found: the tables never change, and the party's board asks it of every breed of
    // every bot's quests each second.
    int homeOf(int breed) {
        if (const auto known = homes_.find(breed); known != homes_.end()) return known->second;
        int home = -1;
        for (const WorldRow& w : kWorlds) {
            const content::Tables* t = world(w.map);
            if (!t || home >= 0) continue;
            for (const content::MonsterNest& nest : t->nests) {
                if (t->kinds[nest.kind].number == breed) home = w.map;
            }
        }
        return homes_[breed] = home;
    }
    std::map<int, int> homes_;
    const content::MonsterKind* kindOf(int map, int breed) {
        const content::Tables* t = world(map);
        if (!t) return nullptr;
        for (const content::MonsterKind& k : t->kinds) {
            if (k.number == breed) return &k;
        }
        return nullptr;
    }
    // Which map a townsperson of this number stands on, or -1.
    int folkMap(int number) {
        for (const WorldRow& w : kWorlds) {
            const content::Tables* t = world(w.map);
            if (!t) continue;
            for (const content::Townsperson& f : t->folk) {
                if (f.number == number) return w.map;
            }
        }
        return -1;
    }
    // **A quest handed in to someone else** (the user, 2026-10-08: "atlans quest giver is on
    // noria, starting at lvl 60 i think"): Peia's The Drowned Song is taken in Noria at level 70
    // and handed in to Lirien in Atlans, and asks nothing but finding her -- no breed to hunt, so
    // the brain never took it, and Lirien's chain behind it never opened. Where it is handed in.
    int handInMap(int q) { return folkMap(sim::questReceiver(sim::questAt(q))); }
    // A quest whose only step is to find whom it is handed in to.
    bool errand(int q) const { return sim::questElsewhere(sim::questAt(q)) && wanted(q).empty(); }
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
    // `share` 0 is his own riskShare: a third, or a half for a wizard rich in potions -- the same
    // the grind allows him. At a fixed third a wizard of 376 health never began the Catacombs,
    // whose Ghosts cost him 154 a kill (2026-10-08).
    // **And with twenty healing potions on him, a quest's breed may cost him his whole health**
    // (the user, 2026-10-08: "there is also quests for atlans,tarkan"): Devin's White Silence asks
    // ten Ice Queens, each a full bar of health to every class at level 150-170, and it opens
    // Tersia's tower and the Atlans and Tarkan chains behind her -- held at a third or a half,
    // no bot of any class ever handed it in. He drinks through the fight, as a player does.
    int huntable(int q, std::vector<int>* breeds, double share = 0.0) {
        // Not a wizard's: on 360-480 health the Ice Queens killed him 38 times a day and he
        // ended 10-30 levels lower; the knight went from 168 to 182 with it.
        // Nor the energy elf's (Options::support): on 20 potions she drank through Peia's Stone
        // Golems and Agons at a full bar and died 90 times in five hours.
        if (share <= 0.0) share = !wizardly() && !options_.support && countOf(sim::heals) >= 20 ? 1.0 : riskShare();
        breeds->clear();
        if (aside_[q] > clock_) return -1;
        int where = -1;
        std::vector<int> here;
        for (const int breed : wanted(q)) {
            const int home = homeOf(breed);
            const content::MonsterKind* kind = home >= 0 ? kindOf(home, breed) : nullptr;
            // And the White Silence's breeds, its Elite Yetis and Ice Queens (2026-10-09): Devias's
            // quest pays the second weapon and opens the tower, Atlans and Tarkan behind it, and
            // held to his health bar no bot handed it in before level 121-184. Drinking through
            // them, three seeds of 30 h: the knight 95-98, the elf 114-120, the wizard 125-133.
            // Still later than a player (the user: "with elf it was very easy"): costOf counts
            // no summons tanking for her and no skill's multiplier.
            const bool drinks = kind && (home == kIcarus || q == sim::questOf(406));
            if (!kind || !(takes(*kind, share) || (drinks && drinksThrough(*kind))) || !reachable(home)) continue;
            if (const auto on = asideOn_.find({q, home}); on != asideOn_.end() && on->second > clock_) continue;
            if (where < 0) where = home;
            if (home == where) breeds->push_back(breed);
            if (home == map()) here.push_back(breed);
        }
        // The map he is on first, when it has any: a knight walked nine minutes to Atlans for
        // Sevina's trial and turned back for the Lost Tower the moment he arrived, all day.
        if (!here.empty()) {
            *breeds = here;
            return map();
        }
        return where;
    }

    // **A quest's breed fought through on his potions** (the user, 2026-10-08: "bots clear
    // Icarus"): a Large Healing Potion mends 30% of him every half second, and against that a
    // Drakan's 4,850 health over a 68-second kill is ten potions, not a death -- held to his one
    // bar, The Sky Door's Drakans and Queen Rainers were never hunted. Taken when the kill costs
    // no more than his health and a third of his potions' mending, and two of its blows through
    // his guard leave him above his drinking line. Icarus's breeds alone: let loose on every
    // quest's, the elves died 20 times a run against 6 and four of six missed Sevina in 60 hours.
    static constexpr int kIcarus = 10;
    bool drinksThrough(const content::MonsterKind& kind) const {
        const int potions = countOf(sim::heals);
        if (potions < 20) return false;
        const auto fear = fearUntil_.find(kind.number);
        if (fear != fearUntil_.end() && fear->second > clock_) return false;
        const sim::Body& hero = realm_->hero();
        const double blow = std::max(0.0, double(kind.maximumDamage) - hero.stats.defense) * guardSeen_;
        // Beside a party's tank its blows are his, not this one's.
        const bool shielded = partyScale_ < 1.0 && lead_.role != Role::Tank;
        if (!shielded && blow * 2.0 > hero.maxHealth * 0.5) return false;
        return costOf(kind) * partyScale_ <= hero.maxHealth + potions / 3.0 * potionMend();
    }
    // **Whether he takes a quest's breed on by himself**, huntable's test for one breed: his
    // riskShare, his whole bar for a knight or an elf with twenty potions, and Icarus's breeds and
    // the White Silence's drunk through.
    bool soloTakes(int q, const content::MonsterKind& kind, int home) const {
        const double share = !wizardly() && !options_.support && countOf(sim::heals) >= 20 ? 1.0 : riskShare();
        const bool drinks = home == kIcarus || q == sim::questOf(406);
        return takes(kind, share) || (drinks && drinksThrough(kind));
    }
    // **A kill's cost to him in a party** (docs/party.md): the party's blows shorten the kill by
    // its members, and the tank takes its blows -- a damage dealer's cost is half again under it,
    // and the elf's heals halve what is left. kTogether is what the board weighs a party by
    // before it is formed: three or more about him, a tank and an elf among them.
    static constexpr double kTogether = 0.25;
    double partyScale() const {
        const bool castle = (lead_.castle > 0 && map() == int(sim::kBloodCastleMap)) ||
                            (lead_.square > 0 && map() == int(sim::kDevilSquareMap));
        if ((lead_.map != map() && !castle) || lead_.size < 2) return 1.0;
        int here = 1;
        const sim::Body& hero = realm_->hero();
        for (const sim::Body& b : realm_->bodies()) {
            if (b.id == hero.id || !b.player || !b.alive() || hero.party == 0 || b.party != hero.party) continue;
            if (std::hypot(b.x - hero.x, b.y - hero.y) <= sim::kPartyReach * 2.0f) ++here;
        }
        if (here < 2) return 1.0;
        double scale = 1.0 / here;
        if (lead_.tank != 0 && lead_.role != Role::Tank) scale *= 0.5;
        if (lead_.healer && lead_.role != Role::Healer) scale *= 0.5;
        return std::max(scale, 0.05);
    }
    // What one of his healing potions mends, the largest he carries (Realm::useItem's rank in
    // tenths of his health and the flat part over it).
    double potionMend() const {
        int rank = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_->satchel()[slot];
            if (!one.empty() && sim::heals(rowOf(one))) rank = std::max(rank, rowOf(one).number);
        }
        const sim::Body& hero = realm_->hero();
        return hero.maxHealth * rank / 10.0 + std::max(0, (rank + 1) * 50 - hero.level);
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
        // **Sevina's class change next** (the user, 2026-10-08: "also bots need to finish 2nd
        // class quest"): table order put every repeatable quest before it, and a knight who
        // handed in her trial at hour 34 hunted The Knights' Halls and The Catacombs on their
        // twelve-hour rounds and never once looked for the Broken Sword in 26 hours.
        const auto sevina = [](int q) { return q == sim::kSevinaTrial || sim::questAt(q).promotes; };
        for (int q = 0; q < sim::kQuests; ++q) {
            if (q != home && q != other && sevina(q)) out.push_back(q);
        }
        for (int q = 0; q < sim::kQuests; ++q) {
            if (q != home && q != other && !sevina(q)) out.push_back(q);
        }
        return out;
    }

    void choose() {
        const Aim was = aim_;
        const int wasQuest = aimQuest_;
        // A giver he set out for is kept until he reaches him, ten minutes at most: from Atlans a
        // knight went to Devias to take The Broken Sword, chose The Drowned Halls in Atlans the
        // moment he landed, and paid both trips 520 times in two hours.
        // Not one set aside since: the room a reward lacked set The Scythe aside every think for
        // ten minutes at a time, 9,452 times in a knight's day.
        if (ordered_ < 0 && options_.quests && wasQuest >= 0 && clock_ - aimSince_ < 10 * 60 * 20 && aside_[wasQuest] <= clock_ &&
            ((was == Aim::Accept && realm_->questOffered(wasQuest)) ||
             (was == Aim::HandIn && realm_->quest(wasQuest).state == sim::QuestState::Ready))) {
            return;
        }
        aimSince_ = clock_;
        aim_ = Aim::Grind;
        aimQuest_ = -1;
        seeking_ = -1;
        quarry_.clear();
        if (castleDue()) {
            aim_ = Aim::Castle;
            aimMap_ = kDevias;
        } else if (squareDue()) {
            aim_ = Aim::Square;
            aimMap_ = kNoria;
        }
        if (options_.quests && aim_ != Aim::Castle && aim_ != Aim::Square) {
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
            // Hand in first.
            const std::vector<int> order = questOrder();
            for (const int q : order) {
                if (aim_ == Aim::HandIn) break;
                // To whom it is handed in, where he stands: Lirien in Atlans for Peia's Drowned Song.
                if (realm_->quest(q).state == sim::QuestState::Ready && aside_[q] <= clock_ && reachable(handInMap(q))) {
                    aim_ = Aim::HandIn;
                    aimQuest_ = q;
                    aimMap_ = handInMap(q);
                }
            }
            // Then down the order, each quest hunted if it is under way or taken if it is offered,
            // whichever comes first (the user, 2026-10-08: "bots first has to finish [Tersia's]
            // questline"): every hunt came before any taking, and while The Drowned Halls' Hydras
            // stayed huntable a knight never took Knights Who Do Not Sleep, so Sevina's Death
            // Knights stood on a floor he could not open for 37 hours.
            for (const int q : order) {
                if (aim_ != Aim::Grind || seeking_ >= 0) break;
                if (!allowed(q)) continue;
                if (realm_->quest(q).state == sim::QuestState::Active) {
                    // A treasure to find: Atlans's grind, any breed he takes there.
                    if (seeks(q) && wanted(q).empty()) {
                        const int at = seekMap();
                        if (at < 0) continue;
                        seeking_ = q;
                        aimQuest_ = q;
                        aimMap_ = at;
                        break;
                    }
                    std::vector<int> breeds;
                    const int where = huntable(q, &breeds);
                    if (where < 0) continue;
                    aim_ = Aim::Hunt;
                    aimQuest_ = q;
                    aimMap_ = where;
                    quarry_ = breeds;
                    continue;
                }
                if (!realm_->questOffered(q)) continue;
                const int at = giverMap(q);
                std::vector<int> breeds;
                if (at < 0 || !reachable(at)) continue;
                // An errand -- someone to find, no breed -- once the place it sends him is reachable;
                // a treasure once he can hunt Atlans.
                if (hasFind(q) && wanted(q).empty() ? seekMap() < 0
                    : errand(q) ? !reachable(handInMap(q)) : huntable(q, &breeds) < 0) continue;
                aim_ = Aim::Accept;
                aimQuest_ = q;
                aimMap_ = at;
            }
        }
        if (aim_ == Aim::Grind && seeking_ >= 0 && aimQuest_ == seeking_) {
            // Grinding Atlans for the treasure (seeks): the map stays.
        } else if (aim_ == Aim::Grind) {
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
                // **Weighed with the purse he lands with**: the trip there paid, a wizard's
                // purse fell under riskShare's line, the breed he went for was too dear on
                // landing, and he paid back -- Lorencia -> Noria -> Lorencia two seconds apart,
                // nine trips in 45 minutes at 4,880 Zen (the Claude bot's runs, 2026-10-09).
                int gate, travel;
                spent_ = w.map != map() && way(w.map, &gate, &travel) && travel >= 0 ? sim::travelAt(travel).zen : 0;
                const int b = bestOn(*t);
                spent_ = 0;
                if (b > best) {
                    best = b;
                    aimMap_ = w.map;
                }
            }
        }
        // **The party's hunt** (lead()), over his own -- but a quest to hand in or to take comes
        // first, a short errand: led past them, partied knights handed in half the quests solo
        // ones did and ended 40 levels lower. And the party's quest offered him is taken on the
        // way (he would not take it alone, which is why the party is there).
        if (lead_.map >= 0 && options_.quests && aim_ != Aim::HandIn && aim_ != Aim::Accept && aim_ != Aim::Castle &&
            aim_ != Aim::Square &&
            reachable(lead_.map)) {
            const int q = lead_.quest;
            if (q >= 0 && realm_->quest(q).state != sim::QuestState::Active && realm_->questOffered(q) &&
                giverMap(q) >= 0 && reachable(giverMap(q))) {
                aim_ = Aim::Accept;
                aimQuest_ = q;
                aimMap_ = giverMap(q);
            } else {
                aim_ = lead_.breeds.empty() ? Aim::Grind : Aim::Hunt;
                aimQuest_ = q >= 0 && realm_->quest(q).state == sim::QuestState::Active ? q : -1;
                aimMap_ = lead_.map;
                quarry_ = lead_.breeds;
                seeking_ = -1;
            }
        }
        // Ordered (order()): that map's grind, over all of the above.
        if (ordered_ >= 0 && reachable(ordered_)) {
            aim_ = Aim::Grind;
            aimQuest_ = -1;
            seeking_ = -1;
            quarry_.clear();
            aimMap_ = ordered_;
        }
        if (aim_ != lastAim_ || aimQuest_ != lastQuest_ || aimMap_ != lastMap_) {
            const char* names[] = {"grinds", "hands in", "hunts for", "takes", "goes to", "goes to"};
            if (aim_ == Aim::Castle) {
                say("%s Blood Castle %d: its door opens in Devias", names[int(aim_)], castleChoice());
            } else if (aim_ == Aim::Square) {
                say("%s Devil Square %d: Charon's gate opens in Noria", names[int(aim_)], squareChoice());
            } else if (aimQuest_ >= 0) {
                say("%s %s (%s)", names[int(aim_)], sim::questAt(aimQuest_).title, worldOf(aimMap_)->name);
            } else if (aim_ == Aim::Hunt) {
                say("hunts beside his party in %s", worldOf(aimMap_)->name);
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
            // **Twice the time a kill of it should take**, thirty seconds at least, and never
            // thirty without taking health off it: a Hydra has 19,000, a knight of level 230 cut
            // 350 a swing off it, and every one he reached he left at a third for five minutes --
            // 0 of The Drowned Halls' four in 17 hours, and Sevina's trial behind it never taken.
            // Icarus's breeds have 11,500 to 95,000. What dies in under fifteen seconds keeps the
            // old thirty: a poor wizard of level 12 who fought on lost 44 lives to Skeletons.
            const sim::Body* body = request.kind == sim::Request::Kind::Attack ? bodyOf(request.target) : nullptr;
            if (request.target != chasing_) {
                chasing_ = request.target;
                chasedSince_ = clock_;
                woundedAt_ = clock_;
                chasedHealth_ = body ? body->health : 0;
                chaseFor_ = kGiveUp;
                // Grinding, not a spellcaster's: a wizard who stayed on 9,000-health Lizard Kings
                // instead of walking on to the next quick kill fell 15 levels in 60 hours, where
                // a knight -- whose quarry stands in his reach and follows him if he turns --
                // ended 10-15 higher with it (294 and 296 against 285 and 278).
                const bool quest = aim_ == Aim::Hunt && body && body->kind >= 0 &&
                                   std::find(quarry_.begin(), quarry_.end(),
                                             tables_->kinds[size_t(body->kind)].number) != quarry_.end();
                if (body && body->kind >= 0 && (quest || !wizardly())) {
                    chaseFor_ = int64_t(std::clamp(2.0 * killTicks(tables_->kinds[size_t(body->kind)]), double(kGiveUp), 10.0 * 60 * 20));
                }
            } else if (body && body->health < chasedHealth_) {
                woundedAt_ = clock_;
                chasedHealth_ = body->health;
            }
            if (clock_ - chasedSince_ > chaseFor_ || clock_ - woundedAt_ > kGiveUp) {
                banned_[chasing_] = clock_ + kForget;
                chasing_ = 0;
                return;
            }
        }
        if (options_.kin == sim::Kin::DarkWizard && request.kind == sim::Request::Kind::Attack) {
            request.skill = quickSpell();
        }
        hand_.ask(request);
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
        int standing = -1;  // the breed of hers that stands
        for (const sim::Body& b : realm_->bodies()) {
            if (b.summoner == hero.id && b.alive()) {
                summoned = true;
                if (b.kind >= 0) standing = tables_->kinds[size_t(b.kind)].number;
            }
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
            // A knight beside his party's tank leaves the guard to him: it is what holds them.
            if (row.number == sim::skill::kDefense && tankHere() != nullptr) continue;
            const bool wanted = (row.boonTicks > 0 && hero.boonUntil <= now) ||
                                (row.mightTicks > 0 && hero.mightUntil <= now) ||
                                (row.mends && hero.health * 2 < hero.maxHealth);
            if (wanted) {
                guardTried_[row.number] = clock_ + 100;
                hand_.invoke(row.number, hero.id);
                return true;
            }
            // The last summon in the table she knows: the later breeds are the stronger. **Only her
            // best, once she knows it** (2026-10-10): the cheaper one, raised whenever the best was
            // a few mana short, came back the moment she sent it away -- an energy elf of level 123
            // cast her Goblin 178 times and her Stone Golem 36. She drinks and waits instead. Two
            // seeds of 24 h: the agility elf 234/243 -> 244/259, castles 20/20 -> 23/24; the
            // energy elf unchanged (123/130 -> 123/131).
            if (row.summons > 0 && (bestSummon() < 0 || i == bestSummon())) summon = i;
        }
        // **Her best summon stands, not an old one** (the user, 2026-10-09: "also elfs has to use
        // their summons which is very strong also"): one standing that is not her best is sent
        // away -- a second cast dismisses it, for nothing (Realm::conjure) -- and the best raised
        // at the next think. A Goblin stood until it died after she had read the Assassin.
        // Only when the best is ready and paid for now, so it is raised at once.
        const int best = bestSummon();
        if (best >= 0 && best == summon && summoned && standing >= 0 && standing != sim::skillAt(best).summons &&
            realm_->cooling(sim::skillAt(best).number) == 0 && hero.mana >= sim::skillAt(best).mana) {
            hand_.invoke(sim::skillAt(best).number, hero.id);
            say("sends her %s away for her best summon", tables_->kinds[size_t(kindIndex(standing))].label.c_str());
            return true;
        }
        if (summoned) summon = -1;
        if (summon >= 0) {
            guardTried_[sim::skillAt(summon).number] = clock_ + 100;
            hand_.invoke(sim::skillAt(summon).number, hero.id);
            return true;
        }
        return false;
    }
    std::map<int32_t, int64_t> guardTried_;
    // The strongest summon she knows and her hand may cast, a skill index, or -1.
    int bestSummon() const {
        const sim::Wearer w = realm_->wearer();
        int best = -1;
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            if (row.summons > 0 && realm_->knows(row.number) && row.suits(w.offHand)) best = i;
        }
        return best;
    }
    int kindIndex(int number) const {
        for (size_t i = 0; i < tables_->kinds.size(); ++i) {
            if (tables_->kinds[i].number == number) return int(i);
        }
        return 0;
    }

    // ---- his part in a party (lead()) ------------------------------------------------------
    // A body of his party on this map, alive, other than himself.
    bool mate(uint32_t id) const {
        const sim::Body& hero = realm_->hero();
        if (id == 0 || id == hero.id || hero.party == 0) return false;
        const sim::Body* one = bodyOf(id);
        return one && one->player && one->alive() && one->party == hero.party;
    }
    const sim::Body* tankHere() const {
        const sim::Body* tank = lead_.tank != 0 && lead_.tank != realm_->hero().id ? bodyOf(lead_.tank) : nullptr;
        return tank && tank->alive() && mate(tank->id) ? tank : nullptr;
    }
    // **The elf's aid** (the user, 2026-10-09: "elfs can heal"): Heal on the lowest of them
    // under 60% within her reach, herself among them; then Greater Damage on whoever's has
    // lapsed; then Greater Defense on a member whose guard has lapsed -- never on the tank, whose
    // Defense guard it would take the place of (one boon a body, and the tank's x3 threat is
    // the guard's). True when she cast.
    static constexpr double kHealUnder = 0.6;
    bool aid() {
        const sim::Body& hero = realm_->hero();
        if (hero.party == 0) return false;
        const int64_t now = realm_->tick();
        const sim::Wearer w = realm_->wearer();
        const float reach = sim::kPartyAidReach - 0.5f;
        const sim::Body* low = nullptr;
        double lowest = kHealUnder;
        for (const sim::Body& b : realm_->bodies()) {
            if (b.id != hero.id && !mate(b.id)) continue;
            if (std::hypot(b.x - hero.x, b.y - hero.y) > reach || b.maxHealth <= 0) continue;
            const double share = double(b.health) / b.maxHealth;
            if (share < lowest) lowest = share, low = &b;
        }
        const auto cast = [&](const sim::SkillRow& row, uint32_t at) {
            int64_t& tried = aidTried_[{row.number, at}];
            if (tried > clock_) return false;
            tried = clock_ + (row.mends ? 20 : 100);
            hand_.invoke(row.number, at);
            if (at != hero.id) ++aidsOf_[row.name];
            return true;
        };
        for (const int pass : {0, 1, 2}) {
            for (int i = 0; i < sim::skillCount(); ++i) {
                const sim::SkillRow& row = sim::skillAt(i);
                if (!row.onSelf() || !realm_->knows(row.number) || realm_->cooling(row.number) > 0) continue;
                if (hero.mana < row.mana || !row.suits(w.offHand)) continue;
                if (pass == 0 && row.mends && low && cast(row, low->id)) return true;
                if (pass == 0) continue;
                for (const sim::Body& b : realm_->bodies()) {
                    if (!mate(b.id) || std::hypot(b.x - hero.x, b.y - hero.y) > reach) continue;
                    if (pass == 1 && row.mightTicks > 0 && b.mightUntil <= now && cast(row, b.id)) return true;
                    if (pass == 2 && row.number == sim::skill::kGreaterDefense && b.id != lead_.tank &&
                        b.boonUntil <= now && cast(row, b.id)) return true;
                }
            }
        }
        return false;
    }
    std::map<std::pair<int32_t, uint32_t>, int64_t> aidTried_;
    std::map<std::string, int> aidsOf_;
    std::map<std::string, int> deathsOf_;  // what killed him and where (printKills)  // her casts on the others of her party, by skill (status)
    // **A damage dealer's target** (docs/party.md, the threat): what the tank has struck, so the
    // monster stays on him -- the one closest to dying, among those his own threat on is under
    // kEaseOff of the tank's. None struck yet: he waits for the tank's first blow. 0 for none.
    static constexpr double kEaseOff = 0.9;
    uint32_t tanksTarget(const sim::Body& tank) const {
        const sim::Body& hero = realm_->hero();
        uint32_t best = 0;
        double weakest = 2.0;
        for (const sim::Body& b : realm_->bodies()) {
            if (!b.monster() || !b.alive() || barred(b.id)) continue;
            if (std::hypot(b.x - hero.x, b.y - hero.y) > kSight) continue;
            const int32_t held = realm_->threatOf(b, tank.id);
            if (held <= 0 || realm_->threatOf(b, hero.id) >= held * kEaseOff) continue;
            const double share = double(b.health) / std::max(1, b.maxHealth);
            if (share < weakest) weakest = share, best = b.id;
        }
        return best;
    }
    // His step toward a body of his party, when he is more than `apart` tiles from it: true
    // while he walks.
    bool keepNear(const sim::Body& one, float apart) {
        const sim::Body& hero = realm_->hero();
        if (std::hypot(one.x - hero.x, one.y - hero.y) <= apart) return false;
        // **On another floor** (the user, 2026-10-09: "can they clear also some of LT floors"):
        // the Lost Tower's and the Dungeon's floors are one map no walk crosses -- his trip or
        // his stairs to the floor the other stands on.
        const int here = realm_->travelFloor();
        const int there = here >= 0 ? realm_->floorAt(one.column(), one.row()) : -1;
        if (here >= 0 && there >= 0 && there != here) {
            chasing_ = 0;
            return goFloor(there, here, false);
        }
        sim::Request request;
        request.kind = sim::Request::Kind::WalkTo;
        request.column = one.column();
        request.row = one.row();
        hand_.ask(request);
        chasing_ = 0;
        return true;
    }

    // Presses the strongest skill that is ready on the body he fights: a blow's force times
    // what it rolls on (a spell adds its own damage to the band), the guards and buffs aside.
    void press(uint32_t at) {
        const sim::Body& hero = realm_->hero();
        const sim::Wearer w = realm_->wearer();
        const sim::Body* target = nullptr;
        for (const sim::Body& b : realm_->bodies()) {
            if (b.id == at) target = &b;
        }
        int best = -1;
        double strongest = 0.0;
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            if (!realm_->knows(row.number) || realm_->cooling(row.number) > 0) continue;
            if (hero.mana < row.mana || row.onSelf() || !row.suits(w.hand)) continue;
            // A storm or a ring cast at a body past its reach is cast at nothing (castsBare):
            // the mana goes on air. A magic gladiator who had just read Twister threw 1,300 of
            // them in an hour, killed nothing and walked to town for mana every three minutes.
            // Twisting Slash too, cast bare since 2026-10-10: held until the body is on a tile
            // beside him, a diagonal one included, or the spin goes on air.
            if (row.castsBare() &&
                (target == nullptr || std::hypot(target->x - hero.x, target->y - hero.y) >
                                          row.reach + (row.wizardry ? 0.0f : 0.5f))) continue;
            const double base = row.wizardry
                                    ? ((w.wizardMinimum + w.wizardMaximum) / 2.0 + row.damage * 1.25) * w.wizardryRate
                                    : blow();
            const double hit = base * sim::force(row, hero.points);
            if (wizardly()) continue;  // a spellcaster's is spell() below
            if (hit > strongest) {
                strongest = hit;
                best = i;
            }
        }
        if (wizardly()) best = spell(target);
        if (best < 0) return;
        hand_.invoke(sim::skillAt(best).number, at);
        pressed_ = best;
    }

    // ---- a spellcaster's mana (the user, 2026-10-08: "DW is weak points, work on DW brains") ---
    // Lightning is 40 mana for 1.8 of the band and Fire Ball 3 for the same 1.8: choosing by the
    // blow alone, a wizard threw 11,273 Lightnings in twelve hours, every Zen he made went on
    // mana potions, half his time was in town, and at level 100 he wore Pad. So a spell is worth
    // its blow times the bodies it will strike -- the chain within six tiles, the line, the arc --
    // a dear one must beat the best cheap one by a quarter to be thrown, and under half his mana
    // only the cheap ones are.
    static constexpr int kCheapMana = 5;
    // **What a spell strikes, as the realm lets it go** (the user, 2026-10-10: "DW has to
    // understand which spells are AOE"): Evil Spirit, Flame, Meteorite's shower and Ice and
    // Poison's splash are `Spread::One` in their rows and were counted as one body each --
    // Evil Spirit, which strikes two in three of everything within ten tiles of him, scored as
    // a single Fire Ball's worth more. Now each as Realm::throwSkill resolves it.
    double struck(const sim::SkillRow& row, const sim::Body* target) const {
        if (target == nullptr) return 1.0;
        const sim::Body& hero = realm_->hero();
        const auto near = [&](float x, float y, float tiles) {
            int n = 0;
            for (const sim::Body& b : realm_->bodies()) {
                if (b.monster() && b.alive() && std::hypot(b.x - x, b.y - y) <= tiles) ++n;
            }
            return std::max(1, n);
        };
        // The spirits round him (Realm::letSpiritsGo): each body within kSpiritReach, two in three.
        if (row.number == sim::skill::kEvilSpirit) {
            return std::max(1.0, near(hero.x, hero.y, sim::kSpiritReach) * sim::kSpiritOdds);
        }
        // Flame's fire (Realm::light): everything within burnTiles of the body, at each burn.
        if (row.burns > 0) return double(near(target->x, target->y, row.burnTiles) * row.burns);
        // Meteorite's shower and Ice and Poison's splash: everything within `splash` of it.
        if (row.splash > 0.0f) return double(near(target->x, target->y, row.splash));
        if (row.spread == sim::Spread::One) return 1.0;
        int n = 0;
        const float dx = target->x - hero.x, dy = target->y - hero.y;
        const float len = std::max(0.5f, std::hypot(dx, dy));
        for (const sim::Body& b : realm_->bodies()) {
            if (!b.monster() || !b.alive()) continue;
            bool in = false;
            if (row.spread == sim::Spread::Ring) {
                in = std::hypot(b.x - target->x, b.y - target->y) <= 6.0f;
            } else if (row.spread == sim::Spread::Line || row.spread == sim::Spread::Beam) {
                // Within a tile and a half of the line from him through the target, inside its reach.
                const float t = ((b.x - hero.x) * dx + (b.y - hero.y) * dy) / len;
                const float off = std::fabs((b.x - hero.x) * dy - (b.y - hero.y) * dx) / len;
                in = t >= 0.0f && t <= row.reach && off <= 1.5f;
            } else {
                // An arc or a fan: in reach and within sixty degrees of the target's bearing.
                const float bx = b.x - hero.x, by = b.y - hero.y, d = std::hypot(bx, by);
                in = d <= row.reach && (d < 0.5f || (bx * dx + by * dy) / (d * len) >= 0.5f);
            }
            if (in) ++n;
        }
        if (row.spread == sim::Spread::Ring) n = std::min(n, sim::kLightningBodies);
        return double(std::max(1, n));
    }
    int spell(const sim::Body* target) const {
        const sim::Body& hero = realm_->hero();
        const sim::Wearer w = realm_->wearer();
        int cheap = -1, dear = -1;
        double cheapWorth = 0.0, dearWorth = 0.0;
        const bool flush = hero.mana * 2 >= hero.maxMana;
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            if (!realm_->knows(row.number) || realm_->cooling(row.number) > 0) continue;
            if (hero.mana < row.mana || row.onSelf() || !row.suits(w.hand) || !row.wizardry) continue;
            // Twisting Slash too, cast bare since 2026-10-10: held until the body is on a tile
            // beside him, a diagonal one included, or the spin goes on air.
            if (row.castsBare() &&
                (target == nullptr || std::hypot(target->x - hero.x, target->y - hero.y) >
                                          row.reach + (row.wizardry ? 0.0f : 0.5f))) continue;
            if (target && std::hypot(target->x - hero.x, target->y - hero.y) > row.reach + 0.5f) continue;
            const double hit = ((w.wizardMinimum + w.wizardMaximum) / 2.0 + row.damage * 1.25) * w.wizardryRate *
                               sim::force(row, hero.points);
            const double worth = hit * struck(row, target);
            if (row.mana <= kCheapMana) {
                if (worth > cheapWorth) cheapWorth = worth, cheap = i;
            } else if (flush && worth > dearWorth) {
                dearWorth = worth, dear = i;
            }
        }
        return dear >= 0 && dearWorth > cheapWorth * 1.25 ? dear : cheap;
    }
    // The quick slot's: the best primary of kCheapMana or less he knows -- Fire Ball once read,
    // else Energy Ball -- thrown whenever press throws nothing.
    int32_t quickSpell() const {
        const sim::Wearer w = realm_->wearer();
        int32_t best = sim::skill::kEnergyBall;
        double strongest = 0.0;
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            if (!row.primary() || !row.wizardry || row.mana > kCheapMana || !realm_->knows(row.number)) continue;
            if (!row.suits(w.hand) || row.spread != sim::Spread::One) continue;
            const double f = sim::force(row, realm_->hero().points);
            if (f > strongest) strongest = f, best = row.number;
        }
        return best;
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
        if (wizardly() && countOf(sim::restores) < 3 && money >= bundle) {
            return "out of mana potions";
        }
        // Only with something to sell or store: a bag full of what he keeps -- jewels, runes,
        // potions, quivers -- sent an elf back to town every thirty-six seconds for three hours.
        if (freeCells() < 6 && (sellable() > 0 || wantsVault())) return "bag full";
        // When he can pay for it: a knight of 19 Zen went to town for his worn gear every three
        // minutes for 36 game hours, never long enough out to earn the mending (level 17).
        if (const int worn = wornDown(); worn >= 0 && !realm_->selfMending() && money >= realm_->repairCost(worn)) {
            return "gear worn down";
        }
        if (archer() && ammo() < 30 && money >= 70) return "out of arrows";
        if (goblinHere() >= 0 && cloakWorth() > 0) return "a cloak to make";
        if (goblinHere() >= 0 && invitationWorth() >= 0) return "an invitation to make";
        if (boxHeld() && freeCells() >= 6 && clock_ - lastTrip_ > 10 * 60 * 20 &&
            (goblinHere() >= 0 || const_cast<Bot*>(this)->reachable(3))) {
            return "the Goblin's box to empty";
        }
        if (runeOwed() && clock_ - lastTrip_ > 2 * 60 * 20 && (goblinHere() >= 0 || const_cast<Bot*>(this)->reachable(3))) {
            return "a rune to save";
        }
        if (clock_ - lastTrip_ > 10 * 60 * 20 && clock_ >= machineAwayUntil_ &&
            (wingsOwed() || cloakWorth() > 0 || invitationWorth() >= 0) &&
            (goblinHere() >= 0 || const_cast<Bot*>(this)->reachable(3))) {
            return "the Chaos Machine";
        }
        if (map() == 0 && clock_ - lastTrip_ > 10 * 60 * 20 && helperOwed() >= 0) return "a pet or a mount to buy";
        // Blood Castle has no counter: an elf went in with a few hundred arrows and stood at the
        // Statue of Saint with none for twelve minutes.
        // Once in ten minutes: a bag with no room for the quivers sent an elf back 800 times a day.
        if (aim_ == Aim::Castle && archer() && ammo() < 1200 && money >= 20000 && clock_ - castleArrowsAt_ > 10 * 60 * 20) {
            const_cast<Bot*>(this)->castleArrowsAt_ = clock_;
            return "arrows for Blood Castle";
        }
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
            if (!hand_.repair(slot)) return;
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
            // **The party's hunt with no party about him** (lead(), partyScale): what he takes
            // alone, till they come -- its breeds are ones he would not, and an elf led to Noria's
            // Elite Goblins before her knight came stood idle there for an hour at level 7.
            if (lead_.map == map() && partyScale_ >= 1.0) return takes(kind);
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
        // **A spellcaster's guard kept up in the field**, not only as a fight begins (the user,
        // 2026-10-08: "does DW use Magic defense skill?"): Magic Shield takes about half of every
        // blow off a wizard of 500 energy for five minutes, and raised only on engaging it covered
        // ten hours of his day -- the packs caught him on the walks between.
        if (wizardly() && !tables_->grid.safe(hero.column(), hero.row()) && guard()) return;
        // The party's elf: her members first, out of town.
        if (lead_.role == Role::Healer && !tables_->grid.safe(hero.column(), hero.row()) && aid()) return;
        sim::Request request;
        // Whatever is on him first -- and the party's tank whatever is on one of them near him,
        // so it turns to him.
        float closest = 6.0f * 6.0f;
        bool engaged = false;
        const bool peels = lead_.role == Role::Tank && hero.party != 0;
        for (const sim::Body& body : realm_->bodies()) {
            if (!body.monster() || !body.alive() || barred(body.id)) continue;
            if (body.quarry != hero.id && !(peels && mate(body.quarry))) continue;
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
            if (aim_ == Aim::Castle && aimMap_ == map()) {
                messenger();
                return;
            }
            if (aim_ == Aim::Square && aimMap_ == map()) {
                charon();
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
        // **In a party** (lead()): a damage dealer or the elf strikes what the tank holds, and
        // with nothing held keeps by him rather than pulling anything of his own; whoever is
        // not the leader keeps near him. Only on the party's map: elsewhere he is on his way.
        if (request.kind == sim::Request::Kind::None && lead_.map == map() && hero.party != 0) {
            if (const sim::Body* tank = lead_.role != Role::Tank ? tankHere() : nullptr;
                tank && std::hypot(tank->x - hero.x, tank->y - hero.y) <= 2.0f * kSight) {
                if (const uint32_t at = tanksTarget(*tank)) {
                    request.kind = sim::Request::Kind::Attack;
                    request.target = at;
                } else {
                    if (keepNear(*tank, 4.0f)) return;
                    if (chasing_ != 0) {
                        sim::Request stop;
                        stop.kind = sim::Request::Kind::Stop;
                        hand_.ask(stop);
                        chasing_ = 0;
                    }
                    return;
                }
            } else if (lead_.leader != 0 && lead_.leader != hero.id && mate(lead_.leader) &&
                       keepNear(*bodyOf(lead_.leader), 8.0f)) {
                return;
            }
        }
        // **The one he is after, while he would still go after it** (2026-10-10): the nearest was
        // picked again every think, and a wizard of level 15 stood a day between the last two
        // Spiders of Lorencia, Once More, sixteen tiles to either side -- each think the other
        // was nearer by a step, each pick began a new walk, and the give-up clock (ask) started
        // over with it. He turned on the spot for 23 game hours. Ask's clock now runs out on
        // one he cannot reach, which bars it and frees him for the next.
        if (request.kind == sim::Request::Kind::None && chasing_ != 0) {
            if (const sim::Body* on = bodyOf(chasing_); on && quarry(*on)) {
                request.kind = sim::Request::Kind::Attack;
                request.target = chasing_;
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
        if (std::getenv("BOT_TRACE") && clock_ > int64_t(std::atof(std::getenv("BOT_TRACE")) * 72000) && clock_ % (20 * 30) < kThink) {
            { const sim::Body* tb = bodyOf(request.target); std::printf("TRACE %s floor %d at %.1f,%.1f hp %d engaged %d kind %d target %u at %.1f,%.1f walking %d\n", clock(clock_).c_str(), realm_->travelFloor(), hero.x, hero.y, hero.health, engaged, int(request.kind), request.target, tb ? tb->x : -1.f, tb ? tb->y : -1.f, int(hero.walking)); std::printf("   chasing %u since %lld wounded %lld banned %zu speed %.3f frozen %lld tick %lld mode %d\n", chasing_, (long long)chasedSince_, (long long)woundedAt_, banned_.size(), hero.speed, (long long)hero.frozenUntil, (long long)realm_->tick(), int(mode_)); if (const sim::Body* cb = bodyOf(chasing_)) std::printf("   chased body: kind %d monster %d alive %d at %.1f,%.1f\n", cb->kind, int(cb->monster()), int(cb->alive()), cb->x, cb->y); else { for (const sim::Lying& l : realm_->lying()) if (l.id == chasing_) std::printf("   chased lying: %s at %d,%d\n", rowOf(l.what).label.c_str(), l.column, l.row); } }
            for (const sim::Body& b : realm_->bodies()) {
                if (!b.monster() || !b.alive()) continue;
                const float dx = b.x - hero.x, dy = b.y - hero.y;
                if (dx * dx + dy * dy > 15 * 15) continue;
                std::printf("   id %u %s lv %d at %.1f,%.1f d %.1f onme %d barred %d quarry %d floor %d\n", b.id, tables_->kinds[size_t(b.kind)].label.c_str(), tables_->kinds[size_t(b.kind)].level, b.x, b.y, std::sqrt(dx*dx+dy*dy), b.quarry == hero.id, barred(b.id), quarry(b), realm_->floorAt(b.column(), b.row()));
            }
        }
        if (request.kind == sim::Request::Kind::None && nextFloor()) return;
        // **No floor to go to: the nearest on his own** (the user, 2026-10-07: "keep testing
        // MG"). A gladiator grinding the Lost Tower whose best breed stood on no floor he could
        // reach stood at the stairs for an hour and three quarters, 27 he would take on his own
        // floor out of sight -- setAside is a quest's, and grinding has none.
        if (request.kind == sim::Request::Kind::None) {
            closest = 1e30f;
            for (const sim::Body& body : realm_->bodies()) {
                if (!quarry(body)) continue;
                const float dx = body.x - hero.x, dy = body.y - hero.y;
                if (dx * dx + dy * dy < closest) {
                    closest = dx * dx + dy * dy;
                    request.kind = sim::Request::Kind::Attack;
                    request.target = body.id;
                }
            }
            if (request.kind == sim::Request::Kind::None) return;
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
    // True while it is taking him to another floor; false when there is none to go to.
    bool nextFloor() {
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
        // The nearest on a floor whose trip is open first: a knight in the Lost Tower's hall for
        // Fire from Afar went for its breeds on floor 4, which The Red Floor opens, and set the
        // quest aside 600 times while the same breeds stood on floor 3, which it had opened.
        float closestOpen = 1e30f;
        int toOpen = -1;
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
            if (realm_->travelRefusal(floor) == sim::TravelRefusal::None && dx * dx + dy * dy < closestOpen) {
                closestOpen = dx * dx + dy * dy;
                toOpen = floor;
            }
        }
        if (toOpen >= 0) to = toOpen;
        // And never straight back to the floor he has just left.
        if (to >= 0 && to == floorLeft_ && clock_ < floorLeftAt_ + 5 * 60 * 20) to = -1;
        if (to < 0 && std::getenv("BOT_LADDER")) {
            std::map<int, int> byFloor;
            for (const sim::Body& body : realm_->bodies()) {
                if (quarry(body, true)) ++byFloor[realm_->floorAt(body.column(), body.row())];
            }
            std::printf("NOFLOOR map %d here %d at %d,%d left %d aim %d quest %d quarry:", map(), here, hero.column(),
                        hero.row(), floorLeft_, int(aim_), aimQuest_);
            for (const auto& [f, n] : byFloor) std::printf(" floor %d x%d (refusal %d)", f, n, f >= 0 ? int(realm_->travelRefusal(f)) : -9);
            std::printf("\n");
        }
        if (to < 0) {
            setAside();
            return false;
        }
        return goFloor(to, here, true);
    }
    // **To another floor of this map** -- paid for when its trip is open and he can spare it,
    // else by the stairs of his floor -- true while on his way. `quest`: a hunt's, which sets
    // its breeds aside when no way is found (nextFloor); a party's keeps trying (keepNear).
    bool goFloor(int to, int here, bool quest) {
        const sim::Body& hero = realm_->hero();
        const sim::TravelRow& row = sim::travelAt(to);
        if (realm_->travelRefusal(to) == sim::TravelRefusal::None && realm_->money() >= row.zen + potionReserve()) {
            if (clock_ >= nextFloorAt_ && hand_.travel(to)) {
                floorLeft_ = here;
                floorLeftAt_ = clock_;
                say("pays %lld zen down to %s", (long long)row.zen, row.name);
                banned_.clear();
                nextFloorAt_ = clock_ + 30 * 20;
            }
            return true;
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
            if (std::getenv("BOT_LADDER")) {
                std::printf("NOSTAIR map %d here %d to %d (%s) refusal %d at %d,%d\n", map(), here, to, row.name,
                            int(realm_->travelRefusal(to)), hero.column(), hero.row());
            }
            if (quest) setAside();
            return false;
        }
        if (!hero.walking) {
            const sim::EnterGate* in = sim::enterGateNumbered(stair);
            sim::Request request;
            request.kind = sim::Request::Kind::WalkTo;
            request.column = (in->box.x1 + in->box.x2) / 2;
            request.row = (in->box.y1 + in->box.y2) / 2;
            hand_.ask(request);
        }
        return true;
    }

    // The trip to the map he is on, which lands at its entrance -- the Lost Tower's hall from its
    // floors, which no walk reaches. True when he paid for it.
    bool toEntrance(const char* whom) {
        for (int i = 0; i < sim::kTravels; ++i) {
            const sim::TravelRow& trip = sim::travelAt(i);
            if (trip.map != map() || realm_->travelRefusal(i) != sim::TravelRefusal::None) continue;
            if (!tables_->grid.safe(trip.column, trip.row) || realm_->money() < trip.zen + 500) continue;
            if (!hand_.travel(i)) continue;
            say("cannot walk to %s: the trip to %s's entrance", whom, trip.name);
            return true;
        }
        return false;
    }

    void setAside() {
        if (aim_ != Aim::Hunt || aimQuest_ < 0) return;
        // Its breeds on this map, not the whole quest: Sevina's trial was set aside whenever the
        // Lost Tower's Death Gorgons were on a floor he could not reach, and its three Atlans
        // breeds, which he took on, were never hunted. The quest itself only when no map is left.
        asideOn_[{aimQuest_, map()}] = clock_ + 10 * 60 * 20;
        std::vector<int> others;
        if (huntable(aimQuest_, &others) >= 0) {
            say("sets %s's breeds in %s aside for 10 min: out of reach", sim::questAt(aimQuest_).title,
                worldOf(map())->name);
            nextAim_ = clock_;
            return;
        }
        aside_[aimQuest_] = clock_ + 10 * 60 * 20;
        say("sets %s aside for 10 min: its breeds are out of reach", sim::questAt(aimQuest_).title);
        if (std::getenv("BOT_LADDER")) {
            for (const int breed : wanted(aimQuest_)) {
                const int home = homeOf(breed);
                const content::MonsterKind* kind = home >= 0 ? kindOf(home, breed) : nullptr;
                const auto fear = fearUntil_.find(breed);
                std::printf("ASIDE breed %d home %d cost %.0f hp %d heals %d takes1 %d reach %d feared %d\n", breed, home,
                            kind ? costOf(*kind) : -1.0, realm_->hero().maxHealth, countOf(sim::heals),
                            kind ? takes(*kind, 1.0) : -1, home >= 0 ? reachable(home) : -1,
                            fear != fearUntil_.end() && fear->second > clock_);
            }
        }
        nextAim_ = clock_;
    }

    // The giver's dialog: walked to and opened by a Talk, and then the quest taken or handed in.
    void visitGiver() {
        const sim::QuestRow& row = sim::questAt(aimQuest_);
        const int32_t whom = aim_ == Aim::HandIn ? sim::questReceiver(row) : row.giver;
        int folk = -1;
        for (size_t i = 0; i < tables_->folk.size(); ++i) {
            if (tables_->folk[i].number == whom) folk = int(i);
        }
        if (folk < 0) return;
        if (realm_->questing() != folk) {
            // A giver he cannot walk to -- Tersia in the Lost Tower's hall from its second floor,
            // where a knight stood for 18 hours to hand in Bad Air -- is reached by the trip to
            // this map, which lands at its entrance (a trip may land on the map he is on).
            if (giverFolk_ != folk) {
                giverFolk_ = folk;
                giverSince_ = clock_;
            } else if (clock_ - giverSince_ > 60 * 20) {
                giverSince_ = clock_;
                if (toEntrance(row.giverName)) return;
            }
            sim::Request request;
            request.kind = sim::Request::Kind::Talk;
            request.target = uint32_t(folk);
            hand_.ask(request);
            return;
        }
        giverFolk_ = -1;  // reached: the next walk to a giver is timed afresh
        if (aim_ == Aim::Accept) {
            if (hand_.acceptQuest(aimQuest_)) say("takes %s from %s", row.title, row.giverName);
        } else {
            int choice = -1;
            for (int c = 0; c < row.choiceCount && choice < 0; ++c) {
                if (realm_->questChoiceFits(aimQuest_, c)) choice = c;
            }
            const int level = realm_->hero().level;
            const int64_t zen = realm_->money();
            if (hand_.completeQuest(aimQuest_, choice,
                                      options_.magic ? sim::QuestPath::Magic : sim::QuestPath::Melee)) {
                ++out_.handedIn[aimQuest_];
                crowded_ = false;
                crowdedQuest_ = -1;
                crowdedTries_ = 0;
                if (out_.firstHandIn[aimQuest_] < 0) out_.firstHandIn[aimQuest_] = clock_;
                say("** hands in %s to %s: +%lld zen, level %d -> %d%s%s", row.title, row.giverName,
                    (long long)(realm_->money() - zen), level, realm_->hero().level,
                    choice >= 0 ? ", chose " : "", choice >= 0 ? row.choices[choice].item : "");
                sortBag();
            } else {
                // Refused when it is ready: the room, which a bag of scattered cells can lack
                // with plenty free. To the counters to sell, and back.
                // Twice running with nothing a counter would take: aside for half an hour, or an
                // elf whose bag was potions, quivers and Chaos asked Tersia 2,208 times in a day.
                // Three refusals since the last hand-in, now that a trip makes room (makeRoom):
                // the first with nothing to sell still goes to the counters.
                if (crowded_ && sellable() == 0 && ++crowdedTries_ >= 3) {
                    crowdedTries_ = 0;
                    aside_[aimQuest_] = clock_ + 30 * 60 * 20;
                    say("sets %s aside for 30 min: no room for its reward", row.title);
                    hand_.closeQuest();
                    nextAim_ = clock_;
                    return;
                }
                say("no room for %s's reward: to the counters", row.giverName);
                if (std::getenv("BOT_LADDER")) {
                    std::string bag;
                    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                        const sim::Held& one = realm_->satchel()[slot];
                        if (!one.empty()) bag += rowOf(one).label + std::string(keepsAt(slot) ? "* " : " ");
                    }
                    std::printf("BAG %s| choices %d\n", bag.c_str(), row.choiceCount);
                }
                crowded_ = true;
                crowdedQuest_ = aimQuest_;
                hand_.closeQuest();
                startTrip();
                return;
            }
        }
        hand_.closeQuest();
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
                hand_.ask(request);
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
        entranceTried_ = false;
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
            machineOwed_ = machineOwed_ || cloakWorth() > 0 || invitationWorth() >= 0 || wingsOwed() || boxHeld();
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
    // A town he can reach whose counters sell what her weapon shoots, or -1.
    int ammoTown() {
        const int hand = ammoHand();
        if (hand < 0) return -1;
        for (const int there : {0, 3, 2}) {
            const content::Tables* t = world(there);
            if (!t || !reachable(there)) continue;
            for (const content::Townsperson& f : t->folk) {
                int count = 0;
                const sim::Offer* shelf = sim::stockOf(f.number, &count);
                for (int i = 0; i < count; ++i) {
                    const int item = t->itemAt(shelf[i].group, shelf[i].number);
                    if (item >= 0 && feeds(t->items[size_t(item)])) return there;
                }
            }
        }
        return -1;
    }
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
        // Out of what her weapon shoots and no counter here sells it: to the town that does. Bolts
        // are Eo's in Noria and not Lorencia's, and an elf with a crossbow went to Lorencia's
        // counters 843 times in two days and bought none.
        if (archer() && ammo() < 30 && !ammoHere()) {
            const int there = ammoTown();
            if (there >= 0 && there != map()) {
                tripOwed_ = true;
                if (goTo(there)) return;
                tripOwed_ = false;
            }
        }
        // No counter on this map (the Dungeon), or none with the arrows she is out of: home to
        // Lorencia's.
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
        // The vault first, when it is owed: deposit every jewel and rune, then the counters.
        if (vaultOwed_) {
            const int keeper = vaultHere();
            if (keeper < 0 || clock_ - vaultSince_ > 90 * 20) {
                vaultOwed_ = false;
            } else if (realm_->banking() == keeper) {
                int stored = 0;
                for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                    const sim::Held one = realm_->satchel()[slot];
                    if (!one.empty() && stores(one) && hand_.deposit(slot) >= 0) ++stored;
                    else if (!one.empty() && stores(one) && std::getenv("BOT_LADDER")) {
                        std::printf("REFUSED %s +%d\n", rowOf(one).label.c_str(), int(one.refinement));
                    }
                }
                int taken = 0;
                std::string what;
                for (int cell = 0; cell < sim::kVaultCells; ++cell) {
                    const sim::Held one = realm_->vault()[cell];
                    if (!fetches(one)) continue;
                    const std::string label = rowOf(one).label;
                    if (hand_.withdraw(cell) < 0) continue;
                    ++taken;
                    what += (what.empty() ? "" : ", ") + label;
                }
                say("stores %d jewels and runes with %s", stored, tables_->folk[size_t(keeper)].name.c_str());
                if (taken > 0) {
                    say("takes %d out of the vault: %s", taken, what.c_str());
                    sortBag();
                }
                hand_.closeVault();
                vaultOwed_ = false;
                tripSince_ = clock_;
                return;
            } else {
                sim::Request request;
                request.kind = sim::Request::Kind::Talk;
                request.target = uint32_t(keeper);
                hand_.ask(request);
                return;
            }
        }
        // The Chaos Goblin stands in Noria alone: a box owed and none here, there first.
        // And when his box holds what a full bag left there: an elf went to Lorencia to empty it.
        // And a good rune to take out of a piece he has bettered (runeOwed).
        if (goblinHere() < 0 && awayTo_ < 0 && clock_ >= machineAwayUntil_ &&
            (wingsOwed() || cloakWorth() > 0 || runeOwed() || (boxHeld() && freeCells() >= 6))) {
            machineAwayUntil_ = clock_ + 30 * 60 * 20;
            tripOwed_ = true;
            if (goTo(3)) {
                say("goes to Noria's Chaos Goblin");
                awayTo_ = 3;
                return;
            }
            tripOwed_ = false;
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
        // Not while a box is owed at the Goblin he stands beside: an elf with a 47% Chaos
        // Weapon box walked to Noria every half hour from level 157, was sent on to Devias's
        // counters the moment she arrived, and never mixed it.
        const bool goblinOwed = goblinHere() >= 0 && (wingsOwed() || cloakWorth() > 0 || (boxHeld() && freeCells() >= 6));
        if (goblinOwed) machineOwed_ = true;
        if (!goblinOwed && errands_.size() == sellers_.size() && clock_ >= awayShopUntil_ && !shopWorth()) {
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
            // The Chaos Goblin last, once the counters have made room for what comes out: a
            // cloak mixed into a full bag stayed in the box and spoiled the next one.
            if (machineOwed_ && goblinVisit()) return;
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
            hand_.closeTrade();
            errands_.erase(errands_.begin());
            tripSince_ = clock_;
            return;
        }
        if (clock_ - tripSince_ > 90 * 20) {  // could not reach him: the next one
            // Once a trip, the entrance first: a knight on the Lost Tower's second floor could not
            // walk to its hall's counters and turned back with a full bag 400 times in a day.
            if (!entranceTried_ && toEntrance(tables_->folk[size_t(folk)].name.c_str())) {
                entranceTried_ = true;
                tripSince_ = clock_;
                return;
            }
            say("could not reach %s", tables_->folk[size_t(folk)].name.c_str());
            if (std::getenv("BOT_LADDER")) {
                int fc = 0, fr = 0;
                realm_->folkTile(folk, &fc, &fr);
                std::printf("UNREACH map %d at %d,%d floor %d folk at %d,%d safe %d walking %d\n", map(),
                            realm_->hero().column(), realm_->hero().row(),
                            realm_->floorAt(realm_->hero().column(), realm_->hero().row()), fc, fr,
                            tables_->grid.safe(realm_->hero().column(), realm_->hero().row()), realm_->hero().walking);
            }
            errands_.erase(errands_.begin());
            tripSince_ = clock_;
            return;
        }
        sim::Request request;
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(folk);
        hand_.ask(request);
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
    // A healing potion's size as healTier counts it (Small 0, Medium 1, Large 2), or -1.
    static int potionTier(const content::ItemRow& row) {
        return sim::heals(row) && row.number >= 1 ? row.number - 1 : -1;
    }
    static int64_t potionPrice(int tier) { return tier == 0 ? 240 : tier == 1 ? 990 : 2200; }

    // **Whether a quest's pay would go into the bag as it stands** (Realm::completeQuest places
    // all of it or nothing): each thing paid to him laid on a copy of the bag in the table's
    // order, the treasure he hands over lifted first. `missing` gets the first that found no room.
    bool rewardFits(int q, const content::ItemRow** missing = nullptr) const {
        const sim::QuestRow& row = sim::questAt(q);
        sim::Satchel bag = realm_->satchel();
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            if (questItem(bag[slot])) bag.lift(slot);
        }
        const bool first = sim::questFirst(row, int(options_.kin), realm_->quest(q).completions);
        const int kin = sim::questPaidKin(row, int(options_.kin), first,
                                          options_.magic ? sim::QuestPath::Magic : sim::QuestPath::Melee);
        const auto place = [&](const char* name, int count) {
            const int32_t item = name ? tables_->itemNamed(name) : -1;
            if (item < 0) return true;
            const content::ItemRow& r = tables_->items[size_t(item)];
            for (int n = 0; n < (sim::stacks(r) ? 1 : std::max(1, count)); ++n) {
                const int slot = bag.free(*tables_, r.width, r.height);
                if (slot < 0) {
                    if (missing) *missing = &r;
                    return false;
                }
                bag.put(slot, sim::Held{item, int16_t(0), int16_t(1)});
            }
            return true;
        };
        for (int i = 0; i < row.paidCount; ++i) {
            if (!sim::questPaysHero(row.paid[i], int(options_.kin), kin, first) ||
                !realm_->questItemFits(row.paid[i])) continue;
            if (!place(row.paid[i].item, row.paid[i].count)) return false;
        }
        for (int c = 0; c < row.choiceCount; ++c) {
            if (realm_->questChoiceFits(q, c)) return place(row.choices[c].item, 1);
        }
        return true;
    }
    // Whether a reward still waits on room he has not made (crowded_).
    bool roomOwed() const {
        return crowded_ && crowdedQuest_ >= 0 && realm_->quest(crowdedQuest_).state == sim::QuestState::Ready &&
               !rewardFits(crowdedQuest_);
    }
    // What giving up a bag slot to a reward costs him, or -1 for what is never given up: what a
    // counter takes anyway nothing, a potion or quiver what buying it again costs, the rest dear.
    double roomCost(int slot) const {
        const sim::Held& one = realm_->satchel()[slot];
        if (one.empty()) return 0.0;
        const content::ItemRow& row = rowOf(one);
        // A second Scroll, Bone or cloak of one level, bag or vault: an elf of seed 3 carried five
        // Scrolls and three Bones to Tersia and was refused Rolling Fire's pay twelve times.
        if (sim::scrollOfArchangel(row) || sim::bloodBone(row) || sim::invisibilityCloak(row)) {
            int same = vaultHolds(one);
            for (int other = sim::kWorn; other < sim::kSlots; ++other) {
                const sim::Held& o = realm_->satchel()[other];
                same += other != slot && !o.empty() && o.item == one.item && o.refinement == one.refinement;
            }
            return same > 0 ? 5.0 : -1.0;
        }
        if (questItem(one) || jewelKind(row) || castleTicket(row) || squareTicket(row) || chaosWeaponRow(row) || anyHelper(row) ||
            sim::firstWing(row) || sim::secondWing(row) || chaos(row)) return -1.0;
        if (!keepsAt(slot)) return 0.1;
        if (sim::heals(row) || sim::restores(row)) return 1.0;
        if (sim::ammunition(row)) return 2.0;
        return 20.0;
    }
    // The cheapest block of the bag a thing of this size would cover, sold (roomCost): what
    // was sold, 0 when no block holds only what he would give up.
    int clearFor(const content::ItemRow& need, bool throwAway = false) {
        double bestCost = 1e30;
        std::set<int> best;
        int cells[sim::kSlots];
        for (int at = sim::kWorn; at < sim::kSlots; ++at) {
            const int n = realm_->satchel().covered(at, need.width, need.height, cells);
            if (n == 0) continue;
            std::set<int> holders;
            double cost = 0.0;
            bool ok = true;
            for (int i = 0; i < n && ok; ++i) {
                const int h = realm_->satchel().holder(*tables_, cells[i]);
                if (h < 0 || !holders.insert(h).second) continue;
                const double c = roomCost(h);
                if (c < 0.0) ok = false;
                else cost += c;
            }
            if (ok && !holders.empty() && cost < bestCost) {
                bestCost = cost;
                best = holders;
            }
        }
        int sold = 0;
        for (const int h : best) sold += throwAway ? hand_.discard(h) != 0 : hand_.sellItem(h) >= 0;
        return sold;
    }
    // **Room made for a quest's pay at the counter** (the user, 2026-10-08: "make it reliable"):
    // free cells are not room for a bow two wide and four tall, and a bag of potions, quivers and
    // jewels in scattered cells refused Tersia's Tiger Bow -- the elf of seed 3 set Fire from Afar
    // aside 59,375 times in 60 hours and never reached Sevina, the knight The Scythe 9,452. The
    // cheapest block of the bag the missing piece would cover is sold, until all of it fits.
    void makeRoom() {
        const int q = crowdedQuest_;
        if (!roomOwed()) return;
        int sold = 0;
        for (int pass = 0; pass < 12; ++pass) {
            const content::ItemRow* need = nullptr;
            if (rewardFits(q, &need) || !need) break;
            const int one = clearFor(*need);
            if (one == 0) break;
            sold += one;
        }
        if (sold) say("sells %d more to make room for %s's reward", sold, sim::questAt(q).giverName);
    }

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
            const int64_t paid = hand_.sellItem(slot);
            if (paid >= 0) {
                ++sold;
                got += paid;
            }
        }
        if (sold) {
            out_.sold += sold;
            say("sells %d things to %s for %lld zen", sold, name.c_str(), (long long)got);
        }
        makeRoom();
        if (realm_->mending()) {
            const int64_t cost = realm_->repairAllCost();
            if (cost > 0 && hand_.repairAll() > 0) say("repairs for %lld zen", (long long)cost);
        }
        int count = 0;
        const sim::Offer* shelf = sim::stockOf(npc, &count);
        // An archer's quiver before anything: without it she cannot earn the rest.
        buyAmmo(shelf, count);
        buyGear(shelf, count);
        buyHelpers(shelf, count);
        if (potionShelf(folk)) buyPotions(shelf, count);
    }

    int buyOne(const sim::Offer& offer, const char* why) {
        const int item = tables_->itemAt(offer.group, offer.number);
        if (item < 0) return -1;
        const int64_t before = realm_->money();
        // Nothing bought into the room a waiting reward needs (makeRoom).
        const bool roomMade = crowded_ && !roomOwed();
        const int slot = hand_.buy(offer.slot);
        if (slot >= 0 && roomMade && roomOwed()) {
            hand_.buyBack();
            return -1;
        }
        if (slot >= 0) {
            ++out_.bought;
            if (why) say("buys %s for %lld zen%s", tables_->items[size_t(item)].label.c_str(),
                         (long long)(before - realm_->money()), why);
        }
        return slot;
    }

    static constexpr int64_t kQuiverMost = 100;  // a +0 quiver: arrows 70, bolts 100
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
        // An archer short of what she shoots keeps two quivers' Zen: an elf whose bolts were not
        // sold here spent her last 300 on potions, could not pay Noria's 100 for a quiver, and
        // went to town for two days at level 39.
        const int64_t keep = archer() && ammo() < 500 ? 2 * kQuiverMost : 0;
        const auto heal = [&](int upTo) {
            if (const sim::Offer* offer = find(sim::heals, 3, tier)) {
                while (countOf(sim::heals) < upTo && realm_->money() >= potionPrice(tier) + keep &&
                       buyOne(*offer, nullptr) >= 0) bought += 3;
            }
        };
        const auto restore = [&](int upTo) {
            if (!casts()) return;
            if (const sim::Offer* offer = find(sim::restores, 3, tier)) {
                while (countOf(sim::restores) < upTo && realm_->money() >= potionPrice(tier) + keep &&
                       buyOne(*offer, nullptr) >= 0) mana += 3;
            }
        };
        // Twice the stock when the purse can stand forty bundles (2026-10-03, "continue
        // improving bots"): a wizard with 250,000 Zen bought twenty-four, drank them in nine
        // minutes in the Dungeon and paid two trips back to Lorencia for the next. Twenty a cell,
        // so it is three cells more.
        const int deep = realm_->money() > potionPrice(tier) * 40 ? 2 : 1;
        // The wizard's mana was the dearer from 2026-10-03, when he threw Lightning at 40 a cast;
        // since he chooses his spells by their mana (spell) he went to town for mana twice a day
        // and for health thirty times, so health comes first as everyone's does.
        if (wizardly()) {
            // The bundle he came for first, when he came for mana (needsTown): health first spent
            // a poor wizard's last Zen, he walked out with none, and was sent back -- 14 trips in
            // a game hour at level 10-18, "buys 3 healing and 0 mana" each (the Claude bot's run,
            // 2026-10-09).
            restore(3);
            heal(30 * deep);
            restore(18 * deep);
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
            // And four thousand once rich: a quiver is a thousand shots, and at 1500 an elf with
            // 447,000 Zen still walked home every 15 minutes, 25 times in ten hours (the Claude
            // bot's run, 2026-10-09) -- seventy Zen a quiver against the walk.
            const int quiver = realm_->money() > 100000 ? 4000 : realm_->money() > 20000 ? 1500 : 500;
            // No cell for a quiver and nothing to shoot: room made as for a reward (clearFor).
            if (ammo() < 30 && realm_->money() >= price && realm_->satchel().free(*tables_, row.width, row.height) < 0 &&
                clearFor(row) > 0) {
                say("makes room for a quiver");
            }
            while (ammo() < quiver && realm_->money() >= price) {
                const int slot = buyOne(shelf[i], " (ammunition)");
                if (slot < 0) break;
                if (realm_->satchel()[hand].empty()) hand_.moveItem(slot, hand);
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
                if (hand_.useItem(slot)) {
                    say("buys %s for %lld zen and learns it", row.label.c_str(), (long long)price);
                } else {
                    hand_.buyBack();
                    --out_.bought;
                    triedOn_[row.name] = clock_ + 2 * 3600 * 20;
                }
                continue;
            }
            if (wearBest() && realm_->satchel()[slot].item != item) {
                say("  (bought %s for %lld zen)", row.label.c_str(), (long long)price);
            } else {
                hand_.buyBack();
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
        if (wizardly() && row.weapon()) {
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
    std::unique_ptr<sim::Realm> own_;  // offline: the realm he plays
    sim::Realm* realm_ = nullptr;      // what he reads and acts on: own_, or on the server a scratch copy of the mirror
    Hand hand_;
    int64_t clock_ = 0;
    Outcome out_;
    Mode mode_ = Mode::Hunt;
    Aim aim_ = Aim::Grind, lastAim_ = Aim::Grind;
    int ordered_ = -1;  // order(): the map he was told to grind, or -1
    mutable int64_t spent_ = 0;  // a trip's price, off the purse riskShare reads (choose)
    int aimQuest_ = -1, aimMap_ = 0, lastQuest_ = -2, lastMap_ = -1;
    std::vector<int> quarry_;
    int64_t aside_[sim::kQuests] = {};
    std::map<std::pair<int, int>, int64_t> asideOn_;  // a quest's breeds on one map, set aside until
    int owedMap_ = -1, owedColumn_ = 0, owedRow_ = 0, walkingTo_ = -1;
    std::vector<int> errands_, sellers_;
    bool safe_ = true, tripOwed_ = false, restOwed_ = false;
    int64_t awayShopUntil_ = 0;
    int awayTo_ = -1;
    int floorLeft_ = -1;       // the floor he last paid to leave, and when
    int64_t floorLeftAt_ = 0;
    bool vaultOwed_ = false;   // this trip calls on the vault keeper first
    bool machineOwed_ = false;
    int owedCastle_ = 1;
    int owedSquare_ = 1, owedSquareWait_ = 0;  // Charon's square and his gate's last seconds
    int64_t machineAwayUntil_ = 0;
    int64_t aimSince_ = 0;     // when choose() last chose afresh
    int seeking_ = -1;         // the quest whose treasure he grinds Atlans for, or -1
    int opened_ = 0;           // Firecrackers and boxes opened
    int64_t castleArrowsAt_ = -1000000;  // the last trip for Blood Castle's arrows
    bool entranceTried_ = false;  // this trip has already paid for its map's entrance
    int giverFolk_ = -1;       // the giver he is walking to, and since when (visitGiver)
    int64_t giverSince_ = 0;
    bool boxStuck_ = false;  // the Goblin's box kept something the bag had no room for
    int crowdedQuest_ = -1;   // the quest whose reward found no room (makeRoom), and its refusals
    int crowdedTries_ = 0;
    bool crowded_ = false;  // a reward found no room: the ladder keeps nothing until one is handed in  // the next trip that may go to Noria for the machine        // the castle his cloak opened, for the raise on the far side  // and on the Chaos Goblin, to take runes out
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
    int32_t chasedHealth_ = 0;  // the chased body's health when it last lost some, and when
    int64_t woundedAt_ = 0, chaseFor_ = kGiveUp;
    int pressed_ = 0, lastLevel_ = 1, drunkAt_ = 0, healed_ = 0;
    double caution_ = 1.0;
    std::map<int, int64_t> shunned_;  // a grinding ground left until this tick (learnCaution, died)
    std::map<int, std::vector<int64_t>> deathsOn_;  // his deaths on each map in the last kFear (died)
    static constexpr int64_t kLeavesMap = 20 * 60 * 20;
    Lead lead_;                 // the party's hunt and his part in it (lead())
    std::vector<Ground> grounds_;  // grounds(), and when it was last reckoned
    int64_t groundsAt_ = -1;
    double partyScale_ = 1.0;   // what a kill costs him beside his party (partyScale), this think
    int64_t cautionAt_ = 0;
};

}  // namespace
