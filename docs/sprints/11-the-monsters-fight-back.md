# Sprint 11: the monsters fight back

Opened 2026-09-22. Lorencia and the Dark Knight, as every sprint from here is until the game
is whole. The sim already fights the whole fight — the Lich has swung at the hero from four
tiles since sprint 5 — and the **screen shows almost none of it**. This sprint spends what the
cook already carries: `attack_skill`, the five attack actions, the meteor models, the paired
attack sounds.

## The one rule finding, first, because it decides the shape of the sprint

**A monster's attack skill changes nothing about its damage.** OpenMU's `Monster.AttackAsync`
(`src/GameLogic/NPC/Monster.cs:112-123`) calls `target.AttackByAsync(this, null, false)` — the
skill argument is **null** — and only afterwards, if `Definition.AttackSkill` is set, applies
elemental effects and tells the client to show the skill animation. In 0.75's Lorencia the one
monster with a skill is the Lich, whose `AttackSkill` is `SkillNumber.Meteorite`
(`Version075/Maps/Lorencia.cs:234`, `Skills/SkillNumber.cs:17` — Meteorite is 2, which is the
2 in `mu.db`'s `monster_kinds.attack_skill`), and Meteorite in 0.75 carries no magic effect to
apply.

So: **no new arithmetic in `src/sim`, and none is allowed.** A Lich's blow is the same
physical `strike()` a spider's is, rolled against the same six numbers. `content/tables.h`
already says `attackSkill` is "a monster's animation and projectile choice, not a skill
system", and that comment is now traced rather than asserted. A version of this sprint that
gives the Lich magic damage is wrong, however good it looks.

This is a **showing** sprint, and its whole risk is that it is worked as a rules sprint.

## What is already true, so nothing here is rebuilt

- The sim honours every breed's `attackRange` (Lich 4, Giant 2, the rest 1), `viewRange`,
  `moveRange` and `attackDelay`; `Realm::think` stops, faces and swings on the beast's own
  swing clock, which is already separate from its thinking clock.
- Every breed's `_attack`, `_die` and `_move` cry is loaded per body and played positioned
  (`Play::heard_`, 2026-09-22).
- The landing cue, the blood, the number and the health bar all hang off `Showing`, and a cue
  drops itself when the attacker's swing is no longer the swing it belonged to.
- The Budge Dragon's fire and dust are built (`game/fx/breath.cpp`) — the one breed-specific
  effect in the tree, and the worked example this sprint's third step follows for pools,
  MU-units-at-the-edge and scaling by the animal.

## What is missing, checked in the code

| | now | MU |
|---|---|---|
| which clip a swing plays | `find(3)`, falling back to `find(4)` — **Attack 1, always** (`play.cpp:294`) | `SwordCount % 3 == 0 ? MONSTER01_ATTACK1 : MONSTER01_ATTACK2`, a counter per character (`ZzzCharacter.cpp:1269-1276`) |
| the attack cry | one event a breed | `Models[o->Type].Sounds[2 + rand() % 2]` — two interchangeable, on `AnimationFrame == 0` (`:1289-1299`), and the cooked table already holds both files for every Lorencia breed but the Spider |
| the Lich's spell | nothing at all | `CreateEffect(MODEL_FIRE, to->Position, ...)` + `SOUND_METEORITE01` (`ZzzCharacter.cpp:5008-5011`) |
| model effects | **the engine cannot draw one**: the transparent pass is sprites | `index.json`'s 18 `missiles` rows, meshes with per-part sheets and blend modes |
| the Shock action (5) | never played | played on a meteor's quake to everything within 200 units (`ZzzEffect.cpp:7752-7773`) |

## The steps, in this order

**1. The swing every breed shows.** `SwordCount % 3`, kept per drawn body, so two swings in
three are Attack 2 and the fight stops being one looped clip. Breeds whose library has no
Attack 2 keep Attack 1 — the cook's `monster_actions` says which actions a breed has. The cry
plays on the clip's own first key from either of the two files, which is what the sound table
already holds; `Sound::load` picks between a event's files already, so this is a spelling
change and a check, not a mechanism.

**2. Reach, seen.** A Giant's blow crosses two tiles and a Lich's four. Nothing is wrong in the
sim and everything may be wrong on screen: the cue lands on the target with no travel, the
attacker faces along `engage`'s aim, and a blow from four tiles away currently reads as the
hero being hit by nothing. The Giant is the easy half — his axes are long and MU shows no
travel for him either. The Lich is step 4.

**3. Model effects, the engine's new capability.** The one real piece of engineering here.
- **The cook** learns `index.json`'s `missiles`: the `.obj` to a `.mum`, each part's sheet to a
  `.ktx` in its role, and the part's `blend` (`opaque` or `additive`) carried into the cooked
  row with `frames`, `scale`, `muzzle`, `lift`, `sideways` and `sideways_spread` — every one of
  which MU2's pipeline already wrote and this tree has never read.
- **The drawing**: a small mesh submitted in the transparent pass (view 5), additive parts
  after opaque ones, no shadow, no prepass, lit by the sky term alone. A pool sized once, a
  frame allocating nothing, and a refused-count in the log, like every pool here.
- **Priced against the effects account**, 0.3 ms, which currently buys sprites only.

**4. The Lich's Meteorite, MU's own chain.** Every number from `ZzzEffect.cpp` and the
`Fire01` missile row, which carries them already (`lift: 400`, `sideways: 130`,
`sideways_spread: 32`, `frames: 40`):
- born at the **target's** tile, 400 units up and 130 + `rand() % 32` units east
  (`ZzzEffect.cpp:2546-2556`), falling at 50 units a frame against MU's 25 Hz reference;
- when it passes the terrain height under it: **six `Stone01`/`Stone02`** flung, one
  `BITMAP_EXPLOTION` particle 80 units up, and the meteor gone (`:7808-7855`);
- **the quake and the shock**: everything alive within 200 units takes `MONSTER01_SHOCK`
  (`PLAYER_SHOCK` for the hero), and the camera takes MU's own `EarthQuake`
  (`(rand() % 4 - 4) * 0.1`) — the one place in this game a monster makes the frame move;
- `SOUND_METEORITE01` — cooked here as `meteorite`, `sounds/eMeteorite.wav` — at the cast, and
  `explosion` at the landing.
- **The blow lands when the meteor does.** The cue's fuse is the fall, not a key in a clip: the
  number, the blood and the bar all wait for the impact, which is what `Showing` was built to
  let a blow do. This is the first cue in the game that is not paced by an animation.

**5. The Shock action, and only where MU plays it.** Action 5 is not a flinch: nothing in MU
asks a target to show that it was hurt, and `showing.cpp:177` already says so. It is played by
the quake in step 4 and nowhere else. A sprint that adds a hit reaction to every blow is
inventing one.

## What is deliberately not here

- **The hero's own skills.** PLAN.md's Diablo 3 shape is its own sprint and nothing here may
  make it harder: a monster's cast goes through the presentation, and the per-skill cooldown
  state it will need still belongs beside the swing clock, not through it.
- **Noria, and anything not standing in Lorencia.**
- **New damage, resistances or elements.** See the finding above. The Lich's
  `FireResistance` of 1/255 is in 0.75's data and stays unread, as it is now.
- **The 17 other missile rows.** Arrows are the Elf's and the bows' sprint; this one cooks the
  rows and draws `Fire01`, `Stone01` and `Stone02`, and the rest come free or do not.

## The gate

- `cmake --build build --target checks` — the three tests and `matcheck`.
- **The seeded log is byte-identical to the run before this sprint.** This is the sharpest
  check the sprint has: every step above is presentation, so a differing byte means something
  drew a number out of the sim's dice. The attack-clip counter, the meteor's sideways scatter
  and the quake all draw from the drawing's own random.
- `--budget`, vsync off, 1080p, Release. The effects account is 0.3 ms and the frame's
  enforced wall time is 5.5.

## Proved by

**Two runs, because the breeds do not share ground.** `mu.db`'s own spawn rows put the 20
Lichs in the south-east (x 95-175, y 168-244) and the 15 Giants in the north-west (x 8-60,
y 11-80), where the Hounds' 45 overlap them (x 8-94, y 11-244). One run cannot hold both and
a sentence that claims it does is a sentence nobody ran.

> **At `--at 135,206`**: a Dark Knight takes a meteor on his head — the fire falls where he
> stands, the stones fly, the ground shakes, the Lich's voice is one of its two, and the number
> and the blood wait for the impact rather than for a key in a clip.
>
> **At `--at 35,45`**: a Giant reaches him from two tiles and a Hound from one, each swinging
> Attack 2 twice in three and Attack 1 once, each with its own pair of cries.
>
> And over both: the frame inside 5.5 ms with the effects account inside 0.3, no pool
> refusals in the log, and the seeded log byte-identical to the run before the sprint.

Each with a shot at the impact and the log read for the pool counts and the budget table.

## Measured

<!-- filled when the sprint closes: the frame, the effects account, the pools, the diff -->
