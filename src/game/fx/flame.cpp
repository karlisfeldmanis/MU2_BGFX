#include "game/fx/flame.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/fx/meteor.h"

namespace mu::game {
namespace {
constexpr float kTwoPi = 6.28318531f;
// The scorch is laid as a grid on the land, as the click marker is, so two tiles of it follow a
// slope rather than cutting into it. Half a metre a cell.
constexpr float kScorchCell = 0.5f;
}  // namespace

float Flame::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Flame::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table, const content::Ground* ground, Meteor* debris) {
    ground_ = ground;
    debris_ = debris;
    if (const content::EffectSheet* sheet = table.effect("flame")) {
        sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    core::logf("flame: sheet %s, stones %s", bgfx::isValid(sheet_) ? "yes" : "NO",
               debris_ ? "the meteor's" : "none");
    return bgfx::isValid(sheet_);
}

void Flame::light(const float at[3], float yaw) {
    for (Fire& one : fires_) {
        if (one.alive) continue;
        one = Fire{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        one.yaw = yaw;
        one.left = kFrames;
        // The first frame's plumes at once, as `CreateEffect` is followed by a move in the
        // same frame: a fire lit is burning on the frame it is lit.
        one.owed = 1.0f;
        return;
    }
}

void Flame::update(float seconds) {
    const float frames = seconds * kFps;
    for (Fire& fire : fires_) {
        if (!fire.alive) continue;
        // Both shimmers, re-rolled every frame as the client re-rolls them.
        fire.scorch = between(0.8f, 1.1f);
        fire.glow = between(0.7f, 1.0f);
        fire.owed += frames;
        while (fire.owed >= 1.0f && fire.left > 0.0f) {
            fire.owed -= 1.0f;
            for (int n = 0; n < kPlumes; ++n) {
                Plume* slot = nullptr;
                for (Plume& one : plumes_) {
                    if (!one.alive) {
                        slot = &one;
                        break;
                    }
                }
                if (slot == nullptr) break;  // a ceiling, not a target: what will not fit drops
                *slot = Plume{};
                slot->alive = true;
                slot->at[0] = fire.at[0] + between(-kSpread, kSpread) * kUnit;
                slot->at[1] = fire.at[1];
                slot->at[2] = fire.at[2] + between(-kSpread, kSpread) * kUnit;
                slot->rise = between(kSlowest, kFastest) * kUnit * kFps;
                slot->size = between(kSmallest, kLargest) * kSheetUnits * kUnit;
                slot->spin = unit() * kTwoPi;
                slot->left = kPlumeFrames;
            }
            // `rand_fps_check(8)`: a stone about one frame in eight, five over a fire's life.
            if (debris_ != nullptr && unit() * float(kStoneOdds) < 1.0f) {
                debris_->stones(fire.at[0], fire.at[2], fire.at[1], 1);
            }
        }
        fire.left -= frames;
        if (fire.left <= 0.0f) fire.alive = false;
    }
    // Straight up at the speed each was born with, and nothing else: no gravity, no drag, no
    // fade -- the particle mover has no arm for sub-type 0.
    for (Plume& one : plumes_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        one.at[1] += one.rise * seconds;
    }
}

void Flame::layScorch(gfx::Effects& effects, const Fire& fire) const {
    if (ground_ == nullptr) return;
    const float across = kScorchTiles * ground_->metresPerTile();
    const int cells = std::clamp(int(std::ceil(across / kScorchCell)), 1, 8);
    // `-o->Angle[2]`: MU turns a terrain bitmap the other way round from a body.
    const float c = std::cos(-fire.yaw), s = std::sin(-fire.yaw);
    const auto place = [&](float a, float b, float* out) {
        const float x = fire.at[0] + (a * c - b * s) * across;
        const float z = fire.at[2] + (a * s + b * c) * across;
        out[0] = x;
        out[1] = ground_->heightAt(x, z) + kScorchLift;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = sheet_;
    sprite.blend = gfx::Blend::Additive;
    // What is left of its life dims nothing: MU draws the scorch at full shimmer to its last
    // frame, and the fire simply goes.
    for (int k = 0; k < 3; ++k) sprite.colour[k] = fire.scorch;
    sprite.colour[3] = 1.0f;
    const float inv = 1.0f / float(cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float a0 = float(i) * inv - 0.5f, a1 = a0 + inv;
            const float b0 = float(j) * inv - 0.5f, b1 = b0 + inv;
            const float ab[4][2] = {{a0, b0}, {a1, b0}, {a1, b1}, {a0, b1}};
            for (int k = 0; k < 4; ++k) {
                place(ab[k][0], ab[k][1], sprite.corner[k]);
                sprite.cornerUv[k][0] = ab[k][0] + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - ab[k][1];
            }
            place((a0 + a1) * 0.5f, (b0 + b1) * 0.5f, sprite.position);
            effects.add(sprite);
        }
    }
}

void Flame::gather(gfx::Effects& effects) const {
    if (!bgfx::isValid(sheet_)) return;
    for (const Fire& fire : fires_) {
        if (fire.alive) layScorch(effects, fire);
    }
    for (const Plume& one : plumes_) {
        if (!one.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * 0.5f;
        sprite.spin = one.spin;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

uint32_t Flame::lights(gfx::PointLight* out, uint32_t max) const {
    uint32_t count = 0;
    const float tile = ground_ ? ground_->metresPerTile() : 1.0f;
    for (const Fire& one : fires_) {
        if (!one.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = one.at[k];
        light.reach = kGlowTiles * tile;
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kGlow[k] * one.glow;
    }
    return count;
}

}  // namespace mu::game
