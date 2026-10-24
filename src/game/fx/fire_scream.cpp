#include "game/fx/fire_scream.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

namespace {

constexpr float kDegree = 3.14159265358979f / 180.0f;

}  // namespace

float FireScream::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool FireScream::open(const std::string& assetDir, content::Textures& textures,
                      const content::Ground* ground) {
    ground_ = ground;
    const auto sheet = [&](const std::string& path) {
        return core::fileExists(assetDir + path)
                   ? textures.load(assetDir + path, content::TextureRole::Decal)
                   : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    const std::string dir = assetDir + "/effects/fire_scream/";
    sheet_.clear();
    streak_.clear();
    ring_.clear();
    loadEffectObj(dir + "darkfirescrem01.obj", kUnit, "motion_blurr", sheet_);
    loadEffectObj(dir + "darkfirescrem01.obj", kUnit, "SwordEffor2", streak_);
    loadEffectObj(dir + "darkfirescrem02.obj", kUnit, "", ring_);
    const auto tail = [](const std::vector<EffectCorner>& tris) {
        float most = 0.0f;
        for (const EffectCorner& c : tris) most = std::max(most, c.z);
        return most;
    };
    sheetTail_ = std::max(tail(sheet_), tail(streak_));
    ringTail_ = tail(ring_);
    sheetTex_ = sheet("/effects/fire_scream/motion_blurr.png");
    streakTex_ = sheet("/effects/fire_scream/swordeffor2.png");
    ringTex_ = sheet("/effects/fire_scream/chainring3.png");
    infernoTex_ = sheet("/effects/fire_scream/inferno.png");
    flameTex_ = sheet("/effects/flame/flame01.png");
    puffTex_ = sheet("/effects/aida/poundingball.png");
    sparkTex_ = sheet("/effects/hit/spark02.png");
    const bool ok = !sheet_.empty() && !streak_.empty() && !ring_.empty() && bgfx::isValid(sheetTex_) &&
                    bgfx::isValid(streakTex_) && bgfx::isValid(ringTex_) && bgfx::isValid(infernoTex_) &&
                    bgfx::isValid(flameTex_) && bgfx::isValid(puffTex_) && bgfx::isValid(sparkTex_);
    core::logf("fire scream: lanes, ribbon, flames and bombs %s", ok ? "yes" : "NO");
    return ok;
}

void FireScream::clear() {
    for (Lane& one : lanes_) one = Lane{};
    for (Flame& one : flames_) one = Flame{};
    for (Puff& one : puffs_) one = Puff{};
    for (Spark& one : sparks_) one = Spark{};
    bombCount_ = 0;
}

void FireScream::loose(const float feet[3], float way) {
    const float m = metre();
    // The grid's row runs against world z: down the aim is (cos, -sin), across it a quarter on.
    for (int side = -1; side <= 1; ++side) {
        const float a = way + float(side) * kTurn * kDegree;
        const float acrossX = -std::sin(way), acrossZ = -std::cos(way);
        for (Lane& lane : lanes_) {
            if (lane.alive) continue;
            lane = Lane{};
            lane.alive = true;
            lane.origin[0] = feet[0] + acrossX * kSide * m * float(side);
            lane.origin[2] = feet[2] + acrossZ * kSide * m * float(side);
            lane.origin[1] = floorAt(lane.origin[0], lane.origin[2]);
            lane.way[0] = std::cos(a);
            lane.way[2] = -std::sin(a);
            lane.across[0] = -lane.way[2];
            lane.across[2] = lane.way[0];
            // The DARK_SCREAM's puff, 20 units on from where it is made.
            for (Puff& puff : puffs_) {
                if (puff.alive) continue;
                puff = Puff{};
                puff.alive = true;
                head(lane, kFrom + 20.0f, puff.at);
                puff.scale = 0.64f + unit() * 0.63f;
                break;
            }
            break;
        }
    }
}

void FireScream::explode(const float at[3]) {
    const float m = metre();
    // MU's three bombs at + (rand % 160, rand % 160): its +x east, its +y the grid's row (-z here).
    for (int b = 0; b < 3 && bombCount_ < kBombs; ++b) {
        float* bomb = bombs_[bombCount_++];
        bomb[0] = at[0] + unit() * 160.0f * m;
        bomb[2] = at[2] - unit() * 160.0f * m;
        bomb[1] = floorAt(bomb[0], bomb[2]) + 80.0f * m;
        int made = 0;
        for (Spark& spark : sparks_) {
            if (spark.alive) continue;
            if (made++ == 20) break;
            spark = Spark{};
            spark.alive = true;
            for (int k = 0; k < 3; ++k) spark.at[k] = bomb[k];
            const float turn = unit() * 360.0f * kDegree, out = (2.0f + unit() * 4.0f) * 3.0f;
            spark.v[0] = std::cos(turn) * out * m;
            spark.v[2] = std::sin(turn) * out * m;
            spark.v[1] = (6.0f + unit() * 15.0f) * m;
            spark.life = 24.0f + float(int(unit() * 16.0f));
            spark.scale = 0.8f + unit() * 0.6f;
        }
    }
}

void FireScream::head(const Lane& lane, float along, float out[3]) const {
    const float m = metre();
    out[0] = lane.origin[0] + lane.way[0] * along * m;
    out[2] = lane.origin[2] + lane.way[2] * along * m;
    out[1] = floorAt(out[0], out[2]) + 3.0f * m;
}

void FireScream::flame(const float at[3], float scale) {
    if (scale <= 0.0f) return;
    for (Flame& one : flames_) {
        if (one.alive) continue;
        one = Flame{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        one.scale = scale - unit() * 0.19f;
        one.gravity = (1.8f + unit() * 0.99f) * one.scale;
        one.life = 33.0f;
        one.spin = unit() * 6.2831853f;
        return;
    }
}

void FireScream::step(float frames) {
    const float m = metre();
    for (Lane& lane : lanes_) {
        if (!lane.alive) continue;
        lane.frame += frames;
        // A flame off each head each whole frame, at the head's scale then: the sheet's
        // (Scale - 0.4) * 3.5 and the flare's. **Ours**: a flame wider than its head is ahead of
        // the lane's start is stood further on, so none stands behind him.
        while (lane.laid < kFrames && float(lane.laid) < lane.frame) {
            const float along = kFrom + kStep * float(lane.laid);
            const float fire = std::max(0.0f, kFireScale - kFireShrink * float(lane.laid));
            const float ring = std::max(0.0f, kRingScale - kRingShrink * float(lane.laid));
            for (const float scale : {(fire - 0.4f) * 3.5f, (ring - 0.4f) * 3.5f}) {
                const float half = 32.0f * scale * kFlameSize;
                float at[3];
                head(lane, std::max(along, half), at);
                flame(at, scale);
            }
            ++lane.laid;
        }
        // The ribbon's head races on, quicker each frame: 1, then 3.5 more once, then 0.5 a frame.
        lane.ribbonDue += frames;
        while (lane.ribbonDue >= 1.0f && lane.ribbonLife > 0.0f) {
            lane.ribbonDue -= 1.0f;
            for (int i = std::min(lane.count, kTails - 1); i > 0; --i) lane.tails[i] = lane.tails[i - 1];
            lane.tails[0] = kFrom + 20.0f + lane.ribbon;
            lane.count = std::min(lane.count + 1, kTails);
            lane.speed += lane.push;
            lane.ribbon += lane.speed;
            lane.push = lane.push < 4.0f ? lane.push + 3.5f : lane.push + 0.5f;
            if (lane.ribbonLife < 5.0f) lane.ribbonLight /= 1.3f;
            lane.ribbonLife -= 1.0f;
        }
        if (lane.frame >= float(kFrames) && lane.ribbonLife <= 0.0f) lane.alive = false;
    }
    for (Flame& one : flames_) {
        if (!one.alive) continue;
        one.at[1] += one.gravity * 0.5f * m * frames;
        one.scale -= one.gravity / 95.0f * frames;
        one.light /= std::pow(1.007f, frames);
        one.life -= frames;
        if (one.life <= 0.0f || one.scale <= 0.0f) one.alive = false;
    }
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        one.scale += 0.19f * frames;
        one.at[1] += 5.0f * m * frames;
        one.life -= frames;
        if (one.life <= 0.0f) one.alive = false;
    }
    for (Spark& one : sparks_) {
        if (!one.alive) continue;
        for (int k = 0; k < 3; ++k) one.at[k] += one.v[k] * frames;
        one.v[1] -= 1.5f * m * frames;
        one.life -= frames;
        if (one.life <= 0.0f) one.alive = false;
    }
}

void FireScream::gatherEffects(gfx::Effects& effects) const {
    const float m = metre();
    const float tile = m / kUnit;
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const float white[3] = {1.0f, 1.0f, 1.0f};
    const float sheetLight[3] = {kSheetLight, kSheetLight, kSheetLight};
    for (const Lane& lane : lanes_) {
        if (!lane.alive) continue;
        // The pair's heads while they live, MU's scale at this frame.
        if (lane.frame < float(kFrames)) {
            const float along = kFrom + kStep * lane.frame;
            // The model's loaded Z is MU's +Y, behind it: down the lane backwards. Each slid on
            // until its tail starts at his shoulders.
            const float back[3] = {-lane.way[0], 0.0f, -lane.way[2]};
            const float fire = kFireScale - kFireShrink * lane.frame;
            if (fire >= 0.1f) {
                const float drawn = fire * kSheetSize;
                float at[3];
                head(lane, std::max(along, sheetTail_ * drawn / kUnit), at);
                submitEffectAlong(effects, sheet_, sheetTex_, gfx::Blend::Additive, at, lane.across, up, back,
                                  drawn * tile, sheetLight, 1.0f);
                submitEffectAlong(effects, streak_, streakTex_, gfx::Blend::Additive, at, lane.across, up, back,
                                  drawn * tile, sheetLight, 1.0f);
            }
            const float ring = kRingScale - kRingShrink * lane.frame;
            if (ring >= 0.1f) {
                float at[3];
                head(lane, std::max(along, ringTail_ * ring / kUnit), at);
                submitEffectAlong(effects, ring_, ringTex_, gfx::Blend::Alpha, at, lane.across, up, back,
                                  ring * tile, white, 1.0f);
            }
        }
        // The ribbon on the ground, its tails from the oldest to the head, Inferno along it.
        if (lane.count >= 2 && lane.ribbonLight > 0.01f) {
            const float half = kRibbonWidth * 0.5f * m;
            for (int i = lane.count - 1; i > 0; --i) {
                float a[3], b[3];
                head(lane, lane.tails[i], a);
                head(lane, lane.tails[i - 1], b);
                gfx::Sprite band;
                band.placed = true;
                band.sheet = infernoTex_;
                band.blend = gfx::Blend::Additive;
                for (int k = 0; k < 3; ++k) {
                    band.corner[0][k] = a[k] - lane.across[k] * half;
                    band.corner[1][k] = b[k] - lane.across[k] * half;
                    band.corner[2][k] = b[k] + lane.across[k] * half;
                    band.corner[3][k] = a[k] + lane.across[k] * half;
                    band.position[k] = (a[k] + b[k]) * 0.5f;
                }
                const float u0 = float(lane.count - 1 - i) / float(lane.count - 1);
                const float u1 = float(lane.count - i) / float(lane.count - 1);
                band.cornerUv[0][0] = u0;
                band.cornerUv[0][1] = 1.0f;
                band.cornerUv[1][0] = u1;
                band.cornerUv[1][1] = 1.0f;
                band.cornerUv[2][0] = u1;
                band.cornerUv[2][1] = 0.0f;
                band.cornerUv[3][0] = u0;
                band.cornerUv[3][1] = 0.0f;
                for (int k = 0; k < 3; ++k) band.colour[k] = lane.ribbonLight;
                effects.add(band);
            }
        }
    }
    for (const Flame& one : flames_) {
        if (!one.alive) continue;
        gfx::Sprite flame;
        for (int k = 0; k < 3; ++k) flame.position[k] = one.at[k];
        flame.halfWidth = flame.halfHeight = 32.0f * one.scale * kFlameSize * m;
        // **Ours**: stood on the ground rather than half in it. MU's is centred 3 units over the
        // land and four metres across, so the ground cut each one off in a flat line and the wall
        // showed stepped edges (the user, 2026-10-24: "cuted/croped parts from spell"); a wider
        // soft fade only dimmed it and kept the steps.
        flame.position[1] += flame.halfHeight;
        flame.spin = one.spin;
        flame.soft = 1.0f;
        for (int k = 0; k < 3; ++k) flame.colour[k] = std::min(1.0f, one.light) * kFlameLight;
        flame.sheet = flameTex_;
        flame.blend = gfx::Blend::Additive;
        effects.add(flame);
    }
    for (const Puff& one : puffs_) {
        if (!one.alive) continue;
        gfx::Sprite puff;
        for (int k = 0; k < 3; ++k) puff.position[k] = one.at[k];
        puff.halfWidth = puff.halfHeight = 32.0f * one.scale * m;
        const float light = std::min(1.0f, one.life / 20.0f);
        for (int k = 0; k < 3; ++k) puff.colour[k] = light;
        puff.sheet = puffTex_;
        puff.blend = gfx::Blend::Additive;
        effects.add(puff);
    }
    for (const Spark& one : sparks_) {
        if (!one.alive) continue;
        gfx::Sprite spark;
        for (int k = 0; k < 3; ++k) spark.position[k] = one.at[k];
        spark.halfWidth = spark.halfHeight = 8.0f * one.scale * m;
        spark.sheet = sparkTex_;
        spark.blend = gfx::Blend::Additive;
        effects.add(spark);
    }
}

}  // namespace mu::game
