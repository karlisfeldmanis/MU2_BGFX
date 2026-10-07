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
#include "game/world/lamps.h"

namespace mu::game {

void Play::sandOnDeath() {
    if (ground_ == nullptr) return;
    // The Ice Monster's shatter, on the death clip's last key: MU calls CreateBlood when the
    // death action stops playing, so the body falls first and then is gone in ten shards.
    for (Drawn& one : drawn_) {
        if (!one.shatters) continue;
        const sim::Body* body = realm_.find(one.id);
        if (body && body->alive()) {
            one.shattered = false;
            continue;
        }
        if (one.shattered || !one.placed || !one.visible || one.deadFor < 0.0f) continue;
        const FigureBody* look = one.figure.body();
        if (!look || !look->library || slotOf(one.figure) != kMonsterDieSlot) continue;
        const content::CookedClip& clip = look->library->clips.clips[size_t(one.figure.clip())];
        if (keyOf(one.figure) < float(clip.frames - 1) - 0.01f) continue;
        one.shattered = true;
        const float feet[3] = {one.crown[0], ground_->heightAt(one.crown[0], one.crown[2]),
                               one.crown[2]};
        ice_.shatter(feet);
        // Out at once, as Play::fall puts a skeleton: the end of the whole death is what
        // takes a body off the screen in Play::follow.
        one.deadFor = kDeathTotal;
    }
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

    // The blade's ribbon, before the dragons: every body mid-skill or mid-blow lays two more
    // points on its streak this frame, off the pose the frame has already computed. See
    // fx/streak.h -- MU's `CreateWeaponBlur`: the skill rung, tested before it asks what is in
    // the hand, and the plain swing's, which MU keeps to swords and this lays for any weapon.
    //
    // Sampled once a rendered frame rather than MU's ten sub-steps an animation frame. The
    // client re-poses the whole skeleton ten times to lay ten points because it samples at 25
    // frames a second and needs the arc smooth; at this frame rate the pose is already there and
    // re-posing a sixty-bone rig to arrive at the same curve would be work for nothing. MU2's
    // Trails made the same cut and says so.
    streak_.update(seconds);
    for (Drawn& one : drawn_) {
        if (!one.visible || !one.placed || one.deadFor >= 0.0f) continue;
        const bool skill = one.casting > 0.0f;
        // A plain blow: a swing thrown with the weapon. Not a salute, which is held as a swing
        // is but given with the weapon slung.
        const bool blow =
            !skill && one.swinging > 0.0f && one.swingSkill == 0 && one.stowed <= 0.0f;
        if (!skill && !blow) continue;
        // A spell is thrown from an empty hand's gesture, and a staff streaks nothing.
        if (const sim::SkillRow* row = sim::skillNumbered(one.swingSkill);
            skill && row && row->wizardry) {
            continue;
        }
        // Nor the wheel: MU blurs SKILL_SWORD1..5 and its action is past them. fx/wheel.h.
        if (skill && one.swingSkill == sim::skill::kTwistingSlash) continue;
        // Nor Rageful Blow, past them too, whose weapon is out of his hand. fx/fury.h.
        if (skill && one.swingSkill == sim::skill::kRagefulBlow) continue;
        // Nor Death Stab: PLAYER_ATTACK_DEATHSTAB is on neither of CreateWeaponBlur's lists -- a
        // spear blurs PLAYER_ATTACK_SPEAR1..SCYTHE3 alone (ZzzCharacter.cpp:3859-3863) -- and the
        // blur the skill rung laid over its thrust was a pink-white smear MU never draws
        // (2026-10-02, the user: "still buggy"). Its own look is fx/deathstab.h.
        if (skill && one.swingSkill == sim::skill::kDeathStab) continue;
        // Nor Impale, on the same thrust or MU's ride one, which is on neither list either.
        if (skill && one.swingSkill == sim::skill::kImpale) continue;
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
            // The look: white on motion_blur_r for a skill; for a plain blow, blur01 from a sixth
            // of the blade, or blur02 over a spear's or a scythe's head, lit by mapping 0's ladder.
            const bool pole = held.stance == "spear" || held.stance == "scythe";
            Streak::Sheet sheet = Streak::Sheet::Skill;
            float light[3] = {1.0f, 1.0f, 1.0f};
            if (blow) {
                sheet = pole ? Streak::Sheet::Spear : Streak::Sheet::Plain;
                const float* ladder = held.plus >= 7   ? kStreakLight[3]
                                      : held.plus >= 5 ? kStreakLight[2]
                                      : held.plus >= 3 ? kStreakLight[1]
                                                       : kStreakLight[0];
                for (int k = 0; k < 3; ++k) light[k] = ladder[k];
            }
            const float way = std::fabs(box.max[axis]) >= std::fabs(box.min[axis]) ? 1.0f : -1.0f;
            float grip[3] = {0.0f, 0.0f, 0.0f}, tip[3] = {0.0f, 0.0f, 0.0f};
            grip[axis] = way * far * (blow && pole ? kStreakPoleFrom : kStreakFrom);
            tip[axis] = way * far;
            float from[3], to[3];
            if (!one.figure.pointOn(held.bone, grip, from)) break;
            if (!one.figure.pointOn(held.bone, tip, to)) break;
            streak_.feed(one.id, from, to, sheet, light);
            break;
        }
    }

    // The hero's refined gear as a light source: fx/gleam.h.
    gleam_.update(seconds);
    if (heroDrawn() && drawn_[heroAt()].visible && drawn_[heroAt()].placed &&
        drawn_[heroAt()].figure.body()) {
        Drawn& one = drawn_[heroAt()];
        const FigureBody* look = one.figure.body();
        {
            // The suit: the most refined piece, lighting from the body's own line, pelvis to neck.
            ShineLook suit;
            for (const ShineLook& part : look->partShine) {
                if (part.level > suit.level) suit = part;
            }
            const float origin[3] = {0.0f, 0.0f, 0.0f};
            float low[3], high[3];
            if (suit.level >= 7 && look->pelvisBone >= 0 && look->neckBone >= 0 &&
                one.figure.pointOn(look->pelvisBone, origin, low) &&
                one.figure.pointOn(look->neckBone, origin, high)) {
                gleam_.feedLight(low, high, suit);
            }
            // The weapon or shield: the most refined thing held, lighting from its own line --
            // grip to tip along its longest axis -- in the hand, or from where it hangs when it
            // is slung.
            const HeldItem* brightest = nullptr;
            for (const HeldItem& held : look->held) {
                if (held.mesh && held.bone >= 0 &&
                    (!brightest || held.shine.level > brightest->shine.level)) {
                    brightest = &held;
                }
            }
            if (brightest && brightest->shine.level >= 7) {
                const content::Bounds& box = brightest->mesh->bounds();
                int axis = 0;
                float far = 0.0f;
                for (int k = 0; k < 3; ++k) {
                    const float reach = std::max(std::fabs(box.min[k]), std::fabs(box.max[k]));
                    if (reach > far) {
                        far = reach;
                        axis = k;
                    }
                }
                const float way =
                    std::fabs(box.max[axis]) >= std::fabs(box.min[axis]) ? 1.0f : -1.0f;
                float tipLocal[3] = {0.0f, 0.0f, 0.0f};
                tipLocal[axis] = way * far;
                float grip[3], tip[3];
                if (one.figure.slung()) {
                    // On the back: a short line at the socket it hangs from.
                    if (one.figure.pointOn(look->backBone, origin, grip)) {
                        gleam_.feedLight(grip, grip, brightest->shine);
                    }
                } else if (one.figure.pointOn(brightest->bone, origin, grip) &&
                           one.figure.pointOn(brightest->bone, tipLocal, tip)) {
                    gleam_.feedLight(grip, tip, brightest->shine);
                }
            }
        }
    }

    // Tarkan's walking sand, MonsterMoveSandSmoke: `CurrentAction == MONSTER01_WALK` and
    // rand_fps_check(1), one puff a reference frame within 100 units of the body.
    for (Drawn& one : drawn_) {
        if (!one.walkSands) continue;
        const sim::Body* body = realm_.find(one.id);
        if (!body || !body->alive() || !one.visible || !one.placed ||
            slotOf(one.figure) != kMonsterWalkSlot) {
            one.walkSandOwed = 0.0f;
            continue;
        }
        const float along[2] = {std::sin(one.yaw), std::cos(one.yaw)};
        one.walkSandOwed += frames;
        while (one.walkSandOwed >= 1.0f) {
            one.walkSandOwed -= 1.0f;
            const float feet[3] = {one.crown[0], ground_->heightAt(one.crown[0], one.crown[2]),
                                   one.crown[2]};
            breath_.walkSand(feet, along, kWalkSandReach, kWalkSandAlpha);
        }
    }

    // The Dark Phoenix's shield, each time it goes up (kPhoenixShieldTicks, kPhoenixBarrier).
    for (Drawn& one : drawn_) {
        if (!one.phoenix) continue;
        const sim::Body* body = realm_.find(one.id);
        const bool up = ((realm_.tick() / kPhoenixShieldTicks + int64_t(one.id)) & 1) == 0;
        if (up && !one.shieldUp && body && body->alive() && one.visible && one.placed) {
            // Round the bird where it flies, not on the plane metres under it: from a little
            // under its middle, so the ribbons rise through it.
            const FigureBody* look = one.figure.body();
            const float tall = look ? look->height * look->scale : 2.0f;
            const float under[3] = {one.crown[0], one.crown[1] - tall * 0.7f, one.crown[2]};
            aura_.cast(kPhoenixBarrier, under, one.yaw, ground_->metresPerTile());
        }
        one.shieldUp = up;
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
            // The Great Bahamut's trail broader and fainter than the dragon's dust (the user,
            // 2026-10-04: 'that smoke is to small and to vissible'); ours.
            const bool great = look->name == kGreatBahamutFigure;
            breath_.puff(feet, along, scale, great ? kGreatBahamutPuffGrow : 1.0f,
                         great ? kGreatBahamutPuffAlpha : 1.0f);
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

// The Bull Fighters: a puff off the muzzle every second reference frame while the clip is in
// one of its four windows, and on the Elite two eyes drawn every frame it is. fx/snort.h and
// fx/eyes.h have the client's code.
void Play::snort(float seconds) {
    snort_.update(seconds);
    dust_.update(seconds);
    eyes_.update(seconds);
    eyeTrails_.update(seconds);
    bodyFlames_.update(seconds);
    // The Staff of Resurrection's fire wherever it is held in hand (fx/staff_fire.h).
    staffFire_.update(seconds);
    {
        float head[3], sparks[3], shaft[10][3];
        StaffFire::points(head, sparks, shaft);
        for (Drawn& one : drawn_) {
            if (!one.visible || !one.placed) continue;
            float worldHead[3], worldSparks[3], worldShaft[10][3];
            if (!one.figure.heldPoint("Staff07", head, worldHead)) continue;
            one.figure.heldPoint("Staff07", sparks, worldSparks);
            for (int j = 0; j < 10; ++j) one.figure.heldPoint("Staff07", shaft[j], worldShaft[j]);
            staffFire_.feed(worldHead, worldSparks, worldShaft);
        }
    }
    // The Saint Crossbow's, the Grand Soul Shield's and the Dragon Spear's lights wherever one
    // is held in hand (fx/held_lights.h).
    heldLights_.update(seconds);
    for (const HeldLights::Item item : HeldLights::kItems) {
        float local[HeldLights::kMostPoints][3], world[HeldLights::kMostPoints][3];
        const int count = HeldLights::points(item, local);
        for (Drawn& one : drawn_) {
            if (!one.visible || !one.placed) continue;
            int at = 0;
            while (at < count && one.figure.heldPoint(HeldLights::mesh(item), local[at], world[at]))
                ++at;
            if (at == count) heldLights_.feed(item, world, count);
        }
    }
    // The motes off a worn wing's tips, his and the envoy's (fx/wing_motes.h), at last frame's
    // pose, as the staff's points are.
    wingMotes_.update(seconds);
    {
        float tips[WingMotes::kMostTips][3];
        const Drawn* hero = drawnOf(realm_.hero().id);
        if (wing_.worn() && hero && hero->visible) {
            const int count = wing_.tips(tips, WingMotes::kMostTips);
            wingMotes_.feed(wing_.worn()->name, realm_.hero().flying, tips, count);
        }
        for (const Standing& one : folk_) {
            if (!one.wing.worn()) continue;
            const int count = one.wing.tips(tips, WingMotes::kMostTips);
            wingMotes_.feed(one.wing.worn()->name, false, tips, count);
        }
    }
    const float frames = seconds * 25.0f;
    // Tarkan's eye trails: MoveEye runs in MoveCharacterVisual for as long as the body is drawn,
    // its fall included, and the joints live as long as it does (fx/eye_trails.h).
    for (Drawn& one : drawn_) {
        if (one.trailBones[0] < 0 || one.trailBones[1] < 0 || !one.visible || !one.placed) {
            continue;
        }
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        float at[3];
        if (one.figure.pointOn(one.trailBones[0], origin, at)) eyeTrails_.feed(one.id, false, at);
        if (one.figure.pointOn(one.trailBones[1], origin, at)) eyeTrails_.feed(one.id, true, at);
    }
    // The plain Beam Knight's hand flames, one a claw each reference frame while it is drawn.
    for (Drawn& one : drawn_) {
        if (one.handFlameBones[3] < 0) continue;
        if (!one.visible || !one.placed) {
            one.handFlameOwed = 0.0f;
            continue;
        }
        one.handFlameOwed += frames;
        while (one.handFlameOwed >= 1.0f) {
            one.handFlameOwed -= 1.0f;
            const float origin[3] = {0.0f, 0.0f, 0.0f};
            for (int h = 0; h < 4; h += 2) {
                float root[3], claw[3];
                if (!one.figure.pointOn(one.handFlameBones[h], origin, root) ||
                    !one.figure.pointOn(one.handFlameBones[h + 1], origin, claw)) {
                    continue;
                }
                float along[3] = {claw[0] - root[0], claw[1] - root[1], claw[2] - root[2]};
                const float n =
                    std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
                if (n < 1e-5f) continue;
                for (float& a : along) a /= n;
                bodyFlames_.handFlame(claw, along, kHandFlameScale);
            }
        }
    }
    // The Zaikan's foot fires and the Death Beam Knight's burning body, each reference frame it
    // lives (MoveCharacterVisual, gated on c->Dead as the dragon's dust is).
    for (Drawn& one : drawn_) {
        if (one.footFireBones[1] < 0 && !one.burns) continue;
        const sim::Body* body = realm_.find(one.id);
        if (!body || !body->alive() || !one.visible || !one.placed) {
            one.burnOwed = 0.0f;
            continue;
        }
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        one.burnOwed += frames;
        while (one.burnOwed >= 1.0f) {
            one.burnOwed -= 1.0f;
            ++one.burnFrame;
            if (one.footFireBones[1] >= 0) {
                const float along[2] = {std::sin(one.yaw), std::cos(one.yaw)};
                for (int f = 0; f < 2; ++f) {
                    float at[3];
                    if (one.figure.pointOn(one.footFireBones[f], origin, at)) {
                        breath_.footFire(at, along);
                    }
                }
            }
            if (!one.burns) continue;
            float points[35][3];
            bool posed = true;
            for (int i = 0; i < 35 && posed; ++i) {
                posed = one.figure.pointOn(one.burnBones[i], origin, points[i]);
            }
            if (!posed) continue;
            for (const BurnSegment& s : kBurnSegments) {
                bodyFlames_.segmentFlame(points[s.from], points[s.to], s.scale, kBurnLight);
            }
            if (one.burnFrame % 2 == 0) {
                bodyFlames_.segmentFlame(points[kBurnBody[0]], points[kBurnBody[1]], 1.3f,
                                         kBurnLight);
                bodyFlames_.stillFlame(points[kBurnHead], 0.5f, kBurnLight);
            }
        }
    }
    // A monster an Immolate rune set burning: flames along bones drawn at random each reference
    // frame while it burns (kEmberFlames).
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        const FigureBody* look = one.figure.body();
        if (!body || !body->alive() || body->burnUntil <= realm_.tick() || !one.visible ||
            !one.placed || look == nullptr || look->boneCount() < 2) {
            one.burningOwed = 0.0f;
            continue;
        }
        if (one.burningDice == 0) one.burningDice = 0x9e3779b9u ^ (one.id * 2654435761u);
        const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        one.burningOwed += frames;
        while (one.burningOwed >= 1.0f) {
            one.burningOwed -= 1.0f;
            for (int f = 0; f < kEmberFlames; ++f) {
                one.burningDice ^= one.burningDice << 13;
                one.burningDice ^= one.burningDice >> 17;
                one.burningDice ^= one.burningDice << 5;
                const int b = 1 + int(one.burningDice % uint32_t(bones.size() - 1));
                const int parent = bones[size_t(b)].parent;
                if (parent < 0) continue;
                float from[3], to[3];
                if (one.figure.pointOn(parent, origin, from) && one.figure.pointOn(b, origin, to)) {
                    bodyFlames_.segmentFlame(from, to, kEmberScale, kEmberLight);
                }
            }
        }
    }
    // A poisoned monster: green smoke off bones drawn at random while the poison lasts.
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        const FigureBody* look = one.figure.body();
        if (!body || !body->alive() || body->player || body->poisonUntil <= realm_.tick() ||
            !one.visible || !one.placed || look == nullptr || look->boneCount() < 2) {
            one.sickOwed = 0.0f;
            continue;
        }
        if (one.burningDice == 0) one.burningDice = 0x9e3779b9u ^ (one.id * 2654435761u);
        const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        one.sickOwed += frames * kSickPuffs;
        while (one.sickOwed >= 1.0f) {
            one.sickOwed -= 1.0f;
            one.burningDice ^= one.burningDice << 13;
            one.burningDice ^= one.burningDice >> 17;
            one.burningDice ^= one.burningDice << 5;
            const int b = int(one.burningDice % uint32_t(bones.size()));
            float at[3];
            if (one.figure.pointOn(b, origin, at)) {
                poison_.sicken(at, std::clamp(look->height * look->scale / 1.8f, 0.7f, 2.0f));
            }
        }
    }
    for (Drawn& one : drawn_) {
        if (one.snortBone < 0 && one.eyeBones[0] < 0) continue;
        if (!one.visible || !one.placed) {
            one.snortOwed = 0.0f;
            continue;
        }
        // RenderEye sits in the render switch, so it lasts as long as the body is drawn,
        // through the fall and the fade.
        if (one.eyeBones[0] >= 0 && one.eyeBones[1] >= 0) {
            for (int e = 0; e < 2; ++e) {
                float at[3];
                if (one.figure.pointOn(one.eyeBones[e], kEyeAt[e], at)) eyes_.feed(at, one.eyeSize);
            }
        }
        if (one.snortBone < 0) continue;
        const int slot = slotOf(one.figure);
        const float key = keyOf(one.figure);
        bool open = one.snortAlways;
        for (const SnortWindow& w : kSnortWindows) {
            if (slot == w.slot && key >= w.from && key <= w.to) open = true;
        }
        if (!open) {
            one.snortOwed = 0.0f;
            continue;
        }
        // rand_fps_check(2): one every second reference frame; the Yeti's breath is (4).
        one.snortOwed += frames / (one.snortAlways ? 4.0f : 2.0f);
        while (one.snortOwed >= 1.0f) {
            one.snortOwed -= 1.0f;
            float at[3];
            if (!one.figure.pointOn(one.snortBone, one.snortAlways ? kYetiBreathAt : kSnortAt, at)) break;
            snort_.puff(at);
        }
    }
}

// The Shadows: a star on each of their joints every frame the body is drawn, through the fall
// and the fade as RenderCharacter draws it. fx/shadow_stars.h has the client's code; MU's sparks
// while it swings are left out, ours, for the user's "still to active".
void Play::shade(float seconds) {
    shadowStars_.update(seconds);
    for (Drawn& one : drawn_) {
        if ((one.shadeBones.empty() && one.auraBone < 0 && !one.embers && one.starBones.empty()) ||
            !one.visible || !one.placed) {
            continue;
        }
        float fade = 1.0f;
        if (one.deadFor >= kDeathHold) {
            const float t = std::clamp((one.deadFor - kDeathHold) / kDeathFade, 0.0f, 1.0f);
            fade = 1.0f - t * t * (3.0f - 2.0f * t);
        } else if (one.spawnFade < kSpawnFadeSeconds) {
            const float t = std::clamp(one.spawnFade / kSpawnFadeSeconds, 0.0f, 1.0f);
            fade = t * t * (3.0f - 2.0f * t);
        }
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        // Its faint light, where its effect is (kAuraLights).
        if (one.auraBone >= 0) {
            float at[3];
            if (one.figure.pointOn(one.auraBone, origin, at)) {
                shadowStars_.glow(at, fade, one.auraColour);
                // The Hydra's gem, its aura bone (kHydraFigure): MU's two pulsing sprites on it.
                if (one.beams == Drawn::Beams::Horn) {
                    shadowStars_.flare(at, fade, std::sin(folkClock_ * 2.0f) * 0.3f + 0.7f);
                }
            }
        }
        // An Alquamos's stars, a light on each, its luminosity MU's 0.7-1 as a slow swing of its
        // own rather than a roll a frame (ours: rolled, nine lights flickered).
        if (!one.starBones.empty()) {
            const float luminosity = 0.85f + 0.15f * std::sin(folkClock_ * 3.0f + float(one.id));
            for (const int bone : one.starBones) {
                float at[3];
                if (one.figure.pointOn(bone, origin, at)) {
                    shadowStars_.starlight(at, fade, luminosity, one.starTint);
                }
            }
        }
        // A Queen Rainer's or a Drakan's crackle along its bones (kQueenArcs, kDrakanArcs): a
        // fresh few each reference frame.
        if (!one.arcBones.empty() && one.deadFor < 0.0f) {
            const int pairs = int(one.arcBones.size() / 2);
            one.arcStep -= seconds * 25.0f;
            if (one.arcStep <= 0.0f) {
                one.arcStep += 1.0f;
                if (one.arcStep <= 0.0f) one.arcStep = 1.0f;
                one.arcLit = 0;
                for (int k = 0; k < one.arcsAFrame; ++k) {
                    wanderDice_ ^= wanderDice_ << 13;
                    wanderDice_ ^= wanderDice_ >> 17;
                    wanderDice_ ^= wanderDice_ << 5;
                    one.arcLit |= 1u << (wanderDice_ % uint32_t(pairs));
                }
            }
            float colour[3];
            for (int k = 0; k < 3; ++k) colour[k] = kQueenArcColour[k] * kQueenArcDim * fade;
            for (int p = 0; p < pairs; ++p) {
                if (!(one.arcLit & (1u << p))) continue;
                float a[3], b[3];
                if (one.figure.pointOn(one.arcBones[size_t(2 * p)], origin, a) &&
                    one.figure.pointOn(one.arcBones[size_t(2 * p + 1)], origin, b)) {
                    shadowStars_.thunderBeam(a, b, one.arcHalf, colour);
                }
            }
        }
        // A Death Gorgon: an ember now and then off a joint picked at random.
        if (one.embers) {
            if (one.deadFor >= 0.0f) continue;
            one.emberOwed += seconds * 25.0f / one.emberEvery;
            while (one.emberOwed >= 1.0f) {
                one.emberOwed -= 1.0f;
                wanderDice_ ^= wanderDice_ << 13;
                wanderDice_ ^= wanderDice_ >> 17;
                wanderDice_ ^= wanderDice_ << 5;
                const int bone = one.emberBone >= 0 ? one.emberBone
                                 : one.shadeBones.empty()
                                     ? -1
                                     : one.shadeBones[wanderDice_ % uint32_t(one.shadeBones.size())];
                if (bone < 0) break;
                float at[3];
                if (one.figure.pointOn(bone, origin, at)) shadowStars_.ember(at);
            }
            continue;
        }
        for (const int bone : one.shadeBones) {
            float at[3];
            if (one.figure.pointOn(bone, origin, at)) shadowStars_.star(at, one.shadePoison, fade);
        }
    }
    // An Alquamos blow's ribbons: MoveJoint's BITMAP_FLARE sub 7 (ZzzEffectJoint.cpp:5603-5697),
    // in MU's units and axes (x east, y north, z up) about the target's feet, a frame at a time.
    for (StarRibbon& one : starRibbons_) {
        const Drawn* target = drawnOf(one.target);
        if (target == nullptr || !target->placed) {
            one.life = 0.0f;
            continue;
        }
        one.owed += seconds * 25.0f;
        while (one.owed >= 1.0f && one.life > 0.0f) {
            one.owed -= 1.0f;
            one.life -= 1.0f;
            const int frame = int(folkClock_ * 25.0f);
            const float fi = float(((one.index % 2) ? frame : -frame) + one.index * 53731);
            const float t0 = std::sin((fi + 55555.0f) * 0.048f) * std::cos(fi * 0.0613f);
            const float t1 = std::sin((fi + 55555.0f) * 0.048f) * std::sin(fi * 0.0613f);
            const float t2 = std::cos((fi + 55555.0f) * 0.048f);
            const float sinAdd = std::sin((fi + 11111.0f) * 0.1113f);
            const float cosAdd = std::cos((fi + 11111.0f) * 0.1113f);
            const float dir[3] = {cosAdd * t1 - sinAdd * t2, sinAdd * t1 + cosAdd * t2, t0};
            const float life = one.life * 40.0f / 30.0f;
            float pos = life < 10.0f ? life * 7.0f : life + 60.0f;
            pos = pos / float(30 + one.multi) * 30.0f;
            const float circle = std::min(std::max(0.0f, 40.0f - life) * 15.0f, 150.0f);
            // The target's feet in MU's frame, in units.
            const float floor =
                ground_ ? ground_->heightAt(target->crown[0], target->crown[2]) : target->crown[1];
            const float feet[3] = {target->crown[0] * 100.0f, -target->crown[2] * 100.0f,
                                   floor * 100.0f};
            float mu[3];
            for (int k = 0; k < 3; ++k) {
                const float swing =
                    25.0f * std::cos(float(one.index * 51231 + k * 3711 + frame / 10) * 0.01f);
                const float last = (100.0f - pos) * (feet[k] + swing);
                mu[k] = (pos * (feet[k] + dir[k] * circle) + last) * 0.01f;
            }
            mu[2] += 100.0f;
            for (int t = std::min(one.tails, 14); t > 0; --t)
                for (int k = 0; k < 3; ++k) one.trail[t][k] = one.trail[t - 1][k];
            one.trail[0][0] = mu[0] * 0.01f;
            one.trail[0][1] = mu[2] * 0.01f;
            one.trail[0][2] = -mu[1] * 0.01f;
            one.tails = std::min(one.tails + 1, 15);
        }
        if (one.life > 0.0f) shadowStars_.ribbon(one.trail, one.tails, 1.0f);
    }
    starRibbons_.erase(std::remove_if(starRibbons_.begin(), starRibbons_.end(),
                                      [](const StarRibbon& r) { return r.life <= 0.0f; }),
                       starRibbons_.end());
    // The Devil's beams, a hand each to the middle of whoever it swung at, while they last.
    for (IceCast& cast : laserCasts_) {
        if (cast.delay > 0.0f) {
            cast.delay -= seconds;
            continue;
        }
        cast.wait -= seconds;
        const Drawn* devil = drawnOf(cast.caster);
        const Drawn* target = drawnOf(cast.target);
        if (devil == nullptr || target == nullptr || !devil->placed || !target->placed) continue;
        const float to[3] = {target->crown[0], target->crown[1] - 0.7f, target->crown[2]};
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        // The Dark Phoenix's: its bolt in its own fire (kPhoenixBoltColour), one broad and three
        // thin from the middle of the bird into the middle of the body, with Lightning's
        // eThunder and a warm light where it bites in place of Lightning's blue. Ours.
        if (devil->phoenix) {
            const FigureBody* bird = devil->figure.body();
            const float high = bird ? bird->height * bird->scale : 2.0f;
            const float from[3] = {devil->crown[0], devil->crown[1] - high * 0.45f,
                                   devil->crown[2]};
            if (!cast.heard) {
                cast.heard = true;
                if (heard_.boltThunder >= 0) emit(heard_.boltThunder, from[0], from[2]);
            }
            const FigureBody* aim = target->figure.body();
            const float tall = aim ? aim->height * aim->scale : 1.4f;
            const float chest[3] = {target->crown[0], target->crown[1] - tall * 0.5f,
                                    target->crown[2]};
            for (int bolt = 0; bolt < 4; ++bolt) {
                float end[3] = {chest[0], chest[1], chest[2]};
                for (int i = 0; i < 3 && bolt > 0; ++i) {
                    wanderDice_ = wanderDice_ * 1664525u + 1013904223u;
                    end[i] += (float((wanderDice_ >> 8) % 1000) / 500.0f - 1.0f) * kLizardBoltWander;
                }
                shadowStars_.thunderBeam(from, end, kLizardBoltHalf[bolt == 0 ? 0 : 1],
                                         kPhoenixBoltColour);
            }
            shadowStars_.glow(chest, 1.0f, kPhoenixBoltColour);
            if (!cast.smoked) {
                cast.smoked = true;
                shadowStars_.wisp(chest);
            }
            continue;
        }
        // A Hydra's: the Lizard King's red lightning, one broad bolt and three thin, from the
        // mouth of the head it left (kHydraHeads) into the middle of the body struck, with
        // Lightning's eThunder (the user, 2026-10-04: 'from heads we shoot red lightiing not
        // lasers?', 'we need to play lighting sound'); the Hydra's own voice stays MuMain's
        // ('use default hydra sounds from MuMain'). Ours, in place of MU's beams.
        if (devil->beams == Drawn::Beams::Horn) {
            const int head = devil->headBones[size_t(cast.head) % 4];
            float from[3];
            if (head < 0 || !devil->figure.pointOnBind(head, kHydraHeads[size_t(cast.head) % 4].mouth, from)) {
                continue;
            }
            if (!cast.heard) {
                cast.heard = true;
                if (heard_.boltThunder >= 0) emit(heard_.boltThunder, from[0], from[2]);
            }
            const FigureBody* aim = target->figure.body();
            const float tall = aim ? aim->height * aim->scale : 1.4f;
            const float chest[3] = {target->crown[0], target->crown[1] - tall * 0.5f,
                                    target->crown[2]};
            for (int bolt = 0; bolt < 4; ++bolt) {
                float end[3] = {chest[0], chest[1], chest[2]};
                for (int i = 0; i < 3; ++i) {
                    if (bolt == 0) break;
                    wanderDice_ = wanderDice_ * 1664525u + 1013904223u;
                    end[i] += (float((wanderDice_ >> 8) % 1000) / 500.0f - 1.0f) * kLizardBoltWander;
                }
                shadowStars_.thunderBeam(from, end, kLizardBoltHalf[bolt == 0 ? 0 : 1],
                                         kLizardBoltColour);
            }
            if (!cast.smoked) {
                cast.smoked = true;
                shadowStars_.wisp(chest);
            }
            continue;
        }
        // MU's four, two a hand: the first to the middle, the second wandering round it, and
        // now and then an ember off the hand (kDevilBeamWander, kDevilFireEveryFrames).
        const bool fire = (wanderDice_ = wanderDice_ * 1664525u + 1013904223u) % 1000 <
                          uint32_t(1000.0f * std::min(1.0f, seconds * 25.0f / kDevilFireEveryFrames));
        // The Lizard King's: off the head of the staff it holds, not its hands (the user,
        // 2026-10-04: 'lughtiing has to happen from lizrard staff not hands') -- ours; MU lets
        // them off its two link bones. One broad bolt to the middle of the body struck and three
        // thin ones wandering a little round it, the broad and the thin MU's Scale 50 and 10.
        // Into the middle of the body, as an arrow is aimed (volleyShot): half its own height
        // under its crown, so they meet the chest of whoever it is ('lughtinig has to connect
        // better with character body').
        if (devil->beams == Drawn::Beams::Thunder) {
            float head[3], sparks[3], shaft[10][3];
            StaffFire::points(head, sparks, shaft);
            float from[3];
            if (!devil->figure.heldPoint(kLizardStaff, head, from)) continue;
            // And Lightning's eThunder with it, once a bolt (the user, 2026-10-04: 'lizard is
            // missing lightiing sound from lighting'), as the Hydra's heads have. Ours.
            if (!cast.heard) {
                cast.heard = true;
                if (heard_.boltThunder >= 0) emit(heard_.boltThunder, from[0], from[2]);
            }
            const FigureBody* aim = target->figure.body();
            const float tall = aim ? aim->height * aim->scale : 1.4f;
            const float chest[3] = {target->crown[0], target->crown[1] - tall * 0.5f,
                                    target->crown[2]};
            // And it clings to the staff (the user, 2026-10-04: 'lighting has to stick on lizard
            // weapon'): two thin bolts crackling up the shaft from the grip to the head, each
            // frame a little off the line, so the weapon is seen to carry the charge it lets go.
            const float grip[3] = {0.0f, 0.0f, kLizardShaftFrom};
            float low[3];
            if (devil->figure.heldPoint(kLizardStaff, grip, low)) {
                for (int arc = 0; arc < 2; ++arc) {
                    float mid[3];
                    for (int i = 0; i < 3; ++i) {
                        wanderDice_ = wanderDice_ * 1664525u + 1013904223u;
                        mid[i] = 0.5f * (low[i] + from[i]) +
                                 (float((wanderDice_ >> 8) % 1000) / 500.0f - 1.0f) * kLizardShaftWander;
                    }
                    shadowStars_.thunderBeam(low, mid, kLizardBoltHalf[1], kLizardBoltColour);
                    shadowStars_.thunderBeam(mid, from, kLizardBoltHalf[1], kLizardBoltColour);
                }
            }
            for (int bolt = 0; bolt < 4; ++bolt) {
                float end[3] = {chest[0], chest[1], chest[2]};
                for (int i = 0; bolt > 0 && i < 3; ++i) {
                    wanderDice_ = wanderDice_ * 1664525u + 1013904223u;
                    end[i] += (float((wanderDice_ >> 8) % 1000) / 500.0f - 1.0f) * kLizardBoltWander;
                }
                shadowStars_.thunderBeam(from, end, kLizardBoltHalf[bolt == 0 ? 0 : 1],
                                         kLizardBoltColour);
            }
            if (!cast.smoked) {
                cast.smoked = true;
                shadowStars_.wisp(chest);
            }
            continue;
        }
        // The Devil's Lightning, drawn as its lasers: its wisp where they meet him, once.
        if (devil->beams == Drawn::Beams::Laser && !cast.smoked) {
            cast.smoked = true;
            shadowStars_.wisp(to);
        }
        for (const int hand : devil->handBones) {
            float from[3];
            if (hand < 0 || !devil->figure.pointOn(hand, origin, from)) continue;
            // The Vepar's: its two blurred joints a hand, both straight to the target.
            if (devil->beams == Drawn::Beams::Blur) {
                for (const float half : kVeparBeamHalf) shadowStars_.blurBeam(from, to, half);
                continue;
            }
            shadowStars_.beam(from, to);
            float off[3] = {to[0], to[1], to[2]};
            for (int i = 0; i < 3; ++i) {
                wanderDice_ = wanderDice_ * 1664525u + 1013904223u;
                off[i] += (float((wanderDice_ >> 8) % 1000) / 500.0f - 1.0f) * kDevilBeamWander;
            }
            shadowStars_.beam(from, off);
            if (fire) shadowStars_.ember(from);
        }
    }
    laserCasts_.erase(std::remove_if(laserCasts_.begin(), laserCasts_.end(),
                                     [](const IceCast& one) { return one.wait <= 0.0f; }),
                      laserCasts_.end());
    // The Balrog's meteor storm (play.cpp's Flame of Evil): a meteor at random within MU's 512
    // units of where it stood, every kBalrogStormEvery, eMeteorite on every other.
    for (MeteorStorm& storm : storms_) {
        storm.left -= seconds;
        storm.next -= seconds;
        while (storm.next <= 0.0f && storm.left > 0.0f) {
            storm.next += kBalrogStormEvery;
            handDice_ ^= handDice_ << 13;
            handDice_ ^= handDice_ >> 17;
            handDice_ ^= handDice_ << 5;
            if (storm.comets) {
                // The Death Beam Knight's: a comet within 400 units, every frame of its blow.
                storm.next += kBlastRainEvery - kBalrogStormEvery;
                const float x =
                    storm.x + (float(handDice_ % 800) * 0.01f - kBlastRainReach);
                const float z =
                    storm.z + (float((handDice_ >> 10) % 800) * 0.01f - kBlastRainReach);
                comet_.cast(x, z, storm.who, 1.0f, kBlastRainFall);
                continue;
            }
            const float x = storm.x + (float(handDice_ % 1024) - 512.0f) * 0.01f;
            const float z = storm.z + (float((handDice_ >> 10) % 1024) - 512.0f) * 0.01f;
            meteor_.cast(x, z, 0);
            if ((handDice_ >> 20) & 1 && heard_.meteorite >= 0) emit(heard_.meteorite, x, z);
        }
    }
    // A Tarkan boss's ring of staffs, at its fourteenth frame (fx/staff_ring.h).
    for (StaffDue& due : staffsDue_) {
        due.wait -= seconds;
        if (due.wait <= 0.0f) staffRing_.cast(due.at, due.yaw);
    }
    staffsDue_.erase(std::remove_if(staffsDue_.begin(), staffsDue_.end(),
                                    [](const StaffDue& one) { return one.wait <= 0.0f; }),
                     staffsDue_.end());
    // A Crust's Inferno, at its CheckAttackTime(5) (kInfernoBlows' wait).
    for (InfernoDue& due : infernosDue_) {
        due.wait -= seconds;
        if (due.wait <= 0.0f) throwInferno(due.feet, due.yaw, due.bombs, due.mesh);
    }
    infernosDue_.erase(std::remove_if(infernosDue_.begin(), infernosDue_.end(),
                                      [](const InfernoDue& one) { return one.wait <= 0.0f; }),
                       infernosDue_.end());
    // A Phantom Knight's spirits, at its boss blow's CheckAttackTime(14).
    for (SpiritDue& due : spiritsDue_) {
        due.wait -= seconds;
        if (due.wait > 0.0f || ground_ == nullptr) continue;
        if (const Drawn* knight = drawnOf(due.who); knight != nullptr && knight->placed) {
            const float feet[3] = {knight->crown[0],
                                   ground_->heightAt(knight->crown[0], knight->crown[2]),
                                   knight->crown[2]};
            spirits_.release(due.who, feet);
        }
    }
    spiritsDue_.erase(std::remove_if(spiritsDue_.begin(), spiritsDue_.end(),
                                     [](const SpiritDue& one) { return one.wait <= 0.0f; }),
                      spiritsDue_.end());
    // The fight's coins, once its Zen has stopped coming (takeZen), with the lane's sum.
    if (zenQuiet_ > 0.0f && !zenOwed_) {
        zenQuiet_ -= seconds;
        if (zenQuiet_ <= 0.0f) {
            zenQuiet_ = 0.0f;
            const Drawn* hero = drawnOf(realm_.hero().id);
            if (heard_.moneyDrop >= 0 && hero && hero->placed) {
                emit(heard_.moneyDrop, hero->crown[0], hero->crown[2]);
            }
        }
    }
    // A spread rain's rocks, each starting its fall when its wait is up, where the body is now.
    for (RockDue& due : rocksDue_) {
        due.wait -= seconds;
        if (due.wait > 0.0f) continue;
        float x = due.x, z = due.z;
        if (const Drawn* on = drawnOf(due.whom); on != nullptr && on->placed) {
            x = on->crown[0];
            z = on->crown[2];
        }
        if (due.comet) {
            const sim::SkillRow* row = sim::skillNumbered(sim::skill::kCometfall);
            comet_.cast(x, z, due.who, due.weight,
                        float(row != nullptr ? row->fallTicks : 0) * float(kTickSeconds));
        } else {
            meteor_.cast(x, z, due.who, due.weight);
        }
    }
    rocksDue_.erase(std::remove_if(rocksDue_.begin(), rocksDue_.end(),
                                   [](const RockDue& one) { return one.wait <= 0.0f; }),
                    rocksDue_.end());
    storms_.erase(std::remove_if(storms_.begin(), storms_.end(),
                                 [](const MeteorStorm& one) { return one.left <= 0.0f; }),
                  storms_.end());
}

// Hanzo at his anvil: MU's sparks off the hammer's head while the blow is between keys 5 and
// 6, and the hearth's smoke and embers while he stands there, which are ours. fx/forge.h.
void Play::smithy(float seconds) {
    forge_.update(seconds);
    const float frames = seconds * 25.0f;
    for (Standing& one : folk_) {
        if (!one.smith) continue;
        float hearth[3];
        one.figure.pointInModel(kHearth, hearth);
        forge_.smoulder(hearth, seconds, one.hearthOwed);
        // rand_fps_check(1) inside the window: a burst every reference frame of it. Read off
        // his own clip, as the hammer's ring is, and only on the blow -- action 0.
        const float key = keyOf(one.figure);
        if (slotOf(one.figure) != 0 || key < kSparksFrom || key > kSparksTo) {
            one.striking = false;
            continue;
        }
        // The first frame inside pays one burst at once, so a blow that crosses the key in a
        // single long frame still throws; every frame after owes its own share.
        one.sparksOwed = one.striking ? one.sparksOwed + frames : 1.0f;
        one.striking = true;
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        float head[3];
        if (!one.figure.pointOn(kSparkBone, origin, head)) continue;
        while (one.sparksOwed >= 1.0f) {
            one.sparksOwed -= 1.0f;
            forge_.strike(head);
        }
    }
}

// MU's `o->Weapon = c->Weapon[0].Type - MODEL_SWORD`: the right hand's, or with that hand empty
// whatever weapon he holds. A polearm flies wider and smaller -- MU's group 3.
void Play::throwWheel(const Drawn& swinger, const sim::Body* body) {
    const HeldItem* weapon = nullptr;
    if (const FigureBody* look = swinger.figure.body()) {
        for (const HeldItem& held : look->held) {
            if (held.kind != "weapon" || held.mesh == nullptr) continue;
            if (weapon == nullptr || inRightHand(held)) weapon = &held;
            if (inRightHand(held)) break;
        }
    }
    const bool polearm = body != nullptr && body->weapon >= 0 &&
                         size_t(body->weapon) < tables_.arms.size() &&
                         tables_.arms[size_t(body->weapon)].group == 3;
    wheel_.cast(swinger.id, weapon ? weapon->mesh : nullptr, weapon ? weapon->shine : ShineLook{},
                polearm);
}

// The same weapon as the wheel's, thrown up off his hand a key into the clip and brought down a
// metre ahead of him; his hand is empty for the clip's first four keys. Both quickened with the
// clip.
void Play::throwFury(Drawn& swinger, const sim::Body* body) {
    const HeldItem* weapon = nullptr;
    if (const FigureBody* look = swinger.figure.body()) {
        for (const HeldItem& held : look->held) {
            if (held.kind != "weapon" || held.mesh == nullptr) continue;
            if (weapon == nullptr || inRightHand(held)) weapon = &held;
            if (inRightHand(held)) break;
        }
    }
    const bool polearm = body != nullptr && body->weapon >= 0 &&
                         size_t(body->weapon) < tables_.arms.size() &&
                         tables_.arms[size_t(body->weapon)].group == 3;
    const float pace = std::max(swinger.swingPace, 0.01f);
    const float feet[3] = {swinger.crown[0],
                           ground_ ? ground_->heightAt(swinger.crown[0], swinger.crown[2]) : 0.0f,
                           swinger.crown[2]};
    fury_.cast(swinger.id, feet, swinger.yaw, Fury::kKeySeconds / pace,
               weapon ? weapon->mesh : nullptr, weapon ? weapon->shine : ShineLook{}, polearm);
    swinger.handEmpty = Fury::kEmptyKeys * Fury::kKeySeconds / pace;
}

void Play::lightForges(Lamps& lamps) const {
    for (const Standing& one : folk_) {
        if (!one.smith) continue;
        gfx::PointLight light;
        one.figure.pointInModel(kHearth, light.position);
        for (int i = 0; i < 3; ++i) light.colour[i] = kForgeColour[i];
        light.reach = kForgeReach;
        light.height = ground_ ? std::max(0.0f, light.position[1] -
                                                    ground_->heightAt(light.position[0],
                                                                      light.position[2]))
                               : 0.0f;
        lamps.add(light, kForgeLow, kForgeHigh, kForgeHz, kForgeSmooth);
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

// A skill read off an orb or a scroll: the smoke and its sound on the same frame.
//
// Written as `rise`'s twin and calling the two in the same order for the same reason -- the
// picture and the wave have to start together or the stabs arrive over smoke that has
// already bloomed. Sound::play takes the rest of the sync, the file's own lead and the
// device's buffer. The puff's timings are cut to this file; see Aura::learn.
void Play::learned() {
    const Drawn* hero = drawnOf(realm_.hero().id);
    if (hero == nullptr || !hero->placed || ground_ == nullptr) return;
    const float feet[3] = {hero->crown[0], ground_->heightAt(hero->crown[0], hero->crown[2]),
                           hero->crown[2]};
    aura_.learn(feet, ground_->metresPerTile());
    sound_.play(heard_.orb);
}

// Where the realm put him, and drawn there from this frame on. `o->Alpha = 0.f` is the spawn
// fade a revive uses.
void Play::warped() {
    warpOwed_ = true;
    // INVENTION: MuMain lands a Town Portal in silence -- TryConsumeItem's scroll branch and
    // ReceiveTeleport's warp branch play nothing. What is borrowed is SOUND_MAGIC, sMagic.wav,
    // which CreateTeleportEnd plays as a wizard's Teleport puts him down (ZzzEffectMagicSkill.cpp:
    // 171): MU's own sound for arriving somewhere by magic. The user's, 2026-09-27.
    sound_.play(heard_.warp);
    const sim::Body& body = realm_.hero();
    if (Drawn* hero = drawnOf(body.id)) {
        // At the gate from THIS frame, not the next tick's: the realm moved him between ticks,
        // and until a step ran the drawing (and the camera on it) stayed where he read the
        // scroll, then jumped. Stood as a revive is -- idle cut in, nothing of the swing left.
        hero->nowX = body.x;
        hero->nowY = body.y;
        hero->nowFacing = body.facing;
        stand(*hero);
    }
    // And the ring where his last click was sending him, which he is not going to now.
    marker_.dismiss();
    if (ground_ == nullptr) return;
    const float metres = ground_->metresPerTile();
    const float x = (body.x + 0.5f) * metres;
    const float z = -(body.y + 0.5f) * metres;
    const float feet[3] = {x, ground_->heightAt(x, z), z};
    warp_.land(feet, metres);
    core::logf("warp: lands at tile %d,%d", body.column(), body.row());
}

void Play::landed() {
    sound_.play(heard_.warp);
    if (ground_ == nullptr) return;
    const sim::Body& body = realm_.hero();
    const float metres = ground_->metresPerTile();
    const float x = (body.x + 0.5f) * metres;
    const float z = -(body.y + 0.5f) * metres;
    const float feet[3] = {x, ground_->heightAt(x, z), z};
    warp_.land(feet, metres);
    core::logf("warp: comes in at tile %d,%d", body.column(), body.row());
}

// The knight's guard raised, and then kept on him while it stands -- and the wizard's Soul
// Barrier, which is the same cage on the user's word of 2026-09-28.
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

void Play::showGuard() {
    guardRise(kGuardShowSeconds);
    guardPreview_ = realm_.tick() + int64_t(kGuardShowSeconds / kTickSeconds);
}

void Play::guardStep() {
    if (ground_ == nullptr) return;
    const sim::Body& hero = realm_.hero();
    // A `--guard` preview has no boon behind it, and is held for its own two seconds instead.
    if (realm_.tick() < guardPreview_) {
        if (const Drawn* drawn = drawnOf(hero.id); drawn != nullptr && drawn->placed) {
            const float feet[3] = {drawn->crown[0],
                                   ground_->heightAt(drawn->crown[0], drawn->crown[2]),
                                   drawn->crown[2]};
            aura_.follow(feet);
        }
        return;
    }
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
                                   // Zen skips the drop's beat: it is the fight's sum, owed
                                   // the moment the body starts to fall.
                                   else let = one.zen > 0 || dropper->deadFor >= kDropDelay;
                                   if (let && one.zen > 0) takeZen(one.zen);
                                   else if (let) landed(one.drop);
                                   return let;
                               }),
                held_.end());
    heldIds_.clear();
    zenOwed_ = false;
    for (const HeldDrop& one : held_) {
        if (one.zen > 0) zenOwed_ = true;
        if (one.zen == 0) heldIds_.push_back(one.drop);
    }
    for (const CrackerOwed& owed : crackerOwed_) {
        if (owed.drop != 0) heldIds_.push_back(owed.drop);
    }
}

void Play::takeZen(int64_t zen) {
    gains_.push_back({Gain::Kind::Zen, zen});
    zenQuiet_ = kZenQuietSeconds;
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
        if (dead.crumbles) bones_.rubble(x, z, ground_->heightAt(x, z), scale);
        else bones_.burst(x, z, ground_->heightAt(x, z), scale);
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
    // And "You Died" with the same key, for the same reason: the realm resolved his death on
    // the tick, but the blow that did it lands on screen up to half a swing later, and the
    // message read as arriving before the fall. See the note in Play::update where the other
    // gains are collected.
    // And the user's bell, flat on the player: not MU's, which has only the scream.
    if (dead.id == realm_.hero().id) {
        gains_.push_back({Gain::Kind::Died, 0});
        sound_.play(heard_.deathBell);
    }
}

void Play::fallWhenLanded() {
    for (Drawn& one : drawn_) {
        // What Nova killed falls with the burst, when he comes down (novaOwed_).
        if (one.novaKilled && novaOwed_.by != 0) continue;
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

float Play::heroRisesIn() const {
    const sim::Body& hero = realm_.hero();
    if (hero.alive()) return -1.0f;
    const double left = double(hero.risesAt - realm_.tick()) * kTickSeconds - accumulator_;
    return float(std::max(0.0, left));
}

int Play::walkers(float* out, int most) const {
    if (!ground_ || !out || most <= 0) return 0;
    int written = 0;
    // Two passes, so the hero is first whatever order drawn_ keeps him in: the field parts
    // round eight bodies at most, and if a frame draws more than that the ones left out
    // should be the crowd's, never his.
    for (int pass = 0; pass < 2 && written < most; ++pass) {
        for (const Drawn& one : drawn_) {
            if (written >= most) break;
            const bool hero = one.id == realm_.hero().id;
            if ((pass == 0) != hero) continue;
            if (!one.placed || !one.visible || one.deadFor >= 0.0f) continue;
            // The crown is the top of the body; the feet are under it on the ground, which is
            // where the aura and the sand are put too.
            float* slot = out + written * 4;
            slot[0] = one.crown[0];
            slot[1] = ground_->heightAt(one.crown[0], one.crown[2]);
            slot[2] = one.crown[2];
            slot[3] = 0.0f;
            ++written;
        }
    }
    return written;
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
            // A player who left the world: gone from the picture, not fallen.
            if (body->gone) {
                one.visible = false;
                continue;
            }
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
        // The pace it walks at: its own speed, halved while it is iced (realm_move.cpp's
        // `kChillFactor`). Both the arrival below and the clip's rate answer to this and not to
        // the nominal speed -- read nominal, an iced walk took every tick for an arrival and
        // strode at full pace over half the ground, moonwalking.
        const float pace =
            body->speed * (body->chilledUntil > realm_.tick() ? sim::kChillFactor : 1.0f) *
            sim::strideFactor(*body);
        const float covered = one.groundSpeed * float(kTickSeconds) / metresPerTile;
        const bool jumped = covered > 2.0f;
        float through = jumped ? 1.0f : through_;
        float arrived = 1.0f;
        if (!body->walking && covered > 1e-4f && covered < pace * 0.999f) {
            arrived = covered / pace;
            through = std::min(1.0f, through_ / arrived);
        }
        // Whether the DRAWN body is covering ground at this instant. This, and not the sim's
        // `walking`, is what the walk clip answers to: the sim stops a body on a tick and the
        // drawing gets there up to a tick later.
        const bool moving = !jumped && covered > 1e-4f && through_ < arrived;
        float tileX = one.wasX + (one.nowX - one.wasX) * through;
        float tileY = one.wasY + (one.nowY - one.wasY) * through;
        // A Crust glides: MU moves its drawn body 0.07 of the way to its tile a reference frame
        // (WSclient.cpp:2026-2034, ZzzCharacter.cpp:3145-3190), so each step eases in and out
        // of the last. Ours: a cap on the lag for MU's snap at the fifteenth frame; a jump is
        // not glided.
        if (one.glides) {
            if (one.glideX < 0.0f || jumped) {
                one.glideX = tileX;
                one.glideY = tileY;
            } else {
                const float k = 1.0f - std::pow(1.0f - kGlideShare, seconds * 25.0f);
                one.glideX += (tileX - one.glideX) * k;
                one.glideY += (tileY - one.glideY) * k;
                // Held within kGlideLag of the realm's place, as MU's snap holds it within a
                // step: on a long walk an unbounded ease trailed it by more than a tile.
                const float ox = one.glideX - tileX, oy = one.glideY - tileY;
                const float off = std::sqrt(ox * ox + oy * oy);
                if (off > kGlideLag) {
                    one.glideX = tileX + ox * (kGlideLag / off);
                    one.glideY = tileY + oy * (kGlideLag / off);
                }
            }
            tileX = one.glideX;
            tileY = one.glideY;
        }
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
        // Evil Spirit's spin on top (kSpiritStormTime): MuMain's PushingCharacter while its
        // StormTime lasts, then what turn it left eased back to the facing.
        if (one.stormTime > 0.0f) {
            const float frames = std::min(one.stormTime, seconds * kStormFramesPerSecond);
            one.spin += one.stormTime * 10.0f * frames * (3.14159265f / 180.0f);
            one.stormTime -= frames;
            if (one.stormTime <= 0.0f) one.spin = wrapped(one.spin);
        } else if (one.spin != 0.0f) {
            one.spin *= std::exp(-3.0f * seconds / kSpinSettleSeconds);
            if (std::fabs(one.spin) < 1e-3f) one.spin = 0.0f;
        }
        one.yaw += one.spin;
        // Killed by Nova: turned to him, so it goes over backwards (ReceiveDie).
        if (one.novaKilled && one.deadFor >= 0.0f) {
            one.yaw = std::atan2(-one.flingWay[0], -one.flingWay[1]);
        }

        // On the Dinorant he sits 30 over the ground and the dragon stands on it (game/pets.h).
        float lift = 0.0f;
        if (body->player && body->riding) {
            const sim::Held& worn = realm_.satchel()[sim::kMount];
            if (!worn.empty() && size_t(worn.item) < tables_.items.size() &&
                tables_.items[size_t(worn.item)].number == 3) {
                lift = flying_ ? kDinorantFlyLift : kDinorantLift;
                // On its run, his seat follows the dragon's back (dinorantBob).
                const FigureBody* seatOn = one.figure.body();
                const int riding = one.figure.clip();
                if (seatOn && riding >= 0 &&
                    (riding == seatOn->rideRunClip || riding == seatOn->rideRunArmedClip)) {
                    lift += dinorantBob(one.figure.through());
                }
            }
        }
        // The Statue of Saint is drawn 120 forward of its tile, along MU's +y -- the row, our
        // -z -- which lays it on the wedge of stone (Object13) beside it; on the ground of the
        // tile it stands on (ZzzObject.cpp:276-282).
        const bool statue =
            !body->player && size_t(body->kind) < tables_.kinds.size() &&
            sim::castleStatue(tables_.kinds[size_t(body->kind)].number);
        // And the Golden Dragon aloft, drawn up off the ground and round its circle
        // (play_raid.cpp).
        lift += raidLift(*body);
        float flyX = x, flyZ = z;
        // And flung back from him while it falls (Nova::flung).
        if (one.novaKilled && one.deadFor >= 0.0f) {
            const float gone = Nova::flung(one.deadFor * kStormFramesPerSecond, one.flingPush,
                                           one.flingPace);
            flyX += one.flingWay[0] * gone;
            flyZ += one.flingWay[1] * gone;
        }
        raidCircle(*body, &flyX, &flyZ, &one.yaw);
        const float position[3] = {flyX, ground_->heightAt(flyX, flyZ) + lift,
                                   statue ? flyZ - 1.2f * metresPerTile : flyZ};
        // The safe zone is a stance and not only a place: inside one MU carries the weapon on
        // the back and stands in the unarmed idle, and steps out of it with the weapon drawn.
        // `place` moves the weapon; the clip below is the other half of the same rule, and the
        // 0.18 s crossfade in Figure::play is what makes the change a blend rather than a cut.
        // And a guard's salute slings his weapon for its length (Play::update, Shout::Salute).
        // And the player carries it slung out of combat as well, running or standing, and draws
        // it when a fight takes him (the user, 2026-09-30; sim::kCombatTicks).
        one.stowed = std::max(0.0f, one.stowed - seconds);
        const FigureBody* dressed = one.figure.body();
        // Not while a blow is still in his hands: the realm can end the fight -- the last body
        // fallen -- at an elf's key 3.2, and her string goes at key 4; slung there, her bow
        // left her hands and her arrow the chest (Play::shootArrow).
        bool shooting = one.swinging > 0.0f;
        for (const Nocking& shot : nocking_) shooting = shooting || shot.shooter == one.id;
        const bool safe = tables_.grid.safe(body->column(), body->row()) || one.stowed > 0.0f ||
                          (body->player && body->combatUntil <= realm_.tick() && !shooting) ||
                          (dressed && dressed->slungAtRest && body->temper != sim::Temper::Fighting);
        // Under the sea a player swims off the safe zone: MU's Fly stance for Atlans, treading
        // water where he stands and swimming where he goes (ZzzCharacter.cpp:298-326, 624-630),
        // with the weapon on his back while he swims, drawn while he treads (bBindBack,
        // :15251-15254). A horse outranks it, as a wing does in MU.
        const bool swims = underwater_ && body->player && !body->riding && dressed &&
                           dressed->swimWalkClip >= 0 && dressed->swimIdleClip >= 0 &&
                           !tables_.grid.safe(body->column(), body->row());
        const bool stroking = swims && one.figure.clip() >= 0 &&
                              (one.figure.clip() == dressed->swimWalkClip ||
                               one.figure.clip() == dressed->swimRunClip);
        one.figure.place(position, one.yaw, safe || stroking);
        // A standing clip on a horse is seated (Figure::seat): a knight's paired blows, which MU
        // has no ride clip for the left hand's, and a two-handed weapon's blows. The armed stop
        // ride holds the legs. MU's own ride clips seat themselves (rideAction).
        if (body->player) {
            const FigureBody* seatLook = one.figure.body();
            const int now = one.figure.clip();
            const bool standing = seatLook && seatLook->library && now >= 0 &&
                                  size_t(now) < seatLook->library->clips.clips.size() &&
                                  !rideAction(seatLook->library->clips.clips[size_t(now)].slot);
            // A buff on a horse moves his arms alone (Drawn::selfClip).
            if (one.selfClip >= 0 && now != one.selfClip) one.selfClip = -1;
            const bool hands = body->riding && one.selfClip >= 0;
            one.figure.seat(body->riding && standing ? seatLook->rideIdleArmedClip : -1, hands);
            // And a two-handed weapon drawn on a horse holds its own standing grip over the
            // ride's seat (Figure::upper), the user: "we need also two hand weapon stance on
            // mount" -- MU's one armed ride stance, 14 and 37, is a one-handed one. Not while
            // a swing plays: the ride swings 55-57 are two-handed already. **ours**.
            const bool twoHanded = seatLook && twoHandedStance(seatLook->stance);
            one.figure.upper(body->riding && twoHanded && !safe && one.swinging <= 0.0f
                                 ? seatLook->idleClip
                                 : -1);
        }
        {
            const FigureBody* look = one.figure.body();
            one.crown[0] = position[0];
            // Seated, the head is lower by about a third: what hangs over it -- a name, a bar, a
            // line he says at the bar (realm_folk.cpp) -- comes down with it and does not float
            // over whoever stands behind him.
            constexpr float kSeated = 0.68f;
            const bool seated = body->pose == sim::Pose::Sitting;
            // **No higher than the bind box's top over the feet**: a part bound under them is
            // not his height. The Hydra's hidden beams reach 2.35 m below its feet and 15 m
            // ahead, so its box stood 5.6 m tall to a 3.3 m body and its bar hung off the top of
            // the screen (the user, 2026-10-05: "i dont see HP bar for hydra"). A body bound
            // above its feet (the Bahamut's 0.23 m) keeps its height as before.
            const float tall = std::min(look->height, look->max[1]);
            one.crown[1] = position[1] + tall * look->scale * (seated ? kSeated : 1.0f);
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
        // A cast held by the realm is the exception: a walk arriving over it is the drawing's
        // own interpolation catching up rather than a step he is taking, and cutting the skill
        // for it is what made the animation look instant. A click breaks the hold
        // (Realm::accept), and then the walk is his and the cast goes with it -- a channel's
        // or a buff's clip as well -- or it slides along the ground under him.
        // Frozen by a Frost Arrow (Realm::callDown): held in the pose the arrow found it in,
        // the clock stopped, until it thaws. The realm gives it no step and no swing for the
        // freeze, but the drawing played on whatever it had -- a swing begun before the arrow
        // landed, an idle rolling round -- and the user saw it "still doing attack". The swing
        // is dropped rather than paused, so it does not finish its blow on the thaw.
        if (!body->player && body->frozenUntil > realm_.tick()) {
            one.swinging = 0.0f;
            one.casting = 0.0f;
            one.shocked = 0.0f;
            one.clipRate = 0.0f;
            continue;
        }
        // Aloft, the dragon holds its flight clip whatever else it would play (play_raid.cpp).
        if (raidFlies(one, *body)) continue;
        one.casting = std::max(0.0f, one.casting - seconds);
        if (one.swinging > 0.0f && body->walking &&
            (one.casting <= 0.0f || realm_.tick() >= body->castUntil)) {
            one.swinging = 0.0f;
            one.casting = 0.0f;
        }
        if (one.swinging > 0.0f) {
            one.clipRate = one.swingPace;
            one.shocked = 0.0f;
            continue;
        }
        // A flinch holds the same way, and a step ends it -- a monster's chase goes on through
        // it, as MU's MONSTER01_SHOCK gives way to the walk. The hero's ends on a walk the realm
        // says he is taking, which is a click he made through it (Play::leftClick); not on the drawing
        // still reaching the tile Play::flinch halted him on, which is `moving` without `walking`.
        one.shocked = std::max(0.0f, one.shocked - seconds);
        if (one.shocked > 0.0f && moving && (!body->player || body->walking)) one.shocked = 0.0f;
        if (one.shocked > 0.0f) {
            one.clipRate = 1.0f;
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
        // And the run is a third walk, which the realm decides (sim::Body::running), turned into
        // and out of with the same phase-kept crossfade as the zone's edge.
        // And on a horse the run ride is the only gait and the stop ride the only stand, bare
        // while the weapon is slung and armed while it is drawn (sim::Body::riding, game/pets.h).
        const bool riding = body->riding && look->rideRunClip >= 0 && look->rideIdleClip >= 0;
        // On wings off a safe tile he flies (sim::Body::flying): MU's Fly stance, the stop fly
        // where he stands and the fly where he goes, never a walk or the run; the crossbow's
        // own pair (ZzzCharacter.cpp:298-326, 615-621). It outranks the sea's swim, as in MU.
        const bool flies = body->flying && !riding && look->flyClip >= 0 && look->flyIdleClip >= 0;
        const bool crossbow = look->stance == "crossbow";
        const int flyMove = crossbow && look->flyCrossbowClip >= 0 ? look->flyCrossbowClip
                                                                   : look->flyClip;
        const int flyIdle = crossbow && look->flyIdleCrossbowClip >= 0 ? look->flyIdleCrossbowClip
                                                                       : look->flyIdleClip;
        const int rideRun = safe || look->rideRunArmedClip < 0 ? look->rideRunClip
                                                               : look->rideRunArmedClip;
        int rideIdle = safe || look->rideIdleArmedClip < 0 ? look->rideIdleClip
                                                           : look->rideIdleArmedClip;
        // A bow drawn on a horse: MU's stop ride weapon is a blade's pose, the right hand
        // forward, and the bow in her left lay flat through her thigh (the user: "bow holding
        // looks incorrect"). MU has no ride stance for a bow, so **ours**: she holds the first
        // key of her ride shot, 58 or 59, the bow raised at the ready.
        const bool archer = look->stance == "bow" || look->stance == "crossbow";
        const int readyBow = riding && archer && !safe && look->library
                                 ? look->library->find(rideSlotFor(look->stance))
                                 : -1;
        if (readyBow >= 0) rideIdle = readyBow;
        // Swimming: the run swim once he runs, as MU's `Run >= 40`; the crossbow's own tread.
        const int swimMove = body->running && look->swimRunClip >= 0 ? look->swimRunClip
                                                                     : look->swimWalkClip;
        const int swimIdle = look->stance == "crossbow" && look->swimIdleCrossbowClip >= 0
                                 ? look->swimIdleCrossbowClip
                                 : look->swimIdleClip;
        const int walkHere = riding                                ? rideRun
                             : flies                               ? flyMove
                             : swims                               ? swimMove
                             : (body->running && look->runClip >= 0) ? look->runClip
                             : (safe && look->walkSafeClip >= 0)   ? look->walkSafeClip
                                                                   : look->walkClip;
        // The fly is one of the rides here: seated in the air, it plants nothing and plays at the
        // run ride's own 0.34 (ZzzOpenData.cpp:511-516), slowed with the ground he covers.
        const auto isRide = [&](int c) {
            return c >= 0 && (c == look->rideRunClip || c == look->rideRunArmedClip ||
                              c == look->flyClip || c == look->flyCrossbowClip);
        };
        const auto isSwim = [&](int c) {
            return c >= 0 && (c == look->swimWalkClip || c == look->swimRunClip);
        };
        const auto isWalk = [&](int c) {
            return c >= 0 && (c == look->walkClip || c == look->walkSafeClip ||
                              c == look->runClip || isRide(c) || isSwim(c));
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
        // A pose stands in for the idle and for nothing else, so the walk above still outranks
        // it: the realm takes the pose off as the walk starts, and this is the same rule for the
        // frames in between. MU's PLAYER_SIT1 233 / female 235, PLAYER_HEALING1 237 / 238,
        // PLAYER_POSE1 239 / 240, IsFemale deciding -- MU2's Poses.Clip. They loop, and that is
        // the whole of why a pose lasts.
        // Those are player.bmd's own slots, one under index.json's labels (which follow
        // Season 6's _enum.h): the file holds four 20-key sits at 233-236, then four 6-key
        // clips -- healing, healing female, pose, pose female -- at 237-240, and the
        // Halloween PLAYER_JACK_1 at 241. Off by one, the elf leaned by playing Jack and the
        // knight by holding the female lean.
        int posed = -1;
        // A townsperson on his rounds sits too (realm_folk.cpp): the same clips, on the same rig.
        if ((body->player || body->warden >= 0) && body->pose != sim::Pose::Standing &&
            look->library) {
            const bool female = look->female;
            const int action = body->pose == sim::Pose::Sitting   ? (female ? 235 : 233)
                               : body->pose == sim::Pose::Hanging ? (female ? 238 : 237)
                                                                  : (female ? 240 : 239);
            posed = look->library->find(action);
        }
        if (walking) {
            clip = walkHere;
        } else if (riding) {
            clip = rideIdle;
        } else if (posed >= 0) {
            clip = posed;
        } else if (flies) {
            clip = flyIdle;
        } else if (swims) {
            clip = swimIdle;
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
                // Out of a skill as slowly as into one: its last key is a hand or a blade far
                // from the stance, and the swing's blend snapped it home.
                const bool fromCast = one.figure.once() && one.swingSkill != 0;
                one.figure.play(clip, false,
                                isWalk(was) ? kHalting : fromCast ? kCastBlend : -1.0f);
                // And the arrival's own cry: the bottom of SetPlayerStop, which a monster runs
                // once when its walk ends (ZzzCharacter.cpp:429-443, from MovePath at :6444),
                // one in sixteen, once -- not scaled by the frame, as it is one call.
                if (isWalk(was) && !body->player && one.cryMove >= 0) {
                    wanderDice_ ^= wanderDice_ << 13;
                    wanderDice_ ^= wanderDice_ >> 17;
                    wanderDice_ ^= wanderDice_ << 5;
                    if ((wanderDice_ >> 8) % 16u == 0u) {
                        emit(one.cryMove, one.crown[0], one.crown[2], one.id);
                    }
                }
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
        // The run ride is seated: no foot plants on the earth, so it plays at MU's own rate and
        // the horse under him carries the ground (game/pets.h). That rate is 0.34, the whole
        // PLAYER_RUN..PLAYER_RUN_RIDE_WEAPON run (ZzzCharacter.cpp:511-516), where actions.json
        // cooked it at 0.3; at 0.3 the rider bounced out of step with the horse's 0.34 (the user,
        // 2026-10-02: "char is not perfectly synced with mount bouncing").
        // Slowed (iced), the ride slows with the ground it covers, and the horse with it, since
        // it takes the rider's place in the clip (Pets::ride): at full rate over half the ground
        // the two galloped on the spot (the user, 2026-10-03). **ours**.
        constexpr float kRideRunRate = 0.34f / 0.3f;
        if (isRide(one.figure.clip())) {
            const float full = body->speed * sim::strideFactor(*body);
            one.clipRate = kRideRunRate * (full > 1e-4f ? std::min(pace / full, 1.0f) : 1.0f);
        }
        // A swim is paced as a walk is (below), by the water it covers against the stroke's own
        // travel: MU's 0.35 strokes 3.09 m a 0.69 s cycle, 4.5 m/s, far past what he swims, and
        // the legs kicked like a sprint (the user, 2026-10-04: 'swiming aniamtjons on legs seemed
        // very fast', 'can we do the same with swimming mosnters?'). Ours. It plants no foot, so
        // the whole cycle's travel is the measure.
        if (isSwim(one.figure.clip())) {
            const float gait = pace * ground_->metresPerTile() / float(kTickSeconds);
            const float travel = one.figure.travel();
            const float duration = one.figure.length();
            if (travel > 0.001f && duration > 0.0f) {
                one.clipRate = std::min(gait * duration / travel, kFastestClip);
            }
        }
        if (readyBow >= 0 && one.figure.clip() == readyBow) {
            one.figure.setClock(0.0f);
            one.clipRate = 0.0f;
        }
        if (isWalk(one.figure.clip()) && !isRide(one.figure.clip()) && !isSwim(one.figure.clip())) {
            const float metresPerTile = ground_->metresPerTile();
            const float gait = pace * metresPerTile / float(kTickSeconds);
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
                (one.figure.clip() == look->runClip
                     ? look->plantSpeedRun
                 : one.figure.clip() == look->walkSafeClip && look->walkSafeClip != look->walkClip
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
    if (!heroDrawn()) return;
    const Drawn& hero = drawn_[heroAt()];
    *column = hero.wasX + (hero.nowX - hero.wasX) * through_;
    *row = hero.wasY + (hero.nowY - hero.wasY) * through_;
}

bool Play::shownAt(uint32_t id, float* column, float* row) const {
    const size_t at = size_t(id) - 1;
    if (at >= drawn_.size() || drawn_[at].id != id) return false;
    const Drawn& one = drawn_[at];
    *column = one.wasX + (one.nowX - one.wasX) * through_;
    *row = one.wasY + (one.nowY - one.wasY) * through_;
    return true;
}

void Play::gather(gfx::Renderer& renderer, const float* viewProj, std::vector<gfx::Drawable>& out,
                  std::vector<gfx::Drawable>* casters, std::vector<gfx::Drawable>* hover,
                  std::vector<gfx::Drawable>* flashed, std::vector<Flash>* flashes) {
    // One eased fade for every reason a body is not simply "there": the hero's own
    // door-opening appearance, a corpse going out at the end of its held pose, and a
    // respawn easing back in. All three are the same smoothstep on a different clock, so
    // they are one function and not three copies of it.
    const auto fadeOf = [&](const Drawn& one) -> float {
        // A Teleport: out over MU's ten frames, gone until he is put down, and back in.
        const bool himself = heroDrawn() && &one == &drawn_[heroAt()];
        if (himself && blinkOut_ >= 0.0f) {
            const float t = std::clamp(blinkOut_ / kBlinkFadeSeconds, 0.0f, 1.0f);
            return 1.0f - t * t * (3.0f - 2.0f * t);
        }
        if (himself && blinkIn_ >= 0.0f) {
            const float t = std::clamp(blinkIn_ / kBlinkFadeSeconds, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
        if (himself && appearing_) {
            const float t = std::clamp(appearAt_ / kAppearSeconds, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }
        if (one.deadFor >= 0.0f) {
            if (one.deadFor < kDeathHold || himself) return 1.0f;
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
        const float fade = fadeOf(one) * one.seeThrough;
        if (fade <= 0.0f) continue;
        const int bones = one.figure.pose(scratch_.data());
        const int palette = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        // And its bow's or crossbow's own clip, on her key: the string drawn and let go with the
        // arm (Figure::poseHeld). Never called here before 2026-10-03, so in play every bow
        // stood strung and still -- only the town's crowd, the lobby and the bench played it.
        one.figure.poseHeld(renderer, scratch_.data());
        // The hover ring's own copy: the SAME pose, just handed to a second list, so the
        // outline mask draws it again without a second call to Figure::pose. Only the body
        // pointedAt() names, and only while it is actually drawn -- a monster that faded out
        // from under the pointer rings nothing, which is what Godot's ObjectDisposedException
        // guard amounted to. Gated on no townsperson winning the same pick, or a guard closer
        // than a monster behind him would ring both at once -- leftClick's own ladder, which
        // this has to agree with since the ring is meant to show what a click would answer.
        if (hover && pointedFolk_ < 0 && one.id == pointedAt_) one.figure.gather(palette, *hover);
        // And its red flash, the same way, if it has just turned on him (Play::watchAggro).
        if (flashed && flashes && !aggro_.empty()) {
            if (const float strength = flashOf(one.id); strength > 0.0f) {
                const size_t from = flashed->size();
                one.figure.gather(palette, *flashed);
                flashes->push_back({from, flashed->size(), strength});
            }
        }
        // An Ale in the hero is MuMain's red: while ABILITY_FAST_ATTACK_SPEED stands, RenderCharacter
        // multiplies his light by (0.9, 0.5, 0.5) and copies it into `c->Light`, which lights every
        // part he wears (ZzzCharacter.cpp:9320). The instance's `light` is that same multiplier.
        const bool soused = one.id == realm_.hero().id && realm_.aleLeft() > 0;
        // And an iced body is MU's blue while it lasts: `eDeBuff_Freeze` puts (0.3, 0.5, 1.0) on
        // its BodyLight (ZzzObject.cpp:1126).
        const sim::Body* inRealm = realm_.find(one.id);
        // A Frost Arrow's freeze is ice as much as the chill is.
        const bool iced = inRealm != nullptr && (inRealm->chilledUntil > realm_.tick() ||
                                                 inRealm->frozenUntil > realm_.tick());
        // And a poisoned one green, MU's `eDeBuff_Poison` (0.3, 1.0, 0.5); both, (0.3, 1.0, 0.8).
        const bool poisoned = one.venomous ||
                              (inRealm != nullptr && inRealm->poisonUntil > realm_.tick());
        // And a burning one an ember (kEmberTint), the Immolate rune's.
        const bool burning = inRealm != nullptr && inRealm->burnUntil > realm_.tick();
        const size_t tintFrom = out.size();
        // And where the world asks, the painted light where it stands (Ground::figureLightAt):
        // Blood Castle, whose doubled exposure lifts a dark-painted floor and drew every figure
        // twice as bright as the stone round it (the user, 2026-10-03: 'its not well balanced').
        float place[3] = {1.0f, 1.0f, 1.0f};
        const bool placeLit = ground_ != nullptr && ground_->figureLight() > 0.0f && inRealm != nullptr;
        if (placeLit) ground_->figureLightAt(inRealm->x, inRealm->y, place);
        // And MU's Level 1, which SelectCharacter lights at -0.4 where an ordinary monster takes
        // +0.2 (Selection.cpp:101-107): the Great Bahamut, a near-black fish beside the red-brown
        // Bahamut (the user, 2026-10-04: 'i think Great Bahamut has some darker tint').
        const FigureBody* lookNow = one.figure.body();
        const bool darkLevel = lookNow != nullptr && lookNow->name == kGreatBahamutFigure;
        if (darkLevel) {
            for (float& k : place) k *= kLevelOneLight;
        }
        const auto tint = [&] {
            if (placeLit || darkLevel) {
                for (size_t i = tintFrom; i < out.size(); ++i) {
                    for (int k = 0; k < 3; ++k) out[i].light[k] *= place[k];
                }
            }
            if (!soused && !iced && !poisoned && !burning && !one.murderer) return;
            // A debuff's light over a murderer's red: MU sets c->Light first and the debuff's
            // BodyLight after it.
            const float* by = poisoned && iced ? kPoisonIcedLight
                              : poisoned       ? kPoisonedLight
                              : iced           ? kIcedLight
                              : burning        ? kEmberTint
                              : soused         ? kSousedLight
                                               : kMurdererLight;
            for (size_t i = tintFrom; i < out.size(); ++i) {
                out[i].light[0] *= by[0];
                out[i].light[1] *= by[1];
                out[i].light[2] *= by[2];
                // The glaze (kIcedChrome). An excellent thing's flag and its +11 sweep ride
                // above the plus (game/shine.h), so they are kept.
                const int flags = shineFlagsOf(out[i].refine);
                if (iced && shineLevelOf(out[i].refine) < kIcedPlus) {
                    out[i].refine = flags + kIcedPlus;
                    std::copy(kIcedChrome, kIcedChrome + 3, out[i].refineColour);
                }
            }
        };
        if (fade < 1.0f) {
            const size_t castFrom = casters ? casters->size() : 0;
            if (casters) one.figure.gather(palette, *casters);
            one.figure.gather(palette, out);
            for (size_t i = tintFrom; i < out.size(); ++i) out[i].fade = fade;
            if (casters) {
                for (size_t i = castFrom; i < casters->size(); ++i) (*casters)[i].fade = fade;
            }
            tint();
            continue;
        }
        // The sun's list is every figure, as the town's is: a body behind the camera still
        // casts into the frame, and culling the shadow pass with the camera's frustum is the
        // bug foundation 7 names. The camera's own cull is Crowd's frustum test and is owed
        // here; at Lorencia's 290 bodies, of which a handful are ever near the camera, it is
        // the next thing to do and not this sprint's.
        if (casters) one.figure.gather(palette, *casters);
        one.figure.gather(palette, out);
        tint();
    }
    // Twisting Slash's copies of his weapon, the camera's alone.
    wheel_.gather(out);
    // And Rageful Blow's weapon in the air.
    fury_.gather(out);
    // The Golden Invasion's dragons in the sky.
    sky_.gather(renderer, scratch_.data(), out, casters);
    // His pet, after him, so the Imp takes his clavicle as this frame posed it.
    if (Drawn* hero = drawnOf(realm_.hero().id); hero && hero->visible) {
        pets_.gather(renderer, hero->figure, scratch_, out, casters);
        wing_.gather(renderer, hero->figure, scratch_, out, casters);
    }
    // The raid's raiders' wings, after their bodies are posed (play_raid.cpp).
    gatherRaiders(renderer, out, casters);
    for (size_t i = 0; i < folk_.size(); ++i) {
        Standing& one = folk_[i];
        const int bones = one.figure.pose(scratch_.data());
        const int palette = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        // And its bow's or crossbow's own clip, on her key: the string drawn and let go with the
        // arm (Figure::poseHeld). Never called here before 2026-10-03, so in play every bow
        // stood strung and still -- only the town's crowd, the lobby and the bench played it.
        one.figure.poseHeld(renderer, scratch_.data());
        // By the table's row, which is what the pick names: folk_ skips the rows with no
        // figure and appends the placements' four at the end, so its own index is not it.
        if (hover && pointedFolk_ >= 0 && one.folk == pointedFolk_) {
            one.figure.gather(palette, *hover);
        }
        if (casters) one.figure.gather(palette, *casters);
        one.figure.gather(palette, out);
        // After her pose, which the wing takes her Bone05 from (game/wings.h).
        if (one.wing.worn()) one.wing.gather(renderer, one.figure, scratch_, out, casters);
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
