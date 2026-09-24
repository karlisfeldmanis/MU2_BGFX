#include "game/world/shades.h"

#include <cmath>
#include <cstring>

#include "content/placement.h"
#include "content/showing.h"
#include "core/log.h"
#include "game/world/town.h"

namespace mu::game {
namespace {

// The quad as each .bmd stores it, in the model's own metres (the .obj's units over a hundred,
// same axes -- the cooked .mum's bounds are the .obj's to the centimetre). Four corners in the
// transparent pass's order: bottom far, bottom near, top near, top far along the span. Both
// hang 7.8 cm to 1.58 m under the deck; Bridge01's runs its whole 8 m, BridgeStone01's the
// 3.3 m of the abutment.
struct Hanging {
    const char* model;
    float x, bottom, top, from, to;
};
constexpr Hanging kHangings[] = {
    {"Bridge01", 0.4165f, -1.5791f, -0.0775f, 3.9937f, -4.0026f},
    {"BridgeStone01", 0.6333f, -1.5791f, -0.0775f, 1.6377f, -1.6395f},
};

// The sheet's corners, MU's own V -- the .obj writes 1 - v, so its 0.0126 is MU's 0.9874 at
// the bottom. The sheet is solid black down its top half and fades over the bottom half, and
// MU laid the whole of it on the quad (its top corners at 0.0174), which reads as a hard black
// band under the deck. The top corners start at the middle instead, where the fade begins, so
// the fade runs the quad's whole height. INVENTION, the user's call on 2026-09-24 ("some
// gradient"), and so is the strength below.
constexpr float kTopV = 0.45f;
constexpr float kUv[4][2] = {{0.0005f, 0.9874f}, {0.9995f, 0.9874f}, {0.9995f, kTopV},
                             {0.0005f, kTopV}};

// How dark the shade is at its darkest, as the alpha it is multiplied by: MU's solid black read
// as a hole under the arches next to this renderer's lit water.
constexpr float kStrength = 0.8f;

}  // namespace

bool Shades::open(const std::string& assetDir, const Town& town, content::Textures& textures) {
    const auto& models = town.cooked().models;
    for (const content::TownInstance& instance : town.cooked().instances) {
        if (instance.model >= models.size()) continue;
        const Hanging* hanging = nullptr;
        for (const Hanging& one : kHangings) {
            if (models[instance.model].name == one.model) hanging = &one;
        }
        if (hanging == nullptr) continue;
        // The placement's own transform, scale included: the quad is part of the mesh and
        // goes where the mesh goes.
        float place[16];
        content::placementTransform(instance.pitch, instance.yaw, instance.roll, instance.scale,
                                    instance.position, place);
        const float local[4][3] = {
            {hanging->x, hanging->bottom, hanging->from},
            {hanging->x, hanging->bottom, hanging->to},
            {hanging->x, hanging->top, hanging->to},
            {hanging->x, hanging->top, hanging->from},
        };
        Quad quad;
        for (int c = 0; c < 4; ++c) {
            for (int j = 0; j < 3; ++j) {
                quad.corner[c][j] = local[c][0] * place[0 * 4 + j] +
                                    local[c][1] * place[1 * 4 + j] +
                                    local[c][2] * place[2 * 4 + j] + place[3 * 4 + j];
                quad.centre[j] += quad.corner[c][j] * 0.25f;
            }
        }
        quads_.push_back(quad);
    }

    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("bridge_shadow")) {
            sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        }
    }
    if (!quads_.empty()) {
        core::logf("shades: %zu under the bridges; sheet bridge_shadow %s", quads_.size(),
                   bgfx::isValid(sheet_) ? "yes" : "NO -- recook the showing");
    }
    return true;
}

void Shades::shutdown() {
    quads_.clear();
    sheet_ = BGFX_INVALID_HANDLE;  // the Textures that loaded it owns it
}

void Shades::gather(gfx::Effects& effects, const float near[3]) const {
    if (!bgfx::isValid(sheet_)) return;
    for (const Quad& quad : quads_) {
        const float dx = quad.centre[0] - near[0], dz = quad.centre[2] - near[2];
        if (dx * dx + dz * dz > kMetres * kMetres) continue;
        gfx::Sprite sprite;
        std::memcpy(sprite.position, quad.centre, sizeof(sprite.position));
        std::memcpy(sprite.corner, quad.corner, sizeof(sprite.corner));
        std::memcpy(sprite.cornerUv, kUv, sizeof(sprite.cornerUv));
        sprite.placed = true;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Alpha;
        sprite.colour[3] = kStrength;
        effects.add(sprite);
    }
}

}  // namespace mu::game
