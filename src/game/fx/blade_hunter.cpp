#include "game/fx/blade_hunter.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
constexpr float kBlue[3] = {0.2f, 0.2f, 1.0f};
constexpr float kPasses = 3.0f;   // RenderBody three times over, added
// DinoE's cells are 64 texels; ours at a third.
constexpr float kBlast = 0.64f * 0.33f;
constexpr float kBlastScales[6] = {1.3f, 2.3f, 1.8f, 1.3f, 2.3f, 1.8f};
constexpr size_t kMostFunnels = 16;
constexpr size_t kMostBlasts = 96;

}  // namespace

bool BladeHunter::open(const std::string& assetDir, content::Textures& textures,
                       const content::Showing& table) {
    shutdown();
    const std::string dir = assetDir + "/effects/kanturu/";
    if (loadEffectObj(dir + "BladeTornado01.obj", kUnit, "shockwave", funnel_)) {
        const std::string path = dir + "shockwave.png";
        if (core::fileExists(path)) shockwave_ = textures.load(path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* sheet = table.effect("dino_blast"))
        blast_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    core::logf("blade hunter: funnel %zu triangles, blast %s", funnel_.size() / 3,
               bgfx::isValid(blast_) ? "yes" : "NO");
    return bgfx::isValid(shockwave_);
}

void BladeHunter::shutdown() {
    ones_.clear();
    funnels_.clear();
    blasts_.clear();
    funnel_.clear();
    shockwave_ = blast_ = BGFX_INVALID_HANDLE;
}

bool BladeHunter::watch(uint32_t id, const Figure& figure, int attack, float key, float yaw,
                        float scale, const float* target, float fade, float seconds, float at[3]) {
    One& one = ones_[id];
    one.seen = true;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return false;
    if (!one.found) {
        one.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (size_t b = 0; b < bones.size(); ++b)
            if (bones[b].name == "Bip01 L Hand") one.hand = int(b);
    }
    if (attack != one.lastAttack || key + 0.5f < one.lastKey) {
        one.frames = 0.0f;
        one.struck = false;
    }
    one.lastAttack = attack;
    one.lastKey = key;
    if (attack == 0 || one.hand < 0) return false;
    one.frames += seconds / kFrame;
    // AttackTime 14: thirteen frames into the blow.
    if (one.struck || one.frames < 13.0f) return false;
    one.struck = true;
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float hand[3];
    if (!figure.pointOn(one.hand, origin, hand)) return false;
    float way[2] = {std::sin(yaw), std::cos(yaw)};
    if (target) {
        const float dx = target[0] - hand[0], dz = target[2] - hand[2];
        if (const float far = std::sqrt(dx * dx + dz * dz); far > 0.1f) {
            way[0] = dx / far;
            way[1] = dz / far;
        }
    }
    // On what it hunts, at the blow's height; with nothing, 140 units out along its facing. Laid
    // 140 out from the hand toward him it stood beside and behind him (the user, 2026-10-23:
    // 'the spell is offset?').
    at[0] = target ? target[0] : hand[0] + way[0] * 140.0f * kUnit * scale;
    at[1] = hand[1] - 30.0f * kUnit * scale;
    at[2] = target ? target[2] : hand[2] + way[1] * 140.0f * kUnit * scale;
    if (funnels_.size() < kMostFunnels) {
        Funnel funnel;
        for (int k = 0; k < 3; ++k) funnel.at[k] = at[k];
        // On the ground: its quarry's feet, else a body's height below the blow.
        funnel.at[1] = target ? target[1] : at[1] - 1.2f * scale;
        funnel.yaw = std::atan2(way[0], way[1]);
        funnel.size = 1.0f;  // CreateEffect's own 1.5, not the hunter's 1.3 on top
        funnel.fade = fade;
        funnels_.push_back(funnel);
    }
    for (const float s : kBlastScales) {
        if (blasts_.size() >= kMostBlasts) break;
        Blast blast;
        for (int k = 0; k < 3; ++k) blast.at[k] = at[k];
        blast.scale = s;
        blast.fade = fade;
        blasts_.push_back(blast);
    }
    return true;
}

void BladeHunter::update(float seconds) {
    for (auto it = ones_.begin(); it != ones_.end();) {
        if (!it->second.seen) it = ones_.erase(it);
        else (it++)->second.seen = false;
    }
    const float frames = std::min(seconds, 0.25f) / kFrame;
    for (Funnel& one : funnels_) {
        one.age += frames;
        one.scale = std::max(0.8f, 1.5f - 0.1f * one.age);
    }
    funnels_.erase(std::remove_if(funnels_.begin(), funnels_.end(),
                                  [](const Funnel& f) { return f.age >= 10.0f; }),
                   funnels_.end());
    for (Blast& one : blasts_) one.age += frames;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(),
                                 [](const Blast& b) { return b.age >= 12.0f; }),
                  blasts_.end());
}

void BladeHunter::gather(gfx::Effects& effects) const {
    for (const Funnel& one : funnels_) {
        // The export leaves the model's bones off: its zx03/zx04 turn it 90 degrees about X, so
        // the run that comes out along the .obj's -Z (loadEffectObj's +Z, 6-182 units) stands
        // up in MU: a tornado. Ours stood on the ground where the blow lands.
        const float c = std::cos(one.yaw), s = std::sin(one.yaw);
        const float x[3] = {c, 0.0f, -s};
        const float y[3] = {s, 0.0f, c};
        const float z[3] = {0.0f, 1.0f, 0.0f};
        const float colour[3] = {kBlue[0] * kPasses * one.fade, kBlue[1] * kPasses * one.fade,
                                 kBlue[2] * kPasses * one.fade};
        submitEffectAlong(effects, funnel_, shockwave_, gfx::Blend::Additive, one.at, x, y, z,
                          one.scale * one.size, colour, 1.0f);
    }
    if (!bgfx::isValid(blast_)) return;
    for (const Blast& one : blasts_) {
        gfx::Sprite sprite;
        sprite.sheet = blast_;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = one.at[k];
            sprite.colour[k] = one.fade;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kBlast * one.scale;
        // Frame (12 - LifeTime) / 3, one of four cells along the strip.
        const int cell = std::min(3, int(one.age / 3.0f));
        sprite.u0 = float(cell) * 0.25f;
        sprite.u1 = sprite.u0 + 0.25f;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
