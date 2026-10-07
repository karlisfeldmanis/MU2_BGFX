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
    if (!one.dual && one.shield >= 0 && size_t(one.shield) < tables_->arms.size()) {
        arms.armourDefense += tables_->arms[size_t(one.shield)].defense;
    }
    // The player's defence is his worn pieces' since sprint 7 -- the shield among them, with its
    // plus -- and not the shield's row alone, so it replaces rather than adds to the above.
    if (one.player) {
        arms.armourDefense = one.wornDefense;
        arms.shieldDefenseRate = one.wornDefenseRate;
        arms.setDefense = setDefense(*tables_, bag_, one.kin);
        arms.weaponMinimumDamage += one.weapon >= 0 ? one.weaponBonus : 0;
        arms.weaponMaximumDamage += one.weapon >= 0 ? one.weaponBonus : 0;
        // Wear, on the band and its plus together, truncated as MuMain's CalculateDamage takes
        // `DamageMin - (WORD)(DamageMin * percent)`. A broken weapon adds nothing.
        arms.weaponMinimumDamage -= int(float(arms.weaponMinimumDamage) * one.weaponCut);
        arms.weaponMaximumDamage -= int(float(arms.weaponMaximumDamage) * one.weaponCut);
        // The second weapon's band, its plus and its wear, the same way round.
        if (one.dual && one.shield >= 0 && size_t(one.shield) < tables_->arms.size()) {
            const content::Arm& off = tables_->arms[size_t(one.shield)];
            arms.dual = true;
            const bool matched = one.weapon >= 0 && size_t(one.weapon) < tables_->arms.size() &&
                                 tables_->arms[size_t(one.weapon)].group == off.group;
            arms.dualRate = matched ? 1.0 : kMixedPair;
            arms.offhandMinimumDamage = off.minimumDamage + one.offhandBonus;
            arms.offhandMaximumDamage = off.maximumDamage + one.offhandBonus;
            arms.offhandMinimumDamage -= int(float(arms.offhandMinimumDamage) * one.offhandCut);
            arms.offhandMaximumDamage -= int(float(arms.offhandMaximumDamage) * one.offhandCut);
        }
        arms.criticalChance = double(one.luckyWorn) * kLuckCritical + one.excel.runeCritical;
        arms.excel = one.excel;
        arms.staffRise = double(one.staffRise);
        arms.pet = one.pet;
        arms.wingDamage = one.wingDamage;
        arms.wingWizardry = one.wingWizardry;
        arms.archery = one.archer != 0;
        arms.quiverPlus = one.quiverPlus;
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
    const int extra = hero.excel.speed + (hero.aleUntil > tick_ ? kAleSpeed : 0) +
                      hero.frenzySpeed(tick_);
    const int milliseconds =
        swingMilliseconds(*tables_, hero.kin, hero.totalPoints().agility, right, left, extra);
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
        // A knight's second weapon is asked for in the shield's place (sim::offHanded).
        const bool second = wantShield && !arm.isShield() && hero.kin == Kin::DarkKnight &&
                            arm.group < kGroupBows && !arm.twoHanded();
        if (arm.isShield() != wantShield && !second) {
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
    for (int asked = 0; asked < 2; ++asked) {
        const int32_t index = asked == 0 ? weapon : shield;
        if (index < 0) continue;
        const int32_t item = tables_->itemNamed(tables_->arms[size_t(index)].name);
        if (item < 0) {
            refusal_ = tables_->arms[size_t(index)].label + " has no item row";
            continue;
        }
        const content::ItemRow& row = tables_->items[size_t(item)];
        const int hand = asked == 1 && placesIn(row, hero.kin, kWeaponLeft) ? kWeaponLeft
                                                                           : placeOf(row);
        if (hand >= 0) bag_.put(hand, Held{item, 0, int16_t(fullDurability(row, 0))});
    }
    // What the cradle gives beside a bow type: a full quiver in the other hand. OpenMU's
    // AddArrowsForFairyElf -- 255 Arrows in slot 0 beside the Short Bow in slot 1 -- and the
    // Bolt the same way beside a crossbow, which no cradle in 0.75 hands out but an arena does.
    // With or without `given`: this call is only ever the cradle, an arena or the harness arming
    // her, and a bow handed over with nothing to shoot is no weapon at all.
    if (weapon >= 0) {
        const content::Arm& arm = tables_->arms[size_t(weapon)];
        const int hand = arm.missile() ? kWeaponLeft : -1;
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

// MuMain's CheckArrow and SearchArrow: the quiver hand is the one the bow leaves free, the left
// for both since bows and crossbows share the weapon slot (2026-10-02). Empty, it is filled from the
// bag by `FindItemReverseIndex` -- the LAST matching slot first -- and one piece is spent, hit or
// miss (AmmunitionConsumptionRate 1 on every bow, Version075/Items/Weapons.cs:329). The wrong
// kind in hand, or none anywhere, and nothing is spent.
bool Realm::nock(Body& hero) {
    if (hero.archer == 0) return true;
    const int hand = kWeaponLeft;
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
    // A quiver of another plus came into the hand, or the last one left it: the band follows.
    if (quiverPlusOf(hero) != hero.quiverPlus) {
        hero.quiverPlus = int8_t(quiverPlusOf(hero));
        reckon(hero.kin, hero.level, hero.totalPoints(), armsOf(hero), &hero.stats, &hero.maxHealth);
    }
    return true;
}

int Realm::quiverPlusOf(const Body& hero) const {
    if (hero.archer == 0) return 0;
    const Held& h = bag_[kWeaponLeft];
    if (h.empty() || h.durability <= 0) return 0;
    const content::ItemRow& row = tables_->items[size_t(h.item)];
    const int wanted = hero.archer == 1 ? kArrowsNumber : kBoltNumber;
    return row.group == kGroupBows && row.number == wanted ? std::clamp(int(h.refinement), 0, 3)
                                                           : 0;
}

bool Realm::arrowless(const Body& hero, const Request& order) const {
    if (hero.archer == 0 || quivered(hero)) return false;
    // A skill she can throw at it still goes; a fan is arrows and `armed` refuses it dry, and
    // one on herself is cast and then the bow is drawn anyway.
    const SkillRow* row = order.skill != skill::kNone ? skillNumbered(order.skill) : nullptr;
    return row == nullptr || row->onSelf() || !armed(hero, *row);
}

bool Realm::quivered(const Body& hero) const {
    if (hero.archer == 0) return false;
    const int hand = kWeaponLeft;
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
void Realm::rearm(Body& hero, const Satchel& kit) {
    const auto rowAt = [&](int slot) -> const content::ItemRow* {
        const Held& h = kit[slot];
        return h.empty() ? nullptr : &tables_->items[size_t(h.item)];
    };
    const content::ItemRow* right = rowAt(kWeaponRight);
    const content::ItemRow* left = rowAt(kWeaponLeft);
    const auto swung = [](const content::ItemRow* r) {
        return r && r->weapon() && !ammunition(*r);
    };
    int weaponSlot = swung(right) ? kWeaponRight : (swung(left) ? kWeaponLeft : -1);
    hero.weapon = weaponSlot >= 0 ? tables_->armNamed(rowAt(weaponSlot)->name) : -1;
    hero.weaponBonus = weaponSlot >= 0 ? damageBonus(kit[weaponSlot].refinement) : 0;
    // A bow type, by the arm's own flags. The hand rule puts both in the right hand, so the
    // left is the quiver's.
    hero.archer = 0;
    if (hero.weapon >= 0 && size_t(hero.weapon) < tables_->arms.size()) {
        const content::Arm& held = tables_->arms[size_t(hero.weapon)];
        hero.archer = held.bow() ? 1 : held.crossbow() ? 2 : 0;
    }
    // A raider shoots without a quiver (realm_raid.cpp); the quiver read is the hero's bag.
    hero.quiverPlus = hero.raider >= 0 ? 0 : int8_t(quiverPlusOf(hero));
    // The additional option on the weapon adds to both ends of the band, as Stats.PhysicalBaseDmg
    // does, and wears with it. A staff's is wizardry damage, which nothing here reckons yet.
    if (weaponSlot >= 0 && rowAt(weaponSlot)->magicPower == 0) {
        hero.weaponBonus += optionValue(*rowAt(weaponSlot), kit[weaponSlot].option);
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
        const int plus = std::clamp(int(kit[weaponSlot].refinement), 0, 15);
        hero.staffRise = float(power) / 2.0f + (power % 2 == 0 ? kEven[plus] : kOdd[plus]);
    }
    // Being excellent: the weapon's band + min x 25 / drop level + 5 (sim::excellentDamage).
    if (weaponSlot >= 0 && kit[weaponSlot].excellent != 0) {
        hero.weaponBonus += excellentDamage(*rowAt(weaponSlot));
    }
    // A Dark Knight's second weapon (sim::offHanded): WebZen's bTwoHandWeapon, a knight with a
    // weapon below the bows in each hand (1.00.93 ObjAttack.cpp:3127-3136). The left hand's band
    // takes its own plus, option and excellence, as gObjCalCharacter adds Left's apart
    // (ObjCalCharacter.cpp:522-531).
    hero.dual = weaponSlot == kWeaponRight && right->group < kGroupBows && swung(left) &&
                offHanded(*left, hero.kin);
    hero.offhandBonus = 0;
    if (hero.dual) {
        const Held& h = kit[kWeaponLeft];
        hero.offhandBonus = damageBonus(h.refinement) + optionValue(*left, h.option) +
                            (h.excellent != 0 ? excellentDamage(*left) : 0);
    }
    // What the excellent options come to: a weapon's from the hands and the pendant, the defence
    // family's from the shield, the five armour slots and the rings. ExcellentOptions.cs, bit n
    // being option n + 1; the jewellery's families are WebZen's (zzzitem.cpp:1380-1440).
    hero.excel = Excellence{};
    for (int slot = kWeaponRight; slot <= kRingLeft; ++slot) {
        const content::ItemRow* row = rowAt(slot);
        const uint8_t bits = kit[slot].excellent;
        if (!row || bits == 0) continue;
        const auto has = [bits](int n) { return (bits >> n) & 1; };
        Excellence& e = hero.excel;
        // Each option twice MU's (the user, 2026-10-06: an excellent thing should be felt).
        if (row->armour() || row->shield() || ring(*row)) {
            if (has(0)) e.zenRate *= 1.8;
            if (has(1)) e.defenseRateRate *= 1.2;
            if (has(2)) e.reflect += 0.10;
            if (has(3)) e.damageDecrease += 0.08;
            if (has(4)) e.manaRate *= 1.08;
            if (has(5)) e.healthRate *= 1.08;
        } else if ((row->weapon() && !ammunition(*row)) || pendant(*row)) {
            if (has(0)) e.killMana += 1.0 / 4.0;
            if (has(1)) e.killLife += 1.0 / 4.0;
            if (has(2)) e.speed += 14;
            // A staff's 4 and 5 are wizardry damage, which nothing here reckons yet, and the
            // Pendant of Lightning's are the staff's.
            const bool wizardry = row->magicPower > 0 || elementOf(*row) == Element::Lightning;
            if (has(3) && !wizardry) e.damageRate *= 1.04;
            if (has(4) && !wizardry) ++e.levelPieces;
            if (has(5)) e.excellentChance += 0.2;
        }
    }
    // The Rune of the Undying, in any socket of anything worn that takes an armour's rune: the
    // armour, the shield, the rings and the pendant, not a weapon. sim::kUndyingHealth each.
    for (int slot = kWeaponRight; slot <= kRingLeft; ++slot) {
        const content::ItemRow* row = rowAt(slot);
        if (!row || (row->weapon() && !row->shield())) continue;
        for (int at = 0; at < std::min<int>(kit[slot].sockets, kMostSockets); ++at) {
            const PowerRow* power = powerOf(kit[slot].powers[at]);
            if (!power) continue;
            Excellence& e = hero.excel;
            if (power->power == Power::Undying) e.undyingRate *= kUndyingHealth;
            if (power->power == Power::KeenEye) e.runeCritical += kKeenEyeCritical;
            if (power->power == Power::Bloodwell) {
                e.lifeSteal += kBloodwellLife;
                e.killMana += kBloodwellMana;
            }
            if (power->power == Power::Frenzy) ++e.frenzies;
            if (power->power == Power::Renewal) e.renewal += kRenewalShare;
            if (power->power == Power::Spirits && (row->shield() || jewellery(*row))) ++e.spirits;
            if (power->power == Power::Plague && (row->shield() || jewellery(*row))) ++e.plagues;
            if (power->power == Power::Wisp && (row->shield() || jewellery(*row))) ++e.wisps;
            if (power->power == Power::Ironskin) e.runeDefense += kIronskinDefense;
            if (power->power == Power::Steadfast) e.blockChance += kSteadfastBlock;
            if (power->power == Power::SecondWind) {
                e.killLife += kSecondWindShare;
                e.killMana += kSecondWindShare;
            }
        }
    }
    // The element runes, in the hands, the rings and the pendant (sim::kElementRuneDamage).
    for (int slot : {kWeaponRight, kWeaponLeft, kAmulet, kRingRight, kRingLeft}) {
        const content::ItemRow* row = rowAt(slot);
        if (!row || row->shield()) continue;
        for (int at = 0; at < std::min<int>(kit[slot].sockets, kMostSockets); ++at) {
            const PowerRow* power = powerOf(kit[slot].powers[at]);
            if (power && elementOf(power->power) != Element::None) {
                ++hero.excel.elementRunes[int(elementOf(power->power))];
            }
            // A knight's or a wizard's Bulwark from his hands, Kinship from the rings and the pendant.
            if (power && power->power == Power::Bulwark && !jewellery(*row) &&
                power->takenBy(hero.kin, hero.second)) {
                hero.excel.bulwark = true;
            }
            if (power && power->power == Power::Kinship && jewellery(*row)) {
                hero.excel.kinship = true;
            }
            if (power && power->power == Power::Wrath && !jewellery(*row)) ++hero.excel.wraths;
            if (power && power->power == Power::Spite && !jewellery(*row)) ++hero.excel.spites;
            // Spirit Plague in a hand here; the shield's and the jewellery's are counted above.
            if (power && power->power == Power::Plague && !jewellery(*row)) ++hero.excel.plagues;
            if (power && power->power == Power::Scorch && !jewellery(*row) &&
                power->takenBy(hero.kin, hero.second)) {
                ++hero.excel.ignitions;
            }
            if (power && power->power == Power::PlagueArrows && !jewellery(*row) &&
                power->takenBy(hero.kin, hero.second)) {
                ++hero.excel.plagueArrows;
            }
            if (power && power->power == Power::Whirlwind && !jewellery(*row) &&
                power->takenBy(hero.kin, hero.second)) {
                ++hero.excel.whirlwinds;
            }
            if (power && power->power == Power::Volley && !jewellery(*row) &&
                power->takenBy(hero.kin, hero.second)) {
                ++hero.excel.volleys;
            }
        }
    }
    // The rings and the pendant: the largest resistance worn in each element (Max3), the
    // resistance powers summed on top, and every piece's life regeneration summed
    // (docs/jewellery.md).
    int resistancePowers[kElements] = {};
    for (int slot : {kAmulet, kRingRight, kRingLeft}) {
        const content::ItemRow* row = rowAt(slot);
        if (!row || !jewellery(*row)) continue;
        Excellence& e = hero.excel;
        const int resists = resistanceOf(*row, kit[slot].refinement);
        if (elementOf(*row) == Element::Ice) e.iceResistance = std::max(e.iceResistance, resists);
        if (elementOf(*row) == Element::Poison) {
            e.poisonResistance = std::max(e.poisonResistance, resists);
        }
        if (elementOf(*row) == Element::Lightning) {
            e.lightningResistance = std::max(e.lightningResistance, resists);
        }
        if (elementOf(*row) == Element::Fire) {
            e.fireResistance = std::max(e.fireResistance, resists);
        }
        e.lifeRegen += optionValue(*row, kit[slot].option);
        // A powered piece's signature and its further powers, every worn piece's added
        // (sim::Affix, docs/jewellery.md "Powers").
        if (!powered(*row)) continue;
        const Held& held = kit[slot];
        const auto add = [&](Affix affix) {
            const int value = affixValue(affix, held.refinement);
            switch (affix) {
                case Affix::Wisdom: e.moreExperience += value; break;
                case Affix::Wealth: e.moreZen += value; break;
                case Affix::Fortune: e.itemFind += value; break;
                case Affix::Leech: e.lifeOnHit += value; break;
                case Affix::Fury: e.criticalDamage += value; break;
                default: resistancePowers[int(affixElement(affix))] += value; break;
            }
        };
        add(signatureOf(*row));
        for (uint8_t a : held.affixes) add(Affix(a));
    }
    hero.excel.iceResistance += resistancePowers[int(Element::Ice)];
    hero.excel.poisonResistance += resistancePowers[int(Element::Poison)];
    hero.excel.lightningResistance += resistancePowers[int(Element::Lightning)];
    hero.excel.fireResistance += resistancePowers[int(Element::Fire)];
    // And Wealth on the Zen, beside the excellent armour's x1.4.
    hero.excel.zenRate *= 1.0 + double(hero.excel.moreZen) / 100.0;
    // Said once a change, when any is worn, so a run's log shows what the fight below it had.
    {
        const Excellence& e = hero.excel;
        if (e.runeCritical > 0.0 || e.lifeSteal > 0.0 || e.frenzies > 0 || e.renewal > 0.0 ||
            e.spirits > 0) {
            core::logf("runes worn: keen eye +%.2f crit, bloodwell %.2f life a wound, %d frenzy, "
                       "renewal %.2f health a 3 s, %d evil spirit",
                       e.runeCritical, e.lifeSteal, e.frenzies, e.renewal, e.spirits);
        }
    }
    // Luck on anything worn, from the hands to the boots, and the rings and the pendant (ours,
    // since they refine: the user, 2026-10-03, "that means that items can have +luck").
    hero.luckyWorn = 0;
    for (int slot = kWeaponRight; slot <= kRingLeft; ++slot) {
        const content::ItemRow* row = rowAt(slot);
        if (row && (takesOptions(*row) || jewellery(*row) || anyWing(*row)) && kit[slot].luck) {
            ++hero.luckyWorn;
        }
    }
    // What each piece's wear takes off it (sim/wear.h), read off the slot it is worn in.
    const auto cutAt = [&](int slot) {
        const Held& h = kit[slot];
        const content::ItemRow& r = tables_->items[size_t(h.item)];
        return wearCut(h.durability, maximumDurability(r, h));
    };
    hero.weaponCut = weaponSlot >= 0 ? cutAt(weaponSlot) : 0.0f;
    hero.offhandCut = hero.dual ? cutAt(kWeaponLeft) : 0.0f;
    hero.shield = left && (left->shield() || hero.dual) ? tables_->armNamed(left->name) : -1;
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
        const int option = row->shield() ? 0 : optionValue(*row, kit[slot].option);
        const int excellent = kit[slot].excellent != 0 ? excellentDefense(*row) : 0;
        const int defense = row->defense + defenseBonus(row->shield(), kit[slot].refinement) +
                            option + excellent;
        hero.wornDefense += defense - int(float(defense) * cut);
        // The shield's block column rises on the armour's table: _shieldDefenseRateIncreaseTable
        // is built from DefenseIncreaseByLevel. Worn down the same way (CalculateSuccessfulBlocking).
        if (row->shield()) {
            hero.shieldDefense = defense - int(float(defense) * cut);
            const int rate = row->defenseRate + defenseBonus(false, kit[slot].refinement) +
                             optionValue(*row, kit[slot].option) +
                             (kit[slot].excellent != 0 ? excellentBlock(*row) : 0);
            hero.wornDefenseRate += rate - int(float(rate) * cut);
        }
    }
    // The wing's defence, worn down as armour is (ItemDefense, the item's m_Defense cut by its
    // m_CurrentDurabilityState).
    if (const content::ItemRow* wing = rowAt(kWings); wing && anyWing(*wing)) {
        const int defense = wingDefense(*wing, kit[kWings].refinement);
        hero.wornDefense += defense - int(float(defense) * cutAt(kWings));
    }
    // The pet in slot 8 and the mount in its own, each while it has life (ItemPowerUpFactory.cs:
    // 38-41): their prices and gifts multiply and add, and either one ridden puts him on it.
    hero.pet = PetPower{};
    for (int slot : {kPet, kMount}) {
        const content::ItemRow* row = rowAt(slot);
        if (!row || kit[slot].durability <= 0) continue;
        const PetPower one = petPower(*row);
        hero.pet.taken *= one.taken;
        hero.pet.dealt *= one.dealt;
        hero.pet.health += one.health;
        hero.pet.lifeCost += one.lifeCost;
        hero.pet.mount = hero.pet.mount || one.mount;
    }
    // Kinship lifts the price and keeps the gift (sim/items.h).
    if (hero.excel.kinship) {
        hero.pet.dealt = std::max(1.0, hero.pet.dealt);
        hero.pet.lifeCost = 0;
    }
    // And the wing, while it has life (gObjWingSprite), after Kinship: the ring lifts a pet's
    // price, not the wing's. Its option by its kind: the Elf's life regeneration beside the
    // rings', Heaven's wizardry, Satan's damage (docs/wings.md).
    hero.wingDamage = hero.wingWizardry = 0;
    if (const content::ItemRow* wing = rowAt(kWings);
        wing && anyWing(*wing) && kit[kWings].durability > 0) {
        const Held& worn = kit[kWings];
        const PetPower power = wingPower(*wing, worn.refinement);
        hero.pet.taken *= power.taken;
        hero.pet.dealt *= power.dealt;
        hero.pet.lifeCost += power.lifeCost;
        const int option = wingOptionValue(*wing, worn);
        switch (wingOption(*wing, worn.wing)) {
            case WingOption::Regeneration: hero.excel.lifeRegen += option; break;
            case WingOption::Wizardry: hero.wingWizardry = option; break;
            case WingOption::Damage: hero.wingDamage = option; break;
        }
        // A 2nd wing's extras (zzzitem.cpp:1488-1505, :3039-3044; ObjCalCharacter.cpp:1264-1265).
        if (secondWing(*wing)) {
            const int extra = kWingExtraBase + kWingExtraPerPlus * std::max<int>(0, worn.refinement);
            if (worn.wing & kWingMaxLife) hero.pet.health += extra;
            if (worn.wing & kWingMaxMana) hero.excel.moreMana += extra;
            if (worn.wing & kWingIgnoreDefense) hero.excel.ignoreDefense += kWingIgnoreChance;
        }
    }
    // The stat runes, in any socket of anything worn, summed (sim::statShareOf).
    hero.runeShare = HeroPoints{};
    for (int slot = kWeaponRight; slot <= kRingLeft; ++slot) {
        if (!rowAt(slot)) continue;
        for (int at = 0; at < std::min<int>(kit[slot].sockets, kMostSockets); ++at) {
            const PowerRow* power = powerOf(kit[slot].powers[at]);
            if (!power) continue;
            const HeroPoints share = statShareOf(power->power);
            hero.runeShare.strength += share.strength;
            hero.runeShare.agility += share.agility;
            hero.runeShare.vitality += share.vitality;
            hero.runeShare.energy += share.energy;
        }
    }
    const int was = hero.maxHealth;
    reckon(hero.kin, hero.level, hero.totalPoints(), armsOf(hero), &hero.stats, &hero.maxHealth);
    // **A guard stands only behind the shield that raised it.** Taking the shield off ends
    // Defense or Soul Barrier on the spot, aura and all (the user, 2026-09-28): the skill asks for
    // a shield to be cast, and a guard that outlived the shield would be the one way round that.
    // The cooldown is left running, so putting the shield back on is not a way to recast sooner.
    if (hero.boonUntil > tick_) {
        const SkillRow* boon = skillNumbered(hero.boonSkill);
        const content::Arm* shield = hero.shield >= 0 ? &tables_->arms[size_t(hero.shield)] : nullptr;
        if (boon != nullptr && (boon->families & arms::kShield) != 0 &&
            !boon->suits(armFamily(hero, shield))) {
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
    Wearer who{hero.kin,
                  hero.level,
                  hero.points,
                  hero.learned,
                  hero.shieldDefense,
                  hero.stats.wizardMinimum,
                  hero.stats.wizardMaximum,
                  hero.stats.wizardryRate,
                  hero.staffRise,
                  familyOf(armAt(hero.weapon)),
                  armFamily(hero, armAt(hero.shield))};
    who.second = hero.second;
    who.totals = hero.totalPoints();
    return who;
}

int Realm::give(int32_t item, int slot, int refinement, int durability, bool luck, int option,
                uint8_t excellent, uint8_t sockets, const uint8_t* powers,
                const uint8_t* affixes) {
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
    if (takesOptions(row) || jewellery(row)) {
        // Luck on a ring or a pendant too, since they refine: ours (2026-10-03); MU's
        // CItem::Convert reads none there.
        put.luck = luck;
        put.option = int8_t(std::clamp(option, 0, kMostOption));
        put.excellent = uint8_t(excellent & 63);
        put.sockets = uint8_t(std::min<int>(sockets, mostSocketsOf(row)));
        for (int i = 0; i < put.sockets && powers; ++i) put.powers[i] = powers[i];
        for (int i = 0; i < 3 && affixes && powered(row); ++i) put.affixes[i] = affixes[i];
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

// What a Town Portal does once it is spent, and what Icarus does to one who can no longer fly:
// he is taken out of everything he was doing and set down in the map's safe zone, or, on a map
// with none of its own, sent home (`Warped`'s c, the mode's to carry out).
void Realm::warpHome(Body& hero) {
    rise(hero);
    dropBlow(hero);
    order_ = Request{};
    pending_ = Request{};
    wants_ = skill::kNone;
    trading_ = -1;
    banking_ = -1;
    closeMachine();
    gating_ = -1;
    angeling_ = -1;
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
    // A map with no safe box of its own -- the Dungeon, which has no spawn gate -- sends him
    // to Lorencia's: OpenMU's SafezoneMapNumber falls back to Lorencia for a map with no
    // spawn gate (BaseMapInitializer.cs:91). He is not set down here; `c` says the map
    // change is owed, and it is the mode's, as a gate's is (Play::takeHome).
    // And Blood Castle's: a scroll read in a castle leaves it, for Devias.
    const int32_t* box = tables_->safeGate;
    const bool home = !(box[2] > box[0] && box[3] > box[1]) || tables_->map == kBloodCastleMap;
    say(What::Warped, hero, landing.first, landing.second, home ? 1 : 0);
}

bool Realm::moveItem(int from, int to) {
    if (!tables_ || !bodies_[0].alive()) return false;
    // In Icarus, nothing that would leave him unable to fly: the last wing or Dinorant off, or
    // the Horn of Uniria on. WebZen refuses taking off either without the other worn
    // (protocol.cpp:5356-5375, user.cpp:16879-16925), MuMain will not equip Uniria there
    // (NewUIMyInventory.cpp:351-354). Tried on a copy, so a refusal changes nothing.
    if (tables_->map == kIcarusMap) {
        Satchel trial = bag_;
        if (!move(*tables_, wearer(), trial, from, to) || !canFly(*tables_, trial)) return false;
    }
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
        // And its second's alone where it asks one: the Scroll of Nova is the Soul Master's.
        if (secondClassOnly(row) && !hero.second) return false;
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
    //
    // Refused and kept while he already stands in the safe zone -- ours, the user's (2026-09-30);
    // OpenMU would spend it and warp him to the gate across the square.
    if (portal(row)) {
        if (tables_->grid.safe(hero.column(), hero.row())) return false;
        spendOne();
        warpHome(hero);
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
    const int64_t steps[3] = {4, 12, 4};  // kPourTicks in all
    const int shares[3] = {20, 60, 20};
    int paid = 0;
    int64_t due = tick_;
    for (int i = 0; i < 3 && sipCount_ < 8; ++i) {
        due += steps[i];
        const int amount = i == 2 ? total - paid : total * shares[i] / 100;
        paid += amount;
        sips_[sipCount_++] = Sip{due, amount, mana, true};
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
    if (settable(*tables_, jewel, thing, hero.kin, hero.second)) {
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
    const Jewel kind = jewelOf(tables_->items[size_t(jewel.item)]);
    const bool soul = kind == Jewel::Soul;
    // The Life works the option and not the plus (kLifeChance), so nothing it does can make a
    // thing ask more than he has. Its own dice: the drop's, `dice_`, as the Soul's roll is.
    if (kind == Jewel::Life) {
        const int was = thing.option;
        const bool took = thing.luck || dice_.nextInt(0, 100) < kLifeChance;
        if (took) {
            if (was == 0 && secondWing(row)) {
                thing.wing = uint8_t(thing.wing & ~kWingOptionKind);
                if (dice_.nextInt(0, 2) == 1) thing.wing = uint8_t(thing.wing | kWingOptionKind);
            }
            thing.option = int8_t(was + 1);
        } else {
            thing.option = 0;
        }
        if (jewel.durability > 1) {
            Held left = jewel;
            left.durability = int16_t(jewel.durability - 1);
            bag_.put(jewelSlot, left);
        } else {
            bag_.lift(jewelSlot);
        }
        bag_.put(targetSlot, thing);
        if (wearable(targetSlot)) rearm(hero);
        say(What::Enlivened, hero, targetSlot, was, thing.option);
        return true;
    }

    const int was = thing.refinement;
    // A plus asks more (`asks`), so a worn thing can outgrow him: it comes off into the bag, as
    // `movable` would never have let him put it on. Ours, the user's -- MU leaves it worn and
    // red. With no room for it the jewel is refused before anything is rolled or spent. Room
    // is asked of the bag as it will be, the last jewel of a stack already gone from its cell.
    Held raised = thing;
    raised.refinement = int16_t(was + 1);
    const bool outgrows = wearable(targetSlot) && !fits(*tables_, wearer(), raised);
    if (outgrows) {
        Satchel after = bag_;
        if (jewel.durability <= 1) after.lift(jewelSlot);
        if (after.free(*tables_, row.width, row.height) < 0) {
            refusal_ = "no room in the bag for what he could no longer wear";
            return false;
        }
    }
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
    int landed = targetSlot;
    if (took && outgrows) {
        bag_.lift(targetSlot);
        landed = bag_.free(*tables_, row.width, row.height);
        bag_.put(landed, thing);
    }
    if (wearable(targetSlot)) rearm(hero);
    say(What::Refined, hero, landed, was, thing.refinement);
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
    // At rest, both pools at once (kRestShare).
    if (hero.alive() && hero.pose != Pose::Standing && tick_ % kRestEveryTicks == 0) {
        hero.health = std::min(hero.maxHealth,
                               hero.health + std::max(1, int(float(hero.maxHealth) * kRestShare)));
        hero.mana = std::min(hero.maxMana,
                             hero.mana + std::max(1, int(float(hero.maxMana) * kRestShare)));
    }
    // The rings' and the pendant's life regeneration: their options' percent of the pool every
    // seventh second, anywhere (gObjRestPotionFill off rest, user.cpp:23387-23520; whole points,
    // as there).
    if (hero.alive() && hero.excel.lifeRegen > 0 && tick_ % kJewelleryRegenTicks == 0 &&
        hero.health < hero.maxHealth) {
        const int back = std::max(1, hero.maxHealth * hero.excel.lifeRegen / 100);
        hero.health = std::min(hero.maxHealth, hero.health + back);
    }
    // The wing's wear by the hour, worn or not fighting (sim::kWingWearTicks). At nought it stays,
    // broken, and does nothing until it is mended.
    if (tick_ % kWingWearTicks == 0 && !bag_[kWings].empty() && bag_[kWings].durability > 0 &&
        size_t(bag_[kWings].item) < tables_->items.size() &&
        anyWing(tables_->items[size_t(bag_[kWings].item)])) {
        wearDown(kWings, 1.0 / kWingWearSteps);
    }
    // Health on the same three seconds, a hundredth of the pool, and only on a safe tile -- and
    // a Renewal rune's share anywhere, beside it (sim::kRenewalShare).
    if (hero.alive() && tick_ % kRecoverEveryTicks == 0) {
        const float share =
            (tables_->grid.safe(hero.column(), hero.row()) ? kHealthRecoveryInSafeZone : 0.0f) +
            float(hero.excel.renewal);
        if (hero.health >= hero.maxHealth || share <= 0.0f) {
            hero.healthCarry = 0.0f;
        } else {
            hero.healthCarry += float(hero.maxHealth) * share;
            const int whole = int(hero.healthCarry);
            hero.healthCarry -= float(whole);
            if (whole > 0 && hero.excel.renewal > 0.0) {
                core::logf("renewal rune: tick %lld, +%d health", (long long)tick_, whole);
            }
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
// afford; failing those, Zen worth the kill's experience plus seven, at the breed's rate.
//
// No skill roll on a dropped piece. Its plus is `(monster level - drop level) / 3`, held to
// the cap of nine and to the breed's MaxItemLevel.
void Realm::leave(const Body& dead, const Body& killer) {
    // What a kill leaves: a jewel, then an item, then Zen, then nothing.
    //
    // **The item chance is ours, not MU2's 0.3.** A nest of spiders dropped a weapon or a piece
    // of armour on nearly every third kill, and the ground round a hunt was paved with them;
    // this is the rate the hunt was asked to have (2026-09-22). Zen and the jewel are MU2's
    // Loot untouched, so the commonest thing a monster leaves is still coins -- the change is
    // that three kills in four now leave coins or nothing.
    //
    // The rest is WebZen's (1.00.93): what drops is anything the monster's level reaches from
    // fifteen below it, inclusive (MAX_MONSTER_ITEM_DROP_RANGE, zzzitem.cpp:5960 -- 18 only
    // from a 2005 flag; OpenMU's Loot.Gap was twelve, exclusive), a potion from eight below
    // (:5920); its plus `(level - drop level) / 3`, and a row whose plus would pass the breed's
    // MaxItemLevel is not in the pool (MonsterItemMng.cpp:626). Zen is the breed's MoneyRate
    // (kDropRates) where MU2's Loot had a flat half.
    constexpr double kJewel = kJewelGroupChance;
    // The Ring of Fortune's and its kind's item find raise both item rolls (sim::Affix::Fortune),
    // taking from the Zen and the nothing below them.
    const double find = 1.0 + double(bodies_[0].excel.itemFind) / 100.0;
    const double itemChance = 0.1 * find;
    const double excellentItemChance = 0.1 * kExcellentShareOfItem * find;
    constexpr int kGap = 15;
    constexpr int kPotionGap = 8;
    constexpr int kBaseMoney = 7;      // Loot.BaseMoney
    constexpr int kMostRefined = kRefineCap;
    const int level = dead.level;
    const DropRate rate = dropRateOf(
        dead.kind >= 0 && size_t(dead.kind) < tables_->kinds.size()
            ? tables_->kinds[size_t(dead.kind)].number
            : -1);
    // Sevina's treasure, while it is sought (realm_quests.cpp): beside whatever else the kill leaves.
    treasure(dead);
    // Blood Castle's scroll, then its bone, anywhere but a castle (sim/items.h): each its own
    // roll off its own dice, and one that lands is the kill's whole drop, as WebZen's `return
    // TRUE` -- the coins and the item rolls below are not made.
    if (!bloodCastleMap(tables_->map)) {
        const bool scroll = ticketDice_.nextInt(0, 10000) < kScrollOfArchangelIn10000;
        const bool bone = !scroll && ticketDice_.nextInt(0, 10000) < kBloodBoneIn10000;
        if (scroll || bone) {
            const int32_t item = tables_->itemAt(kGroupPets, scroll ? 16 : 17);
            if (item >= 0) {
                Lying material;
                material.what = Held{item, int16_t(castleMaterialLevel(level)), 128};
                std::tie(material.column, material.row) = clearing(dead.column(), dead.row());
                material.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
                material.id = nextId_++;
                lying_.push_back(material);
                say(What::Dropped, dead, int32_t(material.id), item, material.what.refinement);
                return;
            }
        }
    }
    double roll = dice_.nextDouble();
    // A dungeon's Firecracker, first, so it takes the body's own tile and the kill's drop the
    // next clear one: its own roll off its own dice, and beside whatever else the kill leaves.
    if (level >= kFirecrackerFromLevel && firecrackerMap(tables_->map) &&
        crackerDice_.nextInt(0, kFirecrackerOdds) == 0) {
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            if (!firecracker(tables_->items[i])) continue;
            Lying cracker;
            cracker.what = Held{int32_t(i), 0, 1};
            std::tie(cracker.column, cracker.row) = clearing(dead.column(), dead.row());
            cracker.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
            cracker.id = nextId_++;
            lying_.push_back(cracker);
            say(What::Dropped, dead, int32_t(cracker.id), int32_t(i), 0);
            break;
        }
    }
    // Loch's Feather in Atlans, the Lost Tower, Tarkan and Icarus, the same way (sim::kFeatherOdds).
    if (level >= kFeatherFromLevel && featherMap(tables_->map) &&
        featherDice_.nextInt(0, kFeatherOdds) == 0) {
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            if (!lochsFeather(tables_->items[i])) continue;
            Lying feather;
            feather.what = Held{int32_t(i), 0, 1};
            std::tie(feather.column, feather.row) = clearing(dead.column(), dead.row());
            feather.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
            feather.id = nextId_++;
            lying_.push_back(feather);
            say(What::Dropped, dead, int32_t(feather.id), int32_t(i), 0);
            break;
        }
    }
    // The Scroll of Nova in the same two, the same way (sim::kNovaScrollOdds).
    if (level >= kNovaScrollFromLevel && featherMap(tables_->map) &&
        novaScrollDice_.nextInt(0, kNovaScrollOdds) == 0) {
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            if (!scrollOfNova(tables_->items[i])) continue;
            Lying scroll;
            scroll.what = Held{int32_t(i), 0, 1};
            std::tie(scroll.column, scroll.row) = clearing(dead.column(), dead.row());
            scroll.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
            scroll.id = nextId_++;
            lying_.push_back(scroll);
            say(What::Dropped, dead, int32_t(scroll.id), int32_t(i), 0);
            break;
        }
    }
    // The first wings in Icarus, the hero's class's, the same way (sim::kIcarusWingOdds).
    if (tables_->map == kIcarusMap && (wingDice_.nextInt(0, kIcarusWingOdds) == 0 || wingDemo_)) {
        const int32_t item = tables_->itemAt(12, firstWingOf(bodies_[0].kin));
        if (item >= 0) {
            const content::ItemRow& row = tables_->items[size_t(item)];
            Lying wing;
            wing.what = Held{item, 0, 0};
            wing.what.durability = int16_t(maximumDurability(row, wing.what));
            wing.what.luck = wingDice_.nextInt(0, 100) < kLuckIn100;
            std::tie(wing.column, wing.row) = clearing(dead.column(), dead.row());
            wing.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
            wing.id = nextId_++;
            lying_.push_back(wing);
            say(What::Dropped, dead, int32_t(wing.id), item, 0);
        }
    }
    // The second class's gear from Atlans's strongest, the same way (sim::kAtlansGearOdds).
    if (tables_->map == kAtlansMap && level >= kAtlansGearFromLevel &&
        gearDice_.nextInt(0, kAtlansGearOdds) == 0) {
        int count = 0;
        for (const content::ItemRow& r : tables_->items) {
            count += r.dropsFromMonsters() && secondClassOnly(r) ? 1 : 0;
        }
        int pick = count > 0 ? gearDice_.nextInt(0, count) : -1;
        for (size_t i = 0; pick >= 0 && i < tables_->items.size(); ++i) {
            const content::ItemRow& r = tables_->items[i];
            if (!r.dropsFromMonsters() || !secondClassOnly(r) || pick-- != 0) continue;
            Lying gear;
            gear.what = Held{int32_t(i), 0, int16_t(fullDurability(r, 0))};
            gear.what.luck = gearDice_.nextInt(0, 100) < kLuckIn100;
            const int under = gearDice_.nextInt(0, 100);
            const int which = gearDice_.nextInt(0, 3);
            if (under < kOptionUnder[which]) gear.what.option = int8_t(kMostOptionDropped - which);
            std::tie(gear.column, gear.row) = clearing(dead.column(), dead.row());
            gear.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
            gear.id = nextId_++;
            lying_.push_back(gear);
            say(What::Dropped, dead, int32_t(gear.id), int32_t(i), 0);
            break;
        }
    }
    // The Orb of Summoning, its own roll off its own dice and beside the rest (sim/items.h).
    if (orbDice_.nextInt(0, kSummonOrbOdds) == 0) {
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            const content::ItemRow& row = tables_->items[i];
            if (!summoningOrb(row) || row.dropLevel > level) continue;
            const int plus = std::min((level - row.dropLevel) / kSummonOrbLevelsAPlus,
                                      kSummonOrbMostPlus);
            Lying orb;
            orb.what = Held{int32_t(i), int16_t(plus), 1};
            std::tie(orb.column, orb.row) = clearing(dead.column(), dead.row());
            orb.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
            orb.id = nextId_++;
            lying_.push_back(orb);
            say(What::Dropped, dead, int32_t(orb.id), int32_t(i), orb.what.refinement);
            break;
        }
    }
    Lying one;
    std::tie(one.column, one.row) = clearing(dead.column(), dead.row());
    one.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;

    // The second class's gear only where it falls (sim::kTarkanMap): Tarkan, Icarus (ours, its
    // port's decision 9: its south reached three drop rows or none) and Blood Castle 6.
    const bool secondGearHere = tables_->map == kTarkanMap || tables_->map == kIcarusMap ||
                                (tables_->map == kBloodCastleMap && run_.castle >= kCastles);
    const auto reaches = [level, secondGearHere](const content::ItemRow& row) {
        return row.dropLevel <= level &&
               (row.maximumDropLevel == 0 || level <= row.maximumDropLevel) &&
               (secondGearHere || !secondClassOnly(row));
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
    // A powered ring's or pendant's further powers (sim::Affix): how many first, at
    // kAffixCountShare among the counts this kill's level reaches (kAffixCountLevel), then that
    // many of the four its signature leaves, none twice. Off the sockets' dice. ours.
    const auto rollAffixes = [&](Held& what, const content::ItemRow& row) {
        const Affix own = signatureOf(row);
        if (own == Affix::None) return;
        double held = 0.0;
        for (int c = 0; c < 4; ++c) held += level >= kAffixCountLevel[c] ? kAffixCountShare[c] : 0.0;
        double pick = runeDice_.nextDouble() * held;
        int count = 1;
        for (int c = 0; c < 4; ++c) {
            if (level < kAffixCountLevel[c]) continue;
            count = c + 1;
            if (pick < kAffixCountShare[c]) break;
            pick -= kAffixCountShare[c];
        }
        uint8_t left[kAffixes - 1];
        int n = 0;
        for (int a = 1; a <= kAffixes; ++a) {
            if (Affix(a) != own) left[n++] = uint8_t(a);
        }
        for (int i = 0; i < count - 1; ++i) {
            const int at = runeDice_.nextInt(0, n);
            what.affixes[i] = left[at];
            left[at] = left[--n];
        }
    };
    // The option: one of three draws, each a third, under 4, 8 or 12 in 100 for +12, +8, +4.
    const auto rollOptions = [&](Held& what, int luckIn100) {
        what.luck = dice_.nextInt(0, 100) < luckIn100;
        const int under = dice_.nextInt(0, 100);
        const int which = dice_.nextInt(0, 3);
        if (under < kOptionUnder[which]) what.option = int8_t(kMostOptionDropped - which);
    };

    // The jewels group is not only the jewels: `AddItemToJewelItemDrop` puts the Ale (drop level
    // 15) and the Town Portal Scroll (30) in it too -- **the scroll left out, ours** (the user,
    // 2026-10-04: "there is no point of town portal") -- and the three pets -- the Guardian Angel
    // (23) and the Imp (28) have rows, carrying the flag; the Horn of Uniria has none. Drawn by the monster's level alone, with no twelve-level gap (GenerateItemFromGroup's
    // `isJewel`). In Lorencia that is the Chaos from level 12 and the Ale from 15; the Bless (25)
    // and the Soul (30) are never left by anything in the town.
    // The Bless, the Soul and the Chaos have their own roll ahead of it (kJewelChance), and the
    // Rune of Creation one ahead of that, so the group's own draw leaves them out.
    const auto jewelGroup = [](const content::ItemRow& r) {
        return (r.jewel() || (r.group == kGroupPotions && r.number == 9)) &&
               !refiningJewel(r);
    };
    // Below kCreationLevel too since the Common runes (2026-10-06): such a rune is a Common.
    const double creationChance = level >= kRuneRarityLevel[int(Rarity::Common)] ? kCreationChance : 0.0;
    if (roll < creationChance) {
        const int32_t item = draw([](const content::ItemRow& r) { return creation(r); });
        if (item < 0) return;
        // A power the killer's class may set (drawRunePower).
        one.what = Held{item, 0, 1};
        one.what.powers[0] = drawRunePower(dice_, killer.kin, killer.second, level);
    } else if ((roll -= creationChance) < kJewelChance) {
        const int32_t item =
            draw([&](const content::ItemRow& r) { return refiningJewel(r) && reaches(r); });
        if (item < 0) return;
        one.what = Held{item, 0, 1};
    } else if ((roll -= kJewelChance) <= kJewel) {
        const int32_t item = draw([&](const content::ItemRow& r) { return jewelGroup(r) && reaches(r); });
        if (item < 0) return;
        one.what = Held{item, 0, 1};
    } else if ((roll -= kJewel) <= excellentItemChance) {
        // Nothing from a monster under 25, and otherwise what a monster 25 levels lower would
        // drop, at +0, luck at 1 in 100, the option as on any drop, and NewOptionRand's options.
        // Only a row that can carry them is drawn.
        const int lower = level - kExcellentLevelDelta;
        if (lower < 0) return;
        const int32_t item = draw([&](const content::ItemRow& r) {
            return r.dropsFromMonsters() && excellentable(r) && r.dropLevel <= lower &&
                   (r.maximumDropLevel == 0 || lower <= r.maximumDropLevel) &&
                   r.dropLevel >= lower - kGap && (secondGearHere || !secondClassOnly(r));
        });
        if (item < 0) return;
        const content::ItemRow& row = tables_->items[size_t(item)];
        one.what = Held{item, 0, int16_t(fullDurability(row, 0))};
        rollOptions(one.what, kExcellentLuckIn100);
        rollAffixes(one.what, row);
        int first = dice_.nextInt(0, kExcellentOptions);
        if (first == 1 && dice_.nextInt(0, 2) != 0) first = dice_.nextInt(0, kExcellentOptions);
        one.what.excellent = uint8_t(1u << first);
        if (dice_.nextInt(0, 4) == 0) {
            one.what.excellent |= uint8_t(1u << dice_.nextInt(0, kExcellentOptions));
        }
        one.what.durability = int16_t(maximumDurability(row, one.what));
    } else if ((roll -= excellentItemChance) <= itemChance) {
        // Prize.TakesRefinement: the weapon and armour groups, ammunition taken back out.
        // A ring's and a pendant's the same way, to +4 (GetLevelItem, docs/jewellery.md).
        const auto plusOf = [level, kMostRefined](const content::ItemRow& r) {
            const bool refinable = r.group <= kGroupBoots && !ammunition(r);
            if (jewellery(r)) return std::clamp((level - r.dropLevel) / 3, 0, kJewelleryMostPlus);
            return refinable ? std::clamp((level - r.dropLevel) / 3, 0, kMostRefined) : 0;
        };
        // A powered ring or pendant past its fifteen levels is still drawn, on kDeepJewellery of
        // the item drops, or its purple and legendary (kAffixCountLevel) could never fall from
        // the kills deep enough to roll them -- and on that share only, or the few rows a deep
        // pool holds would make every other item a ring. ours. Drawn only where it matters.
        const bool deep = level - kGap > kDeepJewelleryFrom && runeDice_.nextBool(kDeepJewellery);
        const int32_t item = draw([&](const content::ItemRow& r) {
            const int gap = r.group == kGroupPotions ? kPotionGap : kGap;
            return r.dropsFromMonsters() && !summoningOrb(r) && reaches(r) &&
                   (r.dropLevel >= level - gap || (deep && powered(r))) &&
                   plusOf(r) <= rate.maxPlus;
        });
        if (item < 0) return;
        const content::ItemRow& row = tables_->items[size_t(item)];
        const int plus = plusOf(row);
        const bool stacks = heals(row) || restores(row);
        // Whole at its plus, as DefaultDropGenerator sets `GetMaximumDurabilityOfOnePiece`.
        one.what = Held{item, int16_t(plus), int16_t(stacks ? 1 : fullDurability(row, plus))};
        // Luck and the option (items.h). No skill: skills are orbs here. A ring or a pendant
        // takes the option, its life regeneration, and luck since it refines (ours, 2026-10-03).
        if (jewellery(row)) {
            rollOptions(one.what, kLuckIn100);
            rollAffixes(one.what, row);
        }
        if (takesOptions(row)) rollOptions(one.what, kLuckIn100);
        // Sockets: rare, and each further one rarer, a ring's and a pendant's too. invention.
        if (takesSockets(row) && runeDice_.nextBool(kSocketChance)) {
            one.what.sockets = 1;
            while (one.what.sockets < mostSocketsOf(row) && runeDice_.nextBool(kMoreSocketChance)) {
                ++one.what.sockets;
            }
        }
    } else if (dice_.nextInt(0, rate.moneyRate) < 10) {
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
    if (!one.what.empty()) questFound(one.what.item);
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
    // A quest item is the Archangel's, not a merchant's (sim::divineStaff).
    if (archangelWeapon(row) || classTreasure(row)) return -1;
    const bool stacks = row.group == kGroupPotions;
    int64_t paid = sellingPrice(row, thing.refinement,
                                stacks ? std::max<int>(1, thing.durability) : 1, thing.skill,
                                thing.durability, row.durability, thing.luck, thing.option,
                                excellentCount(thing.excellent));
    // Worn gear fetches less: 0.6 of the share gone comes off (CalculateSellingPrice).
    if (wears(row)) {
        paid = wornSellingPrice(paid, thing.durability, maximumDurability(row, thing));
    }
    // A Common rune is a sliver of the Jewel of Creation's price (sim/items.h kCommonRuneSale).
    if (const PowerRow* power = powerOf(thing.powers[0]);
        creation(row) && power != nullptr && power->rarity == Rarity::Common) {
        paid = int64_t(double(paid) * kCommonRuneSale);
    }
    return paid > 0 ? paid : -1;
}

int64_t Realm::sellItem(int slot) {
    if (trading_ < 0 || !serving(trading_) || !baggable(slot) || bag_[slot].empty()) return -1;
    // A quest item is not sold (sim::divineStaff).
    const content::ItemRow& selling = tables_->items[size_t(bag_[slot].item)];
    if (archangelWeapon(selling) || classTreasure(selling)) return -1;
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
    if (count > 0) {
        const int slot = candidates[wearDice_.nextInt(0, count)];
        wearDown(slot, double(took) / kDamagePerDurability);
    }
    // And the pet, on every hit taken, as long as it has life. At WebZen's rate, not OpenMU's
    // armour rate (Player.cs:1988, 60 to 100 times slower): gObjSpriteDamage (1.00.93 user.cpp)
    // takes the Angel down by 3/100 of the damage and the Imp by 2/100, the Angel's on the
    // damage BEFORE its own cut, since ObjAttack.cpp calls it ahead of gObjAngelSprite. Landing
    // a blow wears it not at all. At nought it is destroyed rather than left broken
    // (Player.cs:1991-2001), and its powers go with it.
    // The mount in its own slot wears the same way, off the same uncut damage.
    const double uncut = double(took) / bodies_[0].pet.taken;
    bool lostOne = false;
    for (int slot : {kPet, kMount}) {
        const Held& pet = bag_[slot];
        if (pet.empty() || pet.durability <= 0) continue;
        const PetPower power = petPower(tables_->items[size_t(pet.item)]);
        wearDown(slot, uncut * power.wear);
        if (bag_[slot].durability > 0) continue;
        const int32_t lost = bag_[slot].item;
        bag_.lift(slot);
        say(What::PetLost, bodies_[0], lost);
        lostOne = true;
    }
    if (lostOne) rearm(bodies_[0]);
}

void Realm::wearOnLanded(int defense) {
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
    // And once a swing, not once a body (sim::kWeaponWearTicks).
    if (tick_ - weaponWornAt_ < kWeaponWearTicks) return;
    weaponWornAt_ = tick_;
    wearDown(result, weaponWear(tables_->items[size_t(bag_[result].item)], bag_[result], defense));
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
    if (!banked() || bagSlot < 0 || bagSlot >= kSlots || bag_[bagSlot].empty()) return -1;
    const content::ItemRow& row = tables_->items[size_t(bag_[bagSlot].item)];
    const bool worn = wearable(bagSlot);
    if (cell < 0) {
        cell = pour(*tables_, vault_, 0, kVaultCells, bag_[bagSlot]);
        if (cell >= 0) bag_.lift(bagSlot);
        if (cell >= 0 && worn) rearm(bodies_[0]);
        return cell;
    }
    const int onto = vault_.holder(*tables_, cell);
    if (const int went = onto >= 0 ? topUp(*tables_, vault_, onto, bag_[bagSlot]) : 0) {
        unstack(bag_, bagSlot, went);
        return onto;
    }
    // A jewel let go on a thing it works: applied, not stored.
    if (onto >= 0 && worksOn(bag_[bagSlot], vault_[onto])) {
        return refineAcross(Store::Bag, bagSlot, Store::Vault, onto) ? onto : -1;
    }
    if (!vault_.room(*tables_, cell, row.width, row.height)) return -1;
    vault_.put(cell, bag_.lift(bagSlot));
    if (worn) rearm(bodies_[0]);
    return cell;
}

int Realm::withdraw(int cell, int bagSlot) {
    if (!banked() || cell < 0 || cell >= kVaultCells || vault_[cell].empty()) return -1;
    const content::ItemRow& row = tables_->items[size_t(vault_[cell].item)];
    // Onto what he wears: a jewel onto it, or the thing put on.
    if (bagSlot >= 0 && wearable(bagSlot)) {
        if (worksOn(vault_[cell], bag_[bagSlot])) {
            return refineAcross(Store::Vault, cell, Store::Bag, bagSlot) ? bagSlot : -1;
        }
        return wearFromVault(cell, bagSlot);
    }
    if (bagSlot >= 0 && baggable(bagSlot)) {
        const int under = bag_.holder(*tables_, bagSlot);
        // A jewel never works on a jewel, so this never takes a stack's top-up from it.
        if (under >= 0 && worksOn(vault_[cell], bag_[under])) {
            return refineAcross(Store::Vault, cell, Store::Bag, under) ? under : -1;
        }
    }
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

// Put on from the vault: `move`'s gates on a trial bag holding nothing carried but the thing,
// then what that took off goes back to the vault -- into this cell first -- or the bag.
int Realm::wearFromVault(int cell, int worn) {
    Satchel trial = bag_;
    for (int s = kWorn; s < kSlots; ++s) {
        if (!trial[s].empty()) trial.lift(s);
    }
    trial.put(kWorn, vault_[cell]);
    if (!move(*tables_, wearer(), trial, kWorn, worn)) return -1;
    const Vault vaultWas = vault_;
    const Satchel bagWas = bag_;
    vault_.lift(cell);
    bool first = true;
    for (int s = kWorn; s < kSlots; ++s) {
        if (trial[s].empty()) continue;
        const Held off = trial[s];
        const content::ItemRow& row = tables_->items[size_t(off.item)];
        int into = first && vault_.room(*tables_, cell, row.width, row.height)
                       ? cell
                       : vault_.free(*tables_, row.width, row.height);
        first = false;
        if (into >= 0) {
            vault_.put(into, off);
            continue;
        }
        into = bag_.free(*tables_, row.width, row.height);
        if (into < 0) {
            vault_ = vaultWas;
            bag_ = bagWas;
            refusal_ = "no room for what it would take off";
            return -1;
        }
        bag_.put(into, off);
    }
    for (int s = 0; s < kWorn; ++s) {
        if (bag_[s].empty() && trial[s].empty()) continue;
        if (trial[s].empty()) bag_.lift(s);
        else bag_.put(s, trial[s]);
    }
    rearm(bodies_[0]);
    return worn;
}

bool Realm::worksOn(const Held& jewel, const Held& thing) const {
    if (!tables_ || jewel.empty() || thing.empty()) return false;
    const Body& hero = bodies_[0];
    return settable(*tables_, jewel, thing, hero.kin, hero.second) ||
           refinable(*tables_, jewel, thing);
}

bool Realm::refineAcross(Store jewelIn, int jewelAt, Store thingIn, int thingAt) {
    if (!tables_) return false;
    if (jewelIn == Store::Bag && thingIn == Store::Bag) return refine(jewelAt, thingAt);
    const auto open = [&](Store s, int at) {
        switch (s) {
            case Store::Bag: return at >= 0 && at < kSlots;
            case Store::Vault: return banked() && at >= 0 && at < kVaultCells;
            case Store::Machine: return atMachine() && !mixed_ && at >= 0 && at < kMachineCells;
        }
        return false;
    };
    if (!open(jewelIn, jewelAt) || !open(thingIn, thingAt)) return false;
    const auto held = [&](Store s, int at) -> Held {
        return s == Store::Bag ? bag_[at] : s == Store::Vault ? vault_[at] : machine_[at];
    };
    const auto keep = [&](Store s, int at, const Held& what) {
        if (s == Store::Bag) {
            if (what.empty()) bag_.lift(at);
            else bag_.put(at, what);
        } else if (s == Store::Vault) {
            if (what.empty()) vault_.lift(at);
            else vault_.put(at, what);
        } else {
            if (what.empty()) machine_.lift(at);
            else machine_.put(at, what);
        }
    };
    const Held jewel = held(jewelIn, jewelAt);
    const Held thing = held(thingIn, thingAt);
    if (!worksOn(jewel, thing)) return false;
    const content::ItemRow& thingRow = tables_->items[size_t(thing.item)];
    const content::ItemRow& jewelRow = tables_->items[size_t(jewel.item)];
    const bool wornThing = thingIn == Store::Bag && wearable(thingAt);
    // A worn thing a plus could outgrow comes off into the bag: room for it there, asked first.
    if (wornThing && bag_.free(*tables_, thingRow.width, thingRow.height) < 0) {
        refusal_ = "no room in the bag for what he could no longer wear";
        return false;
    }
    // The stage: what he wears, the jewel and the thing, and nothing else carried.
    Satchel stage = bag_;
    for (int s = kWorn; s < kSlots; ++s) {
        if (!stage[s].empty()) stage.lift(s);
    }
    int j = jewelAt, t = thingAt;
    if (jewelIn == Store::Bag) stage.put(j, jewel);
    if (thingIn == Store::Bag && !wornThing) stage.put(t, thing);
    if (thingIn != Store::Bag) {
        t = stage.free(*tables_, thingRow.width, thingRow.height);
        if (t < 0) return false;
        stage.put(t, thing);
    }
    if (jewelIn != Store::Bag) {
        j = stage.free(*tables_, jewelRow.width, jewelRow.height);
        if (j < 0) return false;
        stage.put(j, jewel);
    }
    const Satchel real = bag_;
    bag_ = stage;
    const bool done = refine(j, t);
    const Satchel after = bag_;
    bag_ = real;
    if (!done) return false;
    // Back where each came from: the jewel's remainder, then the thing.
    keep(jewelIn, jewelAt, after[j]);
    if (wornThing && after[t].empty()) {
        // Outgrown: off him and into the bag (refine's own rule), wherever it landed on the stage.
        for (int s = kWorn; s < kSlots; ++s) {
            if (s == j || after[s].empty()) continue;
            const int into = bag_.free(*tables_, thingRow.width, thingRow.height);
            bag_.lift(t);
            bag_.put(into, after[s]);
            break;
        }
    } else {
        keep(thingIn, thingAt, after[t]);
    }
    if (wornThing) rearm(bodies_[0]);
    jeweled_ = true;
    return true;
}

bool Realm::rearrange(int from, int to) {
    if (!banked() || from == to || vault_[from].empty()) return false;
    const int onto = vault_.holder(*tables_, to);
    if (onto >= 0 && onto != from && worksOn(vault_[from], vault_[onto])) {
        return refineAcross(Store::Vault, from, Store::Vault, onto);
    }
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

bool Realm::cracks(int slot) const {
    if (!tables_ || slot < 0 || slot >= kSlots || bag_[slot].empty()) return false;
    const Held& held = bag_[slot];
    if (size_t(held.item) >= tables_->items.size()) return false;
    const content::ItemRow& row = tables_->items[size_t(held.item)];
    return firecracker(row) || boxOfLuck(row) || boxOfKundun(row);
}

// WebZen's FireCrackerOpenEven (Event.cpp:1201-1310 and the Zen at 1520), its rolls in its own
// order: whether an item comes, which row, the level, the skill, the luck, then the option.
Cracked Realm::crack(int slot) {
    Cracked cracked;
    if (!cracks(slot)) return cracked;
    Body& hero = bodies_[0];
    // A dead man throws nothing, as `discard`.
    if (!hero.alive()) return cracked;
    // One off the top of a stack (kFirecrackerStackMost); the last takes the cell with it.
    Held rest = bag_[slot];
    if (rest.durability > 1) {
        rest.durability = int16_t(rest.durability - 1);
        bag_.put(slot, rest);
    } else {
        bag_.lift(slot);
    }
    cracked.opened = true;
    cracked.column = hero.column();
    cracked.row = hero.row();
    // A box opens its own way (Realm::openBox).
    const content::ItemRow& thrown = tables_->items[size_t(rest.item)];
    if (boxOfLuck(thrown) || boxOfKundun(thrown)) {
        return openBox(cracked, boxOfLuck(thrown), std::clamp<int>(rest.refinement, 1, kKundunTiers));
    }

    // The bag's rows this tree has items for, counted then drawn by index as `leave` draws.
    const auto rowOf = [this](const BagRow& wanted) -> int32_t {
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            const content::ItemRow& row = tables_->items[i];
            if (row.group == wanted.group && row.number == wanted.number) return int32_t(i);
        }
        return -1;
    };
    int present = 0;
    for (const BagRow& wanted : kFirecrackerBag) present += rowOf(wanted) >= 0 ? 1 : 0;

    if (present > 0 && dice_.nextInt(0, 10) < kFirecrackerItemIn10) {
        int pick = dice_.nextInt(0, present);
        int32_t item = -1;
        for (const BagRow& wanted : kFirecrackerBag) {
            const int32_t found = rowOf(wanted);
            if (found >= 0 && pick-- == 0) item = found;
        }
        const content::ItemRow& row = tables_->items[size_t(item)];
        Lying one;
        // `level = GetLevel + rand()%5`, Option1 the skill and Option2 the luck at a half each,
        // and the option where either is missing: one in five +12, else +0, +4 or +8. The skill
        // is rolled and not given -- skills are orbs here (`leave`) -- so the option comes as
        // often as MU's does.
        const int plus = kFirecrackerPlus + dice_.nextInt(0, kFirecrackerPluses);
        const bool skill = dice_.nextInt(0, 2) == 1;
        const bool luck = dice_.nextInt(0, 2) == 1;
        int option = 0;
        if (!luck || !skill) option = dice_.nextInt(0, 5) < 1 ? 3 : dice_.nextInt(0, 3);
        if (takesOptions(row)) {
            const int refinement = std::min(plus, kRefineCap);
            one.what = Held{item, int16_t(refinement), int16_t(fullDurability(row, refinement))};
            one.what.luck = luck;
            one.what.option = int8_t(option);
        } else {
            // "혼석, 축석, 영석은 레벨이 없게": the Chaos, the Bless and the Soul come bare.
            one.what = Held{item, 0, 1};
        }
        std::tie(one.column, one.row) = clearing(hero.column(), hero.row());
        one.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
        one.id = nextId_++;
        lying_.push_back(one);
        cracked.id = one.id;
        cracked.item = item;
        cracked.column = one.column;
        cracked.row = one.row;
        say(What::Cracked, hero, int32_t(one.id), item, one.what.refinement);
        return cracked;
    }
    // Zen into the purse at the excellent armour's rate, as a kill's Zen (`leave`).
    // In tens, so the lane does not read as a stray number.
    const int64_t rolled =
        kFirecrackerZenLeast +
        int64_t(dice_.nextInt(0, int((kFirecrackerZenMost - kFirecrackerZenLeast) / 10) + 1)) * 10;
    cracked.zen = int64_t(double(rolled) * hero.excel.zenRate);
    money_ += cracked.zen;
    say(What::Cracked, hero, -1, -1, int32_t(cracked.zen));
    return cracked;
}

// WebZen's EledoradoBoxOpenEven (Event.cpp:603, docs/kundun-box-sources.md 2.2) for a Box of
// Kundun, and OpenMU's Version095d Box of Luck, in their own order: whether an item comes, then
// for the Kundun box whether it is excellent, which row, its level, the skill, the luck and the
// option; else Zen into the purse.
bool Realm::excellentOf(const BagRow* rows, size_t count, Random& dice, Held* out) {
    // Only a row that can carry the options: the pool's rings and pendants are left out here.
    const auto rowOf = [this](const BagRow& wanted) -> int32_t {
        for (size_t i = 0; i < tables_->items.size(); ++i) {
            const content::ItemRow& row = tables_->items[i];
            if (row.group == wanted.group && row.number == wanted.number && excellentable(row) &&
                takesOptions(row)) {
                return int32_t(i);
            }
        }
        return -1;
    };
    int present = 0;
    for (size_t i = 0; i < count; ++i) present += rowOf(rows[i]) >= 0 ? 1 : 0;
    if (present == 0) return false;
    int at = dice.nextInt(0, present);
    int32_t item = -1;
    for (size_t i = 0; i < count && item < 0; ++i) {
        const int32_t found = rowOf(rows[i]);
        if (found >= 0 && at-- == 0) item = found;
    }
    const content::ItemRow& row = tables_->items[size_t(item)];
    Held what{item, 0, int16_t(fullDurability(row, 0))};
    // The skill always; the luck at a half, and the option where it missed: one in five +12,
    // else +0, +4 or +8.
    what.luck = dice.nextInt(0, 100) < kBoxLuckIn100;
    if (!what.luck) what.option = int8_t(dice.nextInt(0, 5) < 1 ? 3 : dice.nextInt(0, 3));
    // NewOptionRand(0), as `leave` rolls an excellent drop's.
    int first = dice.nextInt(0, kExcellentOptions);
    if (first == 1 && dice.nextInt(0, 2) != 0) first = dice.nextInt(0, kExcellentOptions);
    what.excellent = uint8_t(1u << first);
    if (dice.nextInt(0, 4) == 0) what.excellent |= uint8_t(1u << dice.nextInt(0, kExcellentOptions));
    what.durability = int16_t(maximumDurability(row, what));
    *out = what;
    return true;
}

Cracked Realm::openBox(Cracked cracked, bool luck, int tier) {
    Body& hero = bodies_[0];
    const int t = std::clamp(tier, 1, kKundunTiers) - 1;
    // A pool's rows this tree has items for, counted then drawn by index as `crack` draws.
    const auto pick = [this](const BagRow* rows, size_t count) -> int32_t {
        const auto rowOf = [this](const BagRow& wanted) -> int32_t {
            for (size_t i = 0; i < tables_->items.size(); ++i) {
                const content::ItemRow& row = tables_->items[i];
                if (row.group == wanted.group && row.number == wanted.number) return int32_t(i);
            }
            return -1;
        };
        int present = 0;
        for (size_t i = 0; i < count; ++i) present += rowOf(rows[i]) >= 0 ? 1 : 0;
        if (present == 0) return -1;
        int at = dice_.nextInt(0, present);
        for (size_t i = 0; i < count; ++i) {
            const int32_t found = rowOf(rows[i]);
            if (found >= 0 && at-- == 0) return found;
        }
        return -1;
    };
    if (dice_.nextInt(0, 100) < (luck ? kLuckItemIn100 : kKundunItemIn100[t])) {
        int32_t item = -1;
        if (luck) {
            item = pick(kLuckBag, std::size(kLuckBag));
        } else {
            const BagRow* plain = t == 0 ? kKundunPlain1 : t == 1 ? kKundunPlain2 : kKundunPlain3;
            const size_t plains = t == 0 ? std::size(kKundunPlain1)
                                  : t == 1 ? std::size(kKundunPlain2) : std::size(kKundunPlain3);
            const BagRow* fine = t == 0 ? kKundunExcellent1 : t == 1 ? kKundunExcellent2 : kKundunExcellent3;
            const size_t fines = t == 0 ? std::size(kKundunExcellent1)
                                 : t == 1 ? std::size(kKundunExcellent2) : std::size(kKundunExcellent3);
            if (dice_.nextInt(0, 100) < kKundunExcellentIn100[t]) {
                // An excellent one, at its feet as any (Realm::excellentOf).
                Lying one;
                if (excellentOf(fine, fines, dice_, &one.what)) {
                    std::tie(one.column, one.row) = clearing(hero.column(), hero.row());
                    one.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
                    one.id = nextId_++;
                    lying_.push_back(one);
                    cracked.id = one.id;
                    cracked.item = one.what.item;
                    cracked.column = one.column;
                    cracked.row = one.row;
                    say(What::Cracked, hero, int32_t(one.id), one.what.item, 0);
                    return cracked;
                }
            }
            if (item < 0) item = pick(plain, plains);
        }
        if (item >= 0) {
            const content::ItemRow& row = tables_->items[size_t(item)];
            Lying one;
            if (!takesOptions(row)) {
                // The jewels, the pets and the jewellery come bare (Event.cpp's level 0 rows).
                one.what = Held{item, 0, 1};
            } else {
                // An excellent one at +0 with the skill; a plain one at the bag's level and up to
                // one more, the skill at a half. The luck at a half either way, and the option where
                // the skill or the luck missed: one in five +12, else +0, +4 or +8.
                const int plus = luck ? kLuckPlus : kKundunPlainLevel[t] + dice_.nextInt(0, kKundunAddLevel);
                const int refinement = std::min(plus, kRefineCap);
                one.what = Held{item, int16_t(refinement), int16_t(fullDurability(row, refinement))};
                const bool skill = dice_.nextInt(0, 2) == 1;
                one.what.luck = dice_.nextInt(0, 100) < kBoxLuckIn100;
                int option = 0;
                if (!one.what.luck || !skill) option = dice_.nextInt(0, 5) < 1 ? 3 : dice_.nextInt(0, 3);
                one.what.option = int8_t(option);
            }
            std::tie(one.column, one.row) = clearing(hero.column(), hero.row());
            one.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
            one.id = nextId_++;
            lying_.push_back(one);
            cracked.id = one.id;
            cracked.item = item;
            cracked.column = one.column;
            cracked.row = one.row;
            say(What::Cracked, hero, int32_t(one.id), item, one.what.refinement);
            return cracked;
        }
    }
    cracked.zen = int64_t(double(luck ? kLuckZen : kKundunZen[t]) * hero.excel.zenRate);
    money_ += cracked.zen;
    say(What::Cracked, hero, -1, -1, int32_t(cracked.zen));
    return cracked;
}

// The ground is swept first, so a run of a million deaths is not a million drops searched for
// a bare tile.
void Realm::dropFor(int level) {
    if (!tables_) return;
    lying_.clear();
    Body dead = bodies_[0];
    dead.player = false;
    dead.kind = -1;
    dead.level = level;
    leave(dead, bodies_[0]);
}

uint32_t Realm::lay(int32_t item, int refinement, bool luck, int option, uint8_t excellent,
                    uint8_t sockets) {
    if (!tables_ || item < 0 || size_t(item) >= tables_->items.size()) return 0;
    const Body& hero = bodies_[0];
    const content::ItemRow& row = tables_->items[size_t(item)];
    Lying one;
    // Whole, as `leave` makes what a kill leaves.
    one.what = Held{item, int16_t(std::clamp(refinement, 0, kRefineCap)),
                    int16_t(std::max(1, fullDurability(row, refinement)))};
    if (takesOptions(row) || jewellery(row)) {
        one.what.luck = luck;
        one.what.option = int8_t(std::clamp(option, 0, kMostOption));
        one.what.excellent = uint8_t(excellent & 63);
        one.what.sockets = uint8_t(std::min<int>(sockets, mostSocketsOf(row)));
        if (one.what.excellent && wears(row)) one.what.durability = int16_t(maximumDurability(row, one.what));
    }
    std::tie(one.column, one.row) = clearing(hero.column(), hero.row());
    one.vanishesAt = tick_ + int64_t(kLingerSeconds) * 20;
    one.id = nextId_++;
    lying_.push_back(one);
    say(What::Dropped, hero, int32_t(one.id), one.what.item, one.what.refinement);
    return one.id;
}

uint8_t drawRunePower(Random& dice, Kin kin, bool second, int level, bool commons) {
    const auto drawable = [&](const PowerRow& row) {
        if (!commons && row.rarity == Rarity::Common) return false;
        const Element element = elementOf(row.power);
        if (element != Element::None) return elementServes(element, kin);
        return row.takenBy(kin, second);
    };
    int count[kRarities] = {};
    double held = 0.0;
    for (int p = 1; powerOf(uint8_t(p)); ++p) {
        const int r = int(powerOf(uint8_t(p))->rarity);
        if (drawable(*powerOf(uint8_t(p))) && level >= kRuneRarityLevel[r]) ++count[r];
    }
    for (int r = 0; r < kRarities; ++r) held += count[r] > 0 ? kRuneRarityShare[r] : 0.0;
    int rarity = -1;
    if (held > 0.0) {
        double roll = dice.nextDouble() * held;
        for (int r = 0; r < kRarities && rarity < 0; ++r) {
            if (count[r] == 0) continue;
            if (roll < kRuneRarityShare[r]) rarity = r;
            roll -= kRuneRarityShare[r];
        }
        // The last rarity holding one, should rounding carry the roll past them all.
        for (int r = kRarities - 1; rarity < 0 && r >= 0; --r) rarity = count[r] > 0 ? r : -1;
    }
    int pick = rarity >= 0 ? dice.nextInt(0, count[rarity]) : -1;
    for (int p = 1; pick >= 0 && powerOf(uint8_t(p)); ++p) {
        const PowerRow& row = *powerOf(uint8_t(p));
        if (drawable(row) && int(row.rarity) == rarity && level >= kRuneRarityLevel[rarity] &&
            pick-- == 0)
            return uint8_t(p);
    }
    return 0;
}

}  // namespace mu::sim
