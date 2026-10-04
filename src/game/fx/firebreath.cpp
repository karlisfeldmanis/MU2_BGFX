#include "game/fx/firebreath.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {

uint32_t FireBreath::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

bool FireBreath::open(const std::string& assetDir, content::Textures& textures,
                      const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("firebreath: no cooked effect named '%s'", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    flameSheet_ = take("fire03");      // BITMAP_FIRE + 2, Effect/Fire03.jpg
    blastSheet_ = take("dino_blast");  // BITMAP_EXPLOTION + 1, Effect/DinoE.jpg
    sparks_.open(assetDir, textures, table, ground);
    return bgfx::isValid(flameSheet_) || bgfx::isValid(blastSheet_);
}

void FireBreath::clear() {
    for (Carrier& one : carriers_) one = Carrier{};
    for (Flame& one : flames_) one = Flame{};
    for (Blast& one : blasts_) one = Blast{};
}

void FireBreath::cast(const float feet[3], float yaw, const float to[3], float air) {
    Carrier* carrier = nullptr;
    for (Carrier& one : carriers_) {
        if (!one.alive) {
            carrier = &one;
            break;
        }
    }
    if (carrier == nullptr) return;
    *carrier = Carrier{};
    carrier->alive = true;
    // At the body, flat; his facing if it stands on him.
    float ahead[2] = {to[0] - feet[0], to[2] - feet[2]};
    float far = std::sqrt(ahead[0] * ahead[0] + ahead[1] * ahead[1]);
    if (far < 0.01f) {
        ahead[0] = std::sin(yaw);
        ahead[1] = std::cos(yaw);
        far = 0.0f;
    } else {
        ahead[0] /= far;
        ahead[1] /= far;
    }
    const float right[2] = {ahead[1], -ahead[0]};
    for (int k = 0; k < 2; ++k) {
        carrier->ahead[k] = ahead[k];
        carrier->right[k] = right[k];
    }
    // Twenty units ahead of him and fifty up.
    carrier->at[0] = feet[0] + ahead[0] * kStartAhead * kUnit;
    carrier->at[1] = feet[1] + kStartUp * kUnit;
    carrier->at[2] = feet[2] + ahead[1] * kStartAhead * kUnit;
    // As long as the realm holds the blow, within MU's ten; its last frame is the burst, so it
    // steps one fewer times than it lives.
    const int frames = std::clamp(int(std::lround(air * kReferenceFps)), kLeastFrames, kLife);
    carrier->life = carrier->frames = frames;
    const float run = std::max(0.0f, far - kStartAhead * kUnit);
    const float each = run / float(frames - 1);
    carrier->step[0] = ahead[0] * each;
    carrier->step[2] = ahead[1] * each;
    // The two sprays, off his feet as the carrier's start is.
    for (float side : {-kNostrilLeft, kNostrilRight}) {
        const float at[3] = {feet[0] + (right[0] * side + ahead[0] * kNostrilAhead) * kUnit,
                             feet[1] + kNostrilUp * kUnit,
                             feet[2] + (right[1] * side + ahead[1] * kNostrilAhead) * kUnit};
        spray(at, ahead, right);
    }
}

void FireBreath::spray(const float at[3], const float ahead[2], const float right[2]) {
    // JOINT_SPARK subtype 1: (0, -Velocity, 0) through Angle[0] of 5 to 24 degrees and Angle[1]
    // stepped round the ring -- a cone about his facing that far open.
    const float turn = float(roll() % 360u) * kDegrees;
    for (int i = 0; i < kSparksPerRing; ++i) {
        const float open = float(roll() % 20u + 5u) * kDegrees;
        const float round = turn + float(i) * (360.0f / float(kSparksPerRing)) * kDegrees;
        const float out = std::sin(open), along = std::cos(open);
        const float side = std::cos(round) * out, up = std::sin(round) * out;
        const float v = float(roll() % 20u + 16u) * kUnit;
        const float velocity[3] = {(ahead[0] * along + right[0] * side) * v, up * v,
                                   (ahead[1] * along + right[1] * side) * v};
        sparks_.dart(at, velocity, float(roll() % 4u + 4u));
    }
}

void FireBreath::flame(const float at[3], const Carrier& carrier, float scale) {
    if (!bgfx::isValid(flameSheet_)) return;
    for (Flame& one : flames_) {
        if (one.alive) continue;
        one = Flame{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        // Thrown 3.2 to 4.7 units a frame along the facing it was born with.
        const float speed = float(roll() % 16u + 32u) * 0.1f * kUnit;
        one.velocity[0] = carrier.ahead[0] * speed;
        one.velocity[1] = carrier.ahead[1] * speed;
        one.scale = scale;
        return;
    }
}

void FireBreath::gatherEffects(gfx::Effects& effects, const float eye[3], const float near[3],
                               float daylight) const {
    sparks_.gather(effects, eye, near, daylight);
    for (const Flame& one : flames_) {
        if (!one.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        // RenderSprite at the sheet's width a quarter (a cell, 64 units) by its height, times
        // Scale.
        sprite.halfWidth = sprite.halfHeight = kFlamePixels * one.scale * kUnit * 0.5f;
        const int cell = std::clamp(int((23.0f - one.life) / 6.0f), 0, kFlameCells - 1);
        sprite.u0 = float(cell) / float(kFlameCells);
        sprite.u1 = float(cell + 1) / float(kFlameCells);
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kFlameGain;
        sprite.sheet = flameSheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) return;
    }
    for (const Blast& one : blasts_) {
        if (!one.alive || !bgfx::isValid(blastSheet_)) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = kFlamePixels * kBlastScale * kUnit * 0.5f;
        const int cell = std::clamp(int((kBlastLife - one.life) / 3.0f), 0, kBlastCells - 1);
        sprite.u0 = float(cell) / float(kBlastCells);
        sprite.u1 = float(cell + 1) / float(kBlastCells);
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kBlastGain;
        sprite.sheet = blastSheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) return;
    }
}

uint32_t FireBreath::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    const float metres = ground_ ? ground_->metresPerTile() : 1.0f;
    for (const Carrier& carrier : carriers_) {
        if (!carrier.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = carrier.at[k];
        light.reach = kLightTiles * metres;
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kLightColour[k] * kLightGain;
    }
    return count;
}

}  // namespace mu::game
