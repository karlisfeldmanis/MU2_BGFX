#include "game/fx/body_flames.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kUnit = 0.01f;
constexpr float kFps = 25.0f;
constexpr float kSheetUnits = 64.0f;  // Flame01 is a 64 square
constexpr size_t kFlames = 1024;  // the Death Beam Knight alone keeps ~380

}  // namespace

uint32_t BodyFlames::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

bool BodyFlames::open(const std::string& assetDir, content::Textures& textures,
                      const content::Showing& table) {
    const content::EffectSheet* sheet = table.effect("flame");
    if (sheet == nullptr) {
        core::logError("body flames: no cooked effect named 'flame'");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    flames_.assign(kFlames, Flame{});
    open_ = bgfx::isValid(sheet_);
    return open_;
}

void BodyFlames::shutdown() {
    flames_.clear();
    open_ = false;
}

float BodyFlames::luminosity() const { return std::sin(clock_ * 2.0f) * 0.3f + 0.7f; }

void BodyFlames::handFlame(const float at[3], const float along[3], float scale) {
    if (!open_) return;
    for (Flame& one : flames_) {
        if (one.alive) continue;
        one = Flame{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        for (int k = 0; k < 3; ++k) one.along[k] = along[k];
        one.left = 15.0f;
        one.scale = scale + float(roll() % 32 + 32) * 0.01f;
        one.speed = float(roll() % 4 + 4) * 0.15f;
        const float l = luminosity();
        one.light[0] = l;
        one.light[1] = one.light[2] = l * 0.5f;
        one.spin = float(roll() % 360) * 3.14159265f / 180.0f;
        return;
    }
}

BodyFlames::Flame* BodyFlames::slot() {
    if (!open_) return nullptr;
    for (Flame& one : flames_) {
        if (!one.alive) {
            one = Flame{};
            one.alive = true;
            one.spin = float(roll() % 360) * 3.14159265f / 180.0f;
            const float l = luminosity();
            one.light[0] = l;
            one.light[1] = one.light[2] = l * 0.5f;
            return &one;
        }
    }
    return nullptr;
}

void BodyFlames::segmentFlame(const float from[3], const float to[3], float scale,
                              float share) {
    float along[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const float length = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
    if (length < 1e-5f) return;
    Flame* one = slot();
    if (one == nullptr) return;
    for (float& a : along) a /= length;
    const float units = length / kUnit;
    const float inter = units * float(roll() % 80) / 100.0f;
    for (int k = 0; k < 3; ++k) one->at[k] = from[k] + along[k] * inter * kUnit;
    for (int k = 0; k < 3; ++k) one->along[k] = along[k];
    one->speed = (units - inter) / 15.0f;
    one->scale = scale + float(roll() % 32 + 32) * 0.01f;
    one->left = 10.0f;
    one->subtype = 2;
    for (float& l : one->light) l *= share;
}

void BodyFlames::stillFlame(const float at[3], float scale, float share) {
    Flame* one = slot();
    if (one == nullptr) return;
    for (int k = 0; k < 3; ++k) one->at[k] = at[k];
    one->scale = scale + float(roll() % 32 + 32) * 0.01f;
    one->left = 10.0f;
    one->subtype = 3;
    for (float& l : one->light) l *= share;
}

void BodyFlames::update(float seconds) {
    clock_ = std::fmod(clock_ + seconds, 3.14159265f);
    if (!open_) return;
    const float frames = seconds * kFps;
    for (Flame& one : flames_) {
        if (!one.alive) continue;
        const float before = one.left;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        // The kick at LifeTime 10, once, as the frame crosses it (subtype 1's alone).
        if (one.subtype == 1 && before > 10.0f && one.left <= 10.0f) {
            one.speed += 32.0f * 0.2f;
            one.scale -= 0.15f;
        }
        for (int k = 0; k < 3; ++k) {
            one.at[k] += one.along[k] * one.speed * kUnit * frames;
            if (one.subtype != 3) one.light[k] = std::max(0.0f, one.light[k] - 0.05f * frames);
        }
        one.spin = float(roll() % 360) * 3.14159265f / 180.0f;
    }
}

void BodyFlames::gather(gfx::Effects& effects) const {
    if (!open_) return;
    for (const Flame& one : flames_) {
        if (!one.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight =
            std::max(0.0f, one.scale) * kSheetUnits * kUnit * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = one.light[k];
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) return;
    }
}

}  // namespace mu::game
