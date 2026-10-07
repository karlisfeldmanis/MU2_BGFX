# The 2nd level wings

Started 2026-10-04. The user: "lets work on second wings, which is for second class". Wings of
Spirits, Soul and Dragon, MU's group 12 numbers 3-5 (ours 13, 14 and 16, below), slot 7, one
each for the Muse Elf, the Soul Master and the Blade Knight. The Magic Gladiator's Wings of
Darkness (MU's 12/6) is cooked as ours 12/22 (2026-10-07, below) and worn by nobody until his
class is in (docs/mg-port.md).
The 1st wings are docs/wings.md.

Sources: WebZen 1.00.93 (github ptr0x-real/Mu-GS-Webzen-MC-10093), base 0.97d branch unless a
flag is named; OpenMU VersionSeasonSix for the rows (Version095d has no 2nd wings: they come
with WebZen's later 0.97 update, NEW_FORSKYLAND2); MuMain for the drawing.

## How they are made

`WingChaosMix` (MixSystem.cpp:2470-2990); OpenMU's "2nd Level Wings", number 7.

- **Box:** exactly one 1st level wing, one Jewel of Chaos, one **Loch's Feather** (13/14), and
  any excellent things at +4 or more (`IsExtItem() && m_Level >= 4`), nothing else. A 2nd wing
  in the box spoils it.
- **Chance:** the wing's price / 4,000,000 + the excellent things' price / 40,000, at most 100.
  A +0 wing alone is 13%, a +9 one about 33%; an excellent +4 Chaos Dragon Axe adds about 11.
  A low excellent thing adds nothing (a Kris is under 40,000).
- **Zen:** 5,000,000.
- **Success:** the hero's class's 2nd wing at +0 (ours; WebZen's `3 + rand()%4` is any of
  four): luck one in five; the option at 4%, 10% or 20% for its third, second and first level;
  one of three extras one in five; which option kind it carries one in two.
- **Failure:** the box is emptied, the 1st wing with it (WebZen's; the user kept it).

**Loch's Feather** (OpenMU CreateFeather: 13/14, drop level 78, 1x2; MuMain Quest04.bmd,
"Used to upgrade wings" GT 748). MU drops it in Icarus alone (gObjMonster.cpp:4620,
m_bFeatherOnlyIcarus); this game has no Icarus, so **ours**: a kill in Atlans or the Lost Tower
of a monster of level 60 or over leaves one in 500 (`sim::kFeatherOdds`, its own dice).

## The wings

| | ours | MU's | class | size | defence | level |
|---|---|---|---|---|---|---|
| Wings of Spirits | 12/13 | 12/3 | Muse Elf | 5x3 | 30 | 215 |
| Wings of Soul | 12/14 | 12/4 | Soul Master | 5x3 | 30 | 215 |
| Wings of Dragon | 12/16 | 12/5 | Blade Knight | 3x3 | 45 | 215 |

**Our numbers are not MU's.** This project gave 12/3-6 to the knight's orbs (Defense,
Uppercut, Falling Slash, Lunge) before the wings came, so the wings moved to numbers 0.75
leaves empty (the user, 2026-10-04: 'Move the wings'). `sim::kSpiritsNumber` and its fellows
hold them; read every MU line about 12/3-5 as these.

Drop level 150, durability 200, never dropped (OpenMU VersionSeasonSix Wings.cs, class level 2).

- **Fight** (ObjAttack.cpp, `Wing->m_Type > MAKE_ITEMNUM(12,2)`): his blows x(132 + plus)%,
  what reaches him x(75 - 2 a plus)%, the same Life a blow as the 1st wings'.
- **Defence** + 2 a plus and the +10 triangle (zzzitem.cpp:878-896); **level** + 5 a plus
  (:594-597).
- **The option**, +4 a level (regeneration 1%), its kind by `PLUS_WING_OP1_TYPE`
  (zzzitem.cpp:1168-1222): Spirits regeneration or damage, Soul wizardry or regeneration,
  Dragon damage or regeneration.
- **The extras** (`m_NewOption`, zzzitem.cpp:1488-1505, `sim::Held::wing`): max life +50 and 5 a
  plus, max mana the same, or a 3% chance a blow ignores the defence it meets
  (ObjBaseAttack.cpp:1322-1339, a "perfect" hit).
- **Speed:** Dragon flies at 16, the others at 15 (ZzzCharacter.cpp:6324-6331).
- **Look** (MuMain): Wing04 (Spirits, one mesh, added, ZzzObject.cpp:5324-5327), Wing05 (Soul)
  and Wing06 (Dragon), each a second `_R` mesh lit by a pulse -- Dragon's
  `(sin(t*0.001) + 1) / 4`, Soul's `sin(t*0.001) + 1.1` (ZzzObject.cpp:9937-9944). An 8-key
  flap; 20, 75 and 80 bones.

## Steps

1. **The recipe and the feather** -- done 2026-10-04. `Recipe::SecondWings` ahead of the 1st
   wings in kOrder; `Held::wing` saved as "wing"; Loch's Feather imported (Quest04), indexed,
   in every world's tables, and its drop. The Goblin refuses the mix ("no Wings of Dragon is in
   this world's tables") until step 2.
2. **Import** -- done 2026-10-04. Wing04-06 from MuMain with NewingEl (added, as MU's
   BlendMesh 0), NEWWW/NEWW_R and NDW/NDW_R; the `_R` shells are MU's Bright meshes
   (TextureScript.cpp:38-39), added at a slow pulse -- Soul's [1, 1.1], Dragon's [0.25, 0.25]
   at MU's WorldTime*0.001: `pulse_rate: "slow"` in the recipe's glow, carried by asset.sh
   onto the tiled slot, export_gltf's extras and cook.py's mode bit 3 to
   `content::Material::slowPulse`. `separate_meshes`, or the weld made the shell and its base
   one set of faces and Blender kept one. The glow pass draws at LEQUAL, as MU's GL does, so
   a shell on its own base passes. Rows at our numbers, every world's tables (229 items),
   figures and the wardrobe (6 wings).
3. **Wear** -- done 2026-10-04 but for the class gate: slot 7 takes them at 215 + 5 a plus;
   x(132 + plus)% and x(75 - 2 a plus)%, 1 Life a blow on Soul and 3 on the others; defence
   + 2 a plus; the option by its kind bit (`sim::wingOption`); the extras -- max life and mana
   +50 and 5 a plus, the 3% ignore-defence draw (`Fighter::ignoreDefense`, its own draw only
   when above nought); price on the 1st wings' line. The card: "2nd level wings", MuMain's
   three lines, the option by kind, the extras in MuMain's words (GT 740-742).
   **The class gate** -- done 2026-10-04, once Sevina's class change landed (1eea5fa0):
   `sim::fits` refuses a 2nd wing to a hero who is not his class's second (`Wearer::second`),
   and the card names the second class ("Blade Knight"), red until he is one.
4. **Draw** -- done 2026-10-04: on Bone05 as the 1st wings, the pulse, Dragon's 16
   (`sim::kFastFlyFactor`). The bench's Wings tab has all six.

## The Wings of Darkness (2026-10-07)

The user: 'cook MG 2nd wings'. Wing07.bmd (MODEL_WING + 6) on NDEE.OZT, a 64x32 cutout, drawn
tested and blended as Satan's; 31 bones, the 8-key flap. The row is VersionSeasonSix
Wings.cs:83 -- 4x2, defence 40, level 215, drop level 150 -- at **ours 12/22**
(`sim::kDarknessNumber`), its option wizardry or damage by the kind bit (`sim::wingOption`),
and its class `gladiator`, a bit cooked ahead of the class (tools/cook.py's kClass, Kin 3), so
no class here may wear it: an unknown name would have cooked to 0, which is every class.
Figures, the wardrobe (7 wings) and every world's tables (269 items) cooked. Seen on the bench's
Wings tab.

**Drawn as MU draws it** (the user, 2026-10-07: 'wings look not finished'). Without the next two
it was grey shards.
- **The violet pass**: MU draws it again RENDER_BRIGHT | RENDER_CHROME on Chrome02 at
  (0.8, 0.6, 1) (ZzzObject.cpp:6970-6976). Here `game::kShineDarkness` (256, above the shine's
  flags) on the wing's drawables, and shaders/shine.sh's `shineDarknessAdded`: the +7 chrome's
  scrolling UVs on Chrome02, at **0.4** of MU's strength -- at the excellent pass's 0.15 it was
  invisible, at MU's full 1.0 added in linear light every shard was flat cyan. The bench now
  lends the shine's sheets always, not only under --plus, and keeps the flag over a --plus.
- **The sparks** (:9981-10017): `WingLook::sparks`, every frame for ten ribs, a flare_blue at the
  rib's centre bone, Light (0.6, 0.3, 0.8), and two straight strips out to its partner bone, as
  MU's two joints are -- thunder 14 Scale wide and spirit 4 Scale + 5 wide, each one frame, ten
  tails in a line, Light (0.3, 0.3, 1) (ZzzEffectJoint.cpp:1239-1271, 688-720). Scale is MU's 20
  to 26 on sin(t * 0.004), in units. Both added: the spirit's RENDER_TYPE_ALPHA_BLEND is MU's
  EnableAlphaBlend, glBlendFunc(GL_ONE, GL_ONE) (ZzzOpenglUtil.cpp:402); drawn with true alpha its
  JPEG laid solid blue bands over the shards. Bones by name, as the cook keeps MU's names:
  Bone17-20 and 33 to Bone14-11 and 32, Bone30, 29, 34, 27, 26 to Bone03-07. Ours: the flare's
  10 cm and 0.7 of the light on all three. Sheets lent by `WingLook::lendSparks` beside the
  shine, in play and on the bench.
- **The safe-zone pose**: MuMain plays its action 1 (5 keys) instead of the flap while he stands
  in a safe zone (ZzzCharacter.cpp:6973-6976), Darkness alone of the wings; the rig carries both
  actions and `WingLook::update`'s `safe` picks it.
- **Its black ends faded** (ours; the user, 2026-10-07: 'work on transparency for black parts on
  wings'): NDEE's gradient runs to near-black, which MU draws solid; ndee.png's alpha is faded by
  luminance, nothing under 12, full from 70, so the shards' dark ends vanish into the violet
  joints behind them. Wing07.json's sheet_dark_note.
