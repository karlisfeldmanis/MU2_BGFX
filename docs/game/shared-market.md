# One market for every realm

The user, 2026-10-21: "make plan how you do that and implement". The Broker's market
(docs/game/broker.md) was one book a server: a player on North Europe never saw what was listed on
Central Europe. Now one server keeps the book and every other server is its hands.

## The split

The book was always bookkeeping only: every thing and every Zen that moves is a realm command on
the player's own server (`BrokerTake`, `BrokerBuy`, `BrokerGive`, `BrokerZen`). So the service
splits along that line:

- **The keeper** (`server/src/keeper.cpp`) holds the `sim::Book`, and with it what is taken out
  for a buy or a Collect and not yet settled (a *hold*), each seller's home realm, who is on in
  each realm, and the last durable ask it applied from each server. It answers asks and builds
  every `BrokerView`. Exactly one server runs it: `--market-serve PORT` (North Europe), or a
  server alone with no market flags, as before.
- **The hands** (`server/src/broker.cpp`), on every server: hear a `BrokerAsk`, check the Broker
  is near and the slot holds what he saw, queue the realm's commands, and talk to the keeper. On
  the keeper's own server they call it directly; elsewhere through the link.
- **The link** (`server/src/market_link.cpp`): one TCP line from each server to the keeper's,
  `--market-at HOST:PORT`, opened with a challenge and an HMAC of a shared key
  (`MU2_MARKET_KEY`, 64 hex digits) and refused when the two servers' item lists differ.

The client is unchanged but for one row flag, `Elsewhere`: a seller on another realm shows
"Another realm" where the online dot's word was and no Whisper button. No protocol bump.

## An ask, end to end

| ask | hands | keeper |
|---|---|---|
| Look, Browse, Watch, Quote, Cancel, Lower | pass it on | the view, as before |
| List | slot and price checked; `MayList`; `BrokerTake`; then **List** | 20 per account; listed |
| Buy | **Take**; `BrokerBuy` each; then **Settle** with each answer | listings out into a hold; settle: sold, or back |
| Collect | **TakeBox**; `BrokerGive`/`BrokerZen`; then **Return** what did not fit | parcels out into a hold; the rest back |

Bold asks change both sides. The rules that keep a thing from being lost or doubled when a
server stops at the wrong moment:

1. **A durable ask (List, Settle, Return, Merge) is sent only after its server has saved**, in
   the same pass as the character it moved things for. It waits in an outbox, kept in
   `characters.db`, until the keeper answers, and is sent again after a reconnect or a restart.
2. **The keeper answers a remote ask only after it has saved** the book, the holds and what it
   applied. A hold is never handed out before it is on the keeper's disk.
3. **A durable ask is applied once**: each server's asks are numbered in a stream of its own,
   and the keeper skips one numbered at or below the last it applied from that stream.
4. **A hold no server is working on goes back.** After each (re)connect a server replays its
   outbox and then says which holds it still works on (`Live`); the keeper puts every other hold
   of that realm back in the book or the Collect. The keeper's own holds go back when it starts.

## Moving to it

A server that kept a book of its own before sends it to the keeper once as a durable **Merge**:
its listings get new numbers, its Collects and sales join the keeper's, and every seller in it
is that realm's. Then its own book is emptied, in the same write as the outbox.

A character moved to another realm (docs/game/transfer.md) still may not have a listing or a
Collect waiting: the keeper sends each server its sellers' tokens (`Holders`), and the server
adds its own lists at once so the check is never behind.

## When the keeper is away

The market is closed on the other servers ("The market is closed for a moment.",
`BrokerNo::Closed`); everything else plays on. A server reconnects every 5 s.

## Deploying (not done)

1. Make a key: `openssl rand -hex 32`, put it as `MU2_MARKET_KEY=` in every box's
   `/etc/default/mu2-server`.
2. North Europe: `--market-serve 44410`. It listens on every address, as the game's port does,
   and lets in only a line that proves the key; but the line is not encrypted, so keep 44410
   closed in the firewall and let the other boxes reach it through their accounts tunnel with one
   more forward (`-L 44410:127.0.0.1:44410`, and `permitopen` for it on Helsinki's key).
3. The others: `--market-at 127.0.0.1:44410`. Each merges its book on its first connect.
4. Website and client need nothing.

## Tests

- `sim_test`: `Book::merge`.
- `server_test`: the keeper's holds, settle and return, a durable ask applied once, `Live`
  putting back what nobody works on, a book merged, the ledger read back after a restart, and the
  link's frames read back; then a real line to a listening keeper: refused without the key, let
  in with it, an answer held until the keeper saved, and a sale's news sent down the seller's line.
- Locally, 2026-10-21, two servers on private ports: the keeper with 60 sample listings and a
  second with 10 of its own, merged on its first connect. Bots on the second bought the keeper's
  listings (two after the same blade: the second told "Someone bought it first") and listed
  Guardian Angels, Imps and bows into the keeper's book. The keeper killed with -9 and started
  again: 70 listings read back, the second server back on its line in 5 s. Not seen: a bot buying
  across realms the other way (the bots would not buy what was listed), so the sale's news over a
  live pair of servers is proven by server_test's line alone.
