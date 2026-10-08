#include "game/fx/set_shine.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// The ribbon: CreateJoint(BITMAP_FLARE, ..., 0, o) at its default scale of ten
// (ZzzEffectJoint.cpp:1669-1688): one joint on a ring of forty, a cross ten units wide, a
// hundred frames, climbing 0 to 1.5 units a frame (Direction[2] = rand() % 150 / 100), twenty
// tails, in his white light.
constexpr Recipe kSetRibbon{1,     40.0f, 10.0f, 100.0f, 0.0f, 1.5f, 19.0f, 0.0f,
                            false, false, false, {1.0f, 1.0f, 1.0f}};
// Invention, the level-up's (game/fx/aura.cpp kStrength): Flare added at 1 burned white in this
// engine's light.
constexpr float kStrength = 0.65f;
// Invention: the arms' lights at twice MU's half. MU adds 0.5 into 8-bit colour, where it shows
// as a glow; in this engine's light it vanished on the gold of a lit suit even at night. Four
// times was a yellow blaze; two is judged on the night shot of a +11 Brass set.
constexpr float kArmGain = 2.0f;

}  // namespace

float SetShine::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000);
}

bool SetShine::open(const std::string& assetDir, content::Textures& textures) {
    const std::string path = assetDir + "/effects/levelup/flare.png";
    if (!core::fileExists(path)) {
        core::logError("set shine: no %s (tools/sync.sh)", path.c_str());
        return false;
    }
    flare_ = textures.load(path, content::TextureRole::Albedo);
    const std::string light = assetDir + "/effects/light/flare01.png";
    if (core::fileExists(light)) {
        light_ = textures.load(light, content::TextureRole::Albedo);
    } else {
        core::logError("set shine: no %s; a set's arms do not glow", light.c_str());
    }
    ribbons_.open(assetDir, textures);
    return bgfx::isValid(flare_);
}

void SetShine::shutdown() {
    ribbons_.shutdown();
    flare_ = BGFX_INVALID_HANDLE;
    light_ = BGFX_INVALID_HANDLE;
    lights_ = 0;
    for (Flare& one : flares_) one.alive = false;
}

int SetShine::setPlus(const content::Tables& tables, const sim::Satchel& bag) {
    // CheckFullSet: every piece there, one set, and the lowest plus is the set's.
    int set = -1, lowest = 1 << 30;
    for (int slot = sim::kHelm; slot <= sim::kBoots; ++slot) {
        const sim::Held& worn = bag[slot];
        if (worn.empty() || size_t(worn.item) >= tables.items.size()) return -1;
        const int of = sim::setOf(tables.items[size_t(worn.item)]);
        if (of < 0 || (set >= 0 && of != set)) return -1;
        set = of;
        lowest = std::min<int>(lowest, worn.refinement);
    }
    return lowest;
}

void SetShine::throwFlare(const float feet[3], float per) {
    for (Flare& one : flares_) {
        if (one.alive) continue;
        one.alive = true;
        std::copy(feet, feet + 3, one.start);
        one.per = per;
        one.phase = std::floor(unit() * 300.0f) - 150.0f;
        one.life = kLife;
        one.climb = std::floor(unit() * 100.0f) / 100.0f * 4.0f + 1.0f;
        one.height = 0.0f;
        one.scale = 0.19f + std::floor(unit() * 6.0f) * 0.01f;
        one.spin = unit() * 6.2831853f;
        return;
    }
}

void SetShine::update(float seconds, int plus, const float feet[3], float yaw,
                      float metresPerTile) {
    const float frames = seconds * kReferenceFps;
    // The flares in the air climb and turn on MU's frames whatever the set is now.
    for (Flare& one : flares_) {
        if (!one.alive) continue;
        one.life -= frames;
        one.height += one.climb * frames;
        one.scale -= 0.002f * frames;
        if (one.life <= 0.0f || one.scale <= 0.0f) one.alive = false;
    }
    ribbons_.update(seconds);
    // Carried with him wherever he is drawn: a set's ring is his, not the ground's.
    placed_ = feet[0] != 0.0f || feet[1] != 0.0f || feet[2] != 0.0f;
    if (placed_) {
        for (int k = 0; k < 3; ++k) feet_[k] = feet[k];
        ribbons_.followAll(feet);
    }

    // And new ones, a frame at a time: one frame in twenty, at +10 a flare, at +11 a ribbon one
    // time in eight and a flare the rest. Past +11 is MU's later sets, drawn here as +11.
    if (plus < 10) {
        clock_ = 0.0f;
        return;
    }
    const float per = metresPerTile / 100.0f;
    clock_ += frames;
    while (clock_ >= 1.0f) {
        clock_ -= 1.0f;
        if (unit() >= 1.0f / 20.0f) continue;
        if (plus >= 11 && unit() < 1.0f / 8.0f) {
            ribbons_.cast(kSetRibbon, feet, yaw, metresPerTile);
        } else {
            throwFlare(feet, per);
        }
    }
}

void SetShine::lights(const float (*points)[3], int count, const float colour[3],
                      float metresPerTile) {
    lights_ = std::clamp(count, 0, 6);
    for (int k = 0; k < lights_; ++k) std::copy(points[k], points[k] + 3, lightAt_[k]);
    if (colour) std::copy(colour, colour + 3, lightColour_);
    lightPer_ = metresPerTile / 100.0f;
}

void SetShine::gather(gfx::Effects& effects, const content::Ground& ground,
                      const float eye[3]) const {
    ribbons_.gather(effects, ground, eye);
    // The arms' lights: flare01 at its 64 texels times 1.3, the boots' colour at half, added.
    if (bgfx::isValid(light_)) {
        for (int k = 0; k < lights_; ++k) {
            gfx::Sprite sprite;
            std::copy(lightAt_[k], lightAt_[k] + 3, sprite.position);
            sprite.halfWidth = sprite.halfHeight = kTexels * 1.3f * lightPer_ * 0.5f;
            for (int c = 0; c < 3; ++c) sprite.colour[c] = lightColour_[c] * 0.5f * kArmGain;
            sprite.sheet = light_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    if (!bgfx::isValid(flare_)) return;
    for (const Flare& one : flares_) {
        // Not on its first frame, as MU's (`LifeTime != 60`).
        if (!one.alive || one.life > kLife - 1.0f) continue;
        // count = (Velocity[0] + LifeTime) * 0.1 on MU's ground plane; its x is ours, its y our
        // -z, and its height our y.
        const float count = (one.phase + one.life) * 0.1f;
        gfx::Sprite sprite;
        const float* at = placed_ ? feet_ : one.start;
        sprite.position[0] = at[0] + std::sin(count) * kOrbit * one.per;
        sprite.position[1] = at[1] + one.height * one.per;
        sprite.position[2] = at[2] + std::cos(count) * kOrbit * one.per;
        sprite.halfWidth = sprite.halfHeight = kTexels * one.scale * one.per * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kStrength;
        sprite.sheet = flare_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
