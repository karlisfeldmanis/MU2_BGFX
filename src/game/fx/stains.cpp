#include "game/fx/stains.h"

#include <algorithm>
#include <cmath>

#include "content/ground.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kReferenceFps = 25.0f;
constexpr float kDegrees = 3.14159265f / 180.0f;
// CreateBlood's offset off the head bone, in MU's units (100 a tile), either way.
constexpr float kSpread = 50.0f / 100.0f;
// CreatePointer: 50 + rand 0..30 frames. MovePointers: +0.004 a frame, the fade over the last 50.
constexpr float kLeastLife = 50.0f, kMostLife = 80.0f;
constexpr float kGrowth = 0.004f;
constexpr float kFadeFrames = 50.0f;
// MovePointers sets the light to (0.1, 0, 0) every frame.
constexpr float kLight[3] = {0.1f, 0.0f, 0.0f};
// MU's pointer pool, MAX_POINTERS: the oldest gives way.
constexpr size_t kMostStains = 100;
// Over the land and the grid it follows it with, as the click marker's (fx/marker.cpp).
constexpr float kLift = 0.04f;
constexpr float kCell = 0.3f;

}  // namespace

bool Stains::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table) {
    const content::EffectSheet* sheet = table.effect("blood_pool");
    if (sheet == nullptr) {
        core::logError("stains: no cooked effect named 'blood_pool'");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    return bgfx::isValid(sheet_);
}

float Stains::roll(float low, float high) {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return low + (high - low) * float(dice_ % 65536u) / 65536.0f;
}

void Stains::spill(const float head[3]) {
    for (int i = 0; i < 2; ++i) {
        if (stains_.size() >= kMostStains) {
            stains_.erase(std::min_element(stains_.begin(), stains_.end(),
                                           [](const Stain& a, const Stain& b) { return a.left < b.left; }));
        }
        Stain stain;
        stain.x = head[0] + roll(-kSpread, kSpread);
        stain.z = head[2] + roll(-kSpread, kSpread);
        stain.turn = roll(0.0f, 360.0f) * kDegrees;
        stain.across = roll(0.8f, 1.2f);
        stain.left = std::floor(roll(kLeastLife, kMostLife + 1.0f));
        stains_.push_back(stain);
    }
}

void Stains::update(float seconds) {
    const float frames = seconds * kReferenceFps;
    for (Stain& stain : stains_) {
        stain.left -= frames;
        stain.across += kGrowth * frames;
    }
    stains_.erase(std::remove_if(stains_.begin(), stains_.end(),
                                 [](const Stain& s) { return s.left <= 0.0f; }),
                  stains_.end());
}

void Stains::gather(gfx::Effects& effects, const content::Ground& ground) const {
    if (!bgfx::isValid(sheet_)) return;
    const float metresPerTile = ground.metresPerTile();
    for (const Stain& stain : stains_) {
        const float alpha = stain.left < kFadeFrames ? stain.left / kFadeFrames : 1.0f;
        const float across = stain.across * metresPerTile;
        const int cells = std::clamp(int(std::ceil(across / kCell)), 1, 6);
        const float c = std::cos(stain.turn), s = std::sin(stain.turn);
        const auto place = [&](float a, float b, float* out) {
            const float x = stain.x + (a * c - b * s) * across;
            const float z = stain.z + (a * s + b * c) * across;
            out[0] = x;
            out[1] = ground.heightAt(x, z) + kLift;
            out[2] = z;
        };
        gfx::Sprite sprite;
        sprite.placed = true;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Alpha;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kLight[k];
        sprite.colour[3] = alpha;
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
}

}  // namespace mu::game
