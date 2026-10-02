#include "game/fx/deathstab.h"

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

float DeathStab::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool DeathStab::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& /*table*/, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/deathstab/";
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (!core::fileExists(dir + name)) return BGFX_INVALID_HANDLE;
        // Laid along a ribbon once and never tiled, as MU loads both: GL_CLAMP_TO_EDGE.
        return textures.load(dir + name, content::TextureRole::Decal);
    };
    streakSheet_ = sheet("NSkill.png");
    flare_ = sheet("Flare.png");
    const bool sheets = bgfx::isValid(streakSheet_) && bgfx::isValid(flare_);
    core::logf("deathstab: sheets %s", sheets ? "yes" : "NO");
    return sheets;
}

void DeathStab::clear() {
    for (Charge& one : charges_) one = Charge{};
    for (Streak& one : streaks_) one = Streak{};
    for (Spear& one : spears_) one = Spear{};
    for (Helix& one : helices_) one = Helix{};
    for (Wound& one : wounds_) one = Wound{};
}

void DeathStab::begin(uint32_t who, uint32_t target, float clipSeconds) {
    int slot = -1;
    for (int c = 0; c < kCharges; ++c) {
        if (charges_[c].alive && charges_[c].who == who) {
            slot = c;
            break;
        }
        if (!charges_[c].alive && slot < 0) slot = c;
    }
    if (slot < 0) return;
    // Its streaks still being drawn in go with the old charge.
    for (Streak& one : streaks_) {
        if (one.alive && one.charge == slot) one.alive = false;
    }
    Charge& charge = charges_[slot];
    charge = Charge{};
    charge.alive = true;
    charge.who = who;
    charge.target = target;
    // AttackTime's twenty-four frames over the clip as it is actually played.
    charge.rate = clipSeconds > 0.0f ? kClipFrames / (clipSeconds * kReferenceFps) : 1.0f;
}

void DeathStab::moveHelix(Helix& one, float metre) {
    // MoveJoint's sub-type 12: the centre driven along his facing, the point on a circle of 26
    // units about it across his facing and up, sinking as it ages; a tail each move.
    for (int k = 0; k < 3; ++k) one.centre[k] += one.way[k] * kHelixStride * metre;
    const float count = (one.phase + one.left) * 0.1f;
    const float aside = -std::cos(count) * kHelixRadius;
    const float up = std::sin(count) * kHelixRadius - (90.0f - one.left) * 0.3f;
    float side[3];
    across(one.way, side);
    float at[3];
    for (int k = 0; k < 3; ++k) at[k] = one.centre[k] + side[k] * aside * metre;
    at[1] += up * metre;
    one.ribbon.push(at, side, kHelixWidth * metre * 0.5f);
    one.left -= 1.0f;
    if (one.left < 0.0f) one.alive = false;
}

template <int N>
bool DeathStab::drawRibbon(gfx::Effects& effects, const Ribbon<N>& ribbon,
                           bgfx::TextureHandle sheet, const float colour[3], int every,
                           bool skipLong) const {
    // RenderJoint's strip: the sheet's U runs along the tails, V across; both bands of the cross.
    const float longest = kLongestSegment * perUnit();
    for (int band = 0; band < 2; ++band) {
        const auto& edge = band == 0 ? ribbon.side : ribbon.rise;
        for (int t = 0; t + every < ribbon.tails; t += every) {
            const int n = t + every;
            // A segment longer than sixty units is skipped, as RenderJoints skips it.
            float gap = 0.0f;
            for (int k = 0; k < 3; ++k) {
                const float d = (ribbon.side[t][0][k] + ribbon.side[t][1][k]) * 0.5f -
                                (ribbon.side[n][0][k] + ribbon.side[n][1][k]) * 0.5f;
                gap += d * d;
            }
            if (skipLong && gap > longest * longest * float(every * every)) continue;
            gfx::Sprite sprite;
            sprite.placed = true;
            sprite.sheet = sheet;
            sprite.blend = gfx::Blend::Additive;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k];
            sprite.colour[3] = 1.0f;
            const float u0 = float(t) / float(N - 1), u1 = float(n) / float(N - 1);
            for (int k = 0; k < 3; ++k) {
                sprite.corner[0][k] = edge[t][0][k];
                sprite.corner[1][k] = edge[t][1][k];
                sprite.corner[2][k] = edge[n][1][k];
                sprite.corner[3][k] = edge[n][0][k];
                sprite.position[k] = (edge[t][0][k] + edge[n][1][k]) * 0.5f;
            }
            sprite.cornerUv[0][0] = u0;
            sprite.cornerUv[0][1] = 0.0f;
            sprite.cornerUv[1][0] = u0;
            sprite.cornerUv[1][1] = 1.0f;
            sprite.cornerUv[2][0] = u1;
            sprite.cornerUv[2][1] = 1.0f;
            sprite.cornerUv[3][0] = u1;
            sprite.cornerUv[3][1] = 0.0f;
            if (!effects.add(sprite)) return false;
        }
    }
    return true;
}

void DeathStab::gatherEffects(gfx::Effects& effects) const {
    for (const Streak& one : streaks_) {
        if (!one.alive || one.ribbon.tails < 2) continue;
        // Whole and at full light: ours (deathstab.h), where MU skips and fades them away.
        if (!drawRibbon(effects, one.ribbon, streakSheet_, kStreakLight, 1, false)) return;
    }
    for (const Helix& one : helices_) {
        if (!one.alive || one.ribbon.tails < 2) continue;
        if (!drawRibbon(effects, one.ribbon, flare_, kHelixLight, kHelixDrawEvery, true)) return;
    }
}

}  // namespace mu::game
