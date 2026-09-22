// What the hero carries, wears, buys, sells, drinks and picks up -- and what a death leaves
// on the ground for him. The bag and the shop are sim/items.cpp and sim/market.cpp; this is
// the Realm's side of them, where a request becomes a refusal or a change.
//
// The rule the whole of it keeps is sprint 7's: THE REALM DECIDES. A window asks and redraws
// from what it finds afterwards; nothing here trusts what a window believed.
#include "sim/realm.h"

#include "sim/swings.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

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
    // What a kill leaves: a jewel, then an item, then Zen, then nothing.
    //
    // **The item chance is ours, not MU2's 0.3.** A nest of spiders dropped a weapon or a piece
    // of armour on nearly every third kill, and the ground round a hunt was paved with them;
    // this is the rate the hunt was asked to have (2026-09-22). Zen and the jewel are MU2's
    // Loot untouched, so the commonest thing a monster leaves is still coins -- the change is
    // that three kills in four now leave coins or nothing.
    constexpr double kJewel = 0.001, kItem = 0.1, kMoney = 0.5;
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

}  // namespace mu::sim
