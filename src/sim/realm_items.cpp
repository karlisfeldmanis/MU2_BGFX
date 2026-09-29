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
#include "sim/wear.h"

namespace mu::sim {

// How long anything left on the ground lies there, in seconds: Loot.Lingers, and the same for
// a kill's drop and for a thing the character throws away -- MU gives a discard no shorter
// life than a kill's, and a shorter one would be a trap on a misdragged sword.
constexpr int kLingerSeconds = 60;

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
        // Wear, on the band and its plus together, truncated as MuMain's CalculateDamage takes
        // `DamageMin - (WORD)(DamageMin * percent)`. A broken weapon adds nothing.
        arms.weaponMinimumDamage -= int(float(arms.weaponMinimumDamage) * one.weaponCut);
        arms.weaponMaximumDamage -= int(float(arms.weaponMaximumDamage) * one.weaponCut);
        arms.criticalChance = double(one.luckyWorn) * kLuckCritical;
        arms.excel = one.excel;
        arms.staffRise = double(one.staffRise);
        arms.archery = one.archer != 0;
        arms.greaterDamage = one.mightUntil > tick_ ? one.might : 0;
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
    // An Ale's twenty ride with the excellent option's seven: both are AttackSpeedAny in OpenMU.
    const int extra = hero.excel.speed + (hero.aleUntil > tick_ ? kAleSpeed : 0);
    const int milliseconds =
        swingMilliseconds(*tables_, hero.kin, hero.points.agility, right, left, extra);
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
        if (hand >= 0) bag_.put(hand, Held{item, 0, int16_t(fullDurability(row, 0))});
    }
    // What the cradle gives beside a bow type: a full quiver in the other hand. OpenMU's
    // AddArrowsForFairyElf -- 255 Arrows in slot 0 beside the Short Bow in slot 1 -- and the
    // Bolt the same way beside a crossbow, which no cradle in 0.75 hands out but an arena does.
    // With or without `given`: this call is only ever the cradle, an arena or the harness arming
    // her, and a bow handed over with nothing to shoot is no weapon at all.
    if (weapon >= 0) {
        const content::Arm& arm = tables_->arms[size_t(weapon)];
        const int hand = arm.bow() ? kWeaponRight : arm.crossbow() ? kWeaponLeft : -1;
        const int number = arm.bow() ? kArrowsNumber : kBoltNumber;
        for (size_t i = 0; hand >= 0 && i < tables_->items.size(); ++i) {
            const content::ItemRow& row = tables_->items[i];
            if (row.group != kGroupBows || row.number != number) continue;
            if (bag_[hand].empty()) bag_.put(hand, Held{int32_t(i), 0, int16_t(fullDurability(row, 0))});
            break;
        }
    }
    rearm(hero);
    return true;
}

// MuMain's CheckArrow and SearchArrow: the quiver hand is the one the bow leaves free (arrows on
// the right beside a bow, the bolt on the left beside a crossbow). Empty, it is filled from the
// bag by `FindItemReverseIndex` -- the LAST matching slot first -- and one piece is spent, hit or
// miss (AmmunitionConsumptionRate 1 on every bow, Version075/Items/Weapons.cs:329). The wrong
// kind in hand, or none anywhere, and nothing is spent.
bool Realm::nock(Body& hero) {
    if (hero.archer == 0) return true;
    const int hand = hero.archer == 1 ? kWeaponRight : kWeaponLeft;
    const int wanted = hero.archer == 1 ? kArrowsNumber : kBoltNumber;
    const auto fits = [&](const Held& h) {
        if (h.empty()) return false;
        const content::ItemRow& row = tables_->items[size_t(h.item)];
        return row.group == kGroupBows && row.number == wanted;
    };
    if (bag_[hand].empty()) {
        for (int slot = kSlots - 1; slot >= kWorn; --slot) {
            if (!fits(bag_[slot])) continue;
            bag_.put(hand, bag_.lift(slot));
            break;
        }
    }
    if (!fits(bag_[hand]) || bag_[hand].durability <= 0) return false;
    Held left = bag_[hand];
    left.durability = int16_t(left.durability - 1);
    if (left.durability <= 0) {
        bag_.lift(hand);
    } else {
        bag_.put(hand, left);
    }
    return true;
}

bool Realm::quivered(const Body& hero) const {
    if (hero.archer == 0) return false;
    const int hand = hero.archer == 1 ? kWeaponRight : kWeaponLeft;
    const int wanted = hero.archer == 1 ? kArrowsNumber : kBoltNumber;
    const auto fits = [&](const Held& h) {
        if (h.empty() || h.durability <= 0) return false;
        const content::ItemRow& row = tables_->items[size_t(h.item)];
        return row.group == kGroupBows && row.number == wanted;
    };
    if (!bag_[hand].empty()) return fits(bag_[hand]);
    for (int slot = kSlots - 1; slot >= kWorn; --slot) {
        if (fits(bag_[slot])) return true;
    }
    return false;
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
    // A bow type, by the arm's own flags. The hand rule already put a bow on the left and a
    // crossbow on the right, so the other hand is the quiver's.
    hero.archer = 0;
    if (hero.weapon >= 0 && size_t(hero.weapon) < tables_->arms.size()) {
        const content::Arm& held = tables_->arms[size_t(hero.weapon)];
        hero.archer = held.bow() ? 1 : held.crossbow() ? 2 : 0;
    }
    // The additional option on the weapon adds to both ends of the band, as Stats.PhysicalBaseDmg
    // does, and wears with it. A staff's is wizardry damage, which nothing here reckons yet.
    if (weaponSlot >= 0 && rowAt(weaponSlot)->magicPower == 0) {
        hero.weaponBonus += optionValue(*rowAt(weaponSlot), bag_[weaponSlot].option);
    }
    // A staff's rise: half its magic power, and its plus off one of two tables by whether that
    // power is even or odd (Version075/Items/Weapons.cs:29-30, :315). It is what a staff is FOR
    // -- a staff grants no spell in 0.75 -- and the wizardry band is multiplied by a hundredth of
    // it. Its additional option and its excellent 4 and 5 are wizardry damage too and are not
    // reckoned yet; the remarks below still say so.
    hero.staffRise = 0.0f;
    if (weaponSlot >= 0 && rowAt(weaponSlot)->magicPower > 0) {
        static const float kEven[16] = {0, 3, 7, 10, 14, 17, 21, 24, 28, 31, 35, 40, 45, 50, 56, 63};
        static const float kOdd[16] = {0, 4, 7, 11, 14, 18, 21, 25, 28, 32, 36, 40, 45, 51, 57, 63};
        const int power = rowAt(weaponSlot)->magicPower;
        const int plus = std::clamp(int(bag_[weaponSlot].refinement), 0, 15);
        hero.staffRise = float(power) / 2.0f + (power % 2 == 0 ? kEven[plus] : kOdd[plus]);
    }
    // Being excellent: the weapon's band + min x 25 / drop level + 5 (sim::excellentDamage).
    if (weaponSlot >= 0 && bag_[weaponSlot].excellent != 0) {
        hero.weaponBonus += excellentDamage(*rowAt(weaponSlot));
    }
    // What the excellent options come to: a weapon's from the hands, the defence family's from
    // the shield and the five armour slots. ExcellentOptions.cs, bit n being option n + 1.
    hero.excel = Excellence{};
    for (int slot = kWeaponRight; slot <= kBoots; ++slot) {
        const content::ItemRow* row = rowAt(slot);
        const uint8_t bits = bag_[slot].excellent;
        if (!row || bits == 0) continue;
        const auto has = [bits](int n) { return (bits >> n) & 1; };
        Excellence& e = hero.excel;
        if (row->armour() || row->shield()) {
            if (has(0)) e.zenRate *= 1.4;
            if (has(1)) e.defenseRateRate *= 1.1;
            if (has(2)) e.reflect += 0.05;
            if (has(3)) e.damageDecrease += 0.04;
            if (has(4)) e.manaRate *= 1.04;
            if (has(5)) e.healthRate *= 1.04;
        } else if (row->weapon() && !ammunition(*row)) {
            if (has(0)) e.killMana += 1.0 / 8.0;
            if (has(1)) e.killLife += 1.0 / 8.0;
            if (has(2)) e.speed += 7;
            // A staff's 4 and 5 are wizardry damage, which nothing here reckons yet.
            if (has(3) && row->magicPower == 0) e.damageRate *= 1.02;
            if (has(4) && row->magicPower == 0) ++e.levelPieces;
            if (has(5)) e.excellentChance += 0.1;
        }
    }
    // Luck on anything worn, from the hands to the boots.
    hero.luckyWorn = 0;
    for (int slot = kWeaponRight; slot <= kBoots; ++slot) {
        const content::ItemRow* row = rowAt(slot);
        if (row && takesOptions(*row) && bag_[slot].luck) ++hero.luckyWorn;
    }
    // What each piece's wear takes off it (sim/wear.h), read off the slot it is worn in.
    const auto cutAt = [&](int slot) {
        const Held& h = bag_[slot];
        const content::ItemRow& r = tables_->items[size_t(h.item)];
        return wearCut(h.durability, maximumDurability(r, h));
    };
    hero.weaponCut = weaponSlot >= 0 ? cutAt(weaponSlot) : 0.0f;
    hero.shield = left && left->shield() ? tables_->armNamed(left->name) : -1;
    hero.wornDefense = 0;
    hero.wornDefenseRate = 0;
    hero.shieldDefense = 0;
    for (int slot = kWeaponLeft; slot <= kBoots; ++slot) {
        const content::ItemRow* row = rowAt(slot);
        if (!row || (!row->shield() && !row->armour())) continue;
        // CalculateDefense's `defense -= (WORD)(defense * percent)`, piece by piece, and a
        // piece worn to nothing is skipped whole (`Durability != 0`).
        const float cut = cutAt(slot);
        // Armour's additional option is Stats.DefenseBase, added with the piece's own; a
        // shield's is its defence rate instead (below).
        const int option = row->shield() ? 0 : optionValue(*row, bag_[slot].option);
        const int excellent = bag_[slot].excellent != 0 ? excellentDefense(*row) : 0;
        const int defense = row->defense + defenseBonus(row->shield(), bag_[slot].refinement) +
                            option + excellent;
        hero.wornDefense += defense - int(float(defense) * cut);
        // The shield's block column rises on the armour's table: _shieldDefenseRateIncreaseTable
        // is built from DefenseIncreaseByLevel. Worn down the same way (CalculateSuccessfulBlocking).
        if (row->shield()) {
            hero.shieldDefense = defense - int(float(defense) * cut);
            const int rate = row->defenseRate + defenseBonus(false, bag_[slot].refinement) +
                             optionValue(*row, bag_[slot].option) +
                             (bag_[slot].excellent != 0 ? excellentBlock(*row) : 0);
            hero.wornDefenseRate += rate - int(float(rate) * cut);
        }
    }
    const int was = hero.maxHealth;
    reckon(hero.kin, hero.level, hero.points, armsOf(hero), &hero.stats, &hero.maxHealth);
    // **A guard stands only behind the shield that raised it.** Taking the shield off ends
    // Defense or Soul Barrier on the spot, aura and all (the user, 2026-09-28): the skill asks for
    // a shield to be cast, and a guard that outlived the shield would be the one way round that.
    // The cooldown is left running, so putting the shield back on is not a way to recast sooner.
    if (hero.boonUntil > tick_) {
        const SkillRow* boon = skillNumbered(hero.boonSkill);
        const content::Arm* shield = hero.shield >= 0 ? &tables_->arms[size_t(hero.shield)] : nullptr;
        if (boon != nullptr && (boon->families & arms::kShield) != 0 &&
            !boon->suits(familyOf(shield))) {
            hero.boonUntil = 0;
            hero.boonSkill = skill::kNone;
            hero.boonDamageTaken = 1.0f;
        }
    }
    keepBoon(hero);
    restoreMana(hero);
    reswing(hero);
    hero.health = std::min(hero.maxHealth, hero.health + std::max(0, hero.maxHealth - was));
}

Wearer Realm::wearer() const {
    const Body& hero = bodies_[0];
    const auto armAt = [&](int32_t at) -> const content::Arm* {
        return at >= 0 && size_t(at) < tables_->arms.size() ? &tables_->arms[size_t(at)] : nullptr;
    };
    return Wearer{hero.kin,
                  hero.level,
                  hero.points,
                  hero.learned,
                  hero.shieldDefense,
                  hero.stats.wizardMinimum,
                  hero.stats.wizardMaximum,
                  hero.stats.wizardryRate,
                  hero.staffRise,
                  familyOf(armAt(hero.weapon)),
                  familyOf(armAt(hero.shield))};
}

int Realm::give(int32_t item, int slot, int refinement, int durability, bool luck, int option,
                uint8_t excellent, uint8_t sockets, const uint8_t* powers) {
    if (!tables_ || item < 0 || size_t(item) >= tables_->items.size()) return -1;
    const content::ItemRow& row = tables_->items[size_t(item)];
    // A stack asked for anywhere pours into what he carries, twenty a cell.
    if (slot < 0 && stacks(row)) {
        return pour(*tables_, bag_, kWorn, kSlots,
                    Held{item, int16_t(refinement), int16_t(std::max(1, durability))});
    }
    if (slot < 0) slot = bag_.free(*tables_, row.width, row.height);
    if (slot < 0 || !bag_[slot].empty() ||
        (baggable(slot) && !bag_.room(*tables_, slot, row.width, row.height))) {
        return -1;
    }
    if (durability < 0) durability = fullDurability(row, refinement);
    Held put{item, int16_t(refinement), int16_t(durability)};
    if (takesOptions(row)) {
        put.luck = luck;
        put.option = int8_t(std::clamp(option, 0, kMostOption));
        put.excellent = uint8_t(excellent & 63);
        put.sockets = uint8_t(std::min<int>(sockets, kMostSockets));
        for (int i = 0; i < put.sockets && powers; ++i) put.powers[i] = powers[i];
        // Whole means whole with its fifteen, when it was asked for whole.
        if (put.excellent && durability == fullDurability(row, refinement) && wears(row)) {
            put.durability = int16_t(maximumDurability(row, put));
        }
    }
    // A Rune of Creation carries its power.
    if (creation(row) && powers) put.powers[0] = powers[0];
    bag_.put(slot, put);
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

    // ---- an orb is read, and what is left is the skill ----------------------------------------
    //
    // The route the user asked for on 2026-09-23 and the one docs/skills-dk.md §3.3 always
    // described: *bought or dropped, right-click to learn, permanent*. Nothing is handed over any
    // more -- a knight is made with an empty bar and every key on it was bought at Hanzo's or
    // found. `SkillRow::needLevel` is gone with the grant; the level lives on the ITEM, which is
    // where a requirement belongs and where the tooltip already prints it in red.
    //
    // The refusals, in the order the tooltip reads: who may hold it, what he must be, and
    // whether he has read it before. Each is silent, as every refusal down here is.
    // The Orb of Summoning teaches by its plus (sim::asRead); every other row as it stands.
    if (const content::ItemRow read = asRead(row, potion.refinement); read.teaches != 0) {
        // mu.db's class enumeration, as `fits` reads it: bit 0 wizard, 1 elf, 2 knight, and none
        // named is anybody. OpenMU asks this of an orb at the moment it is read, not worn.
        if (row.classes != 0 && (row.classes & (1 << int(hero.kin))) == 0) return false;
        if (hero.level < std::max(row.teachesLevel, asks(row, potion.refinement).level)) {
            return false;
        }
        // A scroll's energy, which is the requirement itself and is not scaled
        // (`ItemExtensions.GetRequirement` returns it whole for anything unwearable): Fire Ball's
        // forty is forty, so a new wizard with thirty keeps the scroll in his bag until he has
        // spent ten points. OpenMU asks it at the read (`CompliesRequirements`).
        if (hero.points.energy < read.teachesEnergy) return false;
        // A second orb of something he knows is refused rather than eaten: `learn` says no to a
        // skill already learned, and the orb stays in the bag to be sold.
        if (!learn(read.teaches)) return false;
        bag_.lift(slot);
        return true;
    }

    // One piece off what was used: ConsumeSourceItemAsync's `Durability -= 1`, and the slot
    // emptied at nought.
    const auto spendOne = [&] {
        Held left = potion;
        left.durability = int16_t(potion.durability - 1);
        if (left.durability <= 0) {
            bag_.lift(slot);
        } else {
            bag_.put(slot, left);
        }
    };

    // ---- the Ale: eighty seconds of a quicker arm ---------------------------------------------
    //
    // No cooldown and no refusal while one stands: ApplyMagicEffectConsumeHandlerPlugIn asks
    // neither, and throws the standing effect away for the new one, so a second Ale is a fresh
    // eighty seconds and never a second twenty.
    if (ale(row)) {
        hero.aleUntil = tick_ + kAleTicks;
        spendOne();
        reswing(hero);
        say(What::Soused, hero, int32_t(kAleTicks), hero.swingTicks);
        return true;
    }

    // ---- the Town Portal Scroll: back to town, at once ----------------------------------------
    //
    // TownPortalScrollConsumeHandlerPlugIn: spend it, then WarpToAsync the map's safe zone
    // spawn gate. No cooldown, no cast and no wait -- the three seconds on MU's GT 157 belong to
    // the levelled scrolls (ZzzInventory.cpp:4238 gates it on Level 1 to 8), and this is the
    // plain one. What else a warp takes is MuMain's ReceiveTeleport with its Flag set: every
    // window shut (HideAll), the hero stopped (SetPlayerStop), nothing selected and no attack
    // standing (`Attacking = -1`). The monsters that were on him lose him, since a warp takes
    // him out of every viewport that held him.
    if (portal(row)) {
        spendOne();
        rise(hero);
        dropBlow(hero);
        order_ = Request{};
        pending_ = Request{};
        wants_ = skill::kNone;
        trading_ = -1;
        banking_ = -1;
        const std::pair<int, int> landing = haven();
        setDown(hero, landing.first, landing.second);
        hero.facing = hero.aim;
        hero.turning = false;
        for (Body& one : bodies_) {
            if (one.player || one.quarry != hero.id) continue;
            one.quarry = 0;
            one.provoked = false;
        }
        // Her summon does not come along: a warp dismisses it, as her death does (the user,
        // 2026-09-29). OpenMU places it at her landing gate instead (PlayerSummon.PlaceAtGate).
        if (summonSlot_ >= 0) dismiss(bodies_[size_t(summonSlot_)]);
        say(What::Warped, hero, landing.first, landing.second);
        return true;
    }

    // ---- the Antidote: the poison on him is gone ------------------------------------------------
    //
    // AntidoteConsumeHandlerPlugIn: it looks up the poison among his effects and disposes of it,
    // and nothing else. With no poison on him it is refused and kept -- ours; OpenMU would spend it
    // on nothing.
    if (antidote(row)) {
        if (hero.poisonUntil <= tick_) return false;
        hero.poisonUntil = 0;
        spendOne();
        say(What::Cured, hero);
        return true;
    }

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
    spendOne();
    say(What::Drank, hero, total, mana ? 1 : 0);
    return true;
}

// OpenMU's UpgradeItemLevelJewelConsumeHandlerPlugIn.ModifyItem, with the two configurations
// its Bless and Soul handlers give it: the Bless at 100 percent, the Soul at 50 and 25 more on
// a lucky thing, a Soul that misses taking one off and everything from +7. The jewel is spent
// either way -- "true doesn't mean that it was successful, just that the consumption
// happened". A success makes the thing whole at its new plus (GetMaximumDurabilityOfOnePiece);
// a miss leaves the wear where it was, held under the lower plus's maximum.
//
// On a worn thing too. OpenMU refuses one (ItemModifyConsumeHandlerPlugIn) and says the
// original server allowed it; the original is what this is, as MU2's Realm.Refine decided.
//
// Luck is `Held::luck`, rolled on a drop. And the Bless draws nothing from the dice: at 100 the
// roll cannot matter, and a seeded log without a Soul in it stays the log it was.
bool Realm::refine(int jewelSlot, int targetSlot) {
    constexpr int kSoulChance = 50;    // SoulJewelConsumeHandlerPlugIn.SuccessRatePercentage
    constexpr int kSoulResetFrom = 7;  // ResetToLevel0WhenFailMinLevel
    if (!tables_ || !baggable(jewelSlot) || targetSlot < 0 || targetSlot >= kSlots ||
        jewelSlot == targetSlot) {
        return false;
    }
    Body& hero = bodies_[0];
    if (!hero.alive()) return false;
    const Held jewel = bag_[jewelSlot];
    Held thing = bag_[targetSlot];
    // A Rune of Creation is set, not spent on a roll: its power into the thing's first empty
    // socket, and the rune gone. Nothing is drawn.
    if (settable(*tables_, jewel, thing, hero.kin)) {
        const int socket = freeSocket(thing);
        thing.powers[socket] = jewel.powers[0];
        bag_.lift(jewelSlot);
        bag_.put(targetSlot, thing);
        if (wearable(targetSlot)) rearm(hero);
        say(What::Set, hero, targetSlot, thing.powers[socket], socket);
        return true;
    }
    if (!refinable(*tables_, jewel, thing)) return false;
    const content::ItemRow& row = tables_->items[size_t(thing.item)];
    const bool soul = jewelOf(tables_->items[size_t(jewel.item)]) == Jewel::Soul;

    const int was = thing.refinement;
    constexpr int kSoulLuck = 25;      // SuccessRateBonusWithLuckPercentage
    const int chance = kSoulChance + (thing.luck ? kSoulLuck : 0);
    const bool took = !soul || dice_.nextInt(0, 100) < chance;  // NextRandomBool(percent)
    if (took) {
        thing.refinement = int16_t(was + 1);
        thing.durability = int16_t(maximumDurability(row, thing));
    } else {
        thing.refinement = int16_t(was >= kSoulResetFrom ? 0 : std::max(0, was - 1));
        const int most = maximumDurability(row, thing);
        if (most > 0) thing.durability = int16_t(std::min<int>(thing.durability, most));
    }
    // The jewel first, then the thing, so a window redrawing between the two sees a hole and
    // never a refined thing beside the jewel that refined it. MU2's order.
    if (jewel.durability > 1) {
        Held left = jewel;
        left.durability = int16_t(jewel.durability - 1);
        bag_.put(jewelSlot, left);
    } else {
        bag_.lift(jewelSlot);
    }
    bag_.put(targetSlot, thing);
    if (wearable(targetSlot)) rearm(hero);
    say(What::Refined, hero, targetSlot, was, thing.refinement);
    return true;
}

// The shield comes back in a safe zone and nowhere else: a fiftieth of its maximum every three
// seconds, the fraction carried. MU2's Realm.Recover and Rates.ShieldRecoveryInSafeZone, off
// OpenMU's RegenerateAsync, which skips the shield outside one.
void Realm::recover(Body& hero) {
    // Mana first, and it is not the shield's rule: the shield comes back in the town alone, and
    // mana comes back everywhere -- OpenMU's regeneration runs wherever the character is standing
    // and only the shield carries the safe-zone test (Player.RegenerateAsync). Three seconds,
    // 1/27.5 of the pool, carried as a fraction so a small pool still fills.
    if (hero.alive() && tick_ % kRecoverEveryTicks == 0 && hero.mana < hero.maxMana) {
        hero.manaCarry += float(hero.maxMana) * kManaRecoveryShare;
        const int whole = int(hero.manaCarry);
        hero.manaCarry -= float(whole);
        if (whole > 0) hero.mana = std::min(hero.maxMana, hero.mana + whole);
    }
    // Health on the same three seconds, a hundredth of the pool, and only on a safe tile.
    if (hero.alive() && tick_ % kRecoverEveryTicks == 0) {
        if (hero.health >= hero.maxHealth || !tables_->grid.safe(hero.column(), hero.row())) {
            hero.healthCarry = 0.0f;
        } else {
            hero.healthCarry += float(hero.maxHealth) * kHealthRecoveryInSafeZone;
            const int whole = int(hero.healthCarry);
            hero.healthCarry -= float(whole);
            if (whole > 0) hero.health = std::min(hero.maxHealth, hero.health + whole);
        }
    }
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
    constexpr int kMostRefined = kRefineCap;
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

    // The jewels group is not only the jewels: `AddItemToJewelItemDrop` puts the Ale (drop level
    // 15) and the Town Portal Scroll (30) in it too, and the three pets, which have no rows
    // here. Drawn by the monster's level alone, with no twelve-level gap (GenerateItemFromGroup's
    // `isJewel`). In Lorencia that is the Chaos from level 12 and the Ale from 15; the Bless (25)
    // and the Soul (30) are never left by anything in the town.
    const auto jewelGroup = [](const content::ItemRow& r) {
        return r.jewel() || (r.group == kGroupPotions && (r.number == 9 || r.number == 10));
    };
    if (roll <= kJewel) {
        const int32_t item = draw([&](const content::ItemRow& r) { return jewelGroup(r) && reaches(r); });
        if (item < 0) return;
        one.what = Held{item, 0, 1};
    } else if ((roll -= kJewel) <= kExcellentChance) {
        // GenerateRandomExcellentItem: nothing from a monster under 25, and otherwise what a
        // monster 25 levels lower would drop, at +0, with luck and the option rolled as on any
        // drop, then one excellent option and a rare second. OpenMU's list admits a row with no
        // excellent options at all -- a potion comes out "excellent" with nothing on it -- and
        // that quirk is left out here: only a row that can carry them is drawn.
        const int lower = level - kExcellentLevelDelta;
        if (lower < 0) return;
        const int32_t item = draw([&](const content::ItemRow& r) {
            return r.dropsFromMonsters() && excellentable(r) && r.dropLevel <= lower &&
                   (r.maximumDropLevel == 0 || lower <= r.maximumDropLevel) &&
                   r.dropLevel > lower - kGap;
        });
        if (item < 0) return;
        const content::ItemRow& row = tables_->items[size_t(item)];
        one.what = Held{item, 0, int16_t(fullDurability(row, 0))};
        one.what.luck = dice_.nextBool(kLuckChance);
        if (dice_.nextBool(kOptionChance)) {
            one.what.option = int8_t(dice_.nextInt(1, kMostOptionDropped + 1));
        }
        const int first = dice_.nextInt(0, kExcellentOptions);
        one.what.excellent = uint8_t(1u << first);
        one.what.durability = int16_t(maximumDurability(row, one.what));
        if (dice_.nextBool(kSecondExcellentChance)) {
            // AddRandomExcOptions draws again until it is not the first.
            int second = dice_.nextInt(0, kExcellentOptions - 1);
            if (second >= first) ++second;
            one.what.excellent |= uint8_t(1u << second);
        }
    } else if ((roll -= kExcellentChance) <= kItem) {
        const int32_t item = draw([&](const content::ItemRow& r) {
            return r.dropsFromMonsters() && reaches(r) && r.dropLevel > level - kGap;
        });
        if (item < 0) return;
        const content::ItemRow& row = tables_->items[size_t(item)];
        // Prize.TakesRefinement: the weapon and armour groups, ammunition taken back out.
        const bool refinable = row.group <= kGroupBoots && !ammunition(row);
        const int plus = refinable ? std::clamp((level - row.dropLevel) / 3, 0, kMostRefined) : 0;
        const bool stacks = heals(row) || restores(row);
        // Whole at its plus, as DefaultDropGenerator sets `GetMaximumDurabilityOfOnePiece`.
        one.what = Held{item, int16_t(plus), int16_t(stacks ? 1 : fullDurability(row, plus))};
        // ApplyRandomOptions: its PossibleItemOptions in the order the initialisers add them,
        // luck first and the additional option after, each at a quarter; the option's level from
        // the levels up to MaximumItemOptionLevelDrop. No skill: skills are orbs here.
        if (takesOptions(row)) {
            one.what.luck = dice_.nextBool(kLuckChance);
            if (dice_.nextBool(kOptionChance)) {
                one.what.option = int8_t(dice_.nextInt(1, kMostOptionDropped + 1));
            }
            // Sockets: rare, and each further one rarer. invention.
            if (takesSockets(row) && runeDice_.nextBool(kSocketChance)) {
                one.what.sockets = 1;
                while (one.what.sockets < kMostSockets && runeDice_.nextBool(kMoreSocketChance)) {
                    ++one.what.sockets;
                }
            }
        }
    } else if (roll - kItem <= kMoney) {
        // Zen is not left on the ground: it goes straight into the purse, with the excellent
        // armour's rate, and is said as picked up from the body it came off (a: the dead
        // body's id), so the showing can ring the coins when that body falls. INVENTION and
        // not MU's, which drops a heap to click: the user's call, 2026-09-28.
        const int64_t zen = int64_t(killExperience(dead.level, killer.level)) + kBaseMoney;
        money_ += int64_t(double(zen) * bodies_[0].excel.zenRate);
        say(What::Picked, bodies_[0], int32_t(dead.id), -1, int32_t(zen));
        return;
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
        // An excellent armour's Zen, at the picking up (MoneyDistribution, MoneyAmountRate).
        money_ += int64_t(double(one.zen) * bodies_[0].excel.zenRate);
    } else {
        // Onto a stack of its kind first, then a free cell (kStackMost).
        slot = pour(*tables_, bag_, kWorn, kSlots, one.what);
        if (slot < 0) return false;
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
    // Measured to where he stands now, which for one on his rounds is not his table's tile.
    int column = 0, row = 0;
    folkTile(folk, &column, &row);
    const float dx = hero.x - float(column), dy = hero.y - float(row);
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
    // A stack for a potion, a full quiver for ammunition, and gear whole at its plus.
    Held bought{item, int16_t(wanted->refinement),
                int16_t(wanted->pieces > 0 ? wanted->pieces
                                           : fullDurability(row, wanted->refinement)),
                wanted->skill};
    // Placed whole or not at all before the Zen goes, a potion onto its stacks first.
    const int slot = pour(*tables_, bag_, kWorn, kSlots, bought);
    if (slot < 0) return -1;
    money_ -= price;
    say(What::Bought, bodies_[0], item, int32_t(price), slot);
    return slot;
}

int64_t Realm::sellValue(int slot) const {
    if (!baggable(slot) || bag_[slot].empty()) return -1;
    const Held& thing = bag_[slot];
    const content::ItemRow& row = tables_->items[size_t(thing.item)];
    const bool stacks = row.group == kGroupPotions;
    int64_t paid = sellingPrice(row, thing.refinement,
                                stacks ? std::max<int>(1, thing.durability) : 1, thing.skill,
                                thing.durability, row.durability, thing.luck, thing.option,
                                excellentCount(thing.excellent));
    // Worn gear fetches less: 0.6 of the share gone comes off (CalculateSellingPrice).
    if (wears(row)) {
        paid = wornSellingPrice(paid, thing.durability, maximumDurability(row, thing));
    }
    return paid > 0 ? paid : -1;
}

int64_t Realm::sellItem(int slot) {
    if (trading_ < 0 || !serving(trading_) || !baggable(slot) || bag_[slot].empty()) return -1;
    const Held thing = bag_[slot];
    const int64_t paid = std::max<int64_t>(0, sellValue(slot));
    bag_.lift(slot);
    money_ += paid;
    if (int(sold_.size()) >= kBuybacks) sold_.erase(sold_.begin());
    sold_.push_back({thing, paid, slot, tick_});
    say(What::Sold, bodies_[0], thing.item, int32_t(paid), slot);
    return paid;
}

const Realm::Sale* Realm::lastSale(int64_t* ticksLeft) const {
    if (sold_.empty()) return nullptr;
    const Sale& last = sold_.back();
    const int64_t left = last.at + int64_t(kBuybackSeconds) * 20 - tick_;
    if (left <= 0) return nullptr;
    if (ticksLeft) *ticksLeft = left;
    return &last;
}

int Realm::buyBack() {
    if (trading_ < 0 || !serving(trading_)) return -1;
    // Anything older than the newest is older still, so a lapsed newest empties the list.
    const Sale* last = lastSale();
    if (!last) {
        sold_.clear();
        return -1;
    }
    if (money_ < last->paid) return -1;
    const content::ItemRow& row = tables_->items[size_t(last->what.item)];
    int slot = -1;
    if (baggable(last->slot) && bag_.room(*tables_, last->slot, row.width, row.height)) {
        bag_.put(last->slot, last->what);
        slot = last->slot;
    } else {
        slot = pour(*tables_, bag_, kWorn, kSlots, last->what);
    }
    if (slot < 0) return -1;
    money_ -= last->paid;
    say(What::Bought, bodies_[0], last->what.item, int32_t(last->paid), slot);
    sold_.pop_back();
    return slot;
}

// ---- wear -----------------------------------------------------------------------------------
//
// OpenMU's Player.cs:1968-2045, for the one player there is. Which piece is a draw from
// `wearDice_`, so the fight's own dice see none of it.

void Realm::wearOnTaken(int took) {
    // One of the equipped defending pieces, at random: the helm to the boots and a shield in
    // the left hand (IsDefensiveItem; wings and rings are defensive there too, and are not worn
    // in this world). A broken one is still a candidate and takes nothing more, as there.
    int candidates[kWorn];
    int count = 0;
    for (int slot = kWeaponLeft; slot <= kBoots; ++slot) {
        const Held& h = bag_[slot];
        if (h.empty()) continue;
        const content::ItemRow& row = tables_->items[size_t(h.item)];
        if ((row.shield() || row.armour()) && wears(row)) candidates[count++] = slot;
    }
    if (count == 0) return;
    const int slot = candidates[wearDice_.nextInt(0, count)];
    wearDown(slot, double(took) / kDamagePerDurability);
}

void Realm::wearOnLanded() {
    // GetRandomOffensiveItem: the two hands and the pendant, less a shield and ammunition, one
    // of them by `Rand.NextInt(3, 6) % 3` and the first there is when that one is empty.
    const auto offensive = [&](int slot) {
        const Held& h = bag_[slot];
        if (h.empty()) return false;
        const content::ItemRow& row = tables_->items[size_t(h.item)];
        return !row.shield() && !ammunition(row) && wears(row);
    };
    const int order[3] = {kWeaponRight, kWeaponLeft, kAmulet};
    int result = -1;
    for (int slot : order) {
        if (result < 0 && offensive(slot)) result = slot;
    }
    if (result < 0) return;
    const int pick = order[wearDice_.nextInt(3, 6) % 3];
    if (offensive(pick)) result = pick;
    // A weapon at nought is spared, as DecreaseWeaponDurabilityAfterHitAsync returns on it.
    if (bag_[result].durability <= 0) return;
    wearDown(result, 1.0 / kHitsPerDurability);
}

void Realm::wearDown(int slot, double amount) {
    Held h = bag_[slot];
    if (h.empty() || h.durability <= 0) return;
    if (wearItem_[slot] != h.item) {
        wearItem_[slot] = h.item;
        wearCarry_[slot] = 0.0;
    }
    wearCarry_[slot] += amount;
    if (wearCarry_[slot] < 1.0) return;
    const int whole = int(wearCarry_[slot]);
    wearCarry_[slot] -= double(whole);
    const content::ItemRow& row = tables_->items[size_t(h.item)];
    const int maximum = maximumDurability(row, h);
    const float before = wearCut(h.durability, maximum);
    h.durability = int16_t(std::max(0, int(h.durability) - whole));
    // Put back only when a whole point went, so the windows mirroring the bag redraw on what
    // they can show and not on every blow (IItemDurabilityChangedPlugIn fires on the same edge).
    bag_.put(slot, h);
    Body& hero = bodies_[0];
    say(What::Worn, hero, slot, h.durability, maximum);
    if (wearCut(h.durability, maximum) != before) rearm(hero);
}

// ---- the repair -----------------------------------------------------------------------------

bool Realm::mending() const {
    return tables_ && trading_ >= 0 && serving(trading_) &&
           repairsAt(tables_->folk[size_t(trading_)].number);
}

bool Realm::selfMending() const {
    const Body& hero = bodies_[0];
    return hero.alive() && hero.level >= kSelfRepairLevel;
}

int64_t Realm::repairCost(int slot) const {
    if (!tables_ || slot < 0 || slot >= kSlots) return 0;
    const Held& h = bag_[slot];
    if (h.empty()) return 0;
    // CalcRepairCost's SelfRepair: the counter's price at a counter that mends, else his own.
    return repairPrice(tables_->items[size_t(h.item)], h.refinement, h.skill, h.durability,
                       mending());
}

int64_t Realm::repairAllCost() const {
    int64_t total = 0;
    for (int slot = 0; slot < kSlots; ++slot) total += repairCost(slot);
    return total;
}

bool Realm::repair(int slot) {
    if ((!mending() && !selfMending()) || slot < 0 || slot >= kSlots) return false;
    const int64_t cost = repairCost(slot);
    if (cost <= 0 || !pay(cost)) return false;
    Held h = bag_[slot];
    h.durability = int16_t(maximumDurability(tables_->items[size_t(h.item)], h));
    bag_.put(slot, h);
    if (wearable(slot)) {
        wearCarry_[slot] = 0.0;
        rearm(bodies_[0]);
    }
    say(What::Repaired, bodies_[0], slot, int32_t(cost), 1);
    return true;
}

int Realm::repairAll() {
    if (!mending()) return 0;
    int mended = 0;
    int64_t paid = 0;
    bool worn = false;
    for (int slot = 0; slot < kSlots; ++slot) {
        const int64_t cost = repairCost(slot);
        if (cost <= 0) continue;
        if (!pay(cost)) break;  // RepairAllItemsAsync: NotEnoughMoneyToRepair, and it stops
        Held h = bag_[slot];
        h.durability = int16_t(maximumDurability(tables_->items[size_t(h.item)], h));
        bag_.put(slot, h);
        if (wearable(slot)) {
            wearCarry_[slot] = 0.0;
            worn = true;
        }
        paid += cost;
        ++mended;
    }
    if (worn) rearm(bodies_[0]);
    if (mended > 0) say(What::Repaired, bodies_[0], -1, int32_t(paid), mended);
    return mended;
}

// ---- the vault ------------------------------------------------------------------------------
//
// Each move looks for the room at its own footprint BEFORE anything leaves where it was, so a
// refusal changes nothing: the realm's own rule for a purchase. Nothing re-reckons the hero,
// because nothing worn is ever moved here.
//
// A stack let go on a stack of its kind pours into it and leaves behind what does not fit, and
// one sent to no cell in particular tops up the stacks there first (kStackMost).

namespace {

// Takes `went` pieces off the stack at `at`, and the cell with them when that was all of it.
template <class Grid>
void unstack(Grid& grid, int at, int went) {
    Held left = grid[at];
    left.durability = int16_t(left.durability - went);
    if (left.durability <= 0) grid.lift(at);
    else grid.put(at, left);
}

}  // namespace

int Realm::deposit(int bagSlot, int cell) {
    if (!banked() || !baggable(bagSlot) || bag_[bagSlot].empty()) return -1;
    const content::ItemRow& row = tables_->items[size_t(bag_[bagSlot].item)];
    if (cell < 0) {
        cell = pour(*tables_, vault_, 0, kVaultCells, bag_[bagSlot]);
        if (cell >= 0) bag_.lift(bagSlot);
        return cell;
    }
    const int onto = vault_.holder(*tables_, cell);
    if (const int went = onto >= 0 ? topUp(*tables_, vault_, onto, bag_[bagSlot]) : 0) {
        unstack(bag_, bagSlot, went);
        return onto;
    }
    if (!vault_.room(*tables_, cell, row.width, row.height)) return -1;
    vault_.put(cell, bag_.lift(bagSlot));
    return cell;
}

int Realm::withdraw(int cell, int bagSlot) {
    if (!banked() || vault_[cell].empty()) return -1;
    const content::ItemRow& row = tables_->items[size_t(vault_[cell].item)];
    if (bagSlot < 0) {
        bagSlot = pour(*tables_, bag_, kWorn, kSlots, vault_[cell]);
        if (bagSlot >= 0) vault_.lift(cell);
        return bagSlot;
    }
    const int onto = baggable(bagSlot) ? bag_.holder(*tables_, bagSlot) : -1;
    if (const int went = onto >= 0 ? topUp(*tables_, bag_, onto, vault_[cell]) : 0) {
        unstack(vault_, cell, went);
        return onto;
    }
    if (!baggable(bagSlot) || !bag_.room(*tables_, bagSlot, row.width, row.height)) return -1;
    bag_.put(bagSlot, vault_.lift(cell));
    return bagSlot;
}

bool Realm::rearrange(int from, int to) {
    if (!banked() || from == to || vault_[from].empty()) return false;
    const int onto = vault_.holder(*tables_, to);
    if (onto >= 0 && onto != from) {
        if (const int went = topUp(*tables_, vault_, onto, vault_[from])) {
            unstack(vault_, from, went);
            return true;
        }
    }
    const content::ItemRow& row = tables_->items[size_t(vault_[from].item)];
    if (!vault_.room(*tables_, to, row.width, row.height, from)) return false;
    vault_.put(to, vault_.lift(from));
    return true;
}

bool Realm::depositZen(int64_t zen) {
    if (!banked() || zen <= 0 || zen > money_) return false;
    money_ -= zen;
    vault_.setZen(vault_.zen() + zen);
    return true;
}

bool Realm::withdrawZen(int64_t zen) {
    if (!banked() || zen <= 0 || zen > vault_.zen()) return false;
    vault_.setZen(vault_.zen() - zen);
    money_ += zen;
    return true;
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

// The drag let go over the world. It is the one gesture in the interface that gives something
// away, which is why nothing here is clever about it: the thing comes out of the slot and lies
// where a kill's drop would lie, to be picked up again by the same Pick order.
//
// A worn slot is thrown too -- MU lets a sword be dragged out of the hand and onto the floor --
// and `rearm` is what makes his arms, his defence and his swing catch up with an empty hand.
uint32_t Realm::discard(int slot) {
    if (!tables_ || slot < 0 || slot >= kSlots) return 0;
    Body& hero = bodies_[0];
    // A dead man throws nothing away, as a dead man moves nothing: the same gate `moveItem`
    // keeps, so a window left open over a corpse cannot empty the bag.
    if (!hero.alive() || bag_[slot].empty()) return 0;
    // Nor a jewel or a +7, which MuMain will not let go of (sim::expensive).
    if (expensive(*tables_, bag_[slot])) return 0;

    Lying one;
    one.what = bag_.lift(slot);
    if (wearable(slot)) rearm(hero);
    std::tie(one.column, one.row) = clearing(hero.column(), hero.row());
    one.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
    one.id = nextId_++;
    lying_.push_back(one);
    // Said for the log and for anything reading the realm within the same tick; the showing
    // does NOT hear it -- see the note on discard() in realm.h.
    say(What::Dropped, hero, int32_t(one.id), one.what.item, one.what.refinement);
    return one.id;
}

uint32_t Realm::lay(int32_t item, int refinement, bool luck, int option, uint8_t excellent) {
    if (!tables_ || item < 0 || size_t(item) >= tables_->items.size()) return 0;
    const Body& hero = bodies_[0];
    const content::ItemRow& row = tables_->items[size_t(item)];
    Lying one;
    // Whole, as `leave` makes what a kill leaves.
    one.what = Held{item, int16_t(std::clamp(refinement, 0, kRefineCap)),
                    int16_t(std::max(1, fullDurability(row, refinement)))};
    if (takesOptions(row)) {
        one.what.luck = luck;
        one.what.option = int8_t(std::clamp(option, 0, kMostOption));
        one.what.excellent = uint8_t(excellent & 63);
        if (one.what.excellent && wears(row)) one.what.durability = int16_t(maximumDurability(row, one.what));
    }
    std::tie(one.column, one.row) = clearing(hero.column(), hero.row());
    one.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
    one.id = nextId_++;
    lying_.push_back(one);
    say(What::Dropped, hero, int32_t(one.id), one.what.item, one.what.refinement);
    return one.id;
}

}  // namespace mu::sim
