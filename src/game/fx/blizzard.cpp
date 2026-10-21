#include "game/fx/blizzard.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {
constexpr float kTwoPi = 6.28318531f;
}  // namespace

float Blizzard::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Blizzard::open(const std::string& assetDir, content::Textures& textures,
                    const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/ice/";
    crystal_.clear();
    loadEffectObj(dir + "Blizzard01.obj", kUnit, "", crystal_);
    if (core::fileExists(dir + "ice.png")) {
        sheet_ = textures.load(dir + "ice.png", content::TextureRole::Albedo);
    }
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    smoke_ = sheet("smoke01");   // BITMAP_SMOKE
    spark_ = sheet("energy");    // BITMAP_ENERGY, Thunder01
    flame_ = sheet("fire03");    // BITMAP_FIRE+2
    star_ = sheet("shiny_02");   // BITMAP_SHINY+1
    glow_ = sheet("light");      // BITMAP_LIGHT
    core::logf("blizzard: crystal %s, sheet %s", crystal_.empty() ? "NO" : "yes",
               bgfx::isValid(sheet_) ? "yes" : "NO");
    return !crystal_.empty() && bgfx::isValid(sheet_);
}

float Blizzard::floorAt(float x, float z) const {
    return ground_ != nullptr ? ground_->heightAt(x, z) : 0.0f;
}

void Blizzard::cast(const float feet[3], float yaw) {
    if (crystal_.empty()) return;
    // MU's ground axes are X and Y, this engine's x and -z; up is MU's Z and this y.
    for (int i = 0; i < kCrystalsACast; ++i) {
        Crystal* crystal = nullptr;
        for (Crystal& one : crystals_) {
            if (!one.alive) {
                crystal = &one;
                break;
            }
        }
        if (crystal == nullptr) return;
        *crystal = Crystal{};
        crystal->alive = true;
        crystal->first = i == 0;
        crystal->yaw = yaw;
        crystal->left = float(15 + int(unit() * 15.0f));
        crystal->fall = -20.0f - float(10 + int(unit() * 30.0f));
        const float rise = float(int(unit() * 50.0f) * i) + kBirthHeight;
        const float aside[2] = {float(int(unit() * 300.0f)) - 150.0f + 100.0f,
                                float(int(unit() * 300.0f)) - 150.0f};
        crystal->line[0] = feet[0] + aside[0] * kUnit;
        crystal->line[1] = feet[2] - aside[1] * kUnit;
        crystal->at[0] = crystal->line[0];
        crystal->at[1] = feet[1] + rise * kUnit;
        crystal->at[2] = crystal->line[1];
        crystal->spin = unit() * kTwoPi;
    }
}

Blizzard::Puff* Blizzard::puff(int kind, const float at[3]) {
    for (Puff& one : puffs_) {
        if (one.alive) continue;
        one = Puff{};
        one.alive = true;
        one.kind = kind;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        one.spin = unit() * kTwoPi;
        switch (kind) {
            case 0:  // the trail's smoke, MU's 1.5
                one.size = between(0.5f, 0.75f);
                one.life = 20.0f;
                break;
            case 1:  // a spark, MU's 0.5
                one.size = between(0.25f, 0.4f);
                one.life = 8.0f;
                break;
            case 2:  // a flame, the crystal's own 0.5
                one.size = between(0.3f, 0.45f);
                one.life = 10.0f;
                break;
            default:  // the ground's blue smoke, (rand() % 32 + 80) * 0.025
                one.size = float(80 + int(unit() * 32.0f)) * 0.025f * 0.5f;
                one.life = 28.0f;
                break;
        }
        return &one;
    }
    return nullptr;
}

void Blizzard::update(float seconds, std::vector<Landing>& landed) {
    const float frames = seconds * kFps;
    for (Crystal& one : crystals_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.standing) {
            // MU's subtype 1: its light down by a tenth a frame, twenty frames.
            one.light /= std::pow(1.1f, frames);
            if (one.left <= 0.0f) one.alive = false;
            continue;
        }
        // The line moves against X, the crystal shivers about it, and it falls faster each frame.
        one.line[0] -= kDrift * kUnit * frames;
        one.at[0] = one.line[0] + std::sin(unit() * 10.0f) * 10.0f * kUnit;
        one.at[2] = one.line[1] - std::sin(unit() * 10.0f) * 10.0f * kUnit;
        one.at[1] += one.fall * kUnit * frames;
        one.fall -= unit() * 5.0f * frames;
        one.light = std::min(kLightMost, one.light + kLightStep * frames);
        // A smoke every frame, and a spark or a flame by a coin.
        one.owed += frames;
        while (one.owed >= 1.0f) {
            one.owed -= 1.0f;
            puff(0, one.at);
            puff(unit() < 0.5f ? 1 : 2, one.at);
        }
        const float floor = floorAt(one.at[0], one.at[2]);
        if (one.at[1] < floor) {
            one.at[1] = floor;
            const float smoke[3] = {one.at[0], floor + 0.5f, one.at[2]};
            puff(3, smoke);
            landed.push_back({one.at[0], floor, one.at[2], unit() < 0.2f, one.first});
            one.standing = true;
            one.left = kStandingFrames;
            continue;
        }
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        one.age += frames;
        if (one.kind == 0 || one.kind == 3) {
            one.size *= std::pow(1.025f, frames);
            one.at[1] += 0.2f * kUnit * kFps * seconds;
        }
        if (one.age >= one.life) one.alive = false;
    }
}

void Blizzard::gather(gfx::Effects& effects) const {
    for (const Crystal& one : crystals_) {
        if (!one.alive || crystal_.empty()) continue;
        const float c = std::cos(one.yaw), s = std::sin(one.yaw);
        const float x[3] = {c, 0.0f, -s}, y[3] = {0.0f, 1.0f, 0.0f}, z[3] = {s, 0.0f, c};
        const float lit[3] = {one.light, one.light, one.light};
        submitEffectAlong(effects, crystal_, sheet_, gfx::Blend::Additive, one.at, x, y, z, kScale,
                          lit, 1.0f);
        if (one.standing) continue;
        // The star and the glow drawn on it each frame, in its rising light.
        const float light = std::min(1.0f, one.light);
        if (bgfx::isValid(star_)) {
            gfx::Sprite star;
            for (int k = 0; k < 3; ++k) star.position[k] = one.at[k];
            star.halfWidth = star.halfHeight = (0.8f + 0.2f * float(int(one.spin * 4.0f) % 4)) * 0.3f;
            star.spin = one.spin + one.left * 0.4f;
            for (int k = 0; k < 3; ++k) star.colour[k] = light;
            star.sheet = star_;
            star.blend = gfx::Blend::Additive;
            effects.add(star);
        }
        if (bgfx::isValid(glow_)) {
            gfx::Sprite glow;
            for (int k = 0; k < 3; ++k) glow.position[k] = one.at[k];
            glow.halfWidth = glow.halfHeight = 0.5f;
            glow.spin = one.spin;
            for (int k = 0; k < 3; ++k) glow.colour[k] = light * kFrost[k];
            glow.sheet = glow_;
            glow.blend = gfx::Blend::Additive;
            effects.add(glow);
        }
    }
    for (const Puff& one : puffs_) {
        if (!one.alive) continue;
        const bgfx::TextureHandle sheet = one.kind == 1 ? spark_ : one.kind == 2 ? flame_ : smoke_;
        if (!bgfx::isValid(sheet)) continue;
        const float open = std::min(1.0f, one.age / (one.life * 0.2f));
        const float out = std::clamp((one.life - one.age) / (one.life * 0.6f), 0.0f, 1.0f);
        const float* tint = one.kind == 3 ? kGroundSmoke : kFrost;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = tint[k] * open * out;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
