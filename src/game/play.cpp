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

void Play::remember() {
    for (Drawn& one : drawn_) {
        one.wasX = one.nowX;
        one.wasY = one.nowY;
        one.wasFacing = one.nowFacing;
        if (const sim::Body* body = realm_.find(one.id)) {
            one.nowX = body->x;
            one.nowY = body->y;
            one.nowFacing = body->facing;
        }
        // What the tick just gone actually covered, in metres a second. Taken from the two
        // positions rather than from the body's `speed`, because those two are not the same
        // number the moment anything interferes: a tick spent turning on the spot covers no
        // ground, a step refused by the grid covers no ground, and the arrival tick covers
        // whatever was left of the tile rather than a whole one. The feet follow what happened.
        const float metresPerTile = ground_ ? ground_->metresPerTile() : 1.0f;
        const float dx = (one.nowX - one.wasX) * metresPerTile;
        const float dy = (one.nowY - one.wasY) * metresPerTile;
        one.groundSpeed = std::sqrt(dx * dx + dy * dy) / float(kTickSeconds);
        // A jump of more than two tiles in one tick is a respawn or a warp, never a step, and it
        // is not drawn part way: interpolated, the tick after a revive put the camera (which
        // follows the drawn hero) at every point on the line from his corpse to the gate, three
        // frames sweeping across the map before it arrived. The body is where it landed from
        // the first frame, and so is everything framed on it.
        if (std::max(std::fabs(one.nowX - one.wasX), std::fabs(one.nowY - one.wasY)) > 2.0f) {
            one.wasX = one.nowX;
            one.wasY = one.nowY;
            one.wasFacing = one.nowFacing;
            one.groundSpeed = 0.0f;
        }
    }
}

// A body the realm has just handed back, made to stand where it rose. Everything the drawing
// kept from its death goes: it is at the new tile from the first frame, even a monster raised a
// tile from its own corpse (under the two tiles `remember` snaps at), and its clip is CUT to the
// idle. Left to `follow`, the idle blended in over the held death pose, and every respawn got up
// off the floor in a fifth of a second while it faded in.
void Play::stand(Drawn& risen) {
    risen.fallOwed = false;
    risen.deadFor = -1.0f;
    risen.spawnFade = 0.0f;
    risen.wasX = risen.nowX;
    risen.wasY = risen.nowY;
    risen.wasFacing = risen.nowFacing;
    risen.groundSpeed = 0.0f;
    risen.swinging = 0.0f;
    risen.casting = 0.0f;
    risen.landing = false;
    risen.castSkill = 0;
    risen.still = 0.0f;
    risen.clipRate = 1.0f;
    const FigureBody* look = risen.figure.body();
    const sim::Body* body = realm_.find(risen.id);
    if (look == nullptr || body == nullptr) return;
    const bool safe = tables_.grid.safe(body->column(), body->row());
    const int idle = (safe && look->idleSafeClip >= 0) ? look->idleSafeClip : look->idleClip;
    if (idle >= 0) risen.figure.play(idle, true, 0.0f);
}

void Play::update(double seconds) {
    if (!isOpen()) return;
    // This frame's gains, and only this frame's: whoever draws the lane runs after this and
    // reads them once. See Play::gains.
    gains_.clear();
    heroCast_ = 0;
    // A potion drunk since the last frame: asked between frames, and said here so the clear
    // above does not take it. See Play::useItem.
    if (drankHealth_ > 0) gains_.push_back({Gain::Kind::Health, drankHealth_});
    if (drankMana_ > 0) gains_.push_back({Gain::Kind::Mana, drankMana_});
    drankHealth_ = drankMana_ = 0;
    accumulator_ += seconds;
    int stepped = 0;
    const int64_t started = bx::getHPCounter();
    // A click is answered on the frame it is made. Waiting for the tick that was due anyway
    // cost 0 to 50 ms, 25 on average, between the press and the first step -- the one delay
    // in the walk a hand can feel. So the tick runs now and the tick clock starts again from
    // here: the sim still ticks twenty times a second, one interval is simply cut short.
    // Invention; MU answers a click on its next frame, and so did this at 20 Hz.
    //
    // ONLY for a click that sets him off from a stand, and at most one every kEarlyApart.
    // Given to every click it was a way to run the sim faster than 20 Hz: a hand spamming
    // clicks mid-walk cut every interval short, so he walked faster than he walks and so did
    // everything else, in a stutter. Mid-walk the delay is not felt anyway -- he is already
    // moving, and the new route takes over on the next tick from where he stands.
    //
    // The picture must not jump for it. Every body is drawn part way between its last two
    // ticks, and a tick taken early would move that drawn point on by whatever was left of the
    // interval -- a few centimetres of pop for everything walking. So each body's drawn
    // position is caught first and becomes the `was` of the new tick, and the drawing carries
    // on from exactly where it was.
    sinceEarly_ += float(seconds);
    const bool early = stepNow_ && accumulator_ < kTickSeconds;
    stepNow_ = false;
    if (early) sinceEarly_ = 0.0f;
    if (early) {
        for (Drawn& one : drawn_) {
            one.caughtX = one.wasX + (one.nowX - one.wasX) * through_;
            one.caughtY = one.wasY + (one.nowY - one.wasY) * through_;
            one.caughtFacing = one.wasFacing + wrapped(one.nowFacing - one.wasFacing) * through_;
        }
        accumulator_ = kTickSeconds;
    }
    while (accumulator_ >= kTickSeconds && stepped < kMostTicks) {
        // Each body's health going into the tick, so a blow's cue can say what it took rather
        // than what it rolled. Bodies and figures share one order (Play::open).
        for (size_t i = 0; i < drawn_.size() && i < realm_.bodies().size(); ++i) {
            drawn_[i].health = realm_.bodies()[i].health;
        }
        realm_.step();
        // AFTER the step, not before it. Before, `now` held the state at the START of the tick
        // and `was` the start of the one before, so the picture trailed the sim by one whole
        // tick on top of the interpolation's own -- a figure at `through_ = 0` was 100 ms
        // behind what the sim had already decided. Now the two ends really are the ticks
        // either side of where the clock stands, which is what the comment below claims.
        remember();
        if (early && stepped == 0) {
            for (Drawn& one : drawn_) {
                one.wasX = one.caughtX;
                one.wasY = one.caughtY;
                one.wasFacing = one.caughtFacing;
            }
        }
        sim::audit(realm_, findings_);
        const uint32_t heroId = realm_.hero().id;
        for (const sim::Happening& happening : realm_.happenings()) {
            // The arena's own line first, so that what the log says happened on a tick is in
            // the log before anything the drawing decides to do about it. Nothing in an
            // ordinary run reaches it.
            if (!arena_.breed.empty()) announce(happening);
            // The marker, off the realm's own word for where the walk ends: `Walked` carries
            // the goal the route was planned to, after the router moved it out of any wall,
            // so the marker is where he will stand and not where the pointer was.
            if (happening.who == heroId && ground_) {
                const float metresPerTile = ground_->metresPerTile();
                const float x = (float(happening.a) + 0.5f) * metresPerTile;
                const float z = -(float(happening.b) + 0.5f) * metresPerTile;
                if (happening.what == sim::What::Walked && mark_) {
                    marker_.show(x, z);
                } else if (happening.what == sim::What::Halted ||
                           happening.what == sim::What::Died) {
                    marker_.dismiss();
                }
            }
            // A body that falls is not removed from the picture on the tick it does: it plays
            // its own death clip, holds the corpse pose the cook already marked `hold`, and
            // fades -- see kDeathHold and kDeathFade. `Rose` is the same body handed back, at
            // its home tile with a fresh clip, and it fades back in rather than popping into
            // being.
            //
            // Not on this tick, though: the blow that killed it resolved here and is SHOWN at
            // its landing cue, half a swing from now. Fallen on the tick, the monster went
            // down before the last blow reached it. So the fall is owed, and paid the frame
            // nothing is left to land on it -- fallWhenLanded, after the cues below.
            // The hero's pickup, heard at his ears: ReceiveGetItem's SOUND_JEWEL01 for a jewel,
            // SOUND_GET_ITEM01 for everything else, and SOUND_DROP_MONEY01's coins for Zen,
            // which is taken with the kill rather than picked up (Play::takeZen). A use, a purchase and a sale are heard off their
            // own answers (useItem, buy, sell): they are asked between ticks, and the next
            // tick clears what they said before this loop could read it.
            // What the lane over the HUD says: his experience, his Zen and his potion. A gain
            // is his and belongs to no body on the map, which is why it is collected apart
            // from the cues and never put over a monster's head.
            if (happening.who == heroId) {
                if (happening.what == sim::What::Gained) {
                    gains_.push_back({Gain::Kind::Experience, happening.a});
                }
                // His Zen is not here either: it is in the purse from the kill's tick, but
                // shown with the fall, in Play::releaseDrops (below, at What::Picked).
                // A potion is not here: it is drunk between ticks, and the next step clears
                // what it said before this loop could read it. Play::useItem says it.
                // His death is NOT said here. The tick it resolves on is half a swing before
                // the blow that caused it is drawn landing, so a message raised now stands
                // over a man still on his feet. It is raised in Play::fall, with the first key
                // of his own death clip.
            }
            if (happening.who == heroId) {
                // Sitting down or leaning back lands with a thud: MuMain's operate arm ends on
                // `PlayBuffer(SOUND_DROP_ITEM01, &Hero->Object)` after the sit and the pose, and
                // not in the Healing branch -- so Noria's hang is silent. Getting up is silent.
                if (happening.what == sim::What::Posed && heard_.itemDrop >= 0 &&
                    (happening.a == int32_t(sim::Pose::Sitting) ||
                     happening.a == int32_t(sim::Pose::Leaning))) {
                    const float metresPerTile = ground_->metresPerTile();
                    emit(heard_.itemDrop, (happening.x + 0.5f) * metresPerTile,
                         -(happening.y + 0.5f) * metresPerTile);
                }
                if (happening.what == sim::What::Picked) {
                    // Zen never lies on the ground: the kill puts it in the purse (Realm::leave)
                    // and it is held here until the body it came off falls, where a drop would
                    // have landed, and then rings coins at him (Play::takeZen).
                    if (happening.b < 0) {
                        const uint32_t dropper = uint32_t(happening.a);
                        if (drawnOf(dropper)) held_.push_back({0, dropper, happening.c});
                        else takeZen(happening.c);
                        continue;
                    }
                    int sound = heard_.take;
                    if (happening.b >= 0 && happening.b < sim::kSlots) {
                        const int32_t item = realm_.satchel()[happening.b].item;
                        if (item >= 0 && size_t(item) < tables_.items.size() &&
                            tables_.items[size_t(item)].jewel() && heard_.jewel >= 0) {
                            sound = -1;
                            const Drawn* hero = drawnOf(heroId);
                            if (hero && hero->placed) {
                                emit(heard_.jewel, hero->crown[0], hero->crown[2]);
                            }
                        }
                    }
                    if (sound >= 0) sound_.play(sound);
                }
            }
            if (happening.what == sim::What::Dropped) {
                // Lying in the realm from this tick; shown once its dropper is down.
                if (drawnOf(happening.who)) held_.push_back({uint32_t(happening.a), happening.who});
            }
            if (happening.what == sim::What::Died) {
                if (Drawn* dead = drawnOf(happening.who)) dead->fallOwed = true;
                // The experience, and so the level, is said after the death on the same tick:
                // the kill is what the level waits to be shown on.
                // A guard's kill the hero helped with is his too, level and all (Realm::kill).
                const sim::Body* killer = realm_.find(happening.whom);
                if (happening.whom == heroId || (killer && killer->warden >= 0)) {
                    levelOn_ = happening.who;
                }
            } else if (happening.what == sim::What::Shouted) {
                speak(happening);
            } else if (happening.what == sim::What::Levelled && happening.who == heroId) {
                levelOwed_ = true;
            } else if (happening.what == sim::What::Rose) {
                if (Drawn* risen = drawnOf(happening.who)) stand(*risen);
            }
            // A cast, said by the realm BEFORE the blow it throws, which is what lets the hit
            // below be drawn with the skill's own clip instead of the weapon's. Nothing else is
            // done here: the damage, the death and the cooldown all resolved on the tick.
            if (happening.what == sim::What::Cast) {
                if (happening.who == heroId) heroCast_ = happening.a;
                // And held past this frame, for what hangs off the whole cast (the burn).
                if (happening.who == heroId) heroCasting_ = happening.a;
                if (Drawn* caster = drawnOf(happening.who)) {
                    const sim::SkillRow* row = sim::skillNumbered(happening.a);
                    caster->castSkill = happening.a;
                    caster->castClip = -1;
                    if (row && caster->figure.body() && caster->figure.body()->library) {
                        caster->castClip = caster->figure.body()->library->find(row->clip);
                    }
                    // A self-cast throws no blow, so there is no Hit coming to play the clip:
                    // it is played here instead, and the wave with it.
                    // The barrier, thrown with the clip and played once: MU's own ribbons for
                    // two seconds at the cast, not for the guard's whole five minutes. The
                    // user's, 2026-09-25.
                    if (row && row->onSelf() && happening.who == heroId) {
                        guardRise(kGuardShowSeconds);
                    }
                    // A channel: its clip, looping for as long as it runs. No blow follows the
                    // cast to play it, so it is played here. The thunder is the pulses' (below),
                    // not the cast's.
                    if (row && row->channelled() && caster->castClip >= 0) {
                        const float lasts = float(row->channelTicks) * float(kTickSeconds);
                        caster->figure.play(caster->castClip, true, kCastBlend);
                        core::logf("channel: %s plays clip %d, %.3f s long", row->name, row->clip,
                                   double(caster->figure.length()));
                        caster->casting = lasts;
                        caster->swingPace = 1.0f;
                        caster->swinging = lasts;
                        ++caster->swingToken;
                        caster->castSkill = 0;
                    }
                    // A Teleport: its clip, the sparks where he stands and the fade out. The realm
                    // puts him down when the fade has run (`Blinked`, below).
                    if (row && row->blinks && happening.who == heroId && caster->placed &&
                        ground_) {
                        const float feet[3] = {
                            caster->crown[0],
                            ground_->heightAt(caster->crown[0], caster->crown[2]),
                            caster->crown[2]};
                        blink_.cast(feet);
                        blinkOut_ = 0.0f;
                        blinkIn_ = -1.0f;
                        marker_.dismiss();
                        // A spell's clip: the staff lays no streak. Left as the last swing's, it
                        // streaked through the blink and was drawn stretched across the jump.
                        caster->swingSkill = happening.a;
                    }
                    if (row && (row->onSelf() || row->blinks) && caster->castClip >= 0) {
                        caster->figure.play(caster->castClip, true, kCastBlend);
                        caster->casting = caster->figure.length();
                        caster->swingPace = 1.0f;
                        caster->swinging = caster->figure.length();
                        ++caster->swingToken;
                        const int index = sim::skillIndexOf(happening.a);
                        if (index >= 0 && heard_.skill[index] >= 0 && caster->placed) {
                            emit(heard_.skill[index], caster->crown[0], caster->crown[2],
                                 caster->id);
                        }
                        caster->castSkill = 0;
                    }
                }
            }
            // A spell let go, at the bottom of its clip: the bolt leaves his hand now and flies for
            // the ticks the realm says, and SOUND_MAGIC goes with it -- ZzzCharacter.cpp:5142
            // plays it on the line after the one that makes the bolt. From his feet to where the
            // target is DRAWN, which is where the eye has it.
            // A spell that missed: the bolt still in the air at that body flies on past it.
            if (happening.what == sim::What::Missed && happening.thrown) {
                bolt_.miss(happening.whom);
                meteor_.missHurl(happening.whom);
            }
            // Put down by a Teleport: drawn there from this frame, not slid there; the sparks and
            // SOUND_MAGIC again (CreateTeleportEnd), and the fade back in.
            if (happening.what == sim::What::Blinked && happening.who == heroId) {
                const sim::Body& body = realm_.hero();
                if (Drawn* hero = drawnOf(body.id)) {
                    hero->nowX = hero->wasX = body.x;
                    hero->nowY = hero->wasY = body.y;
                    hero->groundSpeed = 0.0f;
                }
                // And no ribbon of anything may span the jump.
                streak_.clear();
                if (ground_) {
                    const float metres = ground_->metresPerTile();
                    const float x = (body.x + 0.5f) * metres;
                    const float z = -(body.y + 0.5f) * metres;
                    const float feet[3] = {x, ground_->heightAt(x, z), z};
                    blink_.cast(feet);
                    sound_.play(heard_.warp);
                }
                blinkOut_ = -1.0f;
                blinkIn_ = 0.0f;
            }
            // Pushed by Lightning: the realm slides it, and it flinches as it goes.
            if (happening.what == sim::What::Shoved) {
                if (Drawn* pushed = drawnOf(happening.who);
                    pushed != nullptr && pushed->placed && pushed->shockClip >= 0) {
                    pushed->figure.play(pushed->shockClip, true);
                }
            }
            if (happening.what == sim::What::Loosed) {
                const Drawn* caster = drawnOf(happening.who);
                const Drawn* target = drawnOf(happening.whom);
                if (caster && caster->placed && ground_) {
                    const float feet = ground_->heightAt(caster->crown[0], caster->crown[2]);
                    // At the middle of the body, which is half its drawn height under its crown.
                    float to[3] = {caster->crown[0], feet + 1.0f, caster->crown[2]};
                    if (target && target->placed) {
                        const FigureBody* look = target->figure.body();
                        const float tall = look ? look->height * look->scale : 1.0f;
                        to[0] = target->crown[0];
                        to[1] = target->crown[1] - tall * 0.5f;
                        to[2] = target->crown[2];
                    }
                    // From the middle of his chest (castFrom), the same for every spell.
                    float from[3];
                    const bool atHand = castFrom(*caster, to, from);
                    // Fire Ball is the Lich's rock at its other subtype, thrown flat; every
                    // other spell that flies is the bolt.
                    if (happening.a == sim::skill::kFireBall) {
                        meteor_.hurl(from, to, happening.whom, atHand);
                    } else if (happening.a == sim::skill::kPoison) {
                        const float feet[3] = {to[0], ground_->heightAt(to[0], to[2]), to[2]};
                        poison_.cast(feet, caster->yaw);
                    } else if (happening.a == sim::skill::kIce) {
                        // The block where the body is drawn, turned to his yaw, as MU turns it.
                        const float floor = ground_->heightAt(to[0], to[2]);
                        const float feet[3] = {to[0], floor, to[2]};
                        ice_.freeze(feet, caster->yaw);
                    } else if (happening.a == sim::skill::kMeteorite) {
                        // The Lich's rock, dropped where the body is drawn: MU's
                        // `CreateEffect(MODEL_FIRE, to->Position, ...)` at the let-go. It
                        // falls for the ticks the realm holds the blow.
                        meteor_.cast(to[0], to[2], happening.who);
                    } else if (happening.a == sim::skill::kLightning) {
                        thunder_.strike(from, to, happening.whom);
                    } else if (happening.a == sim::skill::kPowerWave) {
                        // A curtain standing on the ground, under where every spell leaves.
                        const float ground[3] = {from[0], feet, from[2]};
                        wave_.cast(ground, to);
                    } else {
                        bolt_.cast(from, to, happening.whom, atHand);
                    }
                    const int index = sim::skillIndexOf(happening.a);
                    // A channel's pulse lets go at every body in it on one tick: one thunder a
                    // pulse, not one a body.
                    const sim::SkillRow* loosed = sim::skillNumbered(happening.a);
                    // And Meteorite's rain is a rock on every body on one tick: one wave for it.
                    const bool volley =
                        loosed != nullptr && (loosed->channelled() || loosed->splash > 0.0f);
                    const bool again = volley && lastThunderTick_ == int64_t(happening.tick);
                    if (volley) {
                        lastThunderTick_ = int64_t(happening.tick);
                    }
                    if (!again && index >= 0 && heard_.skill[index] >= 0) {
                        emit(heard_.skill[index], from[0], from[2], caster->id);
                    }
                }
            }
            // A swing is drawn because it is a POSE and not an effect: the blood, the number,
            // the fall and the health plate that hang off the landing are sprint 6's, and none
            // of them is here. What a blow does to the picture today is put the attacker into
            // its attack clip, which then blends back to idle or walk when it ends.
            // `Swung` is the blow BEGUN and `Hit`/`Missed` is the same blow settling half a
            // swing later -- the player's two-part swing, added 2026-09-23 so that a click which
            // cancels an attack cancels the damage with it. A monster sends no `Swung` at all and
            // takes both halves on one tick, which is how this read before and still reads.
            if (happening.what == sim::What::Hit || happening.what == sim::What::Missed ||
                happening.what == sim::What::Swung) {
                const bool begun = happening.what == sim::What::Swung;
                int32_t taken = 0;
                // What a shield ate of the blow, which is only ever the hero's: the realm puts
                // nine tenths of the damage onto the pool and overflows the rest into health
                // (Realm::strikeAt), so the difference between what was rolled and what came
                // off health IS the shield's share. Worked out here rather than said by the sim
                // because here is where the health before the blow is still known.
                //
                // Not on a killing blow: health floors at nought there, so the difference is
                // overkill rather than absorption, and a man dying reads his own red and
                // nothing else.
                int32_t absorbed = 0;
                if (happening.what == sim::What::Hit) {
                    if (Drawn* struck = drawnOf(happening.whom)) {
                        taken = std::max(0, struck->health - happening.c);
                        struck->health = happening.c;
                        if (happening.whom == realm_.hero().id && happening.c > 0) {
                            absorbed = std::max(0, happening.a - taken);
                        }
                    }
                }
                if (happening.what == sim::What::Hit && happening.reflected) {
                    // His armour's reflect: the number and the wound on what struck him, now,
                    // and no swing -- he threw nothing. Stamped with the swing he is in, so it
                    // is not dropped as a stale one.
                    if (const Drawn* hero = drawnOf(happening.who)) {
                        Cue cue;
                        cue.attacker = happening.who;
                        cue.target = happening.whom;
                        cue.damage = happening.a;
                        cue.taken = taken;
                        cue.reflected = true;
                        cue.fuse = 0.0f;
                        cue.token = hero->swingToken;
                        showing_.schedule(cue);
                    }
                } else if (Drawn* swinger = drawnOf(happening.who)) {
                    // The pose is started once, by whichever half comes first: `Swung` for the
                    // player, the `Hit` itself for a monster.
                    if (begun) {
                        swinger->landing = true;
                        // Which skill it is, for the clip and the wave below; 0 is a swing.
                        swinger->castSkill = happening.a;
                        swinger->castClip = -1;
                        if (const sim::SkillRow* row = sim::skillNumbered(happening.a)) {
                            if (swinger->figure.body() && swinger->figure.body()->library) {
                                // A spell's two hands on a coin, `PLAYER_SKILL_HAND1 + rand() %
                                // 2` (SetPlayerMagic): every cast, not an alternation.
                                handDice_ ^= handDice_ << 13;
                                handDice_ ^= handDice_ >> 17;
                                handDice_ ^= handDice_ << 5;
                                const int32_t action =
                                    row->clipOther != 0 && (handDice_ & 1u) ? row->clipOther
                                                                            : row->clip;
                                swinger->castClip =
                                    swinger->figure.body()->library->find(action);
                            }
                        }
                    }
                    // Never the hero's `Hit`: every blow of his has a `Swung` in front of it, except
                    // what flies and what a channel strikes -- and a channel's pulse read as a
                    // monster's one-part blow restarted his attack clip over the held stance on
                    // every pulse, until the first swing of the session latched `landing`.
                    const bool pose =
                        begun || (!swinger->landing && happening.who != realm_.hero().id);
                    // MU's SwordCount % 3: one in three is Attack 1, the rest Attack 2.
                    // A breed with no Attack 2 keeps attackClip2 == -1 and always swings
                    // Attack 1 -- the counter still counts, harmlessly.
                    // The clip a cast already chose wins, and the swing counter is NOT spent on
                    // it: a skill is not one of the weapon's swings, and MU2 found that burning a
                    // count here was what broke the alternation a two-handed blade swings on.
                    const bool cast = swinger->castSkill != 0 && swinger->castClip >= 0;
                    int swing = swinger->castClip;
                    if (!cast) {
                        swing = (swinger->attackClip2 >= 0 && swinger->swordCount % 3 != 0)
                                    ? swinger->attackClip2 : swinger->attackClip;
                        ++swinger->swordCount;
                    }
                    if (pose && swing >= 0 && swinger->figure.body()) {
                        // A skill blends in longer than a swing does. An ordinary blow is a jab
                        // out of a stance and 0.18 s hides the join; a skill is a wind-up, and at
                        // the swing's own blend the body arrives in the pose before the arm has
                        // begun to move, which reads as the animation snapping on rather than
                        // starting. **invention**, and the only number in the drawing that a
                        // skill has of its own.
                        swinger->figure.play(swing, true, cast ? kCastBlend : -1.0f);
                        // The clip has to fit between two blows, and MU's own reason is that
                        // the attack speed makes the CLIP run faster -- the swing rate follows
                        // from that, so anything that plays the animation has to apply the same
                        // scaling or the man swings at one speed and connects at another
                        // (Beast.cs:820-826). Only ever faster: a monster whose row gives it
                        // 1.4 s between blows plays its half-second swing at its own pace and
                        // waits, as MU does, rather than being smeared out to fill the gap.
                        const sim::Body* body = realm_.find(happening.who);
                        const float between =
                            body ? float(body->swingTicks) * float(kTickSeconds) : 0.0f;
                        const float clip = swinger->figure.length();
                        // A cast is never hurried: the realm gave it the longer of the weapon's
                        // rhythm and this clip's own length (Realm::throwSkill), so the animation
                        // fits and squeezing it into the weapon's interval would draw a skill
                        // faster than the realm believes it was thrown.
                        swinger->swingPace =
                            (!cast && between > 0.01f && clip > between) ? clip / between : 1.0f;
                        // A spell's clip runs at MagicSpeed, which is the realm's own length for
                        // it (sim::castTicks): played at the authored pace, a quick wizard's
                        // hand was still coming down when the next cast began.
                        const sim::SkillRow* spell =
                            cast ? sim::skillNumbered(swinger->castSkill) : nullptr;
                        if (spell && spell->wizardry && body) {
                            const auto armAt = [&](int32_t at) -> const content::Arm* {
                                return at >= 0 && size_t(at) < tables_.arms.size()
                                           ? &tables_.arms[size_t(at)] : nullptr;
                            };
                            const int32_t ticks =
                                sim::castTicks(tables_, body->kin, body->points.agility,
                                               armAt(body->weapon), armAt(body->shield), *spell);
                            const float fits = float(ticks) * float(kTickSeconds);
                            if (fits > 0.01f && clip > fits) swinger->swingPace = clip / fits;
                        }
                        swinger->swinging = clip / swinger->swingPace;
                        ++swinger->swingToken;
                        // The swing's own noise, on its first key and on the body, so a bull
                        // that roars as it lunges takes the roar with it: a breed's attack cry,
                        // or what is in the character's hands. MU2's Crowd.Swinging.
                        int cry = body == nullptr ? -1
                                  : body->player  ? swingSound(*body)
                                                  : swinger->cryAttack;
                        if (cast) {
                            const int index = sim::skillIndexOf(swinger->castSkill);
                            if (index >= 0 && heard_.skill[index] >= 0) cry = heard_.skill[index];
                            // A spell's wave is not on its wind-up: MU plays SOUND_MAGIC on the
                            // line after the bolt is made, so it goes with `Loosed`.
                            if (spell && spell->wizardry) cry = -1;
                        }
                        if (cry >= 0 && swinger->placed) {
                            emit(cry, swinger->crown[0], swinger->crown[2],
                                          swinger->id);
                        }

                        // And the cue, which is the only thing sprint 6 adds here. The blow
                        // has ALREADY resolved -- the roll, the damage and the death are on
                        // the tick, above, in the sim -- and this decides when it is SHOWN.
                        //
                        // Halfway through the swing as it will actually be drawn, which is
                        // why the fuse comes off `swinging` and not off the clip's authored
                        // length: at haste the swing plays faster and the cue has to move
                        // with it. MU puts the sound and the number on the swing's FIRST key
                        // because ReceiveAttackDamage does all three in one handler, and on
                        // the first key the arm has not moved yet.
                        // And a skill's clip is protected from the step that may follow it.
                        // A spell too: `casting` is what keeps the walk from cutting the clip when
                        // the drawn body is still sliding in to where the realm has stopped him.
                        // The streak asks the skill itself, and a spell lays none.
                        if (cast) swinger->casting = swinger->swinging;

                        // What this swing was thrown with, kept until the blow settles -- for a
                        // player that is a tick or two later, in the branch below.
                        swinger->swingSkill = cast ? swinger->castSkill : 0;
                        // Spent: the clip is playing and the next blow is the weapon's again.
                        swinger->castSkill = 0;

                        Cue cue;
                        cue.attacker = happening.who;
                        cue.target = happening.whom;
                        cue.damage = happening.a;
                        cue.miss = happening.what == sim::What::Missed;
                        cue.taken = taken;
                        cue.absorbed = absorbed;
                        cue.skill = swinger->swingSkill;
                        cue.critical = happening.critical;
                        cue.excellent = happening.excellent;
                        // A Lich (attackSkill == 2) throws a meteor: the cue's fuse is
                        // the FALL, not a key in the clip. The meteor is cast here and
                        // the blow lands when it hits the ground (see meteor update).
                        const bool isMeteor = body && !body->player && body->kind >= 0 &&
                            size_t(body->kind) < tables_.kinds.size() &&
                            tables_.kinds[size_t(body->kind)].attackSkill == 2;
                        if (isMeteor && ground_) {
                            // Cast the meteor at the target's tile.
                            const sim::Body* target = realm_.find(happening.whom);
                            if (target) {
                                const float metresPerTile = ground_->metresPerTile();
                                const float tx = (target->x + 0.5f) * metresPerTile;
                                const float tz = -(target->y + 0.5f) * metresPerTile;
                                meteor_.cast(tx, tz, happening.who);
                                // Placed where it will LAND rather than unplaced, which is
                                // MU2's own departure and the reason is the fall: the sound
                                // leads the strike by a third of a second, and a third of a
                                // second of warning that comes from nowhere is a third of a
                                // second the player cannot use. MU plays it at full volume in
                                // the middle of the head.
                                if (heard_.meteorite >= 0) emit(heard_.meteorite, tx, tz);
                                // The tick, because a run is read afterwards and not watched:
                                // it is what a shot's frame is worked out from under
                                // `--fixed-dt`, where a tick is three frames at sixty.
                                core::logf("meteor: tick %lld, %s#%u throws at tile %d,%d",
                                           (long long)realm_.tick(),
                                           tables_.kinds[size_t(body->kind)].label.c_str(),
                                           happening.who, target->column(), target->row());
                            }
                            // The fall, not a key in the clip: four metres at twelve and a
                            // half a second is 0.32 s, the same every throw. The impact
                            // rushes the cue too (Showing::rush), so the two agree; this is
                            // what lands the blow when the pool was full and there was no
                            // meteor to rush it.
                            cue.fuse = Meteor::fallSeconds();
                        } else {
                            cue.fuse = swinger->swinging * Showing::kLandingPoint;
                        }
                        cue.token = swinger->swingToken;
                        if (!begun) showing_.schedule(cue);
                    }
                    // The settling half of a two-part swing: the clip has been running since the
                    // `Swung`, so the blow is shown the moment it is told rather than half a
                    // swing later. Everything else about the cue is the same.
                    if (!begun && swinger->landing) {
                        // NOT cleared here, and that is what lets a spin settle on four monsters
                        // at once: an area skill says one `Swung` and then a `Hit` per target on
                        // the same tick, and clearing the flag on the first of them would make the
                        // second look like a monster's one-part blow -- replaying the clip, the
                        // wave and the swing counter for every body it caught. Only a `Swung` sets
                        // it, only the player sends one, and every blow he throws has one in front
                        // of it, so a latch is the whole of the rule.
                        Cue cue;
                        cue.attacker = happening.who;
                        cue.target = happening.whom;
                        cue.damage = happening.a;
                        cue.miss = happening.what == sim::What::Missed;
                        cue.taken = taken;
                        cue.absorbed = absorbed;
                        // The swing's own skill, remembered when it began: this is the settling
                        // half, and the cast that named it was two ticks ago.
                        cue.skill = swinger->swingSkill;
                        // A primary spell's number is drawn as a swing's: it is his basic attack,
                        // and the skill ramp is for the keys.
                        if (happening.thrown) {
                            const sim::SkillRow* row = sim::skillNumbered(cue.skill);
                            if (row == nullptr || row->primary()) cue.skill = 0;
                        }
                        cue.thrown = happening.thrown;
                        // Only a spell flies, so a thrown blow is wizardry -- asked of the blow and
                        // not of `swingSkill`, which a dry wizard's staff may already have replaced.
                        cue.magic = happening.thrown;
                        // A poison's pulse is MU's DT_POISON green, not a blow's number.
                        cue.poison = happening.poisoned;
                        cue.critical = happening.critical;
                        cue.excellent = happening.excellent;
                        cue.fuse = 0.0f;
                        cue.token = swinger->swingToken;
                        showing_.schedule(cue);
                    }
                }
            }
            // Everything but the tile crossings, which are most of the log and none of the
            // news. The line is what the run is read by until sprint 6 draws any of it.
            if (happening.what == sim::What::Stepped) continue;
            lastLine_ = sim::describe(happening, realm_);
        }
        // The ask was taken on this tick, whatever became of it.
        mark_ = false;
        accumulator_ -= kTickSeconds;
        ++stepped;
    }
    marker_.update(float(seconds));
    if (appearing_) {
        appearAt_ += float(seconds);
        if (appearAt_ >= kAppearSeconds) appearing_ = false;
    }
    if (stepped > 0) {
        tickMs_ = double(bx::getHPCounter() - started) * 1000.0 /
                  double(bx::getHPFrequency()) / double(stepped);
        // Whatever is left over after the cap is forgiven rather than owed. See kMostTicks.
        if (accumulator_ >= kTickSeconds * kMostTicks) accumulator_ = 0.0;
    }
    through_ = float(std::min(1.0, accumulator_ / kTickSeconds));
    follow(float(seconds));
    for (Drawn& one : drawn_) {
        // A swing runs at its own pace, a walk at the ground's, everything else at the clip's
        // own. `follow` decided which of the three this is; the crossfade runs in real seconds
        // either way, which is why the rate goes in as a rate rather than as a scaled delta.
        one.figure.update(float(seconds), one.clipRate);
        if (one.swinging > 0.0f) one.swinging -= float(seconds);
    }
    for (Standing& one : folk_) {
        one.figure.update(float(seconds));
        // A clip that has come round is a clip that has finished: the next is rolled then,
        // so the town's people are never in step with each other or with themselves.
        if (one.cycles && one.figure.clock() < one.lastClock) {
            const int next = fidget(one);
            if (next != one.figure.clip()) one.figure.play(next, true);
        }
        one.lastClock = one.figure.clock();
    }
    steps();
    hammer();
    smithy(float(seconds));
    exhale(float(seconds));
    snort(float(seconds));
    // Before the puffs are aged, so a Giant's sand is thrown on the same frame its clip reached
    // the key that throws it, exactly as the dragon's dust is.
    sandOnDeath();
    breath_.update(float(seconds));
    // The bones a skeleton left, on the drawing's clock like everything else here.
    bones_.update(float(seconds));
    // The Lich's meteors: advance every live one, collect impacts.
    meteorImpacts_.clear();
    meteor_.update(float(seconds), meteorImpacts_);
    // The wizard's bolts and fireballs, each measured against where its target is drawn this
    // frame.
    const auto standing = [&](uint32_t id) {
        const sim::Body* body = realm_.find(id);
        return body != nullptr && body->alive();
    };
    const auto middle = [&](uint32_t id, float* out) {
        const Drawn* drawn = drawnOf(id);
        if (drawn == nullptr || !drawn->placed) return false;
        const FigureBody* look = drawn->figure.body();
        for (int k = 0; k < 3; ++k) out[k] = drawn->crown[k];
        out[1] -= (look ? look->height * look->scale : 1.0f) * 0.5f;
        return true;
    };
    bolt_.update(float(seconds), standing, middle);
    meteor_.fly(float(seconds), standing, middle);
    wave_.update(float(seconds));
    blink_.update(float(seconds));
    ice_.update(float(seconds));
    poison_.update(float(seconds));
    if (blinkOut_ >= 0.0f) blinkOut_ += float(seconds);
    // A blink the realm dropped -- he died in the fade -- is never put down: he is drawn again.
    if (blinkOut_ >= 0.0f && realm_.hero().blinkAt == 0) blinkOut_ = -1.0f;
    if (blinkIn_ >= 0.0f) {
        blinkIn_ += float(seconds);
        if (blinkIn_ >= kBlinkFadeSeconds) blinkIn_ = -1.0f;
    }
    thunder_.update(float(seconds), standing, middle);
    // The fire on him while he calls a Meteorite down: while its clip is on him, not while the
    // realm holds him -- a cast on the tick he arrives is held while the drawn body is still
    // sliding in on its run, and the fire read as a man on fire running.
    if (const sim::Body& hero = realm_.hero();
        heroCasting_ == sim::skill::kMeteorite && realm_.casting() && ground_) {
        if (const Drawn* drawn = drawnOf(hero.id);
            drawn != nullptr && drawn->placed && drawn->casting > 0.0f) {
            const FigureBody* look = drawn->figure.body();
            const float feet[3] = {drawn->crown[0],
                                   ground_->heightAt(drawn->crown[0], drawn->crown[2]),
                                   drawn->crown[2]};
            meteor_.burn(feet, look ? look->height * look->scale : 1.8f, float(seconds));
        }
    }
    // The frost on him while he casts Ice, and the fumes while he casts Poison.
    if (const sim::Body& hero = realm_.hero();
        (heroCasting_ == sim::skill::kIce || heroCasting_ == sim::skill::kPoison) &&
        realm_.casting() && ground_) {
        if (const Drawn* drawn = drawnOf(hero.id);
            drawn != nullptr && drawn->placed && drawn->casting > 0.0f) {
            const FigureBody* look = drawn->figure.body();
            const float feet[3] = {drawn->crown[0],
                                   ground_->heightAt(drawn->crown[0], drawn->crown[2]),
                                   drawn->crown[2]};
            const float tall = look ? look->height * look->scale : 1.8f;
            if (heroCasting_ == sim::skill::kIce) {
                ice_.chill(feet, tall, float(seconds));
            } else {
                poison_.fume(feet, tall, float(seconds));
            }
        }
    }
    // The crackle on him for as long as he channels.
    if (const sim::Body& hero = realm_.hero(); hero.channelSkill != 0 && ground_) {
        if (const Drawn* drawn = drawnOf(hero.id); drawn != nullptr && drawn->placed) {
            const FigureBody* look = drawn->figure.body();
            const float feet[3] = {drawn->crown[0],
                                   ground_->heightAt(drawn->crown[0], drawn->crown[2]),
                                   drawn->crown[2]};
            thunder_.crackle(feet, look ? look->height * look->scale : 1.8f, float(seconds));
        }
    }
    // On each impact: explosion sound, shock clip on everything within 2 tiles.
    for (const auto& impact : meteorImpacts_) {
        if (heard_.explosion >= 0) emit(heard_.explosion, impact.x, impact.z);
        // The blow lands with the fire. The fuse says the same thing and would land it on its
        // own; this is what keeps the two together when the frame rate is not what the fuse
        // assumed, and what lands it early when the rock was refused by a full pool.
        // Not the hero's own Meteorite: the realm lands that one on its tick, as it lands every
        // spell that flies, and rushing his cues would hurry a swing of his still coming down.
        if (impact.attacker != realm_.hero().id) showing_.rush(impact.attacker);
        // The shock, and three things about it are MU's rather than ours
        // (ZzzEffect.cpp:7752-7773):
        //   * the HERO IS EXCLUDED -- `tc != Hero`. Your own character takes the camera's
        //     jolt and nothing else, and the flinch belongs to everybody around you. The
        //     first version of this shocked him too, which put the knight into a clip in the
        //     middle of his own fight for a meteor that did not touch him.
        //   * the dead are excluded, and so is anything not standing in the world yet.
        //   * 200 units is two tiles, which here is two metres.
        // No sound: MU plays none on a shock. A monster's cooked `_shock` event turned out to
        // be its attack pair under another name, and playing it here would have been a voice
        // MU has never made.
        for (auto& one : drawn_) {
            if (one.id == realm_.hero().id) continue;
            if (!one.placed || !one.figure.body() || one.shockClip < 0) continue;
            const sim::Body* body = realm_.find(one.id);
            if (body == nullptr || !body->alive()) continue;
            const float dx = one.crown[0] - impact.x;
            const float dz = one.crown[2] - impact.z;
            if (dx * dx + dz * dz < kShockTiles * kShockTiles) one.figure.play(one.shockClip, true);
        }
    }

    // --- sprint 6: the landing cue ------------------------------------------------------
    // On the DRAWING's clock and after the swings have been advanced above, so that a cue
    // and the swing it belongs to are read at the same instant. Everything a blow does, it
    // does in one frame: MU2 learned that the hard way when Struck, Hurt and Slain each
    // showed their part the moment the realm called them, and a monster began falling four
    // tenths of a second before the number that killed it appeared over the corpse.
    showing_.advance(float(seconds), due_);
    for (const Cue& cue : due_) {
        const Drawn* swinger = drawnOf(cue.attacker);
        // The gate. A cue belongs to one swing, and if the body has moved on -- a step
        // cancels a swing here -- the cue drops itself. A dropped cue costs a splash and a
        // number and never a fact: the damage was taken on the tick either way.
        if (swinger == nullptr ||
            (!cue.thrown && (swinger->swingToken != cue.token || swinger->swinging <= 0.0f))) {
            showing_.drop();
            continue;
        }
        const sim::Body* target = realm_.find(cue.target);
        if (target == nullptr || ground_ == nullptr) {
            showing_.drop();
            continue;
        }
        // The target where the SIM has it, not where the interpolation has it: this runs
        // before follow() has placed anything this frame, and half a tile of smoothing is
        // below the scatter the blood is thrown with anyway.
        // Where the body is DRAWN, which is where the eye sees the blow land: the sim's
        // position leads a walking body by up to a tile, and blood thrown from there hung in
        // the air beside it. The crown is the feet raised by the height, placed by follow().
        const Drawn* hit = drawnOf(cue.target);
        const FigureBody* look = hit ? hit->figure.body() : nullptr;
        const float height = look ? look->height * look->scale : 1.2f;
        const float metresPerTile = ground_->metresPerTile();
        float x = (target->x + 0.5f) * metresPerTile;
        float z = -(target->y + 0.5f) * metresPerTile;
        if (hit && hit->placed) {
            x = hit->crown[0];
            z = hit->crown[2];
        }
        const float feet[3] = {x, ground_->heightAt(x, z), z};
        const FigureBody* heroLook = drawn_.empty() ? nullptr : drawn_[0].figure.body();
        const float man = heroLook ? heroLook->height * heroLook->scale : 1.8f;
        // How tall the thing actually is, which is what every length in the blood is taken
        // in units of. A figure with no body drawn falls back to a man's height rather than
        // to zero, because zero would collapse the whole effect to a point.
        const bool onHero = cue.target == realm_.hero().id;
        // A number only for what he threw or took: a guard's blow on a monster is blood alone.
        const bool told = onHero || cue.attacker == realm_.hero().id;
        showing_.land(cue, feet, height, man, swinger->yaw, onHero, told);
        // The hit, on the attacker, which is where MoveCharacter plays it: one of MU's four,
        // at random, for every ordinary blow with a target -- a MISS as well. The sound sits in
        // the AttackTime block beside the blood, and only the blood asks `tc->Hit`.
        if (heard_.hit >= 0 && swinger->placed) {
            emit(heard_.hit, swinger->crown[0], swinger->crown[2], swinger->id);
        }
    }
    // After the cues, so the fall comes in the same frame as the number and the blood of the
    // blow that caused it -- or at once, for a death no blow is still owed on.
    fallWhenLanded();
    // And the level, in the same frame as the blow that earned it -- MU2's Rose, reached from
    // the kill's cue. A dropped cue still clears `awaits`, so a level is never lost to one.
    if (levelOwed_ && (levelOn_ == 0 || !showing_.awaits(levelOn_))) {
        levelOwed_ = false;
        levelOn_ = 0;
        rise();
    }
    releaseDrops();
    showing_.update(float(seconds));
    aura_.update(float(seconds));
    warp_.update(float(seconds));
    // And the guard walks with him, or goes when the realm says it has gone.
    if (isOpen()) guardStep();
    // What the town's guards said, aged and taken down.
    for (Said& one : said_) one.age += float(seconds);
    said_.erase(std::remove_if(said_.begin(), said_.end(),
                               [](const Said& one) { return one.age >= kSaidSeconds; }),
                said_.end());
}

std::string Play::nameOf(uint32_t id) const {
    const sim::Body* one = realm_.find(id);
    if (!one) return "nobody";
    if (one->player) return "the hero";
    if (one->warden >= 0) return tables_.folk[size_t(one->warden)].name + "#" + std::to_string(id);
    if (one->kind < 0 || size_t(one->kind) >= tables_.kinds.size()) return "a monster";
    return tables_.kinds[size_t(one->kind)].label + "#" + std::to_string(id);
}

uint32_t Play::wardenBody(int folk) const {
    if (folk < 0) return 0;
    for (const sim::Body& one : realm_.bodies()) {
        if (one.warden == folk) return one.id;
    }
    return 0;
}

// The guard's words: a challenge that names what he has seen, or, after a kill the hero shared,
// where the rest of them are. The user's (2026-09-28) -- MU's guards say nothing -- and a
// challenge is chosen by the breed's label, so a
// breed another map adds falls through to the plain ones rather than to silence. Which of a
// breed's lines is said is off the monster's id and the tick: steady for a replayed seed, and
// not the same line twice in a row at one gate.
void Play::speak(const sim::Happening& happening) {
    struct Lines {
        const char* breed;  // a piece of the label, lowered
        const char* lines[3];
    };
    static const Lines kChallenges[] = {
        {"spider", {"Get over here, you eight-legged bastard!", "Filthy crawler! Come here!",
                    "Back to your webs, vermin!"}},
        {"dragon", {"Come here, you scaly bastard!", "Keep your fire off our walls, lizard!",
                    "Budge, is it? Budge this!"}},
        {"bull", {"Come here, you bull-headed bastard!", "Horns down, beast!",
                  "Charge me, then! Come on!"}},
        {"hound", {"Here, dog! Come and get it!", "Heel, you mangy cur!",
                   "Come here, you flea-bitten bastard!"}},
        {"lich", {"Back to the grave, dead thing!", "Come here, you rotten bastard!",
                  "Keep your spells out of Lorencia!"}},
        {"giant", {"Come here, you great lump!", "The bigger they are...!",
                   "Not one more step, giant!"}},
        {"skeleton", {"Come here, you bony bastard!", "Back to the grave, bag of bones!",
                      "I'll rattle you apart!"}},
    };
    static const char* const kPlain[] = {"Come here, bastard!", "Not past this gate!",
                                         "To arms! Monster at the gate!"};

    const uint32_t pick = happening.whom + happening.tick;
    std::string line;
    if (happening.a == int32_t(sim::Shout::Pointing)) {
        // Where the rest of them are, as he turns to look -- and nothing when there are none. No
        // "thank you": the user's, 2026-09-28.
        if (happening.whom == 0) return;
        line = "There are more of them in that direction.";
    } else {
        std::string label;
        if (const sim::Body* about = realm_.find(happening.whom);
            about && about->kind >= 0 && size_t(about->kind) < tables_.kinds.size()) {
            for (char c : tables_.kinds[size_t(about->kind)].label) {
                label += char(std::tolower(static_cast<unsigned char>(c)));
            }
        }
        line = kPlain[pick % 3];
        for (const Lines& one : kChallenges) {
            if (label.find(one.breed) != std::string::npos) line = one.lines[pick % 3];
        }
    }
    // One line a speaker, and a challenge does not talk over one of his still being read: at a
    // gate a guard takes on the next monster the tick after the last falls, and every one of
    // them said aloud was a new bubble every second. Where the rest are always replaces what he
    // said before it, since it is said once a kill.
    const bool pointing = happening.a == int32_t(sim::Shout::Pointing);
    for (const Said& one : said_) {
        if (!pointing && one.who == happening.who && one.age < kSaidSeconds * 0.75f) return;
    }
    said_.erase(std::remove_if(said_.begin(), said_.end(),
                               [&](const Said& one) { return one.who == happening.who; }),
                said_.end());
    said_.push_back({happening.who, line, 0.0f});
    // With the tick, so a run is read afterwards and a shot aimed at the line.
    std::string flat = line;
    std::replace(flat.begin(), flat.end(), '\n', ' ');
    core::logf("guard: tick %lld, %s says \"%s\" about %s", (long long)realm_.tick(),
               nameOf(happening.who).c_str(), flat.c_str(), nameOf(happening.whom).c_str());
}

// What an arena run is read by. One line a happening, with the tick on it, for the five that
// are worth catching a frame of -- a swing that lands, a swing that misses, a death, a
// respawn and what a death leaves behind. The tick is the point: `--fixed-dt 16.667` makes a
// frame exactly a third of a tick, so `--shot` is aimed by arithmetic rather than by watching.
//
// What is NOT here, on purpose: the Lich's meteor and the skeleton's bones already say their
// own tick from where the drawing casts them (`meteor: tick N`, `bones: tick N`), and those
// two lines know things a happening does not -- which tile the meteor was aimed at, how many
// pieces went up. Repeating them here would be a second, less informed copy.
void Play::announce(const sim::Happening& happening) {
    const long long tick = (long long)realm_.tick();
    switch (happening.what) {
        case sim::What::Hit: {
            // `c` is what the defender has left, after the blow; `a` is the damage. The blow is
            // SHOWN half a swing later (the landing cue), so a shot of the blood is a few
            // frames after this tick and not on it.
            core::logf("arena: tick %lld, %s hits %s for %d -- %d left", tick,
                       nameOf(happening.who).c_str(), nameOf(happening.whom).c_str(),
                       happening.a, happening.c);
            break;
        }
        case sim::What::Missed:
            core::logf("arena: tick %lld, %s swings at %s and misses", tick,
                       nameOf(happening.who).c_str(), nameOf(happening.whom).c_str());
            break;
        case sim::What::Died: {
            // Whether the body comes apart instead of falling is the DRAWING's answer and not
            // the sim's -- MU keys it on the model (Play::open) -- so it is read off the figure
            // and said here, because a run that is looking for the bones wants the tick they
            // start from and the corpse is what it would otherwise search for.
            const Drawn* dead = drawnOf(happening.who);
            core::logf("arena: tick %lld, %s dies, killed by %s -- it %s", tick,
                       nameOf(happening.who).c_str(), nameOf(happening.whom).c_str(),
                       dead && dead->bursts ? "comes apart into bones, leaving no corpse"
                                            : "falls where it stood");
            break;
        }
        case sim::What::Dropped:
            core::logf("arena: tick %lld, %s leaves something at tile %d,%d", tick,
                       nameOf(happening.who).c_str(), int(happening.x), int(happening.y));
            break;
        case sim::What::Rose:
            core::logf("arena: tick %lld, %s stands up again", tick,
                       nameOf(happening.who).c_str());
            break;
        default:
            break;
    }
}

}  // namespace mu::game
