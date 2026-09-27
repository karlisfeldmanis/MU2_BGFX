#include "game/fx/warp.h"

#include <algorithm>
#include <cmath>

#include "content/ground.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// MU's reference clock, and its units in a tile.
constexpr float kReference = 25.0f;
constexpr float kPerTile = 100.0f;

// --- ZzzEffect.cpp BITMAP_MAGIC + 2 subtype 0 ------------------------------------------------
constexpr float kLife = 20.0f;         // LifeTime
constexpr float kFoot = 90.0f;         // RenderCircle's ScaleBottom, the radius at the ground
constexpr float kHead = 130.0f;        // ScaleTop
constexpr float kTall = 200.0f;        // Height
constexpr int kSides = 12;             // RenderCircle's Num, thirty degrees a quad
// `Rotation = (int)WorldTime % 3600 / 10.f` degrees with WorldTime in milliseconds: a tenth of
// a degree a millisecond, which is a hundred degrees a second.
constexpr double kTurnDegreesPerSecond = 100.0;
constexpr float kOpens = 0.15f;        // Scale = (20 - LifeTime) * 0.15, in tiles
constexpr float kDims = 5.0f;          // full until five ticks are left...
constexpr float kDim = 0.2f;           // ...then a fifth a tick
constexpr float kLifts = 5.0f;         // RenderTerrainAlphaBitmap's own lift, in units
constexpr float kOrange[3] = {1.0f, 0.4f, 0.2f};
// The circle follows the land in cells no wider than this, as the aura's does.
constexpr float kCell = 0.5f;
constexpr float kPi = 3.14159265358979f;

}  // namespace

bool Warp::open(const std::string& assetDir, content::Textures& textures) {
    const auto take = [&](const std::string& path) -> bgfx::TextureHandle {
        if (!core::fileExists(path)) {
            core::logError("warp: no %s (tools/sync.sh)", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    wall_ = take(assetDir + "/effects/warp/magic_circle.png");
    circle_ = take(assetDir + "/effects/levelup/magic_ground.png");
    core::logf("warp: wall %s, circle %s", bgfx::isValid(wall_) ? "in hand" : "MISSING",
               bgfx::isValid(circle_) ? "in hand" : "MISSING");
    return bgfx::isValid(wall_) && bgfx::isValid(circle_);
}

void Warp::land(const float feet[3], float metresPerTile) {
    if (!bgfx::isValid(wall_) || !bgfx::isValid(circle_) || metresPerTile <= 0.0001f) return;
    for (int k = 0; k < 3; ++k) feet_[k] = feet[k];
    per_ = metresPerTile / kPerTile;
    age_ = 0.0f;
    living_ = true;
}

void Warp::update(float seconds) {
    if (!living_) return;
    age_ += seconds * kReference;
    clock_ += double(seconds);
    if (age_ >= kLife) living_ = false;
}

void Warp::gather(gfx::Effects& effects, const content::Ground& ground) const {
    if (!living_) return;
    const float turn = float(std::fmod(clock_ * kTurnDegreesPerSecond, 360.0)) * kPi / 180.0f;
    gatherWall(effects, turn);
    gatherWall(effects, -turn);
    gatherCircle(effects, ground);
}

void Warp::gatherWall(gfx::Effects& effects, float turn) const {
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = wall_;
    sprite.blend = gfx::Blend::Additive;
    sprite.colour[3] = 1.0f;
    const float step = 2.0f * kPi / float(kSides);
    // A point on the wall at angle `a` and `t` of the way up, from foot to head.
    const auto at = [&](float a, float t, float* out) {
        const float radius = (kFoot + (kHead - kFoot) * t) * per_;
        out[0] = feet_[0] + std::sin(a) * radius;
        out[1] = feet_[1] + kTall * t * per_;
        out[2] = feet_[2] + std::cos(a) * radius;
    };
    for (int side = 0; side < kSides; ++side) {
        const float a0 = float(side) * step + turn, a1 = a0 + step;
        const float u0 = float(side) / float(kSides), u1 = float(side + 1) / float(kSides);
        for (int band = 0; band < kBands; ++band) {
            const float t0 = float(band) / float(kBands), t1 = float(band + 1) / float(kBands);
            // White at the foot and black at the head, taken halfway up the band.
            const float light = 1.0f - (t0 + t1) * 0.5f;
            for (int d = 0; d < 3; ++d) sprite.colour[d] = light;
            // RenderCircle's corners: foot at a0, foot at a1, head at a1, head at a0, with V
            // running from 1 at the foot to 0 at the head.
            at(a0, t0, sprite.corner[0]);
            at(a1, t0, sprite.corner[1]);
            at(a1, t1, sprite.corner[2]);
            at(a0, t1, sprite.corner[3]);
            const float uv[4][2] = {{u0, 1.0f - t0}, {u1, 1.0f - t0}, {u1, 1.0f - t1},
                                    {u0, 1.0f - t1}};
            for (int k = 0; k < 4; ++k) {
                sprite.cornerUv[k][0] = uv[k][0];
                sprite.cornerUv[k][1] = uv[k][1];
            }
            for (int d = 0; d < 3; ++d) {
                sprite.position[d] = (sprite.corner[0][d] + sprite.corner[2][d]) * 0.5f;
            }
            effects.add(sprite);
        }
    }
}

void Warp::gatherCircle(gfx::Effects& effects, const content::Ground& ground) const {
    const float left = kLife - age_;
    const float across = age_ * kOpens * kPerTile * per_;
    if (left <= 0.0f || across <= 0.0f) return;
    const float lit = left < kDims ? std::max(0.0f, 1.0f - (kDims - left) * kDim) : 1.0f;
    const int cells = std::clamp(int(std::ceil(across / kCell)), 1, 8);
    const float lift = kLifts * per_;
    const auto place = [&](float u, float v, float* out) {
        const float x = feet_[0] + u * across;
        const float z = feet_[2] + v * across;
        out[0] = x;
        out[1] = ground.heightAt(x, z) + lift;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = circle_;
    sprite.blend = gfx::Blend::Additive;
    for (int k = 0; k < 3; ++k) sprite.colour[k] = kOrange[k] * lit;
    sprite.colour[3] = 1.0f;
    const float inv = 1.0f / float(cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float u0 = float(i) * inv - 0.5f, u1 = u0 + inv;
            const float v0 = float(j) * inv - 0.5f, v1 = v0 + inv;
            const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            for (int k = 0; k < 4; ++k) {
                place(uv[k][0], uv[k][1], sprite.corner[k]);
                sprite.cornerUv[k][0] = uv[k][0] + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - uv[k][1];
            }
            place((u0 + u1) * 0.5f, (v0 + v1) * 0.5f, sprite.position);
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
