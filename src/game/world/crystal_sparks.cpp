#include "game/world/crystal_sparks.h"

#include "content/placement.h"
#include "content/showing.h"
#include "core/log.h"
#include "game/world/world_show.h"

namespace mu::game {
namespace {

// Spark03 is 32 texels, so a sprite of Scale 1 is 32 units, 0.32 m, across.
constexpr float kSheetMetres = 0.32f;
// Ours: drawn within this of the camera's point, as the other world effects.
constexpr float kReach = 30.0f;

}  // namespace

void CrystalSparks::open(const std::string& assetDir, const std::string& world,
                         const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (!worldShow(world).crystalSparks) return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("spark_flash"))
            sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    // Which sparks each model carries, found once.
    std::vector<std::vector<const content::TownEmitter*>> carried(town.models.size());
    for (const content::TownEmitter& one : town.emitters) {
        if (one.kind == content::EmitterKind::CrystalSpark &&
            one.model != content::TownEmitter::kWorld) {
            carried[one.model].push_back(&one);
        }
    }
    for (const content::TownInstance& instance : town.instances) {
        if (carried[instance.model].empty()) continue;
        // Turned and scaled: MU's bones carry the object's BodyScale (ZzzBMD.cpp:682-693).
        float turn[16];
        content::placementTransform(instance.pitch, instance.yaw, instance.roll, instance.scale,
                                    instance.position, turn);
        for (const content::TownEmitter* one : carried[instance.model]) {
            Point point;
            for (int j = 0; j < 3; ++j) {
                point.at[j] = one->at[0] * turn[0 * 4 + j] + one->at[1] * turn[1 * 4 + j] +
                              one->at[2] * turn[2 * 4 + j] + turn[3 * 4 + j];
                point.colour[j] = one->colour[j];
            }
            // `high` carries MU's sprite Scale, 7.5.
            point.halfWidth = 0.5f * one->high * kSheetMetres;
            points_.push_back(point);
        }
    }
    open_ = true;
    core::logf("crystal sparks: %zu points, spark03 %s", points_.size(),
               bgfx::isValid(sheet_) ? "yes" : "NO");
}

void CrystalSparks::shutdown() {
    open_ = false;
    points_.clear();
    sheet_ = BGFX_INVALID_HANDLE;
}

void CrystalSparks::update(const float near[3]) {
    for (int k = 0; k < 3; ++k) near_[k] = near[k];
}

void CrystalSparks::gather(gfx::Effects& effects) const {
    if (!open_ || !bgfx::isValid(sheet_)) return;
    for (const Point& one : points_) {
        const float dx = one.at[0] - near_[0], dz = one.at[2] - near_[2];
        if (dx * dx + dz * dz > kReach * kReach) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = one.at[k];
            sprite.colour[k] = one.colour[k];
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = one.halfWidth;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) break;
    }
}

}  // namespace mu::game
