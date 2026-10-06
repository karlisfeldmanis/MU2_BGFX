#include "game/world/sky_clouds.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;        // MU's reference frame, 25 a second
constexpr float kUnit = 0.01f;         // a MU unit, in metres
constexpr float kSheetMetres = 2.56f;  // clouds.jpg, 256 texels, at Scale 1
constexpr float kReach = 30.0f;        // metres from the camera's point a bank is drawn
// MU's light on each puff, 0.1 grey (ZzzObject.cpp:3097), and ours times it: judged in game.
constexpr float kCloudLight = 0.1f;
constexpr float kCloudLevel = 1.0f;
// The flash: one of MU's frames in fifty, and of those one in ten lights a bank's edge.
constexpr int kFlashOdds = 50;
constexpr int kEdgeOdds = 10;
// cloudLight at Scale 0.5, shrinking out over about eight frames (UpdateAnimationFrame).
constexpr float kEdgeScale = 0.5f;
constexpr float kEdgeLife = 8.0f * kFrame;

}  // namespace

float SkyClouds::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void SkyClouds::open(const std::string& assetDir, const std::string& world,
                     const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (world != "icarus") return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("cloud"))
            cloud_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        if (const content::EffectSheet* sheet = table.effect("cloud_light"))
            edge_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    const int first = int(content::EmitterKind::Cloud0);
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model != content::TownEmitter::kWorld) continue;
        const int type = int(one.kind) - first;
        if (type < 0 || type > 5) continue;
        Bank bank;
        for (int i = 0; i < 3; ++i) bank.at[i] = one.at[i];
        bank.first = uint32_t(puffs_.size());
        bank.count = type <= 2 ? 20 : 10;
        for (uint32_t i = 0; i < bank.count; ++i) {
            Puff puff;
            puff.at[0] = one.at[0] + (unit() * 5.0f - 2.5f);
            puff.at[1] = one.at[1] + (20.0f + unit() * 20.0f) * kUnit;
            puff.at[2] = one.at[2] + (unit() * 5.0f - 2.5f);
            puff.g = unit() * 1000.0f;
            puff.start = unit();
            puff.scale = 1.8f + unit() * 0.2f;
            // TurningForce: the box's Scale (an anchor carries none: 1) and 0-0.3 more.
            const float force = 1.0f + unit() * 0.3f;
            const bool forward = type == 1 || type == 4 ? true
                                 : type == 2 || type == 5 ? false
                                                          : (i % 2) == 0;
            puff.turn = (forward ? 0.02f : -0.02f) * force;
            puffs_.push_back(puff);
        }
        banks_.push_back(bank);
    }
    open_ = true;
    core::logf("sky clouds: %zu banks, %zu puffs; cloud %s, cloud light %s", banks_.size(),
               puffs_.size(), bgfx::isValid(cloud_) ? "yes" : "NO",
               bgfx::isValid(edge_) ? "yes" : "NO");
}

void SkyClouds::shutdown() {
    open_ = false;
    banks_.clear();
    puffs_.clear();
    edges_.clear();
    clock_ = owed_ = 0.0f;
    cloud_ = edge_ = BGFX_INVALID_HANDLE;
}

void SkyClouds::update(float seconds, const float near[3]) {
    if (!open_) return;
    clock_ += seconds;
    for (int i = 0; i < 3; ++i) near_[i] = near[i];
    for (Edge& one : edges_) one.age += seconds;
    edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                                [](const Edge& e) { return e.age >= kEdgeLife; }),
                 edges_.end());
    owed_ += seconds;
    while (owed_ >= kFrame) {
        owed_ -= kFrame;
        if (int(unit() * float(kFlashOdds)) != 0) continue;
        if (int(unit() * float(kEdgeOdds)) != 0) continue;
        // A bank in view: one within reach, picked as MU counts down its visible objects.
        std::vector<const Bank*> seen;
        for (const Bank& bank : banks_) {
            const float dx = bank.at[0] - near_[0], dz = bank.at[2] - near_[2];
            if (dx * dx + dz * dz <= kReach * kReach) seen.push_back(&bank);
        }
        if (seen.empty()) continue;
        const Bank& bank = *seen[size_t(unit() * float(seen.size())) % seen.size()];
        Edge edge;
        for (int i = 0; i < 3; ++i) {
            edge.at[i] = bank.at[i];
            // rand() % 10 / 50: 0 to 0.18 a channel.
            edge.colour[i] = float(int(unit() * 10.0f)) / 50.0f;
        }
        edges_.push_back(edge);
    }
}

void SkyClouds::gather(gfx::Effects& effects) const {
    if (!open_) return;
    const float ms = clock_ * 1000.0f;
    if (bgfx::isValid(cloud_)) {
        for (const Bank& bank : banks_) {
            const float dx = bank.at[0] - near_[0], dz = bank.at[2] - near_[2];
            if (dx * dx + dz * dz > kReach * kReach) continue;
            for (uint32_t i = 0; i < bank.count; ++i) {
                const Puff& one = puffs_[bank.first + i];
                gfx::Sprite sprite;
                sprite.position[0] = one.at[0];
                sprite.position[1] = one.at[1] + std::sin((ms + one.g) / 5000.0f) * 20.0f * kUnit;
                sprite.position[2] = one.at[2];
                for (int c = 0; c < 3; ++c) sprite.colour[c] = kCloudLight * kCloudLevel;
                sprite.colour[3] = 1.0f;
                sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * one.scale;
                // Degrees, as MU's Rotation; the sprite's spin is radians.
                sprite.spin = (ms * one.turn + one.start) * 3.14159265f / 180.0f;
                sprite.sheet = cloud_;
                sprite.blend = gfx::Blend::Additive;
                effects.add(sprite);
            }
        }
    }
    if (!bgfx::isValid(edge_)) return;
    for (const Edge& one : edges_) {
        const float left = 1.0f - one.age / kEdgeLife;
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = one.colour[i];
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * kEdgeScale * left;
        sprite.sheet = edge_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
