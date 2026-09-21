#include "game/marker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "content/ground.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// MU2's clock: every number below is per reference frame at MU's 25 a second
// (Marker.cs:57-177), and a frame's real seconds are turned into these once, in update().
constexpr float kReferenceFps = 25.0f;

// The whole marker lives 30 frames, 1.2 s, and spends its last 10 fading, accelerating
// (alpha is the square of what is left). Marker.cs LifeFrames, FadeFrames.
constexpr float kLifeFrames = 30.0f;
constexpr float kFadeFrames = 10.0f;

// The rings: born 0.72 m across, closing 0.024 m a frame, retired at 0.12, a new one every 15
// frames, three at most. MU2 scaled MU's own sizes by 0.6 (OfClientSize) and says so.
constexpr float kRingBorn = 1.2f * 0.6f;
constexpr float kRingGone = 0.2f * 0.6f;
constexpr float kRingShrink = 0.04f * 0.6f;
constexpr float kRingEvery = 15.0f;

// The discs: 0.8 m and 1.2 times that, turning 2 degrees a frame opposite ways, shown for the
// first 24 frames. Not scaled by 0.6 in MU2, and not here.
constexpr float kDiscAcross = 0.8f;
constexpr float kDiscOuter = 1.2f;
constexpr float kDiscTurn = 2.0f;
constexpr float kDiscFrames = 24.0f;

// The pulse: 1.8 m closing to 0.8 and opening again at 0.15 a frame, hard at both ends.
constexpr float kPulseWidest = 1.8f;
constexpr float kPulseNarrowest = 0.8f;
constexpr float kPulseStep = 0.15f;

// The pin: MU units to metres at index.json's 0.6 scale, as MU2's loader did (v * 0.6 / 100,
// z negated), which stands it 0.37 to 1.16 m over the spot.
constexpr float kPinScale = 0.6f / 100.0f;

// Invention: the pin falls onto the spot from 0.4 m higher over 0.15 s, easing out, and turns
// at a quarter turn a second. MU2's pin stood still, and a still additive arrow reads as a
// sticker; the drop is what says "here" at the moment of the click.
constexpr float kPinDrop = 0.4f;
constexpr float kPinDropSeconds = 0.15f;
constexpr float kPinTurn = 1.5707963f;

// Invention: on arrival the marker fades in this long, where MU2 cut it on the frame.
constexpr float kLeaveSeconds = 0.15f;

// How far over the land the flat parts float, and how fine the grid they follow it with is.
// MU2 floated one flat quad 3 cm over the centre's height. The land here is bilinear between
// tile corners and drawn as two triangles a tile, which disagree by a centimetre or two across
// a slope; 4 cm is over that and under anything an eye can see at MU's camera.
constexpr float kLift = 0.04f;
constexpr float kCell = 0.3f;

// MU2's tint, Marker.cs Light: MU creates the effect yellow and overwrites it on the first
// frame with this.
constexpr float kTint[3] = {1.0f, 0.7f, 0.3f};

}  // namespace

bool Marker::open(const std::string& assetDir, content::Textures& textures) {
    const std::string dir = assetDir + "/effects/movetarget/";
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const std::string path = dir + name;
        if (!core::fileExists(path)) {
            core::logError("marker: no %s (tools/sync.sh)", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    rings_ = take("cursorpin01.png");
    discs_ = take("empact01.png");
    pulse_ = take("cursorpin02.png");
    pinSheet_ = take("gra.png");

    // The pin, read straight off MU2's export. Sixty-three corners: not worth a cook step.
    pin_.clear();
    const std::vector<uint8_t> bytes = core::readFile(dir + "MoveTargetPosEffect.obj");
    std::vector<float> at, uv;
    std::string line;
    const auto corner = [&](const char* token) {
        int v = 0, t = 0;
        if (std::sscanf(token, "%d/%d", &v, &t) < 1) return;
        if (v <= 0 || size_t(v) * 3 > at.size()) return;
        Corner c;
        c.x = at[size_t(v - 1) * 3 + 0] * kPinScale;
        c.y = at[size_t(v - 1) * 3 + 1] * kPinScale;
        c.z = -at[size_t(v - 1) * 3 + 2] * kPinScale;
        // OBJ's v runs up the picture and a sheet's runs down it.
        c.u = t > 0 && size_t(t) * 2 <= uv.size() ? uv[size_t(t - 1) * 2] : 0.5f;
        c.v = t > 0 && size_t(t) * 2 <= uv.size() ? 1.0f - uv[size_t(t - 1) * 2 + 1] : 0.5f;
        pin_.push_back(c);
    };
    for (size_t i = 0; i <= bytes.size(); ++i) {
        if (i < bytes.size() && bytes[i] != '\n') {
            line.push_back(char(bytes[i]));
            continue;
        }
        float a = 0, b = 0, c = 0;
        char p[3][32];
        if (std::sscanf(line.c_str(), "v %f %f %f", &a, &b, &c) == 3) {
            at.insert(at.end(), {a, b, c});
        } else if (std::sscanf(line.c_str(), "vt %f %f", &a, &b) == 2) {
            uv.insert(uv.end(), {a, b});
        } else if (std::sscanf(line.c_str(), "f %31s %31s %31s", p[0], p[1], p[2]) == 3) {
            const size_t before = pin_.size();
            for (auto& token : p) corner(token);
            if (pin_.size() != before + 3) pin_.resize(before);
        }
        line.clear();
    }
    core::logf("marker: rings %s, discs %s, pulse %s, pin %zu triangles",
               bgfx::isValid(rings_) ? "in hand" : "MISSING",
               bgfx::isValid(discs_) ? "in hand" : "MISSING",
               bgfx::isValid(pulse_) ? "in hand" : "MISSING", pin_.size() / 3);
    return bgfx::isValid(rings_) || bgfx::isValid(discs_) || bgfx::isValid(pulse_) ||
           !pin_.empty();
}

void Marker::shutdown() {
    // The sheets belong to Textures, which destroys them.
    pin_.clear();
    live_ = false;
}

void Marker::show(float x, float z) {
    live_ = true;
    x_ = x;
    z_ = z;
    lived_ = 0.0f;
    leaving_ = -1.0f;
    seconds_ = 0.0f;
    // The first ring is born on the click's own frame (Marker.cs ShowAt: sinceLast = 15).
    sinceRing_ = kRingEvery;
    std::fill(std::begin(ring_), std::end(ring_), 0.0f);
    turned_ = 0.0f;
    pulseAcross_ = kPulseWidest;
    pulseOpening_ = false;
}

void Marker::dismiss() {
    if (live_ && leaving_ < 0.0f) leaving_ = kLeaveSeconds;
}

void Marker::update(float seconds) {
    if (!live_) return;
    const float step = seconds * kReferenceFps;
    lived_ += step;
    seconds_ += seconds;
    if (leaving_ >= 0.0f) {
        leaving_ -= seconds;
        if (leaving_ <= 0.0f) {
            live_ = false;
            return;
        }
    }
    if (lived_ >= kLifeFrames) {
        live_ = false;
        return;
    }
    turned_ += kDiscTurn * step;
    pulseAcross_ += (pulseOpening_ ? kPulseStep : -kPulseStep) * step;
    if (pulseAcross_ <= kPulseNarrowest) {
        pulseAcross_ = kPulseNarrowest;
        pulseOpening_ = true;
    } else if (pulseAcross_ >= kPulseWidest) {
        pulseAcross_ = kPulseWidest;
        pulseOpening_ = false;
    }

    for (float& across : ring_) {
        if (across <= 0.0f) continue;
        across -= kRingShrink * step;
        if (across <= kRingGone) across = 0.0f;
    }
    sinceRing_ += step;
    if (sinceRing_ >= kRingEvery) {
        sinceRing_ = 0.0f;
        for (float& across : ring_) {
            if (across <= 0.0f) {
                across = kRingBorn;
                break;
            }
        }
    }
}

void Marker::lay(gfx::Effects& effects, const content::Ground& ground, bgfx::TextureHandle sheet,
                 float across, float turn, float alpha) const {
    if (!bgfx::isValid(sheet) || across <= 0.0f || alpha <= 0.0f) return;
    const int cells = std::clamp(int(std::ceil(across / kCell)), 1, 6);
    const float c = std::cos(turn), s = std::sin(turn);
    // A point on the square, in its own units from -0.5 to 0.5, onto the land.
    const auto place = [&](float a, float b, float* out) {
        const float x = x_ + (a * c - b * s) * across;
        const float z = z_ + (a * s + b * c) * across;
        out[0] = x;
        out[1] = ground.heightAt(x, z) + kLift;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = sheet;
    sprite.blend = gfx::Blend::Additive;
    sprite.colour[0] = kTint[0];
    sprite.colour[1] = kTint[1];
    sprite.colour[2] = kTint[2];
    sprite.colour[3] = alpha;
    const float inv = 1.0f / float(cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float a0 = float(i) * inv - 0.5f, a1 = a0 + inv;
            const float b0 = float(j) * inv - 0.5f, b1 = b0 + inv;
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

void Marker::gather(gfx::Effects& effects, const content::Ground& ground) const {
    if (!live_) return;
    // MU2's accelerating fade over the last ten frames, and the arrival's on top of it.
    float alpha = 1.0f;
    const float left = kLifeFrames - lived_;
    if (left <= kFadeFrames) {
        const float d = std::max(0.0f, left / kFadeFrames);
        alpha = d * d;
    }
    if (leaving_ >= 0.0f) alpha *= std::max(0.0f, leaving_ / kLeaveSeconds);
    if (alpha <= 0.0f) return;

    constexpr float kToRadians = 3.14159265f / 180.0f;
    // Under the pin, in MU2's order: the discs and the pulse, then the rings.
    if (lived_ < kDiscFrames) {
        lay(effects, ground, discs_, kDiscAcross, -turned_ * kToRadians, alpha);
        lay(effects, ground, discs_, kDiscAcross * kDiscOuter, turned_ * kToRadians, alpha);
    }
    lay(effects, ground, pulse_, pulseAcross_, 0.0f, alpha);
    for (float across : ring_) lay(effects, ground, rings_, across, 0.0f, alpha);

    if (pin_.empty() || !bgfx::isValid(pinSheet_)) return;
    const float t = std::min(1.0f, seconds_ / kPinDropSeconds);
    const float fall = (1.0f - t) * (1.0f - t) * (1.0f - t) * kPinDrop;
    const float y = ground.heightAt(x_, z_) + fall;
    const float spin = seconds_ * kPinTurn;
    const float c = std::cos(spin), s = std::sin(spin);
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = pinSheet_;
    sprite.blend = gfx::Blend::Additive;
    sprite.colour[0] = kTint[0];
    sprite.colour[1] = kTint[1];
    sprite.colour[2] = kTint[2];
    sprite.colour[3] = alpha;
    for (size_t i = 0; i + 2 < pin_.size(); i += 3) {
        // A triangle is a quad whose last two corners are one point.
        for (int k = 0; k < 4; ++k) {
            const Corner& p = pin_[i + size_t(std::min(k, 2))];
            sprite.corner[k][0] = x_ + p.x * c - p.z * s;
            sprite.corner[k][1] = y + p.y;
            sprite.corner[k][2] = z_ + p.x * s + p.z * c;
            sprite.cornerUv[k][0] = p.u;
            sprite.cornerUv[k][1] = p.v;
        }
        for (int a = 0; a < 3; ++a) {
            sprite.position[a] =
                (sprite.corner[0][a] + sprite.corner[1][a] + sprite.corner[2][a]) / 3.0f;
        }
        effects.add(sprite);
    }
}

}  // namespace mu::game
