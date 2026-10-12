#include "game/fx/hell_maine.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
constexpr float kSixtyFour = 0.64f;  // Impack03, fantaF, PoundingBall, smoke01, Fire02: 64 texels
constexpr const char* kBones[7] = {"Bone02", "Bone03", "zx04", "zx03", "zx08", "Bip01 L Hand",
                                   "Bip01 R Hand"};
constexpr float kBoneScale[5] = {0.3f, 0.3f, 1.0f, 1.0f, 1.0f};
// Ours (see the header): MU's sizes and lights brought down where they piled up.
constexpr float kLightDim = 0.5f;
constexpr float kFireSize = 0.33f;   // a third: at a half its cores still read as bubbles
constexpr float kFireLight = 0.4f;    // every frame as MU, at 0.4: one tongue, not single balls
// The fire lay broken up (the user, 2026-10-12, of MU's even row of twenty white Fire02: 'i dont
// like this beam', then 'Broken-up air beam'): each flame its own size, spin, turn and drift off
// the line, flickering on its own, in an orange, fading toward the far end, thrown out from the
// hand over its first frames.
constexpr float kLaserLight = 0.5f;
constexpr float kLaserSize = 0.35f;
constexpr float kLaserTint[3] = {1.0f, 0.55f, 0.25f};
constexpr float kLaserEnd = 0.25f;      // the far flame's light, of the first's
constexpr float kLaserReachOut = 5.0f;  // flames a frame as it is thrown out
constexpr float kLaserDrift = 0.15f;    // metres off the line, at most
constexpr float kLaserFade = 5.0f;    // its last frames, faded
constexpr int kDecalEvery = 4;        // a laser decal under every fourth flame
constexpr float kLaserDecalHalf = 1.0f;
// MU's.
constexpr float kFireLife = 32.0f;
constexpr float kStormLife = 60.0f, kStormStep = 12.0f * kUnit;
constexpr float kStormLift = 15.0f * kUnit;
constexpr float kStormDiscHalf = 1.0f, kStormDisc = 0.3f;
constexpr float kLaserLife = 20.0f, kLaserGap = 50.0f * kUnit;
constexpr int kLaserFlames = 20;
constexpr float kLaserDecal = 0.2f;
// Twenty (0.2, 0, 0) lights 0.5 m apart over 2 m each: about four reach any point of the line.
constexpr float kLaserGlow = 0.8f, kLaserGlowReach = 2.0f;
constexpr size_t kMostMotes = 1200, kMostStorms = 8, kMostLasers = 8, kMostStones = 96;

}  // namespace

float HellMaine::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool HellMaine::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    shutdown();
    const auto load = [&](const char* name) {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    impact_ = load("impact");          // BITMAP_LIGHT + 1, Object9/Impack03
    fire_ = load("true_fire");         // BITMAP_TRUE_FIRE, Effect/fantaF
    ball_ = load("pounding_ball");     // BITMAP_POUNDING_BALL, Effect/PoundingBall
    smoke_ = load("smoke01");          // BITMAP_SMOKE, Effect/smoke01
    flame_ = load("fire2");            // BITMAP_FIRE + 1, Effect/Fire02
    core::logf("hell maine: lights %s, fire %s, ball %s, smoke %s, flame %s",
               bgfx::isValid(impact_) ? "yes" : "NO", bgfx::isValid(fire_) ? "yes" : "NO",
               bgfx::isValid(ball_) ? "yes" : "NO", bgfx::isValid(smoke_) ? "yes" : "NO",
               bgfx::isValid(flame_) ? "yes" : "NO");
    return bgfx::isValid(impact_);
}

void HellMaine::shutdown() {
    motes_.clear();
    storms_.clear();
    lasers_.clear();
    lights_.clear();
    stones_.clear();
    ones_.clear();
    quake_ = 0.0f;
    impact_ = fire_ = ball_ = smoke_ = flame_ = BGFX_INVALID_HANDLE;
}

void HellMaine::mote(Mote::Kind kind, const float at[3], float scale, float light) {
    if (motes_.size() >= kMostMotes) return;
    Mote one;
    one.kind = kind;
    for (int i = 0; i < 3; ++i) one.at[i] = at[i];
    one.scale = scale;
    one.light = light;
    switch (kind) {
    case Mote::Kind::Fire:
        // Thrown aside along MU's x (its angle is 0: the world's) and up; upright.
        one.velocity[0] = (std::floor(unit() * 10.0f) - 5.0f) * 0.4f * kUnit;
        one.velocity[1] = (std::floor(unit() * 10.0f) + 5.0f) * 0.2f * kUnit;
        one.life = kFireLife;
        break;
    case Mote::Kind::Ball:
    case Mote::Kind::Dark:
        one.velocity[1] = 33.0f * kUnit;
        one.spin = unit() * 6.2831853f;
        one.life = 12.0f;
        break;
    case Mote::Kind::Puff:
        one.velocity[1] = 15.0f * kUnit;
        one.spin = unit() * 6.2831853f;
        one.life = 4.0f;
        break;
    }
    motes_.push_back(one);
}

void HellMaine::watch(uint32_t id, const Figure& figure, int attack, float key, const float feet[3],
                      float yaw, const float* target, float fade, float seconds) {
    One& one = ones_[id];
    one.seen = true;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return;
    if (!one.found) {
        one.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (int k = 0; k < 7; ++k)
            for (size_t b = 0; b < bones.size(); ++b)
                if (bones[b].name == kBones[k]) one.bones[k] = int(b);
    }
    // A new blow: the clip changed, or the same one began again.
    if (attack != one.lastAttack || key + 0.5f < one.lastKey) {
        one.frames = 0.0f;
        one.stormed = false;
        one.lasered = false;
    }
    one.lastAttack = attack;
    one.lastKey = key;

    const float origin[3] = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < 5; ++k) {
        Light light;
        if (one.bones[k] < 0 || !figure.pointOn(one.bones[k], origin, light.at)) continue;
        light.scale = kBoneScale[k];
        light.roll = one.roll;
        light.fade = fade;
        lights_.push_back(light);
    }
    float hands[2][3];
    const bool left = one.bones[5] >= 0 && figure.pointOn(one.bones[5], origin, hands[0]);
    const bool right = one.bones[6] >= 0 && figure.pointOn(one.bones[6], origin, hands[1]);
    float way[2] = {std::sin(yaw), std::cos(yaw)};
    if (target) {
        const float dx = target[0] - feet[0], dz = target[2] - feet[2];
        if (const float far = std::sqrt(dx * dx + dz * dz); far > 0.1f) {
            way[0] = dx / far;
            way[1] = dz / far;
        }
    }

    one.owed += seconds / kFrame;
    while (one.owed >= 1.0f) {
        one.owed -= 1.0f;
        one.roll = std::floor(unit() * 8.0f) / 10.0f + 0.6f;
        const float big = attack == 2 ? 5.0f : 2.5f;
        if (left) mote(Mote::Kind::Fire, hands[0], big * kFireSize, fade * kFireLight);
        if (right) mote(Mote::Kind::Fire, hands[1], (attack == 2 ? 5.0f : 2.1f) * kFireSize, fade * kFireLight);
        if (attack == 0) continue;
        one.frames += 1.0f;
        // AttackTime 10: the storm nine frames into Attack 1, walking out along its facing.
        if (attack == 1 && !one.stormed && one.frames >= 9.0f && storms_.size() < kMostStorms) {
            one.stormed = true;
            Storm storm;
            for (int i = 0; i < 3; ++i) storm.at[i] = feet[i];
            storm.way[0] = way[0];
            storm.way[1] = way[1];
            storm.light = fade;
            storm.owner = id;
            storms_.push_back(storm);
            core::logf("hell maine: Attack 1's storm at %.1f, %.1f toward %.2f, %.2f (%s, yaw %.2f)", feet[0], feet[2],
                       way[0], way[1], target ? "its target" : "its facing", yaw);
        }
        // Attack 2's fire lay at its right hand from key 9, once a blow.
        if (attack == 2 && !one.lasered && key >= 9.0f && right && lasers_.size() < kMostLasers) {
            one.lasered = true;
            Laser laser;
            for (int i = 0; i < 3; ++i) laser.at[i] = hands[1][i];
            laser.way[0] = way[0];
            laser.way[1] = way[1];
            // From its hand along its facing, as MU's: aimed from the hand at its target it went
            // backwards, the hand of a 1.8 Hell Maine reaching past the knight at its feet.
            laser.floor = feet[1];
            laser.seed = uint32_t(unit() * 16777216.0f) | 1u;
            laser.light = fade;
            lasers_.push_back(laser);
            core::logf("hell maine: Attack 2's fire lay from %.1f, %.1f toward %.2f, %.2f (%s)", hands[1][0], hands[1][2],
                       laser.way[0], laser.way[1], target ? "its target" : "its facing");
        }
    }
}

uint32_t HellMaine::lights(gfx::PointLight* out, uint32_t max) const {
    uint32_t count = 0;
    for (const Laser& one : lasers_) {
        if (count >= max) break;
        const float lit = one.light * std::clamp((kLaserLife - one.age) / kLaserFade, 0.0f, 1.0f);
        const float reach = kLaserGap * float(kLaserFlames - 1);
        gfx::PointLight& light = out[count++];
        light = gfx::PointLight{};
        light.line = true;
        light.position[0] = one.at[0];
        light.position[1] = one.floor;
        light.position[2] = one.at[2];
        light.to[0] = one.at[0] + one.way[0] * reach;
        light.to[1] = one.floor;
        light.to[2] = one.at[2] + one.way[1] * reach;
        light.reach = kLaserGlowReach;
        light.height = 1.0f;
        light.colour[0] = kLaserGlow * lit;
        light.colour[1] = light.colour[2] = 0.0f;
    }
    return count;
}

bool HellMaine::takeStone(float out[3]) {
    if (stones_.size() < 3) return false;
    for (int i = 2; i >= 0; --i) {
        out[i] = stones_.back();
        stones_.pop_back();
    }
    return true;
}

void HellMaine::update(float seconds) {
    const float frames = std::min(seconds, 0.25f) / kFrame;
    clock_ += seconds;
    lights_.clear();
    for (auto it = ones_.begin(); it != ones_.end();) {
        if (!it->second.seen) {
            it = ones_.erase(it);
        } else {
            it->second.seen = false;
            ++it;
        }
    }
    for (Mote& one : motes_) {
        one.age += frames;
        for (int i = 0; i < 3; ++i) one.at[i] += one.velocity[i] * frames;
        switch (one.kind) {
        case Mote::Kind::Fire:
            one.at[1] += kUnit * frames;
            one.velocity[0] *= std::pow(0.95f, frames);
            one.scale -= 0.02f * frames;
            break;
        case Mote::Kind::Ball:
        case Mote::Kind::Dark:
            // Shrinking for its first three frames, then swelling.
            if (one.age < 3.0f) one.scale -= 0.4f * frames;
            else one.scale *= std::pow(1.2f, frames);
            if (one.kind == Mote::Kind::Ball) one.light *= std::pow(0.92f, frames);
            break;
        case Mote::Kind::Puff:
            one.scale += 0.15f * frames;
            one.at[0] += std::sin(clock_ * 1000.0f) * 5.0f * kUnit * frames;
            break;
        }
    }
    motes_.erase(std::remove_if(motes_.begin(), motes_.end(),
                                [](const Mote& m) { return m.age >= m.life || m.scale <= 0.0f; }),
                 motes_.end());

    quake_ *= std::pow(0.2f, frames);
    if (std::fabs(quake_) < 0.001f) quake_ = 0.0f;
    for (Storm& one : storms_) {
        one.age += frames;
        one.at[0] += one.way[0] * kStormStep * frames;
        one.at[2] += one.way[1] * kStormStep * frames;
        for (one.owed += frames; one.owed >= 1.0f; one.owed -= 1.0f) {
            one.odd = !one.odd;
            const float low[3] = {one.at[0], one.at[1] + kStormLift, one.at[2]};
            if (quakeFor.shakes(one.owner, one.at[0], one.at[2])) quake_ = (std::floor(unit() * 8.0f) - 4.0f) * 0.1f;
            if (one.odd) {
                mote(Mote::Kind::Ball, low, 2.3f, one.light);
                mote(Mote::Kind::Dark, low, 2.3f, one.light);
                mote(Mote::Kind::Puff, low, 1.8f, one.light);
            }
            if (unit() < 0.5f && stones_.size() + 3 <= kMostStones * 3) {
                stones_.push_back(one.at[0]);
                stones_.push_back(one.at[1]);
                stones_.push_back(one.at[2]);
            }
        }
    }
    storms_.erase(std::remove_if(storms_.begin(), storms_.end(),
                                 [](const Storm& s) { return s.age >= kStormLife; }),
                  storms_.end());
    for (Laser& one : lasers_) one.age += frames;
    lasers_.erase(std::remove_if(lasers_.begin(), lasers_.end(),
                                 [](const Laser& l) { return l.age >= kLaserLife; }),
                  lasers_.end());
}

void HellMaine::gather(gfx::Effects& effects) const {
    const auto sprite = [&](bgfx::TextureHandle sheet, const float at[3], float half, const float colour[3],
                            float spin, gfx::Blend blend) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite one;
        one.sheet = sheet;
        for (int i = 0; i < 3; ++i) {
            one.position[i] = at[i];
            one.colour[i] = colour[i];
        }
        one.colour[3] = 1.0f;
        one.halfWidth = half;
        one.halfHeight = half;
        one.spin = spin;
        one.blend = blend;
        effects.add(one);
    };
    const auto decal = [&](bgfx::TextureHandle sheet, const float at[3], float half, const float colour[3],
                           float spin) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite one;
        one.placed = true;
        one.blend = gfx::Blend::Additive;
        one.sheet = sheet;
        const float c = std::cos(spin) * half, s = std::sin(spin) * half;
        const float x[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        const float z[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        for (int k = 0; k < 4; ++k) {
            one.corner[k][0] = at[0] + x[k] * c - z[k] * s;
            one.corner[k][1] = at[1] + 0.02f;
            one.corner[k][2] = at[2] + x[k] * s + z[k] * c;
            one.cornerUv[k][0] = x[k] > 0.0f ? 1.0f : 0.0f;
            one.cornerUv[k][1] = z[k] > 0.0f ? 1.0f : 0.0f;
        }
        for (int i = 0; i < 3; ++i) {
            one.colour[i] = colour[i];
            one.position[i] = at[i];
        }
        one.position[1] = at[1] + 0.02f;
        one.colour[3] = 1.0f;
        effects.add(one);
    };

    for (const Light& one : lights_) {
        const float colour[3] = {one.roll * kLightDim * one.fade, 0.0f, 0.0f};
        sprite(impact_, one.at, 0.5f * kSixtyFour * one.scale, colour, 0.0f, gfx::Blend::Additive);
    }
    for (const Mote& one : motes_) {
        const float half = 0.5f * kSixtyFour * one.scale;
        switch (one.kind) {
        case Mote::Kind::Fire: {
            // MU overwrites its light every frame as LifeTime / 25, in grey.
            const float grey = one.light * std::max(0.0f, one.life - one.age) / 25.0f;
            const float colour[3] = {grey, grey, grey};
            sprite(fire_, one.at, half, colour, 0.0f, gfx::Blend::Additive);
            break;
        }
        case Mote::Kind::Ball: {
            const float colour[3] = {one.light, one.light, one.light};
            sprite(ball_, one.at, half, colour, one.spin, gfx::Blend::Additive);
            break;
        }
        case Mote::Kind::Dark: {
            const float colour[3] = {one.light, one.light, one.light};
            sprite(smoke_, one.at, half, colour, one.spin, gfx::Blend::Minus);
            break;
        }
        case Mote::Kind::Puff: {
            // MU's (1, 3, 1), which its glColor clamps to white.
            const float colour[3] = {one.light, one.light, one.light};
            sprite(smoke_, one.at, half, colour, one.spin, gfx::Blend::Additive);
            break;
        }
        }
    }
    // Under each storm, PoundingBall spun by WorldTime (milliseconds read as degrees).
    for (const Storm& one : storms_) {
        const float colour[3] = {kStormDisc * one.light, kStormDisc * one.light, kStormDisc * one.light};
        decal(ball_, one.at, kStormDiscHalf, colour, std::fmod(clock_ * 1000.0f, 360.0f) * 0.0174533f);
    }
    // The fire lay: twenty flames out along its facing at its hand's height, a decal under some.
    for (const Laser& one : lasers_) {
        const float left = kLaserLife - one.age;
        const float light = one.light * kLaserLight * std::clamp(left / kLaserFade, 0.0f, 1.0f);
        const float red[3] = {kLaserDecal * light, 0.0f, 0.0f};
        const int out = std::min(kLaserFlames, int(one.age * kLaserReachOut) + 1);
        // The first at its hand, as MU's j = 0 (ZzzEffect.cpp:8973-8996).
        for (int k = 0; k < out; ++k) {
            // This flame's own dice, the same every frame of its beam.
            uint32_t dice = one.seed ^ (uint32_t(k) * 0x9e3779b9u);
            const auto roll = [&dice] {
                dice ^= dice << 13;
                dice ^= dice >> 17;
                dice ^= dice << 5;
                return float(dice >> 8) / 16777216.0f;
            };
            const float size = 0.6f + 0.6f * roll(), turn = (roll() - 0.5f) * 8.0f, phase = roll() * 6.2831853f;
            const float across = (roll() * 2.0f - 1.0f) * kLaserDrift, up = (roll() * 2.0f - 1.0f) * kLaserDrift;
            const float along = kLaserGap * float(k);
            const float at[3] = {one.at[0] + one.way[0] * along + one.way[1] * across, one.at[1] + up,
                                 one.at[2] + one.way[1] * along - one.way[0] * across};
            const float flicker = 0.7f + 0.3f * std::sin(one.age * 1.7f + phase);
            const float end = 1.0f - (1.0f - kLaserEnd) * float(k) / float(kLaserFlames - 1);
            const float lit = light * flicker * end;
            const float colour[3] = {kLaserTint[0] * lit, kLaserTint[1] * lit, kLaserTint[2] * lit};
            sprite(flame_, at, 0.5f * kSixtyFour * 3.0f * kLaserSize * size, colour, phase + one.age * turn * 0.1f,
                   gfx::Blend::Additive);
            if (k % kDecalEvery == kDecalEvery - 1) {
                const float ground[3] = {at[0], one.floor, at[2]};
                decal(smoke_, ground, kLaserDecalHalf, red, float(k) * 0.3f);
            }
        }
    }
}

}  // namespace mu::game
