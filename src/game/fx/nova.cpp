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
    // BITMAP_JOINT_HEALING (JointEnergy01) and BITMAP_SHINY + 1 (Shiny02), cooked for the elf's
    // summon (pipeline/index.py).
    const content::EffectSheet* streak = table.effect("joint_energy");
    const content::EffectSheet* shiny = table.effect("shiny_02");
    if (joint == nullptr || light == nullptr || streak == nullptr || shiny == nullptr) {
        core::logError("nova: no cooked effect named 'joint_spirit', 'light', 'joint_energy' or "
                       "'shiny_02'");
        return false;
    }
    joint_ = textures.load(assetDir + "/" + joint->path, content::TextureRole::Albedo);
    light_ = textures.load(assetDir + "/" + light->path, content::TextureRole::Albedo);
    streak_ = textures.load(assetDir + "/" + streak->path, content::TextureRole::Albedo);
    shiny_ = textures.load(assetDir + "/" + shiny->path, content::TextureRole::Albedo);
    return bgfx::isValid(joint_) && bgfx::isValid(light_) && bgfx::isValid(streak_) &&
           bgfx::isValid(shiny_);
}

uint32_t Nova::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

// How many of something MU sheds `perFrame` a reference frame to shed on this drawn frame: the
// whole part, and the rest by the dice, so a fast screen sheds MU's count a second.
int Nova::many(float perFrame) {
    const float want = perFrame * frames_;
    int n = int(want);
    if (unit() < want - float(n)) ++n;
    return n;
}

Nova::Glow* Nova::glowSlot(Glow* pool, int size) {
    Glow* slot = nullptr;
    for (int i = 0; i < size; ++i) {
        Glow& glow = pool[i];
        if (!glow.alive) return &glow;
        if (slot == nullptr || glow.left < slot->left) slot = &glow;
    }
    return slot;
}

float Nova::flung(float frames, float push, float pace) {
    // Frame by frame, as MU steps it: the push loses the pace, the pace grows one, the body
    // goes the push; under none it holds, and only the first fifteen frames move it.
    float gone = 0.0f;
    const int whole = std::min(int(frames), kFlingFrames);
    for (int f = 0; f < whole; ++f) {
        push = std::max(0.0f, push - pace);
        pace += 1.0f;
        gone += push;
    }
    if (whole < kFlingFrames && frames > float(whole)) {
        gone += std::max(0.0f, push - pace) * (frames - float(whole));
    }
    return gone * kUnit;
}

void Nova::charge(const float* points, int count, int stage, const float feet[3],
                  const float tint[3]) {
    // `m_bySkillCount + 1` lights at each point a frame (kLightsPerBone at most, ours); the
    // pool's oldest give way.
    const float size = 1.3f + 0.08f * float(stage);
    const float each = float(std::min(stage + 1, kLightsPerBone));
    for (int p = 0; p < count; ++p) {
        for (int n = many(each); n > 0; --n) {
            Glow* slot = glowSlot(glows_, kChargeMost);
            *slot = Glow{};
            slot->alive = true;
            for (int k = 0; k < 3; ++k) {
                slot->at[k] = points[p * 3 + k];
                slot->tint[k] = tint[k] * kLightDim;
            }
            slot->size = size;
            slot->rise = kLightRise;
            slot->shrink = 0.95f;
            slot->left = kLightLife;
        }
    }
    // The force: three a frame from five metres out, homing on a metre over his feet.
    for (int n = many(float(kForcePerFrame)); n > 0; --n) {
        Force* slot = nullptr;
        for (Force& force : forces_) {
            if (!force.alive) {
                slot = &force;
                break;
            }
            if (slot == nullptr || force.left < slot->left) slot = &force;
        }
        *slot = Force{};
        slot->alive = true;
        // `Vector(rand() % 90, 0, rand() % 360)`: up to ninety degrees over level, any bearing.
        const float up = float(roll() % 90u) * kDegrees;
        const float round = float(roll() % 360u) * kDegrees;
        slot->to[0] = feet[0];
        slot->to[1] = feet[1] + kForceAim;
        slot->to[2] = feet[2];
        slot->at[0] = feet[0] + std::cos(up) * std::cos(round) * kForceFrom;
        slot->at[1] = feet[1] + std::sin(up) * kForceFrom + kForceLift;
        slot->at[2] = feet[2] + std::cos(up) * std::sin(round) * kForceFrom;
        slot->light[0] = slot->light[1] = 0.5f;
        slot->light[2] = float(roll() % 128u) / 255.0f + 0.5f;
    }
}

void Nova::sparkle(const float* points, int count, const float tint[3]) {
    if (count <= 0) return;
    for (int n = many(float(kSparksMost)); n > 0; --n) {
        Glow* slot = glowSlot(sparks_, kSparkPool);
        *slot = Glow{};
        slot->alive = true;
        const int p = int(roll() % uint32_t(count));
        for (int k = 0; k < 3; ++k) {
            slot->at[k] = points[p * 3 + k];
            slot->tint[k] = tint[k];
        }
        // `0.5 + (rand() % 100) / 50`.
        slot->size = 0.5f + float(roll() % 100u) / 50.0f;
        slot->shrink = 0.95f;
        slot->dim = 0.9f / 0.95f;
        slot->left = kSparkLife;
    }
}

void Nova::release(const float feet[3], int stage, const float tint[3]) {
    lastStage_ = stage;
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
        joint.ring = speed < kFast;
        const float cp = std::cos(kPitch * kDegrees);
        joint.way[0] = cp * std::cos(yaw * kDegrees);
        joint.way[1] = std::sin(kPitch * kDegrees);
        joint.way[2] = cp * std::sin(yaw * kDegrees);
        joint.speed = speed;
        joint.dice = roll();
        for (int k = 0; k < 3; ++k) {
            joint.at[k] = from[k];
            joint.from[k] = from[k];
            joint.tint[k] = tint[k];
        }
        return;
    }
}

void Nova::emit(Burst& burst) {
    const float from[3] = {burst.feet[0], burst.feet[1] + kOver, burst.feet[2]};
    // Sub-type 6: `i * (10 + rand() % 10)` round, one in five with a tail. The other four are
    // spawned all the same: they draw nothing, but each lays its shiny.
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
    frames_ = seconds * kFps;
    owed_ += frames_;
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
            joint.speed += kFaster;
            for (int k = 0; k < 3; ++k) joint.at[k] += joint.way[k] * joint.speed;
            joint.left -= 1.0f;
            if (joint.left <= 0.0f) joint.alive = false;
        }
        for (Force& force : forces_) {
            if (!force.alive) continue;
            for (int i = kForceTails - 1; i > 0; --i) {
                for (int k = 0; k < 3; ++k) force.tail[i][k] = force.tail[i - 1][k];
            }
            for (int k = 0; k < 3; ++k) force.tail[0][k] = force.at[k];
            force.tails = std::min(force.tails + 1, kForceTails);
            // Four units a frame faster each frame, straight at the mark and held there (MU's
            // MoveHumming turns ten degrees a frame onto it, which from straight on is straight).
            force.speed += kForceGain;
            const float d[3] = {force.to[0] - force.at[0], force.to[1] - force.at[1],
                                force.to[2] - force.at[2]};
            const float far = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            const float step = std::min(force.speed, far);
            if (far > 1e-4f) {
                for (int k = 0; k < 3; ++k) force.at[k] += d[k] / far * step;
            }
            force.left -= 1.0f;
            if (force.left < 6.0f) {
                for (float& l : force.light) l /= 1.8f;
            }
            if (force.left <= 0.0f) force.alive = false;
        }
        for (Glow* pool : {glows_, sparks_}) {
            const int size = pool == glows_ ? kChargeMost : kSparkPool;
            for (int i = 0; i < size; ++i) {
                Glow& glow = pool[i];
                if (!glow.alive) continue;
                glow.at[1] += glow.rise;
                glow.size *= glow.shrink;
                glow.light *= glow.shrink * glow.dim;
                glow.left -= 1.0f;
                if (glow.left <= 0.0f || glow.size <= 0.1f) glow.alive = false;
            }
        }
    }
}

// A ribbon through `points` (head first), one face across `side` or two crossed when `side` is
// null, the sheet's U running 1 at the head to 0 at the tail, as MU's tails do
// (ZzzEffectJoint.cpp:7104-7111).
void Nova::ribbon(gfx::Effects& effects, const float (*points)[3], int count, float half,
                  const float* side, const float colour[3], bgfx::TextureHandle sheet) const {
    for (int s = 0; s + 1 < count; ++s) {
        const float* a = points[s];
        const float* b = points[s + 1];
        const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
        if (length < 1e-4f) continue;
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
        const float* sides[2] = {side ? side : across, up};
        for (int f = 0; f < (side ? 1 : 2); ++f) {
            const float* way = sides[f];
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = 0.5f * (a[k] + b[k]);
            sprite.placed = true;
            for (int k = 0; k < 3; ++k) {
                sprite.corner[0][k] = b[k] - way[k] * half;
                sprite.corner[1][k] = a[k] - way[k] * half;
                sprite.corner[2][k] = a[k] + way[k] * half;
                sprite.corner[3][k] = b[k] + way[k] * half;
            }
            const float uv[4][2] = {{uTail, 1}, {uHead, 1}, {uHead, 0}, {uTail, 0}};
            for (int k = 0; k < 4; ++k) {
                sprite.cornerUv[k][0] = uv[k][0];
                sprite.cornerUv[k][1] = uv[k][1];
            }
            for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k];
            sprite.sheet = sheet;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
}

void Nova::gather(gfx::Effects& effects) const {
    if (!bgfx::isValid(joint_) || !bgfx::isValid(light_)) return;
    // The charge's lights and death's sparks: BITMAP_LIGHT, their light their tint times what
    // is left of it.
    for (const Glow* pool : {glows_, sparks_}) {
        const int size = pool == glows_ ? kChargeMost : kSparkPool;
        for (int i = 0; i < size; ++i) {
            const Glow& glow = pool[i];
            if (!glow.alive) continue;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = glow.at[k];
            sprite.halfWidth = sprite.halfHeight = glow.size * 0.25f;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = glow.tint[k] * glow.light;
            sprite.sheet = light_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    // The force: a thin streak crossed on itself, and a light on its head.
    for (const Force& force : forces_) {
        if (!force.alive) continue;
        float points[kForceTails + 1][3];
        for (int k = 0; k < 3; ++k) points[0][k] = force.at[k];
        for (int i = 0; i < force.tails; ++i) {
            for (int k = 0; k < 3; ++k) points[i + 1][k] = force.tail[i][k];
        }
        ribbon(effects, points, force.tails + 1, kForceWidth * 0.5f, nullptr, force.light,
               streak_);
        gfx::Sprite head;
        for (int k = 0; k < 3; ++k) head.position[k] = force.at[k];
        head.halfWidth = head.halfHeight = 0.25f;
        for (int k = 0; k < 3; ++k) head.colour[k] = force.light[k] * kForceHead;
        head.sheet = light_;
        head.blend = gfx::Blend::Additive;
        effects.add(head);
    }
    for (const Joint& joint : joints_) {
        if (!joint.alive) continue;
        // Its shiny where it was let go, in its last ten frames, grey at (6 - |LifeTime - 6|)
        // * 0.15, turned at random: one in kShinyEvery (ours).
        if (!joint.ring && joint.left <= 10.0f && joint.dice % kShinyEvery == 0) {
            const float grey = std::max(0.0f, (6.0f - std::fabs(joint.left - 6.0f)) * 0.15f);
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = joint.from[k];
            const float scale = float(8 + (joint.dice >> 8) % 8u) * 0.05f;
            // Shiny02 is 32 by 64.
            sprite.halfWidth = 0.32f * scale * 0.5f;
            sprite.halfHeight = 0.64f * scale * 0.5f;
            sprite.spin = float((joint.dice >> 12) % 360u) * kDegrees;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = grey;
            sprite.sheet = shiny_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
        if (!joint.tailed || joint.tails == 0) continue;
        float points[kTails + 1][3];
        for (int k = 0; k < 3; ++k) points[0][k] = joint.at[k];
        for (int i = 0; i < joint.tails; ++i) {
            for (int k = 0; k < 3; ++k) points[i + 1][k] = joint.tail[i][k];
        }
        // One face, across: RENDER_FACE_TWO is the tail's sideways pair (CreateTail's x axis).
        const float across[3] = {-joint.way[2], 0.0f, joint.way[0]};
        const float flat = std::sqrt(across[0] * across[0] + across[2] * across[2]);
        const float side[3] = {across[0] / flat, 0.0f, across[2] / flat};
        const float colour[3] = {joint.tint[0] * kSpokeDim, joint.tint[1] * kSpokeDim,
                                 joint.tint[2] * kSpokeDim};
        ribbon(effects, points, joint.tails + 1, kWidth * 0.5f, side, colour, joint_);
    }
}

}  // namespace mu::game
