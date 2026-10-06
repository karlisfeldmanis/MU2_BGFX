#include "game/fx/nova.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kDegrees = 3.14159265f / 180.0f;

}  // namespace

bool Nova::open(const std::string& assetDir, content::Textures& textures,
                const content::Showing& table) {
    const content::EffectSheet* joint = table.effect("joint_spirit");
    const content::EffectSheet* light = table.effect("light");
    if (joint == nullptr || light == nullptr) {
        core::logError("nova: no cooked effect named 'joint_spirit' or 'light'");
        return false;
    }
    joint_ = textures.load(assetDir + "/" + joint->path, content::TextureRole::Albedo);
    light_ = textures.load(assetDir + "/" + light->path, content::TextureRole::Albedo);
    return bgfx::isValid(joint_) && bgfx::isValid(light_);
}

uint32_t Nova::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

void Nova::charge(const float* points, int count, int stage, const float tint[3]) {
    // `m_bySkillCount + 1` lights at each point this frame; the pool's oldest give way.
    const float size = 1.3f + 0.08f * float(stage);
    for (int p = 0; p < count; ++p) {
        for (int n = 0; n < stage + 1; ++n) {
            Glow* slot = nullptr;
            for (Glow& glow : glows_) {
                if (!glow.alive) {
                    slot = &glow;
                    break;
                }
                if (slot == nullptr || glow.left < slot->left) slot = &glow;
            }
            *slot = Glow{};
            slot->alive = true;
            for (int k = 0; k < 3; ++k) {
                slot->at[k] = points[p * 3 + k];
                slot->tint[k] = tint[k];
            }
            slot->size = size * (0.6f + 0.4f * float(roll() % 100u) / 100.0f);
            slot->left = kLightLife;
        }
    }
}

void Nova::release(const float feet[3], int stage, const float tint[3]) {
    for (Burst& burst : bursts_) {
        if (burst.alive) continue;
        burst = Burst{};
        burst.alive = true;
        for (int k = 0; k < 3; ++k) {
            burst.feet[k] = feet[k];
            burst.tint[k] = tint[k];
        }
        // One frame of joints a stage it charged, and at least one.
        burst.framesLeft = std::max(1, stage);
        return;
    }
}

void Nova::spawn(const float from[3], float yaw, float speed, bool tailed, const float tint[3]) {
    for (Joint& joint : joints_) {
        if (joint.alive) continue;
        joint = Joint{};
        joint.alive = true;
        joint.tailed = tailed;
        const float cp = std::cos(kPitch * kDegrees);
        joint.step[0] = cp * std::cos(yaw * kDegrees) * speed;
        joint.step[1] = std::sin(kPitch * kDegrees) * speed;
        joint.step[2] = cp * std::sin(yaw * kDegrees) * speed;
        for (int k = 0; k < 3; ++k) {
            joint.at[k] = from[k];
            joint.tint[k] = tint[k];
        }
        return;
    }
}

void Nova::emit(Burst& burst) {
    const float from[3] = {burst.feet[0], burst.feet[1] + kOver, burst.feet[2]};
    // Sub-type 6: `i * (10 + rand() % 10)` round, one in five with a tail.
    for (int i = 0; i < kPerFrame; ++i) {
        const float yaw = float(i) * float(10 + int(roll() % 10u));
        spawn(from, yaw, kFast, roll() % 5u == 0, burst.tint);
    }
    // And on its last frame the slow ring, sub-type 7, `i * 10` exactly.
    if (burst.framesLeft == 1) {
        for (int i = 0; i < kPerFrame; ++i) spawn(from, float(i) * 10.0f, kSlow, true, burst.tint);
    }
    if (--burst.framesLeft <= 0) burst.alive = false;
}

void Nova::update(float seconds) {
    owed_ += seconds * kFps;
    while (owed_ >= 1.0f) {
        owed_ -= 1.0f;
        for (Burst& burst : bursts_) {
            if (burst.alive) emit(burst);
        }
        for (Joint& joint : joints_) {
            if (!joint.alive) continue;
            if (joint.tailed) {
                for (int i = kTails - 1; i > 0; --i) {
                    for (int k = 0; k < 3; ++k) joint.tail[i][k] = joint.tail[i - 1][k];
                }
                for (int k = 0; k < 3; ++k) joint.tail[0][k] = joint.at[k];
                joint.tails = std::min(joint.tails + 1, kTails);
            }
            for (int k = 0; k < 3; ++k) joint.at[k] += joint.step[k];
            joint.left -= 1.0f;
            if (joint.left <= 0.0f) joint.alive = false;
        }
        for (Glow& glow : glows_) {
            if (!glow.alive) continue;
            glow.left -= 1.0f;
            if (glow.left <= 0.0f) glow.alive = false;
        }
    }
}

void Nova::gather(gfx::Effects& effects) const {
    if (!bgfx::isValid(joint_) || !bgfx::isValid(light_)) return;
    for (const Glow& glow : glows_) {
        if (!glow.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = glow.at[k];
        sprite.halfWidth = sprite.halfHeight = glow.size * 0.25f;
        const float fade = glow.left / kLightLife;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = glow.tint[k] * fade;
        sprite.sheet = light_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    for (const Joint& joint : joints_) {
        if (!joint.alive) continue;
        // Its light, the LifeTime over ten as the spirits' (fx/spirits.cpp), in its tint.
        const float light = std::min(1.0f, joint.left * 0.1f);
        if (!joint.tailed || joint.tails == 0) {
            // A head alone: MU draws the joint's head with no ribbon behind it.
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = joint.at[k];
            sprite.halfWidth = sprite.halfHeight = kWidth * 0.25f;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = joint.tint[k] * light;
            sprite.sheet = light_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
            continue;
        }
        float points[kTails + 1][3];
        for (int k = 0; k < 3; ++k) points[0][k] = joint.at[k];
        for (int i = 0; i < joint.tails; ++i) {
            for (int k = 0; k < 3; ++k) points[i + 1][k] = joint.tail[i][k];
        }
        const int count = joint.tails + 1;
        const float half = kWidth * 0.5f;
        for (int s = 0; s + 1 < count; ++s) {
            const float* a = points[s];
            const float* b = points[s + 1];
            const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
            if (length < 1e-4f) continue;
            // Two crossed quads along the segment, as the spirits' are (fx/spirits.cpp).
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
                for (int k = 0; k < 3; ++k) sprite.colour[k] = joint.tint[k] * light;
                sprite.sheet = joint_;
                // MU's RENDER_TYPE_ALPHA_BLEND; added here, as its light is the picture (ours).
                sprite.blend = gfx::Blend::Additive;
                effects.add(sprite);
            }
        }
    }
}

}  // namespace mu::game
