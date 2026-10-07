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
        Kind said = one.kind;
        float burstX = 0.0f, burstY = 0.0f;
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
            case Kind::Move: answer = moveItem(one.a, one.b) ? 1 : -1; break;
            case Kind::Use: answer = useItem(one.a) ? 1 : -1; break;
            case Kind::Refine: answer = refine(one.a, one.b) ? 1 : -1; break;
            case Kind::Discard:
            case Kind::Crack:
                if (cracks(one.a)) {
                    const Cracked cracked = crack(one.a);
                    said = Kind::Crack;
                    answer = cracked.opened ? 1 : -1;
                    burstX = float(cracked.column);
                    burstY = float(cracked.row);
                } else {
                    const uint32_t thrown = discard(one.a);
                    answer = thrown != 0 ? int64_t(thrown) : -1;
                }
                break;
            case Kind::Spend:
                answer = spend(one.a == 0, one.a == 1, one.a == 2, one.a == 3) ? 1 : -1;
                break;
            case Kind::AcceptQuest: answer = acceptQuest(one.a) ? 1 : -1; break;
            case Kind::CompleteQuest:
                answer = completeQuest(one.a, one.b, QuestPath(std::max(0, one.c))) ? 1 : -1;
                break;
            case Kind::Travel: answer = travel(one.a) ? 1 : -1; break;
            case Kind::GoBack:
                setHeroDown(one.a, one.b, one.c, one.d);
                answer = 1;
                break;
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
                }
                answer = 1;
                break;
            case Kind::EnterCastle: answer = enterCastle(one.a) ? 1 : -1; break;
            case Kind::HandInStaff: answer = handInStaff() ? 1 : -1; break;
            case Kind::ClaimCastle: answer = claimCastle() ? 1 : -1; break;
        }
        if (one.ticket == 0) continue;
        say(What::Answered, bodies_[0], int32_t(said),
            answer < 0 ? -1 : int32_t(std::min<int64_t>(answer, INT32_MAX)), int32_t(one.ticket));
        if (said == Kind::Crack) {
            happenings_.back().x = burstX;
            happenings_.back().y = burstY;
        }
    }
}

}  // namespace mu::sim
