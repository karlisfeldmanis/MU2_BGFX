// The windows' asks, applied at the start of a tick (sim/command.h). Each runs the realm's own
// method for it -- the rules are where they were -- and its answer is said as What::Answered, in
// the order the asks came. docs/server-plan.md phase 1.
#include "sim/realm.h"

#include <algorithm>
#include <cstdint>

namespace mu::sim {

void Realm::applyCommands() {
    if (commands_.empty()) return;
    // Taken out first: a method below may say happenings, never ask, but the list is not walked
    // while anything could append to it.
    std::vector<Command> asked;
    asked.swap(commands_);
    for (const Command& one : asked) {
        int64_t answer = -1;
        using Kind = Command::Kind;
        switch (one.kind) {
            case Kind::None: continue;
            case Kind::Buy: answer = buy(one.a); break;
            case Kind::Sell: answer = sellItem(one.a); break;
            case Kind::BuyBack: answer = buyBack(); break;
            case Kind::Repair: answer = repair(one.a) ? 1 : -1; break;
            case Kind::RepairAll: {
                const int mended = repairAll();
                answer = mended > 0 ? mended : -1;
                break;
            }
            case Kind::Deposit: answer = deposit(one.a, one.b); break;
            case Kind::Withdraw: answer = withdraw(one.a, one.b); break;
            case Kind::Rearrange: answer = rearrange(one.a, one.b) ? 1 : -1; break;
            case Kind::DepositZen: answer = depositZen(one.zen) ? 1 : -1; break;
            case Kind::WithdrawZen: answer = withdrawZen(one.zen) ? 1 : -1; break;
            case Kind::PutIn: answer = putIn(one.a, one.b); break;
            case Kind::TakeOut: answer = takeOut(one.a, one.b); break;
            case Kind::Shuffle: answer = shuffle(one.a, one.b) ? 1 : -1; break;
            case Kind::Mix: answer = mix(one.service, one.a) ? 1 : -1; break;
        }
        say(What::Answered, bodies_[0], int32_t(one.kind),
            answer < 0 ? -1 : int32_t(std::min<int64_t>(answer, INT32_MAX)), int32_t(one.ticket));
    }
}

}  // namespace mu::sim
