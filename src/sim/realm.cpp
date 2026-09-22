#include "sim/realm.h"

#include "sim/swings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/log.h"

namespace mu::sim {

namespace {

// The mana maximum after anything that moves it, and the pool raised by what the maximum
// gained -- the rule the health beside it follows, so a point in energy arrives full rather
// than as a bigger empty gem. A new hero starts at zero of zero and so starts full.
// Mana and the shield, re-reckoned after `reckon` (the shield reads the defence it just set).
// What a maximum gains arrives full; what it loses is clipped.
void restoreMana(Body& hero) {
    const int was = hero.maxMana;
    hero.maxMana = maximumMana(hero.kin, hero.level, hero.points);
    hero.mana = std::min(hero.maxMana, hero.mana + std::max(0, hero.maxMana - was));
    const int wasSd = hero.maxSd;
    hero.maxSd = maximumShield(hero.level, hero.points, hero.stats.defense);
    hero.sd = std::min(hero.maxSd, hero.sd + std::max(0, hero.maxSd - wasSd));
}

}  // namespace

namespace {

// How far past its view range something can be and still keep a monster awake. Realm.cs:63.
constexpr int kMargin = 8;
// How far a monster will be led from where it was put down before it gives up and walks back,
// and twice that for one that has been hit. Both are MU2's bench inventions and are marked as
// such where they are defined: OpenMU has no leash at all, and without one nothing that has
// seen you can ever be outrun, because a monster walks exactly as fast as a character.
constexpr int kLeash = 10;       // invention (MU2's Realm.cs Leash, and it says so itself)
constexpr int kGrudge = 20;      // invention (MU2's Grudge = Leash * 2)
// How often a chase re-plans, in ticks. Realm.cs:1918.
constexpr int kRepath = 3;
// A player's swing, in ticks, WHEN THE TABLES CANNOT SAY. Things.cs:249's 1000 ms, and it is
// now only a fallback: the swing is the length of the clip he swings with, and sim/swings.cpp
// works it out from what is in his hands. A cooked file with no attack actions in it -- an old
// one -- keeps this rather than being handed an interval invented out of no data.
constexpr int kHeroSwingTicks = 20;
// A player walks a tile in 400 ms, as every Lorencia monster does. The monster's 400 is traced
// (Lorencia.cs:87); the player's is MU2's derivation from MU's walk clip (Things.cs:583) and is
// borrowed from the monster row rather than invented separately.
constexpr int kHeroMoveTicks = 8;
// How fast a body comes round to where it is going, in degrees a second. MU snaps its facing;
// MU2 turned at 900 (Walker.cs:85, a bench number and marked as one). Invention: 1440, which is
// 72 degrees a tick, so that a click straight behind costs ONE tick on the spot rather than
// three -- the pivot is the one wait between a click and the first step, and at 900 it was the
// thing being waited for. The drawing interpolates the facing between ticks, so 72 degrees a
// tick still reads as a turn and not a snap.
constexpr float kTurnDegrees = 1440.0f;
// How far off its heading a body may be and still walk, in degrees. Under it, it sets off and
// finishes coming round as it goes, which is what walking round a corner is. Over it -- a click
// behind the character, a monster turning onto somebody who hit it from behind -- it turns on
// the spot first. Walker.cs:98-107 had 120, and that was the moonwalk: a body 119 degrees off
// covers ground for the ticks it takes to come round, which is gliding sideways and backwards
// under a walk clip that faces the other way. Invention: 60, less than one tick's turn, so the
// most a body ever travels off its facing is 60 degrees for one tick, and the drawn facing has
// already swung most of that by the time the step is drawn.
constexpr float kPivotDegrees = 60.0f;

// A player reaches one tile. Sprint 7's weapons have their own reach.
constexpr int kHeroAttackRange = 1;
// How long a dead character lies there before he stands up in town. MU2's Player.cs:1687, three
// seconds, which is also when a summon is taken off him.
constexpr int kRiseTicks = 60;
// The shield: its share of a blow and its safe-zone recovery, every three seconds. Rates.cs.
constexpr float kShieldShare = 0.9f;
constexpr float kShieldRecovery = 0.02f;
constexpr int kRecoveryTicks = 60;

// An angle folded into a half turn either side of nothing, so that a body facing just west of
// north turns a few degrees to face just east of it rather than most of the way round the other
// way. Walker.cs:288-309.
float wrapped(float angle) {
    constexpr float kTurnabout = 6.28318530718f;
    angle = std::fmod(angle, kTurnabout);
    if (angle > 3.14159265359f) angle -= kTurnabout;
    if (angle < -3.14159265359f) angle += kTurnabout;
    return angle;
}

// MU's own reckoning of distance: the larger of the two axis distances, not a Euclidean
// length. Things.cs:607.
float reach(const Body& one, const Body& other) {
    return std::max(std::fabs(one.x - other.x), std::fabs(one.y - other.y));
}

bool within(const Body& one, const Body& other, float range) {
    return reach(one, other) <= range;
}

int strayed(const Body& beast) {
    return std::max(std::abs(beast.column() - beast.homeColumn),
                    std::abs(beast.row() - beast.homeRow));
}

}  // namespace

// What a body has in its hands, as the arithmetic wants it. A monster always has empty hands
// here: its damage band is its own row, whatever it is drawn holding.
Arms Realm::armsOf(const Body& one) const {
    Arms arms;
    if (!tables_) return arms;
    if (one.weapon >= 0 && size_t(one.weapon) < tables_->arms.size()) {
        const content::Arm& held = tables_->arms[size_t(one.weapon)];
        arms.weaponMinimumDamage = held.minimumDamage;
        arms.weaponMaximumDamage = held.maximumDamage;
        arms.armourDefense += held.defense;
    }
    if (one.shield >= 0 && size_t(one.shield) < tables_->arms.size()) {
        arms.armourDefense += tables_->arms[size_t(one.shield)].defense;
    }
    // The player's defence is his worn pieces' since sprint 7 -- the shield among them, with its
    // plus -- and not the shield's row alone, so it replaces rather than adds to the above.
    if (one.player) {
        arms.armourDefense = one.wornDefense;
        arms.shieldDefenseRate = one.wornDefenseRate;
        arms.weaponMinimumDamage += one.weapon >= 0 ? one.weaponBonus : 0;
        arms.weaponMaximumDamage += one.weapon >= 0 ? one.weaponBonus : 0;
    }
    return arms;
}

// The swing clock, re-worked whenever what he holds or what he is changes. It is the length of
// the clip he swings with; see sim/swings.h for the chain and its traces.
void Realm::reswing(Body& hero) {
    if (!tables_) return;
    const content::Arm* right = hero.weapon >= 0 && size_t(hero.weapon) < tables_->arms.size()
                                    ? &tables_->arms[size_t(hero.weapon)]
                                    : nullptr;
    const content::Arm* left = hero.shield >= 0 && size_t(hero.shield) < tables_->arms.size()
                                   ? &tables_->arms[size_t(hero.shield)]
                                   : nullptr;
    const int milliseconds =
        swingMilliseconds(*tables_, hero.kin, hero.points.agility, right, left);
    hero.swingMs = milliseconds;
    const int32_t ticks = swingTicks(milliseconds);
    hero.swingTicks = ticks > 0 ? ticks : kHeroSwingTicks;
}

bool Realm::equip(int32_t weapon, int32_t shield, bool given) {
    refusal_.clear();
    if (!tables_) return false;
    Body& hero = bodies_[0];
    const auto allowed = [&](int32_t index, bool wantShield) {
        if (index < 0) return true;
        if (size_t(index) >= tables_->arms.size()) {
            refusal_ = "there is no such arm";
            return false;
        }
        const content::Arm& arm = tables_->arms[size_t(index)];
        if (arm.isShield() != wantShield) {
            refusal_ = arm.label + " is " + (arm.isShield() ? "a shield" : "a weapon") +
                       " and was asked for as the other";
            return false;
        }
        // mu.db's enumeration: bit 0 Dark Wizard, bit 1 Fairy Elf, bit 2 Dark Knight.
        if ((arm.classes & (1 << int(hero.kin))) == 0) {
            refusal_ = arm.label + " is not for this class";
            return false;
        }
        // The requirement is the item's own, at its base level. An item's level raises what it
        // asks -- "a row's raw strength is not what the game asks" -- and levelled items are
        // sprint 7's along with the rest of what an item is.
        //
        // Not asked at all of what the cradle gives: see the note on equip() in realm.h. The
        // class and the slot are still asked, because the cradle never gave anybody a shield
        // in his sword hand.
        if (given) return true;
        if (hero.points.strength < arm.wantsStrength) {
            refusal_ = arm.label + " wants " + std::to_string(arm.wantsStrength) +
                       " strength and he has " + std::to_string(hero.points.strength);
            return false;
        }
        if (hero.points.agility < arm.wantsAgility) {
            refusal_ = arm.label + " wants " + std::to_string(arm.wantsAgility) +
                       " agility and he has " + std::to_string(hero.points.agility);
            return false;
        }
        return true;
    };
    if (!allowed(weapon, false) || !allowed(shield, true)) return false;

    // Into the satchel's hands, which is where a hand is read from since sprint 7. Both hands
    // are this call's to set: what was in them goes back into the bag, or is lost if the bag
    // has no room -- which it always has, since nothing else puts anything there before this.
    for (int hand : {kWeaponRight, kWeaponLeft}) {
        const Held was = bag_.lift(hand);
        if (!was.empty()) give(was.item, -1, was.refinement, was.durability);
    }
    for (int32_t index : {weapon, shield}) {
        if (index < 0) continue;
        const int32_t item = tables_->itemNamed(tables_->arms[size_t(index)].name);
        if (item < 0) {
            refusal_ = tables_->arms[size_t(index)].label + " has no item row";
            continue;
        }
        const content::ItemRow& row = tables_->items[size_t(item)];
        const int hand = placeOf(row);
        if (hand >= 0) bag_.put(hand, Held{item, 0, int16_t(row.durability)});
    }
    rearm(hero);
    return true;
}

// Reads the hands and the armour off the satchel and re-reckons him. Beast.Rearm: ammunition is
// not a weapon, the bow is the weapon wherever it is held, and the defence is the sum of the
// pieces from the left hand to the boots, each with its plus.
void Realm::rearm(Body& hero) {
    const auto rowAt = [&](int slot) -> const content::ItemRow* {
        const Held& h = bag_[slot];
        return h.empty() ? nullptr : &tables_->items[size_t(h.item)];
    };
    const content::ItemRow* right = rowAt(kWeaponRight);
    const content::ItemRow* left = rowAt(kWeaponLeft);
    const auto swung = [](const content::ItemRow* r) {
        return r && r->weapon() && !ammunition(*r);
    };
    int weaponSlot = swung(right) ? kWeaponRight : (swung(left) ? kWeaponLeft : -1);
    hero.weapon = weaponSlot >= 0 ? tables_->armNamed(rowAt(weaponSlot)->name) : -1;
    hero.weaponBonus = weaponSlot >= 0 ? damageBonus(bag_[weaponSlot].refinement) : 0;
    hero.shield = left && left->shield() ? tables_->armNamed(left->name) : -1;
    hero.wornDefense = 0;
    hero.wornDefenseRate = 0;
    for (int slot = kWeaponLeft; slot <= kBoots; ++slot) {
        const content::ItemRow* row = rowAt(slot);
        if (!row || (!row->shield() && !row->armour())) continue;
        hero.wornDefense += row->defense + defenseBonus(row->shield(), bag_[slot].refinement);
        // The shield's block column rises on the armour's table: _shieldDefenseRateIncreaseTable
        // is built from DefenseIncreaseByLevel.
        if (row->shield()) {
            hero.wornDefenseRate += row->defenseRate + defenseBonus(false, bag_[slot].refinement);
        }
    }
    const int was = hero.maxHealth;
    reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
    restoreMana(hero);
    reswing(hero);
    hero.health = std::min(hero.maxHealth, hero.health + std::max(0, hero.maxHealth - was));
}

Wearer Realm::wearer() const {
    const Body& hero = bodies_[0];
    return Wearer{hero.kin, hero.level, hero.points};
}

int Realm::give(int32_t item, int slot, int refinement, int durability) {
    if (!tables_ || item < 0 || size_t(item) >= tables_->items.size()) return -1;
    const content::ItemRow& row = tables_->items[size_t(item)];
    if (slot < 0) slot = bag_.free(*tables_, row.width, row.height);
    if (slot < 0 || !bag_[slot].empty() ||
        (baggable(slot) && !bag_.room(*tables_, slot, row.width, row.height))) {
        return -1;
    }
    bag_.put(slot, Held{item, int16_t(refinement), int16_t(durability)});
    if (wearable(slot)) rearm(bodies_[0]);
    return slot;
}

bool Realm::moveItem(int from, int to) {
    if (!tables_ || !bodies_[0].alive()) return false;
    if (!move(*tables_, wearer(), bag_, from, to)) return false;
    if (wearable(from) || wearable(to)) rearm(bodies_[0]);
    return true;
}

bool Realm::useItem(int slot) {
    if (!tables_ || !baggable(slot)) return false;
    Body& hero = bodies_[0];
    const Held potion = bag_[slot];
    if (potion.empty() || !hero.alive()) return false;
    const content::ItemRow& row = tables_->items[size_t(potion.item)];
    const bool mana = restores(row);
    if (!mana && !heals(row)) return false;
    // A yes that has not come round yet, not a no. MU2's Realm.Consume.
    if (tick_ < potionUntil_) return false;

    // MU2's Realm.Consume, off OpenMU's RecoverConsumeHandlerPlugIn: the rank in its family
    // (0 the apple, 1 to 3 the potions) buys ten percent of the pool each, the plus one more
    // each, and a flat (rank + 1) x 50 less the level, never below nothing.
    const int rank = mana ? row.number - 4 + 1 : row.number;
    const int pool = mana ? hero.maxMana : hero.maxHealth;
    const int percent = rank * 10 + potion.refinement;
    const int flat = std::max(0, (rank + 1) * 50 - hero.level);
    const int total = int(double(pool) * double(percent) / 100.0 + double(flat));
    // The three instalments, at 200, 600 and 200 ms after one another: 4, 12 and 4 ticks.
    const int64_t steps[3] = {4, 12, 4};
    const int shares[3] = {20, 60, 20};
    int paid = 0;
    int64_t due = tick_;
    for (int i = 0; i < 3 && sipCount_ < 8; ++i) {
        due += steps[i];
        const int amount = i == 2 ? total - paid : total * shares[i] / 100;
        paid += amount;
        sips_[sipCount_++] = Sip{due, amount, mana};
    }
    potionUntil_ = tick_ + 10;  // PotionCooldown, half a second
    Held left = potion;
    left.durability = int16_t(potion.durability - 1);
    if (left.durability <= 0) {
        bag_.lift(slot);
    } else {
        bag_.put(slot, left);
    }
    say(What::Drank, hero, total, mana ? 1 : 0);
    return true;
}

// The shield comes back in a safe zone and nowhere else: a fiftieth of its maximum every three
// seconds, the fraction carried. MU2's Realm.Recover and Rates.ShieldRecoveryInSafeZone, off
// OpenMU's RegenerateAsync, which skips the shield outside one.
void Realm::recover(Body& hero) {
    if (tick_ % kRecoveryTicks != 0 || !hero.alive() || hero.sd >= hero.maxSd) {
        if (hero.sd >= hero.maxSd) hero.sdCarry = 0.0f;
        return;
    }
    if (!tables_->grid.safe(hero.column(), hero.row())) return;
    hero.sdCarry += float(hero.maxSd) * kShieldRecovery;
    const int whole = int(hero.sdCarry);
    hero.sdCarry -= float(whole);
    hero.sd = std::min(hero.maxSd, hero.sd + whole);
}

void Realm::sip() {
    Body& hero = bodies_[0];
    int kept = 0;
    for (int i = 0; i < sipCount_; ++i) {
        const Sip& one = sips_[i];
        if (one.due > tick_) {
            sips_[kept++] = one;
            continue;
        }
        // A dead man drinks nothing: what is still due is spilt.
        if (!hero.alive()) continue;
        if (one.mana) {
            hero.mana = std::min(hero.maxMana, hero.mana + one.amount);
        } else {
            hero.health = std::min(hero.maxHealth, hero.health + one.amount);
        }
    }
    sipCount_ = kept;
}

// What a death leaves: one roll against three groups, rarest first, each taking its own chance
// out of what is left -- DefaultDropGenerator.SelectRandomGroup, as MU2's Realm.Leave walks
// it. Loot.cs's numbers: a jewel at 0.001, an item at 0.3 from what the monster's level can
// afford, Zen at 0.5 worth the kill's experience plus seven; otherwise nothing.
//
// Smaller than MU2's: no luck roll and no skill roll on a dropped piece. Its plus is
// Loot.Refinement, `(monster level - drop level) / 3`, held to the cap of nine.
void Realm::leave(const Body& dead, const Body& killer) {
    constexpr double kJewel = 0.001, kItem = 0.3, kMoney = 0.5;
    constexpr int kGap = 12;           // Loot.Gap: nothing more than twelve levels below it
    constexpr int kBaseMoney = 7;      // Loot.BaseMoney
    constexpr int kLingerSeconds = 60; // Loot.Lingers
    constexpr int kMostRefined = 9;    // Refine.Cap
    const int level = dead.level;
    double roll = dice_.nextDouble();
    Lying one;
    std::tie(one.column, one.row) = clearing(dead.column(), dead.row());
    one.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;

    const auto reaches = [level](const content::ItemRow& row) {
        return row.dropLevel <= level && (row.maximumDropLevel == 0 || level <= row.maximumDropLevel);
    };
    // The pools are counted then drawn from by index, so nothing is allocated for them.
    const auto draw = [&](auto&& admits) -> int32_t {
        int count = 0;
        for (const content::ItemRow& row : tables_->items) count += admits(row) ? 1 : 0;
        if (count == 0) return -1;
        int pick = dice_.nextInt(0, count);
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            if (!admits(tables_->items[i])) continue;
            if (pick-- == 0) return int32_t(i);
        }
        return -1;
    };

    if (roll <= kJewel) {
        const int32_t item = draw([&](const content::ItemRow& r) { return r.jewel() && reaches(r); });
        if (item < 0) return;
        one.what = Held{item, 0, 1};
    } else if ((roll -= kJewel) <= kItem) {
        const int32_t item = draw([&](const content::ItemRow& r) {
            return r.dropsFromMonsters() && reaches(r) && r.dropLevel > level - kGap;
        });
        if (item < 0) return;
        const content::ItemRow& row = tables_->items[size_t(item)];
        // Prize.TakesRefinement: the weapon and armour groups, ammunition taken back out.
        const bool refinable = row.group <= kGroupBoots && !ammunition(row);
        const int plus = refinable ? std::clamp((level - row.dropLevel) / 3, 0, kMostRefined) : 0;
        const bool stacks = heals(row) || restores(row);
        one.what = Held{item, int16_t(plus), int16_t(stacks ? 1 : row.durability)};
    } else if (roll - kItem <= kMoney) {
        one.zen = int64_t(killExperience(dead.level, killer.level)) + kBaseMoney;
    } else {
        return;
    }
    one.id = nextId_++;
    lying_.push_back(one);
    say(What::Dropped, dead, int32_t(one.id), one.what.empty() ? -1 : one.what.item,
        one.what.empty() ? int32_t(one.zen) : one.what.refinement);
}

// A tile near a point with nothing already lying on it. MU2's Realm.Clearing: the client
// takes the position it is handed and drops the item there, so several drops given the same
// spot would land in the same place in the same pose and read as one item rather than as a
// pile -- spreading them is the server's job in MU and it is the sim's job here.
//
// A ring search rather than a scatter, so the nearest free tile is taken first: a single drop
// lands exactly where the thing died, and only a crowd spirals outward. It ignores who is
// standing there and only avoids other drops -- MU lets you stand on loot, and a monster's own
// corpse tile must stay eligible for its own drop, or a respawn's first kill could never leave
// anything where it fell.
std::pair<int, int> Realm::clearing(int column, int row) const {
    constexpr int kDropRings = 2;  // MU2's Realm.DropRings
    if (bare(column, row)) return {column, row};
    for (int ring = 1; ring <= kDropRings; ++ring) {
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                if (bare(column + dx, row + dy)) return {column + dx, row + dy};
            }
        }
    }
    // Everything nearby is taken. Better a pile than no drop.
    return {column, row};
}

// Whether a tile is clear of other drops and is ground a thing may lie on -- the relaxed
// pass, so a body standing on the spot does not refuse it. MU2's Realm.Bare.
bool Realm::bare(int column, int row) const {
    if (!tables_->grid.open(column, row, content::kWallNoMove)) return false;
    for (const Lying& already : lying_) {
        if (already.column == column && already.row == row) return false;
    }
    return true;
}

// Takes what lies at an index into the bag or the purse. Refused, and left lying, when the
// bag has no room at its footprint -- MOVEMENT_GET's "the bag is full".
bool Realm::take(size_t index) {
    const Lying one = lying_[index];
    int slot = -1;
    if (one.what.empty()) {
        money_ += one.zen;
    } else {
        const content::ItemRow& row = tables_->items[size_t(one.what.item)];
        slot = bag_.free(*tables_, row.width, row.height);
        if (slot < 0) return false;
        bag_.put(slot, one.what);
    }
    lying_[index] = lying_.back();
    lying_.pop_back();
    say(What::Picked, bodies_[0], int32_t(one.id), slot, int32_t(one.zen));
    return true;
}

bool Realm::serving(int folk) const {
    if (!tables_ || folk < 0 || size_t(folk) >= tables_->folk.size()) return false;
    const Body& hero = bodies_[0];
    if (!hero.alive()) return false;
    const content::Townsperson& one = tables_->folk[size_t(folk)];
    const float dx = hero.x - float(one.x), dy = hero.y - float(one.y);
    return dx * dx + dy * dy <= kCounter * kCounter;
}

int Realm::buy(int shelfSlot) {
    if (trading_ < 0 || !serving(trading_)) return -1;
    const int npc = tables_->folk[size_t(trading_)].number;
    int count = 0;
    const Offer* stock = stockOf(npc, &count);
    // Last one wins, which is how a shelf keyed on the slot is built -- Hanzo's slot 73.
    const Offer* wanted = nullptr;
    for (int i = 0; i < count; ++i) {
        if (stock[i].slot == shelfSlot) wanted = &stock[i];
    }
    if (!wanted) return -1;
    const int32_t item = tables_->itemAt(wanted->group, wanted->number);
    if (item < 0) return -1;
    const content::ItemRow& row = tables_->items[size_t(item)];
    const int64_t price = buyingPrice(row, wanted->refinement, wanted->pieces > 0 ? wanted->pieces : 1,
                                      wanted->skill, row.durability, row.durability);
    if (money_ < price) return -1;
    const int slot = bag_.free(*tables_, row.width, row.height);
    if (slot < 0) return -1;
    money_ -= price;
    // A stack for a potion, a full quiver for ammunition, and nothing read for gear.
    Held bought{item, int16_t(wanted->refinement),
                int16_t(wanted->pieces > 0 ? wanted->pieces : row.durability), wanted->skill};
    bag_.put(slot, bought);
    say(What::Bought, bodies_[0], item, int32_t(price), slot);
    return slot;
}

int64_t Realm::sellItem(int slot) {
    if (trading_ < 0 || !serving(trading_) || !baggable(slot) || bag_[slot].empty()) return -1;
    const Held thing = bag_[slot];
    const content::ItemRow& row = tables_->items[size_t(thing.item)];
    const bool stacks = row.group == kGroupPotions;
    const int64_t paid = sellingPrice(row, thing.refinement,
                                      stacks ? std::max<int>(1, thing.durability) : 1, thing.skill,
                                      thing.durability, row.durability);
    bag_.lift(slot);
    money_ += paid;
    say(What::Sold, bodies_[0], thing.item, int32_t(paid), slot);
    return paid;
}

bool Realm::pay(int64_t zen) {
    if (zen < 0 || zen > money_) return false;
    money_ -= zen;
    return true;
}

Held Realm::sell(int slot) {
    if (!baggable(slot)) return Held{};
    return bag_.lift(slot);
}

const Body* Realm::find(uint32_t id) const {
    if (id == 0 || id >= indexOfId_.size()) return nullptr;
    const uint32_t at = indexOfId_[id];
    if (at >= bodies_.size()) return nullptr;
    return &bodies_[at];
}

Body* Realm::body(uint32_t id) {
    return const_cast<Body*>(static_cast<const Realm*>(this)->find(id));
}

RealmCounts Realm::counts() const {
    RealmCounts out;
    for (size_t i = 1; i < bodies_.size(); ++i) {
        ++out.monsters;
        if (bodies_[i].alive()) ++out.alive;
        if (bodies_[i].temper != Temper::Asleep && bodies_[i].temper != Temper::Dead) ++out.roused;
        if (bodies_[i].walking) ++out.walking;
    }
    return out;
}

void Realm::say(What what, const Body& who, int32_t a, int32_t b, int32_t c, uint32_t whom) {
    Happening happening;
    happening.tick = uint32_t(tick_);
    happening.what = what;
    happening.who = who.id;
    happening.whom = whom;
    happening.a = a;
    happening.b = b;
    happening.c = c;
    happening.x = who.x;
    happening.y = who.y;
    happenings_.push_back(happening);
}

bool Realm::raise(const content::Tables* tables, uint64_t seed, int playerColumn,
                  int playerRow, Kin kin, int level) {
    tables_ = tables;
    if (!tables_ || tables_->grid.empty()) return false;
    dice_.seed(seed);
    router_.open(&tables_->grid);
    bodies_.clear();
    happenings_.clear();
    happenings_.reserve(4096);
    // The ground: a minute of drops from a fast hunt is a few dozen; 512 is never reached.
    lying_.reserve(512);
    scratch_.reserve(512);
    tick_ = 0;
    nextId_ = 1;
    pending_ = Request{};
    order_ = Request{};

    // The player first, and at index 0 for good: every loop below walks an index, and "the
    // player is bodies_[0]" is cheaper and steadier than a search.
    Body hero;
    hero.id = nextId_++;
    hero.player = true;
    hero.kin = kin;
    hero.points = startingPoints(kin);
    hero.level = std::max(1, std::min(level, kMaximumLevel));
    // A character asked for above level 1 gets his levels and the points that came with them,
    // and the points are spent nowhere: where they go is the player's choice and there is no
    // player down here. It is the honest shape -- a level-20 knight with 95 unspent points is
    // exactly what a level-20 knight who has never opened the window is.
    hero.experience = neededExperience(hero.level);
    hero.pointsInHand = (hero.level - 1) * kPointsPerLevel;
    reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
    restoreMana(hero);
    hero.health = hero.maxHealth;
    hero.speed = 1.0f / float(kHeroMoveTicks);
    int column = playerColumn, row = playerRow;
    if (!router_.nearestOpen(column, row, content::kWallCharacter, 16, &column, &row)) {
        core::logError("the player has nowhere to stand near (%d, %d)", playerColumn, playerRow);
        return false;
    }
    hero.x = float(column);
    hero.y = float(row);
    hero.homeColumn = column;
    hero.homeRow = row;
    hero.temper = Temper::Wandering;
    bodies_.push_back(std::move(hero));
    reswing(bodies_[0]);

    // Then every nest, in the table's own order. Placement rejects a tile the threshold
    // refuses and draws again: between 6% and 12% of every Lorencia nest rectangle is
    // un-standable, so blind placement puts monsters inside walls. Each attempt costs two
    // draws, which is itself part of the seeded stream and is why the attempt count is
    // bounded rather than "until it works".
    size_t placed = 0, short_ = 0;
    for (const content::MonsterNest& nest : tables_->nests) {
        const content::MonsterKind& kind = tables_->kinds[nest.kind];
        for (uint32_t n = 0; n < nest.count; ++n) {
            int tileColumn = 0, tileRow = 0;
            bool found = false;
            for (int attempt = 0; attempt < 20 && !found; ++attempt) {
                tileColumn = dice_.nextInt(nest.x1, nest.x2 + 1);
                tileRow = dice_.nextInt(nest.y1, nest.y2 + 1);
                // Not in the town, either: two of Lorencia's nests clip the safe zone by a
                // fraction of a percent, which is enough to put a Hound inside the ring where
                // nothing may be attacked.
                found = tables_->grid.open(tileColumn, tileRow, content::kWallCharacter) &&
                        !tables_->grid.safe(tileColumn, tileRow);
            }
            if (!found) {
                ++short_;
                continue;
            }
            Body beast;
            beast.id = nextId_++;
            beast.kind = int32_t(nest.kind);
            beast.level = kind.level;
            beast.maxHealth = kind.health;
            beast.health = kind.health;
            beast.stats.level = kind.level;
            beast.stats.attackRate = kind.attackRate;
            beast.stats.defenseRate = kind.defenseRate;
            beast.stats.defense = kind.defense;
            beast.stats.minimumDamage = kind.minimumDamage;
            beast.stats.maximumDamage = kind.maximumDamage;
            beast.swingTicks = kind.attackTicks;
            beast.speed = 1.0f / float(std::max(1, kind.moveTicks));
            beast.x = float(tileColumn);
            beast.y = float(tileRow);
            beast.homeColumn = tileColumn;
            beast.homeRow = tileRow;
            beast.temper = Temper::Asleep;
            // OpenMU's start delay, so a whole nest does not think on one tick forever after.
            beast.thinksAt = dice_.nextInt(0, 100);
            // A route's worth of tiles, taken now rather than on the tick the animal first
            // walks. A wander is at most a few tiles and a chase across a nest is tens; 64 is
            // over the worst either has produced, and a route past it grows once.
            beast.route.reserve(64);
            bodies_.push_back(std::move(beast));
            ++placed;
        }
    }
    if (short_ > 0) {
        core::logError("%zu monsters had no standable tile in their nest after 20 attempts",
                       short_);
    }

    players_.clear();
    indexOfId_.assign(bodies_.size() + 1, uint32_t(bodies_.size()));
    for (uint32_t i = 0; i < bodies_.size(); ++i) {
        indexOfId_[bodies_[i].id] = i;
        if (bodies_[i].player) players_.push_back(i);
    }

    for (const Body& one : bodies_) say(What::Spawned, one, one.level, one.health);
    core::logf("realm: map %u raised, %zu monsters of %zu breeds in %zu nests, player at "
               "(%d, %d), seed %llu", tables_->map, placed, tables_->kinds.size(),
               tables_->nests.size(), column, row, (unsigned long long)seed);
    return true;
}

HeroRecord Realm::record() const {
    HeroRecord out;
    const Body& hero = bodies_[0];
    out.kin = hero.kin;
    out.column = hero.column();
    out.row = hero.row();
    out.facing = hero.facing;
    out.level = hero.level;
    out.experience = hero.experience;
    out.pointsInHand = hero.pointsInHand;
    out.points = hero.points;
    out.health = hero.health;
    out.mana = hero.mana;
    out.money = money_;
    for (int slot = 0; slot < kSlots; ++slot) out.slots[slot] = bag_[slot];
    return out;
}

void Realm::restore(const HeroRecord& saved) {
    Body& hero = bodies_[0];
    hero.level = std::max(1, std::min(saved.level, kMaximumLevel));
    hero.experience = saved.experience;
    hero.pointsInHand = std::max(0, saved.pointsInHand);
    hero.points = saved.points;
    hero.facing = hero.aim = saved.facing;
    money_ = std::max<int64_t>(0, saved.money);
    bag_.clear();
    for (int slot = 0; slot < kSlots; ++slot) {
        const Held& one = saved.slots[slot];
        if (one.empty() || size_t(one.item) >= tables_->items.size()) continue;
        bag_.put(slot, one);
    }
    rearm(hero);
    hero.health = saved.health > 0 ? std::min(saved.health, hero.maxHealth) : hero.maxHealth;
    hero.mana = std::max(0, std::min(saved.mana, hero.maxMana));
}

bool Realm::spend(int strength, int agility, int vitality, int energy) {
    if (strength < 0 || agility < 0 || vitality < 0 || energy < 0) return false;
    Body& hero = bodies_[0];
    const int asked = strength + agility + vitality + energy;
    if (asked == 0 || asked > hero.pointsInHand) return false;
    hero.points.strength += strength;
    hero.points.agility += agility;
    hero.points.vitality += vitality;
    hero.points.energy += energy;
    hero.pointsInHand -= asked;
    const int was = hero.maxHealth;
    reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
    restoreMana(hero);
    // Agility buys attack speed, so spending a point can change how often he swings.
    reswing(hero);
    // Vitality's health arrives full rather than as a bigger empty bar, which is what MU does
    // when a point is spent and is the only part of this that is not pure arithmetic.
    hero.health = std::min(hero.maxHealth, hero.health + std::max(0, hero.maxHealth - was));
    return true;
}

// ---- walking ---------------------------------------------------------------------------

bool Realm::send(Body& one, int column, int row) {
    // The tile he is standing on, asked for while he is between two tiles: a stop THERE. The
    // router has no route from a tile to itself, and this used to be a refusal, which left the
    // old walk running -- a click on his own feet mid-stride carried him on to wherever he was
    // going before. Half a step back to the centre of the tile is the answer the click meant.
    if (column == one.column() && row == one.row() &&
        (std::fabs(one.x - float(column)) > 1e-3f || std::fabs(one.y - float(row)) > 1e-3f)) {
        one.route.clear();
        one.route.push_back(Step{int16_t(column), int16_t(row)});
        one.onStep = 0;
        one.walking = true;
        say(What::Walked, one, column, row, 1);
        return true;
    }
    if (!router_.plan(one.column(), one.row(), column, row, content::kWallCharacter, scratch_)) {
        // A refusal is an event and not a silence. It was a silence for one evening, and the
        // scripted hand -- which asks again whenever it is not walking -- asked for the same
        // impossible tile every tick for the rest of the run: nine thousand ticks in which
        // nothing whatever was written to the log, because nothing whatever happened.
        say(What::Refused, one, column, row);
        return false;
    }
    const int32_t tiles = int32_t(scratch_.size());
    // Pulled tight from where the body really stands, MU2's Route.Along: an eight-way search
    // on tiles answers any heading that is not straight or diagonal with a staircase, and
    // walked tile to tile that staircase is a zig-zag. The legs are exactly as clear as the
    // tiles were -- Router::sees tests every tile a leg touches -- so nothing walks through
    // anything the plan went round. Invention against MU, which walks the staircase.
    router_.pull(one.x, one.y, content::kWallCharacter, scratch_);
    one.route.assign(scratch_.begin(), scratch_.end());
    one.onStep = 0;
    one.walking = true;
    // The goal the route actually ends on, not the one asked for: the router moves a goal in a
    // wall to the nearest open tile, and the marker is put down from this event.
    say(What::Walked, one, one.route.back().column, one.route.back().row, tiles);
    return true;
}

void Realm::halt(Body& one) {
    if (!one.walking) return;
    one.walking = false;
    one.route.clear();
    one.onStep = 0;
    say(What::Halted, one);
}

// One tick of movement along the line the route describes. The diagonal is NOT penalised here
// and that is the whole of the diagonal question: MU's A* costs a diagonal 7 against a
// straight 5 because that is a SEARCH cost, and OpenMU's GetStepDelay scales the step delay by
// 1.414 because that is a SPEED. Taking both makes a diagonal walk 41% slow and taking neither
// makes it fast. Here a walk is a line and a line has a length, which is MU2's answer
// (Walker.cs:63-67) and is the one that needs no penalty of either kind.
// Turns a body toward its aim and says whether it is still swinging round on the spot -- in
// which case it may not cover ground this tick. Walker.cs:275-286.
bool Realm::turn(Body& one) {
    constexpr float kToRadians = 3.14159265359f / 180.0f;
    const float apart = wrapped(one.aim - one.facing);
    // One tick's worth, and the tick is the only clock: 900 degrees a second at 20 Hz is 45
    // degrees a tick, so a full reversal takes four ticks.
    const float most = kTurnDegrees * kToRadians / 20.0f;
    // Kept within a half turn either side of nothing: a hand that keeps clicking behind him
    // wound it past 400 degrees, harmless to every reader but a trap for the next one.
    one.facing = wrapped(std::fabs(apart) <= most ? one.aim
                                                  : one.facing + (apart < 0.0f ? -most : most));
    one.turning = std::fabs(wrapped(one.aim - one.facing)) > kPivotDegrees * kToRadians;
    return one.turning;
}

void Realm::advance(Body& one) {
    // A body that is standing still still comes round: a fighter between two blows turns onto
    // what it is hitting, and a walk that has just been given spends its first tick or two
    // turning before any ground is covered.
    if (!one.walking) {
        turn(one);
        return;
    }
    // Aimed at the tile it is walking to, and turned toward it BEFORE any ground is covered.
    if (one.onStep < one.route.size()) {
        const Step& target = one.route[one.onStep];
        const float dx = float(target.column) - one.x;
        const float dy = float(target.row) - one.y;
        if (dx * dx + dy * dy > 1e-6f) one.aim = std::atan2(dy, dx);
    }
    if (turn(one)) return;  // still coming round: no ground this tick
    // A tile crossed is said when the body's own tile changes, not when it reaches a point of
    // its route: a pulled route's points are the ends of long legs, and the log's Stepped
    // still means one tile.
    const int wasColumn = one.column(), wasRow = one.row();
    float left = one.speed;
    while (left > 0.0f && one.onStep < one.route.size()) {
        const Step& target = one.route[one.onStep];
        const float dx = float(target.column) - one.x;
        const float dy = float(target.row) - one.y;
        const float distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= 1e-6f) {
            ++one.onStep;
            continue;
        }
        one.aim = std::atan2(dy, dx);
        if (distance <= left) {
            one.x = float(target.column);
            one.y = float(target.row);
            left -= distance;
            ++one.onStep;
        } else {
            one.x += dx / distance * left;
            one.y += dy / distance * left;
            left = 0.0f;
        }
    }
    if (one.column() != wasColumn || one.row() != wasRow) {
        say(What::Stepped, one, one.column(), one.row());
    }
    if (one.onStep >= one.route.size()) {
        one.walking = false;
        one.route.clear();
        one.onStep = 0;
        say(What::Halted, one);
    }
}

// ---- the mind --------------------------------------------------------------------------

bool Realm::worth(const Body& beast, const Body& target, int range) const {
    return target.alive() && within(beast, target, float(range)) &&
           !tables_->grid.safe(target.column(), target.row());
}

void Realm::rouse(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    const float far = float(kind.viewRange + kMargin);
    float nearest = 1e30f;
    for (uint32_t who : players_) {
        // Alive or not, and the "or not" is the point: being roused is about whether anybody
        // can SEE the monster, and a dead character is still standing there. Whether there is
        // anything worth attacking is a separate question and worth() does check.
        nearest = std::min(nearest, reach(beast, bodies_[who]));
    }
    const bool roused = nearest <= far || (beast.provoked && beast.quarry != 0);
    const bool was = beast.temper != Temper::Asleep;
    if (roused == was) return;

    if (roused) {
        beast.temper = Temper::Wandering;
        // A monster that has just woken thinks on the NEXT tick and with a jitter, which is
        // OpenMU's start delay: a whole nest that woke on one tick would swing in unison
        // forever after. No jitter for one woken by a blow -- there is one animal, it has
        // already been told what hit it, and the stagger would only be slowness where somebody
        // is watching for a reaction.
        beast.thinksAt = tick_ + (beast.provoked ? 1 : dice_.nextInt(1, std::max(2, 20 / 4)));
        say(What::Roused, beast, 1, nearest > 1e29f ? -1 : int32_t(nearest));
    } else {
        beast.temper = Temper::Asleep;
        beast.quarry = 0;
        beast.provoked = false;
        halt(beast);
        say(What::Roused, beast, 0, -1);
    }
}

void Realm::wander(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    if (kind.moveRange <= 0) return;
    // Two draws, always in this order, and the tile is tested rather than corrected: a
    // wander that retried until it found somewhere would consume a variable number of draws.
    const int column = beast.column() + dice_.nextInt(-kind.moveRange, kind.moveRange + 1);
    const int row = beast.row() + dice_.nextInt(-kind.moveRange, kind.moveRange + 1);
    if (tables_->grid.open(column, row, content::kWallCharacter)) send(beast, column, row);
}

void Realm::retreat(Body& beast) {
    if (beast.walking || tick_ < beast.repathsAt) return;
    beast.repathsAt = tick_ + kRepath;
    if (!send(beast, beast.homeColumn, beast.homeRow)) wander(beast);
}

void Realm::engage(Body& one, const Body& target) {
    halt(one);
    // Aimed at what it is hitting. A halt keeps whatever aim the walk it ended had, and a blow
    // is the only other thing that aims, so a fighter stopped in reach between two blows faces
    // wherever it was last walking -- MU2 measured a Skeleton Warrior 105 degrees off the
    // knight it was fighting.
    one.aim = std::atan2(target.y - one.y, target.x - one.x);
}

// Whether a chase needs planning again: the chaser has stopped, or its quarry has moved a whole
// tile from where it was when the line was drawn. Realm.cs:1985-1996, and the tile is MU2's
// `Drift`: under it the approach tile would not change anyway and re-planning only jitters the
// line; over it the walker is heading somewhere its quarry has left.
//
// This was `quarry.column() != chaseColumn`, which is a WHOLE-tile comparison and fires the
// moment a monster's rounded position changes -- so a fighter standing next to something that
// shuffled between two tiles re-planned three times a second and took a step each time. It read
// as a character walking while he was hitting something, which is what it was: measured over
// 4 000 ticks, 205 walk orders for 140 blows. With the drift it is 24.
bool Realm::drifted(const Body& chaser, const Body& target) const {
    constexpr float kDrift = 1.0f;
    return !chaser.walking ||
           std::fabs(target.x - chaser.chaseX) + std::fabs(target.y - chaser.chaseY) > kDrift;
}

// The nearest tile beside a target that a walker can both stand on and finish its approach
// from. Nearest rather than OpenMU's random one: the randomness is there to stop a pack
// converging on one tile, and nearest-to-the-walker keeps that spread by construction while
// being STABLE -- asked again a tick later from almost the same place it gives the same
// answer, so a chase that re-plans three times a second does not zig-zag.
bool Realm::beside(const Body& target, int radius, const Body& walker, int* column, int* row) {
    bool found = false;
    float closest = 1e30f;
    for (int down = -radius; down <= radius; ++down) {
        for (int across = -radius; across <= radius; ++across) {
            if (across == 0 && down == 0) continue;
            const int c = target.column() + across, r = target.row() + down;
            if (!tables_->grid.open(c, r, content::kWallCharacter)) continue;
            if (c == walker.column() && r == walker.row()) continue;
            // Standing here has to be close enough on the same measure the arrival will be
            // judged by -- the centre of this tile against where the target actually is. Without
            // this the approach and the arrival are in different spaces and can disagree
            // forever, which is a deadlock rather than a wobble.
            if (std::max(std::fabs(float(c) - target.x), std::fabs(float(r) - target.y)) >
                float(radius)) {
                continue;
            }
            const float toX = float(c) - walker.x, toY = float(r) - walker.y;
            const float far = toX * toX + toY * toY;
            if (!found || far < closest) {
                found = true;
                closest = far;
                *column = c;
                *row = r;
            }
        }
    }
    return found;
}

void Realm::think(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];

    // The quarry it has, while it is worth having; else the nearest that is. A target learned
    // by being hit is not measured against eyesight at all -- what bounds that chase is the
    // grudge, which is asked first.
    uint32_t chosen = 0;
    if (strayed(beast) <= (beast.provoked ? kGrudge : kLeash)) {
        if (const Body* held = find(beast.quarry)) {
            const bool keep = beast.provoked
                                  ? held->alive() && !tables_->grid.safe(held->column(), held->row())
                                  : worth(beast, *held, kind.viewRange);
            if (keep) chosen = held->id;
        }
        if (chosen == 0) {
            float closest = 1e30f;
            for (uint32_t who : players_) {
                const Body& one = bodies_[who];
                if (!worth(beast, one, kind.viewRange)) continue;
                const float distance = reach(beast, one);
                if (distance < closest) {
                    closest = distance;
                    chosen = one.id;
                }
            }
        }
    }
    beast.quarry = chosen;

    if (chosen == 0) {
        // Lost, so the grudge goes with it: provoked qualifies a quarry and means nothing
        // without one.
        beast.provoked = false;
        if (strayed(beast) > kLeash) {
            beast.temper = Temper::Homing;
            retreat(beast);
            return;
        }
        beast.temper = Temper::Wandering;
        if (!beast.walking && tick_ >= beast.thinksAt) {
            beast.thinksAt = tick_ + kind.attackTicks;
            wander(beast);
        }
        return;
    }

    const Body& quarry = *find(chosen);
    // In reach: stop, face it, and swing when the swing comes off its own clock. A monster
    // standing in a safe zone cannot attack out of it, which is the same tile bit that stops
    // it being attacked there.
    if (within(beast, quarry, float(kind.attackRange)) &&
        !tables_->grid.safe(beast.column(), beast.row())) {
        beast.temper = Temper::Fighting;
        engage(beast, quarry);
        if (tick_ >= beast.swingsAt) {
            // The swing clock is the beast's own and is NOT the thinking clock. They were one
            // field in MU2 once, so making a monster think oftener made it hit oftener, which
            // is a fight the player loses for reasons nothing on screen explains. It is also
            // where a per-skill cooldown will go.
            beast.swingsAt = tick_ + kind.attackTicks;
            strikeAt(beast, *body(chosen));
        }
        return;
    }

    beast.temper = Temper::Chasing;
    const bool chases = beast.provoked || within(beast, quarry, float(kind.viewRange + 1));
    if (chases && tick_ >= beast.repathsAt) {
        beast.repathsAt = tick_ + kRepath;
        // Only when it has stopped or its quarry has moved a tile. Re-planning on the clock
        // alone is not harmless: the line is pulled tight from wherever the walker is standing,
        // so the same order given three times a second yields a slightly different line each
        // time and the animal picks its way over in a zig-zag.
        if (drifted(beast, quarry)) {
            beast.chaseX = quarry.x;
            beast.chaseY = quarry.y;
            int column = 0, row = 0;
            if (beside(quarry, std::max(1, kind.attackRange), beast, &column, &row) &&
                send(beast, column, row)) {
                return;
            }
        } else {
            return;
        }
    }

    if (!beast.walking && tick_ >= beast.thinksAt) {
        beast.thinksAt = tick_ + kind.attackTicks;
        wander(beast);
    }
}

// ---- the fight -------------------------------------------------------------------------

void Realm::strikeAt(Body& attacker, Body& target) {
    if (!target.alive()) return;  // no blow lands on the dead: the invariant, kept here
    const Blow blow = strike(attacker.stats, target.stats, dice_);
    if (!blow.hit) {
        say(What::Missed, attacker, 0, 0, 0, target.id);
        return;
    }
    // The shield takes nine tenths, and what it cannot cover falls through to health: a pool
    // with three points left protects by three and no more. MU2's Realm.Wound, off OpenMU's
    // GetHitInfo shieldRatio and Player.HitAsync's overflow. Monsters have none.
    int wound = blow.damage;
    if (target.sd > 0) {
        const int onto = int(float(blow.damage) * kShieldShare);
        const int over = onto - target.sd;
        target.sd = std::max(0, target.sd - onto);
        wound = blow.damage - onto + std::max(0, over);
    }
    target.health = std::max(0, target.health - wound);
    say(What::Hit, attacker, blow.damage, blow.rolled, target.health, target.id);
    if (!target.player) {
        // Hit, so it knows who did it however far off he is standing, and it is awake whether
        // or not it can see him. Without this half a caster outside its sight kills it without
        // it ever running a thought.
        target.provoked = true;
        target.quarry = attacker.id;
        if (target.temper == Temper::Asleep) target.temper = Temper::Wandering;
    }
    if (target.health <= 0) kill(target, attacker);
}

void Realm::kill(Body& dead, Body& killer) {
    dead.temper = Temper::Dead;
    dead.walking = false;
    dead.route.clear();
    dead.onStep = 0;
    dead.quarry = 0;
    dead.provoked = false;
    say(What::Died, dead, dead.level, 0, 0, killer.id);

    if (dead.player) {
        // He stands up in town three seconds later, at the map's own spawn box with his health
        // restored -- MU's answer to where is a property of the map and not of the death, and
        // reviving him where he fell puts him back inside whatever killed him. Realm.cs:2013.
        dead.risesAt = tick_ + kRiseTicks;
        order_ = Request{};
        pending_ = Request{};
        return;
    }

    const content::MonsterKind& kind = tables_->kinds[size_t(dead.kind)];
    dead.risesAt = tick_ + kind.respawnTicks;
    // Every monster the killer is still holding as a quarry forgets it, or a chase carries on
    // toward a corpse.
    for (Body& one : bodies_) {
        if (one.quarry == dead.id) {
            one.quarry = 0;
            one.provoked = false;
        }
    }
    // What it leaves, before the experience is paid, so the Zen reads the killer's level as
    // it was when the blow landed.
    if (killer.player) leave(dead, killer);
    if (killer.player) {
        // (int) of the formula, as OpenMU's CalculateAfterKillAsync truncates it
        // (PlayerExperience.cs:105). No rate: a replica that quietly pays several times over
        // is not a replica.
        gain(killer, int32_t(killExperience(dead.level, killer.level)));
    }
}

void Realm::gain(Body& hero, int32_t award) {
    if (award <= 0) return;
    // PlayerExperience.cs:197-240: a while loop, because one kill can carry more than one
    // level, and the experience is spent up to each threshold rather than poured past it.
    // Experience past the cap is discarded rather than banked.
    int32_t remaining = award;
    while (remaining > 0) {
        if (hero.level >= kMaximumLevel) return;
        const uint64_t needed = neededExperience(hero.level + 1);
        uint64_t gained = uint64_t(remaining);
        bool levels = false;
        if (needed - hero.experience < gained) {
            gained = needed - hero.experience;
            levels = true;
        }
        hero.experience += gained;
        say(What::Gained, hero, int32_t(gained), int32_t(hero.experience));
        if (!levels) return;
        ++hero.level;
        hero.pointsInHand += kPointsPerLevel;
        // Re-reckoned and then refilled, in that order: the health a level gives is part of
        // the maximum it is refilled to.
        reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
        restoreMana(hero);
        reswing(hero);
        hero.health = hero.maxHealth;
        hero.mana = hero.maxMana;
        hero.sd = hero.maxSd;
        say(What::Levelled, hero, hero.level, hero.pointsInHand);
        remaining -= int32_t(gained);
    }
}

void Realm::reviveHero() {
    Body& hero = bodies_[0];
    const int32_t* gate = tables_->safeGate;
    int column = hero.column(), row = hero.row();
    if (gate[2] > gate[0] && gate[3] > gate[1]) {
        // A tile drawn from inside the box, then the nearest standable one to it. The box is a
        // rectangle and the town inside it is not all floor, so a draw lands on something about
        // one time in three; MU2 watched a character stand up on his own corpse beside the thing
        // that killed him before it added the fallback.
        column = dice_.nextInt(gate[0], gate[2] + 1);
        row = dice_.nextInt(gate[1], gate[3] + 1);
        int open = column, openRow = row;
        if (router_.nearestOpen(column, row, content::kWallCharacter, 8, &open, &openRow)) {
            column = open;
            row = openRow;
        }
    }
    hero.x = float(column);
    hero.y = float(row);
    hero.health = hero.maxHealth;
    hero.mana = hero.maxMana;
    hero.sd = hero.maxSd;
    hero.sdCarry = 0.0f;
    hero.temper = Temper::Wandering;
    hero.walking = false;
    hero.route.clear();
    hero.onStep = 0;
    hero.quarry = 0;
    hero.swingsAt = tick_;
    hero.repathsAt = 0;
    say(What::Rose, hero, hero.level, hero.health);
}

void Realm::raiseBeast(Body& beast) {
    const content::MonsterKind& kind = tables_->kinds[size_t(beast.kind)];
    beast.health = beast.maxHealth;
    beast.x = float(beast.homeColumn);
    beast.y = float(beast.homeRow);
    beast.temper = Temper::Asleep;
    beast.quarry = 0;
    beast.provoked = false;
    beast.walking = false;
    beast.route.clear();
    beast.onStep = 0;
    beast.swingsAt = tick_ + kind.attackTicks;
    beast.thinksAt = tick_;
    say(What::Rose, beast, beast.level, beast.health);
}

// ---- the player ------------------------------------------------------------------------

// The order the window raised since the last tick becomes the one he is following. Taken
// BEFORE he moves this tick, not after: after, a click waited a whole tick in `pending_`, was
// planned at the end of the next one, and was first walked on the tick after that -- 100 ms
// of the character ignoring the mouse before the drawing's own interpolation added its 50.
void Realm::accept() {
    Body& hero = bodies_[0];
    if (!hero.alive()) return;

    if (pending_.kind != Request::Kind::None) {
        order_ = pending_;
        pending_ = Request{};
        // Any order is walking away from a counter, including another Talk.
        trading_ = -1;
        if (order_.kind == Request::Kind::WalkTo) {
            send(hero, order_.column, order_.row);
        } else if (order_.kind == Request::Kind::Stop) {
            halt(hero);
            order_ = Request{};
        } else if (order_.kind == Request::Kind::Pick) {
            for (const Lying& one : lying_) {
                if (one.id == order_.target) send(hero, one.column, one.row);
            }
        } else if (order_.kind == Request::Kind::Talk) {
            if (order_.target >= tables_->folk.size()) {
                order_ = Request{};
            } else if (!serving(int(order_.target))) {
                const content::Townsperson& one = tables_->folk[order_.target];
                send(hero, one.x, one.y);
            }
        }
    }
}

void Realm::press() {
    Body& hero = bodies_[0];
    if (!hero.alive()) return;

    if (order_.kind == Request::Kind::Pick) {
        // Taken on arrival: within a tile of it, which is standing on it or beside it -- the
        // grid may refuse the tile itself when something died against a wall. The reach is
        // this project's; MU picks up when the walk ends on the item.
        size_t at = lying_.size();
        for (size_t i = 0; i < lying_.size(); ++i) {
            if (lying_[i].id == order_.target) at = i;
        }
        if (at == lying_.size()) {
            order_ = Request{};
            return;
        }
        const Lying& one = lying_[at];
        if (std::fabs(hero.x - float(one.column)) <= 1.0f &&
            std::fabs(hero.y - float(one.row)) <= 1.0f) {
            halt(hero);
            take(at);
            order_ = Request{};
        }
        return;
    }

    if (order_.kind == Request::Kind::Talk) {
        // Served the tick he is within reach, whether he walked there or was already there.
        // A townsperson who sells nothing -- a guard, the vault keeper -- is walked to and
        // then nothing happens, which is MU's own answer to talking to a guard.
        if (serving(int(order_.target))) {
            const content::Townsperson& one = tables_->folk[order_.target];
            halt(hero);
            if (sells(one.number)) {
                trading_ = int(order_.target);
                say(What::Served, hero, trading_, one.number);
            }
            order_ = Request{};
        }
        return;
    }

    if (order_.kind != Request::Kind::Attack) return;
    const Body* target = find(order_.target);
    if (!target || !target->alive()) {
        order_ = Request{};
        return;
    }
    if (within(hero, *target, float(kHeroAttackRange)) &&
        !tables_->grid.safe(target->column(), target->row())) {
        engage(hero, *target);
        if (tick_ >= hero.swingsAt) {
            hero.swingsAt = tick_ + hero.swingTicks;
            strikeAt(hero, *body(order_.target));
        }
        return;
    }
    // Out of reach: close, on the same re-plan clock a monster's chase uses -- but not until
    // the blow he is in the middle of has finished.
    //
    // A swing and a step are never both, and the way out of a swing is to cancel it. For the
    // player that interval IS the swing animation: sim/swings.cpp works `swingTicks` out of the
    // attack clip's own keys and play speed, so `tick_ < swingsAt` is exactly "the axe is still
    // coming down". Without this line a quarry that shuffled one tile pulled him out of his own
    // blow and he walked with the axe still swinging -- which he then did on three quarters of
    // every swing frame drawn.
    //
    // What cancels it is an order, and only an order: a click on the ground, a click on
    // something else, or a stop, all of which arrive above as `pending_` and replace this one
    // before this line is reached. So the player is never held still by his own attack -- he
    // gives it up, which is what an attack cancel is -- and the chase, which is the engine's
    // decision rather than his, waits its turn.
    if (tick_ < hero.swingsAt) return;
    if (tick_ >= hero.repathsAt) {
        hero.repathsAt = tick_ + kRepath;
        if (drifted(hero, *target)) {
            hero.chaseX = target->x;
            hero.chaseY = target->y;
            int column = 0, row = 0;
            if (beside(*target, kHeroAttackRange, hero, &column, &row)) send(hero, column, row);
        }
    }
}

// ---- the tick --------------------------------------------------------------------------

void Realm::step() {
    ++tick_;
    happenings_.clear();

    // The order is fixed and is written down because it is the behaviour: the player walks and
    // swings, then every monster is roused, thinks and moves in index order, then the dead are
    // considered for respawn. Nothing here walks a hash container, and every id came from one
    // monotonic counter.
    Body& hero = bodies_[0];
    sip();
    recover(hero);
    // What has lain its minute goes, in the order it lies -- a fixed order, since the list is
    // only ever appended to and swapped out of by the tick's own events.
    for (size_t i = 0; i < lying_.size();) {
        if (lying_[i].vanishesAt <= tick_) {
            say(What::Vanished, hero, int32_t(lying_[i].id));
            lying_[i] = lying_.back();
            lying_.pop_back();
        } else {
            ++i;
        }
    }
    if (hero.alive()) {
        accept();
        advance(hero);
        press();
    } else if (tick_ >= hero.risesAt) {
        reviveHero();
    }

    for (size_t i = 1; i < bodies_.size(); ++i) {
        Body& beast = bodies_[i];
        if (beast.alive()) {
            rouse(beast);
            if (beast.temper != Temper::Asleep) {
                advance(beast);
                think(beast);
            } else if (beast.walking) {
                // Asleep, but not until it has come to a stand. A monster whose walk ended on
                // the tick nobody was left near it would otherwise keep that tick's pace for as
                // long as it slept, and the drawing decides walk or idle from exactly that.
                advance(beast);
            }
        } else if (tick_ >= beast.risesAt) {
            raiseBeast(beast);
        }
    }
}

// ---- the log ---------------------------------------------------------------------------

std::string describe(const Happening& happening, const Realm& realm) {
    const auto name = [&realm](uint32_t id) -> std::string {
        const Body* one = realm.find(id);
        if (!one) return "nobody";
        if (one->player) return "hero";
        return realm.tables()->kinds[size_t(one->kind)].label + "#" + std::to_string(id);
    };
    char line[512];
    const char* who = nullptr;
    std::string whoName = name(happening.who);
    who = whoName.c_str();
    // Fixed precision everywhere, and never %g: the log's own formatting is part of the
    // contract two runs are compared under.
    switch (happening.what) {
        case What::Spawned:
            std::snprintf(line, sizeof(line), "%6u spawned %s at %.3f,%.3f level %d hp %d",
                          happening.tick, who, happening.x, happening.y, happening.a,
                          happening.b);
            break;
        case What::Rose:
            std::snprintf(line, sizeof(line), "%6u rose %s at %.3f,%.3f hp %d", happening.tick,
                          who, happening.x, happening.y, happening.b);
            break;
        case What::Roused:
            std::snprintf(line, sizeof(line), "%6u %s %s at %.3f,%.3f nearest %d",
                          happening.tick, happening.a ? "woke" : "slept", who, happening.x,
                          happening.y, happening.b);
            break;
        case What::Refused:
            std::snprintf(line, sizeof(line), "%6u %s cannot reach %d,%d from %.3f,%.3f",
                          happening.tick, who, happening.a, happening.b, happening.x,
                          happening.y);
            break;
        case What::Walked:
            std::snprintf(line, sizeof(line), "%6u %s walks to %d,%d in %d steps",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
        case What::Stepped:
            std::snprintf(line, sizeof(line), "%6u %s steps to %d,%d", happening.tick, who,
                          happening.a, happening.b);
            break;
        case What::Halted:
            std::snprintf(line, sizeof(line), "%6u %s halts at %.3f,%.3f", happening.tick, who,
                          happening.x, happening.y);
            break;
        case What::Missed:
            std::snprintf(line, sizeof(line), "%6u %s misses %s", happening.tick, who,
                          name(happening.whom).c_str());
            break;
        case What::Hit:
            std::snprintf(line, sizeof(line), "%6u %s hits %s for %d (roll %d) leaving %d",
                          happening.tick, who, name(happening.whom).c_str(), happening.a,
                          happening.b, happening.c);
            break;
        case What::Died:
            std::snprintf(line, sizeof(line), "%6u %s dies at %.3f,%.3f to %s", happening.tick,
                          who, happening.x, happening.y, name(happening.whom).c_str());
            break;
        case What::Gained:
            std::snprintf(line, sizeof(line), "%6u %s gains %d experience, %d in all",
                          happening.tick, who, happening.a, happening.b);
            break;
        case What::Drank:
            std::snprintf(line, sizeof(line), "%6u %s drinks for %d %s", happening.tick, who,
                          happening.a, happening.b ? "mana" : "health");
            break;
        case What::Served:
            std::snprintf(line, sizeof(line), "%6u %s is served by %s", happening.tick, who,
                          realm.tables()->folk[size_t(happening.a)].name.c_str());
            break;
        case What::Bought:
            std::snprintf(line, sizeof(line), "%6u %s buys %s for %d into slot %d", happening.tick,
                          who, realm.tables()->items[size_t(happening.a)].label.c_str(),
                          happening.b, happening.c);
            break;
        case What::Sold:
            std::snprintf(line, sizeof(line), "%6u %s sells %s for %d from slot %d", happening.tick,
                          who, realm.tables()->items[size_t(happening.a)].label.c_str(),
                          happening.b, happening.c);
            break;
        case What::Dropped:
            if (happening.b < 0) {
                std::snprintf(line, sizeof(line), "%6u %s leaves %d Zen (#%d) at %.3f,%.3f",
                              happening.tick, who, happening.c, happening.a, double(happening.x),
                              double(happening.y));
            } else {
                std::snprintf(line, sizeof(line), "%6u %s leaves %s +%d (#%d) at %.3f,%.3f",
                              happening.tick, who,
                              realm.tables()->items[size_t(happening.b)].label.c_str(), happening.c,
                              happening.a, double(happening.x), double(happening.y));
            }
            break;
        case What::Picked:
            std::snprintf(line, sizeof(line), "%6u %s picks up #%d into slot %d (%d Zen)",
                          happening.tick, who, happening.a, happening.b, happening.c);
            break;
        case What::Vanished:
            std::snprintf(line, sizeof(line), "%6u #%d vanishes", happening.tick, happening.a);
            break;
        case What::Levelled:
            std::snprintf(line, sizeof(line), "%6u %s reaches level %d with %d points",
                          happening.tick, who, happening.a, happening.b);
            break;
    }
    return std::string(line);
}

}  // namespace mu::sim
