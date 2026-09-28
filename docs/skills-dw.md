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

- **It is a primary** (`SkillRow::primary()`): no cooldown, paced by its own clip. **It pays no mana
  back** -- no spell does. For a day every landed spell refunded a twentieth of the pool, six a hit
  on a 120-mana wizard, more than Energy Ball, Fire Ball or Power Wave cost, so casting filled him up
  (*"it gaining mana a lot"*). Now he casts until he is dry and swings his staff, whose landed blow
  pays back as the knight's swing does; regeneration is the three-second twenty-seventh as before. It is walked out of like a swing (no `castUntil` lock) until it leaves his hand.
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

**Taking the shield off ends it** (and the knight's Defense), aura and share together, in
`Realm::rearm` -- the user's, 2026-09-28. The cooldown runs on, so the shield put back on is not a
quicker recast. (For an afternoon the wizard's needed no shield -- *"remove shield requirement"* -- and
it was put back the same day: *"restore that DW needs shield for Soul Barrier otherwise it's not
balanced"*. The two guards stand level only if both ask the same thing of the off hand.)

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
  energy ball"*): no cooldown, paced by its clip, and no mana back. Against
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

## 2d. Power Wave

0.75's row, `CreateSkill(PowerWave, ..., DamageType.Wizardry, 14, 6, manaConsumption: 5,
energyRequirement: 56)`: fourteen damage, six tiles, five mana, skill 11, no element. Taught by the
Scroll of Power Wave (`Book11`, group 15 #10, Pasi's slot 1, 1 100 zen), refused under 56 energy. A
**primary like the other two**, on the shape the user gave Fire Ball: about twice Energy Ball against
one body at 56 energy for five mana. The clips are Energy Ball's, the wave is `SOUND_MAGIC`, and it
flies at the bolt's fifteen tiles a second.

**It strikes everything in its line** (the user, 2026-09-28: *"power wave is kind of aoe, because it
can go through multiple monsters"*), where 0.75 strikes the one body. `Spread::Line`: every body
ahead of him within three quarters of a tile of the line toward what he aimed at, out to the
**twelve tiles the wave visibly sweeps** (`kLineTiles`; *"increase range for that spell because it
goes far"* -- the first cut stopped at six, where MU starts fading it). He still aims it at a body
within the six tiles of its reach. `Realm::looseLine`
says one `Loosed` (one wave is drawn) and puts a flight in the air for each body, nearest first, so
each is struck when the wave reaches it. The air holds 32 flights now, up from 8. Seen in the arena against five spiders: one
wave, two numbers.

**The look** is `fx/wave`, MU2's `Wave` ported. What is MU's: `Magic02.bmd` at 0.9, one additive
curtain standing on the ground; sixty units a frame, flat, for twenty frames -- twelve tiles, so it
**sweeps through its target and on out**, never stopping (its mover calls no `CheckTargetRange`);
the sheet streaming along it (`BlendMeshTexCoordU = -LifeTime * 0.2`, the first scrolling texture
here, which the albedo sampler's wrap allows); four smoke01 puffs a frame in a 45-degree cone,
faintly blue, doubling and stopping hard; a blue light three tiles wide. Ours: it stands on the
ground under the spot every spell leaves from; and, on *"make it little bit more blurry"*, two wider,
dimmer copies of the curtain (1.12x at 35%, 1.26x at 18%) with its brightness held at 0.8 where MU's
saturates. `fx/effect_mesh` is the .obj reader and basis draw the meteor and the wave now share.
`--bolt-every N --bolt-skill 11` is its bench.

## 2e. Lightning -- the first channel

0.75's numbers where they survive: seventeen damage and skill 3 -- but each strike lands at **twice
the band** (`force` 2, *"lightning has to be stronger because it's a cooldown spell"*; a spell's
`force` was fixed at one, and is now the row's own, one on every other spell), about 60-110 at 120
energy; and the mana is **forty**, not 0.75's fifteen (*"lightning has to spend more mana"*), about
MU's Ice -- off the Scroll of Lighting
(`Book03`, group 15 #2, Pasi's slot 2), refused under 72 energy. The rest is this game's (the user,
2026-09-28): *"Lightning in our game will be first cast duration spell ... DW uses special animation
and lightning finds all monsters around him and casts lightning to them (aoe)"*, *"it also has
cooldown 10 seconds"*, and *"we need additional UI feature for channeling spells"*.

- **A channel as long as its clip, that goes round** (`SkillRow::channelTicks` 42, `pulseTicks` 3,
  `strikeFrom` 14, `strikeUntil` 32; `Realm::channel`). It lasts the Recovery clip once, 2.08 s
  (*"make it shorter, like actual animation length"*), and strikes **only while his arm is up** in
  it, 0.7 s to 1.6 s, read off the clip frame by frame on the bench (*"when hand is up only then start
  channeling"*): a strike every three ticks, seven at most.
- **Its card** says so (`Desk::skillSheet`, *"update tooltip for this spell, because it's multiple
  monsters and is channeling"*): "Each strike" for the damage, then Channel 2.1 s, Strikes up to 7,
  Area 4 tiles round him, a grey "going round, 1 at most on one body", and Pushes a tile away. Power
  Wave's card got its line the same day: Range 6, Area a line of 12 tiles, "strikes everything it
  passes through". Each strike goes to **one** body within
  **four tiles** (`Spread::Ring` at `reach` 4): the first clockwise from where the last one went
  (`Body::channelTurn`, starting where he faces), so the bolt sweeps round the ring (*"not to all
  monsters at the same time but like rotation"*); a lone body takes every strike, and a body that
  walks in joins the round. **No body is struck more than once in a cast** (`strikesEach` 1, the
  tally on `Body::channelStruck`): seven strikes into a lone monster was a one-shot (*"when there is
  a single monster the DW casts all lightning to one monster and basically one-shots him"*), and two
  was still *"overpowered on single target"* -- so it is a spell for a crowd: a lone body takes one
  strike, and up to seven round him take one each. A strike with nothing in reach is not thrown. Earlier cuts ran three
  seconds from a fifth of a second in, striking everything at once, then one at a time.
  The clip's length is logged at every cast (`channel: ... s long`); the sim's clip table does not
  carry 183, so the ticks are written on the row and the log is how to check them.
- **It pushes** what each strike leaves standing a step straight away from him, slid over six ticks
  (`Realm::push`), and not again until it has landed: no teleport, a flinch, no thinking or walking while it slides, never onto a
  blocked or sheltered tile, and a death mid-slide leaves the body on its tile. 0.75 moves a random
  neighbour at once. Nothing in Lorencia resists lightning, so the resistance roll is not made.
- **Ten seconds of cooldown**, before agility's haste as every key's is (9.5 s on a young wizard),
  floored at the channel and two seconds. Forty mana at the cast. It asks for something within
  four tiles before it goes, so it is never spent on empty air.
- **He channels in it.** MU's "Skill recovery" (183) -- one arm thrown up to the sky -- looping for
  the whole channel. Chosen by the user off bench sheets after two misses: "Skill lightning shock"
  (186) held on its last key read as frozen (*"cast animation is freezed"*), and "Skill drain life"
  (169) turned out in play to be a mount's pose (*"this casting animation is for mount"*). He
  **cannot walk out of it**, the rule of 2026-09-23 for every skill: the realm holds him
  (`castUntil`) and drops a click to move. One thunder a strike.
- **He crackles** (*"add some electric effect to the character itself"*; `Thunder::crackle`, ours):
  while it runs, every two reference frames two small thin sparks jump between random points round
  his body, three frames each, and a blue light flickers on him.
- **No cast bar.** One was built on *"we need additional UI feature for channeling spells"* -- a
  draining bar over the plate, then a glass panel, then a flat one -- and taken out on *"don't show
  channeling UI"*. The channel is read off his pose and the bolts.
- **The stance was missing** at first (*"i did not see casting animation"*): a pulse's `Hit`, with
  no `Swung` in front of it, was read by the drawing as a monster's one-part blow and restarted his
  attack clip over the held stance, every pulse, until the session's first swing latched
  `landing`. The hero's `Hit` never starts a pose now.
- **The look** is `fx/thunder`: a jagged bolt re-thrown every reference frame for a third of a
  second, two crossed quads a segment on MU's JointThunder01, a wide joint and a thin one, the sheet
  scrolling. Each throw is its own (*"more random so they all don't look the same"*): a random walk
  off the line pinned back to both ends, eight to sixteen points at uneven gaps, a wildness rolled
  per throw, the thin joint wandering on its own, and a branch forking off the side about half the
  time; MU's Thunder01 spark on the body and a blue light three tiles wide there;
  a little cool-grey smoke off the bolt's own path as it goes out. MU2's `Thunder.cs` is the full
  joint walk if this ever needs it. `SOUND_THUNDER01`.

Measured in `sim_test`: over 6 000 ticks a wizard of twelve channels 30 times, never inside the
cooldown, six pulses a channel, up to four bodies in one pulse; he does not move while it runs; 88
pushes, no tick sliding a body more than 0.4 of a tile. Filmed with `--arena "Bull Fighter"
--arena-count 4 --arena-learn 3 --level 12`: bolts to all four at once, the bar draining.

## 2f. Meteorite -- a rain on a cooldown

0.75's row for the damage, `CreateSkill(Meteorite, ..., DamageType.Wizardry, 21, 6,
manaConsumption: 12, energyRequirement: 104, elementalModifier: Earth)`: twenty-one damage and skill
2, off the Scroll of Meteorite (`Book02`, group 15 #1, Pasi's slot 3, 11 000 zen), refused under 104
energy. MuMain drops it where the body stands at the let-go, `CreateEffect(MODEL_FIRE,
to->Position, ...)` and SOUND_METEORITE01 (ZzzCharacter.cpp:5008): the Lich's own rock. The rest is
the user's, 2026-09-28:

- **A cooldown spell**, so harder and dearer, as Lightning taught: **six seconds** before agility's
  haste (5.7 s on a level-30 wizard), **three times the band** (`force` 3, about 100-170 a rock at
  104 energy), **thirty mana** where 0.75 asks twelve, nine tiles like the other two he throws at a
  body.
- **A rock on every body round the one he calls it on** (`SkillRow::splash` 4, `Realm::rain`):
  everything within four tiles of the aimed body at the let-go gets its own rock, all let go
  together, each a `Loosed` and a flight of its own; only the aimed body pays back (*"meteor did not
  landed on multiple monsters around"*, *"only one meteor was flying"*; two tiles left half of four
  Bull Fighters out, *"only 2 but there are 4 monsters"*). 0.75 drops one rock on one body.
- **A fall, not a flight** (`SkillRow::fallTicks` 7): the rock lands 0.34 s after the let-go however
  far off the body is (`Meteor::fallSeconds`), and the realm lands the blow then.
- **Lightning's pose** (*"use same casting animation as lighting"*): MU's "Skill recovery" (183),
  played once where MU casts it with 147/148, and the rocks are let go at the middle of it with his
  arm up. He cannot walk out of it. The cook now carries 183 in the tables at the exporter's 0.25
  (MU gives it no play speed), so the realm times the clip: 41 ticks at level 30.
- **He burns while he casts** (*"use some fire effect for character"*; `Meteor::burn`, ours, as
  Lightning's crackle is): small bright embers born round his body climb off him, with a warm light
  three tiles wide on his chest, for as long as the clip is on him. The first cut ran one frame (the
  state it read was the frame's own) and left a single ember at his feet; the second stood on him as
  dull red specks.
- The drawing drops `fx/meteor`'s rock on each `Loosed` at where the body is drawn, one wave for
  the volley; the landing's explosion, stones, flinch and camera jolt are the Lich's. A hero's rock
  does not rush the showing's cues: the realm lands it.

Measured in `sim_test`: a level-30 wizard hunting 6 000 ticks casts 18 times, never inside the
cooldown, lets go 30 rocks, lands 29, up to three bodies in one volley, every rock landing seven
ticks after its let-go, and never moves while he casts. Filmed with `--arena "Bull Fighter"
--arena-count 4 --arena-learn 2 --level 30`: four rocks, four numbers. `--bolt-every N
--bolt-skill 2` drops one on the bench. Frame cost not measured.

## 2h. Teleport -- a blink to the ground he points at

0.75's row, `CreateSkill(Teleport, ..., manaConsumption: 30, energyRequirement: 88)`: thirty mana,
six tiles, no damage, skill 6, off the Scroll of Teleport (`Book06`, group 15 #5, Pasi's slot 4,
5 000 zen), refused under 88 energy. MuMain's: aimed at the tile under the pointer and refused on a
wall (ClassAttack.cpp:1514); `CreateTeleportBegin` plays "Skill teleport" (152, not used here), fades the body a
tenth of its alpha a frame, throws `BITMAP_SPARK + 1` and SOUND_MAGIC; the server puts him down and
`CreateTeleportEnd` fades him back in with the spark and the sound again.

- **A key aimed at the ground** (`SkillRow::blinks`, `Realm::invokeAt`): the key throws at the tile
  under the pointer; with no ground under it nothing is asked. It is never on the right button.
- **Cast with one hand**: Energy Ball's quick throw (147), where MU plays "Skill teleport" (152) --
  the user's, *"more simple cast animation for teleport"*.
- **He is put down eight ticks after the cast** (`Realm::blink`), when MU's ten-frame fade has run,
  and may act four ticks later; the drawing fades him out and in over 0.4 s each way and snaps him
  and the camera to the tile (`What::Blinked`).
- **Ours** (the user, 2026-09-28, "a blink ... on a short cooldown, D3 style"): three seconds of
  cooldown before agility's haste; a point past six tiles is pulled back along the line to six, and
  a wall or sheltered ground falls back to the nearest open tile toward him (`Realm::blinkTo`), where
  MU refuses both; the fight he was in is dropped, as the Town Portal drops it. Nothing is cast in
  the safe zone, this included, as 0.75 refuses every skill there.
- **The sparks** (`fx/blink`): MU's column of Spark03 flashing out from his feet for ten frames at
  both ends. MU's eighteen a frame up 4.3 m at fifty units a frame was *"sparkles to crazy"*; four a
  frame up the height of a man, drifting at a fifth of the speed and dimmer, and cut to specks over
  three rounds of *"sparkles to big"* (and no doubled spark at his feet), is ours. The staff's
  streak is off for the clip; it had been drawn stretched across the jump.

Measured in `sim_test`: cast at tick 1 and put down at tick 9, three tiles, thirty mana, cooling; a
press while it cools does nothing; twenty tiles off he goes six at the most; with no ground named it
is not thrown. Filmed in the arena with `--arena-learn 6 --ui-hover 0.7:0.45 --ui-skill 60:1`: the
column, the fade, and him standing three tiles off while the Bull Fighters swing at where he was.

## 2i. Ice -- a cooldown burst that halves the walk

0.75's row, `CreateSkill(Ice, ..., DamageType.Wizardry, 10, 6, manaConsumption: 38,
energyRequirement: 120, elementalModifier: Ice)`: ten damage, thirty-eight mana, skill 7, off the
Scroll of Ice (`Book07`, group 15 #6, Pasi's slot 5, 14 000 zen), refused under 120 energy. Its
element is the point: `IsIced` for ten seconds with `MovementSpeedFactor` at OpenMU's 0.5, which
MuMain agrees with (`Speed *= 0.5f`, ZzzCharacter.cpp:6353). MuMain makes the ice where the body
stands at the let-go -- `MODEL_ICE` and five `MODEL_ICE_SMALL`, SOUND_ICE (ZzzCharacter.cpp:4956)
-- on `SetPlayerMagic`'s two hands, and draws the frozen body blue, (0.3, 0.5, 1.0)
(ZzzObject.cpp:1126).

- **A cooldown spell** (the user, 2026-09-28), so wider and harder than 0.75's, as Lightning and
  Meteorite are: **everything within four tiles of the body he aims at** takes its own ice and its
  own chill (`splash` 4 through Meteorite's `Realm::rain`, with no fall -- `flies` is so fast it
  lands on the let-go), at **twice the band**, on **five seconds** of cooldown before agility's
  haste. The mana and the ten seconds are 0.75's. Two tiles, the first cut, iced one of four Bull
  Fighters (*"only 1 of 4 monsters was iced"*).
- **The chill** (`SkillRow::chillTicks` 200, `Body::chilledUntil`, `kChillFactor`): what it leaves
  standing walks at half speed; a second chill restarts the ten seconds. Only the walk: MU slows
  nothing else. A respawn clears it.
- **The look** is `fx/ice`, MU2's `Ice.cs` ported: Ice01 stepping a key a frame through its six
  poses at 0.8, holding, and going out a twentieth a frame with a wisp of vapour; five Ice02 shards
  thrown flat, bouncing and tumbling; the body blue while the chill lasts (`kIcedLight`). MU's
  negative ground light under the block is not ported -- this renderer only adds light.
- **Frost on him while he casts** (*"character need some ice smoke effect on cast"*; `Ice::chill`,
  ours, as Meteorite's burn is): pale blue wisps born round his body, rising off him.
- The card says Each body, Range 9, Area 4 tiles round its target, "it bursts on each body in it",
  and Slows to half for 10 s.

Measured in `sim_test`: a level-30 wizard hunting 6 000 ticks casts it 14 times, never inside the
cooldown, strikes 20 times, ices up to three bodies in one cast, and no iced body ever covers more
than half its ground in a tick. Seen in the arena with `--arena-learn 7`: the block and shards on
the Bull Fighter and the Bull Fighter blue.

## 2j. Poison -- a cooldown burst that goes on hurting

0.75's row, `CreateSkill(Poison, ..., DamageType.Wizardry, 12, 6, manaConsumption: 42,
energyRequirement: 140, elementalModifier: Poison)`: twelve damage, forty-two mana, skill 1, off the
Scroll of Poison (`Book01`, group 15 #0, Pasi's slot 6, 17 000 zen), refused under 140 energy.
`IsPoisoned` for twenty seconds, a pulse every three (`PoisonMagicEffect`), shown as MU's DT_POISON
green number. MuMain lays `MODEL_POISON` and ten smoke puffs where the body stands at the let-go,
SOUND_HEART, and draws the body green, (0.3, 1, 0.5) -- (0.3, 1, 0.8) when it is iced as well.

- **A cooldown spell with an area** (the user, 2026-09-28, *"also cooldown spell with aoe"*):
  everything within **four tiles** of the body he aims at takes the blow and the poison, Ice's shape
  (`splash` 4 through `Realm::rain`), at **twice the band**, on **six seconds** of cooldown before
  agility's haste. The mana and the twenty seconds are 0.75's.
- **Each pulse is a quarter of the blow that landed** (`Body::poisonDamage`, `Realm::poisonPulse`),
  where 0.75's is 3% of the health left -- three a pulse on a Bull Fighter, which is no poison.
  Six pulses, half again the blow. A pulse **never kills**: it leaves one health, as 0.75's share of
  what is left never reaches nought. A second poison replaces the first. Ours.
- **The look** is `fx/poison`, MU2's `Poison.cs` ported: Poison01's eleven poses at 0.7, wall01 flat
  and wall02 added, forty frames; ten smoke01 puffs thrown out and up and slowing hard; a green
  miasma light two tiles wide; green fumes rising off him while he casts (`Poison::fume`, ours, as
  Ice's frost is).
- The card says Each body, Range 9, Area 4 tiles round its target, Poisons for 20 s, "a quarter of
  the blow every 3 s".
- **And it poisons him** (the user, 2026-09-28, *"migrate also antidote potion"*): a bite from a
  poisoner (`kPoisoners`, realm_tuning.h) poisons him for 0.75's twenty seconds, a pulse every three
  at `PoisonDamageMultiplier` 0.03 of the health he has left, never the last point, green on him and
  green in the number, with MU's poison cell (`buff_poison`) in the buff strip and its card. 0.75's
  poisoners are the Dungeon's Poison Bull (8) and Larva (12) and Lost Tower's Poison Shadow (39),
  none cooked yet; **Lorencia's Spider (3) poisons too, ours**, so the Antidote has work.
- **The Antidote** (group 14 #8, Amy's, single and three) clears it: `AntidoteConsumeHandlerPlugIn`
  disposes of the poison and does nothing else. With none on him it is refused and kept (ours).
  `sim_test`: a level-1 knight among the spiders is bitten at tick 50, pulses at 3% of what he has,
  is never killed by it, and one Antidote clears it with no pulse after.

Measured in `sim_test`: a level-30 wizard hunting 6 000 ticks casts it 13 times, never inside the
cooldown, poisons up to two bodies in one cast, and no pulse kills. Seen in the arena with
`--arena-learn 1`: the cloud and puffs on the Bull Fighters and them green.

## 2b'. Where a spell leaves him

**One place for every spell: the middle of his chest**, 60% of his drawn height up and 70 cm toward
the target -- just past his outstretched arms (*"little bit front of arms"*; 25 cm was inside them) (`Play::castFrom`), the user's call of 2026-09-28 (*"spells come from same position
somewhere in center of body"*). MU uses a fixed height per spell, 100 units for the bolt and 120 for
the fireball; this is one height for both, scaled to the figure. The throwing hand was tried first,
and the two cast clips put it in very different places at the let-go (1.35 m up and in front on 147,
1.90 m up overhead on 148), so the ball jumped from throw to throw.

## 2g. The keys, for spells with no cooldown

- **The box wipes and rings for them too** (*"show the spell reset animation also for spells which
  don't have cooldowns"*): a primary's wait is the cast he is in, or a swing or channel still running
  (`Body::swingsAt`), wiped over its box against its own clip, and the ring plays as he is free. On
  steady auto-attack the next cast starts the tick he is free, so the box is never seen ready for a
  frame: a wipe that was nearly done and has started over also rings (`Desk::lastCooling_`).
- **Held, it goes on** (*"if I hold W and there is no cooldown it has to continue"*): a key bound to
  a primary asks again every frame it is down (`gfx::Window::down`), and the realm throws it each time
  he is free. A key with a cooldown is a press, as before.

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
- Pasi's shelf is done: all seven of 0.75's scrolls teach, and Soul Barrier beside them.
- Teleport on the right button's quick slot does nothing: the right button throws at a body.
- A thrown `Missed` does not say which spell, so a bolt and a fireball in the air at one body at
  once can turn the wrong one aside. Rare, drawing only.
- `sim_test`'s two fist checks fail since the empty hand swings the sword's pair (0db5733a); the
  test still asks for MU's 462 ms fist.
