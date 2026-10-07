# The seam

Server-plan phase 1 (docs/server-plan.md §4), started 2026-10-07 on the user's word: *"we need
to start to build server side for our game and move server to hetzner"*, *"do it by phases"*,
*"contino"*. Sprint 16 laid the foundations the same day.

**The goal: `game/` never sees a `Realm`.** It sends commands and reads a client-side view;
the realm decides everything at the start of a tick. Single player keeps working after every
batch: the realm still sits in the process, behind what will be phase 1's `LocalLink`.

## The batches, in order

1. **Commands for the counter, the vault and the machine.** Done 2026-10-07, below.
2. Commands for the bag: `moveItem`, `useItem`, `refine`, `crack`, `discard`, `spend`. Done 2026-10-07, below.
3. Commands for quests, travel and Go Back!. Done 2026-10-07, below. The debug switches (`give`,
   `lay`, castle, …) stay direct until phase 6 makes them GM commands: they run once at start.
4. The `Link` interface (`send`, `step`, `happenings`, `realm`) and `LocalLink` around the realm.
   Done 2026-10-07, below.
5. ~~The `View`: Play and the twelve UI files read it, not the realm (~670 call sites).~~ **The
   mirror and its gate**, done 2026-10-07, below: the client reads a `const sim::Realm` the link
   keeps, and layercheck refuses a writable one.
6. Figures matched to bodies by id, not index; then `Realm::despawn` (sprint 16 step 4's rest).
7. ~~`layercheck`'s new arrow~~: folded into 5.

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

## 2. The bag — done

- Six more kinds: `Move`, `Use`, `Refine`, `Discard`, `Spend`, and `Crack` -- never sent, only
  answered: the realm decides whether a thrown thing is laid down or opened (`Realm::cracks`),
  where the client decided before, and says which; a Crack's answer carries the burst's tile in
  `x` and `y`.
- `Play::Asked` keeps what an answer needs from before the command ran: the thing used or
  thrown, where he stood (Go Back! after a Town Portal), the swing (the Ale's log), the thing
  before its jewel, whether a slot was worn, whether a box opens with no firework.
- The step loop notes the hero's `Drank`, `Warped` and `Cracked` just before each answer
  (`lastDrank_`, ...), cleared on each answer and each step; `answered()` reads them for the
  potion's lane, the scroll home and the firework's payload. The hero's own `Dropped` no longer
  goes to `held_` (his throw is landed by the answer, not hidden behind a death).
- Desk: the bag's moves, jewels, uses, throws and the potion keys wait on tickets; a potion key's
  box is struck on the yes (`Then::Strike`).
- sim_test 6601 checks, the standing 10 failing; `testCommands` adds a potion used, a Firecracker
  opened and answered as a Crack after its Cracked, and an empty slot refused as a Discard. A
  muted run is clean. **For the user to try:** drag and equip, potions by click and by key, a
  jewel on a thing, a Firecracker and a box thrown, a stat point spent.

## 3. The giver and the road — done

- `AcceptQuest`, `CompleteQuest` (with the reward and the QuestPath), `Travel` and `GoBack`.
  `Command` grew `c` and `d` for them.
- An in-map trip and Go Back! say `Climbed` inside the tick now, so the step's own branch draws
  the landing and the two `warped()` calls that stood in for it are gone. Another map's trip
  still hands the mode `travelled_`.
- A quest's levels are still shown one by one: the step counts the hero's `Levelled` since the
  last answer and the rises owed before them, and the hand-in's answer owes them all.
- The travel log's reasons gained `Quest`, which had no line and read past the end of the table.
- Desk: the giver's dialog shuts on a yes (`Then::CloseQuest`), the travel list on a paid trip
  (`Then::Travel`); the press is the list's either way.
- sim_test 6606 checks, the standing 10 failing: Go Back! sets him down with its Climbed first, a
  quest with no dialog open and a trip never opened are refused. A muted run is clean.

## 4. The Link — done

- `game/link.h`: `Link` is `send(Command)`, `step()`, `happenings()` and `realm()` (what there is
  to see; batch 5 puts the View there). `LocalLink` holds the realm, in process.
- **Play no longer holds a writable realm.** Its `realm_` is a const reference to the link's,
  so the compiler found every write: 37 of them.
  - The player's own became commands: walk, attack, talk, pick and perch (`Order`), skills
    (`Cast`, `CastAt`, `LetGo`), every window walked away from (`Close` with a `Window`), and
    Blood Castle's three (`EnterCastle`, `HandInStaff`, `ClaimCastle`, answered; the desk clicks
    or refuses, and shuts the Archangel's page on a claim). Orders and casts go with ticket 0
    and are never answered: the walk and the swing are the answer.
  - What will be the server's goes through `local_` (`LocalLink::local()`), each a line to move:
    raise and configure, the raid's setup, the roads, the arena's set-up (`spend`, `equip`,
    `learn`, `undying`, `wingDemo`), saves (`restore`, `restoreVault`, `restoreMachine`), the
    wall clock, the weather's rain, and the GM switches (`give`, `lay`, `earn`, `invade`, the
    castle's door, bridge and garrison, `raidSkipTo`, the sweep's `setDown`).
- `Realm::takeJeweled()`, a flag the client read and cleared, is no longer read: the vault's and
  the machine's jewel ring comes off the `Refined` said before the answer.
- sim_test as before (6606, the standing 10). A muted `--talk Lumen` run walks there by an Order
  command and is served.

## 5. The mirror, and the gate — done

**A change to the plan.** The client reads 86 Realm methods (667 calls), and many are not data
but rule queries over the realm's state: `questOffered`, `judged` (the machine's odds),
`repairCost`, `travelRefusal`, `knows`, `router`, `hazards`, the quest goals. A View of its own
would have to answer those with a second copy of the rules in the client, which is the mistake
server-plan §3 lists first. So the View is a **mirror realm**: the client reads a
`const sim::Realm&` that the link keeps -- LocalLink's is the realm itself; RemoteLink's will be a
`sim::Realm` the server's stream fills, running the same const queries. MU2's `Mirror` was this.
The reads stay where they are; phase 4 decides which of the realm's state the stream carries,
and the list above is where it starts.

**The gate:** `tools/layercheck.py` refuses a writable `sim::Realm` -- a reference, a pointer or
one of its own -- anywhere in `game/` or `app/`, except `game/link.h`, Play's `local_` and
`game/headless.cpp` (the scripted hand, which plays the server's part). It passes today; a probe
with `mu::sim::Realm&`, `sim::Realm*` and `sim::Realm mine` is refused and both `const` forms are
not.
