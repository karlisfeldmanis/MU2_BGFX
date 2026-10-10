// A trade between two players: MU's CNewUITrade and WebZen's trade packets (protocol.cpp
// CGTradeRequestSend, CGTradeResponseRecv, CGTradeMoneyRecv, CGTradeOkButtonRecv; user.cpp
// gObjTradeOkButton, gObjTradeCancel), kept in the realm. The user picked "A · MuMain, tidied"
// on 2026-10-10 (claude.ai/artifact/FVFs4zXhXtKXMweGzoNrA5): MU's one Accept each, which any
// change to either offer drops, and a hold of kDealHoldTicks before it is taken again.
//
// What is WebZen's: the ask refused to a player who is dead, busy in another window or on an
// event map; the answer only within reach; the Zen up to 100,000,000 and no more than he carries;
// both accepts dropped by any change; the swap only when everything fits both bags, the Zen moved
// with it. What is ours: the hold (MuMain greys OK for 150 frames on the client alone); a refused
// swap drops the accepts and leaves the window open, where WebZen cancels the trade; the leash; and
// each thing going back to the slot it came from, which WebZen gets by rolling the whole bag back
// (gObjInventoryRollback).
#include "sim/realm.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "sim/maps.h"

namespace mu::sim {
namespace {

// A thing that never changes hands: Sevina's treasures and the Archangel's weapons, carried back
// to whoever asked for them (describe.cpp's purple, Tone::Quest). **ours**.
bool kept(const content::ItemRow& row) { return archangelWeapon(row) || classTreasure(row); }

// One side's offer put back into a bag: each thing to the slot it came from when that is free,
// else wherever it fits. What fits nowhere is returned in `left`.
void putBack(const content::Tables& tables, Satchel& bag, Machine& box, const int8_t* from,
             std::vector<Held>* left) {
    for (int cell = 0; cell < kMachineCells; ++cell) {
        if (box[cell].empty()) continue;
        const Held what = box.lift(cell);
        const content::ItemRow& row = tables.items[size_t(what.item)];
        const int slot = from[cell];
        if (baggable(slot) && bag.room(tables, slot, row.width, row.height)) {
            bag.put(slot, what);
        } else if (pour(tables, bag, kWorn, kSlots, what) < 0 && left) {
            left->push_back(what);
        }
    }
}

}  // namespace

int Realm::dealPartner(size_t one) const {
    const uint32_t other = heroes_[one].dealing;
    return other == 0 ? -1 : playerOfId(other);
}

DealWhy Realm::dealBusy(size_t one) const {
    const Player& p = heroes_[one];
    if (p.dealing != 0 || (p.askedBy != 0 && p.askedUntil > tick_)) return DealWhy::Busy;
    if (p.trading >= 0 || p.banking >= 0 || p.mixing >= 0 || p.questing >= 0 || p.gating >= 0 ||
        p.angeling >= 0) {
        return DealWhy::Busy;
    }
    if (!bodies_[p.body].alive()) return DealWhy::Dead;
    return DealWhy::None;
}

void Realm::sayDeal(size_t to, DealEvent event, uint32_t whom, int32_t b, int32_t c) {
    const Body& him = bodies_[heroes_[to].body];
    say(What::Deal, him, int32_t(event), b, c, whom);
    happenings_.back().audience = him.id;
}

bool Realm::dealAsk(uint32_t whom) {
    const size_t asker = me_;
    const int other = playerOfId(whom);
    const auto refuse = [&](DealWhy why) {
        sayDeal(asker, DealEvent::Refused, whom, int32_t(why));
        return false;
    };
    if (!tables_ || other < 0 || size_t(other) == asker) return refuse(DealWhy::Nobody);
    const Body& a = mine();
    const Body& b = bodies_[heroes_[size_t(other)].body];
    if (b.gone) return refuse(DealWhy::Nobody);
    if (const MapRow* map = mapNumbered(int(tables_->map)); map != nullptr && map->event) {
        return refuse(DealWhy::Here);
    }
    if (const DealWhy why = dealBusy(asker); why != DealWhy::None) return refuse(why);
    if (const DealWhy why = dealBusy(size_t(other)); why != DealWhy::None) return refuse(why);
    if (std::max(std::abs(a.column() - b.column()), std::abs(a.row() - b.row())) > kDealReach) {
        return refuse(DealWhy::Far);
    }
    heroes_[size_t(other)].askedBy = a.id;
    heroes_[size_t(other)].askedUntil = tick_ + kDealAskTicks;
    sayDeal(size_t(other), DealEvent::Asked, a.id);
    sayDeal(asker, DealEvent::Asked, b.id);
    return true;
}

bool Realm::dealAnswer(uint32_t asker, bool yes) {
    const size_t answering = me_;
    Player& asked = me();
    if (asked.askedBy == 0 || asked.askedUntil <= tick_) return false;
    if (asker != 0 && asker != asked.askedBy) return false;
    asker = asked.askedBy;
    asked.askedBy = 0;
    asked.askedUntil = 0;
    const int other = playerOfId(asker);
    if (other < 0 || bodies_[heroes_[size_t(other)].body].gone) return false;
    if (!yes) {
        sayDeal(size_t(other), DealEvent::Declined, mine().id);
        return true;
    }
    // Asked again of both, as CGTradeResponseRecv does: nothing may have opened in between.
    const Body& a = bodies_[heroes_[size_t(other)].body];
    const Body& b = mine();
    DealWhy why = dealBusy(answering);
    if (why == DealWhy::None) why = dealBusy(size_t(other));
    if (why == DealWhy::None &&
        std::max(std::abs(a.column() - b.column()), std::abs(a.row() - b.row())) > kDealReach) {
        why = DealWhy::Far;
    }
    if (why != DealWhy::None) {
        sayDeal(size_t(other), DealEvent::Refused, b.id, int32_t(why));
        sayDeal(answering, DealEvent::Refused, a.id, int32_t(why));
        return false;
    }
    for (const size_t one : {answering, size_t(other)}) {
        Player& p = heroes_[one];
        p.dealing = one == answering ? a.id : b.id;
        p.offer.clear();
        std::fill(std::begin(p.offerFrom), std::end(p.offerFrom), int8_t(-1));
        p.offerZen = 0;
        p.accepted = false;
        p.holdUntil = 0;
        p.dealVersion = 0;
        p.askedBy = 0;
        p.askedUntil = 0;
    }
    sayDeal(answering, DealEvent::Opened, a.id);
    sayDeal(size_t(other), DealEvent::Opened, b.id);
    return true;
}

void Realm::dealChanged() {
    const int other = dealPartner(me_);
    for (const int one : {int(me_), other}) {
        if (one < 0) continue;
        Player& p = heroes_[size_t(one)];
        p.accepted = false;
        p.holdUntil = tick_ + kDealHoldTicks;
        ++p.dealVersion;
    }
}

int Realm::dealPut(int bagSlot, int cell) {
    if (!tables_ || me().dealing == 0 || !baggable(bagSlot) || me().bag[bagSlot].empty()) return -1;
    if (!mine().alive()) return -1;
    const Held& what = me().bag[bagSlot];
    const content::ItemRow& row = tables_->items[size_t(what.item)];
    if (kept(row)) return -1;
    Machine& box = me().offer;
    if (cell < 0) cell = box.free(*tables_, row.width, row.height);
    if (cell < 0 || cell >= kMachineCells || !box.room(*tables_, cell, row.width, row.height)) return -1;
    box.put(cell, me().bag.lift(bagSlot));
    me().offerFrom[cell] = int8_t(bagSlot);
    dealChanged();
    return cell;
}

int Realm::dealTake(int cell, int bagSlot) {
    if (!tables_ || me().dealing == 0 || cell < 0 || cell >= kMachineCells) return -1;
    Machine& box = me().offer;
    const int at = box.holder(*tables_, cell);
    if (at < 0) return -1;
    const content::ItemRow& row = tables_->items[size_t(box[at].item)];
    Satchel& bag = me().bag;
    int landed = -1;
    for (const int slot : {bagSlot, int(me().offerFrom[at])}) {
        if (baggable(slot) && bag.room(*tables_, slot, row.width, row.height)) {
            bag.put(slot, box[at]);
            landed = slot;
            break;
        }
    }
    if (landed < 0) landed = pour(*tables_, bag, kWorn, kSlots, box[at]);
    if (landed < 0) return -1;
    box.lift(at);
    me().offerFrom[at] = -1;
    dealChanged();
    return landed;
}

bool Realm::dealZen(int64_t zen) {
    if (me().dealing == 0 || zen < 0 || zen > kDealMostZen || zen > me().money) return false;
    if (zen == me().offerZen) return true;
    me().offerZen = zen;
    dealChanged();
    return true;
}

bool Realm::dealAccept(bool yes) {
    const int other = dealPartner(me_);
    if (other < 0) return false;
    if (!yes) {
        if (!me().accepted) return true;
        me().accepted = false;
        ++me().dealVersion;
        ++heroes_[size_t(other)].dealVersion;
        return true;
    }
    if (me().holdUntil > tick_ || me().offerZen > me().money) return false;
    me().accepted = true;
    ++me().dealVersion;
    ++heroes_[size_t(other)].dealVersion;
    if (heroes_[size_t(other)].accepted) dealSwap();
    return true;
}

void Realm::closeDeal() {
    if (me().dealing != 0) dealCancel(me_, DealWhy::Closed);
}

int Realm::dealMisfit(bool theirs) const {
    const int other = dealPartner(me_);
    if (!tables_ || other < 0) return -1;
    const Player& giver = theirs ? heroes_[size_t(other)] : me();
    Satchel bag = theirs ? me().bag : heroes_[size_t(other)].bag;
    for (int cell = 0; cell < kMachineCells; ++cell) {
        if (giver.offer[cell].empty()) continue;
        if (pour(*tables_, bag, kWorn, kSlots, giver.offer[cell]) < 0) return cell;
    }
    return -1;
}

void Realm::dealSwap() {
    const size_t a = me_;
    const int other = dealPartner(a);
    if (other < 0) return;
    const size_t b = size_t(other);
    Player& pa = heroes_[a];
    Player& pb = heroes_[b];
    if (pa.offerZen > pa.money || pb.offerZen > pb.money) {
        dealCancel(a, DealWhy::Zen);
        return;
    }
    // Both bags as they would be, all of it or none (TradeItemInventoryPutTest).
    Satchel intoA = pa.bag, intoB = pb.bag;
    int gotA = 0, gotB = 0;
    bool fitsA = true, fitsB = true;
    for (int cell = 0; cell < kMachineCells; ++cell) {
        if (!pb.offer[cell].empty()) {
            fitsA = fitsA && pour(*tables_, intoA, kWorn, kSlots, pb.offer[cell]) >= 0;
            ++gotA;
        }
        if (!pa.offer[cell].empty()) {
            fitsB = fitsB && pour(*tables_, intoB, kWorn, kSlots, pa.offer[cell]) >= 0;
            ++gotB;
        }
    }
    if (!fitsA || !fitsB) {
        pa.accepted = pb.accepted = false;
        ++pa.dealVersion;
        ++pb.dealVersion;
        sayDeal(a, DealEvent::NoRoom, bodies_[pb.body].id, fitsA ? 0 : 1);
        sayDeal(b, DealEvent::NoRoom, bodies_[pa.body].id, fitsB ? 0 : 1);
        return;
    }
    pa.bag = intoA;
    pb.bag = intoB;
    const int64_t zenA = pa.offerZen, zenB = pb.offerZen;
    pa.money += zenB - zenA;
    pb.money += zenA - zenB;
    for (Player* p : {&pa, &pb}) {
        p->offer.clear();
        std::fill(std::begin(p->offerFrom), std::end(p->offerFrom), int8_t(-1));
        p->dealing = 0;
        p->offerZen = 0;
        p->accepted = false;
        p->holdUntil = 0;
        ++p->dealVersion;
    }
    sayDeal(a, DealEvent::Done, bodies_[pb.body].id, gotA, int32_t(std::min<int64_t>(zenB, INT32_MAX)));
    sayDeal(b, DealEvent::Done, bodies_[pa.body].id, gotB, int32_t(std::min<int64_t>(zenA, INT32_MAX)));
}

void Realm::dealCancel(size_t one, DealWhy why) {
    const int other = dealPartner(one);
    for (const int side : {int(one), other}) {
        if (side < 0) continue;
        Player& p = heroes_[size_t(side)];
        if (p.dealing == 0) continue;
        const uint32_t whom = p.dealing;
        std::vector<Held> left;
        if (tables_) putBack(*tables_, p.bag, p.offer, p.offerFrom, &left);
        // A thing with no room left in the bag goes on the ground at his feet, his alone.
        for (const Held& what : left) {
            if (const uint32_t id = throwDown(bodies_[p.body], what); id != 0) lying_.back().owner = bodies_[p.body].id;
        }
        std::fill(std::begin(p.offerFrom), std::end(p.offerFrom), int8_t(-1));
        p.dealing = 0;
        p.offerZen = 0;
        p.accepted = false;
        p.holdUntil = 0;
        ++p.dealVersion;
        sayDeal(size_t(side), DealEvent::Cancelled, whom, int32_t(why));
    }
}

void Realm::dealTick() {
    for (size_t one = 0; one < heroes_.size(); ++one) {
        Player& p = heroes_[one];
        // An ask unanswered in time: the asker hears so.
        if (p.askedBy != 0 && p.askedUntil <= tick_) {
            const int asker = playerOfId(p.askedBy);
            if (asker >= 0) sayDeal(size_t(asker), DealEvent::Declined, bodies_[p.body].id, 1);
            p.askedBy = 0;
            p.askedUntil = 0;
        }
        if (p.dealing == 0) continue;
        const Body& him = bodies_[p.body];
        const int other = dealPartner(one);
        DealWhy why = DealWhy::None;
        if (other < 0 || bodies_[heroes_[size_t(other)].body].gone) {
            why = DealWhy::Gone;
        } else if (him.gone) {
            why = DealWhy::Gone;
        } else {
            const Body& them = bodies_[heroes_[size_t(other)].body];
            if (!him.alive() || !them.alive()) why = DealWhy::Dead;
            else if (std::max(std::abs(him.column() - them.column()), std::abs(him.row() - them.row())) > kDealLeash) {
                why = DealWhy::Far;
            } else if (p.trading >= 0 || p.banking >= 0 || p.mixing >= 0 || p.questing >= 0 || p.gating >= 0 ||
                       p.angeling >= 0) {
                why = DealWhy::Busy;
            }
        }
        if (why == DealWhy::None) continue;
        if (other < 0 || heroes_[size_t(other)].dealing != him.id) {
            // The other side is gone: only this one's offer comes home.
            const uint32_t whom = p.dealing;
            std::vector<Held> left;
            if (tables_) putBack(*tables_, p.bag, p.offer, p.offerFrom, &left);
            for (const Held& what : left) throwDown(him, what);
            std::fill(std::begin(p.offerFrom), std::end(p.offerFrom), int8_t(-1));
            p.dealing = 0;
            p.offerZen = 0;
            p.accepted = false;
            ++p.dealVersion;
            if (!him.gone) sayDeal(one, DealEvent::Cancelled, whom, int32_t(why));
            continue;
        }
        dealCancel(one, why);
    }
}

Satchel Realm::bagWithOffer() const {
    Satchel bag = me().bag;
    if (me().dealing == 0 || !tables_) return bag;
    Machine box = me().offer;
    std::vector<Held> left;
    putBack(*tables_, bag, box, me().offerFrom, &left);
    return bag;
}

const Machine& Realm::dealOffer(bool theirs) const {
    static const Machine none;
    if (me().dealing == 0) return none;
    if (!theirs) return me().offer;
    const int other = dealPartner(me_);
    return other < 0 ? none : heroes_[size_t(other)].offer;
}

int64_t Realm::dealZenOf(bool theirs) const {
    if (me().dealing == 0) return 0;
    if (!theirs) return me().offerZen;
    const int other = dealPartner(me_);
    return other < 0 ? 0 : heroes_[size_t(other)].offerZen;
}

bool Realm::dealAccepted(bool theirs) const {
    if (me().dealing == 0) return false;
    if (!theirs) return me().accepted;
    const int other = dealPartner(me_);
    return other >= 0 && heroes_[size_t(other)].accepted;
}

}  // namespace mu::sim
