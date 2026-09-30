# Devias drops: what the standard monsters leave, and what is not built yet

Research notes, 2026-09-29. Nothing was imported, built or cooked; this page is the build list.
Monsters and levels are `docs/devias-port.md` §1.5 (OpenMU Version075). The drop rule is
`Realm::leave`, `src/sim/realm_items.cpp:665`. The item pool is the **built catalogue**, so any
row below that is not in `assets/index.json` never drops at all, and the monster draws from the
rest of its window.

## 1. The rule, applied

`Realm::leave`: one roll, rarest first.

- **Jewel group, 0.001.** Drop level at or below the monster's, no gap: Chaos (12), Ale (15),
  Bless (25), Soul (30), Town Portal Scroll (30). OpenMU also puts the three pets in this
  group (Guardian Angel 23, Uniria 25, Imp 28, `Version075/Items/Pets.cs:33-37`); MU2_BGFX
  has no rows for them.
- **Excellent, 0.0001.** What a monster 25 levels lower drops: none under level 25.
- **Item, 0.1.** Drop level in **(level − 12, level]**, `drops_from_monsters`, at
  +`(level − drop level) / 3`.
- **Zen, 0.5.** Otherwise nothing.

| monster | lvl | spawned | item window | rows in window | not built | excellent window |
|---|---:|---:|---|---:|---:|---|
| Worm | 20 | 65 | 9-20 | 49 | 5 | -- |
| Ice Monster | 22 | 75 | 11-22 | 47 | 7 | -- |
| Hommerd | 24 | 75 | 13-24 | 44 | 7 | -- |
| Elite Yeti | 36 | 235 | 25-36 | 37 | 3 | 0-11 (all built) |
| Ice Queen | 52 | 75 | 41-52 | 27 | **12** | 16-27 (Halberd, Flail, Elven Axe unbuilt) |
| Assassin | 26 | 35 | 15-26 | 44 | 6 | 0-1 |
| Yeti | 30 | 0 | 19-30 | 39 | 6 | 0-5 |

The Yeti has a breed row in Version075 but no spawn (port §1.5); listed for completeness. The
Assassin does spawn: 35 in 128-251 by 0-128, the town's quarter (Version075/Maps/Devias.cs:69,
`NpcDictionary[21]`) -- this table first read it as unspawned.

The Ice Queen is the point: 12 of her 27 rows are unbuilt, so today she drops only the
Plate/Guardian/Spirit pieces, the Giant Sword, the Great Scythe and the Bronze Shield.
Levels **37-40** fall in no Devias window, so the Great Hammer (38) and Light Saber (40)
are not Devias drops even though they sit in the same gap in the catalogue.

## 2. The build list

Every row below is in OpenMU `Version075/Items/Weapons.cs` or `Armors.cs` and in mu.db's `items`,
and every `.bmd` and sheet is on disk under `LEGACY/reference/MuMain/src/bin/Data` (all checked;
scratch OBJ exports at
`/private/tmp/claude-501/-Users-karlisfeldmanis-Documents-muremaster2-MU2-BGFX/3a714ccf-0248-45c8-aa78-ad1d46de3c2c/scratchpad/devias_items/`).
Stance follows MuMain's `ZzzCharacter.cpp:356-395`: sword/axe/mace two-handed (width 2) is
`two_hand_sword`, one-handed is `sword`; group 3 two-handed is `scythe` (only the Spear and Dragon
Lance are `spear`); staff two-handed is `scythe`.

### 2.1 Weapons: 13 in Devias windows

| item | 0.75 row | drop | who drops it | bmd | tris | sheets (new unless noted) | stance | classes |
|---|---|---:|---|---|---:|---|---|---|
| Double Poleaxe | 3,5 | 13 | Worm, Ice Monster, Hommerd | Item/Spear06 | 62 | Polearms02 | scythe | DK, Elf |
| Halberd | 3,6 | 19 | Worm, Ice Monster, Hommerd | Item/Spear07 | 52 | Polearms03 | scythe | DK, Elf |
| Flail | 2,2 | 22 | Ice Monster, Hommerd; exc. Ice Queen | Item/Mace03 | 124 | flail00 (tga), flail02 | sword | DK |
| Elven Axe | 1,4 | 26 | Elite Yeti; exc. Ice Queen | Item/Axe05 | 38 | Axes06 (jpg+tga) | sword | DW, Elf |
| Giant Trident | 3,3 | 29 | Elite Yeti | Item/Spear04 | 60 | Javelins04 | scythe | DK, Elf |
| Sword of Salamander | 0,9 | 32 | Elite Yeti | Item/Sword10 | 84 | sword11 (jpg+tga) | two_hand_sword | DK |
| Light Spear | 3,0 | 42 | Ice Queen | Item/Spear01 | 128 | sword12 (**have**), sword12_3 | scythe | DK, Elf |
| Legendary Sword | 0,11 | 44 | Ice Queen | Item/Sword12 | 134 | sword13 | two_hand_sword | DK |
| Larkan Axe | 1,7 | 46 | Ice Queen | Item/Axe08 | 72 | Axes09 | two_hand_sword | DK |
| Serpent Spear | 3,4 | 46 | Ice Queen | Item/Spear05 | 112 | Javelins05 | scythe | DK, Elf |
| Double Blade | 0,13 | 48 | Ice Queen | Item/Sword14 | 180 | sword015, sword015_2 | sword | DK, Elf |
| Serpent Crossbow | 4,12 | 48 | Ice Queen | Item/CrossBow05 | 165 | crossbow05 (**have**) | crossbow | Elf |
| Gorgon Staff | 5,4 | 52 | Ice Queen | Item/Staff05 | 220 | wand06 | scythe (see §3) | DW |

The Larkan Axe is also Hommerd's weapon in MuMain (`ZzzCharacter.cpp:13997-14045`, with the Big
Round Shield, already built), so it earns its place twice.

OpenMU calls, to paste into each recipe's `stats.call`
(`CreateWeapon(group, number, slot, skill, width, height, drops, name, dropLevel, min, max, speed,
durability, magic, level, str, agi, ene, vit, DW, DK, Elf)`):

```
Weapons.cs:129 CreateWeapon(3, 5, 0, 0, 2, 4, true, "Double Poleaxe", 13, 19, 31, 30, 38, 0, 0, 70, 50, 0, 0, 0, 1, 1)
Weapons.cs:130 CreateWeapon(3, 6, 0, 0, 2, 4, true, "Halberd", 19, 25, 35, 30, 40, 0, 0, 70, 50, 0, 0, 0, 1, 1)
Weapons.cs:118 CreateWeapon(2, 2, 0, 0, 1, 3, true, "Flail", 22, 22, 32, 15, 32, 0, 0, 80, 50, 0, 0, 0, 1, 0)
Weapons.cs:110 CreateWeapon(1, 4, 0, 0, 1, 3, true, "Elven Axe", 26, 26, 38, 40, 32, 0, 0, 50, 70, 0, 0, 1, 0, 1)
Weapons.cs:127 CreateWeapon(3, 3, 0, 0, 2, 4, true, "Giant Trident", 29, 35, 43, 25, 44, 0, 0, 90, 30, 0, 0, 0, 1, 1)
Weapons.cs:98  CreateWeapon(0, 9, 0, 20, 2, 3, true, "Sword of Salamander", 32, 32, 46, 30, 40, 0, 0, 103, 0, 0, 0, 0, 1, 0)
Weapons.cs:124 CreateWeapon(3, 0, 0, 22, 2, 4, true, "Light Spear", 42, 50, 63, 25, 56, 0, 0, 60, 70, 0, 0, 0, 1, 1)
Weapons.cs:100 CreateWeapon(0, 11, 0, 20, 2, 3, true, "Legendary Sword", 44, 56, 72, 20, 54, 0, 0, 120, 0, 0, 0, 0, 1, 0)
Weapons.cs:113 CreateWeapon(1, 7, 0, 19, 2, 3, true, "Larkan Axe", 46, 54, 67, 25, 55, 0, 0, 140, 0, 0, 0, 0, 1, 0)
Weapons.cs:128 CreateWeapon(3, 4, 0, 20, 2, 4, true, "Serpent Spear", 46, 58, 80, 20, 58, 0, 0, 90, 30, 0, 0, 0, 1, 1)
Weapons.cs:102 CreateWeapon(0, 13, 0, 22, 1, 3, true, "Double Blade", 48, 48, 56, 30, 43, 0, 0, 70, 70, 0, 0, 0, 1, 1)
Weapons.cs:147 CreateWeapon(4, 12, 0, 24, 2, 3, true, "Serpent Crossbow", 48, 50, 61, 40, 45, 0, 0, 30, 100, 0, 0, 0, 0, 1)
Weapons.cs:156 CreateWeapon(5, 4, 0, 0, 2, 4, true, "Gorgon Staff", 52, 29, 32, 25, 65, 58, 0, 50, 0, 0, 0, 1, 0, 0)
```

### 2.2 Armour and shield: 5 from the Ice Queen

| item | 0.75 row | drop | bmd | tris | sheets | classes |
|---|---|---:|---|---:|---|---|
| Legendary Gloves | 10,3 | 44 | Player/GloveMale04 | 164 | gloves_13m, hide_m (**have**) | DW |
| Legendary Boots | 11,3 | 46 | Player/BootMale04 | 132 | boots_13m, hide_m (**have**) | DW |
| Legendary Shield | 6,14 | 48 | Item/Shield15 | 158 | shield11 (tga), shield11_a | DW, Elf |
| Legendary Helm | 7,3 | 50 | Player/HelmMale04 | 106 | head_13m, skin_warrior_01 (**have**) | DW |
| Dragon Gloves | 10,1 | 52 | Player/GloveMale02 | 156 | gloves_03 | DK |

```
Armors.cs:107 CreateGloves(3, "Legendary Gloves", 44, 11, 0, 42, 20, 0, 1, 0, 0)
Armors.cs:124 CreateBoots(3, 6, 2, 2, "Legendary Boots", 46, 12, 0, 42, 30, 0, 1, 0, 0)
Armors.cs:50  CreateShield(14, 1, 0, 2, 3, "Legendary Shield", 48, 7, 48, 50, 90, 25, 1, 0, 1)
Armors.cs:56  CreateArmor(3, 2, 2, 2, "Legendary Helm", 50, 18, 42, 30, 0, 1, 0, 0)
Armors.cs:105 CreateGloves(1, "Dragon Gloves", 52, 14, 6, 68, 120, 30, 0, 1, 0)
```

The worn pieces need `export-rig` as well as `export-obj`, like the Elf sets in `e4bb8d5e`.

## 3. Decisions before the build

1. **Gorgon Staff's hand.** 0.75 says two-handed (width 2, so `scythe`). The Angelic and
   Serpent Staves were made one-handed as ours (`73fe60fa`); the Thunder Staff stayed two-handed.
   Recommendation: two-handed, as the Thunder Staff.
2. **Half sets.** Devias drops three Legendary pieces and one Dragon piece. Legendary Pants (53)
   and Armor (56), and Dragon Boots (54), Pants (55), Helm (57), Armor (59), fall just past the Ice
   Queen and drop nowhere in Lorencia/Noria/Devias. Recommendation: build the Legendary pants and
   armour with the rest (6 more model files, all on disk) so the DW set can be seen whole on the
   bench; leave the Dragon set for Lost Tower.
3. **Rings and pendants.** Ring of Poison (17), Ring of Ice (20), Pendant of Fire (13) and
   Pendant of Lighting (21) are 0.75 drops (`Jewelery.cs:47-51`, `DropsFromMonsters = true`) in the
   Worm, Ice Monster and Hommerd windows (models `Item/Ring01-02`, `Item/Necklace01-02`). MU2_BGFX
   has no ring or pendant slot and mu.db no rows, so they are a feature, not a build.
   Recommendation: leave out.
4. **Pets** in the jewel group (Angel, Uniria, Imp): no rows, no feature. Leave out.

## 4. How to build, when asked

Per item (the import stage, `docs/content.md`), then the recipe JSON beside the `.obj` with the
call above, then `tools/asset.sh`, sync, and `cook_one`:

    D=../LEGACY/reference/MuMain/src/bin/Data     # or ./tools/fetch_mumain.sh and reference/MuMain/...
    dotnet run --project tools/MuExtract -- export-obj $D/Item/Spear06.bmd source/items/weapons/Spear06.obj
    python3 pipeline/decode_texture.py $D/Item/Polearms02.OZJ source/textures/polearms02.png
    # worn pieces also:
    dotnet run --project tools/MuExtract -- export-rig $D/Player/GloveMale04.bmd source/items/armor/GloveMale04.rig.json

Batches that make sense: **low Devias** (Double Poleaxe, Halberd, Flail, Elven Axe, Giant Trident,
Sword of Salamander -- 6, what most kills can leave), then **the Ice Queen** (the other 12, plus
the two Legendary pieces from §3.2 if agreed). Run `pipeline/index.py` after `--build-only`
batches, and cook only what changed.

## 5. Progress, and what MuMain does to them beyond the mesh

**Built, cooked and passed 2026-09-29** (studio sheets in `shots/studio/`, verdicts in `docs/cook-log.md`, item tables recooked for lorencia/noria/devias; not committed): Spear06 Double
Poleaxe, Spear07 Halberd, Mace03 Flail, Axe05 Elven Axe, Spear04 Giant Trident, Sword10 Sword of
Salamander. Materials read per island off MU's sheet (notes in each recipe). Built niced with
Blender held to 4 threads (`BLENDER=` a wrapper passing `-t 4`); about 30 s an item.

Checked in MuMain for every one: animations (`export-rig --actions=all`), `ItemObjectAttribute`
(`ZzzObject.cpp:5199`, which sets BlendMesh glows and UV scrolls and runs for held items too,
`ZzzCharacter.cpp:6650`), the swing blur (`ZzzCharacter.cpp:3945-3990`), the inventory pose and
the back position.

| item | what MuMain adds | here |
|---|---|---|
| Flail | **its own rig: 5 bones, 3 actions** -- action 1 at rest, action 2 during a sword swing (`ZzzCharacter.cpp:10107-10118`): the head swings on its chain | **missing.** Held items are rigid (`src/game/figures.h:48`); a bow's rig is only read for its muzzle. Built at action 0 key 0. Needs held-item clip playback |
| Sword of Salamander | swing blur (`:3983`) | covered: `play_show.cpp:89` streaks every held weapon |
| Flail, Elven Axe, Salamander | a `.tga` part | cut-outs, declared in the recipe (`cutout`) -- the pipeline does not sniff alpha, so undeclared they draw as solid cards |
| Double Poleaxe, Halberd, Giant Trident, Elven Axe | nothing | -- |

Pre-checked for the Ice Queen batch:

| item | what MuMain adds |
|---|---|
| Double Blade (Sword14) | mesh 1 is `BlendMesh = 1`, drawn additive -- a glow (`ZzzObject.cpp:5315`, shared with the Blade); its swing blur is red (`ZzzCharacter.cpp:3951`) |
| Legendary Shield (Shield15) | mesh 1 additive with a random UV jump every frame -- a shimmer (`ZzzObject.cpp:5270-5274`); own back position (`ZzzCharacter.cpp:6883`) |
| Serpent Crossbow (CrossBow05) | 14-bone rig like the other crossbows; fires MODEL_ARROW_THUNDER (`ZzzEffectMagicSkill.cpp:227`) |
| Light Spear (Spear01) | 3 bones, one action; its own inventory angle only (`ZzzInventory.cpp:8055`) |
| Legendary Sword, Serpent Spear | swing blur only |
| Larkan Axe, Gorgon Staff | nothing beyond the mesh; both are also held by monsters (Hommerd `ZzzCharacter.cpp:14016`, the Gorgon `:14051`) |

**The Blade (Sword06, already built) has the same BlendMesh glow and does not show it**: no item
recipe carries a glow. The Blade and the Double Blade want one pipeline rule for an additive
mesh; the Legendary Shield's shimmer wants that plus a UV jitter.

### 5.1 What the studio caught

- **Steel's 0.2 is too polished for these.** Every head went black facing the ground and every
  round shaft flared white at grazing sun. All six carry `material_overrides` steel 0.45, the
  two pole shafts plate_steel 0.5 -- the Gladius's lesson again (`docs/cook-log.md` line 78).
- **The Flail's bind pose is not a pose MU shows.** Action 0 folds the ball onto the grip with
  a link floating free; it is exported at `--action=1 --key=0`, MU's rest.
- **A tiled UV cannot be baked.** The chain's two cards repeat a 16-px ring 9-10 times down
  one quad (v -4.6..4.7). The source OBJ now cuts them into 19 segments of one ring each (its
  own comment says so), and `pipeline/clean_lowpoly.py` gained a recipe knob, `bake_share`,
  so nineteen copies of a 16-px ring do not take the map from the head. Default unchanged.
  Watch for tiled UVs in the Ice Queen batch (check each island's `mu_uv` for values
  outside 0..1).
- **A cut-out card must be declared** (`cutout`), and a one-sided card seen edge-on in the
  studio's flat pose (the Elven Axe's crescent) reads as a thin dark hook; judge it in the hand.
- **Devias has no nests cooked yet**, so these drop only where Lorencia/Noria levels reach them
  until its monsters go in.

## 6. Audit: every weapon, shield and orb, for baked animation and glows (2026-09-29)

All 81 weapon/shield/orb rows in mu.db, built or not: each `.bmd` exported with
`export-rig --actions=all` (bone motion measured per action), its sheets checked for MU's
texture-script suffixes (`_R`/`_H`/`_S`/`_N`, `TextureScript.cpp:16-60`: none in this set), and
every line of MuMain naming its model constant read. What MuMain does beyond the mesh:

- `ItemObjectAttribute` (`ZzzObject.cpp:5199-5347`) sets `BlendMesh` (that mesh drawn ADDED),
  `BlendMeshLight` (its brightness, often `sin(t*0.004)*0.3+0.7`, a pulse), a UV jitter/scroll,
  or `HiddenMesh`. It runs for held items too (`ZzzCharacter.cpp:6650`).
- A sword's own action 0 plays continuously at idle speed (`ZzzCharacter.cpp:10137-10141`); a
  bow's plays on the shot; the Flail swaps actions 1/2 (`:10107`).
- `NoneBlendMesh` (`ZzzOpenData.cpp:1245-1249`) only keeps the refinement chrome off a mesh --
  not a glow.

**Built, and MU2_BGFX draws them wrong or still:**

| item | what MU does | here now |
|---|---|---|
| Blade (Sword06) | mesh 1 `sword07_2` added: a blue flame over the blade | baked as opaque paint (grey slab, blue shape) |
| Serpent Shield (Shield12) | mesh 1 `shield082` (flat yellow) added, pulsing | a brass part |
| Bronze Shield (Shield13) | mesh 1 `shield092` (green gradient) added, pulsing | brass gems |
| Katache (Sword04) | 6-bone rig: its tassel sways at idle (action 0, 0.31 rad) | rigid |
| Flail (Mace03) | 5-bone rig: rest sway, swing on a blow | rigid at the rest pose |
| Short/Bow/Elven/Battle/Tiger Bow | 13-bone rig: draws on the shot (7 keys, 51 units) | rigid |
| Crossbow, Golden, Arquebus, Light | 12-bone rig: draws on the shot | rigid |
| Tiger Bow (Bow05) | two flickering orange sparkles on the limb tips, bones 2 and 6 (`ZzzCharacter.cpp:7034-7058`) | none |
| Orbs of Healing/Greater Defense/Greater Damage/Summoning | whole mesh added (`BlendMesh = 0`) -- lying on the ground only | opaque |
| Skull Staff (Staff01) | `BlendMesh = 2` on a one-mesh model: nothing, in MU too | -- |

**Unbuilt, Devias (Ice Queen):** Light Spear (mesh 1 `sword12_3` added, pulsing), Double Blade
(mesh 1 `sword015_2` added; red swing blur), Legendary Shield (mesh 1 `shield11_a` added, random
UV jump per frame), Serpent Crossbow (draw rig; fires `Data/Skill/ArrowThunder01`).

**Unbuilt, beyond Devias:** Light Saber (mesh 1 added, pulsing), Lighting Sword (mesh 1 added,
pulsing, UV jump), Crystal Morning Star (mesh 1 added, pulsing), Crystal Sword (mesh 0 added;
own action), Chaos Dragon Axe (mesh 1 added, pulsing; 9-bone rig moving), Bill of Balrog (UV
scroll down), Dragon Shield (mesh 1 added, pulsing), Silver Bow / Chaos Nature Bow (sparkles;
Chaos Nature mesh added), Bluewing/Aquagold Crossbow and Staff of Resurrection (all meshes
added, pulsing; Resurrection's rig turns 1.85 rad), Chaos Lightning Staff (mesh 1 added at random
brightness; 38-bone rig), Legendary Staff (mesh 2 added, UV jump).

**Two features cover almost all of it:**
1. *Item glows* -- a recipe names a glow slot; the item export emits that slot as its own
   additive primitive on MU's raw sheet (MU's UVs) instead of baking it, with an optional pulse
   and UV jitter/scroll. The renderer's glow pass already takes any mesh part flagged glow
   (`src/gfx/renderer.cpp:190,900`), held items included. Fixes the Blade and both shields now,
   and unblocks the Light Spear, Double Blade and Legendary Shield.
2. *Held-item clips* -- play a weapon's own action (idle loop for swords, the shot for bows and
   crossbows, the Flail's rest/swing). Needs the weapon rig carried through the cook and posed
   per frame; the bows' rigs are already exported for their muzzles.

### 6.1 Item glows, built (2026-09-29)

A recipe names a glow by its sheet slot: `"glow": {"shield082": {"pulse": [0.3, 0.7]}}`, with
`pulse` [a, b] for MU's `BlendMeshLight = sin(WorldTime*0.004)*a + b` and `jitter` for its
per-frame sheet jump. The pieces:

- `pipeline/export_gltf.py` -- the slot's faces become a part `glow:<slot>` sampling MU's own
  UVs (`mesh_attributes(swapped=...)`) on the upscaled raw sheet, material BLEND with
  `extras {item, pulse, jitter}` (`glow_material`).
- `pipeline/clean_lowpoly.py` -- a glow slot takes 0.02 of the atlas (`bake_share`, also a
  recipe knob of its own since the Flail's chain).
- `tools/cook.py` -- material flag bit 5: an item glow, then pulse a, b and jitter as three
  floats. Files without it read as before.
- `src/content/cooked.*`, `mesh.*` -- `Material::pulse`, `jitter`, `itemGlow`.
- `src/gfx/renderer.cpp` -- the glow pass draws an item glow at its pulse alone; the jump is
  re-rolled 25 times a second, in V only.

**An item glow is not a lamp.** The first cut drew them at the world sheet's `glow_strength`
(2.0 in Lorencia, for fires and windows), and the Serpent Shield's two fangs -- `shield082`,
flat yellow, MU's mesh 1 -- came out as blown-out orange bars the user found wrong. MU adds
an item's glow at its BlendMeshLight and nothing else; `itemGlow` keeps the sheet off it.

Built with glows: Blade, Serpent Shield (and its hood to ceramic), Bronze Shield, Light Spear,
Double Blade, Legendary Shield.

### 6.2 Held-item clips: the bows and crossbows (2026-09-29)

MU plays a bow's or crossbow's action 0 (the string drawing, 7 keys) only while its archer is
in PLAYER_ATTACK_BOW, _CROSSBOW, _FLY_BOW or _FLY_CROSSBOW (actions 50-53), at the attack's
own speed from the attack's first frame, and holds it on key 0 otherwise
(`ZzzCharacter.cpp:10095-10106, 10160-10166`). Here:

- `tools/cook.py` (wardrobe) and `tools/cook_one.py` bake a weapon's own clip beside its mesh
  when it has a rig of its own (1-16 bones): `cooked/wardrobe/clips/<mesh>.muc`, through
  `cook_world_clip`. All nine bows and crossbows have one.
- `Figures::heldClip` loads it once; `bind()` hangs it on the HeldItem (`clip`, `onShot`).
- `Figure::poseHeld` poses it into its own palette row after the body's pose: key = the body's
  key into its attack, wrapped round the weapon's 7, else key 0 (which is exactly the bind pose:
  checked, every bone's skin matrix is identity at key 0). Called by the crowd, the bench and
  the pedestals; `gather` draws the held item with that row.
- Checked on the bench: `mu2 --figure CrossbowGuard --clip 51`, rigid against played,
  `shots/held/`.

Not yet: the Katache's tassel and the Flail's swing. Neither glb carries its rig -- their
recipes have no `.rig.json` -- so each needs `export-rig --actions=all` and a rebuild, and the
Flail's source OBJ was re-posed at action 1 and its chain re-cut, which a rigged build has to
redo on the bind pose. The Tiger Bow's two limb sparkles are an effect, not a clip.
