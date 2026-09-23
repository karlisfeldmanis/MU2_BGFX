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
    }
}

void Play::update(double seconds) {
    if (!isOpen()) return;
    // This frame's gains, and only this frame's: whoever draws the lane runs after this and
    // reads them once. See Play::gains.
    gains_.clear();
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
            // SOUND_GET_ITEM01 for everything else -- and for Zen too, which in MU is silent;
            // MU2 gave it the pickup after the commonest pickup in the game read as having
            // missed it, and that is kept. A use, a purchase and a sale are heard off their
            // own answers (useItem, buy, sell): they are asked between ticks, and the next
            // tick clears what they said before this loop could read it.
            // What the lane over the HUD says: his experience, his Zen and his potion. A gain
            // is his and belongs to no body on the map, which is why it is collected apart
            // from the cues and never put over a monster's head.
            if (happening.who == heroId) {
                if (happening.what == sim::What::Gained) {
                    gains_.push_back({Gain::Kind::Experience, happening.a});
                } else if (happening.what == sim::What::Picked && happening.b < 0) {
                    gains_.push_back({Gain::Kind::Zen, happening.c});
                } else if (happening.what == sim::What::Drank) {
                    gains_.push_back({happening.b ? Gain::Kind::Mana : Gain::Kind::Health,
                                      happening.a});
                }
                // His death is NOT said here. The tick it resolves on is half a swing before
                // the blow that caused it is drawn landing, so a message raised now stands
                // over a man still on his feet. It is raised in Play::fall, with the first key
                // of his own death clip.
            }
            if (happening.who == heroId) {
                if (happening.what == sim::What::Picked) {
                    // Zen rings coins rather than the pickup: it is the one thing picked up
                    // that is not a thing, and it is now swept up rather than clicked
                    // (Realm::sweep), so this is the only sound the whole heap ever makes.
                    if (happening.b < 0) {
                        const Drawn* hero = drawnOf(heroId);
                        if (heard_.moneyDrop >= 0 && hero && hero->placed) {
                            emit(heard_.moneyDrop, hero->crown[0], hero->crown[2]);
                            continue;
                        }
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
                if (happening.whom == heroId) levelOn_ = happening.who;
            } else if (happening.what == sim::What::Levelled && happening.who == heroId) {
                levelOwed_ = true;
            } else if (happening.what == sim::What::Rose) {
                if (Drawn* risen = drawnOf(happening.who)) {
                    risen->fallOwed = false;
                    risen->deadFor = -1.0f;
                    risen->spawnFade = 0.0f;
                }
            }
            // A cast, said by the realm BEFORE the blow it throws, which is what lets the hit
            // below be drawn with the skill's own clip instead of the weapon's. Nothing else is
            // done here: the damage, the death and the cooldown all resolved on the tick.
            if (happening.what == sim::What::Cast) {
                if (Drawn* caster = drawnOf(happening.who)) {
                    const sim::SkillRow* row = sim::skillNumbered(happening.a);
                    caster->castSkill = happening.a;
                    caster->castClip = -1;
                    if (row && caster->figure.body() && caster->figure.body()->library) {
                        caster->castClip = caster->figure.body()->library->find(row->clip);
                    }
                    // A self-cast throws no blow, so there is no Hit coming to play the clip:
                    // it is played here instead, and the wave with it.
                    // The barrier, thrown with the clip and lasting exactly as long as the
                    // realm says the boon does -- one number, read off the row both places.
                    if (row && row->onSelf() && happening.who == heroId) {
                        guardRise(float(row->boonTicks) * float(kTickSeconds));
                    }
                    if (row && row->onSelf() && caster->castClip >= 0) {
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
                if (Drawn* swinger = drawnOf(happening.who)) {
                    // The pose is started once, by whichever half comes first: `Swung` for the
                    // player, the `Hit` itself for a monster.
                    if (begun) {
                        swinger->landing = true;
                        // Which skill it is, for the clip and the wave below; 0 is a swing.
                        swinger->castSkill = happening.a;
                        swinger->castClip = -1;
                        if (const sim::SkillRow* row = sim::skillNumbered(happening.a)) {
                            if (swinger->figure.body() && swinger->figure.body()->library) {
                                swinger->castClip =
                                    swinger->figure.body()->library->find(row->clip);
                            }
                        }
                    }
                    const bool pose = begun || !swinger->landing;
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
                        cue.critical = happening.critical;
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
    exhale(float(seconds));
    // Before the puffs are aged, so a Giant's sand is thrown on the same frame its clip reached
    // the key that throws it, exactly as the dragon's dust is.
    sandOnDeath();
    breath_.update(float(seconds));
    // The bones a skeleton left, on the drawing's clock like everything else here.
    bones_.update(float(seconds));
    // The Lich's meteors: advance every live one, collect impacts.
    meteorImpacts_.clear();
    meteor_.update(float(seconds), meteorImpacts_);
    // On each impact: explosion sound, shock clip on everything within 2 tiles.
    for (const auto& impact : meteorImpacts_) {
        if (heard_.explosion >= 0) emit(heard_.explosion, impact.x, impact.z);
        // The blow lands with the fire. The fuse says the same thing and would land it on its
        // own; this is what keeps the two together when the frame rate is not what the fuse
        // assumed, and what lands it early when the rock was refused by a full pool.
        showing_.rush(impact.attacker);
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
        if (swinger == nullptr || swinger->swingToken != cue.token || swinger->swinging <= 0.0f) {
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
        showing_.land(cue, feet, height, man, swinger->yaw, onHero);
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
    // And the guard walks with him, or goes when the realm says it has gone.
    if (isOpen()) guardStep();
}

std::string Play::nameOf(uint32_t id) const {
    const sim::Body* one = realm_.find(id);
    if (!one) return "nobody";
    if (one->player) return "the hero";
    if (one->kind < 0 || size_t(one->kind) >= tables_.kinds.size()) return "a monster";
    return tables_.kinds[size_t(one->kind)].label + "#" + std::to_string(id);
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
