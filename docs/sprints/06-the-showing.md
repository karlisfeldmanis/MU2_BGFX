# Sprint 6: the showing

**Proved by:** a fight that reads as MU2's does; no allocation per frame in the pools.

**Planned** 2026-09-20, before any code, from `06-the-showing.census.md` — MuMain's own C++,
MU2's `client/core` read one file at a time, and `MU2/build/index.json` as it stands. Sprint
5 is planned and about a fifth built; **this sprint assumes it lands first**, and §"What this
sprint is owed" names exactly what it needs out of it.

## The gate, stated before the work

Three numbers and one judgement, and the judgement is the user's.

1. **The frame holds.** Mean wall frame time under 5.5 ms over Lorencia's town with a fight
   in it, vsync off, Release, 1080p — the one enforced number in `docs/budget.md`.
2. **The transparent pass has an account and stays inside it.** It has none today. Opening
   one is the first decision of the sprint and it is taken against a measurement, not before
   it. See "The account this sprint does not have".
3. **No allocation per frame in the pools**, proved rather than asserted: the pass owns a
   fixed instance array sized at load, a free list, and a logged high-water mark per kind.
   The proving sentence says "no allocation", so the sprint reports the count and not an
   opinion.
4. **The fight reads.** Judged by shot and by the user, on `--bench effect` for each piece
   alone and in the town for the whole.

## The account this sprint does not have

`docs/budget.md` divides 5.5 ms into shadow 1.0, prepass 0.7, ssao 0.5, shade 2.3, present
0.5 and 0.2 deliberately unspent. **There is no effects line**, and the plan forbids
borrowing from spare.

This is the sprint's largest exposure and it is handled in the open:

- The pass is built, then measured on a real fight, before any account is written down.
- If it fits inside an account that is measurably underspent, the milliseconds are moved
  there **explicitly**, in `budget.md`, with the measurement that justified it beside it.
- If it does not fit, the sprint says so and stops, as the working rules require. It does not
  quietly take the spare and it does not publish a number taken on an empty map.

The honest guess before measuring: a few hundred sprites, depth-tested, no depth write, one
program, blending at 1080p. The cost is fill rate and it scales with how much of the screen
the effects cover, not with how many there are — which means **the worst case is one large
effect filling the view, not a busy fight**, and that is what gets measured.

## What the data turned out to be

The census has it whole; the four findings that change what gets built:

- **Nothing in this engine has ever drawn a transparent pixel.** No `BGFX_STATE_BLEND`
  anywhere; cutout is a `discard`, chosen so blending would not be needed. So this sprint's
  first half is a renderer sprint, not a content one: a seventh view, a sort, a blend state,
  an effect material that is **not** the world's closed material model, and the view-id
  contract edited in `views.h`, `conventions.md` and `budget.md` together.
- **The damage number needs no text system.** MU draws it from a 256×32 digit sheet, so it is
  a row of textured quads in the pass this sprint is already building. Sprint 7's text atlas
  is not a dependency, and `Points.cs`' transcription of `ZzzEffectPoint.cpp` gives every
  constant: 140 units up, 10 a frame slowing by 0.3, alpha × 0.4, scale 15 or 50, digits
  overlapping by a third, and a miss that is a separate larger sprite and not a zero.
- **The art and the sound are already described and neither is cooked.** `index.json` carries
  151 effect sheets, 18 missiles with their `blend` already resolved per part, 104 sound
  events, and — the one nobody would think to re-derive — `sound_onsets`, the silent lead of
  each wav, up to 0.26 s. `cook.py` touches none of it and `bootstrap.sh` does not pin
  miniaudio, though `PLAN.md` point 12 says it does.
- **One cue, or the blow comes apart.** MU fires swing, sound and number together on the
  swing's first key; MU2 moved the sound to the end of the blend and the number to halfway
  through the swing, and had to make Struck, Hurt and Slain share a single cue when the
  monster started falling four tenths of a second before the number that killed it appeared
  over the corpse. Whatever a blow does, it does in one frame.

## What this sprint builds, in this order

The order is renderer first, because everything below it draws through the thing the first
two steps make, and sound last, because it is a subsystem and not a feature.

1. **The transparent view.** Id 5, after shade and before the tonemap; `present` and `hud`
   shift to 6 and 7. `views.h`, `docs/conventions.md`'s frame table and `docs/budget.md`'s
   account table are edited in the same commit or the accounts mis-attribute. Depth-test
   against the prepass's depth, **no depth write**, sorted back to front over the live
   instances only. Before *or* after the MSAA resolve is measured both ways and the cheaper
   one that still reads is kept.
2. **The effect material, which is its own small model.** Sheet, blend (`opaque`, `alpha`,
   `additive`), tint, and nothing else. The world's material model — albedo, normal, ORM,
   emissive, cutout, two-sided, skinned — is left closed, as `conventions.md` says it is. One
   program, one instance buffer, one draw per blend mode.
3. **The pool, filled at load.** A fixed array per kind, a free list, a logged high-water
   mark, and the pass's programs drawn once behind the loading screen so the first blow does
   not pay for a pipeline compile. This is MU2's `Pool.cs` lesson: the four pools that built
   on demand hitched on the first blow, the first number, the first swing and the first
   level — the four most conspicuous moments in a fight.
4. **The cook grows an effects and sounds pass.** The 151 sheets to BC7 `.ktx` with mips and
   the cutout-alpha rescale the plan already requires; the sound events, their files, their
   onsets and their gains into one flat versioned table beside sprint 5's. What becomes of
   13 MB of wav is measured, not assumed.
5. **The landing cue.** One cue per blow, on the drawing's own scaled clock. The swing sound
   at the end of the blend into the swing, gated on the clip still being the swing; the hit
   sound and the number halfway through it. Struck, Hurt and Slain share it. The arithmetic
   stays on the tick where sprint 5 put it, and **nothing downstream ever reads a fact off a
   cue** — a dropped cue costs a sound and never a fact.
6. **Blood.** Ten particles at the target on every landed blow of every kind, thrown by the
   attacker as `MoveCharacter` throws them, not by the thing hit. Sparks only on the five
   skill-sword actions, which nothing this sprint can fire — built because it is the same
   code path, left unreachable and said so. Every length in **units of the target**, not in
   metres, or a spider wears a cloud bigger than itself. The death spatter is a separate
   decal at the head bone and belongs to step 7.
7. **The fall.** The death clip held on its last key; **no fade and no sinking**, which is
   what MU does. Fifteen reference frames — 0.6 s — on its feet when somebody else killed it,
   none when you did. The player's own fall is seven keys at play speed 0.45, which is 0.53 s
   and not 0.4. No flinch from a melee blow, and the census records why the client looks like
   it says otherwise.
8. **The health plate.** MU's own is a banner at the top of the screen with twenty segments;
   MU2 replaced it with a plate over the monster and that is the better answer, marked
   `invention`. It is screen-space over a world position, which makes it the only piece here
   that touches the HUD — it goes in the `hud` view, not the transparent one, and sprint 7
   inherits it rather than rebuilding it.
9. **Sound, through miniaudio.** Pinned in `bootstrap.sh` by commit first, since `PLAN.md`
   already claims it is. Then: the listener is the character and not the camera; the emitter's
   place is the vector from the hero to it **rotated by the camera's yaw alone**, so left and
   right are the screen's axes; the mix is flat; two voices an event with a new play stealing
   the oldest; an emitter follows what made it. Events fire through their onsets. The swing
   ladder is transcribed as its own ladder and **never derived from the clip ladder** — they
   deliberately disagree.
10. **`--bench effect`.** One effect at a time under the game's light, its sheet reloaded
    live, a shot and a log — as `--bench model` and `--bench monster` already work. This is
    where the look is judged and where the fill-rate worst case is measured.

## What this sprint is owed, and by sprint 5

Sprint 5's event list is `spawned, stepped, hit, died, dropped, gained, levelled`. The
showing needs three things it does not yet promise, and they are cheaper to add there than
here, because adding them here changes a seeded log that is meant to be byte-identical:

- **`hit` carries the amount, and whether it was a miss, a critical or ordinary.** The
  number's colour and its scale (15 against 50) depend on it, and a miss is a different
  sprite entirely.
- **`died` carries who killed it.** A kill you did yourself does not wait the fifteen frames;
  one killed by somebody else does.
- **The swing is an event**: when it began and which clip. The cue is halfway through *that*
  clip and drops itself if the clip changed.

## What is deliberately deferred

- **Skills, entirely, and not even a placeholder.** `PLAN.md`'s sprint table used to say
  "skills one story each" on this row; its own decided section says the skill system is
  Diablo 3's shape — learned permanently, four keys, real cooldowns — and belongs in its own
  sprint after 9. The table row was the older text and has been corrected to match. What this
  sprint must do about skills is the same thing sprint 5 must: **not bake MU's assumptions
  in**. No effect that can only be thrown from an item, no cue with room for exactly one
  timer, and the effect pool keyed by effect and not by weapon.
- **Items, drops and the bag** — sprint 7, whole. A kill still gives experience and nothing
  else, and `dropped` still says so to nobody.
- **The HUD proper, the text atlas, the pointer and pick** — sprint 7. The health plate is
  the single exception and is built here because a fight without it does not read.
- **Lamps, fires, water and the baked cubemap** — sprint 8. An effect lights nothing this
  sprint; it is drawn, not a light.
- **Missiles.** `index.json` describes eighteen and every one of them belongs to a bow or a
  spell. The bow is sprint 7's weapon and the spells are after 9. The cook reads the table
  because it is the same sweep; nothing stands on it.

## The two things most likely to go wrong

Written before the code, as sprints 2, 3 and 5 did:

- **The pass is measured on the wrong worst case and the account is a fiction.** The
  temptation is to measure a busy fight, because that is what the sprint is about and what
  the shot shows. But a sorted, blended, depth-tested pass costs **fill rate**, and thirty
  small sprites over a spider cover a fraction of the screen while one blood cloud close to
  a camera looking down covers half of it. The number that decides whether the account is
  honest is the near case, and it has to be sought deliberately because ordinary play will
  not hand it over often enough to show up in a median. If the two disagree, both go in the
  file and the worse one is the account.
- **The cue becomes load-bearing.** It is designed to be droppable — no sound without the
  animation that earns it — and every one of the eight things hanging off it is a temptation
  to read one fact from it "just this once": the health the plate shows, whether the corpse
  has begun to fall, which figure the blood belongs to. The day one of them does, a dropped
  cue stops costing a sound and starts costing a fact, and it will show up as a monster with
  a full health bar lying dead. The defence is that the cue carries only what it draws, the
  sim's own state is read for everything else, and the seeded headless run — which draws
  nothing — keeps proving that the facts are all on the tick.

## Measured

Filled in when the sprint lands: the transparent pass's cost in the near case and in an
ordinary fight, whether it went before or after the resolve and what the other cost, the
account it was given and what was taken from to pay for it, the pools' high-water mark per
kind and the allocation count in a frame, what the effects and sounds added to the cook and
to load time, and the frame with a fight in the town against the town without one.
