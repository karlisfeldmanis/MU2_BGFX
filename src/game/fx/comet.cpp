#include "game/fx/comet.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {
constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;
}  // namespace

float Comet::nearness(const float at[3], const float* eye) {
    if (eye == nullptr) return 1.0f;
    float d = 0.0f;
    for (int k = 0; k < 3; ++k) d += (at[k] - eye[k]) * (at[k] - eye[k]);
    const float t = std::clamp((std::sqrt(d) - kNearGone) / (kNearWhole - kNearGone), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Comet::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Comet::open(const std::string& assetDir, content::Textures& textures,
                 const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/comet/";
    mesh_.clear();
    loadEffectObj(dir + "Blast01.obj", kUnit, "ring2", mesh_);
    if (core::fileExists(dir + "ring2.png")) {
        sheet_ = textures.load(dir + "ring2.png", content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* trail = table.effect("comet_trail")) {
        trailSheet_ = textures.load(assetDir + "/" + trail->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* flash = table.effect("light")) {
        flashSheet_ = textures.load(assetDir + "/" + flash->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* smoke = table.effect("smoke01")) {
        smokeSheet_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    }
    core::logf("comet: Blast01 %zu triangles, sheet %s, trail %s, flash %s", mesh_.size() / 3,
               bgfx::isValid(sheet_) ? "yes" : "NO", bgfx::isValid(trailSheet_) ? "yes" : "NO",
               bgfx::isValid(flashSheet_) ? "yes" : "NO");
    return !mesh_.empty() && bgfx::isValid(sheet_);
}

void Comet::shutdown() {
    mesh_.clear();
    for (Live& one : comets_) one.alive = false;
    for (Flash& one : flashes_) one.alive = false;
    for (Puff& one : puffs_) one.alive = false;
}

void Comet::cast(float x, float z, uint32_t attacker, float weight, float fallSeconds) {
    if (mesh_.empty() || ground_ == nullptr) return;
    Live* slot = nullptr;
    for (Live& one : comets_) {
        if (!one.alive) {
            slot = &one;
            break;
        }
    }
    if (slot == nullptr) {
        ++refused_;
        return;
    }
    *slot = Live{};
    slot->alive = true;
    slot->attacker = attacker;
    slot->weight = std::sqrt(std::max(weight, 0.01f));
    slot->size = between(kSmallest, kLargest) / std::sqrt(2.0f) * slot->weight;
    slot->floorY = ground_->heightAt(x, z);
    slot->left = std::max(fallSeconds, 0.05f);
    // Up in MU's band and east of the spot by the slant's tangent -- MU's comets all come in
    // from +x, `Position[0] += rand() % 100 + 200` -- so it lands on the ground the realm named
    // in the time it was given, along MU's own heading.
    const float lift = between(kLowest, kHighest) * kUnit;
    const float side = lift * std::tan(kSlantDegrees * kPi / 180.0f);
    slot->at[0] = x + side;
    slot->at[1] = slot->floorY + lift;
    slot->at[2] = z;
    slot->velocity[0] = -side / slot->left;
    slot->velocity[1] = -lift / slot->left;
    slot->velocity[2] = 0.0f;
    for (int k = 0; k < 3; ++k) slot->tails[0][k] = slot->at[k];
    slot->tailCount = 1;
    slot->tailDue = 1.0f;
}

void Comet::update(float seconds, std::vector<Landing>& landings) {
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    for (Live& one : comets_) {
        if (!one.alive) continue;
        if (one.landed) {
            // Its ribbon after it: the tails run on into the ground, so the trail draws itself
            // down into the landing and fades, rather than vanishing with the head.
            one.linger -= frames;
            if (one.linger <= 0.0f) one.alive = false;
        } else {
            const float step = std::fmin(seconds, one.left);
            for (int k = 0; k < 3; ++k) one.at[k] += one.velocity[k] * step;
            one.left -= seconds;
        }
        // A tail a reference frame, newest first, ten kept.
        one.tailDue -= frames;
        while (one.tailDue <= 0.0f) {
            one.tailDue += 1.0f;
            for (int t = std::min(one.tailCount, kTails - 1); t > 0; --t) {
                for (int k = 0; k < 3; ++k) one.tails[t][k] = one.tails[t - 1][k];
            }
            one.tailCount = std::min(one.tailCount + 1, kTails);
        }
        for (int k = 0; k < 3; ++k) one.tails[0][k] = one.at[k];
        if (one.landed || one.left > 0.0f) continue;
        // On the ground: the glow here, the rest by the caller, and the ribbon a while yet.
        one.landed = true;
        one.linger = kTrailLinger;
        one.at[1] = one.floorY;
        landings.push_back({one.at[0], one.floorY, one.at[2], one.weight, one.attacker});
        for (Flash& flash : flashes_) {
            if (flash.alive) continue;
            flash.alive = true;
            flash.at[0] = one.at[0];
            flash.at[1] = one.floorY + kFlashLift * kUnit;
            flash.at[2] = one.at[2];
            flash.size = kFlashUnits * kUnit * one.weight;
            flash.spin = 0.0f;
            flash.left = kFlashFrames;
            break;
        }
        const int count = std::max(2, int(std::lround(kPuffsALanding * one.weight)));
        int puffs = count;
        for (Puff& puff : puffs_) {
            if (puffs == 0) break;
            if (puff.alive) continue;
            const int nth = count - puffs;
            --puffs;
            const float turn = unit() * kTwoPi;
            const float reach = kPuffSpread * std::sqrt(unit());
            puff = Puff{};
            puff.alive = true;
            puff.at[0] = one.at[0] + std::cos(turn) * reach;
            puff.at[1] = one.floorY + 0.1f;
            puff.at[2] = one.at[2] + std::sin(turn) * reach;
            puff.spin = unit() * kTwoPi;
            puff.wait = kPuffWaits * (float(nth) + between(0.8f, 1.2f));
            puff.weight = one.weight;
        }
    }
    for (Flash& one : flashes_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        if (one.wait > 0.0f) {
            one.wait -= frames;
            continue;
        }
        one.age += frames;
        one.at[1] += kPuffRise * frames;
        one.spin += 0.01f * frames;
        if (one.age >= kPuffFrames) one.alive = false;
    }
}

void Comet::gatherRibbon(gfx::Effects& effects, const Live& comet, const float* eye,
                         float fade) const {
    if (!bgfx::isValid(trailSheet_) || comet.tailCount < 2 || fade <= 0.0f) return;
    const float half = kRibbonUnits * kUnit * 0.5f * comet.weight;
    for (int t = 0; t + 1 < comet.tailCount; ++t) {
        const float* a = comet.tails[t];
        const float* b = comet.tails[t + 1];
        const float along[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        // Across the segment and the line to the eye, so the ribbon faces the camera.
        float toEye[3] = {0.0f, 1.0f, 0.0f};
        if (eye != nullptr) {
            for (int k = 0; k < 3; ++k) toEye[k] = eye[k] - a[k];
        }
        float side[3] = {along[1] * toEye[2] - along[2] * toEye[1],
                         along[2] * toEye[0] - along[0] * toEye[2],
                         along[0] * toEye[1] - along[1] * toEye[0]};
        const float length = std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
        if (length < 1e-5f) continue;
        for (float& s : side) s *= half / length;
        gfx::Sprite sprite;
        sprite.placed = true;
        const float* ends[2] = {a, b};
        for (int c = 0; c < 4; ++c) {
            const float* at = ends[c == 0 || c == 3 ? 0 : 1];
            const float s = (c == 0 || c == 1) ? -1.0f : 1.0f;
            for (int k = 0; k < 3; ++k) sprite.corner[c][k] = at[k] + side[k] * s;
            // The sheet's bright end at the comet, its dark end at the last tail it HAS, as
            // MU's `(NumTails - j) / (MaxTails - 1)` reaches 0 at its last: over all ten, a
            // comet falls in nine frames and its trail never reached the dark end, so it
            // stopped bright and square.
            sprite.cornerUv[c][0] =
                1.0f - float(t + (c == 0 || c == 3 ? 0 : 1)) / float(comet.tailCount - 1);
            sprite.cornerUv[c][1] = s < 0.0f ? 0.0f : 1.0f;
        }
        for (int k = 0; k < 3; ++k) sprite.position[k] = (a[k] + b[k]) * 0.5f;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = fade * kRibbonTint[k];
        sprite.sheet = trailSheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) return;
    }
}

void Comet::gather(gfx::Effects& effects, const float* eye) const {
    constexpr float kWhite[3] = {1.0f, 1.0f, 1.0f};
    // MU's own frame, AngleMatrix(0, 20, 0): twenty degrees about MU's y, which is our -z.
    // The mesh keeps its built-in lean north; built from the velocity instead, the mesh turned
    // a quarter about its length and that lean cancelled the slant, so the rays stood upright
    // over a slanted ribbon (2026-10-06).
    const float slant = kSlantDegrees * kPi / 180.0f;
    const float x[3] = {std::cos(slant), -std::sin(slant), 0.0f};
    const float y[3] = {std::sin(slant), std::cos(slant), 0.0f};
    const float z[3] = {0.0f, 0.0f, 1.0f};
    for (const Live& one : comets_) {
        if (!one.alive) continue;
        if (one.landed) {
            gatherRibbon(effects, one, eye, nearness(one.at, eye) * one.linger / kTrailLinger);
            continue;
        }
        const float fade = nearness(one.at, eye);
        if (fade <= 0.0f) continue;
        gatherRibbon(effects, one, eye, fade);
        const float lit[3] = {kWhite[0] * fade, kWhite[1] * fade, kWhite[2] * fade};
        submitEffectAlong(effects, mesh_, sheet_, gfx::Blend::Additive, one.at, x, y, z,
                          one.size, lit, 1.0f);
    }
    if (!bgfx::isValid(flashSheet_)) return;
    for (const Flash& one : flashes_) {
        if (!one.alive) continue;
        const float fade = std::clamp(one.left / kFlashFrames, 0.0f, 1.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = one.size * (1.3f - 0.5f * fade) * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = fade * kFlashPeak * kFlashTint[k];
        sprite.sheet = flashSheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) return;
    }
    if (!bgfx::isValid(smokeSheet_)) return;
    for (const Puff& one : puffs_) {
        if (!one.alive || one.wait > 0.0f) continue;
        const float t = one.age / kPuffFrames;
        // Opens over its first fifth and goes out over the rest.
        const float open = std::min(1.0f, t * 5.0f);
        const float out = std::clamp((1.0f - t) / 0.8f, 0.0f, 1.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight =
            (kPuffSize[0] + (kPuffSize[1] - kPuffSize[0]) * t) * one.weight * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kPuffTint[k] * open * out;
        sprite.sheet = smokeSheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) return;
    }
}

uint32_t Comet::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Live& one : comets_) {
        if (!one.alive || one.landed || count >= max) continue;
        gfx::PointLight& light = out[count++];
        light.position[0] = one.at[0];
        light.position[1] = one.floorY;
        light.position[2] = one.at[2];
        light.reach = kGlowTiles * metres() * one.weight;
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kGlow[k];
    }
    for (const Flash& one : flashes_) {
        if (!one.alive || count >= max) continue;
        const float fade = std::clamp(one.left / kFlashFrames, 0.0f, 1.0f);
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = one.at[k];
        light.reach = kGlowTiles * metres();
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kGlow[k] * kFlashGlow * fade;
    }
    return count;
}

}  // namespace mu::game
