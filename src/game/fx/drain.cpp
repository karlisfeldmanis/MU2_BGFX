#include "game/fx/drain.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
// MU's particle is its bitmap's width times its scale, in units (ZzzEffectParticle.cpp:8923).
constexpr float kMotePixels = 64.0f;
constexpr float kMoteLight[3] = {1.0f, 0.2f, 0.2f};
constexpr float kGhostLight[3] = {0.8f, 0.1f, 0.2f};
constexpr float kStrandLight[3] = {1.0f, 0.0f, 0.1f};
// A joint's scale is its width in units; the ghost sheet is four times as long as it is wide.
constexpr float kGhostWide = 40.0f, kGhostLong = 160.0f;
constexpr float kStrandWide = 10.0f, kStrandLong = 50.0f;
// How far a ghost swings either way as it flies, ours, for MU's 90-degree turns.
constexpr float kGhostSwing = 0.35f;

// Where a flier's head is at `t` of its way, swung across its flight.
void headAt(const float from[3], const float to[3], const float side[3], float swing, float t,
            float out[3]) {
    const float sway = std::sin(t * kPi * 3.0f) * swing * (1.0f - t);
    for (int k = 0; k < 3; ++k) out[k] = from[k] + (to[k] - from[k]) * t + side[k] * sway;
}

// A strip from `tail` to `head`, `half` either side, as two crossed faces -- one level, one
// upright -- so it has width from any angle, as fx/thunder draws a bolt.
void strip(gfx::Effects& effects, bgfx::TextureHandle sheet, const float tail[3],
           const float head[3], float half, const float light[3], float alpha) {
    float along[3] = {head[0] - tail[0], head[1] - tail[1], head[2] - tail[2]};
    const float len =
        std::max(1e-4f, std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]));
    for (float& one : along) one /= len;
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
    for (int face = 0; face < 2; ++face) {
        const float* side = face == 0 ? level : upright;
        gfx::Sprite quad;
        quad.placed = true;
        quad.sheet = sheet;
        quad.blend = gfx::Blend::Additive;
        for (int k = 0; k < 3; ++k) quad.colour[k] = std::clamp(light[k] * alpha, 0.0f, 1.0f);
        quad.colour[3] = 1.0f;
        for (int k = 0; k < 3; ++k) {
            quad.corner[0][k] = tail[k] - side[k] * half;
            quad.corner[1][k] = head[k] - side[k] * half;
            quad.corner[2][k] = head[k] + side[k] * half;
            quad.corner[3][k] = tail[k] + side[k] * half;
            quad.position[k] = (tail[k] + head[k]) * 0.5f;
        }
        quad.cornerUv[0][0] = 0.0f; quad.cornerUv[0][1] = 1.0f;
        quad.cornerUv[1][0] = 1.0f; quad.cornerUv[1][1] = 1.0f;
        quad.cornerUv[2][0] = 1.0f; quad.cornerUv[2][1] = 0.0f;
        quad.cornerUv[3][0] = 0.0f; quad.cornerUv[3][1] = 0.0f;
        effects.add(quad);
    }
}

}  // namespace

float Drain::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Drain::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table) {
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    mote_ = sheet("drain_mote");    // BITMAP_LIGHT + 2
    ghost_ = sheet("drain_ghost");  // BITMAP_DRAIN_LIFE_GHOST
    light_ = sheet("light");        // BITMAP_LIGHT
    const bool whole = bgfx::isValid(mote_) && bgfx::isValid(ghost_) && bgfx::isValid(light_);
    core::logf("drain: mote %s, ghost %s, light %s", bgfx::isValid(mote_) ? "yes" : "NO",
               bgfx::isValid(ghost_) ? "yes" : "NO", bgfx::isValid(light_) ? "yes" : "NO");
    return whole;
}

void Drain::cast(uint32_t caster, uint32_t target, const float self[3], const float there[3]) {
    for (Cast& one : casts_) {
        if (one.alive) continue;
        one = Cast{};
        one.alive = true;
        one.caster = caster;
        one.target = target;
        for (int k = 0; k < 3; ++k) {
            one.self[k] = self[k];
            one.there[k] = there[k];
        }
        // A hundred units before him, toward what he drains, a little over his middle: MU's
        // bone 18 (his hand) pushed along his facing.
        const float dx = there[0] - self[0], dz = there[2] - self[2];
        const float len = std::max(1e-3f, std::sqrt(dx * dx + dz * dz));
        one.hand[0] = self[0] + dx / len * 100.0f * kUnit;
        one.hand[1] = self[1] + 0.2f;
        one.hand[2] = self[2] + dz / len * 100.0f * kUnit;
        frame(one, 0);
        one.done = 0;
        return;
    }
}

void Drain::throwMote(const float middle[3]) {
    for (Mote& one : motes_) {
        if (one.alive) continue;
        one = Mote{};
        one.alive = true;
        // 40 units either way, 30 deep, 80 up and -100..80 about that, from the feet -- here
        // from the middle, a metre over them.
        one.at[0] = middle[0] + (unit() * 80.0f - 40.0f) * kUnit;
        one.at[2] = middle[2] + (unit() * 60.0f - 30.0f) * kUnit;
        one.at[1] = middle[1] + (unit() * 180.0f - 100.0f - 20.0f) * kUnit;
        one.scale = 1.8f;
        one.spin = unit() * 2.0f * kPi;
        one.left = kMoteFrames;
        return;
    }
}

void Drain::throwFlier(bool ghost, const float from[3], const float to[3], uint32_t whom) {
    for (Flier& one : fliers_) {
        if (one.alive) continue;
        one = Flier{};
        one.alive = true;
        one.ghost = ghost;
        one.whom = whom;
        for (int k = 0; k < 3; ++k) {
            one.from[k] = from[k];
            one.to[k] = to[k];
        }
        const float dx = to[0] - from[0], dz = to[2] - from[2];
        const float len = std::max(1e-3f, std::sqrt(dx * dx + dz * dz));
        one.side[0] = -dz / len;
        one.side[2] = dx / len;
        one.swing = ghost ? kGhostSwing * (unit() < 0.5f ? -1.0f : 1.0f) * (0.5f + unit()) : 0.0f;
        one.full = ghost ? kGhostFrames : kStrandFrames;
        return;
    }
}

// MU's frame `at` of the 70 (its LifeTime 70 - at).
void Drain::frame(Cast& one, int at) {
    // None, one or two motes: one in ten, seven in ten, two in ten.
    const auto count = [&]() {
        const float roll = unit();
        return roll < 0.1f ? 0 : roll < 0.8f ? 1 : 2;
    };
    for (int i = count(); i > 0; --i) throwMote(one.self);
    if (at >= 10) {
        for (int i = count(); i > 0; --i) throwMote(one.there);
    }
    if (at <= 4) {
        // None to four ghosts: 0 one in ten, 1 three in ten, 3 five in ten, 4 one in ten (MU's
        // case 4-5 falls through to 3).
        const float roll = unit();
        const int ghosts = roll < 0.1f ? 0 : roll < 0.4f ? 1 : roll < 0.9f ? 3 : 4;
        for (int i = 0; i < ghosts; ++i) {
            float from[3];
            for (int k = 0; k < 3; ++k) from[k] = one.hand[k] + unit() * 50.0f * kUnit;
            float to[3] = {one.there[0], one.there[1] + (unit() * 40.0f - 20.0f) * kUnit,
                           one.there[2]};
            throwFlier(true, from, to, one.target);
        }
    }
    if (at == 6) {
        // Off its bones, one in two: here a dozen points about its middle, one in two.
        for (int i = 0; i < 12; ++i) {
            if (unit() < 0.5f) continue;
            const float from[3] = {one.there[0] + (unit() - 0.5f) * 0.8f,
                                   one.there[1] + (unit() - 0.5f) * 1.4f,
                                   one.there[2] + (unit() - 0.5f) * 0.8f};
            throwFlier(false, from, one.self, one.caster);
        }
    }
}

void Drain::gather(gfx::Effects& effects) const {
    for (const Mote& one : motes_) {
        if (!one.alive || !bgfx::isValid(mote_)) continue;
        const float fade = std::min(1.0f, one.left / kMoteFade);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = one.at[k];
            sprite.colour[k] = kMoteLight[k] * fade;
        }
        sprite.halfWidth = sprite.halfHeight = kMotePixels * one.scale * 0.5f * kUnit;
        sprite.spin = one.spin;
        sprite.sheet = mote_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    for (const Flier& one : fliers_) {
        if (!one.alive) continue;
        const float t = std::min(1.0f, one.age / one.full);
        // In over its first fifth, out over its last.
        const float alpha = std::min(1.0f, std::min(t / 0.2f, (1.0f - t) / 0.2f));
        float head[3], tail[3];
        headAt(one.from, one.to, one.side, one.swing, t, head);
        const float longest = (one.ghost ? kGhostLong : kStrandLong) * kUnit;
        const float way[3] = {head[0] - one.from[0], head[1] - one.from[1], head[2] - one.from[2]};
        const float gone = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
        const float back = std::min(longest, gone);
        float behind[3];
        headAt(one.from, one.to, one.side, one.swing, std::max(0.0f, t - 0.15f), behind);
        float dir[3] = {head[0] - behind[0], head[1] - behind[1], head[2] - behind[2]};
        const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (dl < 1e-4f || back < 1e-3f) continue;
        for (int k = 0; k < 3; ++k) tail[k] = head[k] - dir[k] / dl * back;
        if (one.ghost) {
            strip(effects, ghost_, tail, head, kGhostWide * 0.5f * kUnit, kGhostLight, alpha);
        } else {
            strip(effects, light_, tail, head, kStrandWide * 0.5f * kUnit, kStrandLight, alpha);
        }
    }
}

}  // namespace mu::game
