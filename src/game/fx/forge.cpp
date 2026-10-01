#include "game/fx/forge.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

// MU's 25, which is what a reference frame is.
constexpr float kReferenceFps = 25.0f;
// A tile is a hundred of MU's units and one metre here.
constexpr float kUnit = 0.01f;
constexpr float kDegrees = 3.14159265f / 180.0f;

// Room for what one smith could ever have in the air: four of each a reference frame over the
// five frames keys 5 to 6 take, a streak living at most 15 and a mote at most 39, and his
// next blow 44 frames on. A few dozen of each; a second smith would still fit.
constexpr size_t kStreaks = 128;
constexpr size_t kMotes = 256;
constexpr size_t kPuffs = 64;

// BITMAP_JOINT_SPARK subtype 0 (ZzzEffectJoint.cpp): Scale 2, Velocity rand()%20 + 6,
// LifeTime rand()%8 + 8, MaxTails 2, Light white and never changed. MoveJoint moves it
// (0, -Velocity, 0) turned by its Angle every frame and nothing else: no gravity, no drag.
// MaxTails 2 is one segment, from where it was a frame ago to where it is, so a streak is as
// long as a reference frame's travel -- kept as that DURATION, as fx/streak.h keeps the
// blade's, so the line is not a seventh of its length at 180 fps.
constexpr float kStreakWidth = 2.0f;  // units, the cross's full width
// fs_flame's heat for a streak. MU's Light is 1 throughout; this is the heat that reads as
// the sheet's own orange-yellow rather than as white, since heat 1 is the white core.
constexpr float kStreakHeat = 0.8f;

// BITMAP_SPARK subtype 0 (ZzzEffectParticle.cpp): Scale (rand()%4 + 4) * 0.1, LifeTime
// rand()%16 + 24, Angle[2] re-rolled to rand()%360, Gravity rand()%16 + 6, Velocity
// (0, (rand()%20 + 20) * 0.1, 0) turned by the angle. Every frame: Light = LifeTime / 16,
// Position.z += Gravity, Gravity -= 2, and under the ground it is put back on it with
// Gravity = -Gravity * 0.6 and four frames off its life. Spark02 is four pixels, and a
// sprite is drawn at the sheet's width times its Scale: a mote is two centimetres across.
constexpr float kMotePixels = 4.0f;
constexpr float kMoteBright = 16.0f;  // the LifeTime its Light is 1 at

// **Less of it than MU, the user's call (2026-09-24): "sparkles is too crazy".** MU's own
// count and reach, seen at this frame rate and bloom, read as fireworks: four streaks a
// reference frame flying up to four metres over the weapon rack, and four motes born
// white-hot. So a burst is one streak and two motes of MU's four and four, a streak flies at
// 0.6 of MU's speed for 0.6 of its life -- a third of the reach, a hand's length to a metre --
// and a mote is never hotter than 0.75, which is orange rather than a white that blooms. The
// window, the bone, the directions, the hop and the bounce are MU's untouched.
constexpr int kStreaksPerBurst = 1;
constexpr int kMotesPerBurst = 2;
constexpr float kStreakPace = 0.6f;
constexpr float kMoteHeat = 0.75f;

// fs_flame reads the sheet's red times r * 20 as the shape. The two spark sheets are small
// and bilinear spreads their one bright texel thin, so both are read at twice their red and
// the saturate() in the shader keeps the core whole.
constexpr float kSparkShape = 2.0f / 20.0f;

// ---- ours from here down: the hearth ------------------------------------------------------
//
// A thin smoke off the coals, a puff about every 0.7 s, rising slowly and opening as it
// goes; and an ember lifted off them about once a second. The lamps' bonfire gives 2.6 puffs
// a second, 0.5 to 0.8 m across, at 0.85 alpha; a forge is a covered bed of coals and not a
// log fire, so this is half the rate, half the size and two-thirds the alpha of that.
constexpr float kSmokePerSecond = 1.4f;
constexpr float kEmbersPerSecond = 0.9f;
constexpr float kSmokeAlpha = 0.55f;

float mix(float a, float b, float t) { return a + (b - a) * t; }
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// MU's axes to ours: its z is up and its y is a row, which runs down our -z.
void fromMu(float x, float y, float z, float out[3]) {
    out[0] = x;
    out[1] = z;
    out[2] = -y;
}

}  // namespace

uint32_t Forge::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

float Forge::unit() { return float(roll() & 0xFFFFFF) / float(0x1000000); }

bool Forge::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table, const content::Ground* ground) {
    shutdown();
    ground_ = ground;
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("forge: no cooked effect named '%s'", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    streak_ = take("joint_spark");  // Effect/Spark01, BITMAP_JOINT_SPARK
    mote_ = take("spark");          // Effect/Spark02, BITMAP_SPARK
    ember_ = take("light");         // Effect/flare01, the lamps' ember
    smoke_ = take("smoke");         // Effect/smoke02, the lamps' smoke
    streaks_.reserve(kStreaks);
    motes_.reserve(kMotes);
    puffs_.reserve(kPuffs);
    open_ = true;
    return true;
}

void Forge::shutdown() {
    streaks_.clear();
    motes_.clear();
    puffs_.clear();
    streak_ = mote_ = ember_ = smoke_ = BGFX_INVALID_HANDLE;
    open_ = false;
}

void Forge::strike(const float at[3]) {
    if (!open_) return;
    // Vector(rand() % 60 + 60 + 90, 0, rand() % 30, Angle).
    for (int i = 0; i < 4; ++i) fling(at, 150.0f, 0.0f, i < kStreaksPerBurst, i < kMotesPerBurst);
}

void Forge::fling(const float at[3], float rollFrom, float yawFrom, bool streak, bool mote) {
    if (!open_) return;
    {
        // Angle[0] is AngleMatrix's roll, about MU's x, and Angle[2] its yaw.
        const float r = (float(roll() % 60) + rollFrom) * kDegrees;
        const float sr = std::sin(r), cr = std::cos(r);

        if (streak && bgfx::isValid(streak_) && streaks_.size() < kStreaks) {
            // (0, -Velocity, 0) through AngleMatrix: (cr sy, -cr cy, -sr) * Velocity.
            const float y = (float(roll() % 30) + yawFrom) * kDegrees;
            const float v = float(roll() % 20 + 6) * kUnit * kStreakPace;
            Streak one;
            for (int k = 0; k < 3; ++k) one.position[k] = at[k];
            fromMu(cr * std::sin(y) * v, -cr * std::cos(y) * v, -sr * v, one.velocity);
            one.life = float(roll() % 8 + 8) * kStreakPace;
            streaks_.push_back(one);
        } else if (streak && bgfx::isValid(streak_)) {
            ++refused_;
        }

        if (!mote) return;
        if (bgfx::isValid(mote_) && motes_.size() < kMotes) {
            // Subtype 0 throws the yaw away: Angle[2] = rand() % 360. Then (0, v, 0) through
            // AngleMatrix: (-cr sy, cr cy, sr) * v -- mostly outward, and up to half of it up
            // or down, which MU adds every frame on top of the hop, bounce or no bounce.
            const float y = float(roll() % 360) * kDegrees;
            const float v = float(roll() % 20 + 20) * 0.1f * kUnit;
            Mote one;
            for (int k = 0; k < 3; ++k) one.position[k] = at[k];
            fromMu(-cr * std::sin(y) * v, cr * std::cos(y) * v, sr * v, one.velocity);
            one.gravity = float(roll() % 16 + 6);
            one.scale = float(roll() % 4 + 4) * 0.1f;
            one.life = float(roll() % 16 + 24);
            motes_.push_back(one);
        } else if (bgfx::isValid(mote_)) {
            ++refused_;
        }
    }
}

void Forge::puff(const float at[3], uint8_t kind) {
    if (puffs_.size() >= kPuffs) {
        ++refused_;
        return;
    }
    Puff one;
    one.kind = kind;
    one.phase = 6.2831853f * unit();
    const float angle = 6.2831853f * unit();
    const float radius = 0.12f * std::sqrt(unit());
    one.position[0] = at[0] + std::cos(angle) * radius;
    one.position[1] = at[1];
    one.position[2] = at[2] + std::sin(angle) * radius;
    if (kind == kSmoke) {
        one.life = mix(2.4f, 3.4f, unit());
        one.size = mix(0.28f, 0.45f, unit());
        one.position[1] += 0.15f;
        one.spin = 6.2831853f * unit();
        one.spinRate = (unit() - 0.5f) * 0.5f;
        one.velocity[0] = (unit() - 0.5f) * 0.12f;
        one.velocity[1] = mix(0.35f, 0.55f, unit());
        one.velocity[2] = (unit() - 0.5f) * 0.12f;
    } else {
        one.life = mix(0.9f, 1.8f, unit());
        one.size = mix(0.05f, 0.09f, unit());
        one.velocity[0] = (unit() - 0.5f) * 0.7f;
        one.velocity[1] = mix(0.6f, 1.3f, unit());
        one.velocity[2] = (unit() - 0.5f) * 0.7f;
    }
    puffs_.push_back(one);
}

void Forge::smoulder(const float at[3], float seconds, float owed[2]) {
    if (!open_) return;
    const float dt = std::min(seconds, 0.1f);
    owed[0] += dt * kSmokePerSecond;
    while (owed[0] >= 1.0f) {
        owed[0] -= 1.0f;
        if (bgfx::isValid(smoke_)) puff(at, kSmoke);
    }
    owed[1] += dt * kEmbersPerSecond;
    while (owed[1] >= 1.0f) {
        owed[1] -= 1.0f;
        // Not on the clock: embers come in no rhythm.
        if (bgfx::isValid(ember_) && unit() < 0.7f) puff(at, kEmber);
    }
}

void Forge::update(float seconds) {
    // MU's motion is stated per reference frame; a drawn frame is this many of them. A long
    // hitch is capped, as the lamps cap theirs: a spark need not be right about a frame
    // nobody saw.
    const float frames = std::min(seconds, 0.1f) * kReferenceFps;
    for (Streak& one : streaks_) {
        one.life -= frames;
        for (int k = 0; k < 3; ++k) one.position[k] += one.velocity[k] * frames;
    }
    streaks_.erase(std::remove_if(streaks_.begin(), streaks_.end(),
                                  [](const Streak& one) { return one.life <= 0.0f; }),
                   streaks_.end());

    for (Mote& one : motes_) {
        one.life -= frames;
        one.position[1] += one.gravity * kUnit * frames;
        one.gravity -= 2.0f * frames;
        const float floor =
            ground_ ? ground_->heightAt(one.position[0], one.position[2]) : 0.0f;
        // Only while falling: MU's test has no need of that at one step a frame, and at
        // seven a frame a mote just flipped could be flipped back before it cleared the floor.
        if (one.position[1] < floor && one.gravity < 0.0f) {
            one.position[1] = floor;
            one.gravity = -one.gravity * 0.6f;
            one.life -= 4.0f;
        }
        for (int k = 0; k < 3; ++k) one.position[k] += one.velocity[k] * frames;
    }
    motes_.erase(std::remove_if(motes_.begin(), motes_.end(),
                                [](const Mote& one) { return one.life <= 0.0f; }),
                 motes_.end());

    const float dt = std::min(seconds, 0.1f);
    for (Puff& one : puffs_) {
        one.age += dt;
        if (one.age >= one.life) continue;
        const float t = one.age / one.life;
        // The lamps' sway: two sines of the particle's own, so no two beat together.
        const float swayX = std::sin(one.phase + one.age * 7.0f) +
                            0.5f * std::sin(one.phase * 2.3f + one.age * 13.0f);
        const float swayZ = std::cos(one.phase * 1.7f + one.age * 6.0f);
        if (one.kind == kEmber) {
            one.velocity[1] += (0.6f - 1.4f * t) * dt;
            one.velocity[0] += swayX * 2.2f * dt;
            one.velocity[2] += swayZ * 2.2f * dt;
        } else {
            one.velocity[0] += swayX * 0.12f * dt;
            one.velocity[2] += swayZ * 0.12f * dt;
            one.velocity[1] *= std::pow(0.8f, dt);
        }
        for (int k = 0; k < 3; ++k) one.position[k] += one.velocity[k] * dt;
        one.spin += one.spinRate * dt;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& one) { return one.age >= one.life; }),
                 puffs_.end());
}

void Forge::gather(gfx::Effects& effects, const float eye[3], const float near[3],
                   float daylight) const {
    // The lamps' reach: a forge further off than their fires is not drawn either.
    constexpr float kReach2 = 45.0f * 45.0f;
    const auto far = [&](const float at[3]) {
        const float dx = at[0] - near[0], dz = at[2] - near[2];
        return dx * dx + dz * dz > kReach2;
    };

    for (const Streak& one : streaks_) {
        if (far(one.position)) continue;
        // The segment it drew over its last reference frame, and a ribbon along it turned to
        // face the eye: MU's cross of two quads, of which the camera only ever sees one.
        float tail[3], along[3], mid[3], look[3];
        for (int k = 0; k < 3; ++k) {
            tail[k] = one.position[k] - one.velocity[k];
            along[k] = one.velocity[k];
            mid[k] = 0.5f * (tail[k] + one.position[k]);
            look[k] = eye[k] - mid[k];
        }
        float side[3] = {along[1] * look[2] - along[2] * look[1],
                         along[2] * look[0] - along[0] * look[2],
                         along[0] * look[1] - along[1] * look[0]};
        const float length = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (length < 1e-6f) continue;
        const float half = kStreakWidth * kUnit * 0.5f / length;
        for (float& s : side) s *= half;
        gfx::Sprite sprite;
        sprite.placed = true;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = mid[k];
            sprite.corner[0][k] = tail[k] - side[k];
            sprite.corner[1][k] = one.position[k] - side[k];
            sprite.corner[2][k] = one.position[k] + side[k];
            sprite.corner[3][k] = tail[k] + side[k];
        }
        // Spark01 is eight pixels long and four across: its length runs along u.
        const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int c = 0; c < 4; ++c) {
            sprite.cornerUv[c][0] = uv[c][0];
            sprite.cornerUv[c][1] = uv[c][1];
        }
        sprite.colour[0] = kSparkShape;
        sprite.colour[1] = kStreakHeat;
        sprite.colour[2] = 0.0f;
        sprite.colour[3] = 1.0f;
        sprite.sheet = streak_;
        sprite.blend = gfx::Blend::Flame;
        if (!effects.add(sprite)) return;
    }

    for (const Mote& one : motes_) {
        if (far(one.position)) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.position[k];
        sprite.halfWidth = sprite.halfHeight = kMotePixels * one.scale * kUnit * 0.5f;
        // MU's Light = LifeTime / 16, which glColor clamps at 1: whole for its first half,
        // then dimming to nothing. Read as heat, it is white-hot and then cooling.
        const float light = std::clamp(one.life / kMoteBright, 0.0f, 1.0f);
        sprite.colour[0] = kSparkShape;
        sprite.colour[1] = light * kMoteHeat;
        sprite.colour[2] = 0.0f;
        sprite.colour[3] = std::min(1.0f, light * 2.0f);
        sprite.sheet = mote_;
        sprite.blend = gfx::Blend::Flame;
        if (!effects.add(sprite)) return;
    }

    for (const Puff& one : puffs_) {
        if (far(one.position)) continue;
        const float t = one.age / one.life;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.position[k];
        sprite.spin = one.spin;
        if (one.kind == kEmber) {
            sprite.halfWidth = sprite.halfHeight = 0.5f * one.size * (1.0f - 0.5f * t);
            // The lamps' ember exactly: flare01's red as the shape, cooling yellow to red.
            sprite.colour[0] = 1.0f / 20.0f * 5.0f;
            sprite.colour[1] = 0.9f * (1.0f - 0.75f * t);
            sprite.colour[2] = 0.0f;
            const float twinkle = 0.65f + 0.35f * std::sin(one.phase + one.age * 23.0f);
            sprite.colour[3] = twinkle * (1.0f - smooth(0.6f, 1.0f, t));
            sprite.sheet = ember_;
            sprite.blend = gfx::Blend::Flame;
        } else {
            const float size = one.size * (1.0f + 1.8f * t);
            sprite.halfWidth = sprite.halfHeight = 0.5f * size;
            // The lamps' smoke: grey by the day's light, and warm from the coals under it
            // while it is young and low.
            const float warm = 1.0f - t;
            const float fire = warm * warm * 0.35f;
            sprite.colour[0] = 0.30f * daylight + fire;
            sprite.colour[1] = 0.29f * daylight + fire * 0.45f;
            sprite.colour[2] = 0.30f * daylight + fire * 0.12f;
            sprite.colour[3] =
                kSmokeAlpha * smooth(0.0f, 0.15f, t) * (1.0f - smooth(0.35f, 1.0f, t));
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Smoke;
        }
        if (!effects.add(sprite)) return;
    }
}

}  // namespace mu::game
