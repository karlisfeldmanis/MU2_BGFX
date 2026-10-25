#include "game/fx/berserker.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
constexpr float kSmoke = 0.64f * 0.33f;  // smoke01 is 64 texels; ours at a third
constexpr float kLife = 16.0f;
constexpr float kGreen[3] = {127.0f / 255.0f, 1.0f, 200.0f / 255.0f};
constexpr size_t kMostPuffs = 600;

}  // namespace

float Berserker::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool Berserker::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    shutdown();
    if (const content::EffectSheet* sheet = table.effect("smoke01"))
        smoke_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    core::logf("berserker: breath %s", bgfx::isValid(smoke_) ? "yes" : "NO");
    return bgfx::isValid(smoke_);
}

void Berserker::shutdown() {
    ones_.clear();
    puffs_.clear();
    smoke_ = BGFX_INVALID_HANDLE;
}

void Berserker::watch(uint32_t id, const Figure& figure, bool breathes, float fade, float seconds) {
    One& one = ones_[id];
    one.seen = true;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return;
    if (!one.found) {
        one.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (size_t b = 0; b < bones.size(); ++b)
            if (bones[b].name == "Bone08") one.mouth = int(b);
    }
    if (!breathes || one.mouth < 0) {
        one.owed = 0.0f;
        return;
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float at[3];
    if (!figure.pointOn(one.mouth, origin, at)) return;
    for (one.owed += seconds / kFrame; one.owed >= 1.0f; one.owed -= 1.0f) {
        if (puffs_.size() >= kMostPuffs) break;
        Puff puff;
        for (int i = 0; i < 3; ++i) puff.at[i] = at[i];
        puff.scale = (std::floor(unit() * 32.0f) + 48.0f) * 0.006f;
        puff.spin = unit() * 6.2832f;  // MU's Rotation, WorldTime % 360 as it is made
        puff.fade = fade;
        puffs_.push_back(puff);
    }
}

void Berserker::update(float seconds) {
    for (auto it = ones_.begin(); it != ones_.end();) {
        if (!it->second.seen) it = ones_.erase(it);
        else (it++)->second.seen = false;
    }
    const float frames = std::min(seconds, 0.25f) / kFrame;
    for (Puff& one : puffs_) {
        one.age += frames;
        one.rise += 0.2f * frames;
        one.at[1] += one.rise * kUnit * frames;
        one.scale += 0.05f * frames;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) { return p.age >= kLife; }),
                 puffs_.end());
}

void Berserker::gather(gfx::Effects& effects) const {
    if (!bgfx::isValid(smoke_)) return;
    for (const Puff& one : puffs_) {
        const float light = std::max(0.0f, kLife - one.age) / 8.0f * one.fade;
        gfx::Sprite sprite;
        sprite.sheet = smoke_;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = kGreen[i] * light;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSmoke * one.scale;
        sprite.spin = one.spin;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
