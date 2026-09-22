#include "game/breath.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

// MU's 25, which is what a reference frame is.
constexpr float kReferenceFps = 25.0f;
// A tile is a hundred of MU's units and one metre here.
constexpr float kUnit = 0.01f;

// Room for what the client could ever have in the air: a spark a reference frame over the
// attack's first four keys and a 24-frame life, for a handful of dragons biting at once; and a
// puff every fourth frame over a 32-frame life, eight a dragon, for the nest's dozen.
constexpr size_t kSparks = 256;
constexpr size_t kPuffs = 256;

// BITMAP_FIRE subtype 1 (ZzzEffectParticle.cpp): LifeTime 24, Scale (rand()%4 + 10) * 0.01,
// Velocity (0, -(rand()%16 + 32) * 0.1, 0) turned by the object's angle, then in the default
// motion Velocity *= 0.98, Gravity += 0.004, Scale += Gravity, Position[2] += Gravity * 10.
// Fire01 is four 64-pixel frames in a 256 strip, played once: Frame = (23 - LifeTime) / 6.
constexpr float kSparkLife = 24.0f;
constexpr int kFireFrames = 4;
// Drawn through fs_flame, as the town's fires are, and not in the sheet's own colour: Fire01 is
// dim red paint, and added as it stands into this HDR frame the breath was a faint orange haze
// the tonemap all but lost. The flame program reads the sheet as a shape and colours it by heat,
// which is the town's marked invention (docs/sprints/08b) applied to the same sheet. The
// gains are the lamps' (lamps.cpp kCellGain): the four cells are painted 1x, 9x, 19x and 19x
// darker than the first.
constexpr float kCellGain[kFireFrames] = {1.0f, 8.8f, 19.0f, 19.0f};
// How hot a spark is born, and how much of that it has lost by its last frame. MU's Light is
// flat over the life; the cooling is what reads the growing spark as flame catching and going
// out, rather than a lit ball expanding.
constexpr float kSparkHeat = 0.9f, kSparkCooling = 0.7f;

// BITMAP_SMOKE + 1 subtype 0: LifeTime 32, Scale (rand()%32 + 32) * 0.01, Velocity (0, 3, 0)
// turned by the angle with Velocity *= 0.9, Scale += 0.08 a frame, Light = LifeTime / 32 on
// every channel, and its height pinned each frame to the terrain plus half its own height.
constexpr float kPuffLife = 32.0f;

}  // namespace

uint32_t Breath::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

bool Breath::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("breath: no cooked effect named '%s'", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    fire_ = take("fire");
    smoke_ = take("smoke");
    sparks_.reserve(kSparks);
    puffs_.reserve(kPuffs);
    open_ = bgfx::isValid(fire_) || bgfx::isValid(smoke_);
    return open_;
}

void Breath::shutdown() {
    sparks_.clear();
    puffs_.clear();
    open_ = false;
}

void Breath::spark(const float at[3], const float along[2], float scale) {
    if (!open_ || !bgfx::isValid(fire_)) return;
    if (sparks_.size() >= kSparks) {
        ++refused_;
        return;
    }
    Spark one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    const float speed = float(roll() % 16 + 32) * 0.1f * kUnit * scale;
    one.velocity[0] = along[0] * speed;
    one.velocity[1] = along[1] * speed;
    one.scale = float(roll() % 4 + 10) * 0.01f;
    one.size = scale;
    sparks_.push_back(one);
}

void Breath::puff(const float feet[3], const float along[2], float scale) {
    if (!open_ || !bgfx::isValid(smoke_)) return;
    if (puffs_.size() >= kPuffs) {
        ++refused_;
        return;
    }
    Puff one;
    // Within 32 units either way of the body, as the case scatters it.
    one.position[0] = feet[0] + float(int(roll() % 64) - 32) * kUnit * scale;
    one.position[1] = feet[1];
    one.position[2] = feet[2] + float(int(roll() % 64) - 32) * kUnit * scale;
    const float speed = 3.0f * kUnit * scale;
    one.velocity[0] = along[0] * speed;
    one.velocity[1] = along[1] * speed;
    one.scale = float(roll() % 32 + 32) * 0.01f;
    one.size = scale;
    puffs_.push_back(one);
}

void Breath::update(float seconds) {
    // MU's motion is stated per reference frame; a drawn frame is this many of them.
    const float frames = seconds * kReferenceFps;
    for (Spark& one : sparks_) {
        one.life -= frames;
        one.position[0] += one.velocity[0] * frames;
        one.position[2] += one.velocity[1] * frames;
        const float drag = std::pow(0.98f, frames);
        one.velocity[0] *= drag;
        one.velocity[1] *= drag;
        // Gravity gains a constant and Scale gains Gravity: the second integral, which is why
        // a spark catches slowly and then flares. Taken a frame's worth at a time.
        one.gravity += 0.004f * frames;
        one.scale += one.gravity * frames;
        one.position[1] += one.gravity * 10.0f * kUnit * one.size * frames;
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(),
                                 [](const Spark& one) { return one.life <= 0.0f; }),
                  sparks_.end());
    for (Puff& one : puffs_) {
        one.life -= frames;
        one.position[0] += one.velocity[0] * frames;
        one.position[2] += one.velocity[1] * frames;
        const float drag = std::pow(0.9f, frames);
        one.velocity[0] *= drag;
        one.velocity[1] *= drag;
        one.scale += 0.08f * frames;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& one) { return one.life <= 0.0f; }),
                 puffs_.end());
}

void Breath::gather(gfx::Effects& effects) const {
    for (const Spark& one : sparks_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        // A frame of the strip is 64 units wide at Scale 1.
        const float half = 64.0f * one.scale * kUnit * one.size * 0.5f;
        sprite.halfWidth = sprite.halfHeight = half;
        const int frame = std::clamp(int((23.0f - one.life) / 6.0f), 0, kFireFrames - 1);
        sprite.u0 = float(frame) / float(kFireFrames);
        sprite.u1 = float(frame + 1) / float(kFireFrames);
        // fs_flame's vertex colour: r the cell's gain, g the heat, a the fade. MU never fades
        // this subtype -- Luminosity is computed and not written into its Light -- so the
        // alpha stays whole and the strip's thin last cell is what ends it.
        const float t = 1.0f - std::max(0.0f, one.life) / kSparkLife;
        sprite.colour[0] = kCellGain[frame] / 20.0f;
        sprite.colour[1] = kSparkHeat * (1.0f - kSparkCooling * t);
        sprite.colour[2] = 0.0f;
        sprite.colour[3] = 1.0f;
        sprite.sheet = fire_;
        sprite.blend = gfx::Blend::Flame;
        effects.add(sprite);
    }
    for (const Puff& one : puffs_) {
        gfx::Sprite sprite;
        const float half = 64.0f * one.scale * kUnit * one.size * 0.5f;
        sprite.halfWidth = sprite.halfHeight = half;
        // The ground pin: RequestTerrainHeight + Height * Scale * 0.5, straight up in the world,
        // so the puff sits on the floor and the billboard faces the camera from there.
        const float floor =
            ground_ ? ground_->heightAt(one.position[0], one.position[2]) : one.position[1];
        sprite.position[0] = one.position[0];
        sprite.position[1] = floor + half;
        sprite.position[2] = one.position[2];
        const float light = std::max(0.0f, one.life / kPuffLife);
        sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = light;
        sprite.colour[3] = light;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Dust;
        effects.add(sprite);
    }
}

}  // namespace mu::game
