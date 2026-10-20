// The Jeweler's three services on the realm (sim/jeweler.h): Moss the Gambler's Recut, Salvage
// and Combine, each on bag slots in place. Off the Chaos Machine's dice, mixDice_, which the
// snapshot already carries.

#include "sim/realm.h"

#include <algorithm>

#include "sim/jeweler.h"

namespace mu::sim {
namespace {

const content::ItemRow* rowAt(const content::Tables& tables, const Held& what) {
    if (what.empty() || what.item < 0 || size_t(what.item) >= tables.items.size()) return nullptr;
    return &tables.items[size_t(what.item)];
}

}  // namespace

int Realm::recut(int slot, int line) {
    if (me().jeweling < 0) {
        refusal_ = "not at the Jeweler";
        return -1;
    }
    if (slot < kWorn || slot >= kSlots || line < 0 || line > 2) {
        refusal_ = "nothing to recut";
        return -1;
    }
    Held piece = me().bag[slot];
    const content::ItemRow* row = rowAt(*tables_, piece);
    if (row == nullptr || !jewelerTakes(*row) || piece.affixes[line] == 0) {
        refusal_ = "nothing to recut";
        return -1;
    }
    const int powers = affixCount(*row, piece);
    const int64_t zen = recutZen(piece.refinement);
    if (me().money < zen) {
        refusal_ = "not the Zen for it";
        return -1;
    }
    const ItemKey key = recutJewel(powers);
    const int32_t jewel = tables_->itemAt(key.group, key.number);
    int jewelSlot = -1;
    for (int s = kWorn; s < kSlots && jewelSlot < 0; ++s) {
        if (!me().bag[s].empty() && me().bag[s].item == jewel) jewelSlot = s;
    }
    if (jewel < 0 || jewelSlot < 0) {
        refusal_ = powers >= 4 ? "no Jewel of Life" : powers == 3 ? "no Jewel of Soul" : "no Jewel of Chaos";
        return -1;
    }
    const Affix made = recutRoll(*row, piece, line, mixDice_);
    if (made == Affix::None) {
        refusal_ = "nothing left to recut it to";
        return -1;
    }
    me().money -= zen;
    Held spent = me().bag.lift(jewelSlot);
    if (spent.durability > 1) {
        --spent.durability;
        me().bag.put(jewelSlot, spent);
    }
    piece.affixes[line] = uint8_t(made);
    me().bag.lift(slot);
    me().bag.put(slot, piece);
    return int(made);
}

int Realm::salvage(int slot) {
    if (me().jeweling < 0) {
        refusal_ = "not at the Jeweler";
        return -1;
    }
    if (slot < kWorn || slot >= kSlots) {
        refusal_ = "nothing to salvage";
        return -1;
    }
    const Held piece = me().bag[slot];
    const content::ItemRow* row = rowAt(*tables_, piece);
    if (row == nullptr || !jewelerTakes(*row)) {
        refusal_ = "nothing to salvage";
        return -1;
    }
    const bool twice = piece.refinement >= kSalvageTwiceFrom || piece.luck;
    const int prize = salvageRoll(affixCount(*row, piece), twice, mixDice_);
    const ItemKey key = salvagePrize(prize);
    const int32_t item = tables_->itemAt(key.group, key.number);
    if (item < 0) {
        refusal_ = "nothing to salvage";
        return -1;
    }
    // The piece first, so its cell is free for the prize.
    me().bag.lift(slot);
    if (give(item, -1, 0, salvageCount(prize)) < 0) {
        me().bag.put(slot, piece);
        refusal_ = "no room in the bag";
        return -1;
    }
    return prize + 1;
}

int Realm::combine(int first, int second, int third) {
    if (me().jeweling < 0) {
        refusal_ = "not at the Jeweler";
        return -1;
    }
    const int slots[3] = {first, second, third};
    int powers = -1, plus = kRefineCap;
    bool bound = false;
    for (int i = 0; i < 3; ++i) {
        const int s = slots[i];
        if (s < kWorn || s >= kSlots || (i > 0 && s == slots[0]) || (i > 1 && s == slots[1])) {
            refusal_ = "three pieces of one colour";
            return -1;
        }
        const Held& piece = me().bag[s];
        const content::ItemRow* row = rowAt(*tables_, piece);
        if (row == nullptr || !jewelerTakes(*row)) {
            refusal_ = "three pieces of one colour";
            return -1;
        }
        const int count = affixCount(*row, piece);
        if (powers >= 0 && count != powers) {
            refusal_ = "three pieces of one colour";
            return -1;
        }
        powers = count;
        plus = std::min<int>(plus, piece.refinement);
        bound = bound || piece.bound;
    }
    const int64_t zen = combineZen(powers);
    if (zen <= 0) {
        refusal_ = "purple and legendary do not combine";
        return -1;
    }
    if (me().money < zen) {
        refusal_ = "not the Zen for it";
        return -1;
    }
    // A powered row at random, each as likely as the next.
    int32_t rows[16];
    int n = 0;
    for (size_t i = 0; i < tables_->items.size() && n < 16; ++i) {
        if (jewelerTakes(tables_->items[i])) rows[n++] = int32_t(i);
    }
    if (n == 0) {
        refusal_ = "three pieces of one colour";
        return -1;
    }
    const int32_t item = rows[mixDice_.nextInt(0, n)];
    Held made;
    combineAffixes(tables_->items[size_t(item)], powers + 1, made, mixDice_);
    Held taken[3];
    for (int i = 0; i < 3; ++i) taken[i] = me().bag.lift(slots[i]);
    const int at = give(item, -1, plus, -1, false, 0, 0, 0, nullptr, made.affixes, bound);
    if (at < 0) {
        for (int i = 0; i < 3; ++i) me().bag.put(slots[i], taken[i]);
        refusal_ = "no room in the bag";
        return -1;
    }
    me().money -= zen;
    return at + 1;
}

}  // namespace mu::sim
