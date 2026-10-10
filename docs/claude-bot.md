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

- *A rich elf walked home for arrows every 15 minutes.* A quiver is 1000 shots and she stocked
  1500 at most. Over 100k zen she now stocks 4000 (04c94c8d). Offline, four 10-hour seeds:
  town trips 230 → 190, levels the same or higher.
- *A wizard paid to go and paid straight back.* The trip's price took his purse under
  `riskShare`'s "rich" line, so on landing the breed he came for was too dear. A map behind a
  paid trip is now weighed with the purse he lands with (7a67dd41). Six seeds: such round trips
  37 → 17, levels unchanged. That removes bouncing; it doesn't make him faster.
- *The server charged every repeated trip.* A client behind its server asked again before it
  saw the first answer: a gladiator paid 3000 zen every 10 seconds, 2.9M zen in 18 game hours.
  A connection the server has sent to another world now has its commands dropped (6db1c5be).
- *A late answer stranded a bot.* The earlier trip fix cleared its record when the wait ran
  out, so an answer 45 s late matched nothing and the bot stayed while the server held him due
  elsewhere. Trips are now kept until answered, and the wait is wall time (fbc40c4b). With
  both fixes, 4 game hours: 64 trips charged, 64 answered, every bot sent elsewhere arrived.

- *An elf's Frenzy made her flip maps until broke.* The brain timed fights on her swing as it
  stood, Frenzy stacks included; they fall away on a trip, so a quest looked easy from the
  Dungeon and too dear on landing. Fights are now timed on the steady swing (09fbec00). Four
  seeds: levels 149/174/151/184 → 166/201/172/183, none broke (two had been).
- *The wizard couldn't afford his spells.* He drank mana under 30% but cast his dear spells only
  over 50%, so he threw Fire Ball all day (24,000 casts; Meteorite, Ice and Flame none). He drinks
  under half now, and his points go to energy after his armour's strength (a7827d51). Four
  seeds: levels 670 → 747, deaths 157 → 121.
- *Elves and gladiators wouldn't farm where Life drops.* The fight-cost estimate counts a
  monster hitting for the whole kill, no range, no summon. Halved for the agility elf and the
  melee gladiator (6cbb3652): elves make wings in a day on all four seeds, gladiators +40 levels
  on average with fewer deaths. Not the knight (more deaths, no gain) or the wizard. Not the
  support elf either (mu2-bgfx-64, ce51dce1).
- *The wizard didn't know his area spells.* Evil Spirit, Flame, Meteorite's shower and
  Ice/Poison's splash were scored as single-target. Now counted as the realm resolves them, and
  Flame's force raised 1.0 → 1.5 at the user's ask (5cbb75a4). Deaths 174 → 96, levels the same.

**The wizard on strong ground: open, a balance question**

Every bot-side lever was tried and none moved the wizard onto level-50+ ground (2-8% of his
kills): five point builds, caution halved (2-3× the deaths), Evil Spirit 7-9 hours earlier from
the Catacombs (fewer levels, kept at the Halls), Magic Shield (already up ~90%). At level 120+ he
has 180-440 health and 70 defence against the knight's 1,075 and 130, and a level-52 Devias breed
is reckoned at ~700 health a kill. Whether a wizard should hold that ground alone is the user's
call: if yes, his survival (Magic Shield's share, health per level, how fast monsters reach a
caster) needs a balance pass; if not, mu2-bgfx-64's wing-farming parties carry him there.

Also seen: the new item content of 058adb75 (new sets and arms in the drop pool) costs the
wizard 15-30 levels a day: same binary, old tables 188/179/188/192, new 169/179/161/165, the
gear he finds falling from drop level 28-38 to 20-29. Passed to mu2-bgfx-64 for the content's
owner.

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

## Lessons from running it

- **Brain changes are judged offline.** `build/bot --kin K --seed N --hours 10` runs 10 game
  hours in about 25 s, against 3-5 minutes on a server at 100×. The server is for what only the
  network has: trips, reconnects, chat, party.
- **Four bots at --speed 100 lag one netbot** by 25-50 game seconds, and more when the Mac is
  loaded. That lag is what found the two trip bugs, but levels and deaths from a loaded run
  mean nothing. Check `uptime` first.
- **Never copy a binary over one that is running.** macOS kills the new launch. Give each run
  its own directory of binaries.
- **The cooked tables are shared and change under you.** Every build reads assets/cooked at run
  time, so two runs a few hours apart may differ by content, not code. Compare builds side by
  side, in the same hour.
- **Find a class's ceiling with a knob first.** A temporary COST_SCALE on costOf, run at 1, 0.5
  and 0.25, showed in one batch which classes were too timid and which were right.

## How to judge a run

`python3 tools/netbot/judge.py NETBOT_LOG HOURS`. For
each bot, within the first HOURS game hours, it reports level, deaths, paid trips and their
zen, trips paid twice within 5 s, and hunt flips. Compare two builds of the same HEAD on two
servers, one class per bot, at most two server+netbot pairs at a time (about 6 of the Mac's 10
cores). With four bots, netbot lags a --speed 100 server by about a fifth, and a loaded Mac
skews deaths and levels.
