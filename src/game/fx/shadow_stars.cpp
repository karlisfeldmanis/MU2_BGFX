#include "game/fx/shadow_stars.h"

#include <algorithm>

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
// **Ours**: how much of MU's Light the rings keep, and how much of what is behind them the
// Shadow's stars take away. Lowered over the user's four asks (fx/shadow_stars.h): MU's crisp
// rings summed past white in HDR, where its 8-bit framebuffer clipped them green.
constexpr float kRingDim = 0.08f;
constexpr float kStarDim = 0.5f;
// A dozen joints a Shadow; room for a pack of them in sight.
constexpr size_t kStars = 12 * 32;
// The Poison Shadow's light: its own green at this much, about a quarter of the Poison spell's
// miasma (fx/poison.h), over this reach, hung this high; and no more than this many at once,
// of the renderer's four moving lights.
constexpr float kGlowDim = 0.3f;
constexpr float kGlowReach = 2.5f;
constexpr float kGlowHeight = 1.5f;
constexpr uint32_t kGlowsMost = 2;

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
    stars_.reserve(kStars);
    open_ = bgfx::isValid(shiny_) && bgfx::isValid(ring_);
    return open_;
}

void ShadowStars::shutdown() {
    stars_.clear();
    open_ = false;
}

void ShadowStars::update() {
    stars_.clear();
    glows_.clear();
}

void ShadowStars::glow(const float at[3], float fade) {
    if (!open_ || fade <= 0.0f || glows_.size() >= 32) return;
    Glow one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.fade = fade;
    glows_.push_back(one);
}

uint32_t ShadowStars::lights(gfx::PointLight* out, uint32_t max, const float near[3]) const {
    const uint32_t most = std::min<uint32_t>(max, kGlowsMost);
    auto distance = [near](const Glow& one) {
        const float dx = one.position[0] - near[0];
        const float dz = one.position[2] - near[2];
        return dx * dx + dz * dz;
    };
    // The nearest first, by picking; there are few.
    std::vector<const Glow*> order;
    for (const Glow& one : glows_) order.push_back(&one);
    std::sort(order.begin(), order.end(),
              [&](const Glow* a, const Glow* b) { return distance(*a) < distance(*b); });
    uint32_t count = 0;
    for (const Glow* one : order) {
        if (count >= most) break;
        gfx::PointLight& light = out[count++];
        for (int i = 0; i < 3; ++i) light.position[i] = one->position[i];
        light.reach = kGlowReach;
        light.height = kGlowHeight;
        for (int i = 0; i < 3; ++i) light.colour[i] = kGreen[i] * kGlowDim * one->fade;
    }
    return count;
}

void ShadowStars::star(const float at[3], bool poison, float fade) {
    if (!open_ || stars_.size() >= kStars || fade <= 0.0f) return;
    Star one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.poison = poison;
    one.fade = fade;
    stars_.push_back(one);
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
            sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = kStarDim * one.fade;
            sprite.sheet = shiny_;
            sprite.blend = gfx::Blend::Minus;
        }
        effects.add(sprite);
    }
}

}  // namespace mu::game
