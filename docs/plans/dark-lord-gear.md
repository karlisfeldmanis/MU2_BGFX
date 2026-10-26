# Dark Lord -- the missing gear

Inventory for phase 6 of docs/plans/dark-lord.md (§7 "Items to cook", the sets table :167-178,
phase 6 :242). Written 2026-10-25 read-only; nothing built. The Cape of Lord: see dark-lord-cape.md.

Sources: OpenMU `VersionSeasonSix/Items/{Armors,Weapons}.cs` (no 0.75/0.95d/0.97d initialiser
has any Dark Lord item); **MuMain's own item table** `Data/Local/Eng/item.bmd`, decoded here
(84-byte legacy records, BuxCode XOR, layout `Data/GameData/ItemData/ItemFieldDefs.h`); MuMain's
Data for models and sheets (every .bmd read for its per-mesh sheet names); MuMain's source for
effects. "MuMain" below = `LEGACY/reference/MuMain/src/source/Engine/Object/` unless a path is given.

**The two tables agree on every number below but one**: MuMain's item.bmd asks **level 380** of
Paewang (7-11/51) and the Absolute Scepter (2/17) -- as of every S4 socket item, Titan to Hades
-- where OpenMU asks 0. Our built socket sets (Seraphim/Faith/Hades, *Male50/51/53) took
OpenMU's 0. **No item here asks Command** in either table (item.bmd `RequireCharisma` 0 on all).

## Summary

| item | MU id | ours | drop | files in MuMain | MU effects | where (dark-lord.md §6/§7) |
|---|---|---|---|---|---|---|
| Adamantine set (5) | 7-11/26 | *Male27 | 57-78 | all present | 3 | Atlans, Tarkan; Tersia 1-5 a piece a floor |
| Dark Steel set (5) | 7-11/27 | *Male28 | 75-96 | all present | 3 | Tarkan, Icarus; Sky Door gloves+boots, Phoenix mask+pants+armour |
| Glorious set (5) | 7-11/38 | *Male39 | 91-105 | all present | 3 | Icarus, BC |
| Dark Master set (5) | 7-11/28 | *Male29 | 89-117 | all present | 3 | Icarus top, Aida |
| Paewang set (5) | 7-11/51 | *Male52 | 98-132 | all present | 3 (+sockets) | Aida |
| Sunlight set (5) | 7-11/33 | *Male34 | 110-147, lvl 380 | all present | 4 | 380 gear: Kanturu only |
| Divine Scepter of Archangel | 2/13 | Mace14 | 150 | present (saint.bmd) | 4 | Blood Castle reward (not a monster drop) |
| Soleil Scepter | 2/14 | Mace15 | 146, lvl 380 | present (HDK_Mace.bmd) | 3 | 380 gear: Kanturu only |
| Absolute Scepter | 2/17 | Mace18 | 135 | present (Mace_18.bmd) | 4 (+3 sockets) | Aida |
| Cross Shield | 6/21 | Shield22 | 70 | present (crosssheild.bmd) | 3 | Atlans, Tarkan; LT5 +5 (2 sockets) |
| Crest of Monarch | 13/14 +1 | ? | 78 (feather) | present (DarkLordSleeve.bmd) | 0 | Phoenix every clear; MU: Icarus 1/1000 at mlvl 82+ |

"Glorious" and "Paewang" are **neither alternate names of the sets above nor Korean ones**: item.bmd
names every set apart -- Light Plate 라운드 ("Round"), Adamantine 다크소울 ("Dark Soul"), Dark Steel
다크브리즌, Dark Master 다크마스터, Sunlight 썬라이트, **Glorious 글로리어스** (MU 38, the CW_ wave of
Ashcrow/Eclipse/Iris/Valiant, Season 2-3), **Paewang 패왕** ("Overlord", MU 51, the Season 4 socket
wave with Titan..Hades; OpenMU spells the boots "Phaewang", Armors.cs:341, MuMain `MODEL_PHAEWANG_BOOTS`
_enum.h:1913). MU versions: Light Plate/Adamantine/Dark Steel/Dark Master, the archangel scepter
and the Crest came with the class (1.0); Sunlight/Soleil with Kanturu (Season 1, the HDK_ files);
Glorious and Cross Shield in the CW_/shield-17-21 wave (Season 2-3, by file family -- not
confirmed against a dated source); Paewang/Absolute Season 4 (socket rows, OpenMU SocketSystem.cs:165,
179-215; MuMain Network/Server/SocketSystem.cpp:47, 62).

## Common to every Dark Lord set

- **Models**: MuMain loads *Male26-29 in one loop, `MODEL_LIGHT_PLATE_* + i`, i < 4
  (ZzzOpenData.cpp:213-219); Sunlight is the HDK_ loop's fifth (`HDK_*Male05`, :222-233);
  Glorious the CW_ loop's fifth (`CW_*Male05`, :235-247); Paewang the 45..53 loops (*Male52,
  :267-280). Ours by the index-plus-one rule. All .bmd v12, 51 bones, 1 action.
- **Helm**: every DL helm is 3 meshes, mesh 1 `hair_R.jpg` (the white fins). A `_R` sheet is
  MU's Bright mesh, drawn added (TextureScript.cpp:31-40); a Dark Lord's helm breathes its blend
  mesh at sin(WT*0.001)*0.1 + 0.7 (ZzzCharacter.cpp:9608-9610). Copy HelmMale26.json's
  `additive ["hair_R"]`, `glow {"hair_R": {"pulse": [0.1, 0.7], "pulse_rate": "slow"}}`, `tiled`,
  `separate_meshes`, `cull false`. `lightplate_hair_r.png` and `lightplate_level_man02.png` are
  already decoded (content/textures); the same Player/hair_R and level_man02 sheets -- reuse
  rather than decode a third copy (Paewang's face is SkinClass305 instead).
- **No render arm** for any DL armour enum (grep of MODEL_ADAMANTINE/DARK_STEEL/DARK_MASTER/
  GLORIOUS/PAEWANG/PHAEWANG/SUNLIGHT_* and `MODEL_<part> + n` in ZzzObject.cpp/ZzzCharacter.cpp:
  no BlendMesh, no extra pass, no sprite). PartObjectColor2 (the +11 sweep) names none: 0.
- **Refine colour** (PartObjectColor armour switch, ZzzObject.cpp:6739-6765; colours :6788-6818):
  26 -> 12 (0.47,0.67,0.6), 27 -> 10 (0.75,0.65,0.5), 28 -> 15 (0.2,0.3,0.6), 33 -> 22
  (0.45,0.45,0.23), 38 -> 28 (0.45,0.45,0.23), 51 -> 40 (0.7,0.2,0.2). Our `colourOf` stops at
  case 25 (src/game/shine.cpp:113) and `kColours` at 34 (:68): add 35-40 (:6813-6818).
- **Full set**: the generic aura (fx/set_shine). The DL wears his helm, so no `gladiatorSet` row.
- **Inventory**: MuMain nudges Glorious armour (ZzzInventory.cpp:8138) and gloves (:9317) only.

Requirements below: def / durability / Str / Agi; Command 0; classes DL only (OpenMU
`darkLordClassLevel` 1, item.bmd class[4] = 1). Gloves add attack speed (OpenMU
Updates/FixAttackSpeedCalculationUpdate.cs:62-74).

### Adamantine (*Male27, MU 26) -- the first gap

OpenMU Armors.cs:88, 143, 202, 261, 316. Str 77, Agi 21, durability 56.

| piece | drop | def | sheets (Player/) |
|---|---|---|---|
| HelmMale27 | 66 | 24 | level_man02, hair_R, head_18 |
| ArmorMale27 | 78 | 36 | upper_18 |
| PantMale27 | 70 | 26 | lower_18 |
| GloveMale27 | 57 | 18, speed 6 | gloves_18 |
| BootMale27 | 60 | 20 | boots_18 |

`_18` names are generic (head_18, upper_18 ...): prefix them (`adamantine_`), as Light Plate's
`lightplate_`. Ancient Agnis / Broy sit on it (AncientSets.cs:581, 599; not now). Effects: helm
hair, colour 12, set aura. MuMain also dresses the Doppelganger in Dark Steel (GMDoppelGanger1.cpp:143-147).

### Dark Steel (*Male28, MU 27)

Armors.cs:89, 144, 203, 262, 317. Str 84, Agi 22, durability 70.

| piece | drop | def | sheets |
|---|---|---|---|
| HelmMale28 | 86 | 26 | level_man02, hair_R, Kmpp |
| ArmorMale28 | 96 | 43 | Klpp, Kupp, hide |
| PantMale28 | 92 | 31 | Klpp, hide |
| GloveMale28 | 75 | 21, speed 5 | Kgpp, hide |
| BootMale28 | 83 | 25 | Kbpp, hide |

`hide` is MU capping the openings, a black square, as on ArmorMale10/ArmorElf01; `hide.png`
is decoded already. Effects: helm hair, colour 10, set aura.

### Glorious (*Male39, MU 38)

CW_*Male05 (ZzzOpenData.cpp:236-247). Armors.cs:98, 155, 214, 273, 328. Str 80, Agi 21, durability 74.

| piece | drop | def | sheets |
|---|---|---|---|
| HelmMale39 (CW_HelmMale05) | 97 | 30 | level_man02, hair_R, dark350helmet |
| ArmorMale39 | 105 | 47 | dark350upper |
| PantMale39 | 101 | 35 | dark350lower |
| GloveMale39 | 91 | 25, speed 5 | dark350gloves |
| BootMale39 | 98 | 29 | dark350boots |

(dark350cape.OZJ is in Player/ but no Glorious mesh names it.) Effects: helm hair, colour 28, set
aura. OpenMU puts it in Kundun+5 and the blue boxes (BoxOfLuck.cs:567, 950, 1013).

### Dark Master (*Male29, MU 28)

Armors.cs:90, 145, 204, 263, 318. Str 80, Agi 21, durability 78.

| piece | drop | def | sheets |
|---|---|---|---|
| HelmMale29 | 106 | 34 | level_man02, hair_R, KShp |
| ArmorMale29 | 117 | 51 | KSup, KSlp, **KSup2.tga** |
| PantMale29 | 110 | 39 | KSlp |
| GloveMale29 | 89 | 29, speed 4 | KSgp |
| BootMale29 | 95 | 33 | KSbp |

KSup2.OZT is the one sheet with alpha (trims): alpha below 0.99 is alpha-tested, a cut-out.
Effects: helm hair, colour 15, set aura. MuMain's NPC/demo DL wears it (ZzzCharacter.cpp:15055-15065).

### Paewang (*Male52, MU 51)

Armors.cs:109, 168, 227, 286, 341. Str 105, Agi 38, durability 86; **level 0 (OpenMU) vs 380
(item.bmd)**. Sockets on every piece (SocketSystem.cs:179-215).

| piece | drop | def | sheets |
|---|---|---|---|
| HelmMale52 | 111 | 44 | **Item5001_Helm** (mesh 0), hair_R, SkinClass305 (mesh 2, the face) |
| ArmorMale52 | 132 | 58 | Item5001_Armor |
| PantMale52 | 116 | 46 | Item5001_Pant |
| GloveMale52 | 101 | 34, speed 7 | Item5001_Glove |
| BootMale52 | 98 | 38 | Item5001_Boot |

The helm's mesh order differs from the others (mask first, face last). Effects: helm hair,
colour 40, set aura.

### Sunlight (*Male34, MU 33) -- 380

HDK_*Male05 (ZzzOpenData.cpp:222-233). Armors.cs:94, 150, 209, 268, 323. Level 380, Str 62,
Agi 16, durability 82.

| piece | drop | def | sheets |
|---|---|---|---|
| HelmMale34 (HDK_HelmMale05) | 130 | 46 | level_man02, hair_R, dark400_helmet |
| ArmorMale34 | 147 | 64 | dark400_lower, dark400_upper |
| PantMale34 | 140 | 52 | dark400_lower |
| GloveMale34 | 110 | 40, speed 5 | dark400_gloves |
| BootMale34 | 121 | 44 | dark400_boots |

Not dormant any more by itself: `sim::kanturuGear` is `needLevel >= 380` (src/sim/items.h:556), and
since a4aea731 the 380 gear drops on Kanturu's maps only -- a recipe with level 380 and
`drops_from_monsters true` (as HelmMale30) joins that pool with no code. Effects: helm hair,
colour 22, set aura, and the **+10/+11 boots aura** (ZzzCharacter.cpp:11004-11030: BITMAP_WATERFALL_2
at three points, Sunlight's light (0.6, 0.4, 0)) -- fx/set_shine lacks it for every 380 set
(the Kanturu gap). MuMain counts it 380 gear for the excellent level (ZzzInfomation.cpp:832-841).

## The scepters

All group 2, skill 66 (Force Wave), 1x4, one-handed (`stance "sword"`, as Mace09), DL only.
Weapons.cs:195-199; the hidden-mesh-aware refine pass covers 2/8-2/13 (ZzzObject.cpp:9091-9092).

### Mace14 -- Divine Scepter of Archangel (2/13)

Weapons.cs:195: drop 150, 200-223, speed 45, durability 90, magic 138, Str 75, Agi 16;
**dropsFromMonsters false**. Model `Item/saint.bmd` (not Mace14.bmd; ZzzOpenData.cpp:649), 1 mesh
`saint.jpg`, 1 bone. Effects:
1. Body: mesh 0, then mesh 0 again RENDER_LIGHTMAP with Chrome01, V sliding -WT*0.0001
   (ZzzObject.cpp:7060-7064): a `chrome` over, as Mace10's second entry.
2. Shine: no named colour, so 0 (orange) by PartObjectColor's fall-through.
3. Held: BITMAP_LIGHT at 2 in L*(1, 0.3, 0.1) on bone 0, and one time in three a grey (0.4)
   BITMAP_SHINY+1 at 0.6 turned at random (ZzzCharacter.cpp:7649-7661).
4. Ground light, as the other archangel weapons (ZzzCharacter.cpp:5666-5669): join the
   `archangel` test in HeldLights::lights (src/game/fx/held_lights.cpp:87).
MuMain treats it as a wing for excellence (`IsWing`, ZzzInfomation.cpp:470-484). Inventory
offset ZzzInventory.cpp:10109.

### Mace15 -- Soleil Scepter (2/14) -- 380

Weapons.cs:196: drop 146, 130-153, speed 40, durability 95, magic 84, **level 380**, Str 80, Agi 15.
`Item/HDK_Mace.bmd` (ZzzOpenData.cpp:1097), 1 mesh `dark.jpg`, 3 bones. Effects:
1. Body: mesh 0, then mesh 0 again RENDER_BRIGHT with BITMAP_CHROME6, U and V both
   (WT%4000)*0.0002 - 0.3 (ZzzObject.cpp:7644-7666) -- the Sylph Wind Bow's pass: copy Bow22.json's
   `chrome` (`chrome06.png` is decoded) with both axes.
2. Shine colour 22 (ZzzObject.cpp:6632-6634).
3. Held: orange (1, 0.3, 0) flicker at (0, -100, r) and (0, 100, r) on bone 0 -- two BITMAP_SHINY+1
   and a BITMAP_LIGHT, scales random each frame (ZzzCharacter.cpp:7106-7128).
Inventory angle ZzzInventory.cpp:8101. MuMain's demo DL holds it (ZzzCharacter.cpp:15065).

### Mace18 -- Absolute Scepter (2/17)

Weapons.cs:199: drop 135, 114-132, speed 40, durability 90, magic 72, Str 119, Agi 24; **level 0
(OpenMU) vs 380 (item.bmd)**; 3 sockets (SocketSystem.cs:165). `Item/Mace_18.bmd`
(ZzzOpenData.cpp:1111), meshes `darkepsostep_Cepter.jpg`, `Ebow_R.jpg`; 5 bones. Effects:
1. Mesh 1 `Ebow_R` is a Bright mesh, drawn added (TextureScript.cpp:31-40); `ebow_r.png` exists in
   content/textures -- check it is the same Item/Ebow_R before reusing.
2. Shine colour 40 (ZzzObject.cpp:6678-6679).
3. Held (ZzzCharacter.cpp:7930-7971): on bones 3 (Zx01) and 4 (Zx02) a violet light (0.6, 0.5,
   0.8) at random 1-2 / 1.5-3.5, a BITMAP_SHINY+1 breathing |sin(WT*0.0008)|*0.8 + 0.2, and a
   **BITMAP_LIGHTNING_MEGA1..3** crackle jittered off each point, sheet chosen every 100 ms. No
   lightning-mega sheet is in content/effects yet.
4. It **plays its clip while held**, at PLAYER_STOP_MALE's speed (ZzzCharacter.cpp:10151-10155),
   as the Frost Mace: `export-rig --actions=all` and `held_loop`, after Mace17.json.
Inventory angle ZzzInventory.cpp:9182.

## Shield22 -- Cross Shield (6/21)

Armors.cs:62 (CreateShield, ArmorInitializerBase.cs:158): drop 70, defence 16, block 75,
durability 65, Str 140, Agi 55, skill 18 (recorded and unread, as Shield18's), DL only.
`Item/crosssheild.bmd` (MuMain's spelling; ZzzOpenData.cpp:1121), meshes `crossshield_body`,
`gra`, `crossshield_cross`; 14 bones. (`gra.png` is decoded already, the Frost Mace's.) Effects:
1. Body (ZzzObject.cpp:7249-7256): mesh 0 plain; mesh 1 (gra) RENDER_BRIGHT|RENDER_CHROME, V
   sliding -(WT%2000)*0.0005; mesh 2 (the cross) RENDER_BRIGHT -- added. `separate_meshes`.
2. Shine colour 30 (0.7, 0.3, 0.3) (ZzzObject.cpp:6686-6687), on mesh 0 alone (:9136-9139).
3. Held (ZzzCharacter.cpp:8193-8222): L = |sin(WT*0.001)| + 0.1; BITMAP_LIGHT 1.2 in
   L*(0.2, 0.2, 0.8) on bones 4-7 and 1.4 in L*(0.2, 0.6, 0.6) on bones 8-13 -- ten points (rig kept).
Inventory offset ZzzInventory.cpp:10157.

## Crest of Monarch (Loch's Feather +1)

Not its own row in MU: ITEM_LOCHS_FEATHER (13/14) at level 1 (item.bmd 13/14: drop 78, 1x2,
level 78, all classes). OpenMU drops it in Icarus as that (Maps/Icarus.cs:39-47: chance 0.001,
monster level 82+, `ItemLevel = 1`). MuMain swaps the model at +1 to **MODEL_EVENT+16 =
Item/DarkLordSleeve.bmd** (ZzzObject.cpp:6174-6177; ZzzOpenData.cpp:1015; ZzzInventory.cpp:10483-10485),
1 mesh `DarkLordSleeve.tga` (DarkLordSleeve.OZT present), ground angle 45 / scale 0.5
(ZzzObject.cpp:5806-5810), its own name (ZzzInventory.cpp:6579-6581). **dark-lord.md §7's
"draws Quest05.bmd" is wrong**: Quest05 is MODEL_LOCHS_FEATHER+1 = MODEL_FRUITS (_enum.h:2026), the
Fruit (Stts.jpg). No effects. Ours: Quest04.json is the feather, `drops_from_monsters false`, its
own roll (sim::kFeatherOdds). Its use is the cape's mix: see dark-lord-cape.md.

## How the built ones were made (follow this path)

Commits (git from the repo root): e84d6159 Mace09, d00bf026 its effects, 0ed08c9e Mace10-12,
ce8a22d5 Light Plate, af1b2d13 Mace13 + Mace16. Do the next ones in the Cooker with the user's
studio open (MU2_BGFX/CLAUDE.md, docs/content/cooker.md): `status`, `show`, `edit`, `plan` before
`cook`, and ask before the wardrobe/every-world tables cook.

1. **Rows**: `./mu new item "<name>" --db` writes the recipe, its `stats` and the mu.db `items`
   row from OpenMU (docs/content/recipes.md:121-134). The recipe keeps `call`/`from`/`call_note`
   (the OpenMU line), `requires`, `classes ["lord"]`, `mu_name_from` (ZzzOpenData line), and for a
   weapon `stance "sword"`, `two_handed false`. mu.db writes sit in the WAL: checkpoint before committing.
2. **Model**: `dotnet run --project pipeline/MuExtract -- export-obj <bmd> content/items/<kind>/<Name>.obj`
   and `export-rig` (armour always; a weapon only when it has a held light on a bone or moves --
   `--actions=all` then). Sheets: read them off the .bmd (pipeline/model_sheets.py; it currently
   dies importing terrain.py, which exits on import -- a 20-line reader does it), then
   `pipeline/decode_texture.py` per sheet into content/textures, **prefixed when MU's name is generic**
   (`lightplate_*_17`). `sheet_materials`: painted_steel with the cast grain override for plate
   (ArmorMale26.json `material_overrides`), steel/painted_steel/brass for scepters.
3. **Every MU effect in the same batch** (the user, during Mace09: "dont forget to cook effects for
   every weapon you make", "or armor item"; d00bf026 had to add the mesh-0 pulse and colour 9
   after the first cook): grep the MODEL_ enum and `MODEL_<group> + n` everywhere, list each hit,
   port it -- recipe `glow` (`over` when MU hides nothing and redraws added; `pulse`,
   `scrolls_per_second`/`axis`), `chrome` (image own/chrome01/chrome06, uv mesh/chrome, tint,
   pulse), `additive` for `_R` sheets; **src/game/shine.cpp `colourOf`** (one line per named item
   or armour number); **src/game/fx/held_lights.{h,cpp}**: a value in `enum class Item`
   (held_lights.h:~238), `kItems[54]` grows by one each, `mesh()` name (held_lights.cpp:~154),
   `points()` case with the bones at rest, the feed/gather case (:~572, :~1128); `kMostPoints` 44 covers all.
4. **Cook**: `./mu make <Stem>` (asset.sh -> index.py -> sync_one -> cook_one), then
   `pipeline/cook.py --world lorencia --only wardrobe --threads 3` and `--only tables` for **every
   world**; a set also gets the set's `cook_one`. Judge in the studio (`tools/studio.py <Name>
   [--plus 9]`), log with `pipeline/cook_one.py --pass NAME "note"` (docs/content/cook-log.md:336-342).
5. Traps met or known: helm `_R` hair needs `tiled` + its own material (an added sheet;
   HelmMale26.json `tiled_why`) and `cull false` (fins wink out); the Shining Scepter is
   CW_Mace.bmd yet ours Mace16 (index-plus-one -- so saint.bmd -> Mace14, HDK_Mace -> Mace15,
   Mace_18 -> Mace18, crosssheild -> Shield22); recipe json edits need `--rebuild`; rebuild before
   cook_one; cook_one skips misc items (the Crest); HDK armour .obj can carry face-less vertices
   that fail the rig count -- drop them from obj and rig together (Sunlight); RENDER_TEXTURE|RENDER_BRIGHT
   is added (ZzzBMD.cpp:1534); `shine.cpp`'s colours stop at 34 (af1b2d13 grew them there).

## Lists a new row must join

| list | file | for |
|---|---|---|
| refine colour | src/game/shine.cpp:73-118, kColours :34-69 | every item (armour 26/27/28/33/38/51; 2/14 -> 22, 2/17 -> 40, 6/21 -> 30); add colours 35-40 |
| held lights | src/game/fx/held_lights.h:238-283, .cpp:154-160 and the cases | Mace14, Mace15, Mace18, Shield22 (Divine also :87 `archangel`) |
| 380 pool | none (sim/items.h:556 reads `needLevel`) | Sunlight, Soleil: level 380 in the recipe |
| quest pay | src/sim/quests.cpp `row.paid[n++] = {.item=..., .kin=int8_t(Kin::DarkLord)}` (as :379); tersiaDoor..Fifth :661-893, tersiaSkyDoor :1017, tersiaPhoenix :1108; `kQuestPaid` 24 (quests.h:32) -> 32 | Adamantine, Cross Shield +5, Dark Steel, Crest |
| Blood Castle | src/sim/event.h:136 `kArchangelWeapons[3][2]`, castle_instance.cpp:214-226, items.h:1612-1617 `archangelWeapon` | Mace14 if it comes from the statue |
| Box of Kundun | none (bands by drop level, sim/items.h:1700-1712); the comment "the Dark Lord, whose only set is Light Plate" goes stale | -- |
| set aura +10 boots | fx/set_shine (no 380 arm yet) | Sunlight |
| tests | tests/sim_test.cpp (testQuestLadder, dark-lord.md §6) | the ladder rows |

Drops need no list: monster pools take every `drops_from_monsters` row by drop level, so the
"worlds" columns of §7 fall out of the drop levels.

## Build order and batches

His gap is armour: one set (Light Plate, 42-62) against scepters to 140. Sets first, by drop level.

1. **Adamantine (5) + Cross Shield** -- 57-78, Atlans/Tarkan, Tersia 1-5 and LT5 pay.
2. **Dark Steel (5)** -- 75-96, Sky Door and Phoenix pay.
3. **Glorious (5)** -- 91-105, Icarus/BC.
4. **Dark Master (5) + Divine Scepter** -- 89-117 and the BC reward.
5. **Paewang (5) + Absolute Scepter** -- the Aida socket pair; needs the lightning-mega sheet and the held loop.
6. **Sunlight (5) + Soleil** -- the 380 pair for Kanturu; the 380 boots aura with it.
7. Crest with the cape (see dark-lord-cape.md). **Built 2026-10-26**: dark-lord-cape.md "Built".

## Open questions

1. Paewang and the Absolute Scepter: OpenMU's level 0 (as our Seraphim/Faith/Hades) or MuMain's
   380 (which makes them Kanturu gear through `kanturuGear`)?
2. The Divine Scepter: our Blood Castle's archangel weapons are quest items worn by nobody
   (items.h:1612-1617). Is his a wearable reward (how given?), or does it drop off a statue, which
   MU never had for him (three statues, event.h:131-136)?
3. The Crest: a row of its own (a free 13/number, own model) or Loch's Feather +1 with a model
   swap by plus? As +1, `lochsFeather()` (items.cpp:469) counts it a feather in the wing mix.
4. Where Adamantine's five go across Tersia 1-5 (which floor which piece) and the Cross Shield's
   +5/2 sockets at LT5 -- §6 says so, the rows do not exist yet.
5. Shine colours for the already-built 29-32, 34-37, 49/50/52 sets fall to 0 (orange) in
   `colourOf` (shine.cpp:96-117 stops at 25) -- fix with this work or apart?
6. The Glorious helm's dark350cape.OZJ sits unused in Player/: nothing to do unless the user
   wants it for the cape.

## Decided (the user, 2026-10-25)

1. Paewang and the Absolute Scepter ask **level 380** (MuMain's): Kanturu gear through `kanturuGear`.
2. The Divine Scepter of Archangel is a **quest item like the others**: carried, worn by nobody.
3. The Crest of Monarch is **its own item row** (its own number and DarkLordSleeve.bmd), never a feather.
4. The missing refine colours of the built 380, Season 2-3 and socket sets are **fixed with this work**.
