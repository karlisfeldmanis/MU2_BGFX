# Dark Lord -- the plan

MU's fifth class, ours as `Kin::DarkLord = 4`. Drafted 2026-10-23 from three read-only audits;
the full notes, with file:line for every number, sit in the session scratchpad (dl-source.md,
dl-codebase.md, dl-content.md) and their findings are folded in here. Nothing is built.

The template is the Magic Gladiator (docs/worlds/mg-port.md, the 2026-10-07 commits). Like him the
Dark Lord has **no second class** (Lord Emperor is Season 3; not ours), counts as "second" where a
reward asks it, passes gates at two thirds (`sim::moveLevel`), and gets 7 points a level.

Sources: OpenMU `ClassDarkLord.cs` and `VersionSeasonSix`, WebZen GameServer 1.00.93 (outranks OpenMU
for rules), MuMain for art and effects, **MuClient_S17_v1.19.74 for his bare body** (the user, 2026-10-23).

---

## 1. Decided (the user, 2026-10-23)

| # | question | decided |
|---|---|---|
| 1 | Command | **A fifth stat.** The raven, Critical Damage, Electric Spike, Earthshake, the Cape and his item requirements read it; the character window shows the fifth row for him only |
| 2 | Creation gate | **A level-250 hero** on the account (MU's) |
| 3 | Dark Raven | **In the first patch** |
| 4 | Pet breeding | **The Pet Trainer NPC** (MU's, Lorencia 122,110): breeds, repairs, revives |
| 5 | Jewel of Creation in the mixes | Jewel of Life in its place |
| 6 | Electric Spike's party drain | **Dropped**; +Command/10 and +50 per party member kept |
| 7 | Wings | **MU-true: no wings at all.** His only back item is the Cape of Lord at 180 (OpenMU Wings.cs:88; no 1st, 2nd or 3rd wing has a Dark Lord class level). The user, 2026-10-23: "DL has to be true on actual MU" |
| 8 | Era | **Fire Scream a skill; Chaotic Diseier only a Legendary rune's blow** |
| 9 | Release | **One release** (protocol and kept layout bump once) |

Open, for later (the user, 2026-10-23: "at the begining we will work with character model and we think about other details later"): decisions 5, 6 and 8 and Hanzo's scrolls (section 8) depart from MU -- MU drains the party on Electric Spike, has Chaotic Diseier as a scroll skill, asks Jewel of Creation in the pet mixes, and sells no DL scroll. Settle them against "DL has to be true on actual MU".

---

## 2. Class rules (sim)

From WebZen `classdef.cpp:39` / OpenMU `ClassDarkLord.cs`:

- Start: Str 26, Agi 20, Vit 20, Ene 15, **Command 25**. Life 90 (+1.5 a level, +2 a Vit), mana 40 (+1 a level, +1.5 an Ene). 7 points a level.
- Damage Str/7 + Ene/14 to Str/5 + Ene/10; attack speed Agi/10; defence Agi/7. Attack rate: WebZen's Agi·3 + Str/4 (OpenMU/MuMain say Agi·2.5 + Str/6 -- WebZen wins).
- Every DL skill x(200 + Ene/20)% (Chaotic x(200 + Ene/25)%), plus +Str/25 + Ene/50 flat.
- Starts in **Lorencia** with Short Sword, Small Shield and Force. Blood Castle / Devil Square on the MG's band.
- Weapons: scepters (his own), plus daggers, short swords, small axes and the early maces. Shields: the MU list plus his **Cross Shield**.

Code: `rules.h:268` (`DarkLord = 4`, `kKinCount 5`, `pointsPerLevel`, `moveLevel`), `rules.cpp:166-188,375`
(kRows, kStarting, kMana), (no scepter stance: MuMain holds and swings a scepter on the one-hand ladder, ZzzCharacter.cpp:345-350, 1161-1176, as ours already does), `skills.cpp:1046,1060`, `cradle.cpp:11`,
`items.h:840-848` (`kLordOnly`, **in `kEveryClass`**), `items.h:514` mask `& 7` -> `& 0x1F`,
`items.cpp:774` `elementServes` (fire, lightning), `machine.cpp:812-850` (wing switches), `raid_party.cpp:43`.
A `hasSecondClass(kin)` helper replaces ~9 `== MagicGladiator` checks; a `kLastKin` replaces ~10 clamps
(grep `int(sim::Kin::MagicGladiator)`).

## 3. Skills (Diablo 3 shape)

| ours | MU | role | notes |
|---|---|---|---|
| right-click primary | **Force** 60 | no cooldown, no mana back (MU's); lands on key 3 of its strike, never flies | a scepter with the skill option turns it into **Force Wave** 66 (a line) -- MU's own rule, kept |
| Q/W/E/R | **Fire Burst** 61 | target + everything within 3 tiles after 0.5 s | scroll |
| | **Electric Spike** 65 | line, +Cmd/10 +50 per party member | scroll; drain per decision 6 |
| | **Critical Damage** 64 | party buff 10 tiles, +Cmd/25 + Ene/30 crit dmg, 60 + Ene/10 s | scroll |
| | **Summon** 63 | party lands within 4 tiles after 7 s | scroll; refused where kanturu-port.md says |
| | **Earthshake** 62 | mounted only, 5 tiles, +Str/10 + Cmd/5 + 10·horse level | from the Dark Horse, as Fire Breath comes from the Dinorant (`SkillRow::mounted`) |
| | **Fire Scream** 78 | +Explosion 79 on a double hit | scroll |

`kSkills` 40 -> 47, rows on the end (the learned mask is u64). Cooldowns and mana are ours to set on the
bench. Animations from the shared rig (`actions.json` already has `PLAYER_DARKLORD_STAND/WALK`,
`ATTACK_STRIKE`, `*_RIDE_HORSE`, `*_DARKHORSE`, `SKILL_FLASH`, `ATTACK_TELEPORT`); re-check the indices,
the audit counted them by hand.

Effects to port (one src/game/fx/*.cpp each; MuMain sources in dl-source.md §2):
Force (m_waves + m_Piercing), Fire Burst (PierPart + DarkLordSkill), Electric Spike (NSkill flare),
Summon (Circle01 + circle light), Critical Damage (DarkLordSkill on the weapon bones), Earthshake
(shock waves, ground stones EarthQuake01-08, camera shake), Fire Scream (darkfirescrem01/02 + bombs).
Sounds: SOUND_ATTACK_SPEAR, FIRE_BUST, ELEC_STRIKE, PART_TELEPORT, CRITICAL, EARTH_QUAKE, FIRE_SCREAM
(a new sound needs the showing cook).

## 4. Pets

**Dark Horse** (13/4) -- the third mount in slot 12 (docs/game/mount.md). Asks level 218 + 2·horse level.
Defence +5 + Agi/20 + 2·level, damage taken -15..40%. Draw `Skill/DarkHorse.bmd` (13 dk* sheets) ridden,
`DarkHorseHorn.bmd` as the item.

**Dark Raven** (13/5) -- worn in the **left hand** (no shield with it). Damage 180-200 + 15·level, +Cmd/8..Cmd/4,
a scepter's magic power adds; strikes every 1500 - speed·10 ms within 5 tiles; four modes (normal, random,
with owner, target -- OpenMU RavenCommandManager.cs). Asks Command 185 + 15·level. Built on the elf's summon
(`realm_summon.cpp`: a body at the end of `bodies_`, hunting, kills are his), but worn, flying, never dies
(loses durability instead). Draw `Skill/darkspirit.bmd` (GOBoid.cpp), item `spiritbill.bmd`.

**Both**: pet level 1-50, exp (10 + L)·L³·100, a pet at 0 durability loses 10% of its level's exp. Level and
exp are new kept bytes. Bred at the Trainer from a **Spirit** (13/31): horse 5 Bless + 5 Soul + Chaos + Life
+ 5,000,000 zen, raven 2 + 2 + Chaos + Life + 1,000,000, 60%.

## 5. Runes (`kLordOnly`, weapon, appended from Power 78 -- the run must stay unbroken)

The elf's count: Common 2, Rare 1, Epic 3, Legendary 3 (one `second`), plus 4 parked Ancients.
Every shared rune (34 rows) opens to him through `kEveryClass`; of the elements he takes Inferno and Thunder.

| family | Common | Rare | Epic | Legendary |
|---|---|---|---|---|
| Fire Burst (fire) | **Firebrand** FB +15% | | **Conflagrate** FB +30%, 15% burn 3%/s | **Sunburst*** FB +50%, 20% bursts into four (late) |
| Earthshake (lightning) | **Tremor** 5% faint earthshake around you | | **Quake** 7% lesser earthshake | **Earthbreaker** 10% earthshake (first-class signature) |
| Raven | | **Talon** raven +15% | **Murder** 20% raven hits a second monster | |
| standalone | | | | **Diseier** 10% on hit, Chaotic Diseier bolts 3 ahead (Tarkan) |
| Ancient (parked, Kanturu) | Plasma Storm, Dragon Roar, Phoenix Shot, Lightning Shock -- other classes' spells, as the DW's four |

Reuse: Kindle / Scorch / Pyroblaster re-aimed at Fire Burst; Ashfall / Brimstone / Hellfire / Ring of Fire for
the "around you" shape; Stormcall for Diseier's strike. New: the earthshake look, Diseier's look, the raven.
If decision 1 makes Command a stat: **Lesser / Greater Dominion** (+Command, Rare / Epic, any socket, his alone).
`PowerRow::takenBy(kin, isSecond)` must get `true` for him, or Sunburst and his Ancients never set.
Rune hero levels follow the table (20/80/150/250/380).

Names checked against src, content and docs/game: none taken (Smoulder is a talent, so the Common is Firebrand; Quake only names a placement field).

## 6. Quest rewards (docs/game/quest-rewards.md; sockets 1,2,2,3,3,3 as the knight)

Native wherever the knight is (`questNative`, quests.h:174, as the MG). **`kQuestPaid` 24 -> 32** (Devin
reaches 24 rows and the Phoenix 25 with his).

| quest | Dark Lord is paid |
|---|---|
| Marlon | Flail (1 socket), Firebrand + Tremor, Scroll of Fire Burst |
| Devin | Battle Scepter (2), Lesser Ascendance, Lesser Might (or Dominion), Scroll of Critical Damage |
| Catacombs | Light Plate mask + gloves, Keen Eye, Scroll of Electric Spike |
| Halls | Light Plate pants + boots, Serpent Shield + Steadfast, **Spirit of Dark Raven** |
| Pit | Light Plate armour, Scroll of Fire Scream |
| Lirien | Master Scepter (2), **Earthbreaker**, Quake, Talon (no Buckler: he has no guard skill) |
| Tersia 1-5 | Adamantine a piece a floor; LT2 Greater Might/Dominion; LT4 Scroll of Summon; LT5 **Cross Shield +5** (2) |
| Tersia 6 | Conflagrate, Murder |
| Balrog | 2 Bless + 2 Soul, Wrath, Stormcall (as everyone) |
| Kantur's Legion | Great Scepter +7 (3), **Diseier**, Inferno |
| Kantur's Captain | Great Lord Scepter +7, **Spirit of Dark Horse** (offered as to the MG, `row.second`) |
| Sky Door | Dark Steel gloves + boots, Greater Ascendance, Thunder (no wind in his kit) |
| Phoenix | Lord Scepter (3), Dark Steel mask + pants + armour, **Sunburst**, Evil Spirit, **Crest of Monarch** every clear |
| Sevina | not his |
| Seed Master, Aida | as everyone |

Weapon ladder against the knight's: Flail 22 / Battle 54 / Master 72 / Great +7 82 / Lord 98 / Great Lord +7 140.
Sets: A Light Plate (42-62), B Adamantine (66-78), C Dark Steel (86-96). `testQuestLadder` guards it.

## 7. Items to cook

Every model and sheet below is present in MuMain's Data (checked); our file = MU index + 1.

**Body** -- `HelmClass05`..`BootClass05` from **S17** (`third_party/MuClient_S17_v1.19.74/Data/Player/`,
1.2-3.2x MuMain's bytes, helm the most; sheet HQlevel_man02), MuMain's Class05 as the fallback, into
`content/players/DarkLordBare.json` as `MagicGladiatorBare.json`. Bust `Logo/NewFace05.bmd` + `darkload_face`.

**Scepters** (group 2, DL only; each a held-light case in `src/game/fx/held_lights.cpp`, MuMain lines in dl-content.md §3)

| ours | MU | drop | worlds |
|---|---|---|---|
| Mace09 | Battle Scepter | 54 | Dungeon, Atlans, LT |
| Mace10 | Master Scepter | 72 | Atlans, Tarkan |
| Mace11 | Great Scepter | 82 | Tarkan, Icarus, DS |
| Mace12 | Lord Scepter | 98 | Icarus, BC |
| Mace16 | Shining Scepter (CW_Mace) | 110 | Aida |
| Mace18 | Absolute Scepter | 135 | Aida |
| Mace13 | Great Lord Scepter | 140 | Aida top, Captain |
| Mace14 | Divine Scepter of Archangel (saint) | 150 | Blood Castle reward |
| Mace15 | Soleil Scepter (HDK_Mace) | 146, lvl 380 | dormant, with the 380 gear |

Skipped: Stryker (gamble item). Frost Mace is not his.

**Armour** (each with a full-set row as `kGladiatorSet`)

| set | drop | worlds | model |
|---|---|---|---|
| Leather / Bronze / Scale | 10 / 18 / 28 | Lorencia, Noria, Devias | existing: add "lord", helms too |
| Light Plate | 42-62 | Dungeon, LT, Atlans | *Male26 |
| Adamantine | 66-78 | Atlans, Tarkan | *Male27 |
| Dark Steel | 86-96 | Tarkan, Icarus | *Male28 |
| Glorious | 97-105 | Icarus, BC | CW_*Male05 -> *Male39 |
| Dark Master | 106-117 | Icarus top, Aida | *Male29 |
| Paewang | 111-132 | Aida | *Male52 |
| Sunlight | 130-147, lvl 380 | dormant | HDK_*Male05 -> *Male34 |
| Cross Shield | 70 | Atlans, Tarkan; LT5 reward | crosssheild.bmd -> Shield22 |

There is no "Great Lord" armour set in MU; it is only a scepter. Ancient sets Agnis / Broy: not now.

**Wings, pets, misc**

| item | needs |
|---|---|
| Cape of Lord (13/30, lvl 180) | `DarkLordRobe.bmd` + sleeve. MuMain draws it as **cloth physics** (ZzzCharacter.cpp:8435) -- the hardest item; first try is a rigged cape. Second-wing mix with Crest in place of Loch's Feather; a free wings number (12/22 is Darkness) |
| Crest of Monarch | Loch's Feather +1, draws `Quest05.bmd` |
| Dark Horse, Dark Raven, two Spirits | see §4; `DarkHorseSoul.bmd` for the Spirit |
| Scrolls x6 | one model `skillScroll.bmd` (dkScroll sheets); MU's numbers 12/21-24 clash with Skillshot and Darkness, take free ones |
| Fruit of Command | only if Command is a stat |

Cooks: wardrobe (new gear), every world's tables (new items), figures (new body), showing (new sounds),
`./mu make realm` + rebuild. Plan before each; ask before the ones that change what the game shows.

## 8. NPCs and shops

- **Pet Trainer** (226, `Npc/Breeder.bmd`), Lorencia 122,110: breeds, repairs and revives the pets. New NPC + window.
- **Hanzo** sells the Mace / Morning Star / Flail to him and the first two scrolls (Fire Burst, Critical
  Damage) beside the knight's orbs (ours; no MU merchant sells them). Scepters and later scrolls drop or are quest pay.
- Lobby: a fifth class button behind the gate.

## 9. Talents (6 columns, passive, generated by pipeline/talents_table.js -- `KIN` map at :176)

Scepter (Force, Force Wave, one-hand + shield) / Fire Burst (fire, Fire Burst, Fire Scream) / Earthshake
(lightning, Earthshake, Electric Spike; a "while mounted" hold is new) / Raven (the elf's Summon* stats) /
Command (Critical Damage, Summon, party: Patron, Iron Will) / Fortune (shared). No keystones, no cooldown
reduction, no movement speed. Canvas first (claude.ai/artifact/RTLpPhd7Prn4G6eX86FCaL). At 7 points a level
he gets the MG's 111 points at 400. Six columns stay under `kTalentColumnsMost` 7.

## 10. The client

Every place sized for four or ending at the MG: `capsule.cpp:14-20` (`kKinShort/kKinInk`),
`desk.cpp:1335` `faces[8]`, `lobby.h:45` `kClassRows`, the `kClass0..3` enum, `args.cpp:561`,
`describe.cpp` name tables, the character window (`card.cpp:185,743`: a fifth stat row in the fixed 190x429
window), rebirth window, signin JSON, bots' `build[4]`, sim_test loops over three classes.

## 11. Release

- Rules print moves with any src/sim change: **keep drafts out of src/sim** until the patch starts.
- Protocol 48 -> **49** (wire.h:172): the new kin, Spend's fifth stat, raven mode command, new skills.
- Kept layout 14 -> **15** (wire.h:187): `points.command`, `coolsLeft` for 47 skills, pet level/exp;
  `takeKept` still reads 14 and older.
- Client and servers ship together. Site copy lists classes (web/site, five languages; account.html already badges "Dark Lord").

---

## Phases (each one shown in the studio before the next)

1. **Body** -- S17 Class05 bare body, the bust, the stand/walk clips; show him in the studio.
   **Done 2026-10-23:** content/players/body/*Class05 (S17, 1774 triangles, 51 bones, sheets
   HQlevel_man02 + HQhair_R added as MU's bright mesh), content/players/DarkLordBare.json,
   content/lobby/NewFace05 (MuMain bust, `separate_meshes`), figure tables cooked for every world.
   Dev switch `MU2_BARE_BODY=DarkLordBare` (game/world/world.cpp) draws your hero in his body.
   His clips: **76 stand** (the raven on his arm), **77 walk**, 78/79 stop/run on the horse,
   80 Force (Attack strike). content/players/rig/actions.json labels every action from 75 up one
   slot high: MuMain's `PLAYER_FLY_RIDE = PLAYER_ATTACK_END` (_enum.h) is an alias, not a new
   action. **Not fixed, on purpose** (the user, 2026-10-23: "if it will break other systems its bad idea"): the table's speeds feed sim::castTicks, and some slots were already corrected by hand. Use his clips by their real numbers.
2. **Class** -- `Kin::DarkLord`, every switch and clamp, Command (decision 1), creation behind the gate, the character window; sim_test green.
3. **Primary and gear** -- Force / Force Wave and its effect, scepter stance, the first scepters and Light Plate cooked; he fights in Lorencia.
   His clips' speeds first: actions.json entries 76-89 only (his and the horse rider's, used by no code), each given its real MU play speed -- Force 0.25 (now 0.3), Earthshake 0.2 (0.3), the horse idles 1.0 (0.2). The rest of the table stays (2026-10-23).
   **Done 2026-10-23** (82773543 and the next): the runtime speeds win over ZzzOpenData's load-time ones, as everywhere in the table -- walk and horse run 0.33 (ZzzCharacter.cpp:532-533, every frame), Force 0.25 and Earthshake 0.3 (SetAttackSpeed, :949, :1013). Index and figures/clips/player.muc re-cooked on the branch.
   **Force done and approved 2026-10-23** (the user: "perfect"; f534176f .. 89c76a46): MU's row, born known, his primary; WebZen's (200 + Ene/20)% and Str/25 + Ene/50 + 10-15 before the defence; let go and landed on STRIKE's key 3 of 7 (`release` 3/7), never flown (`flies` 1000), he snaps onto the body; fx/force.h (MODEL_PIERCING2, two MODEL_WAVES on edge across his line, the BITMAP_PIERCING streaks, sDarkSpear), **all in front of him** (the user: "only frontal"); icon off newui_skill2; strike clips 80/82 in the tables cook. Still to do here: the scepters, Force Wave (66: a scepter with the skill option, 50-75 to every monster in a 1-wide, 4-long line), Light Plate.
4. **Skills** -- Fire Burst, Electric Spike, Critical Damage, Summon, Fire Scream: sim then fx, one at a time, judged in game.
5. **Pets** -- Pet Trainer, Spirits, Dark Horse + Earthshake, Dark Raven + modes, pet levels.
6. **Rest of the gear** -- Adamantine, Dark Steel, Glorious, Dark Master, Paewang, Cross Shield, the later scepters, Cape of Lord, Crest.
7. **Runes and quests** -- his nine runes, the ladder rows, `kQuestPaid` 32, `testQuestLadder`.
8. **Talents** -- canvas, table, bench.
9. **Balance** -- a level-150 and level-400 kit in tests/kits, `tests/skill_sweep.cpp` against the class targets.
10. **Bots, site, release** -- netbot builds, site copy, protocol 49 / kept 15, ship together.
