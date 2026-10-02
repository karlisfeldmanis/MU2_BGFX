# The first mount: the Horn of Uniria (13/2)

Researched 2026-10-02. Steps 1-4 (models, sim, riding, hooves and dust) done the same day; the card's polish is left. The 0.75 mount: group 13 number 2, the third of the
helpers after the Guardian Angel and the Imp (`docs/pets.md`). Sources: OpenMU Version075,
MuMain (a Season 6 fork, read for the client), WebZen GameServer 1.00.93's 0.97d base (outranks
OpenMU on rules, `docs/webzen-audit.md`).

## What MU does

### The item
- **Slot 8, the helper slot** (OpenMU `Version075/Items/Pets.cs:36,52`; WebZen `ItemDef.h:34`
  `EQUIPMENT_HELPER`). Uniria, the Angel and the Imp shut each other out: a mount or a pet, not
  both. 1x1, Life 255, level 25, all three classes, the jewel drop group like the pets.
- **Stats: none but speed.** OpenMU gives MovementSpeed 15 (`MovementSpeedConstants.cs:35`
  `BasicMountMovementSpeed`). WebZen has no damage, defence or absorb code for 13/2 anywhere.
  Its one effect there is that a player rider is never put into hit stiffness (`ObjAttack.cpp:
  2734-2739`), which is a player-against-player rule and means nothing here. MuMain's tooltip has
  only `Life n`.
- **Wear** (WebZen `user.cpp:10615-10620`, `gObjSpriteDamage`): every hit taken wears it by
  damage/100. Attacking does not wear it. At 0 it is deleted (`:10750-10763`). It can't be
  repaired: 13/0-3 cost 0 and are refused (`protocol.cpp:8570-8588`). OpenMU wears it by
  damage/100000, a thousand times slower. Our pets already follow WebZen (Angel 3/100, Imp 2/100).
- **Price** (WebZen `zzzitem.cpp:2433-2439`, OpenMU the same): `dropLevel^3 + 100` = 15,725, shown 15,700 (prices round to the hundred),
  a third back on sale.
- Ten 255-Life horns + a Chaos make a Dinorant at 70% (WebZen `MixSystem.cpp:2156-2240`). That
  comes later.

### Riding (MuMain)
- **No bone link.** The mount copies the rider's position and yaw every frame (`GOBoid.cpp:515,
  524`). The seat is built into the player's ride clips, so the horse is a second figure on the
  same spot. Scale 0.9 at spawn, 1.0 drawn (`:111-114, 684-691`). Model `Data/Skill/Rider01.bmd`
  with sheets `Item/unicon.OZJ` and `unicon01.OZT`; the bag icon is `Player/Helper03.bmd`.
- **Horse clips** (`GOBoid.cpp:525-595`, play speed 0.34): 0 idle, 2 moving, 3 attacking, 6 skill.
- **Rider clips** (all in `player.bmd` and named in `source/players/rig/actions.json`):

| when | clip |
|---|---|
| stand, bare hands | 13 `PLAYER_STOP_RIDE` |
| stand, armed | 14 `PLAYER_STOP_RIDE_WEAPON` |
| move (no walk pose: a rider always runs) | 36 `PLAYER_RUN_RIDE` / 37 `_WEAPON` |
| attack: fist or one-hand, staff included | 54 `PLAYER_ATTACK_RIDE_SWORD` |
| attack: two-hand | 55 `..._TWO_HAND_SWORD` |
| attack: spear / scythe and polearms | 56 / 57 |
| attack: bow / crossbow | 58 / 59 |
| a spell | 156 `PLAYER_RIDE_SKILL` (`ZzzCharacter.cpp:1326-1328`) |
| a knight skill, an elf buff | 68 `PLAYER_SKILL_RIDER` (`ClassAttack.cpp:1807`) |
| hit | none: `SetPlayerShock` returns early for a rider (`:1368`) |
| die | 232 Die 1, falls off as on foot |

  (`ZzzCharacter.cpp:283-291, 567-583, 1112-1153`.)
- **Speed** (`ZzzCharacter.cpp:6320-6335`): 15 at once, the running speed, against the walk's
  12. MU's point is that a rider skips the 1.6 s walk into a run.
- **Dust**: half the frames a moving horse kicks `BITMAP_SMOKE+1` (brown) at ±32 XY, white
  `BITMAP_SMOKE` in Devias (`GOBoid.cpp:542-556`). No hoof sound exists in MuMain (only the Dark
  Horse and Fenrir have run sounds).

### Skills while riding
- **0.75: nothing is blocked.** WebZen's server refuses no skill or attack on Uniria.
- MuMain (S6) lets a mounted knight use only a short list (Impale, Death Stab, Rider, Fire Burst,
  ... `SkillExecution.cpp:329-360`). **None of our five knight skills are on it**, so that list
  would leave him with nothing. The list belongs to later seasons.
- The wizard and the elf cast anything mounted, on the two clips above.
- Impale (47, "must be riding Uniria", `public.h:222`) is 0.95d+, not ours.

### The safe zone (MuMain)
- **No dismount.** The horse stays equipped, but in a safe tile it is drawn at alpha 0
  (`GOBoid.cpp:498-502`), and every riding branch is gated `!c->SafeZone`. In town he stands and
  walks on foot, weapon slung (`:15243`), at the walk's 12 (`:456-459`). Step out and the horse
  is under him again.
- This matches our town rule as it is: walk bare, weapon on the back, no skills on a safe tile
  (`realm_skills.cpp:142`).

### Maps
- Atlans refuses Uniria (it can't swim), Icarus wants wings or a Dinorant, Kanturu and Chaos
  Castle refuse it (WebZen `user.cpp:15988, 26244-26316`). None are our maps. Lorencia, Noria,
  Devias, the Dungeon and the Lost Tower all allow it.

## Ours (proposed)

1. **Speed 16, above the run.** The user, 2026-10-02: "mount has to be faster that runing". Our
   run is 14 (`kRunFactor` 14/12), so MU's 15 would be 7% faster and hardly felt. 17, OpenMU's
   `HorseOrFenrirMovementSpeed`, was tried first and was 'litttle bit to fast'; 16 is ~14% over
   the run. **ours** on Uniria.
2. **Rides in combat too.** MU's rider never walks: clip 36/37 and full speed whether fighting or
   not. Our rule makes a man on foot walk in combat. Proposed: mounted, he rides at 16 in and out
   of combat, so the horse is the way out of a fight. *Open: user's call.*
3. **Skills unblocked**, as 0.75: knight skills play clip 68, spells 156, the elf's bow 58/59.
   *Open: whether the skill clip on the horse reads well enough, judged in game.*
4. **Town: the horse goes, he walks.** MU's alpha-0 cut, closed with a short fade **ours**
   (`visible-mu-pops-get-closed`). *Open: fade or MU's cut.*
5. **Wear damage/100 per hit taken**, WebZen, as the pets. Destroyed at 0, no repair.
6. **Sold on Lumen's shelf** beside the pets, 15,700 **ours** (0.75 sells it nowhere), and in
   the jewel drop group from monster level 25.
7. **Pet card**: type "Mount", "Moving speed" line (MuMain GT 68), `Life n / 255` in the foot; the
   buff strip shows its cell with Life as the bar, as the pets do.

## Done

- **The mount's own slot** (2026-10-02, ours; the user: "create new equipment slot, for
  mounts"). `sim::kMount` is worn slot 12, so `kWorn` is 13 and the bag starts one further on.
  Uniria and Dinorant go there (`placeOf`), the Angel and the Imp stay in slot 8, and a pet and
  a mount are worn together: `rearm` multiplies their prices and gifts, `wearOnTaken` wears each,
  the HUD shows both life bars and `Pets` draws both. In the bag it stands in the narrow column
  right of the armour, 25x46. Its ghost is `win_ghost_mount.png`, Rider01 side on out of
  `pipeline/mount_ghost.py` -- MU has no mount plate to cut one from. Save version 2: a version
  1 file's bag slots move up one and a horn worn in slot 8 moves to the mount's slot.
- **Step 1, the models** (2026-10-02). `source/items/pets/Rider01` is the ridden horse (MuExtract
  off MuMain's clean `Skill/Rider01.bmd`: 24 bones, 478 triangles, 4 clips, play speed 0.34) and
  `Helper03` the horn, the bag item and the drop (42 triangles, static, no rig). Sheets
  `source/textures/unicon.png` (128², opaque) and `unicon01.png` (32², the mane, cut by its own
  alpha). Both tiled, as the Imp, so the mane's alpha does not bleed off an atlas neighbour:
  the horse is fur + hair, the horn bone + leather. Built and synced into `assets/items/pets/`;
  **not indexed and not cooked**, as the pets were, until the sim equips 13/2. Only Helper03
  carries the item row, so the horse can never be a drop.
- **What the clips are**, rendered in Blender: Uniria stands on its hind legs, MU's own design,
  not a bad bind pose. 0 idle, 1 a near copy never played, 2 a long bounding leap (the run,
  locked in place), 3 a head-down butt of the horn (the attack). `--model` draws the rest pose,
  rearing; it is not the stance.

Bench: `build/mu2 --model items/pets/Rider01/Rider01.glb --dist 4`.

- **Step 2, the sim** (2026-10-02). 13/2 goes in slot 8 (`placeOf`); `PetPower::mount` and wear
  0.1/10, damage/100 a hit taken, through the pets' `wearOnTaken`, destroyed at 0. `Body::riding`
  is set every tick in `Realm::advance`: a mount with life left, off a safe tile, walking or not,
  in a fight or out. The step covers `strideFactor` ground (`kRideFactor` 16/12 riding,
  `kRunFactor` running), read by the drawing's pace too so the two cannot part. On Lumen's shelf,
  cell 4, at 15,700. Indexed and cooked (tables on all five worlds, figures). The buff cell is
  `buff_uniria`, the rearing horse on a dusk violet (`pipeline/model_icons.py`), with "Movement
  speed +21% over running"; the card says "Moving speed: ride outside town, faster than running".
  sim_test `testMount`: 20 ticks east of (200,160) cover 2.75 tiles on foot, 3.54 mounted.
  Nothing is drawn yet: he still walks and runs on foot, only faster (step 3).
- **Step 3, ridden** (2026-10-02). `game/pets.h` draws Rider01 on his spot and facing at 1.0,
  faded 0.25 s at a safe zone's edge (ours; MU cuts Alpha to 0), its clip GOBoid's off his: 2
  riding on, 3 a weapon's swing, 0 otherwise. He rides on 13/14 and 36/37 (bare while slung,
  armed while drawn), swings 54-59 by stance (dual Kris too, every blow on the right hand's 54:
  MU has no paired ride swing), casts 156 as a wizard and 68 otherwise; an elf's arrow skill
  takes the ride bow. The run ride plays at MU's 0.34 (actions.json cooked 0.3) and the horse's
  clip takes his clip's place as a fraction each frame, so the bounces hold together (the user:
  "char is not perfectly synced with mount bouncing"). **Ours**: a drawn bow or crossbow on the
  horse holds the first key of its ride shot between shots, as MU's stop ride weapon laid the
  bow flat through her thigh ("bow holding looks incorrect"). Checked in the arena against
  Hounds: two-hand Slash, dual Cyclone, Berdysh Falling Slash, sword and shield Lunge, wizard
  Fire Ball, elf bow Triple Shot and crossbow. Monsters on the neighbouring tiles stand inside
  the horse's long body, as they would in MU. `--give NAME:1:H` wears a weapon in the off hand.
- **Seated standing clips** (2026-10-02, the user: "we need to figure out how to make dual vield
  attack on mount", then "try 1", "we need also two hand weapon stance on mount", "not only two
  hand stance bt also two hand attack"). `Figure::seat(clip)` takes Bip01, the pelvis and both
  legs from a second clip on its own clock; `Figure::upper(clip)` the spine and above. On a horse
  any clip that is not one of MU's ride clips (`rideAction`) is seated on the armed stop ride, 14:
  the knight's pair keeps its four standing blows, 39 41 40 42, and a two-handed sword, spear or
  scythe its own standing blow (43, 46, 47) rather than the one-handed-looking ride swings 55-57.
  Drawn and not swinging, a two-handed weapon holds its standing grip over the ride stance
  (`upper`), the ride clip staying the main one so the horse's sync holds. All **ours**. Found on
  the way: a pair's counter was stepped by both halves of the hero's blow, 0 2 0 2, so only the
  right hand ever swung -- on foot as well; counted now only where the clip plays.
- **Hooves and dust** (2026-10-02). `mount_hoof` is MU's Dark Horse pHorseStep1-3 at random,
  two a bound of the run ride (ours on Uniria; MuMain gives it nothing). `game/fx/dust.h` is
  GOBoid's dust, a puff every second reference frame at the galloping horse +-32: smoke02 brown,
  and on Devias MU's white smoke01, added so its black is nothing. Ours, the user's taste ("dust
  is to much vissible", "little bit more blury and less vissible", "in devias it has to be
  white"): a faint ground haze, 1.1 times MU's size at 0.15 strength (snow added at 0.35),
  faded in, held on the ground where MU's BITMAP_SMOKE rises.

## Steps

1. **Import and build** `Rider01.bmd` (horse, clips 0/2/3/6) and `Helper03.bmd` (the bag item)
   through `source/` + `tools/asset.sh`; cook. Bench the horse alone.
2. **Sim**: 13/2 in `PetPower`, the speed in `realm_move.cpp` (mounted and off a safe tile: 16,
   combat or not), wear in `Realm::wearOnTaken`, the price and shelf, `testMount` in sim_test.
3. **Figure**: a second figure at the rider's position and yaw, the rider's clip table switched
   to the ride clips off a safe tile, the horse's clip driven by the rider's, faded in town.
4. **Dust** off the hooves (brown, white in Devias), the card and the buff cell.

Test: `build/mu2 --world lorencia --play --level 30 --give Helper03:1:W`.

# The second mount: the Horn of Dinorant (13/3)

Started 2026-10-02 (the user: "lets work on next mount", "dyno"). **Not 0.75**: OpenMU's
Version075 stops at 13/2, and the Dinorant is 0.95d's. Every rule below is marked from there.

## What MU does

- **Item** (OpenMU `Version095d/Items/Pets.cs:40`): 13/3, slot 8, level 110, Life 255, never
  dropped. Damage dealt x1.15, taken x0.9, speed 15, `IsDinorantEquipped` (it flies), and the
  Fire Breath skill. WebZen 1.00.93: each x1.15 blow costs the rider 1 HP; it absorbs 10%, 15%
  with its option (ObjAttack.cpp:1293-1328); options max AG, +5 attack speed, absorb
  (zzzitem.cpp:1225-1243); wears damage/200 a hit taken (user.cpp:10623-10628); not repaired.
- **Made, not found**: ten 255-Life Horns of Uniria and a Jewel of Chaos in the Chaos Machine, 70%
  (WebZen MixSystem.cpp:2156-2240). There is no Chaos Machine here yet.
- **Fire Breath**, skill 49 ("Raid Shoot" in WebZen, AT_SKILL_RIDER in MuMain): the knight's,
  carried by the item, not learned (zzzitem.cpp:1018-1022, user.cpp:3746); damage
  `(200 + Energy/10) / 100` of his swing (ObjAttack.cpp:1392-1407). MuMain plays PLAYER_SKILL_RIDER
  68 with a BITMAP_SHOTGUN effect and SOUND_SKILL_SWORD3 (ZzzCharacter.cpp:4406-4409).
- **Drawn** (MuMain): `Skill/Rider02.bmd`, MODEL_PEGASUS, scale 0.9 then 1.0, Velocity 0.34. Eight
  clips in ground/flying pairs: 0/1 idle, 2/3 moving, 4/5 attack, 6/7 the rider skill; the odd
  ones in Tarkan, Icarus and the Maya scene. On a ground map the rider is lifted 30 off the
  terrain (ZzzCharacter.cpp:6381-6390) and the dragon drawn 30 under him, on the ground
  (GOBoid.cpp:517-523). Its tooltip: "Increase 15% of Damage", "Absorb 10% of Damage".
- **Maps**: it may enter Icarus (wings or a Dinorant), not Atlans; none of ours rule it out.

## Done

- **Step 1, the models** (2026-10-02). `source/items/pets/Rider02` (MuExtract off MuMain's clean
  Data: 73 bones, 922 triangles, 8 clips at 0.34; sheets `rdgon.png` 256², `rdgonw.png` 128x64
  the wings, cut by their own alpha) and `Helper04` the horn (44 triangles, `reddragont.png`),
  carrying the 13/3 row. Tiled, the wings on foliage, lit through as the Imp's. Built and synced,
  not indexed. Bench: `build/mu2 --model items/pets/Rider02/Rider02.glb --dist 5`.
- **Step 2-3, ridden** (2026-10-02). The user's picks: sold on Lumen's shelf (cell 5, there being
  no Chaos Machine), level 160 (drop level with it, so 4,096,100 Zen), MU's powers only.
  `PetPower`: the ride, x1.15 dealt for 1 life a blow, x0.9 taken, wear damage/200. Drawn by
  `game/pets.h` as Uniria's horse is, off Rider02: its attack is clip 4 where Uniria's is 3; the
  rider sits `kDinorantLift` (MU's 30) over the ground and the dragon on it. The bounce: MU plays
  one run ride on both mounts and Uniria's back rises and falls with it key for key, the
  Dinorant's does not (+16 -16 -3 +11 -19 -5 +17 against his pelvis's +7 -18 +7 +6 -17 +8 +7),
  so on the run his seat follows the dragon's back (`dinorantBob`, ours; the user: "bouncing with
  dyno is not perfectly synced"). Buff cell `buff_dinorant`, the dragon on steel blue. sim_test
  `testDinorant`. Not built: Fire Breath, its options, flight -- not chosen.
