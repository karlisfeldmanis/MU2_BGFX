#include "game/fx/dust.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kReferenceFps = 25.0f;
// A tile is a hundred of MU's units and one metre here.
constexpr float kUnit = 0.01f;
// A puff every second reference frame over a 32-frame life is sixteen a running mount.
constexpr size_t kPuffs = 128;
constexpr float kLife = 32.0f;
// smoke02's side, which MU's Width is times Scale (as the Budge Dragon's, fx/breath.cpp).
constexpr float kSheet = 64.0f;
// How much of it shows, **ours**: through the soft Dust blend smoke02 reads far denser than MU's
// EnableAlphaBlend3 drew it, and a trail of orange clouds hid the horse's legs (the user: "dust
// is to much vissible"). A third, faded in over the first fifth of its life as the Budge
// Dragon's smoke is (fx/breath.cpp), and born a little smaller.
constexpr float kDustAlpha = 0.3f;
constexpr float kDustScale = 0.75f;

}  // namespace

uint32_t Dust::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

bool Dust::open(const std::string& assetDir, content::Textures& textures,
                const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const content::EffectSheet* sheet = table.effect("smoke");
    if (sheet == nullptr) {
        core::logError("dust: no cooked effect named 'smoke'");
        return false;
    }
    smoke_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    puffs_.reserve(kPuffs);
    open_ = bgfx::isValid(smoke_);
    return open_;
}

void Dust::shutdown() {
    puffs_.clear();
    open_ = false;
}

void Dust::puff(const float at[3], float yaw) {
    if (!open_ || puffs_.size() >= kPuffs) return;
    Puff one;
    one.position[0] = at[0] + float(int(roll() % 16) - 8) * kUnit;
    one.position[1] = at[1];
    one.position[2] = at[2] + float(int(roll() % 16) - 8) * kUnit;
    one.scale = float(roll() % 32 + 32) * 0.01f;
    one.spin = float(roll() % 360) * 3.14159265f / 180.0f;
    // Behind him: a figure faces (sin yaw, cos yaw) on the ground (play_show.cpp), and MU's 3 is
    // along the model's +Y, the back.
    one.velocity[0] = -std::sin(yaw) * 3.0f * kUnit;
    one.velocity[1] = -std::cos(yaw) * 3.0f * kUnit;
    puffs_.push_back(one);
}

void Dust::update(float seconds) {
    const float frames = seconds * kReferenceFps;
    const float drag = std::pow(0.9f, frames);
    for (Puff& one : puffs_) {
        one.life -= frames;
        one.scale += 0.08f * frames;
        one.position[0] += one.velocity[0] * frames;
        one.position[2] += one.velocity[1] * frames;
        one.velocity[0] *= drag;
        one.velocity[1] *= drag;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& one) { return one.life <= 0.0f; }),
                 puffs_.end());
}

void Dust::gather(gfx::Effects& effects) const {
    for (const Puff& one : puffs_) {
        gfx::Sprite sprite;
        const float half = kSheet * one.scale * kDustScale * kUnit * 0.5f;
        const float floor =
            ground_ ? ground_->heightAt(one.position[0], one.position[2]) : one.position[1];
        sprite.position[0] = one.position[0];
        sprite.position[1] = floor + half;
        sprite.position[2] = one.position[2];
        sprite.halfWidth = sprite.halfHeight = half;
        sprite.spin = one.spin;
        const float light = std::clamp(one.life / kLife, 0.0f, 1.0f);
        const float risen = std::min(1.0f, (1.0f - light) / 0.2f);
        sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = light;
        sprite.colour[3] = light * risen * kDustAlpha;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Dust;
        effects.add(sprite);
    }
}

}  // namespace mu::game
