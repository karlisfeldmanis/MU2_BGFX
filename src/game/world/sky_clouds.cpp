#include "game/world/sky_clouds.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;        // MU's reference frame, 25 a second
constexpr float kUnit = 0.01f;         // a MU unit, in metres
constexpr float kSheetMetres = 2.56f;  // cloudLight.jpg, 256 texels, at Scale 1
constexpr float kReach = 24.0f;        // metres from the camera's point a bank is drawn
constexpr float kFadeOver = 4.0f;      // and faded out over the last of them, so none pops
// Ours, the clouds' look (see the header). A bank of MU's 20 wears kNear of the nine smoky wisps,
// one of its 10 kFar; a wisp fades out well inside its cell, so a quad kHalfLow-kHalfHigh metres
// half wide draws a streak some 6-10 m long, and the banks' wisps run into one another as one
// smoky layer rather than standing as puffs. The moon's blue-grey, each at
// kShadeLow-1 of it; drifting at most kDrift radians a second either way.
constexpr int kNear = 3, kFar = 2;
constexpr float kHalfLow = 4.5f, kHalfHigh = 7.5f;
constexpr float kMoon[3] = {0.52f, 0.60f, 0.76f};
constexpr float kShadeLow = 0.6f;
constexpr float kDrift = 0.04f;
// The deck under the road: one bank in kDeepOdds wears a big dark cloud kDeepLow-kDeepHigh m
// below it, bluer and thinner, so the navy beneath the road has depth.
constexpr int kDeepOdds = 2;
constexpr float kDeepLow = 3.0f, kDeepHigh = 7.0f;
constexpr float kDeepTint[3] = {0.20f, 0.28f, 0.44f};
constexpr int kCells = 3;  // the sheet's 3x3
// The glints and their sparks.
constexpr int kGlintOdds = 10;
constexpr float kGlintFrames = 400.0f;
constexpr float kGlintDown = 10.0f, kGlintAround = 25.0f;
// MU's 0.3; a glint's fifteen-odd sparks overlap, and at 0.3 each they summed to a white blot.
// Ours, the user's faint effects: 0.07.
constexpr float kSparkLight = 0.07f;
constexpr float kFlareMetres = 0.64f;  // flare01, 64 texels
// The flash: two reference frames; the cloud under him is ours for MU's cloud.bmd at Scale 10.
constexpr float kFlashSeconds = 2.0f * kFrame;
constexpr float kFlashCloudHalf = 9.0f;
// The flash: one of MU's frames in fifty, and of those one in ten lights a bank's edge.
constexpr int kFlashOdds = 50;
constexpr int kEdgeOdds = 10;
// cloudLight at Scale 0.5, shrinking out over about eight frames (UpdateAnimationFrame).
constexpr float kEdgeScale = 0.5f;
constexpr float kEdgeLife = 8.0f * kFrame;

}  // namespace

void SkyClouds::live(Puff& puff, float wander) {
    puff.wander = wander;
    // Periods of 35-70 s for the wander, 15-30 s for the breath, 8-16 s for each change.
    puff.wanderHz[0] = 1.0f / (35.0f + unit() * 35.0f);
    puff.wanderHz[1] = 1.0f / (35.0f + unit() * 35.0f);
    puff.breathHz = 1.0f / (15.0f + unit() * 15.0f);
    puff.morphHz = 1.0f / (8.0f + unit() * 8.0f);
    for (float& p : puff.phase) p = unit() * 6.2831853f;
}

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
        // The clouds' own sheet, read as the PNG and not the showing's BC7: at a fifth of opaque
        // BC7's sixteen alpha steps a block drew the thin edges as contour lines (the user,
        // 2026-10-06: 'much more transparent'). 9 MB with its mips; index.py copies it there.
        cloud_ = textures.load(assetDir + "/effects/clouds/sky_clouds.png",
                               content::TextureRole::Albedo);
        if (const content::EffectSheet* sheet = table.effect("light"))
            light_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        if (const content::EffectSheet* sheet = table.effect("cloud"))
            flashCloud_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
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
        // MU's 20 or 10 puffs, as kNear or kFar of the nine, scattered as MU scatters them.
        const int count = type <= 2 ? kNear : kFar;
        for (int i = 0; i < count; ++i) {
            Puff puff;
            puff.at[0] = one.at[0] + (unit() * 5.0f - 2.5f);
            puff.at[1] = one.at[1] + (20.0f + unit() * 20.0f) * kUnit + (unit() - 0.5f) * 0.6f;
            puff.at[2] = one.at[2] + (unit() * 5.0f - 2.5f);
            puff.g = unit() * 1000.0f;
            puff.start = unit() * 6.2831853f;
            puff.turn = (unit() * 2.0f - 1.0f) * kDrift;
            puff.half = kHalfLow + unit() * (kHalfHigh - kHalfLow);
            puff.stretch = 1.0f + unit() * 0.35f;
            puff.shade = kShadeLow + unit() * (1.0f - kShadeLow);
            puff.alpha = 0.07f + unit() * 0.06f;
            puff.cell = uint8_t(unit() * float(kCells * kCells)) % uint8_t(kCells * kCells);
            live(puff, 1.5f + unit() * 1.0f);
            puffs_.push_back(puff);
        }
        if (int(unit() * float(kDeepOdds)) == 0) {
            Puff deep;
            deep.deep = true;
            deep.at[0] = one.at[0] + (unit() * 8.0f - 4.0f);
            deep.at[1] = one.at[1] - (kDeepLow + unit() * (kDeepHigh - kDeepLow));
            deep.at[2] = one.at[2] + (unit() * 8.0f - 4.0f);
            deep.g = unit() * 1000.0f;
            deep.start = unit() * 6.2831853f;
            deep.turn = (unit() * 2.0f - 1.0f) * kDrift * 0.5f;
            deep.half = 10.0f + unit() * 5.0f;
            deep.stretch = 1.1f + unit() * 0.4f;
            deep.shade = 0.8f + unit() * 0.2f;
            deep.alpha = 0.07f + unit() * 0.05f;
            deep.cell = uint8_t(unit() * float(kCells * kCells)) % uint8_t(kCells * kCells);
            live(deep, 3.0f + unit() * 2.0f);
            puffs_.push_back(deep);
        }
        bank.count = uint32_t(puffs_.size()) - bank.first;
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
    glints_.clear();
    sparks_.clear();
    flash_ = 0.0f;
    clock_ = owed_ = 0.0f;
    cloud_ = edge_ = light_ = flashCloud_ = BGFX_INVALID_HANDLE;
}

void SkyClouds::update(float seconds, const float near[3], const float hero[3],
                       const std::function<void(const float*, const float*)>& bolt) {
    if (!open_) return;
    clock_ += seconds;
    for (int i = 0; i < 3; ++i) near_[i] = near[i];
    for (Edge& one : edges_) one.age += seconds;
    edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                                [](const Edge& e) { return e.age >= kEdgeLife; }),
                 edges_.end());
    flash_ = std::max(0.0f, flash_ - seconds);
    // The sparks and glints move by MU's frames, scaled to this one.
    const float frames = seconds / kFrame;
    for (Spark& one : sparks_) {
        one.age += frames;
        one.scale -= 0.05f * frames;
        one.at[1] += 2.5f * kUnit * frames;
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(),
                                 [](const Spark& p) { return p.age >= p.life || p.scale <= 0.0f; }),
                  sparks_.end());
    owed_ += seconds;
    while (owed_ >= kFrame) {
        owed_ -= kFrame;
        // The glints: each a frame along its arc, and a spark shed where it is.
        for (Glint& one : glints_) {
            for (int i = 0; i < 3; ++i) one.at[i] += one.velocity[i];
            one.velocity[1] -= 0.01f * kUnit;
            one.age += 1.0f;
            const float dx = one.at[0] - near_[0], dz = one.at[2] - near_[2];
            if (dx * dx + dz * dz > kReach * kReach) continue;
            Spark spark;
            for (int i = 0; i < 3; ++i) spark.at[i] = one.at[i];
            spark.at[0] += (unit() * 2.0f - 1.0f) * 0.4f * 0.5f * kUnit;
            spark.at[2] += (unit() * 2.0f - 1.0f) * 0.4f * 0.5f * kUnit;
            spark.scale = (0.5f + unit() * 0.5f) * 3.0f;
            spark.life = 10.0f + float(int(unit() * 10.0f));
            sparks_.push_back(spark);
        }
        glints_.erase(std::remove_if(glints_.begin(), glints_.end(),
                                     [](const Glint& g) {
                                         return g.age >= kGlintFrames || g.velocity[1] < -2.0f * kUnit;
                                     }),
                      glints_.end());
        if (int(unit() * float(kGlintOdds)) == 0) {
            Glint glint;
            glint.at[0] = hero[0] + (unit() * 2.0f - 1.0f) * kGlintAround;
            glint.at[1] = hero[1] - kGlintDown;
            glint.at[2] = hero[2] + (unit() * 2.0f - 1.0f) * kGlintAround;
            const float dx = glint.at[0] - near_[0], dz = glint.at[2] - near_[2];
            if (dx * dx + dz * dz <= kReach * kReach) {
                // 70 degrees up on MU's heading of 30 (x east, y north; ours z is -y).
                const float speed = float(9 + int(unit() * 5.0f)) * 0.5f * kUnit;
                const float up = 70.0f * 3.14159265f / 180.0f, head = 30.0f * 3.14159265f / 180.0f;
                glint.velocity[0] = speed * std::cos(up) * std::sin(head);
                glint.velocity[2] = -speed * std::cos(up) * std::cos(head);
                glint.velocity[1] = speed * std::sin(up);
                glints_.push_back(glint);
            }
        }
        if (int(unit() * float(kFlashOdds)) != 0) continue;
        // The flash, round a point 1.5 m from him.
        flash_ = kFlashSeconds;
        flashL_ = float(4 + int(unit() * 4.0f)) * 0.05f;
        flashAt_[0] = hero[0] + (unit() * 3.0f - 1.5f);
        flashAt_[1] = hero[1];
        flashAt_[2] = hero[2] + (unit() * 3.0f - 1.5f);
        // One in five, the two far bolts: MU's four layouts, in its units and axes (x east,
        // y north, z up), turned as MoveHeavenThunder turns them and 300 units under him.
        if (bolt && int(unit() * 5.0f) == 0) {
            const auto turn = [](float x, float y, float degrees, float* ox, float* oy) {
                const float r = degrees * 3.14159265f / 180.0f;
                *ox = x * std::cos(r) - y * std::sin(r);
                *oy = x * std::sin(r) + y * std::cos(r);
            };
            static constexpr float kLayout[4][6] = {
                {-400, -1000, 240, -200, -1000, +1},
                {-300, -400, 210, -500, -1000, -1},
                {-200, -400, 235, -1000, -1500, +1},
                {-200, 400, 200, -600, -1200, +1},
            };
            const float* l = kLayout[int(unit() * 4.0f) % 4];
            float px, py;
            turn(l[0], l[1], -45.0f, &px, &py);
            const float heroX = hero[0] / kUnit, heroY = -hero[2] / kUnit;
            const float cx = heroX + l[5] * px, cy = heroY + l[5] * py;
            float ox, oy;
            turn(l[3], l[4], l[2], &ox, &oy);
            const float under = hero[1] - 3.0f;
            const float from[3] = {(cx + ox) * kUnit, under, -(cy + oy) * kUnit};
            const float to[3] = {(cx - ox) * kUnit, under, -(cy - oy) * kUnit};
            bolt(from, to);
            bolt(from, to);
        }
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

uint32_t SkyClouds::lights(gfx::PointLight* out, uint32_t max) const {
    if (!open_ || flash_ <= 0.0f || max == 0) return 0;
    gfx::PointLight& light = out[0];
    light = gfx::PointLight{};
    for (int i = 0; i < 3; ++i) light.position[i] = flashAt_[i];
    light.reach = 2.0f;
    light.height = 2.0f;
    light.colour[0] = flashL_ * 0.3f;
    light.colour[1] = flashL_ * 0.3f;
    light.colour[2] = flashL_ * 0.081f;
    return 1;
}

void SkyClouds::gather(gfx::Effects& effects) const {
    if (!open_) return;
    const float ms = clock_ * 1000.0f;
    if (bgfx::isValid(cloud_)) {
        for (const Bank& bank : banks_) {
            const float dx = bank.at[0] - near_[0], dz = bank.at[2] - near_[2];
            const float far2 = dx * dx + dz * dz;
            if (far2 > kReach * kReach) continue;
            const float edge = std::min(1.0f, (kReach - std::sqrt(far2)) / kFadeOver);
            for (uint32_t i = 0; i < bank.count; ++i) {
                const Puff& one = puffs_[bank.first + i];
                constexpr float kTau = 6.2831853f;
                const float t = clock_;
                const float x = one.at[0] + std::sin(t * one.wanderHz[0] * kTau + one.phase[0]) * one.wander;
                const float z = one.at[2] + std::cos(t * one.wanderHz[1] * kTau + one.phase[1]) * one.wander;
                const float y = one.at[1] + std::sin((ms + one.g) / 5000.0f) * 20.0f * kUnit;
                const float breath = 1.0f + 0.12f * std::sin(t * one.breathHz * kTau + one.phase[2]);
                // The outline deforms: width and height swell out of step, 20% each.
                const float wide = 1.0f + 0.2f * std::sin(t * one.breathHz * 1.3f * kTau + one.phase[0]);
                const float tall = 1.0f + 0.2f * std::sin(t * one.breathHz * 0.9f * kTau + one.phase[1]);
                // The change, never back and forth: two layers half a cycle apart, each weighed
                // sin^2 of its cycle so the two sum to one, and each taking a new cloud of the
                // nine at the moment its weight is nought -- so a cloud is always becoming
                // another (the user, 2026-10-06: 'we need that cloud change shape').
                const float u = t * one.morphHz + one.phase[3] / kTau;
                const uint32_t self = bank.first + i;
                const float* tint = one.deep ? kDeepTint : kMoon;
                for (int layer = 0; layer < 2; ++layer) {
                    const float v = u + (layer == 0 ? 0.0f : 0.5f);
                    const float cycle = std::floor(v);
                    const float wave = std::sin(3.14159265f * (v - cycle));
                    const float weight = wave * wave;
                    if (weight < 0.02f) continue;
                    uint32_t h = self * 2654435761u ^ (uint32_t(int32_t(cycle)) * 40503u + uint32_t(layer));
                    h ^= h >> 15;
                    h *= 0x2c1b3c6du;
                    h ^= h >> 12;
                    const int cell = int(h % uint32_t(kCells * kCells));
                    gfx::Sprite sprite;
                    sprite.position[0] = x;
                    sprite.position[1] = y;
                    sprite.position[2] = z;
                    for (int c = 0; c < 3; ++c) sprite.colour[c] = tint[c] * one.shade;
                    sprite.colour[3] = one.alpha * weight * edge;
                    sprite.halfWidth = one.half * one.stretch * breath * wide;
                    sprite.halfHeight = one.half * breath * tall;
                    // Each new cloud at its own turn, so no two changes look alike.
                    sprite.spin = one.start + t * one.turn + float(h % 628u) * 0.01f;
                    const int column = cell % kCells, row = cell / kCells;
                    sprite.u0 = float(column) / float(kCells);
                    sprite.u1 = float(column + 1) / float(kCells);
                    sprite.v0 = float(row) / float(kCells);
                    sprite.v1 = float(row + 1) / float(kCells);
                    sprite.sheet = cloud_;
                    sprite.blend = gfx::Blend::Alpha;
                    effects.add(sprite);
                }
            }
        }
    }
    if (bgfx::isValid(light_)) {
        for (const Spark& one : sparks_) {
            gfx::Sprite sprite;
            for (int i = 0; i < 3; ++i) {
                sprite.position[i] = one.at[i];
                sprite.colour[i] = kSparkLight;
            }
            sprite.colour[3] = 1.0f;
            sprite.halfWidth = sprite.halfHeight = 0.5f * kFlareMetres * one.scale;
            sprite.sheet = light_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    // The flash under him: MU's cloud.bmd lit for two frames, ours a big added cloud of its colour,
    // 2 m north and 1.9 m down.
    if (flash_ > 0.0f && bgfx::isValid(flashCloud_)) {
        gfx::Sprite sprite;
        sprite.position[0] = flashAt_[0];
        sprite.position[1] = flashAt_[1] - 1.9f;
        sprite.position[2] = flashAt_[2] - 2.0f;
        sprite.colour[0] = flashL_ * 0.3f;
        sprite.colour[1] = flashL_ * 0.3f;
        sprite.colour[2] = flashL_ * 0.081f;
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = kFlashCloudHalf;
        sprite.spin = flashL_ * 40.0f;
        sprite.sheet = flashCloud_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
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
