# The Claude bot

The user, 2026-10-09: "lets create a claude bot witch is you", to test every system from a
player's seat and to find what the bots' brain should learn.

A character on the server played by Claude, the model. netbot's brain (tools/bot/brain.h) still
swings, drinks, shops and walks: a model's turn is seconds long and a fight is not. The model
takes the slow part: where he hunts, what he says, who he parties with. And it reads his log as
a player would, so the brain's mistakes show.

## Running it

```
mu2_server --port 44560 --store SCRATCH/characters.db --speed 100
netbot --server 127.0.0.1 --port 44560 --dir SCRATCH/bots --claude SCRATCH/claude Claude:dw Tavik:dk
```

`--claude DIR` makes the first bot named the Claude bot. Any others play as usual.

`--speed N` runs the server N times as fast. At 100, 50 game minutes take 30 s, with the server
at ~19% of a core and netbot at 15%, and no divergence. The clients step whatever ticks come.
Real-second timers stay in real seconds: invites expire after 60 s, a party member with no
connection is let go after 30 s, and a stalled line is cut.

Build these in a separate build directory (`cmake -B SCRATCH/build`): relinking `build/netbot`
kills any bots running from it.

## The files (tools/netbot/claude.h)

- **state.txt**, rewritten each game second: health, mana, zen, unspent points, where he is,
  the brain's aim, what he wears, potions, what is within 20 tiles (players by name, monsters
  by breed with their health), his quests, his party.
- **heard.log**: every chat line, with its channel and speaker. Each order is logged with
  its answer.
- **orders.txt**, appended by the model, one order per line:

| order | does |
|---|---|
| `say TEXT` | to his map |
| `party TEXT` | to his party |
| `whisper NAME TEXT` | to one player |
| `invite NAME`, `accept [NAME]`, `decline [NAME]`, `kick NAME`, `leave` | the party; no name takes the latest invite |
| `go WORLD` | grind WORLD over the brain's own choice, quests set aside (`Bot::order`) |
| `auto` | the brain chooses again |

## What playing found

The first runs, 2026-10-09, Claude (dw) and Tavik (dk), fresh, on a local server at 100×.

**Fixed**

- *A wizard sent for mana bought health.* `needsTown` sent him for mana once he had one
  bundle's zen. `buyPotions` then spent it on health first, he left with no mana, and the next
  decision sent him back: 11 mana trips in 4 game hours. He now buys the bundle he came for
  first (3d5a38d2). Result: 0 mana trips, 23 → 14 trips overall, level 59 → 69 at 4 h.

- *A paid trip bought twice.* A trip whose answer took longer than netbot's two-second
  `kAnswerWait` was decided again. The brain, still on the old map in the scratch, paid again,
  and the first answer's ticket had gone with the last decision's commands. The server charged
  both. Paid trips are now kept until answered (4e4cb791). Four bots over 8 game hours:
  74 double payments → 0.

**Seen, left as they are**

- *Flipping between two quests on two maps.* The White Silence (Devias) ↔ The Catacombs
  (Lorencia or the Dungeon): seven paid trips in 28 game minutes for one wizard, and 12-17 flips
  in 8 hours for the knight and the gladiator. Mostly fear: he dies to an Elite Yeti or Ice
  Queen, the brain "leaves them for 15 min", the quest is no longer huntable, and he goes to
  the other quest until the fear passes. A hold-and-restock change for the
  potion-short case (drinksThrough's floor of 20) fired twice in 8 hours and changed nothing
  measurable, so it was not kept. The trips cost little zen. The real cost is time, and the
  fix belongs to the threat and party work in `choose()`.
- *Bots never take an invite or answer a bot.* Tavik heard Claude's invite and whisper and did
  nothing. That's by design today (talk.h never answers a bot's whisper; only `--party` mates
  accept invites). A bot that accepts an invite from a nearby player or bot of its level band
  would let parties form on their own.

## How to judge a run

`python3 tools/netbot/judge.py NETBOT_LOG HOURS`. For
each bot, within the first HOURS game hours, it reports level, deaths, paid trips and their
zen, trips paid twice within 5 s, and hunt flips. Compare two builds of the same HEAD on two
servers, one class per bot, at most two server+netbot pairs at a time (about 6 of the Mac's 10
cores). With four bots, netbot lags a --speed 100 server by about a fifth, and a loaded Mac
skews deaths and levels.
