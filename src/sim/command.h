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
        // The bag (Realm::moveItem, useItem, refine, discard, crack, spend).
        Move,       // a: from, b: to
        Use,        // a: the slot
        Refine,     // a: the jewel's slot, b: the thing's
        Discard,    // a: the slot, b: 1 for a vault cell. Thrown on the ground -- or, for a
                    // Firecracker or a box from the bag, opened (Realm::cracks), and then
                    // answered as Crack
        Crack,      // only ever an answer: a Discard that opened. The answer's x and y are the
                    // tile it burst over; the Cracked said before it has the rest
        Spend,      // a: 0 strength, 1 agility, 2 vitality, 3 energy
        // The quest giver's dialog and the road (Realm::acceptQuest, completeQuest, travel,
        // setHeroDown).
        AcceptQuest,    // a: the quest
        CompleteQuest,  // a: the quest, b: the reward chosen, c: the QuestPath
        Travel,         // a: the travel row
        GoBack,         // Go Back!: his way back taken (Realm::goBack); it carries nothing
                        // as dx and dy in hundredths
        // The player's orders and skills (Realm::ask, invoke, invokeAt, letGo). Sent with ticket
        // 0, as a click is: no answer is said, the walk and the swing are the answer.
        Order,      // a: the sim::Request::Kind, b: column, c: row, d: skill, target
        Cast,       // a: the skill, target: at whom, 0 for the standing fight's
        CastAt,     // a: the skill, b: column, c: row
        LetGo,      // a held charge let go (Nova)
        // A window walked away from (Realm::closeTrade, closeVault, closeMachine, closeQuest,
        // closeGate, closeAngel). a: the Window.
        Close,
        // Blood Castle's (Realm::enterCastle, handInStaff, claimCastle).
        EnterCastle,  // a: the castle
        HandInStaff,
        ClaimCastle,
        // The world's door (docs/sprints/19-many-heroes.md), sent by the server and never by a
        // client: a player in (Realm::join; a: the Kin, b: the level, c: column, d: row; target
        // and zen: his weapon's and shield's arm index plus one, 0 for none, put in his hands as
        // sim::outfit does; answered on HIM with his body id) or out (Realm::depart; `player` is
        // who leaves).
        Join,
        Leave,
        // **A body's party** (docs/party.md), sent by the server and never by a client, as the
        // door is: a the party's number, 0 for none; target the body. Parties are the server's,
        // since a party spans maps and a map is a realm (Realm::setParty).
        PartySet,
        // Devil Square's Claim on the result (Realm::claimSquare).
        ClaimSquare,
        // **A trade between two players** (Realm::dealAsk and the rest, sim/realm_deal.cpp): MU's
        // CNewUITrade, kept in the realm so every mirror sees both offers.
        DealAsk,     // target: the player asked
        DealAnswer,  // a: 1 yes, 0 no; target: who asked, 0 for whoever asked last
        DealPut,     // a: the bag slot, b: the box cell or -1 for the first it fits
        DealTake,    // a: the box cell, b: the bag slot or -1 for where it came from
        DealZen,     // zen: the whole sum offered, which replaces the last
        DealAccept,  // a: 1 accept, 0 take it back
        // Charon's Enter (Realm::enterSquare; a: the square).
        EnterSquare,
    };
    // The last kind there is: what a wire reads past it is None (net::takeCommand). Moved with
    // every kind added at the end.
    static constexpr Kind kLastKind = Kind::EnterSquare;
    // Close's a.
    // Trade is the merchant's counter, as the realm has always called it; Deal is a trade between
    // two players, which closing cancels for both.
    enum class Window : uint8_t { Trade, Vault, Machine, Quest, Gate, Angel, Deal };
    Kind kind = Kind::None;
    // Who asks: a player's body id. 0 is the one hero this realm has until phase 2.
    uint32_t player = 0;
    // The asker's own number for it, handed back in the answer so its window knows which ask
    // was answered. The realm reads nothing into it. 0 asks for no answer.
    uint32_t ticket = 0;
    int32_t a = -1, b = -1, c = -1, d = -1;
    uint32_t target = 0;
    int64_t zen = 0;
    Service service = Service::Combine;
};

}  // namespace mu::sim
