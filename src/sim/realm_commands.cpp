// The windows' asks, applied at the start of a tick (sim/command.h). Each runs the realm's own
// method for it -- the rules are where they were -- and its answer is said as What::Answered, in
// the order the asks came. server/README.md phase 1.
#include "sim/realm.h"
#include "sim/broker.h"

#include "sim/cradle.h"

#include <algorithm>
#include <cstdint>

namespace mu::sim {

namespace {

// **Part of a stack** (the user, 2026-10-20: "we need stacking jewels", with Shift-click taking
// some off): a window's move, deposit, put-in and the rest carry how many in `c`, and the realm
// splits those off into a free cell of the same grid first, so the method runs on a whole thing
// as it always has. `c` of -1, 0 or the whole stack is the whole stack, as before. Ours.
//
// The cell the part went to; -1 when nothing was split (not a stack, or all of it); -2 when the
// grid has no free cell to split it into.
template <class Grid>
int splitOff(const content::Tables& tables, Grid& grid, int at, int pieces) {
    const Held whole = grid[at];
    if (whole.empty() || size_t(whole.item) >= tables.items.size()) return -1;
    const content::ItemRow& row = tables.items[size_t(whole.item)];
    if (poursMost(row) <= 0 || ammunition(row) || pieces <= 0 || pieces >= whole.durability) return -1;
    const int cell = grid.free(tables, row.width, row.height);
    if (cell < 0) return -2;
    Held part = whole, left = whole;
    part.durability = int16_t(pieces);
    left.durability = int16_t(whole.durability - pieces);
    grid.put(at, left);
    grid.put(cell, part);
    return cell;
}

// A refused part poured back onto the stack it came off.
template <class Grid>
void joinBack(const content::Tables& tables, Grid& grid, int at, int cell) {
    if (grid[cell].empty()) return;
    const int went = topUp(tables, grid, at, grid[cell]);
    Held left = grid[cell];
    left.durability = int16_t(left.durability - went);
    if (left.durability <= 0) grid.lift(cell);
    else grid.put(cell, left);
}

}  // namespace

void Realm::restoreKept(const Kept& kept) {
    restore(kept.hero);
    me().vault = kept.vault;
    me().machine = kept.machine;
}

Kept Realm::keptOf(uint32_t id) {
    Kept out;
    const int at = playerOfId(id);
    if (at < 0) return out;
    For him(*this, size_t(at));
    out.hero = record();
    out.vault = me().vault;
    out.machine = me().machine;
    return out;
}

int64_t Realm::heldOf(uint32_t id) const {
    const int at = playerOfId(id);
    return at < 0 ? -1 : heroes_[size_t(at)].money + heroes_[size_t(at)].vault.zen();
}

void Realm::applyCommands() {
    if (commands_.empty()) {
        carried_.clear();
        return;
    }
    // Taken out first: a method below may say happenings, never ask, but the list is not walked
    // while anything could append to it.
    std::vector<Command> asked;
    asked.swap(commands_);
    for (const Command& one : asked) {
        // For the player who asked: 0 is the first, as it was the one. An id that is no
        // player's -- one who left, a stale ask -- is dropped unanswered.
        const int asker = one.player == 0 ? 0 : playerOfId(one.player);
        if (asker < 0 || (one.kind != Command::Kind::Join && bodies_[heroes_[size_t(asker)].body].gone)) {
            continue;
        }
        For his(*this, size_t(asker));
        // Who the answer is said on: the asker, or the one a Join let in.
        uint32_t answerer = mine().id;
        int64_t answer = -1;
        using Kind = Command::Kind;
        Kind said = one.kind;
        float burstX = 0.0f, burstY = 0.0f;
        // A part of a stack, split off first (splitOff): `from` is the grid it comes out of, 0 the
        // bag, 1 the vault, 2 the machine's box; `same` a move inside that grid.
        Command run = one;
        int from = -1, partOf = -1, part = -1;
        bool same = false;
        if (tables_ && one.c > 0) {
            switch (one.kind) {
                case Kind::Move: from = 0, same = true; break;
                case Kind::Rearrange: from = 1, same = true; break;
                case Kind::Shuffle: from = 2, same = true; break;
                case Kind::Sell:
                case Kind::Deposit:
                case Kind::PutIn:
                case Kind::DealPut: from = 0; break;
                case Kind::Discard: from = one.b == 1 ? 1 : 0; break;
                case Kind::Withdraw: from = 1; break;
                case Kind::TakeOut: from = 2; break;
                default: break;
            }
            const bool inBag = from != 0 || baggable(one.a);
            const int limit = from == 0 ? kSlots : from == 1 ? kVaultCells : kMachineCells;
            if (from >= 0 && inBag && one.a >= 0 && one.a < limit) {
                part = from == 0   ? splitOff(*tables_, me().bag, one.a, one.c)
                       : from == 1 ? splitOff(*tables_, me().vault, one.a, one.c)
                                   : splitOff(*tables_, me().machine, one.a, one.c);
                if (part == -2) {
                    refusal_ = "no free cell to split the stack into";
                    if (one.ticket != 0) say(What::Answered, mine(), int32_t(said), -1, int32_t(one.ticket));
                    continue;
                }
                if (part >= 0) {
                    partOf = one.a;
                    run.a = part;
                }
            }
        }
        // Let go on the very cell the part was split into: it is there already.
        if (same && part >= 0 && part == one.b) {
            if (one.ticket != 0) say(What::Answered, mine(), int32_t(said), 1, int32_t(one.ticket));
            continue;
        }
        switch (run.kind) {
            case Kind::None: continue;
            case Kind::Buy: answer = buy(one.a); break;
            case Kind::Sell: answer = sellItem(run.a); break;
            case Kind::BuyBack: answer = buyBack(); break;
            case Kind::Repair: answer = repair(one.a) ? 1 : -1; break;
            case Kind::RepairAll: {
                const int mended = repairAll();
                answer = mended > 0 ? mended : -1;
                break;
            }
            case Kind::Deposit: answer = deposit(run.a, one.b); break;
            case Kind::Withdraw: answer = withdraw(run.a, one.b); break;
            case Kind::Rearrange: answer = rearrange(run.a, one.b) ? 1 : -1; break;
            case Kind::DepositZen: answer = depositZen(one.zen) ? 1 : -1; break;
            case Kind::WithdrawZen: answer = withdrawZen(one.zen) ? 1 : -1; break;
            case Kind::PutIn: answer = putIn(run.a, one.b); break;
            case Kind::TakeOut: answer = takeOut(run.a, one.b); break;
            case Kind::Shuffle: answer = shuffle(run.a, one.b) ? 1 : -1; break;
            case Kind::Mix: answer = mix(one.service, one.a) ? 1 : -1; break;
            case Kind::Move: answer = moveItem(run.a, one.b) ? 1 : -1; break;
            case Kind::Use: answer = useItem(one.a) ? 1 : -1; break;
            case Kind::Refine: answer = refine(one.a, one.b) ? 1 : -1; break;
            case Kind::Discard:
            case Kind::Crack:
                if (one.b == 1) {
                    const uint32_t thrown = discardFromVault(run.a);
                    answer = thrown != 0 ? int64_t(thrown) : -1;
                } else if (cracks(run.a)) {
                    const Cracked cracked = crack(run.a);
                    said = Kind::Crack;
                    answer = cracked.opened ? 1 : -1;
                    burstX = float(cracked.column);
                    burstY = float(cracked.row);
                } else {
                    const uint32_t thrown = discard(run.a);
                    answer = thrown != 0 ? int64_t(thrown) : -1;
                }
                break;
            case Kind::Spend: {
                // b: how many, 1 when not said (a client before protocol 43).
                const int many = std::max(1, one.b);
                answer = spend(one.a == 0 ? many : 0, one.a == 1 ? many : 0, one.a == 2 ? many : 0,
                               one.a == 3 ? many : 0)
                             ? 1
                             : -1;
                break;
            }
            case Kind::ResetPoints: answer = int64_t(resetPoints()); break;
            case Kind::JewelRecut: answer = recut(one.a, one.b); break;
            case Kind::JewelSalvage: answer = salvage(one.a); break;
            case Kind::JewelCombine: answer = combine(one.a, one.b, one.c); break;
            case Kind::AcceptQuest: answer = acceptQuest(one.a) ? 1 : -1; break;
            case Kind::CompleteQuest:
                answer = completeQuest(one.a, one.b, QuestPath(std::max(0, one.c))) ? 1 : -1;
                break;
            case Kind::Travel: answer = travel(one.a) ? 1 : -1; break;
            case Kind::GoBack: answer = goBack() ? 1 : -1; break;
            case Kind::Order: {
                Request request;
                request.kind = Request::Kind(one.a);
                request.column = one.b;
                request.row = one.c;
                request.skill = one.d;
                request.target = one.target;
                ask(request);
                answer = 1;
                break;
            }
            case Kind::Cast: invoke(one.a, one.target); answer = 1; break;
            case Kind::CastAt: invokeAt(one.a, one.b, one.c); answer = 1; break;
            case Kind::LetGo: letGo(); answer = 1; break;
            case Kind::Close:
                switch (Command::Window(one.a)) {
                    case Command::Window::Trade: closeTrade(); break;
                    case Command::Window::Vault: closeVault(); break;
                    case Command::Window::Machine: closeMachine(); break;
                    case Command::Window::Quest: closeQuest(); break;
                    case Command::Window::Gate: closeGate(); break;
                    case Command::Window::Angel: closeAngel(); break;
                    case Command::Window::Deal: closeDeal(); break;
                    case Command::Window::Broker: closeBroker(); break;
                    case Command::Window::Jeweler: closeJeweler(); break;
                }
                answer = 1;
                break;
            case Kind::EnterCastle: answer = enterCastle(one.a) ? 1 : -1; break;
            case Kind::HandInStaff: answer = handInStaff() ? 1 : -1; break;
            case Kind::ClaimCastle: answer = claimCastle() ? 1 : -1; break;
            case Kind::ClaimSquare: answer = claimSquare() ? 1 : -1; break;
            case Kind::EnterSquare: answer = enterSquare(one.a) ? 1 : -1; break;
            case Kind::AutoDrink: setAutoDrink(one.a, one.b); answer = 1; break;
            case Kind::Join: {
                const uint32_t id = join(Kin(std::clamp(one.a, 0, int(Kin::MagicGladiator))),
                                         std::clamp(one.b, 1, kMaximumLevel), one.c, one.d);
                answer = id != 0 ? int64_t(id) : -1;
                if (id != 0) {
                    // His hands, as sim::outfit fills the first's.
                    For him(*this, size_t(playerOfId(id)));
                    outfitArms(*this, int32_t(one.target) - 1, int32_t(one.zen) - 1);
                    // And all of him, when he comes from another world.
                    for (const Carried& carried : carried_) {
                        if (carried.ticket == one.ticket) restoreKept(carried.kept);
                    }
                    answerer = id;
                }
                break;
            }
            case Kind::Leave: answer = depart(mine().id) ? 1 : -1; break;
            case Kind::PartySet: answer = setParty(one.target, uint32_t(std::max(0, one.a))) ? 1 : -1; break;
            case Kind::DealAsk: answer = dealAsk(one.target) ? 1 : -1; break;
            case Kind::DealAnswer: answer = dealAnswer(one.target, one.a == 1) ? 1 : -1; break;
            case Kind::DealPut: answer = dealPut(run.a, one.b); break;
            case Kind::DealTake: answer = dealTake(one.a, one.b); break;
            case Kind::DealZen: answer = dealZen(one.zen) ? 1 : -1; break;
            case Kind::DealAccept: answer = dealAccept(one.a == 1) ? 1 : -1; break;
            case Kind::BrokerTake:
                answer = brokerTake(int(one.zen & 0xffff), (one.zen & 0x10000) != 0, unpackHeld(one));
                break;
            case Kind::BrokerBuy:
                answer = brokerBuy(unpackHeld(one), one.zen, (one.target & 0x80000000u) != 0);
                break;
            case Kind::BrokerGive: answer = brokerGive(unpackHeld(one)); break;
            case Kind::BrokerZen: answer = brokerZen(one.zen); break;
        }
        // Refused, the part goes back onto its stack.
        if (part >= 0 && answer < 0) {
            if (from == 0) joinBack(*tables_, me().bag, partOf, part);
            else if (from == 1) joinBack(*tables_, me().vault, partOf, part);
            else joinBack(*tables_, me().machine, partOf, part);
        }
        if (one.ticket == 0) continue;
        say(What::Answered, *find(answerer), int32_t(said),
            answer < 0 ? -1 : int32_t(std::min<int64_t>(answer, INT32_MAX)), int32_t(one.ticket));
        if (said == Kind::Crack) {
            happenings_.back().x = burstX;
            happenings_.back().y = burstY;
        }
    }
    carried_.clear();
}

// Out of realm.h, unchanged, to pay the size ratchet for the bleed (2026-10-12).
Realm::Pouring Realm::pouring(bool mana) const {
    Pouring out;
    for (int i = 0; i < me().sipCount; ++i) {
        const Sip& one = me().sips[i];
        if (!one.drunk || one.mana != mana) continue;
        out.left = std::max(out.left, one.due - tick_);
        out.amount += one.amount;
    }
    return out;
}

// Out of realm.h, unchanged, to pay the size ratchet for the bleed (2026-10-12).
void Realm::smite(uint32_t id) {
    const Body* found = find(id);
    if (found == nullptr || !found->monster() || !found->alive()) return;
    Body& beast = *const_cast<Body*>(found);
    beast.health = 0;
    kill(beast, mine());
}

}  // namespace mu::sim
