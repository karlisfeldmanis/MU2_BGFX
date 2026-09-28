#include "game/fx/thunder.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kTwoPi = 6.28318531f;

}  // namespace

float Thunder::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Thunder::open(const std::string& assetDir, content::Textures& textures,
                   const content::Showing& table) {
    const auto cooked = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) return BGFX_INVALID_HANDLE;
        // An albedo, so it wraps: the sheet repeats twice along a bolt and scrolls.
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    joint_ = cooked("joint_thunder");
    spark_ = cooked("energy");
    smoke_ = cooked("smoke01");
    core::logf("thunder: joint %s, spark %s", bgfx::isValid(joint_) ? "yes" : "NO",
               bgfx::isValid(spark_) ? "yes" : "NO");
    return bgfx::isValid(joint_);
}

void Thunder::strike(const float from[3], const float to[3], uint32_t target) {
    Arc* arc = nullptr;
    for (Arc& one : arcs_) {
        if (!one.alive) {
            arc = &one;
            break;
        }
    }
    if (arc == nullptr) return;
    *arc = Arc{};
    arc->alive = true;
    for (int k = 0; k < 3; ++k) {
        arc->from[k] = from[k];
        arc->to[k] = to[k];
    }
    arc->target = target;
    arc->left = kFrames;
    throwPath(*arc);
}

void Thunder::crackle(const float feet[3], float tall, float seconds) {
    const float frames = seconds * kReference;
    // The light on him, held a little past the last call so it does not blink between frames.
    for (int k = 0; k < 3; ++k) crackleAt_[k] = feet[k];
    crackleAt_[1] += tall * 0.55f;
    crackleLit_ = 0.1f;
    crackleRoll_ = 0.6f + unit() * 0.4f;
    crackleDue_ -= frames;
    if (crackleDue_ > 0.0f) return;
    crackleDue_ += kCrackleEvery;
    const auto around = [&](float out[3]) {
        const float turn = unit() * kTwoPi;
        const float reach = kCrackleRadius * (0.5f + unit() * 0.5f);
        out[0] = feet[0] + std::cos(turn) * reach;
        out[1] = feet[1] + tall * (0.2f + unit() * 0.75f);
        out[2] = feet[2] + std::sin(turn) * reach;
    };
    for (int n = 0; n < kCracklePair; ++n) {
        Arc* arc = nullptr;
        for (Arc& one : arcs_) {
            if (!one.alive) {
                arc = &one;
                break;
            }
        }
        if (arc == nullptr) return;
        *arc = Arc{};
        arc->alive = true;
        arc->small = true;
        around(arc->from);
        around(arc->to);
        arc->target = 0;
        arc->left = kCrackleFrames;
        throwPath(*arc);
        arc->forked = false;
    }
}

void Thunder::throwPath(Arc& arc) {
    // Two directions off the line to throw the points along: level across it, and up.
    float line[3] = {arc.to[0] - arc.from[0], arc.to[1] - arc.from[1], arc.to[2] - arc.from[2]};
    const float far = std::max(1e-3f, std::sqrt(line[0] * line[0] + line[1] * line[1] +
                                                line[2] * line[2]));
    for (float& one : line) one /= far;
    float across[3] = {-line[2], 0.0f, line[0]};
    const float wide = std::max(1e-3f, std::sqrt(across[0] * across[0] + across[2] * across[2]));
    across[0] /= wide;
    across[2] /= wide;
    // A shorter bolt wanders less, so point blank is not a scribble; and each throw its own mood.
    const float mood = kTamest + unit() * (kWildest - kTamest);
    const float step = kJag * mood * std::min(1.0f, far / 3.0f);
    arc.points = kFewest + int(unit() * float(kPoints - kFewest + 1));
    arc.points = std::min(arc.points, kPoints);
    // Where along the line each point falls: uneven gaps, so the kinks do not come in a rhythm.
    float at[kPoints];
    at[0] = 0.0f;
    for (int i = 1; i < arc.points; ++i) at[i] = at[i - 1] + 0.4f + unit();
    for (int i = 1; i < arc.points; ++i) at[i] /= at[arc.points - 1];
    for (int path = 0; path < 2; ++path) {
        float (*points)[3] = path == 0 ? arc.wide : arc.thin;
        // The thin joint wanders on its own and a little harder, so the two do not run as one.
        const float stride = path == 0 ? step : step * 1.3f;
        // A random walk off the line, then the drift taken off so both ends are pinned.
        float side[kPoints], lift[kPoints];
        side[0] = lift[0] = 0.0f;
        for (int i = 1; i < arc.points; ++i) {
            side[i] = side[i - 1] + (unit() * 2.0f - 1.0f) * stride;
            lift[i] = lift[i - 1] + (unit() * 2.0f - 1.0f) * stride * 0.6f;
        }
        const float endSide = side[arc.points - 1], endLift = lift[arc.points - 1];
        for (int i = 0; i < arc.points; ++i) {
            const float t = at[i];
            const float s = side[i] - endSide * t, l = lift[i] - endLift * t;
            for (int k = 0; k < 3; ++k) {
                points[i][k] = arc.from[k] + (arc.to[k] - arc.from[k]) * t + across[k] * s;
            }
            points[i][1] += l;
        }
    }
    // Now and then a branch splits off the wide joint partway along and dies in the air.
    arc.forked = unit() < kForkChance && arc.points > 4;
    if (arc.forked) {
        const int root = 1 + int(unit() * float(arc.points - 3));
        const float length = kForkShortest + unit() * (kForkLongest - kForkShortest);
        const float swing = (unit() * 2.0f - 1.0f);
        float dir[3] = {line[0] + across[0] * swing * 1.4f, line[1] + (unit() - 0.3f) * 0.8f,
                        line[2] + across[2] * swing * 1.4f};
        const float d = std::max(1e-3f, std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] +
                                                  dir[2] * dir[2]));
        for (float& one : dir) one /= d;
        for (int i = 0; i < kForkPoints; ++i) {
            const float t = float(i) / float(kForkPoints - 1);
            for (int k = 0; k < 3; ++k) {
                arc.fork[i][k] = arc.wide[root][k] + dir[k] * length * t +
                                 (i > 0 ? (unit() * 2.0f - 1.0f) * step * 0.8f : 0.0f);
            }
        }
    }
    arc.spark = kSparkSmallest + unit() * (kSparkLargest - kSparkSmallest);
    arc.sparkRoll = unit() * kTwoPi;
    arc.glow = 0.75f + unit() * 0.25f;
}

void Thunder::smoke(const Arc& arc) {
    if (!bgfx::isValid(smoke_)) return;
    for (int i = 1; i + 1 < arc.points; i += kSmokeEvery) {
        Puff* one = nullptr;
        for (Puff& p : puffs_) {
            if (!p.alive) {
                one = &p;
                break;
            }
        }
        if (one == nullptr) return;
        *one = Puff{};
        one->alive = true;
        for (int k = 0; k < 3; ++k) one->at[k] = arc.wide[i][k];
        one->drift[0] = (unit() * 2.0f - 1.0f) * kSmokeDrift;
        one->drift[1] = kSmokeRise * (0.7f + unit() * 0.6f);
        one->drift[2] = (unit() * 2.0f - 1.0f) * kSmokeDrift;
        one->size = kSmokeBorn * (0.8f + unit() * 0.4f);
        one->spin = unit() * kTwoPi;
        one->left = one->full = kSmokeFrames * (0.8f + unit() * 0.4f);
    }
}

void Thunder::step(Arc& arc, float frames) {
    // The smoke rises off the path it last took, as the bolt starts to go out.
    if (!arc.small && !arc.smoked && arc.left - frames <= kFadeFrames) {
        arc.smoked = true;
        smoke(arc);
    }
    arc.left -= frames;
    if (arc.left <= 0.0f) {
        arc.alive = false;
        return;
    }
    arc.reroll -= frames;
    if (arc.reroll <= 0.0f) {
        arc.reroll += kRerollFrames;
        throwPath(arc);
    }
}

void Thunder::gather(gfx::Effects& effects) const {
    gatherSmoke(effects);
    for (const Arc& arc : arcs_) {
        if (!arc.alive) continue;
        const float lit = std::min(1.0f, arc.left / kFadeFrames);
        for (int path = 0; path < 3; ++path) {
            if (path == 2 && !arc.forked) break;
            const float (*points)[3] = path == 0 ? arc.wide : path == 1 ? arc.thin : arc.fork;
            const int count = path == 2 ? kForkPoints : arc.points;
            const float half = (path == 0 ? kWide : kThin) * 0.5f * (arc.small ? kCrackleWidth : 1.0f);
            const float bright = path == 0 ? lit : path == 1 ? lit * 0.9f : lit * 0.7f;
            for (int i = 0; i + 1 < count; ++i) {
                const float* a = points[i];
                const float* b = points[i + 1];
                float along[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float len = std::max(1e-4f, std::sqrt(along[0] * along[0] +
                                                            along[1] * along[1] +
                                                            along[2] * along[2]));
                for (float& one : along) one /= len;
                // Two faces a segment, crossed: one level, one upright, so the bolt has width
                // however the camera meets it -- the cross MU's joints are drawn as.
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
                const float u0 = float(i) / float(count - 1) * kRepeats - clock_;
                const float u1 = float(i + 1) / float(count - 1) * kRepeats - clock_;
                for (int face = 0; face < 2; ++face) {
                    const float* side = face == 0 ? level : upright;
                    gfx::Sprite quad;
                    quad.placed = true;
                    quad.sheet = joint_;
                    quad.blend = gfx::Blend::Additive;
                    for (int k = 0; k < 3; ++k) quad.colour[k] = kWhite[k] * bright;
                    quad.colour[3] = 1.0f;
                    for (int k = 0; k < 3; ++k) {
                        quad.corner[0][k] = a[k] - side[k] * half;
                        quad.corner[1][k] = b[k] - side[k] * half;
                        quad.corner[2][k] = b[k] + side[k] * half;
                        quad.corner[3][k] = a[k] + side[k] * half;
                        quad.position[k] = (a[k] + b[k]) * 0.5f;
                    }
                    quad.cornerUv[0][0] = u0; quad.cornerUv[0][1] = 1.0f;
                    quad.cornerUv[1][0] = u1; quad.cornerUv[1][1] = 1.0f;
                    quad.cornerUv[2][0] = u1; quad.cornerUv[2][1] = 0.0f;
                    quad.cornerUv[3][0] = u0; quad.cornerUv[3][1] = 0.0f;
                    effects.add(quad);
                }
            }
        }
        // The spark where it bites: MU's Thunder01 at the wide joint's head, re-rolled a frame.
        // (The smoke is gathered below, once, for every bolt's puffs together.)
        if (!arc.small && bgfx::isValid(spark_)) {
            gfx::Sprite spark;
            for (int k = 0; k < 3; ++k) spark.position[k] = arc.to[k];
            spark.halfWidth = spark.halfHeight = kSparkWidth * arc.spark * 0.5f;
            spark.spin = arc.sparkRoll;
            for (int k = 0; k < 3; ++k) spark.colour[k] = lit;
            spark.sheet = spark_;
            spark.blend = gfx::Blend::Additive;
            effects.add(spark);
        }
    }
}

void Thunder::gatherSmoke(gfx::Effects& effects) const {
    for (const Puff& one : puffs_) {
        if (!one.alive) continue;
        // In over its first fifth and thinning over the rest: added, so the fade is on the colour.
        const float age = 1.0f - one.left / one.full;
        const float fade = std::min(1.0f, age / 0.2f) * (one.left / one.full);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kSmokeTint[k] * fade;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

uint32_t Thunder::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    if (crackleLit_ > 0.0f && count < max) {
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = crackleAt_[k];
        light.reach = kCrackleGlowTiles;
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kCrackleGlow[k] * crackleRoll_;
    }
    for (const Arc& arc : arcs_) {
        if (!arc.alive || arc.small || count >= max) continue;
        const float lit = std::min(1.0f, arc.left / kFadeFrames) * arc.glow;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = arc.to[k];
        light.reach = kGlowTiles;
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kGlow[k] * lit;
    }
    return count;
}

uint32_t Thunder::striking() const {
    uint32_t count = 0;
    for (const Arc& arc : arcs_) {
        if (arc.alive) ++count;
    }
    return count;
}

}  // namespace mu::game
