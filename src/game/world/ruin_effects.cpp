#include "game/world/ruin_effects.h"

#include <algorithm>
#include <cmath>

#include "content/placement.h"
#include "content/showing.h"
#include "core/log.h"
#include "game/world/world_show.h"

namespace mu::game {
namespace {

constexpr float kFrames = 25.0f;               // MU's reference frames a second
constexpr float kArcOdds = 30.0f;              // rand() % 30 == 1
constexpr float kSpitEvery = 3.0f;             // rand_fps_check(3)
constexpr float kUnit = 0.01f;                 // metres a MU unit
constexpr float kSpitSheetMetres = 32.0f * kUnit;  // water is 32 texels: Scale 1 is 32 units
constexpr float kSpitShrink = 0.026f * kFrames;    // Scale a second
constexpr float kSpitDim = 1.02f;                  // the light's fall a frame
constexpr float kReach = 30.0f;                // ours: metres from the camera's point
constexpr size_t kMostBlobs = 600;

}  // namespace

float RuinEffects::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void RuinEffects::open(const std::string& assetDir, const std::string& world,
                       const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (!worldShow(world).ruinEffects) return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("plant_spit"))
            spit_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        thunderOpen_ = thunder_.open(assetDir, textures, table);
    }
    // What each model carries, found once, in the cook's order.
    std::vector<std::vector<const content::TownEmitter*>> carried(town.models.size());
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model == content::TownEmitter::kWorld) continue;
        if (one.kind == content::EmitterKind::TankArcFrom ||
            one.kind == content::EmitterKind::TankArcTo ||
            one.kind == content::EmitterKind::PlantSpit) {
            carried[one.model].push_back(&one);
        }
    }
    for (const content::TownInstance& instance : town.instances) {
        if (carried[instance.model].empty()) continue;
        // Turned and scaled: MU's bones carry the object's BodyScale.
        float turn[16];
        content::placementTransform(instance.pitch, instance.yaw, instance.roll, instance.scale,
                                    instance.position, turn);
        auto place = [&](const content::TownEmitter& one, float out[3]) {
            for (int j = 0; j < 3; ++j) {
                out[j] = one.at[0] * turn[0 * 4 + j] + one.at[1] * turn[1 * 4 + j] +
                         one.at[2] * turn[2 * 4 + j] + turn[3 * 4 + j];
            }
        };
        Tank tank;
        bool from = false, to = false;
        for (const content::TownEmitter* one : carried[instance.model]) {
            if (one->kind == content::EmitterKind::TankArcFrom) {
                place(*one, tank.from);
                from = true;
            } else if (one->kind == content::EmitterKind::TankArcTo) {
                place(*one, tank.to);
                to = true;
            } else {
                Plant plant;
                place(*one, plant.at);
                for (int k = 0; k < 3; ++k) plant.colour[k] = one->colour[k];
                plant.scale = instance.scale;
                plant.owed = unit();
                plants_.push_back(plant);
            }
        }
        if (from && to) tanks_.push_back(tank);
    }
    blobs_.reserve(kMostBlobs);
    open_ = true;
    core::logf("ruin effects: %zu tank arcs, %zu spitting plants; water %s, thunder %s",
               tanks_.size(), plants_.size(), bgfx::isValid(spit_) ? "yes" : "NO",
               thunderOpen_ ? "yes" : "NO");
}

void RuinEffects::shutdown() {
    open_ = false;
    tanks_.clear();
    plants_.clear();
    blobs_.clear();
    spit_ = BGFX_INVALID_HANDLE;
    thunderOpen_ = false;
}

void RuinEffects::update(float seconds, const float near[3]) {
    if (!open_) return;
    for (int k = 0; k < 3; ++k) near_[k] = near[k];
    const float dt = std::min(seconds, 0.1f);
    const float frames = dt * kFrames;
    auto nearby = [&](const float at[3]) {
        const float dx = at[0] - near[0], dz = at[2] - near[2];
        return dx * dx + dz * dz < kReach * kReach;
    };
    if (thunderOpen_) {
        const float odds = 1.0f - std::pow(1.0f - 1.0f / kArcOdds, frames);
        for (const Tank& tank : tanks_) {
            if (nearby(tank.from) && unit() < odds) thunder_.fork(tank.from, tank.to, true);
        }
        // Every arc here is pinned at both ends (target 0), so neither is asked.
        thunder_.update(seconds, [](uint32_t) { return false; }, [](uint32_t, float*) { return false; });
    }
    for (Blob& one : blobs_) {
        one.age += dt;
        one.at[1] += one.rise * dt;
        one.scale -= kSpitShrink * dt;
        const float keep = std::pow(1.0f / kSpitDim, frames);
        for (int k = 0; k < 3; ++k) one.colour[k] *= keep;
    }
    blobs_.erase(std::remove_if(blobs_.begin(), blobs_.end(),
                                [](const Blob& b) { return b.age >= b.life || b.scale <= 0.0f; }),
                 blobs_.end());
    if (!bgfx::isValid(spit_)) return;
    for (Plant& plant : plants_) {
        if (!nearby(plant.at)) {
            plant.owed = 0.0f;
            continue;
        }
        plant.owed += frames / kSpitEvery;
        while (plant.owed >= 1.0f) {
            plant.owed -= 1.0f;
            if (blobs_.size() >= kMostBlobs) break;
            Blob blob;
            // Within 20 units across in MU's x and y and 0-40 up, scaled as the bone is.
            const float across = 20.0f * kUnit * plant.scale;
            blob.at[0] = plant.at[0] + (unit() * 2.0f - 1.0f) * across;
            blob.at[1] = plant.at[1] + unit() * 2.0f * across;
            blob.at[2] = plant.at[2] + (unit() * 2.0f - 1.0f) * across;
            blob.rise = (2.0f + unit()) * kFrames * kUnit;
            blob.scale = (50.0f + unit() * 31.0f) * 0.02f;
            blob.life = (60.0f + unit() * 9.0f) / kFrames;
            for (int k = 0; k < 3; ++k) blob.colour[k] = plant.colour[k];
            blobs_.push_back(blob);
        }
    }
}

void RuinEffects::gather(gfx::Effects& effects) const {
    if (!open_) return;
    if (thunderOpen_) thunder_.gather(effects);
    if (!bgfx::isValid(spit_)) return;
    for (const Blob& one : blobs_) {
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = one.at[k];
            sprite.colour[k] = one.colour[k];
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * one.scale * kSpitSheetMetres;
        sprite.sheet = spit_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) break;
    }
}

}  // namespace mu::game
