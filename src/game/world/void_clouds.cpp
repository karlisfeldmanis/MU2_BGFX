#include "game/world/void_clouds.h"

#include <algorithm>
#include <cmath>

#include "content/grid.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr int kWisps = 72;
// Spawned within this of the camera's point, and let go past a little more.
constexpr float kReach = 34.0f;
constexpr float kLetGo = 40.0f;
constexpr float kLifeMin = 22.0f, kLifeMax = 36.0f;
// How far under the floor the layer lies, and the sheets' half width and growth.
constexpr float kDepthMin = 1.5f, kDepthMax = 4.5f;
constexpr float kSizeMin = 7.0f, kSizeMax = 11.0f;
constexpr float kGrowth = 0.3f;
constexpr float kDrift[2] = {0.5f, 0.22f};  // metres a second
constexpr float kScatter = 0.12f;
constexpr float kSpin = 0.045f;             // radians a second at most
// Faint, as asked: subtle. Cold grey, the castle's light. First at 48 sheets, 0.07 and (0.44, 0.46,
// 0.54) they greyed the whole chasm (the user: 'clouds is to light'); at 30, 0.035 and (0.34, 0.36,
// 0.43) they were gone ('now little bot more vissible'). And 'they have to moove little bit':
// the drift 0.22 to 0.5 m/s, the turn 0.02 to 0.045. Then 'we need more clouds': 38 to 72 over
// 34 m, each a little fainter, 0.055 to 0.05, so their overlap stays dark; and 'little bit to
// vissible': 0.038.
constexpr float kAlpha = 0.038f;
constexpr float kColour[3] = {0.30f, 0.32f, 0.38f};

bool wanted(const std::string& world) { return world == "bloodcastle"; }

}  // namespace

float VoidClouds::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void VoidClouds::open(const std::string& assetDir, const std::string& world,
                      const content::Ground& ground, content::Textures& textures) {
    shutdown();
    if (!wanted(world)) return;
    size_ = ground.size();
    metresPerTile_ = ground.metresPerTile();
    std::vector<float> heights;
    int voids = 0;
    for (int r = 0; r < size_; ++r) {
        for (int c = 0; c < size_; ++c) {
            if ((ground.attributesAt(c, r) & content::kNoGround) != 0) {
                ++voids;
            } else if (ground.walkable(c, r)) {
                heights.push_back(ground.heightAt((float(c) + 0.5f) * metresPerTile_,
                                                  -(float(r) + 0.5f) * metresPerTile_));
            }
        }
    }
    if (voids == 0 || heights.empty()) return;
    std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
    floor_ = heights[heights.size() / 2];
    ground_ = &ground;
    const std::string path = assetDir + "/effects/fire/smoke02.png";
    if (core::fileExists(path)) sheet_ = textures.load(path, content::TextureRole::Albedo);
    wisps_.assign(kWisps, Wisp());
    core::logf("void clouds: %d void tiles, floor %.2f m, %d wisps; smoke02 %s", voids, floor_,
               kWisps, bgfx::isValid(sheet_) ? "yes" : "NO");
}

void VoidClouds::shutdown() {
    wisps_.clear();
    ground_ = nullptr;
    sheet_ = BGFX_INVALID_HANDLE;
}

bool VoidClouds::voidAt(float x, float z) const {
    // Column is +x and row is -z, each tile's centre at its half (docs/conventions.md).
    const int c = int(std::floor(x / metresPerTile_));
    const int r = int(std::floor(-z / metresPerTile_));
    if (c < 0 || r < 0 || c >= size_ || r >= size_) return true;  // past the map is void too
    return (ground_->attributesAt(c, r) & content::kNoGround) != 0;
}

bool VoidClouds::spawn(Wisp& wisp, const float near[3], bool anyAge) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        const float angle = 6.2831853f * unit();
        const float reach = kReach * std::sqrt(unit());
        const float x = near[0] + std::cos(angle) * reach;
        const float z = near[2] + std::sin(angle) * reach;
        if (!voidAt(x, z)) continue;
        wisp.at[0] = x;
        wisp.at[2] = z;
        wisp.at[1] = floor_ - (kDepthMin + (kDepthMax - kDepthMin) * unit());
        wisp.drift[0] = kDrift[0] + (unit() - 0.5f) * 2.0f * kScatter;
        wisp.drift[1] = kDrift[1] + (unit() - 0.5f) * 2.0f * kScatter;
        wisp.life = kLifeMin + (kLifeMax - kLifeMin) * unit();
        wisp.age = anyAge ? wisp.life * unit() : 0.0f;
        wisp.size = kSizeMin + (kSizeMax - kSizeMin) * unit();
        wisp.turn = 6.2831853f * unit();
        wisp.spin = (unit() - 0.5f) * 2.0f * kSpin;
        wisp.alive = true;
        return true;
    }
    return false;
}

void VoidClouds::update(float seconds, const float near[3]) {
    if (!ground_) return;
    for (Wisp& wisp : wisps_) {
        if (wisp.alive) {
            wisp.age += seconds;
            wisp.at[0] += wisp.drift[0] * seconds;
            wisp.at[2] += wisp.drift[1] * seconds;
            wisp.turn += wisp.spin * seconds;
            const float dx = wisp.at[0] - near[0], dz = wisp.at[2] - near[2];
            if (wisp.age >= wisp.life || dx * dx + dz * dz > kLetGo * kLetGo) wisp.alive = false;
        }
        if (!wisp.alive) spawn(wisp, near, wisp.life == 0.0f);
    }
}

void VoidClouds::gather(gfx::Effects& effects) const {
    if (!ground_ || !bgfx::isValid(sheet_)) return;
    for (const Wisp& wisp : wisps_) {
        if (!wisp.alive) continue;
        const float t = wisp.age / wisp.life;
        const float alpha = kAlpha * std::sin(3.14159265f * std::clamp(t, 0.0f, 1.0f));
        if (alpha <= 0.002f) continue;
        const float half = wisp.size * (1.0f + kGrowth * t);
        const float c = std::cos(wisp.turn) * half, s = std::sin(wisp.turn) * half;
        gfx::Sprite sprite;
        for (int a = 0; a < 3; ++a) sprite.position[a] = wisp.at[a];
        sprite.placed = true;
        const float corners[4][2] = {{-c + s, -s - c}, {c + s, s - c}, {c - s, s + c}, {-c - s, -s + c}};
        const float uvs[4][2] = {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
        for (int k = 0; k < 4; ++k) {
            sprite.corner[k][0] = wisp.at[0] + corners[k][0];
            sprite.corner[k][1] = wisp.at[1];
            sprite.corner[k][2] = wisp.at[2] + corners[k][1];
            sprite.cornerUv[k][0] = uvs[k][0];
            sprite.cornerUv[k][1] = uvs[k][1];
        }
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kColour[k];
        sprite.colour[3] = alpha;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Smoke;
        effects.add(sprite);
    }
}

}  // namespace mu::game
