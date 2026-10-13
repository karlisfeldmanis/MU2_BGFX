# The wings

The 1st level wings, then the 2nd (below, "The 2nd level wings"), and the Magic Gladiator's
Wings of Darkness at the end.

## The 1st level wings

Started 2026-10-04. The user: "lets work on first wings, we need logic how we can make first
wings". Wings of Elf, Heaven and Satan, group 12 numbers 0-2, slot 7 (`sim::kWings`).

### How they are made

Not a recipe of its own in MU: the Chaos Weapon's box with a Chaos weapon in it.

- **Box:** a Chaos Dragon Axe, Nature Bow or Lightning Staff at +4 or better with an option,
  one or more Jewels of Chaos; any Bless, Soul and other +4 things with an option raise the
  chance.
- **Chance:** the box's old price / 20,000, at most 100. **Zen:** 10,000 a percent.
- **Success:** the hero's class's wing at +0, its luck and option rolled as the Chaos weapon's
  are. **Failure:** the jewels gone, each thing a plus lower, its option down half the time.

| box | chance | Zen |
|---|---|---|
| Chaos weapon +4, +4 option, 1 Chaos | ~11% | 110k |
| the same with luck | ~14% | 140k |
| +4, luck, +12 option, 1 Chaos | ~30% | 300k |
| each Bless / Soul more | +5 / +3.5 | |

So the ladder is: a +4 thing with an option -> Chaos Weapon mix -> raise the Chaos weapon to
+4 and give it an option -> the wing mix with jewels.

Sources: WebZen 1.00.93 `MixSystem.cpp` `ChaosBoxMix` (:240-460, the value and `MixResult2`)
and `DefaultChaosMix` (:462-600, the rolls and the wing pick); OpenMU Version095d
`ChaosMixes.cs:172` "1st Level Wings", number 11. Version075 has the wings
(`Items/Wings.cs`, `DropsFromMonsters = false`) and no recipe for them.

**Ours** (the user's picks, 2026-10-04): the hero's class's wing, not WebZen's `rand()%3` of
any class; +0, as `CHAOS_MIX_WING_ITEMLEVEL_FIX`, not the base branch's +0..+4. Two Chaos
weapons in a box are wings, as WebZen's flag is (OpenMU takes one at most). The +S roll is
not taken (skills are orbs here).

### The items (OpenMU Version075 `Items/Wings.cs`)

| | number | size | defence | level | class | option |
|---|---|---|---|---|---|---|
| Wings of Elf | 0 | 3x2 | 10 | 180 | elf | Health Recover |
| Wings of Heaven | 1 | 5x3 | 10 | 180 | wizard | Wizardry Damage |
| Wings of Satan | 2 | 5x2 | 20 | 180 | knight | Physical Damage |

All: drop level 100, durability 200, damage x1.12 and damage taken x0.88 (each raised by the
wing's plus through OpenMU's per-level tables), luck, CanFly. Level 180 kept (the user).

### What MU draws (MuMain, `ZzzCharacter.cpp`)

- The wing is its own model (`Item/Wing01-03.bmd`) with its own flapping action, linked
  rigidly to player bone 47 at (0, 0, 15) (:15400-15430).
- Out of a safe zone a winged hero flies instead of running: `PLAYER_FLY`, or
  `PLAYER_FLY_CROSSBOW` with a crossbow (:615-621), the bow shots `PLAYER_ATTACK_FLY_BOW_UP`
  (:1053-1064). The wing flaps at PlaySpeed 1 while flying, 0.25 otherwise.
- Speed 15 against a run's 12 out of a safe zone, as on the Horn of Uniria (:6320-6335).

### Steps

1. **The recipe** -- done 2026-10-04. `Recipe::Wings` in `sim/machine.cpp`, ahead of the
   Chaos Weapon; the realm refuses it ("no Wings of Satan is in this world's tables") until
   step 2. `sim_test` `testChaosMachine` judges it.
2. **Import** -- done 2026-10-04. Wing01-03 from MuMain (`Item/Wing0N.bmd`, one mesh, 11/16/16
   bones, one 7-key flap each) with elfin_wing (added, as MU's BlendMesh 0), angel_wing and
   devil_wing (cut by their alpha, foliage for the Imp's reason), recipes in
   `source/items/wings/`, kind `wing`. Built, synced, their three rows merged into
   `assets/index.json`, every world's tables cooked (222 items). index.py carries a slot 7
   row's durability; cook.py takes the wings into a world's standalone figures beside the
   pets, which the next `--only figures` cook of each world writes. The mix now hands the
   wing over (`sim_test`: a 100% box gives a knight the Wings of Satan at +0, whole). In the
   bag and on the ground it is its own glb, unturned (MU turns it (270, 0, 45) there).
3. **Wear** -- done 2026-10-04, WebZen 1.00.93's base 0.97d branch throughout
   (`sim::firstWing`, `wingPower`, `wingDefense`; `sim_test` checks each):
   - his blows x(112 + 2 a plus)%, at 1 Life a blow for Heaven (the wizard's) and 3 for
     Satan and Elf (ObjAttack.cpp:1140-1210; NEW_FORSKYLAND3's elf at 1 not taken); what
     reaches him x(88 - 2 a plus)% (:1219-1280). Folded into `Body::pet` after Kinship, so
     the ring lifts a pet's price and not the wing's. Only while it has life.
   - defence: the row's (10/10/20) + 3 a plus, the triangle from +10 (zzzitem.cpp:880-896),
     worn down as armour's.
   - the option: Satan +4 a level on both damage bands, Heaven +4 on wizardry, Elf 1% life
     regeneration a level beside the rings' (zzzitem.cpp:1150-1165, :3026-3036).
   - luck counts as any lucky thing worn (ours: 5% critical, the project's luck, where WebZen
     gives a wing 4).
   - level 180 + 4 a plus (the rings' branch, zzzitem.cpp:628-632); Bless and Soul raise it
     (user.cpp:28576-28584 refuses only from 12/7).
   - wear by the hour, not on hits: 1/565 of a point every ten seconds worn, a point in
     94 minutes (gObjSecondDurDown); at nought it stays, broken and powerless, until mended.
   - price `40000000 + (40 + L) * L^2 * 11`, L = 100 + 3 a plus and the steeper rungs
     (zzzitem.cpp:2485-2488): 55.4 million at +0, a third of it sold.
   - the card: "Increase N% of Damage", "Absorb N% of Damage", "Increase speed" (GT 577-579,
     ZzzInventory.cpp:4270-4278), the Life price in red (ours, as the Imp's), defence as
     Armor with its rail, the option by kind. In the bag a wing lies span across (MU turns
     it (270, 0, 45) there).
4. **Draw** -- done 2026-10-04. `game/wings.h` (WingLook) hangs the wing on Bone05 at
   (0, 0, 15), its flap at rate 1 (MU's 0.25) and 4 while he flies (MU's 1). Off a safe tile,
   not riding, a winged hero flies (`sim::Body::flying`, at kFlyFactor, MU's 15 against the
   walk's 12): MU's stop fly 11 where he stands and fly 34 where he goes, 12 and 35 with a
   crossbow, played as the run ride is (0.34, slowed with the ground). It outranks Atlans's
   swim, as MU's wing does. **Effects**: MuMain gives the 1st wings none -- no sprite,
   particle or light, and no plus shine (ZzzObject.cpp:9566-9569); the Elf's are added.
5. **Wardrobe** -- done 2026-10-04. `cook.py --only wardrobe` takes kind `wing` (mesh and
   flap clip, and a `wings` list); every world's figures cooked `--no-monsters` so each
   carries Wing01-03. The bench has a **Wings** tab: each wing on its class's bare body,
   standing in the stop fly (`--category wings --pick Satan`). Studio shots at 3.2 m passed
   by eye: the joint between the shoulders, Satan's membrane lit through, Heaven's feathers,
   the Elf's glow.

## The 2nd level wings

Started 2026-10-04. The user: "lets work on second wings, which is for second class". Wings of
Spirits, Soul and Dragon, MU's 12/3-5, one each for the Muse Elf, the Soul Master and the Blade
Knight. Sources: WebZen 1.00.93 (base 0.97d unless a flag is named), OpenMU VersionSeasonSix for
the rows (Version095d has none: they come with NEW_FORSKYLAND2), MuMain for the drawing.

### How they are made

`WingChaosMix` (MixSystem.cpp:2470-2990); OpenMU's "2nd Level Wings", number 7.

- **Box:** exactly one 1st level wing, one Jewel of Chaos, one **Loch's Feather** (13/14), and any
  excellent things at +4 or more, nothing else. A 2nd wing in the box spoils it.
- **Chance:** the wing's price / 4,000,000 + the excellent things' price / 40,000, at most 100. A
  +0 wing alone is 13%, a +9 one about 33%. **Zen:** 5,000,000.
- **Success:** the hero's class's 2nd wing at +0 (ours; WebZen's is any of four): luck one in
  five; the option at 4%, 10% or 20% for its third, second and first level; one of three extras
  one in five; the option's kind one in two. The Magic Gladiator gets Darkness without a class
  change (`sim/machine.cpp`).
- **Failure:** the box emptied, the 1st wing with it (WebZen's; the user kept it).

**Loch's Feather** (OpenMU 13/14, drop level 78; MuMain Quest04.bmd, "Used to upgrade wings").
MU drops it in Icarus alone (gObjMonster.cpp:4620). Here it falls on its own roll
(`sim::kFeatherOdds`) where a world's manifest says `drops: feather` (Tarkan and Icarus since
order-plan slice D4b); before Icarus was ported it was ours in Atlans and the Lost Tower.

### The wings

| | ours | MU's | class | size | defence | level |
|---|---|---|---|---|---|---|
| Wings of Spirits | 12/13 | 12/3 | Muse Elf | 5x3 | 30 | 215 |
| Wings of Soul | 12/14 | 12/4 | Soul Master | 5x3 | 30 | 215 |
| Wings of Dragon | 12/16 | 12/5 | Blade Knight | 3x3 | 45 | 215 |
| Wings of Darkness | 12/22 | 12/6 | Magic Gladiator | 4x2 | 40 | 215 |

**Our numbers are not MU's.** This project gave 12/3-6 to the knight's orbs (Defense, Uppercut,
Falling Slash, Lunge) before the wings came (the user, 2026-10-04: 'Move the wings').
`sim::kSpiritsNumber` and its fellows hold them; read every MU line about 12/3-6 as these. Drop
level 150, durability 200, never dropped.

- **Fight:** his blows x(132 + plus)%, what reaches him x(75 - 2 a plus)%, 1 Life a blow on Soul
  and 3 on the others.
- **Defence** + 2 a plus and the +10 triangle; **level** + 5 a plus.
- **The option**, +4 a level (regeneration 1%), its kind by `PLUS_WING_OP1_TYPE`: Spirits
  regeneration or damage, Soul wizardry or regeneration, Dragon damage or regeneration, Darkness
  wizardry or damage (`sim::wingOption`).
- **The extras** (`sim::Held::wing`): max life +50 and 5 a plus, max mana the same, or a 3% chance
  a blow ignores the defence it meets.
- **The class gate:** `sim::fits` refuses a 2nd wing to a hero who is not his class's second; the
  card names the second class, red until he is one.
- **Speed:** Dragon flies at 16 (`sim::kFastFlyFactor`), the others at 15.
- **Look:** Wing04-06 from MuMain, Spirits added whole; Soul's and Dragon's `_R` shells are MU's
  Bright meshes, added at a slow pulse (`pulse_rate: "slow"`, cook.py's mode bit 3,
  `content::Material::slowPulse`), drawn at LEQUAL as MU's GL does. `separate_meshes`, or the
  weld made a shell and its base one set of faces.

Steps, all done 2026-10-04: the recipe and the feather (`Recipe::SecondWings`, `Held::wing` saved
as "wing"), the import, wear, the class gate once Sevina's class change landed (1eea5fa0), and the
drawing; the bench's Wings tab has all of them.

### The Wings of Darkness (2026-10-07)

The user: 'cook MG 2nd wings'. Wing07.bmd on NDEE.OZT, 31 bones, the 8-key flap, class
`gladiator`. Drawn as MU draws it (the user, 2026-10-07: 'wings look not finished'):
- **The violet pass:** MU draws it again RENDER_BRIGHT | RENDER_CHROME on Chrome02 at (0.8, 0.6, 1)
  (ZzzObject.cpp:6970-6976): `game::kShineDarkness` and `shine.sh`'s `shineDarknessAdded`, at **0.4**
  of MU's strength (at 0.15 invisible, at 1.0 every shard flat cyan).
- **The sparks** (:9981-10017, `WingLook::sparks`): for ten ribs a flare_blue at the rib's centre
  bone and two strips to its partner bone, thunder and spirit, both added (MU's EnableAlphaBlend
  is ONE, ONE; drawn with true alpha the JPEG laid solid blue bands).
- **The safe-zone pose:** its action 1 instead of the flap on a safe tile (ZzzCharacter.cpp:
  6973-6976), Darkness alone of the wings.
- **Its black ends faded** (ours; the user: 'work on transparency for black parts on wings'):
  ndee.png's alpha by luminance, nothing under 12, full from 70.
