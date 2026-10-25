#include "game/fx/sleep.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;
// RenderTerrainAlphaBitmap's own lift, in MU units, and the land followed in cells no wider than
// this, as fx/weakness's circles are.
constexpr float kLift = 5.0f;
constexpr float kCell = 0.5f;
// The sheets' pixels: MU's particle is its bitmap's width and height times its scale, in units.
constexpr float kFlarePixels = 64.0f;   // flare01
constexpr float kShinyPixels = 64.0f;   // shiny05
constexpr float kSparkWide = 32.0f, kSparkTall = 64.0f;  // Shiny02
constexpr float kWaterPixels = 32.0f;   // water
// The let-go's colours (ZzzCharacter.cpp:5167-5193; MoveHandlers.cpp:2253-2378).
constexpr float kCircleLight[3] = {0.7f, 0.3f, 0.8f};
constexpr float kRingLight[3] = {0.8f, 0.3f, 0.9f};
constexpr float kFlareLight[3] = {0.8f, 0.1f, 0.9f};
constexpr float kShinyLight[3] = {0.7f, 0.6f, 0.9f};
constexpr float kStreakLight[3] = {0.7f, 0.5f, 0.7f};
constexpr float kPuffLight[3] = {0.8f, 0.3f, 0.9f};
// The rings stand 100 units up the sleeper's feet; a streak is born 200 units out.
constexpr float kRingLift = 100.0f;
constexpr float kStreakBirth = 200.0f;
constexpr float kStreakHalf = 2.5f;  // five wide

float clampOne(float v) { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

float Sleep::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Sleep::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/sleep/";
    const char* names[2] = {"elshildring.obj", "elshildring2.obj"};
    for (int i = 0; i < 2; ++i) {
        ring_[i].clear();
        loadEffectObj(dir + names[i], kUnit, "", ring_[i]);
    }
    if (core::fileExists(dir + "macardkmono.png")) {
        band_ = textures.load(dir + "macardkmono.png", content::TextureRole::Albedo);
    }
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    circle_ = sheet("magic_ground");   // BITMAP_MAGIC + 1, Effect/Magic_Ground2
    flare_ = sheet("light");           // BITMAP_LIGHT, flare01
    shiny_ = sheet("weakness_shiny");  // BITMAP_SHINY + 5, shiny05
    spark_ = sheet("shiny_02");        // BITMAP_SHINY + 1, Shiny02
    streak_ = sheet("joint_energy");   // BITMAP_JOINT_HEALING's JointEnergy01
    water_ = sheet("sleep_puff");      // BITMAP_TWINTAIL_WATER, water
    const bool whole = !ring_[0].empty() && !ring_[1].empty() && bgfx::isValid(band_) &&
                       bgfx::isValid(circle_);
    core::logf("sleep: rings %s, circle %s, streak %s, puff %s", whole ? "yes" : "NO",
               bgfx::isValid(circle_) ? "yes" : "NO", bgfx::isValid(streak_) ? "yes" : "NO",
               bgfx::isValid(water_) ? "yes" : "NO");
    return whole;
}

void Sleep::cast(const float feet[3]) {
    for (Circle& one : circles_) {
        if (one.alive) continue;
        one = Circle{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = feet[k];
        one.left = 20.0f;
        return;
    }
}

void Sleep::rings(uint32_t id, const float feet[3]) {
    int made = 0;
    for (Ring& one : rings_) {
        if (made == 2) break;
        if (one.alive) continue;
        one = Ring{};
        one.alive = true;
        one.second = made++ == 1;
        one.id = id;
        for (int k = 0; k < 3; ++k) one.feet[k] = feet[k];
        // ZzzEffect.cpp:875-914: 34 frames at 0.1, the second 35 at 0.15.
        one.left = one.second ? 35.0f : 34.0f;
        one.scale = one.second ? 0.15f : 0.1f;
    }
}

void Sleep::throwMote(bool puff, const float at[3], float light) {
    for (Mote& one : motes_) {
        if (one.alive) continue;
        one = Mote{};
        one.alive = true;
        one.puff = puff;
        one.light = light;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        if (puff) {
            // ZzzEffectParticle.cpp:1131-1141: 20 units either way across, 0 to 40 up.
            one.at[0] += (unit() * 40.0f - 20.0f) * kUnit;
            one.at[1] += unit() * 40.0f * kUnit;
            one.at[2] += (unit() * 40.0f - 20.0f) * kUnit;
            one.scale = 1.0f + unit() * 0.6f;
            one.spin = unit() * 6.2832f;
            one.rise = 2.0f + unit();
            one.left = 60.0f + float(int(unit() * 10.0f));
        } else {
            one.scale = 0.8f + unit() * 0.6f;
            one.spin = unit() * 6.2832f;
            one.left = 1.0f;  // a frame's sparkle
        }
        return;
    }
}

void Sleep::puff(const float at[3]) {
    // 20 units below the bone (MoveHandlers.cpp:2338-2378).
    const float below[3] = {at[0], at[1] - 20.0f * kUnit, at[2]};
    throwMote(true, below);
}

void Sleep::ringCentre(const Ring& one, float out[3]) const {
    out[0] = one.feet[0];
    out[1] = one.feet[1] + kRingLift * kUnit;
    out[2] = one.feet[2];
}

void Sleep::step(float seconds) {
    const float frames = std::min(seconds, 0.25f) * kFps;
    // The motes first, so a sparkle a streak throws below is drawn the frame it is thrown.
    for (Mote& one : motes_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (!one.puff) {
            if (one.left <= 0.0f) one.alive = false;
            continue;
        }
        one.scale -= 0.026f * frames;
        one.alpha -= 0.05f * frames;
        one.light *= std::pow(1.0f / 1.02f, frames);
        one.at[1] += one.rise * kUnit * frames;
        if (one.left <= 0.0f || one.scale <= 0.0f || one.alpha <= 0.0f) one.alive = false;
    }
    for (Circle& one : circles_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    for (int r = 0; r < kRings; ++r) {
        Ring& one = rings_[r];
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        one.alpha += (one.left > 20.0f ? 0.05f : -0.05f) * frames;
        one.alpha = std::clamp(one.alpha, 0.0f, 1.0f);
        one.turn += (one.second ? -8.0f : 8.0f) * frames;
        one.scale += 0.035f * frames;
        // Three streaks a frame, born 200 units out above the centre, each heading anywhere.
        // The first ring throws them; the pair's second would double them on one body.
        if (one.second) continue;
        one.owed += frames;
        float centre[3];
        ringCentre(one, centre);
        while (one.owed >= 1.0f) {
            one.owed -= 1.0f;
            for (int n = 0; n < 3; ++n) {
                for (Streak& s : streaks_) {
                    if (s.alive) continue;
                    s = Streak{};
                    s.alive = true;
                    s.ring = uint32_t(r);
                    const float yaw = unit() * 6.2832f;
                    const float up = unit() * 1.4f;  // above the centre: 0 to 80 degrees up
                    const float out[3] = {std::cos(up) * std::cos(yaw), std::sin(up),
                                          std::cos(up) * std::sin(yaw)};
                    for (int k = 0; k < 3; ++k) {
                        s.at[k] = centre[k] + out[k] * kStreakBirth * kUnit;
                        s.tail[k] = s.at[k];
                    }
                    // Any heading; the homing turns it in.
                    const float hy = unit() * 6.2832f, hp = unit() * 3.1416f - 1.5708f;
                    s.heading[0] = std::cos(hp) * std::cos(hy);
                    s.heading[1] = std::sin(hp);
                    s.heading[2] = std::cos(hp) * std::sin(hy);
                    s.left = 10.0f;
                    break;
                }
            }
        }
    }
    for (Streak& s : streaks_) {
        if (!s.alive) continue;
        const Ring& ring = rings_[s.ring];
        float centre[3];
        ringCentre(ring, centre);
        s.owed += frames;
        while (s.owed >= 1.0f && s.alive) {
            s.owed -= 1.0f;
            s.left -= 1.0f;
            // Home on the centre, at most ten degrees a frame, and four units a frame faster.
            float want[3] = {centre[0] - s.at[0], centre[1] - s.at[1], centre[2] - s.at[2]};
            const float far = std::sqrt(want[0] * want[0] + want[1] * want[1] + want[2] * want[2]);
            if (far > 1e-4f) {
                for (float& w : want) w /= far;
                const float dot = std::clamp(s.heading[0] * want[0] + s.heading[1] * want[1] +
                                                 s.heading[2] * want[2],
                                             -1.0f, 1.0f);
                const float apart = std::acos(dot);
                const float most = 10.0f * kDegrees;
                if (apart <= most) {
                    for (int k = 0; k < 3; ++k) s.heading[k] = want[k];
                } else {
                    const float t = most / apart;
                    float len = 0.0f;
                    for (int k = 0; k < 3; ++k) {
                        s.heading[k] += (want[k] - s.heading[k]) * t;
                        len += s.heading[k] * s.heading[k];
                    }
                    len = std::sqrt(len);
                    if (len > 1e-4f) {
                        for (float& h : s.heading) h /= len;
                    }
                }
            }
            s.speed += 4.0f;
            for (int k = 0; k < 3; ++k) {
                s.tail[k] = s.at[k];
                s.at[k] += s.heading[k] * s.speed * kUnit;
            }
            s.light /= 1.08f;
            // Its Shiny02 sparkle at the centre it homes on, this frame (ZzzEffectJoint.cpp:
            // 3598-3608): the white spark the footage shows in the band's middle.
            throwMote(false, centre, s.light);
            if (s.left <= 0.0f || far * (1.0f / kUnit) <= s.speed) s.alive = false;
        }
    }
}

void Sleep::gatherCircle(gfx::Effects& effects, const Circle& one) const {
    if (!bgfx::isValid(circle_) || ground_ == nullptr) return;
    // `(20 - LifeTime) x 0.15` tiles across, full bright, fading over its last five frames.
    const float across = (20.0f - one.left) * 0.15f;
    if (across <= 0.0f) return;
    const float fade = std::min(1.0f, one.left / 5.0f);
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
    for (int k = 0; k < 3; ++k) sprite.colour[k] = clampOne(kCircleLight[k] * fade);
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

void Sleep::gather(gfx::Effects& effects) const {
    for (const Circle& one : circles_) {
        if (one.alive) gatherCircle(effects, one);
    }
    for (const Ring& one : rings_) {
        if (!one.alive || one.alpha <= 0.0f) continue;
        float centre[3];
        ringCentre(one, centre);
        // The ring itself, added at its light by its BlendMeshLight.
        const std::vector<EffectCorner>& tris = ring_[one.second ? 1 : 0];
        if (!tris.empty() && bgfx::isValid(band_)) {
            const float yaw = one.turn * kDegrees;
            const float c = std::cos(yaw), s = std::sin(yaw);
            const float x[3] = {c, 0.0f, -s}, y[3] = {0.0f, 1.0f, 0.0f}, z[3] = {s, 0.0f, c};
            float lit[3];
            for (int k = 0; k < 3; ++k) lit[k] = clampOne(kRingLight[k] * one.alpha);
            submitEffectAlong(effects, tris, band_, gfx::Blend::Additive, centre, x, y, z,
                              one.scale, lit, 1.0f);
        }
        // Its two flares at 5 and two shinies at 2 and 1, turning both ways, at its Alpha.
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = centre[k];
        sprite.blend = gfx::Blend::Additive;
        if (bgfx::isValid(flare_)) {
            sprite.sheet = flare_;
            sprite.halfWidth = sprite.halfHeight = kFlarePixels * 5.0f * 0.5f * kUnit;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = clampOne(kFlareLight[k] * one.alpha);
            sprite.spin = 0.0f;
            effects.add(sprite);
        }
        if (bgfx::isValid(shiny_)) {
            sprite.sheet = shiny_;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = clampOne(kShinyLight[k] * one.alpha);
            const float size[2] = {2.0f, 1.0f};
            for (int n = 0; n < 2; ++n) {
                sprite.halfWidth = sprite.halfHeight = kShinyPixels * size[n] * 0.5f * kUnit;
                sprite.spin = (n == 0 ? 1.0f : -1.0f) * one.turn * kDegrees;
                effects.add(sprite);
            }
        }
    }
    if (bgfx::isValid(streak_)) {
        for (const Streak& s : streaks_) {
            if (!s.alive) continue;
            // Its two tails, a ribbon five wide: two crossed quads, flat and up.
            float along[3] = {s.at[0] - s.tail[0], s.at[1] - s.tail[1], s.at[2] - s.tail[2]};
            const float far = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
            if (far < 1e-4f) continue;
            for (float& a : along) a /= far;
            float flat[3] = {along[2], 0.0f, -along[0]};
            const float flen = std::sqrt(flat[0] * flat[0] + flat[2] * flat[2]);
            if (flen > 1e-4f) {
                flat[0] /= flen;
                flat[2] /= flen;
            } else {
                flat[0] = 1.0f;
            }
            const float lift[3] = {0.0f, 1.0f, 0.0f};
            for (const float* width : {static_cast<const float*>(flat), static_cast<const float*>(lift)}) {
                gfx::Sprite sprite;
                sprite.placed = true;
                for (int k = 0; k < 3; ++k) {
                    sprite.corner[0][k] = s.tail[k] - width[k] * kStreakHalf * kUnit;
                    sprite.corner[1][k] = s.at[k] - width[k] * kStreakHalf * kUnit;
                    sprite.corner[2][k] = s.at[k] + width[k] * kStreakHalf * kUnit;
                    sprite.corner[3][k] = s.tail[k] + width[k] * kStreakHalf * kUnit;
                    sprite.position[k] = 0.5f * (s.at[k] + s.tail[k]);
                    sprite.colour[k] = clampOne(kStreakLight[k] * s.light);
                }
                sprite.cornerUv[0][0] = 0.0f;
                sprite.cornerUv[0][1] = 0.0f;
                sprite.cornerUv[1][0] = 1.0f;
                sprite.cornerUv[1][1] = 0.0f;
                sprite.cornerUv[2][0] = 1.0f;
                sprite.cornerUv[2][1] = 1.0f;
                sprite.cornerUv[3][0] = 0.0f;
                sprite.cornerUv[3][1] = 1.0f;
                sprite.colour[3] = 1.0f;
                sprite.sheet = streak_;
                sprite.blend = gfx::Blend::Additive;
                effects.add(sprite);
            }
        }
    }
    for (const Mote& one : motes_) {
        if (!one.alive) continue;
        const bgfx::TextureHandle sheet = one.puff ? water_ : spark_;
        if (!bgfx::isValid(sheet)) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        if (one.puff) {
            const float light = std::max(0.0f, one.alpha) * one.light;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = clampOne(kPuffLight[k] * light);
            sprite.halfWidth = sprite.halfHeight = kWaterPixels * one.scale * 0.5f * kUnit;
        } else {
            for (int k = 0; k < 3; ++k) sprite.colour[k] = clampOne(kStreakLight[k] * one.light);
            sprite.halfWidth = kSparkWide * one.scale * 0.5f * kUnit;
            sprite.halfHeight = kSparkTall * one.scale * 0.5f * kUnit;
        }
        sprite.spin = one.spin;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
