#include "game/fx/fire_burst.h"

#include <algorithm>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

namespace {

constexpr float kDegree = 3.14159265358979f / 180.0f;

// MU's own, in degrees (Main 1.03.35 ZzzAI.cpp:31-148; MuMain Engine/AI/ZzzAI.cpp:60-145).
float createAngle(float x1, float y1, float x2, float y2) {
    const float nx = x2 - x1, ny = y2 - y1;
    if (std::fabs(nx) < 0.0001f) return ny < 0.0f ? 0.0f : 180.0f;
    if (std::fabs(ny) < 0.0001f) return nx < 0.0f ? 270.0f : 90.0f;
    const float a = std::atan(ny / nx) / kDegree + 90.0f;
    return nx < 0.0f ? a + 180.0f : a;
}

float turnAngle2(float angle, float a, float d) {
    if (angle < 0.0f) angle += 360.0f;
    if (a < 0.0f) a += 360.0f;
    if (angle < 180.0f) {
        const float aa = angle - d;
        if (a >= angle + d && a < angle + 180.0f) return angle + d;
        if (aa >= 0.0f && (a >= angle + 180.0f || a < aa)) return angle - d;
        if (aa < 0.0f && (a >= angle + 180.0f && a < aa + 360.0f)) return angle - d + 360.0f;
        return a;
    }
    const float aa = angle + d;
    if (a < angle - d && a >= angle - 180.0f) return angle - d;
    if (aa < 360.0f && (a < angle - 180.0f || a >= aa)) return angle + d;
    if (aa >= 360.0f && (a < angle - 180.0f && a >= aa - 360.0f)) return angle + d - 360.0f;
    return a;
}

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

}  // namespace

float FireBurst::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool FireBurst::open(const std::string& assetDir, content::Textures& textures,
                     const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/fire_burst/";
    const auto sheet = [&](const char* name) {
        return core::fileExists(dir + name) ? textures.load(dir + name, content::TextureRole::Decal)
                                            : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    headSheet_ = sheet("m_Piercing.png");
    pieceSheet_ = sheet("m_Piercing02.png");
    flashSheet_ = sheet("KingS_R.png");
    fireSheet_ = sheet("fire02.png");
    head_.clear();
    piece_.clear();
    flash_.clear();
    // In MU's units; scaled to metres at the draw, as the ground's tile decides.
    const bool ok = loadEffectObj(dir + "PierPart.obj", 1.0f, "m_Piercing", head_) &&
                    loadEffectObj(dir + "PierPart.obj", 1.0f, "m_Piercing02", piece_) &&
                    loadEffectObj(dir + "DarkLordSkill.obj", 1.0f, "", flash_) &&
                    bgfx::isValid(headSheet_) && bgfx::isValid(pieceSheet_) &&
                    bgfx::isValid(flashSheet_) && bgfx::isValid(fireSheet_);
    core::logf("fire burst: heads, trail, flash and sheets %s", ok ? "yes" : "NO");
    return ok;
}

void FireBurst::clear() {
    for (Charge& one : charges_) one = Charge{};
    for (Head& one : heads_) one = Head{};
    for (Piece& one : pieces_) one = Piece{};
    for (Fire& one : fires_) one = Fire{};
    for (Flash& one : flashes_) one = Flash{};
}

void FireBurst::begin(uint32_t who, uint32_t target, float clipSeconds) {
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
    slot->target = target;
    // STRIKE's AttackTime over the clip as it is actually played.
    slot->rate = clipSeconds > 0.0f ? kClipFrames / (clipSeconds * kReferenceFps) : 1.0f;
}

void FireBurst::toWorld(const Line& line, const float mu[3], float out[3]) const {
    wayToWorld(line, mu, out);
    for (int k = 0; k < 3; ++k) out[k] = line.origin[k] + out[k] * perUnit();
}

void FireBurst::wayToWorld(const Line& line, const float mu[3], float out[3]) const {
    // MU's caster faces -Y: its X lies across him, its -Y ahead, its Z up.
    for (int k = 0; k < 3; ++k) out[k] = line.across[k] * mu[0] - line.ahead[k] * mu[1];
    out[1] += mu[2];
}

void FireBurst::letGo(const float feet[3], const float way[3], uint32_t target, float targetTall) {
    Line line;
    for (int k = 0; k < 3; ++k) {
        line.origin[k] = feet[k];
        line.ahead[k] = way[k];
    }
    line.across[0] = way[2];
    line.across[1] = 0.0f;
    line.across[2] = -way[0];
    const float tall = targetTall / perUnit();
    // Three heads off his facing +90, 0 and -90; the first at the body's crown, the others at
    // its middle (`Pos[2] -= BoundingBoxMax[2] / 2` after the first, ZzzCharacter.cpp:5076).
    const float turns[3] = {90.0f, 0.0f, 270.0f};
    for (int h = 0; h < 3; ++h) {
        for (Head& one : heads_) {
            if (one.alive) continue;
            one = Head{};
            one.alive = true;
            one.target = target;
            one.line = line;
            one.at[1] = -kAhead;
            one.at[2] = kUp;
            one.angle[2] = turns[h];
            one.aimUp = h == 0 ? tall : tall * 0.5f;
            // With no body, the middle of four tiles ahead.
            one.aim[1] = -400.0f;
            one.aim[2] = tall * 0.5f;
            break;
        }
    }
    // The flash's two arms.
    for (const float roll : {45.0f, -45.0f}) {
        for (Flash& one : flashes_) {
            if (one.alive) continue;
            one = Flash{};
            one.alive = true;
            one.line = line;
            one.roll = roll;
            break;
        }
    }
}

void FireBurst::fly(Head& head, const float* there) {
    // The aim re-read from the body each frame, as MU's head reads its owner's position.
    if (there != nullptr) {
        const float metre = perUnit();
        float d[3];
        for (int k = 0; k < 3; ++k) d[k] = (there[k] - head.line.origin[k]) / metre;
        head.aim[0] = d[0] * head.line.across[0] + d[2] * head.line.across[2];
        head.aim[1] = -(d[0] * head.line.ahead[0] + d[2] * head.line.ahead[2]);
        head.aim[2] = d[1] + head.aimUp;
    }
    for (int i = 1; float(i) < head.gravity; ++i) {
        if (unit() < 0.5f) head.angle[0] += head.angle[0] < -90.0f ? 20.0f : -20.0f;
        // MoveHumming: the yaw, then the pitch, each turned at most `velocity` toward the aim.
        head.angle[2] = turnAngle2(head.angle[2],
                                   createAngle(head.at[0], head.at[1], head.aim[0], head.aim[1]),
                                   head.velocity);
        const float flat = std::hypot(head.at[0] - head.aim[0], head.at[1] - head.aim[1]);
        head.angle[0] = turnAngle2(head.angle[0],
                                   360.0f - createAngle(head.at[2], flat, head.aim[2], 0.0f),
                                   head.velocity);
        head.velocity += 0.4f;
        if (head.left < 10.0f) head.velocity += 0.1f;
        const float pitch = head.angle[0] * kDegree, yaw = head.angle[2] * kDegree;
        head.at[0] += kStep * std::cos(pitch) * std::sin(yaw);
        head.at[1] -= kStep * std::cos(pitch) * std::cos(yaw);
        head.at[2] -= kStep * std::sin(pitch);
        stamp(head);
    }
    head.gravity += 0.1f;
    head.left -= 1.0f;
    if (head.left <= 0.0f) head.alive = false;
}

void FireBurst::stamp(const Head& head) {
    float m[3][3];
    angleMatrix(head.angle, m);
    // The model's X, Y and Z: the export is MU's turned Y-up and its Z negated at the load, so a
    // corner's (x, y, z) is MU's (x, z, y) -- its X lands on the matrix's first column, its Y on
    // the third and its Z on the second.
    const float col0[3] = {m[0][0], m[1][0], m[2][0]};
    const float col1[3] = {m[0][1], m[1][1], m[2][1]};
    const float col2[3] = {m[0][2], m[1][2], m[2][2]};
    float at[3];
    toWorld(head.line, head.at, at);
    for (Piece& one : pieces_) {
        if (one.alive) continue;
        one = Piece{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        wayToWorld(head.line, col0, one.x);
        wayToWorld(head.line, col2, one.y);
        wayToWorld(head.line, col1, one.z);
        one.alpha = std::clamp((kHeadFrames - head.left + 1.0f) / 5.0f, 0.0f, 1.0f);
        one.left = head.left;
        break;
    }
    // Its fire, flown on down the head's heading: (0, -1, 0) turned by the head.
    const float heading[3] = {-col1[0], -col1[1], -col1[2]};
    for (Fire& one : fires_) {
        if (one.alive) continue;
        one = Fire{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        wayToWorld(head.line, heading, one.way);
        one.speed = between(32.0f, 40.0f) * 0.3f;
        one.spin = between(0.0f, 6.2831853f);
        break;
    }
}

void FireBurst::gatherEffects(gfx::Effects& effects) const {
    const float metre = perUnit();
    for (const Piece& one : pieces_) {
        if (!one.alive || one.alpha <= 0.01f) continue;
        const float colour[3] = {1.0f, 1.0f, 1.0f};
        submitEffectAlong(effects, piece_, pieceSheet_, gfx::Blend::Alpha, one.at, one.x, one.y,
                          one.z, kPieceScale * metre, colour, one.alpha);
    }
    for (const Head& one : heads_) {
        if (!one.alive) continue;
        float m[3][3];
        angleMatrix(one.angle, m);
        const float col0[3] = {m[0][0], m[1][0], m[2][0]};
        const float col1[3] = {m[0][1], m[1][1], m[2][1]};
        const float col2[3] = {m[0][2], m[1][2], m[2][2]};
        float at[3], x[3], y[3], z[3];
        toWorld(one.line, one.at, at);
        wayToWorld(one.line, col0, x);
        wayToWorld(one.line, col2, y);
        wayToWorld(one.line, col1, z);
        const float colour[3] = {1.0f, 1.0f, 1.0f};
        submitEffectAlong(effects, head_, headSheet_, gfx::Blend::Alpha, at, x, y, z,
                          kHeadScale * metre, colour, 1.0f);
    }
    for (const Fire& one : fires_) {
        if (!one.alive) continue;
        gfx::Sprite fire;
        for (int k = 0; k < 3; ++k) fire.position[k] = one.at[k];
        fire.halfWidth = fire.halfHeight = kFireSize * 0.5f * one.scale * metre;
        fire.spin = one.spin;
        const float light = std::fmin(1.0f, one.left * 0.2f);
        fire.colour[0] = fire.colour[1] = fire.colour[2] = light;
        fire.sheet = fireSheet_;
        fire.blend = gfx::Blend::Additive;
        effects.add(fire);
    }
    // The flash: MU's (45, +-45, 0), turned to his facing, and slid ahead so its back edge sits
    // where it is made.
    for (const Flash& one : flashes_) {
        if (!one.alive) continue;
        const float angles[3] = {45.0f, one.roll, 0.0f};
        float m[3][3];
        angleMatrix(angles, m);
        float back = 0.0f;
        for (const EffectCorner& c : flash_) {
            back = std::fmax(back, (m[1][0] * c.x + m[1][1] * c.z + m[1][2] * c.y) * one.scale);
        }
        const float mu[3] = {0.0f, -kAhead - back, kUp};
        const float col0[3] = {m[0][0], m[1][0], m[2][0]};
        const float col1[3] = {m[0][1], m[1][1], m[2][1]};
        const float col2[3] = {m[0][2], m[1][2], m[2][2]};
        float at[3], x[3], y[3], z[3];
        toWorld(one.line, mu, at);
        wayToWorld(one.line, col0, x);
        wayToWorld(one.line, col2, y);
        wayToWorld(one.line, col1, z);
        const float colour[3] = {one.light, 0.6f * one.light, 0.3f * one.light};
        submitEffectAlong(effects, flash_, flashSheet_, gfx::Blend::Additive, at, x, y, z,
                          one.scale * metre, colour, 1.0f);
    }
}

}  // namespace mu::game
