# The 1st level wings

Started 2026-10-04. The user: "lets work on first wings, we need logic how we can make first
wings". Wings of Elf, Heaven and Satan, group 12 numbers 0-2, slot 7 (`sim::kWings`).

## How they are made

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

## The items (OpenMU Version075 `Items/Wings.cs`)

| | number | size | defence | level | class | option |
|---|---|---|---|---|---|---|
| Wings of Elf | 0 | 3x2 | 10 | 180 | elf | Health Recover |
| Wings of Heaven | 1 | 5x3 | 10 | 180 | wizard | Wizardry Damage |
| Wings of Satan | 2 | 5x2 | 20 | 180 | knight | Physical Damage |

All: drop level 100, durability 200, damage x1.12 and damage taken x0.88 (each raised by the
wing's plus through OpenMU's per-level tables), luck, CanFly. Level 180 kept (the user).

## What MU draws (MuMain, `ZzzCharacter.cpp`)

- The wing is its own model (`Item/Wing01-03.bmd`) with its own flapping action, linked
  rigidly to player bone 47 at (0, 0, 15) (:15400-15430).
- Out of a safe zone a winged hero flies instead of running: `PLAYER_FLY`, or
  `PLAYER_FLY_CROSSBOW` with a crossbow (:615-621), the bow shots `PLAYER_ATTACK_FLY_BOW_UP`
  (:1053-1064). The wing flaps at PlaySpeed 1 while flying, 0.25 otherwise.
- Speed 15 against a run's 12 out of a safe zone, as on the Horn of Uniria (:6320-6335).

## Steps

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
