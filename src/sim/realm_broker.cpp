// The realm's side of the Broker (sim/broker.h, docs/game/broker.md): the four commands the server
// sends to move a player's things in and out of the market inside the lockstep, so every mirror
// sees his bag change as the server's realm does. The market itself -- the listings, the prices,
// the Collect boxes -- is the server's book and none of the realm's.
#include "sim/realm.h"

#include <algorithm>

#include "sim/broker.h"
#include "sim/realm_tuning.h"

namespace mu::sim {

namespace {

// The very thing: every field a Held has. A slot that changed between the server's look and the
// tick -- a move, a refine, a sale -- is not taken.
bool same(const Held& a, const Held& b) {
    return a.item == b.item && a.refinement == b.refinement && a.durability == b.durability &&
           a.skill == b.skill && a.luck == b.luck && a.option == b.option &&
           a.excellent == b.excellent && a.sockets == b.sockets &&
           std::equal(std::begin(a.powers), std::end(a.powers), std::begin(b.powers)) &&
           std::equal(std::begin(a.affixes), std::end(a.affixes), std::begin(b.affixes)) &&
           a.wing == b.wing && a.bound == b.bound;
}

}  // namespace

const Satchel* Realm::satchelOf(uint32_t id) const {
    const int at = playerOfId(id);
    return at < 0 ? nullptr : &heroes_[size_t(at)].bag;
}

const Vault* Realm::vaultOf(uint32_t id) const {
    const int at = playerOfId(id);
    return at < 0 ? nullptr : &heroes_[size_t(at)].vault;
}

bool Realm::atBroker(uint32_t id) const {
    const int at = playerOfId(id);
    if (!tables_ || at < 0) return false;
    const Body& hero = bodies_[heroes_[size_t(at)].body];
    if (!hero.alive()) return false;
    for (size_t i = 0; i < tables_->folk.size(); ++i) {
        if (tables_->folk[i].number != kBroker) continue;
        int column = 0, row = 0;
        folkTile(int(i), &column, &row);
        const float dx = hero.x - float(column), dy = hero.y - float(row);
        if (dx * dx + dy * dy <= kCounter * kCounter) return true;
    }
    return false;
}

int Realm::brokerTake(int slot, bool fromVault, const Held& expect, int pieces) {
    if (!tables_ || !mine().alive() || expect.empty()) return -1;
    // **Part of a stack** (the user, 2026-10-23: "i cant sell stacked unirias as single item"):
    // that many come off and the rest stays in its cell; 0 or the whole stack lifts the cell.
    const content::ItemRow* row = size_t(expect.item) < tables_->items.size() ? &tables_->items[size_t(expect.item)] : nullptr;
    const bool part = row != nullptr && stacks(*row) && !ammunition(*row) && pieces > 0 && pieces < expect.durability;
    const auto takeFrom = [&](auto& grid) {
        if (!part) {
            grid.lift(slot);
            return;
        }
        Held left = expect;
        left.durability = int16_t(expect.durability - pieces);
        grid.put(slot, left);
    };
    if (fromVault) {
        if (slot < 0 || slot >= kVaultCells || !same(me().vault[slot], expect)) return -1;
        takeFrom(me().vault);
        return 1;
    }
    // From the bag, never off his body: a worn thing is taken off first, as for a trade.
    if (!baggable(slot) || !same(me().bag[slot], expect)) return -1;
    takeFrom(me().bag);
    return 1;
}

int Realm::brokerBuy(const Held& what, int64_t price, bool intoVault) {
    if (!tables_ || what.empty() || price < 0) return -1;
    // His carried Zen first, then his vault's (the user, 2026-10-11).
    if (me().money + me().vault.zen() < price) return -1;
    const int64_t carried = std::min(me().money, price);
    me().money -= carried;
    if (price > carried) me().vault.setZen(me().vault.zen() - (price - carried));
    // A player's buy goes to his vault (the user, 2026-10-11), and to his bag only when the vault
    // is full; a bot's to his bag, where he wears an upgrade from.
    if (intoVault) {
        if (pour(*tables_, me().vault, 0, kVaultCells, what) >= 0) return 1;
        return pour(*tables_, me().bag, kWorn, kSlots, what) >= 0 ? 3 : 2;
    }
    return pour(*tables_, me().bag, kWorn, kSlots, what) >= 0 ? 1 : 2;
}

int Realm::brokerGive(const Held& what) {
    if (!tables_ || what.empty()) return -1;
    return pour(*tables_, me().bag, kWorn, kSlots, what) >= 0 ? 1 : -1;
}

int64_t Realm::brokerZen(int64_t zen) {
    if (zen <= 0) return -1;
    // No more than MU's ceiling carried; the rest stays in his Collect.
    const int64_t room = std::max<int64_t>(0, kBrokerMostPrice - me().money);
    const int64_t given = std::min(zen, room);
    if (given <= 0) return -1;
    me().money += given;
    return given;
}

}  // namespace mu::sim
