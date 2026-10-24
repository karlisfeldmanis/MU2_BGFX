#include "game/fx/critical_damage.h"

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

}  // namespace

float CriticalDamage::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool CriticalDamage::open(const std::string& assetDir, content::Textures& textures,
                          const content::Ground* ground) {
    ground_ = ground;
    const auto sheet = [&](const std::string& path) {
        return core::fileExists(assetDir + path)
                   ? textures.load(assetDir + path, content::TextureRole::Decal)
                   : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    // The flash is Fire Burst's quad and sheet; the coils' Fire04 and the level-up's Flare.
    flashSheet_ = sheet("/effects/fire_burst/KingS_R.png");
    ribbonSheet_ = sheet("/effects/critical_damage/fire04.png");
    flareSheet_ = sheet("/effects/levelup/flare.png");
    flash_.clear();
    const bool ok = loadEffectObj(assetDir + "/effects/fire_burst/DarkLordSkill.obj", 1.0f, "", flash_) &&
                    bgfx::isValid(flashSheet_) && bgfx::isValid(ribbonSheet_) &&
                    bgfx::isValid(flareSheet_);
    core::logf("critical damage: flash, coils and sheets %s", ok ? "yes" : "NO");
    return ok;
}

void CriticalDamage::clear() {
    for (Charge& one : charges_) one = Charge{};
    for (Wearer& one : wearers_) one = Wearer{};
    for (Flash& one : flashes_) one = Flash{};
    for (Coil& one : coils_) one = Coil{};
}

void CriticalDamage::begin(uint32_t who, float clipSeconds) {
    for (Charge& one : charges_) {
        if (one.alive) continue;
        one.alive = true;
        one.who = who;
        one.left = clipSeconds * kLetGo;
        return;
    }
}

void CriticalDamage::standing(uint32_t who) {
    Wearer* free = nullptr;
    for (Wearer& one : wearers_) {
        if (one.who == who) {
            one.seen = true;
            return;
        }
        if (one.who == 0 && free == nullptr) free = &one;
    }
    // First seen under it: its first pulse a full interval on, the cast's flash being its own.
    if (free != nullptr) *free = Wearer{who, kPulseSeconds, true};
}

void CriticalDamage::flash(const float hand[3], float yaw, bool left) {
    for (Flash& one : flashes_) {
        if (one.alive) continue;
        one = Flash{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = hand[k];
        one.ahead[0] = std::sin(yaw);
        one.ahead[2] = std::cos(yaw);
        one.across[0] = one.ahead[2];
        one.across[2] = -one.ahead[0];
        one.roll = left ? -45.0f : 45.0f;
        return;
    }
}

void CriticalDamage::coil(uint32_t who, int bone, bool bow, int kind) {
    for (Coil& one : coils_) {
        if (one.alive) continue;
        one = Coil{};
        one.alive = true;
        one.who = who;
        one.bone = bone;
        one.bow = bow;
        one.kind = kind;
        return;
    }
}

void CriticalDamage::ribbon(gfx::Effects& effects, const float (*points)[3], int count, float width,
                            const float colour[3]) const {
    if (count < 2) return;
    const float up[3] = {0.0f, 1.0f, 0.0f};
    for (int i = 0; i + 1 < count; ++i) {
        const float* a = points[i];
        const float* b = points[i + 1];
        float along[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float length = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        if (length < 1e-4f) continue;
        for (float& c : along) c /= length;
        // A cross of two bands, flat and upright, as a tail is.
        float flat[3] = {along[2], 0.0f, -along[0]};
        const float flatLength = std::sqrt(flat[0] * flat[0] + flat[2] * flat[2]);
        if (flatLength < 1e-4f) {
            flat[0] = 1.0f;
            flat[2] = 0.0f;
        } else {
            flat[0] /= flatLength;
            flat[2] /= flatLength;
        }
        const float u0 = float(i) / float(count - 1), u1 = float(i + 1) / float(count - 1);
        for (const float* side : {static_cast<const float*>(flat), up}) {
            gfx::Sprite band;
            band.placed = true;
            band.sheet = ribbonSheet_;
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

void CriticalDamage::gatherEffects(gfx::Effects& effects) const {
    const float metre = perUnit();
    for (const Coil& one : coils_) {
        if (!one.alive || one.count < 2) continue;
        const float colour[3] = {one.light, 0.8f * one.light, one.light};
        ribbon(effects, one.points, one.count, kCoilWidth * metre, colour);
        for (int i = 0; i < one.flares; ++i) {
            gfx::Sprite flare;
            for (int k = 0; k < 3; ++k) flare.position[k] = one.flare[i][k];
            flare.halfWidth = flare.halfHeight = kFlareScale * 32.0f * metre;
            for (int k = 0; k < 3; ++k) flare.colour[k] = colour[k];
            flare.sheet = flareSheet_;
            flare.blend = gfx::Blend::Additive;
            effects.add(flare);
        }
    }
    // The flash: MU's (45, +-45, 0), turned to his facing and slid ahead so its back edge sits
    // at the hand (as fx/fire_burst.cpp lays it).
    for (const Flash& one : flashes_) {
        if (!one.alive) continue;
        const float angles[3] = {45.0f, one.roll, 0.0f};
        float m[3][3];
        angleMatrix(angles, m);
        float back = 0.0f;
        for (const EffectCorner& c : flash_) {
            back = std::fmax(back, (m[1][0] * c.x + m[1][1] * c.z + m[1][2] * c.y) * one.scale);
        }
        // MU's caster frame: x across him, -y ahead, z up; the corner's (x, y, z) is MU's (x, z, y).
        const auto toWorld = [&](const float mu[3], float out[3]) {
            for (int k = 0; k < 3; ++k) out[k] = one.across[k] * mu[0] - one.ahead[k] * mu[1];
            out[1] += mu[2];
        };
        const float col0[3] = {m[0][0], m[1][0], m[2][0]};
        const float col1[3] = {m[0][1], m[1][1], m[2][1]};
        const float col2[3] = {m[0][2], m[1][2], m[2][2]};
        const float slide[3] = {0.0f, -back, 0.0f};
        float at[3], x[3], y[3], z[3];
        toWorld(slide, at);
        for (int k = 0; k < 3; ++k) at[k] = one.at[k] + at[k] * metre;
        toWorld(col0, x);
        toWorld(col2, y);
        toWorld(col1, z);
        const float colour[3] = {one.light, 0.6f * one.light, 0.3f * one.light};
        submitEffectAlong(effects, flash_, flashSheet_, gfx::Blend::Additive, at, x, y, z,
                          one.scale * metre, colour, 1.0f);
    }
}

}  // namespace mu::game
