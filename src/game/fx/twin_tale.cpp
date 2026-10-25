#include "game/fx/twin_tale.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
constexpr float kSprite = 0.64f * 0.33f;  // smoke01 and water are 64 texels; ours at a third
constexpr float kMarkHalf = 0.5f;         // RenderTerrainAlphaBitmap's size 1, a tile across
constexpr float kMarkLift = 0.03f;
constexpr float kMarkEvery = 0.5f;        // seconds
constexpr float kBeamHalf = 15.0f * kUnit * 0.33f;
constexpr float kBeamFrames = 6.0f;
constexpr float kChest = 1.2f;
constexpr float kWalkGreen[3] = {0.6f, 1.0f, 0.4f};
constexpr float kHitSmoke[3] = {0.4f, 0.9f, 0.6f};
constexpr float kDeathSmoke[3] = {0.1f, 1.0f, 0.2f};
constexpr float kWater[3] = {0.4f, 1.0f, 0.6f};
constexpr float kBeamLight[3] = {0.6f, 1.0f, 0.6f};
constexpr const char* kHair[2] = {"Bone24", "Bone32"};  // Twintail_Hair24, _Hair32
constexpr size_t kMostMarks = 200;
constexpr size_t kMostPuffs = 1200;
constexpr size_t kMostBeams = 64;

}  // namespace

float TwinTale::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool TwinTale::open(const std::string& assetDir, content::Textures& textures,
                    const content::Showing& table) {
    shutdown();
    const auto cooked = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    spark_ = cooked("spark_flash");    // BITMAP_SPARK + 1, Effect/Spark03
    cloud_ = cooked("cloud");          // BITMAP_CLOUD
    smoke_ = cooked("smoke01");        // BITMAP_SMOKE
    energy_ = cooked("joint_energy");  // BITMAP_JOINT_ENERGY, JointEnergy01
    const std::string water = assetDir + "/effects/kanturu/water.png";  // BITMAP_TWINTAIL_WATER
    if (core::fileExists(water)) water_ = textures.load(water, content::TextureRole::Albedo);
    core::logf("twin tale: marks %s, cloud %s, smoke %s, water %s, beam %s",
               bgfx::isValid(spark_) ? "yes" : "NO", bgfx::isValid(cloud_) ? "yes" : "NO",
               bgfx::isValid(smoke_) ? "yes" : "NO", bgfx::isValid(water_) ? "yes" : "NO",
               bgfx::isValid(energy_) ? "yes" : "NO");
    return bgfx::isValid(spark_);
}

void TwinTale::shutdown() {
    ones_.clear();
    marks_.clear();
    puffs_.clear();
    beams_.clear();
    spark_ = cloud_ = smoke_ = water_ = energy_ = BGFX_INVALID_HANDLE;
}

void TwinTale::puff(const float at[3], const float colour[3], bool water, float fade) {
    if (puffs_.size() >= kMostPuffs) return;
    Puff one;
    for (int i = 0; i < 3; ++i) one.colour[i] = colour[i];
    one.water = water;
    one.fade = fade;
    one.spin = unit() * 6.2832f;
    if (water) {
        // MU's x and y across, its z up: ours x and z across, y up.
        one.at[0] = at[0] + (std::floor(unit() * 120.0f) - 60.0f) * kUnit;
        one.at[2] = at[2] + (std::floor(unit() * 100.0f) - 50.0f) * kUnit;
        one.at[1] = at[1] + std::floor(unit() * 100.0f) * kUnit;
        one.life = 50.0f + std::floor(unit() * 10.0f);
        one.scale = (std::floor(unit() * 32.0f) + 140.0f) * 0.01f;
    } else {
        for (int i = 0; i < 3; ++i) one.at[i] = at[i] + (unit() - 0.5f) * 0.4f;
        one.life = 20.0f + std::floor(unit() * 8.0f);
        one.scale = (std::floor(unit() * 32.0f) + 64.0f) * 0.01f;
        one.rise = 1.0f + unit() * 2.0f;
    }
    puffs_.push_back(one);
}

void TwinTale::watch(uint32_t id, const Figure& figure, bool walking, bool hair, float key,
                     const float* target, bool dying, float dyingKey, const float feet[3],
                     float fade, float seconds) {
    const float floor = feet[1];
    One& one = ones_[id];
    one.seen = true;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return;
    if (!one.found) {
        one.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (int k = 0; k < 2; ++k)
            for (size_t b = 0; b < bones.size(); ++b)
                if (bones[b].name == kHair[k]) one.hair[k] = int(b);
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float tips[2][3];
    bool has[2];
    for (int k = 0; k < 2; ++k) has[k] = one.hair[k] >= 0 && figure.pointOn(one.hair[k], origin, tips[k]);
    // A beam already out stays on its tip as the hair whips (the user saw them left hanging).
    for (Beam& beam : beams_)
        if (beam.id == id && has[beam.hair])
            for (int i = 0; i < 3; ++i) beam.from[i] = tips[beam.hair][i];

    // Walking: a mark under one hair, then the other, every half second.
    if (walking && !dying) {
        one.sinceMark += seconds;
        if (one.sinceMark > kMarkEvery && has[one.nextHair] && marks_.size() < kMostMarks) {
            one.sinceMark = 0.0f;
            Mark mark;
            mark.at[0] = tips[one.nextHair][0];
            mark.at[1] = floor + kMarkLift;
            mark.at[2] = tips[one.nextHair][2];
            mark.fade = fade;
            marks_.push_back(mark);
            one.nextHair ^= 1;
        }
    } else {
        one.sinceMark = 0.0f;
    }
    if (!dying) one.clouded = false;

    float chest[3] = {0.0f, 0.0f, 0.0f};
    if (target) {
        chest[0] = target[0];
        chest[1] = target[1] + kChest;
        chest[2] = target[2];
    }
    for (one.owed += seconds / kFrame; one.owed >= 1.0f; one.owed -= 1.0f) {
        if (dying) {
            if (dyingKey > 3.0f) continue;
            for (int s = 0; s < 5; ++s) puff(feet, kDeathSmoke, false, fade);
            puff(feet, kWater, true, fade);
            if (!one.clouded && marks_.size() + 2 <= kMostMarks) {
                one.clouded = true;
                for (int c = 0; c < 2; ++c) {
                    Mark mark;
                    mark.at[0] = feet[0];
                    mark.at[1] = floor + kMarkLift;
                    mark.at[2] = feet[2];
                    mark.cloud = true;
                    mark.scale = 3.5f;
                    mark.spin = unit() * 6.2832f;
                    mark.fade = fade;
                    marks_.push_back(mark);
                }
            }
            continue;
        }
        if (!hair || !target) continue;
        // The Hair Attack: each tip, one frame in two, a streak at what it attacks.
        for (int k = 0; k < 2; ++k) {
            if (!has[k] || unit() >= 0.5f || beams_.size() >= kMostBeams) continue;
            Beam beam;
            beam.id = id;
            beam.hair = k;
            for (int i = 0; i < 3; ++i) {
                beam.from[i] = tips[k][i];
                beam.to[i] = chest[i];
            }
            beam.fade = fade;
            beams_.push_back(beam);
        }
        // Keys 5.5 to 6.5: smoke and water burst on it.
        if (key >= 5.5f && key <= 6.5f) {
            for (int s = 0; s < 5; ++s) puff(target, kHitSmoke, false, fade);
            for (int w = 0; w < 2; ++w) puff(target, kWater, true, fade);
        }
    }
}

void TwinTale::update(float seconds) {
    for (auto it = ones_.begin(); it != ones_.end();) {
        if (!it->second.seen) it = ones_.erase(it);
        else (it++)->second.seen = false;
    }
    const float frames = std::min(seconds, 0.25f) / kFrame;
    for (Mark& one : marks_) {
        one.age += frames;
        one.alpha -= 0.01f * frames;
        if (one.cloud) continue;
        one.light *= std::pow(1.0f / 1.02f, frames);
        // A second shrinking, a second growing, in turn (MU's m_iAnimation on its own clock).
        const bool growing = int(one.age / 25.0f) % 2 == 1;
        one.scale = growing ? std::min(1.2f, one.scale + 0.02f * frames)
                            : std::max(0.0f, one.scale - 0.015f * frames);
    }
    marks_.erase(std::remove_if(marks_.begin(), marks_.end(),
                                [](const Mark& m) { return m.alpha <= 0.0f; }),
                 marks_.end());
    for (Puff& one : puffs_) {
        one.age += frames;
        if (one.water) {
            one.scale -= 0.013f * frames;
            one.alpha -= 0.05f * frames;
            one.light *= std::pow(1.0f / 1.02f, frames);
        } else {
            one.scale += 0.05f * frames;
            one.light = std::max(0.0f, 1.0f - one.age / one.life);
            one.at[1] += one.rise * kUnit * frames;
        }
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) {
                                    return p.age >= p.life || p.alpha <= 0.0f || p.scale <= 0.0f;
                                }),
                 puffs_.end());
    for (Beam& one : beams_) one.age += frames;
    beams_.erase(std::remove_if(beams_.begin(), beams_.end(),
                                [](const Beam& b) { return b.age >= kBeamFrames; }),
                 beams_.end());
}

void TwinTale::gather(gfx::Effects& effects) const {
    // The marks lie flat on the ground: a placed quad turned by its spin.
    for (const Mark& one : marks_) {
        const bgfx::TextureHandle sheet = one.cloud ? cloud_ : spark_;
        if (!bgfx::isValid(sheet) || one.scale <= 0.0f) continue;
        const float light = std::max(0.0f, one.alpha) * one.light * one.fade;
        const float half = kMarkHalf * one.scale;
        const float c = std::cos(one.spin) * half, s = std::sin(one.spin) * half;
        gfx::Sprite sprite;
        sprite.placed = true;
        const float dx[4] = {-c + s, c + s, c - s, -c - s};
        const float dz[4] = {-s - c, s - c, s + c, -s + c};
        const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
        for (int k = 0; k < 4; ++k) {
            sprite.corner[k][0] = one.at[0] + dx[k];
            sprite.corner[k][1] = one.at[1];
            sprite.corner[k][2] = one.at[2] + dz[k];
            sprite.cornerUv[k][0] = uv[k][0];
            sprite.cornerUv[k][1] = uv[k][1];
        }
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = kWalkGreen[i] * light;
        }
        if (one.cloud)
            for (int i = 0; i < 3; ++i) sprite.colour[i] = kWater[i] * light;
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    for (const Puff& one : puffs_) {
        const bgfx::TextureHandle sheet = one.water ? water_ : smoke_;
        if (!bgfx::isValid(sheet)) continue;
        const float light = one.light * std::max(0.0f, one.alpha) * one.fade;
        gfx::Sprite sprite;
        sprite.sheet = sheet;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = one.colour[i] * light;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSprite * one.scale;
        sprite.spin = one.spin;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    if (!bgfx::isValid(energy_)) return;
    for (const Beam& one : beams_) {
        const float light = (1.0f - one.age / kBeamFrames) * one.fade;
        float along[3] = {one.to[0] - one.from[0], one.to[1] - one.from[1], one.to[2] - one.from[2]};
        const float far = std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]);
        if (far < 0.05f) continue;
        for (float& a : along) a /= far;
        // Across: flat (perpendicular to the beam on the ground) and up, two crossed quads.
        float flat[3] = {along[2], 0.0f, -along[0]};
        const float flen = std::sqrt(flat[0] * flat[0] + flat[2] * flat[2]);
        if (flen > 1e-4f) {
            flat[0] /= flen;
            flat[2] /= flen;
        } else {
            flat[0] = 1.0f;
        }
        const float lift[3] = {0.0f, 1.0f, 0.0f};
        for (const float* width : {static_cast<const float*>(flat), static_cast<const float*>(lift)}) {
            gfx::Sprite sprite;
            sprite.placed = true;
            for (int k = 0; k < 3; ++k) {
                sprite.corner[0][k] = one.from[k] - width[k] * kBeamHalf;
                sprite.corner[1][k] = one.to[k] - width[k] * kBeamHalf;
                sprite.corner[2][k] = one.to[k] + width[k] * kBeamHalf;
                sprite.corner[3][k] = one.from[k] + width[k] * kBeamHalf;
                sprite.position[k] = 0.5f * (one.from[k] + one.to[k]);
                sprite.colour[k] = kBeamLight[k] * light;
            }
            sprite.cornerUv[0][0] = 0.0f;
            sprite.cornerUv[0][1] = 0.0f;
            sprite.cornerUv[1][0] = 1.0f;
            sprite.cornerUv[1][1] = 0.0f;
            sprite.cornerUv[2][0] = 1.0f;
            sprite.cornerUv[2][1] = 1.0f;
            sprite.cornerUv[3][0] = 0.0f;
            sprite.cornerUv[3][1] = 1.0f;
            sprite.colour[3] = 1.0f;
            sprite.sheet = energy_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
