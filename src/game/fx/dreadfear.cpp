#include "game/fx/dreadfear.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;           // MU's reference frame
constexpr float kUnit = 0.01f;            // a MU unit, in metres
constexpr float kSprite = 0.64f * 0.33f;  // the sheets are 64 texels; ours at a third
constexpr float kEyeLight[3] = {0.25f, 0.7f, 0.6f};
constexpr const char* kEyes[2] = {"Bone52", "Bone54"};                       // Dreadfear_Eye52, _Eye54
constexpr const char* kWings[4] = {"Bone32", "Bone34", "Bone51", "Bone53"};  // Dreadfear_Wing32 ...
constexpr size_t kMostPuffs = 500;

}  // namespace

float Dreadfear::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool Dreadfear::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    shutdown();
    const auto cooked = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    eye_ = cooked("impact");        // BITMAP_LIGHT + 1, Object9/Impack03
    cloud_ = cooked("iron_cloud");  // BITMAP_CLUD64, Effect/Clud64
    smoke_ = cooked("smoke01");     // BITMAP_SMOKE
    core::logf("dreadfear: eyes %s, cloud %s, smoke %s", bgfx::isValid(eye_) ? "yes" : "NO",
               bgfx::isValid(cloud_) ? "yes" : "NO", bgfx::isValid(smoke_) ? "yes" : "NO");
    return bgfx::isValid(eye_);
}

void Dreadfear::shutdown() {
    ones_.clear();
    puffs_.clear();
    eye_ = cloud_ = smoke_ = BGFX_INVALID_HANDLE;
}

void Dreadfear::shed(const float at[3], float fade) {
    if (puffs_.size() >= kMostPuffs) return;
    Puff one;
    one.fade = fade;
    one.life = 40.0f;
    one.smoke = std::floor(unit() * 4.0f) != 0.0f;
    one.scale = (std::floor(unit() * 20.0f) + 250.0f) * 0.01f;
    one.spin = unit() * 6.2832f;
    // MU's x and y across, its z up: ours x and z across, y up.
    one.at[0] = at[0] + (std::floor(unit() * 20.0f) - 10.0f) * kUnit;
    one.at[2] = at[2] + (std::floor(unit() * 20.0f) - 10.0f) * kUnit;
    one.at[1] = at[1] - 20.0f * kUnit;
    one.gravity = (std::floor(unit() * 10.0f) + 5.0f) * 0.1f;
    puffs_.push_back(one);
}

void Dreadfear::dissolve(const float at[3], float fade) {
    if (puffs_.size() >= kMostPuffs) return;
    Puff one;
    one.fade = fade;
    one.dying = true;
    one.life = 60.0f;
    one.scale = (std::floor(unit() * 64.0f) + 64.0f) * 0.02f;
    one.spin = unit() * 6.2832f;
    one.at[0] = at[0] + (std::floor(unit() * 250.0f) - 125.0f) * kUnit;
    one.at[2] = at[2] + (std::floor(unit() * 250.0f) - 125.0f) * kUnit;
    one.at[1] = at[1] - std::floor(unit() * 45.0f) * kUnit;
    one.turn = (std::floor(unit() * 100.0f) - 50.0f) * 0.1f * 0.5f * 0.01745329f;  // radians a frame
    puffs_.push_back(one);
}

void Dreadfear::watch(uint32_t id, const Figure& figure, bool standing, bool dying, float fade,
                      float seconds) {
    One& one = ones_[id];
    one.seen = true;
    one.eyeSeen[0] = one.eyeSeen[1] = false;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return;
    if (!one.found) {
        one.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (size_t b = 0; b < bones.size(); ++b) {
            for (int k = 0; k < 2; ++k)
                if (bones[b].name == kEyes[k]) one.eyes[k] = int(b);
            for (int k = 0; k < 4; ++k)
                if (bones[b].name == kWings[k]) one.wings[k] = int(b);
        }
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    one.fade = fade;
    for (int k = 0; k < 2; ++k)
        one.eyeSeen[k] = one.eyes[k] >= 0 && figure.pointOn(one.eyes[k], origin, one.eyeAt[k]);
    float wings[4][3];
    bool has[4];
    for (int k = 0; k < 4; ++k) has[k] = one.wings[k] >= 0 && figure.pointOn(one.wings[k], origin, wings[k]);

    for (one.owed += seconds / kFrame; one.owed >= 1.0f; one.owed -= 1.0f) {
        one.eyeScale = 0.5f + (std::floor(unit() * 10.0f) - 5.0f) * 0.01f;
        if (dying) {
            for (int k = 0; k < 4; ++k)
                if (has[k]) dissolve(wings[k], fade);
            continue;
        }
        if (standing) continue;
        // One frame in three off the first pair, one in three off the second.
        for (int pair = 0; pair < 2; ++pair) {
            if (unit() >= 1.0f / 3.0f) continue;
            for (int k = pair * 2; k < pair * 2 + 2; ++k)
                if (has[k]) shed(wings[k], fade);
        }
    }
}

void Dreadfear::update(float seconds) {
    for (auto it = ones_.begin(); it != ones_.end();) {
        if (!it->second.seen) it = ones_.erase(it);
        else (it++)->second.seen = false;
    }
    const float frames = std::min(seconds, 0.25f) / kFrame;
    for (Puff& one : puffs_) {
        one.age += frames;
        if (one.dying) {
            one.light *= std::pow(1.0f / 1.012f, frames);
            one.scale += 0.01f * frames;
            one.spin += one.turn * frames;
        } else {
            one.light = std::min(1.0f, (one.life - one.age) / 10.0f);
            one.gravity -= 0.01f * frames;
            one.at[1] -= one.gravity * kUnit * frames;
            one.scale -= 0.03f * frames;
            one.alpha -= 0.05f * frames;
        }
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) {
                                    return p.age >= p.life || p.light <= 0.05f || p.alpha <= 0.0f ||
                                           p.scale <= 0.0f;
                                }),
                 puffs_.end());
}

void Dreadfear::gather(gfx::Effects& effects) const {
    const auto add = [&](bgfx::TextureHandle sheet, const float at[3], float half, const float colour[3],
                         float light, float spin) {
        if (!bgfx::isValid(sheet) || light <= 0.0f) return;
        gfx::Sprite sprite;
        sprite.sheet = sheet;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = at[i];
            sprite.colour[i] = colour[i] * light;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = half;
        sprite.spin = spin;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };
    for (const auto& [id, one] : ones_)
        for (int k = 0; k < 2; ++k)
            if (one.eyeSeen[k]) add(eye_, one.eyeAt[k], 0.5f * kSprite * one.eyeScale, kEyeLight, one.fade, 0.0f);
    const float white[3] = {1.0f, 1.0f, 1.0f};
    for (const Puff& one : puffs_) {
        const bgfx::TextureHandle sheet = !one.dying && one.smoke ? smoke_ : cloud_;
        add(sheet, one.at, 0.5f * kSprite * one.scale, white, one.light * std::max(0.0f, one.alpha) * one.fade,
            one.spin);
    }
}

}  // namespace mu::game
