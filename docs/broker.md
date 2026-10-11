# The Broker: the market

A WoW-style auction house for MU2. A Broker stands in Lorencia, Devias and Noria, and all three
open one market. Players and bots list things from the bag or the vault at a buyout price, and
buy what others list. Designed on the canvas https://claude.ai/artifact/1f7nUNsQRELX6BMbnf84Bk
(2026-10-11); the user's rulings are below and are not to be re-argued.

## The rules (the user, 2026-10-11)

| | ruling |
|---|---|
| where | only at a Broker; no browsing from afar |
| who | players and bots alike; bots may price up to 5% under the cheapest |
| fee | none: no commission, no deposit |
| price | from the thing's NPC sale value up to 2,000,000,000 |
| what | gear, jewels, runes, wings, pets, event tickets and parts; **not** potions, ammo, quest things, anything bound (`tradeable()`), anything not mended |
| how long | 12, 24 or 48 hours, counted only while the server runs |
| how many | 20 listings an account, shared by its characters (a bot: by its character) |
| own | a character may not buy a listing of his own account's |
| change | a price may be lowered for nothing; raising it is cancel and list again |
| pay with | carried Zen first, then the vault's |
| sold | the money waits in the seller's **Collect**; a chat line tells him if he is on |
| Collect | per character, 30 days and then gone; unsold and cancelled things come back there |
| a buy | a player's into his vault, his bag if the vault is full, else his Collect (the user, 2026-10-11); a bot's into his bag, else his Collect |
| seller | named on every listing, with whether he is on; whisper him from the card |
| level | none |

## The shape

Three layers, each the one the rest of the game already uses for the same kind of thing:

1. **`sim/broker.{h,cpp}` -- the book.** The listings and every Collect box, and the rules over
   them: list, buy, cancel, lower, expire, collect. Pure data with no realm in it, so `sim_test`
   proves it and the server and a local harness can both hold one. Written to bytes and read
   back (`Book::write`, `Book::read`).
2. **The realm moves the things.** A listing leaves a bag and a purchase enters one inside the
   lockstep, so every mirror sees it: four commands **sent by the server and never by a client**,
   as `PartySet` is. A whole `Held` rides in a command's a, b, c, d and target (`sim::packHeld`).
   - `BrokerTake` -- out of his bag or vault, only if the slot still holds exactly that thing
     (`a` its slot, `b` 1 for the vault, `c` how many of a stack; the Held to match). Answers
     how many came out.
   - `BrokerBuy` -- pays `zen` (carried first, then his vault's) and puts the Held in his bag,
     or, with `target`'s top bit (every player's buy, not a bot's), in his vault. Answers 1 in
     the bag (or the vault), 3 in the bag because the vault was full, 2 paid but no room (the
     server puts it in his Collect), -1 he cannot pay.
   - `BrokerGive` -- a thing out of Collect into his bag: 1, or -1 when it does not fit.
   - `BrokerZen` -- Zen out of Collect: answers what he could carry.
3. **`server/src/broker.cpp` -- the service.** Like the party: it spans maps, so it is the
   server's, kept in `characters.db` beside the characters. It hears `BrokerAsk` frames, checks
   the asker stands at a Broker (`sim::atBroker`), queues the realm commands above with its own
   tickets, finishes each when its tick answers, and sends `BrokerView` frames back. It saves the
   book on every change and the character with it (`Session::keepAt`), so a crash cannot leave a
   thing in two places.

The client: `Link::askBroker`/`takeBrokerViews` (RemoteLink over the wire; LocalLink answers
"the market is on the server"), `Play` passes them through, and `game/ui/broker.{h,cpp}` is the
window: three columns wide (618 x 429 units), at the vault's place with the bag beside it,
Buy / Sell / My listings.

### Wire (protocol 25)

- `Kind::BrokerAsk` up: Browse (a page: category, search, usable-by class, flags, sort), List
  (from bag/vault, slot, count, unit price, hours), Buy (listing, count), Cancel, Lower
  (listing, new unit price), Collect, Mine.
- `Kind::BrokerView` down: a page of listings, his listings, his Collect, his account's count,
  and an answer to his last ask (done, or why not).
- `Command::Kind::BrokerTake`, `BrokerBuy`, `BrokerGive`, `BrokerZen`.

### Sounds

`window_open` when it opens, `window_click` on tabs and rows, `window_refused` on a refusal,
`item_get` when a buy lands, `jewel_get` for a jewel, `money_drop` with every buy, listing,
cancel and collect (the user, 2026-10-11), and a new `broker_sold`, the coin sound, for "your thing sold".

## Phases

1. The book and its tests; the realm's four commands. (`sim_test`) -- **done**
2. The wire and the server's service; a netbot or a test client through it. -- **done**
3. The window, the NPCs in three cities, the sounds; shot in game. -- **done**, the user's look owed
4. The bots list their spares and buy their upgrades through it. -- **done** (tools/netbot/errands.h)

## Where it stands (2026-10-11, end of the second session; uncommitted)

Files, by layer:

- sim: `sim/broker.{h,cpp}` (the book, the rules, `npcValue`, `brokerable`, `packHeld`, the search),
  `sim/realm_broker.cpp` (the four commands, `atBroker`, `satchelOf`, `vaultOf`),
  `Realm::talk` moved to `sim/realm_folk.cpp` with the Broker's branch, `Player::brokering`
  (snapshot shape 10), `Command::Kind::BrokerTake..BrokerZen`, `Window::Broker`.
  `Realm::sellValue` now calls `sim::npcValue`.
- net: `net/broker_wire.{h,cpp}`; `wire.h` protocol 25, `Kind::BrokerAsk = 17`, `BrokerView = 18`.
- client: `Link::askBroker` / `takeBrokerView` (local play answers Closed), `RemoteLink`,
  `Play::askBroker`, `takeBrokerView`, `closeBroker`.
- server: `server/src/broker.{h,cpp}` (the service), `Store::keepBook` / `findBook` (a `broker`
  table in characters.db), hooks in `main.cpp` (dispatch, `brokerTick` each second, `brokerKeep`
  in the save pass, `--market-sample N`) and `world.cpp` (`brokerHeard` after each tick).
- window: `game/ui/broker.{h,cpp}` (asks, answers, layout, pointer, keys), `broker_draw.cpp`,
  `broker_layout.h`; the desk opens it as the vault, `Desk::dealNews` moved to
  `game/ui/desk_deal.cpp` to pay for the lines.
- NPCs: npc 900 rows in `source/world/{lorencia,devias,noria}/world.json` "npcs" (Orsin, Kesra,
  Liane), figure `mirazu` (MuMain's Mirage, `source/npc/mirazu.json`), placed by
  `source/world/*/placements.json` so the figure cook takes him.
- tests: `tests/sim_test_broker.cpp` (in sim_test), Devias's townsfolk count raised to 13.
- MU's scale for a whole-model townsperson: the recipe's `scale` (mirazu 0.95,
  w_CursedTemple.cpp:212) rides index.py's npc row and the cook's standalone entry into
  `FigureBody::scale`, which `Play` already stands every townsperson at.
- the Watch list (the design's star): `Book::watch` keeps a character's watches with the book
  (shape 2), a gear piece by its listing and alike things by `Book::watchItem(item)`, at most
  `kBrokerWatched` (40). `BrokerWhat::Watch` toggles one and answers the page again; the query's
  `watchedOnly` is the rail's "Watched" (the server fills `watcher` from the session), rows carry
  `BrokerRow::Watched`, and `kinds` carries one more count after the categories: how many he
  watches. The card's Watch pill sits under the name.

- the bots (phase 4): `tools/netbot/errands.h`, beside the Market (moved out of netbot.cpp
  to `market.h` to pay for the lines). Every twenty game minutes a bot on a map with a Broker,
  with spares or at least 100,000 Zen, walks to a tile beside him (`NetBot::errand`, the
  brain's meeting), Looks, takes in his Collect, lists `Bot::spares` up to 3 listings for 24
  hours at up to 5% under the cheapest (a step of 0-5% his own, by his name; else the last sale,
  else four times the NPC's; never under it), then looks through two pages usable by his class
  and buys the one with the most `Bot::worth` that costs at most two thirds of his Zen. Its
  deadline is the wall's (30 s): at `--speed 100` a game minute passes in 0.6 s. `--no-trades`
  turns it off with the Market.
- the server's `--kit` takes `Zen+N` (a test character's purse).

Checked (2026-10-11, second session): sim_test 7441 checks, 0 failed (the watches among them);
server_test 27/0; layercheck; sizecheck (only another session's bag.cpp). In game against a
private server (`mu2_server --port 44511 --store <scratch>/market.db --market-sample 60 --kit
"60:Zen+50000000,Jewel01,Jewel01,Jewel02,Sword01+5"`, clients `--world lorencia --play --server
127.0.0.1:44511 --talk Orsin --mute --save <scratch>/demo.json --ui-click F:X:Y ...`):
- the row pictures draw (the shelf stage);
- Buy: a ticket, a gear piece, a Jewel of Soul from its group; the confirm at 2,554,500
  ("Confirm 2,554,500", the second press buys); item_get, jewel_get, window_click;
- Watch: the pill turns to Watching, the row gets the gold star, "Watched 1", and the Watched
  category shows that one listing;
- Sell: a Bless listed for 48 hours (item_drop); a Soul at 6,990,099,999 refused ("Not at that
  price", window_refused); My listings: a raised price refused ("A price can only be lowered"),
  Cancel into Collect, Collect all (item_get for a thing, money_drop for Zen);
- a second client bought the seller's Bless; the seller's "Sold lately" and Collect showed
  133,200, and after the server stopped characters.db held exactly the Zen the windows did
  (47,508,635 and 49,866,800); no divergence in any client;
- fixed on the way: while a Broker box typed, the desk took the pointer from every window, the
  Broker's own too, so a typed price could never be listed (desk.cpp hands it the real one);
- Mirazu shot three tiles east of Baz, in all three towns' cooked figures and tables;
- the bots on `mu2_server --port 44512 --speed 100 --market-sample 60 --kit
  "40:Zen+3000000,..."`: Hrothgar listed 3 and stopped there, a fresh knight listed Bronze Armor
  at 2,280 against 2,400 and bought a Hand Axe +9 for 209,000 (+9 to his score) on his next
  visit; no divergence, no server error.

Owed:
- selling from the vault was not clicked (a test character's vault is empty);
- a jewel buy of more than one (the sample holds one of each jewel): the book's fill is proved
  in sim_test, the window's quantity steps were pressed;
- deploying: protocol 25 means every client and the Hetzner server together, with the
  re-cooked tables of the three towns.
