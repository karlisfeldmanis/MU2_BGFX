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
#include "sim/gates.h"
#include "sim/realm_tuning.h"

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

void Play::watchAggro(float seconds) {
    for (Aggro& one : aggro_) one.seconds += seconds;
    aggro_.erase(std::remove_if(aggro_.begin(), aggro_.end(),
                                [](const Aggro& one) { return one.seconds >= kFlashSeconds; }),
                 aggro_.end());
    // Every live monster after him now; one that was not last frame has just turned. A dead
    // hero is nobody's quarry, so rising in town and being seen again flashes afresh.
    const uint32_t heroId = realm_.hero().id;
    huntingNow_.clear();
    for (const sim::Body& one : realm_.bodies()) {
        if (!one.monster() || !one.alive() || one.quarry != heroId) continue;
        huntingNow_.push_back(one.id);
        if (std::find(hunting_.begin(), hunting_.end(), one.id) == hunting_.end()) {
            aggro_.erase(std::remove_if(aggro_.begin(), aggro_.end(),
                                        [&](const Aggro& a) { return a.id == one.id; }),
                         aggro_.end());
            aggro_.push_back({one.id, 0.0f});
        }
    }
    hunting_.swap(huntingNow_);
}

float Play::flashOf(uint32_t id) const {
    // Only the newest kFlashRings: there are no more rings than that to draw them in.
    const size_t first = aggro_.size() > size_t(kFlashRings) ? aggro_.size() - kFlashRings : 0;
    for (size_t i = first; i < aggro_.size(); ++i) {
        if (aggro_[i].id != id) continue;
        // Two blinks over kFlashSeconds, out to nothing between and after: a flash, not a
        // ring that lingers and could be taken for the hover's.
        const float t = std::clamp(aggro_[i].seconds / kFlashSeconds, 0.0f, 1.0f);
        const float blink = std::sin(t * 2.0f * bx::kPi);
        return blink * blink * (1.0f - 0.35f * t);
    }
    return 0.0f;
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
                            tables_.items[size_t(item)].jewel() &&
                            tables_.items[size_t(item)].group != sim::kGroupPets &&
                            heard_.jewel >= 0) {
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
                // A salute is a pose as well as a line: MU's PLAYER_SALUTE1, played once and
                // held as a swing is held. **Action 218, whatever player.muc's label says.**
                // Its labels are one slot off here: "Salute 1" (219) is 9 frames like the two
                // after it, which are Rock-Paper-Scissors -- a fist shaken and thrown out, the
                // guard aiming at Marlon the user saw -- and 218, labelled "Respect", is the hand
                // brought up to the helmet and held; 217 is the bow. Checked frame by frame on
                // the bench, 2026-09-29.
                if (happening.a == int32_t(sim::Shout::Salute)) {
                    constexpr int kSaluteAction = 218;
                    if (Drawn* guard = drawnOf(happening.who);
                        guard && guard->figure.body() && guard->figure.body()->library) {
                        const int clip = guard->figure.body()->library->find(kSaluteAction);
                        if (clip >= 0) {
                            guard->figure.play(clip, true, kCastBlend);
                            // Held as a swing is, but not a cast: `casting` is what lays the
                            // blade's streak, and a salute is no blow.
                            guard->swinging = guard->figure.length();
                            guard->swingPace = 1.0f;
                            ++guard->swingToken;
                            // Given with the weapon slung, the town's way of carrying it, and
                            // taken back up a moment after the hand comes down.
                            guard->stowed = guard->figure.length() + 0.3f;
                        }
                    }
                }
                speak(happening);
            } else if (happening.what == sim::What::Levelled && happening.who == heroId) {
                // Off a kill, one rise however many levels it carried: the realm has added them
                // all, and only a quest's are shown one by one (the user, 2026-10-01). A rise
                // already owed covers this one too.
                levelsOwed_ = std::max(levelsOwed_, 1);
            } else if (happening.what == sim::What::Rose) {
                if (Drawn* risen = drawnOf(happening.who)) stand(*risen);
                // Risen on a map with no safe zone: owed Lorencia, as a Town Portal read there is.
                if (happening.who == heroId && happening.c == 1) homeOwed_ = true;
            } else if (happening.what == sim::What::Spawned) {
                // Her summon raised (Realm::conjure): the one body whose figure changes, to the
                // breed she called, stood up where the realm put it. Every other Spawned is the
                // realm's first tick, before any drawing.
                const sim::Body* summon = realm_.find(happening.who);
                Drawn* drawn = drawnOf(happening.who);
                if (summon && summon->summoner != 0 && drawn && figures_) {
                    const std::string& figure = tables_.kinds[size_t(summon->kind)].figure;
                    const FigureBody* look = figures_->body(figure);
                    // A breed this world's figure table does not carry fights undrawn, and says
                    // so: cooked per world (tools/cook.py --only figures), a summon is only as
                    // visible as the world she raised it in lets it be.
                    if (!look) {
                        core::logError("summon: %s has no figure in this world", figure.c_str());
                    }
                    if (look) {
                        fit(*drawn, *summon, look);
                        // And its cries: loaded when the map opened, off the placeholder breed
                        // its dormant slot had, so a Goblin cried as a Bull Fighter (the user,
                        // 2026-09-29, "elf goblin summon has wrong sound"). The breed she called,
                        // named as Play::open names every breed's.
                        std::string named;
                        for (char c : tables_.kinds[size_t(summon->kind)].label) {
                            if (c != ' ') named += char(std::tolower(static_cast<unsigned char>(c)));
                        }
                        drawn->cryAttack = sound_.load(named + "_attack", true, true);
                        drawn->cryDie = sound_.load(named + "_die", true, true);
                        drawn->cryMove = sound_.load(named + "_move", true, true);
                        drawn->wasX = drawn->nowX = summon->x;
                        drawn->wasY = drawn->nowY = summon->y;
                        drawn->deadFor = -1.0f;
                        drawn->fallOwed = false;
                        stand(*drawn);
                    }
                }
            } else if (happening.what == sim::What::Dismissed) {
                // Gone without a fall, as MU's summons go: out of the picture on the tick.
                if (Drawn* gone = drawnOf(happening.who)) {
                    gone->fallOwed = false;
                    gone->deadFor = kDeathTotal;
                }
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
                    // user's, 2026-09-25. A guard's alone -- Defense, Soul Barrier and the elf's
                    // Greater Defense -- and not her Heal or Greater Damage, which raise none.
                    if (row && row->boonTicks > 0 && happening.who == heroId) {
                        guardRise(kGuardShowSeconds);
                    }
                    // Her Heal and Greater Damage: a burst where she stands, once (fx/aura.h,
                    // kMending and kMight -- ours, MU draws neither).
                    if (row && (row->mends || row->mightTicks > 0) && happening.who == heroId &&
                        caster->placed && ground_) {
                        const float feet[3] = {
                            caster->crown[0],
                            ground_->heightAt(caster->crown[0], caster->crown[2]),
                            caster->crown[2]};
                        aura_.cast(row->mends ? kMending : kMight, feet, caster->yaw,
                                   ground_->metresPerTile());
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
            // Through a gate: the mode changes the map on the next frame (PlayMode::frame).
            if (happening.what == sim::What::Gated && happening.who == heroId && gated_ == 0) {
                gated_ = happening.a;
                gatedColumn_ = happening.b;
                gatedRow_ = happening.c;
            }
            // A stair to another floor of this map: he is already down there in the realm; the
            // drawing, the camera and the marker follow him as they do a Town Portal's.
            if (happening.what == sim::What::Climbed && happening.who == heroId) warped();
            // A Dungeon trap caught him (sim/traps.h): the number and the blood on him now, as a
            // poison's pulse is shown -- no swing is behind it -- and he flinches. The Iron Stick
            // strikes, and the trap sounds where it stands: aGrate, or sFlame for the Fire Trap
            // (ZzzCharacter.cpp:1223-1237).
            if (happening.what == sim::What::Trapped && happening.who == heroId) {
                int32_t taken = 0;
                if (Drawn* hero = drawnOf(heroId)) {
                    taken = std::max(0, hero->health - happening.c);
                    hero->health = happening.c;
                }
                Cue cue;
                cue.attacker = heroId;
                cue.target = heroId;
                cue.damage = happening.a;
                cue.taken = taken;
                cue.miss = happening.a == 0;
                cue.thrown = true;
                cue.fuse = 0.0f;
                float at[3] = {0.0f, 0.0f, 0.0f}, facing[2] = {0.0f, 0.0f};
                int32_t number = 0;
                trapShow_.fired(size_t(happening.b), at);
                const bool placed = trapShow_.where(size_t(happening.b), at, facing, &number);
                // The Lost Tower's Meteorite Trap: its Flame of Evil drawn as a meteor on him,
                // eMeteorite where it lands (ZzzCharacter.cpp:1999), and the blow shown as the
                // Lich's is, when the stone lands.
                if (placed && number == 103 && ground_) {
                    const sim::Body& body = realm_.hero();
                    const float tile = ground_->metresPerTile();
                    const float tx = (body.x + 0.5f) * tile, tz = -(body.y + 0.5f) * tile;
                    meteor_.cast(tx, tz, 0);
                    if (heard_.meteorite >= 0) emit(heard_.meteorite, tx, tz);
                    cue.fuse = Meteor::fallSeconds();
                } else if (placed) {
                    const int sound = number == 102 ? heard_.trapFlame : heard_.grate;
                    if (sound >= 0) emit(sound, at[0], at[2]);
                }
                showing_.schedule(cue);
            }
            if (happening.what == sim::What::Barred && happening.who == heroId) {
                // Said over him, once a step into the box: MU's refusal is a system line
                // (CheckGate, ZzzInterface.cpp:2709); a sealed gate's is ours.
                const sim::EnterGate* sealed = sim::enterGateNumbered(happening.a);
                const std::string line =
                    happening.b == 0
                        ? std::string(sealed && sealed->sealed ? sealed->sealed : "The way")
                              + " is sealed. Its door will not open."
                        : "Only characters of level " + std::to_string(happening.b) +
                              " or higher can enter.";
                said_.erase(std::remove_if(said_.begin(), said_.end(),
                                           [&](const Said& one) { return one.who == heroId; }),
                            said_.end());
                said_.push_back({heroId, line, 0.0f});
                core::logf("gate: %d refuses him: %s", happening.a, line.c_str());
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
            // A draw that emptied the quiver hand, or refilled it from the bag.
            if ((happening.what == sim::What::Swung || happening.what == sim::What::Arrowless) &&
                happening.who == realm_.hero().id && quiverName() != dressedQuiver_) {
                redress();
            }
            if (happening.what == sim::What::Shoved) {
                if (Drawn* pushed = drawnOf(happening.who);
                    pushed != nullptr && pushed->placed && pushed->shockClip >= 0) {
                    shock(*pushed, pushed->shockClip);
                }
            }
            // The hit, on the caster at the release, as MU plays it for a spell and an arrow alike
            // (ZzzCharacter.cpp:5251-5310): not when the thing arrives. Once a swing, and only for
            // the skill the swing was thrown with -- a rune's Ice, Lightning or rock says `Loosed`
            // on a swing that was something else, and MU has no rune to play a hit for.
            if (happening.what == sim::What::Loosed && happening.whom != 0) {
                if (Drawn* caster = drawnOf(happening.who);
                    caster != nullptr && caster->placed && happening.a == caster->swingSkill &&
                    caster->heardToken != caster->swingToken) {
                    caster->heardToken = caster->swingToken;
                    const sim::Body* shooter = realm_.find(happening.who);
                    const sim::SkillRow* shot = sim::skillNumbered(happening.a);
                    const bool arrow = shooter != nullptr && shooter->archer != 0 &&
                                       (shot == nullptr || shot->arrows > 0);
                    const int hit = arrow ? heard_.missile : heard_.hit;
                    if (hit >= 0) emit(hit, caster->crown[0], caster->crown[2], caster->id);
                }
            }
            // A rune's lightning -- Stormcall, Loosed on a swing that was not Lightning -- always
            // shakes what it struck (the user, 2026-10-01: "monster is not reacting to that
            // lighting"). Only its push flinched it, and a push into a wall or a safe tile is
            // not taken, which left the blow's one-in-two coin as its only answer.
            if (happening.what == sim::What::Loosed && happening.a == sim::skill::kLightning &&
                happening.who == heroId) {
                const Drawn* caster = drawnOf(happening.who);
                Drawn* struck = drawnOf(happening.whom);
                if (caster && caster->swingSkill != sim::skill::kLightning && struck &&
                    struck->placed && struck->shockClip >= 0 && struck->shocked <= 0.0f) {
                    shock(*struck, struck->shockClip);
                }
            }
            // Evil Spirit let go round him, his spell's or his shield rune's (fx/spirits.h), with
            // SOUND_EVIL as MU plays it at the release (ZzzCharacter.cpp:4603).
            if (happening.what == sim::What::Spirits) {
                if (const Drawn* caster = drawnOf(happening.who);
                    caster != nullptr && caster->placed && ground_) {
                    const float feet[3] = {
                        caster->crown[0], ground_->heightAt(caster->crown[0], caster->crown[2]),
                        caster->crown[2]};
                    spirits_.release(happening.who, feet);
                    // Not again until it is half through (kEvilSoundTicks): the spell has no
                    // cooldown, and under MU's two voices a cast every clip cut it back to its
                    // start each time -- a stutter, heard as a loop (the user, 2026-10-01:
                    // "wierd looping sound"). Ours; a cast in between goes out unheard.
                    const int index = sim::skillIndexOf(sim::skill::kEvilSpirit);
                    const bool sounding = int64_t(happening.tick) - lastEvilTick_ < kEvilSoundTicks;
                    if (!sounding) lastEvilTick_ = int64_t(happening.tick);
                    if (!sounding && index >= 0 && heard_.skill[index] >= 0) {
                        emit(heard_.skill[index], feet[0], feet[2], caster->id);
                    }
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
                    if (happening.a == sim::skill::kNone) {
                        // An archer's arrow (`Realm::looseArrow`).
                        shootArrow(*caster, to, happening.whom);
                    } else if (happening.a == sim::skill::kSkillshot) {
                        // The fan, drawn as the realm strikes it: straight at the body and
                        // kFanDegrees apart either side, out to the row's reach, flying on
                        // through what they meet (MU's Triple Shot, `Kind = 1`).
                        const sim::SkillRow* shot = sim::skillNumbered(happening.a);
                        const float tile = ground_->metresPerTile();
                        const float reach = (shot ? shot->reach : 6.0f) * tile;
                        const float centre = std::atan2(to[2] - caster->crown[2],
                                                        to[0] - caster->crown[0]);
                        const int count = shot ? shot->arrows : 3;
                        for (int a = 0; a < count; ++a) {
                            const int step = (a + 1) / 2;
                            // World z runs against the grid's rows, so a turn the realm makes
                            // one way is drawn the other (docs/conventions.md).
                            const float turn = -float(a % 2 == 1 ? step : -step) *
                                               sim::kFanDegrees * 3.14159265f / 180.0f;
                            const float far[3] = {
                                caster->crown[0] + std::cos(centre + turn) * (reach + tile),
                                to[1], caster->crown[2] + std::sin(centre + turn) * (reach + tile)};
                            shootArrow(*caster, far, 0);
                        }
                    } else if (happening.a == sim::skill::kFireBall) {
                        meteor_.hurl(from, to, happening.whom, atHand);
                    } else if (happening.a == sim::skill::kPoison) {
                        const float feet[3] = {to[0], ground_->heightAt(to[0], to[2]), to[2]};
                        poison_.cast(feet, caster->yaw);
                    } else if (happening.a == sim::skill::kFlame) {
                        // On the centre of the TILE the realm lit it on -- the body's tile on
                        // this tick, which is where `Realm::light` put the fire -- as MU lights it
                        // at `SkillX + 0.5`, not where the body is drawn.
                        const float tile = ground_->metresPerTile();
                        float at[3] = {to[0], 0.0f, to[2]};
                        if (const sim::Body* lit = realm_.find(happening.whom)) {
                            at[0] = (float(lit->column()) + 0.5f) * tile;
                            at[2] = -(float(lit->row()) + 0.5f) * tile;
                        }
                        at[1] = ground_->heightAt(at[0], at[2]);
                        flame_.light(at, caster->yaw);
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
                    // Only a spell's: its wave is held off the wind-up for this. Anything else
                    // -- Skillshot's fan -- rang its sound on the `Swung` already, and a second
                    // here was the double shot the user heard (2026-09-29).
                    const bool spell = loosed != nullptr && loosed->wizardry;
                    if (spell && !again && index >= 0 && heard_.skill[index] >= 0) {
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
                } else if (happening.what == sim::What::Hit && happening.poisoned &&
                           happening.whom == heroId) {
                    // A poison's pulse on him: the green number now, and no swing -- the monster
                    // that poisoned him may be across the field or dead.
                    Cue cue;
                    cue.attacker = happening.who;
                    cue.target = happening.whom;
                    cue.damage = happening.a;
                    cue.taken = taken;
                    // And what his shield took of the pulse, which shows ABSORBED as a blow's does.
                    cue.absorbed = absorbed;
                    cue.poison = true;
                    cue.thrown = true;
                    cue.fuse = 0.0f;
                    showing_.schedule(cue);
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
                                // Skillshot is her own draw -- the bow's 50 or the crossbow's 51
                                // -- whichever she holds, and not the row's one number.
                                if (row->arrows > 0 && swinger->attackClip >= 0) {
                                    swinger->castClip = swinger->attackClip;
                                }
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
                        swing = swinger->dualClips[0] >= 0
                                    ? swinger->dualClips[swinger->swordCount % 4]
                                : (swinger->attackClip2 >= 0 && swinger->swordCount % 3 != 0)
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
                                               armAt(body->weapon), armAt(body->shield), *spell,
                                               body->frenzySpeed(realm_.tick()));
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
                            // Twisting Slash's goes with its wheel, fifteen frames in.
                            if (swinger->castSkill == sim::skill::kTwistingSlash) {
                                cry = -1;
                                throwWheel(*swinger, body);
                            }
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
                        } else if (Arrows::Model shot; shoots(happening.who, &shot)) {
                            // The bolt leaves at MU's release key and the blow lands with it;
                            // the fuse is only the fallback if the bolt never arrives.
                            const float release = std::min(15.0f / 25.0f, swinger->swinging);
                            volleys_.push_back({happening.who, happening.whom, release});
                            cue.fuse = release + 0.6f;
                            cue.arrow = true;
                        } else {
                            cue.fuse = swinger->swinging * Showing::kLandingPoint;
                        }
                        // An Ice Monster (attackSkill == 7): its blow is a bite, and the Ice it
                        // casts with it lands fifteen reference frames on, hit or miss.
                        if (body && !body->player && body->kind >= 0 &&
                            size_t(body->kind) < tables_.kinds.size() &&
                            tables_.kinds[size_t(body->kind)].attackSkill == sim::skill::kIce) {
                            iceCasts_.push_back({happening.who, happening.whom, 15.0f / 25.0f});
                        }
                        // An Ice Queen (attackSkill == 11) throws Power Wave at the same frame,
                        // and her blow is shown when the middle wave reaches the target: the
                        // release, and the flat distance at the wave's 15 m a second.
                        if (body && !body->player && body->kind >= 0 &&
                            size_t(body->kind) < tables_.kinds.size() &&
                            tables_.kinds[size_t(body->kind)].attackSkill ==
                                sim::skill::kPowerWave) {
                            waveCasts_.push_back({happening.who, happening.whom, 15.0f / 25.0f});
                            const Drawn* target = drawnOf(happening.whom);
                            if (target && swinger->placed && target->placed) {
                                const float dx = target->crown[0] - swinger->crown[0];
                                const float dz = target->crown[2] - swinger->crown[2];
                                cue.fuse = 15.0f / 25.0f + std::sqrt(dx * dx + dz * dz) / 15.0f;
                            }
                        }
                        // A Thunder Lich (attackSkill == 3) calls Lightning at that frame, and
                        // the bolt is there the moment it is called, so the blow shows with it.
                        if (body && !body->player && body->kind >= 0 &&
                            size_t(body->kind) < tables_.kinds.size() &&
                            tables_.kinds[size_t(body->kind)].attackSkill ==
                                sim::skill::kLightning) {
                            // The Devil draws its own (kDevilFigure): its beams from both
                            // hands and sEvil as the swing begins, the blow with the swing.
                            const Drawn* devil = drawnOf(happening.who);
                            const bool own = devil && devil->handBones[0] >= 0;
                            if (own) {
                                laserCasts_.push_back({happening.who, happening.whom,
                                                       kDevilBeamSeconds});
                                if (heard_.evil >= 0 && devil->placed) {
                                    emit(heard_.evil, devil->crown[0], devil->crown[2]);
                                }
                            } else {
                                thunderCasts_.push_back(
                                    {happening.who, happening.whom, 15.0f / 25.0f});
                                cue.fuse = 15.0f / 25.0f;
                            }
                        }
                        // A boss's Flame of Evil, one blow in five (sim kBosses). MU throws the
                        // Death Gorgon's as a ring of eighteen MODEL_FIRE rolling out from it and
                        // the Balrog's as its Hellfire circle with meteors raining round it
                        // (ZzzCharacter.cpp:1959-1990); **ours**, and few, as the user wants the
                        // tower's effects: the Gorgon's six fireballs rolling out along the ground
                        // with eMeteorite; the Balrog's flat ring of fire spreading on the ground
                        // with sHellFire, and four of the Lich's meteors at random within MU's
                        // 512 units. The blow itself shows as a swing's does.
                        if (happening.boss && body && ground_) {
                            const float tile = ground_->metresPerTile();
                            const float bx = (body->x + 0.5f) * tile, bz = -(body->y + 0.5f) * tile;
                            const bool gorgon = body->kind >= 0 &&
                                                size_t(body->kind) < tables_.kinds.size() &&
                                                tables_.kinds[size_t(body->kind)].number == 35;
                            const float floor[3] = {bx, ground_->heightAt(bx, bz), bz};
                            if (gorgon) {
                                for (int i = 0; i < 6; ++i) {
                                    const float turn = float(i) * 6.2831853f / 6.0f;
                                    shadowStars_.roll(floor, std::cos(turn), std::sin(turn));
                                }
                                if (heard_.meteorite >= 0) emit(heard_.meteorite, bx, bz);
                            } else {
                                shadowStars_.circle(floor);
                                if (heard_.hellfire >= 0) emit(heard_.hellfire, bx, bz);
                                for (int i = 0; i < 4; ++i) {
                                    handDice_ ^= handDice_ << 13;
                                    handDice_ ^= handDice_ >> 17;
                                    handDice_ ^= handDice_ << 5;
                                    const float x = bx + (float(handDice_ % 1024) - 512.0f) * 0.01f;
                                    const float z =
                                        bz + (float((handDice_ >> 10) % 1024) - 512.0f) * 0.01f;
                                    meteor_.cast(x, z, 0);
                                }
                            }
                            core::logf("boss: tick %lld, %s#%u throws its Flame of Evil",
                                       (long long)realm_.tick(),
                                       tables_.kinds[size_t(body->kind)].label.c_str(),
                                       happening.who);
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
                        // A primary's number is drawn as a swing's, the skill ramp being for the
                        // skills that wait. Thrown or not: Lightning is a channel round him and
                        // flies nowhere, and has no cooldown since 2026-09-30 as the rest have not.
                        if (const sim::SkillRow* row = sim::skillNumbered(cue.skill);
                            row != nullptr && row->primary())
                            cue.skill = 0;
                        cue.thrown = happening.thrown;
                        // An archer's arrow, hers or a monster's, asked of the shooter and not of
                        // `swingSkill`, which a dry wizard's staff may already have replaced. A
                        // spell's number is a swing's or a skill's as the knight's are -- white
                        // for a primary, amber for one with a cooldown -- and no longer lavender (the user,
                        // 2026-10-01: "use standard damage color also for DW damage numbers"),
                        // which is MU's own: it draws a spell's number in the swing's white.
                        bool arrow = false;
                        if (happening.thrown) {
                            const sim::Body* shooter = realm_.find(happening.who);
                            const sim::SkillRow* shot = sim::skillNumbered(swinger->swingSkill);
                            arrow = shooter != nullptr && shooter->archer != 0 &&
                                    (shot == nullptr || shot->arrows > 0);
                        }
                        cue.arrow = arrow;
                        // A poison's pulse is MU's DT_POISON green, not a blow's number.
                        cue.poison = happening.poisoned;
                        cue.rune = happening.rune;
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
    chatter(float(seconds));
    orbs(float(seconds));
    smithy(float(seconds));
    exhale(float(seconds));
    snort(float(seconds));
    shade(float(seconds));
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
    for (Volley& volley : volleys_) {
        volley.wait -= float(seconds);
        if (volley.wait <= 0.0f) volleyShot(volley.shooter, volley.target);
    }
    volleys_.erase(std::remove_if(volleys_.begin(), volleys_.end(),
                                  [](const Volley& v) { return v.wait <= 0.0f; }),
                   volleys_.end());
    for (IceCast& cast : iceCasts_) {
        cast.wait -= float(seconds);
        if (cast.wait > 0.0f || ground_ == nullptr) continue;
        const Drawn* caster = drawnOf(cast.caster);
        const Drawn* target = drawnOf(cast.target);
        if (caster == nullptr || target == nullptr || !target->placed) continue;
        // On the target's feet where it is drawn, turned to the caster's yaw, as MU turns it.
        const float feet[3] = {target->crown[0],
                               ground_->heightAt(target->crown[0], target->crown[2]),
                               target->crown[2]};
        ice_.freeze(feet, caster->yaw);
        if (heard_.iceCast >= 0) emit(heard_.iceCast, feet[0], feet[2]);
    }
    iceCasts_.erase(std::remove_if(iceCasts_.begin(), iceCasts_.end(),
                                   [](const IceCast& c) { return c.wait <= 0.0f; }),
                    iceCasts_.end());
    for (IceCast& cast : waveCasts_) {
        cast.wait -= float(seconds);
        if (cast.wait > 0.0f || ground_ == nullptr) continue;
        const Drawn* caster = drawnOf(cast.caster);
        const Drawn* target = drawnOf(cast.target);
        if (caster == nullptr || target == nullptr || !caster->placed || !target->placed) continue;
        // Off her feet, as MU's o->Position; the wave keeps a flat heading, so only the
        // target's x and z steer it. The fan turns that heading ten degrees either way.
        const float from[3] = {caster->crown[0],
                               ground_->heightAt(caster->crown[0], caster->crown[2]),
                               caster->crown[2]};
        const float dx = target->crown[0] - from[0], dz = target->crown[2] - from[2];
        // Three only for the Ice Queen: `if (o->Type == MODEL_ICE_QUEEN)` adds the two side
        // waves, and every other caster -- the Hell Spider -- throws the middle one alone
        // (ZzzCharacter.cpp:5045-5055).
        const FigureBody* casterLook = caster->figure.body();
        const bool fans = casterLook != nullptr && casterLook->name == kFanningFigure;
        for (float degrees : {0.0f, 10.0f, -10.0f}) {
            if (degrees != 0.0f && !fans) continue;
            const float turn = degrees * 3.14159265f / 180.0f;
            const float c = std::cos(turn), s = std::sin(turn);
            const float to[3] = {from[0] + dx * c - dz * s, from[1] + 1.0f,
                                 from[2] + dx * s + dz * c};
            wave_.cast(from, to);
        }
        const int index = sim::skillIndexOf(sim::skill::kPowerWave);
        if (index >= 0 && heard_.skill[index] >= 0) emit(heard_.skill[index], from[0], from[2]);
    }
    waveCasts_.erase(std::remove_if(waveCasts_.begin(), waveCasts_.end(),
                                    [](const IceCast& c) { return c.wait <= 0.0f; }),
                     waveCasts_.end());
    for (IceCast& cast : thunderCasts_) {
        cast.wait -= float(seconds);
        if (cast.wait > 0.0f || ground_ == nullptr) continue;
        const Drawn* caster = drawnOf(cast.caster);
        const Drawn* target = drawnOf(cast.target);
        if (caster == nullptr || target == nullptr || !caster->placed || !target->placed) continue;
        // The middle of the target, and the caster's chest, as a hero's Lightning is drawn.
        const FigureBody* look = target->figure.body();
        const float tall = look ? look->height * look->scale : 1.0f;
        const float to[3] = {target->crown[0], target->crown[1] - tall * 0.5f, target->crown[2]};
        float from[3];
        castFrom(*caster, to, from);
        thunder_.strike(from, to, cast.target);
        const int index = sim::skillIndexOf(sim::skill::kLightning);
        if (index >= 0 && heard_.skill[index] >= 0) emit(heard_.skill[index], from[0], from[2]);
    }
    thunderCasts_.erase(std::remove_if(thunderCasts_.begin(), thunderCasts_.end(),
                                       [](const IceCast& c) { return c.wait <= 0.0f; }),
                        thunderCasts_.end());
    arrows_.update(float(seconds), middle);
    for (uint32_t shooter : arrows_.landed()) showing_.rush(shooter);
    meteor_.fly(float(seconds), standing, middle);
    wave_.update(float(seconds));
    blink_.update(float(seconds));
    ice_.update(float(seconds));
    poison_.update(float(seconds));
    flame_.update(float(seconds));
    spirits_.update(float(seconds), [&](uint32_t id, float* feet) {
        const Drawn* drawn = drawnOf(id);
        if (drawn == nullptr || !drawn->placed || !ground_) return false;
        feet[0] = drawn->crown[0];
        feet[1] = ground_->heightAt(drawn->crown[0], drawn->crown[2]);
        feet[2] = drawn->crown[2];
        return true;
    });
    if (blinkOut_ >= 0.0f) blinkOut_ += float(seconds);
    // A blink the realm dropped -- he died in the fade -- is never put down: he is drawn again.
    if (blinkOut_ >= 0.0f && realm_.hero().blinkAt == 0) blinkOut_ = -1.0f;
    if (blinkIn_ >= 0.0f) {
        blinkIn_ += float(seconds);
        if (blinkIn_ >= kBlinkFadeSeconds) blinkIn_ = -1.0f;
    }
    thunder_.update(float(seconds), standing, middle);
    // Twisting Slash's wheel follows him as he is drawn, and sounds as it starts to turn.
    wheel_.update(
        float(seconds),
        [&](uint32_t id, float* feet, float& yaw) {
            const Drawn* drawn = drawnOf(id);
            if (drawn == nullptr || !drawn->placed || drawn->deadFor >= 0.0f) return false;
            feet[0] = drawn->crown[0];
            feet[2] = drawn->crown[2];
            feet[1] = ground_ ? ground_->heightAt(feet[0], feet[2]) : 0.0f;
            yaw = drawn->yaw;
            return true;
        },
        [&](uint32_t id, const float* feet) {
            const int index = sim::skillIndexOf(sim::skill::kTwistingSlash);
            if (index >= 0 && heard_.skill[index] >= 0) emit(heard_.skill[index], feet[0], feet[2], id);
        });
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
            if (dx * dx + dz * dz < kShockTiles * kShockTiles) shock(one, one.shockClip);
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
        Drawn* swinger = drawnOf(cue.attacker);
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
        // the AttackTime block beside the blood, and only the blood asks `tc->Hit`. Once a
        // swing: a spin settling on four bodies is one release. A blow that FLEW sounded its
        // hit at the release (`Loosed`, above), and a poison's pulse or a trap has none.
        if (!cue.thrown && swinger->placed && swinger->heardToken != cue.token) {
            swinger->heardToken = cue.token;
            const int hit = cue.arrow ? heard_.missile : heard_.hit;
            if (hit >= 0) emit(hit, swinger->crown[0], swinger->crown[2], swinger->id);
        }
        // Her own arrow sounds again where it goes in (CheckClientArrow, ZzzEffect.cpp:6620),
        // miss or not: the client's own test found the body, not the server.
        if (cue.thrown && cue.arrow && cue.attacker == realm_.hero().id && heard_.missile >= 0) {
            emit(heard_.missile, x, z);
        }
        if (!cue.miss && cue.damage > 0 && target->alive()) {
            if (Drawn* struck = drawnOf(cue.target)) flinch(*struck, onHero);
        }
    }
    // After the cues, so the fall comes in the same frame as the number and the blood of the
    // blow that caused it -- or at once, for a death no blow is still owed on.
    fallWhenLanded();
    // And the level, in the same frame as the blow that earned it -- MU2's Rose, reached from
    // the kill's cue. A dropped cue still clears `awaits`, so a level is never lost to one.
    // A quest's levels are a sequence, each its own rise and sound, kLevelApart after the last
    // -- INVENTION, the user's (2026-09-30): MU's while loop sends one ReceiveLevelUp a level,
    // all on the same frame, and a quest's three levels read as one. A kill's levels rise once
    // (the user, 2026-10-01: "if by killing we get multiple lvls we play it once").
    levelWait_ = std::max(0.0f, levelWait_ - float(seconds));
    if (levelsOwed_ > 0 && levelWait_ <= 0.0f && (levelOn_ == 0 || !showing_.awaits(levelOn_))) {
        --levelsOwed_;
        levelOn_ = 0;
        levelWait_ = kLevelApart;
        core::logf("level: the rise at %d, %d more owed", realm_.hero().level - levelsOwed_,
                   levelsOwed_);
        rise();
    }
    releaseDrops();
    watchAggro(float(seconds));
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
    // And his pet, off slot 8 while it has life: the Angel flies, the Imp rides (game/pets.h).
    if (isOpen()) {
        if (Drawn* hero = drawnOf(realm_.hero().id)) {
            int pet = -1;
            const sim::Held& worn = realm_.satchel()[sim::kPet];
            if (!worn.empty() && size_t(worn.item) < tables_.items.size()) {
                const content::ItemRow& row = tables_.items[size_t(worn.item)];
                if (row.group == sim::kGroupPets && worn.durability > 0) pet = row.number;
            }
            pets_.update(float(seconds), hero->figure, pet, realm_.hero().alive());
        }
    }
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
        // Noria's eight, said by its elves -- no Lorencia guard ever sees one. Ours, the user's
        // of 2026-09-29, in the Lorencia guards' manner.
        // The last match wins, so the Elite Goblin comes after the goblin it also matches.
        {"goblin", {"Goblins on the road! Loose!", "Back to your holes, you little thieves!",
                    "Not one step into Noria!"}},
        {"elite goblin", {"Their captain! Bring him down!", "Loose! Loose on the big one!",
                          "Not so elite with an arrow in you!"}},
        {"scorpion", {"Scorpion! Mind the tail!", "Back to the sand, crawler!",
                      "Pin it before it strikes!"}},
        {"beetle", {"Beetle on the road! Loose!", "Crack that shell!",
                    "Come here, you armoured pest!"}},
        {"hunter", {"A hunter! Draw on him first!", "Not in our forest, poacher!",
                    "Loose before he does!"}},
        {"forest", {"The woods have turned! Loose!", "Back into the trees, rot-heart!",
                    "Burn in your own roots!"}},
        {"agon", {"Agon! Keep your distance and loose!", "Come here, you ugly brute!",
                  "Not one step nearer the town!"}},
        {"golem", {"A golem! Aim for the joints!", "Back to the rocks, stone-head!",
                   "Keep it off the gate! Loose!"}},
    };
    static const char* const kPlain[] = {"Come here, bastard!", "Not past this gate!",
                                         "To arms! Monster at the gate!"};

    // The speaker too: a road's two guards take on the same monster on the same tick, and
    // without him in it they said the same line together.
    const uint32_t pick = happening.whom + happening.tick + happening.who;
    // Who walks the rounds a Chat or a Salute is about, by MU's NPC number: 229 Marlon, 257 Peia.
    const auto roundsOf = [&](uint32_t id) {
        const sim::Body* one = realm_.find(id);
        return one && one->warden >= 0 ? tables_.folk[size_t(one->warden)].number : 0;
    };
    std::string line;
    if (happening.a == int32_t(sim::Shout::Greet)) {
        // The Guild Master, Sevina or the Messenger, spoken to (Realm's Talk). Ours, the user's of
        // 2026-09-29: MU's guild window has nothing to open alone, so he says it is not ready yet
        // (the user, 2026-09-30), in Devin's plain register.
        static const char* const kGuildMaster[] = {
            "The guild halls are not ready yet, traveller. Come back another day.",
            "No banners are being sworn yet. Devias is not ready for guilds.",
            "Not yet. When the guilds open, you will hear of it first.",
        };
        // The Messenger of Archangel, whose Blood Castle is still to come (the user, 2026-09-30).
        static const char* const kMessenger[] = {
            "The Archangel's castle is not open yet. Its gate is still sealed.",
            "Not yet, warrior. Blood Castle is not ready for you.",
            "The Archangel has not called for help yet. Wait for his word.",
        };
        // Charon, whose Devil Square is still to come (the user, 2026-09-30).
        static const char* const kCharon[] = {
            "The Devil's Square is not open yet. Its doors stay shut.",
            "Not yet, mortal. The square is not ready for you.",
            "Come back when the square opens. You will know the hour.",
        };
        // Sevina, whose class change is still to come (the user, 2026-09-30): the hero is not
        // ready. Hers is the quest the Soul Master and the Blade Knight are born of.
        static const char* const kSevina[] = {
            "You are not ready, child. The path beyond your strength is not yet open to you.",
            "Not yet. Grow stronger, and come back to me when the gods can hear you.",
            "I see what you could become. But not today.",
        };
        const int folk = happening.c;
        if (folk < 0 || size_t(folk) >= tables_.folk.size()) return;
        said_.erase(std::remove_if(said_.begin(), said_.end(),
                                   [&](const Said& one) { return one.folk == folk; }),
                    said_.end());
        Said one;
        one.who = happening.who;
        // And Devin, before Lorencia or Noria is cleared (the user, 2026-09-30): his quest waits
        // on theirs, and he sends the hero back to them, in his short, serious register.
        static const char* const kDevinNotYet[] = {
            "Not yet. Lorencia and Noria still need you more than I do.",
            "Help Marlon or Peia first. Then come to me.",
            "The south is not safe yet. Finish there, and Devias will be waiting.",
        };
        const int32_t number = tables_.folk[size_t(folk)].number;
        const char* const* lines = number == sim::kSevina      ? kSevina
                                   : number == sim::kMessenger ? kMessenger
                                   : number == sim::kCharon    ? kCharon
                                   : sim::questOf(number) >= 0 ? kDevinNotYet
                                                               : kGuildMaster;
        one.line = lines[realm_.tick() % 3];
        one.folk = folk;
        said_.push_back(one);
        core::logf("greet: tick %lld, %s says \"%s\"", (long long)realm_.tick(),
                   tables_.folk[size_t(folk)].name.c_str(), one.line.c_str());
        return;
    }
    if (happening.a == int32_t(sim::Shout::Chat)) {
        // Marlon and Lumen at her bar (realm_folk.cpp): her line and then his, turn about, over
        // whichever of them says it. Ours, the user's of 2026-09-29.
        static const char* const kBarTalk[] = {
            "The usual, Sir Marlon?",
            "The usual, Lumen. And make it strong.",
            "You look tired. Were you on the walls all night again?",
            "The dead were on the old road again. More of them than last week.",
            "The merchants say the Dungeon gate groans after dark.",
            "Let them talk. Keep your doors barred at night.",
            "And who keeps watch over you, Sir Marlon?",
            "The oath does. Same as always.",
            "Then drink. The oath can wait one cup.",
            "...One cup.",
        };
        static_assert(sizeof(kBarTalk) / sizeof(kBarTalk[0]) == sim::kChatLines,
                      "one line for every line the realm says");
        // Peia and Elf Lala under her harp, Lala first. Ours, the user's of 2026-09-29: "elf
        // quest giver has to talk with elf lala at some point". The song gone quiet is Peia's
        // quest's own story (sim/quests.cpp, "Noria's Song").
        static const char* const kHarpTalk[] = {
            "Peia! Stay a while. The harp has missed its listener.",
            "Only a while, Lala. The roads don't watch themselves.",
            "The trees were restless last night. The song kept breaking.",
            "Goblins at the west road again. Bolder than last week.",
            "And the golems? They never came so near the town before.",
            "Something is waking them. Something under the forest.",
            "Then I'll play louder. The old songs keep the dark back.",
            "Play, then. My archers will keep the rest.",
            "Come back before dusk. I'll save you the last song.",
            "...The last song, then.",
        };
        static_assert(sizeof(kHarpTalk) / sizeof(kHarpTalk[0]) == sim::kChatLines,
                      "one line for every line the realm says");
        if (happening.b < 0 || happening.b >= sim::kChatLines) return;
        const char* const* talk = roundsOf(happening.who) == 257 ? kHarpTalk : kBarTalk;
        const int folk = happening.c;
        said_.erase(std::remove_if(said_.begin(), said_.end(),
                                   [&](const Said& one) {
                                       return folk >= 0 ? one.folk == folk
                                                        : one.folk < 0 && one.who == happening.who;
                                   }),
                    said_.end());
        Said one;
        one.who = happening.who;
        one.line = talk[happening.b];
        one.folk = folk;
        said_.push_back(one);
        core::logf("bar: tick %lld, %s says \"%s\"", (long long)realm_.tick(),
                   folk >= 0 ? tables_.folk[size_t(folk)].name.c_str() : nameOf(happening.who).c_str(),
                   one.line.c_str());
        return;
    }
    if (happening.a == int32_t(sim::Shout::Salute)) {
        // To the last knight of Lorencia, as he comes by the post (realm_folk.cpp).
        static const char* const kSalutes[] = {"Sir Marlon!", "All quiet at the gate, sir.",
                                               "The gate holds, sir."};
        // And Noria's watch to Peia, as she looks in on each road.
        static const char* const kElfSalutes[] = {"Captain Peia!", "The road is quiet, captain.",
                                                  "Nothing gets past us, captain."};
        line = roundsOf(happening.whom) == 257 ? kElfSalutes[pick % 3] : kSalutes[pick % 3];
    } else if (happening.a == int32_t(sim::Shout::Pointing)) {
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
        // The Golden Archer keeps the gate in his own few words, and keeps count (golden-archer.md).
        static const char* const kArcher[] = {"None pass the gate.", "Back down the stair.",
                                              "One more for the count."};
        if (const sim::Body* speaker = realm_.find(happening.who);
            speaker && speaker->warden >= 0 && tables_.folk[size_t(speaker->warden)].number == 236) {
            line = kArcher[pick % 3];
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
                       dead && dead->crumbles ? "comes apart into stones, leaving no corpse"
                       : dead && dead->bursts ? "comes apart into bones, leaving no corpse"
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

void Play::shock(Drawn& one, int clip) {
    one.figure.play(clip, true);
    one.shocked = one.figure.length();
}

// The flinch: SetPlayerShock (ZzzCharacter.cpp:1365), from ReceiveAttackDamage's `else`, which is
// every ordinary hit under OpenMU -- it never raises the target id's top bit on a hit. There
// anything but the hero takes a shock one blow in two, unless it is mid-attack: MONSTER01_SHOCK,
// and on its first frame the breed's attack pair, `Sounds[2 + rand() % 2]`. SetAction to the
// clip already playing changes nothing, so a flinch in progress is not begun again and cries
// once. A killing blow is left to the fall that comes this frame.
//
// The hero flinches too, and that is **ours**: under 0.75 the `else` has no shock for him, and
// MU flinches him only from the `if` a Webzen server's success bit reaches. The user's, 2026-09-29,
// on the same coin. What the `if` does to him is MU's: PLAYER_SHOCK, `c->Movement = false`, and
// pMaleScream1-3 or pFemaleScream1-2 by IsFemale. MU also refuses a click to move while the clip
// plays (ZzzInterface.cpp:3127); here it does not, on the user's word of 2026-09-29, and the walk
// the click starts ends the clip. Not while he swings or casts -- MU's player branch cuts a
// swing, but here the swing carries the landing cue of his own blow -- and not while he chases
// a monster, which the halt would cancel: a stop that costs him his fight is not a flinch.
void Play::flinch(Drawn& struck, bool isHero) {
    if (!struck.placed || struck.swinging > 0.0f || struck.shocked > 0.0f) return;
    const FigureBody* look = struck.figure.body();
    int clip = struck.shockClip;
    if (isHero) {
        if (struck.casting > 0.0f || realm_.casting()) return;
        // Not while he walks, either: the flinch's stop cancelled the walk, so under a Lich's
        // fire at Lorencia's dungeon arch every click was undone within a second and he could
        // not reach the stair (the user, 2026-09-30: "monster keep hitting char and you are not
        // going inside"). A walking hero takes the blow and walks on.
        if (realm_.hero().walking) return;
        clip = look && look->library ? look->library->find(kPlayerShockSlot) : -1;
    }
    if (clip < 0 || struck.figure.clip() == clip) return;
    flinchDice_ ^= flinchDice_ << 13;
    flinchDice_ ^= flinchDice_ >> 17;
    flinchDice_ ^= flinchDice_ << 5;
    if ((flinchDice_ & 1u) == 0) return;
    shock(struck, clip);
    const bool silent = look && look->name == kSilentFlinchFigure;
    const int cry = !isHero ? (silent ? -1 : struck.cryAttack)
                            : (look && look->female ? heard_.shockFemale : heard_.shock);
    if (cry >= 0) emit(cry, struck.crown[0], struck.crown[2], struck.id);
}

}  // namespace mu::game

