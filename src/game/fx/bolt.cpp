#include "game/fx/bolt.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kTwoPi = 6.28318531f;
constexpr float kRadian = 0.0174532925f;
constexpr float kWhite[3] = {1.0f, 1.0f, 1.0f};

}  // namespace

float Bolt::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Bolt::open(const std::string& assetDir, content::Textures& textures,
                const content::Showing& table) {
    const auto cooked = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("bolt: no cooked effect named '%s'", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    energy_ = cooked("energy");
    spark_ = cooked("spark_flash");
    haze_ = cooked("flare_blue");
    return bgfx::isValid(energy_);
}

void Bolt::cast(const float from[3], const float to[3], uint32_t target) {
    Head* head = nullptr;
    for (Head& one : heads_) {
        if (!one.alive) {
            head = &one;
            break;
        }
    }
    if (head == nullptr) {
        ++refused_;
        return;
    }
    // Level, and aimed only in the horizontal: the direction the client rotates is (0, -60, 0),
    // with no vertical term, so a bolt at a target on a slope flies flat past it rather than
    // climbing -- and CheckTargetRange is a distance on the ground.
    const float dx = to[0] - from[0], dz = to[2] - from[2];
    const float flat = std::sqrt(dx * dx + dz * dz);
    *head = Head{};
    head->alive = true;
    head->at[0] = from[0];
    head->at[1] = from[1] + kChest / kPerMetre;
    head->at[2] = from[2];
    head->along[0] = flat > 1e-4f ? dx / flat : 0.0f;
    head->along[1] = 0.0f;
    head->along[2] = flat > 1e-4f ? dz / flat : 0.0f;
    head->life = kLife;
    head->coreRoll = unit() * kTwoPi;
    head->target = target;
}

bool Bolt::fly(Head& head, float factor, bool standing, const float* there) {
    // MoveEffect's own order: the object is moved, its lifetime counted down, and then it is
    // destroyed OR it lays its particles. A bolt that ran out this frame draws nothing leaving.
    const float went = kSpeed / kPerMetre * factor;
    for (int k = 0; k < 3; ++k) head.at[k] += head.along[k] * went;
    head.life -= factor;
    if (head.life <= 0.0f) return false;
    head.coreRoll += kSpinDegrees * kRadian * factor;

    const float lit = std::min(1.0f, head.life * kWakeTint);

    // The haze first, MU2's and on its own spacing: by distance, so its count does not follow
    // the frame rate.
    head.hazed += went;
    const float hazeStep = kSpeed / kHazeRate / kPerMetre;
    while (head.hazed >= hazeStep) {
        head.hazed -= hazeStep;
        const float tint[3] = {kHaze[0] * kHazeTint * lit, kHaze[1] * kHazeTint * lit,
                               kHaze[2] * kHazeTint * lit};
        const float rise[3] = {0.0f, kHazeRises / kPerMetre, 0.0f};
        lay(haze_, kHazeWidth, head.at, tint, kHazeScale, unit() * kTwoPi, 0.0f, kHazeOpens, rise,
            kHazeFrames, true);
    }

    // And the wake, one every sixty units, which is the client's one a reference frame: at MU's
    // own 25 fps `rand_fps_check(1)` is always true, so the even spacing IS the client's, and a
    // coin at a higher frame rate leaves stretches with nothing alive at all (MU2 saw the ball
    // stutter along its path).
    head.flown += went;
    const float wakeStep = kWakeSpacing / kPerMetre;
    while (head.flown >= wakeStep) {
        head.flown -= wakeStep;
        const float colour[3] = {lit, lit, lit};
        // The star, at its own size and angle, turning.
        lay(energy_, kEnergyWidth, head.at, colour,
            kWakeScale * (kSmallest + unit() * (kLargest - kSmallest)), unit() * kTwoPi,
            kSpinDegrees * kRadian);
        // And the halo round it, four times the sheet, which is most of what is seen.
        lay(spark_, kSparkWidth, head.at, colour, kSparkScale, 0.0f, 0.0f, kSparkShrink);
    }

    // **Arriving does not end the bolt; it pins it.** CheckTargetRange sets LifeTime to 1 and
    // nothing else, so inside a tile of the target the bolt keeps going -- through it, laying its
    // wake and a contact spark each frame -- and dies once past, when the one runs out. Only on
    // something still standing (`to->Live`): a bolt whose target died in the air flies on and
    // expires of its own accord, with no flash.
    if (!standing || there == nullptr) return true;
    const float lx = there[0] - head.at[0], lz = there[2] - head.at[2];
    const float reach = kArrives / kPerMetre;
    if (lx * lx + lz * lz > reach * reach) return true;
    head.life = 1.0f;
    // Carried on along the bolt's own heading, `o->Angle`: MU has no impact burst, only the
    // spark it keeps laying while the range test lets it live. A fresh random direction was
    // MU2's first reading and fanned sparkles out to all sides.
    if (unit() <= std::min(1.0f, factor)) {
        const float velocity[3] = {head.along[0] * kFlashSpeed / kPerMetre, 0.0f,
                                   head.along[2] * kFlashSpeed / kPerMetre};
        lay(spark_, kSparkWidth, head.at, kWhite, kFlashScale, 0.0f, 0.0f, kFlashShrink, velocity);
    }
    return true;
}

void Bolt::lay(bgfx::TextureHandle sheet, float width, const float at[3], const float colour[3],
               float scale, float roll, float spin, float shrink, const float* velocity,
               float life, bool dims) {
    if (!bgfx::isValid(sheet)) return;
    for (Mote& one : motes_) {
        if (one.alive) continue;
        one.alive = true;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = at[k];
            one.velocity[k] = velocity ? velocity[k] : 0.0f;
            one.colour[k] = colour[k];
        }
        one.scale = scale;
        one.width = width;
        one.shrink = shrink;
        one.roll = roll;
        one.spin = spin;
        one.life = life;
        one.full = life;
        one.dims = dims;
        one.sheet = sheet;
        return;
    }
    ++refused_;
}

void Bolt::step(float factor) {
    // MoveParticles' order: the lifetime first, so a two-frame spark is cut off bright before
    // its shrink could reach the 0.2 the client kills it at.
    for (Mote& one : motes_) {
        if (!one.alive) continue;
        one.life -= factor;
        if (one.life <= 0.0f) {
            one.alive = false;
            continue;
        }
        one.scale -= one.shrink * factor;
        if (one.scale < kGone) {
            one.alive = false;
            continue;
        }
        for (int k = 0; k < 3; ++k) one.at[k] += one.velocity[k] * factor;
        one.roll += one.spin * factor;
    }
}

void Bolt::gather(gfx::Effects& effects) const {
    const auto put = [&](bgfx::TextureHandle sheet, const float at[3], float size, float roll,
                         const float colour[3], float fade) {
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = at[k];
        sprite.halfWidth = sprite.halfHeight = size * 0.5f;
        sprite.spin = roll;
        // RenderParticles gives a three-channel bitmap GL_ONE, GL_ONE: the black in the sheet
        // does the masking, so the fade goes on the colour and not on an alpha nobody reads.
        for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k] * fade;
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };
    for (const Mote& one : motes_) {
        if (!one.alive) continue;
        const float fade = one.dims && one.full > 0.0f ? one.life / one.full : 1.0f;
        put(one.sheet, one.at, one.width * one.scale, one.roll, one.colour, fade);
    }
    // The core, MU2's: the same star at the bolt itself, on the bolt's own brightness.
    if (!bgfx::isValid(energy_)) return;
    for (const Head& head : heads_) {
        if (!head.alive) continue;
        const float lit = std::min(1.0f, head.life * kWakeTint);
        put(energy_, head.at, kEnergyWidth * kCoreScale, head.coreRoll, kWhite, lit);
    }
}

uint32_t Bolt::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Head& head : heads_) {
        if (!head.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = head.at[k];
        light.reach = kGlowTiles;
        light.height = kChest / kPerMetre;
        // The client's light is a colour times the bolt's age -- `Luminosity = LifeTime * 0.2`,
        // four at birth -- and dropping the multiplier made MU2's first one invisible. Clamped
        // at one here, as the wake is, because this renderer's point light is not GL_ONE's.
        const float lum = std::min(1.0f, head.life * kGlowTint);
        for (int k = 0; k < 3; ++k) light.colour[k] = kGlow[k] * lum;
    }
    return count;
}

uint32_t Bolt::flying() const {
    uint32_t count = 0;
    for (const Head& head : heads_) count += head.alive ? 1u : 0u;
    return count;
}

}  // namespace mu::game
