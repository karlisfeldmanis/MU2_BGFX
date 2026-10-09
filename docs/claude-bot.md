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

**Open (handed to the party's brain work, mu2-bgfx-64)**

- *Flipping between two quests on two maps.* From 4:07 to 4:35 the aim alternated between
  The White Silence (Devias) and The Catacombs (Lorencia), at 2000 zen per trip:
  Devias → Lorencia → Noria → Lorencia → Devias → Lorencia → Devias. `choose()` keeps a giver
  he set out for, but not a hunt against another hunt on a different map. A hunt under way
  should hold until its quest is done, or until its map has gone dry for a while.
- *A paid trip bought twice.* "pays 2000 zen to travel to Noria" at 4:15:06 and again at
  4:15:08, two seconds apart.
- *Bots never take an invite or answer a bot.* Tavik heard Claude's invite and whisper and did
  nothing. That's by design today (talk.h never answers a bot's whisper; only `--party` mates
  accept invites). A bot that accepts an invite from a nearby player or bot of its level band
  would let parties form on their own.
