#include "game/fx/rite.h"

#include <algorithm>
#include <cmath>

#include "content/ground.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kSky = 9.0f;  // metres from his feet to where the light is lost in the sky
// -- under the game's camera: a taller one ran past the lens and washed the screen white.
// Ours, every colour: one gold for the rite, and a white-gold for the light's heart.
constexpr float kGold[3] = {1.0f, 0.72f, 0.30f};
constexpr float kWhiteGold[3] = {1.0f, 0.90f, 0.70f};
// What the darkening takes, by channel: more of the blue than of the red, so Devias's blue snow
// goes to a warm dusk under the light rather than to a cold grey.
constexpr float kDusk[3] = {0.30f, 0.42f, 0.56f};
constexpr float kCell = 0.5f;  // a disc follows the land in cells no wider than this

float ramp(float t, float from, float to) {
    if (to <= from) return t >= to ? 1.0f : 0.0f;
    return std::clamp((t - from) / (to - from), 0.0f, 1.0f);
}
float smooth(float k) { return k * k * (3.0f - 2.0f * k); }
float easeOut(float k) { return 1.0f - (1.0f - k) * (1.0f - k); }

// The rite's envelopes. Every part is drawn off these and every one is smooth: nothing switches
// on or off in a frame (the user, 2026-10-22: "everything has to be in perfect sync without any
// shuttering or buggy looks").
float gathered(float t) { return smooth(ramp(t, 0.3f, 3.0f)); }                   // the circle's
float arrived(float t) { return smooth(ramp(t, Rite::kFalls, Rite::kLands)); }    // the light down
// Gone as the bass drops: faded out where it stands, never slid away (the user, 2026-10-22: "it
// looks like its sliding not fading out").
float released(float t) { return 1.0f - smooth(ramp(t, Rite::kLifts, Rite::kLifts + 1.6f)); }
// A hit's pulse: up in `attack`, back down over `decay`.
float pulseAt(float t, float at, float attack, float decay) {
    if (t < at) return 0.0f;
    if (t < at + attack) return smooth(ramp(t, at, at + attack));
    return 1.0f - smooth(ramp(t, at + attack, at + attack + decay));
}
// The rite sound's bass, under 180 Hz, every 0.05 s from 6.5 s: measured off content/sounds/rite.wav
// (0.1 s Hann windows, smoothed over three), 22 dB to 37 dB made 0 to 1. Two hits -- the first at
// 6.90-6.95 s, the loudest at 8.40 s -- and its tail gone by 9.85 s. The ground explosion is drawn
// off it, so it swells, peaks and dies with the low end itself (the user, 2026-10-22: "ground
// explosion has to longer and more in sync with sound").
constexpr float kBassFrom = 6.5f, kBassStep = 0.05f;
constexpr float kBass[] = {
    0.60f, 0.54f, 0.50f, 0.53f, 0.59f, 0.65f, 0.75f, 0.84f, 0.86f, 0.86f,
    0.82f, 0.76f, 0.68f, 0.52f, 0.41f, 0.45f, 0.56f, 0.67f, 0.72f, 0.72f,
    0.73f, 0.73f, 0.71f, 0.69f, 0.67f, 0.62f, 0.58f, 0.56f, 0.55f, 0.52f,
    0.51f, 0.56f, 0.61f, 0.68f, 0.78f, 0.87f, 0.92f, 0.96f, 0.97f, 0.94f,
    0.86f, 0.75f, 0.61f, 0.49f, 0.42f, 0.38f, 0.32f, 0.29f, 0.31f, 0.36f,
    0.43f, 0.47f, 0.46f, 0.45f, 0.42f, 0.39f, 0.42f, 0.44f, 0.42f, 0.37f,
    0.31f, 0.25f, 0.22f, 0.20f, 0.17f, 0.12f, 0.07f, 0.00f, 0.00f, 0.00f,
    0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
    0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f, 0.00f,
    0.00f,
};
float bassOf(float t) {
    constexpr int kCount = int(sizeof(kBass) / sizeof(kBass[0]));
    const float at = (t - kBassFrom) / kBassStep;
    if (at <= 0.0f) return kBass[0];
    if (at >= float(kCount - 1)) return kBass[kCount - 1];
    const int i = int(at);
    return kBass[i] + (kBass[i + 1] - kBass[i]) * (at - float(i));
}
constexpr float kSecondHit = 8.2f;  // the loudest bass rises here, to its peak at 8.40 s

float strikeOf(float t) { return pulseAt(t, Rite::kLands, 0.04f, 0.5f); }   // the light landing
float burstOf(float t) { return pulseAt(t, Rite::kBursts, 0.06f, 0.75f); }  // the burst
// Everything on him ends together once he has stood, whatever of the sound's tail is still playing
// (the user, 2026-10-22: "then it end it has to end all lighting for char even if the sound is
// little bit playing").
float endOf(float t) { return 1.0f - smooth(ramp(t, Rite::kEnds - 1.2f, Rite::kEnds)); }
// His own glow once the light has gone: in as it lifts, out by the end.
float afterglowOf(float t) { return smooth(ramp(t, Rite::kLifts + 0.3f, Rite::kLifts + 1.3f)) * endOf(t); }

// The light's strength: come down, rising with the plateau's swell, a pulse on the burst, gone
// back up as the bass drops.
float lightOf(float t) {
    return arrived(t) * (0.75f + 0.25f * smooth(ramp(t, 3.7f, 6.8f)) + 0.25f * burstOf(t)) * released(t);
}

// The beam's half-width at the ground, metres: a wide stage light, swelling as it strikes and on
// the burst. It narrows to 0.65 of this at the sky.
float radiusOf(float t) { return 1.7f + 0.7f * strikeOf(t) + 0.6f * burstOf(t); }

}  // namespace

bool Rite::open(const std::string& assetDir, content::Textures& textures) {
    const auto take = [&](const std::string& path) -> bgfx::TextureHandle {
        if (!core::fileExists(path)) {
            core::logError("rite: no %s (pipeline/sync.sh)", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    runes_ = take(assetDir + "/effects/rite/runes.png");
    beam_ = take(assetDir + "/effects/rite/beam.png");
    rays_ = take(assetDir + "/effects/rite/rays.png");
    pool_ = take(assetDir + "/effects/rite/pool.png");
    ring_ = take(assetDir + "/effects/rite/ring.png");
    wall_ = take(assetDir + "/effects/warp/magic_circle.png");
    flare_ = take(assetDir + "/effects/levelup/flare.png");
    return ready();
}

bool Rite::ready() const {
    return bgfx::isValid(runes_) && bgfx::isValid(beam_) && bgfx::isValid(rays_) && bgfx::isValid(pool_) && bgfx::isValid(ring_) &&
           bgfx::isValid(wall_) && bgfx::isValid(flare_);
}

void Rite::begin(const float feet[3]) {
    if (!ready()) return;
    for (int k = 0; k < 3; ++k) feet_[k] = feet[k];
    age_ = 0.0f;
    owed_ = 0.0f;
    burst_ = false;
    living_ = true;
    for (Mote& mote : motes_) mote.alive = false;
    core::logf("rite: begun at %.2f %.2f %.2f", double(feet[0]), double(feet[1]), double(feet[2]));
}

float Rite::roll(float lo, float hi) {
    dice_ = dice_ * 1664525u + 1013904223u;
    return lo + (hi - lo) * float(dice_ >> 8) / float(1u << 24);
}

void Rite::spawnMote(bool fromBody) {
    for (Mote& mote : motes_) {
        if (mote.alive) continue;
        mote = Mote{};
        mote.alive = true;
        if (fromBody) {
            const float a = roll(0.0f, 2.0f * kPi), r = roll(0.0f, 0.35f);
            mote.at[0] = feet_[0] + std::sin(a) * r;
            mote.at[1] = feet_[1] + roll(0.2f, 1.6f);
            mote.at[2] = feet_[2] + std::cos(a) * r;
            mote.velocity[1] = roll(0.25f, 0.5f);
            mote.life = roll(1.5f, 2.2f);
            mote.size = roll(0.010f, 0.020f);
        } else {
            // Out of the circle while it draws itself; out of the whole beam once it stands, a
            // dust of light drifting up through it.
            const float reach = age_ >= kLands ? radiusOf(age_) * 0.9f
                                               : std::max(0.4f, 1.5f * smooth(ramp(age_, 0.2f, 2.6f)));
            const float a = roll(0.0f, 2.0f * kPi), r = reach * std::sqrt(roll(0.0f, 1.0f));
            mote.at[0] = feet_[0] + std::sin(a) * r;
            mote.at[1] = feet_[1] + (age_ >= kLands ? roll(0.05f, 2.5f) : 0.05f);
            mote.at[2] = feet_[2] + std::cos(a) * r;
            mote.velocity[1] = roll(0.4f, 1.0f);
            mote.life = roll(1.6f, 2.6f);
            mote.size = roll(0.012f, 0.026f);
        }
        mote.left = mote.life;
        return;
    }
}

void Rite::burst() {
    burst_ = true;
    // Every mote still rising is blown outward to live out its life -- cut short they blinked
    // out -- and the sparks are thrown from his chest.
    for (Mote& mote : motes_) {
        if (!mote.alive || mote.falls) continue;
        const float dx = mote.at[0] - feet_[0], dz = mote.at[2] - feet_[2];
        const float apart = std::max(0.05f, std::sqrt(dx * dx + dz * dz));
        mote.velocity[0] += dx / apart * 2.5f;
        mote.velocity[2] += dz / apart * 2.5f;
    }
    for (int n = 0; n < 160; ++n) {
        for (Mote& spark : motes_) {
            if (spark.alive) continue;
            spark = Mote{};
            spark.alive = true;
            spark.falls = true;
            const float a = roll(0.0f, 2.0f * kPi), speed = roll(2.5f, 6.0f);
            spark.at[0] = feet_[0];
            spark.at[1] = feet_[1] + 1.0f;
            spark.at[2] = feet_[2];
            spark.velocity[0] = std::sin(a) * speed;
            spark.velocity[1] = roll(1.5f, 5.0f);
            spark.velocity[2] = std::cos(a) * speed;
            spark.life = spark.left = roll(1.0f, 2.0f);
            spark.size = roll(0.014f, 0.032f);
            break;
        }
    }
}

void Rite::update(float seconds) {
    if (!living_) return;
    age_ += seconds;
    if (!burst_ && age_ >= kBursts) burst();
    // Motes through the swell, more as it builds; from his body after.
    float rate = 0.0f;
    bool fromBody = false;
    if (age_ >= 0.5f && age_ < kBursts) rate = 12.0f + 33.0f * ramp(age_, 0.5f, 6.5f);
    if (age_ >= kLifts && age_ < kEnds - 1.2f) rate = 4.0f * afterglowOf(age_), fromBody = true;
    owed_ += rate * seconds;
    while (owed_ >= 1.0f) {
        owed_ -= 1.0f;
        spawnMote(fromBody);
    }
    bool any = false;
    for (Mote& mote : motes_) {
        if (!mote.alive) continue;
        mote.left -= seconds;
        if (mote.left <= 0.0f) {
            mote.alive = false;
            continue;
        }
        any = true;
        if (mote.falls) {
            mote.velocity[1] -= 9.8f * seconds;
            for (int k = 0; k < 3; ++k) mote.at[k] += mote.velocity[k] * seconds;
            if (mote.at[1] < feet_[1] + 0.03f) {
                mote.at[1] = feet_[1] + 0.03f;
                mote.velocity[1] = -mote.velocity[1] * 0.3f;
                mote.velocity[0] *= 0.6f;
                mote.velocity[2] *= 0.6f;
            }
            continue;
        }
        // While the light stands the motes turn slowly about it.
        if (age_ >= kLands && age_ < kBursts) {
            const float dx = mote.at[0] - feet_[0], dz = mote.at[2] - feet_[2];
            mote.velocity[0] += (-dx * 0.6f + dz * 0.9f) * seconds;
            mote.velocity[2] += (-dz * 0.6f - dx * 0.9f) * seconds;
        }
        for (int k = 0; k < 3; ++k) mote.at[k] += mote.velocity[k] * seconds;
    }
    if (age_ >= kEnds && !any) living_ = false;
}

void Rite::gatherDisc(gfx::Effects& effects, const content::Ground& ground, bgfx::TextureHandle sheet,
                      gfx::Blend blend, float radius, float turn, const float colour[3],
                      float alpha) const {
    if (radius <= 0.01f || alpha <= 0.002f) return;
    const float across = radius * 2.0f;
    const int cells = std::clamp(int(std::ceil(across / kCell)), 1, 14);
    const float lift = 0.05f;
    const float c = std::cos(turn), s = std::sin(turn);
    const auto place = [&](float u, float v, float* out) {
        const float x = feet_[0] + u * across;
        const float z = feet_[2] + v * across;
        out[0] = x;
        out[1] = ground.heightAt(x, z) + lift;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = sheet;
    sprite.blend = blend;
    for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k] * alpha;
    sprite.colour[3] = 1.0f;
    const float inv = 1.0f / float(cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float u0 = float(i) * inv - 0.5f, u1 = u0 + inv;
            const float v0 = float(j) * inv - 0.5f, v1 = v0 + inv;
            const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            for (int k = 0; k < 4; ++k) {
                place(uv[k][0], uv[k][1], sprite.corner[k]);
                // The sheet turned under the disc, about its middle.
                const float u = uv[k][0] * c - uv[k][1] * s, v = uv[k][0] * s + uv[k][1] * c;
                sprite.cornerUv[k][0] = u + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - v;
            }
            place((u0 + u1) * 0.5f, (v0 + v1) * 0.5f, sprite.position);
            // The pass sorts every quad by its own point, back to front. A darkening sharing its
            // cells with the light came out in blocks, under the light in some and over it in
            // others; sorted from well under the ground it is always drawn first. The added
            // ones do not care in which order they land.
            // Far under it: a wave's cells are wider than the darkening's, and from 3 m under one of
            // them still sorted after the light beside it and laid a dark square over it.
            if (blend == gfx::Blend::Minus) sprite.position[1] -= 60.0f;
            effects.add(sprite);
        }
    }
}

void Rite::gatherWalls(gfx::Effects& effects, float turn, float alpha) const {
    if (alpha <= 0.002f) return;
    constexpr int kSides = 12, kBands = 4;
    constexpr float kFoot = 1.35f, kHead = 1.55f, kTall = 1.3f;
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = wall_;
    sprite.blend = gfx::Blend::Additive;
    sprite.colour[3] = 1.0f;
    const float step = 2.0f * kPi / float(kSides);
    const auto at = [&](float a, float t, float* out) {
        const float radius = kFoot + (kHead - kFoot) * t;
        out[0] = feet_[0] + std::sin(a) * radius;
        out[1] = feet_[1] + kTall * t;
        out[2] = feet_[2] + std::cos(a) * radius;
    };
    for (int side = 0; side < kSides; ++side) {
        const float a0 = float(side) * step + turn, a1 = a0 + step;
        const float u0 = float(side) / float(kSides), u1 = float(side + 1) / float(kSides);
        for (int band = 0; band < kBands; ++band) {
            const float t0 = float(band) / float(kBands), t1 = float(band + 1) / float(kBands);
            const float light = (1.0f - (t0 + t1) * 0.5f) * alpha;
            for (int d = 0; d < 3; ++d) sprite.colour[d] = kGold[d] * light;
            at(a0, t0, sprite.corner[0]);
            at(a1, t0, sprite.corner[1]);
            at(a1, t1, sprite.corner[2]);
            at(a0, t1, sprite.corner[3]);
            const float uv[4][2] = {{u0, 1.0f - t0}, {u1, 1.0f - t0}, {u1, 1.0f - t1}, {u0, 1.0f - t1}};
            for (int k = 0; k < 4; ++k) {
                sprite.cornerUv[k][0] = uv[k][0];
                sprite.cornerUv[k][1] = uv[k][1];
            }
            for (int d = 0; d < 3; ++d) sprite.position[d] = (sprite.corner[0][d] + sprite.corner[2][d]) * 0.5f;
            effects.add(sprite);
        }
    }
}

// The light from heaven: one tall quad stood on its own axis and turned to the eye, so it is the
// same column from wherever it is seen, wider at the ground than at the sky. Three of them over
// one another: a wide soft haze, the beam's body, and its shafts shimmering slowly.
void Rite::gatherBeam(gfx::Effects& effects, const float eye[3]) const {
    const float t = age_;
    if (t < kFalls || released(t) <= 0.0f) return;
    const float top = feet_[1] + kSky;
    float bottom = feet_[1];
    if (t < kLands) {
        const float k = ramp(t, kFalls, kLands);
        bottom = top - kSky * k * k;  // falling, faster as it comes
    }
    const float light = lightOf(t);
    if (light <= 0.002f || top - bottom < 0.05f) return;
    float side[2] = {eye[2] - feet_[2], -(eye[0] - feet_[0])};
    const float length = std::sqrt(side[0] * side[0] + side[1] * side[1]);
    if (length < 1e-4f) side[0] = 1.0f, side[1] = 0.0f;
    else side[0] /= length, side[1] /= length;
    // Dimming, it draws in a little, as a stage light does when it is taken down.
    const float wide = radiusOf(t) * (0.8f + 0.2f * released(t));
    const auto layer = [&](bgfx::TextureHandle sheet, float scale, const float tint[3], float strength) {
        const float foot = wide * scale, head = wide * scale * 0.65f;
        gfx::Sprite quad;
        quad.placed = true;
        quad.sheet = sheet;
        quad.blend = gfx::Blend::Additive;
        const float corners[4][3] = {{feet_[0] - side[0] * foot, bottom, feet_[2] - side[1] * foot},
                                     {feet_[0] + side[0] * foot, bottom, feet_[2] + side[1] * foot},
                                     {feet_[0] + side[0] * head, top, feet_[2] + side[1] * head},
                                     {feet_[0] - side[0] * head, top, feet_[2] - side[1] * head}};
        const float uv[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
        for (int k = 0; k < 4; ++k) {
            for (int d = 0; d < 3; ++d) quad.corner[k][d] = corners[k][d];
            quad.cornerUv[k][0] = uv[k][0];
            quad.cornerUv[k][1] = uv[k][1];
        }
        quad.position[0] = feet_[0];
        quad.position[1] = (bottom + top) * 0.5f;
        quad.position[2] = feet_[2];
        for (int d = 0; d < 3; ++d) quad.colour[d] = tint[d] * strength * light;
        quad.colour[3] = 1.0f;
        effects.add(quad);
    };
    layer(beam_, 1.8f, kGold, 0.05f);
    layer(beam_, 1.0f, kWhiteGold, 0.16f);
    layer(rays_, 1.0f, kWhiteGold, 0.22f * (0.6f + 0.4f * std::sin(t * 1.3f)));
    layer(rays_, 0.8f, kGold, 0.22f * (0.6f + 0.4f * std::sin(t * 1.7f + 2.0f)));
    // The strike: a soft flash at his feet as it lands.
    if (const float k = strikeOf(t); k > 0.002f) {
        gfx::Sprite flash;
        flash.sheet = flare_;
        flash.blend = gfx::Blend::Additive;
        flash.position[0] = feet_[0];
        flash.position[1] = feet_[1] + 0.4f;
        flash.position[2] = feet_[2];
        flash.halfWidth = flash.halfHeight = 1.2f + 1.8f * k;
        for (int d = 0; d < 3; ++d) flash.colour[d] = kWhiteGold[d] * 0.9f * k;
        flash.colour[3] = 1.0f;
        effects.add(flash);
    }
}

uint32_t Rite::lights(gfx::PointLight* out, uint32_t max) const {
    if (!living_ || out == nullptr || max == 0) return 0;
    const float t = age_;
    // The circle's warm glow handing over to the light from above, the burst's pulse on top, his
    // own glow after: one light, its strength and height continuous throughout.
    const float quake = t >= kBursts ? bassOf(t) * smooth(ramp(t, kBursts, kBursts + 0.06f)) : 0.0f;
    const float strength = 0.45f * gathered(t) * (1.0f - arrived(t)) + 1.1f * lightOf(t) + 0.7f * burstOf(t) +
                           0.3f * quake + 0.55f * afterglowOf(t);
    const float high = 1.0f + 1.2f * arrived(t) * released(t);  // metres over his feet it hangs
    if (strength <= 0.01f) return 0;
    gfx::PointLight& light = out[0];
    light = gfx::PointLight{};
    light.position[0] = feet_[0];
    light.position[1] = feet_[1] + high;
    light.position[2] = feet_[2];
    light.reach = 2.6f;  // him and the circle, not the square: on snow more is a whiteout
    light.height = high;
    light.wrap = 0.6f;  // round him: a figure in a pool of light is lit on every side
    for (int k = 0; k < 3; ++k) light.colour[k] = kWhiteGold[k] * strength;
    return 1;
}

void Rite::gather(gfx::Effects& effects, const content::Ground& ground, const float eye[3]) const {
    if (!living_) return;
    const float t = age_;
    // The ground goes to dusk around him: MU's Minus blend, what is behind times one less the
    // sheet, taking more blue than red.
    {
        const float dark = 0.8f * gathered(t) * (1.0f - smooth(ramp(t, kLifts, kLifts + 2.0f)));
        gatherDisc(effects, ground, flare_, gfx::Blend::Minus, 7.0f, 0.0f, kDusk, dark);
    }
    // Gold under him: the rune circles, drawn out of nothing, brightening with the swell and
    // flaring on the burst; and the light's own pool where it stands.
    {
        const float bright = (0.5f + 0.35f * smooth(ramp(t, kFalls, 6.8f)) + 0.3f * strikeOf(t) + 0.6f * burstOf(t)) *
                             (1.0f - smooth(ramp(t, kBursts + 0.2f, 9.5f)));
        gatherDisc(effects, ground, runes_, gfx::Blend::Additive, 1.5f * smooth(ramp(t, 0.2f, 2.6f)),
                   t * 0.35f, kGold, bright);
        gatherDisc(effects, ground, runes_, gfx::Blend::Additive, 2.4f * smooth(ramp(t, 1.2f, 3.4f)),
                   -t * 0.2f, kGold, bright * 0.45f);
        // Before the light: a faint warm glow gathering in the circle.
        gatherDisc(effects, ground, pool_, gfx::Blend::Additive, 1.3f, 0.0f, kGold,
                   0.18f * gathered(t) * (1.0f - arrived(t)));
        gatherDisc(effects, ground, pool_, gfx::Blend::Additive, radiusOf(t) * 1.05f, 0.0f, kGold, 0.55f * lightOf(t));
    }
    // The walls rise out of the circle and go with the burst.
    {
        const float alpha = 0.5f * smooth(ramp(t, 1.5f, 3.0f)) * (1.0f - smooth(ramp(t, kBursts, kBursts + 0.5f)));
        gatherWalls(effects, t * 1.2f, alpha);
        gatherWalls(effects, -t * 1.2f, alpha * 0.7f);
    }
    gatherBeam(effects, eye);
    // The ground explosion, two gold waves rolling out over the ground on the sound's two bass hits,
    // each as bright as the bass is at that moment, the glow under both swelling with it.
    if (t >= kBursts) {
        const float in = smooth(ramp(t, kBursts, kBursts + 0.06f));  // in over two frames, not one
        const float first = ramp(t, kBursts, kBursts + 1.5f);
        gatherDisc(effects, ground, ring_, gfx::Blend::Additive, 1.5f + 6.5f * easeOut(first), 0.0f, kGold,
                   0.8f * in * bassOf(t) * (1.0f - smooth(ramp(t, 7.9f, 8.5f))));
        if (t >= kSecondHit) {
            const float second = ramp(t, kSecondHit, 9.9f);
            gatherDisc(effects, ground, ring_, gfx::Blend::Additive, 1.2f + 9.0f * easeOut(second), 0.0f, kGold,
                       0.9f * smooth(ramp(t, kSecondHit, kSecondHit + 0.12f)) * bassOf(t));
        }
        gatherDisc(effects, ground, pool_, gfx::Blend::Additive, 1.5f + 4.0f * easeOut(ramp(t, kBursts, 9.5f)), 0.0f,
                   kGold, 0.12f * in * bassOf(t));
    }
    // The burst: a flash at his chest.
    if (t >= kBursts && t < kBursts + 1.0f) {
        const float in = smooth(ramp(t, kBursts, kBursts + 0.06f));
        if (t < kBursts + 0.4f) {
            const float f = ramp(t, kBursts, kBursts + 0.4f);
            gfx::Sprite flash;
            flash.sheet = flare_;
            flash.blend = gfx::Blend::Additive;
            flash.position[0] = feet_[0];
            flash.position[1] = feet_[1] + 1.0f;
            flash.position[2] = feet_[2];
            flash.halfWidth = flash.halfHeight = 1.0f + 3.0f * f;
            for (int d = 0; d < 3; ++d) flash.colour[d] = kWhiteGold[d] * 0.45f * in * (1.0f - f) * (1.0f - f);
            flash.colour[3] = 1.0f;
            effects.add(flash);
        }
    }
    // Reborn: a gold glow on him, fading out.
    if (t >= kLifts) {
        const float a = 0.3f * afterglowOf(t) * (0.85f + 0.15f * std::sin(t * 3.0f));
        if (a > 0.002f) {
            gfx::Sprite glow;
            glow.sheet = flare_;
            glow.blend = gfx::Blend::Additive;
            glow.position[0] = feet_[0];
            glow.position[1] = feet_[1] + 1.0f;
            glow.position[2] = feet_[2];
            glow.halfWidth = glow.halfHeight = 0.8f;
            for (int d = 0; d < 3; ++d) glow.colour[d] = kGold[d] * a;
            glow.colour[3] = 1.0f;
            effects.add(glow);
            gatherDisc(effects, ground, pool_, gfx::Blend::Additive, 0.9f, 0.0f, kGold, a * 0.7f);
        }
    }
    // The motes and the sparks: small points of light.
    gfx::Sprite speck;
    speck.sheet = flare_;
    speck.blend = gfx::Blend::Additive;
    speck.colour[3] = 1.0f;
    for (const Mote& mote : motes_) {
        if (!mote.alive) continue;
        const float k = 1.0f - mote.left / mote.life;
        const float a = mote.falls ? (1.0f - k) * 1.2f : std::sin(kPi * k) * endOf(t);
        for (int d = 0; d < 3; ++d) speck.position[d] = mote.at[d];
        speck.halfWidth = speck.halfHeight = mote.size * 1.6f;
        const float* tint = mote.falls ? kWhiteGold : kGold;
        for (int d = 0; d < 3; ++d) speck.colour[d] = tint[d] * a;
        effects.add(speck);
    }
}

}  // namespace mu::game
