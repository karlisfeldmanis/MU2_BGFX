#include <cmath>
#include "game/world/crystal_sparks.h"

#include "content/placement.h"
#include "content/showing.h"
#include "core/log.h"
#include "game/world/town.h"
#include "game/world/world_show.h"

namespace mu::game {
namespace {

// Spark03 is 32 texels, so a sprite of Scale 1 is 32 units, 0.32 m, across.
constexpr float kSheetMetres = 0.32f;
// Ours: drawn within this of the camera's point, as the other world effects.
constexpr float kReach = 30.0f;

}  // namespace

void lendCrystalChrome(Town& town, const std::string& assetDir, const std::string& world,
                       content::Textures& textures) {
    const std::string& model = worldShow(world).crystalChrome;
    if (model.empty() || !town.isOpen()) return;
    content::Showing table;
    std::string error;
    const content::EffectSheet* sheet = nullptr;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        sheet = table.effect("chrome");
    }
    if (!sheet) {
        core::logError("crystal chrome: the showing has no chrome sheet");
        return;
    }
    const bgfx::TextureHandle chrome =
        textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    const content::CookedTown& cooked = town.cooked();
    size_t lent = 0;
    for (size_t i = 0; i < cooked.models.size(); ++i) {
        if (cooked.models[i].name != model) continue;
        content::Mesh* mesh = town.meshAt(i);
        if (!mesh) continue;
        const size_t parts = mesh->parts().size();
        for (size_t p = 0; p < parts; ++p) {
            content::Material pass = mesh->materials()[mesh->parts()[p].material];
            if (!pass.glow) continue;
            // The body's glow at its own light, its sheet swapped for the chrome and every
            // slide of its own taken off: the chrome moves by its normals and the clock.
            pass.albedo = chrome;
            pass.emissive = BGFX_INVALID_HANDLE;
            pass.chromePass = 5;
            pass.scrollPerSecond = 0.0f;
            pass.scrollVPerSecond = 0.0f;
            pass.scrollAlongU = pass.scrollAlongUV = pass.maskHeld = pass.waterFrames = false;
            pass.jitter = 0.0f;
            pass.glowShadow = 0.0f;
            mesh->addPart(p, pass);
            ++lent;
        }
    }
    core::logf("crystal chrome: %zu part(s) of %s take Chrome01", lent, model.c_str());
}

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

void CrystalSparks::update(float seconds, const float near[3]) {
    clock_ += seconds;
    for (int k = 0; k < 3; ++k) near_[k] = near[k];
}

void CrystalSparks::gather(gfx::Effects& effects) const {
    if (!open_ || !bgfx::isValid(sheet_)) return;
    for (const Point& one : points_) {
        const float dx = one.at[0] - near_[0], dz = one.at[2] - near_[2];
        if (dx * dx + dz * dz > kReach * kReach) continue;
        // Ours, so the sparks sit in the crystal rather than on it (the user, 2026-10-23: "that
        // blue light has to be better integrated on crystal so it does not look like just
        // placed there"): MU's blue taken halfway to the crystal's own pink-violet, at its
        // brightness; 0.6 of MU's size; each twinkling slowly on its own phase from 0.45 to 1;
        // and soft, thinning where it meets the facets instead of ending as a disc on them.
        const float phase = (one.at[0] * 1.7f + one.at[2] * 2.3f + one.at[1] * 3.1f);
        const float twinkle = 0.45f + 0.55f * (0.5f + 0.5f * std::sin(clock_ * 1.3f + phase));
        const float own = (one.colour[0] + one.colour[1] + one.colour[2]) / 3.0f;
        const float crystal[3] = {1.0f, 0.7f, 0.95f};
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = one.at[k];
            sprite.colour[k] = (0.5f * one.colour[k] + 0.5f * crystal[k] * own * 1.6f) * twinkle;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = one.halfWidth * 0.6f;
        sprite.soft = 0.4f;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) break;
    }
}

}  // namespace mu::game
