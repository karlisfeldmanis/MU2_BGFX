# The seam

Server-plan phase 1 (docs/server-plan.md §4), started 2026-10-07 on the user's word: *"we need
to start to build server side for our game and move server to hetzner"*, *"do it by phases"*,
*"contino"*. Sprint 16 laid the foundations the same day.

**The goal: `game/` never sees a `Realm`.** It sends commands and reads a client-side view;
the realm decides everything at the start of a tick. Single player keeps working after every
batch: the realm still sits in the process, behind what will be phase 1's `LocalLink`.

## The batches, in order

1. **Commands for the counter, the vault and the machine.** Done 2026-10-07, below.
2. Commands for the bag: `moveItem`, `useItem`, `refine`, `crack`, `discard`, `spend`.
3. Commands for quests, travel and the debug switches (`give`, `lay`, castle, …; a `gm` flag).
4. The `Link` interface (`send`, `happenings`, `self`) and `LocalLink` around the realm.
5. The `View`: Play and the twelve UI files read it, not the realm (~670 call sites, by file).
6. Figures matched to bodies by id, not index; then `Realm::despawn` (sprint 16 step 4's rest).
7. `layercheck`'s new arrow: `game/ui` and `game/fx` may not include `sim/realm.h`. The gate
   that says phase 1 is done.

## 1. The counter, the vault and the machine — done

- `sim::Command` (`sim/command.h`): fourteen kinds -- `Buy`, `Sell`, `BuyBack`, `Repair`,
  `RepairAll`, `Deposit`, `Withdraw`, `Rearrange`, `DepositZen`, `WithdrawZen`, `PutIn`,
  `TakeOut`, `Shuffle`, `Mix` -- with the asker's id and a ticket.
- `Realm::command()` queues; `step()` applies the queue first, in order (`realm_commands.cpp`),
  through the realm's own methods, and says `What::Answered` (a: kind, b: the method's answer or
  -1, c: ticket). The direct methods stay public for the tests and tools.
- `Play`'s fourteen requests send and return the ticket. `Play::answered()` makes the sound, the
  redress and the log when the answer comes, and keeps it in `answers()` for one frame.
- `Desk` waits on each ticket (`expect(ticket, Then)`) and hears its yes or no a tick later:
  took/refused, the mixer's spark, the number box shut or refused.
- `testCommands` in sim_test: nothing moves before the step; both asks answered in order with
  their tickets; once each. sim_test 6593 checks, the standing 10 failing. A muted 900-frame run
  is clean. **For the user to try in game:** buy, sell, buy back, repair, the vault's moves and
  coins, the machine's moves and a mix -- each now answers 50 ms later, as MU's server does.
