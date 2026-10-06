#include "game/fx/ice.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {
constexpr float kTwoPi = 6.28318531f;
}  // namespace

float Ice::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Ice::open(const std::string& assetDir, content::Textures& textures,
               const content::Showing& table) {
    const std::string dir = assetDir + "/effects/ice/";
    keyCount_ = 0;
    for (int k = 0; k < 6; ++k) {
        char name[32];
        std::snprintf(name, sizeof(name), "Ice01_%02d.obj", k);
        keys_[k].clear();
        if (!loadEffectObj(dir + name, kUnit, "", keys_[k])) break;
        keyCount_ = k + 1;
    }
    loadEffectObj(dir + "Ice02.obj", kUnit, "", shard_);
    if (core::fileExists(dir + "ice.png")) {
        sheet_ = textures.load(dir + "ice.png", content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* sheet = table.effect("smoke01")) {
        smoke_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    core::logf("ice: %d keys, shard %s, sheet %s", keyCount_, shard_.empty() ? "NO" : "yes",
               bgfx::isValid(sheet_) ? "yes" : "NO");
    return keyCount_ > 0 && bgfx::isValid(sheet_);
}

void Ice::freeze(const float feet[3], float yaw) {
    if (keyCount_ == 0) return;
    Block* block = nullptr;
    for (Block& one : blocks_) {
        if (!one.alive) {
            block = &one;
            break;
        }
    }
    if (block != nullptr) {
        *block = Block{};
        block->alive = true;
        for (int k = 0; k < 3; ++k) block->at[k] = feet[k];
        block->yaw = yaw;
        block->left = kBlockFrames;
    }
    shards(feet, kShards);
}

// CreateBlood's `case MODEL_ICE_MONSTER` (ZzzEffectBlurSpark.cpp:449-455): the body is put out
// (`o->Live = false`) and ten MODEL_ICE_SMALL go up off its feet -- the same shard as the
// block's, with no block.
void Ice::shatter(const float feet[3], float scale) {
    if (keyCount_ == 0) return;
    shards(feet, kShatterShards, scale);
}

void Ice::shards(const float feet[3], int count, float scale) {
    // A smaller shard is thrown less far, but not in proportion, or it only drops.
    const float throwing = 0.5f + 0.5f * scale;
    for (int n = 0; n < count; ++n) {
        Shard* shard = nullptr;
        for (Shard& one : shards_) {
            if (!one.alive) {
                shard = &one;
                break;
            }
        }
        if (shard == nullptr) break;
        *shard = Shard{};
        shard->alive = true;
        for (int k = 0; k < 3; ++k) shard->at[k] = feet[k];
        shard->at[1] += kShardLift * kUnit * scale;
        shard->floor = feet[1];
        shard->size = between(0.8f, 1.1f) * scale;
        shard->yaw = unit() * kTwoPi;
        const float speed = between(6.4f, 32.0f) * kFps * kUnit * throwing;
        const float way = unit() * kTwoPi;
        shard->velocity[0] = std::sin(way) * speed;
        shard->velocity[2] = std::cos(way) * speed;
        shard->rise = between(8.0f, 23.0f) * kFps * kUnit * throwing;
        shard->left = between(32.0f, 47.0f);
    }
}

Ice::Wisp* Ice::puff(const float at[3]) {
    if (!bgfx::isValid(smoke_)) return nullptr;
    for (Wisp& one : wisps_) {
        if (one.alive) continue;
        one = Wisp{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        one.size = between(0.48f, 0.79f);
        one.spin = unit() * kTwoPi;
        return &one;
    }
    return nullptr;
}

void Ice::chill(const float feet[3], float tall, float seconds) {
    frostDue_ -= seconds * kFps;
    while (frostDue_ <= 0.0f) {
        frostDue_ += kFrostEvery;
        const float turn = unit() * kTwoPi;
        const float reach = kFrostRadius * (0.3f + unit() * 0.7f);
        const float at[3] = {feet[0] + std::cos(turn) * reach,
                             feet[1] + tall * (0.25f + unit() * 0.55f),
                             feet[2] + std::sin(turn) * reach};
        Wisp* wisp = puff(at);
        if (wisp == nullptr) return;
        wisp->frost = true;
        wisp->size = between(0.25f, 0.42f);
    }
}

void Ice::rime(const float feet[3], float tall, float seconds) {
    const float owed = seconds * kFps / kRimeEvery;
    int count = int(owed);
    if (unit() < owed - float(count)) ++count;
    for (int i = 0; i < count; ++i) {
        const float turn = unit() * kTwoPi;
        const float reach = kRimeRadius * (0.4f + unit() * 0.6f);
        const float at[3] = {feet[0] + std::cos(turn) * reach,
                             feet[1] + tall * (0.15f + unit() * 0.75f),
                             feet[2] + std::sin(turn) * reach};
        Wisp* wisp = puff(at);
        if (wisp == nullptr) return;
        wisp->frost = true;
        wisp->size = between(0.18f, 0.3f);
    }
}

void Ice::update(float seconds) {
    const float frames = seconds * kFps;
    for (Block& one : blocks_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.frame < kHoldsAt) {
            one.frame = std::min(kHoldsAt, one.frame + frames);
            continue;
        }
        one.alpha -= kFadeStep * frames;
        one.vapour += frames;
        while (one.vapour >= 1.0f) {
            one.vapour -= 1.0f;
            if (unit() < 0.5f) {
                const float at[3] = {one.at[0] + between(-32.0f, 32.0f) * kUnit,
                                     one.at[1] + between(32.0f, 160.0f) * kUnit,
                                     one.at[2] + between(-32.0f, 32.0f) * kUnit};
                puff(at);
            }
        }
        if (one.alpha <= 0.0f || one.left <= 0.0f) one.alive = false;
    }
    const float drag = std::pow(0.9f, frames);
    for (Shard& one : shards_) {
        if (!one.alive) continue;
        for (int k = 0; k < 3; k += 2) {
            one.at[k] += one.velocity[k] * seconds;
            one.velocity[k] *= drag;
        }
        one.at[1] += one.rise * seconds;
        one.rise -= 3.0f * kFps * kFps * kUnit * seconds;
        float turn = one.size * 32.0f * frames;
        if (one.at[1] < one.floor) {
            one.at[1] = one.floor;
            one.rise = -one.rise * 0.5f;
            one.left -= 4.0f;
            turn += one.size * 128.0f;
        }
        one.tumble += turn;
        one.left -= frames;
        one.vapour += frames;
        while (one.vapour >= 1.0f) {
            one.vapour -= 1.0f;
            if (unit() < 0.1f) puff(one.at);
        }
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Wisp& one : wisps_) {
        if (!one.alive) continue;
        one.age += frames;
        one.size *= std::pow(1.025f, frames);
        one.at[1] += (one.frost ? 0.5f : 0.2f * kUnit * kFps) * seconds;
        if (one.age >= kWispFrames) one.alive = false;
    }
}

void Ice::gather(gfx::Effects& effects) const {
    const float white[3] = {1.0f, 1.0f, 1.0f};
    for (const Block& one : blocks_) {
        if (!one.alive || keyCount_ == 0) continue;
        const int key = std::clamp(int(one.frame), 0, keyCount_ - 1);
        // Turned to the caster's yaw about the up axis.
        const float c = std::cos(one.yaw), s = std::sin(one.yaw);
        const float x[3] = {c, 0.0f, -s}, y[3] = {0.0f, 1.0f, 0.0f}, z[3] = {s, 0.0f, c};
        float lit[3];
        for (int k = 0; k < 3; ++k) lit[k] = white[k] * std::max(0.0f, one.alpha);
        submitEffectAlong(effects, keys_[key], sheet_, gfx::Blend::Additive, one.at, x, y, z,
                          kBlockScale, lit, 1.0f);
    }
    for (const Shard& one : shards_) {
        if (!one.alive || shard_.empty()) continue;
        // Yaw, then a tumble end over end about its own X.
        const float cy = std::cos(one.yaw), sy = std::sin(one.yaw);
        const float t = -one.tumble * 0.0174532925f;
        const float ct = std::cos(t), st = std::sin(t);
        const float x[3] = {cy, 0.0f, -sy};
        const float y[3] = {sy * st, ct, cy * st};
        const float z[3] = {sy * ct, -st, cy * ct};
        const float lit[3] = {kShardLight, kShardLight, kShardLight};
        submitEffectAlong(effects, shard_, sheet_, gfx::Blend::Additive, one.at, x, y, z,
                          one.size, lit, 1.0f);
    }
    for (const Wisp& one : wisps_) {
        if (!one.alive) continue;
        // Opens over its first fifth, holds, and goes out over its last twenty frames.
        const float open = std::min(1.0f, one.age / (kWispFrames * 0.2f));
        const float out = std::clamp((kWispFrames - one.age) / kWispFades, 0.0f, 1.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * 0.5f;
        sprite.spin = one.spin;
        const float* tint = one.frost ? kFrost : kBreath;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = tint[k] * open * out;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
