# Sprint 6: the showing — data census

Researched 2026-09-20, before the sprint file and before any code, from MuMain's own C++
under `LEGACY/reference/MuMain/src/source`, MU2's `client/core` read one file at a time, and
`MU2/build/index.json` as it stands. Sprint 5 is planned and a fifth built; this census
assumes it lands first and names exactly what it owes.

The questions a census has to answer here are not "what does an effect look like" — they are
**what does this engine not have yet**, because the answer turns out to be most of what the
showing needs.

## 0. The finding that shapes the sprint: nothing here has ever drawn a transparent pixel

`grep BGFX_STATE_BLEND src/` returns nothing. Six draw states exist in `renderer.cpp` and
every one of them is opaque: depth-write `LESS` in the shadow and the prepass, depth-test
`EQUAL` with no depth write in the shade, and two full-screen blits. Cutout is a `discard`
in the fragment shader, which is not blending and was chosen so it would not have to be.

So the showing is not "add some effects to the frame". It is **the first transparent pass
this engine has**, and four things follow that are structural rather than content:

- **A seventh view, and the view ids are fixed by contract.** `src/gfx/views.h` says so in
  its first sentence and `docs/conventions.md`'s frame table repeats it: `ViewHud = 6`
  already exists and charges to `present`. Effects must draw **after** the shade and
  **before** the tonemap, so they belong at id 5 and everything below shifts by one. That is
  a deliberate, reviewed edit of the contract, in both files and the budget, not a quiet
  insertion.
- **It has no account.** `docs/budget.md` has shadow, prepass, ssao, shade, present and
  0.2 ms of spare. There is no line for effects, and the plan forbids borrowing from spare.
  Sprint 6 either opens an account by taking milliseconds from a measured underspend, or it
  states its cost and fails. This is the sprint's single biggest exposure and it is a
  judgement call for the senior, not a detail.
- **The MSAA target decides where the pass goes.** `renderer.h:41` says msaa applies to the
  prepass, the depth and the shade target. Effects drawn into the multisampled shade target
  before the resolve get antialiased edges and cost bandwidth at sample rate; drawn after
  the resolve they are cheaper and aliased. MU's effects are soft-edged sprites, so the
  second is probably right and it is **measured, not assumed**.
- **Depth test yes, depth write no**, and sorted back to front per frame. That is the one
  piece of per-frame sorting this engine has deliberately avoided so far — `PLAN.md` point 7
  says chunks are sorted once at load. A transparent pass cannot be. The sort is over the
  live effect instances only, which is tens of items, not thousands.

## 1. What content already exists, and none of it is cooked

`MU2/build` carries the art whole and `index.json` already describes it:

| key | count | what it is |
|---|---|---|
| `effects` | 151 | a name to one `.png` sheet — `effects/hit/blood.png`, `effects/movetarget/*`, 25 folders, 2.4 MB total |
| `missiles` | 18 | a name to an `.obj` plus parts, each part with a `sheet` and a **`blend`** of `opaque` or `additive`, a `scale` and a `frames` count |
| `sounds` | 104 | an event name to a list of `.wav` files — `melee_hit` is four files, `agon_attack` is two |
| `sound_onsets` | 103 | seconds of silence at the head of each event's file |
| `sound_gains` | 7 | per-event decibel trims |

Three things to take from that table:

- **`blend` is already authored per part.** MU2's pipeline resolved additive against opaque
  at build time, so the cook does not have to re-derive it from the art. The engine's closed
  material model has no `blend` field — it has cutout, two-sided and skinned. Effects want a
  fourth state, and the honest reading is that an effect material is **not** the world
  material model: it is a second, much smaller one (sheet, blend mode, tint) that the
  transparent pass owns. Adding `additive` as a fourth flag to the world material would make
  every wall pay for it.
- **`sound_onsets` exists because MU's wav files have silent leads**, and a cue that fires
  on the file rather than on the sound lands late by up to a quarter of a second
  (`agon_attack` is 0.167 s, `beetlemonster_attack` 0.260). This number is already measured
  and must be carried, not rediscovered.
- **Nothing in `tools/cook.py` touches any of it.** Its passes are meshes, ground, clips,
  placements, figures and — half-built, sprint 5's — tables. Effects and sounds are a new
  pass, and the 13 MB of wav is the first content this project would ship uncompressed.

## 2. miniaudio is promised and not pinned

`PLAN.md` point 12 says `bootstrap.sh` pins "bgfx/bx/bimg, glfw, cgltf, stb and miniaudio by
commit". `bootstrap.sh` fetches bgfx, cgltf and stb_image; `extern/` holds three things.
**There is no audio in this project at all** — no dependency, no device, no voice, no
listener. Sound is not a feature of sprint 6, it is a subsystem sprint 6 introduces, and it
is the part most likely to be underestimated because MU2 already solved it.

## 3. The landing cue, which is the sprint's spine

MU fires everything about a blow at once and MU2 deliberately does not. Both halves are
traced.

**What MU does.** `WSclient.cpp`'s `ReceiveAttackDamage` handles the damage packet, and in
that one handler the client starts the swing, plays the hit sound, and puts up the number —
all on the swing's **first key**, because `AnimationFrame == 0` is true in the same breath.
The blood is thrown from somewhere else entirely: `ZzzCharacter.cpp`'s `MoveCharacter`, on
`if (tc->Hit >= 1)`, by the **attacker**, at the target. Nothing in MU asks the thing being
hit to show that it was.

**What MU2 changed, and why it is worth copying.** `docs/combat.md` §"When each of them is
shown":

| what | MU | MU2 |
|---|---|---|
| swing sound | swing's first key | end of the blend into the swing |
| hit sound + damage number | swing's first key | halfway through the swing |

Faithful reads badly: on the first key the body is still blended most of the way into
whatever it was doing, so the sound arrives before the arm does and the number appears while
he is still standing. Halfway is **chosen, not transcribed** — MU has no impact frame for an
ordinary melee attack, and the nearest thing the client commits to is its melee *skill*
effects being gated on `AnimationFrame >= 3` of a seven-key attack.

Three rules come with it, and all three are cheap to keep and expensive to retrofit:

- **The arithmetic does not move.** The roll, the damage and the death resolve on the tick,
  as sprint 5 built them. Only when they are *drawn* moves. Nothing downstream may read a
  fact off the cue.
- **A cue is gated on the clip still being the swing.** If the body has moved on before the
  blend finishes, the cue finds a different clip playing and drops itself. A dropped cue
  costs a sound and never a fact.
- **Struck, Hurt and Slain share one cue** (`Crowd.cs:535`). MU2 learned this the hard way:
  the three used to show their parts the instant the realm called them, which was coherent
  while all three were instant and broke the moment the number moved to the impact frame —
  the monster fell, and four tenths of a second later the number that killed it appeared
  over the corpse. **Whatever a blow does, it does in a single frame.**

And one clock rule behind all of them: everything the showing times — a cue, a fuse, a
corpse's fall — runs on **the drawing's own clock**, advanced by the scaled frame delta, not
on the wall clock. MU2 found that at haste every timed thing fell behind the simulation.

## 4. Blood

Ported in MU2 as `Wounds.cs` and traced to `ZzzCharacter.cpp`:

- `if (tc->Hit >= 1)` throws **ten `BITMAP_BLOOD + 1` particles** at the target, for every
  landed blow of every kind. The only exemption is `MODEL_GHOST`.
- Immediately below, `CreateSpark` — but **only** when the swinger's action is one of
  `PLAYER_ATTACK_SKILL_SWORD1..5` (actions 60–64). So the sparks are keyed to the *skill*
  swing, not the weapon.
- **MU does not vary this by weapon.** An axe and a bow put the same blood on the same
  spider. Written down because it is the opposite of what it looks like it ought to be, and
  because improving it into a per-weapon table is exactly the guess this project does not
  make.
- The **death spatter is a different thing**: `CreateBlood` lays two `BITMAP_BLOOD` decals at
  the head bone when something falls. A decal, not a particle, and part of dying.

**The one departure MU2 made and this sprint should make too.** Every distance in the
client's blood is absolute and was chosen against a player: a band 90 to 153 units above the
target's feet, splashes half a metre across. Ported exactly onto a spider — 80 units tall
against a player's 120, both hand-written in `CreateCharacter` — that puts a grey cloud
larger than the animal above it, and a dark mass over a monster reads as smoke, not as a
wound. MU knows each model's height and does not use it. Every length is taken in units of
the target instead.

## 5. The damage number, and it needs no text system

The most useful finding for scheduling. `PLAN.md` gives the text atlas and the HUD to sprint
7, which looks like a dependency and is not: **MU draws its damage numbers as sprites**, from
`Data/Interface/FontTest` — a 256×32 sheet of ten 16-pixel digits with the word `Miss` under
them. A number is a row of textured quads, which is the transparent pass this sprint is
already building.

MU2's `Points.cs` is the port, from `ZzzEffectPoint.cpp`'s `CreatePoint`, `MovePoints`,
`RenderPoints` and `RenderNumberPoints`, with the colour table from `ReceiveAttackDamage`:

- appears **140 units above the thing that was hit**, and rises;
- rises at **10 units a reference frame, slowing by 0.3 a frame**, and dies when that reaches
  zero — a third of a second over one and a third seconds, about 1.67 m of travel;
- alpha is that same falling number **× 0.4**, so it holds full opacity and fades over the
  last third of a second rather than fading the whole way;
- a plain hit draws at **scale 15**; a critical, excellent or perfect at **50**, shrinking
  back to 15 at 5 a frame;
- each digit is a square of the scale, the next placed `Scale / 0.7071 / 2` along the
  camera's horizontal — so **digits overlap by nearly a third**;
- the string is nudged back by `Length × 5` on world X and `Length × Scale × 0.125` on both
  world axes, which is MU's centring and **is not actually centred**;
- **a miss is not a zero.** The client passes −1 and draws a separate `Miss` sprite at a
  fixed 45×20 with no centring and no scaling, so a miss is *larger* than a plain hit and
  never grows or shrinks.
- **The height is flat**: 140 units above the origin whatever the thing is, so a spider and
  a giant put their numbers in the same place. That is not an oversight — the number sits at
  a readable height above the *tile*.

Every rate above is per frame of **MU's 25 Hz reference clock**, and `conventions.md` already
carries the trap: a speed takes one factor of the tick rate, an acceleration two.

## 6. The fall

- **MU does not fade or sink a corpse.** `Crowd.cs:191`: the death animation is clamped on
  its last key, the body lies where it fell, and it disappears only because the server takes
  it out of the viewport. MU2's corpse fade is MU2's own and is marked as such.
- **A thing killed by somebody else stays on its feet for fifteen reference frames** — 0.6 s
  — while `ZzzCharacter.cpp` counts `c->Dead` up by `FPS_ANIMATION_FACTOR` and calls
  `SetPlayerDie` at fifteen. A kill you did yourself does not wait.
- **The player's own fall is 0.53 s**: seven keys at play speed 0.45. `ZzzOpenData` sets
  `PLAYER_DIE1` and `DIE2` in their own two-line loop; the 0.4 three lines below starts at
  `PLAYER_SIT1` (234, one past the second death) and does not reach them. Read as 0.4, the
  fall takes a ninth longer than MU's. The client has two deaths and only ever uses the
  first.
- **Both death clips are marked `Loop = true`, which means they hold on the last key** — the
  field is named for the opposite of what it does.
- **There is no flinch from a melee blow**, and the client looks like it says otherwise.
  `ReceiveAttackDamage` calls `SetPlayerShock` inside `if (success)`, but `success` is
  `TargetId >> 15` — the top bit of the target id, which OpenMU only ever raises on *scope*
  packets to mean "just spawned". An ordinary hit falls to the `else`, whose hero branch has
  no shock in it. Your character keeps swinging while he is being bitten.

## 7. The health bar

**MU's is a banner, not a bar over a head.** `CNewUINameWindow::RenderName`'s monster branch
is `RenderText(320, 2, c->ID, ...)` with `DrawHealthBar(320, 15, ...)` under it — a name and
twenty segments pinned to the top edge of the screen, which is where a 2003 client put a
thing it did not want to solve the projection for.

MU2 built that first, it worked, and MU2 replaced it with a floating plate over the monster
(`Vitals.cs`) on the argument that a top-edge banner cannot say *which* of the six around
you it is describing. That is **an invention, and a good one**, and it is the one place in
this sprint where copying MU faithfully is probably the wrong call. Two of its own details
were tried and cut for being arguments that did not survive being looked at: segmenting the
track (it read as a broken bar at that size) and the level in brackets.

Note it is a **screen-space** element over a world position, which makes it the HUD's
problem, not the transparent pass's — and the HUD is sprint 7. This is the one piece of the
showing that genuinely straddles the boundary, and the sprint file has to decide which side
it falls on.

## 8. Sound

- `MuMain/src/source/Audio/DSPlaySound.cpp` is the whole of MU's sound engine, 780 lines,
  and `Update3DPositions` at line 458 is the whole of its spatialisation.
- **The soundfield is turned by the camera's yaw and nothing else.** The emitter's place in
  the listener's space is the vector from the hero to the thing, rotated by `g_Camera.Angle[2]`;
  `Angle[0]` and `Angle[1]` are dropped, so the pitch of the lens never tilts the mix. The
  axis that decides left from right is **the screen's**. A thing at the right of the screen
  is on your right.
- **The listener is the character, not the camera.**
- **Two voices per event, and a new play steals the oldest.**
- **An emitter follows what made it**: `PlayBuffer` stores the `OBJECT*` and the position is
  re-read every frame for as long as the voice lives. A Bull Fighter that screams and then
  charges drags the scream with it.
- **The mix is flat** — `SetPosition(-x, 0, -y)`, the Y thrown away.

Which sounds a blow makes (`docs/combat.md` §"Which sound") is a **second ladder that
deliberately does not agree with the clip ladder**: a Berdysh and a Kris play different
actions and the same sound; a Light Spear and a Berdysh play the same action and different
sounds. Derive one from the other and it is wrong for both. A bow in the left hand is
`player_bow`, a crossbow in the right `player_crossbow`, a Light Saber (0,10) or Light Spear
(3,0) `player_swing_long`, anything else `player_swing` (two files at random), **nothing in
either hand is no sound at all**. All fire on the swing's first key, so **a swing that misses
is still audible**. The hit is a separate event, `melee_hit`, four files at random, and it
belongs to the attacker.

## 9. Pools

MU2's `Pool.cs` records the failure whole, and it is worth copying the conclusion rather than
the mechanism, since bgfx has no scene graph to allocate into:

> Four effects had written this out and all four had the same hole in it. The blood, the
> damage numbers, the weapon streak and the level-up aura each kept a list, scanned it for
> something not in use, and built a new one when the scan came up empty. Because the pools
> all started empty, that cost landed on the first frame each effect was ever needed: the
> first blow struck, the first number shown, the first swing, the first level — the four most
> conspicuous moments in a fight. It read as the effect hitching, and it was the effect being
> built.

The engine-specific half of that cost was a pipeline compiled for a material the renderer had
not drawn before. **That hazard is identical here**: bgfx compiles a program's pipeline state
on first use on Metal, and `docs/budget.md` already drops the first 30 frames of every run
for exactly this reason. So the pool is one thing, it is filled at load, and the transparent
pass's programs are drawn once behind the loading screen before the first fight.

The proving sentence is "no allocation per frame in the pools", which means the pass owns a
fixed instance array sized at load and a free list, and the sprint should be able to state
the ceiling per kind.

## 10. What sprint 5 owes this sprint

Sprint 5's event list is `spawned, stepped, hit, died, dropped, gained, levelled`. The
showing needs, per event, things the list does not yet promise:

- `hit`: who swung, who was hit, **how much**, and whether it was a miss, a critical or an
  ordinary hit — because the number's colour and its scale 15-versus-50 depend on it, and a
  miss is a different sprite.
- `died`: who, and **who killed it**, because a kill you did yourself does not wait the
  fifteen frames and one killed by somebody else does.
- `stepped` and the swing: the showing needs to know **when a swing began and which clip**,
  since the cue is halfway through *that clip* and is dropped if the clip changed.

If sprint 5 lands without the third of those, sprint 6 starts by adding it to the sim's
event list, which is a change to a byte-identical seeded log and therefore not free.

## 11. Traps, collected

1. **The view ids are a contract in three files** — `views.h`, `conventions.md`'s frame
   table, `budget.md`'s account table. Inserting a view touches all three or the accounts
   silently mis-attribute.
2. **Blend mode is not a material flag.** Effects get their own small material, or the world
   material grows a field every wall pays for.
3. **Per-frame rates are per MU's 25 Hz**, and an acceleration takes two factors of it.
   `Points`' 0.3-a-frame slowdown is an acceleration.
4. **`sound_onsets` or the cue lands late.** Up to 0.26 s on the rows measured.
5. **A sorted transparent pass is the first per-frame sort in this engine.** Keep it over
   tens of instances, never over the world.
6. **The first-use pipeline compile lands on the first blow** unless the programs are drawn
   behind the loading screen.
7. **`success` in `ReceiveAttackDamage` does not mean the blow landed.** Anything read out of
   that branch is a spawn bit.
8. **A cue may never be the source of a fact.** If it can be dropped, nothing downstream can
   depend on it.

## 12. Unknown, and named rather than guessed

- **What the transparent pass costs**, on which the whole account question turns. Not
  knowable before it is built; the sprint's first measurement.
- **Whether effects go before or after the MSAA resolve.** Measured both ways.
- **Whether the health plate belongs to this sprint or to sprint 7's HUD.** A decision, not a
  measurement.
- **What the 13 MB of wav becomes in the cook.** Copied whole, trimmed by their onsets, or
  compressed. No measurement of load time exists yet.
- **How many effect instances a real fight peaks at.** Sprint 5's seeded hunt can answer this
  before a single effect is drawn, and should.
