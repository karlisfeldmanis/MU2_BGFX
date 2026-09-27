#include "game/fx/snort.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kReferenceFps = 25.0f;
// A tile is a hundred of MU's units and one metre here.
constexpr float kUnit = 0.01f;

// A puff every second reference frame over a 16-frame life is eight a snorting bull, and a
// window never runs longer than the life; room for a few dozen bulls at once.
constexpr size_t kPuffs = 256;

// BITMAP_SMOKE subtype 0 (ZzzEffectParticle.cpp): LifeTime 16, Scale (rand()%32 + 48) * 0.01,
// Rotation WorldTime % 360; then every frame Luminosity = LifeTime / 8 on every channel,
// Gravity += 0.2, Position[2] += Gravity, Scale += 0.05. Width is the sheet's 64 times Scale.
constexpr float kSheet = 64.0f;

}  // namespace

uint32_t Snort::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

bool Snort::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table) {
    const content::EffectSheet* sheet = table.effect("smoke01");
    if (sheet == nullptr) {
        core::logError("snort: no cooked effect named 'smoke01'");
        return false;
    }
    smoke_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    puffs_.reserve(kPuffs);
    open_ = bgfx::isValid(smoke_);
    return open_;
}

void Snort::shutdown() {
    puffs_.clear();
    open_ = false;
}

void Snort::puff(const float at[3]) {
    if (!open_) return;
    if (puffs_.size() >= kPuffs) {
        ++refused_;
        return;
    }
    Puff one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.scale = float(roll() % 32 + 48) * 0.01f;
    one.spin = float(roll() % 360) * 3.14159265f / 180.0f;
    puffs_.push_back(one);
}

void Snort::update(float seconds) {
    const float frames = seconds * kReferenceFps;
    for (Puff& one : puffs_) {
        one.life -= frames;
        // Gravity gains a constant and the height gains Gravity: the lift accelerates.
        one.gravity += 0.2f * frames;
        one.position[1] += one.gravity * kUnit * frames;
        one.scale += 0.05f * frames;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& one) { return one.life <= 0.0f; }),
                 puffs_.end());
}

void Snort::gather(gfx::Effects& effects) const {
    for (const Puff& one : puffs_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        sprite.halfWidth = sprite.halfHeight = kSheet * one.scale * kUnit * 0.5f;
        sprite.spin = one.spin;
        // LifeTime / 8, which glColor clamps: whole for the first half of the life, then out.
        const float light = std::clamp(one.life / 8.0f, 0.0f, 1.0f);
        sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = light;
        sprite.colour[3] = 1.0f;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
