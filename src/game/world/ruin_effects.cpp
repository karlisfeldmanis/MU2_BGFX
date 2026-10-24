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
constexpr float kTowerArcOdds = 20.0f;         // rand_fps_check(20)
constexpr float kEnergySheetMetres = 64.0f * 0.01f;  // Thunder01 is 64
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
        if (const content::EffectSheet* sheet = table.effect("energy"))
            energy_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        thunderOpen_ = thunder_.open(assetDir, textures, table);
    }
    // The world's lightning boxes.
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model != content::TownEmitter::kWorld) continue;
        if (one.kind == content::EmitterKind::VoidStrike || one.kind == content::EmitterKind::WallArc ||
            one.kind == content::EmitterKind::NorthBolt) {
            Bolt bolt;
            for (int k = 0; k < 3; ++k) bolt.at[k] = one.at[k];
            bolt.kind = one.kind;
            bolts_.push_back(bolt);
        }
    }
    // What each model carries, found once, in the cook's order.
    std::vector<std::vector<const content::TownEmitter*>> carried(town.models.size());
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model == content::TownEmitter::kWorld) continue;
        if (one.kind == content::EmitterKind::TankArcFrom ||
            one.kind == content::EmitterKind::TankArcTo ||
            one.kind == content::EmitterKind::PlantSpit ||
            one.kind == content::EmitterKind::TowerSpark) {
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
            } else if (one->kind == content::EmitterKind::TowerSpark) {
                Tower tower;
                place(*one, tower.at);
                towers_.push_back(tower);
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
    core::logf("ruin effects: %zu tank arcs, %zu spitting plants, %zu towers, %zu bolt boxes; water %s, "
               "energy %s, thunder %s",
               tanks_.size(), plants_.size(), towers_.size(), bolts_.size(), bgfx::isValid(spit_) ? "yes" : "NO",
               bgfx::isValid(energy_) ? "yes" : "NO", thunderOpen_ ? "yes" : "NO");
}

void RuinEffects::shutdown() {
    open_ = false;
    tanks_.clear();
    plants_.clear();
    towers_.clear();
    bolts_.clear();
    blobs_.clear();
    spit_ = BGFX_INVALID_HANDLE;
    energy_ = BGFX_INVALID_HANDLE;
    thunderOpen_ = false;
}

void RuinEffects::update(float seconds, const float near[3]) {
    if (!open_) return;
    for (int k = 0; k < 3; ++k) near_[k] = near[k];
    const float dt = std::min(seconds, 0.1f);
    const float frames = dt * kFrames;
    clock_ += dt;
    auto nearby = [&](const float at[3]) {
        const float dx = at[0] - near[0], dz = at[2] - near[2];
        return dx * dx + dz * dz < kReach * kReach;
    };
    if (thunderOpen_) {
        const float odds = 1.0f - std::pow(1.0f - 1.0f / kArcOdds, frames);
        for (const Tank& tank : tanks_) {
            if (nearby(tank.from) && unit() < odds) thunder_.fork(tank.from, tank.to, true);
        }
        // The towers' bolts, thrown round bone 4 in the world's axes, not the tower's: MU adds
        // to Position (x east, y south, z up) after the bone's transform.
        const float towerOdds = 1.0f - std::pow(1.0f - 1.0f / kTowerArcOdds, frames);
        for (const Tower& tower : towers_) {
            if (!nearby(tower.at) || unit() >= towerOdds) continue;
            float from[3], to[3];
            from[0] = tower.at[0] - (50.0f + 100.0f * unit()) * kUnit;
            from[2] = tower.at[2] + 50.0f * unit() * kUnit;
            to[0] = tower.at[0] + 80.0f * unit() * kUnit;
            to[2] = tower.at[2] - 50.0f * unit() * kUnit;
            from[1] = to[1] = tower.at[1] + 10.0f * kUnit;
            thunder_.fork(from, to, true);
        }
        // The lightning boxes: each bolt ends on the box. MU's x is east (ours x), its y south
        // (ours -z), its z up (ours y).
        const float everyTwenty = 1.0f - std::pow(1.0f - 1.0f / 20.0f, frames);
        const float everyTen = 1.0f - std::pow(1.0f - 1.0f / 10.0f, frames);
        for (const Bolt& bolt : bolts_) {
            const bool strike = bolt.kind == content::EmitterKind::VoidStrike;
            if (!nearby(bolt.at) || unit() >= (strike ? everyTwenty : everyTen)) continue;
            float from[3] = {bolt.at[0], bolt.at[1], bolt.at[2]};
            if (strike) {
                from[0] += (unit() * 10.0f - 5.0f) * kUnit;
                from[2] += (unit() * 10.0f - 5.0f) * kUnit;
                from[1] += 1100.0f * kUnit;
            } else if (bolt.kind == content::EmitterKind::WallArc) {
                from[0] -= 50.0f * kUnit;
            } else {
                from[2] += 350.0f * kUnit;  // MU's y - 350: north
            }
            thunder_.fork(from, bolt.at, true);
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
    if (bgfx::isValid(energy_)) {
        // fLumi = (sin(WorldTime * 0.001) + 1) / 2: white, from nothing to full every 6.3 s.
        const float lumi = 0.5f * (std::sin(clock_) + 1.0f);
        for (const Tower& one : towers_) {
            const float dx = one.at[0] - near_[0], dz = one.at[2] - near_[2];
            if (dx * dx + dz * dz > kReach * kReach) continue;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) {
                sprite.position[k] = one.at[k];
                sprite.colour[k] = lumi;
            }
            sprite.colour[3] = 1.0f;
            sprite.blend = gfx::Blend::Additive;
            // MU's Spark03 of Scale 10 here is not drawn (the user, 2026-10-24: "remove that
            // blue sphere"): only the crackles and the bolts.
            if (!bgfx::isValid(energy_)) continue;
            // The two living crackles: born on this reference frame and the one before, each
            // its own size and turn, spun 20 degrees a frame since. Rolled from the frame and
            // the tower so a frame draws the same pair however often it is gathered.
            const uint32_t frame = uint32_t(clock_ * kFrames);
            for (uint32_t back = 0; back < 2; ++back) {
                uint32_t roll = (frame - back) * 2654435761u ^
                                uint32_t(one.at[0] * 97.0f) * 40503u ^ uint32_t(one.at[2] * 89.0f);
                roll = roll * 1664525u + 1013904223u;
                const float size = 0.6f + 0.7f * float((roll >> 8) & 0xFFFF) / 65535.0f;
                const float turn = float(roll >> 24) / 255.0f * 6.2831853f;
                gfx::Sprite crackle = sprite;
                crackle.halfWidth = crackle.halfHeight = 0.5f * 1.5f * size * kEnergySheetMetres;
                crackle.spin = turn + float(back) * 0.349f;
                crackle.soft = 0.0f;
                crackle.sheet = energy_;
                if (!effects.add(crackle)) break;
            }
        }
    }
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
