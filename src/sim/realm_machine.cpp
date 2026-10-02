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
    if (mixing_ < 0) return;
    mixing_ = -1;
    mixed_ = false;
    if (!tables_) return;
    for (int cell = 0; cell < kMachineCells; ++cell) {
        if (machine_[cell].empty()) continue;
        if (pour(*tables_, bag_, kWorn, kSlots, machine_[cell]) >= 0) machine_.lift(cell);
    }
    if (!machine_.empty()) core::logf("machine: the bag is full; what is left stays in the box");
}

int Realm::putIn(int bagSlot, int cell) {
    if (!atMachine() || mixed_ || !baggable(bagSlot) || bag_[bagSlot].empty()) return -1;
    const content::ItemRow& row = tables_->items[size_t(bag_[bagSlot].item)];
    if (cell < 0) {
        cell = pour(*tables_, machine_, 0, kMachineCells, bag_[bagSlot]);
        if (cell >= 0) bag_.lift(bagSlot);
        return cell;
    }
    const int onto = machine_.holder(*tables_, cell);
    if (const int went = onto >= 0 ? topUp(*tables_, machine_, onto, bag_[bagSlot]) : 0) {
        unstack(bag_, bagSlot, went);
        return onto;
    }
    if (!machine_.room(*tables_, cell, row.width, row.height)) return -1;
    machine_.put(cell, bag_.lift(bagSlot));
    return cell;
}

int Realm::takeOut(int cell, int bagSlot) {
    if (!atMachine() || machine_[cell].empty()) return -1;
    const content::ItemRow& row = tables_->items[size_t(machine_[cell].item)];
    int landed = -1;
    if (bagSlot < 0) {
        landed = pour(*tables_, bag_, kWorn, kSlots, machine_[cell]);
        if (landed >= 0) machine_.lift(cell);
    } else {
        const int onto = baggable(bagSlot) ? bag_.holder(*tables_, bagSlot) : -1;
        if (const int went = onto >= 0 ? topUp(*tables_, bag_, onto, machine_[cell]) : 0) {
            unstack(machine_, cell, went);
            landed = onto;
        } else if (baggable(bagSlot) && bag_.room(*tables_, bagSlot, row.width, row.height)) {
            bag_.put(bagSlot, machine_.lift(cell));
            landed = bagSlot;
        }
    }
    // The answer taken out, the box is the player's again.
    if (landed >= 0 && machine_.empty()) mixed_ = false;
    return landed;
}

bool Realm::shuffle(int from, int to) {
    if (!atMachine() || from == to || machine_[from].empty()) return false;
    const int onto = machine_.holder(*tables_, to);
    if (onto >= 0 && onto != from) {
        if (const int went = topUp(*tables_, machine_, onto, machine_[from])) {
            unstack(machine_, from, went);
            return true;
        }
    }
    const content::ItemRow& row = tables_->items[size_t(machine_[from].item)];
    if (!machine_.room(*tables_, to, row.width, row.height, from)) return false;
    machine_.put(to, machine_.lift(from));
    return true;
}

Judged Realm::judged(Service service, int socket) const {
    if (!tables_) return Judged{};
    return judge(*tables_, machine_, service, socket, bodies_[0].kin);
}

bool Realm::mix(Service service, int socket) {
    if (!tables_ || !atMachine() || mixed_) return false;
    Body& hero = bodies_[0];
    if (!hero.alive()) return false;
    const Judged j = judge(*tables_, machine_, service, socket, hero.kin);
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
    if (j.recipe == Recipe::Dinorant && tables_->itemAt(kGroupPets, 3) < 0) {
        refusal_ = "no Horn of Dinorant is in this world's tables";
        return false;
    }
    const int32_t rune = tables_->itemAt(kGroupPotions, 22);
    if ((service == Service::RemoveRune || service == Service::FuseRunes) && rune < 0) {
        refusal_ = "no Rune of Creation is in this world's tables";
        return false;
    }
    if (money_ < j.zen) {
        refusal_ = "not the Zen for it";
        return false;
    }
    money_ -= j.zen;

    // Rand.NextRandomBool(successRate). A sure thing draws nothing.
    const bool made = j.rate >= 100 || mixDice_.nextInt(0, 100) < j.rate;
    // Takes everything but the thing at `keep` out of the box: the jewels and the runes spent.
    const auto spendAllBut = [&](int keep) {
        for (int cell = 0; cell < kMachineCells; ++cell) {
            if (cell != keep) machine_.lift(cell);
        }
    };
    switch (service) {
        case Service::Combine:
            if (j.recipe == Recipe::PlusTen || j.recipe == Recipe::PlusEleven) {
                // The thing is Reference 1: StaysAsIs and up one on success, Disappear on
                // failure. The jewels Disappear either way.
                const Held thing = machine_[j.target];
                machine_.clear();
                if (made) {
                    const content::ItemRow& row = tables_->items[size_t(thing.item)];
                    machine_.put(j.target, atPlus(row, thing, std::min(kMachineCap,
                                                                       thing.refinement + 1)));
                }
            } else if (j.recipe == Recipe::Dinorant) {
                // PegasiaChaosMix: the box goes either way, and a success is a whole Horn of
                // Dinorant (ItemSerialCreateSend's 255), its options not rolled (sim/machine.h).
                machine_.clear();
                if (made) {
                    const int32_t horn = tables_->itemAt(kGroupPets, 3);
                    if (horn >= 0) {
                        const content::ItemRow& row = tables_->items[size_t(horn)];
                        Held dinorant{horn, 0, 0};
                        dinorant.durability = int16_t(maximumDurability(row, dinorant));
                        machine_.put(0, dinorant);
                    }
                }
            } else if (made) {
                machine_.clear();
                // SimpleItemCraftingHandler.CreateResultItemsAsync, in its order: the pick, the
                // plus, then ChaosWeaponAndFirstWingsCrafting's luck and option.
                const int32_t item = answers[mixDice_.nextInt(0, choices)];
                const content::ItemRow& row = tables_->items[size_t(item)];
                Held answer{item, int16_t(mixDice_.nextInt(0, 5)), 0};
                answer.durability = int16_t(std::max(1, fullDurability(row, answer.refinement)));
                answer.luck = mixDice_.nextInt(0, 100) < j.rate / 5 + 4;
                const int i = mixDice_.nextInt(0, 3);
                if (mixDice_.nextInt(0, 100) < j.rate / 5 + 4 * (i + 1)) {
                    answer.option = int8_t(3 - i);
                }
                machine_.put(machine_.free(*tables_, row.width, row.height), answer);
            } else {
                // The jewels Disappear; each thing is ChaosWeaponAndFirstWingsDowngradedRandom:
                // to a plus below the one it had, and its option down a level half the time.
                for (int cell = 0; cell < kMachineCells; ++cell) {
                    if (machine_[cell].empty()) continue;
                    const content::ItemRow& row = tables_->items[size_t(machine_[cell].item)];
                    if (jewelOfChaos(row) || jewelOfBless(row) || jewelOfSoul(row)) {
                        machine_.lift(cell);
                        continue;
                    }
                    Held thing = atPlus(row, machine_[cell],
                                        mixDice_.nextInt(0, machine_[cell].refinement));
                    if (thing.option > 0 && mixDice_.nextInt(0, 2) == 0) --thing.option;
                    machine_.put(cell, thing);
                }
            }
            break;
        case Service::RemoveRune: {
            Held thing = machine_[j.target];
            Held freed{rune, 0, 1};
            freed.powers[0] = thing.powers[j.socket];
            thing.powers[j.socket] = 0;
            spendAllBut(j.target);
            machine_.put(j.target, thing);
            machine_.put(machine_.free(*tables_, 1, 1), freed);
            break;
        }
        case Service::AddSocket: {
            Held thing = machine_[j.target];
            spendAllBut(j.target);
            if (made) thing.sockets = uint8_t(thing.sockets + 1);
            machine_.put(j.target, thing);
            break;
        }
        case Service::FuseRunes: {
            // The next rarity's runes his class may set, as a drop draws them (Realm::leave),
            // and any of that rarity should his class have none.
            int rarity = -1;
            for (int cell = 0; cell < kMachineCells && rarity < 0; ++cell) {
                const PowerRow* power =
                    machine_[cell].empty() ? nullptr : powerOf(machine_[cell].powers[0]);
                if (power && creation(tables_->items[size_t(machine_[cell].item)])) {
                    rarity = int(power->rarity) + 1;
                }
            }
            const auto drawable = [&](const PowerRow& row) {
                const Element element = elementOf(row.power);
                if (element != Element::None) return elementServes(element, hero.kin);
                return row.takenBy(hero.kin);
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
            machine_.clear();
            machine_.put(0, fused);
            break;
        }
    }
    mixed_ = !machine_.empty();
    core::logf("machine: %s at %d%% for %lld zen -- %s",
               service == Service::Combine ? recipeName(j.recipe) : serviceName(service), j.rate,
               static_cast<long long>(j.zen), made ? "made" : "failed");
    say(What::Mixed, hero, service == Service::Combine ? int32_t(j.recipe) : 100 + int(service),
        made ? 1 : 0, j.rate);
    return true;
}

}  // namespace mu::sim
