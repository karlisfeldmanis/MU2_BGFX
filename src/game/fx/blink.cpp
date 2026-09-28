#include "game/fx/blink.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {

float Blink::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Blink::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table) {
    if (const content::EffectSheet* sheet = table.effect("spark_flash")) {
        sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    core::logf("blink: spark %s", bgfx::isValid(sheet_) ? "yes" : "NO");
    return bgfx::isValid(sheet_);
}

void Blink::cast(const float feet[3]) {
    Pillar* pillar = nullptr;
    for (Pillar& one : pillars_) {
        if (!one.alive) {
            pillar = &one;
            break;
        }
    }
    if (pillar == nullptr) pillar = &pillars_[0];
    *pillar = Pillar{};
    pillar->alive = true;
    for (int k = 0; k < 3; ++k) pillar->feet[k] = feet[k];
    pillar->left = kLifeFrames;
    pillar->due = 1.0f;
    row(*pillar);
}

void Blink::row(const Pillar& pillar) {
    // `Luminosity = LifeTime * 0.1`, the whole row lit alike.
    const float light = std::clamp(pillar.left * 0.1f, 0.0f, 1.0f) * kBrightness;
    for (int j = 0; j < kRows; ++j) {
        Spark* spark = nullptr;
        for (Spark& one : sparks_) {
            if (!one.alive) {
                spark = &one;
                break;
            }
        }
        if (spark == nullptr) return;
        spark->alive = true;
        spark->at[0] = pillar.feet[0];
        spark->at[1] = pillar.feet[1] + float(j + 1) * kRowUnits * kUnit;
        spark->at[2] = pillar.feet[2];
        // A random direction: MU turns (0, -50, 0) by three random angles.
        const float z = unit() * 2.0f - 1.0f;
        const float turn = unit() * 6.28318531f;
        const float flat = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float speed = kFlyUnits * kUnit * kReferenceFps;
        spark->velocity[0] = flat * std::cos(turn) * speed;
        spark->velocity[1] = z * speed;
        spark->velocity[2] = flat * std::sin(turn) * speed;
        spark->scale = kScale;
        spark->light = light;
        spark->spin = unit() * 6.28318531f;
    }
}

void Blink::update(float seconds) {
    const float frames = seconds * kReferenceFps;
    for (Spark& one : sparks_) {
        if (!one.alive) continue;
        for (int k = 0; k < 3; ++k) one.at[k] += one.velocity[k] * seconds;
        one.scale -= kShrink * frames;
        if (one.scale < kGone) one.alive = false;
    }
    for (Pillar& one : pillars_) {
        if (!one.alive) continue;
        one.left -= frames;
        one.due -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        // A row a reference frame, at any frame rate.
        while (one.due <= 0.0f) {
            one.due += 1.0f;
            row(one);
        }
    }
}

void Blink::gather(gfx::Effects& effects) const {
    if (!bgfx::isValid(sheet_)) return;
    for (const Spark& one : sparks_) {
        if (!one.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.scale * kMetresPerScale * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = one.light;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
