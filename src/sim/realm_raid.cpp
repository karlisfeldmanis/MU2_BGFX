// The Golden Dragon as a raid boss (sim/raid.h; docs/golden-dragon-raid.md is the design).
//
// What the realm does: when the invasion's dragon stands, its health is the raid's (raidHealth),
// and four stages follow on its health -- the ground's bite, breath and roar; the flight's
// strafes and minions; the enraged meteors and their pools; the last stand's Golden Inferno, the
// wings' shadows its only shelter, and the hard enrage after eight minutes. Every big blow is a
// Hazard told before it lands (What::Raid, RaidEvent::Tell) so the drawing can mark the ground.
//
// And the party: the end-game characters of source/raid/party.json, the first worn by the hero
// and the rest raised as raiders -- bodies reckoned from their own kit by the hero's own rearm,
// whose blows roll as a guard's and a summon's do, and who stay down when they fall. Their mind
// (`raid`) is ours, as small as it can be and still play the fight: a role's place and target, the
// skills of its kit in order on their own cooldowns, a potion below two fifths, and a step out of
// a marked tile -- late by a reaction and, one tell in some, not at all.
//
// The rolls: the dragon's off `raidDice_`, the raiders' off `raiderDice_`, so a run with no raid
// is not moved by one.
#include "sim/realm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <tuple>

#include "core/log.h"
#include "sim/realm_tuning.h"
#include "sim/wear.h"

namespace mu::sim {
namespace {

int32_t itemNamed(const content::Tables& tables, const std::string& name) {
    for (size_t i = 0; i < tables.items.size(); ++i) {
        if (tables.items[i].name == name) return int32_t(i);
    }
    return -1;
}

// A kit's piece as `Realm::give` makes a thing: whole, with its options where its row takes them.
Held kitHeld(const content::Tables& tables, int32_t item, const KitPiece& piece) {
    const content::ItemRow& row = tables.items[size_t(item)];
    Held put{item, int16_t(piece.refinement), int16_t(fullDurability(row, piece.refinement))};
    if (takesOptions(row) || jewellery(row)) {
        put.luck = piece.luck;
        put.option = int8_t(std::clamp(piece.option, 0, kMostOption));
        put.excellent = uint8_t(piece.excellent & 63);
        put.sockets = uint8_t(std::min<int>(piece.sockets, mostSocketsOf(row)));
        for (int i = 0; i < put.sockets; ++i) put.powers[i] = piece.powers[i];
        if (put.excellent && wears(row)) put.durability = int16_t(maximumDurability(row, put));
    }
    return put;
}

// The kit worn on a scratch satchel through the real gates (sim::movable), or why not.
std::string kitInto(const content::Tables& tables, const RaiderKit& kit, Satchel* bag) {
    Wearer who;
    who.kin = kit.kin;
    who.level = kit.level;
    who.points = kit.points;
    who.second = kit.second;
    for (const KitPiece& piece : kit.pieces) {
        const int32_t item = itemNamed(tables, piece.item);
        if (item < 0) return kit.name + ": no item named '" + piece.item + "'";
        if (!wearable(piece.slot)) return kit.name + ": '" + piece.item + "' in no worn slot";
        const Held held = kitHeld(tables, item, piece);
        bag->put(kWorn, held);
        if (!movable(tables, who, *bag, kWorn, piece.slot)) {
            // Why, as the item card would say it: what it asks that he has not.
            const content::ItemRow& row = tables.items[size_t(item)];
            const Needs short_ =
                shortOf(asks(row, held.refinement, held.excellent != 0), who.level, who.points);
            char why[160];
            std::snprintf(why, sizeof(why),
                          " (short: level %d, strength %d, agility %d, vitality %d, energy %d%s%s)",
                          short_.level, short_.strength, short_.agility, short_.vitality,
                          short_.energy, archangelWeapon(row) ? "; an Archangel weapon" : "",
                          secondClassOnly(row) && !who.second ? "; a second's" : "");
            return kit.name + ": '" + piece.item + "' refused in slot " +
                   std::to_string(piece.slot) + why;
        }
        move(tables, who, *bag, kWorn, piece.slot);
    }
    return std::string();
}

float shareOf(const Body& one) {
    return one.maxHealth > 0 ? float(one.health) / float(one.maxHealth) : 0.0f;
}

}  // namespace

std::string Realm::kitRefusal(const RaiderKit& kit) const {
    if (!tables_) return "no tables";
    Satchel scratch;
    return kitInto(*tables_, kit, &scratch);
}

// ---- raised with the realm ------------------------------------------------------------------

void Realm::raiseRaid() {
    raid_ = RaidState{};
    raid_.players = raidPlayers_;
    raiderSlots_.clear();
    minionSlots_.clear();
    raiderBags_.clear();
    for (Hazard& one : hazards_) one = Hazard{};
    for (int i = 0; i <= kRaidersMost; ++i) {
        reactAt_[i] = 0;
        reactSerial_[i] = 0;
        raidPotions_[i] = 0;
        drinkAt_[i] = 0;
        raidNext_[i] = 0;
    }
    if (invaderSlot_ < 0 || !raidAsked_) return;
    {
        // Its health and its blow as the raid's (sim/raid.h), laid on before it ever rises.
        Body& dragon = bodies_[size_t(invaderSlot_)];
        dragon.maxHealth = raidHealth(raid_.players);
        dragon.stats.minimumDamage = int(float(dragon.stats.minimumDamage) * kRaidBlowScale);
        dragon.stats.maximumDamage = int(float(dragon.stats.maximumDamage) * kRaidBlowScale);
    }
    // The minions: kMinionsMost bodies of OpenMU's Golden Budge Dragon, down until a wave.
    int32_t minionKind = -1;
    for (size_t i = 0; i < tables_->kinds.size(); ++i) {
        if (tables_->kinds[i].number == kGoldenBudgeDragonNumber) minionKind = int32_t(i);
    }
    if (minionKind < 0) {
        core::logf("raid: breed %d is not in map %u's tables; no minions", kGoldenBudgeDragonNumber,
                   tables_->map);
    } else {
        const content::MonsterKind& kind = tables_->kinds[size_t(minionKind)];
        for (int i = 0; i < kMinionsMost; ++i) {
            Body minion;
            minion.id = nextId_++;
            minion.kind = minionKind;
            minion.level = kind.level;
            minion.maxHealth = int(float(kind.health) * kMinionHealthScale);
            minion.health = 0;
            minion.stats.level = kind.level;
            minion.stats.attackRate = kind.attackRate;
            minion.stats.defenseRate = kind.defenseRate;
            minion.stats.defense = kind.defense;
            minion.stats.minimumDamage = int(float(kind.minimumDamage) * kMinionBlowScale);
            minion.stats.maximumDamage = int(float(kind.maximumDamage) * kMinionBlowScale);
            minion.swingTicks = kind.attackTicks;
            minion.speed = 1.0f / float(std::max(1, kind.moveTicks));
            minion.nest = -1;
            minion.temper = Temper::Dead;
            minion.risesAt = std::numeric_limits<int64_t>::max();
            minion.route.reserve(64);
            minionSlots_.push_back(int(bodies_.size()));
            bodies_.push_back(std::move(minion));
        }
    }
    if (party_.empty()) return;
    // The hero wears the first kit; the rest stand beside him as raiders.
    if (const std::string why = kitRefusal(party_[0]); !why.empty()) {
        core::logError("raid: the hero's kit refused: %s", why.c_str());
    } else {
        dressHero(party_[0]);
    }
    raidPotions_[0] = party_[0].potions;
    const Body& hero = bodies_[0];
    const int count = std::min<int>(int(party_.size()) - 1, kRaidersMost);
    raiderBags_.reserve(size_t(count));
    for (int k = 1; k <= count; ++k) {
        const RaiderKit& kit = party_[size_t(k)];
        Satchel bag;
        if (const std::string why = kitInto(*tables_, kit, &bag); !why.empty()) {
            core::logError("raid: kit refused: %s", why.c_str());
            continue;
        }
        // Round him in a ring of two tiles, in party order.
        const float angle = float(k) * 6.2831853f / float(std::max(1, count));
        int column = hero.column(), row = hero.row();
        router_.nearestOpen(hero.column() + int(std::lround(std::cos(angle) * 2.0f)),
                            hero.row() + int(std::lround(std::sin(angle) * 2.0f)),
                            content::kWallCharacter, 6, &column, &row);
        Body one;
        one.id = nextId_++;
        one.raider = int32_t(raiderSlots_.size());
        one.kind = -1;
        one.kin = kit.kin;
        one.second = kit.second;
        one.level = std::clamp(kit.level, 1, kMaximumLevel);
        one.points = kit.points;
        one.x = float(column);
        one.y = float(row);
        one.homeColumn = column;
        one.homeRow = row;
        one.facing = one.aim = hero.facing;
        one.temper = Temper::Wandering;
        one.speed = 1.0f / float(kHeroMoveTicks);
        one.route.reserve(64);
        // It knows what its kit throws.
        for (const int32_t skill : kit.skills) {
            const int at = skillIndexOf(skill);
            if (at >= 0) one.learned |= uint64_t(1) << at;
        }
        raidPotions_[k] = kit.potions;
        raiderBags_.push_back(bag);
        raiderSlots_.push_back(int(bodies_.size()));
        bodies_.push_back(std::move(one));
    }
    for (size_t i = 0; i < raiderSlots_.size(); ++i) {
        Body& one = bodies_[size_t(raiderSlots_[i])];
        rearm(one, raiderBags_[i]);
        one.health = one.maxHealth;
        one.mana = one.maxMana;
        core::logf("raid: raider %zu %s, level %d, health %d, mana %d, damage %d-%d, defence %d",
                   i + 1, party_[i + 1].name.c_str(), one.level, one.maxHealth, one.maxMana,
                   one.stats.minimumDamage, one.stats.maximumDamage, one.stats.defense);
    }
}

void Realm::dressHero(const RaiderKit& kit) {
    Body& hero = bodies_[0];
    hero.level = std::clamp(kit.level, 1, kMaximumLevel);
    hero.experience = neededExperience(hero.level);
    hero.points = kit.points;
    hero.pointsInHand = 0;
    hero.second = kit.second;
    // And the skills of his kit, learned, for the bar.
    for (const int32_t skill : kit.skills) {
        const int at = skillIndexOf(skill);
        if (at >= 0) hero.learned |= uint64_t(1) << at;
    }
    for (const KitPiece& piece : kit.pieces) {
        const int32_t item = itemNamed(*tables_, piece.item);
        if (item < 0) continue;
        bag_.put(piece.slot, kitHeld(*tables_, item, piece));
    }
    rearm(hero);
    hero.health = hero.maxHealth;
    hero.mana = hero.maxMana;
    core::logf("raid: the hero %s, level %d, health %d, mana %d, damage %d-%d, defence %d",
               kit.name.c_str(), hero.level, hero.maxHealth, hero.maxMana, hero.stats.minimumDamage,
               hero.stats.maximumDamage, hero.stats.defense);
}

void Realm::minionSpoils(const Body& dead) {
    if (raid_.stage == RaidStage::None || dead.kind < 0 || size_t(dead.kind) >= tables_->kinds.size() ||
        tables_->kinds[size_t(dead.kind)].number != kGoldenBudgeDragonNumber) {
        return;
    }
    const Body& hero = bodies_[0];
    // The rune's rarity by the dragon's level, not the little one's fifteen (a Common there).
    const int level = invaderSlot_ >= 0 ? bodies_[size_t(invaderSlot_)].level : dead.level;
    const auto pick = [&](auto&& admits) -> int32_t {
        int count = 0;
        for (const content::ItemRow& row : tables_->items) count += admits(row) ? 1 : 0;
        if (count == 0) return -1;
        int at = raidDice_.nextInt(0, count);
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            if (admits(tables_->items[i]) && at-- == 0) return int32_t(i);
        }
        return -1;
    };
    const auto lay = [&](const Held& what) {
        Lying one;
        one.what = what;
        std::tie(one.column, one.row) = clearing(dead.column(), dead.row());
        one.vanishesAt = tick_ + kSpoilsLingerTicks;
        one.id = nextId_++;
        lying_.push_back(one);
        say(What::Dropped, dead, int32_t(one.id), what.item, 0);
        core::logf("raid: a minion leaves %s%s", tables_->items[size_t(what.item)].name.c_str(),
                   what.powers[0] != 0 ? " with its power" : "");
    };
    // Its own Box of Luck, always.
    for (size_t i = 0; i < tables_->items.size(); ++i) {
        if (boxOfLuck(tables_->items[i])) {
            lay(Held{int32_t(i), 0, 1});
            break;
        }
    }
    if (raidDice_.nextInt(0, kMinionJewelOdds) == 0) {
        const int32_t item = pick([](const content::ItemRow& r) { return refiningJewel(r); });
        if (item >= 0) lay(Held{item, 0, 1});
    }
    if (raidDice_.nextInt(0, kMinionRuneOdds) == 0) {
        const int32_t item = pick([](const content::ItemRow& r) { return creation(r); });
        if (item >= 0) {
            Held rune{item, 0, 1};
            rune.powers[0] = drawRunePower(raidDice_, hero.kin, hero.second, level, false);
            lay(rune);
        }
    }
}

void Realm::dragonHoard(const Body& dragon) {
    if (raid_.stage == RaidStage::None || !tables_) return;
    const Body& hero = bodies_[0];
    const auto first = [this](auto&& admits) -> int32_t {
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            if (admits(tables_->items[i])) return int32_t(i);
        }
        return -1;
    };
    // What it leaves, in order, laid round it evenly from a turn the dice choose.
    Held hoard[kHoardBoxes + kHoardRunes + kHoardJewels + 1 + kHoardExcellents + 1];
    int count = 0;
    const int32_t box = first([](const content::ItemRow& r) { return boxOfKundun(r); });
    for (int i = 0; i < kHoardBoxes && box >= 0; ++i) {
        hoard[count++] = Held{box, int16_t(1 + raidDice_.nextInt(0, kKundunTiers)), 1};
    }
    const int32_t rune = first([](const content::ItemRow& r) { return creation(r); });
    for (int i = 0; i < kHoardRunes && rune >= 0; ++i) {
        Held one{rune, 0, 1};
        one.powers[0] = drawRunePower(raidDice_, hero.kin, hero.second, dragon.level, false);
        hoard[count++] = one;
    }
    int jewels = 0;
    for (const content::ItemRow& r : tables_->items) jewels += refiningJewel(r) ? 1 : 0;
    for (int i = 0; i < kHoardJewels && jewels > 0; ++i) {
        int at = raidDice_.nextInt(0, jewels);
        for (size_t k = 0; k < tables_->items.size(); ++k) {
            if (refiningJewel(tables_->items[k]) && at-- == 0) hoard[count++] = Held{int32_t(k), 0, 1};
        }
    }
    const int excellents = kHoardExcellents + (raidDice_.nextInt(0, kHoardExcellentOdds) == 0 ? 1 : 0);
    for (int i = 0; i < excellents; ++i) {
        if (excellentOf(kKundunExcellent3, std::size(kKundunExcellent3), raidDice_, &hoard[count])) ++count;
    }
    if (raidDice_.nextInt(0, kHoardFeatherOdds) == 0) {
        const int32_t feather = first([](const content::ItemRow& r) { return lochsFeather(r); });
        if (feather >= 0) hoard[count++] = Held{feather, 0, 1};
    }
    const double turn = raidDice_.nextDouble() * 6.283185307179586;
    for (int i = 0; i < count; ++i) {
        const double angle = turn + 6.283185307179586 * double(i) / double(count);
        const int c = int(std::lround(dragon.x + std::cos(angle) * kHoardReach));
        const int r = int(std::lround(dragon.y + std::sin(angle) * kHoardReach));
        Lying one;
        one.what = hoard[i];
        std::tie(one.column, one.row) = clearing(c, r);
        one.vanishesAt = tick_ + kHoardLingerTicks;
        one.id = nextId_++;
        lying_.push_back(one);
        say(What::Dropped, dragon, int32_t(one.id), one.what.item, one.what.refinement);
        core::logf("raid: the dragon leaves %s +%d%s", tables_->items[size_t(one.what.item)].name.c_str(),
                   one.what.refinement, one.what.excellent ? ", excellent" : "");
    }
}

bool Realm::shrugs(const Body& one) {
    if (!isBoss(one)) return false;
    if (tick_ - raid_.immuneSaidAt >= kImmuneSayTicks) {
        raid_.immuneSaidAt = tick_;
        say(What::Raid, one, int32_t(RaidEvent::Immune));
    }
    return true;
}

const Satchel& Realm::kitOf(const Body& one) const {
    return one.raider >= 0 && size_t(one.raider) < raiderBags_.size() ? raiderBags_[size_t(one.raider)]
                                                                       : bag_;
}

int Realm::partyIndex(const Body& one) const {
    if (one.player) return 0;
    if (one.raider >= 0) return one.raider + 1;
    if (one.summoner != 0) return kRaidersMost + 1;
    return -1;
}

void Realm::raidSkipTo(RaidStage stage) {
    if (invaderSlot_ < 0) return;
    Body& dragon = bodies_[size_t(invaderSlot_)];
    if (!dragon.alive()) return;
    const int percent = stage == RaidStage::Flight      ? kFlightAt
                        : stage == RaidStage::Enraged   ? kEnragedAt
                        : stage == RaidStage::LastStand ? kLastStandAt
                                                        : 100;
    dragon.health = std::max(1, int(int64_t(dragon.maxHealth) * percent / 100));
}

// ---- the dragon's clock -----------------------------------------------------------------------

void Realm::raidTick() {
    if (invaderSlot_ < 0 || !raidAsked_) return;
    Body& dragon = bodies_[size_t(invaderSlot_)];
    if (raid_.stage == RaidStage::None) {
        if (!dragon.alive() || invasion_.phase != InvasionPhase::Standing) return;
        raid_.landedAt = tick_;
        raid_.nextMove = tick_ + kRiseIdleTicks + kMoveEvery / 2;
        beginStage(dragon, RaidStage::Ground);
        return;
    }
    if (!dragon.alive()) {
        // Killed, or its thirty minutes up: the fight is over, and what it laid goes with it.
        raid_.stage = RaidStage::None;
        raid_.aloft = false;
        for (Hazard& one : hazards_) one = Hazard{};
        return;
    }
    // **Not killed in time, it leaves** (the user, 2026-10-06: 'only when dragons is not killed
    // on time they fly away and weather clears'): up, untouchable, and gone kDepartTicks later,
    // the invasion over and the storm with it. It flies only to come and to go -- 'dragon never
    // flies again during the fight, if he landed he fights on legs'.
    if (raid_.departing) {
        if (tick_ >= raid_.departsAt) {
            dragon.health = 0;
            dragon.quarry = 0;
            halt(dragon);
            dropBlow(dragon);
            for (Body& one : bodies_) {
                if (one.quarry == dragon.id) {
                    one.quarry = 0;
                    one.provoked = false;
                }
            }
            say(What::Dismissed, dragon);
            core::logf("raid: the dragon flies away at tick %lld", (long long)tick_);
            raid_.stage = RaidStage::None;
            raid_.aloft = false;
            for (Hazard& one : hazards_) one = Hazard{};
            endInvasion();
        }
        return;
    }
    // The swarm's nest is its master: each minion's home is the dragon's tile, so its leash pulls
    // it back to the fight -- ten runed fighters' pushes and the chase after a raider running
    // back from town had walked one twenty-four tiles off (the audit, 2026-10-06).
    for (int slot : minionSlots_) {
        Body& minion = bodies_[size_t(slot)];
        if (!minion.alive()) continue;
        minion.homeColumn = dragon.column();
        minion.homeRow = dragon.row();
    }
    if (tick_ - raid_.landedAt >= kHardEnrage) {
        raid_.departing = true;
        raid_.departsAt = tick_ + kDepartTicks;
        raid_.aloft = true;
        halt(dragon);
        dropBlow(dragon);
        for (Hazard& one : hazards_) one = Hazard{};
        say(What::Raid, dragon, int32_t(RaidEvent::Aloft), 1);
        return;
    }
    const int percent = int(int64_t(dragon.health) * 100 / std::max(1, dragon.maxHealth));
    if (raid_.stage == RaidStage::Ground && percent <= kFlightAt) {
        beginStage(dragon, RaidStage::Flight);
    }
    if (raid_.stage == RaidStage::Flight && !raid_.secondWave && percent <= kSecondWaveAt) {
        raid_.secondWave = true;
        minionWave(dragon);
    }
    if (raid_.stage <= RaidStage::Flight && percent <= kEnragedAt) {
        beginStage(dragon, RaidStage::Enraged);
    }
    if (raid_.stage == RaidStage::Enraged && percent <= kLastStandAt) {
        beginStage(dragon, RaidStage::LastStand);
    }
    // The second stage's fire from the sky, in lines, while it fights on.
    if (raid_.stage == RaidStage::Flight && tick_ >= raid_.nextStrafe) strafe(dragon);
    if (raid_.stage >= RaidStage::Enraged && tick_ >= raid_.nextStorm) storm(dragon);
    if (raid_.stage == RaidStage::LastStand && tick_ >= raid_.nextInferno) inferno(dragon, true);
    if (tick_ >= raid_.nextMove && tick_ >= raid_.busyUntil && tick_ >= dragon.wakesAt) {
        raidMove(dragon);
    }
    hazardTick(dragon);
}

void Realm::beginStage(Body& dragon, RaidStage stage) {
    raid_.stage = stage;
    say(What::Raid, dragon, int32_t(RaidEvent::Stage), int32_t(stage));
    core::logf("raid: stage %d at tick %lld, %d of %d health", int(stage), (long long)tick_,
               dragon.health, dragon.maxHealth);
    switch (stage) {
        case RaidStage::Flight:
            // On its legs still: it roars its minions down and fire rains in lines.
            raid_.nextStrafe = tick_ + 60;
            minionWave(dragon);
            break;
        case RaidStage::Enraged:
            raid_.nextStorm = tick_ + 60;
            break;
        case RaidStage::LastStand:
            raid_.nextInferno = tick_ + 100;
            break;
        default:
            break;
    }
}

Hazard* Realm::layHazard(const Hazard& one) {
    // A full list gives up the pool nearest its end for anything told: a burning tile matters
    // less than a blow on its way.
    Hazard* free = nullptr;
    for (Hazard& at : hazards_) {
        if (at.kind == HazardKind::None) {
            free = &at;
            break;
        }
    }
    if (free == nullptr && one.kind != HazardKind::Pool) {
        for (Hazard& at : hazards_) {
            if (at.kind == HazardKind::Pool && (free == nullptr || at.endsAt < free->endsAt)) free = &at;
        }
        if (free != nullptr) *free = Hazard{};
    }
    for (Hazard& at : hazards_) {
        if (&at != free) continue;
        at = one;
        at.serial = raid_.serial;
        // A pool is not told: it is the fire the meteor left, seen as it lands.
        if (one.kind != HazardKind::Pool && invaderSlot_ >= 0) {
            say(What::Raid, bodies_[size_t(invaderSlot_)], int32_t(RaidEvent::Tell),
                int32_t(one.kind), int32_t(one.landsAt - tick_));
            happenings_.back().x = one.x;
            happenings_.back().y = one.y;
        }
        return &at;
    }
    return nullptr;
}

void Realm::raidMove(Body& dragon) {
    raid_.nextMove = tick_ + kMoveEvery;
    // The party near it: a Roar Shock when two or more are under it, else a Breath at the
    // densest of them in reach.
    int under = 0;
    const Body* aim = nullptr;
    int crowd = -1;
    for (const Body& one : bodies_) {
        if (!partisan(one) || !one.alive()) continue;
        const float gap = std::hypot(one.x - dragon.x, one.y - dragon.y);
        if (gap <= kShockReach + 0.5f) ++under;
        if (gap > kBreathReach) continue;
        int near = 0;
        for (const Body& other : bodies_) {
            if (partisan(other) && other.alive() && std::hypot(other.x - one.x, other.y - one.y) <= 3.0f) {
                ++near;
            }
        }
        if (near > crowd) {
            crowd = near;
            aim = &one;
        }
    }
    ++raid_.serial;
    Hazard move;
    move.x = dragon.x;
    move.y = dragon.y;
    if (under >= 2 && raid_.stage >= RaidStage::Enraged) {
        move.kind = HazardKind::Hellfire;
        move.reach = kHellfireReach;
        move.share = kHellfireShare;
        move.landsAt = tick_ + kHellfireTell;
        move.endsAt = move.landsAt;
    } else if (under >= 2) {
        move.kind = HazardKind::Shock;
        move.reach = kShockReach;
        move.share = kShockShare;
        move.landsAt = tick_ + kShockTell;
        move.endsAt = move.landsAt;
    } else if (aim != nullptr) {
        move.kind = HazardKind::Breath;
        move.reach = kBreathReach;
        move.facing = std::atan2(aim->y - dragon.y, aim->x - dragon.x);
        move.share = kBreathShare;
        move.landsAt = tick_ + kBreathTell;
        move.endsAt = move.landsAt + kBreathTicks;
        move.nextAt = move.landsAt;
        dragon.aim = dragon.facing = move.facing;
    } else {
        return;
    }
    halt(dragon);
    raid_.busyUntil = move.endsAt + 1;
    dragon.swingsAt = std::max(dragon.swingsAt, raid_.busyUntil);
    layHazard(move);
}

void Realm::strafe(Body& dragon) {
    raid_.nextStrafe = tick_ + kStrafeEvery;
    // Through the densest of the party, at a bearing of its own.
    const Body* aim = nullptr;
    int crowd = -1;
    for (const Body& one : bodies_) {
        if (!partisan(one) || !one.alive() || !within(dragon, one, kInfernoReach)) continue;
        int near = 0;
        for (const Body& other : bodies_) {
            if (partisan(other) && other.alive() && std::hypot(other.x - one.x, other.y - one.y) <= 3.0f) {
                ++near;
            }
        }
        if (near > crowd) {
            crowd = near;
            aim = &one;
        }
    }
    if (aim == nullptr) return;
    ++raid_.serial;
    const float bearing = float(raidDice_.nextDouble() * 6.283185307179586);
    const float c = std::cos(bearing), s = std::sin(bearing);
    for (int i = 0; i < kStrafeFires; ++i) {
        const float along = -kStrafeLength * 0.5f + kStrafeLength * float(i) / float(kStrafeFires - 1);
        Hazard fire;
        fire.kind = HazardKind::Impact;
        fire.x = aim->x + c * along;
        fire.y = aim->y + s * along;
        fire.reach = kImpactReach;
        fire.share = kStrafeShare;
        fire.landsAt = tick_ + kImpactTell + kImpactGap * i;
        fire.endsAt = fire.landsAt;
        layHazard(fire);
    }
}

void Realm::storm(Body& dragon) {
    raid_.nextStorm = tick_ + kMeteorStormEvery;
    ++raid_.serial;
    for (const Body& one : bodies_) {
        if (!partisan(one) || !one.alive() || !within(dragon, one, kInfernoReach)) continue;
        Hazard rock;
        rock.kind = HazardKind::Impact;
        rock.x = float(one.column());
        rock.y = float(one.row());
        rock.reach = kImpactReach;
        rock.share = kMeteorShare;
        rock.landsAt = tick_ + kImpactTell;
        rock.endsAt = rock.landsAt;
        rock.pools = true;
        layHazard(rock);
    }
}

void Realm::inferno(Body& dragon, bool shadows) {
    raid_.nextInferno = tick_ + kInfernoEvery;
    ++raid_.serial;
    Hazard fire;
    fire.kind = HazardKind::Inferno;
    fire.x = dragon.x;
    fire.y = dragon.y;
    // The hard enrage's reaches past any step out of it (kWipeReach): nowhere to stand.
    fire.reach = shadows ? kInfernoReach : kWipeReach;
    fire.share = shadows ? kInfernoShare : kWipeShare;
    fire.landsAt = tick_ + kInfernoTell;
    fire.endsAt = fire.landsAt;
    if (shadows) {
        // Under its wings, left and right of its facing; and behind it with more than five.
        const float f = dragon.facing;
        const float bearings[kShadowsMost] = {f + 1.5707963f, f - 1.5707963f, f + 3.1415927f};
        fire.shades = raid_.players > 5 ? 3 : 2;
        for (int i = 0; i < fire.shades; ++i) {
            int column = 0, row = 0;
            const int c = int(std::lround(dragon.x + std::cos(bearings[i]) * kShadowOut));
            const int r = int(std::lround(dragon.y + std::sin(bearings[i]) * kShadowOut));
            if (!router_.nearestOpen(c, r, content::kWallCharacter, 3, &column, &row)) {
                column = c;
                row = r;
            }
            fire.shadeX[i] = float(column);
            fire.shadeY[i] = float(row);
        }
    }
    if (layHazard(fire) != nullptr) {
        for (int i = 0; i < fire.shades; ++i) {
            say(What::Raid, dragon, int32_t(RaidEvent::Shadow), i);
            happenings_.back().x = fire.shadeX[i];
            happenings_.back().y = fire.shadeY[i];
        }
    }
    halt(dragon);
    raid_.busyUntil = std::max(raid_.busyUntil, fire.landsAt + 1);
    dragon.swingsAt = std::max(dragon.swingsAt, raid_.busyUntil);
}

void Realm::minionWave(Body& dragon) {
    const int want = minionsFor(raid_.players);
    int came = 0;
    for (int slot : minionSlots_) {
        if (came >= want) break;
        Body& minion = bodies_[size_t(slot)];
        if (minion.alive() || minion.risesAt == tick_) continue;
        // Down round it, four to nine tiles out, on a tile a body may stand on.
        int column = -1, row = -1;
        for (int attempt = 0; attempt < 12 && column < 0; ++attempt) {
            const double angle = raidDice_.nextDouble() * 6.283185307179586;
            const double out = 4.0 + raidDice_.nextDouble() * 5.0;
            const int c = int(std::lround(dragon.x + std::cos(angle) * out));
            const int r = int(std::lround(dragon.y + std::sin(angle) * out));
            if (tables_->grid.open(c, r, content::kWallCharacter) && !tables_->grid.safe(c, r)) {
                column = c;
                row = r;
            }
        }
        if (column < 0) continue;
        minion.homeColumn = column;
        minion.homeRow = row;
        minion.x = float(column);
        minion.y = float(row);
        minion.risesAt = tick_;
        ++came;
    }
    halt(dragon);
    dropBlow(dragon);
    raid_.busyUntil = std::max(raid_.busyUntil, tick_ + kSummonTicks);
    dragon.swingsAt = std::max(dragon.swingsAt, raid_.busyUntil);
    say(What::Raid, dragon, int32_t(RaidEvent::Wave), came);
    core::logf("raid: %d minions at tick %lld", came, (long long)tick_);
}

bool Realm::inHazard(const Hazard& h, float x, float y) const {
    const float dx = x - h.x, dy = y - h.y;
    switch (h.kind) {
        case HazardKind::Breath: {
            const float gap = std::hypot(dx, dy);
            if (gap > h.reach) return false;
            return gap < 1.0f || std::fabs(wrapped(std::atan2(dy, dx) - h.facing)) <= kBreathHalfAngle;
        }
        case HazardKind::Inferno: {
            if (std::max(std::fabs(dx), std::fabs(dy)) > h.reach) return false;
            for (int i = 0; i < h.shades; ++i) {
                if (std::hypot(x - h.shadeX[i], y - h.shadeY[i]) <= kShadowReach) return false;
            }
            return true;
        }
        case HazardKind::None:
            return false;
        default:
            return std::max(std::fabs(dx), std::fabs(dy)) <= h.reach;
    }
}

void Realm::scorch(Body& dragon, Body& one, float share) {
    if (!one.alive()) return;
    // A share of what it can hold, through what turns harm aside -- a guard, a barrier, a pet
    // (Fighter::damageTaken) -- and the shield's nine tenths as a blow's (Realm::strikeAt).
    const int blow = std::max(1, int(double(share) * double(one.maxHealth) * one.stats.damageTaken));
    int wound = blow;
    if (one.sd > 0) {
        const int onto = int(float(blow) * kShieldShare);
        const int over = onto - one.sd;
        one.sd = std::max(0, one.sd - onto);
        wound = blow - onto + std::max(0, over);
    }
    one.health = std::max(0, one.health - wound);
    say(What::Hit, dragon, blow, blow, one.health, one.id);
    // Thrown, and not a boss's Flame of Evil: the drawing shows the hazard's own fire
    // (play_raid.cpp), and a `boss` hit is drawn as a Balrog's storm of meteors.
    happenings_.back().thrown = true;
    if (one.health <= 0) kill(one, dragon);
}

void Realm::hazardTick(Body& dragon) {
    for (Hazard& h : hazards_) {
        if (h.kind == HazardKind::None || tick_ < h.landsAt) continue;
        if (tick_ == h.landsAt && h.kind != HazardKind::Pool) {
            say(What::Raid, dragon, int32_t(RaidEvent::Strike), int32_t(h.kind));
            happenings_.back().x = h.x;
            happenings_.back().y = h.y;
        }
        const bool pulse = h.kind == HazardKind::Breath || h.kind == HazardKind::Pool;
        if (!pulse || tick_ >= h.nextAt) {
            if (pulse) h.nextAt = tick_ + (h.kind == HazardKind::Breath ? kBreathEvery : kPoolEvery);
            for (Body& one : bodies_) {
                if (!partisan(one) || !one.alive() || !inHazard(h, one.x, one.y)) continue;
                scorch(dragon, one, h.share);
                // The roar's shove, and Hellfire's: a tile straight away from it, as the
                // Lightning push is.
                if ((h.kind == HazardKind::Shock || h.kind == HazardKind::Hellfire) && one.alive() &&
                    one.pushTicks == 0) {
                    push(one, h.x, h.y);
                }
            }
            if (h.kind == HazardKind::Impact && h.pools) {
                Hazard pool;
                pool.kind = HazardKind::Pool;
                pool.x = h.x;
                pool.y = h.y;
                pool.reach = kPoolReach;
                pool.share = kPoolShare;
                pool.landsAt = tick_ + kPoolEvery;
                pool.nextAt = pool.landsAt;
                pool.endsAt = tick_ + kPoolTicks;
                h = Hazard{};
                layHazard(pool);
                continue;
            }
        }
        if (tick_ >= h.endsAt) h = Hazard{};
    }
}

// ---- the dragon's mind --------------------------------------------------------------------------

void Realm::bossThink(Body& dragon) {
    // A move in hand, or aloft: it neither walks nor swings.
    if (raid_.aloft || tick_ < raid_.busyUntil) {
        if (dragon.walking) halt(dragon);
        return;
    }
    // Whom it fights: the most threat, held until another has kThreatMargin times its.
    int best = -1;
    float most = 0.0f;
    int held = -1;
    for (const Body& one : bodies_) {
        if (!partisan(one) || !one.alive() || tables_->grid.safe(one.column(), one.row())) continue;
        const int index = partyIndex(one);
        if (index < 0) continue;
        if (one.id == dragon.quarry) held = index;
        if (raid_.threat[index] > most) {
            most = raid_.threat[index];
            best = index;
        }
    }
    int chosen = held;
    if (best >= 0 && (held < 0 || most > raid_.threat[held] * kThreatMargin)) chosen = best;
    if (chosen >= 0) {
        for (const Body& one : bodies_) {
            if (partisan(one) && one.alive() && partyIndex(one) == chosen) {
                dragon.quarry = one.id;
                dragon.provoked = true;
                dragon.provokedUntil = tick_ + kGrudgeTicks;
                break;
            }
        }
    }
    const int64_t before = dragon.swingsAt;
    think(dragon);
    // Enraged, it swings sooner (sim/raid.h kEnragedSwing).
    if (raid_.stage >= RaidStage::Enraged && dragon.swingsAt != before && dragon.swingsAt > tick_) {
        dragon.swingsAt = tick_ + std::max<int64_t>(1, int64_t(float(dragon.swingsAt - tick_) * kEnragedSwing));
    }
}

void Realm::raidAfter() {
    if (raid_.stage == RaidStage::None || invaderSlot_ < 0) return;
    const Body& dragon = bodies_[size_t(invaderSlot_)];
    const float keep = std::pow(0.5f, 1.0f / float(kThreatHalf));
    for (float& one : raid_.threat) one *= keep;
    for (const Happening& one : happenings_) {
        // A minion killed stays down until the next wave calls it: the kill's own respawn
        // (Realm::kill, its breed's regen) would stand it up again in seconds.
        if (one.what == What::Died) {
            for (int slot : minionSlots_) {
                Body& minion = bodies_[size_t(slot)];
                if (minion.id == one.who) minion.risesAt = std::numeric_limits<int64_t>::max();
            }
        }
        if (one.what != What::Hit || one.whom != dragon.id || one.reflected) continue;
        const Body* by = find(one.who);
        if (by == nullptr) continue;
        const int index = partyIndex(*by);
        if (index < 0) continue;
        const bool tank = index < int(party_.size()) && party_[size_t(index)].role == RaidRole::Tank;
        raid_.threat[index] += float(one.a) * (tank ? kTankThreat : 1.0f);
    }
}

// ---- the raiders' mind --------------------------------------------------------------------------

void Realm::reviveRaider(Body& one) {
    // A tile in the town's spawn box as Realm::haven draws the hero's, off raiderDice_.
    const int32_t* gate = tables_->safeGate;
    int column = one.column(), row = one.row();
    if (gate[2] > gate[0] && gate[3] > gate[1]) {
        column = raiderDice_.nextInt(gate[0], gate[2] + 1);
        row = raiderDice_.nextInt(gate[1], gate[3] + 1);
        int open = column, openRow = row;
        if (router_.nearestOpen(column, row, content::kWallCharacter, 8, &open, &openRow)) {
            column = open;
            row = openRow;
        }
    }
    halt(one);
    one.x = float(column);
    one.y = float(row);
    one.health = one.maxHealth;
    one.mana = one.maxMana;
    one.temper = Temper::Wandering;
    one.risesAt = 0;
    one.castUntil = 0;
    one.swingsAt = tick_;
    one.pushTicks = 0;
    one.pushAt = 0;
    say(What::Rose, one, one.level, one.health);
}

bool Realm::dodge(Body& one, int index) {
    // The newest volley told: judged once -- when it steps out, or that it does not.
    if (reactSerial_[index] != raid_.serial) {
        reactSerial_[index] = raid_.serial;
        const bool miss = raiderDice_.nextInt(0, kDodgeMissOdds) == 0;
        reactAt_[index] = miss ? -1 : tick_ + raiderDice_.nextInt(int(kReactLeast), int(kReactMost) + 1);
    }
    const auto danger = [&](float x, float y) {
        for (const Hazard& h : hazards_) {
            if (h.kind == HazardKind::None) continue;
            // What is told and has not landed, and what burns on.
            const bool live = tick_ < h.landsAt ||
                              ((h.kind == HazardKind::Breath || h.kind == HazardKind::Pool) &&
                               tick_ <= h.endsAt);
            // A volley it has not reacted to yet is not seen; a pool is always seen.
            const bool seen = h.kind == HazardKind::Pool || h.serial != raid_.serial ||
                              (reactAt_[index] >= 0 && tick_ >= reactAt_[index]);
            if (live && seen && inHazard(h, x, y)) return true;
        }
        return false;
    };
    if (!danger(one.x, one.y)) return false;
    if (one.walking) return true;  // already stepping out
    // The nearest tile out of every marked one, ring by ring.
    for (int ring = 1; ring <= 16; ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) continue;
                const int c = one.column() + dx, r = one.row() + dy;
                if (!tables_->grid.open(c, r, wallOf(one)) || tables_->grid.safe(c, r)) continue;
                if (danger(float(c), float(r))) continue;
                if (send(one, c, r)) {
                    say(What::Raid, one, int32_t(RaidEvent::Raider), int32_t(RaiderAct::Dodge));
                    return true;
                }
            }
        }
    }
    return false;
}

void Realm::raiderStrike(Body& one, Body& target, const SkillRow* row) {
    const bool ranged = one.archer > 0 || (row != nullptr && row->wizardry) ||
                        one.kin == Kin::DarkWizard;
    const float hit = row != nullptr ? force(*row, one.totalPoints()) : 1.0f;
    one.aim = std::atan2(target.y - one.y, target.x - one.x);
    // What the drawing shows of it, said as the hero's are: an arrow or a fan let go (`Loosed`,
    // Realm::looseArrow and looseFan), a spell's rock or ring (`Loosed`), Evil Spirit's spirits
    // round him (`Spirits`). Its blows land now, on the tick, as a raider's always do.
    const int32_t flight = std::max<int32_t>(1, int32_t(std::ceil(reach(one, target) / 0.875f)));
    if (row == nullptr && one.archer > 0) {
        say(What::Loosed, one, 0, flight, 0, target.id);
    } else if (row != nullptr && row->number == skill::kEvilSpirit) {
        say(What::Spirits, one, int32_t(kSpiritDelayTicks), 0, 0, target.id);
    } else if (row != nullptr && (row->wizardry || row->spread == Spread::Fan)) {
        say(What::Loosed, one, row->number, row->showers() ? row->fallTicks : flight, 0, target.id);
    }
    if (row == nullptr || row->spread == Spread::One) {
        strikeAt(one, target, hit, row, ranged);
        return;
    }
    // A sweep, a ring or a rain: every monster it would catch, round the body it was aimed at
    // for what falls (a shower, Evil Spirit's ring) and round him for what he swings.
    uint32_t victims[kVictims] = {};
    int found = 0;
    if (row->showers()) {
        // A Meteorite's shower as Realm::shower lays it: kShowerRocks rocks over the splash round
        // the body it was called on, each striking every monster within kRockBlast of where it
        // falls -- a big body near the middle takes several.
        for (int rock = 0; rock < kShowerRocks; ++rock) {
            const double angle = raiderDice_.nextDouble() * 6.283185307179586;
            const double out = std::sqrt(raiderDice_.nextDouble()) * double(row->splash);
            const float x = target.x + float(std::cos(angle) * out);
            const float y = target.y + float(std::sin(angle) * out);
            for (Body& other : bodies_) {
                if (!other.monster() || !other.alive()) continue;
                if (std::hypot(other.x - x, other.y - y) > kRockBlast) continue;
                strikeAt(one, other, hit, row, true);
            }
        }
        return;
    }
    if (row->number == skill::kEvilSpirit) {
        // Evil Spirit as Realm::letSpiritsGo lets it go: round him, each monster within
        // kSpiritReach taken at kSpiritOdds.
        for (const Body& other : bodies_) {
            if (found >= kVictims) break;
            if (!other.monster() || !other.alive()) continue;
            const float dx = other.x - one.x, dy = other.y - one.y;
            if (dx * dx + dy * dy >= kSpiritReach * kSpiritReach) continue;
            if (!raiderDice_.nextBool(kSpiritOdds)) continue;
            victims[found++] = other.id;
        }
    } else {
        found = gather(one, *row, victims, kVictims);
    }
    for (int i = 0; i < found; ++i) {
        if (Body* struck = body(victims[i]); struck != nullptr && struck->alive()) {
            strikeAt(one, *struck, hit, row, ranged);
        }
    }
}

void Realm::raid(Body& one, int index) {
    if (!one.alive() || index < 0 || index >= int(party_.size())) return;
    if (one.raider >= 0) {
        // Pushed by the roar: it slides and does nothing else until it lands.
        if (one.pushTicks > 0) {
            one.x += one.pushX;
            one.y += one.pushY;
            if (--one.pushTicks == 0) {
                one.x = float(one.column());
                one.y = float(one.row());
            }
            return;
        }
        // Back from town at the hero's run (sim::kRunFactor), walking within the fight.
        const Body* landed = invaderSlot_ >= 0 ? &bodies_[size_t(invaderSlot_)] : nullptr;
        const bool far = landed != nullptr && !within(one, *landed, 12.0f);
        one.speed = (far ? kRunFactor : 1.0f) / float(kHeroMoveTicks);
        advance(one);
    }
    const RaiderKit& kit = party_[size_t(index)];
    // A potion below two fifths (sim/raid.h): a large healing potion's thirty percent and its
    // flat (Realm::useItem's arithmetic), at once rather than poured.
    if (shareOf(one) < kDrinkBelow && raidPotions_[index] > 0 && tick_ >= drinkAt_[index]) {
        const int worth = int(double(one.maxHealth) * 0.3) + std::max(0, 200 - one.level);
        one.health = std::min(one.maxHealth, one.health + worth);
        --raidPotions_[index];
        drinkAt_[index] = tick_ + kDrinkEvery;
        say(What::Raid, one, int32_t(RaidEvent::Raider), int32_t(RaiderAct::Drink), worth);
    }
    // Its mana back as the hero's comes back (Realm::recover): 1/27.5 of the pool every three
    // seconds, anywhere. The hero's own recover() runs for him.
    if (one.raider >= 0 && tick_ % kRecoverEveryTicks == 0 && one.mana < one.maxMana) {
        one.manaCarry += float(one.maxMana) * kManaRecoveryShare;
        const int whole = int(one.manaCarry);
        one.manaCarry -= float(whole);
        one.mana = std::min(one.maxMana, one.mana + whole);
    }
    if (dodge(one, index)) return;
    if (tick_ < one.castUntil || tick_ < one.swingsAt) return;

    Body* dragon = invaderSlot_ >= 0 && bodies_[size_t(invaderSlot_)].alive()
                       ? &bodies_[size_t(invaderSlot_)]
                       : nullptr;
    // Its own guard first: Defense on a knight with a shield, Soul Barrier on a wizard.
    const auto cast = [&](const SkillRow& row, Body& at) {
        const int skillAt = skillIndexOf(row.number);
        const int32_t clip = clipTicksOf(one, row);
        if (skillAt >= 0) {
            one.cools[skillAt] =
                tick_ + cooldownTicks(row, one.totalPoints().agility, floorTicksFor(row, clip));
            // A guard or a buff waits on its cast alone, not on its own five minutes as the
            // hero's does (cooldownTicks): MU's elf buffed as her mana let her, and a raid's elves
            // keep every knight covered, each knight his Defense after he stands up again (the
            // user, 2026-10-06: 'give always buffs', 'every class shoudl use defense auras'). Ours.
            if (row.boonTicks > 0 || row.mightTicks > 0) one.cools[skillAt] = tick_ + clip;
        }
        one.mana = std::max(0, one.mana - row.mana);
        one.castUntil = tick_ + clip;
        one.swingsAt = tick_ + std::max(clip, one.swingTicks);
        say(What::Raid, one, int32_t(RaidEvent::Raider), int32_t(RaiderAct::Cast), row.number, at.id);
        // And as the hero's cast is said (Realm::throwSkill), so the drawing plays its clip and
        // shows what it throws (Play::update, What::Cast and What::Loosed).
        say(What::Cast, one, row.number, skillAt >= 0 ? int32_t(one.cools[skillAt] - tick_) : 0, 0, at.id);
    };
    // Off its cooldown, its class's, paid for, and what his hands can throw it with -- `armed`'s
    // gates -- the shield's family for a guard on himself, the weapon's for the rest. Its mana is
    // its own pool, paid on the cast and back at the hero's rate (above), so a knight is not
    // throwing Twisting Slash on every blow (the user, 2026-10-06: 'they cant use twisting slash
    // all the time or other spells'); dry, he swings.
    const auto armAt = [&](int32_t at) -> const content::Arm* {
        return at >= 0 && size_t(at) < tables_->arms.size() ? &tables_->arms[size_t(at)] : nullptr;
    };
    const auto ready = [&](const SkillRow& row) {
        const int skillAt = skillIndexOf(row.number);
        if (skillAt < 0 || one.cools[skillAt] > tick_ || !skillFor(row, one.kin)) return false;
        if (one.mana < row.mana) return false;
        return row.onSelf() ? row.suits(armFamily(one, armAt(one.shield)))
                            : row.suits(familyOf(armAt(one.weapon)));
    };
    for (const int32_t number : kit.skills) {
        const SkillRow* row = skillNumbered(number);
        if (row == nullptr || !ready(*row)) continue;
        // A guard on itself: while none stands.
        if ((number == skill::kDefense || number == skill::kSoulBarrier) && one.boonUntil <= tick_) {
            one.boonUntil = tick_ + row->boonTicks;
            one.boonSkill = number;
            one.boonDamageTaken = 1.0f - boonShare(*row, one.totalPoints(), one.shieldDefense);
            one.stats.damageTaken = double(one.boonDamageTaken) * one.pet.taken;
            cast(*row, one);
            return;
        }
        // **Every elf keeps the knights up** (the user, 2026-10-06: 'can we don that elfs can heal
        // and buff tanks?', then 'elfs should try to heal DKs and give always buffs'). Each elf --
        // the healer and the archers alike -- heals the most hurt in reach, the knights before the
        // rest and the tank before them: the healer anyone below four fifths, an archer a knight
        // below three fifths. And keeps Greater Defense and Greater Damage standing, the tank's
        // first, then every knight's, then anyone's. invention, with the party.
        if (one.kin != Kin::FairyElf) continue;
        const bool healer = kit.role == RaidRole::Healer;
        Body* pick = nullptr;
        float best = 0.0f;
        for (Body& other : bodies_) {
            // Cast on another from her stand's reach: this game's Heal and guards are thrown on
            // oneself (reach 1), and a raid's elf keeps a tank up from six tiles. invention.
            if (!partisan(other) || !other.alive() ||
                !within(one, other, std::max(row->reach, kRangedStand))) {
                continue;
            }
            const int at = partyIndex(other);
            const RaidRole role =
                at >= 0 && at < int(party_.size()) ? party_[size_t(at)].role : RaidRole::Melee;
            const bool tank = role == RaidRole::Tank;
            const bool knight = other.kin == Kin::DarkKnight;
            // Who comes first: the tank, then a knight, then anyone.
            const float rank = tank ? 3.0f : knight ? 2.0f : 1.0f;
            float score = 0.0f;
            if (row->mends) {
                const float bar = healer ? 0.8f : (knight ? 0.6f : 0.0f);
                const float share = shareOf(other);
                if (share < bar) score = (1.0f - share) + rank * 0.1f;
            } else if (number == skill::kGreaterDefense && other.boonUntil <= tick_) {
                score = rank;
            } else if (number == skill::kGreaterDamage && other.mightUntil <= tick_) {
                score = rank;
            }
            if (score > best) {
                best = score;
                pick = &other;
            }
        }
        if (pick == nullptr) continue;
        if (row->mends) {
            pick->health = std::min(pick->maxHealth, pick->health + healOf(one.totalPoints()));
        } else if (number == skill::kGreaterDefense) {
            pick->boonUntil = tick_ + row->boonTicks;
            pick->boonSkill = number;
            pick->boonDamageTaken = 1.0f - boonShare(*row, one.totalPoints(), pick->shieldDefense);
            pick->stats.damageTaken = double(pick->boonDamageTaken) * pick->pet.taken;
        } else if (number == skill::kGreaterDamage) {
            pick->might = mightOf(one.totalPoints());
            pick->mightUntil = tick_ + row->mightTicks;
            rearm(*pick, kitOf(*pick));
        }
        cast(*row, *pick);
        return;
    }

    // What it fights. The knights take the minions while they stand -- the tank only when the
    // dragon is aloft -- and the dragon otherwise; the ranged the dragon, aloft or not; the
    // healer nothing, and stands back.
    const bool melee = kit.role == RaidRole::Tank || kit.role == RaidRole::Melee;
    Body* minion = nullptr;
    for (int slot : minionSlots_) {
        Body& m = bodies_[size_t(slot)];
        if (!m.alive()) continue;
        if (minion == nullptr || reach(one, m) < reach(one, *minion)) minion = &m;
    }
    Body* target = nullptr;
    if (melee) {
        const bool takeMinion = minion != nullptr && (raid_.aloft || kit.role == RaidRole::Melee);
        target = takeMinion ? minion : (dragon != nullptr && !raid_.aloft ? dragon : minion);
        // Aloft with no minion standing: under it, ready for its landing, not idle far off.
        if (target == nullptr && dragon != nullptr) {
            if (!within(one, *dragon, 3.0f)) approach(one, *dragon, 2, false);
            return;
        }
    } else if (kit.role != RaidRole::Healer) {
        target = dragon != nullptr ? dragon : minion;
    } else if (dragon != nullptr && !raid_.aloft) {
        // The healer, nobody to mend: her mace on the dragon, beside the tank (the user,
        // 2026-10-06: 'some of chars is not even fighting boss').
        target = dragon;
    }
    // The healer stands by the tank, four tiles off, while the tank stands in the fight -- not
    // after him into town when he has fallen and stood up there.
    if (target == nullptr && kit.role == RaidRole::Healer && dragon != nullptr) {
        for (const Body& other : bodies_) {
            const int at = partyIndex(other);
            if (!other.alive() || at < 0 || at >= int(party_.size()) ||
                party_[size_t(at)].role != RaidRole::Tank || !within(other, *dragon, kInfernoReach)) {
                continue;
            }
            if (!within(one, other, 4.0f)) approach(one, other, 4, true);
            return;
        }
    }
    if (target == nullptr) {
        // The healer, or nothing left: keep within reach of the dragon, out of its breath.
        if (dragon != nullptr && !within(one, *dragon, kRangedStand + 1.0f)) {
            approach(one, *dragon, int(kRangedStand), true);
        }
        return;
    }
    const float armsReach = 1.0f + float(bulkOf(numberOf(*target)));
    // The healer's mace is a melee weapon too.
    const bool close = melee || kit.role == RaidRole::Healer;
    const float range = close ? armsReach : kRangedStand;
    if (!within(one, *target, range) || (!close && !seen(one, *target))) {
        approach(one, *target, int(range), !close);
        return;
    }
    engage(one, *target);
    // Round the kit from after the last skill thrown (raidNext_).
    const int count = int(kit.skills.size());
    for (int step = 0; step < count; ++step) {
        const int at = (raidNext_[index] + step) % count;
        const int32_t number = kit.skills[size_t(at)];
        const SkillRow* row = skillNumbered(number);
        if (row == nullptr || !ready(*row) || row->boonTicks > 0 || row->mends || row->mightTicks > 0) {
            continue;
        }
        // What is aimed at a body flies its reach or the stand's; what sweeps round him (a ring,
        // an arc, a line) only reaches its own -- Inferno from six tiles catches nothing.
        const bool aimed = row->spread == Spread::One || row->spread == Spread::Fan ||
                           row->spread == Spread::Line || row->spread == Spread::Beam ||
                           row->showers() || row->number == skill::kEvilSpirit;
        const float bulk = float(bulkOf(numberOf(*target)));
        if (!within(one, *target, aimed ? std::max(row->reach, range) : row->reach + bulk)) continue;
        raidNext_[index] = (at + 1) % count;
        cast(*row, *target);
        raiderStrike(one, *target, row);
        // Its runes answer a spell as the hero's do (Realm::land): Stormcall once a cast, and an
        // Arcane Echo throws it again, free.
        if (row->wizardry && one.alive()) {
            stormcall(one, *target, 0);
            if (target->alive() && echoes(one)) {
                core::logf("echo rune: tick %lld, raider %d's %d again", (long long)tick_,
                           one.raider, row->number);
                raiderStrike(one, *target, row);
            }
        }
        return;
    }
    one.swingsAt = tick_ + one.swingTicks;
    say(What::Raid, one, int32_t(RaidEvent::Raider), int32_t(RaiderAct::Swing), 0, target->id);
    raiderStrike(one, *target, nullptr);
}

}  // namespace mu::sim
