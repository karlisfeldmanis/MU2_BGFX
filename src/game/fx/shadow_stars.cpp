#include "game/fx/shadow_stars.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kUnit = 0.01f;
// Shiny02 at Scale 2.5 and Magic_Ground2 at 0.8, in MU's units, halved.
constexpr float kStarHalfWidth = 32.0f * 2.5f * kUnit * 0.5f;
constexpr float kStarHalfHeight = 64.0f * 2.5f * kUnit * 0.5f;
constexpr float kRingHalf = 128.0f * 0.8f * kUnit * 0.5f;
// The Poison Shadow's Light.
constexpr float kGreen[3] = {0.2f, 0.7f, 0.1f};
// **Ours**: how much of MU's Light the rings keep, and how much of what is behind them the
// Shadow's stars take away. Lowered over the user's four asks (fx/shadow_stars.h): MU's crisp
// rings summed past white in HDR, where its 8-bit framebuffer clipped them green.
constexpr float kRingDim = 0.08f;
constexpr float kStarDim = 0.5f;
// The Alquamos's star lights: flare01 (64 texels) at MU's Scale 0.6, in (0.8, 0.9, 1). Ours at
// half of MU's light, the Shadows' kStarDim, for the user's subtle auras.
constexpr float kStarlightHalf = 64.0f * 0.6f * kUnit * 0.5f;
constexpr float kStarlightTint[3] = {0.8f, 0.9f, 1.0f};
constexpr float kStarlightDim = 0.5f;
// Its blow's ribbons: Scale 30 units wide in (0.2, 0.2, 1); the head's Shiny02 (32 texels) at
// 0.85 and flare01 at 1.5, in (0.5, 0.5, 1). Ours at 0.6 of MU's light.
constexpr float kRibbonHalf = 30.0f * kUnit * 0.5f;
constexpr float kRibbonTint[3] = {0.2f, 0.2f, 1.0f};
constexpr float kRibbonHead[3] = {0.5f, 0.5f, 1.0f};
constexpr float kRibbonShinyHalf = 32.0f * 0.85f * kUnit * 0.5f;
constexpr float kRibbonLightHalf = 64.0f * 1.5f * kUnit * 0.5f;
constexpr float kRibbonDim = 0.6f;
// A dozen joints a Shadow; room for a pack of them in sight.
constexpr size_t kStars = 12 * 32;
// A monster's faint light (play_tuning.h kAuraLights): its colour at this much -- for the Poison
// Shadow's green about a quarter of the Poison spell's miasma (fx/poison.h) -- over this reach,
// hung this high; and no more than this many at once, of the renderer's four moving lights.
constexpr float kGlowDim = 0.3f;
constexpr float kGlowReach = 2.5f;
constexpr float kGlowHeight = 1.5f;
constexpr uint32_t kGlowsMost = 2;
// Its embers: a 64-unit Fire01 cell, burning through the strip's four cells over this many
// reference frames while rising this fast, at this much of full light. Ours.
constexpr float kEmberHalf = 0.64f * 0.5f * 0.6f;
constexpr float kEmberLife = 12.0f;
constexpr float kEmberRise = 0.6f;   // metres a second
constexpr float kEmberDim = 0.45f;
constexpr size_t kEmbers = 96;
// The Devil's beam: MU's 50-unit joint, ours at this half width and light.
constexpr float kBeamHalf = 0.18f;
constexpr float kBeamDim = 0.5f;
// The Death Gorgon's rolling fireballs: this fast, this long, this big, this bright. Ours.
constexpr float kRollSpeed = 4.0f;    // metres a second
constexpr float kRollLife = 0.7f;     // seconds
constexpr float kRollHalf = 0.35f;
constexpr float kRollDim = 0.7f;
// The Balrog's circle: out to this radius over its life, at this light. Ours.
constexpr float kCircleRadius = 4.0f;  // metres
constexpr float kCircleLife = 0.9f;    // seconds
constexpr float kCircleDim = 0.5f;
// The Hydra's gem: MU's sprites at a sheet's pixels times their scale in centimetres --
// lightning2 (128 square) at 1, Shiny03 (128 by 16) at 4 -- in RenderLight's orange-white, at
// this much of MU's light. Ours, so the flare does not drown the gem in this HDR frame.
constexpr float kFlareLightningHalf = 128.0f * 1.0f * kUnit * 0.5f;
constexpr float kFlareStreakHalfWidth = 128.0f * 4.0f * kUnit * 0.5f;
constexpr float kFlareStreakHalfHeight = 16.0f * 4.0f * kUnit * 0.5f;
constexpr float kFlareColour[3] = {1.0f, 0.6f, 0.4f};
constexpr float kFlareDim = 0.5f;
// A lightning's wisp of smoke: this many puffs, this big growing to this, rising this fast and
// drifting this far, gone over this life at this much of the sheet. Ours, minimal.
constexpr int kWispPuffs = 3;
constexpr float kWispHalf = 0.25f;
constexpr float kWispGrow = 2.2f;
constexpr float kWispRise = 0.5f;     // metres a second
constexpr float kWispDrift = 0.25f;   // metres a second
constexpr float kWispLife = 1.1f;     // seconds
constexpr float kWispDim = 0.5f;
// A Queen Rainer's blizzard (ShadowStars::blizzard). Ours: at half of MU's grey, twenty shards
// summed over one body being a white blot at its own.
constexpr int kBlizzardShards = 20;
constexpr float kBlizzardFall = 15.0f;   // reference frames
constexpr float kBlizzardDim = 0.5f;
constexpr float kBlizzardShinyHalf = 32.0f * kUnit * 0.5f;
constexpr float kBlizzardLightHalf = 64.0f * kUnit * 0.5f;
constexpr size_t kWisps = 96;

}  // namespace

bool ShadowStars::open(const std::string& assetDir, content::Textures& textures,
                       const content::Showing& table) {
    auto load = [&](const char* name) {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("shadow stars: no cooked effect named '%s'", name);
            return bgfx::TextureHandle BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    // Ours: blurred copies of MU's two sheets (pipeline/index.py), the user's "more blurry".
    shiny_ = load("shiny_02_soft");
    ring_ = load("magic_ground_soft");
    fire_ = load("fire");
    laser_ = load("joint_laser");
    blur_ = load("trail_motion");
    thunder_ = load("joint_thunder");
    lightning_ = load("lightning_2");
    smoke_ = load("smoke01");
    wisps_.reserve(kWisps);
    streak_ = load("shiny_03");
    light_ = load("light");
    flare_ = load("flare");
    shinyAdded_ = load("shiny_02");
    flares_.reserve(8);
    stars_.reserve(kStars);
    embers_.reserve(kEmbers);
    open_ = bgfx::isValid(shiny_) && bgfx::isValid(ring_);
    return open_;
}

void ShadowStars::shutdown() {
    stars_.clear();
    open_ = false;
}

void ShadowStars::update(float seconds) {
    stars_.clear();
    starlights_.clear();
    ribbons_.clear();
    glows_.clear();
    beams_.clear();
    flares_.clear();
    for (Roll& one : rolls_) {
        one.age += seconds;
        one.position[0] += one.dx * kRollSpeed * seconds;
        one.position[2] += one.dz * kRollSpeed * seconds;
    }
    rolls_.erase(std::remove_if(rolls_.begin(), rolls_.end(),
                                [](const Roll& one) { return one.age >= kRollLife; }),
                 rolls_.end());
    for (Circle& one : circles_) one.age += seconds;
    circles_.erase(std::remove_if(circles_.begin(), circles_.end(),
                                  [](const Circle& one) { return one.age >= kCircleLife; }),
                   circles_.end());
    for (Wisp& one : wisps_) {
        one.age += seconds;
        one.position[0] += one.drift[0] * seconds;
        one.position[1] += kWispRise * seconds;
        one.position[2] += one.drift[1] * seconds;
    }
    wisps_.erase(std::remove_if(wisps_.begin(), wisps_.end(),
                                [](const Wisp& one) { return one.age >= kWispLife; }),
                 wisps_.end());
    for (Shard& one : shards_) {
        if (one.wait > 0.0f) {
            one.wait -= seconds;
            continue;
        }
        one.frames += seconds * 25.0f;
    }
    shards_.erase(std::remove_if(shards_.begin(), shards_.end(),
                                 [](const Shard& one) { return one.frames >= kBlizzardFall; }),
                  shards_.end());
    for (Ember& one : embers_) {
        one.age += seconds * 25.0f;
        one.position[1] += kEmberRise * seconds;
    }
    embers_.erase(std::remove_if(embers_.begin(), embers_.end(),
                                 [](const Ember& one) { return one.age >= kEmberLife; }),
                  embers_.end());
}

void ShadowStars::glow(const float at[3], float fade, const float colour[3]) {
    if (!open_ || fade <= 0.0f || glows_.size() >= 32) return;
    Glow one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.fade = fade;
    for (int i = 0; i < 3; ++i) one.colour[i] = colour[i];
    glows_.push_back(one);
}

void ShadowStars::beam(const float from[3], const float to[3]) {
    if (!open_ || !bgfx::isValid(laser_) || beams_.size() >= 32) return;
    Beam one;
    for (int i = 0; i < 3; ++i) {
        one.from[i] = from[i];
        one.to[i] = to[i];
    }
    beams_.push_back(one);
}

void ShadowStars::blurBeam(const float from[3], const float to[3], float half) {
    if (!open_ || !bgfx::isValid(blur_) || beams_.size() >= 32) return;
    Beam one;
    for (int i = 0; i < 3; ++i) {
        one.from[i] = from[i];
        one.to[i] = to[i];
    }
    one.half = half;
    beams_.push_back(one);
}

void ShadowStars::blizzard(const float at[3], float wait) {
    if (!open_ || shards_.size() >= 8 * kBlizzardShards) return;
    const auto roll = [&]() {
        dice_ = dice_ * 1664525u + 1013904223u;
        return dice_ >> 8;
    };
    for (int i = 0; i < kBlizzardShards; ++i) {
        Shard one;
        one.start[0] = at[0] + (float(roll() % 200u) - 100.0f + 100.0f) * kUnit;
        one.start[1] = at[1] + 500.0f * kUnit;
        one.start[2] = at[2] + (float(roll() % 200u) - 100.0f) * kUnit;
        // LifeTime rand() % 15 + 15, the last fifteen frames the fall.
        one.wait = wait + float(roll() % 15u) / 25.0f;
        one.frames = 0.0f;
        one.scale = float(roll() % 4u + 4u) * 0.2f;
        one.spin = float(roll() % 360u) * 0.0174533f;
        shards_.push_back(one);
    }
}

void ShadowStars::thunderBeam(const float from[3], const float to[3], float half,
                              const float colour[3]) {
    if (!open_ || !bgfx::isValid(thunder_) || beams_.size() >= 64) return;
    Beam one;
    for (int i = 0; i < 3; ++i) {
        one.from[i] = from[i];
        one.to[i] = to[i];
    }
    one.half = half;
    one.thunder = true;
    for (int i = 0; i < 3; ++i) one.colour[i] = colour[i];
    beams_.push_back(one);
}

void ShadowStars::roll(const float at[3], float dx, float dz) {
    if (!open_ || !bgfx::isValid(fire_) || rolls_.size() >= 48) return;
    Roll one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.position[1] += kRollHalf;
    one.dx = dx;
    one.dz = dz;
    one.age = 0.0f;
    rolls_.push_back(one);
}

void ShadowStars::circle(const float at[3]) {
    if (!open_ || circles_.size() >= 8) return;
    Circle one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.position[1] += 0.05f;
    one.age = 0.0f;
    circles_.push_back(one);
}

void ShadowStars::flare(const float at[3], float fade, float pulse) {
    if (!open_ || fade <= 0.0f || flares_.size() >= 8) return;
    Flare one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.level = fade * pulse;
    flares_.push_back(one);
}

void ShadowStars::wisp(const float at[3]) {
    if (!open_ || !bgfx::isValid(smoke_)) return;
    for (int i = 0; i < kWispPuffs && wisps_.size() < kWisps; ++i) {
        Wisp one;
        for (int k = 0; k < 3; ++k) one.position[k] = at[k];
        const float turn = (float(i) + float(wisps_.size() % 7) * 0.37f) * 2.0943951f;
        one.drift[0] = std::cos(turn) * kWispDrift;
        one.drift[1] = std::sin(turn) * kWispDrift;
        one.age = -0.08f * float(i);  // one after another
        wisps_.push_back(one);
    }
}

void ShadowStars::ember(const float at[3]) {
    if (!open_ || !bgfx::isValid(fire_) || embers_.size() >= kEmbers) return;
    Ember one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.age = 0.0f;
    embers_.push_back(one);
}

uint32_t ShadowStars::lights(gfx::PointLight* out, uint32_t max, const float near[3]) const {
    const uint32_t most = std::min<uint32_t>(max, kGlowsMost);
    auto distance = [near](const Glow& one) {
        const float dx = one.position[0] - near[0];
        const float dz = one.position[2] - near[2];
        return dx * dx + dz * dz;
    };
    // The nearest first, by picking; there are few.
    std::vector<const Glow*> order;
    for (const Glow& one : glows_) order.push_back(&one);
    std::sort(order.begin(), order.end(),
              [&](const Glow* a, const Glow* b) { return distance(*a) < distance(*b); });
    uint32_t count = 0;
    for (const Glow* one : order) {
        if (count >= most) break;
        gfx::PointLight& light = out[count++];
        for (int i = 0; i < 3; ++i) light.position[i] = one->position[i];
        light.reach = kGlowReach;
        light.height = kGlowHeight;
        for (int i = 0; i < 3; ++i) light.colour[i] = one->colour[i] * kGlowDim * one->fade;
    }
    return count;
}

void ShadowStars::starlight(const float at[3], float fade, float luminosity, const float* tint) {
    if (!open_ || fade <= 0.0f || starlights_.size() >= kStars) return;
    Starlight one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    for (int i = 0; i < 3; ++i) one.tint[i] = tint ? tint[i] : kStarlightTint[i];
    one.level = fade * luminosity;
    starlights_.push_back(one);
}

void ShadowStars::ribbon(const float (*points)[3], int count, float fade) {
    if (!open_ || fade <= 0.0f || count < 2 || ribbons_.size() >= 16) return;
    Ribbon one;
    one.count = std::min(count, kRibbonTails);
    for (int t = 0; t < one.count; ++t)
        for (int i = 0; i < 3; ++i) one.points[t][i] = points[t][i];
    one.fade = fade;
    ribbons_.push_back(one);
}

void ShadowStars::star(const float at[3], bool poison, float fade) {
    if (!open_ || stars_.size() >= kStars || fade <= 0.0f) return;
    Star one;
    for (int i = 0; i < 3; ++i) one.position[i] = at[i];
    one.poison = poison;
    one.fade = fade;
    stars_.push_back(one);
}

void ShadowStars::gather(gfx::Effects& effects) const {
    if (!open_) return;
    if (bgfx::isValid(light_)) {
        for (const Starlight& one : starlights_) {
            gfx::Sprite sprite;
            for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
            sprite.halfWidth = sprite.halfHeight = kStarlightHalf;
            for (int i = 0; i < 3; ++i) sprite.colour[i] = one.tint[i] * kStarlightDim * one.level;
            sprite.sheet = light_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    for (const Ribbon& one : ribbons_) {
        // The tails as MU's joints are drawn: two crossed faces a stride, the sheet once down
        // the whole from the newest tail (RenderJoints).
        if (bgfx::isValid(flare_)) {
            for (int j = 0; j + 1 < one.count; ++j) {
                const float* a = one.points[j];
                const float* b = one.points[j + 1];
                float dir[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                if (len < 1e-4f) continue;
                for (float& v : dir) v /= len;
                float level[3] = {-dir[2], 0.0f, dir[0]};
                const float lw = std::sqrt(level[0] * level[0] + level[2] * level[2]);
                if (lw < 1e-3f) {
                    level[0] = 1.0f;
                    level[2] = 0.0f;
                } else {
                    level[0] /= lw;
                    level[2] /= lw;
                }
                const float upright[3] = {level[1] * dir[2] - level[2] * dir[1],
                                          level[2] * dir[0] - level[0] * dir[2],
                                          level[0] * dir[1] - level[1] * dir[0]};
                const float u0 = float(j) / float(kRibbonTails - 1);
                const float u1 = float(j + 1) / float(kRibbonTails - 1);
                for (int face = 0; face < 2; ++face) {
                    const float* side = face == 0 ? level : upright;
                    gfx::Sprite quad;
                    quad.placed = true;
                    quad.sheet = flare_;
                    quad.blend = gfx::Blend::Additive;
                    for (int i = 0; i < 3; ++i) quad.colour[i] = kRibbonTint[i] * kRibbonDim * one.fade;
                    quad.colour[3] = 1.0f;
                    for (int i = 0; i < 3; ++i) {
                        quad.corner[0][i] = a[i] - side[i] * kRibbonHalf;
                        quad.corner[1][i] = b[i] - side[i] * kRibbonHalf;
                        quad.corner[2][i] = b[i] + side[i] * kRibbonHalf;
                        quad.corner[3][i] = a[i] + side[i] * kRibbonHalf;
                        quad.position[i] = (a[i] + b[i]) * 0.5f;
                    }
                    quad.cornerUv[0][0] = u0; quad.cornerUv[0][1] = 1.0f;
                    quad.cornerUv[1][0] = u1; quad.cornerUv[1][1] = 1.0f;
                    quad.cornerUv[2][0] = u1; quad.cornerUv[2][1] = 0.0f;
                    quad.cornerUv[3][0] = u0; quad.cornerUv[3][1] = 0.0f;
                    effects.add(quad);
                }
            }
        }
        // Its head: Shiny02 at 0.8-0.9 and two flare01 at 1.44-1.62, as one each here.
        const bgfx::TextureHandle heads[2] = {shinyAdded_, light_};
        const float halves[2] = {kRibbonShinyHalf, kRibbonLightHalf};
        for (int h = 0; h < 2; ++h) {
            if (!bgfx::isValid(heads[h])) continue;
            gfx::Sprite sprite;
            for (int i = 0; i < 3; ++i) sprite.position[i] = one.points[0][i];
            sprite.halfWidth = sprite.halfHeight = halves[h];
            for (int i = 0; i < 3; ++i) sprite.colour[i] = kRibbonHead[i] * kRibbonDim * one.fade;
            sprite.sheet = heads[h];
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    for (const Star& one : stars_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        if (one.poison) {
            sprite.halfWidth = sprite.halfHeight = kRingHalf;
            for (int i = 0; i < 3; ++i) sprite.colour[i] = kGreen[i] * kRingDim * one.fade;
            sprite.sheet = ring_;
            sprite.blend = gfx::Blend::Additive;
        } else {
            sprite.halfWidth = kStarHalfWidth;
            sprite.halfHeight = kStarHalfHeight;
            sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = kStarDim * one.fade;
            sprite.sheet = shiny_;
            sprite.blend = gfx::Blend::Minus;
        }
        effects.add(sprite);
    }
    // The beams: two crossed quads along each, one lying flat and one standing, so it has width
    // from any angle without the camera.
    for (const Beam& one : beams_) {
        const float d[3] = {one.to[0] - one.from[0], one.to[1] - one.from[1], one.to[2] - one.from[2]};
        const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
        if (flat < 0.01f) continue;
        const float half = one.half > 0.0f ? one.half : kBeamHalf;
        const float side[2][3] = {{-d[2] / flat * half, 0.0f, d[0] / flat * half},
                                  {0.0f, half, 0.0f}};
        for (const auto& s : side) {
            gfx::Sprite sprite;
            for (int i = 0; i < 3; ++i) sprite.position[i] = 0.5f * (one.from[i] + one.to[i]);
            sprite.placed = true;
            for (int i = 0; i < 3; ++i) {
                sprite.corner[0][i] = one.from[i] - s[i];
                sprite.corner[1][i] = one.to[i] - s[i];
                sprite.corner[2][i] = one.to[i] + s[i];
                sprite.corner[3][i] = one.from[i] + s[i];
            }
            const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            for (int k = 0; k < 4; ++k) {
                sprite.cornerUv[k][0] = uv[k][0];
                sprite.cornerUv[k][1] = uv[k][1];
            }
            sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = kBeamDim;
            if (one.thunder) {
                for (int i = 0; i < 3; ++i) sprite.colour[i] = one.colour[i];
            }
            sprite.sheet = one.thunder ? thunder_ : one.half > 0.0f ? blur_ : laser_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    for (const Roll& one : rolls_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        sprite.halfWidth = sprite.halfHeight = kRollHalf;
        const int cell = std::min(3, int(one.age / kRollLife * 4.0f));
        sprite.u0 = float(cell) * 0.25f;
        sprite.u1 = sprite.u0 + 0.25f;
        const float left = 1.0f - one.age / kRollLife;
        for (int i = 0; i < 3; ++i) sprite.colour[i] = kRollDim * left;
        sprite.sheet = fire_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    // The circle: a flat ring on the ground, its own Magic_Ground2 grown from nothing.
    for (const Circle& one : circles_) {
        const float t = one.age / kCircleLife;
        const float r = kCircleRadius * std::sqrt(t);
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        sprite.placed = true;
        const float c[4][2] = {{-r, r}, {r, r}, {r, -r}, {-r, -r}};
        const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int k = 0; k < 4; ++k) {
            sprite.corner[k][0] = one.position[0] + c[k][0];
            sprite.corner[k][1] = one.position[1];
            sprite.corner[k][2] = one.position[2] + c[k][1];
            sprite.cornerUv[k][0] = uv[k][0];
            sprite.cornerUv[k][1] = uv[k][1];
        }
        const float left = 1.0f - t;
        sprite.colour[0] = 1.0f * kCircleDim * left;
        sprite.colour[1] = 0.35f * kCircleDim * left;
        sprite.colour[2] = 0.05f * kCircleDim * left;
        sprite.sheet = ring_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    for (const Flare& one : flares_) {
        for (int pass = 0; pass < 2; ++pass) {
            const bgfx::TextureHandle sheet = pass == 0 ? lightning_ : streak_;
            if (!bgfx::isValid(sheet)) continue;
            gfx::Sprite sprite;
            for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
            sprite.halfWidth = pass == 0 ? kFlareLightningHalf : kFlareStreakHalfWidth;
            sprite.halfHeight = pass == 0 ? kFlareLightningHalf : kFlareStreakHalfHeight;
            for (int i = 0; i < 3; ++i) sprite.colour[i] = kFlareColour[i] * kFlareDim * one.level;
            sprite.sheet = sheet;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    for (const Wisp& one : wisps_) {
        if (one.age < 0.0f) continue;
        const float t = one.age / kWispLife;
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        sprite.halfWidth = sprite.halfHeight = kWispHalf * (1.0f + (kWispGrow - 1.0f) * t);
        // In quickly and out slowly.
        const float level = std::min(1.0f, t * 6.0f) * (1.0f - t);
        for (int i = 0; i < 3; ++i) sprite.colour[i] = kWispDim * level;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    for (const Shard& one : shards_) {
        if (one.wait > 0.0f) continue;
        // After f frames: 20 f + f (f - 1) units down, 10 f west; the grey 0.1 a frame.
        const float f = one.frames;
        const float at[3] = {one.start[0] - 10.0f * f * kUnit,
                             one.start[1] - (20.0f * f + f * (f - 1.0f)) * kUnit, one.start[2]};
        const float grey = 0.1f * (f + 1.0f) * kBlizzardDim;
        for (int pass = 0; pass < 2; ++pass) {
            const bgfx::TextureHandle sheet = pass == 0 ? shinyAdded_ : light_;
            if (!bgfx::isValid(sheet)) continue;
            gfx::Sprite sprite;
            for (int i = 0; i < 3; ++i) sprite.position[i] = at[i];
            sprite.halfWidth = sprite.halfHeight =
                pass == 0 ? kBlizzardShinyHalf * one.scale : kBlizzardLightHalf;
            sprite.spin = one.spin;
            for (int i = 0; i < 3; ++i) sprite.colour[i] = grey;
            sprite.sheet = sheet;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    for (const Ember& one : embers_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        sprite.halfWidth = sprite.halfHeight = kEmberHalf;
        const int cell = std::min(3, int(one.age / kEmberLife * 4.0f));
        sprite.u0 = float(cell) * 0.25f;
        sprite.u1 = sprite.u0 + 0.25f;
        const float left = 1.0f - one.age / kEmberLife;
        for (int i = 0; i < 3; ++i) sprite.colour[i] = kEmberDim * left;
        sprite.sheet = fire_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
