#include "game/world/lava_smoke.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// Enough, and large enough, that they overlap into one layer over all the lava in view rather
// than patches (the user: 'that smoke above lava layer has to be not on some spots bot all of
// lava').
// 280 when all the ring is lava: the 140 the user judged at 163,40 stood on about half a ring
// of lava, and update() lets live only the lava's share of these.
constexpr int kWisps = 280;
// Spawned within this of the camera's point, and let go past a little more.
constexpr float kReach = 22.0f;
constexpr float kLetGo = 26.0f;
// Each wisp's life, its height over the lava, its half width and how far it grows.
constexpr float kLifeMin = 9.0f, kLifeMax = 15.0f;
constexpr float kHeightMin = 0.35f, kHeightMax = 0.8f;
constexpr float kSizeMin = 2.5f, kSizeMax = 3.5f;
constexpr float kGrowth = 0.25f;  // of its size over its life
// One slow drift for the whole layer, a little scatter each, and a slow turn.
constexpr float kDrift[2] = {0.18f, -0.08f};
constexpr float kScatter = 0.06f;
constexpr float kSpin = 0.06f;  // radians a second at most
// How much it covers at the middle of its life, and its colour: warm grey, lit from below.
// 0.10 read a little strong once it covered everything (the user: 'make little bit less
// vissible').
// And 0.07 still a little much (the user: 'lets not remove that smoke effect from lava but
// make it less vissible').
constexpr float kAlpha = 0.045f;
constexpr float kColour[3] = {0.62f, 0.46f, 0.38f};

}  // namespace

float LavaSmoke::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void LavaSmoke::open(const std::string& assetDir, const std::string& world,
                     const content::Ground& ground, content::Textures& textures) {
    shutdown();
    if (world != "losttower") return;
    ground_ = &ground;
    size_ = ground.size();
    metresPerTile_ = ground.metresPerTile();
    lava_.assign(size_t(size_) * size_t(size_), 0);
    int count = 0;
    for (int r = 0; r < size_; ++r) {
        for (int c = 0; c < size_; ++c) {
            const int slot = ground.floorAt(c, r);
            if (slot >= 0 && ground.floorName(slot) == "TileWater01") {
                lava_[size_t(r) * size_t(size_) + size_t(c)] = 1;
                ++count;
            }
        }
    }
    if (count == 0) {
        lava_.clear();
        return;
    }
    const std::string path = assetDir + "/effects/fire/smoke02.png";
    if (core::fileExists(path)) sheet_ = textures.load(path, content::TextureRole::Albedo);
    wisps_.assign(kWisps, Wisp());
    core::logf("lava smoke: %d lava tiles, %d wisps; smoke02 %s", count, kWisps,
               bgfx::isValid(sheet_) ? "yes" : "NO");
}

void LavaSmoke::shutdown() {
    lava_.clear();
    wisps_.clear();
    ground_ = nullptr;
    sheet_ = BGFX_INVALID_HANDLE;
}

// A wisp over a lava tile near `near`, or false when the tries found none. `anyAge` starts it
// part way through its life, so a layer raised at once does not fade in all together.
bool LavaSmoke::spawn(Wisp& wisp, const float near[3], bool anyAge) {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const float angle = 6.2831853f * unit();
        const float reach = kReach * std::sqrt(unit());
        const float x = near[0] + std::cos(angle) * reach;
        const float z = near[2] + std::sin(angle) * reach;
        // Column is +x and row is -z, each tile's centre at its half (docs/conventions.md).
        const int c = int(std::floor(x / metresPerTile_));
        const int r = int(std::floor(-z / metresPerTile_));
        if (c < 0 || r < 0 || c >= size_ || r >= size_) continue;
        if (!lava_[size_t(r) * size_t(size_) + size_t(c)]) continue;
        const float height =
            ground_->heightAt(x, z) + kHeightMin + (kHeightMax - kHeightMin) * unit();
        const float drift[2] = {kDrift[0] + (unit() - 0.5f) * 2.0f * kScatter,
                                kDrift[1] + (unit() - 0.5f) * 2.0f * kScatter};
        const float life = kLifeMin + (kLifeMax - kLifeMin) * unit();
        float size = kSizeMin + (kSizeMax - kSizeMin) * unit();
        // **Clear of the walls, all its life** (the user, 2026-10-05: 'smoke effect above the
        // lava is making vissible crops'): a sheet whose centre is on the lava still reached into
        // the rock at the lava's edge, and the wall cut it along a straight line. The ground
        // under its whole square, grown, where it starts and where its drift ends.
        // Smaller where the lava is a narrow channel, down to a third, so a strip between two
        // walls still has its smoke.
        const auto fits = [&](float sized) {
            const float half = sized * (1.0f + kGrowth) * 0.8f;  // the disc is gone by the edge
            return clear(x, z, half, height) &&
                   clear(x + drift[0] * life, z + drift[1] * life, half, height);
        };
        while (size > kSizeMin * 0.34f && !fits(size)) size *= 0.75f;
        if (!fits(size)) continue;
        wisp.at[0] = x;
        wisp.at[2] = z;
        wisp.at[1] = height;
        wisp.drift[0] = drift[0];
        wisp.drift[1] = drift[1];
        wisp.life = life;
        wisp.age = anyAge ? wisp.life * unit() : 0.0f;
        wisp.size = size;
        wisp.turn = 6.2831853f * unit();
        wisp.spin = (unit() - 0.5f) * 2.0f * kSpin;
        wisp.alive = true;
        return true;
    }
    return false;
}

bool LavaSmoke::clear(float x, float z, float half, float height) const {
    // A 5 x 5 lattice over the square, and a hand's breadth of air under the sheet at each.
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 5; ++j) {
            const float px = x + (float(i) / 2.0f - 1.0f) * half;
            const float pz = z + (float(j) / 2.0f - 1.0f) * half;
            if (ground_->heightAt(px, pz) > height - 0.1f) return false;
        }
    }
    return true;
}

bool LavaSmoke::lavaAt(float x, float z) const {
    const int c = int(std::floor(x / metresPerTile_));
    const int r = int(std::floor(-z / metresPerTile_));
    if (c < 0 || r < 0 || c >= size_ || r >= size_) return false;
    return lava_[size_t(r) * size_t(size_) + size_t(c)] != 0;
}

void LavaSmoke::update(float seconds, const float near[3]) {
    if (lava_.empty()) return;
    // How much of the ring round the camera is lava, on a fixed 16 x 16 lattice over it: only
    // that share of the wisps may live, so the smoke is as thick over a narrow strip as over a
    // wide field. Without it every dead wisp searched until it found lava, and all of them piled
    // onto whatever little was in reach (the user, at 247,97: 'smoke look skind of wierd on
    // this place').
    int inside = 0, onLava = 0;
    for (int i = 0; i < 16; ++i) {
        for (int j = 0; j < 16; ++j) {
            const float u = (float(i) + 0.5f) / 8.0f - 1.0f, v = (float(j) + 0.5f) / 8.0f - 1.0f;
            if (u * u + v * v > 1.0f) continue;
            ++inside;
            onLava += lavaAt(near[0] + u * kReach, near[2] + v * kReach) ? 1 : 0;
        }
    }
    const int allowed = inside > 0 ? (kWisps * onLava + inside - 1) / inside : 0;
    int alive = 0;
    for (const Wisp& wisp : wisps_) alive += wisp.alive ? 1 : 0;
    for (Wisp& wisp : wisps_) {
        if (wisp.alive) {
            wisp.age += seconds;
            wisp.at[0] += wisp.drift[0] * seconds;
            wisp.at[2] += wisp.drift[1] * seconds;
            wisp.turn += wisp.spin * seconds;
            const float dx = wisp.at[0] - near[0], dz = wisp.at[2] - near[2];
            if (wisp.age >= wisp.life || dx * dx + dz * dz > kLetGo * kLetGo) wisp.alive = false;
        }
        // A dead wisp tries for a new spot while the lava in reach has room for it; off the lava
        // it simply waits.
        if (!wisp.alive && alive < allowed && spawn(wisp, near, wisp.life == 0.0f)) ++alive;
    }
}

void LavaSmoke::gather(gfx::Effects& effects) const {
    if (lava_.empty() || !bgfx::isValid(sheet_)) return;
    for (const Wisp& wisp : wisps_) {
        if (!wisp.alive) continue;
        const float t = wisp.age / wisp.life;
        // In and out on a sine, so it is never seen arriving or leaving.
        const float alpha = kAlpha * std::sin(3.14159265f * std::clamp(t, 0.0f, 1.0f));
        if (alpha <= 0.002f) continue;
        const float half = wisp.size * (1.0f + kGrowth * t);
        const float c = std::cos(wisp.turn) * half, s = std::sin(wisp.turn) * half;
        gfx::Sprite sprite;
        for (int a = 0; a < 3; ++a) sprite.position[a] = wisp.at[a];
        sprite.placed = true;
        // A flat square turned by `turn`, counter-clockwise from the eye above.
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
