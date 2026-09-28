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
    // A shorter bolt jags less, so point blank is not a scribble.
    const float jag = kJag * std::min(1.0f, far / 3.0f);
    for (int path = 0; path < 2; ++path) {
        float (*points)[3] = path == 0 ? arc.wide : arc.thin;
        for (int i = 0; i < kPoints; ++i) {
            const float t = float(i) / float(kPoints - 1);
            // Pinned at both ends and freest in the middle.
            const float room = jag * std::sin(t * 3.14159265f);
            const float side = (unit() * 2.0f - 1.0f) * room;
            const float lift = (unit() * 2.0f - 1.0f) * room * 0.6f;
            for (int k = 0; k < 3; ++k) {
                points[i][k] = arc.from[k] + (arc.to[k] - arc.from[k]) * t + across[k] * side;
            }
            points[i][1] += lift;
        }
    }
    arc.spark = kSparkSmallest + unit() * (kSparkLargest - kSparkSmallest);
    arc.sparkRoll = unit() * kTwoPi;
    arc.glow = 0.75f + unit() * 0.25f;
}

void Thunder::step(Arc& arc, float frames) {
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
    for (const Arc& arc : arcs_) {
        if (!arc.alive) continue;
        const float lit = std::min(1.0f, arc.left / kFadeFrames);
        for (int path = 0; path < 2; ++path) {
            const float (*points)[3] = path == 0 ? arc.wide : arc.thin;
            const float half = (path == 0 ? kWide : kThin) * 0.5f;
            const float bright = path == 0 ? lit : lit * 0.9f;
            for (int i = 0; i + 1 < kPoints; ++i) {
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
                const float u0 = float(i) / float(kPoints - 1) * kRepeats - clock_;
                const float u1 = float(i + 1) / float(kPoints - 1) * kRepeats - clock_;
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
        if (bgfx::isValid(spark_)) {
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

uint32_t Thunder::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Arc& arc : arcs_) {
        if (!arc.alive || count >= max) continue;
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
