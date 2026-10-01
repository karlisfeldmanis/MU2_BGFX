#include "game/fx/shadow_stars.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kUnit = 0.01f;
// Shiny02 at Scale 2.5 and Magic_Ground2 at 0.8, in MU's units, halved.
constexpr float kStarHalfWidth = 32.0f * 2.5f * kUnit * 0.5f;
constexpr float kStarHalfHeight = 64.0f * 2.5f * kUnit * 0.5f;
constexpr float kRingHalf = 128.0f * 0.8f * kUnit * 0.5f;
// The Poison Shadow's Light.
constexpr float kGreen[3] = {0.2f, 0.7f, 0.1f};
// **Ours**: the rings at this much of MU's Light. Thirty-nine soft rings over one body add up
// past white in HDR, where MU's 8-bit framebuffer clipped each channel and kept it green; as
// the Gorgon star's kStarDim, for the same reason. On the blurred sheet, and lowered step by step
// for the user's "too active" and "it has to be more subtle" (2026-10-01): 0.35, 0.2, 0.1.
constexpr float kRingDim = 0.1f;
// **Ours**, the same asks: the Shadow's stars take this much of what is behind them rather than
// all of it, and the sparks are this much of MU's Light.
constexpr float kStarDim = 0.6f;
constexpr float kSparkDim = 0.3f;
// Thirty-nine joints a Shadow; room for a pack of them in sight.
constexpr size_t kStars = 39 * 16;
constexpr size_t kSparks = 39 * 8;
constexpr float kSparkLife = 2.0f;           // reference frames
constexpr float kSparkTurn = 20.0f * 3.14159265f / 180.0f;  // a reference frame

}  // namespace

bool ShadowStars::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table) {
    auto load = [&](const char* name) {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("shadow stars: no cooked effect named '%s'", name);
            return bgfx::TextureHandle BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    // Ours: blurred copies of MU's two sheets (pipeline/index.py), the user's "more blurry".
    shiny_ = load("shiny_02_soft");
    ring_ = load("magic_ground_soft");
    energy_ = load("energy");
    stars_.reserve(kStars);
    sparks_.reserve(kSparks);
    open_ = bgfx::isValid(shiny_) && bgfx::isValid(ring_) && bgfx::isValid(energy_);
    return open_;
}

void ShadowStars::shutdown() {
    stars_.clear();
    sparks_.clear();
    open_ = false;
}

void ShadowStars::update(float seconds) {
    stars_.clear();
    const float frames = seconds * 25.0f;
    for (Spark& one : sparks_) {
        one.left -= frames;
        one.spin += kSparkTurn * frames;
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(),
                                 [](const Spark& one) { return one.left <= 0.0f; }),
                  sparks_.end());
}

void ShadowStars::star(const float at[3], bool poison, float fade) {
    if (!open_ || stars_.size() >= kStars || fade <= 0.0f) return;
    Star one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.poison = poison;
    one.fade = fade;
    stars_.push_back(one);
}

void ShadowStars::spark(const float at[3], bool poison) {
    if (!open_ || sparks_.size() >= kSparks) return;
    auto roll = [this](int n) {
        dice_ ^= dice_ << 13;
        dice_ ^= dice_ >> 17;
        dice_ ^= dice_ << 5;
        return int(dice_ % uint32_t(n));
    };
    Spark one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    // Scale * (rand() % 8 + 6) * 0.1 over Thunder01's 64 units.
    one.halfSize = 64.0f * float(roll(8) + 6) * 0.1f * kUnit * 0.5f;
    one.spin = float(roll(360)) * 3.14159265f / 180.0f;
    one.left = kSparkLife;
    one.poison = poison;
    sparks_.push_back(one);
}

void ShadowStars::gather(gfx::Effects& effects) const {
    if (!open_) return;
    for (const Star& one : stars_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        if (one.poison) {
            sprite.halfWidth = sprite.halfHeight = kRingHalf;
            for (int i = 0; i < 3; ++i) sprite.colour[i] = kGreen[i] * kRingDim * one.fade;
            sprite.sheet = ring_;
            sprite.blend = gfx::Blend::Additive;
        } else {
            sprite.halfWidth = kStarHalfWidth;
            sprite.halfHeight = kStarHalfHeight;
            // Light (1, 1, 1): the whole of the sheet taken out of what is behind it.
            sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = kStarDim * one.fade;
            sprite.sheet = shiny_;
            sprite.blend = gfx::Blend::Minus;
        }
        effects.add(sprite);
    }
    for (const Spark& one : sparks_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        sprite.halfWidth = sprite.halfHeight = one.halfSize;
        sprite.spin = one.spin;
        for (int i = 0; i < 3; ++i) sprite.colour[i] = (one.poison ? kGreen[i] : 1.0f) * kSparkDim;
        sprite.sheet = energy_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
