#include "game/fx/arrow.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

namespace {

constexpr float kTwoPi = 6.28318530718f;

void normalise(float v[3]) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length < 1e-6f) return;
    for (int k = 0; k < 3; ++k) v[k] /= length;
}

}  // namespace

bool Arrows::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table, float metresPerTile) {
    metresPerTile_ = metresPerTile > 0.0f ? metresPerTile : 1.0f;
    const std::string dir = assetDir + "/effects/missiles/";
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        const std::string path = dir + name;
        if (!core::fileExists(path)) {
            core::logError("arrows: no %s", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    // One part a mesh group, each with the sheet and the blend its JSON names. "Opaque" is the
    // alpha blend on a sheet with no alpha, as the meteor's rock is drawn.
    struct Recipe {
        Model model;
        const char* obj;
        const char* group;
        const char* sheet;
        gfx::Blend blend;
    };
    static const Recipe kRecipes[] = {
        {Wood, "Arrow01.obj", "arrow", "arrow.png", gfx::Blend::Alpha},
        {Wood, "Arrow01.obj", "fire01", "fire01.png", gfx::Blend::Additive},
        {Steel, "ArrowSteel01.obj", "", "bow_b.png", gfx::Blend::Alpha},
        {Saw, "ArrowSaw01.obj", "", "toothed_whee.png", gfx::Blend::Alpha},
        {Laser, "ArrowLaser01.obj", "", "bow_c.png", gfx::Blend::Additive},
    };
    int loaded = 0;
    for (const Recipe& r : kRecipes) {
        Part part;
        if (!loadEffectObj(dir + r.obj, kUnit, r.group, part.triangles)) {
            core::logError("arrows: %s (%s) did not load", r.obj, r.group);
            continue;
        }
        part.sheet = sheet(r.sheet);
        part.blend = r.blend;
        shapes_[r.model].parts.push_back(std::move(part));
        ++loaded;
    }
    const content::EffectSheet* fire = table.effect("fire");
    if (fire != nullptr) {
        emberSheet_ = textures.load(assetDir + "/" + fire->path, content::TextureRole::Albedo);
    }
    core::logf("arrows: %d parts of 5, embers %s", loaded,
               bgfx::isValid(emberSheet_) ? "yes" : "NO");
    return loaded > 0;
}

void Arrows::shutdown() {
    for (Shape& shape : shapes_) shape.parts.clear();
    for (Shot& one : shots_) one.alive = false;
    for (Ember& one : embers_) one.alive = false;
}

Arrows::Model Arrows::modelFor(int32_t group, int32_t number) {
    if (group != 4) return Wood;
    switch (number) {
        case 8:   // Crossbow
        case 9:   // Golden Crossbow
            return Steel;
        case 10:  // Arquebus
            return Saw;
        case 11:  // Light Crossbow
            return Laser;
        default:
            // Ours: the Serpent, Bluewing and Aquagold crossbows throw bolts not built here.
            return number >= 8 ? Steel : Wood;
    }
}

float Arrows::roll() {
    dice_ = dice_ * 1664525u + 1013904223u;
    return float(dice_ >> 8) / float(1u << 24);
}

void Arrows::loose(const float from[3], const float to[3], uint32_t whom, Model model) {
    Shot* shot = nullptr;
    for (Shot& one : shots_) {
        if (!one.alive) {
            shot = &one;
            break;
        }
    }
    if (shot == nullptr) return;
    *shot = Shot{};
    shot->alive = true;
    shot->model = model;
    shot->whom = whom;
    for (int k = 0; k < 3; ++k) {
        shot->at[k] = from[k];
        shot->to[k] = to[k];
        shot->along[k] = to[k] - from[k];
    }
    normalise(shot->along);
    shot->left = kFrames;
    shot->flown = 0.0f;
    shot->glow = 0.7f + 0.1f * float(int(roll() * 4.0f));
}

void Arrows::update(float seconds, const std::function<bool(uint32_t, float*)>& middle) {
    const float frames = seconds * kReference;
    const float speed = kTilesASecond * metresPerTile_;  // metres a second
    const float spacing = kEmberSpacingUnits * kUnit;
    for (Shot& shot : shots_) {
        if (!shot.alive) continue;
        shot.left -= frames;
        if (shot.left <= 0.0f) {
            shot.alive = false;
            continue;
        }
        // Follows the body while it is drawn; flies on straight once it is not.
        float seen[3];
        if (middle && middle(shot.whom, seen)) {
            for (int k = 0; k < 3; ++k) shot.to[k] = seen[k];
            for (int k = 0; k < 3; ++k) shot.along[k] = shot.to[k] - shot.at[k];
            normalise(shot.along);
        }
        const float step = speed * seconds;
        for (int k = 0; k < 3; ++k) shot.at[k] += shot.along[k] * step;
        shot.glow = 0.7f + 0.1f * float(int(roll() * 4.0f));
        if (shot.model == Wood) {
            shot.flown += step;
            while (shot.flown >= spacing) {
                shot.flown -= spacing;
                shed(shot);
            }
        }
        // A tile short of the body, on the ground plane: CheckClientArrow, and the realm's hit.
        const float dx = shot.to[0] - shot.at[0], dz = shot.to[2] - shot.at[2];
        if (std::sqrt(dx * dx + dz * dz) <= kStopsShort * metresPerTile_) shot.alive = false;
    }
    for (Ember& e : embers_) {
        if (!e.alive) continue;
        e.left -= frames;
        e.size -= kEmberShrink * kEmberSheetUnits * kUnit * frames;
        if (e.left <= 0.0f || e.size <= 0.0f) {
            e.alive = false;
            continue;
        }
        e.rise += kEmberRise * frames;
        e.at[1] += e.rise * kEmberRiseScale * kUnit * frames;
        for (int k = 0; k < 3; ++k) e.at[k] += e.velocity[k] * seconds;
    }
}

void Arrows::shed(const Shot& shot) {
    if (!bgfx::isValid(emberSheet_)) return;
    for (Ember& e : embers_) {
        if (e.alive) continue;
        e.alive = true;
        for (int k = 0; k < 3; ++k) e.at[k] = shot.at[k];
        // On after the arrow at (rand()%16+32)*0.1 units a frame.
        const float drift =
            (kSlowestDrift + (kFastestDrift - kSlowestDrift) * roll()) * kUnit * kReference;
        for (int k = 0; k < 3; ++k) e.velocity[k] = shot.along[k] * drift;
        e.size = (kSmallestEmber + (kLargestEmber - kSmallestEmber) * roll()) *
                 kEmberSheetUnits * kUnit;
        e.spin = roll() * kTwoPi;
        e.rise = 0.0f;
        e.left = kEmberFrames;
        return;
    }
}

void Arrows::gather(gfx::Effects& effects) const {
    const float up[3] = {0.0f, 1.0f, 0.0f};
    for (const Shot& shot : shots_) {
        if (!shot.alive) continue;
        // The model's head is its loaded +Z (the fire sprite trails at -Z), so Z is laid along
        // the flight, X across it and Y the rest of the way round.
        float across[3] = {up[1] * shot.along[2] - up[2] * shot.along[1],
                           up[2] * shot.along[0] - up[0] * shot.along[2],
                           up[0] * shot.along[1] - up[1] * shot.along[0]};
        normalise(across);
        float lift[3] = {shot.along[1] * across[2] - shot.along[2] * across[1],
                         shot.along[2] * across[0] - shot.along[0] * across[2],
                         shot.along[0] * across[1] - shot.along[1] * across[0]};
        normalise(lift);
        for (const Part& part : shapes_[shot.model].parts) {
            if (!bgfx::isValid(part.sheet)) continue;
            const float white[3] = {1.0f, 1.0f, 1.0f};
            submitEffectAlong(effects, part.triangles, part.sheet, part.blend, shot.at, across,
                              lift, shot.along, kScale, white, 1.0f);
        }
    }
    for (const Ember& e : embers_) {
        if (!e.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = e.at[k];
        sprite.halfWidth = sprite.halfHeight = e.size * 0.5f;
        sprite.spin = e.spin;
        // Frame = (23 - LifeTime) / 6 across the strip's four cells.
        const int cell =
            std::clamp(int((kEmberFrames - 1.0f - e.left) / float(kEmberHeld)), 0, kEmberCells - 1);
        sprite.u0 = float(cell) / float(kEmberCells);
        sprite.u1 = float(cell + 1) / float(kEmberCells);
        // Held, not faded: what an ember loses is its size.
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kEmberLight[k] * 0.85f;
        sprite.colour[3] = 1.0f;
        sprite.sheet = emberSheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

uint32_t Arrows::flying() const {
    uint32_t count = 0;
    for (const Shot& one : shots_) count += one.alive ? 1u : 0u;
    return count;
}

}  // namespace mu::game
