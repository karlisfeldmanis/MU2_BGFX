#include "game/fx/raven.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kStreakLight[3] = {1.0f, 0.8f, 0.6f};
constexpr float kSpiritLight[3] = {1.0f, 0.6f, 0.4f};
constexpr float kFlareLight[3] = {0.5f, 0.5f, 0.5f};
// A joint's scale is its width in units: the dash's 20..59 (ours their middle), the spirit's 20,
// the flare's 40.
constexpr float kStreakWide = 30.0f, kSpiritWide = 20.0f, kFlareWide = 40.0f;
// How far back its tail is drawn, in frames of its flight. Ours.
constexpr float kTail = 4.0f;

// A strip from `tail` to `head`, `half` either side, as two crossed faces (fx/drain's).
void strip(gfx::Effects& effects, bgfx::TextureHandle sheet, const float tail[3], const float head[3],
           float half, const float light[3], float alpha) {
    float along[3] = {head[0] - tail[0], head[1] - tail[1], head[2] - tail[2]};
    const float len = std::max(1e-4f, std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]));
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
    const float upright[3] = {level[1] * along[2] - level[2] * along[1], level[2] * along[0] - level[0] * along[2],
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

float RavenShow::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool RavenShow::open(const std::string& assetDir, content::Textures& textures, const content::Showing& table) {
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    light_ = sheet("light");          // BITMAP_LIGHT, Effect/flare01
    spirit_ = sheet("joint_spirit");  // BITMAP_JOINT_SPIRIT, JointSpirit01
    flare_ = sheet("flare");          // BITMAP_FLARE, Effect/Flare
    shiny_ = sheet("shiny");          // BITMAP_SHINY
    const bool whole = bgfx::isValid(light_) && bgfx::isValid(spirit_) && bgfx::isValid(flare_);
    core::logf("raven show: light %s, spirit %s, flare %s", bgfx::isValid(light_) ? "yes" : "NO",
               bgfx::isValid(spirit_) ? "yes" : "NO", bgfx::isValid(flare_) ? "yes" : "NO");
    return whole;
}

RavenShow::Joint* RavenShow::free() {
    for (Joint& one : joints_) {
        if (!one.alive) return &one;
    }
    return nullptr;
}

void RavenShow::streaks(const float at[3]) {
    for (int i = 0; i < 10; ++i) {
        Joint* one = free();
        if (!one) return;
        *one = Joint{};
        one->alive = true;
        one->kind = 0;
        one->life = 10.0f;
        const float yaw = unit() * 2.0f * kPi, pitch = 30.0f * kPi / 180.0f;
        one->dir[0] = std::cos(pitch) * std::sin(yaw);
        one->dir[1] = std::sin(pitch);
        one->dir[2] = std::cos(pitch) * std::cos(yaw);
        one->speed = (1.0f + unit() * 4.0f) * kUnit;
        for (int k = 0; k < 3; ++k) one->at[k] = one->was[k] = at[k];
    }
}

void RavenShow::dash(const float at[3]) {
    for (Burst& one : bursts_) {
        if (one.alive) continue;
        one = Burst{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        streaks(at);
        one.thrown = 1;
        return;
    }
}

void RavenShow::ball(const float from[3], const float to[3]) {
    for (int i = 0; i < 5; ++i) {
        Joint* one = free();
        if (!one) return;
        *one = Joint{};
        one->alive = true;
        one->kind = 1;
        one->life = 20.0f;
        one->speed = 40.0f * kUnit;
        // Spread off the bird's body, as its triangles are.
        for (int k = 0; k < 3; ++k) one->at[k] = one->was[k] = from[k] + (unit() - 0.5f) * 0.4f;
        // MU's +-70 about the target and 50 up.
        one->aim[0] = to[0] + (unit() - 0.5f) * 1.4f;
        one->aim[1] = to[1] + 0.5f;
        one->aim[2] = to[2] + (unit() - 0.5f) * 1.4f;
    }
}

void RavenShow::level(const float at[3], bool up) {
    for (int i = 0; i < (up ? 5 : 15); ++i) {
        Joint* one = free();
        if (!one) return;
        *one = Joint{};
        one->alive = true;
        one->kind = 2;
        one->life = 50.0f;
        one->spin = unit() * 2.0f * kPi;
        one->rise = (2.0f + unit() * 2.5f) * kUnit;
        for (int k = 0; k < 3; ++k) one->aim[k] = at[k];
        for (int k = 0; k < 3; ++k) one->at[k] = one->was[k] = at[k];
    }
}

void RavenShow::update(float seconds) {
    const float frames = seconds * kFps;
    for (Burst& one : bursts_) {
        if (!one.alive) continue;
        one.age += frames;
        while (one.thrown < 3 && one.age >= float(one.thrown)) {
            streaks(one.at);
            ++one.thrown;
        }
        if (one.thrown >= 3) one.alive = false;
    }
    for (Joint& one : joints_) {
        if (!one.alive) continue;
        one.age += frames;
        if (one.age >= one.life) {
            one.alive = false;
            continue;
        }
        float before[3] = {one.at[0], one.at[1], one.at[2]};
        if (one.kind == 0) {
            // Quicker by half a unit a frame.
            one.speed += 0.5f * kUnit * frames;
            for (int k = 0; k < 3; ++k) one.at[k] += one.dir[k] * one.speed * frames;
        } else if (one.kind == 1) {
            float to[3] = {one.aim[0] - one.at[0], one.aim[1] - one.at[1], one.aim[2] - one.at[2]};
            const float gap = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
            const float step = std::min(gap, one.speed * frames);
            for (int k = 0; k < 3; ++k) one.at[k] += gap > 1e-4f ? to[k] / gap * step : 0.0f;
            // Arrived: its last ten frames left to fade there.
            if (gap < 0.5f) one.life = std::min(one.life, one.age + 10.0f);
        } else {
            // A turn about its centre, 40 out, rising.
            const float angle = one.spin + one.age * 0.25f;
            one.at[0] = one.aim[0] + std::cos(angle) * 0.4f;
            one.at[2] = one.aim[2] + std::sin(angle) * 0.4f;
            one.at[1] += one.rise * frames;
        }
        // The tail follows a few frames behind the head.
        const float lag = std::clamp(frames / kTail, 0.0f, 1.0f);
        for (int k = 0; k < 3; ++k) one.was[k] += (before[k] - one.was[k]) * lag;
    }
}

void RavenShow::gather(gfx::Effects& effects) const {
    for (const Joint& one : joints_) {
        if (!one.alive) continue;
        const float left = one.life - one.age;
        // Its light down over its last frames: a streak's over all ten, a spirit's and a flare's ten.
        const float fade = one.kind == 0 ? std::clamp(left / one.life, 0.0f, 1.0f) : std::clamp(left / 10.0f, 0.0f, 1.0f);
        const float* light = one.kind == 0 ? kStreakLight : one.kind == 1 ? kSpiritLight : kFlareLight;
        const float wide = (one.kind == 0 ? kStreakWide : one.kind == 1 ? kSpiritWide : kFlareWide) * kUnit;
        const bgfx::TextureHandle sheet = one.kind == 0 ? light_ : one.kind == 1 ? spirit_ : flare_;
        if (!bgfx::isValid(sheet)) continue;
        strip(effects, sheet, one.was, one.at, wide * 0.5f, light, fade);
        // A spirit's head: a light and a shine of 2 x life/10 + 2.
        if (one.kind == 1 && bgfx::isValid(light_)) {
            gfx::Sprite head;
            for (int k = 0; k < 3; ++k) {
                head.position[k] = one.at[k];
                head.colour[k] = kSpiritLight[k] * fade;
            }
            head.colour[3] = 1.0f;
            head.halfWidth = head.halfHeight = (2.0f * left / 10.0f + 2.0f) * 0.08f;
            head.sheet = light_;
            head.blend = gfx::Blend::Additive;
            effects.add(head);
            if (bgfx::isValid(shiny_)) {
                head.sheet = shiny_;
                head.halfWidth = head.halfHeight *= 0.6f;
                effects.add(head);
            }
        }
    }
}

}  // namespace mu::game
