#include "game/fx/recall.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

float Recall::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Recall::open(const std::string& assetDir, content::Textures& textures,
                  const content::Ground* ground) {
    ground_ = ground;
    const auto sheet = [&](const std::string& path, content::TextureRole role) {
        return core::fileExists(assetDir + path) ? textures.load(assetDir + path, role)
                                                 : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    // Hellfire's two meshes: the star's quarter runs to its sheet's edges, so it is clamped; the
    // wall's sheet scrolls along U, so wraps (fx/hellfire.cpp).
    sigil_.tris.clear();
    wall_.tris.clear();
    loadEffectObj(assetDir + "/effects/hellfire/Circle01.obj", kUnit, "", sigil_.tris);
    loadEffectObj(assetDir + "/effects/hellfire/Circle02.obj", kUnit, "", wall_.tris);
    sigil_.sheet = sheet("/effects/recall/magic_b.png", content::TextureRole::Decal);
    wall_.sheet = sheet("/effects/hellfire/magic_a02.png", content::TextureRole::Albedo);
    blueSheet_ = sheet("/effects/defense/flare_blue.png", content::TextureRole::Decal);
    handSheet_ = sheet("/effects/light/flare01.png", content::TextureRole::Decal);
    const bool ok = !sigil_.tris.empty() && !wall_.tris.empty() && bgfx::isValid(sigil_.sheet) &&
                    bgfx::isValid(wall_.sheet) && bgfx::isValid(blueSheet_) && bgfx::isValid(handSheet_);
    core::logf("recall: circles and sheets %s", ok ? "yes" : "NO");
    return ok;
}

void Recall::clear() {
    for (Charge& one : charges_) one = Charge{};
    for (Ring& one : rings_) one = Ring{};
    for (Spark& one : sparks_) one = Spark{};
    for (Pillar& one : pillars_) one = Pillar{};
    for (Holder& one : holders_) one = Holder{};
}

void Recall::begin(uint32_t who) {
    for (Charge& one : charges_) {
        if (one.alive) continue;
        one = Charge{true, who, kLetGo};
        return;
    }
}

void Recall::holding(uint32_t who) {
    Holder* free = nullptr;
    for (Holder& one : holders_) {
        if (one.who == who) {
            one.seen = true;
            return;
        }
        if (one.who == 0 && free == nullptr) free = &one;
    }
    if (free != nullptr) *free = Holder{who, true, 0.0f};
}

void Recall::glitter(const float hand[3]) {
    for (Spark& one : sparks_) {
        if (one.alive) continue;
        one = Spark{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = hand[k];
        one.hand = true;
        one.life = 10.0f + float(int(unit() * 10.0f));
        one.scale = 0.5f + unit() * 0.5f;
        one.rise = 2.5f;
        one.colour[0] = 0.3f;
        one.colour[1] = 0.5f;
        one.colour[2] = 1.0f;
        return;
    }
}

void Recall::spawn(const Ring& ring) {
    // A point within 200 units of the middle, MU's `rand() % 200` each way.
    const float m = metre();
    const float at[3] = {ring.at[0] + (unit() * 2.0f - 1.0f) * kReach * m, ring.at[1],
                         ring.at[2] + (unit() * 2.0f - 1.0f) * kReach * m};
    if (ring.life > kSparkLast) {
        for (Spark& one : sparks_) {
            if (one.alive) continue;
            one = Spark{};
            one.alive = true;
            for (int k = 0; k < 3; ++k) one.at[k] = at[k];
            one.life = 30.0f + float(int(unit() * 10.0f));
            one.rise = float(int(unit() * 3.0f));
            break;
        }
    }
    if (ring.life > kPillarLast) {
        for (Pillar& one : pillars_) {
            if (one.alive) continue;
            one = Pillar{};
            one.alive = true;
            for (int k = 0; k < 3; ++k) one.head[k] = at[k];
            one.head[1] += kPillarHigh * m;
            one.life = 25.0f + float(int(unit() * 50.0f));
            one.fall = 35.0f + float(int(unit() * 20.0f));
            break;
        }
    }
}

void Recall::step(float frames) {
    const float m = metre();
    for (Ring& ring : rings_) {
        if (!ring.alive) continue;
        // One frame in two, in whole reference frames as MU steps it.
        ring.due += frames;
        while (ring.due >= 1.0f) {
            ring.due -= 1.0f;
            ring.life -= 1.0f;
            if (unit() < 0.5f) spawn(ring);
        }
        if (ring.life <= 0.0f) ring.alive = false;
    }
    for (Spark& one : sparks_) {
        if (!one.alive) continue;
        if (one.hand) {
            one.at[1] += one.rise * m * frames;
        } else {
            // Up from 0-2 a frame, quicker by 0.4 a frame to 8.
            one.at[1] += one.rise * m * frames;
            one.rise = std::min(8.0f, one.rise + 0.4f * frames);
            if (one.life < 5.0f) one.light *= std::pow(0.98f, frames);
        }
        one.life -= frames;
        if (one.life <= 0.0f) one.alive = false;
    }
    for (Pillar& one : pillars_) {
        if (!one.alive) continue;
        one.due += frames;
        while (one.alive && one.due >= 1.0f) {
            one.due -= 1.0f;
            // The tails follow the head, the newest first.
            for (int i = std::min(one.count, kTails - 1); i > 0; --i) {
                for (int k = 0; k < 3; ++k) one.tails[i][k] = one.tails[i - 1][k];
            }
            for (int k = 0; k < 3; ++k) one.tails[0][k] = one.head[k];
            one.count = std::min(one.count + 1, kTails);
            // Hung until its life is 25, then down, 5 a frame quicker each frame. **Ours**: it
            // stops on the ground, where MU's goes on under it unseen.
            if (one.life <= 25.0f) {
                const float floor = ground_ ? ground_->heightAt(one.head[0], one.head[2]) : 0.0f;
                one.head[1] = std::max(floor, one.head[1] - one.fall * m);
                one.fall += 5.0f;
            }
            one.life -= 1.0f;
            if (one.life <= 0.0f) one.alive = false;
        }
    }
}

void Recall::gatherEffects(gfx::Effects& effects) const {
    const float m = metre();
    const float up[3] = {0.0f, 1.0f, 0.0f};
    for (const Ring& ring : rings_) {
        if (!ring.alive) continue;
        // MU's yaw turns the model's X to (cos, -sin) and its loaded Z, MU's -Y, to (sin, cos).
        const float s = std::sin(ring.yaw), c = std::cos(ring.yaw);
        const float x[3] = {c, 0.0f, -s};
        const float z[3] = {s, 0.0f, c};
        const float light = lightOf(ring.life);
        // Each channel the light times MU's colour, clamped at one as glColor clamps it: the star
        // in its own blue from 0.4 s in, going (0.5, 0.5, 1) as it fades.
        float sigil[3], wall[3];
        const float walled = std::min(light, kWallCap);
        for (int k = 0; k < 3; ++k) {
            sigil[k] = std::min(1.0f, kSigilColour[k] * light);
            wall[k] = std::min(1.0f, kWallColour[k] * walled);
        }
        if (light > 0.0f) {
            submitEffectAlong(effects, sigil_.tris, sigil_.sheet, gfx::Blend::Additive, ring.at, x, up, z,
                              1.0f, sigil, 1.0f, 0.0f);
            submitEffectAlong(effects, wall_.tris, wall_.sheet, gfx::Blend::Additive, ring.at, x, up, z,
                              1.0f, wall, 1.0f, -float(int(ring.life)) * 0.01f);
        }
    }
    for (const Spark& one : sparks_) {
        if (!one.alive) continue;
        gfx::Sprite spark;
        for (int k = 0; k < 3; ++k) spark.position[k] = one.at[k];
        spark.halfWidth = spark.halfHeight = one.scale * 32.0f * m;
        const float fade = one.hand ? std::min(1.0f, one.life / 5.0f) : one.light;
        for (int k = 0; k < 3; ++k) spark.colour[k] = one.colour[k] * fade;
        spark.sheet = one.hand ? handSheet_ : blueSheet_;
        spark.blend = gfx::Blend::Additive;
        effects.add(spark);
    }
    // The pillars: two upright bands crossed down each tail, flareBlue across their width.
    for (const Pillar& one : pillars_) {
        if (!one.alive || one.count < 2) continue;
        for (int i = 0; i + 1 < one.count; ++i) {
            const float* a = one.tails[i];
            const float* b = one.tails[i + 1];
            const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
            if (dx * dx + dy * dy + dz * dz < 1e-8f) continue;
            const float u0 = float(i) / float(one.count - 1), u1 = float(i + 1) / float(one.count - 1);
            const float half = kPillarWidth * 0.5f * m;
            const float across[2][3] = {{half, 0.0f, 0.0f}, {0.0f, 0.0f, half}};
            for (const auto& side : across) {
                gfx::Sprite band;
                band.placed = true;
                band.sheet = blueSheet_;
                band.blend = gfx::Blend::Additive;
                for (int k = 0; k < 3; ++k) {
                    band.corner[0][k] = a[k] - side[k];
                    band.corner[1][k] = b[k] - side[k];
                    band.corner[2][k] = b[k] + side[k];
                    band.corner[3][k] = a[k] + side[k];
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
                for (int k = 0; k < 3; ++k) band.colour[k] = 1.0f;
                band.colour[3] = 1.0f;
                effects.add(band);
            }
        }
    }
}

uint32_t Recall::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    const float metres = ground_ ? ground_->metresPerTile() : 1.0f;
    for (const Ring& ring : rings_) {
        if (!ring.alive || count >= max) continue;
        // White times the wall's light, four tiles round (AddTerrainLight, MoveHandlers.cpp:3865-3897).
        const float light = std::min(lightOf(ring.life), kWallCap);
        if (light <= 0.0f) continue;
        gfx::PointLight& one = out[count++];
        for (int k = 0; k < 3; ++k) one.position[k] = ring.at[k];
        one.reach = kGlowTiles * metres;
        one.height = 1.0f;
        for (int k = 0; k < 3; ++k) one.colour[k] = light;
    }
    return count;
}

}  // namespace mu::game
