# Pets: the Guardian Angel and the Imp (Satan)

Started 2026-09-29. The two 0.75 helpers, group 13 numbers 0 and 1. The Horn of Uniria (13/2)
is 0.75 too and is not in this pass.

## Done

- **Imported** from MuMain's clean `bin/Data`: `Player/Helper01.bmd` (Angel, the .bmd calls
  itself `fairy.smd`) and `Player/Helper02.bmd` (Imp, `satan.smd`), each 2 meshes, 8 bones and
  one 7-key action; sheets `Item/fairy.OZJ` 16², `fairy2.OZJ` 32², `satan.OZJ` 16²,
  `satan2.ozt` 32² with alpha. Recipes in `source/items/pets/`, sheets in `source/textures/`.
- **Built** with `tools/asset.sh Helper01|Helper02` and synced into `assets/items/pets/` with
  `sync_one.py`. The Angel is `tiled` so its wings (MU's `BlendMesh = 1`) are additive and on a
  shadowless node; the Imp bakes, wings cut by their own alpha. Both play action 0 at MU's 0.5.
- **Not indexed and not cooked**, on purpose: `index.json` is the drop pool, and both are in
  the 0.1% jewel group from monster level 23/28 (Devias's Hommerd and Yetis), while nothing in
  the sim can equip group 13 yet. Run `pipeline/index.py` only when step 1 below lands.

Bench: `build/mu2 --model items/pets/Helper01/Helper01.glb` (static; `--model` plays no clip).

The Imp is tiled too since 2026-09-30 (the user: "something wrong with IMP wings"). Baked, its
96² atlas packed each wing quad flush against a body island, so the quad edges sampled the
body's opaque alpha -- two thin dark lines off the wings -- and the wing seen from its unlit
side shaded near black. Now `satan2` is its own sheet, cut by its alpha and on `foliage`, lit
through from both faces as Bat01's wings are; the .glb went from 900 KB to 44 KB.

## What MU does (MuMain, a Season 6 fork; the core of both is era logic)

**Guardian Angel flies.** `ChangeCharacterExt` (ZzzCharacter.cpp:12650-12662) creates a mount
object only for helper 0. `GOBoid.cpp`:

- Spawn (:67-126): scale 0.7, fades in, light (3,3,3), Velocity 0.5 as its play speed, at the
  owner ±256 XY and 128-256 up.
- Move (`MoveMount`, :605-660), per 25 Hz reference frame: XY distance to the owner against
  `FlyRange = 150`. Outside it, turn toward the owner at up to 20°/frame. Forward is local −Y
  along `Direction`; about 1 frame in 32 it re-rolls speed: −12.8..−19.1 outside the range,
  −1.6..−7.9 inside with a random heading, and a vertical ±3.2. Height jitters ±8 a frame and is
  pushed back into 100-200 above the owner by ±1.5. Four grey `BITMAP_SPARK` a frame within ±8.
- Render (`RenderMount`, :673-708): scale 1.0 (1.2 on the character screen), and a green-ish
  `BITMAP_LIGHT` sprite of size 1 at it, luminosity 0.7-1.0 × (0.5, 0.8, 0.6).
- Gone when the owner dies.

**The Imp rides the shoulder.** No follower is ever made for it (the fly code for `MODEL_IMP`
in GOBoid.cpp:121-126 is unreachable). ZzzCharacter.cpp:15433-15466 links it to player bone 34,
`Bip01 L Clavicle` in `source/players/rig/player.rig.json`, at (20, 0, 0) from the bone,
unlinked (the offset rotated by the bone, angle 0), at the character's scale, PlaySpeed 0.5,
with a red `BITMAP_LIGHT` of size 1.5 at (20, 0, 15), luminosity 0.7-1.0 × (0.5, 0, 0).

**Item side.** `EQUIPMENT_HELPER` is slot 8 (`sim::kPet`), 1×1. Tooltip: `Life: n` (GT 70)
rather than Durability; Angel "Absorb 20% of damage" + "Max HP +50", Imp "Increase 30% of
attacking/wizardry damage" (ZzzInventory.cpp:4258-4268, 4656-4661). The character window shows
damage ×1.3 with the Imp (NewUICharacterInfoWindow.cpp:734-738). A HUD life bar for the pet out
of 255 (NewUIItemEnduranceInfo.cpp:408-492). The drop draws the Angel's wings additive too.

## What 0.75 decides (OpenMU Version075)

`Items/Pets.cs:32-37`, `CreatePet` :40-71: 1×1, durability 255, drop level = required level
(23 Angel, 28 Imp), all three classes, no options/luck/plus, drops from monsters and in the
jewel group (0.001 a kill, drop level ≤ monster level, no 12-level window).

- Angel: `DamageReceiveDecrement` ×0.8 and `MaximumHealth` +50.
- Imp: `AttackDamageIncrease` ×1.3, skills included.
- Both applied in AttackableExtensions.cs:220-224, after defence and the level/10 floor,
  before the skill multiplier, and only while durability > 0 (ItemPowerUpFactory.cs:38-41).
- Wear: every hit taken wears the pet by damage/2000 (Player.cs:1968-2025, the same
  `kDamagePerDurability` as armour in `sim/wear.h`); attacking does not wear it. At 0 it is
  destroyed. Repair-all skips it; a single repair needs the pet trainer.
- Price: `dropLevel³ + 100`, so Angel 12,200 buy / 4,000 sell, Imp 22,000 / 7,300.

## Downsides, and where OpenMU leaves WebZen (researched 2026-09-30, in the game the same day)

WebZen's own GameServer, 1.00.93 (0.97d to Season 4.6, github ptr0x-real/Mu-GS-Webzen-MC-10093),
cross-checked against a Season 6 1.00.90 decompile and the 0.97k emulator:

- **The Imp costs 3 HP on every blow landed** (ObjAttack.cpp:1045-1060, `gObjSatanSprite`: "on
  every attack, damage x1.3 and HP decreases"). If that would take Life below 0, it is clamped to 0 and that
  blow gets no x1.3; it never kills. In all three sources and muonlinefanz's item page; OpenMU
  and MuMain's tooltip leave it out. The Angel has no cost of any kind.
- **The Angel absorbed 30%, not 20%**, unless `NEW_FORSKYLAND3` (ObjAttack.cpp:1077-1087, "30% cut
  to 20%"). The flag reads as the Icarus update, after 0.75 -- an inference, not stated.
- **Wear is far faster** (user.cpp `gObjSpriteDamage`): per hit taken the Angel loses damage x
  0.3/10 and the Imp damage x 0.2/10 -- about 1 Life per 33 or 50 damage, 60-100x OpenMU's
  damage/2000. The Angel's is taken on the damage before its own cut. The /10 is tagged
  `happycat@20050201`, so 0.75 (2003) may have worn ten times faster again.
- Attacking never wears either; no NPC repairs group 13 numbers 0-3 (protocol.cpp:5753); no map
  or class restriction (InfinityMU's "DK can't use the Imp" is that server's own).

## In the game (2026-09-29)

- **Rules** (`sim::PetPower`, `sim/items.cpp`): group 13 goes in slot 8 (`placeOf`); while its
  Life is above 0 the Angel multiplies damage taken by 0.7 (with the guard skill's share, as two
  DamageReceiveDecrement power-ups multiply) and adds 50 to maximum health before the excellent
  x1.04; the Imp multiplies every blow dealt by 1.3 after the level floor (`rules.cpp` step 7)
  and takes 3 of his life for every blow that lands (`Realm::strikeAt`); with 3 or less he
  neither pays nor gets the x1.3 -- WebZen lays him at 0, ours stops short since 0 is dead here.
  Every hit taken wears the Angel by 3/100 of the damage before its cut and the Imp by 2/100
  (`Realm::wearOnTaken`), and at 0 it is destroyed (`What::PetLost`). sim_test `testPets`.
- **Price and shelf** (`sim/market.cpp`): `dropLevel^3 + 100`, 12,200 and 22,000, a third back.
  Both on Lumen's shelf in the tavern, cells 2 and 3 -- ours, 0.75 sells them nowhere.
- **Card** (`game/ui/describe.cpp`): type "Pet", MU's own lines ("Absorb 30% of Damage", "Max HP
  +50 increased", "Increase 30% of attacking & Wizardry Dmg") with the numbers off `petPower`, and
  the Imp's price in red, "Life -3 for each successful attack" (ours: MuMain never printed it),
  `Life n / 255` in the foot. They
  ride in the jewel drop group but are not jewels: no gold name, droppable, not bold on the ground.
- **Buff strip** (`game/ui/hud.cpp`): the pet's cell first, its Life as the bar. The two icons are
  ours, rendered from the pets' own models by `pipeline/model_icons.py` (MuDream has no pet cell).
- **Drawn** (`game/pets.h`): cooked as standalone figures on every map (`tools/cook.py`). The Imp
  rides `Bip01 L Clavicle` at (20,0,0) in the bone's frame (`Figure::mount`); the Angel flies
  GOBoid's steering at 25 Hz, drawn between its last two steps with an eased heading, at 0.7.

Test: `build/mu2 --world lorencia --play --level 30 --give Helper01:1:W` (or Helper02).

## Still open

1. The Angel's four grey sparks and green BITMAP_LIGHT, and the Imp's red one.
2. The Horn of Uniria (13/2).
