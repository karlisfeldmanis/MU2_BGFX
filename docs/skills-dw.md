# The Dark Wizard's spells, and the right button every class shares

Written 2026-09-28, the day the first one was built. `docs/skills-dk.md` is the knight's argument
and most of it holds here; this is what the wizard changed, and the one rule that changed for all
three classes with him. `MU2/docs/spells.md` is the reading of 0.75 and MuMain behind it, done for
the Godot client and used here as the second source.

## 1. The right button is a quick slot

The user's rules, 2026-09-28, in their order:

- *"right click uses energy ball and when its oom it goes to melee range and attack with weapon"*
- *"we need option to change right click spell (energy ball at start) with other spells"*
- *"same logic is also for DK and Elf"*, *"basically right click is quick slot but for right click"*
- *"if DW attacks with left mouse click he uses melee attack with weapon"*

So the plate's gold box -- MU's own "skill in hand", the box the list already opens from -- holds a
skill, as Q W E R T do, and is filled the same way: dragged out of the list (`Hud::kRightSlot`,
`Desk::bound_[5]`). It is saved as the sixth entry of `Saved::bar`; a file from before has five and
reads its sixth as empty.

- **Right-click a monster**: an Attack order carrying the slot's skill (`Request::skill`).
  `Realm::press` throws it whenever it can be thrown and **swings the weapon whenever it cannot** --
  no mana, the wrong hand, not learned. A thrown spell that is only cooling waits at its own range;
  a knight's skill that is cooling is swung through, which is the auto-attack floor §3.1a of the
  knight's doc already gave him.
- **Right-click the ground**: stop, as before.
- **Left-click a monster**: the weapon, for every class. A wizard hits with his staff.
- **An empty slot**: the right button's attack is the left button's.
- **A primary goes to the slot by itself** the first time it is learned; everything else takes the
  first free key, and the slot is the player's to fill.
- **Held**, it goes on killing (*"hold right click and continue to kill monsters"*): while the
  button is down and the monster he is on dies, the next is the one under the pointer, and failing
  that the nearest within the slot's reach -- six tiles for Energy Ball, two for the weapon. It
  never changes target while the one he is on is alive, because a new order drops the blow in the
  air (`Play::holdRight`).

The Elf has no skill built yet, so her right button is her weapon until she has one.

## 2. Energy Ball

0.75's row, `CreateSkill(EnergyBall, ..., DamageType.Wizardry, 3, 6, manaConsumption: 1)`: three
damage, six tiles, one mana. **Born knowing it** (`AddEnergyBallForDarkWizard`), granted in
`Realm::raise`; `restore` ORs the save's mask over it, so a wizard made before today has it too.

| | what | from |
|---|---|---|
| damage | `(energy/9 + 3)` to `(energy/4 + 3 + 3/2)`, times `1 + staffRise/100`, truncated | `GetSkillDmg`, `ClassDarkWizard.cs:72-81` |
| roll | the swing's hit roll, then **defence before** the excellent/critical multipliers | `AttackableExtensions.cs:155-182` |
| staff rise | `magicPower/2` plus a per-level table by the power's parity | `Weapons.cs:29-30, :315` |
| multiplier | 1: a wizard's `SkillMultiplier` is flat | `ClassDarkWizard.cs:112` |
| clip | 147 or 148 on a coin, at `0.29 + agility/10 x 0.004` | `SetPlayerMagic`, ZzzCharacter.cpp:939 |
| flight | 15 tiles a second, ending a tile short of the body | BITMAP_ENERGY, `CheckTargetRange` |
| sound | `sMagic`, on the let-go and not the wind-up | ZzzCharacter.cpp:5142 |

**Ours, and marked:**

- **It is a primary** (`SkillRow::primary()`): no cooldown, paced by its own clip; a hit pays back a
  twentieth of the pool as a knight's swing does, so it is the generator and the later spells the
  spenders. It is walked out of like a swing (no `castUntil` lock) until it leaves his hand.
- **The staff's own option and its excellent 4 and 5** are wizardry damage and are not reckoned yet.

**The blow is let go, then lands.** `Realm::land` at half the clip no longer strikes a thrown skill:
it says `Loosed` and puts it in `Realm::flights_` for the ticks the gap takes, and `Realm::arrive`
lands it at the top of a later tick, flagged `thrown`. A bolt whose target died in the air lands on
nothing; a new order does not take back a bolt already let go; a death clears them. The drawing
throws `fx/bolt` on `Loosed` and shows the landing cue on the thrown `Hit`, past the gate that
drops a cue whose swing has moved on -- by then the next cast may have begun.

**The look**, tuned on the bolt bench on the user's asking (*"a nice trail, a perfect impact"*,
then *"stupid sparkles, we need elegant trail smoke"* and *"and puff on impact"*); `fx/bolt.h`
carries the argument at its head. MU's is kept where it is MU's: the speed, the 1.28 m halo, the
turning Thunder01 star, the ground light. Ours, and marked:

- **One ball.** The halo is drawn once at the head; MU lays one with every wake stamp, and a stamp
  a metre back with its own halo is a second ball. The wake is smaller and dimmer, and fades.
- **A smoke tail** on MU's smoke01, tinted the bolt's blue, added as MU adds that sheet: puffs born
  small behind the ball, opening, wandering off the line and going out, so the wisp tapers and
  breaks up. Tried and thrown out on the way: MU2's flare_blue haze (a solid beam), a ribbon on
  joint_energy (too thin to see), shiny sparkles (the user's "stupid sparkles"), smoke02 (painted
  orange -- a brown band), a magic_ground shock ring (a daisy).
- **The impact is on the body**: a white-violet starburst, a soft bloom, a puff of the same smoke
  thrown out all round and slowed hard, and a short blue flash on the ground. The bolt ends there.
- **It meets the body.** Aimed at the middle of what it was thrown at and steered gently after it,
  where MU flies level at the caster's chest and aims once; on this camera that read as a bolt
  passing under a Budge Dragon.
- **A miss flies past.** The realm's `Missed` reaches the drawing a tile before the bolt reaches the
  body, and that bolt goes on by and out, with no impact.
- **Magic damage is lavender** (`Mark::Magic`, *"different color for magic damage"*), the bolt's own
  violet and far from the shield's blue beside it.

`--bolt-every N [--bolt-tiles T]` is the bench: a bolt every N frames from where he stands at a point
T tiles to screen-right, drawing only, with a one-tile step that way beforehand so he faces it.

**Two bugs found on the way.** `spell_magic` is also the Town Portal's unplaced wave, and
`Sound::load` handed the spell that unplaced handle, which `playAt` refuses: the spell was silent.
Events are now found by name and kind together. And a spell's clip was left unprotected from the
walk (`casting` was withheld to keep the staff from streaking), so a wizard who stopped and cast on
one tick could lose the clip to the drawn body still sliding in; the streak now asks the skill.

## 3. Measured

- `tests/sim_test.cpp`: the band on paper (30 energy rolls 6 to 10, a 23-rise staff lifts the top to
  13); a level-30 wizard hunting 4 000 ticks throws 86 bolts over 8 fights, opens 6 of them from
  range, lands 82; dried out through the save's door, the same order swings the staff and throws
  nothing.
- Headless, `--class 0 --seed 7 --ticks 4000 --level 30 --at 190,110`: 152 bolts let go, invariants
  all kept, fingerprint `669ac2e7ada8f3ae` twice. The knight's run beside it is unchanged by
  construction (a knight's order carries no skill) and was `350ff029cbdc0f10` twice.
- In the window, `--class 0 --arena Spider --arena-count 3 --fixed-dt 16.667`: the bolt reads blue
  and crosses to the spider, the gold box carries its icon. An arena wizard spends into energy.

## 4. Owed

- The chip in the list reads `RMB` for the slot; unseen in a shot.
- The next spells, in scroll-drop order: Fire Ball 5, Power Wave 9, Lightning 13. Each is a row, a
  cooldown (they are keys, not primaries), and an effect -- Fire Ball is `fx/meteor` at subtype 1.
- Scrolls to learn them by, which the orb route already has the shape for.
