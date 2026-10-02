# The first mount: the Horn of Uniria (13/2)

Researched 2026-10-02. Step 1 (the models) done the same day; nothing in the sim or the game yet. The 0.75 mount: group 13 number 2, the third of the
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
- **Price** (WebZen `zzzitem.cpp:2433-2439`, OpenMU the same): `dropLevel^3 + 100` = 15,725,
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

1. **Speed 17, above the run.** The user, 2026-10-02: "mount has to be faster that runing". Our
   run is 14 (`kRunFactor` 14/12), so MU's 15 would be 7% faster and hardly felt. 17 is OpenMU's
   `HorseOrFenrirMovementSpeed`, MU's own number for the later mounts, ~21% over the run.
   **ours** on Uniria.
2. **Rides in combat too.** MU's rider never walks: clip 36/37 and full speed whether fighting or
   not. Our rule makes a man on foot walk in combat. Proposed: mounted, he rides at 17 in and out
   of combat, so the horse is the way out of a fight. *Open: user's call.*
3. **Skills unblocked**, as 0.75: knight skills play clip 68, spells 156, the elf's bow 58/59.
   *Open: whether the skill clip on the horse reads well enough, judged in game.*
4. **Town: the horse goes, he walks.** MU's alpha-0 cut, closed with a short fade **ours**
   (`visible-mu-pops-get-closed`). *Open: fade or MU's cut.*
5. **Wear damage/100 per hit taken**, WebZen, as the pets. Destroyed at 0, no repair.
6. **Sold on Lumen's shelf** beside the pets, 15,725 **ours** (0.75 sells it nowhere), and in
   the jewel drop group from monster level 25.
7. **Pet card**: type "Mount", "Moving speed" line (MuMain GT 68), `Life n / 255` in the foot; the
   buff strip shows its cell with Life as the bar, as the pets do.

## Done

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

## Steps

1. **Import and build** `Rider01.bmd` (horse, clips 0/2/3/6) and `Helper03.bmd` (the bag item)
   through `source/` + `tools/asset.sh`; cook. Bench the horse alone.
2. **Sim**: 13/2 in `PetPower`, the speed in `realm_move.cpp` (mounted and off a safe tile: 17,
   combat or not), wear in `Realm::wearOnTaken`, the price and shelf, `testMount` in sim_test.
3. **Figure**: a second figure at the rider's position and yaw, the rider's clip table switched
   to the ride clips off a safe tile, the horse's clip driven by the rider's, faded in town.
4. **Dust** off the hooves (brown, white in Devias), the card and the buff cell.

Test: `build/mu2 --world lorencia --play --level 30 --give Helper03:1:W`.
