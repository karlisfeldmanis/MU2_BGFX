#include "game/fx/weakness.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;
// RenderTerrainAlphaBitmap's own lift, in MU units, and the land followed in cells no wider
// than this, as fx/warp's circle does.
constexpr float kLift = 5.0f;
constexpr float kCell = 0.5f;
// The sheets' pixels: MU's particle is its bitmap's width and height times its scale, in units
// (ZzzEffectParticle.cpp:8923-8924).
constexpr float kShinyPixels = 64.0f;
constexpr float kPinWide = 16.0f, kPinTall = 128.0f;

float clampOne(float v) { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

float Weakness::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Weakness::open(const std::string& assetDir, content::Textures& textures,
                    const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/weakness/";
    const char* names[3] = {"Suhwanzin2.obj", "Suhwanzin22.obj", "Suhwanzin222.obj"};
    for (int i = 0; i < 3; ++i) {
        walls_[i].clear();
        loadEffectObj(dir + names[i], kUnit, "", walls_[i]);
    }
    if (core::fileExists(dir + "monolines.png")) {
        lines_ = textures.load(dir + "monolines.png", content::TextureRole::Albedo);
    }
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    circle_ = sheet("weakness_circle");  // BITMAP_MAGIC_ZIN
    shiny_ = sheet("weakness_shiny");    // BITMAP_SHINY + 6
    pin_ = sheet("weakness_pin");        // BITMAP_PIN_LIGHT
    glow_ = sheet("light");              // BITMAP_LIGHT
    const bool whole = !walls_[0].empty() && !walls_[1].empty() && !walls_[2].empty() &&
                       bgfx::isValid(lines_) && bgfx::isValid(circle_);
    core::logf("weakness: walls %s, circle %s, shiny %s, pin %s", whole ? "yes" : "NO",
               bgfx::isValid(circle_) ? "yes" : "NO", bgfx::isValid(shiny_) ? "yes" : "NO",
               bgfx::isValid(pin_) ? "yes" : "NO");
    return whole;
}

void Weakness::cast(const float feet[3]) {
    // The five circles, in the let-go's order: subtype 1 at 7, subtype 0 at 2, subtype 2 at 1,
    // 0.2 and 0.1.
    const struct {
        int kind;
        float scale;
        float light[3];
    } circles[5] = {{1, 7.0f, {2.0f, 0.1f, 0.1f}},
                    {0, 2.0f, {2.0f, 0.4f, 0.3f}},
                    {2, 1.0f, {2.0f, 0.4f, 0.3f}},
                    {2, 0.2f, {2.0f, 0.4f, 0.3f}},
                    {2, 0.1f, {2.0f, 0.4f, 0.3f}}};
    int made = 0;
    for (Circle& one : circles_) {
        if (made == 5) break;
        if (one.alive) continue;
        const auto& want = circles[made++];
        one = Circle{};
        one.alive = true;
        one.kind = want.kind;
        one.scale = want.scale;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = feet[k];
            one.light[k] = want.light[k];
        }
        // ZzzEffect.cpp:1240-1249: 50, 40 and 30 frames; subtype 1 born unseen.
        one.left = one.kind == 0 ? 50.0f : one.kind == 1 ? 40.0f : 30.0f;
        one.alpha = one.kind == 1 ? 0.0f : 1.0f;
    }
    made = 0;
    for (Wall& one : wallsLive_) {
        if (made == 3) break;
        if (one.alive) continue;
        one = Wall{};
        one.alive = true;
        one.model = made++;
        for (int k = 0; k < 3; ++k) one.at[k] = feet[k];
        one.left = 40.0f;
    }
    made = 0;
    for (Spring& one : springs_) {
        if (made == 2) break;
        if (one.alive) continue;
        one = Spring{};
        one.alive = true;
        one.pin = made++ == 1;
        for (int k = 0; k < 3; ++k) one.at[k] = feet[k];
        one.left = one.pin ? 40.0f : 24.0f;
    }
}

void Weakness::throwMote(bool pin, const float at[3], float scale) {
    for (Mote& one : motes_) {
        if (one.alive) continue;
        one = Mote{};
        one.alive = true;
        one.pin = pin;
        // The particle's own birth: five units either way on each axis, and a little more scale.
        one.at[0] = at[0] + (unit() * 10.0f - 5.0f) * kUnit;
        one.at[1] = at[1] + (unit() * 10.0f - 5.0f) * kUnit;
        one.at[2] = at[2] + (unit() * 10.0f - 5.0f) * kUnit;
        one.scale = scale + (pin ? float(int(unit() * 5.0f)) : float(int(unit() * 3.0f))) * 0.1f;
        one.left = 30.0f;
        for (int k = 0; k < 3; ++k) one.light[k] = kRed[k];
        return;
    }
}

void Weakness::mark(const float at[3]) {
    if (unit() < 0.5f) throwMote(false, at, 0.5f);
    if (unit() < 0.5f) throwMote(true, at, 1.0f);
}

void Weakness::update(float seconds) {
    const float frames = seconds * kFps;
    for (Circle& one : circles_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        const bool ending = one.left < 20.0f;
        switch (one.kind) {
            case 0:
                one.alpha += (ending ? -0.05f : (one.alpha < 1.0f ? 0.05f : 0.0f)) * frames;
                break;
            case 1:
                one.alpha += (ending ? -0.03f : (one.alpha < 0.7f ? 0.06f : 0.0f)) * frames;
                break;
            default:
                if (one.scale < 3.5f) one.scale += 0.1f * frames;
                one.alpha += (ending ? -0.05f : (one.alpha < 1.0f ? 0.05f : 0.0f)) * frames;
                break;
        }
        one.alpha = std::clamp(one.alpha, 0.0f, 1.0f);
    }
    for (Wall& one : wallsLive_) {
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
        one.turn += (one.model == 1 ? -3.0f : 3.0f) * frames;
    }
    for (Spring& one : springs_) {
        if (!one.alive) continue;
        one.owed += frames;
        while (one.owed >= 1.0f && one.left > 0.0f) {
            one.owed -= 1.0f;
            one.left -= 1.0f;
            // Up to 250 units aside on both ground axes, 50 to 150 up.
            const float at[3] = {one.at[0] + (unit() * 500.0f - 250.0f) * kUnit,
                                 one.at[1] + (150.0f - unit() * 100.0f) * kUnit,
                                 one.at[2] + (unit() * 500.0f - 250.0f) * kUnit};
            // The star every frame; the streak one frame in two (rand_fps_check(2)).
            if (!one.pin) {
                throwMote(false, at, 0.5f);
            } else if (unit() < 0.5f) {
                throwMote(true, at, 1.0f);
            }
        }
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Mote& one : motes_) {
        if (!one.alive) continue;
        one.left -= frames;
        one.scale -= 0.02f * frames;
        if (one.left <= 0.0f || one.scale <= 0.0f) {
            one.alive = false;
            continue;
        }
        // The star sinks 1.3 units a frame and turns; the streak falls ten.
        one.at[1] -= (one.pin ? 10.0f : 1.3f) * kUnit * frames;
        if (!one.pin) one.spin += 5.0f * kDegrees * frames;
        const float flicker = (unit() * 0.1f - 0.05f) * frames;
        for (float& k : one.light) k = std::max(0.0f, k + flicker);
    }
}

void Weakness::gatherCircle(gfx::Effects& effects, const Circle& one) const {
    if (!bgfx::isValid(circle_) || ground_ == nullptr) return;
    // ZzzEffect.cpp:9960-9976: subtype 0 at alpha * 2, 1 at alpha / 2.5, 2 at alpha.
    const float by = one.kind == 0 ? one.alpha * 2.0f : one.kind == 1 ? one.alpha / 2.5f : one.alpha;
    float colour[3];
    for (int k = 0; k < 3; ++k) colour[k] = clampOne(one.light[k] * by);
    if (colour[0] + colour[1] + colour[2] <= 0.0f) return;
    const float across = one.scale;  // tiles, and a tile is a metre
    const int cells = std::clamp(int(std::ceil(across / kCell)), 1, 8);
    const auto place = [&](float u, float v, float* out) {
        const float x = one.at[0] + u * across;
        const float z = one.at[2] + v * across;
        out[0] = x;
        out[1] = ground_->heightAt(x, z) + kLift * kUnit;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = circle_;
    sprite.blend = gfx::Blend::Additive;
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
                sprite.cornerUv[k][0] = uv[k][0] + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - uv[k][1];
            }
            place((u0 + u1) * 0.5f, (v0 + v1) * 0.5f, sprite.position);
            effects.add(sprite);
        }
    }
}

void Weakness::gather(gfx::Effects& effects) const {
    for (const Circle& one : circles_) {
        if (one.alive) gatherCircle(effects, one);
    }
    // The walls, lit by the let-go's (2.0, 0.4, 0.3) at their BlendMeshLight.
    constexpr float kWallLight[3] = {2.0f, 0.4f, 0.3f};
    constexpr float kScale = 0.6f;
    for (const Wall& one : wallsLive_) {
        if (!one.alive || walls_[one.model].empty() || one.glow <= 0.0f) continue;
        const float yaw = one.turn * kDegrees;
        const float c = std::cos(yaw), s = std::sin(yaw);
        const float x[3] = {c, 0.0f, -s}, y[3] = {0.0f, 1.0f, 0.0f}, z[3] = {s, 0.0f, c};
        float lit[3];
        for (int k = 0; k < 3; ++k) lit[k] = clampOne(kWallLight[k] * one.glow);
        submitEffectAlong(effects, walls_[one.model], lines_, gfx::Blend::Additive, one.at, x, y,
                          z, kScale, lit, 1.0f);
    }
    for (const Mote& one : motes_) {
        if (!one.alive) continue;
        const bgfx::TextureHandle sheet = one.pin ? pin_ : shiny_;
        if (!bgfx::isValid(sheet)) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = one.at[k];
            sprite.colour[k] = clampOne(one.light[k]);
        }
        if (one.pin) {
            sprite.halfWidth = kPinWide * one.scale * 0.5f * kUnit;
            sprite.halfHeight = kPinTall * one.scale * 0.5f * kUnit;
        } else {
            sprite.halfWidth = sprite.halfHeight = kShinyPixels * one.scale * 0.5f * kUnit;
        }
        sprite.spin = one.spin;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
        // The star is drawn twice, its sheet and a BITMAP_LIGHT glow the same size.
        if (!one.pin && bgfx::isValid(glow_)) {
            sprite.sheet = glow_;
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
