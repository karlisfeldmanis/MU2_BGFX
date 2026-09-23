// Where every body stands, which clip it plays and which way it is turned -- and the things
// that happen TO one: a fall, a level, a dragon's breath, a drop let go.
//
// This is the half of play.h's contract that owns presentation: the smoothing between two
// ticks, the gait, the coast into a stop, the death's hold and fade. It decides nothing. A
// fall waits for the blow that caused it to be SHOWN landing, which is why `fallWhenLanded`
// exists and why `shownAlive` is not `Body::alive`.
#include "game/play.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/frustum.h"
#include "game/play_tuning.h"

namespace mu::game {

void Play::sandOnDeath() {
    if (ground_ == nullptr) return;
    for (Drawn& one : drawn_) {
        if (!one.sands) continue;
        const sim::Body* body = realm_.find(one.id);
        // Up again: a respawned Giant may throw sand the next time it goes down.
        if (body && body->alive()) {
            one.sanded = false;
            continue;
        }
        if (one.sanded || !one.placed || !one.visible) continue;
        // MU reads the OBJECT's own action and clock and nothing else -- not `c->Dead`, not the
        // corpse's age -- so this begins a third of the way into the fall and not when the body
        // died. A body that never reaches key 8 simply never throws any, which is what MU does.
        const float key = keyOf(one.figure);
        if (slotOf(one.figure) != kMonsterDieSlot || key < kSandFrom || key >= kSandTo) continue;

        // **Twenty puffs, ONCE, and not twenty a reference frame.** This is the one place MU's
        // `rand_fps_check` does not convert the way the dragon's dust does, and reading it as if
        // it did made the death a sandstorm.
        //
        // `rand_fps_check(n)` compensates for the RENDER rate: it passes with probability
        // REFERENCE_FPS/FPS, so a loop of twenty attempts yields twenty per reference frame
        // however fast the machine draws. The dragon's dust is thrown for as long as it lives,
        // so twenty-per-reference-frame is its rate and the conversion is the whole answer.
        // This is not a rate. MU's window is one key of the death clip wide, and MU advances
        // `AnimationFrame` by about one key per reference frame, so the window IS one reference
        // frame and the total is twenty.
        //
        // A cooked clip here runs at its own authored duration instead, and Giant01's death is
        // slower than MU's reference: measured, it reaches key 8 nine tenths of a second after
        // the body falls, so one key spans about 2.8 reference frames and the integrated form
        // threw about 56 puffs where MU throws 20. Nearly three times, which is what "very
        // aggressive" looked like. Latching on the crossing gives MU's number at any clip rate
        // and any frame rate, which is what the case actually asks for.
        one.sanded = true;
        const FigureBody* look = one.figure.body();
        const float scale = look ? look->scale : 1.0f;
        // Round the feet and NOT the crown: MU throws from `o->Position`, which is where the
        // body stands, and Breath::sand pins the height to the land from there.
        const float feet[3] = {one.crown[0], ground_->heightAt(one.crown[0], one.crown[2]),
                               one.crown[2]};
        // Laid evenly round a ring rather than scattered, and the ring turned by the body's own
        // id so two Giants falling together do not throw the identical figure. No dice: this is
        // the only effect in the game whose layout is decided rather than rolled, because a
        // rolled ring of ten clumps as often as it spreads and the whole point of the shape is
        // that it opens. Invention, with the rest of the dressing -- see Breath::sand.
        const float phase = float(one.id % 360u) * 3.14159265f / 180.0f;
        for (int i = 0; i < kSandPuffs; ++i) {
            const float angle = phase + (float(i) + 0.5f) / float(kSandPuffs) * 6.2831853f;
            const float out[2] = {std::cos(angle), std::sin(angle)};
            breath_.sand(feet, out, kSandReach, scale);
        }
        // As the bones' burst and the meteor's throw do, and for the same reason: a run is read
        // afterwards rather than watched, and this is what a shot's frame is worked out from.
        core::logf("sand: tick %lld, #%u throws up %d puffs as it falls", (long long)realm_.tick(),
                   one.id, int(kSandPuffs));
    }
}

void Play::exhale(float seconds) {
    if (ground_ == nullptr) return;
    const float frames = seconds * 25.0f;

    // The blade's ribbon, before the dragons: every body mid-skill lays two more points on its
    // streak this frame, off the pose the frame has already computed. See fx/streak.h -- this is
    // MU's `CreateWeaponBlur` rung that tests the skill actions before it asks what is in the
    // hand, and it is the one effect a knight's skill has of its own.
    //
    // Sampled once a rendered frame rather than MU's ten sub-steps an animation frame. The
    // client re-poses the whole skeleton ten times to lay ten points because it samples at 25
    // frames a second and needs the arc smooth; at this frame rate the pose is already there and
    // re-posing a sixty-bone rig to arrive at the same curve would be work for nothing. MU2's
    // Trails made the same cut and says so.
    streak_.update(seconds);
    for (Drawn& one : drawn_) {
        if (one.casting <= 0.0f || !one.visible || !one.placed) continue;
        const FigureBody* look = one.figure.body();
        if (look == nullptr) continue;
        // Three keys of wind-up: the client's `AnimationFrame >= 3`, so the gathering of the
        // swing leaves nothing and the streak appears as the blade comes round.
        if (keyOf(one.figure) < kStreakWindUp) continue;
        for (const HeldItem& held : look->held) {
            if (held.kind != "weapon" || held.mesh == nullptr || held.bone < 0) continue;
            if (held.stance == "bow" || held.stance == "crossbow") continue;
            // Where the blade runs, read off the mesh's own box rather than trusted to an axis:
            // MU2's `Model.Blade` makes the same choice, because the rack is not consistent about
            // which way a weapon is modelled. The grip is at the origin -- a held item hangs off
            // its bone with an identity transform -- so the tip is whichever end of the longest
            // axis is furthest from it.
            const content::Bounds& box = held.mesh->bounds();
            int axis = 0;
            float far = 0.0f;
            for (int k = 0; k < 3; ++k) {
                const float reach = std::max(std::fabs(box.min[k]), std::fabs(box.max[k]));
                if (reach > far) {
                    far = reach;
                    axis = k;
                }
            }
            if (far <= 0.001f) continue;
            const float way = std::fabs(box.max[axis]) >= std::fabs(box.min[axis]) ? 1.0f : -1.0f;
            float grip[3] = {0.0f, 0.0f, 0.0f}, tip[3] = {0.0f, 0.0f, 0.0f};
            grip[axis] = way * far * kStreakFrom;
            tip[axis] = way * far;
            float from[3], to[3];
            if (!one.figure.pointOn(held.bone, grip, from)) break;
            if (!one.figure.pointOn(held.bone, tip, to)) break;
            streak_.feed(one.id, from, to);
            break;
        }
    }

    for (Drawn& one : drawn_) {
        if (!one.breathes) continue;
        const sim::Body* body = realm_.find(one.id);
        const FigureBody* look = one.figure.body();
        // c->Dead == 0: the moment the death arrives, not when the corpse is gone. A dragon
        // that kept raising dust while it lay there was gated on being drawn alone.
        if (!body || !body->alive() || !look || !one.visible || !one.placed) {
            one.fireOwed = one.dustOwed = 0.0f;
            continue;
        }
        const float scale = look->scale;
        // o->Angle, which both particles' velocities are turned by: the facing on the ground.
        // A model looks down +z, which placementTransform turns to (sin yaw, cos yaw).
        const float along[2] = {std::sin(one.yaw), std::cos(one.yaw)};

        // rand_fps_check(4), one puff every fourth reference frame.
        one.dustOwed += frames / 4.0f;
        while (one.dustOwed >= 1.0f) {
            one.dustOwed -= 1.0f;
            const float feet[3] = {one.crown[0], ground_->heightAt(one.crown[0], one.crown[2]),
                                   one.crown[2]};
            breath_.puff(feet, along, scale);
        }

        // rand_fps_check(1) inside the bite's first four keys: a spark every reference frame.
        const bool biting = slotOf(one.figure) == kBreathSlot && keyOf(one.figure) >= 0.0f &&
                            keyOf(one.figure) <= kBreathThrough;
        if (!biting || one.headBone < 0) {
            one.fireOwed = 0.0f;
            continue;
        }
        one.fireOwed += frames;
        while (one.fireOwed >= 1.0f) {
            one.fireOwed -= 1.0f;
            const float origin[3] = {0.0f, 0.0f, 0.0f};
            float head[3];
            if (!one.figure.pointOn(one.headBone, origin, head)) break;
            // Out of the face along the facing rather than along the bone: the client takes the
            // bone's POSITION and the object's ANGLE, two different things, and a dragon throws
            // its head through the bite -- along the bone, the jet went where the head went.
            // MU2's Breath.Aim found this; the facing is already turned onto the quarry.
            const float out =
                (kBreathNear + (kBreathFar - kBreathNear) * float(wanderDice_ % 1000) / 1000.0f) *
                scale;
            wanderDice_ ^= wanderDice_ << 13;
            wanderDice_ ^= wanderDice_ >> 17;
            wanderDice_ ^= wanderDice_ << 5;
            const float at[3] = {head[0] + along[0] * out, head[1], head[2] + along[1] * out};
            breath_.spark(at, along, scale);
        }
    }
}

void Play::rise() {
    const Drawn* hero = drawnOf(realm_.hero().id);
    if (hero == nullptr || !hero->placed || ground_ == nullptr) return;
    // At his feet where he is drawn, and in the facing he has now; it does not follow him.
    const float feet[3] = {hero->crown[0], ground_->heightAt(hero->crown[0], hero->crown[2]),
                           hero->crown[2]};
    // The sound in the same call as the flares, so the two start on one frame: MU's
    // ReceiveLevelUp is one block that throws the joints and plays SOUND_LEVEL_UP together.
    // Sound::play takes the rest of the sync -- the file's silence and the device's buffer.
    aura_.rise(feet, hero->yaw, ground_->metresPerTile());
    sound_.play("player_level_up");
}

// The knight's guard raised, and then kept on him while it stands.
//
// Two calls and not one because the barrier follows the body, which the level-up's flares do
// not: MU leaves a burst where it was thrown (`TargetPosition` is never assigned), and a guard
// that stayed behind while he walked out of it would be a guard around nobody.
void Play::guardRise(float seconds) {
    const Drawn* hero = drawnOf(realm_.hero().id);
    if (hero == nullptr || !hero->placed || ground_ == nullptr) return;
    const float feet[3] = {hero->crown[0], ground_->heightAt(hero->crown[0], hero->crown[2]),
                           hero->crown[2]};
    aura_.guard(feet, hero->yaw, ground_->metresPerTile(), seconds);
}

void Play::guardStep() {
    if (ground_ == nullptr) return;
    const sim::Body& hero = realm_.hero();
    // The realm decides when it lapses, as it decides everything else; the drawing reads it.
    // Both halves matter: a boon that ran out and a character who died take the ribbons away.
    if (!hero.alive() || hero.boonUntil <= realm_.tick()) {
        aura_.release();
        return;
    }
    const Drawn* drawn = drawnOf(hero.id);
    if (drawn == nullptr || !drawn->placed) return;
    const float feet[3] = {drawn->crown[0], ground_->heightAt(drawn->crown[0], drawn->crown[2]),
                           drawn->crown[2]};
    aura_.follow(feet);
}

void Play::releaseDrops() {
    held_.erase(std::remove_if(held_.begin(), held_.end(),
                               [&](const HeldDrop& one) {
                                   const Drawn* dropper = drawnOf(one.dropper);
                                   bool let = true;
                                   if (dropper == nullptr) let = true;
                                   else if (dropper->fallOwed) let = false;
                                   else if (dropper->deadFor < 0.0f) let = true;  // rose again
                                   else let = dropper->deadFor >= kDropDelay;
                                   if (let) landed(one.drop);
                                   return let;
                               }),
                held_.end());
    heldIds_.clear();
    for (const HeldDrop& one : held_) heldIds_.push_back(one.drop);
}

void Play::fall(Drawn& dead) {
    dead.fallOwed = false;
    dead.deadFor = 0.0f;
    // Whatever pace a swing or a walk left it at is not this clip's: MU's own death plays at
    // its authored speed regardless of what killed him mid-step.
    dead.clipRate = 1.0f;
    dead.swinging = 0.0f;
    // **A skeleton has no corpse.** `o->Live = false` on the same instruction that makes the
    // bones, so the model stops drawing at once and the death clip in its rig is never
    // reached. `deadFor` is put straight at the end of the whole death, which is what already
    // takes a body off the screen in Play::follow -- the burst IS the fall for this breed, and
    // everything that waits on a body going down waits on this.
    if (dead.bursts && bones_.isOpen() && ground_) {
        const float scale = dead.figure.body() ? dead.figure.body()->scale : 1.0f;
        const float x = dead.crown[0], z = dead.crown[2];
        bones_.burst(x, z, ground_->heightAt(x, z), scale);
        dead.deadFor = kDeathTotal;
        // As the meteor's throw does, and for the same reason: a run is read afterwards
        // rather than watched, and this is what a shot's frame is worked out from.
        core::logf("bones: tick %lld, #%u comes apart at %.1f,%.1f -- %u pieces in the air",
                   (long long)realm_.tick(), dead.id, x, z, bones_.live());
    } else if (dead.deathClip >= 0) {
        dead.figure.play(dead.deathClip, true);
    }
    // With the death clip's first key, as PlayMonsterSound is: a body that waits for its
    // killing blow to land waits the same to cry out. Where it falls, and not followed --
    // a corpse goes nowhere.
    if (dead.cryDie >= 0 && dead.placed) emit(dead.cryDie, dead.crown[0], dead.crown[2]);
}

void Play::fallWhenLanded() {
    for (Drawn& one : drawn_) {
        if (one.fallOwed && !showing_.awaits(one.id)) fall(one);
    }
}

bool Play::shownAlive(uint32_t id) const {
    const sim::Body* body = realm_.find(id);
    if (!body) return false;
    if (body->alive()) return true;
    for (const Drawn& one : drawn_) {
        if (one.id == id) return one.fallOwed;
    }
    return false;
}

void Play::follow(float seconds) {
    if (!ground_) return;
    const float metresPerTile = ground_->metresPerTile();
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        if (!body || !one.figure.body()) {
            one.visible = false;
            continue;
        }
        // A dead body holds where it fell rather than following anything below -- it has no
        // more ticks coming, so there is nothing new to interpolate toward -- and stays
        // visible only for as long as `deadFor` says its death clip and its fade are still
        // playing (Died and Rose, in Play::update). Left at rest, not repathed and not
        // reposed: the position, the clip and the pose it died in are exactly the last ones
        // `follow` ever wrote for it.
        if (!body->alive()) {
            // Still standing on screen until its killing blow lands: drawn where it last was,
            // in whatever it was playing.
            if (one.fallOwed) {
                one.visible = true;
                continue;
            }
            one.visible = one.deadFor >= 0.0f && (body->player || one.deadFor < kDeathTotal);
            if (one.deadFor >= 0.0f) one.deadFor += seconds;
            continue;
        }
        one.visible = true;
        if (one.spawnFade < kSpawnFadeSeconds) one.spawnFade += seconds;
        // Out of range is not drawn and not posed. Measured in the log beside the drawn count,
        // because foundation 7 says a culling change that is not visible in those numbers did
        // not happen.
        const sim::Body& hero = realm_.hero();
        if (std::max(std::fabs(body->x - hero.x), std::fabs(body->y - hero.y)) > kDrawRange) {
            one.visible = false;
            continue;
        }

        // Between the two ticks either side of where the clock stands. Presentation only:
        // every reach, aim and hit in the sim used the sim's own position, and this one is
        // never read back into it. docs/conventions.md, "Time".
        //
        // Not always evenly, though. The tick a walk ends on covers only what was left of the
        // last tile -- 0.74 of a step, say -- and spread over the whole 50 ms that is the body
        // slowing to a quarter of its pace under feet still striding at full, then standing
        // with the walk still playing: the slide at the end of every walk. So the arrival is
        // drawn at the body's own pace and finishes early, at `arrived` of the way through,
        // and the drawn body stands still on the spot for the rest of the tick. A jump of more
        // than two tiles is a respawn or a gate and is not walked at all.
        const float covered = one.groundSpeed * float(kTickSeconds) / metresPerTile;
        const bool jumped = covered > 2.0f;
        float through = jumped ? 1.0f : through_;
        float arrived = 1.0f;
        if (!body->walking && covered > 1e-4f && covered < body->speed * 0.999f) {
            arrived = covered / body->speed;
            through = std::min(1.0f, through_ / arrived);
        }
        // Whether the DRAWN body is covering ground at this instant. This, and not the sim's
        // `walking`, is what the walk clip answers to: the sim stops a body on a tick and the
        // drawing gets there up to a tick later.
        const bool moving = !jumped && covered > 1e-4f && through_ < arrived;
        const float tileX = one.wasX + (one.nowX - one.wasX) * through;
        const float tileY = one.wasY + (one.nowY - one.wasY) * through;
        const float x = (tileX + 0.5f) * metresPerTile;
        const float z = -(tileY + 0.5f) * metresPerTile;
        // A model looks down +z, and placementTransform negates the angle it is given, so +z
        // ends up along (sin yaw, cos yaw). The sim's facing is an angle in TILE space, where
        // +x is the column and +y is the row -- and the row runs south, which is -z. So the
        // direction of travel in the world is (cos facing, -sin facing) and the yaw that
        // points a model down it is atan2 of those two, in that order.
        // docs/conventions.md, "Space" and "Matrices".
        // The facing between the same two ticks the position is taken between, the short way
        // round. Read raw off the sim it snaps up to 45 degrees at a time; see Drawn::wasFacing.
        const float facing =
            one.wasFacing + wrapped(one.nowFacing - one.wasFacing) * through_;
        const float dx = std::cos(facing);
        const float dz = -std::sin(facing);
        one.yaw = std::atan2(dx, dz);

        const float position[3] = {x, ground_->heightAt(x, z), z};
        // The safe zone is a stance and not only a place: inside one MU carries the weapon on
        // the back and stands in the unarmed idle, and steps out of it with the weapon drawn.
        // `place` moves the weapon; the clip below is the other half of the same rule, and the
        // 0.18 s crossfade in Figure::play is what makes the change a blend rather than a cut.
        const bool safe = tables_.grid.safe(body->column(), body->row());
        one.figure.place(position, one.yaw, safe);
        {
            const FigureBody* look = one.figure.body();
            one.crown[0] = position[0];
            one.crown[1] = position[1] + look->height * look->scale;
            one.crown[2] = position[2];
            one.placed = true;
        }
        // How long it has covered no ground. A walk is not always given up on purpose: a step
        // refused because something stood in it, a corner arrived at exactly on the boundary,
        // the gap while a route is replaced, and every tick spent turning on the spot all read
        // as still. Taken at face value each of them drops into the idle and comes straight
        // back out, and because the fade is longer than the blip the man goes soft in the knees
        // at every obstacle. MU2's `Crowd.Coasting` covers them with two ticks of patience.
        one.still = moving ? 0.0f : one.still + seconds;
        // A swing holds until it has played out, and then walk or idle take it back. `play`
        // ignores a request for the clip already running, so the two below are comparisons
        // rather than restarts, and the blend between them is the crossfade's.
        // A step cancels the swing; the two are never drawn at once. MU has no animation that
        // is both, and a figure that keeps swinging while it slides along the ground is the
        // most conspicuous thing in a fight -- measured on the hunt before this line existed,
        // 921 of 1217 swing-frames were played over a walking body, because a fighter re-paths
        // toward a quarry that shuffled and the swing clip outlives the halt that fed it.
        //
        // Cancelling is the whole of the rule: the blow itself already landed on the tick, and
        // nothing downstream reads a fact off the pose. What is lost is the rest of an
        // animation, which is what an attack cancel loses in any game that has one.
        // A cast is the exception, and it is the one the rule above was written before there
        // were any: the realm does not let him walk until the clip is done, so a walk arriving
        // over a skill is the drawing's own interpolation catching up rather than a step he is
        // taking, and cutting the skill for it is what made the animation look instant.
        one.casting = std::max(0.0f, one.casting - seconds);
        if (one.swinging > 0.0f && body->walking && one.casting <= 0.0f) one.swinging = 0.0f;
        if (one.swinging > 0.0f) {
            one.clipRate = one.swingPace;
            continue;
        }
        const FigureBody* look = one.figure.body();
        int clip = look->idleClip;
        // Which walk this body walks in HERE. Inside a safe zone MU gives PLAYER_WALK_MALE
        // whatever is carried, the walking half of the rule the idle below already keeps: the
        // knight crosses the town square empty-handed with the axe on his back, and draws it
        // as he steps out. Both walks count as "the walk" everywhere below -- stepping over
        // the zone's edge mid-stride is a change of walk, crossfaded and resumed at the same
        // phase, and must not read as stopping and setting off again.
        const int walkHere =
            (safe && look->walkSafeClip >= 0) ? look->walkSafeClip : look->walkClip;
        const auto isWalk = [&](int c) {
            return c >= 0 && (c == look->walkClip || c == look->walkSafeClip);
        };
        // Walking is what the drawn body is doing, and nothing else sets a walk going: a body
        // the sim has walking but still turning on the spot stays in its idle until the first
        // step lands, rather than marching in place through the pivot. Once walking, a turn on
        // the spot mid-walk keeps the walk -- a reversal is two ticks and dropping to the idle
        // for them is a stumble.
        //
        // And the end of a walk is exactly where the drawn body stops, with no patience after
        // it. MU2's coast held the walk a tenth of a second past every stop, which was striding
        // on the spot at every arrival -- "weird walking when he has already stopped". The coast
        // is kept for monsters only, whose chase halts and re-plans between ticks and would
        // flicker to idle without it; the character's walk is replaced, never halted, when a
        // click re-aims it.
        const bool walking = moving || (body->walking && isWalk(one.figure.clip())) ||
                             (!body->player && body->walking && one.still < kCoasting);
        if (walking) {
            clip = walkHere;
        } else if (safe && look->idleSafeClip >= 0) {
            clip = look->idleSafeClip;
        }
        if (clip < 0) continue;

        const int was = one.figure.clip();
        if (clip != was) {
            if (isWalk(clip)) {
                // Setting off: the longest change in the game -- a standing pose to a
                // mid-stride one, where the legs are further apart than in any other
                // transition -- and the one MU's own key length serves worst. Resumed where
                // the cycle left off rather than restarted at one leg fully forward.
                one.figure.play(clip, false, kGaiting);
                one.figure.setClock(one.walkPhase);
            } else {
                if (isWalk(was)) one.walkPhase = one.figure.clock();
                // Coming to a stop is an arrival, drawn on the frame the body stops, and the
                // fade is only long enough not to be a cut: any longer is feet sliding under a
                // body that is no longer going anywhere.
                one.figure.play(clip, false, isWalk(was) ? kHalting : -1.0f);
            }
        }

        // And the rate the clip runs at: the gait's own speed over the ground divided by the
        // speed the clip was authored to travel at. A Dark Knight walks 2.5 m/s and his walk
        // carries 2.4288 m over a 0.933 s cycle, which is 2.60 m/s, so he runs it at 0.961 and
        // his feet keep the earth. MU2's `Crowd.Rate`, and the cook's `travel` is what makes it
        // possible at all.
        //
        // The speed is the body's NOMINAL one and not the ground it covered on the last tick,
        // and that distinction was worth a regression to learn. Measured per tick, the same
        // walk reports 2.5 m/s for most ticks, 0.74 on the tick it arrives on -- a tile is not
        // a whole number of ticks, so the last one covers a fraction -- and 0.0 for every tick
        // spent pivoting. Fed to the clip, those become a step in slow motion at the end of
        // every walk and feet that stop dead while the man turns, which is precisely what the
        // first person to see it said: "foot gets freezed, looks slow motion". The tick
        // quantises movement; a gait does not, and the animation follows the gait.
        // The noise of something walking about. PlayMonsterSound from the bottom of
        // SetPlayerWalk, which the client runs every frame a thing moves: rand_fps_check(16),
        // one in sixteen per 25 fps reference frame, scaled to the frame actually drawn. Gated
        // on the walk clip, the witness the clip chooser uses, so a monster whose step was
        // refused stands silent. MU2's Crowd.Wander.
        if (!body->player && one.cryMove >= 0 && isWalk(one.figure.clip())) {
            wanderDice_ ^= wanderDice_ << 13;
            wanderDice_ ^= wanderDice_ >> 17;
            wanderDice_ ^= wanderDice_ << 5;
            const float roll = float(wanderDice_ >> 8) / float(1u << 24);
            if (roll < seconds * 25.0f / 16.0f) {
                emit(one.cryMove, one.crown[0], one.crown[2], one.id);
            }
        }
        one.clipRate = 1.0f;
        if (isWalk(one.figure.clip())) {
            const float metresPerTile = ground_->metresPerTile();
            const float gait = body->speed * metresPerTile / float(kTickSeconds);
            // The clip's own planted foot decides, and the cook's whole-cycle travel is the
            // fallback for a body that plants nothing measurable. The two disagree by 4% on
            // MU's walk, and the stance is the one to believe: `travel` counts the swinging
            // foot as well, which is in the air going the other way at twice the speed, and
            // no eye has ever judged a walk by it. tools/stride.py has both numbers and the
            // slide each leaves.
            // The plant speed of the walk actually playing: the two walks are two clips
            // with two sets of feet, and pacing the unarmed one by the armed one's stride
            // would slide it.
            const float plant =
                (one.figure.clip() == look->walkSafeClip && look->walkSafeClip != look->walkClip
                     ? look->plantSpeedSafe
                     : look->plantSpeed) *
                look->scale;
            const float travel = one.figure.travel();
            const float duration = one.figure.length();
            if (plant > 0.01f) {
                one.clipRate = std::min(gait / plant, kFastestClip);
            } else if (travel > 0.001f && duration > 0.0f) {
                one.clipRate = std::min(gait * duration / travel, kFastestClip);
            }
        }
    }
}

Play::Drawn* Play::drawnOf(uint32_t id) {
    // The bodies are made once and never reordered, and ids are handed out from 1 in that same
    // order, so this is an index and not a search.
    const size_t at = size_t(id) - 1;
    return at < drawn_.size() && drawn_[at].id == id ? &drawn_[at] : nullptr;
}

void Play::focus(float* column, float* row) const {
    if (drawn_.empty()) return;
    const Drawn& hero = drawn_[0];
    *column = hero.wasX + (hero.nowX - hero.wasX) * through_;
    *row = hero.wasY + (hero.nowY - hero.wasY) * through_;
}

void Play::gather(gfx::Renderer& renderer, const float* viewProj, std::vector<gfx::Drawable>& out,
                  std::vector<gfx::Drawable>* casters, std::vector<gfx::Drawable>* hover) {
    // One eased fade for every reason a body is not simply "there": the hero's own
    // door-opening appearance, a corpse going out at the end of its held pose, and a
    // respawn easing back in. All three are the same smoothstep on a different clock, so
    // they are one function and not three copies of it.
    const auto fadeOf = [&](const Drawn& one) -> float {
        if (&one == &drawn_[0] && appearing_) {
            const float t = std::clamp(appearAt_ / kAppearSeconds, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
        if (one.deadFor >= 0.0f) {
            if (one.deadFor < kDeathHold || &one == &drawn_[0]) return 1.0f;
            const float t = std::clamp((one.deadFor - kDeathHold) / kDeathFade, 0.0f, 1.0f);
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }
        if (one.spawnFade < kSpawnFadeSeconds) {
            const float t = std::clamp(one.spawnFade / kSpawnFadeSeconds, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
        return 1.0f;
    };
    // What is left of the skeletons: lit, opaque meshes on the grass, gathered with the
    // bodies and NOT with the casters -- eleven small shadows on the frame a fight is busiest
    // are not worth the shadow map's time, which is MU2's call and is recorded as one.
    bones_.gather(out);
    for (Drawn& one : drawn_) {
        if (!one.visible || !one.figure.body()) continue;
        const float fade = fadeOf(one);
        if (fade <= 0.0f) continue;
        const int bones = one.figure.pose(scratch_.data());
        const int palette = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        // The hover ring's own copy: the SAME pose, just handed to a second list, so the
        // outline mask draws it again without a second call to Figure::pose. Only the body
        // pointedAt() names, and only while it is actually drawn -- a monster that faded out
        // from under the pointer rings nothing, which is what Godot's ObjectDisposedException
        // guard amounted to. Gated on no townsperson winning the same pick, or a guard closer
        // than a monster behind him would ring both at once -- leftClick's own ladder, which
        // this has to agree with since the ring is meant to show what a click would answer.
        if (hover && pointedFolk_ < 0 && one.id == pointedAt_) one.figure.gather(palette, *hover);
        if (fade < 1.0f) {
            const size_t outFrom = out.size(), castFrom = casters ? casters->size() : 0;
            if (casters) one.figure.gather(palette, *casters);
            one.figure.gather(palette, out);
            for (size_t i = outFrom; i < out.size(); ++i) out[i].fade = fade;
            if (casters) {
                for (size_t i = castFrom; i < casters->size(); ++i) (*casters)[i].fade = fade;
            }
            continue;
        }
        // The sun's list is every figure, as the town's is: a body behind the camera still
        // casts into the frame, and culling the shadow pass with the camera's frustum is the
        // bug foundation 7 names. The camera's own cull is Crowd's frustum test and is owed
        // here; at Lorencia's 290 bodies, of which a handful are ever near the camera, it is
        // the next thing to do and not this sprint's.
        if (casters) one.figure.gather(palette, *casters);
        one.figure.gather(palette, out);
    }
    for (size_t i = 0; i < folk_.size(); ++i) {
        Standing& one = folk_[i];
        const int bones = one.figure.pose(scratch_.data());
        const int palette = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        // By the table's row, which is what the pick names: folk_ skips the rows with no
        // figure and appends the placements' four at the end, so its own index is not it.
        if (hover && pointedFolk_ >= 0 && one.folk == pointedFolk_) {
            one.figure.gather(palette, *hover);
        }
        if (casters) one.figure.gather(palette, *casters);
        one.figure.gather(palette, out);
    }
}

void Play::settle(Standing& one) {
    const FigureBody* look = one.figure.body();
    if (!look || !look->library) return;
    // Its own clips only. A townsperson on the player's rig -- the guards -- would roll
    // among dying, sitting and casting, which is why MU2 named them a single clip.
    const size_t count = look->library->clips.clips.size();
    one.cycles = look->library->name != "player" && count > 1;
    if (!one.cycles) return;
    // Seeded from where it stands, so a town comes up the same way every time and two
    // figures of a kind do not share a roll.
    const float* at = one.figure.position();
    one.dice = uint32_t(int32_t(at[0] * 37.0f) * 73856093 ^ int32_t(at[2] * 61.0f) * 19349663) | 1u;
    one.figure.play(fidget(one), true, 0.0f);
    const float length = one.figure.length();
    one.dice ^= one.dice << 13;
    one.dice ^= one.dice >> 17;
    one.dice ^= one.dice << 5;
    if (length > 0.0f) one.figure.setClock(float(one.dice % 1000u) / 1000.0f * length);
    one.lastClock = one.figure.clock();
}

int Play::fidget(Standing& one) {
    const size_t count = one.figure.body()->library->clips.clips.size();
    const auto roll = [&one](uint32_t below) {
        one.dice ^= one.dice << 13;
        one.dice ^= one.dice >> 17;
        one.dice ^= one.dice << 5;
        return one.dice % below;
    };
    if (count < 2 || roll(16) < 12) return 0;
    return 1 + int(roll(uint32_t(count - 1)));
}

int32_t Play::shownHealth(uint32_t id) const {
    const sim::Body* body = realm_.find(id);
    if (!body || !shownAlive(id)) return 0;
    return std::min(body->maxHealth, std::max(0, body->health) + showing_.owed(id));
}

}  // namespace mu::game
