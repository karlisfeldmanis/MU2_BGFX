#pragma once

// What a player asks of the realm that is not an order to walk or fight: a purchase, a vault
// move, the Chaos Machine. docs/server-plan.md phase 1: every such ask is a Command, queued with
// Realm::command and applied at the start of the next tick in the order it came, and its answer
// is a happening (What::Answered) and not a return value. That is the shape a wire carries --
// the client sends the command and hears the answer a round trip later -- and it is MU's own:
// its shop and vault windows wait for the server's reply. Between ticks nothing is decided, so
// no dice are rolled outside the tick either.
//
// Moved over a window at a time; the realm's direct methods stay for the tests and tools until
// the last caller in game/ is gone.

#include <cstdint>

#include "sim/machine.h"

namespace mu::sim {

struct Command {
    enum class Kind : uint8_t {
        None,
        // The merchant's counter (Realm::buy, sellItem, buyBack, repair, repairAll).
        Buy,        // a: the shelf slot
        Sell,       // a: the bag slot
        BuyBack,
        Repair,     // a: the slot
        RepairAll,
        // The vault (Realm::deposit, withdraw, rearrange, depositZen, withdrawZen).
        Deposit,    // a: the bag slot, b: the vault cell or -1 for the first it fits
        Withdraw,   // a: the vault cell, b: the bag slot or -1
        Rearrange,  // a: from, b: to, both vault cells
        DepositZen, // zen
        WithdrawZen,// zen
        // The Chaos Machine (Realm::putIn, takeOut, shuffle, mix).
        PutIn,      // a: the bag slot, b: the box cell or -1
        TakeOut,    // a: the box cell, b: the bag slot or -1
        Shuffle,    // a: from, b: to, both box cells
        Mix,        // service, a: the socket or -1
    };
    Kind kind = Kind::None;
    // Who asks: a player's body id. 0 is the one hero this realm has until phase 2.
    uint32_t player = 0;
    // The asker's own number for it, handed back in the answer so its window knows which ask
    // was answered. The realm reads nothing into it.
    uint32_t ticket = 0;
    int32_t a = -1, b = -1;
    int64_t zen = 0;
    Service service = Service::Combine;
};

}  // namespace mu::sim
