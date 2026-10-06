#include "game/fx/poison.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {
constexpr float kTwoPi = 6.28318531f;
}  // namespace

float Poison::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Poison::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table) {
    const std::string dir = assetDir + "/effects/poison/";
    keyCount_ = 0;
    for (int k = 0; k < 11; ++k) {
        char name[32];
        std::snprintf(name, sizeof(name), "Poison01_%02d.obj", k);
        flat_[k].clear();
        lit_[k].clear();
        const bool one = loadEffectObj(dir + name, kUnit, "wall01", flat_[k]);
        const bool two = loadEffectObj(dir + name, kUnit, "wall02", lit_[k]);
        if (!one && !two) break;
        keyCount_ = k + 1;
    }
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (!core::fileExists(dir + name)) return bgfx::TextureHandle{bgfx::kInvalidHandle};
        return textures.load(dir + name, content::TextureRole::Albedo);
    };
    wall1_ = sheet("wall01.png");
    wall2_ = sheet("wall02.png");
    if (const content::EffectSheet* smoke = table.effect("smoke01")) {
        smoke_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    }
    core::logf("poison: %d keys, walls %s/%s", keyCount_, bgfx::isValid(wall1_) ? "yes" : "NO",
               bgfx::isValid(wall2_) ? "yes" : "NO");
    return keyCount_ > 0;
}

Poison::Puff* Poison::freePuff() {
    if (!bgfx::isValid(smoke_)) return nullptr;
    for (Puff& one : puffs_) {
        if (!one.alive) return &one;
    }
    return nullptr;
}

void Poison::cast(const float feet[3], float yaw) {
    for (Cloud& one : clouds_) {
        if (one.alive) continue;
        one = Cloud{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = feet[k];
        one.yaw = yaw;
        one.left = kCloudFrames;
        break;
    }
    for (int n = 0; n < kPuffs; ++n) {
        Puff* puff = freePuff();
        if (puff == nullptr) break;
        *puff = Puff{};
        puff->alive = true;
        puff->at[0] = feet[0] + between(-32.0f, 32.0f) * kUnit;
        puff->at[1] = feet[1] + between(32.0f, 96.0f) * kUnit;
        puff->at[2] = feet[2] + between(-32.0f, 32.0f) * kUnit;
        // Out at a random yaw and up at forty-five degrees.
        const float way = unit() * kTwoPi;
        const float speed = between(40.0f, 47.0f) * kUnit * kFps;
        const float flat = speed * 0.70710678f;
        puff->velocity[0] = std::cos(way) * flat;
        puff->velocity[1] = flat;
        puff->velocity[2] = std::sin(way) * flat;
        puff->size = between(0.80f, 1.11f);
        puff->full = 50.0f;
        puff->spin = unit() * kTwoPi;
    }
}

void Poison::fume(const float feet[3], float tall, float seconds) {
    fumeDue_ -= seconds * kFps;
    while (fumeDue_ <= 0.0f) {
        fumeDue_ += kFumeEvery;
        Puff* puff = freePuff();
        if (puff == nullptr) return;
        *puff = Puff{};
        puff->alive = true;
        puff->fume = true;
        const float turn = unit() * kTwoPi;
        const float reach = kFumeRadius * (0.3f + unit() * 0.7f);
        puff->at[0] = feet[0] + std::cos(turn) * reach;
        puff->at[1] = feet[1] + tall * (0.25f + unit() * 0.55f);
        puff->at[2] = feet[2] + std::sin(turn) * reach;
        puff->velocity[1] = 0.5f;
        puff->size = between(0.25f, 0.42f);
        puff->full = 28.0f;
        puff->spin = unit() * kTwoPi;
    }
}

void Poison::sicken(const float at[3], float scale) {
    Puff* puff = freePuff();
    if (puff == nullptr) return;
    *puff = Puff{};
    puff->alive = true;
    puff->sick = true;
    for (int k = 0; k < 3; ++k) puff->at[k] = at[k];
    const float turn = unit() * kTwoPi;
    puff->velocity[0] = std::cos(turn) * 0.08f;
    puff->velocity[1] = kSickRise * (0.7f + unit() * 0.6f);
    puff->velocity[2] = std::sin(turn) * 0.08f;
    puff->size = between(0.4f, 0.65f) * scale;
    puff->full = kSickFrames;
    puff->spin = unit() * kTwoPi;
}

void Poison::update(float seconds) {
    const float frames = seconds * kFps;
    for (Cloud& one : clouds_) {
        if (!one.alive) continue;
        one.left -= frames;
        one.frame = std::min(float(keyCount_ - 1), one.frame + kKeysPerFrame * frames);
        one.glow = between(0.7f, 1.0f);
        if (one.left <= 0.0f) one.alive = false;
    }
    const float drag = std::pow(0.4f, frames);
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        for (int k = 0; k < 3; ++k) one.at[k] += one.velocity[k] * seconds;
        if (one.sick) {
            one.size *= std::pow(1.02f, frames);
        } else if (!one.fume) {
            for (float& v : one.velocity) v *= drag;
            one.size += 0.05f * frames;
        } else {
            one.size *= std::pow(1.025f, frames);
        }
        one.age += frames;
        if (one.age >= one.full) one.alive = false;
    }
}

void Poison::gather(gfx::Effects& effects) const {
    for (const Cloud& one : clouds_) {
        if (!one.alive || keyCount_ == 0) continue;
        const int key = std::clamp(int(one.frame), 0, keyCount_ - 1);
        const float c = std::cos(one.yaw), s = std::sin(one.yaw);
        const float x[3] = {c, 0.0f, -s}, y[3] = {0.0f, 1.0f, 0.0f}, z[3] = {s, 0.0f, c};
        // Dimming over its last five frames.
        const float lit = std::clamp(one.left / kDarkensUnder, 0.0f, 1.0f);
        const float white[3] = {lit, lit, lit};
        if (bgfx::isValid(wall1_) && !flat_[key].empty()) {
            submitEffectAlong(effects, flat_[key], wall1_, gfx::Blend::Alpha, one.at, x, y, z,
                              kCloudScale, white, lit);
        }
        if (bgfx::isValid(wall2_) && !lit_[key].empty()) {
            submitEffectAlong(effects, lit_[key], wall2_, gfx::Blend::Additive, one.at, x, y, z,
                              kCloudScale, white, 1.0f);
        }
    }
    for (const Puff& one : puffs_) {
        if (!one.alive) continue;
        const float t = one.age / one.full;
        const float open = std::min(1.0f, t / 0.15f);
        const float out = 1.0f - t;
        const float* tint = one.sick ? kSick : one.fume ? kCasterFume : kFume;
        const float bright = one.sick ? kSickBright : one.fume ? 1.0f : 0.45f;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = tint[k] * bright * open * out;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

uint32_t Poison::lights(gfx::PointLight* out, uint32_t max) const {
    uint32_t count = 0;
    for (const Cloud& one : clouds_) {
        if (!one.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = one.at[k];
        light.reach = kMiasmaTiles;
        light.height = 1.0f;
        const float lit = std::clamp(one.left / kDarkensUnder, 0.0f, 1.0f) * one.glow;
        for (int k = 0; k < 3; ++k) light.colour[k] = kMiasma[k] * lit;
    }
    return count;
}

}  // namespace mu::game
