#include "game/fx/witch_queen.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/crowd.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
constexpr float kLightDim = 0.5f;  // ours: her lights and foot glow at half MU's
constexpr float kBoneScale[5] = {0.8f, 1.1f, 1.0f, 1.7f, 0.8f};
constexpr const char* kBones[6] = {"Bip01 L Hand", "zx", "zx01", "zx02", "zx04", "Bip01"};
constexpr float kFootGlow[3] = {0.7f, 0.5f, 0.7f};
constexpr float kSmokeLight[3] = {0.5f, 0.2f, 0.5f};
constexpr float kImpact = 0.64f;                 // Impack03, 64 texels
constexpr float kShinyWide = 0.32f, kShinyTall = 0.64f;   // Shiny02, 32 by 64
constexpr float kSixtyFour = 0.64f;              // Chrome03 and fantaB
constexpr float kAhead = 70.0f * kUnit;          // the blow's flashes off her root (Bip01)
constexpr float kGlowDrop = 65.0f * kUnit * 1.4f;  // the foot glow under it, at her 1.4
constexpr float kTargetLift = 100.0f * kUnit;
constexpr float kRingOut = 250.0f * kUnit;
constexpr float kSky = 1500.0f * kUnit;          // 800 + 700 over the point
constexpr float kStrikeLift = 30.0f * kUnit;
constexpr float kDecalHalf = 1.0f;               // two tiles square
// Ours, the impact on whom she strikes (the user, 2026-10-12: 'work on character impact effects
// there is some burst and some purple smoke'). MU throws the smoke and a flash every frame from
// key 4 and a growing Shiny02 every frame from key 6, at her and at her target; a dozen copies
// piled into a white-violet blob over him and a rayed burst. Here the smoke and flash every other
// frame, the smoke at 0.6; one shine a blow at each place, growing 1.15 a frame, not 1.3, in her
// magenta rather than white.
constexpr float kSmokeKeep = 0.6f;
constexpr float kShineGrow = 1.15f;
constexpr float kShineTint[3] = {1.0f, 0.6f, 1.0f};
constexpr float kStrikeLife = 20.0f;             // MU's joint's LifeTime, the licks and scorch
constexpr float kBlueScale = 2.0f;
constexpr size_t kMostMotes = 900, kMostStrikes = 64, kMostThrows = 64;

}  // namespace

float WitchQueen::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool WitchQueen::open(const std::string& assetDir, content::Textures& textures,
                      const content::Showing& table) {
    shutdown();
    const auto load = [&](const char* name) {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    impact_ = load("impact");          // BITMAP_LIGHT + 1, Object9/Impack03
    shiny_ = load("shiny_02");         // BITMAP_SHINY + 1, Effect/Shiny02
    smoke_ = load("chrome03");         // BITMAP_CHROME3, Effect/Chrome03
    blue_ = load("true_blue");         // BITMAP_TRUE_BLUE, Effect/fantaB
    ground_ = load("magic_ground");    // BITMAP_MAGIC + 1, Effect/Magic_Ground2
    core::logf("witch queen: lights %s, shine %s, smoke %s, blue %s, ground %s",
               bgfx::isValid(impact_) ? "yes" : "NO", bgfx::isValid(shiny_) ? "yes" : "NO",
               bgfx::isValid(smoke_) ? "yes" : "NO", bgfx::isValid(blue_) ? "yes" : "NO",
               bgfx::isValid(ground_) ? "yes" : "NO");
    return bgfx::isValid(impact_);
}

void WitchQueen::shutdown() {
    motes_.clear();
    strikes_.clear();
    throws_.clear();
    lights_.clear();
    queens_.clear();
    impact_ = shiny_ = smoke_ = blue_ = ground_ = BGFX_INVALID_HANDLE;
}

void WitchQueen::mote(Mote::Kind kind, const float at[3], float scale, float fade) {
    if (motes_.size() >= kMostMotes) return;
    Mote one;
    one.kind = kind;
    for (int i = 0; i < 3; ++i) one.at[i] = at[i];
    one.scale = scale;
    one.light = fade;
    // MU rolls only the smoke (rand() % 360) and the foot glow; the flashes, shines and licks
    // stand upright (Rotation 0, ZzzEffectParticle.cpp:1373, 1993-2008, 3357-3372).
    one.spin = kind == Mote::Kind::Smoke ? unit() * 6.2831853f : 0.0f;
    switch (kind) {
    case Mote::Kind::Smoke: one.life = 30.0f; break;
    case Mote::Kind::Flash:
        // Up to 30 units aside (MU's Position[1], across her).
        one.at[0] += (unit() * 60.0f - 30.0f) * kUnit;
        one.life = 19.0f;
        break;
    case Mote::Kind::Shine: one.life = 7.0f; break;
    case Mote::Kind::Blue:
        one.velocity[0] = (unit() * 4.0f - 2.0f) * kUnit;
        one.velocity[1] = (std::floor(unit() * 3.0f) + 1.0f) * kUnit;
        one.life = 24.0f;
        break;
    }
    motes_.push_back(one);
}

void WitchQueen::watch(uint32_t id, const Figure& figure, int attack, float key, const float feet[3],
                       float yaw, const float* target, float fade, float seconds) {
    Queen& queen = queens_[id];
    queen.seen = true;
    if (!figure.body() || !figure.body()->skeletonMesh || fade <= 0.0f) return;
    if (!queen.found) {
        queen.found = true;
        const auto& bones = figure.body()->skeletonMesh->bones();
        for (int k = 0; k < 6; ++k)
            for (size_t b = 0; b < bones.size(); ++b)
                if (bones[b].name == kBones[k]) queen.bones[k] = int(b);
    }
    // A new blow: the clip changed, or the same one began again.
    if (attack != queen.lastAttack || key + 0.5f < queen.lastKey) {
        queen.frames = 0.0f;
        queen.ring = false;
        queen.shone = false;
    }
    queen.lastAttack = attack;
    queen.lastKey = key;

    // Her five lights, this frame's; the roll is MU's, a new one each reference frame.
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    for (int k = 0; k < 5; ++k) {
        Light one;
        if (queen.bones[k] < 0 || !figure.pointOn(queen.bones[k], origin, one.at)) continue;
        one.scale = kBoneScale[k];
        one.roll = queen.roll;
        one.fade = fade;
        one.foot = false;
        lights_.push_back(one);
    }
    // MU's Footstepst is her bone 0, Bip01: her root, at the hips (1.3 m at her 1.4), not the
    // ground. The first cut laid all of these on the ground, where they cut into it as hard-edged
    // blocks. The sub 4 glow 65 under it, at 4, still; the blows' flashes 70 off it.
    float root[3] = {feet[0], feet[1] + 2.0f * kGlowDrop, feet[2]};
    if (queen.bones[5] >= 0) figure.pointOn(queen.bones[5], origin, root);
    lights_.push_back(Light{{root[0], root[1] - kGlowDrop, root[2]}, 4.0f, queen.roll, fade, true});
    const float ahead[3] = {root[0] + std::sin(yaw) * kAhead, root[1], root[2] + std::cos(yaw) * kAhead};
    queen.owed += seconds / kFrame;
    while (queen.owed >= 1.0f) {
        queen.owed -= 1.0f;
        queen.roll = std::floor(unit() * 30.0f) / 100.0f + 0.6f;
        if (attack == 0) continue;
        queen.frames += 1.0f;
        queen.odd = !queen.odd;
        if (key >= 4.0f && queen.odd) {
            mote(Mote::Kind::Smoke, ahead, 2.0f, fade * kSmokeKeep);
            mote(Mote::Kind::Flash, ahead, 1.0f, fade);
        }
        if (attack == 1 && key >= 6.0f && !queen.shone) {
            queen.shone = true;
            mote(Mote::Kind::Shine, ahead, 1.3f, fade);
            if (target) {
                const float above[3] = {target[0], target[1] + kTargetLift, target[2]};
                mote(Mote::Kind::Shine, above, 1.3f, fade);
            }
        }
        // Sub 16 off her target, homing on it 80 up: Thunder's thin fork, two frames long.
        if (attack == 2 && key >= 6.0f && target && throws_.size() < kMostThrows) {
            Throw crackle{{target[0], target[1], target[2]}, {target[0], target[1] + kTargetLift, target[2]}, true,
                          false};
            throws_.push_back(crackle);
        }
        // AttackTime 10: the ring is cast nine frames into her second blow.
        if (attack == 2 && !queen.ring && queen.frames >= 9.0f) {
            queen.ring = true;
            ring(feet, fade);
        }
    }
}

void WitchQueen::ring(const float feet[3], float fade) {
    // At the effect's LifeTime 10, 8, 6, 4 and 2: frames 10 to 18 of its 20, 72 degrees apart.
    core::logf("witch queen: Attack 2's ring of ten strikes cast round %.1f, %.1f", feet[0], feet[2]);
    float turn = unit() * 6.2831853f;
    for (int k = 0; k < 5 && strikes_.size() < kMostStrikes; ++k) {
        turn += 1.2566371f;
        Strike one;
        one.at[0] = feet[0] + std::cos(turn) * kRingOut;
        one.at[1] = feet[1] + kStrikeLift;
        one.at[2] = feet[2] + std::sin(turn) * kRingOut;
        one.wait = 10.0f + 2.0f * float(k);
        one.light = fade;
        one.loud = k == 0;
        strikes_.push_back(one);
    }
}

bool WitchQueen::take(Throw& out) {
    if (throws_.empty()) return false;
    out = throws_.back();
    throws_.pop_back();
    return true;
}

void WitchQueen::update(float seconds) {
    const float frames = std::min(seconds, 0.25f) / kFrame;
    lights_.clear();
    for (auto it = queens_.begin(); it != queens_.end();) {
        if (!it->second.seen) {
            it = queens_.erase(it);
        } else {
            it->second.seen = false;
            ++it;
        }
    }
    for (Mote& one : motes_) {
        one.age += frames;
        for (int i = 0; i < 3; ++i) one.at[i] += one.velocity[i] * frames;
        switch (one.kind) {
        case Mote::Kind::Smoke:
            one.light *= std::pow(0.92f, frames);
            one.scale -= 0.05f * frames;
            break;
        case Mote::Kind::Flash: one.scale *= std::pow(0.92f, frames); break;
        case Mote::Kind::Shine:
            one.scale *= std::pow(kShineGrow, frames);
            one.light *= std::pow(0.9f, frames);
            break;
        case Mote::Kind::Blue: one.scale -= 0.02f * frames; break;
        }
    }
    motes_.erase(std::remove_if(motes_.begin(), motes_.end(),
                                [](const Mote& m) { return m.age >= m.life || m.scale <= 0.0f; }),
                 motes_.end());

    // A ring point's turn: its pair of joints from 1 500 up to it, handed to Thunder (MU throws
    // two; Thunder's one bolt is MU's wide and thin pair); then its licks and scorch.
    for (Strike& one : strikes_) {
        if (!one.thrown) {
            one.wait -= frames;
            if (one.wait > 0.0f) continue;
            one.thrown = true;
            if (throws_.size() < kMostThrows)
                throws_.push_back({{one.at[0], one.at[1] + kSky, one.at[2]}, {one.at[0], one.at[1], one.at[2]}, false,
                                   one.loud});
        }
        one.age += frames;
        one.scorch *= std::pow(1.0f / 1.1f, frames);
        // BITMAP_TRUE_BLUE at 2 off the point every frame.
        for (one.blueOwed += frames; one.blueOwed >= 1.0f; one.blueOwed -= 1.0f)
            mote(Mote::Kind::Blue, one.at, kBlueScale, one.light);
    }
    strikes_.erase(std::remove_if(strikes_.begin(), strikes_.end(),
                                  [](const Strike& one) { return one.age >= kStrikeLife; }),
                   strikes_.end());
}

void WitchQueen::gather(gfx::Effects& effects) const {
    const auto sprite = [&](bgfx::TextureHandle sheet, const float at[3], float halfWide, float halfTall,
                            const float colour[3], float spin) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite one;
        one.sheet = sheet;
        for (int i = 0; i < 3; ++i) {
            one.position[i] = at[i];
            one.colour[i] = colour[i];
        }
        one.colour[3] = 1.0f;
        one.halfWidth = halfWide;
        one.halfHeight = halfTall;
        one.spin = spin;
        one.blend = gfx::Blend::Additive;
        effects.add(one);
    };
    for (const Light& one : lights_) {
        const float dim = kLightDim * one.fade;
        if (one.foot) {
            const float colour[3] = {kFootGlow[0] * dim, kFootGlow[1] * dim, kFootGlow[2] * dim};
            sprite(shiny_, one.at, 0.5f * kShinyWide * one.scale, 0.5f * kShinyTall * one.scale, colour,
                   one.roll * 20.0f);
            continue;
        }
        const float colour[3] = {(one.roll + 0.5f) * dim, (one.roll - 0.05f) * dim, (one.roll + 0.5f) * dim};
        sprite(impact_, one.at, 0.5f * kImpact * one.scale, 0.5f * kImpact * one.scale, colour, 0.0f);
    }
    for (const Mote& one : motes_) {
        const float light = one.light;
        float colour[3] = {light, light, light};
        switch (one.kind) {
        case Mote::Kind::Smoke:
            for (int i = 0; i < 3; ++i) colour[i] = kSmokeLight[i] * light;
            sprite(smoke_, one.at, 0.5f * kSixtyFour * one.scale, 0.5f * kSixtyFour * one.scale, colour, one.spin);
            break;
        case Mote::Kind::Flash:
            sprite(impact_, one.at, 0.5f * kImpact * one.scale, 0.5f * kImpact * one.scale, colour, one.spin);
            break;
        case Mote::Kind::Shine:
            for (int i = 0; i < 3; ++i) colour[i] = kShineTint[i] * light;
            sprite(shiny_, one.at, 0.5f * kShinyWide * one.scale, 0.5f * kShinyTall * one.scale, colour, one.spin);
            break;
        case Mote::Kind::Blue:
            // MU overwrites its light every frame as LifeTime / 25 (:8333-8335): it fades out.
            for (int i = 0; i < 3; ++i) colour[i] = one.light * std::max(0.0f, one.life - one.age) / 25.0f;
            sprite(blue_, one.at, 0.5f * kSixtyFour * one.scale, 0.5f * kSixtyFour * one.scale, colour, one.spin);
            break;
        }
    }
    // Under each strike, Magic_Ground2 lying on the ground two tiles square, fading from 0.3.
    if (!bgfx::isValid(ground_)) return;
    for (const Strike& one : strikes_) {
        if (!one.thrown) continue;
        gfx::Sprite decal;
        decal.placed = true;
        decal.blend = gfx::Blend::Additive;
        decal.sheet = ground_;
        const float y = one.at[1] - kStrikeLift + 0.02f;
        const float x[4] = {-kDecalHalf, kDecalHalf, kDecalHalf, -kDecalHalf};
        const float z[4] = {-kDecalHalf, -kDecalHalf, kDecalHalf, kDecalHalf};
        for (int c = 0; c < 4; ++c) {
            decal.corner[c][0] = one.at[0] + x[c];
            decal.corner[c][1] = y;
            decal.corner[c][2] = one.at[2] + z[c];
            decal.cornerUv[c][0] = x[c] > 0.0f ? 1.0f : 0.0f;
            decal.cornerUv[c][1] = z[c] > 0.0f ? 1.0f : 0.0f;
        }
        for (int i = 0; i < 3; ++i) {
            decal.colour[i] = one.scorch * one.light;
            decal.position[i] = one.at[i];
        }
        decal.position[1] = y;
        decal.colour[3] = 1.0f;
        effects.add(decal);
    }
}

}  // namespace mu::game
