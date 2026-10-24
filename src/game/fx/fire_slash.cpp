#include "game/fx/fire_slash.h"

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kTwoPi = 6.28318531f;

float clampOne(float v) { return std::clamp(v, 0.0f, 1.0f); }

float length(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// An angle brought into -pi..pi.
float wrapped(float a) {
    const float kPi = 3.14159265f;
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

// MU's TurnAngle2: `from` turned toward `to` by at most `most`, the short way round.
float turnToward(float from, float to, float most) {
    const float off = wrapped(to - from);
    return from + std::clamp(off, -most, most);
}

// A way of `yaw` and `pitch` in our (x, up, z), and the two axes of a tail's cross on it: MU's
// AngleMatrix X, which stays level, and Z, which tilts with the pitch.
void frame(float yaw, float pitch, float way[3], float side[3], float up[3]) {
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    way[0] = cp * sy; way[1] = sp; way[2] = cp * cy;
    side[0] = cy; side[1] = 0.0f; side[2] = -sy;
    up[0] = -sp * sy; up[1] = cp; up[2] = -sp * cy;
}

// A quad whose colour runs past one, as layers: one at the colour up to one, the rest over it.
// MuMain's ImmediateRenderer takes MU's float colour unclamped (Render/Core/ImmediateRenderer.cpp
// Color3f), and the pass packs a colour in a byte a channel; additive, two layers are the sum.
bool addLit(gfx::Effects& effects, gfx::Sprite quad, const float rgb[3]) {
    float left[3] = {rgb[0], rgb[1], rgb[2]};
    for (int layer = 0; layer < 2; ++layer) {
        if (left[0] <= 0.0f && left[1] <= 0.0f && left[2] <= 0.0f) break;
        for (int k = 0; k < 3; ++k) {
            quad.colour[k] = clampOne(left[k]);
            left[k] -= 1.0f;
        }
        if (!effects.add(quad)) return false;
    }
    return true;
}

}  // namespace

float FireSlash::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool FireSlash::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            // Albedos, so they wrap: the ribbon's is GL_REPEAT and the bolts' scrolls.
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    ribbonSheet_ = sheet("fire_slash_ribbon");  // Effect/motion_blur_r2
    skullSheet_ = sheet("fire_slash_skull");    // Skill/Skull
    fireSheet_ = sheet("fire");                 // Effect/Fire01, four cells
    boltSheet_ = sheet("joint_thunder");        // Effect/JointThunder01
    core::logf("fire slash: ribbon %s, skull %s, fire %s, bolt %s",
               bgfx::isValid(ribbonSheet_) ? "yes" : "NO", bgfx::isValid(skullSheet_) ? "yes" : "NO",
               bgfx::isValid(fireSheet_) ? "yes" : "NO", bgfx::isValid(boltSheet_) ? "yes" : "NO");
    return bgfx::isValid(ribbonSheet_);
}

void FireSlash::clear() {
    for (Gathering& one : gatherings_) one = Gathering{};
    for (Ribbon& one : ribbons_) one = Ribbon{};
    for (Bolt& one : bolts_) one = Bolt{};
    for (Fire& one : fires_) one = Fire{};
    skullCount_ = 0;
}

void FireSlash::local(float yaw, float turn, float x, float y, float out[2]) {
    const float c = std::cos(turn * kDegrees), s = std::sin(turn * kDegrees);
    const float X = x * c - y * s, Y = x * s + y * c;
    const float fs = std::sin(yaw), fc = std::cos(yaw);
    out[0] = fc * X - fs * Y;
    out[1] = -fs * X - fc * Y;
}

void FireSlash::cast(uint32_t owner, float release) {
    // A new cast by the same body takes over its old one's slots.
    for (int n = 0; n < kGatherings; ++n) {
        Gathering* slot = nullptr;
        for (Gathering& one : gatherings_) {
            if (!one.alive) {
                slot = &one;
                break;
            }
        }
        if (slot == nullptr) break;
        *slot = Gathering{};
        slot->alive = true;
        slot->owner = owner;
        slot->wait = float(n) / kReferenceFps;  // AttackTime 1, then 2
    }
    Ribbon* ribbon = nullptr;
    for (Ribbon& one : ribbons_) {
        if (one.alive && one.owner == owner && !one.made) {
            ribbon = &one;
            break;
        }
        if (!one.alive && ribbon == nullptr) ribbon = &one;
    }
    if (ribbon == nullptr) return;
    *ribbon = Ribbon{};
    ribbon->alive = true;
    ribbon->owner = owner;
    ribbon->wait = std::max(0.0f, release);
}

void FireSlash::bolt(const float from[3], const float to[3], float yaw, float pitch, float scale,
                     float frames) {
    for (Bolt& one : bolts_) {
        if (one.alive) continue;
        one = Bolt{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = from[k];
            one.to[k] = to[k];
        }
        one.yaw = yaw;
        one.pitch = pitch;
        one.half = scale * perUnit() * 0.5f;
        // LifeTime moves and the one it reaches 0 on: MU kills a joint below 0.
        one.left = frames + 1.0f;
        // MoveJoint's throw a tail, (rand() % 1024 - 512) / Scale degrees.
        one.jitter = 512.0f / std::max(1.0f, scale) * kDegrees;
        // MU moves a joint on the frame it is made.
        stepBolt(one);
        return;
    }
}

void FireSlash::bladeBolt(int on, float from, float upto) {
    for (Bolt& one : bolts_) {
        if (one.alive) continue;
        one = Bolt{};
        one.alive = true;
        one.on = on;
        one.from = from;
        one.upto = upto;
        one.half = 10.0f * perUnit() * 0.5f;  // MU's Scale 10
        one.left = kBladeBoltFrames + 1.0f;
        one.jitter = kBladeThrow * kDegrees;
        stepBolt(one);
        return;
    }
}

void FireSlash::onBlade(const Gathering& one, float share, float out[3]) {
    for (int k = 0; k < 3; ++k) out[k] = one.grip[k] + (one.tip[k] - one.grip[k]) * share;
}

void FireSlash::stepBolt(Bolt& one) {
    const float stride = kBoltStep * perUnit();
    const float reach = kBoltReach * perUnit();
    const float homing = kBoltHoming * kDegrees;
    float way[3], side[3], up[3];
    const bool onBlade_ = one.on >= 0;
    if (onBlade_) {
        // On the blade (the header's "ours"): walked afresh from where the blade is now, aimed
        // straight at its other point, and no preamble step.
        const Gathering& gathering = gatherings_[one.on];
        if (!gathering.alive) {
            one.alive = false;
            return;
        }
        onBlade(gathering, one.from, one.at);
        onBlade(gathering, one.upto, one.to);
        const float d[3] = {one.to[0] - one.at[0], one.to[1] - one.at[1], one.to[2] - one.at[2]};
        one.yaw = std::atan2(d[0], d[2]);
        one.pitch = std::atan2(d[1], std::sqrt(d[0] * d[0] + d[2] * d[2]));
        one.tails = 0;
    } else {
        // Once a frame, Velocity on its unthrown way (MoveJoint's preamble).
        frame(one.yaw, one.pitch, way, side, up);
        for (int k = 0; k < 3; ++k) one.at[k] += way[k] * stride;
    }
    float far = 0.0f;
    for (int j = 0; j < kBoltTails; ++j) {
        // MoveHumming: turned toward its target, at most 50 degrees in each.
        const float d[3] = {one.to[0] - one.at[0], one.to[1] - one.at[1], one.to[2] - one.at[2]};
        const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
        far = length(d);
        if (far > 1e-5f) {
            one.yaw = turnToward(one.yaw, std::atan2(d[0], d[2]), homing);
            one.pitch = turnToward(one.pitch, std::atan2(d[1], flat), homing);
        }
        // Thrown off it, and a tail laid on the thrown frame where it stands.
        const float yaw = one.yaw + (unit() * 2.0f - 1.0f) * one.jitter;
        const float pitch = one.pitch + (unit() * 2.0f - 1.0f) * one.jitter;
        frame(yaw, pitch, way, side, up);
        for (int t = std::min(one.tails, kBoltTails - 1); t > 0; --t) {
            for (int c = 0; c < 4; ++c) {
                for (int k = 0; k < 3; ++k) one.trail[t][c][k] = one.trail[t - 1][c][k];
            }
        }
        for (int k = 0; k < 3; ++k) {
            one.trail[0][0][k] = one.at[k] - side[k] * one.half;
            one.trail[0][1][k] = one.at[k] + side[k] * one.half;
            one.trail[0][2][k] = one.at[k] - up[k] * one.half;
            one.trail[0][3][k] = one.at[k] + up[k] * one.half;
        }
        one.tails = std::min(one.tails + 1, kBoltTails);
        // On the blade it stops at its point rather than curling round it.
        if (onBlade_ && far < stride) break;
        // Past 150 from its target it is gone (sub 3).
        if (!onBlade_ && far > reach) {
            one.alive = false;
            return;
        }
        // And Velocity along the thrown way.
        for (int k = 0; k < 3; ++k) one.at[k] += way[k] * stride;
    }
    one.left -= 1.0f;
    if (one.left <= 0.0f) one.alive = false;
}

void FireSlash::stepGathering(Gathering& one) {
    const int index = int(&one - gatherings_);
    for (int n = 0; n < kGatherPoints; ++n) {
        // MU's three points a frame, on the blade (the header's "ours"); its sparks, on the odd
        // frames, are not drawn.
        const float along = between(kBladeFrom, 1.0f);
        if (one.life % 2 == 0) {
            // A crackle from this point to another a third to a half of the blade on.
            const float span = between(0.3f, 0.5f) * (unit() < 0.5f ? -1.0f : 1.0f);
            const float upto = std::clamp(along + span, kBladeFrom, 1.0f);
            if (std::fabs(upto - along) > 0.1f) bladeBolt(index, along, upto);
        }
    }
    if (--one.life <= 0) one.alive = false;
}

void FireSlash::stepRibbon(Ribbon& one) {
    const float metre = perUnit();
    float axis[2];
    local(one.yaw, kStartTurn, 1.0f, 0.0f, axis);
    float ahead[2];
    local(one.yaw, kStartTurn, 0.0f, -1.0f, ahead);
    const float aheadYaw = std::atan2(ahead[0], ahead[1]);  // the joint's angle, for its bolts
    for (int step = 0; step < kStepsAFrame && one.tails < kTails; ++step) {
        float off[2];
        local(one.yaw, one.turn, 0.0f, -kRadius, off);
        const float at[3] = {one.pivot[0] + off[0] * metre, one.pivot[1],
                             one.pivot[2] + off[1] * metre};
        for (int t = std::min(one.tails, kTails - 1); t > 0; --t) {
            for (int e = 0; e < 2; ++e) {
                for (int k = 0; k < 3; ++k) one.edge[t][e][k] = one.edge[t - 1][e][k];
            }
        }
        for (int e = 0; e < 2; ++e) {
            const float side = (e == 0 ? -1.0f : 1.0f) * kHalfWidth * metre;
            one.edge[0][e][0] = at[0] + axis[0] * side;
            one.edge[0][e][1] = at[1];
            one.edge[0][e][2] = at[2] + axis[1] * side;
        }
        // A flame on a coin, drifting the joint's way, lit by the joint's light as it is now.
        if (unit() < kFireChance) {
            for (Fire& fire : fires_) {
                if (fire.alive) continue;
                fire = Fire{};
                fire.alive = true;
                for (int k = 0; k < 3; ++k) fire.at[k] = at[k];
                fire.at[0] += between(-kFireScatter, kFireScatter) * metre;
                fire.at[2] += between(-kFireScatter, kFireScatter) * metre;
                float drift[2];
                local(one.yaw, kStartTurn, 0.0f, -between(3.2f, 4.7f), drift);
                fire.drift[0] = drift[0] * metre;
                fire.drift[2] = drift[1] * metre;
                fire.scale = between(1.28f, 1.91f) * kFireBigger;
                fire.left = kFireLife;
                fire.light = one.light;
                break;
            }
        }
        // And two blue bolts from the last point to this one, set off on the joint's angle.
        bolt(one.last, at, aheadYaw, 0.0f, std::floor(between(5.0f, 15.0f)), 5.0f);
        bolt(one.last, at, aheadYaw, 0.0f, std::floor(between(4.0f, 12.0f)), 5.0f);
        for (int k = 0; k < 3; ++k) one.last[k] = at[k];
        ++one.tails;
        one.turn -= kStepTurn;
    }
    // Its light, after the tails: LifeTime / 30 down to ten, then a fifth less each frame (MU's
    // a third, ours).
    one.light = one.life >= 10 ? float(one.life) / 30.0f : one.light / kFadeUnderTen;
    // Twenty-one frames: MU's LifeTime 20 to 0, dead below it.
    if (--one.life < 0) one.alive = false;
}

uint32_t FireSlash::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    const float metres = ground_ ? ground_->metresPerTile() : 1.0f;
    for (const Ribbon& one : ribbons_) {
        if (!one.alive || !one.made || one.light <= 0.0f || count >= max) continue;
        // Over the middle of the crescent, a tile ahead of him.
        float ahead[2];
        local(one.yaw, 0.0f, 0.0f, -100.0f, ahead);
        gfx::PointLight& light = out[count++];
        light.position[0] = one.pivot[0] + ahead[0] * perUnit();
        light.position[1] = one.pivot[1];
        light.position[2] = one.pivot[2] + ahead[1] * perUnit();
        light.reach = kLightTiles * metres;
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kLightColour[k] * std::min(1.0f, one.light) * kLightGain;
    }
    return count;
}

void FireSlash::gatherEffects(gfx::Effects& effects) const {
    const float metre = perUnit();
    // The ribbons: a quad between each two tails, U along them with the head at 1, V across.
    for (const Ribbon& one : ribbons_) {
        if (!one.alive || !one.made || one.tails < 2 || !bgfx::isValid(ribbonSheet_)) continue;
        const int numTails = one.tails - 1;
        for (int j = 0; j + 1 < one.tails; ++j) {
            gfx::Sprite quad;
            quad.placed = true;
            quad.sheet = ribbonSheet_;
            quad.blend = gfx::Blend::Additive;
            // ((18 - j) / 18) * 2 * Light: 1.33 at the head on its first frame, unclamped.
            const float lit = float(kTails - j) / float(kTails) * 2.0f * one.light;
            const float rgb[3] = {lit, lit, lit};
            quad.colour[3] = 1.0f;
            const float u0 = float(numTails - j) / float(kTails - 1);
            const float u1 = float(numTails - j - 1) / float(kTails - 1);
            for (int k = 0; k < 3; ++k) {
                quad.corner[0][k] = one.edge[j][0][k];
                quad.corner[1][k] = one.edge[j][1][k];
                quad.corner[2][k] = one.edge[j + 1][1][k];
                quad.corner[3][k] = one.edge[j + 1][0][k];
                quad.position[k] = (one.edge[j][0][k] + one.edge[j + 1][1][k]) * 0.5f;
            }
            quad.cornerUv[0][0] = u0; quad.cornerUv[0][1] = 0.0f;
            quad.cornerUv[1][0] = u0; quad.cornerUv[1][1] = 1.0f;
            quad.cornerUv[2][0] = u1; quad.cornerUv[2][1] = 1.0f;
            quad.cornerUv[3][0] = u1; quad.cornerUv[3][1] = 0.0f;
            if (!addLit(effects, quad, rgb)) return;
        }
    }
    // The flames: Fire01's four cells over their life, each a whole flame, never faded (MU
    // writes no Luminosity into this subtype's Light); heat its joint's light, cooling a little.
    if (bgfx::isValid(fireSheet_)) {
        for (const Fire& one : fires_) {
            if (!one.alive || one.scale <= 0.0f) continue;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
            sprite.halfWidth = sprite.halfHeight = kFirePixels * one.scale * metre * 0.5f;
            const int cell = std::clamp(int((kFireLife - 1.0f - one.left) / 6.0f), 0, 3);
            sprite.u0 = float(cell) * 0.25f;
            sprite.u1 = float(cell + 1) * 0.25f;
            const float aged = 1.0f - clampOne(one.left / kFireLife);
            sprite.colour[0] = kFireCellGain[cell] * kFireFull / 20.0f;
            // The joint's light as heat on its square root: the ramp's brightness runs on the heat
            // squared, and MU's 0.67 (the flames of its second and third frames) is no ember.
            sprite.colour[1] = clampOne(kFireHeat * std::sqrt(one.light) * (1.0f - kFireCools * aged));
            sprite.colour[2] = 0.0f;
            sprite.colour[3] = 1.0f;
            sprite.sheet = fireSheet_;
            sprite.blend = gfx::Blend::Breath;
            if (!effects.add(sprite)) return;
        }
    }
    // The bolts: each tail's cross joined to the next, both faces, the sheet scrolling along
    // them with the clock's second, the second face twice as fast (ZzzEffectJoint.cpp:7118-7124,
    // 7303-7320). Lit (0.5, 0.5, 1) to the end.
    if (bgfx::isValid(boltSheet_)) {
        const float scroll = clock_ * kBoltScroll - std::floor(clock_ * kBoltScroll);
        for (const Bolt& one : bolts_) {
            if (!one.alive || one.tails < 2) continue;
            const int numTails = one.tails - 1;
            for (int j = 0; j < numTails; ++j) {
                const float l1 = float(numTails - j) / float(kBoltTails - 1) * kBoltRepeats - scroll;
                const float l2 = float(numTails - j - 1) / float(kBoltTails - 1) * kBoltRepeats - scroll;
                for (int face = 0; face < 2; ++face) {
                    // Face one on the tails' Z (corners 2, 3), face two on their X (0, 1).
                    const int lo = face == 0 ? 2 : 0, hi = face == 0 ? 3 : 1;
                    const float shift = face == 0 ? 0.0f : scroll * 2.0f;
                    gfx::Sprite quad;
                    quad.placed = true;
                    quad.sheet = boltSheet_;
                    quad.blend = gfx::Blend::Additive;
                    for (int k = 0; k < 3; ++k) quad.colour[k] = kBoltBlue[k];
                    quad.colour[3] = 1.0f;
                    for (int k = 0; k < 3; ++k) {
                        quad.corner[0][k] = one.trail[j][lo][k];
                        quad.corner[1][k] = one.trail[j + 1][lo][k];
                        quad.corner[2][k] = one.trail[j + 1][hi][k];
                        quad.corner[3][k] = one.trail[j][hi][k];
                        quad.position[k] = (one.trail[j][lo][k] + one.trail[j + 1][hi][k]) * 0.5f;
                    }
                    quad.cornerUv[0][0] = l1 + shift; quad.cornerUv[0][1] = 1.0f;
                    quad.cornerUv[1][0] = l2 + shift; quad.cornerUv[1][1] = 1.0f;
                    quad.cornerUv[2][0] = l2 + shift; quad.cornerUv[2][1] = 0.0f;
                    quad.cornerUv[3][0] = l1 + shift; quad.cornerUv[3][1] = 0.0f;
                    if (!effects.add(quad)) return;
                }
            }
        }
    }
    // The skulls over each cut body.
    if (bgfx::isValid(skullSheet_)) {
        for (int i = 0; i < skullCount_; ++i) {
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = skulls_[i].at[k];
            sprite.halfWidth = sprite.halfHeight = kSkullPixels * metre * 0.5f;
            sprite.sheet = skullSheet_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) return;
        }
    }
}

}  // namespace mu::game
