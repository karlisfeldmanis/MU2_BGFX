#include "game/fx/omen.h"

#include <algorithm>
#include <cmath>

#include "content/ground.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// Over the land by what the click marker keeps (fx/marker.cpp), and cut no finer than it is.
constexpr float kLift = 0.05f;
constexpr float kCell = 0.6f;
constexpr int kMostCells = 24;
// A stain, not a light: a dark red laid over the land with the alpha blend, and a char for a
// pool. Additive, the ember of the first cut blew out to near white in this HDR frame (the
// user, 2026-10-06: 'very bright nit polished'). And a dark one for the shadows. All ours.
constexpr float kEmber[3] = {0.55f, 0.08f, 0.03f};
constexpr float kChar[3] = {0.08f, 0.03f, 0.02f};
constexpr float kShade[3] = {0.55f, 0.5f, 0.45f};
// How strong: faint as it is told, plain as it lands, a flash, and the fade after. Halved on
// 2026-10-06 (the user: 'very bright'), and it must stay under the fire it warns of.
constexpr float kFirst = 0.10f;
constexpr float kLast = 0.38f;
constexpr float kFlash = 0.45f;
constexpr float kFlashSeconds = 0.12f;
constexpr float kFadeSeconds = 0.4f;
constexpr float kBurning = 0.3f;
constexpr float kShadeStrength = 0.7f;
// The Inferno's field at this share of a mark's strength, a deep red dimming.
constexpr float kFieldShare = 0.35f;
constexpr float kField[3] = {0.25f, 0.03f, 0.02f};

}  // namespace

bool Omen::open(const std::string& assetDir, content::Textures& textures) {
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const std::string path = assetDir + "/effects/raid/" + name;
        if (!core::fileExists(path)) {
            core::logError("omen: no %s", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    disc_ = take("omen_disc.png");
    blot_ = take("omen_blot.png");
    marks_.reserve(64);
    return bgfx::isValid(disc_) || bgfx::isValid(blot_);
}

void Omen::shutdown() {
    disc_ = BGFX_INVALID_HANDLE;
    blot_ = BGFX_INVALID_HANDLE;
    marks_.clear();
}

void Omen::tell(Shape shape, float x, float z, float radius, float tell, float hold, float yaw,
                float half) {
    Mark mark;
    mark.shape = shape;
    mark.x = x;
    mark.z = z;
    mark.radius = radius;
    mark.tell = std::max(0.0f, tell);
    mark.hold = std::max(0.0f, hold);
    mark.yaw = yaw;
    mark.half = half;
    marks_.push_back(mark);
}

void Omen::update(float seconds) {
    for (Mark& mark : marks_) mark.age += seconds;
    marks_.erase(std::remove_if(marks_.begin(), marks_.end(),
                                [](const Mark& m) { return m.age > m.tell + m.hold + kFadeSeconds; }),
                 marks_.end());
}

void Omen::disc(gfx::Effects& effects, const content::Ground& ground, const Mark& mark,
                bgfx::TextureHandle sheet, float alpha, bool dark) const {
    if (!bgfx::isValid(sheet) || alpha <= 0.0f) return;
    const float across = mark.radius * 2.0f;
    const int cells = std::clamp(int(std::ceil(across / kCell)), 1, kMostCells);
    const auto place = [&](float a, float b, float* out) {
        const float x = mark.x + a * across, z = mark.z + b * across;
        out[0] = x;
        out[1] = ground.heightAt(x, z) + kLift;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = sheet;
    sprite.blend = dark ? gfx::Blend::Minus : gfx::Blend::Alpha;
    const float* tint = dark                         ? kShade
                        : mark.shape == Shape::Pool  ? kChar
                        : mark.shape == Shape::Field ? kField
                                                     : kEmber;
    for (int i = 0; i < 3; ++i) sprite.colour[i] = tint[i];
    sprite.colour[3] = alpha;
    const float inv = 1.0f / float(cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float a0 = float(i) * inv - 0.5f, a1 = a0 + inv;
            const float b0 = float(j) * inv - 0.5f, b1 = b0 + inv;
            // A cell wholly outside the disc draws nothing.
            const float na = std::min(std::fabs(a0), std::fabs(a1)) * (a0 * a1 > 0 ? 1.0f : 0.0f);
            const float nb = std::min(std::fabs(b0), std::fabs(b1)) * (b0 * b1 > 0 ? 1.0f : 0.0f);
            if (na * na + nb * nb > 0.25f) continue;
            const float ab[4][2] = {{a0, b0}, {a1, b0}, {a1, b1}, {a0, b1}};
            for (int k = 0; k < 4; ++k) {
                place(ab[k][0], ab[k][1], sprite.corner[k]);
                sprite.cornerUv[k][0] = ab[k][0] + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - ab[k][1];
            }
            place((a0 + a1) * 0.5f, (b0 + b1) * 0.5f, sprite.position);
            effects.add(sprite);
        }
    }
}

void Omen::cone(gfx::Effects& effects, const content::Ground& ground, const Mark& mark,
                float alpha) const {
    if (!bgfx::isValid(blot_) || alpha <= 0.0f) return;
    // Rings out from the mouth and slices across the sixty degrees, each cell a quad on the
    // land; the blot's middle for an even fill, dimmer as it reaches out.
    constexpr int kRings = 8, kSlices = 10;
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = blot_;
    sprite.blend = gfx::Blend::Alpha;
    for (int i = 0; i < 3; ++i) sprite.colour[i] = kEmber[i];
    // The drawing's yaw looks down (sin yaw, cos yaw) in x and z (Play::follow).
    const auto place = [&](float out, float turn, float* at) {
        const float a = mark.yaw + turn;
        const float x = mark.x + std::sin(a) * out, z = mark.z + std::cos(a) * out;
        at[0] = x;
        at[1] = ground.heightAt(x, z) + kLift;
        at[2] = z;
    };
    for (int r = 0; r < kRings; ++r) {
        const float o0 = mark.radius * float(r) / kRings, o1 = mark.radius * float(r + 1) / kRings;
        // Soft at both ends: in from the mouth and out to its reach.
        const float t = (float(r) + 0.5f) / kRings;
        sprite.colour[3] = alpha * std::min(1.0f, t * 4.0f) * (1.0f - 0.55f * t);
        for (int s = 0; s < kSlices; ++s) {
            const float t0 = -mark.half + 2.0f * mark.half * float(s) / kSlices;
            const float t1 = -mark.half + 2.0f * mark.half * float(s + 1) / kSlices;
            // The edges of the fan softer than its middle.
            const float edge = 1.0f - std::fabs((float(s) + 0.5f) / kSlices * 2.0f - 1.0f);
            const float keep = sprite.colour[3];
            sprite.colour[3] *= 0.35f + 0.65f * std::sqrt(edge);
            place(o0, t0, sprite.corner[0]);
            place(o0, t1, sprite.corner[1]);
            place(o1, t1, sprite.corner[2]);
            place(o1, t0, sprite.corner[3]);
            for (int k = 0; k < 4; ++k) {
                sprite.cornerUv[k][0] = 0.45f + 0.1f * float(k == 1 || k == 2);
                sprite.cornerUv[k][1] = 0.45f + 0.1f * float(k >= 2);
            }
            place((o0 + o1) * 0.5f, (t0 + t1) * 0.5f, sprite.position);
            effects.add(sprite);
            sprite.colour[3] = keep;
        }
    }
}

void Omen::gather(gfx::Effects& effects, const content::Ground& ground) const {
    for (const Mark& mark : marks_) {
        float alpha = 0.0f;
        if (mark.age < mark.tell) {
            const float t = mark.tell > 0.0f ? mark.age / mark.tell : 1.0f;
            alpha = kFirst + (kLast - kFirst) * std::pow(t, 1.5f);
        } else if (mark.age < mark.tell + kFlashSeconds) {
            alpha = kFlash;
        } else if (mark.age < mark.tell + mark.hold) {
            alpha = kBurning;
        } else {
            const float left = 1.0f - (mark.age - mark.tell - mark.hold) / kFadeSeconds;
            alpha = (mark.hold > 0.0f ? kBurning : kFlash) * std::max(0.0f, left);
        }
        switch (mark.shape) {
            case Shape::Disc:
                disc(effects, ground, mark, disc_, alpha, false);
                break;
            case Shape::Field:
                disc(effects, ground, mark, blot_, alpha * kFieldShare, false);
                break;
            case Shape::Pool:
                disc(effects, ground, mark, blot_, std::min(alpha, kBurning), false);
                break;
            case Shape::Shadow:
                // Dark from the first, deepening as it is told; gone with the fire.
                disc(effects, ground, mark, blot_,
                     kShadeStrength * (mark.age < mark.tell ? 0.5f + 0.5f * mark.age / std::max(0.01f, mark.tell)
                                                            : std::max(0.0f, 1.0f - (mark.age - mark.tell) / kFadeSeconds)),
                     true);
                break;
            case Shape::Cone:
                cone(effects, ground, mark, alpha);
                break;
        }
    }
}

}  // namespace mu::game
