# The party

Started 2026-10-09. The user: "lets implement party system, so bots can form 5-man party when
they can kill some quest monsters, elfs can heal, dk can tank, and 2 dps", then "if there is
5-man party lets give 10% exp boost". It is docs/server-plan.md phase 7's first item ("party:
shared experience, kill credit and the party bar"). Chat is a separate proposal (the chat
mockup, claude.ai/artifact/AMHkjGrUWpJVkPmAbuDvGX); the party rides on its channels.

## The user's decisions (2026-10-09)

| question | decision |
|---|---|
| quest kill credit | **every member near the kill** counts it, if he has that step |
| experience | **MU's own party rule** (below), and **+10% for a full party of five** (ours) |
| tanking | **a threat table**: a monster fights whoever holds the most threat on it (ours; MU 0.75 has no taunt) |
| bots on the server | **DK, Elf, DW, MG and a second DK**: Ragnar and Elora stay, three join |

## MU's rules (WebZen GameServer 1.00.93, 0.97d base)

- **Five at most** (`PartyClass.h`, `MAX_PARTYUSER 5`). One leader, who invites and kicks;
  anyone may leave; the leader leaving hands the party to the next member.
- **Experience** (`gObjExpParty`, user.cpp). Members on the kill's map **within 10 tiles of the
  monster** are "in view". Their levels sum to `totallevel`, and `partylevel` is its mean. The
  kill's experience is reckoned once, at `partylevel`, then multiplied by `viewpercent` and
  shared **by level**: a member gets `total * viewpercent * hisLevel / totallevel / 100`.
  `viewpercent` by members in view, the MODIFY_GETTING_EXP_RATE_20060214 branch, because our
  solo kill (sim::killExperience, `(level + 25) * level / 3`) is that branch's:

  | in view | 2 | 3 | 4 | 5 |
  |---|---|---|---|---|
  | plain party | 160 | 180 | 200 | 220 |
  | **set party** (a wizard, a knight and an elf in view) | -- | 230 | 270 | 300 |

  Ours on top: **x1.1 when all five are in view**. A member out of view gets nothing; a party
  of one in view is paid as a solo kill. A member's share is capped at his own level's
  experience. Our kExperienceRate and the Rings of Wisdom apply to each share as to a solo
  kill.
- **Drops** stay the killer's (top hitter), as now.

## What is ours

- **Quest kills**: everyone in view (same rule: the kill's map, 10 tiles) with the step counts
  it. The tracker shows it as his own.
- **Threat** (the Golden Dragon raid already keeps one, `Realm` raid threat): every blow adds
  its damage; a Dark Knight's adds double; a heal adds half its health healed to every monster
  fighting the healed one. A monster changes target only to one holding 10% more than the
  current. Off a party, nothing changes: a monster with one attacker attacks him.
- **The +10%** for five.

## The user's further asks (2026-10-09)

- "it make sense they go together on dungeons": bots party for the Dungeon's and the Lost
  Tower's chains, not only for one hard breed.
- "we need to be smart about how they will fill quests, because some will need more kill some
  already will be finished some kills": a shared kill counts only for members who still want
  that breed (countKill skips a finished step). The party's brain picks the breed the most
  members still want; a member whose steps are done may leave, or stay as the party's support.
- "when you will finish party and chat implementation, lets launch server with 50 bots and lets
  see how this evolves". Each netbot steps its own mirror of its map on the Mac, so the load of
  fifty is measured first (docs/server-capacity: ~14 us a player a tick on the box).

## Done

- 2026-10-09, a9f28b19: the realm's half (Body::party, Command::Kind::PartySet, payKill,
  countPartyKill, testParty).
- 2026-10-09, 05495abc: the server's half (protocol 16: PartyAsk, PartyState; the server's party
  section; Link::askParty/party; netbot --party). Run locally: four bots, one party, no
  divergence. The client's bar and the chat's /invite, /leave, /kick, /accept, /decline are the
  chat session's (src/game/ui/chat.*), reading Link::party().
- **Threat, decided 2026-10-09** (the user: "how tank will be able to hold threat if DW does more
  damage?", then "what about MG, he also can be tank"): x3 threat while the Defense guard is up
  -- a knight's or a Magic Gladiator's, so the tank is whoever raises it; a monster turns to
  another only past +10% (in melee) or +30% (at range) of its target's; a tank's first blow
  adds a tenth of the monster's health; his area skills put threat on all they strike; a heal
  adds half of it to each monster on the healed. Bots' damage dealers ease off at 90% of the
  tank's. A taunt only if this is not enough.

## The bots' party (sprint 4, 2026-10-09)

Two halves, so the brain stays one brain offline and on the server:

- **The board** (`tools/netbot/netbot.cpp`, `Board`, `netbot --parties`): once a second it reads
  each bot's quests (`Bot::needs`: every Clear step still short, under way or offered, with
  whether he takes that breed `alone` and whether he would `together`, a kill's cost to him
  at `kTogether`, a quarter) and the server's party state.
  - *Forming*: bots **stuck** on a quest (a breed they would take together but not alone) are
    put with the others stuck on it; a free knight or melee gladiator to tank and a free elf to
    heal are recruited if missing and they can stand there (`Bot::helps`), then any free hand up
    to three. The tank leads, else the highest level; he invites, the others take his invite. A
    bot stuck with no one says `LFG <quest>, <breed> in <map>, level N` on his map, once in ten
    minutes.
  - *Leading*: the party hunts the breed the most members still want -- a quest under way
    weighs twice an offered one, a stuck breed twice again, then the leader's map, then the
    kills left -- and, there, every breed any member still wants. A breed is open to the party
    when the tank and half the members stand it. The leader says each change on Party: "Next:
    Ice Queen in devias, Brann tanks." A member done with it stays as support.
  - *Breaking up*: ten game minutes with no member stuck (stuck turns on purse and potions; a
    party broken the moment it was not, re-formed a minute later). `--party` (one fixed party
    of the run's bots, the first named its leader) never breaks up.
  - The board sees only its own process's bots: a party is of bots run together.
- **The brain** (`tools/bot/brain.h`, `Bot::Lead` / `lead()`): the board's hunt over his own --
  except a quest to hand in, and the party's quest offered him is taken on the way. His part:
  - *Tank*: Defense kept up (`guard`), and whatever is on a member near him is his first target.
  - *Damage dealers and the elf*: strike what the tank holds -- the one closest to dying among
    those whose threat they hold under 90% of the tank's (`tanksTarget`, `kEaseOff`); nothing
    held, they keep by him and pull nothing; a knight beside the tank leaves Defense to him.
  - *The elf* (`aid`): Heal on the lowest under 60% within 8 tiles, herself among them; Greater
    Damage on whoever's has lapsed; Greater Defense on a member whose guard has lapsed, never
    the tank.
  - *Together*: the party's map is his aim, so he follows across maps; on it, whoever is not the
    leader keeps within 8 tiles of him.
  - *Cost*: a kill's cost to him is scaled by `partyScale` -- one over the members within 20
    tiles, half again beside a tank, half again beside an elf -- so a partied bot takes, and
    drinks through, what he would not alone.
- **The Lost Tower's floors** (the user, 2026-10-09: "can they clear also some of LT floors and
  maybe start atlans quest line? an dmaybe go to bc together?"): a member on another floor than
  the one he keeps near takes the trip or the stairs to it (`goFloor`, split off `nextFloor`).
  Tersia's floors and Lirien's Atlans chain are Clear steps like any other, so the board forms
  for them as for Devias.
- **Blood Castle together**: the board gives a party the highest castle two or more of its
  members hold a cloak for and have the band's floor for (`Bot::castlesOpen`), and each who can
  goes into that one at the door's time (`Lead::castle`, `castleChoice`), the party's cost scale
  holding inside; free bots due at the same door are made a party for it. The party holds while
  any of them is due there or inside.
- The server's roster (`netbot` with no names): Ragnar (DK), Elora (DW), Ilaria (Elf), Varkas
  (MG), Hrothgar (DK).
- `netbot --threads N` (up to four by default): each bot's mirror and brain are his own, so a
  thread plays a share of them; the board and the minute's report hold every bot's lock. The
  user: "cant we speed up this things? we can control server thics" -- one thread kept five bots
  at a full core at `mu2_server --speed 200`, and a netbot behind its server stepped its ticks
  late (its bots reacting late, 100-190 deaths a run). With three threads, `--speed 100` runs
  about 170x (the server's tick is not exact), every bot in step.

### Measured (2026-10-09)

A local server at `--speed 100`, five fresh bots (DK, Elf, DW, MG, DK), `--threads 3`, to 35-40
game hours: `--parties` against the same roster solo, the same build.

| | final level | deaths (all five) | The White Silence handed in at |
|---|---|---|---|
| parties | 135-144 | 264 | DW 111, DK 111, Elf 128, MG 132, DK 133 |
| solo | 149-178 | 617 | DK 97, DK 104, MG 121, Elf 127, DW 143 |

Parties more than halve the deaths and bring the wizard's White Silence 32 levels sooner; they
level 15-35 slower all told, and the knights hand it in later -- the party's time on the road
and waiting is not yet paid back. Earlier runs found and fixed: a knight landing on another map
read as out of his party (his line had not heard it yet) and paid 2,000 zen back; the hunt
flipped on a tie of maps and on purses; an elf led to breeds she could not take alone stood
idle till her knight came; the board's timers were the wall's, so at `--speed 200` a party was
given up before its invite was heard.

The second pair (the same roster, ~35 game hours), after `mu2_server --speed N` ran the wall
clock N times as fast too (Blood Castle's door and the quests' twelve-hour rests kept real time,
so castle parties stood at the Messenger for game hours): parties went into Blood Castle
together (castles 1 and 2), formed for the Catacombs, the Knights' Halls, The Drowned Halls in
Atlans and the tower's Bad Air, and hunted Atlans 44 times and the Lost Tower 15.

| | final level | deaths | The White Silence handed in at |
|---|---|---|---|
| parties | 141-185 | 271 (the wizard 142) | DW 93, DK 99, MG 100, Elf 113, DK 124 |
| solo | 151-223 | 193 | DK 97, DK 98, MG 113, Elf 118, DW 143 |

Its parties lost hours to a netbot fault, since fixed: a decision's answer was waited for 40
game ticks, 20 ms at `--speed 100`, under a round trip, so a bot asked again on a mirror that had
not heard it -- an elf "handed in" Noria's Song 82 times and a knight 111, each refused, the
party standing by. `kAnswerWait` is two seconds of the wall now. And two parties at one quest
are made one (`Board::merge`; a free bot stuck on a party's quest joins it).

## The plan, a sprint each

1. **The realm's party** (`src/sim`): parties of up to five, the commands Invite, Accept,
   Decline, Leave and Kick, the leader, a member's death and logout; shared experience and
   quest kills as above; carried in the snapshot and over the wire. sim_test covers the
   formula, credit, leaving and the full-party boost.
2. **The client**: the party bar (names, health, mana, a crown on the leader, greyed out of
   view), invite from the player's right-click menu and `/invite name`, `/leave`, `/kick
   name`; party system lines on the chat's Party channel ("Valeria has joined the party.")
   with Accept and Decline on an invite. Party chat waits for the chat itself.
3. **Threat**: the table for every monster fighting a party member; the knight's double.
4. **The bots**: a bot stuck on a quest step it cannot take alone (sim::drinksThrough fails)
   asks for a party; bots whose roles are missing and who can reach the map join; the elf heals
   and buffs the lowest member (Heal, Greater Defense, Greater Damage), the knight leads and
   holds the target, the damage dealers strike the knight's target; the party breaks up when
   the step is done. Three new netbots (an elf, a Magic Gladiator, a second knight). Measured
   as the Ice Queens were: the level each class hands in Devias's quest, and the deaths.

### Later the same evening

- No party for the towns' own quests (Marlon's, Peia's): at level 1 every breed reads too hard
  alone, and those parties held five bots 40-80 levels under the same five solo; a knight made
  their tank at level 1 died broke and ended at 17. And a broke bot no longer goes to town for
  gear it cannot pay to mend.
- A member who cannot stand the party's breed goes his own way, in the party still (a wizard led
  into Atlans died to Bahamuts 81 times); a quest to take is a short errand the party's hunt no
  longer overrides.
- **The energy elf** (the user: "maybe create a elf which is energy elf which can give best buffs
  and heals"): `netbot NAME:eelf`, `bot --kin elf --path energy` -- points 1/2/2/5, energy last;
  costOf whole for her (not the elf's half) and quests on a caster's share. Offline, 3 seeds of
  24 h: levels 106-133, 31-64 deaths. In a party her four partners ended 112-134 against 91-116
  solo, with a third of the deaths.
- Checked (the user: "also check if elf is buffing and healing party memebers, also check if DW
  is using magic shield"): netbot's minute line now counts her casts on others -- Greater Damage
  1,463, Greater Defense 549, Heal 50 in one party's 35 game hours -- and the wizard's Magic
  Shield, 359 times.

### The verdict (2026-10-10)

Three rosters of five fresh bots (two knights, an elf, a wizard, a gladiator), each run as
`--parties` and solo at the same time on frozen binaries of ce51dce1, `--speed 50`, ~28 game
hours, every bot in step with its server (one solo elf's mirror diverged at 19:27 and is left
out).

| | parties | solo |
|---|---|---|
| levels, knights (6) | 1,039 | 1,081 (-4%) |
| levels, wizards (3) | 451 | 470 (-4%) |
| levels, gladiators (3) | 536 | 594 (-10%) |
| levels, elves (2) | 332 | 356 (-7%) |
| deaths | 466 | 531 |
| The White Silence handed in | 15 of 15, mean level 99 | 12 of 15 (no wizard), mean 106 |
| Blood Castle entries | 129 | 103 |
| quests handed in | 222 | 274 |

Parties now cost a little levelling (4-10%) and pay it back in safety and progress: fewer
deaths, Devias's quest for every class and seven levels sooner, the wizards' only in a party,
and a quarter more Blood Castles. Fewer quests all told -- the party's hunt still holds a
member off his own repeatable ones.
