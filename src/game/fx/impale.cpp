#include "game/fx/impale.h"

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

float Impale::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Impale::open(const std::string& assetDir, content::Textures& textures,
                  const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/impale/";
    sheet_ = BGFX_INVALID_HANDLE;
    if (core::fileExists(dir + "MMn2.png")) {
        sheet_ = textures.load(dir + "MMn2.png", content::TextureRole::Decal);
    }
    spear_.clear();
    // In MU's units; scaled to metres at the draw, as the ground's tile decides.
    const bool ok = loadEffectObj(dir + "RidingSpear01.obj", 1.0f, "", spear_) &&
                    bgfx::isValid(sheet_);
    core::logf("impale: spear and sheet %s", ok ? "yes" : "NO");
    return ok;
}

void Impale::clear() {
    for (Charge& one : charges_) one = Charge{};
    for (Ghost& one : ghosts_) one = Ghost{};
}

Impale::Ghost* Impale::freeGhost() {
    for (Ghost& one : ghosts_) {
        if (!one.alive) return &one;
    }
    return nullptr;
}

void Impale::begin(uint32_t who, uint32_t target, float clipSeconds) {
    Charge* slot = nullptr;
    for (Charge& one : charges_) {
        if (one.alive && one.who == who) {
            slot = &one;
            break;
        }
        if (!one.alive && !slot) slot = &one;
    }
    if (!slot) return;
    *slot = Charge{};
    slot->alive = true;
    slot->who = who;
    slot->target = target;
    // MU's ride clip's AttackTime over the clip as it is actually played.
    slot->rate = clipSeconds > 0.0f ? kClipFrames / (clipSeconds * kReferenceFps) : 1.0f;
}

void Impale::gatherEffects(gfx::Effects& effects) const {
    const float metre = perUnit();
    // RidingSpear01's long Z laid along his facing, its bright head leading. The loader negates
    // Z, so the model's +Z is drawn on -z: z is the facing turned back.
    for (const Ghost& one : ghosts_) {
        if (!one.alive) continue;
        const float x[3] = {one.way[2], 0.0f, -one.way[0]};
        const float y[3] = {0.0f, 1.0f, 0.0f};
        const float z[3] = {-one.way[0], -one.way[1], -one.way[2]};
        const float light = kGhostLight * one.left * 0.05f;
        const float colour[3] = {light, light, light};
        submitEffectAlong(effects, spear_, sheet_, gfx::Blend::Additive, one.at, x, y, z,
                          kGhostScale * metre, colour, 1.0f);
    }
}

}  // namespace mu::game
