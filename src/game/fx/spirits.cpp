#include "game/fx/spirits.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;

// TurnAngle2's walk: toward `to` by at most `step` degrees, the short way round.
float turnToward(float from, float to, float step) {
    float gap = std::fmod(to - from, 360.0f);
    if (gap > 180.0f) gap -= 360.0f;
    if (gap < -180.0f) gap += 360.0f;
    return from + std::clamp(gap, -step, step);
}

}  // namespace

bool Spirits::open(const std::string& assetDir, content::Textures& textures,
                   const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const content::EffectSheet* sheet = table.effect("joint_spirit");
    if (sheet == nullptr) {
        core::logError("spirits: no cooked effect named 'joint_spirit'");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    return bgfx::isValid(sheet_);
}

void Spirits::release(uint32_t caster, const float feet[3]) {
    // Four quarters, a wide and a thin joint each: MU's `i * 90` and its Scale 80 and 20.
    int made = 0;
    for (Joint& joint : joints_) {
        if (joint.alive) continue;
        const int quarter = made / 2;
        joint = Joint{};
        joint.alive = true;
        joint.caster = caster;
        joint.at[0] = feet[0];
        joint.at[1] = feet[1] + kStart;
        joint.at[2] = feet[2];
        joint.aim[0] = feet[0];
        joint.aim[1] = feet[1] + kAim;
        joint.aim[2] = feet[2];
        joint.yaw = float(quarter) * 90.0f;
        joint.width = made % 2 == 0 ? kWide : kThin;
        if (++made == 8) return;
    }
}

void Spirits::fly(Joint& joint) {
    // The tail is where the head was, frame by frame.
    for (int i = kTails - 1; i > 0; --i) {
        for (int k = 0; k < 3; ++k) joint.tail[i][k] = joint.tail[i - 1][k];
    }
    for (int k = 0; k < 3; ++k) joint.tail[0][k] = joint.at[k];
    joint.tails = std::min(joint.tails + 1, kTails);
    // MoveHumming: heading and pitch turned toward his middle.
    const float dx = joint.aim[0] - joint.at[0], dz = joint.aim[2] - joint.at[2];
    const float flat = std::sqrt(dx * dx + dz * dz);
    joint.yaw = turnToward(joint.yaw, std::atan2(dz, dx) / kDegrees, kTurn);
    joint.pitch =
        turnToward(joint.pitch, std::atan2(joint.aim[1] - joint.at[1], flat) / kDegrees, kTurn);
    // And the wobble: `rand() % 32 - 16`, times 0.2 on the pitch and 0.8 on the heading, eased
    // by 0.6 and 0.8 a frame.
    const auto roll = [&] {
        dice_ ^= dice_ << 13;
        dice_ ^= dice_ >> 17;
        dice_ ^= dice_ << 5;
        return float(int(dice_ % 32u) - 16);
    };
    joint.turnPitch += roll() * 0.2f;
    joint.turnYaw += roll() * 0.8f;
    joint.pitch += joint.turnPitch;
    joint.yaw += joint.turnYaw;
    joint.turnPitch *= 0.6f;
    joint.turnYaw *= 0.8f;
    // Kept between one and four metres over the ground: five degrees up or down, no wobble.
    const float floor =
        ground_ ? ground_->heightAt(joint.at[0], joint.at[2]) : joint.aim[1] - kAim;
    if (joint.at[1] < floor + kLowest) {
        joint.turnPitch = 0.0f;
        joint.pitch = 5.0f;
    }
    if (joint.at[1] > floor + kHighest) {
        joint.turnPitch = 0.0f;
        joint.pitch = -5.0f;
    }
    const float cp = std::cos(joint.pitch * kDegrees);
    joint.at[0] += cp * std::cos(joint.yaw * kDegrees) * kSpeed;
    joint.at[1] += std::sin(joint.pitch * kDegrees) * kSpeed;
    joint.at[2] += cp * std::sin(joint.yaw * kDegrees) * kSpeed;
    joint.left -= 1.0f;
    if (joint.left <= 0.0f) joint.alive = false;
}

void Spirits::gather(gfx::Effects& effects) const {
    if (!bgfx::isValid(sheet_)) return;
    for (const Joint& joint : joints_) {
        if (!joint.alive || joint.tails == 0) continue;
        // The head, then the tail: the sheet's head (its right) at the joint's.
        float points[kTails + 1][3];
        for (int k = 0; k < 3; ++k) points[0][k] = joint.at[k];
        for (int i = 0; i < joint.tails; ++i) {
            for (int k = 0; k < 3; ++k) points[i + 1][k] = joint.tail[i][k];
        }
        const int count = joint.tails + 1;
        const float light = kDark * std::min(1.0f, joint.left * 0.1f);
        const float half = joint.width * 0.5f;
        for (int s = 0; s + 1 < count; ++s) {
            const float* a = points[s];
            const float* b = points[s + 1];
            const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
            if (length < 1e-4f) continue;
            // Two crossed quads along the segment, so it has width from any angle: one across
            // it on the ground's plane, one across it standing.
            float across[3] = {0.0f, 0.0f, 1.0f};
            if (flat > 1e-4f) {
                across[0] = -d[2] / flat;
                across[2] = d[0] / flat;
            }
            const float n[3] = {d[0] / length, d[1] / length, d[2] / length};
            const float up[3] = {n[1] * across[2] - n[2] * across[1],
                                 n[2] * across[0] - n[0] * across[2],
                                 n[0] * across[1] - n[1] * across[0]};
            const float uHead = 1.0f - float(s) / float(count - 1);
            const float uTail = 1.0f - float(s + 1) / float(count - 1);
            const float* sides[2] = {across, up};
            for (const float* side : sides) {
                gfx::Sprite sprite;
                for (int k = 0; k < 3; ++k) sprite.position[k] = 0.5f * (a[k] + b[k]);
                sprite.placed = true;
                for (int k = 0; k < 3; ++k) {
                    sprite.corner[0][k] = b[k] - side[k] * half;
                    sprite.corner[1][k] = a[k] - side[k] * half;
                    sprite.corner[2][k] = a[k] + side[k] * half;
                    sprite.corner[3][k] = b[k] + side[k] * half;
                }
                const float uv[4][2] = {{uTail, 1}, {uHead, 1}, {uHead, 0}, {uTail, 0}};
                for (int k = 0; k < 4; ++k) {
                    sprite.cornerUv[k][0] = uv[k][0];
                    sprite.cornerUv[k][1] = uv[k][1];
                }
                sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = light;
                sprite.sheet = sheet_;
                sprite.blend = gfx::Blend::Minus;
                effects.add(sprite);
            }
        }
    }
}

}  // namespace mu::game
