#include "game/fx/wave.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
constexpr float kRadian = kPi / 180.0f;

}  // namespace

float Wave::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Wave::open(const std::string& assetDir, content::Textures& textures,
                const content::Showing& table) {
    const std::string dir = assetDir + "/effects/wave/";
    curtain_.clear();
    if (loadEffectObj(dir + "Magic02.obj", kUnit * kScale, "", curtain_)) {
        const std::string sheet = dir + "magic_g.png";
        // An albedo, so the sampler wraps: the scroll runs the U offset to -4 and a clamped
        // sheet would smear its edge pixel across the whole curtain.
        if (core::fileExists(sheet)) sheet_ = textures.load(sheet, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* smoke = table.effect("smoke01")) {
        smoke_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    }
    core::logf("wave: Magic02 %zu triangles, sheet %s, smoke %s", curtain_.size() / 3,
               bgfx::isValid(sheet_) ? "yes" : "NO", bgfx::isValid(smoke_) ? "yes" : "NO");
    return !curtain_.empty() && bgfx::isValid(sheet_);
}

void Wave::shutdown() {
    curtain_.clear();
    for (Sweep& one : waves_) one.alive = false;
    for (Puff& one : puffs_) one.alive = false;
}

void Wave::cast(const float from[3], const float to[3]) {
    if (curtain_.empty()) return;
    Sweep* wave = nullptr;
    for (Sweep& one : waves_) {
        if (!one.alive) {
            wave = &one;
            break;
        }
    }
    if (wave == nullptr) {
        ++refused_;
        return;
    }
    *wave = Sweep{};
    wave->alive = true;
    for (int k = 0; k < 3; ++k) wave->at[k] = from[k];
    // Flat: MU's direction has no vertical term, so a wave at something on a hill sweeps past it
    // at the height it left.
    const float x = to[0] - from[0], z = to[2] - from[2];
    const float flat = std::sqrt(x * x + z * z);
    wave->along[0] = flat > 1e-4f ? x / flat : 1.0f;
    wave->along[1] = 0.0f;
    wave->along[2] = flat > 1e-4f ? z / flat : 0.0f;
    wave->left = kFrames;
    wave->glow = kBrightestGlow;
}

void Wave::puff(const Sweep& wave) {
    if (!bgfx::isValid(smoke_)) return;
    Puff* one = nullptr;
    for (Puff& p : puffs_) {
        if (!p.alive) {
            one = &p;
            break;
        }
    }
    if (one == nullptr) {
        ++refused_;
        return;
    }
    *one = Puff{};
    one->alive = true;
    for (int k = 0; k < 3; ++k) one->at[k] = wave.at[k];
    // Thrown off the heading by up to forty-five degrees on both axes: a yaw about the up axis
    // and a pitch about the one across the heading.
    const float yaw = between(-kPuffSpread, kPuffSpread) * kRadian;
    const float pitch = between(-kPuffSpread, kPuffSpread) * kRadian;
    const float c = std::cos(yaw), s = std::sin(yaw);
    float dir[3] = {wave.along[0] * c - wave.along[2] * s, 0.0f,
                    wave.along[0] * s + wave.along[2] * c};
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    dir[0] *= cp;
    dir[2] *= cp;
    dir[1] = sp;
    const float speed = between(kSlowestPuff, kFastestPuff) * kUnit;
    for (int k = 0; k < 3; ++k) one->velocity[k] = dir[k] * speed;
    one->size = between(kSmallestPuff, kLargestPuff) * kSmokeWidth;
    one->spin = unit() * kTwoPi;
    one->left = kPuffFrames;
}

void Wave::update(float seconds) {
    const float frames = seconds * kReference;
    for (Sweep& wave : waves_) {
        if (!wave.alive) continue;
        const float went = kSpeed * kUnit * frames;
        for (int k = 0; k < 3; ++k) wave.at[k] += wave.along[k] * went;
        wave.left -= frames;
        if (wave.left <= 0.0f) {
            wave.alive = false;
            continue;
        }
        wave.glow = between(kDimmestGlow, kBrightestGlow);
        if (wave.left < kFadesUnder) {
            wave.glow = std::max(0.0f, wave.glow - (kFadesUnder - wave.left) * kFadeStep);
        }
        // Four a frame, stepped by distance so the count does not follow the machine.
        wave.blown += went;
        while (wave.blown >= kPuffSpacing * kUnit) {
            wave.blown -= kPuffSpacing * kUnit;
            puff(wave);
        }
    }
    const float drag = std::pow(kPuffDrag, frames);
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            one.at[k] += one.velocity[k] * frames;
            one.velocity[k] *= drag;
        }
        one.size += kPuffGrows * kSmokeWidth * frames;
    }
}

void Wave::gather(gfx::Effects& effects) const {
    const float up[3] = {0.0f, 1.0f, 0.0f};
    for (const Sweep& wave : waves_) {
        if (!wave.alive) continue;
        // The model's loaded -Z is its front (it runs 1.17 m ahead of its origin and 2.01 m
        // behind), so Z is laid against the heading; X is across it.
        const float across[3] = {wave.along[2], 0.0f, -wave.along[0]};
        const float back[3] = {-wave.along[0], 0.0f, -wave.along[2]};
        const float lit = std::min(kBrightest, wave.left * kLightsBy);
        const float colour[3] = {kDaylight[0] * lit, kDaylight[1] * lit, kDaylight[2] * lit};
        const float scroll = -wave.left * kScrollsBy;
        submitEffectAlong(effects, curtain_, sheet_, gfx::Blend::Additive, wave.at, across, up,
                          back, 1.0f, colour, 1.0f, scroll);
        for (int g = 0; g < kGhosts; ++g) {
            const float dim[3] = {colour[0] * kGhostLight[g], colour[1] * kGhostLight[g],
                                  colour[2] * kGhostLight[g]};
            submitEffectAlong(effects, curtain_, sheet_, gfx::Blend::Additive, wave.at, across, up,
                              back, kGhostGrow[g], dim, 1.0f, scroll);
        }
    }
    for (const Puff& one : puffs_) {
        if (!one.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * 0.5f;
        sprite.spin = one.spin;
        // Its mover's `Light = LifeTime / 8`, faintly blue; added, so the fade is on the colour.
        const float light = std::min(1.0f, one.left / kSootFrames);
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kSoot[k] * light;
        sprite.colour[3] = 1.0f;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

uint32_t Wave::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Sweep& wave : waves_) {
        if (!wave.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = wave.at[k];
        light.reach = kGlowTiles;
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kGlow[k] * wave.glow;
    }
    return count;
}

uint32_t Wave::flying() const {
    uint32_t count = 0;
    for (const Sweep& wave : waves_) {
        if (wave.alive) ++count;
    }
    return count;
}

}  // namespace mu::game
