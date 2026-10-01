// The Lorencia bot: a character played from level 1 with no window, the way a player would --
// hunting what his level can take, drinking potions, resting in town when he is low, picking up
// what falls, wearing what is better, learning the orbs he can read, and walking to town to
// sell, repair, restock potions and buy gear. It writes what happened, and when.
//
// The hand is the bot's and not the sim's: it only asks the realm what a player may ask --
// requests, potion right-clicks, drags, counters -- so whatever it finds (a jewel's wait, a
// breed that kills him, a shelf nobody can afford) is the game's answer and not a shortcut's.
// It draws from its own dice, seeded from the run's seed, as game/headless.cpp's hand does.
//
// Not the seeded hunt: `mu2 --headless` is the log the tests compare, and this changes nothing
// in it.
//
//   build/bot [--kin dk|dw|elf] [--seed N] [--runs N] [--hours H] [--until-jewel] [--quiet]

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "content/tables.h"
#include "core/log.h"
#include "sim/items.h"
#include "sim/market.h"
#include "sim/random.h"
#include "sim/realm.h"
#include "sim/skills.h"
#include "sim/wear.h"

using namespace mu;

namespace {

constexpr int kTown[2] = {138, 124};  // where a Lorencia character is born and rises
constexpr float kSight = 12.0f;       // MU's InfoRange, as the headless hand looks
constexpr float kLootReach = 10.0f;
constexpr int kThink = 5;             // ticks between decisions: four a second
constexpr int64_t kGiveUp = 30 * 20;  // a target not won in thirty seconds is out of reach
constexpr int64_t kForget = 5 * 60 * 20;
constexpr int64_t kFear = 15 * 60 * 20;  // a breed that killed him twice is left this long

// MU's NPC numbers (Tables::folk carries them).
constexpr int kAmy = 253, kHanzo = 251, kPasi = 254, kHarold = 250, kMartin = 248;

struct Options {
    sim::Kin kin = sim::Kin::DarkKnight;
    uint64_t seed = 1;
    int runs = 1;
    double hours = 4.0;
    bool untilJewel = false;
    bool quiet = false;
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

// What a run ends with, for the summary across seeds.
struct Outcome {
    int64_t ticks = 0;
    int level = 1, kills = 0, deaths = 0;
    int64_t zen = 0, firstJewelTick = -1;
    int firstJewelKills = 0, firstJewelLevel = 0;
    std::string firstJewel;
    int jewels = 0, runes = 0, trips = 0, bought = 0, sold = 0, drunk = 0;
};

class Bot {
public:
    Bot(const content::Tables& tables, sim::Realm& realm, const Options& options, uint64_t seed)
        : tables_(tables), realm_(realm), options_(options), dice_(seed ^ 0x424f54ull /* 'BOT' */) {
        for (size_t i = 0; i < tables.folk.size(); ++i) {
            const int npc = tables.folk[i].number;
            if (npc == kAmy) amy_ = int(i);
            if (npc == kHanzo) hanzo_ = int(i);
            if (npc == kPasi) pasi_ = int(i);
            if (npc == kHarold) harold_ = int(i);
            if (npc == kMartin) martin_ = int(i);
        }
    }

    // Before the realm's step: one decision every kThink ticks.
    void play() {
        const int64_t now = realm_.tick();
        if (now % kThink != 0) return;
        const sim::Body& hero = realm_.hero();
        if (!hero.alive()) {
            mode_ = Mode::Hunt;
            errands_.clear();
            return;
        }
        spend();
        drink();
        if (now >= nextSort_) {
            sortBag();
            nextSort_ = now + 100;
        }

        switch (mode_) {
            case Mode::Hunt: hunt(); break;
            case Mode::Rest: rest(); break;
            case Mode::Town: town(); break;
        }
    }

    // After the step: what happened to him.
    void heard() {
        const uint32_t me = realm_.hero().id;
        for (const sim::Happening& h : realm_.happenings()) {
            if (h.what == sim::What::Died) {
                if (h.who == me) {
                    ++out_.deaths;
                    const sim::Body* killer = bodyOf(h.whom);
                    const int breed = killer ? killer->kind : -1;
                    const std::string name = breed >= 0 ? tables_.kinds[size_t(breed)].label : "something";
                    if (breed >= 0) {
                        const int level = tables_.kinds[size_t(breed)].level;
                        if (++deathsTo_[level] >= 2) {
                            fearUntil_[level] = realm_.tick() + kFear;
                            deathsTo_[level] = 0;
                            say("killed by %s; leaves level-%d breeds for 15 min", name.c_str(), level);
                        } else {
                            say("killed by %s", name.c_str());
                        }
                    }
                } else if (h.whom == me) {
                    ++out_.kills;
                    const sim::Body* dead = bodyOf(h.who);
                    if (dead && dead->kind >= 0) ++killsOf_[tables_.kinds[size_t(dead->kind)].label];
                }
            } else if (h.what == sim::What::Dropped && h.b >= 0 && size_t(h.b) < tables_.items.size()) {
                const content::ItemRow& row = tables_.items[size_t(h.b)];
                if (sim::refiningJewel(row) || sim::creation(row)) {
                    sim::creation(row) ? ++out_.runes : ++out_.jewels;
                    if (out_.firstJewelTick < 0) {
                        out_.firstJewelTick = realm_.tick();
                        out_.firstJewel = row.label;
                        out_.firstJewelKills = out_.kills;
                        out_.firstJewelLevel = realm_.hero().level;
                    }
                    say("** %s dropped (kill %d, level %d)", row.label.c_str(), out_.kills,
                        realm_.hero().level);
                }
            }
        }
        const int level = realm_.hero().level;
        if (level > lastLevel_) {
            if (level / 10 > lastLevel_ / 10 || level <= 5) {
                say("level %d (%d kills, %lld zen)", level, out_.kills, (long long)realm_.money());
            }
            lastLevel_ = level;
        }
    }

    bool foundJewel() const { return out_.firstJewelTick >= 0; }

    Outcome finish() {
        out_.ticks = realm_.tick();
        out_.level = realm_.hero().level;
        out_.zen = realm_.money();
        return out_;
    }

    void printKills() const {
        std::vector<std::pair<int, std::string>> rows;
        for (const auto& [name, n] : killsOf_) rows.push_back({n, name});
        std::sort(rows.rbegin(), rows.rend());
        std::printf("  kills:");
        for (const auto& [n, name] : rows) std::printf(" %s %d,", name.c_str(), n);
        std::printf("\n  worn:");
        for (int slot = 0; slot < sim::kWorn; ++slot) {
            const sim::Held& one = realm_.satchel()[slot];
            if (!one.empty()) {
                std::printf(" %s%s", tables_.items[size_t(one.item)].label.c_str(),
                            one.refinement ? (" +" + std::to_string(one.refinement)).c_str() : "");
                std::printf(",");
            }
        }
        const sim::Body& hero = realm_.hero();
        std::printf("\n  damage %d-%d, defence %d, health %d, points %d/%d/%d/%d\n",
                    hero.stats.minimumDamage, hero.stats.maximumDamage, hero.stats.defense,
                    hero.maxHealth, hero.points.strength, hero.points.agility,
                    hero.points.vitality, hero.points.energy);
    }

private:
    enum class Mode { Hunt, Rest, Town };
    enum class Errand { Hanzo, Pasi, Harold, Amy };

    template <typename... A>
    void say(const char* format, A... args) {
        if (options_.quiet) return;
        std::printf("  [%s] ", clock(realm_.tick()).c_str());
        std::printf(format, args...);
        std::printf("\n");
    }

    const sim::Body* bodyOf(uint32_t id) const {
        for (const sim::Body& b : realm_.bodies()) {
            if (b.id == id) return &b;
        }
        return nullptr;
    }

    const content::ItemRow& rowOf(const sim::Held& held) const { return tables_.items[size_t(held.item)]; }

    // ---- points ---------------------------------------------------------------------------
    // Each class's usual build: the knight strength and vitality, the wizard energy, the elf
    // agility. The weights are the bot's, a common player's choice.
    void spend() {
        const int points = realm_.hero().pointsInHand;
        if (points <= 0) return;
        int w[4] = {4, 2, 3, 0};  // strength, agility, vitality, energy
        if (options_.kin == sim::Kin::DarkWizard) { w[0] = 2; w[1] = 2; w[2] = 2; w[3] = 4; }
        if (options_.kin == sim::Kin::FairyElf) { w[0] = 2; w[1] = 5; w[2] = 2; w[3] = 1; }
        const int sum = w[0] + w[1] + w[2] + w[3];
        int give[4];
        int left = points;
        for (int i = 0; i < 4; ++i) {
            give[i] = points * w[i] / sum;
            left -= give[i];
        }
        give[options_.kin == sim::Kin::DarkWizard ? 3 : options_.kin == sim::Kin::FairyElf ? 1 : 0] += left;
        realm_.spend(give[0], give[1], give[2], give[3]);
    }

    // ---- potions --------------------------------------------------------------------------
    int countOf(bool (*kind)(const content::ItemRow&)) const {
        int n = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_.satchel()[slot];
            if (!one.empty() && kind(rowOf(one))) n += std::max<int>(1, one.durability);
        }
        return n;
    }

    bool drinkOne(bool (*kind)(const content::ItemRow&)) {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_.satchel()[slot];
            if (!one.empty() && kind(rowOf(one)) && realm_.useItem(slot)) {
                ++out_.drunk;  // counted here: the next step clears what useItem says
                return true;
            }
        }
        return false;
    }

    bool casts() const {
        if (options_.kin != sim::Kin::DarkKnight) return true;
        for (int i = 0; i < sim::skillCount(); ++i) {
            if (realm_.knows(sim::skillAt(i).number)) return true;
        }
        return false;
    }

    void drink() {
        const sim::Body& hero = realm_.hero();
        if (hero.health * 2 < hero.maxHealth) drinkOne(sim::heals);
        if (casts() && hero.mana * 10 < hero.maxMana * 3) drinkOne(sim::restores);
    }

    // ---- the bag --------------------------------------------------------------------------
    int freeCells() const {
        bool taken[sim::kBagRows][sim::kBagColumns] = {};
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_.satchel()[slot];
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

    // How good he is now: his blow and his guard, as the realm has re-reckoned them.
    double score() const {
        const sim::Body& hero = realm_.hero();
        double blow = (hero.stats.minimumDamage + hero.stats.maximumDamage) / 2.0 +
                      (hero.stats.offhandMinimumDamage + hero.stats.offhandMaximumDamage) / 2.0;
        if (options_.kin == sim::Kin::DarkWizard) {
            const sim::Wearer w = realm_.wearer();
            blow = (w.wizardMinimum + w.wizardMaximum) / 2.0 * (1.0 + w.staffRise);
        }
        return blow * 2.0 + hero.stats.defense + hero.stats.defenseRate * 0.5;
    }

    // Tries each bag piece in each place it may go, and keeps it there only if he is better.
    // True when anything went on.
    bool wearBest() {
        bool any = false;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held held = realm_.satchel()[slot];
            if (held.empty()) continue;
            const content::ItemRow& row = rowOf(held);
            if (sim::placeOf(row) < 0 || sim::ammunition(row)) continue;
            // An elf keeps to the bow: her skills and her arrows are its.
            if (options_.kin == sim::Kin::FairyElf && (row.shield() || (row.weapon() && row.group != sim::kGroupBows))) continue;
            for (const int place : {int(sim::kWeaponRight), int(sim::kWeaponLeft), sim::placeOf(row)}) {
                if (!sim::placesIn(row, options_.kin, place)) continue;
                if (!sim::movable(tables_, realm_.wearer(), realm_.satchel(), slot, place)) continue;
                const bool wasEmpty = realm_.satchel()[place].empty();
                const double before = score();
                if (!realm_.moveItem(slot, place)) continue;
                if (score() > before + 0.5) {
                    say("wears %s%s", row.label.c_str(),
                        held.refinement ? (" +" + std::to_string(held.refinement)).c_str() : "");
                    any = true;
                    break;
                }
                // Back as it was.
                if (wasEmpty) realm_.moveItem(place, slot);
                else realm_.moveItem(slot, place);
            }
        }
        return any;
    }

    // Reads the orbs and scrolls he can; what he cannot is sold with the rest.
    void readOrbs() {
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_.satchel()[slot];
            if (one.empty() || rowOf(one).teaches == 0) continue;
            const std::string label = rowOf(one).label;
            if (realm_.useItem(slot)) say("learns from %s", label.c_str());
        }
    }

    void sortBag() {
        readOrbs();
        wearBest();
    }

    // What he keeps through a sale: jewels, runes, potions, and what he can still read.
    bool keeps(const sim::Held& one) const {
        const content::ItemRow& row = rowOf(one);
        return sim::refiningJewel(row) || sim::creation(row) || sim::heals(row) ||
               sim::restores(row) || sim::ammunition(row) || row.group == sim::kGroupPets;
    }

    // ---- hunting --------------------------------------------------------------------------
    // The level of the breed he hunts: the strongest a fight with costs him no more than a
    // third of his health, reckoned off his band and guard against the breed's (0.75's hit
    // chance, 1 - defence rate / attack rate, floored at 3%), and that has not killed him twice
    // lately. The weakest breed when none passes.
    static double hitChance(double attackRate, double defenseRate) {
        if (attackRate <= 0.0) return 0.03;
        return std::clamp(1.0 - defenseRate / attackRate, 0.03, 1.0);
    }
    double costOf(const content::MonsterKind& kind) const {
        const sim::Body& hero = realm_.hero();
        double blow = (hero.stats.minimumDamage + hero.stats.maximumDamage) / 2.0 +
                      (hero.stats.offhandMinimumDamage + hero.stats.offhandMaximumDamage) / 2.0;
        if (options_.kin == sim::Kin::DarkWizard) {
            const sim::Wearer w = realm_.wearer();
            blow = (w.wizardMinimum + w.wizardMaximum) / 2.0 * (1.0 + w.staffRise);
        }
        const double landed = std::max(1.0, blow - kind.defense) *
                              hitChance(hero.stats.attackRate, float(kind.defenseRate));
        const double seconds = kind.health / landed * std::max(1, hero.swingTicks) / 20.0;
        const double taken = std::max(0.0, (kind.minimumDamage + kind.maximumDamage) / 2.0 - hero.stats.defense) *
                             hitChance(float(kind.attackRate), hero.stats.defenseRate);
        return taken * seconds * 20.0 / std::max(1, kind.attackTicks);
    }
    int quarryLevel() const {
        const int64_t now = realm_.tick();
        const sim::Body& hero = realm_.hero();
        int best = -1, weakest = 1 << 30;
        for (const content::MonsterNest& nest : tables_.nests) {
            const content::MonsterKind& kind = tables_.kinds[nest.kind];
            weakest = std::min(weakest, kind.level);
            const auto fear = fearUntil_.find(kind.level);
            if (fear != fearUntil_.end() && fear->second > now) continue;
            if (costOf(kind) * 3.0 > hero.maxHealth) continue;
            if (kind.level > best) best = kind.level;
        }
        return best < 0 ? weakest : best;
    }

    bool barred(uint32_t id) const {
        const auto it = banned_.find(id);
        return it != banned_.end() && it->second > realm_.tick();
    }

    void ask(sim::Request request) {
        if (request.kind == sim::Request::Kind::Attack || request.kind == sim::Request::Kind::Pick) {
            if (request.target != chasing_) {
                chasing_ = request.target;
                chasedSince_ = realm_.tick();
            } else if (realm_.tick() - chasedSince_ > kGiveUp) {
                banned_[chasing_] = realm_.tick() + kForget;
                chasing_ = 0;
                return;
            }
        }
        if (options_.kin == sim::Kin::DarkWizard && request.kind == sim::Request::Kind::Attack) {
            request.skill = sim::skill::kEnergyBall;
        }
        realm_.ask(request);
    }

    // Presses a skill that is ready on the body he fights, in turn, as the headless hand does.
    void press(uint32_t at) {
        for (int n = 1; n <= sim::skillCount(); ++n) {
            const int i = (pressed_ + n) % sim::skillCount();
            const sim::SkillRow& row = sim::skillAt(i);
            if (!realm_.knows(row.number) || realm_.cooling(row.number) > 0) continue;
            if (realm_.hero().mana < row.mana) continue;
            realm_.invoke(row.number, at);
            pressed_ = i;
            return;
        }
    }

    // Whether he needs the town: potions out, the bag full, the gear worn down, or money enough
    // that a shelf may hold something better.
    const char* needsTown() const {
        const int64_t money = realm_.money();
        if (countOf(sim::heals) < 3 && money >= 240) return "out of potions";
        if (options_.kin == sim::Kin::DarkWizard && countOf(sim::restores) < 3 && money >= 240) {
            return "out of mana potions";
        }
        if (freeCells() < 6) return "bag full";
        for (int slot = 0; slot < sim::kWorn; ++slot) {
            const sim::Held& one = realm_.satchel()[slot];
            if (one.empty() || sim::ammunition(rowOf(one))) continue;
            const int full = sim::maximumDurability(rowOf(one), one);
            if (full > 0 && one.durability * 4 < full) return "gear worn down";
        }
        if (archer() && ammo() < 30 && money >= 70) return "out of arrows";
        if (realm_.tick() - lastTrip_ > 30 * 60 * 20 && money >= 5000) return "zen to spend";
        return nullptr;
    }

    // An elf's: the hand her bow or crossbow leaves for its ammunition (a bow is held left and
    // its arrows right, a crossbow right and its bolts left -- placeOf), or -1 with neither.
    int ammoHand() const {
        for (const int slot : {int(sim::kWeaponRight), int(sim::kWeaponLeft)}) {
            const sim::Held& one = realm_.satchel()[slot];
            if (!one.empty() && rowOf(one).group == sim::kGroupBows && !sim::ammunition(rowOf(one))) {
                return slot == sim::kWeaponRight ? sim::kWeaponLeft : sim::kWeaponRight;
            }
        }
        return -1;
    }
    bool archer() const { return ammoHand() >= 0; }
    // The shots she has for that hand, in it and in the bag.
    int ammo() const {
        const int hand = ammoHand();
        int n = 0;
        for (int slot = 0; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_.satchel()[slot];
            if (!one.empty() && sim::ammunition(rowOf(one)) && sim::placeOf(rowOf(one)) == hand) {
                n += one.durability;
            }
        }
        return n;
    }

    void hunt() {
        const sim::Body& hero = realm_.hero();
        // Low, and nothing to drink: back to town to sit it out.
        if (hero.health * 10 < hero.maxHealth * 3 && countOf(sim::heals) == 0) {
            say("retreats at %d/%d health, no potions", hero.health, hero.maxHealth);
            mode_ = Mode::Rest;
            return;
        }
        const int cap = quarryLevel();
        sim::Request request;
        // Whatever is on him first.
        float closest = 6.0f * 6.0f;
        bool engaged = false;
        for (const sim::Body& body : realm_.bodies()) {
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
        if (!engaged) freeSince_ = realm_.tick();
        const bool pressed = realm_.tick() - freeSince_ > 60 * 20 && hero.health * 10 > hero.maxHealth * 7;
        if (!engaged || pressed) {
            if (const char* why = needsTown()) {
                say("goes to town: %s", why);
                startTrip();
                return;
            }
        }
        // Then what fell, while the bag has room.
        if (!engaged) {
            closest = kLootReach * kLootReach;
            const int room = freeCells();
            for (const sim::Lying& one : realm_.lying()) {
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
        // Then the nearest he can take in sight.
        if (request.kind == sim::Request::Kind::None) {
            closest = kSight * kSight;
            for (const sim::Body& body : realm_.bodies()) {
                if (!body.monster() || !body.alive() || body.level > cap || barred(body.id)) continue;
                const float dx = body.x - hero.x, dy = body.y - hero.y;
                if (dx * dx + dy * dy < closest) {
                    closest = dx * dx + dy * dy;
                    request.kind = sim::Request::Kind::Attack;
                    request.target = body.id;
                }
            }
        }
        // Then off to the nearest of the breed he hunts, wherever it is.
        if (request.kind == sim::Request::Kind::None) {
            closest = 1e30f;
            for (const sim::Body& body : realm_.bodies()) {
                if (!body.monster() || !body.alive() || body.level != cap || barred(body.id)) continue;
                const float dx = body.x - hero.x, dy = body.y - hero.y;
                if (dx * dx + dy * dy < closest) {
                    closest = dx * dx + dy * dy;
                    request.kind = sim::Request::Kind::Attack;
                    request.target = body.id;
                }
            }
        }
        if (request.kind == sim::Request::Kind::None) return;
        ask(request);
        if (request.kind == sim::Request::Kind::Attack) press(request.target);
    }

    // Sits in the safe zone until he is nearly whole, then goes shopping if he must.
    void rest() {
        const sim::Body& hero = realm_.hero();
        if (!tables_.grid.safe(hero.column(), hero.row())) {
            if (!hero.walking) {
                sim::Request request;
                request.kind = sim::Request::Kind::WalkTo;
                request.column = kTown[0];
                request.row = kTown[1];
                realm_.ask(request);
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
    void startTrip() {
        errands_.clear();
        if (hanzo_ >= 0) errands_.push_back(hanzo_);
        if (options_.kin == sim::Kin::DarkWizard && pasi_ >= 0) errands_.push_back(pasi_);
        if (harold_ >= 0) errands_.push_back(harold_);
        // Martin wanders the west road with the Bone, Scale and Brass pieces: worth the walk
        // only with the Zen for one.
        if (martin_ >= 0 && realm_.money() >= 8000 + potionReserve()) errands_.push_back(martin_);
        if (amy_ >= 0) errands_.push_back(amy_);
        mode_ = Mode::Town;
        tripSince_ = realm_.tick();
        lastTrip_ = realm_.tick();
        ++out_.trips;
    }

    void town() {
        if (errands_.empty()) {
            mode_ = Mode::Hunt;
            return;
        }
        const int folk = errands_.front();
        if (realm_.trading() == folk) {
            serve(folk);
            realm_.closeTrade();
            errands_.erase(errands_.begin());
            tripSince_ = realm_.tick();
            return;
        }
        if (realm_.tick() - tripSince_ > 90 * 20) {  // could not reach him: the next one
            say("could not reach %s", tables_.folk[size_t(folk)].name.c_str());
            errands_.erase(errands_.begin());
            tripSince_ = realm_.tick();
            return;
        }
        sim::Request request;
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(folk);
        realm_.ask(request);
    }

    // What the potions he needs will cost, kept back from gear.
    int64_t potionReserve() const {
        const int64_t price = potionPrice(healTier());
        return price * 7 + (casts() ? price * 4 : 0);
    }
    // Small, medium or large: by how much a small one would mend of him.
    int healTier() const {
        const int maxHealth = realm_.hero().maxHealth;
        return maxHealth < 400 ? 0 : maxHealth < 1200 ? 1 : 2;
    }
    static int64_t potionPrice(int tier) { return tier == 0 ? 240 : tier == 1 ? 990 : 2200; }

    void serve(int folk) {
        const int npc = tables_.folk[size_t(folk)].number;
        const std::string& name = tables_.folk[size_t(folk)].name;
        // Sell what is not kept and not worn better.
        wearBest();
        int sold = 0;
        int64_t got = 0;
        for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
            const sim::Held& one = realm_.satchel()[slot];
            if (one.empty() || keeps(one)) continue;
            const int64_t paid = realm_.sellItem(slot);
            if (paid >= 0) {
                ++sold;
                got += paid;
            }
        }
        if (sold) {
            out_.sold += sold;
            say("sells %d things to %s for %lld zen", sold, name.c_str(), (long long)got);
        }
        if (realm_.mending()) {
            const int64_t cost = realm_.repairAllCost();
            if (cost > 0 && realm_.repairAll() > 0) say("repairs for %lld zen", (long long)cost);
        }
        int count = 0;
        const sim::Offer* shelf = sim::stockOf(npc, &count);
        if (npc == kAmy) {
            buyPotions(shelf, count);
        } else {
            buyGear(shelf, count);
        }
    }

    int buyOne(const sim::Offer& offer, const char* why) {
        const int item = tables_.itemAt(offer.group, offer.number);
        if (item < 0) return -1;
        const int64_t before = realm_.money();
        const int slot = realm_.buy(offer.slot);
        if (slot >= 0) {
            ++out_.bought;
            if (why) say("buys %s for %lld zen%s", tables_.items[size_t(item)].label.c_str(),
                         (long long)(before - realm_.money()), why);
        }
        return slot;
    }

    void buyPotions(const sim::Offer* shelf, int count) {
        const int tier = healTier();
        const auto find = [&](bool (*kind)(const content::ItemRow&), int pieces, int want) -> const sim::Offer* {
            int seen = 0;
            for (int i = 0; i < count; ++i) {
                const int item = tables_.itemAt(shelf[i].group, shelf[i].number);
                if (item < 0 || !kind(tables_.items[size_t(item)]) || shelf[i].pieces != pieces) continue;
                if (tables_.items[size_t(item)].number == 0 && kind == sim::heals) continue;  // the apple
                if (seen++ == want) return &shelf[i];
            }
            return nullptr;
        };
        // Mana first for a wizard, whose every blow is a spell; health first for the others.
        int bought = 0, mana = 0;
        const auto heal = [&](int upTo) {
            if (const sim::Offer* offer = find(sim::heals, 3, tier)) {
                while (countOf(sim::heals) < upTo && realm_.money() >= potionPrice(tier) &&
                       buyOne(*offer, nullptr) >= 0) bought += 3;
            }
        };
        const auto restore = [&](int upTo) {
            if (!casts()) return;
            if (const sim::Offer* offer = find(sim::restores, 3, tier)) {
                while (countOf(sim::restores) < upTo && realm_.money() >= potionPrice(tier) &&
                       buyOne(*offer, nullptr) >= 0) mana += 3;
            }
        };
        if (options_.kin == sim::Kin::DarkWizard) {
            heal(6);
            restore(30);
            heal(24);
        } else {
            heal(30);
            restore(21);
        }
        if (bought || mana) say("buys %d healing and %d mana potions (%lld zen left)", bought, mana,
                                (long long)realm_.money());
        const int hand = ammoHand();
        for (int i = 0; hand >= 0 && i < count; ++i) {
            const int item = tables_.itemAt(shelf[i].group, shelf[i].number);
            if (item < 0 || shelf[i].refinement) continue;
            const content::ItemRow& row = tables_.items[size_t(item)];
            if (!sim::ammunition(row) || sim::placeOf(row) != hand) continue;
            const int64_t price = sim::buyingPrice(row, 0, shelf[i].pieces, shelf[i].skill);
            while (ammo() < 200 && realm_.money() >= price) {
                const int slot = buyOne(shelf[i], " (ammunition)");
                if (slot < 0) break;
                if (realm_.satchel()[hand].empty()) realm_.moveItem(slot, hand);
            }
        }
    }

    // Gear and orbs from a shelf: an orb he can read, then any piece that makes him better,
    // within what is left over the potions he will need. A purchase that does not go on or
    // cannot be read is bought back at once.
    void buyGear(const sim::Offer* shelf, int count) {
        for (int i = 0; i < count; ++i) {
            const int item = tables_.itemAt(shelf[i].group, shelf[i].number);
            if (item < 0) continue;
            const content::ItemRow& row = tables_.items[size_t(item)];
            const bool orb = row.teaches != 0;
            if (!orb && (sim::placeOf(row) < 0 || sim::ammunition(row))) continue;
            if (orb && realm_.knows(row.teaches)) continue;
            const sim::Held held{item, int16_t(shelf[i].refinement), int16_t(1)};
            // `fits` is for what is worn; an orb is asked its class here and the rest by
            // useItem, which says no to what he cannot read yet.
            if (orb) {
                if (row.classes != 0 && (row.classes & (1 << int(options_.kin))) == 0) continue;
                if (realm_.hero().level < row.teachesLevel ||
                    realm_.hero().points.energy < row.teachesEnergy) continue;
            } else if (!sim::fits(tables_, realm_.wearer(), held)) {
                continue;
            }
            const int64_t price = sim::buyingPrice(row, shelf[i].refinement, shelf[i].pieces, shelf[i].skill);
            if (price > realm_.money() - potionReserve()) continue;
            if (!orb && !promising(row, shelf[i].refinement)) continue;
            const int slot = buyOne(shelf[i], nullptr);
            if (slot < 0) continue;
            if (orb) {
                if (realm_.useItem(slot)) {
                    say("buys %s for %lld zen and learns it", row.label.c_str(), (long long)price);
                } else {
                    realm_.buyBack();
                    --out_.bought;
                }
                continue;
            }
            if (wearBest() && realm_.satchel()[slot].item != item) {
                say("  (bought %s for %lld zen)", row.label.c_str(), (long long)price);
            } else {
                realm_.buyBack();
                --out_.bought;
            }
        }
    }

    // Worth buying to try on: a weapon with a bigger band, or armour with more defence, than
    // what he wears in its place.
    bool promising(const content::ItemRow& row, int plus) const {
        const int place = sim::placeOf(row);
        const sim::Held& worn = realm_.satchel()[place];
        if (options_.kin == sim::Kin::DarkWizard && row.weapon()) {
            return worn.empty() || row.magicPower > rowOf(worn).magicPower;
        }
        if (worn.empty()) return true;
        const content::ItemRow& has = rowOf(worn);
        if (row.weapon()) {
            return row.minimumDamage + row.maximumDamage + 2 * sim::damageBonus(plus) >
                   has.minimumDamage + has.maximumDamage + 2 * sim::damageBonus(worn.refinement);
        }
        return row.defense + sim::defenseBonus(row.shield(), plus) >
               has.defense + sim::defenseBonus(has.shield(), worn.refinement);
    }

    const content::Tables& tables_;
    sim::Realm& realm_;
    Options options_;
    sim::Random dice_;
    Outcome out_;
    Mode mode_ = Mode::Hunt;
    std::vector<int> errands_;
    int amy_ = -1, hanzo_ = -1, pasi_ = -1, harold_ = -1, martin_ = -1;
    int64_t tripSince_ = 0, lastTrip_ = 0, nextSort_ = 0, freeSince_ = 0;
    std::unordered_map<uint32_t, int64_t> banned_;
    std::map<int, int64_t> fearUntil_;
    std::map<int, int> deathsTo_;
    std::map<std::string, int> killsOf_;
    uint32_t chasing_ = 0;
    int64_t chasedSince_ = 0;
    int pressed_ = 0, lastLevel_ = 1;
};

const char* cradleWeapon(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard: return "Staff01";
        case sim::Kin::FairyElf: return "Bow01";
        case sim::Kin::DarkKnight: return "Axe01";
    }
    return "";
}

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
        else if (a == "--quiet") options.quiet = true;
        else {
            std::printf("usage: bot [--kin dk|dw|elf] [--seed N] [--runs N] [--hours H] "
                        "[--until-jewel] [--quiet]\n");
            return 2;
        }
    }
    core::logSilence(true);
    const std::string path = std::string(MU2_ASSET_DIR) + "/cooked/lorencia/lorencia.mur";
    content::Tables tables;
    std::string error;
    if (!content::loadTables(path, tables, error)) {
        std::printf("bot: %s: %s\n", path.c_str(), error.c_str());
        return 1;
    }

    std::vector<Outcome> outcomes;
    for (int run = 0; run < options.runs; ++run) {
        const uint64_t seed = options.seed + uint64_t(run);
        sim::Realm realm;
        if (!realm.raise(&tables, seed, kTown[0], kTown[1], options.kin, 1)) {
            std::printf("bot: the realm did not raise\n");
            return 1;
        }
        const int32_t weapon = tables.armNamed(cradleWeapon(options.kin));
        if (!realm.equip(weapon, -1)) realm.equip(weapon, -1, true);
        std::printf("%s, seed %llu\n", kinName(options.kin), (unsigned long long)seed);
        Bot bot(tables, realm, options, seed);
        const int64_t cap = int64_t(options.hours * 3600.0 * 20.0);
        while (realm.tick() < cap && !(options.untilJewel && bot.foundJewel())) {
            bot.play();
            realm.step();
            bot.heard();
        }
        const Outcome o = bot.finish();
        std::printf("  after %s: level %d, %d kills, %d deaths, %lld zen, %d trips to town, "
                    "%d bought, %d sold, %d potions drunk, %d jewels, %d runes\n",
                    clock(o.ticks).c_str(), o.level, o.kills, o.deaths, (long long)o.zen, o.trips,
                    o.bought, o.sold, o.drunk, o.jewels, o.runes);
        if (o.firstJewelTick >= 0) {
            std::printf("  first jewel: %s at %s, kill %d, level %d\n", o.firstJewel.c_str(),
                        clock(o.firstJewelTick).c_str(), o.firstJewelKills, o.firstJewelLevel);
        } else {
            std::printf("  no jewel\n");
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
    }
    return 0;
}
