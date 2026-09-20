# Sprint 4: the figures — data census

**Not a plan.** A read of what is actually in `assets/` before sprint 4 is written, in the
form sprints 2 and 3 used: measured facts with the file each came from, and the traps named
before any code. Nothing under `src/`, `shaders/`, `assets/` or `CMakeLists.txt` was touched;
nothing was built or run. Every .glb number below was read out of the file's own JSON chunk
with `struct`/`json`, the way `tools/cook.py`'s `read_glb` does it.

Anything not measured is marked **unknown** or **inferred**.

---

## 0. The one finding that changes the sprint

**The player clip library is not in `assets/`.** Every player body part, every armour piece
and every NPC body part in this tree has **zero embedded animations** — 0 clips across
`players/body` (15 glb), `npc/body` (11 glb) and `items/armor` (45 glb). The clips exist, but
they live in MU2's build as separate libraries that `tools/sync.sh` never copied:

| library | in MU2/build | bytes | clips | joints | in `MU2_BGFX/assets`? |
|---|---|---|---|---|---|
| `players/rig/player.actions.glb` | yes | 13 415 968 | **283** | 60 | **no** |
| `npc/body/Man01.actions.glb` | yes | 79 756 | 2 | 43 | **no** |
| `npc/body/Female01.actions.glb` | yes | 110 508 | 2 | 48 | **no** |
| `npc/body/Girl01.actions.glb` | yes | 86 428 | 2 | 37 | **no** |

`find assets -name '*.actions.*'` returns **0 files**. `assets/players/` contains only `body/`
— there is no `rig/`.

**Why sync missed them, exactly.** `tools/sync.py:121-126` takes each `.glb` path named in
`index.json`, strips `.glb`, and looks for `<stem>.actions.glb` beside it. `index.json` names
`players/body/ArmorClass02/ArmorClass02.glb`, so sync looks for
`players/body/ArmorClass02/ArmorClass02.actions.glb`. The real library is at
`players/rig/player.actions.glb` — a different directory and a different stem. The same
applies to the three NPC libraries (`npc/body/ManHead01/…` vs `npc/body/Man01.actions.glb`).
`grep -c actions.glb` on both `index.json` files returns **0**: no index entry reaches them.

So sprint 4 cannot animate a player, an NPC or the Skeleton Warrior from the content that is
here. Either `sync.py`'s convention gains a case, or `index.json` gains the paths. **Monsters
are unaffected** — their clips are embedded in their own glb.

---

## 1. What figure content exists

Enumerated by tree. "clips" counts glTF animations embedded in the file.

| tree | glb | bytes | tris | prims | materials | images | skinned | clips |
|---|---|---|---|---|---|---|---|---|
| `players/body` | 15 | 7 277 120 | 3 625 | 19 | 23 | 39 | 15 | **0** |
| `npc/body` | 11 | 5 141 396 | 1 656 | 11 | 11 | 22 | 11 | **0** |
| `assets/monsters` | 14 | 20 617 368 | 6 581 | 24 | 29 | 36 | 14 | 91 |
| `assets/npc` | 6 | 6 241 708 | 4 145 | 10 | 10 | 18 | 6 | 14 |
| `assets/lobby` | 3 | 4 625 832 | 4 289 | 3 | 3 | 6 | 3 | 6 |
| `items/armor` | 45 | 81 656 348 | 6 237 | 65 | 83 | 135 | 45 | 0 |
| `items/weapons` | 33 | 33 965 828 | 2 557 | 81 | 110 | 99 | 9 | 5 |
| `items/shields` | 12 | 23 311 736 | 536 | 12 | 12 | 36 | 0 | 0 |
| `items/misc` | 16 | 9 551 912 | 680 | 23 | 30 | 40 | 0 | 0 |
| **total** | **155** | **192.4 MB** | **30 306** | **248** | **311** | **431** | **103** | **116** |

The whole figure catalogue is **30 306 triangles** — less than a quarter of the ground's
131 072 (`docs/sprints/02-the-ground.md`) and less than a tenth of the town's 432 248
placed triangles (`03-the-town.md`). Triangles are not this sprint's problem.

**Bytes are almost all texture.** 431 distinct embedded images, **314.1 Mtexels**, of which
**275 images are 1024×1024** (288 Mtexels, 92%). Size histogram across all 155 figure glb:

    1024x1024 x275   192x192 x47   384x384 x43   768x768 x26   96x96 x22
    64x64 x5   384x768 x3   384x192 x3   512x512 x2   192x384 x2   96x192 x1   24x24 x1

The 1024² images are the ORM and normal maps: a 120-triangle `ArmorClass02` carries a 768²
albedo and a **1024² ORM**. That ratio is the figures' whole memory story.

**What Lorencia actually places.** `assets/world/lorencia/lorencia.json` has 2845 placements;
12 model names have no world glb, of which **14 placements are figures**: `BerdyshGuard` 5,
`Storage01` 2, `WanderingMerchant` 2, `CrossbowGuard` 1, `LumentheBarmaid` 1, `PotionGirlAmy`
1, `Smith01` 1, `Wizard01` 1. This matches `mu.db`'s `npc_spawns` exactly (14 rows, all
Lorencia; the table has no `map` column). `ElfMerchant01`, `ElfWizard01` and `MixNpc01` are
in `assets/assets/npc/` but are **not placed in Lorencia** — they belong to another map.

---

## 2. The player body

**A character is five parts plus up to two held items.** From `index.json`'s `characters`
(9 entries, 4 `playable`):

| character | parts | right hand | left hand | idle |
|---|---|---|---|---|
| `DarkKnight` | Helm/Armor/Pant/Glove/BootMale10 | `Sword01` | `Shield10` | — |
| `DarkKnightBare` | Helm/Armor/Pant/Glove/BootClass02 | — | — | — |
| `DarkWizardBare` | …`Class01` | — | — | — |
| `FairyElf` (`female: true`) | …`Class03` | — | — | — |
| `BerdyshGuard` | …`Male10` | `Spear08` | — | `action7` |
| `CrossbowGuard` | …`Male10` | `CrossBow04` | — | `action1` |
| `LumentheBarmaid` | 4 × `Female…01` | — | — | — |
| `PotionGirlAmy` | **3** × `Girl…01` | — | — | — |
| `WanderingMerchant` | 4 × `Man…01` | — | — | — |

So the part count is **five for a player-rig character (helm, armor, pant, glove, boot), and
three or four for an NPC body** — NPC sets use head/upper/lower/boots/gloves and are not the
same five slots.

**Naming.** Bare bodies are `<Slot>Class<NN>` where `NN` is the class: 01 wizard, 02 knight,
03 elf (traced from `characters`, which pairs `Class01` with `trade: "wizard"`, `Class02` with
`"knight"`, `Class03` with `"elf"`). Equipment is `<Slot>Male<NN>` or `<Slot>Elf<NN>` where
`NN` is the item's level. Directories present:

    armor:   ArmorElf01, ArmorMale01/03/05/06/07/08/09/10   (and the same 9 for Boot, Glove, Helm, Pant)
    weapons: 33   shields: 12   misc: 16

**Equipment varies the parts by swapping the glb for the slot, one file per slot per level.**
`index.json`'s `objects` (321 rows) carries the equipment catalogue: 90 rows of
`kind` armor/weapon/shield (45/33/12). By class: knight 58, elf 30, wizard 27. Armour
`drop_level` runs over 27 distinct values from 3 to 48 — so the **rules** have many levels
but the **art** has only 9 armour sets per slot; which level maps to which glb is on each
object row's `glb`, not derivable from the name alone.

**Distinct meshes and textures.**
- Player-rig bodies + armour: **60 glb** (15 bare + 45 equipment), 9 862 triangles,
  84 primitives, 174 embedded images.
- Held items that can go on a player: 33 weapons + 12 shields + 16 misc = 61 glb,
  3 773 triangles, 175 images.
- NPC bodies: 11 glb, 1 656 triangles, 22 images.

---

## 3. The rig

Measured on every skinned figure glb.

- **The skeleton is MU's Bip01.** Joint names read `Bip01`, `Bip01 Footsteps`, `Bip01 Pelvis`,
  `Bip01 L Thigh`, `Bip01 Spine`, `Bip01 R Hand` … alongside MU's own dummies
  (`dummy_9`, `Bone01`–`Bone08`, `Mesh01`–`Mesh04`, `knife_gdf`, `hand_bofdgne01`, `gfh`).
- **Exactly one skin per file.** All 103 skinned figure glb have `skins_count == 1`.
- **Inverse bind matrices are present in every one** (`inverseBindMatrices` on every skin).
- **Joint counts per skin:**

  | rig | joints | used by |
  |---|---|---|
  | player | **60** | all 15 `players/body`, all 45 `items/armor`, `Skeleton01` |
  | `Man01` | 43 | 4 `Man*` NPC parts, `Smith01`, `ElfWizard01` |
  | `Female01` | 48 | 4 `Female*` NPC parts, `Giant01` |
  | `Girl01` | 37 | 3 `Girl*` NPC parts, `Hunter01` |
  | monsters | 9 – 60 | one per monster, see §5 |
  | lobby faces | 100 – 115 | `NewFace01/02/03` — the largest rigs in the tree |
  | bows/crossbows | small | 5 weapon glb carry their own skin and one clip |

- **Every part of a set has the identical joint list, in the identical order, as its clip
  library.** Verified by name-and-order comparison: all 15 `players/body` parts,
  `items/armor/ArmorMale10` and `Skeleton01` produce the same 60-name list as
  `players/rig/player.actions.glb`; the Man/Female/Girl parts match their libraries the same
  way. **One bone palette per figure serves all of its parts** — a Dark Knight is one 60-bone
  palette and 5–7 draws, not 5 palettes.

- **The `uvec4` trap of `docs/conventions.md` — the data does NOT hit it, but it does hit a
  neighbouring one.** Every `JOINTS_0` accessor in every figure glb has `componentType`
  **5123 = UNSIGNED_SHORT**, not UNSIGNED_BYTE. bgfx's `AttribType` has `Uint8`, `Uint10`,
  `Int16`, `Half` and `Float` — **there is no unsigned 16-bit vertex attribute type**. So the
  bytes cannot be handed to bgfx as they sit. The measured maximum joint index actually
  referenced is small (`ArmorClass02` 36, `ArmorMale10` 43, `BullFighter01` 39, `Skeleton01`
  39, `NewFace01` unknown but ≤115), and the largest rig is 115 joints, so **u16 → u8
  converts losslessly** and the shader side stays `uvec4` as conventions requires. That
  conversion is a cook-time step that does not exist yet.

- **`WEIGHTS_0` is `f32`/`VEC4` on every figure glb**, and the weights sum to 1.0 within 0.01
  on every vertex checked (0 bad out of 108/230/3072/222/2908 on the sampled files). vec4
  fits with nothing to spare and nothing wasted.

---

## 4. The clips

**Two different homes, and that is the structural fact.**

- **Monsters: embedded.** 13 of 14 monster glb carry **7 clips each**, named `action0` …
  `action6`. `Skeleton01` carries **0** (see §5).
- **Players and NPCs: an external library**, `*.actions.glb`, meshless (`meshes: 0`,
  `nodes: 61`, one skin, 283 animations for the player) — **and not present here** (§0).
- **Bows and crossbows: embedded**, 1 clip each on their own small skin
  (`Bow01`, `Bow02`, `CrossBow01/03/04`).

**Everything is baked per-bone TRS, and nothing is a morph.** Every channel on every clip
measured targets `translation` or `rotation` only — **no `scale` channel anywhere**, no
`weights` channel, no morph targets. Every sampler is `LINEAR`. So a bake to a flat pose
array is a straight resample, and a crossfade is a per-bone quaternion/vector blend.

**The player library, measured** (`MU2/build/players/rig/player.actions.glb`):
283 clips, 28 300 channels, 60 joints of which **50 are animated** (100 channels a clip =
50 translation + 50 rotation). Sample counts run **3 to 101**; the sum is 3 012 keys over
**347.9 s** of clip. Examples:

    action0  "Set"                 3 keys  0.3200 s
    action1  "Stop male"           7 keys  0.8571 s
    action15 "Walk male"           7 keys  0.8000 s
    action38 "Attack fist"         8 keys  0.4667 s
    action39 "Attack sword right 1" 8 keys 1.1200 s

**The clip's duration is exactly `(samples − 1) / (action_speeds[i] × 25)`** — true for all
283 clips with a speed, to within 0.005 s, 0 exceptions. That is MU's own rule: `action_speeds`
is the per-frame key advance at MU's 25 Hz, so the clip's frame rate is `speed × 25`
(`Walk male` 0.3 × 25 = 7.5 fps). This is the `per-frame-rates-need-their-order` rule of
`docs/conventions.md` in its concrete form, and it is the number the crossfade and the walk
have to be driven by.

**The index tables, and what is actually in them.**

| table | rows | what it is | example rows |
|---|---|---|---|
| `actions` | 286 (keys 0–285) | the player action **names** | `0: "Set"`, `1: "Stop male"`, `15: "Walk male"`, `25: "Run"`, `38: "Attack fist"`, `39: "Attack sword right 1"` |
| `action_speeds` | 246 | key advance per 25 Hz frame → clip fps = ×25 | `1: 0.28`, `10: 0.3`, `15: 0.3`, `38: 0.6`, `39: 0.25` |
| `action_keys` | 284 | MU's own key count | `0: 3`, `1: 6`, `15: 7`, `38: 7` |
| `action_travel` | **26** | metres the clip is meant to carry the figure | `15–22: 2.4288`, `24: 3.0921`, `25–31: 2.4288`, `34/35: 0.0032`, `36: 3.7667`, `37: 3.7388`, `77: 2.4288`, `226: 0.0717`, `246: 0.0436`, `259: 3.7667`, `260: 3.7388` |
| `monster_actions` | 12 | the monster clip slot names | `0 Idle, 1 Idle 2, 2 Walk, 3 Attack 1, 4 Attack 2, 5 Shock, 6 Die, 7 Appear, 8 Attack 3, 9 Attack 4, 10 Run, 11 Attack 5` |
| `monster_holds` | 1 | the slots that hold their last frame | `[6]` — Die |

`action_keys` is **not** the clip's key count in the library: `samples − 1 == action_keys` on
247 of 283 clips and differs on **36**, where `samples == action_keys` instead (`action0` 3/3,
`action10` 9/9, `action15`–`action25` 7/7, …). `action_speeds` covers only 246 of 286 actions
and `action_keys` misses 284 and 285.

**`action_travel` is not root motion.** Measured on `action15`, `action1` and `action25`: the
`Bip01`, `Bip01 Footsteps` and `Bip01 Pelvis` translation channels move only **vertically**
(`action15` Bip01 range 0.000 / 0.081 / 0.000 in x/y/z; `action25` Footsteps 0 / 0.235 / 0).
The clips are **in place**. So the walk speed has to come from `action_travel ÷ duration`
(`2.4288 m ÷ 0.8 s = 3.036 m/s` for `Walk male`) — *inferred*, not confirmed against MU2's own
code, which was not read for this census.

**Monster clips, measured per model** (channels / max samples / duration, LINEAR throughout):

| monster | nodes | idle (0) | idle2 (1) | walk (2) | atk1 (3) | atk2 (4) | shock (5) | die (6) |
|---|---|---|---|---|---|---|---|---|
| Agon01 | 42 | 21/3.200 | 21/4.000 | 7/0.706 | 8/0.848 | 8/0.848 | 7/0.480 | 13/0.873 |
| BeetleMonster01 | 48 | 21/3.200 | 21/4.000 | 7/0.480 | 7/0.727 | 7/0.727 | 7/0.480 | 13/0.873 |
| BudgeDragon01 | 40 | 7/0.960 | 31/6.000 | 7/0.343 | 8/0.848 | 8/0.848 | 8/0.560 | 7/0.436 |
| BullFighter01 | 46 | 21/3.200 | 31/6.000 | 7/0.706 | 10/1.091 | 10/1.091 | 14/1.040 | 9/0.582 |
| ChainScorpion01 | 10 | 7/0.960 | 7/1.200 | 6/0.500 | 7/0.727 | 7/0.727 | 7/0.480 | 7/0.436 |
| ForestMonster01 | 42 | 31/4.800 | 31/6.000 | 7/0.706 | 7/0.727 | 7/0.727 | 7/0.480 | 9/0.582 |
| Giant01 | 49 | 21/4.571 | 21/5.714 | 7/1.008 | 9/1.385 | 9/1.385 | 21/2.286 | 11/0.727 |
| Goblin01 | 46 | 13/1.920 | 13/2.400 | 7/0.400 | 7/0.727 | 7/0.727 | 8/0.560 | 8/0.509 |
| Hound01 | 37 | 8/1.120 | 16/3.000 | 7/0.706 | 10/1.091 | 10/1.091 | 14/1.040 | 13/0.873 |
| Hunter01 | 38 | 26/4.000 | 26/5.000 | 7/0.706 | 7/0.727 | 7/0.727 | 7/0.480 | 7/0.436 |
| Lich01 | 51 | 21/3.200 | 21/4.000 | 7/0.706 | 13/1.455 | 9/0.970 | 21/1.600 | 12/0.800 |
| Spider01 | 28 | 7/0.960 | 7/1.200 | 5/0.133 | 8/0.848 | 8/0.848 | 8/0.560 | 13/0.873 |
| StoneGolem01 | 32 | 21/4.571 | 21/5.714 | 7/1.008 | 7/1.039 | 7/1.039 | 7/0.686 | 6/0.364 |
| **Skeleton01** | 61 | — | — | — | — | — | — | — |

Each monster's `index.json` row carries its own `action_keys` (7 entries, slots 0–6) and
`action_travel` (usually `{"2": <metres>}`). Monster clip key counts follow `samples = keys + 1`
on the rows checked (Agon `0: 20` → 21 samples).

**Cost of baking flat.** The player library resampled to 25 Hz is **8 983 poses**; at 60 bones
× 3 `vec4` in RGBA32F that is **25.9 MB** of bone texture for the clip data alone. Kept at the
source keys (3 012) it is **8.7 MB**. Since every clip is LINEAR with 3–101 keys and the
source rate is already `speed × 25` (never above 25 Hz), **resampling to 25 Hz adds 3× the
data and no information**. Whether sprint 4 bakes at all, or bakes to each clip's own rate,
is a decision this census does not make.

---

## 5. The monsters, and what Lorencia spawns

**14 monster glb, 16 `monsters` rows.** Two rows reuse another's glb with a different scale
and gear: `EliteBullFighter01` → `BullFighter01.glb` (scale 1.15 vs 0.8), `EliteGoblin01` →
`Goblin01.glb` (1.2 vs 0.8). `mu.db`'s `monster_kinds` has the same 16 numbers, and
`index.json`'s `breeds` mirrors it — 16 rows, no orphans in either direction.

| model | tris | prims | materials | images | texels | joints | clips |
|---|---|---|---|---|---|---|---|
| Agon01 | 536 | 2 | 3 | 3 | 2.69 M | 41 | 7 |
| BeetleMonster01 | 434 | 2 | 2 | 2 | 0.17 M | 46 | 7 |
| BudgeDragon01 | 320 | 2 | 3 | 3 | 2.24 M | 39 | 7 |
| BullFighter01 | 680 | 4 | 5 | 3 | 3.15 M | 45 | 7 |
| ChainScorpion01 | 252 | 1 | 1 | 2 | 1.09 M | 9 | 7 |
| ForestMonster01 | 512 | 1 | 1 | 2 | 1.20 M | 41 | 7 |
| Giant01 | 709 | 1 | 1 | 3 | 3.15 M | 48 | 7 |
| Goblin01 | 376 | 1 | 1 | 2 | 1.20 M | 45 | 7 |
| Hound01 | 638 | 3 | 4 | 3 | 2.36 M | 36 | 7 |
| Hunter01 | 424 | 1 | 1 | 2 | 1.64 M | 37 | 7 |
| Lich01 | 586 | 3 | 4 | 3 | 2.36 M | 50 | 7 |
| Skeleton01 | 486 | 1 | 1 | 3 | 2.24 M | **60** | **0** |
| Spider01 | 168 | 1 | 1 | 2 | 2.10 M | 27 | 7 |
| StoneGolem01 | 460 | 1 | 1 | 3 | 0.11 M | 31 | 7 |

**`Skeleton01` is a player-rig figure, not a monster model.** 60 joints, the identical
60-name joint list as `player.actions.glb`, and no embedded clips. Its `index.json` row is
shaped like a character rather than a monster: `"glb"` is a **list**, `model_index` is **−1**,
and it carries `"stance": "sword"` and `"death": "bone_burst"` in place of `action_keys` /
`action_travel`. It is animated from the player library — which is the one thing missing (§0).

**Lorencia's spawn table** (`mu.db` `monster_spawns where map=0`, 9 rows; identical to
`index.json`'s `breeds`/`monsters` spawn arrays for map 0):

| id | breed | model | rect (x1,x2,y1,y2) | count |
|---|---|---|---|---|
| 1 | 3 Spider | Spider01 | 180,226, 90,244 | 45 |
| 2 | 2 Budge Dragon | BudgeDragon01 | 180,226, 90,244 | 40 |
| 3 | 2 Budge Dragon | BudgeDragon01 | 135,240, 20,88 | 20 |
| 4 | 0 Bull Fighter | BullFighter01 | 135,240, 20,88 | 45 |
| 5 | 1 Hound | Hound01 | 8,94, 11,244 | 45 |
| 6 | 4 Elite Bull Fighter | BullFighter01 @1.15 | 8,94, 11,244 | 45 |
| 7 | 6 Lich | Lich01 | 95,175, 168,244 | 20 |
| 8 | 14 Skeleton Warrior | **Skeleton01** | 95,175, 168,244 | 15 |
| 9 | 7 Giant | Giant01 | 8,60, 11,80 | 15 |

**Lorencia spawns 290 monsters at once, across 7 distinct models.** Not 30. All 290 are alive
in the sim simultaneously (`respawn_seconds: 10` on every kind, so the population is held
full). Of those 290, **15 (Skeleton Warrior) cannot be animated from `assets/` as it stands**.

The other seven models — Agon, Beetle, Chain Scorpion, Elite Goblin, Forest Monster, Goblin,
Hunter, Stone Golem — have **no Lorencia spawn** and are for other maps.

Gear attached to spawned monsters, and the bone it hangs on — **all 18 bone names referenced
by `index.json` exist in their glb and are skin joints** (checked by name against each skin's
joint list, 18/18 present):

    BullFighter01/Elite  right_hand_bone "left_bone"    Axe07 / Spear08
    Hound01              "Bone_left"                    Sword05
    Lich01               "knife_gdf"                    Staff03
    Giant01              "knife_bone" + "hand_bone01"   Axe03 x2
    Agon01/EliteGoblin   "knife" + "shield"             Sword09 / Mace02 + Shield02
    SkeletonWarrior      (no bone named)                Sword07 + Shield05
    effect bones: "Bip01 Head", "smok_bone", "light_point", "top_bone01/02" — all present

---

## 6. The numbers sprint 4 is judged against

> *"a Dark Knight in armour and 30 animated monsters in the town, inside budget"* — `PLAN.md`

**A Dark Knight in armour** = `characters["DarkKnight"]`: `HelmMale10` + `ArmorMale10` +
`PantMale10` + `GloveMale10` + `BootMale10` + `Sword01` + `Shield10`.

| part | tris | prims | images |
|---|---|---|---|
| HelmMale10 | 42 | 2 | 192² basecolor, 1024² orm, 1024² normal |
| ArmorMale10 | 198 | 1 | 384², 1024², 1024² |
| PantMale10 | 94 | 1 | 384², 1024², 1024² |
| GloveMale10 | 174 | 1 | 192², 1024², 1024² |
| BootMale10 | 186 | 1 | 192², 1024², 1024² |
| Sword01 (unskinned) | 68 | 3 | **519²**, 1024², 1024² |
| Shield10 (unskinned) | 20 | 1 | 192², 1024², 1024² |
| **total** | **782** | **10** | 21 distinct images |

    DK triangles   = 42+198+94+174+186+68+20 = 782
    DK bones       = 60 (one palette, shared by all 5 skinned parts)
    DK texels      = 14 x 1024² + 4 x 192² + 2 x 384² + 1 x 519²
                   = 14 680 064 + 147 456 + 294 912 + 269 361 = 15 391 793  (15.39 Mtexels)

**30 animated monsters.** Drawn from Lorencia's own mix, weighted by the 290 spawns:

`hidden_mesh` is taken out where it applies (per-primitive counts measured: `BullFighter01`
is `[6, 626, 26, 22]` so primitive 0 costs 6; `Hound01` is `[160, 172, 306]` so primitive 0
costs **160**). Whether `hidden_mesh` indexes a primitive or a glTF mesh is **unverified** —
these files have one mesh each, so the two readings coincide here.

| model | share of 290 | tris drawn, incl. held gear |
|---|---|---|
| BudgeDragon01 | 60 | 320 |
| BullFighter01 | 45 | 680 − 6 hidden + Axe07 64 = 738 |
| EliteBullFighter01 | 45 | 680 − 6 hidden + Spear08 112 = 786 |
| Hound01 | 45 | 638 − 160 hidden + Sword05 65 = 543 |
| Spider01 | 45 | 168 |
| Lich01 | 20 | 586 + Staff03 68 = 654 |
| Giant01 | 15 | 709 + Axe03 64 + Axe03 64 = 837 |
| SkeletonWarrior | 15 | 486 + Sword07 66 + Shield05 22 = 574 |

    weighted mean = (320x60 + 738x45 + 786x45 + 543x45 + 168x45 + 654x20 + 837x15 + 574x15) / 290
                  = 154 020 / 290 = 531.1 tris per monster
    30 monsters   = 30 x 531.1 = 15 933 triangles
    all 290       = 154 020 triangles   (161 760 if the hidden primitives are drawn)

    bones: weighted mean = (39x60 + 45x45 + 45x45 + 36x45 + 27x45 + 50x20 + 48x15 + 60x15) / 290
                         = 11 845 / 290 = 40.8 bones per monster
    30 monsters   = 1 225 bones;  + DK 60 = 1 285 bones
    bone texture  = 1 285 x 3 vec4 x 16 B = 61.7 KB RGBA32F   (3 855 texels; a 256x16 RGBA32F
                    texture holds 4 096 — the palette for the judged scene is one small texture)

**Texture texels for the judged scene.** 41 distinct images across the 8 Lorencia breeds and
their gear (**32 866 304 texels**, of which 30 images are 1024² = 31.46 M, **95.7%**), plus
the DK's 21 (15 391 793; 14 at 1024² = 95.4%):

    scene texels   = 32 866 304 + 15 391 793 = 48 258 097  (48.26 Mtexels, 62 distinct images)
    RGBA8, no mips = 48.26 M x 4 B                       = 193.0 MB
    BC7 + full mip chain = 48.26 M x 1 B x 4/3           =  64.3 MB
    of which 44 images at 1024² (46.14 M texels)         =  61.5 MB, 95.6% of it

**The judged frame, against sprint 2's measured 2.30 ms bare land:**

    triangles added  = 782 (DK) + 15 933 (30 monsters) = 16 715
    as a fraction of the ground's 131 072                = 12.8%
    as a fraction of the town's 432 248 placed           =  3.9%
    submitted 3x (shadow, prepass, shade)                = 50 145

Triangles are not the risk. **62 textures at 64.3 MB for 16 715 triangles is**, and so is the
draw count: the DK alone is 10 primitives, and 30 monsters across 8 models with gear is
another 19 body primitives + 7 gear models before instancing groups them.

---

## 7. Traps

Each one checked against the files, not assumed.

1. **TEXCOORD_0 zeroed with the real UVs in TEXCOORD_1 — DOES NOT apply to MU2's figures.**
   `mesh.cpp:155-167` warns about this for Fab-converted figures. Measured: every figure glb
   carries both `TEXCOORD_0` and `TEXCOORD_1` on every primitive (uniformly — 155 of 155), and
   **zero vertices have `TEXCOORD_0 == (0,0)`** in `ArmorClass02` (0/108), `HelmClass02`
   (0/190), `ArmorMale10` (0/230), `Sword01` (0/234), `BullFighter01` (0/3072), `Spider01`
   (0/245), `Skeleton01` (0/578), `ManUpper01` (0/222), `ElfWizard01` (0/2908). Set 0 is the
   real layout, as it is for the town. The comment's caution is correct and its case is absent.
   `cook.py:290` already takes set 0 and nothing else for the same reason.

2. **Bind pose vs rest pose — NOT a trap; the data is clean.** For each skin, the joint node's
   composed world matrix times its inverse bind matrix was compared to identity.
   Max |W·IBM − I| = **0.00000** on `ArmorClass02`, `ArmorMale10` and `ManUpper01`, and
   **0.00001** on `BullFighter01`. The node hierarchy's rest pose **is** the bind pose, so a
   figure loaded with an identity palette draws in bind pose correctly, and the sway models of
   sprint 3 that draw in bind pose today are right.

3. **Inward-facing normals — not found.** Checked as the fraction of triangles whose
   geometric normal (from the index winding) opposes the averaged vertex normal:
   `BullFighter01` 0/680, `Spider01` 0/168, `Skeleton01` 0/486, `HelmClass02` 0/119,
   `Sword01` 0/68. Nothing is turned inside out.

4. **Mixed winding on single sheets — present, small, and already covered.**
   `ArmorClass02` 3/120 flipped, `ArmorMale10` 2/198, `ElfWizard01` 2/629, and
   **`ManUpper01` 16/250 (6.4%)**. This is exactly what `docs/conventions.md` records
   ("MU's figures are single sheets with mixed winding and are drawn two-sided"), and the data
   backs it absolutely: **311 of 311 figure materials have `doubleSided: true`**. No figure
   material anywhere in `assets/` is single-sided. Two-sided is not a flag to decide per
   material here; it is the constant.

5. **Joints referenced that do not exist — none.** Max `JOINTS_0` index measured is always
   inside the skin: 36/60, 20/60, 43/60, 39/45, 23/27, 39/60, 31/43, 39/43. No out-of-range
   index on any file checked. All 18 attachment and effect bone names from `index.json` resolve
   to real skin joints (§5).

6. **`JOINTS_0` is UNSIGNED_SHORT and bgfx has no `Uint16` attribute.** §3. It converts to u8
   losslessly (largest rig 115 joints) but it does not pass through untouched. A loader that
   hands the bytes to bgfx as `Uint8` without converting reads **the low byte of each u16 and
   the high byte of the next** — joint 0 for half the palette, which looks like a figure
   collapsed to the origin rather than like a type error. Metal will not complain.

7. **`Sword01_basecolor` is 519×519** — the only image in all 155 figure glb whose dimensions
   are not a multiple of 4, and it is on the Dark Knight's own sword, i.e. inside the sentence
   the sprint is judged by. BC7 encodes 4×4 blocks; 519 is not divisible by 4 and 519/2 = 259.5
   does not halve cleanly down a mip chain either. `cook.py` has no case for it today.

8. **`hidden_mesh` — two monsters hide a primitive.** `BullFighter01` and `Hound01` both carry
   `"hidden_mesh": 0`, meaning primitive 0 is not drawn (MU's convention for a mesh replaced by
   the held weapon). Draw it and the monster wears two axes. It matters here because both are
   Lorencia spawns: Bull Fighter 45 + Elite Bull Fighter 45 + Hound 45 = **135 of the 290**.
   Measured per primitive: `BullFighter01` is `[6, 626, 26, 22]` (6 hidden) and `Hound01` is
   `[160, 172, 306]` — **Hound hides a quarter of itself**. §6 subtracts both. Whether
   `hidden_mesh` indexes a primitive or a glTF mesh is unverified; both files have one mesh, so
   it does not matter for these two, and would for a multi-mesh model.

9. **`cook.py` refuses a mesh node with a transform — and no figure has one.** `cook.py:253-260`
   raises `"a mesh node carries a transform"`. Measured: **0 of 155** figure glb have a mesh node
   with `matrix`/`translation`/`rotation`/`scale`. The other precondition, `cook.py:294` raising
   on a primitive without `TANGENT`, also holds: **0 primitives missing TANGENT** across all 155.
   So the existing cook's two guards both pass on figures; what it lacks is
   `JOINTS_0`/`WEIGHTS_0` in the vertex struct, the skin, and the clips.

10. **`cook.py` is scoped per world (`--world lorencia`) and writes to
    `assets/cooked/<world>/`.** Figures are not under `assets/world/`, so no figure has been
    cooked and there is no path that would reach one. The `.mum` header is
    `MU2M` version 1 with a `<3f3f3f2f>` vertex (position, normal, tangent4, uv2 = 48 B,
    matching `content/mesh.h`'s `Vertex`); a skinned vertex adds 4 joint bytes + 4 weight
    floats = 20 B, a 42% growth, and the header version would have to move.

11. **`action_keys` is not the clip's key count** on 36 of 283 player clips (§4). Driving a
    clip off `action_keys` rather than off the glb's own sample count makes those 36 run
    12–14% fast or slow — a wrong pace, not a wrong picture.

12. **The walk clips have no root motion** (§4). A figure animated from `action15` and moved by
    nothing will moonwalk in place; a figure moved at a speed picked independently of
    `action_travel ÷ duration` will slide. `action_travel` covers only **26 of 286** actions.

13. **`Skeleton01` has 0 clips and 60 joints.** A loader that animates a monster from its own
    glb silently draws 15 of Lorencia's 290 in bind pose. Its `index.json` row is also the only
    one where `"glb"` is a **list** rather than a string, and the only one with
    `model_index: -1` — a parser written against the other 15 rows will throw or take the wrong
    branch on it.

14. **Weapons and shields are unskinned.** 52 mesh nodes across `items/weapons`, `items/shields`
    and `items/misc` have no skin, so they attach to a bone rather than deform. For monsters the
    bone is named (`right_hand_bone`); **for `characters` no bone is named at all** — the
    `DarkKnight` row gives `right_hand: Sword01` and `left_hand: Shield10` and nothing else. The
    player rig does carry `knife_gdf` and `hand_bofdgne01` as joints, which are MU's weapon and
    shield dummies, but **which bone each hand item uses is unknown from the data here** and has
    to come from MU2's own code.

15. **Five weapons are skinned and carry a clip of their own.** `Bow01`, `Bow02`, `CrossBow01`,
    `CrossBow03`, `CrossBow04` each have one skin, 13–14 nodes, and one 8-key 1.12 s
    `action0` on `Bone01`–`Bone03`. A bow is not a static attachment; it animates on its own
    timeline, in sync with the figure's draw action. `CrossbowGuard` is one of Lorencia's 14
    placed figures and carries `CrossBow04`.

16. **Monsters carry more than geometry in `index.json`.** `effects` (5 monsters: breath_fire,
    ground_dust, snort_smoke, death_dust, with per-clip `windows` given as normalised
    `from`/`through` positions in a named clip), `eyes` (EliteBullFighter, two bone-anchored
    sprites), `sprites` (ChainScorpion, a flickering light on `light_point`), `tint`
    (EliteBullFighter recolours a material named `fur`), `metal` (Agon `brass`, Giant `mu2` —
    names from `index.json`'s 33-entry `materials` library), `death` (`bone_burst`,
    `stone_burst`). These are sprint 6's and sprint 8's, but **the `windows` are expressed as
    fractions of a clip's length**, so whatever sprint 4 does with clip time has to be
    queryable as a normalised position or those windows cannot be honoured later.

---

## 8. Unknown

- Which bone a character's `right_hand`/`left_hand` item attaches to (trap 14).
- Which `objects` row (item level) maps to which `<Slot>Male<NN>` glb — on each row's own
  `glb` field; not enumerated here.
- `NewFace01/02/03`'s max referenced joint index (rigs of 100–115 joints; only the joint
  *count* was read, not the per-vertex maximum).
- How many monsters are on screen at once from the play camera. 290 exist; sprint 2 says the
  camera sees roughly a fiftieth of a 256 m map, which would be ~6 — but the nine spawn
  rectangles are not uniform over the map and none of them was intersected with the camera's
  footprint. **The sentence's "30" is not derived from anything measured here.**
- Whether MU2 drives walk speed from `action_travel ÷ duration` (inferred in §4, not read).
- Whether `hidden_mesh` indexes a primitive or a glTF mesh (trap 8) — indistinguishable on the
  two files that use it.
- Whether `assets/interface/` holds anything a figure needs (111 png, not opened).
- `assets/effects/` (51 .obj + 57 .png) was not opened; it is sprint 6's.
