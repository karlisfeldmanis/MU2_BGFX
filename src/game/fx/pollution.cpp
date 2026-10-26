#include "game/fx/pollution.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;
constexpr float kLift = 5.0f;       // RenderTerrainAlphaBitmap's own, in units
constexpr float kCell = 0.5f;       // the land followed in cells no wider (fx/warp's)
// The casting circle's rings: model, turn a frame (MoveHandlers.cpp:1140-1161); their lights
// are the book's (Pollution::Palette).
struct RingRow {
    const char* path;   // under effects/
    const char* sheet;
    float turn;
};
constexpr RingRow kRings[7] = {
    {"pollution/Suhwanzin1.obj", "pollution/testmsop.png", -3.0f},
    {"pollution/Suhwanzin11.obj", "pollution/testmsop.png", 3.0f},
    {"pollution/Suhwanzin111.obj", "pollution/testmsop.png", -3.0f},
    {"weakness/Suhwanzin2.obj", "weakness/monolines.png", 3.0f},
    {"weakness/Suhwanzin22.obj", "weakness/monolines.png", -3.0f},
    {"weakness/Suhwanzin222.obj", "weakness/monolines.png", 3.0f},
    {"pollution/Suhwanzin4.obj", "pollution/monolines.png", 0.0f},
};

float clampOne(float v) { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

bool Pollution::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/";
    const auto take = [&](const std::string& path) -> bgfx::TextureHandle {
        if (!core::fileExists(path)) return BGFX_INVALID_HANDLE;
        return textures.load(path, content::TextureRole::Albedo);
    };
    bool whole = true;
    for (int i = 0; i < kRingModels; ++i) {
        rings_[i].clear();
        loadEffectObj(dir + kRings[i].path, kUnit, "", rings_[i]);
        ringSheet_[i] = take(dir + kRings[i].sheet);
        whole = whole && !rings_[i].empty();
    }
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    light_ = sheet("light");            // BITMAP_LIGHT
    impact_ = sheet("move_rings");      // BITMAP_SUMMON_IMPACT, empact01
    core::logf("casting circle: rings %s", whole ? "yes" : "NO");
    return whole;
}

void Pollution::circle(const float caster[3], const Palette& palette) {
    for (Circle& one : circles_) {
        if (one.alive) continue;
        one = Circle{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = caster[k];
            one.impact[k] = palette.impact[k];
        }
        one.left = 44.0f;
        break;
    }
    int made = 0;
    for (Ring& one : ringsLive_) {
        if (made == kRingModels) break;
        if (one.alive) continue;
        one = Ring{};
        one.alive = true;
        one.model = made++;
        const float* light = one.model < 3 ? palette.rings1 : one.model < 6 ? palette.rings2
                                                                            : palette.ring4;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = caster[k];
            one.light[k] = light[k];
        }
        const bool four = one.model == 6;
        one.scale = four ? 1.0f : 0.9f;
        one.left = four ? 25.0f : 40.0f;
    }
}

void Pollution::update(float seconds) {
    const float frames = seconds * kFps;
    for (Circle& one : circles_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        // Subtype 10's darkness, 44 frames; subtype 9's rings, 40 of them, ending together.
        one.darkAlpha += (one.left < 20.0f ? -0.03f : (one.darkAlpha < 1.0f ? 0.05f : 0.0f)) * frames;
        one.darkAlpha = std::clamp(one.darkAlpha, 0.0f, 1.0f);
        one.ringAlpha += (one.left < 24.0f ? -0.05f : 0.0f) * frames;
        one.ringAlpha = std::clamp(one.ringAlpha, 0.0f, 1.0f);
        one.ringTurn[0] -= 8.0f * frames;
        one.ringTurn[1] += 4.0f * frames;
    }
    for (Ring& one : ringsLive_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        if (one.left < 20.0f) {
            one.glow -= 0.03f * frames;
        } else if (one.glow < 0.5f) {
            one.glow += 0.05f * frames;
        }
        one.glow = std::clamp(one.glow, 0.0f, 0.5f);
        one.turn += kRings[one.model].turn * frames;
        if (one.model == 6) one.scale += 0.6f * frames;
    }
}

void Pollution::gatherGround(gfx::Effects& effects, bgfx::TextureHandle sheet, gfx::Blend blend,
                             const float at[3], float across, float turnDegrees,
                             const float colour[3]) const {
    if (!bgfx::isValid(sheet) || ground_ == nullptr || across <= 0.0f) return;
    if (colour[0] + colour[1] + colour[2] <= 0.0f) return;
    const int cells = std::clamp(int(std::ceil(across / kCell)), 1, 8);
    const float c = std::cos(turnDegrees * kDegrees), s = std::sin(turnDegrees * kDegrees);
    const auto place = [&](float u, float v, float* out) {
        const float x = at[0] + u * across;
        const float z = at[2] + v * across;
        out[0] = x;
        out[1] = ground_->heightAt(x, z) + kLift * kUnit;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = sheet;
    sprite.blend = blend;
    for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k];
    sprite.colour[3] = 1.0f;
    const float inv = 1.0f / float(cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float u0 = float(i) * inv - 0.5f, u1 = u0 + inv;
            const float v0 = float(j) * inv - 0.5f, v1 = v0 + inv;
            const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            for (int k = 0; k < 4; ++k) {
                place(uv[k][0], uv[k][1], sprite.corner[k]);
                // The sheet turned about its middle, as RenderTerrainAlphaBitmap's Rotation.
                const float ru = c * uv[k][0] - s * uv[k][1], rv = s * uv[k][0] + c * uv[k][1];
                sprite.cornerUv[k][0] = ru + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - rv;
            }
            place((u0 + u1) * 0.5f, (v0 + v1) * 0.5f, sprite.position);
            effects.add(sprite);
        }
    }
}

void Pollution::gather(gfx::Effects& effects) const {
    for (const Circle& one : circles_) {
        if (!one.alive) continue;
        // Subtype 10: darkness under him, laid over itself up to five times.
        const int layers = one.left < 3.0f ? 1 : one.left < 6.0f ? 2 : one.left < 9.0f ? 3
                           : one.left < 12.0f ? 4 : 5;
        const float dark[3] = {one.darkAlpha, one.darkAlpha, one.darkAlpha};
        for (int i = 0; i < layers; ++i) {
            gatherGround(effects, light_, gfx::Blend::Minus, one.at, 12.0f, 0.0f, dark);
        }
        // Subtype 9: the two impact circles, turning against each other.
        if (one.left > 4.0f) {  // its 40 frames, the last of the darkness's 44
            float ring[3];
            for (int k = 0; k < 3; ++k) ring[k] = clampOne(one.impact[k] * one.ringAlpha);
            gatherGround(effects, impact_, gfx::Blend::Additive, one.at, 2.4f, one.ringTurn[0], ring);
            gatherGround(effects, impact_, gfx::Blend::Additive, one.at, 2.88f, one.ringTurn[1], ring);
        }
    }
    for (const Ring& one : ringsLive_) {
        if (!one.alive || rings_[one.model].empty() || one.glow <= 0.0f) continue;
        const float yaw = one.turn * kDegrees;
        const float c = std::cos(yaw), s = std::sin(yaw);
        const float x[3] = {c, 0.0f, -s}, y[3] = {0.0f, 1.0f, 0.0f}, z[3] = {s, 0.0f, c};
        float lit[3];
        for (int k = 0; k < 3; ++k) lit[k] = clampOne(one.light[k] * one.glow);
        submitEffectAlong(effects, rings_[one.model], ringSheet_[one.model], gfx::Blend::Additive,
                          one.at, x, y, z, one.scale, lit, 1.0f);
    }
}

}  // namespace mu::game
