#include "game/fx/eyes.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kUnit = 0.01f;
// eye01.jpg's own size in MU's units at Scale 1.
constexpr float kWidth = 32.0f, kHeight = 16.0f;
// Two a monster, for every elite in sight.
constexpr size_t kEyes = 64;

}  // namespace

bool Eyes::open(const std::string& assetDir, content::Textures& textures,
                const content::Showing& table) {
    const content::EffectSheet* sheet = table.effect("eye");
    if (sheet == nullptr) {
        core::logError("eyes: no cooked effect named 'eye'");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    eyes_.reserve(kEyes);
    open_ = bgfx::isValid(sheet_);
    return open_;
}

void Eyes::shutdown() {
    eyes_.clear();
    open_ = false;
}

void Eyes::update(float seconds) {
    eyes_.clear();
    // Wrapped at the sine's own period (pi seconds at 0.002 a millisecond) so a long session
    // does not lose the float's precision.
    clock_ = std::fmod(clock_ + seconds, 3.14159265f);
}

void Eyes::feed(const float at[3], float size) {
    if (!open_ || eyes_.size() >= kEyes) return;
    Eye one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.size = size;
    eyes_.push_back(one);
}

void Eyes::gather(gfx::Effects& effects) const {
    // sinf(WorldTime * 0.002f) * 0.3f + 0.8f, WorldTime in milliseconds; glColor clamps the
    // top of it, so the eyes hold full for a while at the crest of every pulse.
    const float light = std::min(1.0f, std::sin(clock_ * 2.0f) * 0.3f + 0.8f);
    for (const Eye& one : eyes_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        sprite.halfWidth = kWidth * kUnit * 0.5f * one.size;
        sprite.halfHeight = kHeight * kUnit * 0.5f * one.size;
        sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = light;
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
