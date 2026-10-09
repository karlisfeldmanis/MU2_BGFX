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
