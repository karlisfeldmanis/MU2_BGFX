// What the realm sounds like: the swing from what is in his hands, the footsteps read off the
// walk cycle's own clock, Hanzo's hammer, a thing landing on the grass, and the ears placed on
// the camera each frame.
//
// Every placed sound goes through `emit`, and is heard only if the camera holds where it is.
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
#include "game/world/ornaments.h"

namespace mu::game {

int Play::swingSound(const sim::Body& body) const {
    // The player half of the `AnimationFrame == 0.f` block at the end of SetPlayerAttack, in
    // the client's own order, and NOT derived from the clip: a Berdysh and a Kris play
    // different actions and the same sound. MU2's Crowd.Swinging.
    const auto arm = [&](int32_t i) -> const content::Arm* {
        return i >= 0 && size_t(i) < tables_.arms.size() ? &tables_.arms[size_t(i)] : nullptr;
    };
    const content::Arm* right = arm(body.weapon);
    const content::Arm* left = arm(body.shield);
    for (const content::Arm* held : {right, left}) {
        if (held && held->bow()) return heard_.bow;
    }
    for (const content::Arm* held : {right, left}) {
        if (held && held->crossbow()) return heard_.crossbow;
    }
    // MODEL_SWORD+10 and MODEL_SPEAR -- the Light Saber and the Light Spear at (3,0), the
    // group's first item and not the group, so the Berdysh is not given the long swing.
    if (right && ((right->group == 0 && right->number == 10) ||
                  (right->group == 3 && right->number == 0))) {
        return heard_.swingLong;
    }
    // Bare hands make no swing sound in MU. They do here: an empty hand swings the sword's clip
    // now (sim/swings.cpp), and the same swing in silence read as something missing. Ours.
    return heard_.swing;
}

void Play::steps() {
    // PlayWalkSound, and the hero's alone: the client guards it with `c == Hero`, so nobody
    // else in the world has feet you can hear. MU2's Crowd.Steps.
    Drawn* hero = drawnOf(realm_.hero().id);
    const sim::Body& him = realm_.hero();
    const FigureBody* look = hero ? hero->figure.body() : nullptr;
    const int clip = hero ? hero->figure.clip() : -1;
    const bool walking = hero && look && him.alive() && hero->visible && clip >= 0 &&
                         (clip == look->walkClip || clip == look->walkSafeClip ||
                          clip == look->runClip);
    if (!walking || ground_ == nullptr) {
        // Not walking, so the next cycle starts fresh: the client clears both latches the
        // moment the animation is not running.
        leftFoot_ = rightFoot_ = striding_ = false;
        return;
    }
    const float key = keyOf(hero->figure);
    // A change of walk -- into the run or out of it, over the zone's edge -- keeps the phase
    // but not the key count, so it is a fresh start and not a wrap.
    if (clip != stepClip_) striding_ = false;
    stepClip_ = clip;
    // The run is not MU's and lands its feet where its own clip does (FigureBody::runFeet).
    const bool run = clip == look->runClip && look->runFeet[0] >= 0.0f;
    const float firstFoot = run ? look->runFeet[0] : kFirstFoot;
    const float secondFoot = run ? look->runFeet[1] : kSecondFoot;
    // Setting off part way through a cycle -- a walk resumes where it was left -- a foot the
    // phase has already gone past counts as heard, or a walk picked up at three quarters would
    // crunch on the frame it starts with no foot landing under it.
    if (!striding_) {
        striding_ = true;
        leftFoot_ = key > firstFoot;
        rightFoot_ = key > secondFoot;
        stepKey_ = key;
    }
    // The cycle wrapped, which is where MU clears them. Seen as the key going back rather than
    // as MU's `key < 1.5`, because the run's first foot may land on key 0.
    if (key < stepKey_) leftFoot_ = rightFoot_ = false;
    stepKey_ = key;
    if (key < firstFoot) return;
    const auto tread = [&]() {
        const float metresPerTile = ground_->metresPerTile();
        const int column = int(std::floor(hero->crown[0] / metresPerTile));
        const int row = int(std::floor(-hero->crown[2] / metresPerTile));
        const int floor = ground_->floorAt(column, row);
        // Devias's arm comes first in PlayWalkSound: snow everywhere but its planks and its
        // four patterned floors, which fall through to the soil step. The snow is heard as the
        // grass step, not MU's pWalk(Snow): the user's call (2026-09-29), it made more sense.
        const int sound = snowy_ ? (floor != 3 && floor < 10 ? heard_.grass : heard_.soil)
                          : floor == kGrassFloor ? heard_.grass
                                                 : heard_.soil;
        if (sound >= 0) emit(sound, hero->crown[0], hero->crown[2], hero->id);
    };
    if (!leftFoot_) {
        leftFoot_ = true;
        tread();
    }
    if (!rightFoot_ && key >= secondFoot) {
        rightFoot_ = true;
        tread();
    }
}

void Play::hammer() {
    if (heard_.hammer < 0) return;
    for (Standing& one : folk_) {
        if (!one.smith) continue;
        // The blow, and not whatever else he does: action0 is the eleven-key swing and his
        // other clip is a look along a blade that would ring the anvil at its side.
        const float key = keyOf(one.figure);
        if (slotOf(one.figure) != 0 || key < kHammerFrom || key > kHammerTo) {
            one.rung = false;
            continue;
        }
        if (one.rung) continue;
        one.rung = true;
        // Placed at him, where MU plays it unplaced: a smith heard from the far bank is the
        // wrong half of MU's simplification to keep. MU2's call, marked there too.
        const float* at = one.figure.position();
        emit(heard_.hammer, at[0], at[2]);
    }
}

void Play::gatherFolkLights(gfx::Effects& effects) const {
    // Charon's light: two of the one sheet turning against each other, and the wisps homing in.
    if (bgfx::isValid(orbSheet_)) {
        const float lit = std::sin(folkClock_ * 1000.0f * 0.002f) * 0.35f + 0.65f;
        for (const Standing& one : folk_) {
            if (one.orbBone < 0) continue;
            float at[3];
            if (!one.figure.pointOn(one.orbBone, kCharonOrbAt, at)) continue;
            for (const float turn : {1.0f, -1.0f}) {
                gfx::Sprite sprite;
                for (int i = 0; i < 3; ++i) sprite.position[i] = at[i];
                sprite.halfWidth = sprite.halfHeight = kCharonOrbHalf;
                sprite.spin = turn * std::fmod(folkClock_ * kCharonOrbSpin, 6.2831853f);
                sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = lit;
                sprite.sheet = orbSheet_;
                sprite.blend = gfx::Blend::Additive;
                effects.add(sprite);
            }
        }
    }
    if (bgfx::isValid(wispSheet_)) {
        for (const Wisp& wisp : wisps_) {
            // The ribbon's eight points, brightest at the head; and the head a spark of the
            // orb's own sheet, MU's CreateParticle(BITMAP_LIGHTNING+1, ..., 3, 0.05f). Ours: the
            // points are the round `light` sheet in JointEnergy01's own pale blue -- the 8x4
            // strip is a ribbon's cross-section and drawn as a billboard it is a hard box.
            const bgfx::TextureHandle dot = bgfx::isValid(folkLight_) ? folkLight_ : wispSheet_;
            for (int t = 0; t < wisp.tails; ++t) {
                gfx::Sprite sprite;
                for (int i = 0; i < 3; ++i) sprite.position[i] = wisp.tail[t][i];
                sprite.halfWidth = sprite.halfHeight = 0.10f * (1.0f - 0.08f * float(t));
                const float fade = 1.0f - float(t) / 8.0f;
                sprite.colour[0] = 0.40f * fade;
                sprite.colour[1] = 0.42f * fade;
                sprite.colour[2] = 0.60f * fade;
                sprite.sheet = dot;
                sprite.blend = gfx::Blend::Additive;
                effects.add(sprite);
            }
            if (bgfx::isValid(orbSheet_)) {
                gfx::Sprite spark;
                for (int i = 0; i < 3; ++i) spark.position[i] = wisp.position[i];
                spark.halfWidth = spark.halfHeight = 0.5f * 1.28f * 0.05f;
                spark.sheet = orbSheet_;
                spark.blend = gfx::Blend::Additive;
                effects.add(spark);
            }
        }
    }
    if (!bgfx::isValid(folkLight_)) return;
    const float luminosity = std::sin(folkClock_ * 1000.0f * 0.002f) * 0.3f + 0.7f;
    for (const Standing& one : folk_) {
        if (one.glowBone < 0) continue;
        gfx::Sprite sprite;
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        if (!one.figure.pointOn(one.glowBone, origin, sprite.position)) continue;
        // CreateSprite's Scale over a 64-texel sheet, in metres.
        sprite.halfWidth = sprite.halfHeight = 0.5f * 0.64f * one.glowScale;
        sprite.colour[0] = luminosity;
        sprite.colour[1] = 0.6f * luminosity;
        sprite.colour[2] = 0.4f * luminosity;
        sprite.sheet = folkLight_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    // And the monsters' own: the Chain Scorpion's (1, 0.4, 0.2) at Scale 1, drawn while its
    // body is -- through the fall, as RenderCharacter draws it.
    for (const Drawn& one : drawn_) {
        if (one.lightBone < 0 || !one.visible || !one.placed) continue;
        gfx::Sprite sprite;
        const float origin[3] = {0.0f, 0.0f, 0.0f};
        if (!one.figure.pointOn(one.lightBone, origin, sprite.position)) continue;
        sprite.halfWidth = sprite.halfHeight = 0.5f * 0.64f;
        sprite.colour[0] = monsterLuminosity_;
        sprite.colour[1] = 0.4f * monsterLuminosity_;
        sprite.colour[2] = 0.2f * monsterLuminosity_;
        sprite.sheet = folkLight_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    // And the Gorgon Staff's star in the Gorgon's fist: Scale 2 of Shiny02, 90 units along the
    // staff from the grip, (0.4, 0.8, 0.6) on the same rolled Luminosity. MU writes it as
    // (0, -90, 0) in its link bone's frame; the held staff here sits on knife_gdf with no turn
    // (Figure::gather), its length on its own +Z (Staff05.glb: -0.85 to 1.39 m, the skull at
    // 0.60-0.88), so the same 90 units is +Z in this bone's frame.
    if (!bgfx::isValid(starSheet_)) return;
    for (const Drawn& one : drawn_) {
        if (one.starBone < 0 || !one.visible || !one.placed) continue;
        gfx::Sprite sprite;
        const float down[3] = {0.0f, 0.0f, 0.9f};
        if (!one.figure.pointOn(one.starBone, down, sprite.position)) continue;
        // Shiny02 is 32 texels wide, so Scale 2 is 64 units: 0.64 m across. And **ours**: the
        // light at 0.6 of MU's -- the sheet peaks at full white (the flare at 154), and added in
        // HDR it burned to a white star where MU's clipped GL add read pale green (the user,
        // 2026-09-30: "too bright and weird").
        sprite.halfWidth = sprite.halfHeight = 0.5f * 0.32f * 2.0f;
        constexpr float kStarDim = 0.6f;
        sprite.colour[0] = 0.4f * kStarDim * monsterLuminosity_;
        sprite.colour[1] = 0.8f * kStarDim * monsterLuminosity_;
        sprite.colour[2] = 0.6f * kStarDim * monsterLuminosity_;
        sprite.sheet = starSheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

// Charon's wisps: thrown every half second from a point up to 50 units round his light, risen
// 5 units a reference frame for the first twenty, then humming home at a speed that climbs 5 a
// frame to 30, and gone inside 35 units of it or at 120 frames (ZzzEffectJoint.cpp:210-226,
// 3282-3360). MU's MoveHumming turns by the speed in degrees a frame, which here is a straight
// line home; and the SOUND_GET_ENERGY it plays on every landing, twice a second, is left out.
void Play::orbs(float seconds) {
    const float frames = seconds * 25.0f;
    const auto roll = [&]() {
        wispDice_ ^= wispDice_ << 13;
        wispDice_ ^= wispDice_ >> 17;
        wispDice_ ^= wispDice_ << 5;
        return float(wispDice_ % 100) * 0.01f - 0.5f;  // -0.5 to 0.49 m: MU's rand()%100-50
    };
    for (Standing& one : folk_) {
        if (one.orbBone < 0 || !bgfx::isValid(wispSheet_)) continue;
        one.wispOwed += seconds / kCharonWispEvery;
        while (one.wispOwed >= 1.0f) {
            one.wispOwed -= 1.0f;
            float orb[3];
            if (!one.figure.pointOn(one.orbBone, kCharonOrbAt, orb) || wisps_.size() >= 32) break;
            Wisp wisp;
            for (int i = 0; i < 3; ++i) {
                wisp.target[i] = orb[i];
                wisp.position[i] = orb[i] + roll();
            }
            wisps_.push_back(wisp);
        }
    }
    // The tail is laid a point a reference frame, as MU's joint keeps its last eight.
    wispStep_ += frames;
    const bool lay = wispStep_ >= 1.0f;
    if (lay) wispStep_ = std::fmod(wispStep_, 1.0f);
    for (Wisp& wisp : wisps_) {
        wisp.age += frames;
        float away[3], distance = 0.0f;
        for (int i = 0; i < 3; ++i) {
            away[i] = wisp.target[i] - wisp.position[i];
            distance += away[i] * away[i];
        }
        distance = std::sqrt(distance);
        if (wisp.age <= 20.0f) {
            wisp.position[1] += 0.05f * frames;
        } else if (distance > 0.0f) {
            wisp.velocity = std::min(30.0f, wisp.velocity + 5.0f * frames);
            const float step = std::min(distance, wisp.velocity * 0.01f * frames);
            for (int i = 0; i < 3; ++i) wisp.position[i] += away[i] / distance * step;
        }
        if (lay) {
            for (int t = std::min(wisp.tails, 7); t > 0; --t) {
                for (int i = 0; i < 3; ++i) wisp.tail[t][i] = wisp.tail[t - 1][i];
            }
            for (int i = 0; i < 3; ++i) wisp.tail[0][i] = wisp.position[i];
            wisp.tails = std::min(8, wisp.tails + 1);
        }
    }
    wisps_.erase(std::remove_if(wisps_.begin(), wisps_.end(),
                                [](const Wisp& w) {
                                    if (w.age >= 120.0f) return true;
                                    if (w.age <= 20.0f) return false;
                                    float d = 0.0f;
                                    for (int i = 0; i < 3; ++i) {
                                        const float a = w.target[i] - w.position[i];
                                        d += a * a;
                                    }
                                    return d <= 0.35f * 0.35f;
                                }),
                 wisps_.end());
}

void Play::chatter(float seconds) {
    folkClock_ = std::fmod(folkClock_ + seconds, 3600.0f);
    monsterRollWait_ -= seconds;
    if (monsterRollWait_ <= 0.0f) {
        monsterRollWait_ = 1.0f / 25.0f;
        monsterRoll_ ^= monsterRoll_ << 13;
        monsterRoll_ ^= monsterRoll_ >> 17;
        monsterRoll_ ^= monsterRoll_ << 5;
        monsterLuminosity_ = float(monsterRoll_ % 8u + 2u) * 0.1f;
    }
    for (Standing& one : folk_) {
        if (one.voice < 0) continue;
        one.busy -= seconds;
        if (one.busy > 0.0f) continue;
        one.dice ^= one.dice << 13;
        one.dice ^= one.dice >> 17;
        one.dice ^= one.dice << 5;
        const float roll = float(one.dice % 10000u) / 10000.0f;
        if (roll >= seconds / one.every) continue;
        const float* at = one.figure.position();
        emit(one.voice, at[0], at[2]);
        one.busy = sound_.seconds(one.voice);
    }
}

void Play::landed(uint32_t drop) {
    if (ground_ == nullptr) return;
    for (const sim::Lying& one : realm_.lying()) {
        if (one.id != drop) continue;
        // CreateItemDrop's branch: SOUND_JEWEL01 for the jewels, SOUND_DROP_ITEM01 for any
        // other thing. MU also rings CreateMoneyDrop's SOUND_DROP_MONEY01 for Zen and this
        // does NOT -- the user's call, 2026-09-23, and it follows from the sweep: Zen is picked
        // up the moment he walks onto it, so the coins land and are collected within a second
        // of each other and the two sounds tread on one another. The coins are kept for the
        // half that is worth hearing, which is the taking.
        if (one.what.empty()) return;
        int sound = heard_.itemDrop;
        if (one.what.item >= 0 && size_t(one.what.item) < tables_.items.size() &&
                   tables_.items[size_t(one.what.item)].jewel() && heard_.jewel >= 0) {
            sound = heard_.jewel;
        }
        const float metresPerTile = ground_->metresPerTile();
        emit(sound, (float(one.column) + 0.5f) * metresPerTile,
                      -(float(one.row) + 0.5f) * metresPerTile);
        return;
    }
}

void Play::emit(int event, float x, float z, uint32_t following) {
    // Only what the camera holds is heard. **A departure from MU**, whose falloff mixes every
    // attached object however far off it is and plays the townspeople's noises unplaced
    // everywhere: at 1/d past two and a half metres, Hanzo's anvil carried from the square to
    // Harold's campfire fifty tiles away. MU2 already culled its scenery sounds to the shot;
    // this is that rule for everything placed. A voice already sounding is not cut off when
    // its source leaves the frame -- only a new one is refused.
    // Past the frame's edge the voice would be silent anyway (Sound's kEdgeSilent); this is the
    // early out, and the fade is the rule.
    const float y = ground_ ? ground_->heightAt(x, z) + kHeardHeight : 0.0f;
    if (shotKnown_ && ground_) {
        const Frustum frustum(shot_);
        const float centre[3] = {x, y, z};
        if (!frustum.holds(centre, kHeardReach)) return;
    }
    sound_.playAt(event, x, y, z, following);
    // The hero's big moments lean the world back (docs/spatial-sound.md, C): his fall, and a
    // skill he casts. Not a landed blow, which is every second of a fight.
    const uint32_t him = realm_.hero().id;
    bool big = event == heard_.die || event == heard_.dieFemale;
    for (int skill : heard_.skill) big = big || (skill >= 0 && event == skill && following == him);
    if (big) sound_.duck();
}

void Play::ui(Ui which) {
    switch (which) {
        case Ui::Click: sound_.play(heard_.click); break;
        case Ui::Refused: sound_.play(heard_.refused); break;
        case Ui::Took: sound_.play(heard_.take); break;
        case Ui::Opened: sound_.play(heard_.opened); break;
    }
}

void Play::hear(const gfx::Camera& camera, bool indoors) {
    if (!sound_.isOpen()) return;
    // The air: on while he is not under a roof, which is the client's own switch -- it stops
    // SOUND_WIND01 on HeroTile 4. Unplaced: wind is not somewhere, it is everywhere.
    // The Dungeon's aDungeon rides that slot, and the Dungeon is under a roof on every tile:
    // its air plays throughout, as SceneManager.cpp:859-861 loops it for the whole map.
    sound_.loop(heard_.wind, !indoors || dungeonAir_);
    // And the same switch is the room: a slap off the town's walls in the open, a small room
    // under a roof (docs/spatial-sound.md, E).
    sound_.room(indoors ? Sound::Room::Roofed : Sound::Room::Open);
    // The walls are the rules' own line of sight on the tile grid (F). The far end is pulled a
    // tile back toward the ears first: a smith at his anvil or a thing lying against a house
    // stands on or beside a closed tile, and is not behind it. Within two tiles nothing is.
    sound_.walls(
        [](void* context, const float from[3], const float to[3]) {
            const Play& play = *static_cast<const Play*>(context);
            if (play.ground_ == nullptr) return true;
            const float perTile = std::max(play.ground_->metresPerTile(), 0.001f);
            const float ax = from[0] / perTile - 0.5f, ay = -from[2] / perTile - 0.5f;
            float bx = to[0] / perTile - 0.5f, by = -to[2] / perTile - 0.5f;
            const float dx = ax - bx, dy = ay - by;
            const float tiles = std::sqrt(dx * dx + dy * dy);
            if (tiles <= 2.0f) return true;
            bx += dx / tiles;
            by += dy / tiles;
            return play.realm_.router().sees(ax, ay, bx, by, content::kWallNoMove);
        },
        this);
    const Drawn* hero = drawnOf(realm_.hero().id);
    if (hero == nullptr || !hero->placed) return;
    // The ears at the character, the pan from the shot point() kept this frame.
    if (!shotKnown_) return;
    (void)camera;
    sound_.listen(hero->id, hero->crown, shot_);
    sound_.follow(
        [](void* context, uint32_t id, float* x, float* y, float* z) {
            const Drawn* one = static_cast<Play*>(context)->drawnOf(id);
            if (one == nullptr || !one->placed || !one->visible) return false;
            *x = one->crown[0];
            *y = one->crown[1];
            *z = one->crown[2];
            return true;
        },
        this);
}

void Play::hearWorld(const Lamps* lamps, const Ornaments& ornaments, Inside inside,
                     void* context) {
    if (!sound_.isOpen()) return;
    const Drawn* hero = drawnOf(realm_.hero().id);
    const bool placed = hero != nullptr && hero->placed && shotKnown_;
    float fire[3], water[3];
    // An indoor hearth is passed over while he is out of doors, and the next fire out there is
    // the one heard. In doors he hears either, and the walls' line of sight does the rest.
    struct Ears {
        Inside inside;
        void* context;
        bool in;
    } ears{inside, context, false};
    if (placed && inside != nullptr) ears.in = inside(context, hero->crown[0], hero->crown[2]);
    const auto heard = [](void* self, const float at[3]) {
        const Ears& e = *static_cast<const Ears*>(self);
        return e.inside == nullptr || e.in || !e.inside(e.context, at[0], at[2]);
    };
    const bool burns =
        placed && lamps != nullptr && lamps->nearestBonfire(hero->crown, fire, heard, &ears);
    const bool flows = placed && ornaments.nearestFountain(hero->crown, water);
    hearFrom(heard_.fire, burns ? fire : nullptr, kFireFull, kFireReach);
    hearFrom(heard_.fountain, flows ? water : nullptr, kFountainFull, kFountainReach);
}

void Play::hearFrom(int event, const float* at, float full, float reach) {
    if (event < 0) return;
    float d = reach;
    if (at != nullptr) {
        const float* ear = drawnOf(realm_.hero().id)->crown;
        const float dx = at[0] - ear[0], dz = at[2] - ear[2];
        d = std::sqrt(dx * dx + dz * dz);
    }
    sound_.loop(event, d < reach);
    if (d >= reach) return;
    sound_.loopAt(event, at, std::clamp((reach - d) / (reach - full), 0.0f, 1.0f));
}

}  // namespace mu::game
