#include "game/world/sand_haze.h"

#include <cmath>

#include "content/showing.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kOut = 0.5f;          // metres in front of the eye
constexpr float kAspect = 2.6f;       // wider than any screen this runs on
constexpr float kColour[3] = {0.3f, 0.3f, 0.25f};
// Ours: a share of MU's light, the storm a trace rather than a veil. 0.6, then 0.4 (the user,
// 2026-10-05: 'redcue dust screen effect little bit').
constexpr float kLevel = 0.4f;

void normalise(float v[3]) {
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 1e-6f) for (int i = 0; i < 3; ++i) v[i] /= l;
}

}  // namespace

void SandHaze::open(const std::string& assetDir, const std::string& world,
                    content::Textures& textures) {
    shutdown();
    if (world != "tarkan") return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("sand"))
            cloud_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        if (const content::EffectSheet* sheet = table.effect("sand_fine"))
            specks_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    core::logf("sand haze: 2 layers, sand %s, sand_fine %s", bgfx::isValid(cloud_) ? "yes" : "NO",
               bgfx::isValid(specks_) ? "yes" : "NO");
}

void SandHaze::shutdown() {
    clock_ = 0.0f;
    cloud_ = specks_ = BGFX_INVALID_HANDLE;
}

void SandHaze::gather(gfx::Effects& effects, const gfx::Camera& camera) const {
    if (!isOpen()) return;
    float forward[3], right[3], up[3];
    for (int i = 0; i < 3; ++i) forward[i] = camera.target[i] - camera.position[i];
    normalise(forward);
    right[0] = forward[1] * camera.up[2] - forward[2] * camera.up[1];
    right[1] = forward[2] * camera.up[0] - forward[0] * camera.up[2];
    right[2] = forward[0] * camera.up[1] - forward[1] * camera.up[0];
    normalise(right);
    up[0] = right[1] * forward[2] - right[2] * forward[1];
    up[1] = right[2] * forward[0] - right[0] * forward[2];
    up[2] = right[0] * forward[1] - right[1] * forward[0];
    const float halfH = kOut * std::tan(camera.fovDegrees * 0.5f * 3.14159265f / 180.0f) * 1.05f;
    const float halfW = halfH * kAspect;
    float centre[3];
    for (int i = 0; i < 3; ++i) centre[i] = camera.position[i] + forward[i] * kOut;

    // MU's two layers: the UV rectangle across the screen and its slide. (WorldTime % 100000)
    // wraps every 100 s, which the fmod keeps.
    const float t = std::fmod(clock_, 100.0f);
    struct Layer {
        bgfx::TextureHandle sheet;
        float across, down, slide;
    };
    const Layer layers[2] = {{cloud_, 0.3f, 0.3f, t * 0.2f}, {specks_, 3.0f, 2.0f, t * 1.0f}};
    for (const Layer& layer : layers) {
        if (!bgfx::isValid(layer.sheet)) continue;
        gfx::Sprite sprite;
        sprite.placed = true;
        for (int i = 0; i < 3; ++i) sprite.position[i] = centre[i];
        const float sx[4] = {-1.0f, 1.0f, 1.0f, -1.0f};
        const float sy[4] = {-1.0f, -1.0f, 1.0f, 1.0f};
        for (int c = 0; c < 4; ++c) {
            for (int i = 0; i < 3; ++i)
                sprite.corner[c][i] = centre[i] + right[i] * sx[c] * halfW + up[i] * sy[c] * halfH;
            // The screen's wider share takes more of the sheet, as MU's 640-wide quad did; the
            // quarter-sheet lean runs from the top edge to the bottom.
            const float u = (sx[c] * 0.5f + 0.5f) * layer.across * (kAspect / (4.0f / 3.0f));
            const float v = (0.5f - sy[c] * 0.5f) * layer.down;
            sprite.cornerUv[c][0] = u + layer.slide + (sy[c] < 0.0f ? 0.25f * layer.across : 0.0f);
            sprite.cornerUv[c][1] = v;
        }
        for (int i = 0; i < 3; ++i) sprite.colour[i] = kColour[i] * kLevel;
        sprite.colour[3] = 1.0f;
        sprite.sheet = layer.sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
