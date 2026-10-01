#include "game/fx/wheel.h"

#include <algorithm>
#include <cmath>

#include "content/placement.h"
#include "core/log.h"

namespace mu::game {

float Wheel::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Wheel::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) return BGFX_INVALID_HANDLE;
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    smoke_ = take("smoke01");  // Effect/smoke01, BITMAP_SMOKE
    flare_ = take("light");    // Effect/flare01, BITMAP_LIGHT
    sparks_.open(assetDir, textures, table, ground);
    core::logf("wheel: smoke %s, flare %s", bgfx::isValid(smoke_) ? "yes" : "NO",
               bgfx::isValid(flare_) ? "yes" : "NO");
    return bgfx::isValid(smoke_) && bgfx::isValid(flare_);
}

void Wheel::clear() {
    for (Turn& turn : turns_) turn = Turn{};
    puffCount_ = 0;
}

void Wheel::cast(uint32_t owner, const content::Mesh* weapon, const ShineLook& shine,
                 bool polearm) {
    Turn* turn = nullptr;
    for (Turn& one : turns_) {
        if (!one.alive) {
            turn = &one;
            break;
        }
    }
    if (turn == nullptr) return;
    *turn = Turn{};
    turn->alive = true;
    turn->owner = owner;
    turn->weapon = weapon;
    turn->shine = shine;
    turn->polearm = polearm;
    turn->wait = kStartFrames;
}

void Wheel::place(Turn& turn, Copy& copy) const {
    // Its angle turns 18 a frame from his facing; in this engine's axes a yaw is MU's Angle[2]
    // unchanged (content/placement.h), so MU's (0, -150, 0) is our (0, 0, 1.5) turned by it.
    copy.orbit = turn.facing - kOrbitTurn * kDegrees * copy.age;
    const float radius = (turn.polearm ? kPoleOrbitUnits : kOrbitUnits) * kUnit;
    copy.foot[0] = turn.feet[0] + std::sin(copy.orbit) * radius;
    copy.foot[1] = turn.feet[1];
    copy.foot[2] = turn.feet[2] + std::cos(copy.orbit) * radius;
    // RenderWheelWeapon: raised 100, rolled 90, and its yaw the orbit's plus a spin that has
    // turned 30 a frame since the copy was made.
    const float raised[3] = {copy.foot[0], copy.foot[1] + kRaiseUnits * kUnit, copy.foot[2]};
    const float yaw = copy.orbit - kSpinTurn * kDegrees * copy.age;
    content::placementTransform(0.0f, yaw, 90.0f * kDegrees, turn.polearm ? kPoleScale : kScale,
                                raised, copy.transform);
}

void Wheel::emit(const Copy& copy) {
    // Four JOINT_SPARKs along its path, calmed to one; and SPARK motes, calmed to a coin.
    const float orbit = copy.orbit / kDegrees;
    sparks_.fling(copy.foot, -30.0f, orbit + 90.0f, true, unit() < kMoteChance);
    // SMOKE subtype 3: ten frames, Scale 0.8 to 1.11, thrown (0, -(40..47), 0) on a random
    // pitch of 45 either way and any yaw.
    if (!bgfx::isValid(smoke_) || puffCount_ >= kPuffs) return;
    Puff& puff = puffs_[puffCount_++];
    for (int k = 0; k < 3; ++k) puff.at[k] = copy.foot[k];
    puff.life = 10.0f;
    puff.scale = float(int(unit() * 32.0f) + 80) * 0.01f;
    const float pitch = (float(int(unit() * 90.0f)) - 45.0f) * kDegrees;
    const float heading = float(int(unit() * 360.0f)) * kDegrees;
    const float v = float(int(unit() * 8.0f) + 40) * kUnit;
    // (0, -v, 0) through AngleMatrix, (cp sy, -cp cy, -sp) * v, and MU's (x, y, z) is our
    // (x, z, -y).
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    puff.velocity[0] = cp * std::sin(heading) * v;
    puff.velocity[1] = -sp * v;
    puff.velocity[2] = cp * std::cos(heading) * v;
    puff.spin = unit() * 6.28318531f;
}

void Wheel::advance(float frames) {
    // MoveParticles: age, move, then the subtype's own -- velocity times 0.4 a frame, scale
    // up 0.1. Stated a frame at a time, so a drawn frame applies it as a power.
    const float keep = std::pow(0.4f, frames);
    int kept = 0;
    for (int i = 0; i < puffCount_; ++i) {
        Puff puff = puffs_[i];
        puff.life -= frames;
        if (puff.life <= 0.0f) continue;
        // The geometric sum over the frames this step covers, so the burst carries as far at
        // any frame rate.
        const float carried = frames > 0.0f ? (1.0f - keep) / (1.0f - 0.4f) : 0.0f;
        for (int k = 0; k < 3; ++k) {
            puff.at[k] += puff.velocity[k] * carried;
            puff.velocity[k] *= keep;
        }
        puff.scale += 0.1f * frames;
        puffs_[kept++] = puff;
    }
    puffCount_ = kept;
}

void Wheel::gather(std::vector<gfx::Drawable>& out) const {
    for (const Turn& turn : turns_) {
        if (!turn.alive || !turn.turning || turn.weapon == nullptr) continue;
        for (int i = 0; i < kCopies; ++i) {
            const Copy& copy = turn.copies[i];
            if (!copy.alive) continue;
            gfx::Drawable drawable;
            drawable.mesh = turn.weapon;
            std::copy(copy.transform, copy.transform + 16, drawable.transform);
            wear(turn.shine, drawable);
            drawable.fade = kAlpha[i];
            drawable.inProbe = false;
            out.push_back(drawable);
        }
    }
}

void Wheel::gatherEffects(gfx::Effects& effects, const float eye[3], const float near[3],
                          float daylight) const {
    sparks_.gather(effects, eye, near, daylight);
    if (bgfx::isValid(smoke_)) {
        for (int i = 0; i < puffCount_; ++i) {
            const Puff& puff = puffs_[i];
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = puff.at[k];
            sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * puff.scale;
            sprite.spin = puff.spin;
            // Light = LifeTime / 8 in (0.8, 0.8, 1), which glColor clamps at one.
            const float light = std::min(1.0f, puff.life / 8.0f);
            sprite.colour[0] = light * 0.8f;
            sprite.colour[1] = light * 0.8f;
            sprite.colour[2] = light;
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) return;
        }
    }
    if (!bgfx::isValid(flare_)) return;
    for (const Turn& turn : turns_) {
        if (!turn.alive || !turn.turning) continue;
        for (const Copy& copy : turn.copies) {
            if (!copy.alive) continue;
            gfx::Sprite sprite;
            sprite.position[0] = copy.foot[0];
            sprite.position[1] = copy.foot[1] + kFlareUnits * kUnit;
            sprite.position[2] = copy.foot[2];
            sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * kFlareScale;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = kFlareColour[k];
            sprite.sheet = flare_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) return;
        }
    }
}

uint32_t Wheel::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Turn& turn : turns_) {
        if (!turn.alive || !turn.turning || count >= max) continue;
        // MoveEffect's Luminosity, less a fifth for each of a copy's last five frames.
        float sum = 0.0f;
        for (const Copy& copy : turn.copies) {
            if (!copy.alive) continue;
            const float left = kCopyFrames - copy.age;
            sum += std::max(0.0f, turn.glow - std::max(0.0f, 5.0f - left) * 0.2f);
        }
        if (sum <= 0.0f) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = turn.feet[k];
        const float metres = ground_ ? ground_->metresPerTile() : 1.0f;
        light.reach = (kOrbitUnits * kUnit + kLightTiles * metres);
        light.height = 1.0f;
        // Five of MU's lights summed would pass the brightest of the others here (Thunder's 1);
        // held there.
        for (int k = 0; k < 3; ++k) light.colour[k] = std::min(1.0f, kLightPerCopy * sum);
    }
    return count;
}

}  // namespace mu::game
