#include "game/fx/hellfire.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

uint32_t Hellfire::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

float Hellfire::wallLight(float left) {
    const float light = left >= kWallFrames - kWallRise ? (kWallFrames - left) * 0.1f : left * 0.1f;
    return std::clamp(light, 0.0f, 1.0f);
}

bool Hellfire::open(const std::string& assetDir, content::Textures& textures,
                    const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/hellfire/";
    const auto load = [&](const char* mesh, const char* sheet, content::TextureRole role,
                          Model& model) {
        model.tris.clear();
        loadEffectObj(dir + mesh, kUnit, "", model.tris);
        if (core::fileExists(dir + sheet)) model.sheet = textures.load(dir + sheet, role);
    };
    // The sigil's quarter star runs to its sheet's edges, so it is clamped (a wrapping sampler
    // drew the far edge round the disc as a square); the wall's sheet scrolls along U, so wraps.
    load("Circle01.obj", "magic_a01.png", content::TextureRole::Decal, sigil_);
    load("Circle02.obj", "magic_a02.png", content::TextureRole::Albedo, wall_);
    core::logf("hellfire: sigil %zu, wall %zu triangles, sheets %s", sigil_.tris.size() / 3,
               wall_.tris.size() / 3,
               bgfx::isValid(sigil_.sheet) && bgfx::isValid(wall_.sheet) ? "yes" : "NO");
    return !sigil_.tris.empty() && !wall_.tris.empty() && bgfx::isValid(sigil_.sheet) &&
           bgfx::isValid(wall_.sheet);
}

void Hellfire::clear() {
    for (Ring& one : rings_) one = Ring{};
    quake_ = 0.0f;
}

void Hellfire::cast(const float feet[3], float yaw) {
    for (Ring& ring : rings_) {
        if (ring.alive) continue;
        ring = Ring{};
        ring.alive = true;
        for (int k = 0; k < 3; ++k) ring.at[k] = feet[k];
        ring.yaw = yaw;
        ring.sigil = kSigilFrames;
        ring.wall = kWallFrames;
        return;
    }
}

void Hellfire::gatherEffects(gfx::Effects& effects) const {
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const auto draw = [&](const Model& model, const Ring& ring, float light, float uShift) {
        if (model.tris.empty() || !bgfx::isValid(model.sheet) || light <= 0.0f) return;
        // MU's yaw turns the model's X to (cos, -sin) and its loaded Z, MU's -Y, to (sin, cos).
        const float s = std::sin(ring.yaw), c = std::cos(ring.yaw);
        const float x[3] = {c, 0.0f, -s};
        const float z[3] = {s, 0.0f, c};
        const float colour[3] = {light, light, light};
        submitEffectAlong(effects, model.tris, model.sheet, gfx::Blend::Additive, ring.at, x, up,
                          z, 1.0f, colour, 1.0f, uShift);
    };
    for (const Ring& ring : rings_) {
        if (!ring.alive) continue;
        // The sigil at `LifeTime * 0.1`, which glColor clamps at one.
        if (ring.sigil > 0.0f) draw(sigil_, ring, std::min(1.0f, ring.sigil * 0.1f), 0.0f);
        // The wall, its sheet scrolled a hundredth of U a frame, in whole frames as MU steps it.
        if (ring.wall > 0.0f) {
            draw(wall_, ring, wallLight(ring.wall), -float(int(ring.wall)) * 0.01f);
        }
    }
}

uint32_t Hellfire::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    const float metres = ground_ ? ground_->metresPerTile() : 1.0f;
    for (const Ring& ring : rings_) {
        if (!ring.alive || ring.wall <= 0.0f || count >= max) continue;
        const float light = wallLight(ring.wall);
        if (light <= 0.0f) continue;
        gfx::PointLight& one = out[count++];
        for (int k = 0; k < 3; ++k) one.position[k] = ring.at[k];
        one.reach = kGlowTiles * metres;
        one.height = 1.0f;
        for (int k = 0; k < 3; ++k) one.colour[k] = kGlowColour[k] * light;
    }
    return count;
}

}  // namespace mu::game
