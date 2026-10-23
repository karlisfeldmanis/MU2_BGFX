#include "game/fx/force.h"

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

float Force::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool Force::open(const std::string& assetDir, content::Textures& textures,
                 const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/force/";
    const auto sheet = [&](const char* name) {
        return core::fileExists(dir + name) ? textures.load(dir + name, content::TextureRole::Decal)
                                            : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    waveSheet_ = sheet("combo3.png");
    coneSheet_ = sheet("strongP_R.png");
    burstSheet_ = sheet("piercing.png");
    streakSheet_ = sheet("streak.png");
    wave_.clear();
    cone_.clear();
    burst_.clear();
    // In MU's units; scaled to metres at the draw, as the ground's tile decides.
    const bool ok = loadEffectObj(dir + "m_waves.obj", 1.0f, "", wave_) &&
                    loadEffectObj(dir + "m_Piercing.obj", 1.0f, "strongP_R", cone_) &&
                    loadEffectObj(dir + "m_Piercing.obj", 1.0f, "piercing", burst_) &&
                    bgfx::isValid(waveSheet_) && bgfx::isValid(coneSheet_) &&
                    bgfx::isValid(burstSheet_) && bgfx::isValid(streakSheet_);
    core::logf("force: waves, piercing and sheets %s", ok ? "yes" : "NO");
    return ok;
}

void Force::clear() {
    for (Charge& one : charges_) one = Charge{};
    for (Wave& one : waves_) one = Wave{};
    for (Pierce& one : pierces_) one = Pierce{};
    for (Streak& one : streaks_) one = Streak{};
}

void Force::begin(uint32_t who, uint32_t target, float clipSeconds) {
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
    // STRIKE's AttackTime over the clip as it is actually played.
    slot->rate = clipSeconds > 0.0f ? kClipFrames / (clipSeconds * kReferenceFps) : 1.0f;
}

void Force::wave(const float at[3], const float way[3], bool small, float scale) {
    for (Wave& one : waves_) {
        if (one.alive) continue;
        one = Wave{};
        one.alive = true;
        one.small = small;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = at[k];
            one.way[k] = way[k];
        }
        one.scale = scale;
        one.gravity = 0.01f;
        one.left = kWaveFrames;
        return;
    }
}

void Force::streak(const float from[3], const float way[3]) {
    for (Streak& one : streaks_) {
        if (one.alive) continue;
        one = Streak{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) {
            one.from[k] = from[k];
            one.way[k] = way[k];
        }
        one.width = between(70.0f, 110.0f);
        const bool warm = unit() < 0.5f;
        one.colour[0] = 1.0f;
        one.colour[1] = warm ? 0.8f : 1.0f;
        one.colour[2] = warm ? 0.6f : 1.0f;
        one.left = kStreakFrames;
        return;
    }
}

void Force::letGo(const float feet[3], const float way[3]) {
    const float metre = perUnit();
    // The piercing, 130 up and down his line.
    for (Pierce& one : pierces_) {
        if (one.alive) continue;
        one = Pierce{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) {
            one.at[k] = feet[k];
            one.way[k] = way[k];
        }
        one.at[1] += kUp * metre;
        one.left = kPierceFrames;
        break;
    }
    // Two waves, each with its two streaks laid where it was made, before the scatter.
    for (int w = 0; w < 2; ++w) {
        float at[3] = {feet[0], feet[1] + kUp * metre, feet[2]};
        for (int j = 0; j < 2; ++j) streak(at, way);
        at[0] += between(-kScatter, kScatter) * metre;
        at[2] += between(-kScatter, kScatter) * metre;
        wave(at, way, false, between(0.1f, 0.6f));
    }
}

void Force::gatherEffects(gfx::Effects& effects) const {
    const float metre = perUnit();
    const float up[3] = {0.0f, 1.0f, 0.0f};
    // The rings stood up across his line. The export is MU's model turned Y-up (MuExtract's
    // AxisConvention: x, z, -y), so m_waves lies flat in it, 36 deep in its Y; MU's Angle[0] 90
    // stands it on its edge, its face down his facing -- a ring the piercing passes through.
    // So the model's X goes across him, its Y (the depth) along his line, and its Z (the
    // loader negates it) up.
    for (const Wave& one : waves_) {
        if (!one.alive) continue;
        const float x[3] = {one.way[2], 0.0f, -one.way[0]};
        const float y[3] = {one.way[0], one.way[1], one.way[2]};
        const float colour[3] = {one.light, one.light, one.light};
        submitEffectAlong(effects, wave_, waveSheet_, gfx::Blend::Additive, one.at, x, y, up,
                          one.scale * metre, colour, 1.0f);
    }
    // The piercing's long Z along his line, its bone already in the export (MuExtract bakes the
    // rest pose), its point (the model's +Z, 193) thrust out ahead of him. **Ours: all of it in
    // front of him** (the user, 2026-10-23: "that spell dont has to go behind the DL only
    // frontal"), where a clip of MU's client draws the cone flaring behind his head. The loader
    // negates Z, so z is his facing turned back.
    for (const Pierce& one : pierces_) {
        if (!one.alive) continue;
        const float x[3] = {one.way[2], 0.0f, -one.way[0]};
        const float z[3] = {-one.way[0], -one.way[1], -one.way[2]};
        // Its back end (-74) brought up to his chest, so nothing of it is behind him.
        float at[3];
        for (int k = 0; k < 3; ++k) at[k] = one.at[k] + one.way[k] * kPierceBack * kPierceScale * metre;
        const float colour[3] = {one.light, one.light, one.light};
        submitEffectAlong(effects, cone_, coneSheet_, gfx::Blend::Additive, at, x, up, z,
                          kPierceScale * metre, colour, 1.0f);
        submitEffectAlong(effects, burst_, burstSheet_, gfx::Blend::Additive, at, x, up, z,
                          kPierceScale * metre, colour, 1.0f);
    }
    // The streaks: a band across and a band up, down his line, as a tail's cross is.
    const float length = kStreakTail * float(kStreakTails) * metre;
    for (const Streak& one : streaks_) {
        if (!one.alive) continue;
        const float across[3] = {one.way[2], 0.0f, -one.way[0]};
        const float half = one.width * 0.5f * metre;
        for (const float* side : {across, up}) {
            gfx::Sprite band;
            band.placed = true;
            band.sheet = streakSheet_;
            band.blend = gfx::Blend::Additive;
            for (int k = 0; k < 3; ++k) {
                const float tip = one.from[k] + one.way[k] * length;
                band.corner[0][k] = one.from[k] - side[k] * half;
                band.corner[1][k] = tip - side[k] * half;
                band.corner[2][k] = tip + side[k] * half;
                band.corner[3][k] = one.from[k] + side[k] * half;
                band.position[k] = (one.from[k] + tip) * 0.5f;
            }
            const float uv[4][2] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
            for (int c = 0; c < 4; ++c) {
                band.cornerUv[c][0] = uv[c][0];
                band.cornerUv[c][1] = uv[c][1];
            }
            for (int k = 0; k < 3; ++k) band.colour[k] = one.colour[k];
            band.colour[3] = 1.0f;
            effects.add(band);
        }
    }
}

}  // namespace mu::game
