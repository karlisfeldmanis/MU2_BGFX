#include "game/fx/aqua.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {

float Aqua::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Aqua::open(const std::string& assetDir, content::Textures& textures,
                const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    if (const content::EffectSheet* spark = table.effect("spark_flash")) {
        sheet_ = textures.load(assetDir + "/" + spark->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* glow = table.effect("light")) {
        glow_ = textures.load(assetDir + "/" + glow->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* smoke = table.effect("smoke01")) {
        smoke_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    }
    const bool sheets = bgfx::isValid(sheet_) && bgfx::isValid(glow_) && bgfx::isValid(smoke_);
    core::logf("aqua: sheets %s", sheets ? "yes" : "NO");
    return sheets;
}

void Aqua::clear() {
    for (Beam& one : beams_) one = Beam{};
    for (Puff& one : puffs_) one = Puff{};
}

void Aqua::cast(const float feet[3], float wayX, float wayZ) {
    for (Beam& beam : beams_) {
        if (beam.alive) continue;
        // A tile is a hundred of MU's units. The hand: ahead, to his side and up.
        const float perUnit = metres() / 100.0f;
        beam = Beam{};
        beam.alive = true;
        beam.way[0] = wayX;
        beam.way[1] = wayZ;
        beam.from[0] = feet[0] + wayX * kAhead * perUnit + wayZ * kSide * perUnit;
        beam.from[1] = feet[1] + kLift * perUnit;
        beam.from[2] = feet[2] + wayZ * kAhead * perUnit - wayX * kSide * perUnit;
        beam.left = kFrames;
        // The smoke, strung along it and a little to either side, waiting for it to fade.
        const float length = float(kSprites - 1) * kStep * perUnit;
        int made = 0;
        for (Puff& one : puffs_) {
            if (made == kPuffsPerBeam) break;
            if (one.alive) continue;
            const float along = length * (float(made) + unit()) / float(kPuffsPerBeam);
            const float aside = (unit() - 0.5f) * 0.6f;
            one = Puff{};
            one.alive = true;
            one.at[0] = beam.from[0] + wayX * along + wayZ * aside;
            one.at[1] = beam.from[1] + (unit() - 0.5f) * 0.4f;
            one.at[2] = beam.from[2] + wayZ * along - wayX * aside;
            one.spin = unit() * 6.28318531f;
            one.wait = kPuffWait + unit() * 3.0f;
            ++made;
        }
        return;
    }
}

void Aqua::update(float seconds) {
    const float frames = std::fmin(seconds, 0.1f) * kReferenceFps;
    for (Beam& beam : beams_) {
        if (!beam.alive) continue;
        beam.left -= frames;
        if (beam.left <= 0.0f) beam.alive = false;
    }
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        if (one.wait > 0.0f) {
            one.wait -= frames;
            continue;
        }
        one.age += frames;
        one.at[1] += kPuffRise * seconds;
        if (one.age >= kPuffFrames) one.alive = false;
    }
}

float Aqua::lit(const Beam& beam) const {
    return std::clamp(beam.left / kFadeFrames, 0.0f, 1.0f);
}

void Aqua::gatherEffects(gfx::Effects& effects) const {
    if (!bgfx::isValid(sheet_)) return;
    const float perUnit = metres() / 100.0f;
    const float side = kScale * kMetresPerScale * kTaller * 0.5f;
    const float tall = kScale * kMetresPerScale * kTallerUp * 0.5f;
    for (const Beam& beam : beams_) {
        if (!beam.alive) continue;
        const float light = kShare * lit(beam);
        for (int j = 0; j < kSprites; ++j) {
            const float along = float(j) * kStep * perUnit;
            // Swelling in over its first sprites, narrowing and fading over its last third.
            const float share = float(j) / float(kSprites - 1);
            const float swell = std::min(1.0f, float(j + 1) / float(kSwell));
            const float taper =
                share <= kTaperFrom
                    ? 1.0f
                    : 1.0f - (1.0f - kTaperEnd) * (share - kTaperFrom) / (1.0f - kTaperFrom);
            gfx::Sprite sprite;
            sprite.position[0] = beam.from[0] + beam.way[0] * along;
            sprite.position[1] = beam.from[1];
            sprite.position[2] = beam.from[2] + beam.way[1] * along;
            // Unturned, as MU's CreateSprite lays them: turned each its own way, the star's rays
            // stuck out of the line at random and it read as crooked (the user, 2026-10-02:
            // "aqua has to be straight").
            sprite.spin = 0.0f;
            sprite.blend = gfx::Blend::Additive;
            // The soft body, tall.
            if (bgfx::isValid(glow_)) {
                gfx::Sprite body = sprite;
                body.halfWidth = side * taper;
                body.halfHeight = tall * taper;
                for (int k = 0; k < 3; ++k) body.colour[k] = kLight[k] * light * swell * taper;
                body.sheet = glow_;
                if (!effects.add(body)) return;
            }
            // MU's spark_flash at its own square size, the core.
            sprite.halfWidth = sprite.halfHeight = side * taper;
            for (int k = 0; k < 3; ++k) {
                sprite.colour[k] = kLight[k] * light * kCoreShare * swell * taper;
            }
            sprite.sheet = sheet_;
            if (!effects.add(sprite)) return;
        }
    }
    // The smoke after it: smoke01 mixed as Inferno's is, opening as it rises.
    for (const Puff& one : puffs_) {
        if (!one.alive || one.wait > 0.0f || !bgfx::isValid(smoke_)) continue;
        const float t = std::clamp(one.age / kPuffFrames, 0.0f, 1.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = 0.5f * (kPuffBorn + (kPuffGrown - kPuffBorn) * t);
        sprite.spin = one.spin + t * 0.6f;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kPuffGrey[k];
        const float in = std::min(1.0f, t / 0.15f);
        const float out = 1.0f - std::clamp((t - 0.35f) / 0.65f, 0.0f, 1.0f);
        sprite.colour[3] = kPuffAlpha * in * out;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Smoke;
        if (!effects.add(sprite)) return;
    }
}

uint32_t Aqua::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    const float perUnit = metres() / 100.0f;
    const float length = float(kSprites - 1) * kStep * perUnit;
    for (const Beam& beam : beams_) {
        if (!beam.alive) continue;
        for (int g = 0; g < kGlows && count < max; ++g) {
            const float along = length * (float(g) + 0.5f) / float(kGlows);
            gfx::PointLight& one = out[count++];
            one.position[0] = beam.from[0] + beam.way[0] * along;
            one.position[1] = beam.from[1];
            one.position[2] = beam.from[2] + beam.way[1] * along;
            one.reach = kGlowTiles * metres();
            one.height = 1.5f;
            for (int k = 0; k < 3; ++k) one.colour[k] = kLight[k] * lit(beam);
        }
    }
    return count;
}

}  // namespace mu::game
