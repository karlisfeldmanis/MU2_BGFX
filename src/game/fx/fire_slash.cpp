#include "game/fx/fire_slash.h"

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kTwoPi = 6.28318531f;

float clampOne(float v) { return std::clamp(v, 0.0f, 1.0f); }

float length(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

}  // namespace

float FireSlash::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool FireSlash::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            // Albedos, so they wrap: the ribbon's is GL_REPEAT and the bolts' scrolls.
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    ribbonSheet_ = sheet("fire_slash_ribbon");  // Effect/motion_blur_r2
    skullSheet_ = sheet("fire_slash_skull");    // Skill/Skull
    fireSheet_ = sheet("fire");                 // Effect/Fire01, four cells
    boltSheet_ = sheet("joint_thunder");        // Effect/JointThunder01
    sparkSheet_ = sheet("spark_flash");         // Effect/Spark03
    shinySheet_ = sheet("shiny_02");            // Effect/Shiny02
    core::logf("fire slash: ribbon %s, skull %s, fire %s, bolt %s",
               bgfx::isValid(ribbonSheet_) ? "yes" : "NO", bgfx::isValid(skullSheet_) ? "yes" : "NO",
               bgfx::isValid(fireSheet_) ? "yes" : "NO", bgfx::isValid(boltSheet_) ? "yes" : "NO");
    return bgfx::isValid(ribbonSheet_);
}

void FireSlash::clear() {
    for (Gathering& one : gatherings_) one = Gathering{};
    for (Ribbon& one : ribbons_) one = Ribbon{};
    for (Bolt& one : bolts_) one = Bolt{};
    for (Spark& one : sparks_) one = Spark{};
    for (Fire& one : fires_) one = Fire{};
    skullCount_ = 0;
}

void FireSlash::local(float yaw, float turn, float x, float y, float out[2]) {
    const float c = std::cos(turn * kDegrees), s = std::sin(turn * kDegrees);
    const float X = x * c - y * s, Y = x * s + y * c;
    const float fs = std::sin(yaw), fc = std::cos(yaw);
    out[0] = fc * X - fs * Y;
    out[1] = -fs * X - fc * Y;
}

void FireSlash::cast(uint32_t owner, float release) {
    // A new cast by the same body takes over its old one's slots.
    for (int n = 0; n < kGatherings; ++n) {
        Gathering* slot = nullptr;
        for (Gathering& one : gatherings_) {
            if (!one.alive) {
                slot = &one;
                break;
            }
        }
        if (slot == nullptr) break;
        *slot = Gathering{};
        slot->alive = true;
        slot->owner = owner;
        slot->wait = float(n) / kReferenceFps;  // AttackTime 1, then 2
    }
    Ribbon* ribbon = nullptr;
    for (Ribbon& one : ribbons_) {
        if (one.alive && one.owner == owner && !one.made) {
            ribbon = &one;
            break;
        }
        if (!one.alive && ribbon == nullptr) ribbon = &one;
    }
    if (ribbon == nullptr) return;
    *ribbon = Ribbon{};
    ribbon->alive = true;
    ribbon->owner = owner;
    ribbon->wait = std::max(0.0f, release);
}

void FireSlash::bolt(uint32_t owner, const float from[3], const float to[3], float scale,
                     float frames) {
    for (Bolt& one : bolts_) {
        if (one.alive) continue;
        one = Bolt{};
        one.alive = true;
        one.owner = owner;
        for (int k = 0; k < 3; ++k) {
            one.to[k] = to[k];
            one.trail[0][k] = from[k];
            one.way[k] = to[k] - from[k];
        }
        one.tails = 1;
        one.half = scale * perUnit() * 0.5f;
        one.left = frames;
        // MoveJoint's turn a step, (rand() % 1024 - 512) / Scale degrees, held to a quarter turn.
        one.jitter = std::min(90.0f, 512.0f / std::max(1.0f, scale)) * kDegrees;
        return;
    }
}

void FireSlash::stepBolt(Bolt& one) {
    // Toward its target (MoveHumming, a turn of 50 a move -- here straight at it), thrown off it
    // by the step's jitter in both the level and the upright, and a tail laid where it lands.
    float to[3] = {one.to[0] - one.trail[0][0], one.to[1] - one.trail[0][1],
                   one.to[2] - one.trail[0][2]};
    const float far = length(to);
    const float metre = perUnit();
    if (far > 1e-4f) {
        for (int k = 0; k < 3; ++k) one.way[k] = to[k] / far;
    }
    const float yawOff = (unit() * 2.0f - 1.0f) * one.jitter;
    const float pitchOff = (unit() * 2.0f - 1.0f) * one.jitter;
    const float c = std::cos(yawOff), s = std::sin(yawOff);
    float way[3] = {one.way[0] * c - one.way[2] * s, one.way[1], one.way[0] * s + one.way[2] * c};
    way[1] += std::sin(pitchOff);
    const float size = std::max(1e-4f, length(way));
    const float stride = kBoltStep * metre;
    for (int t = std::min(one.tails, kBoltTails - 1); t > 0; --t) {
        for (int k = 0; k < 3; ++k) one.trail[t][k] = one.trail[t - 1][k];
    }
    for (int k = 0; k < 3; ++k) one.trail[0][k] += way[k] / size * stride;
    one.tails = std::min(one.tails + 1, kBoltTails);
    one.left -= 1.0f;
    if (one.left <= 0.0f) one.alive = false;
}

void FireSlash::stepGathering(Gathering& one) {
    const float metre = perUnit();
    for (int n = 0; n < kGatherPoints; ++n) {
        // A point on the sphere round his hand.
        const float z = unit() * 2.0f - 1.0f, turn = unit() * kTwoPi;
        const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        const float at[3] = {one.hand[0] + r * std::cos(turn) * kGatherRadius * metre,
                             one.hand[1] + z * kGatherRadius * metre,
                             one.hand[2] + r * std::sin(turn) * kGatherRadius * metre};
        if (one.life % 2 == 0) {
            bolt(one.owner, at, one.hand, 10.0f, 10.0f);
        } else {
            for (Spark& spark : sparks_) {
                if (spark.alive) continue;
                spark = Spark{};
                spark.alive = true;
                for (int k = 0; k < 3; ++k) {
                    spark.at[k] = at[k];
                    spark.step[k] = (one.hand[k] - at[k]) / 10.0f;
                }
                spark.scale = between(0.10f, 0.59f);
                spark.left = kSparkLife;
                break;
            }
        }
    }
    one.shiny = between(kShinyLeast, kShinyMost);
    one.shinyRoll = unit() * kTwoPi;
    if (--one.life <= 0) one.alive = false;
}

void FireSlash::stepRibbon(Ribbon& one) {
    const float metre = perUnit();
    // Its light this frame: LifeTime / 30 down to ten, then a third less each frame.
    one.light = one.life >= 10 ? float(one.life) / 30.0f : one.light / 1.5f;
    float axis[2];
    local(one.yaw, kStartTurn, 1.0f, 0.0f, axis);
    for (int step = 0; step < kStepsAFrame && one.tails < kTails; ++step) {
        float off[2];
        local(one.yaw, one.turn, 0.0f, -kRadius, off);
        const float at[3] = {one.pivot[0] + off[0] * metre, one.pivot[1],
                             one.pivot[2] + off[1] * metre};
        for (int t = std::min(one.tails, kTails - 1); t > 0; --t) {
            for (int e = 0; e < 2; ++e) {
                for (int k = 0; k < 3; ++k) one.edge[t][e][k] = one.edge[t - 1][e][k];
            }
        }
        for (int e = 0; e < 2; ++e) {
            const float side = (e == 0 ? -1.0f : 1.0f) * kHalfWidth * metre;
            one.edge[0][e][0] = at[0] + axis[0] * side;
            one.edge[0][e][1] = at[1];
            one.edge[0][e][2] = at[2] + axis[1] * side;
        }
        // A flame on a coin, drifting the joint's way.
        if (unit() < 0.5f) {
            for (Fire& fire : fires_) {
                if (fire.alive) continue;
                fire = Fire{};
                fire.alive = true;
                for (int k = 0; k < 3; ++k) fire.at[k] = at[k];
                float drift[2];
                local(one.yaw, kStartTurn, 0.0f, -between(3.2f, 4.7f), drift);
                fire.drift[0] = drift[0] * metre;
                fire.drift[2] = drift[1] * metre;
                fire.scale = between(1.28f, 1.91f);
                fire.left = kFireLife;
                fire.spin = unit() * kTwoPi;
                break;
            }
        }
        // And two blue bolts from the last point to this one.
        if (one.tails > 0) {
            bolt(0, one.last, at, between(5.0f, 14.0f), 5.0f);
            bolt(0, one.last, at, between(4.0f, 11.0f), 5.0f);
        }
        for (int k = 0; k < 3; ++k) one.last[k] = at[k];
        ++one.tails;
        one.turn -= kStepTurn;
    }
    if (--one.life <= 0) one.alive = false;
}

void FireSlash::gatherEffects(gfx::Effects& effects) const {
    const float metre = perUnit();
    // The ribbons: a quad between each two tails, U along them with the head at 1, V across.
    for (const Ribbon& one : ribbons_) {
        if (!one.alive || !one.made || one.tails < 2 || !bgfx::isValid(ribbonSheet_)) continue;
        const int numTails = one.tails - 1;
        for (int j = 0; j + 1 < one.tails; ++j) {
            gfx::Sprite quad;
            quad.placed = true;
            quad.sheet = ribbonSheet_;
            quad.blend = gfx::Blend::Additive;
            const float lit = clampOne((float(kTails - j) - 0.5f) / float(kTails) * 2.0f * one.light);
            for (int k = 0; k < 3; ++k) quad.colour[k] = lit;
            quad.colour[3] = 1.0f;
            const float u0 = float(numTails - j) / float(kTails - 1);
            const float u1 = float(numTails - j - 1) / float(kTails - 1);
            for (int k = 0; k < 3; ++k) {
                quad.corner[0][k] = one.edge[j][0][k];
                quad.corner[1][k] = one.edge[j][1][k];
                quad.corner[2][k] = one.edge[j + 1][1][k];
                quad.corner[3][k] = one.edge[j + 1][0][k];
                quad.position[k] = (one.edge[j][0][k] + one.edge[j + 1][1][k]) * 0.5f;
            }
            quad.cornerUv[0][0] = u0; quad.cornerUv[0][1] = 0.0f;
            quad.cornerUv[1][0] = u0; quad.cornerUv[1][1] = 1.0f;
            quad.cornerUv[2][0] = u1; quad.cornerUv[2][1] = 1.0f;
            quad.cornerUv[3][0] = u1; quad.cornerUv[3][1] = 0.0f;
            if (!effects.add(quad)) return;
        }
    }
    // The flames: Fire01's four cells over their life, fading with it.
    if (bgfx::isValid(fireSheet_)) {
        for (const Fire& one : fires_) {
            if (!one.alive || one.scale <= 0.0f) continue;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
            sprite.halfWidth = sprite.halfHeight = kFirePixels * one.scale * metre * 0.5f;
            const int cell = std::clamp(int((kFireLife - 1.0f - one.left) / 6.0f), 0, 3);
            sprite.u0 = float(cell) * 0.25f;
            sprite.u1 = float(cell + 1) * 0.25f;
            const float lum = clampOne(one.left / kFireLife);
            for (int k = 0; k < 3; ++k) sprite.colour[k] = lum;
            sprite.sheet = fireSheet_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) return;
        }
    }
    // The sparks drawn in to his hand.
    if (bgfx::isValid(sparkSheet_)) {
        for (const Spark& one : sparks_) {
            if (!one.alive) continue;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
            sprite.halfWidth = sprite.halfHeight = kSparkPixels * one.scale * metre * 0.5f;
            const float lum = clampOne(one.left / 5.0f);
            for (int k = 0; k < 3; ++k) sprite.colour[k] = lum;
            sprite.sheet = sparkSheet_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) return;
        }
    }
    // The glow on his hand while a gathering lasts.
    if (bgfx::isValid(shinySheet_)) {
        for (const Gathering& one : gatherings_) {
            if (!one.alive || one.wait > 0.0f) continue;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = one.hand[k];
            sprite.halfWidth = kShinyWide * one.shiny * kShinyFrame * metre * 0.5f;
            sprite.halfHeight = kShinyTall * one.shiny * kShinyFrame * metre * 0.5f;
            sprite.spin = one.shinyRoll;
            sprite.sheet = shinySheet_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) return;
        }
    }
    // The bolts: each a crossed strip along its trail, the sheet scrolling along it.
    if (bgfx::isValid(boltSheet_)) {
        const float scroll = clock_ * kBoltScroll - std::floor(clock_ * kBoltScroll);
        for (const Bolt& one : bolts_) {
            if (!one.alive || one.tails < 2) continue;
            const float lit = clampOne(one.left / kBoltFade);
            for (int i = 0; i + 1 < one.tails; ++i) {
                const float* a = one.trail[i];
                const float* b = one.trail[i + 1];
                float along[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float len = std::max(1e-4f, length(along));
                for (float& v : along) v /= len;
                float level[3] = {-along[2], 0.0f, along[0]};
                const float lw = std::sqrt(level[0] * level[0] + level[2] * level[2]);
                if (lw < 1e-3f) {
                    level[0] = 1.0f;
                    level[2] = 0.0f;
                } else {
                    level[0] /= lw;
                    level[2] /= lw;
                }
                const float upright[3] = {level[1] * along[2] - level[2] * along[1],
                                          level[2] * along[0] - level[0] * along[2],
                                          level[0] * along[1] - level[1] * along[0]};
                const float u0 = float(i) / float(kBoltTails - 1) * kBoltRepeats - scroll;
                const float u1 = float(i + 1) / float(kBoltTails - 1) * kBoltRepeats - scroll;
                for (int face = 0; face < 2; ++face) {
                    const float* side = face == 0 ? level : upright;
                    gfx::Sprite quad;
                    quad.placed = true;
                    quad.sheet = boltSheet_;
                    quad.blend = gfx::Blend::Additive;
                    for (int k = 0; k < 3; ++k) quad.colour[k] = kBoltBlue[k] * lit;
                    quad.colour[3] = 1.0f;
                    for (int k = 0; k < 3; ++k) {
                        quad.corner[0][k] = a[k] - side[k] * one.half;
                        quad.corner[1][k] = b[k] - side[k] * one.half;
                        quad.corner[2][k] = b[k] + side[k] * one.half;
                        quad.corner[3][k] = a[k] + side[k] * one.half;
                        quad.position[k] = (a[k] + b[k]) * 0.5f;
                    }
                    quad.cornerUv[0][0] = u0; quad.cornerUv[0][1] = 1.0f;
                    quad.cornerUv[1][0] = u1; quad.cornerUv[1][1] = 1.0f;
                    quad.cornerUv[2][0] = u1; quad.cornerUv[2][1] = 0.0f;
                    quad.cornerUv[3][0] = u0; quad.cornerUv[3][1] = 0.0f;
                    if (!effects.add(quad)) return;
                }
            }
        }
    }
    // The skulls over each cut body.
    if (bgfx::isValid(skullSheet_)) {
        for (int i = 0; i < skullCount_; ++i) {
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = skulls_[i].at[k];
            sprite.halfWidth = sprite.halfHeight = kSkullPixels * metre * 0.5f;
            sprite.sheet = skullSheet_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) return;
        }
    }
}

}  // namespace mu::game
