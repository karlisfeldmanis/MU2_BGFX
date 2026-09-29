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
Two thin dark lines show off the Imp's wing edges on the bench, unexplained yet.

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

## Next steps

1. **Sim.** Group 13 into the item rules: equip into `kPet` (level requirement only), the
   ×0.8 / +50 / ×1.3 in the damage chain at OpenMU's place, the wear on hits taken, destruction
   at 0, price. Then `pipeline/index.py` so they drop.
2. **Worn Imp.** Draw Helper02 on `Bip01 L Clavicle` at (20,0,0) MU units, with its red light.
3. **Flying Angel.** A follower object stepping GOBoid's maths per reference tick, with its
   sparks and green light, playing action 0.
4. **Bag and tooltip.** The slot-8 ghost is already cut (`bag_slot_pet`); the `Life:` line and
   the two effect lines; the HUD life bar.
5. Cook them (`cook_one`) once they are indexed.
