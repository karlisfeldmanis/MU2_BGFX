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

**The wizard on strong ground: his strength was his health, twice over** (2026-10-10, the
user: "improve DW brains so he can farm 50+ and get to his first wings")

Every bot-side lever had been tried and none moved the wizard onto level-50+ ground (2-8% of his
kills): five point builds, caution halved, Evil Spirit earlier, Magic Shield. What the builds
never touched was `spend`'s first rule: the strength his class's best armour *at his level* asks.
At level 160-180 that was the Dark Soul set's 221 -- a set that falls from level-90 breeds he
never fought -- and every wizard had exactly 221 strength, 15 vitality and 218-260 health,
wearing Sphinx and Bone of drop level 23-31. A wizard's health is two a point of vitality
(`sim::reckon`), so that strength was his health twice over.

- *Strength for what can fall for him.* A thing falls only from a breed of its drop level or over
  (the realm's drop roll), so the armour asked for is now the best whose drop level the strongest
  breed he takes, on any world he can reach, or a shelf there, can give him (`findableLevel`).
  Same binary, four seeds of 24 h: levels 153/164/170/161 -> 188/222/245/207, kills on level-50+
  breeds 0% -> 2-29%, Jewels of Life 0 -> 3, wings on one seed. The knight gains too (12 h:
  140/133/124/99 -> 148/121/130/161). Not the elf: her levels the same and her deaths 31 -> 69
  over four seeds (her strength is her blow), so she keeps the old ask.
- *A day between two Spiders.* Found on the way: `hunt` picked the nearest quarry again every
  think, and a wizard of level 15 with the last two Spiders of Lorencia, Once More sixteen tiles
  to either side turned on the spot for 23 game hours -- each think the other was nearer by a
  step, each pick began a new walk, and `ask`'s give-up clock started over with it. He now keeps
  the one he is after while he would still go after it; the clock then bars one he cannot reach.
- *Vitality, now that the points are his.* Eight seeds of 24 h: all energy
  188/222/245/207/173/206/172/190 (deaths 17-64, wings 2 of 8); 0/0/1/3 216/209/197/216/188/216/185/180 (deaths 17-27, wings 4
  of 8, 11-22% of kills on strong ground); 0/0/1/1 210/199/197/191/149/225/197/167 (deaths
  5-15, wings 2 of 8, 1-16%). 0/0/1/3 is the wizard's build.
- *Where his Life comes from.* Blood Castle 1 pays a Jewel of Life a win (088048ae), and the
  offline wizard goes every hour once he has a cloak and reckons he takes its garrison: on
  0/0/1/3, 26 wins in 55 runs over eight days. The drops give one or two a day at most
  (`kLifeOdds` 2500 a kill of level 50+). The ladder that made wings on seed 2: the castle's Life
  on his Chaos Nature Bow at 15:32, four Bless to +4, the wings' box at 90% at 17:30.

The report now ends each run with its strong-ground line: kills of level 50+, Jewels of Life
that fell, when the wings were made, and the castle runs and wins.

**Blood Castle, and every class at once** (2026-10-10, the user: "lets work on DK and Elf, MG
now")

The report gained a deaths line (what killed him, and on which map). A 24-hour baseline of
every build showed one loss common to all of them: castle runs lost with the garrison and both
sorcerers dead. The castle trace (`BOT_CASTLE`) showed why. The statue falls, the Archangel's
weapon drops at its feet, and a bot with a full bag stood on it until the run timed out: the
take is refused with no room at its footprint, and there is no counter in the castle. Six of a
knight's ten runs ended so. Now the cheapest block of the bag the weapon would cover is thrown
on the ground first (`clearFor`'s discard). And the statue's weapon is never worn inside the
castle, since the hand-in looks only in the bag: a knight wore the Divine Sword and lost the run
with it on his hip.

The energy elf died 101 times a day to Noria's Stone Golems and Agons while her quest's Beetle
Monsters stayed huntable: the fear rule is a breed's, and what killed her was never what she
hunted. Three deaths on one map inside fifteen minutes now take her off it for twenty (hers
alone: at two deaths, or for the agility elf, it cost levels).

Frozen binaries (a private worktree at 4da79f57 with the cooked tables copied, since other
sessions changed the sim and the tables during the day), two seeds of 24 h each:

| build | levels, before -> after | castles won | wings |
|---|---|---|---|
| knight | 226/219 -> 241/216 | 10/20 -> 18/19 | 1 -> 1 |
| agility elf | 201/235 -> 234/243 | 14/20 -> 20/20 | 0 -> 2 |
| energy elf | 115/122 -> 123/130, deaths 136 -> 49 | none | 0 -> 0 |
| melee gladiator | 209/223 -> 224/254 | 5/21 -> 20/26 | 0 -> 1 |
| magic gladiator | 235/245 -> 231/255, deaths 84 -> 64 | 7/27 -> 11/26 | 1 -> 0 |
| wizard | 162/191 -> 170/191 | 3/11 -> 4/11 | 0 -> 0 |

All told castles won 39/99 -> 73/102 and wings 2 -> 4 of 12. Still open: the magic gladiator
loses most castle runs, the energy elf never reaches level-50 ground or a castle, and the
wizard's levels fell 20-40 from the morning's runs with no brain change: same seed, a day's
content and sim changes between them (the Devil Square drops reshuffle the loot dice).

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
