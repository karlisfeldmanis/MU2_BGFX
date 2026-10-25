#include "game/fx/earthshake.h"

#include <algorithm>
#include <cmath>

#include "content/grid.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {

namespace {

constexpr float kDegree = 3.14159265358979f / 180.0f;
// Keys 2 to 4 of either stone's clip, in units under key 1, its risen pose (muextract --key).
constexpr float kSettle[3] = {-3.5f, -0.6f, -3.5f};

}  // namespace

uint32_t Earthshake::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

bool Earthshake::groundAt(float x, float z) const {
    if (ground_ == nullptr) return true;
    const float per = std::max(ground_->metresPerTile(), 0.001f);
    const int column = int(std::floor(x / per)), row = int(std::floor(-z / per));
    return (ground_->attributesAt(column, row) & (content::kNoMove | content::kNoGround)) == 0;
}

bool Earthshake::open(const std::string& assetDir, content::Textures& textures, const content::Showing& table,
                      const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/earthshake/";
    const char* names[2] = {"GroundStone.obj", "GroundStone2.obj"};
    // Key 0 stands this far under key 1 (muextract --key=0 against --key=1, a rigid lift).
    const float lifts[2] = {112.7f, 174.8f};
    for (int i = 0; i < 2; ++i) {
        models_[i] = Model{};
        loadEffectObj(dir + names[i], kUnit, "DhorSS", models_[i].rock);
        loadEffectObj(dir + names[i], kUnit, "DhorSS_R", models_[i].seam);
        models_[i].lift = lifts[i];
    }
    const auto sheet = [&](const std::string& path) {
        return core::fileExists(path) ? textures.load(path, content::TextureRole::Albedo)
                                      : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    rockTex_ = sheet(dir + "dhorss.png");
    seamTex_ = sheet(dir + "dhorss_r.png");
    if (const content::EffectSheet* wave = table.effect("shockwave")) {
        waveTex_ = sheet(assetDir + "/" + wave->path);
    }
    const bool ok = !models_[0].rock.empty() && !models_[1].rock.empty() && bgfx::isValid(rockTex_) &&
                    bgfx::isValid(seamTex_) && bgfx::isValid(waveTex_);
    core::logf("earthshake: stones %zu and %zu triangles, rings and spikes %s", models_[0].rock.size() / 3,
               models_[1].rock.size() / 3, ok ? "yes" : "NO");
    return ok;
}

void Earthshake::clear() {
    for (Wave& one : waves_) one = Wave{};
    for (Ring& one : rings_) one = Ring{};
    for (Stone& one : stones_) one = Stone{};
    for (Jolt& one : jolts_) one = Jolt{};
    quake_ = 0.0f;
}

void Earthshake::stomp(const float feet[3], float seconds) {
    // From the press, every 400 ms while the horse plays its clip 3.
    for (float at = 0.0f; at < seconds; at += kWaveEvery) {
        for (Wave& wave : waves_) {
            if (wave.alive) continue;
            wave = Wave{};
            wave.alive = true;
            for (int k = 0; k < 3; ++k) wave.at[k] = feet[k];
            wave.wait = at;
            break;
        }
    }
}

void Earthshake::strike(uint32_t owner, const float feet[3], float wait) {
    for (int i = 0; i < 2; ++i) {
        for (Ring& ring : rings_) {
            if (ring.alive) continue;
            ring = Ring{};
            ring.alive = true;
            ring.owner = owner;
            for (int k = 0; k < 3; ++k) ring.at[k] = feet[k];
            ring.wait = wait + float(i) * kRingGap;
            ring.radius = kRings[i];
            break;
        }
    }
    for (Jolt& jolt : jolts_) {
        if (jolt.alive) continue;
        jolt = Jolt{};
        jolt.alive = true;
        jolt.owner = owner;
        for (int k = 0; k < 3; ++k) jolt.at[k] = feet[k];
        jolt.wait = wait;
        break;
    }
}

void Earthshake::raise(const Ring& ring) {
    // Six, 60 degrees apart from a random start, `radius` units out.
    const float m = metre();
    float turn = float(roll() % 360u);
    for (int i = 0; i < kPerRing; ++i) {
        turn += 60.0f;
        const float a = turn * kDegree;
        const float x = ring.at[0] + std::sin(a) * ring.radius * m;
        const float z = ring.at[2] + std::cos(a) * ring.radius * m;
        if (!groundAt(x, z)) continue;
        for (Stone& stone : stones_) {
            if (stone.alive) continue;
            stone = Stone{};
            stone.alive = true;
            stone.model = int(roll() % 2u);
            stone.at[0] = x;
            stone.at[1] = floorAt(x, z);
            stone.at[2] = z;
            stone.yaw = float(roll() % 360u) * kDegree;
            stone.scale = (stone.model == 0 ? 1.2f : 1.0f) + float(roll() % 30u) / 100.0f;
            break;
        }
    }
}

float Earthshake::sunk(const Stone& stone) const {
    // Its one clip at 0.3 a frame, once, held on its last key.
    const float key = std::min(stone.age * kStoneSpeed, 4.0f);
    const float at[5] = {-models_[stone.model].lift, 0.0f, kSettle[0], kSettle[1], kSettle[2]};
    const int k = std::min(int(key), 3);
    const float t = key - float(k);
    return at[k] + (at[k + 1] - at[k]) * t;
}

void Earthshake::gatherEffects(gfx::Effects& effects, float daylight) const {
    const float m = metre();
    // The ground rings: twenty tiles across, closing one a frame.
    if (bgfx::isValid(waveTex_)) {
        for (const Wave& wave : waves_) {
            if (!wave.alive || wave.wait > 0.0f) continue;
            const float half = 0.5f * std::max(0.0f, kWaveScale - wave.age) * 100.0f * m;
            if (half <= 0.0f) continue;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = wave.at[k];
            sprite.position[1] += kWaveOver;
            sprite.placed = true;
            const float corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
            for (int c = 0; c < 4; ++c) {
                sprite.corner[c][0] = wave.at[0] + corner[c][0] * half;
                sprite.corner[c][1] = wave.at[1] + kWaveOver;
                sprite.corner[c][2] = wave.at[2] + corner[c][1] * half;
                sprite.cornerUv[c][0] = 0.5f + 0.5f * corner[c][0];
                sprite.cornerUv[c][1] = 0.5f + 0.5f * corner[c][1];
            }
            for (int k = 0; k < 3; ++k) sprite.colour[k] = kWaveLight;
            sprite.sheet = waveTex_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    // The spikes: the rock lit by the day, its seams bright.
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const float lit = std::clamp(daylight, 0.25f, 1.0f);
    for (const Stone& stone : stones_) {
        if (!stone.alive) continue;
        const Model& model = models_[stone.model];
        // Its last 8 frames: alpha / 1.3 and the seams' light / 2 a frame.
        const float fading = std::max(0.0f, stone.age - (kStoneLife - 8.0f));
        const float alpha = std::pow(1.0f / 1.3f, fading);
        const float bright = std::pow(0.5f, fading);
        const float s = std::sin(stone.yaw), c = std::cos(stone.yaw);
        const float x[3] = {c, 0.0f, -s};
        const float z[3] = {s, 0.0f, c};
        const float at[3] = {stone.at[0], stone.at[1] + sunk(stone) * stone.scale * m, stone.at[2]};
        const float rock[3] = {lit, lit, lit};
        submitEffectAlong(effects, model.rock, rockTex_, gfx::Blend::Alpha, at, x, up, z, stone.scale, rock,
                          alpha);
        const float seam[3] = {bright, bright, bright};
        submitEffectAlong(effects, model.seam, seamTex_, gfx::Blend::Additive, at, x, up, z, stone.scale, seam,
                          1.0f);
    }
}

uint32_t Earthshake::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr || max == 0) return 0;
    // One warm light at the middle of each ring's stones while they stand: MU's two-tile terrain
    // light under each, as one.
    uint32_t count = 0;
    const float metres = ground_ ? ground_->metresPerTile() : 1.0f;
    float sum[3] = {}, most = 0.0f;
    int standing = 0;
    for (const Stone& stone : stones_) {
        if (!stone.alive) continue;
        for (int k = 0; k < 3; ++k) sum[k] += stone.at[k];
        most = std::max(most, 1.0f - stone.age / kStoneLife);
        ++standing;
    }
    if (standing == 0) return 0;
    gfx::PointLight& light = out[count++];
    for (int k = 0; k < 3; ++k) light.position[k] = sum[k] / float(standing);
    light.reach = kLightTiles * metres;
    light.height = 1.0f;
    for (int k = 0; k < 3; ++k) light.colour[k] = kLightColour[k] * most;
    return count;
}

}  // namespace mu::game
