#include "game/fx/storm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

float Storm::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Storm::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/storm/";
    keyCount_ = 0;
    size_t most = 0;
    for (int k = 0; k < kKeys; ++k) {
        char name[32];
        std::snprintf(name, sizeof(name), "Storm01_%02d.obj", k);
        poses_[k].clear();
        if (!loadEffectObj(dir + name, kUnit, "", poses_[k])) break;
        // Blended key to key, so every pose must be the same triangles in the same order.
        if (k > 0 && poses_[k].size() != poses_[0].size()) break;
        most = std::max(most, poses_[k].size());
        keyCount_ = k + 1;
    }
    blended_.assign(most, EffectCorner{});
    // Scrolled along U, so it wraps: an albedo, not a decal.
    if (core::fileExists(dir + "typhoon.png")) {
        sheet_ = textures.load(dir + "typhoon.png", content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* smoke = table.effect("smoke01")) {
        smoke_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    }
    core::logf("storm: %d keys, %zu triangles, sheets %s", keyCount_,
               keyCount_ > 0 ? poses_[0].size() / 3 : size_t(0),
               bgfx::isValid(sheet_) && bgfx::isValid(smoke_) ? "yes" : "NO");
    return keyCount_ > 0 && bgfx::isValid(sheet_);
}

void Storm::clear() {
    for (Whirl& one : whirls_) one = Whirl{};
    for (Puff& one : puffs_) one = Puff{};
}

void Storm::cast(const float feet[3], float wayX, float wayZ) {
    if (keyCount_ == 0) return;
    for (Whirl& whirl : whirls_) {
        if (whirl.alive) continue;
        whirl = Whirl{};
        whirl.alive = true;
        for (int k = 0; k < 3; ++k) whirl.at[k] = feet[k];
        whirl.way[0] = wayX;
        whirl.way[1] = wayZ;
        // The model's loaded Z (MU's -Y, the way Direction points) turned onto the way it walks.
        whirl.yaw = std::atan2(wayX, wayZ);
        whirl.left = kFrames;
        return;
    }
}

void Storm::puff(const Whirl& whirl) {
    if (!bgfx::isValid(smoke_)) return;
    for (Puff& one : puffs_) {
        if (one.alive) continue;
        one = Puff{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = whirl.at[k];
        // (0, -(40..47), 0) turned by a pitch of -45..45 and a random yaw.
        const float pitch = between(-45.0f, 45.0f) * kTwoPi / 360.0f;
        const float turn = unit() * kTwoPi;
        const float speed = between(40.0f, 47.0f) * kUnit * kReferenceFps;
        const float flat = speed * std::cos(pitch);
        one.velocity[0] = std::cos(turn) * flat;
        one.velocity[1] = speed * std::sin(pitch);
        one.velocity[2] = std::sin(turn) * flat;
        one.size = between(0.80f, 1.11f);
        one.left = 10.0f;
        one.spin = unit() * kTwoPi;
        return;
    }
}

void Storm::gatherEffects(gfx::Effects& effects) const {
    const float up[3] = {0.0f, 1.0f, 0.0f};
    for (const Whirl& whirl : whirls_) {
        if (!whirl.alive || keyCount_ == 0 || !bgfx::isValid(sheet_)) continue;
        // BMD::Animation's two keys, weighted by the frame's fraction.
        const int from = std::clamp(int(whirl.key), 0, keyCount_ - 1);
        const int to = std::min(from + 1, keyCount_ - 1);
        const float t = whirl.key - float(from);
        const std::vector<EffectCorner>& a = poses_[from];
        const std::vector<EffectCorner>& b = poses_[to];
        for (size_t i = 0; i < a.size(); ++i) {
            EffectCorner& out = blended_[i];
            out.x = a[i].x + (b[i].x - a[i].x) * t;
            out.y = a[i].y + (b[i].y - a[i].y) * t;
            out.z = a[i].z + (b[i].z - a[i].z) * t;
            out.u = a[i].u;
            out.v = a[i].v;
        }
        const float s = std::sin(whirl.yaw), c = std::cos(whirl.yaw);
        const float x[3] = {c, 0.0f, -s};
        const float z[3] = {s, 0.0f, c};
        const float light = std::min(1.0f, whirl.left * 0.1f);
        const float colour[3] = {light, light, light};
        // `blended_` is the size of the largest pose and every pose is that size (open), so the
        // whole of it is this key's.
        submitEffectAlong(effects, blended_, sheet_, gfx::Blend::Additive, whirl.at, x, up, z,
                          kScale, colour, 1.0f, -whirl.left * 0.1f);
    }
    for (const Puff& one : puffs_) {
        if (!one.alive) continue;
        const float lum = std::min(1.0f, one.left / 8.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * 0.5f;
        sprite.spin = one.spin;
        sprite.colour[0] = lum * 0.8f;
        sprite.colour[1] = lum * 0.8f;
        sprite.colour[2] = lum;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

uint32_t Storm::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Whirl& whirl : whirls_) {
        if (!whirl.alive || count >= max) continue;
        // Up over its first five frames, then the funnel's own `LifeTime * 0.1`.
        const float lit = std::min({1.0f, whirl.left * 0.1f, (kFrames - whirl.left) * 0.2f});
        if (lit <= 0.0f) continue;
        gfx::PointLight& one = out[count++];
        for (int k = 0; k < 3; ++k) one.position[k] = whirl.at[k];
        one.reach = kGlowTiles * metres();
        one.height = 1.0f;
        for (int k = 0; k < 3; ++k) one.colour[k] = kGlow[k] * lit;
    }
    return count;
}

}  // namespace mu::game
