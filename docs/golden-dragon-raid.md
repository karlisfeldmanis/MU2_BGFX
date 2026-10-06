# The Golden Dragon as a raid boss — design, for the user's go

Status: **sprints 1 and 2 built 2026-10-06.** Sprint 1, the sim and `tools/raid`. Sprint 2, the
drawing and `--raid N [--raid-box A|B|C] [--raid-stage S] [--raid-now]`: the raiders in their
kits and wings (Figures::dress), the tells as a dark red stain on the land (fx/omen.h; meteors
unmarked -- the user: 'dont show the meteor warning they just has to happen'), the breath from
bone 11, the rocks, the pools' low embers, Hellfire's ring for the Inferno, the shadows darkened,
the dragon drawn 3 m up on clip 7 while aloft, the boss bar under the herald in its grammar
(game/ui/boss_bar.h; always up in the fight -- the user: 'always show the HP bar not only on
hover'), the raiders' names and bars, the camera's 3 m pull (unmeasured on the frame budget yet),
and the taiko track while he fights it. Still switched on only by --raid: a plain invasion in
play is OpenMU's. Sprint 3: the Kundun boxes and the loot. Builds on the Golden Invasion (d3d59e2e).
Every rule is traced (file:line) or marked **invention**.

Sources: MuMain = `LEGACY/reference/MuMain/src/source`; WZ = WebZen GameServer 1.00.93
(ptr0x-real/Mu-GS-Webzen-MC-10093) and WZD `Data/Monster.txt` (revision 22 Aug 2008);
OM = OpenMU.

## 0. What the sources give us

| | Golden Dragon 79 | Golden Dragon 44 | Golden Budge Dragon 43 |
|---|---|---|---|
| OM (VersionSeasonSix InvasionMobsInitialization.cs:74-101), **what we cook** | L80, 22,000 HP, 300–350, def 230, range 2, view 7 | — | L15, 2,500 HP, 120–125, def 45 |
| WZD Monster.txt:287 / :276 / :275 | L80, **100,000 HP**, 6,000–8,000, def 5,000, A.Type **150**, range 3, swing 2,000 ms | L60, 50,000 HP, 1,000–3,000, A.Type 150, range 4 | L15, 4,400 HP, 240–250, A.Type 0 |
| WZ DragonEvent.cpp:89-160 | — | class 44 raised in one of three Lorencia boxes (135,61–146,70; 120,204–126,219; 67,116–77,131), 3 s after the notice, gone after 5 min | — |

- **A.Type 150 is the boss blow** (Flame of Evil one blow in five, within five tiles) that
  `kBosses` already implements for the Balrog and friends (realm_tuning.h:336-343,
  WZ gObjMonster.cpp:1849-1925). Breed 79 carries it in WZD, so adding 79 to `kBosses` is traced.
- WZD's 6,000–8,000 damage and 5,000 defence are a later season's ladder and one-shot a level-80
  hero; **not used**. Its 100,000 HP is used, as the ten-player end of the health formula (§2).
- **Correction to the brief:** MU's Red Dragon boss skill breathes **three** MODEL_FIRE from bone
  11, not two — yaw −30°, 0° (pitch −30°) and +30° (ZzzCharacter.cpp:1939-1948), plus one fire
  falling at random within 512 units (≈5 tiles) every frame of the skill (:1950-1955).
  A MODEL_FIRE sub 4 that reaches the ground shakes the camera and throws every character within
  200 units (2 tiles) into its shock (ZzzEffect.cpp:7732-7770). These three facts are the breath,
  the fire rain and the knock below.

## 1. The stages

Health thresholds on the dragon's own pool. A stage change is said by the realm on the tick
(new `What::Raid`), and every big blow is said **before** it lands with its tile, radius and
ticks, so the drawing can mark the ground. Everything here runs on ticks (20 a second).

### Stage 1 — Ground (100 → 70%)
| Move | What | Telegraph | Source |
|---|---|---|---|
| Bite | clip 3, its band, on its quarry; one in five is a Flame of Evil on all within 5 tiles | the swing itself | OM band; A.Type 150 via kBosses |
| Fire Breath | clip 4 from bone 11: a 60° cone, 6 tiles, three jets (−30/0/+30), burns each tick for 2 s | **1.2 s**: it rears, a faint ember cone is laid on the ground | jets: ZzzCharacter.cpp:1939-1948; cone, reach, timing **invention** |
| Roar Shock | clip 1, everyone within 2 tiles shoved 2 tiles out and shocked | 0.8 s, the roar's own wind-up | 200-unit shock: ZzzEffect.cpp:7732-7770; as a push, **invention** |

Every 12 s it picks Breath or Shock: Shock when two or more fighters are within 2 tiles,
otherwise Breath at the densest group. **invention**. **The tail swipe is dropped**: Monster32 has no tail clip, and a bite played backwards
would read as a glitch.

### Stage 2 — Flight (70 → 40%)
- It roars (mKundunRoar) and lifts off on clip 7 to ~6 m. Aloft it **cannot be struck in melee**;
  bows and spells still reach it (**invention**: gives the knight the minions and the elf/wizard
  the boss).
- **Minions:** at lift-off, `2 + N/2` Golden Budge Dragons (OM breed 43) rise around the arena,
  again at 50% health. They are breed 43 at OM's numbers, unscaled. The summon is a phase of its
  own inside the flight (the user, 2026-10-06): the dragon hovers, roars, and the minions come down
  in the dive's small form, the ground marked where each lands. Each minion's own drops are in
  §1 Loot ("good loot", the user).
- **Strafing runs** every 10 s: it flies a straight line over the arena and fire falls along it,
  MU's fire rain within 512 units (ZzzCharacter.cpp:1950-1955). Each impact tile is marked
  **1.5 s** before it lands (**invention**); a strike in the open is 25% of a hero's max health.
- It lands when the minions are all down **or** after 45 s, whichever is first (**invention**),
  with the invasion's dive (invasion_sky.cpp) reused for the descent.

### Stage 3 — Enraged (40 → 15%)
- Lands with mKundunShudder; a faint red cast on it, monster-aura faint (memory: auras subtle).
  Swing 20% faster (**invention**).
- **Meteor storm** every 8 s: one meteor per living fighter, each on a marked tile under where that
  fighter stood 1.5 s earlier; the Balrog's storm and fx/meteor (play_show.cpp:680). 35% of
  max health in the 1-tile blast.
- **Fire pools:** each meteor leaves a burning tile, 2×2, for 12 s; 5% of max health a second
  inside. The arena slowly fills, so the fight has to move (**invention**).
- Breath and Roar Shock continue from stage 1.

### Stage 4 — Last stand (15 → 0%)
- **Golden Inferno**, every 30 s: it rises a little and roars (Ferea_King_Attack_Roar), a ring
  marker grows over the whole arena for **6 s**, and then everything in it takes 90% of max
  health. The only safe ground is **inside the shadows of its wings**: two 2-tile circles marked
  on the ground beside it. That fits "the dragons are shadows" (**invention**).
- **Hard enrage:** 8 minutes after it landed, the Inferno comes with no shadows, and it
  wipes the raid. The invasion's 30-minute stand still ends it if nobody fights.
- Breath, Shock, storm and pools all continue.

Death ends the invasion as today (`endInvasion`), with clip 6.

### Loot: Boxes of Kundun (the user, 2026-10-06: 'dragoons drop kundum box')

The sources name the boxes (docs/kundun-box.md §2, golden-invasion.md):

| Source | Golden Dragon (79) drops | Golden Budge Dragon (43) drops |
|---|---|---|
| WZ 0.97d EledoradoEvent / OM InvasionConfigurationDefaults.Golden | one **Box of Kundun +3** | Box of Luck (14,11 level 0) |
| WZ 2009 renewal (`ADD_GOLDEN_EVENT_RENEWAL_20090311`, gObjMonster.cpp:4183-4335) | **five** boxes, **+1 to +3**, scattered round the corpse | — |
| WZ renewal, the Great Golden Dragon | five boxes, +4/+5 | — |

**Proposal:** the dragon drops the renewal's **five boxes, +1 to +3**, scattered round its
corpse. This is traced, and a raid boss should feel like a pile. The minions drop OM's **Box of
Luck**, one each. +4/+5 stay out, since they belong to a bigger dragon we don't have, and there
is no N-scaling of the loot: it's one hero's.

**The boxes don't exist yet.** kundun-box.md is research only. Building them means:
- items 14,11 at levels 8-10 ("Box of Kundun +1..+3", MM ZzzInventory.cpp:1704) and level 0
  (Box of Luck), with the glow at `(L-8)*2+1`;
- opened by throwing, as the Firecracker already is (the same item, 14,11 at level 2; `Realm::crack`);
- WZ's roll (Event.cpp:603 `EledoradoBoxOpenEven`): bags eventitembag8-10 from WZD, item
  30/25/20% (of which 5/4/3% excellent), else 50k/100k/150k Zen, plain level row + `rand()%2`;
- their own Random stream, as crackerDice_.

This is a step of its own after the fight (§4's order).

**Beside the boxes** (the user, 2026-10-06: 'it also drop runes, jewels, maybe feather'). All of
this is **invention**: no MU source gives a golden monster anything but its box. Scattered with
the boxes, rolled off the raid's own stream:

| Drop | How many | Rule |
|---|---|---|
| Rune of Creation (14,22) | **2**, certain | `drawRunePower` at the dragon's level 80, for his class: rarity at kRuneRarityShare (60/30/10) with Legendary in reach, stat runes among the powers (items.h:758-765) |
| Jewels | **3**, certain | each drawn evenly from Bless (14,13), Soul (14,14), Life (14,16) and Chaos (12,15) |
| Loch's Feather (13,14) | **1 in 4** | kills elsewhere give it 1 in 500 and only in Atlans and the Lost Tower (items.h:185-192). Lorencia gets it only off this dragon |

The counts and the feather's odds are placeholders for the user to tune.

**Each Golden Budge Dragon minion** (the user, 2026-10-06: 'can drop good loot'). Every one drops
these, beside its corpse, on the raid's stream:

| Drop | Odds | Source |
|---|---|---|
| Box of Luck (14,11 level 0) | always | WZ gObjMonster.cpp:4107; drop-boxes.md §2. On its own it is weak: 20% an item (+0/+1, option always), else 3-6 piles of 1,000 Zen (Event.cpp:2311) |
| a jewel (Bless, Soul, Life or Chaos, evenly) | 1 in 4 | **invention** |
| a Rune of Creation (drawn at level 80, his class) | 1 in 10 | **invention** |

With `2 + N/2` minions a wave and two waves, ten players' fight brings 14 minions, so about 3-4
jewels and 1-2 runes from the minions alone. Alone it brings 4 minions, about one jewel.

**Settled (the user, 2026-10-06: 'basicaly multiple drops around the corpse when dragon is
killed'):** on its death everything above lands at once as separate things on the ground, never
as one pile on one tile. They go out in a ring round the corpse, one tile per drop, each tile
found by the kill drop's own `Realm::clearing` from a point 1-3 tiles out at an even step of
bearing. The bearing's offset is rolled once, so the ring's turn varies. Each drop lingers as a
kill's drop does, and each is said as its own `What::Dropped`, so every one gets its name label
and drop sound. The bearing and the 1-3 tiles are ours, after the renewal's "scattered round the
corpse".

## 2. "Tough, for 10 players" in a single-player game

The numbers take the player count **N** as a parameter, fixed when it lands:

- **Health** `H(N) = 22,000 + (100,000 − 22,000) · (N − 1) / 9`: OM's at one, WZD's at ten.
  The ends are traced; the straight line between them is **invention**.
- **Blows** keep OM's 300–350 band. They hit one body; N does not touch them.
- **Mechanics** hurt in shares of the struck body's max health (above), so a mistake costs the
  same at any level or gear. **invention**: MU has no share-of-health boss damage.
- **Counts** scale with N: minions `2 + N/2`, meteors one per living fighter, wing shadows two at N ≤ 5
  and three above that.

**Ten heroes of the three classes, no guards** (the user, 2026-10-06: 'without guards is the
fight', then 'we need to create simulation where 10 characters from different classes is
fighting the boss'). The guards keep out of it: a guard never takes the invader or its minions
as a quarry, and they never take him (**invention**).

### 2a. The raid simulation

**The party: end-game characters in proper gear** (the user, 2026-10-06: 'make realistic "end
game" characters', 'with some proper gear'). The hero is one of the ten, and the other nine are
**raiders**. "End game" means the top of what this game holds: second class, the best sets and
arms already built (source/items), the 2nd wings, +9 to +11, excellent options, and runes in
sockets. Levels vary as a real party's would.

| Role | # | Class, level | Armour (5 pieces) | Hands | Wings, pet | Runes in sockets |
|---|---|---|---|---|---|---|
| Tank | 1 | Blade Knight 320 | Dark Phoenix +11, exc (dmg dec, reflect) | Divine Sword of Archangel +9 / Dragon Shield +11 | Wings of Dragon +9, Guardian Angel | Undying ×3 in armour, Stormcall |
| Melee | 3 | Blade Knight 260, 290, 340 | Dark Phoenix +9 to +11; one in Black Dragon +11 | Dragon Spear +11 (two-handed), Sword of Destruction +10, Divine Sword +9 | Wings of Dragon +7 to +9, Imp | Whirlwind, Stormcall, Meteor |
| Healer | 1 | Muse Elf 280 | Divine +11, exc | Elemental Mace +9 / Elemental Shield +11 | Wings of Spirits +9, Guardian Angel | Undying, Renewal |
| Archers | 2 | Muse Elf 270, 330 | Divine +9 / +10 | Great Reign Crossbow +11 exc, Celestial Bow +10 | Wings of Spirits +7/+9, Imp | Piercing Volley, Frost Arrow |
| Wizards | 3 | Soul Master 250, 300, 360 | Grand Soul +9 to +11 | Dragon Soul Staff +11, Staff of Destruction +10, Divine Staff of Archangel +9 / Grand Soul Shield | Wings of Soul +7 to +9, Imp | Echo, Meteor |

- **Points:** `kPointsPerLevel` (5) a level plus the class's start, spent as each class is
  really built: the tank in vitality and strength; the melee in strength and agility, with energy
  enough for the skills; the elves in agility (archers) or energy (healer); the wizards in energy,
  with vitality to live. The real splits come from the bot's `--build` and go in this file
  (**invention**).
- **Jewellery:** each wears a ring and pendant of its class's powers (powered-jewellery), resistance
  to fire included where it is built.
- **Excellent and luck:** every weapon excellent with two options, two armour pieces excellent
  each, luck on half, the additional option +12/+16. This is the drop and machine rules' best
  realistic roll, not a cap.
- **No gear is made up.** Every piece is a built item (checked against source/items, 2026-10-06).
  A piece a class can't wear, or whose requirement its points don't meet, fails the kit's own
  check in sim_test (`equip`'s rules), so the kits stay legal. The kits are data,
  `source/raid/party.json`, read by the sim test and `tools/raid`, not code.
- **Skills:** each raider knows every skill its class can learn by this level. It throws them in
  its role's priority order: the tank uses Defense on cooldown and Twisting Slash; the melee use
  Death Stab, Rageful Blow and Cyclone; the healer uses Heal on the lowest, Greater Defense on the
  tank and Greater Damage on the melee; the archers use Penetration and the elf's multi-shot; the
  wizards use Evil Spirit, Meteorite, Inferno and Soul Barrier on themselves.

With gear like this, 100,000 health is too little: such a party would kill it in a minute or two.
So **the boss's health and the mechanics' shares are what the headless tune moves**, starting at
WZD's 100,000 and raised until the §2a target holds (5-7 minute kill, 2-4 deaths, wins 50-70%).
Hits stay shares of max health, so they hurt end-game bodies too. The dragon's own blow band goes
up with the tune, from OM's 300-350 toward WZD's 6,000-8,000. WZD's numbers are a later season's
ladder, which end-game gear is.

**What a raider is, in the sim** (src/sim, no drawing in it):
- A `Body` with a new `raider` index (−1 elsewhere) into a fixed `raiders_[9]` that holds each
  one's own satchel kit. `monster()` excludes raiders, and they are raised dormant with the
  invader as `invaderSlot_` is, so no pointer into `bodies_` moves.
- `rearm` reckons a raider from its own kit: `rearm(Body&, const Satchel&)`, `bag_` the
  default. Every `bodies_[0]` and `bag_` read on the fight and skill paths (about twenty, in
  realm_fight.cpp and realm_skills.cpp) is either handed the caster or stays the hero's alone:
  the pet, Zen, loot, experience and the save.
- Archers never run out of arrows. A raider drinks a large healing potion below 40% health, on
  the hero's half-second cooldown, from a stock of 20. Both are **invention**.
- **A dead raider stays dead for the fight.** 0.75 has no resurrection, and losing people is
  what makes it tough. The hero dies as he does today.
- **Its mind** (`Realm::raid(Body&)`, own stream `raiderDice_`, **invention**): its role's place
  (the tank at the dragon's face, melee at its flanks, ranged 5-7 tiles out, the healer behind the
  ranged), its target (the dragon, and the minions first for the melee while they stand), its
  skills by a priority list on their own cooldowns, and **the dodge**: at each telegraph it steps
  out after a reaction of 0.4-1.0 s, and fails 1 in 6 (stands still). The reaction and the failure
  are the difficulty knobs.
- The dragon's threat comes back: it fights whoever dealt the most damage in the last 10 s, ×1.3
  margin to switch (the tank's Defense and taunt count double). Breath aims at the densest group,
  meteors go to every living fighter. This is the §1 text for a crowd again.

**Two ways to run it:**
1. **Headless, all ten AI** (the hero too, by the raider mind): `tools/raid` (built beside
   `tools/bot`), `--runs 50 --seed S --party 10`. It prints per run, and summed: win or wipe, kill
   time, the stage reached, deaths by class and by the move that killed, damage and healing share
   by class, potions drunk. That is how "tough" is measured.
   **Target** (**invention**, the user to pick): wins 50-70% of runs, 2-4 deaths in a win, the
   kill in 5-7 minutes, and nobody untouched. N, the dodge failure and the enrage clock are tuned
   until it lands there; the numbers go into this file.
2. **In the window** (`--raid 10`): the hero played by the user, nine raiders beside him. Each
   raider is drawn in its class body and kit (the hero's dress, per Drawn), with its name over it,
   and gets an escort-style green bar and a row in a small party list at the left (name, class
   colour, health). This is the larger half of the work.

**Order:** the stages and the raider sim first, with `tools/raid` and the headless tune. Then the
drawn raiders and party list. Then the boxes.

### 2a'. What the tune settled (sprint 1, `build/raid`)

The numbers in sim/raid.h after the headless tune, each an **invention** found by `build/raid
--runs 30..60` against the §2a target:

| Knob | Value | Why |
|---|---|---|
| `kRaidHealthScale` | ×8.5 (850,000 for ten) | ×1 died in 50 s; ×8.5 lands the kill at ~7:00 |
| `kRaidBlowScale` | ×6 (1,800-2,100 a bite) | ×1 never scratched the tank; ×8 killed it in two |
| `kTankThreat` | ×3 | at ×2 the top archer pulled it |
| `kDodgeMissOdds` | 1 in 8 | 1 in 6 was mostly fire deaths |
| `kFlightLeast` | 20 s | the wave died in 6 s, and a flight that short is no stage |
| `kWipeReach` | 64 tiles | the hard enrage was outrun at 14 |

Elves keep the tank up (the user, 2026-10-06: 'can we don that elfs can heal and buff tanks?'):
the healer heals the tank first and keeps Greater Defense and Greater Damage up, the archers heal
the tank below half and put Greater Defense on it, from six tiles (the hero's Heal is thrown on
himself; ours). The tank throws no Defense of his own, so the elves' guard holds.

The kits in source/raid/party.json are each worn through the real gates: a piece's plus is the
highest its wearer's points meet (+0 to +11), and an excellent piece no build could wear went
plain. Archangel weapons are Blood Castle's and can't be worn, so the swords are Swords of
Destruction.

**Measured** (60 seeds, the three boxes in turn): **34 of 60 won (57%), the kill at 7:00 median
(6:17-7:51), 3.0 deaths a win**; deaths 304 fire (the hazards), 48 bites, 10 Flame of Evil; the
dragon's damage taken: archers 49%, wizards 18%, tank 17%, melee 16%. One seed fights the same
fight twice.

What a raider is not, yet: its weapon's runes and its excellent options' procs are the player's
alone (they read his bag), it spends no mana, and its potion lands at once.

### 2a''. After the first look in game (2026-10-06)

The user played it and asked, and the fight now does this:
- **It never flies in the fight** ('dragon never flies again during the fight, if he landed he
  fights on legs'). Stage two stays on the ground: the minions and the lines of fire from the sky,
  every blow reaching it. It flies to come and to go.
- **Not killed by its clock, it leaves** ('only when dragons is not killed on time they fly away
  and weather clears'): at 8:00 it rises, untouchable, and is gone 5 s later; the invasion ends and
  the storm, held all the fight, passes. No wipe.
- **The fallen come back** ('if chars died they probably respawn at citty and they have to return
  to fight'): up in Lorencia's spawn box after the hero's 3 s, running back.
- **No warnings at all** ('dont show spell warning from dragon spell just happend and players will
  learn that'): nothing on the ground, no move named on the bar.
- **Hellfire** from the enraged stage on, in the roar's place when two or more crowd it.
- **A melee reach**: bulk 2 (sim::bulkOf), knights strike from its edge.
- The raiders throw their whole kits in turn (raidNext_), say their casts and shots as the hero's
  are said, and so are drawn; the healer fights with her mace when nobody needs her.

Retuned with respawns (build/raid --runs 60): health x9.5 (950,000 for ten), bite x15 (4,500-5,250,
on the way to WZD's 6,000-8,000): about 55% won, the kill at 7:30, near its clock.

Later the same day:
- **A swarm** ('i never saw stage with massive golden budge dragon spawn'): 2 + 2 a player minions
  (22 for ten, at most 24), four to nine tiles out, health x12 and bite x6 for the party they meet.
- **It summons, it does not strike** ('dragon can do summoning animations (not attacking)'): a
  wave halts it, drops any blow in hand, and holds it kSummonTicks (1.5 s) while it plays its roar
  (Monster32's clip 1) from where the landing's starts -- whole, its opening read as a wing-beat
  ('for some reason 1 time dragon used fly animation').
- **The minions are golden** ('golden budge dragon has to be with golden effect'): drawn at +9,
  Chrome01 and Shiny01 added, MuMain's RENDER_METAL | RENDER_CHROME pair (ZzzCharacter.cpp:8716-8785).
- **The party's runes work** ('probably our raid group is too weak without runes'): each weapon
  carries its class's (knights Stormcall/Meteor/Ice or Fireburst/Ring of Fire/Hellfire, elves
  Frost Arrow, wizards Arcane Echo and Stormcall, Wrath in every one), and a raider's procs roll
  from its own kit on the raiders' dice (Realm::stormcall, Realm::echoes), so the hero's runs are
  unchanged.
- **The boss is immune to crowd control** ('bosses has to be immune to CC', 'and there has to be
  damage text immune'): no freeze, chill, push, shove or pull takes it (Realm::shrugs), and
  IMMUNE goes up over it at most twice a second. Every wound still lands. Ten runed fighters'
  Stormcall pushes had walked it out of its fight.
- **The minions are worth killing** ('we need also some decent drops for small golden budge
  dragons'): beside their Zen, a refining jewel 1 in 4 and a Rune of Creation 1 in 10, its power
  drawn for the hero at the dragon's level (Realm::minionSpoils). The Box of Luck waits for the boxes.
- **Guards leave it alone** ('guards cant attack dragon'): a guard's blow is a share of what it
  strikes and took 91,200 off it a blow; no guard takes the raid's dragon on.

Retuned with all of that (build/raid --runs 120, two seed blocks): health x11.0 (1,100,000 for
ten), bite x15: 149 of 240 won (62%), the kill about 7:25, every loss its clock.

### 2b. Where it happens: where the dragon actually lands

(the user, 2026-10-06: 'fight has to happen in place where dragon actualy could land')

The arena patch (171,66) is a bench's choice, not a landing place, so the raid doesn't use it.
The fight happens where the invasion's own rule (realm_invasion.cpp) puts the dragon down:

- **In play:** the rule is unchanged. It lands 5-9 tiles from him on standable, non-safe ground,
  widening ring by ring out of town. The party fights wherever that is.
- **The demo and the headless runs** stand the party in one of **WebZen's three Lorencia boxes for
  the Dragon Event** (DragonEvent.cpp:103-109). Those are the places MU itself put dragons in
  Lorencia:

  | Box | Tiles | Checked in attributes.png |
  |---|---|---|
  | A | 135,61 – 146,70 | 120/120 open, none safe; 425/441 open within 10 of its centre |
  | B | 120,204 – 126,219 | 112/112 open, none safe; 408/441 |
  | C | 67,116 – 77,131 | 176/176 open, none safe; 435/441 |

  `--raid N [--raid-box A|B|C]` puts the party in the box's middle and starts the invasion
  (`--invasion`'s path). The dragon then lands by the realm's rule, 5-9 tiles from the hero,
  never placed by hand. The 30 s entrance plays in the window; `--raid-now` cuts it to the landing
  for tuning. The headless runs rotate the three boxes over the seeds, so the tune is not one
  field's.
- Every stage happens where it lands: the flight's strafes and the minions keep within 12 tiles of
  the landing tile, and its last stand is there too. A box's own trees and fences shape the fight.

### 2d. The fight's music

(the user, 2026-10-06: 'lets use ... taiko-invasion-epic-drums-cinematic-music ... for dragon
fight scene when character is fighting dragon')

`source/music/dragon_fight.mp3` (Vasily Atsevich's "Taiko Invasion", 2:00, from downloads/). It
plays while the dragon is roused and he or a raider is in the fight (the raid's stage is not
None and someone within 14 tiles of it is alive), looping, and fades out over two seconds after
it dies or he leaves. It is the one exception to music-is-rare's no fight music: the user asked
for it by name. No MU source has it.

### 2c. The camera draws back near a big monster

(the user, 2026-10-06: 'it make sense if camera zooms out when character is getting closer to the
big monster')

- **MU's own shape:** on a tile flagged TW_CAMERA_UP, MU's camera draws back up to
  CUSTOM_CAMERA_DISTANCE1, 200 units (2 m), 10 units a frame, and comes in again the same way
  off it (MuMain DefaultCamera.cpp:703-724, `_define.h:95`). Our grid already carries the bit
  (`kCameraUp`, content/grid.h). MU also moves the camera for its boss events (g_Direction,
  DefaultCamera.cpp:606).
- **Ours:** the same ease, keyed to a **boss-rank** monster (§3) that is alive and roused within
  14 tiles of him rather than to a tile. Its pace is MU's, 10 units a frame at 25 frames a second,
  2.5 m a second. Its reach is **3 m, from 9.5 m to 12.5 m** (**invention**: MU's 2 m is for a
  man-sized framing, and a 0.9-scale dragon with ten fighters round it needs a little more). It
  holds while the boss lives and is within 18 tiles (the gap stops it pumping at the edge), and it
  eases back in when the boss dies or is left behind. It is a drawing-only thing: the sim does not
  know it.
- **The frame:** world.cpp fixed the camera at 9.5 m on 2026-09-24 so that every cull is measured
  at one framing. A second framing has to be measured too before it lands. That means a windowed
  run in box A at 12.5 m with the dragon, the minions and the ten fighters, grepping "Metal, WxH"
  in the log, compared against budget.md's 2K line. If it overdraws, the reach shrinks; nothing
  else is borrowed. The ten drawn heroes are the bigger cost and get measured in the same run.
- MU's TW_CAMERA_UP tiles themselves are not wired. That would be its own change, after this.

## 3. The label: it must read as special (the user, 2026-10-06)

A general **rank** on a monster kind (`normal / elite / boss`, a recipe field, cooked into
`monster_kinds`). It will serve the Balrog, Gorgon and Kundun later. In the Sanctuary style: no
diamonds, no ornaments, no strokes.

- **Boss** (79, and later kBosses): the small hover bar is replaced while it is roused and within
  view by a **wide bar at the top centre**. Name in gold over it, a small line under the name
  ("Invasion — Stage 2 of 4"), thin notches on the bar at 70/40/15%, and a cast bar under it while
  a telegraph runs, naming the move ("Fire Breath", "Golden Inferno"). The enrage clock goes at its
  right end in stage 4. On the minimap, a larger gold mark.
- **Elite** (the Golden Budge Dragons): the normal hover bar, the name in gold.
- **Raiders**: a name over each and a green bar, as an escort's; the party list at the left.

## 4. How it is shown and tested

- **`--raid N`** (default 10): Lorencia, the party in one of WebZen's dragon boxes (§2b), the
  invasion begun and the dragon landing by the realm's own rule.
  **`--raid-stage S`** (1–4) starts it at that stage's threshold, so each stage can be judged
  alone. It composes with `--arena-undying` and `--mute`.
- **sim (src/sim)**: `sim/raid.h` holds the constants and stage enum. `realm_raid.cpp` holds the
  boss's think, telegraphs, hazards and minions. A fixed-size hazard list (pools, marked impacts)
  lives in Realm. Minion bodies are raised dormant with the invader, as `invaderSlot_`
  and `summonSlot_` are. **One new stream, `raidDice_`**, so no other roll moves.
  `What::Raid` (a: stage began / telegraph / impact / pool; b, c: tile; whom: target).
- **drawing (src/game)**: `play_raid.cpp` turns telegraphs into ground marks (the marker/hellfire
  ground grid; ember tint, faint), the breath into fx/firebreath at full size from bone 11, the
  strafes and storm into fx/meteor and fx/breath, the pools into a low fx/flame bed, and the shadows
  into darkened ground discs. Faint and blurred throughout.
- **content**: GoldenBudgeDragon01 (BudgeDragon01 at 0.7 in the +7 gold chrome, `summoned_in:
  lorencia`, OM breed 43). Sounds to cook: mKundunRoar, mKundunShudder, Ferea_King_Attack_Roar.
- **sim_test `testRaid`**, headless, one check per transition:
  1. ground → flight at 70%, it is aloft, melee refused, `2 + N/2` minions risen;
  2. flight → ground when the minions die, and separately on the 45 s timer;
  3. → enraged at 40%, swing quicker, a storm puts one marked impact per living fighter, pools burn;
  4. → last stand at 15%; an Inferno fells those outside the shadows and spares those in them;
  5. hard enrage at 8 minutes wipes; death ends the invasion and nothing rises;
  6. no guard takes the dragon or a minion as quarry;
  7. a raider is never `monster()`, a dead raider does not rise, a raider's kit never touches the
     hero's bag;
  8. a party of ten under the raider mind kills it on a fixed seed (`tools/raid` gives the rates);
  9. (drawing, by a windowed run, not sim_test) the camera reaches 12.5 m near the roused dragon
     and returns to 9.5 m after its death; the frame at 12.5 m is within budget.md's line;
  10. two seeded runs give identical logs, and a seeded Lorencia run with no invasion is
     byte-identical to before (raidDice_ moves nothing else).
  Stays at the standing 17 failures.

## 5. Open questions for the user

1. **Settled 2026-10-06:** no guards; ten heroes of the three classes, nine of them raiders
   (§2a), end-game: second class, levels 250-360, the best built sets at +9 to +11 with runes.
   Is the party and its kit right, and should the drawn raiders come in this sprint or after the
   headless tune?
2. **Health:** WZD's 100,000 as the start, raised by the tune until an end-game party needs 5-7
   minutes. Fine?
3. **Aloft = ranged only** in stage 2, or does it dip low enough for melee on each strafe?
4. **Loot** (settled 2026-10-06: many drops scattered round the corpse): five Boxes of Kundun
   +1..+3, 2 runes, 3 jewels and a 1-in-4 feather, plus a Box of Luck from each minion. Only the
   counts are still open.
5. **Settled 2026-10-06:** the fight happens where the dragon lands by the realm's rule; the demo
   stands the party in WebZen's three Lorencia boxes (§2b).
6. **Camera** (§2c): draw back to 12.5 m near a roused boss, at MU's camera-up pace, if the frame
   allows. Is 3 m the right reach?
