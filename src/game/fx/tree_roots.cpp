#include "game/fx/tree_roots.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
constexpr float kRootsLife = 20.0f;
constexpr float kRootsLift = 20.0f * kUnit;
constexpr float kPuffLife = 25.0f;
constexpr float kSmokeMetres = 0.64f;  // smoke01 is 64 texels
constexpr float kSmokeLight[3] = {0.5f, 0.7f, 0.5f};
constexpr size_t kMostPuffs = 200;
constexpr const char* kFootsteps = "Bip01 Footsteps";

}  // namespace

float TreeRoots::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool TreeRoots::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    shutdown();
    const std::string dir = assetDir + "/effects/aida/";
    loadEffectObj(dir + "tree_eff.obj", kUnit, "", roots_);
    sheet_ = textures.load(dir + "tree_effa01.png", content::TextureRole::Albedo);
    if (const content::EffectSheet* smoke = table.effect("smoke01"))
        smoke_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    core::logf("tree roots: %zu triangles, sheet %s, smoke %s", roots_.size() / 3,
               bgfx::isValid(sheet_) ? "yes" : "NO", bgfx::isValid(smoke_) ? "yes" : "NO");
    return !roots_.empty();
}

void TreeRoots::shutdown() {
    roots_.clear();
    bursts_.clear();
    puffs_.clear();
    trees_.clear();
    sheet_ = smoke_ = BGFX_INVALID_HANDLE;
}

void TreeRoots::watch(uint32_t id, bool blowing, const Figure& figure, float yaw, float fade,
                      float seconds) {
    Tree& tree = trees_[id];
    const bool began = blowing && !tree.blowing;
    tree.blowing = blowing;
    if (!blowing || fade <= 0.0f || figure.body() == nullptr ||
        figure.body()->skeletonMesh == nullptr) {
        tree.owed = 0.0f;
        return;
    }
    int foot = -1;
    const auto& bones = figure.body()->skeletonMesh->bones();
    for (size_t b = 0; b < bones.size(); ++b)
        if (bones[b].name == kFootsteps) foot = int(b);
    float feet[3];
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    if (foot < 0 || !figure.pointOn(foot, origin, feet)) return;
    if (began) {
        Burst burst;
        for (int i = 0; i < 3; ++i) burst.at[i] = feet[i];
        burst.at[1] += kRootsLift;
        burst.yaw = yaw;
        bursts_.push_back(burst);
    }
    // MU's (0, -100, 20) in the Footsteps bone's frame: a metre ahead of it, 20 cm up.
    float ahead[3];
    const float local[3] = {0.0f, -1.0f, 0.2f};
    if (!figure.pointOn(foot, local, ahead)) return;
    tree.owed += seconds / kFrame;  // CreateParticleFpsChecked: one a reference frame
    while (tree.owed >= 1.0f && puffs_.size() < kMostPuffs) {
        tree.owed -= 1.0f;
        Puff puff;
        for (int i = 0; i < 3; ++i) puff.at[i] = ahead[i];
        puff.scale = (std::floor(unit() * 64.0f) + 64.0f) * 0.01f;
        puff.spin = unit() * 6.2831853f;
        puff.light = fade;
        puffs_.push_back(puff);
    }
}

void TreeRoots::update(float seconds) {
    const float frames = std::min(seconds, 0.25f) / kFrame;
    for (Burst& one : bursts_) one.age += frames;
    bursts_.erase(std::remove_if(bursts_.begin(), bursts_.end(),
                                 [](const Burst& b) { return b.age >= kRootsLife; }),
                  bursts_.end());
    for (Puff& one : puffs_) {
        // A frame: its light times LifeTime / 20, which flares it a little and then sinks it.
        const float before = one.age;
        one.age += frames;
        for (float f = std::floor(before) + 1.0f; f <= one.age; f += 1.0f)
            one.light *= std::max(0.0f, kPuffLife - f) / 20.0f;
        one.scale += 0.15f * frames;
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) { return p.age >= kPuffLife; }),
                 puffs_.end());
}

void TreeRoots::gather(gfx::Effects& effects) const {
    for (const Burst& one : bursts_) {
        // Scale 0.05, up 0.1 a frame to 1; from there its alpha down 0.07 a frame.
        const float scale = std::min(1.0f, 0.05f + 0.1f * one.age);
        const float grown = (1.0f - 0.05f) / 0.1f;
        const float alpha = std::clamp(1.0f - 0.07f * std::max(0.0f, one.age - grown), 0.0f, 1.0f);
        // The mesh's roots run along its own +y in MU's frame; turned half round here, they
        // strike forward, at what the tree swung at, as MU's do in its facing.
        const float c = -std::cos(one.yaw), s = -std::sin(one.yaw);
        const float x[3] = {c, 0.0f, -s}, y[3] = {0.0f, 1.0f, 0.0f}, z[3] = {s, 0.0f, c};
        const float white[3] = {1.0f, 1.0f, 1.0f};
        submitEffectAlong(effects, roots_, sheet_, gfx::Blend::Alpha, one.at, x, y, z, scale,
                          white, alpha);
    }
    if (!bgfx::isValid(smoke_)) return;
    for (const Puff& one : puffs_) {
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) {
            sprite.position[i] = one.at[i];
            sprite.colour[i] = kSmokeLight[i] * one.light;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSmokeMetres * one.scale;
        sprite.spin = one.spin;
        sprite.sheet = smoke_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
