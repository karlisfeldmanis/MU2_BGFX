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

The Elf has no skill built yet, so her right button is her weapon until she has one.

**The wizard's left button is a staff.** A new wizard is made holding the Skull Staff (`Staff01`),
which is ours -- 0.75 makes him empty-handed -- and the user's choice of 2026-09-28 after both
bare-handed swings were filmed: MU's fist (action 38) is a flailing spin with the torso side-on to
the target, and the knight's one-handed swing with nothing in the hand ends with the arms held out
wide. With the staff it is a weapon swing, the knight's clip and pace, and the staff's 3% rise is on
his Energy Ball. He is given it short of its strength, as the knight is given his axe. An empty
hand now swings the sword's pair (39/40) with the swing's whoosh rather than MU's silent fist, for
the day he takes the staff off. A wizard saved before this change keeps his empty hands.

`--arena-left` makes the arena's hand attack with the left button, for filming the weapon.

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

## 2a. Soul Barrier -- the knight's guard in the wizard's hand

Asked for on 2026-09-28 to balance the two classes: the knight had Defense (a five-minute guard
behind his shield) and the wizard had nothing. The wizard now buys the **Scroll of Soul Barrier**
at Pasi's (slot 7, the one empty cell after the seven 0.75 scrolls), reads it, and casts it on
himself. MU's own name and number: skill 16, `AT_SKILL_SOUL_BARRIER`, scroll group 15 number 15
(`Book16`). Season 6 in OpenMU's tree, not 0.75, and brought down to the knight's level on purpose.

What is the knight's, column for column (`sim/skills.cpp`):

- **Self only, behind a shield.** `families = kShield`, the same gate as Defense; the wizard wears
  the Small Shield, the Buckler and the Skull Shield. MU casts it on a party member too; there is
  no party, and the user ruled it self-only.
- Five minutes (`boonTicks` 6000), twelve seconds of cooldown floored at its length plus two.
- Level 6 to read, no stat asked, drop level 6, 300 zen -- the Orb of Defense's own numbers.
- **Drawn as Defense is**: the knight's stance (clip 187) and his green cage for two seconds at the
  cast, and the same buff strip (its own cell, `eBuff_WizDefense`, the blue crescent). The reading
  is every class's orb burst and swoosh (`Play::learned`).

What is its own: the wave, `SOUND_SOULBARRIER` (`eSoulBarrier.wav`), and the mana, MU's 70.

**The share.** Same curve and cap as the guard, `0.60 * p / (p + 150)`, with different points:

| | shield defence | strength | agility | energy |
|---|---|---|---|---|
| Defense (`guardPoints`) | 5 | 1.1 | 0.5 | -- |
| Soul Barrier (`barrierPoints`) | 5 | -- | 0.5 | 1.1 |

Calibrated so a new character of each class behind the same shield stands within a point:

| behind | new knight {28,20,25,10} | new wizard {18,18,15,30} |
|---|---|---|
| Small Shield (1) | 14.0% | 14.3% |
| Buckler +1 (3) | 16.3% | 16.5% |

The knight's is the mirror, strength in energy's place (2026-09-28; it had been 0.4 strength, 1.0
agility and 1.2 energy, which left a knight spending on strength behind a wizard spending on energy).
Each spending on his main stat stays level: 21.4% against 21.6% at level 6, 34.4% against 34.5% at
level 23, 45.1% each late. MU's own `10 + agility/50 + energy/200` percent (SkillTooltipModel.cpp:248) is not
followed. `tests/sim_test.cpp` holds the parity, the gate and the route through Pasi.

## 2b. Fire Ball

0.75's row, `CreateSkill(FireBall, ..., DamageType.Wizardry, 8, 6, manaConsumption: 3,
energyRequirement: 40, elementalModifier: Fire)`: eight damage, six tiles, three mana, skill 4.
Taught by the Scroll of Fire Ball (`Book04`, group 15 #3, Pasi's slot 0, 300 zen), which
`Realm::useItem` now refuses under **forty energy** -- a scroll's energy is the requirement whole,
so a new wizard's thirty leaves it in his bag for two levels. 0.75 asks it again at each cast;
energy never goes down here, so that test is not written. Fire is a gate with nothing behind it.

- **A primary, as Energy Ball is** (the user, 2026-09-28: *"fireball dont have cooldowns same as
  energy ball"*): no cooldown, paced by its clip, a hit pays back a twentieth of the pool. Against
  one body it is about one and a half bolts at forty energy (12-22 against 7-14) for three mana
  against one, so it is the right button's upgrade once it is learned.
- **It flies at 12.5 tiles a second**, MU's fifty units a frame against the bolt's sixty. The speed
  is a column now (`SkillRow::flies`), read by `Realm::loose` for the landing and by the drawing.
- The clips are Energy Ball's (147/148 on a coin); the wave is `SOUND_METEORITE01`, which MU shares
  with Meteorite.

**The look** is `fx/meteor`'s `hurl`: MU's `MODEL_FIRE` at subtype 1, ported from MU2's
`Meteor.Hurl`. What is MU's: the rock alone with no flame cone (subtype 1's `BlendMeshLight = 0`),
lifted 120 units, sized 0.8 to 1.1, an ember a frame, its orange light on the ground, two stones at
the arrival. Ours, tuned on the bench with the user watching:

- **The meteor's flame, laid on its side.** MU's rock alone read as a black lump, and a big glow over
  it as *"just orange sphere"*; asked to use the meteor's own assets, the fireball now draws
  Fire01's flame cone too -- the Lich's streak -- turned by a whole basis so it trails back along
  the flight, stretched 1.8x (*"little bit longer fire trail"*), with the meteor's 0.4-0.7 flicker
  at 1.35x. Two wider, dimmer copies of the cone (1.25x at 35%, 1.55x at 18%) blur its mesh edge
  (*"little bit more blurry"*). The rock turns as it flies. The cone's front is set a little past
  the rock's leading face (0.32 m ahead of its centre, tuned by eye): as modelled it put a bright rim ahead of the ball, pushed all the way
  back it left a bare stone in front of a flat end. **The tail grows with the flight**: at full length it is three
  metres, and a ball just out of his hand drew it back through him and out behind (*"looks like
  fireball trail is behind character"*), so the cone is squeezed to the ground covered.
- **A haze round it**: a dim 1.9 m orange halo and a 0.55 m heart on the `light` sheet, drawn half a
  metre toward the eye -- at the rock's own centre, the stone sorted over the flare and hid it.
- **Embers that cool.** Half the meteor's size, born orange and fading to MU's red and out (on the
  square root of their life, so they hold heat and the sparks run further back), so the stream
  breaks up instead of standing as one red tube.
- **No smoke trail.** MU2 laid soot here; on the bench it went through a brown band (smoke02 under
  `Dust`), a black one (smoke01 has no alpha, so `Dust` drew every puff opaque), too long, and grey
  under `Smoke` -- and was then taken out: *"there is already smoke in fireball"*.
- **Aimed at the body's middle and steered after it**, as the bolt is; a miss flies on past.
- **A half-size Explotion01 burst on the body**, with the two stones; its light is the blast's own.
- **The light**: four tiles of red-orange that travel with the ball -- wider than the meteor's two,
  on *"most of DW spells are light emitters"*.

`--bolt-every N --bolt-skill 4` throws it on the bench.

## 2b'. Where a spell leaves him

Both spells leave **the hand that threw them**, ours (the user, 2026-09-28, *"find the perfect spot
where energy ball and fireball is coming from"*). MU lets them go from the middle of the body at a
fixed height -- 100 units for the bolt, 120 for the fireball -- which on this camera is a ball out of
his chest. `Play::castFrom` reads `Bip01 R Hand` and `Bip01 L Hand` on the pose drawn last, takes
whichever is thrust further toward the target, and carries it 12 cm past the wrist along the
forearm to the palm. Measured in the arena at the let-go: clip 147 throws from the palm 1.35 m up
and 0.63 m in front of him; clip 148 is the overhead throw, 1.90 m up and 0.17 m out, and the ball
steers down from there. A rig without hands falls back to MU's heights. The bench uses the same
call, though its figure stands idle and does not play the clip.

## 2c. Cooldowns outlive a restart

The user, 2026-09-28: every key's wait is saved, as `"cooling": [[skill, ticks left], ...]` by
MU's number, and put back on load no longer than the skill's own cooldown at his agility, so an
edited file cannot lock a key. The time away does not count, as it does not for the boon. Energy
Ball's own trail was halved the same day (*"energy ball trail was to long"*: 6 + 3 frames a puff).

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
- Fire Ball, `tests/sim_test.cpp`: refused at thirty energy and read at forty; a level-30 wizard
  hunting 3 000 ticks on it alone throws 91, lands 89, swings the staff 0 times. Defense's wait
  comes back through the save to the tick, and a forged one is capped. Headless seed 7 after the
  experience rate went to ten: invariants all kept, `ba9a6d2620bdf6f8`.
- Fire Ball in the window, `--play --class 0 --at 190,110 --bolt-every 120 --bolt-skill 4
  --fixed-dt 16.667`: ball, embers, burst and stones seen in shots; frame cost not measured.

## 4. Owed

- The chip in the list reads `RMB` for the slot; unseen in a shot.
- The next spells, in scroll-drop order: Power Wave 9, Lightning 13. Each is a row, a cooldown or
  not (Fire Ball was ruled a primary), and an effect -- Power Wave is MU2's `Wave`, Lightning its
  `Thunder`. Each should throw some light.
- A thrown `Missed` does not say which spell, so a bolt and a fireball in the air at one body at
  once can turn the wrong one aside. Rare, drawing only.
- `sim_test`'s two fist checks fail since the empty hand swings the sword's pair (0db5733a); the
  test still asks for MU's 462 ms fist.
