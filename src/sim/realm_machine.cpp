// The Chaos Machine's half of the realm (sim/machine.h, docs/chaos-machine.md).
//
// The moves are the vault's: each looks for its room before anything leaves where it was, so a
// refusal changes nothing, and a stack let go on a stack of its kind pours into it. The mix is
// OpenMU's BaseItemCraftingHandler.DoMixAsync: judged, paid, rolled, and the box changed by
// each line's SuccessResult or FailResult. Its draws come off `mixDice_`, so a run that never
// mixes rolls what it rolled before.
#include "sim/realm.h"

#include <algorithm>

#include "core/log.h"
#include "sim/wear.h"

namespace mu::sim {
namespace {

template <class Grid>
void unstack(Grid& grid, int at, int went) {
    Held left = grid[at];
    left.durability = int16_t(left.durability - went);
    if (left.durability <= 0) grid.lift(at);
    else grid.put(at, left);
}

// A thing put up or down a plus keeps the share of its wear it had: OpenMU's
// `GetMaximumDurabilityOfOnePiece() * Durability / previousMaxDurability`.
Held atPlus(const content::ItemRow& row, Held thing, int plus) {
    const int before = std::max(1, maximumDurability(row, thing));
    thing.refinement = int16_t(plus);
    const int after = maximumDurability(row, thing);
    if (row.durability > 0 && !stacks(row)) {
        thing.durability = int16_t(std::clamp(after * thing.durability / before, 0, after));
    }
    return thing;
}

// Version075's three answers: MODEL_MACE+6, MODEL_BOW+6, MODEL_STAFF+7.
constexpr int kChaosWeapons[3][2] = {{2, 6}, {4, 6}, {5, 7}};

}  // namespace

void Realm::closeMachine() {
    if (me().mixing < 0) return;
    me().mixing = -1;
    me().mixed = false;
    if (!tables_) return;
    for (int cell = 0; cell < kMachineCells; ++cell) {
        if (me().machine[cell].empty()) continue;
        if (pour(*tables_, me().bag, kWorn, kSlots, me().machine[cell]) >= 0) me().machine.lift(cell);
    }
    if (!me().machine.empty()) core::logf("machine: the bag is full; what is left stays in the box");
}

int Realm::putIn(int bagSlot, int cell) {
    // A worn thing too, straight off him -- the 1st wing for the 2nd (the user, 2026-10-05:
    // 'tried to drag 1st wings to chaos machine straight from inventory was not able').
    if (!atMachine() || me().mixed || bagSlot < 0 || bagSlot >= kSlots || me().bag[bagSlot].empty()) {
        return -1;
    }
    const content::ItemRow& row = tables_->items[size_t(me().bag[bagSlot].item)];
    const bool worn = wearable(bagSlot);
    if (cell < 0) {
        cell = pour(*tables_, me().machine, 0, kMachineCells, me().bag[bagSlot]);
        if (cell >= 0) me().bag.lift(bagSlot);
        if (cell >= 0 && worn) rearm(mine());
        return cell;
    }
    const int onto = me().machine.holder(*tables_, cell);
    if (const int went = onto >= 0 ? topUp(*tables_, me().machine, onto, me().bag[bagSlot]) : 0) {
        unstack(me().bag, bagSlot, went);
        return onto;
    }
    // A jewel let go on a thing in the box it works: applied there (Realm::refineAcross).
    if (onto >= 0 && worksOn(me().bag[bagSlot], me().machine[onto])) {
        return refineAcross(Store::Bag, bagSlot, Store::Machine, onto) ? onto : -1;
    }
    if (!me().machine.room(*tables_, cell, row.width, row.height)) return -1;
    me().machine.put(cell, me().bag.lift(bagSlot));
    if (worn) rearm(mine());
    return cell;
}

int Realm::takeOut(int cell, int bagSlot) {
    if (!atMachine() || me().machine[cell].empty()) return -1;
    const content::ItemRow& row = tables_->items[size_t(me().machine[cell].item)];
    int landed = -1;
    if (bagSlot < 0) {
        landed = pour(*tables_, me().bag, kWorn, kSlots, me().machine[cell]);
        if (landed >= 0) me().machine.lift(cell);
    } else if (const int under = baggable(bagSlot)           ? me().bag.holder(*tables_, bagSlot)
                                 : bagSlot >= 0 && bagSlot < kSlots ? bagSlot
                                                                    : -1;
               !me().mixed && under >= 0 && worksOn(me().machine[cell], me().bag[under])) {
        // A jewel from the box onto a thing carried or worn.
        if (refineAcross(Store::Machine, cell, Store::Bag, under)) landed = under;
    } else {
        const int onto = baggable(bagSlot) ? me().bag.holder(*tables_, bagSlot) : -1;
        if (const int went = onto >= 0 ? topUp(*tables_, me().bag, onto, me().machine[cell]) : 0) {
            unstack(me().machine, cell, went);
            landed = onto;
        } else if (baggable(bagSlot) && me().bag.room(*tables_, bagSlot, row.width, row.height)) {
            me().bag.put(bagSlot, me().machine.lift(cell));
            landed = bagSlot;
        }
    }
    // The answer taken out, the box is the player's again.
    if (landed >= 0 && me().machine.empty()) me().mixed = false;
    return landed;
}

bool Realm::shuffle(int from, int to) {
    if (!atMachine() || from == to || me().machine[from].empty()) return false;
    const int onto = me().machine.holder(*tables_, to);
    if (onto >= 0 && onto != from) {
        if (const int went = topUp(*tables_, me().machine, onto, me().machine[from])) {
            unstack(me().machine, from, went);
            return true;
        }
        if (!me().mixed && worksOn(me().machine[from], me().machine[onto])) {
            return refineAcross(Store::Machine, from, Store::Machine, onto);
        }
    }
    const content::ItemRow& row = tables_->items[size_t(me().machine[from].item)];
    if (!me().machine.room(*tables_, to, row.width, row.height, from)) return false;
    me().machine.put(to, me().machine.lift(from));
    return true;
}

Judged Realm::judged(Service service, int socket) const {
    if (!tables_) return Judged{};
    return judge(*tables_, me().machine, service, socket, mine().kin);
}

bool Realm::mix(Service service, int socket) {
    if (!tables_ || !atMachine() || me().mixed) return false;
    Body& hero = mine();
    if (!hero.alive()) return false;
    const Judged j = judge(*tables_, me().machine, service, socket, hero.kin);
    if (!j.ready) {
        refusal_ = "the box is not ready for " + std::string(serviceName(service));
        return false;
    }
    int answers[3];
    int choices = 0;
    if (j.recipe == Recipe::ChaosWeapon) {
        for (const auto& one : kChaosWeapons) {
            const int32_t item = tables_->itemAt(one[0], one[1]);
            if (item >= 0) answers[choices++] = item;
        }
        if (choices == 0) {
            refusal_ = "no Chaos weapon is in this world's tables";
            return false;
        }
    }
    if (j.recipe == Recipe::Wings) {
        // The hero's class's wing alone (sim/machine.h), so the pick below has one choice.
        const int32_t wing = tables_->itemAt(12, firstWingOf(hero.kin));
        if (wing < 0) {
            refusal_ = std::string("no ") + firstWingName(hero.kin) + " is in this world's tables";
            return false;
        }
        answers[choices++] = wing;
    }
    const int32_t secondWingItem =
        j.recipe == Recipe::SecondWings ? tables_->itemAt(12, secondWingOf(hero.kin)) : -1;
    if (j.recipe == Recipe::SecondWings && secondWingItem < 0) {
        refusal_ = std::string("no ") + secondWingName(hero.kin) + " is in this world's tables";
        return false;
    }
    if (j.recipe == Recipe::Cloak && tables_->itemAt(kGroupPets, 18) < 0) {
        refusal_ = "no Invisibility Cloak is in this world's tables";
        return false;
    }
    if (j.recipe == Recipe::Cloak && hero.level < kCloakFromLevel) {
        refusal_ = "Must be over level 15 to combine a Cloak of Invisibility.";
        return false;
    }
    if (j.recipe == Recipe::Dinorant && tables_->itemAt(kGroupPets, 3) < 0) {
        refusal_ = "no Horn of Dinorant is in this world's tables";
        return false;
    }
    const int32_t rune = tables_->itemAt(kGroupPotions, 22);
    if ((service == Service::RemoveRune || service == Service::FuseRunes) && rune < 0) {
        refusal_ = "no Rune of Creation is in this world's tables";
        return false;
    }
    if (me().money < j.zen) {
        refusal_ = "not the Zen for it";
        return false;
    }
    me().money -= j.zen;

    // Rand.NextRandomBool(successRate). A sure thing draws nothing.
    const bool made = j.rate >= 100 || mixDice_.nextInt(0, 100) < j.rate;
    // Takes everything but the thing at `keep` out of the box: the jewels and the runes spent.
    const auto spendAllBut = [&](int keep) {
        for (int cell = 0; cell < kMachineCells; ++cell) {
            if (cell != keep) me().machine.lift(cell);
        }
    };
    switch (service) {
        // The one-mix services -- Upgrade, Chaos Weapon, 1st and 2nd Wings, Dinorant, Cloak -- are
        // Combine's own mixes, judged to them alone.
        case Service::Combine:
        case Service::FirstWings:
        case Service::SecondWings:
        case Service::ChaosWeapon:
        case Service::Upgrade:
        case Service::Dinorant:
        case Service::Cloak:
            if (j.recipe == Recipe::PlusTen || j.recipe == Recipe::PlusEleven) {
                // The thing is Reference 1: StaysAsIs and up one on success, Disappear on
                // failure. The jewels Disappear either way.
                const Held thing = me().machine[j.target];
                me().machine.clear();
                if (made) {
                    const content::ItemRow& row = tables_->items[size_t(thing.item)];
                    me().machine.put(j.target, atPlus(row, thing, std::min(kMachineCap,
                                                                       thing.refinement + 1)));
                }
            } else if (j.recipe == Recipe::Dinorant) {
                // PegasiaChaosMix: the box goes either way, and a success is a whole Horn of
                // Dinorant (ItemSerialCreateSend's 255), its options not rolled (sim/machine.h).
                me().machine.clear();
                if (made) {
                    const int32_t horn = tables_->itemAt(kGroupPets, 3);
                    if (horn >= 0) {
                        const content::ItemRow& row = tables_->items[size_t(horn)];
                        Held dinorant{horn, 0, 0};
                        dinorant.durability = int16_t(maximumDurability(row, dinorant));
                        me().machine.put(0, dinorant);
                    }
                }
            } else if (j.recipe == Recipe::SecondWings) {
                // WingChaosMix: the box goes either way; a success is the wing at +0 with
                // WebZen's own rolls (MixSystem.cpp:2817-2880).
                me().machine.clear();
                if (made) {
                    const content::ItemRow& row = tables_->items[size_t(secondWingItem)];
                    Held wing{secondWingItem, 0, 0};
                    wing.durability = int16_t(maximumDurability(row, wing));
                    wing.luck = mixDice_.nextInt(0, 5) == 0;
                    const int roll = mixDice_.nextInt(0, 100);
                    switch (mixDice_.nextInt(0, 3)) {
                        case 0: if (roll < 4) wing.option = 3; break;
                        case 1: if (roll < 10) wing.option = 2; break;
                        default: if (roll < 20) wing.option = 1; break;
                    }
                    if (mixDice_.nextInt(0, 5) == 0) wing.wing = uint8_t(1u << mixDice_.nextInt(0, 3));
                    if (mixDice_.nextInt(0, 2) != 0) wing.wing |= kWingOptionKind;
                    me().machine.put(0, wing);
                }
            } else if (j.recipe == Recipe::Cloak) {
                // The box goes either way (BloodCastle.cpp:1409-1435); a success is a whole
                // cloak of the scroll's level.
                int level = 1;
                for (int cell = 0; cell < kMachineCells; ++cell) {
                    const content::ItemRow* row = me().machine[cell].empty()
                        ? nullptr
                        : &tables_->items[size_t(me().machine[cell].item)];
                    if (row && scrollOfArchangel(*row)) level = me().machine[cell].refinement;
                }
                me().machine.clear();
                if (made) {
                    const int32_t cloak = tables_->itemAt(kGroupPets, 18);
                    const content::ItemRow& row = tables_->items[size_t(cloak)];
                    Held ticket{cloak, int16_t(level), 0};
                    ticket.durability = int16_t(maximumDurability(row, ticket));
                    me().machine.put(0, ticket);
                }
            } else if (made) {
                me().machine.clear();
                // SimpleItemCraftingHandler.CreateResultItemsAsync, in its order: the pick, the
                // plus, then ChaosWeaponAndFirstWingsCrafting's luck and option. A wing is +0
                // (CHAOS_MIX_WING_ITEMLEVEL_FIX) and draws no plus.
                const int32_t item = answers[mixDice_.nextInt(0, choices)];
                const content::ItemRow& row = tables_->items[size_t(item)];
                const int plus = j.recipe == Recipe::Wings ? 0 : mixDice_.nextInt(0, 5);
                Held answer{item, int16_t(plus), 0};
                answer.durability = int16_t(std::max(1, fullDurability(row, answer.refinement)));
                answer.luck = mixDice_.nextInt(0, 100) < j.rate / 5 + 4;
                const int i = mixDice_.nextInt(0, 3);
                if (mixDice_.nextInt(0, 100) < j.rate / 5 + 4 * (i + 1)) {
                    answer.option = int8_t(3 - i);
                }
                me().machine.put(me().machine.free(*tables_, row.width, row.height), answer);
            } else {
                // The jewels Disappear; each thing is ChaosWeaponAndFirstWingsDowngradedRandom:
                // to a plus below the one it had, and its option down a level half the time.
                for (int cell = 0; cell < kMachineCells; ++cell) {
                    if (me().machine[cell].empty()) continue;
                    const content::ItemRow& row = tables_->items[size_t(me().machine[cell].item)];
                    if (jewelOfChaos(row) || jewelOfBless(row) || jewelOfSoul(row)) {
                        me().machine.lift(cell);
                        continue;
                    }
                    Held thing = atPlus(row, me().machine[cell],
                                        mixDice_.nextInt(0, me().machine[cell].refinement));
                    if (thing.option > 0 && mixDice_.nextInt(0, 2) == 0) --thing.option;
                    me().machine.put(cell, thing);
                }
            }
            break;
        case Service::RemoveRune: {
            Held thing = me().machine[j.target];
            Held freed{rune, 0, 1};
            freed.powers[0] = thing.powers[j.socket];
            thing.powers[j.socket] = 0;
            spendAllBut(j.target);
            me().machine.put(j.target, thing);
            me().machine.put(me().machine.free(*tables_, 1, 1), freed);
            break;
        }
        case Service::AddSocket: {
            Held thing = me().machine[j.target];
            spendAllBut(j.target);
            if (made) thing.sockets = uint8_t(thing.sockets + 1);
            me().machine.put(j.target, thing);
            break;
        }
        case Service::FuseRunes: {
            // The next rarity's runes his class may set, as a drop draws them (Realm::leave),
            // and any of that rarity should his class have none.
            int rarity = -1;
            for (int cell = 0; cell < kMachineCells && rarity < 0; ++cell) {
                const PowerRow* power =
                    me().machine[cell].empty() ? nullptr : powerOf(me().machine[cell].powers[0]);
                if (power && creation(tables_->items[size_t(me().machine[cell].item)])) {
                    rarity = int(nextRarity(power->rarity));
                }
            }
            const auto drawable = [&](const PowerRow& row) {
                const Element element = elementOf(row.power);
                if (element != Element::None) return elementServes(element, hero.kin);
                return row.takenBy(hero.kin, hero.second);
            };
            int count = 0, any = 0;
            for (int p = 1; powerOf(uint8_t(p)); ++p) {
                const PowerRow& row = *powerOf(uint8_t(p));
                if (int(row.rarity) != rarity) continue;
                ++any;
                if (drawable(row)) ++count;
            }
            const bool mine = count > 0;
            int pick = mixDice_.nextInt(0, mine ? count : std::max(1, any));
            Held fused{rune, 0, 1};
            for (int p = 1; powerOf(uint8_t(p)); ++p) {
                const PowerRow& row = *powerOf(uint8_t(p));
                if (int(row.rarity) != rarity || (mine && !drawable(row))) continue;
                if (pick-- == 0) fused.powers[0] = uint8_t(p);
            }
            me().machine.clear();
            me().machine.put(0, fused);
            break;
        }
    }
    me().mixed = !me().machine.empty();
    core::logf("machine: %s at %d%% for %lld zen -- %s",
               sim::byRecipe(service) ? recipeName(j.recipe)
                                                                         : serviceName(service),
               j.rate,
               static_cast<long long>(j.zen), made ? "made" : "failed");
    say(What::Mixed, hero,
        sim::byRecipe(service) ? int32_t(j.recipe)
                                                                 : 100 + int(service),
        made ? 1 : 0, j.rate);
    return true;
}

}  // namespace mu::sim
