# The second class's gear

Started 2026-10-04. The user: "lets find what else items can use 2nd class like weapons and
armors, we decide which we make", then picked the early sets. The 2nd wings are
docs/second-wings.md; the class change is docs/class-change-quest.md.

## What MU has

WebZen's own item list, `Data/lang/Kor/item(Kor).txt`, Version 1.00m (2005-03-07; the 0.99.60T
package, github makaytrue/0.99.60T), against OpenMU's VersionSeasonSix rows: **41 items** in
groups 0-11 ask a second class. None was built here.

| wave | Blade Knight | Soul Master | Muse Elf |
|---|---|---|---|
| early sets | **Dark Phoenix** (17), drop 86-100 | **Grand Soul** (18), drop 70-91 | **Divine** (19), drop 72-92 |
| early arms | Dragon Spear 3/10 | Dragon Soul Staff 5/9, Grand Soul Shield 6/15 | Elemental Mace 2/7, Celestial Bow 4/17, Great Reign Crossbow 4/19, Elemental Shield 6/16 |
| later sets | Great Dragon (21), drop 94-126 | Dark Soul (22), drop 87-122 | Red Spirit (24), drop 84-109 |
| later arms | Dark Breaker 0/17, Knight Blade 0/20 | Staff of Kundun 5/11 | Arrow Viper Bow 4/20 |

The waves are this doc's grouping (drop level and the sets' order), not a dated release list.
Season 2 adds 32 more at level 380 (Dragon Knight, Venom Mist, Sylphid Ray, Flamberge, Sword
Breaker, Imperial Staff and their kind); not taken.

**The user's pick (2026-10-04): the three early sets**, fifteen pieces.

## How the gate is carried

A recipe's `stats.class_level: 2` rides index.py into the index, and cook.py sets the class's
bit again eight higher in the item's i32 `classes` (bit 8 Soul Master, 9 Muse Elf, 10 Blade
Knight), so nothing that reads bits 0-2 changes. `sim::secondClassOnly(row)` reads it; `fits`
refuses such a thing to a hero who is not his class's second (`Wearer::second`), and the card
names the second class in its class line, red until he is one. The 2nd wings ride the same
path.

## The sets

MuMain loads the set at index 17 from file 18 (`18 + i`, ZzzOpenData.cpp:180-198): Dark Phoenix
is `*Male18`, Grand Soul `*Male19` (its pants `t_PantMale19`), Divine `*Male20`. Rows off OpenMU
VersionSeasonSix Armors.cs; WebZen's 1.00m row agrees in every number checked.

### Dark Phoenix (Blade Knight) -- built 2026-10-04

| piece | group | drop | defence | durability | strength | agility | extra |
|---|---|---|---|---|---|---|---|
| Helm | 7 | 92 | 43 | 80 | 205 | 62 | |
| Armor (2x3) | 8 | 100 | 63 | 80 | 214 | 65 | |
| Pants | 9 | 96 | 54 | 80 | 207 | 63 | |
| Gloves | 10 | 86 | 37 | 80 | 205 | 63 | attack speed 6 |
| Boots | 11 | 93 | 40 | 80 | 198 | 60 | walk speed 2 |

Sheets head_bld, upper_bld, lower_bld (and its .OZT as lower_bld_t on the cuirass's skirt),
gloves_bld, boots_bld, hide, level_man01: blue lacquer with gold trim, on `painted_steel` --
as `plate_steel` the blue read as polished black metal with white blowouts in the fire.

Its pants and cuirass: the grain override and the skirt's cutout below.

### Grand Soul (Soul Master) -- built 2026-10-04

Helm 81/27, armour (2x3) 91/33, pants 86/30, gloves 70/20 (attack speed 5), boots 76/22;
durability 67; strength 59 (gloves 49), agility 20 (gloves and boots 10). A dark violet robe
with gold trim, on `cloth`; the helm's level_man022 is the wizard's own head under the hood
(face `skin`, white hair `hair`). **The pants' 56-triangle panel is MU's physics cloth**
(CPhysicsClothMesh off bone 17, `t_lower_14m.tga`, ZzzObject.cpp:10774-10796): drawn rigid
here for now, a cape standing a little off his back. How to draw it is open (a hanging cape
with a sway, or real cloth).

### Divine (Muse Elf) -- built 2026-10-04

Helm 85/37, armour (2x2) 92/44, pants 88/39, gloves 72/29 (attack speed 6), boots 81/30;
durability 74; strength 50, agility 110. Magenta-violet lacquer with gold trim on
`painted_steel`, her bare skin (`level_man033`) on `skin`, the cuirass and pants cut by their
.OZT alpha. The helm's `level_man01` piece is a pale square of that sheet, drawn by MU as it is
(HideSkin is false for every character part), on `feather`. **Two texture coordinates in MU's
own HelmMale20.bmd are (0, 1.114222e+23)**; the bake layout shrank to nothing round them and
the helm baked black until each took a neighbouring corner's coordinate in the .obj.

### Traps these sets found

- **Sheets by group.** A sheet renamed on disk -- a jpg and a tga of one stem (`lower_14m_jpg`,
  `lower_14m_tga`), an .OZT kept apart (`lower_bld_t`) -- has to be mapped to its obj group in
  the recipe's `sheets`, or that group bakes black with only a log line to say so.
- **painted_steel's grain is a blade's**, brushed along V: on armour it drew vertical streaks.
  The sets override it with plate_steel's cast grain.
- **Glossy paint wants a shallow grain.** At painted_steel's 0.28 roughness the cast pits at
  plate_steel's depth 0.05 each caught a glint: white specks over Divine's flanks and
  pauldrons. Divine takes depth 0.02, range 0.03.
- **Cutouts are declared, not detected**, and name the group.

## Where it falls

The user, 2026-10-04: "there will be tarkan map where 2nd class drop, also BC6 and strongest
monsters from atlans also can drop some". Ours, all of it (`sim::secondClassOnly` rows):

- **Out of every ordinary and excellent pool** but Tarkan's (MU's map 8, `sim::kTarkanMap`, not
  built yet) and Blood Castle 6's, where it falls by the level window as anything does --
  castle 6's garrison stands to about 99, which reaches the early sets' 70-100.
- **Atlans's strongest** (`kAtlansGearFromLevel` 43: Valkyrie 46, Vepar 45, Bahamut 43): one
  kill in `kAtlansGearOdds` (400) leaves a random piece at +0 with a drop's luck and option, on
  its own dice (`gearDice_`). Their levels never reach the gear by the window.
- The 2nd wings never drop (`drops_from_monsters` false); they are the Goblin's.

`sim_test` testSecondClassDrops: 108 pieces off 40,000 Atlans kills at 46, none under 43, none
in the Lost Tower at 99, none in castle 1, some in castle 6.
