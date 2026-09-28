#include "game/fx/bolt.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kTwoPi = 6.28318531f;
constexpr float kRadian = 0.0174532925f;
constexpr float kWhite[3] = {1.0f, 1.0f, 1.0f};

float length3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

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
    energy_ = cooked("energy");        // Thunder01, MU's BITMAP_ENERGY
    spark_ = cooked("spark_flash");    // the sword strike's contact, BITMAP_SPARK + 1
    smoke_ = cooked("smoke01");        // MU's smoke01, for the tail and the puff
    star_ = cooked("lobby_spark");     // a white-violet starburst, the impact's flash
    bloom_ = cooked("light");          // a soft white flare, the impact's bloom
    return bgfx::isValid(energy_);
}

void Bolt::cast(const float from[3], const float to[3], uint32_t target) {
    Head* head = nullptr;
    for (Head& one : heads_) {
        if (!one.used) {
            head = &one;
            break;
        }
    }
    if (head == nullptr) {
        ++refused_;
        return;
    }
    // Aimed at the middle of the body, height and all -- ours; MU's (0, -60, 0) has no vertical
    // term and flies level at his chest. See the head of the file.
    *head = Head{};
    head->flying = true;
    head->used = true;
    head->at[0] = from[0];
    head->at[1] = from[1] + kChest / kPerMetre;
    head->at[2] = from[2];
    float way[3] = {to[0] - head->at[0], to[1] - head->at[1], to[2] - head->at[2]};
    const float far = length3(way);
    if (far > 1e-4f) {
        for (int k = 0; k < 3; ++k) head->along[k] = way[k] / far;
    } else {
        head->along[0] = 1.0f;
    }
    head->life = kLife;
    head->coreRoll = unit() * kTwoPi;
    head->target = target;
    for (int k = 0; k < 3; ++k) head->aim[k] = to[k];
}

bool Bolt::fly(Head& head, float factor, bool standing, const float* there) {
    // MoveEffect's own order: the object is moved, its lifetime counted down, and then it is
    // destroyed OR it lays its particles.
    const float went = kSpeed / kPerMetre * factor;
    // Steered after the body as it moves: ours, see the head of the file. Not once it is past.
    const bool chasing = standing && there != nullptr && !head.passed;
    if (chasing) {
        float want[3] = {there[0] - head.at[0], there[1] - head.at[1], there[2] - head.at[2]};
        const float far = length3(want);
        if (far > 1e-4f) {
            const float share = std::min(1.0f, kSteer * factor);
            for (int k = 0; k < 3; ++k) head.along[k] += (want[k] / far - head.along[k]) * share;
            const float norm = std::max(1e-4f, length3(head.along));
            for (int k = 0; k < 3; ++k) head.along[k] /= norm;
        }
    }
    for (int k = 0; k < 3; ++k) head.at[k] += head.along[k] * went;
    head.life -= factor;
    if (head.life <= 0.0f) return false;
    head.coreRoll += kSpinDegrees * kRadian * factor;

    // Arrived: within reach of the body's middle, or past it this frame. The impact is thrown ON
    // the body and the bolt ends there -- ours; MU flies the head on through. Only on something
    // still standing: a bolt whose target died in the air flies on past and goes out of its own
    // accord (`CheckTargetRange` asks `to->Live` first). And one the realm said missed flies by.
    if (chasing) {
        const float left[3] = {there[0] - head.at[0], there[1] - head.at[1],
                               there[2] - head.at[2]};
        const float ahead = left[0] * head.along[0] + left[1] * head.along[1] +
                            left[2] * head.along[2];
        if (length3(left) <= kStrikes || ahead <= 0.0f) {
            if (head.missing) {
                head.passed = true;
                head.life = std::min(head.life, kPastFrames);
            } else {
                for (int k = 0; k < 3; ++k) head.at[k] = there[k];
                strike(head.at, head.along);
                return false;
            }
        }
    }

    const float lit = std::min(1.0f, head.life * kWakeTint);

    // The smoke, ours: a puff every ten centimetres, laid a touch behind the head so the ball
    // stays clean, born small and thin, opening, lifting a little and thinning away. Mixed and
    // not added, so it is a wisp in the air and not more light.
    head.smoked += went;
    while (head.smoked >= kSmokeSpacing) {
        head.smoked -= kSmokeSpacing;
        float at[3], velocity[3];
        for (int k = 0; k < 3; ++k) {
            at[k] = head.at[k] - head.along[k] * (0.12f + head.smoked) + (unit() - 0.5f) * 0.05f;
            velocity[k] = (unit() - 0.5f) * kSmokeWander;
        }
        velocity[1] += kSmokeRises;
        puff(at, velocity, kSmokeBorn * (0.85f + unit() * 0.3f),
             kSmokeFrames + unit() * kSmokeMore, kSmokeAlpha);
    }

    // The wake, one every sixty units, which is the client's one a reference frame. FADED over
    // its two frames, which is ours: cut off bright, two stamps a metre apart are two balls.
    head.flown += went;
    const float wakeStep = kWakeSpacing / kPerMetre;
    while (head.flown >= wakeStep) {
        head.flown -= wakeStep;
        const float colour[3] = {lit * kWakeDim, lit * kWakeDim, lit * kWakeDim};
        // MU's is 0.6 to 1.3 of the sheet; smaller here, so the stamps are the tail of the ball
        // and not a second one.
        lay(energy_, kEnergyWidth, head.at, colour,
            (kSmallest + unit() * (kLargest - kSmallest)) * (kWakeSmall + unit() * (kWakeLarge - kWakeSmall)),
            unit() * kTwoPi, kSpinDegrees * kRadian, 0.0f, nullptr, kWakeFrames, true);
    }
    return true;
}

void Bolt::miss(uint32_t target) {
    // The one nearest arriving: bolts at one body are let go in order and land in order, so the
    // first still in the air is the one this blow belongs to.
    Head* first = nullptr;
    for (Head& head : heads_) {
        if (!head.flying || head.target != target || head.missing) continue;
        if (first == nullptr || head.life < first->life) first = &head;
    }
    if (first != nullptr) first->missing = true;
}

void Bolt::puff(const float at[3], const float velocity[3], float scale, float life,
                float alpha) {
    lay(smoke_, kSmokeWidth, at, kSmokeTint, scale, unit() * kTwoPi, (unit() - 0.5f) * 0.08f,
        -kSmokeGrows, velocity, life, false, 0.92f, true, alpha);
}

void Bolt::strike(const float at[3], const float along[3]) {
    Burst* burst = nullptr;
    for (Burst& one : bursts_) {
        if (!one.alive) {
            burst = &one;
            break;
        }
    }
    if (burst != nullptr) {
        *burst = Burst{};
        burst->alive = true;
        for (int k = 0; k < 3; ++k) burst->at[k] = at[k];
        burst->roll = unit() * kTwoPi;
    } else {
        ++refused_;
    }
    // The puff: the trail's own smoke thrown out all round from the body -- flatter than high,
    // a little along the heading -- and slowed hard, so it blooms and hangs where it struck.
    // Bigger and thicker than a trail puff, with one at the middle to fill the hole.
    for (int i = 0; i < kPuffs; ++i) {
        const float yaw = unit() * kTwoPi, pitch = (unit() - 0.35f) * 0.9f;
        const float speed = (kPuffSlow + unit() * (kPuffFast - kPuffSlow)) / kReference;
        float velocity[3] = {std::cos(yaw) * std::cos(pitch) + along[0] * 0.4f, std::sin(pitch),
                             std::sin(yaw) * std::cos(pitch) + along[2] * 0.4f};
        const float norm = std::max(1e-4f, length3(velocity));
        for (int k = 0; k < 3; ++k) velocity[k] *= speed / norm;
        puff(at, velocity, 1.0f + unit() * 0.6f, 14.0f + unit() * 8.0f, kPuffAlpha);
    }
    const float still[3] = {0.0f, kSmokeRises, 0.0f};
    puff(at, still, 1.1f, 20.0f, kPuffAlpha);
    // And MU's own contact spark, laid along the heading, as `CheckTargetRange`'s arm lays it.
    const float velocity[3] = {along[0] * 0.5f, 0.0f, along[2] * 0.5f};
    lay(spark_, kSparkWidth, at, kWhite, 6.0f, 0.0f, 0.0f, 2.0f, velocity, kWakeFrames, true);
}

void Bolt::lay(bgfx::TextureHandle sheet, float width, const float at[3], const float colour[3],
               float scale, float roll, float spin, float shrink, const float* velocity,
               float life, bool dims, float drag, bool smoke, float alpha) {
    if (!bgfx::isValid(sheet)) return;
    for (Mote& one : motes_) {
        if (one.alive) continue;
        one.alive = true;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = at[k];
            one.velocity[k] = velocity ? velocity[k] : 0.0f;
            one.colour[k] = colour[k];
        }
        one.drag = drag;
        one.scale = scale;
        one.width = width;
        one.shrink = shrink;
        one.roll = roll;
        one.spin = spin;
        one.life = life;
        one.full = life;
        one.dims = dims;
        one.smoke = smoke;
        one.alpha = alpha;
        one.sheet = sheet;
        return;
    }
    ++refused_;
}

void Bolt::step(float factor) {
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
        if (one.drag < 1.0f) {
            const float kept = std::pow(one.drag, factor);
            for (int k = 0; k < 3; ++k) one.velocity[k] *= kept;
        }
        one.roll += one.spin * factor;
    }
    for (Burst& one : bursts_) {
        if (!one.alive) continue;
        one.age += factor;
        if (one.age >= kBurstFrames) one.alive = false;
    }
}

void Bolt::gather(gfx::Effects& effects, const float eye[3]) const {
    const auto put = [&](bgfx::TextureHandle sheet, const float at[3], float size, float roll,
                         const float colour[3], float fade) {
        if (!bgfx::isValid(sheet) || fade <= 0.0f) return;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = at[k];
        sprite.halfWidth = sprite.halfHeight = size * 0.5f;
        sprite.spin = roll;
        // A three-channel bitmap is GL_ONE, GL_ONE: the black in the sheet does the masking, so
        // the fade goes on the colour.
        for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k] * fade;
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };

    for (const Mote& one : motes_) {
        if (!one.alive) continue;
        if (one.smoke) {
            // Gathered over its first sixth and thinned over the rest: a puff born at full reads
            // as a stamp. Added, as MU adds smoke01, so the fade is on the colour.
            const float t = one.full > 0.0f ? 1.0f - one.life / one.full : 1.0f;
            const float in = std::min(1.0f, t / 0.16f);
            put(one.sheet, one.at, one.width * one.scale, one.roll, one.colour,
                one.alpha * in * std::pow(1.0f - t, 1.3f));
            continue;
        }
        const float fade = one.dims && one.full > 0.0f ? one.life / one.full : 1.0f;
        put(one.sheet, one.at, one.width * one.scale, one.roll, one.colour, fade);
    }
    // The ball: the halo MU lays with every stamp, drawn once and here; and MU2's core star in
    // it, on the bolt's own brightness.
    for (const Head& head : heads_) {
        if (!head.flying) continue;
        const float lit = std::min(1.0f, head.life * kWakeTint);
        const float halo[3] = {kHaloTint, kHaloTint, kHaloTint};
        put(spark_, head.at, kSparkWidth * kSparkScale, 0.0f, halo, lit);
        put(energy_, head.at, kEnergyWidth * kCoreScale, head.coreRoll, kWhite, lit);
    }
    // The impacts: the flash, the bloom behind it, and the ring opening round both.
    for (const Burst& one : bursts_) {
        if (!one.alive) continue;
        const float flash = std::clamp(one.age / kFlashFrames, 0.0f, 1.0f);
        const float starTint[3] = {1.0f, 0.92f, 1.0f};
        put(star_, one.at, kFlashFrom + (kFlashTo - kFlashFrom) * std::sqrt(flash), one.roll,
            starTint, (1.0f - flash) * (1.0f - flash));
        const float bloom = std::clamp(one.age / kBloomFrames, 0.0f, 1.0f);
        const float bloomTint[3] = {0.55f, 0.7f, 1.0f};
        put(bloom_, one.at, kBloomWide, 0.0f, bloomTint, 1.0f - bloom);
    }
}

uint32_t Bolt::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Head& head : heads_) {
        if (!head.flying || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = head.at[k];
        light.reach = kGlowTiles;
        light.height = kChest / kPerMetre;
        // `Luminosity = LifeTime * 0.2`, four at birth, clamped at one here as the wake is.
        const float lum = std::min(1.0f, head.life * kGlowTint) * kGlowShare;
        for (int k = 0; k < 3; ++k) light.colour[k] = kGlow[k] * lum;
    }
    // The impact's flash on the ground, ours: bright for a breath and gone with the ring.
    for (const Burst& one : bursts_) {
        if (!one.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = one.at[k];
        light.reach = kImpactGlowTiles;
        light.height = kChest / kPerMetre;
        const float left = 1.0f - std::clamp(one.age / kBurstFrames, 0.0f, 1.0f);
        for (int k = 0; k < 3; ++k) light.colour[k] = kImpactGlow[k] * left * left;
    }
    return count;
}

uint32_t Bolt::flying() const {
    uint32_t count = 0;
    for (const Head& head : heads_) count += head.flying ? 1u : 0u;
    return count;
}

}  // namespace mu::game
