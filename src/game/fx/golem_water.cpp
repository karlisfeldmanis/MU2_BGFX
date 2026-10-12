#include "game/fx/golem_water.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
constexpr float kDripLife = 50.0f, kSmokeLife = 25.0f, kWaveLife = 7.0f, kTrailLife = 15.0f;
constexpr float kJointLife = 120.0f;
constexpr float kDripLight[3] = {0.0f, 0.4f, 0.4f};
constexpr float kTrailLight[3] = {0.4f, 0.8f, 1.0f};
constexpr float kWaveLight = 5.0f;
constexpr float kSixtyFour = 0.64f;     // waterFall2 and smoke01 are 64 texels
constexpr float kWaveMetres = 1.28f;    // ShockWave is 128
constexpr float kJointHalf = 10.0f * kUnit;   // Scale 20
constexpr float kTargetLift = 100.0f * kUnit, kHomeLift = 120.0f * kUnit;
constexpr float kHandAhead[3] = {30.0f * kUnit, 0.0f, 0.0f};
constexpr size_t kMostMotes = 900;
constexpr size_t kMostJoints = 48;
constexpr const char* kBones[4] = {"Bip01 L Hand", "Bip01 R Hand", "Bip01 Head", "Bip01 Footsteps"};

}  // namespace

float GolemWater::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool GolemWater::open(const std::string& assetDir, content::Textures& textures,
                      const content::Showing& table) {
    shutdown();
    const auto load = [&](const char* name) {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    drip_ = load("waterfall_mist");   // Effect/WATERFALL2, BITMAP_WATERFALL_2
    smoke_ = load("smoke01");         // BITMAP_SMOKE
    wave_ = load("shockwave");        // BITMAP_SHOCK_WAVE
    // BITMAP_JOINT_ENERGY is Effect/JointLaser01 (ZzzOpenData.cpp:5288), which the comet's
    // folder already carries.
    laser_ = textures.load(assetDir + "/effects/comet/joint_laser01.png", content::TextureRole::Albedo);
    core::logf("golem water: drips %s, smoke %s, wave %s, drain %s", bgfx::isValid(drip_) ? "yes" : "NO",
               bgfx::isValid(smoke_) ? "yes" : "NO", bgfx::isValid(wave_) ? "yes" : "NO",
               bgfx::isValid(laser_) ? "yes" : "NO");
    return bgfx::isValid(drip_);
}

void GolemWater::shutdown() {
    motes_.clear();
    joints_.clear();
    golems_.clear();
    bursts_.clear();
    drip_ = smoke_ = wave_ = laser_ = BGFX_INVALID_HANDLE;
}

void GolemWater::mote(Mote::Kind kind, const float at[3], float fade) {
    if (motes_.size() >= kMostMotes) return;
    Mote one;
    one.kind = kind;
    for (int i = 0; i < 3; ++i) one.at[i] = at[i];
    one.spin = unit() * 6.2831853f;
    one.light = fade;
    switch (kind) {
    case Mote::Kind::Drip:
        // Up to 10 units aside and 20 up or down; falling 3-5 units a frame.
        one.at[0] += (std::floor(unit() * 20.0f) - 10.0f) * kUnit;
        one.at[2] += (std::floor(unit() * 20.0f) - 10.0f) * kUnit;
        one.at[1] += (std::floor(unit() * 40.0f) - 20.0f) * kUnit;
        one.velocity[1] = -(std::floor(unit() * 3.0f) + 3.0f) * kUnit;
        one.scale = (std::floor(unit() * 6.0f) + 6.0f) * 0.1f;
        one.life = kDripLife;
        break;
    case Mote::Kind::Smoke:
        one.scale = (std::floor(unit() * 64.0f) + 64.0f) * 0.01f;
        one.life = kSmokeLife;
        break;
    case Mote::Kind::Wave:
        one.scale = 0.5f;
        one.life = kWaveLife;
        break;
    case Mote::Kind::Trail:
        one.scale = 0.8f;
        one.life = kTrailLife;
        break;
    }
    motes_.push_back(one);
}

void GolemWater::watch(uint32_t id, const Figure& figure, bool attack1, float key, const float* target,
                       float fade, float seconds) {
    Golem& golem = golems_[id];
    golem.seen = true;
    if (!figure.body() || !figure.body()->skeletonMesh) return;
    if (!golem.found) {
        golem.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (int k = 0; k < 4; ++k)
            for (size_t b = 0; b < bones.size(); ++b)
                if (bones[b].name == kBones[k]) golem.bones[k] = int(b);
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float feet[3];
    if (golem.bones[3] < 0 || !figure.pointOn(golem.bones[3], origin, feet)) return;
    for (int i = 0; i < 3; ++i) golem.home[i] = feet[i];
    golem.home[1] += kHomeLift;
    if (fade <= 0.0f) return;

    // The drips, one off each of the three bones a reference frame (CreateParticleFpsChecked).
    golem.dripOwed += seconds / kFrame;
    while (golem.dripOwed >= 1.0f) {
        golem.dripOwed -= 1.0f;
        for (int k = 0; k < 3; ++k) {
            float at[3];
            if (golem.bones[k] >= 0 && figure.pointOn(golem.bones[k], origin, at)) mote(Mote::Kind::Drip, at, fade);
        }
    }

    // The first blow, from its fourth key: smoke at the left hand, a wave from 5 to 5.5, and from
    // 6.9 a joint off the target, one a reference frame each.
    if (!attack1 || key < 4.0f) {
        golem.blowOwed = 0.0f;
        golem.burst = false;
        return;
    }
    golem.blowOwed += seconds / kFrame;
    while (golem.blowOwed >= 1.0f) {
        golem.blowOwed -= 1.0f;
        float hand[3];
        if (golem.bones[0] >= 0 && figure.pointOn(golem.bones[0], kHandAhead, hand)) {
            mote(Mote::Kind::Smoke, hand, fade);
            if (key >= 5.0f && key <= 5.5f) {
                mote(Mote::Kind::Wave, hand, fade);
                if (!golem.burst) bursts_.push_back({hand[0], hand[1], hand[2]});
                golem.burst = true;
            }
        }
        if (target && key >= 6.9f && joints_.size() < kMostJoints) {
            Joint joint;
            joint.golem = id;
            joint.fade = fade;
            for (int i = 0; i < 3; ++i) {
                joint.at[i] = target[i];
                joint.home[i] = golem.home[i];
            }
            joint.at[1] += kTargetLift;
            // MultiUse: one way or the other, on both axes alike.
            const float way = unit() < 0.5f ? 1.0f : -1.0f;
            joint.side[0] = joint.side[1] = way;
            joints_.push_back(joint);
        }
    }
}

bool GolemWater::takeBurst(float at[3]) {
    if (bursts_.empty()) return false;
    for (int i = 0; i < 3; ++i) at[i] = bursts_.back()[size_t(i)];
    bursts_.pop_back();
    return true;
}

void GolemWater::update(float seconds) {
    const float frames = std::min(seconds, 0.25f) / kFrame;
    // A golem not watched last frame is gone; its joints keep the last home they had.
    for (auto it = golems_.begin(); it != golems_.end();) {
        if (!it->second.seen) {
            it = golems_.erase(it);
        } else {
            it->second.seen = false;
            ++it;
        }
    }
    for (Mote& one : motes_) {
        one.age += frames;
        switch (one.kind) {
        case Mote::Kind::Drip: {
            // Moved by its velocity first (MovePosition), then the velocity eased and rolled.
            for (int i = 0; i < 3; ++i) one.at[i] += one.velocity[i] * frames;
            one.velocity[0] = (std::floor(unit() * 20.0f) - 10.0f) * 0.1f * kUnit;
            one.velocity[2] = (std::floor(unit() * 20.0f) - 10.0f) * 0.1f * kUnit;
            one.velocity[1] += 0.1f * kUnit * frames;
            one.scale += 0.03f * frames;
            one.light *= std::pow(1.0f / 1.05f, frames);
            if (one.life - one.age < 10.0f) one.light *= std::pow(1.0f / 1.1f, frames);
            break;
        }
        case Mote::Kind::Smoke:
            one.scale += 0.09f * frames;
            break;
        case Mote::Kind::Wave:
            one.light *= std::pow(1.0f / 1.8f, frames);
            one.scale += 0.3f * frames;
            break;
        case Mote::Kind::Trail:
            one.light *= std::pow(1.0f / 1.1f, frames);
            one.scale -= 0.05f * frames;
            break;
        }
    }
    motes_.erase(std::remove_if(motes_.begin(), motes_.end(),
                                [](const Mote& m) { return m.age >= m.life || m.scale <= 0.0f; }),
                 motes_.end());

    // The joints, and their tails and trail laid a point a reference frame.
    tailStep_ += frames;
    const bool lay = tailStep_ >= 1.0f;
    if (lay) tailStep_ = std::fmod(tailStep_, 1.0f);
    for (Joint& joint : joints_) {
        joint.age += frames;
        auto golem = golems_.find(joint.golem);
        if (golem != golems_.end())
            for (int i = 0; i < 3; ++i) joint.home[i] = golem->second.home[i];
        if (joint.age <= 20.0f) {
            joint.at[0] += joint.side[0] * (std::floor(unit() * 5.0f) + 3.0f) * kUnit * frames;
            joint.at[2] += joint.side[1] * (std::floor(unit() * 5.0f) + 3.0f) * kUnit * frames;
            joint.at[1] += 8.0f * kUnit * frames;
        } else {
            joint.speed = std::min(40.0f, joint.speed + 7.0f * frames);
            float away[3], distance = 0.0f;
            for (int i = 0; i < 3; ++i) {
                away[i] = joint.home[i] - joint.at[i];
                distance += away[i] * away[i];
            }
            distance = std::sqrt(distance);
            const float step = std::min(distance, joint.speed * kUnit * frames);
            if (distance > 0.0f)
                for (int i = 0; i < 3; ++i) joint.at[i] += away[i] / distance * step;
            if (distance - step <= 35.0f * kUnit) joint.landed = true;
        }
        if (lay) {
            for (int t = std::min(joint.tails, 7); t > 0; --t)
                for (int i = 0; i < 3; ++i) joint.tail[t][i] = joint.tail[t - 1][i];
            for (int i = 0; i < 3; ++i) joint.tail[0][i] = joint.at[i];
            joint.tails = std::min(8, joint.tails + 1);
            mote(Mote::Kind::Trail, joint.at, joint.fade);
        }
    }
    joints_.erase(std::remove_if(joints_.begin(), joints_.end(),
                                 [](const Joint& j) { return j.landed || j.age >= kJointLife; }),
                  joints_.end());
}

void GolemWater::gather(gfx::Effects& effects) const {
    for (const Mote& one : motes_) {
        gfx::Sprite sprite;
        float base[3] = {1.0f, 1.0f, 1.0f};
        float light = one.light, metres = kSixtyFour;
        switch (one.kind) {
        case Mote::Kind::Drip:
            sprite.sheet = drip_;
            for (int i = 0; i < 3; ++i) base[i] = kDripLight[i];
            break;
        case Mote::Kind::Smoke:
            // Light LifeTime / 20: 1.25 at its first frame, out at its last.
            sprite.sheet = smoke_;
            light *= std::max(0.0f, one.life - one.age) / 20.0f;
            break;
        case Mote::Kind::Wave:
            sprite.sheet = wave_;
            base[0] = base[1] = base[2] = kWaveLight;
            metres = kWaveMetres;
            break;
        case Mote::Kind::Trail:
            sprite.sheet = smoke_;
            for (int i = 0; i < 3; ++i) base[i] = kTrailLight[i];
            break;
        }
        if (!bgfx::isValid(sprite.sheet)) continue;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = base[i] * light;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * metres * one.scale;
        sprite.spin = one.spin;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    if (!bgfx::isValid(laser_)) return;
    // A joint's tail as MU draws one: two strips crossed along it, one standing and one lying,
    // JointLaser01 stretched from the head down the tail.
    for (const Joint& joint : joints_) {
        for (int t = 0; t + 1 < joint.tails; ++t) {
            const float* a = joint.tail[t];
            const float* b = joint.tail[t + 1];
            float along[3], length = 0.0f;
            for (int i = 0; i < 3; ++i) {
                along[i] = b[i] - a[i];
                length += along[i] * along[i];
            }
            length = std::sqrt(length);
            if (length < 1e-4f) continue;
            for (int i = 0; i < 3; ++i) along[i] /= length;
            float lying[3] = {-along[2], 0.0f, along[0]};
            float flat = std::sqrt(lying[0] * lying[0] + lying[2] * lying[2]);
            if (flat < 1e-4f) {
                lying[0] = 1.0f;
                flat = 1.0f;
            }
            const float standing[3] = {0.0f, 1.0f, 0.0f};
            const float u0 = float(t) / float(joint.tails - 1), u1 = float(t + 1) / float(joint.tails - 1);
            for (int plane = 0; plane < 2; ++plane) {
                float across[3];
                for (int i = 0; i < 3; ++i)
                    across[i] = (plane == 0 ? standing[i] : lying[i] / flat) * kJointHalf;
                gfx::Sprite quad;
                quad.placed = true;
                quad.blend = gfx::Blend::Additive;
                quad.sheet = laser_;
                for (int i = 0; i < 3; ++i) {
                    quad.colour[i] = joint.fade;
                    quad.corner[0][i] = a[i] - across[i];
                    quad.corner[1][i] = b[i] - across[i];
                    quad.corner[2][i] = b[i] + across[i];
                    quad.corner[3][i] = a[i] + across[i];
                    quad.position[i] = (a[i] + b[i]) * 0.5f;
                }
                quad.colour[3] = 1.0f;
                quad.cornerUv[0][0] = u0; quad.cornerUv[0][1] = 1.0f;
                quad.cornerUv[1][0] = u1; quad.cornerUv[1][1] = 1.0f;
                quad.cornerUv[2][0] = u1; quad.cornerUv[2][1] = 0.0f;
                quad.cornerUv[3][0] = u0; quad.cornerUv[3][1] = 0.0f;
                effects.add(quad);
            }
        }
    }
}

}  // namespace mu::game
