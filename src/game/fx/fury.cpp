#include "game/fx/fury.h"

#include <algorithm>
#include <cmath>

#include "content/grid.h"
#include "content/placement.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// A fire's brightness, the ramp 02, 05 and 08 share: up from nothing over its first
// `rise` frames, then `LifeTime * 0.1` -- which glColor clamps at one.
float fireLight(float left, float frames, float rise) {
    const float light = left >= frames - rise ? (frames - left) * 0.1f : left * 0.1f;
    return std::clamp(light, 0.0f, 1.0f);
}

}  // namespace

uint32_t Fury::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

float Fury::floorAt(float x, float z) const {
    return (ground_ ? ground_->heightAt(x, z) : 0.0f) + kOverGround;
}

bool Fury::groundAt(float x, float z) const {
    if (ground_ == nullptr) return true;
    const float per = std::max(ground_->metresPerTile(), 0.001f);
    const int column = int(std::floor(x / per)), row = int(std::floor(-z / per));
    return (ground_->attributesAt(column, row) & (content::kNoMove | content::kNoGround)) == 0;
}

bool Fury::open(const std::string& assetDir, content::Textures& textures,
                const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/fury/";
    const auto load = [&](const char* mesh, const char* sheet, Model& model) {
        model.tris.clear();
        loadEffectObj(dir + mesh, kUnit, "", model.tris);
        // Albedo, so the sampler wraps: the fires scroll their sheet along U.
        if (core::fileExists(dir + sheet)) {
            model.sheet = textures.load(dir + sheet, content::TextureRole::Albedo);
        }
    };
    load("EarthQuake01.obj", "fury.png", crater_);
    load("EarthQuake02.obj", "magic_a02.png", fire_);
    load("EarthQuake03.obj", "fury_t.png", craterLines_);
    load("EarthQuake04.obj", "test_2.png", patch_);
    load("EarthQuake05.obj", "magic_a02.png", patchFire_);
    load("EarthQuake07.obj", "test_2.png", crack_);
    load("EarthQuake08.obj", "magic_a02.png", crackFire_);
    load("flashing.obj", "magic2.png", flash_);
    load("tail.obj", "ring2.png", tail_);
    sparks_.open(assetDir, textures, table, ground);
    if (const content::EffectSheet* smoke = table.effect("smoke")) {  // Effect/smoke02
        smokeSheet_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    }
    core::logf("fury: crater %zu, fire %zu, crack %zu triangles, sheets %s", crater_.tris.size() / 3,
               fire_.tris.size() / 3, crack_.tris.size() / 3,
               bgfx::isValid(crater_.sheet) && bgfx::isValid(fire_.sheet) ? "yes" : "NO");
    return !crater_.tris.empty() && bgfx::isValid(crater_.sheet);
}

void Fury::clear() {
    for (Blow& one : blows_) one = Blow{};
    for (Piece& one : pieces_) one = Piece{};
    for (Tail& one : tails_) one = Tail{};
    for (Flash& one : flashes_) one = Flash{};
    for (Glow& one : glows_) one = Glow{};
    for (Puff& one : smoke_) one = Puff{};
    quake_ = 0.0f;
}

void Fury::cast(uint32_t owner, const float feet[3], float yaw, float wait,
                const content::Mesh* weapon, const ShineLook& shine, bool polearm) {
    Blow* blow = nullptr;
    for (Blow& one : blows_) {
        if (!one.alive) {
            blow = &one;
            break;
        }
    }
    if (blow == nullptr) return;
    *blow = Blow{};
    blow->alive = true;
    blow->owner = owner;
    blow->weapon = weapon;
    blow->shine = shine;
    blow->polearm = polearm;
    for (int k = 0; k < 3; ++k) blow->feet[k] = feet[k];
    blow->yaw = yaw;
    blow->wait = wait;
    blow->subType = int(roll() % 100u);
}

void Fury::place(Blow& blow, float frames) {
    // Between the last frame's place and this one's, so the throw is smooth at any rate.
    const float t = std::clamp(frames, 0.0f, 1.0f);
    float at[3];
    for (int k = 0; k < 3; ++k) at[k] = blow.wasAt[k] + (blow.weaponAt[k] - blow.wasAt[k]) * t;
    const float tumble = (blow.tumble + kTumble * (t - 1.0f)) * kDegrees;
    content::placementTransform(tumble, blow.yaw + kTurned * kDegrees, 0.0f,
                                blow.polearm ? kPoleScale : kScale, at, blow.transform);
}

void Fury::lay(Kind kind, const float at[3], float yaw, float scale, float frames) {
    for (Piece& piece : pieces_) {
        if (piece.alive) continue;
        piece = Piece{};
        piece.alive = true;
        piece.kind = kind;
        for (int k = 0; k < 3; ++k) piece.at[k] = at[k];
        piece.yaw = yaw;
        piece.scale = scale;
        piece.left = piece.frames = frames;
        return;
    }
}

void Fury::agePieces(float frames, float& craterLight) {
    for (Piece& piece : pieces_) {
        if (!piece.alive) continue;
        const float before = piece.left;
        piece.left -= frames;
        if (piece.left <= 0.0f) {
            piece.alive = false;
            continue;
        }
        // The burning cracks smoke a little while they burn; see kTricklePerSecond.
        if (piece.kind == Kind::CrackFire && piece.left > 10.0f &&
            float(roll() % 10000u) / 10000.0f < kTricklePerSecond * frames / kReferenceFps) {
            puff(piece.at);
        }
        // EarthQuake01 shakes the camera on every third frame while it is over fifteen.
        if (piece.kind == Kind::Crater) {
            for (int f = int(std::ceil(before)) - 1; float(f) > piece.left - 1.0f && f > 15; --f) {
                if (f % 3 == 0) quake_ = float(int(roll() % 8u) - 4) * 0.1f;
            }
            craterLight = std::max(craterLight, piece.left);
        }
        // Each sinks half a unit a frame under its own LifeTime.
        float sinksUnder = 10.0f;
        switch (piece.kind) {
        case Kind::CraterLines: sinksUnder = 13.0f; break;
        case Kind::CraterFire: sinksUnder = 5.0f; break;
        case Kind::PatchFire:
        case Kind::CrackFire: sinksUnder = 15.0f; break;
        default: break;
        }
        if (piece.left < sinksUnder) {
            piece.sunk += 0.5f * kUnit * std::min(frames, sinksUnder - piece.left);
        }
    }
    for (Glow& glow : glows_) {
        if (!glow.alive) continue;
        glow.left -= frames;
        if (glow.left <= 0.0f) {
            glow.alive = false;
            continue;
        }
        glow.light = fireLight(glow.left, 40.0f, 10.0f);
        glow.flash = std::max(0.0f, glow.flash - frames);
    }
}

void Fury::puff(const float at[3]) {
    if (!bgfx::isValid(smokeSheet_)) return;
    for (Puff& one : smoke_) {
        if (one.alive) continue;
        const auto unit = [this] { return float(roll() % 10000u) / 10000.0f; };
        one = Puff{};
        one.alive = true;
        one.at[0] = at[0];
        one.at[1] = at[1] + 0.1f;
        one.at[2] = at[2];
        one.rise = 0.25f + 0.2f * unit();
        one.drift[0] = (unit() - 0.5f) * 0.12f;
        one.drift[1] = (unit() - 0.5f) * 0.12f;
        one.size = 0.4f + 0.3f * unit();
        one.spin = 6.2831853f * unit();
        one.life = 2.5f + unit();
        return;
    }
}

void Fury::gather(std::vector<gfx::Drawable>& out) const {
    for (const Blow& blow : blows_) {
        // RenderFuryStrike draws it while LifeTime is over ten.
        if (!blow.alive || !blow.flying || blow.weapon == nullptr || blow.life <= 10) continue;
        gfx::Drawable drawable;
        drawable.mesh = blow.weapon;
        std::copy(blow.transform, blow.transform + 16, drawable.transform);
        wear(blow.shine, drawable);
        drawable.inProbe = false;
        out.push_back(drawable);
    }
}

void Fury::gatherEffects(gfx::Effects& effects, const float eye[3], const float near[3],
                         float daylight) const {
    sparks_.gather(effects, eye, near, daylight);
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const auto draw = [&](const Model& model, gfx::Blend blend, const float at[3], float yaw,
                          float scale, float light, float uShift) {
        if (model.tris.empty() || !bgfx::isValid(model.sheet) || light <= 0.0f) return;
        // MU's yaw turns the model's X to (cos, -sin) and its loaded Z, MU's -Y, to (sin, cos).
        const float s = std::sin(yaw), c = std::cos(yaw);
        const float x[3] = {c, 0.0f, -s};
        const float z[3] = {s, 0.0f, c};
        const float colour[3] = {light, light, light};
        submitEffectAlong(effects, model.tris, model.sheet, blend, at, x, up, z, scale, colour,
                          1.0f, uShift);
    };
    for (const Piece& piece : pieces_) {
        if (!piece.alive) continue;
        const float at[3] = {piece.at[0], piece.at[1] - piece.sunk, piece.at[2]};
        const float scroll = -float(int(piece.left)) * 0.01f;
        switch (piece.kind) {
        case Kind::CraterLines:
            // Solid and lit, by the light of the ground it lies on.
            {
                const float lit = std::clamp(daylight, 0.2f, 1.0f);
                if (craterLines_.tris.empty() || !bgfx::isValid(craterLines_.sheet)) break;
                const float s = std::sin(piece.yaw), c = std::cos(piece.yaw);
                const float x[3] = {c, 0.0f, -s};
                const float z[3] = {s, 0.0f, c};
                const float colour[3] = {lit, lit, lit};
                submitEffectAlong(effects, craterLines_.tris, craterLines_.sheet, gfx::Blend::Alpha,
                                  at, x, up, z, piece.scale, colour, 1.0f);
            }
            break;
        case Kind::Crater:
            draw(crater_, gfx::Blend::Additive, at, piece.yaw, piece.scale,
                 std::min(1.0f, piece.left * 0.1f / 3.0f), 0.0f);
            break;
        case Kind::CraterFire:
            draw(fire_, gfx::Blend::Additive, at, piece.yaw, piece.scale,
                 fireLight(piece.left, 20.0f, 10.0f), scroll);
            break;
        case Kind::Patch:
            draw(patch_, gfx::Blend::Additive, at, piece.yaw, piece.scale,
                 std::min(1.0f, piece.left * 0.1f / 3.0f), 0.0f);
            break;
        case Kind::PatchFire:
            draw(patchFire_, gfx::Blend::Additive, at, piece.yaw, piece.scale,
                 fireLight(piece.left, 40.0f, 10.0f), scroll);
            break;
        case Kind::Crack:
            draw(crack_, gfx::Blend::Additive, at, piece.yaw, piece.scale,
                 std::min(1.0f, piece.left * 0.1f / 3.0f), 0.0f);
            break;
        case Kind::CrackFire:
            draw(crackFire_, gfx::Blend::Additive, at, piece.yaw, piece.scale,
                 fireLight(piece.left, 40.0f, 10.0f), scroll);
            break;
        }
    }
    for (const Flash& flash : flashes_) {
        if (!flash.alive) continue;
        // BlendMeshLight 1.5 while it grows, then LifeTime / 30.
        const float light = flash.scale > 2.0f ? flash.left / 30.0f : 1.5f;
        draw(flash_, gfx::Blend::Additive, flash.at, flash.yaw, flash.scale, std::min(1.0f, light),
             0.0f);
    }
    // The smoke: grey by the day's light, in and out softly, opening as it rises, as the
    // forge's (fx/forge.cpp).
    for (const Puff& one : smoke_) {
        if (!one.alive) continue;
        const float t = one.age / one.life;
        const auto smooth = [](float a, float b, float x) {
            const float u = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
            return u * u * (3.0f - 2.0f * u);
        };
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = 0.5f * one.size * (1.0f + 1.8f * t);
        sprite.spin = one.spin + 0.2f * one.age;
        const float grey = 0.30f * daylight;
        sprite.colour[0] = grey;
        sprite.colour[1] = grey * 0.97f;
        sprite.colour[2] = grey;
        sprite.colour[3] = kSmokeAlpha * smooth(0.0f, 0.15f, t) * (1.0f - smooth(0.35f, 1.0f, t));
        sprite.sheet = smokeSheet_;
        sprite.blend = gfx::Blend::Smoke;
        if (!effects.add(sprite)) break;
    }
    for (const Tail& tail : tails_) {
        if (!tail.alive) continue;
        draw(tail_, gfx::Blend::Additive, tail.at, 45.0f * kDegrees, 1.0f,
             kTailLight * tail.left / 20.0f, 0.0f);
    }
}

uint32_t Fury::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    const float metres = ground_ ? ground_->metresPerTile() : 1.0f;
    for (const Glow& glow : glows_) {
        if (!glow.alive) continue;
        // The fires' glow: a floor of a quarter from the landing, so it never blinks out
        // between the flash and the fires coming up.
        const float fire = std::max(glow.light, glow.left > 30.0f ? 0.25f : 0.0f);
        if (fire > 0.0f && count < max) {
            gfx::PointLight& light = out[count++];
            for (int k = 0; k < 3; ++k) light.position[k] = glow.at[k];
            light.reach = kGlowTiles * metres;
            light.height = 1.0f;
            for (int k = 0; k < 3; ++k) light.colour[k] = kGlowColour[k] * fire;
        }
        if (glow.flash > 0.0f && count < max) {
            gfx::PointLight& light = out[count++];
            for (int k = 0; k < 3; ++k) light.position[k] = glow.at[k];
            light.reach = kFlashTiles * metres;
            light.height = 1.2f;
            const float left = glow.flash / kFlashLightFrames;
            for (int k = 0; k < 3; ++k) light.colour[k] = kFlashColour[k] * left * left;
        }
    }
    return count;
}

}  // namespace mu::game
