#include "game/fx/electric_spike.h"

#include <algorithm>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

namespace {

constexpr float kDegree = 3.14159265358979f / 180.0f;

// MU's AngleMatrix (ZzzMathLib.cpp:194): angles[2] the yaw about Z, [1] about Y, [0] about X.
void angleMatrix(const float angles[3], float m[3][3]) {
    const float sy = std::sin(angles[2] * kDegree), cy = std::cos(angles[2] * kDegree);
    const float sp = std::sin(angles[1] * kDegree), cp = std::cos(angles[1] * kDegree);
    const float sr = std::sin(angles[0] * kDegree), cr = std::cos(angles[0] * kDegree);
    m[0][0] = cp * cy;
    m[1][0] = cp * sy;
    m[2][0] = -sp;
    m[0][1] = sr * sp * cy - cr * sy;
    m[1][1] = sr * sp * sy + cr * cy;
    m[2][1] = sr * cp;
    m[0][2] = cr * sp * cy + sr * sy;
    m[1][2] = cr * sp * sy - sr * cy;
    m[2][2] = cr * cp;
}

// A fixed scatter for a zap's frame, so drawing it twice draws the same bolt.
float hashUnit(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9e3779b1u ^ (b + 0x7f4a7c15u) * 0x85ebca6bu;
    h ^= h >> 15;
    h *= 0x2c1b3c6du;
    h ^= h >> 12;
    return float(h & 0xFFFFFFu) / float(0x1000000u);
}

}  // namespace

float ElectricSpike::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool ElectricSpike::open(const std::string& assetDir, content::Textures& textures,
                         const content::Ground* ground) {
    ground_ = ground;
    const auto sheet = [&](const std::string& path, content::TextureRole role) {
        return core::fileExists(assetDir + path) ? textures.load(assetDir + path, role)
                                                 : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    // An albedo, so it wraps: the sheet runs along the bolt.
    boltSheet_ = sheet("/effects/thunder/joint_thunder01.png", content::TextureRole::Albedo);
    starSheet_ = sheet("/effects/summon/shiny02.png", content::TextureRole::Decal);
    ringSheet_ = sheet("/effects/fire_burst/KingS_R.png", content::TextureRole::Decal);
    ring_.clear();
    const bool ok = loadEffectObj(assetDir + "/effects/fire_burst/DarkLordSkill.obj", 1.0f, "", ring_) &&
                    bgfx::isValid(boltSheet_) && bgfx::isValid(starSheet_) && bgfx::isValid(ringSheet_);
    core::logf("electric spike: bolt, stars, rings and sheets %s", ok ? "yes" : "NO");
    return ok;
}

void ElectricSpike::clear() {
    for (Charge& one : charges_) one = Charge{};
    for (Gather& one : gathers_) one = Gather{};
    for (Zap& one : zaps_) one = Zap{};
    for (Joint& one : joints_) one = Joint{};
    for (Ring& one : rings_) one = Ring{};
}

void ElectricSpike::begin(uint32_t who, float clipSeconds) {
    Charge* slot = nullptr;
    for (Charge& one : charges_) {
        if (one.alive && one.who == who) {
            slot = &one;
            break;
        }
        if (!one.alive && !slot) slot = &one;
    }
    if (!slot) return;
    *slot = Charge{};
    slot->alive = true;
    slot->who = who;
    // The clip's keys over the clip as it is actually played.
    slot->rate = clipSeconds > 0.0f ? kClipFrames / (clipSeconds * kReferenceFps) : 1.0f;
}

void ElectricSpike::gather(uint32_t who, const float hand[3]) {
    for (Gather& one : gathers_) {
        if (one.alive) continue;
        one = Gather{};
        one.alive = true;
        one.who = who;
        for (int k = 0; k < 3; ++k) one.at[k] = hand[k];
        gatherFrame(one);
        return;
    }
}

void ElectricSpike::gatherFrame(Gather& one) {
    const float metre = perUnit();
    for (int j = 0; j < 3; ++j) {
        one.star[j][0] = float(int(unit() * 8.0f) + 8) * 0.2f;
        one.star[j][1] = between(0.0f, 6.2831853f);
        // Every other frame a zap from 120 units out, any way round, into the hand.
        if ((int(one.left) % 2) != 0) continue;
        for (Zap& zap : zaps_) {
            if (zap.alive) continue;
            const float yaw = between(0.0f, 6.2831853f), pitch = between(-1.5707963f, 1.5707963f);
            zap = Zap{};
            zap.alive = true;
            zap.from[0] = one.at[0] + std::cos(pitch) * std::cos(yaw) * 120.0f * metre;
            zap.from[1] = one.at[1] + std::sin(pitch) * 120.0f * metre;
            zap.from[2] = one.at[2] + std::cos(pitch) * std::sin(yaw) * 120.0f * metre;
            for (int k = 0; k < 3; ++k) zap.to[k] = one.at[k];
            zap.seed = dice_;
            break;
        }
    }
    one.left -= 1.0f;
    if (one.left <= 0.0f) one.alive = false;
}

void ElectricSpike::loose(uint32_t who, const float feet[3], float way) {
    const float ahead[3] = {std::cos(way), 0.0f, -std::sin(way)};
    const float across[3] = {ahead[2], 0.0f, -ahead[0]};
    for (Charge& one : charges_) {
        if (!one.alive || one.who != who) continue;
        for (int k = 0; k < 3; ++k) one.way[k] = ahead[k];
    }
    // The main joint, then the four that coil: 1 and 2 late by two to four frames.
    for (int kind = 0; kind <= 4; ++kind) {
        for (Joint& one : joints_) {
            if (one.alive) continue;
            one = Joint{};
            one.alive = true;
            one.kind = kind;
            for (int k = 0; k < 3; ++k) {
                one.origin[k] = feet[k];
                one.ahead[k] = ahead[k];
                one.across[k] = across[k];
            }
            one.start[1] = -kAhead;
            one.start[2] = kUp;
            for (int k = 0; k < 3; ++k) one.at[k] = one.start[k];
            if (kind == 0) {
                one.width = 250.0f;
            } else {
                one.width = 100.0f;
                one.turn = (kind == 2 || kind == 4) ? -180.0f : 180.0f;
                one.wait = kind <= 2 ? float(int(unit() * 3.0f) + 2) : 0.0f;
                one.left += one.wait;
                // Set off on its circle: (0, 0, 80) turned by (80, +-180, 0), as made.
                const float angles[3] = {80.0f, one.turn, 0.0f};
                float m[3][3];
                angleMatrix(angles, m);
                for (int k = 0; k < 3; ++k) one.at[k] = one.start[k] + m[k][2] * 80.0f;
            }
            break;
        }
    }
}

void ElectricSpike::step(Joint& one) {
    if (one.wait > 0.0f) {
        one.wait -= 1.0f;
    } else if (one.tails < kTails - 1) {
        for (int i = 1; float(i) < one.steps && one.tails < kTails - 1; ++i) {
            for (int k = 0; k < 3; ++k) one.tail[one.tails][k] = one.at[k];
            ++one.tails;
            one.start[1] += one.velocity;  // the velocity is negative: down his line
            if (one.kind == 0) {
                for (int k = 0; k < 3; ++k) one.at[k] = one.start[k];
            } else {
                // Round his line: (0, 0, radius) turned about it by `turn`; 3 and 4 the mirror.
                one.turn -= 20.0f;
                const float off[3] = {one.radius * std::sin(one.turn * kDegree), 0.0f,
                                      one.radius * std::cos(one.turn * kDegree)};
                const float sign = one.kind >= 3 ? -1.0f : 1.0f;
                for (int k = 0; k < 3; ++k) one.at[k] = one.start[k] + sign * off[k];
                one.radius -= 2.5f;
            }
            one.velocity -= 2.0f;
        }
        one.steps += 2.0f;
    }
    if (one.left < 10.0f) one.light /= 1.3f;
    one.left -= 1.0f;
    if (one.left <= 0.0f) one.alive = false;
}

void ElectricSpike::jointWorld(const Joint& one, const float mu[3], float out[3]) const {
    const float metre = perUnit();
    for (int k = 0; k < 3; ++k) {
        out[k] = one.origin[k] + (one.across[k] * mu[0] - one.ahead[k] * mu[1]) * metre;
    }
    out[1] += mu[2] * metre;
}

void ElectricSpike::ring(const float hand[3], const float way[3]) {
    // (180, 45, 0) and (0, 0, his facing), laid on his line.
    const float angles[2][2] = {{180.0f, 45.0f}, {0.0f, 0.0f}};
    for (const auto& angle : angles) {
        for (Ring& one : rings_) {
            if (one.alive) continue;
            one = Ring{};
            one.alive = true;
            for (int k = 0; k < 3; ++k) {
                one.at[k] = hand[k];
                one.ahead[k] = way[k];
            }
            one.across[0] = way[2];
            one.across[1] = 0.0f;
            one.across[2] = -way[0];
            one.pitch = angle[0];
            one.roll = angle[1];
            break;
        }
    }
}

void ElectricSpike::ribbon(gfx::Effects& effects, const float (*points)[3], int count, float width,
                           const float colour[3], float scroll) const {
    if (count < 2) return;
    const float up[3] = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i + 1 < count; ++i) {
        const float* a = points[i];
        const float* b = points[i + 1];
        float along[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float length = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        if (length < 1e-4f) continue;
        for (float& c : along) c /= length;
        // Across the segment flat, and across it upright: a cross of two bands, as a tail is.
        float flat[3] = {along[2], 0.0f, -along[0]};
        const float flatLength = std::sqrt(flat[0] * flat[0] + flat[2] * flat[2]);
        if (flatLength < 1e-4f) {
            flat[0] = 1.0f;
            flat[2] = 0.0f;
        } else {
            flat[0] /= flatLength;
            flat[2] /= flatLength;
        }
        const float u0 = float(i) / float(count - 1) - scroll, u1 = float(i + 1) / float(count - 1) - scroll;
        for (const float* side : {static_cast<const float*>(flat), up}) {
            gfx::Sprite band;
            band.placed = true;
            band.sheet = boltSheet_;
            band.blend = gfx::Blend::Additive;
            for (int k = 0; k < 3; ++k) {
                band.corner[0][k] = a[k] - side[k] * width * 0.5f;
                band.corner[1][k] = b[k] - side[k] * width * 0.5f;
                band.corner[2][k] = b[k] + side[k] * width * 0.5f;
                band.corner[3][k] = a[k] + side[k] * width * 0.5f;
                band.position[k] = (a[k] + b[k]) * 0.5f;
            }
            band.cornerUv[0][0] = u0;
            band.cornerUv[0][1] = 1.0f;
            band.cornerUv[1][0] = u1;
            band.cornerUv[1][1] = 1.0f;
            band.cornerUv[2][0] = u1;
            band.cornerUv[2][1] = 0.0f;
            band.cornerUv[3][0] = u0;
            band.cornerUv[3][1] = 0.0f;
            for (int k = 0; k < 3; ++k) band.colour[k] = colour[k];
            band.colour[3] = 1.0f;
            effects.add(band);
        }
    }
}

void ElectricSpike::gatherEffects(gfx::Effects& effects) const {
    const float metre = perUnit();
    // The sheet runs along every ribbon once a second (`WorldTime % 1000`).
    const float scroll = std::fmod(clock_, 1.0f);
    for (const Gather& one : gathers_) {
        if (!one.alive) continue;
        for (const auto& star : one.star) {
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
            sprite.halfWidth = sprite.halfHeight = star[0] * 32.0f * metre;
            sprite.spin = star[1];
            sprite.sheet = starSheet_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    const int frame = int(clock_ * kReferenceFps);
    for (const Zap& one : zaps_) {
        if (!one.alive) continue;
        // Its walk, thrown again each frame: six points from the far end into the hand.
        float points[6][3];
        for (int i = 0; i < 6; ++i) {
            const float t = float(i) / 5.0f;
            for (int k = 0; k < 3; ++k) {
                const float jitter = (i == 0 || i == 5)
                                         ? 0.0f
                                         : (hashUnit(one.seed + uint32_t(i * 3 + k), uint32_t(frame)) - 0.5f) *
                                               24.0f * metre;
                points[i][k] = one.from[k] + (one.to[k] - one.from[k]) * t + jitter;
            }
        }
        const float colour[3] = {0.5f, 0.5f, 1.0f};
        ribbon(effects, points, 6, 10.0f * metre, colour, scroll);
    }
    for (const Joint& one : joints_) {
        if (!one.alive || one.tails == 0) continue;
        float points[kTails + 1][3];
        int count = 0;
        for (int i = 0; i < one.tails; ++i) jointWorld(one, one.tail[i], points[count++]);
        jointWorld(one, one.at, points[count++]);
        const float colour[3] = {one.light, 0.8f * one.light, one.light};
        ribbon(effects, points, count, one.width * metre, colour, scroll);
    }
    // The rings, their back edge slid up to the hand, so nothing of them is behind it.
    for (const Ring& one : rings_) {
        if (!one.alive) continue;
        const float angles[3] = {one.pitch, one.roll, 0.0f};
        float m[3][3];
        angleMatrix(angles, m);
        float back = 0.0f;
        for (const EffectCorner& c : ring_) {
            back = std::fmax(back, (m[1][0] * c.x + m[1][1] * c.z + m[1][2] * c.y) * one.scale);
        }
        const auto world = [&](const float v[3], float out[3]) {
            for (int k = 0; k < 3; ++k) out[k] = one.across[k] * v[0] - one.ahead[k] * v[1];
            out[1] += v[2];
        };
        const float col0[3] = {m[0][0], m[1][0], m[2][0]};
        const float col1[3] = {m[0][1], m[1][1], m[2][1]};
        const float col2[3] = {m[0][2], m[1][2], m[2][2]};
        float x[3], y[3], z[3], at[3];
        world(col0, x);
        world(col2, y);
        world(col1, z);
        for (int k = 0; k < 3; ++k) at[k] = one.at[k] + one.ahead[k] * back * metre;
        const float colour[3] = {0.8f * one.light, 0.5f * one.light, one.light};
        submitEffectAlong(effects, ring_, ringSheet_, gfx::Blend::Additive, at, x, y, z,
                          one.scale * metre, colour, 1.0f);
    }
}

}  // namespace mu::game
