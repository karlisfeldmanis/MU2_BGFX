#include "game/fx/raven.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/fx/mu_angles.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kStreakLight[3] = {1.0f, 0.8f, 0.6f};
constexpr float kSpiritLight[3] = {1.0f, 0.6f, 0.4f};
constexpr float kFlareLight[3] = {0.5f, 0.5f, 0.5f};
constexpr float kSparkLight[3] = {0.3f, 0.4f, 0.7f};
constexpr float kGlowLight[3] = {1.0f, 0.6f, 0.4f};
constexpr float kFlashLight[3] = {1.0f, 0.8f, 0.6f};
// A joint's scale is its width in units: the dash's 20..59 (ours their middle), the spirit's 20,
// the flare's 40.
constexpr float kStreakWide = 30.0f, kSpiritWide = 20.0f, kFlareWide = 40.0f;
// A particle is its texels wide at Scale 1: Spark03 32, flare01 64.
constexpr float kSparkTexels = 32.0f, kGlowTexels = 64.0f;
constexpr float kGlowScale = 1.3f;
// Ours: the sparks at half MU's size and the charge's light and the spirits' heads at a third --
// at MU's they swamped the bird (the user, 2026-10-25: "sparkles from bird is to large"), as Aida's
// sparks were (fx/aida_sparks.h).
constexpr float kSparkSize = 0.5f, kGlowSize = 1.0f / 3.0f, kHeadSize = 1.0f / 3.0f;
// How far back its tail is drawn, in frames of its flight. Ours.
constexpr float kTail = 4.0f;
// The ball's spirits: every 22nd of mesh 1's 96 triangles, three vertices each.
constexpr int kSpirits = 15;
// A spirit's ribbon, pieces a step between its tails.
constexpr int kBends = 3;
// A spirit that has struck goes out over these frames, its light with it. Ours.
constexpr float kStruck = 4.0f;
// A spirit's turn grows by MU's own each this many frames of its life. Ours.
constexpr float kTighten = 5.0f;

void toMu(const float world[3], float mu[3]) {
    mu[0] = world[0] * 100.0f;
    mu[1] = -world[2] * 100.0f;
    mu[2] = world[1] * 100.0f;
}
void fromMu(const float mu[3], float world[3]) {
    world[0] = mu[0] * 0.01f;
    world[1] = mu[2] * 0.01f;
    world[2] = -mu[1] * 0.01f;
}

// A strip from `tail` to `head`, `half` either side, as two crossed faces (fx/drain's), the sheet's
// U running from `uTail` to `uHead` along it.
void strip(gfx::Effects& effects, bgfx::TextureHandle sheet, const float tail[3], const float head[3],
           float half, const float light[3], float alpha, float uTail = 0.0f, float uHead = 1.0f) {
    float along[3] = {head[0] - tail[0], head[1] - tail[1], head[2] - tail[2]};
    const float len = std::max(1e-4f, std::sqrt(along[0] * along[0] + along[1] * along[1] + along[2] * along[2]));
    for (float& one : along) one /= len;
    float level[3] = {-along[2], 0.0f, along[0]};
    const float lw = std::sqrt(level[0] * level[0] + level[2] * level[2]);
    if (lw < 1e-3f) {
        level[0] = 1.0f;
        level[2] = 0.0f;
    } else {
        level[0] /= lw;
        level[2] /= lw;
    }
    const float upright[3] = {level[1] * along[2] - level[2] * along[1], level[2] * along[0] - level[0] * along[2],
                              level[0] * along[1] - level[1] * along[0]};
    for (int face = 0; face < 2; ++face) {
        const float* side = face == 0 ? level : upright;
        gfx::Sprite quad;
        quad.placed = true;
        quad.sheet = sheet;
        quad.blend = gfx::Blend::Additive;
        for (int k = 0; k < 3; ++k) quad.colour[k] = std::clamp(light[k] * alpha, 0.0f, 1.0f);
        quad.colour[3] = 1.0f;
        for (int k = 0; k < 3; ++k) {
            quad.corner[0][k] = tail[k] - side[k] * half;
            quad.corner[1][k] = head[k] - side[k] * half;
            quad.corner[2][k] = head[k] + side[k] * half;
            quad.corner[3][k] = tail[k] + side[k] * half;
            quad.position[k] = (tail[k] + head[k]) * 0.5f;
        }
        quad.cornerUv[0][0] = uTail; quad.cornerUv[0][1] = 1.0f;
        quad.cornerUv[1][0] = uHead; quad.cornerUv[1][1] = 1.0f;
        quad.cornerUv[2][0] = uHead; quad.cornerUv[2][1] = 0.0f;
        quad.cornerUv[3][0] = uTail; quad.cornerUv[3][1] = 0.0f;
        effects.add(quad);
    }
}

// A billboard of `half` metres, additive.
void dot(gfx::Effects& effects, bgfx::TextureHandle sheet, const float at[3], float half, const float light[3],
         float alpha) {
    gfx::Sprite one;
    for (int k = 0; k < 3; ++k) {
        one.position[k] = at[k];
        one.colour[k] = std::clamp(light[k] * alpha, 0.0f, 1.0f);
    }
    one.colour[3] = 1.0f;
    one.halfWidth = one.halfHeight = half;
    one.sheet = sheet;
    one.blend = gfx::Blend::Additive;
    effects.add(one);
}

}  // namespace

float RavenShow::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool RavenShow::open(const std::string& assetDir, content::Textures& textures, const content::Showing& table) {
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    light_ = sheet("light");          // BITMAP_LIGHT, Effect/flare01
    spirit_ = sheet("joint_spirit");  // BITMAP_JOINT_SPIRIT, JointSpirit01
    flare_ = sheet("flare");          // BITMAP_FLARE, Effect/Flare
    shiny_ = sheet("shiny");          // BITMAP_SHINY
    spark_ = sheet("spark_flash");    // BITMAP_SPARK + 1, Effect/Spark03
    // MODEL_DARKLORD_SKILL, fx/fire_burst's: DarkLordSkill.bmd's one quad and KingS_R.
    const std::string burst = assetDir + "/effects/fire_burst/";
    flashSheet_ = textures.load(burst + "KingS_R.png", content::TextureRole::Decal);
    flashMesh_.clear();
    const bool flash = loadEffectObj(burst + "DarkLordSkill.obj", 1.0f, "", flashMesh_) && bgfx::isValid(flashSheet_);
    // MODEL_AIR_FORCE, the dash's puff: content/effects/raven.
    const std::string air = assetDir + "/effects/raven/";
    puffSheet_ = textures.load(air + "SpiE_R.png", content::TextureRole::Decal);
    puffMesh_.clear();
    const bool puff = loadEffectObj(air + "AirForce.obj", 1.0f, "", puffMesh_) && bgfx::isValid(puffSheet_);
    const bool whole = bgfx::isValid(light_) && bgfx::isValid(spirit_) && bgfx::isValid(flare_);
    core::logf("raven show: light %s, spirit %s, flare %s, spark %s, flash %s, puff %s",
               bgfx::isValid(light_) ? "yes" : "NO", bgfx::isValid(spirit_) ? "yes" : "NO",
               bgfx::isValid(flare_) ? "yes" : "NO", bgfx::isValid(spark_) ? "yes" : "NO", flash ? "yes" : "NO",
               puff ? "yes" : "NO");
    return whole;
}

RavenShow::Joint* RavenShow::free() {
    for (Joint& one : joints_) {
        if (!one.alive) return &one;
    }
    return nullptr;
}

void RavenShow::spark(const float at[3], float yaw) {
    Joint* one = free();
    if (!one) return;
    *one = Joint{};
    one->alive = true;
    one->kind = kSpark;
    one->life = 10.0f;
    one->scale = 0.8f;
    // (-(2..3), 0, 0) turned by its heading alone: across its way, MU's x to ours.
    const float h = yaw * kPi / 180.0f, speed = (2.0f + float(int(unit() * 2.0f))) * kUnit;
    one->dir[0] = -std::cos(h);
    one->dir[1] = 0.0f;
    one->dir[2] = std::sin(h);
    one->speed = speed;
    for (int k = 0; k < 3; ++k) one->at[k] = one->was[k] = at[k];
}

void RavenShow::streaks(const float at[3]) {
    for (int i = 0; i < 10; ++i) {
        Joint* one = free();
        if (!one) return;
        *one = Joint{};
        one->alive = true;
        one->kind = kStreak;
        one->life = 10.0f;
        const float yaw = unit() * 2.0f * kPi, pitch = 30.0f * kPi / 180.0f;
        one->dir[0] = std::cos(pitch) * std::sin(yaw);
        one->dir[1] = std::sin(pitch);
        one->dir[2] = std::cos(pitch) * std::cos(yaw);
        one->speed = (1.0f + unit() * 4.0f) * kUnit;
        for (int k = 0; k < 3; ++k) one->at[k] = one->was[k] = at[k];
    }
}

void RavenShow::flash(const float at[3]) {
    for (Flash& one : flashes_) {
        if (one.alive) continue;
        one = Flash{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        one.roll = float(int(unit() * 180.0f)) - 90.0f;
        return;
    }
}

void RavenShow::puff(const float at[3], float yaw, float pitch) {
    for (Puff& one : puffs_) {
        if (one.alive) continue;
        one = Puff{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        one.yaw = yaw;
        one.pitch = pitch;
        return;
    }
}

void RavenShow::glow(const float at[3]) {
    Joint* one = free();
    if (!one) return;
    *one = Joint{};
    one->alive = true;
    one->kind = kGlow;
    one->life = 2.0f;
    for (int k = 0; k < 3; ++k) one->at[k] = one->was[k] = at[k];
}

void RavenShow::ball(const float from[3], const float to[3], float yaw) {
    for (int i = 0; i < kSpirits; ++i) {
        Joint* one = free();
        if (!one) return;
        *one = Joint{};
        one->alive = true;
        one->kind = kSpirit;
        one->life = 20.0f;
        one->speed = 40.0f;
        // Off the bird's body, as its triangles' vertices are (ours: spread about it).
        float start[3];
        for (int k = 0; k < 3; ++k) start[k] = from[k] + (unit() - 0.5f) * 0.4f;
        toMu(start, one->mu);
        for (int k = 0; k < 3; ++k) one->muWas[k] = one->mu[k];
        toMu(to, one->aim);
        // Pitched up 0..89, turned +-60 off its heading (ZzzBMD.cpp:2194-2196).
        one->angle[0] = -float(int(unit() * 90.0f));
        one->angle[2] = yaw + float(int(unit() * 120.0f)) - 60.0f;
        for (int k = 0; k < 3; ++k) one->at[k] = one->was[k] = start[k];
    }
}

void RavenShow::flySpirit(Joint& one) {
    // A coin a frame kicks its pitch 20 (ZzzEffectJoint.cpp:3842-3848).
    if (unit() < 0.5f) one.angle[0] += one.angle[0] < -90.0f ? 20.0f : -20.0f;
    // At the target +-70 (-70..79) on the level and 50 up, rolled anew each frame, turned on at
    // its speed less 20 degrees (:3852-3856, ZzzAI.cpp:134-145).
    const float aim[3] = {one.aim[0] + float(int(unit() * 150.0f)) - 70.0f,
                          one.aim[1] + float(int(unit() * 150.0f)) - 70.0f, one.aim[2] + 50.0f};
    // Ours: and tighter as it ages, twice MU's turn by its fifth frame, three times by its tenth, so
    // every one curls in -- at MU's own, one that passed its target sailed on out of the fight.
    const float turn = (one.speed - 20.0f) * (1.0f + one.age / kTighten);
    one.angle[2] = turnAngle2(one.angle[2], createAngle(one.mu[0], one.mu[1], aim[0], aim[1]), turn);
    const float flat = std::hypot(one.mu[0] - aim[0], one.mu[1] - aim[1]);
    one.angle[0] = turnAngle2(one.angle[0], 360.0f - createAngle(one.mu[2], flat, aim[2], 0.0f), turn);
    // How near it is, to the target 50 up itself, not the aim rolled about it (MoveHumming's is to
    // the rolled one, which jumps up to 70 a frame; ours, for the strike below).
    const float near[3] = {one.mu[0] - one.aim[0], one.mu[1] - one.aim[1], one.mu[2] - one.aim[2] - 50.0f};
    const float distance = std::sqrt(near[0] * near[0] + near[1] * near[1] + near[2] * near[2]);
    one.speed = one.speed > 50.0f ? 50.0f : one.speed + 1.0f;
    // Its life down twice as fast within 50 of it, its light / 1.2 a frame under ten (:3864-3872).
    // Ours: within 50 it has struck, and goes out over its next four frames -- at MU's rate it
    // looped a metre about the target before it faded (the user, 2026-10-25: "looks little bit
    // unfinsihes and buggy").
    if (distance < 50.0f) one.life = std::min(one.life, one.age + kStruck);
    if (one.life - one.age < 10.0f) one.light /= 1.2f;
    // On down its heading, Velocity a frame.
    for (int k = 0; k < 3; ++k) one.muWas[k] = one.mu[k];
    const float pitch = one.angle[0] * kMuDegree, yaw = one.angle[2] * kMuDegree;
    one.mu[0] += one.speed * std::cos(pitch) * std::sin(yaw);
    one.mu[1] -= one.speed * std::cos(pitch) * std::cos(yaw);
    one.mu[2] -= one.speed * std::sin(pitch);
}

void RavenShow::level(const float at[3], bool up) {
    for (int i = 0; i < (up ? 5 : 15); ++i) {
        Joint* one = free();
        if (!one) return;
        *one = Joint{};
        one->alive = true;
        one->kind = kFlare;
        one->life = 50.0f;
        one->spin = unit() * 2.0f * kPi;
        one->rise = (2.0f + unit() * 2.5f) * kUnit;
        for (int k = 0; k < 3; ++k) one->aim[k] = at[k];
        for (int k = 0; k < 3; ++k) one->at[k] = one->was[k] = at[k];
    }
}

void RavenShow::horseLevel(const float at[3]) {
    constexpr float kOut = 50.0f * kUnit;
    const float corners[4][2] = {{-kOut, 0.0f}, {0.0f, -kOut}, {kOut, 0.0f}, {0.0f, kOut}};
    for (const auto& corner : corners) {
        const float from[3] = {at[0] + corner[0], at[1], at[2] + corner[1]};
        for (int i = 0; i < 3; ++i) {
            Joint* one = free();
            if (!one) return;
            *one = Joint{};
            one->alive = true;
            one->kind = kFlare;
            one->life = 50.0f;
            one->spin = unit() * 2.0f * kPi;
            one->rise = (2.0f + unit() * 2.5f) * kUnit;
            for (int k = 0; k < 3; ++k) one->aim[k] = one->at[k] = one->was[k] = from[k];
        }
    }
}

void RavenShow::update(float seconds) {
    const float frames = seconds * kFps;
    // The spirits a whole frame at a time, for MU's rolls are a frame's.
    owed_ = std::min(owed_ + frames, 8.0f);
    while (owed_ >= 1.0f) {
        for (Joint& one : joints_) {
            if (!one.alive || one.kind != kSpirit) continue;
            flySpirit(one);
            // Where it stood before the step goes onto its tails, the oldest dropped.
            const int kept = std::min(one.tailCount, kTails - 1);
            for (int i = kept; i > 0; --i) {
                for (int k = 0; k < 3; ++k) one.tails[i][k] = one.tails[i - 1][k];
            }
            fromMu(one.muWas, one.tails[0]);
            one.tailCount = kept + 1;
        }
        owed_ -= 1.0f;
    }
    for (Puff& one : puffs_) {
        if (!one.alive) continue;
        one.scale += 0.2f * frames;
        one.light *= std::pow(1.0f / 1.5f, frames);
        one.left -= frames;
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Flash& one : flashes_) {
        if (!one.alive) continue;
        one.scale += one.velocity * frames;
        one.velocity += 0.02f * frames;
        one.left -= frames;
        if (one.left < 7.0f) one.light *= std::pow(1.0f / 1.8f, frames);
        if (one.left <= 0.0f) one.alive = false;
    }
    for (Joint& one : joints_) {
        if (!one.alive) continue;
        one.age += frames;
        if (one.age >= one.life) {
            one.alive = false;
            continue;
        }
        float before[3] = {one.at[0], one.at[1], one.at[2]};
        if (one.kind == kStreak) {
            // Quicker by half a unit a frame.
            one.speed += 0.5f * kUnit * frames;
            for (int k = 0; k < 3; ++k) one.at[k] += one.dir[k] * one.speed * frames;
        } else if (one.kind == kSpark) {
            for (int k = 0; k < 3; ++k) one.at[k] += one.dir[k] * one.speed * frames;
            one.scale *= std::pow(0.9f, frames);
            one.light *= std::pow(1.0f / 1.03f, frames);
        } else if (one.kind == kSpirit) {
            float mu[3];
            for (int k = 0; k < 3; ++k) mu[k] = one.muWas[k] + (one.mu[k] - one.muWas[k]) * owed_;
            fromMu(mu, one.at);
        } else if (one.kind == kFlare) {
            // A turn about its centre, 40 out, rising.
            const float angle = one.spin + one.age * 0.25f;
            one.at[0] = one.aim[0] + std::cos(angle) * 0.4f;
            one.at[2] = one.aim[2] + std::sin(angle) * 0.4f;
            one.at[1] += one.rise * frames;
        }
        // The tail follows a few frames behind the head.
        const float lag = std::clamp(frames / kTail, 0.0f, 1.0f);
        for (int k = 0; k < 3; ++k) one.was[k] += (before[k] - one.was[k]) * lag;
    }
}

void RavenShow::gather(gfx::Effects& effects) const {
    for (const Joint& one : joints_) {
        if (!one.alive) continue;
        const float left = one.life - one.age;
        if (one.kind == kSpark) {
            if (bgfx::isValid(spark_)) dot(effects, spark_, one.at, kSparkTexels * one.scale * 0.5f * kUnit * kSparkSize, kSparkLight, one.light);
            continue;
        }
        if (one.kind == kGlow) {
            if (bgfx::isValid(light_)) dot(effects, light_, one.at, kGlowTexels * kGlowScale * 0.5f * kUnit * kGlowSize, kGlowLight, 1.0f);
            continue;
        }
        // Its light down over its last frames: a streak's over all ten, a flare's ten, a spirit's
        // its own.
        const float fade = one.kind == kStreak ? std::clamp(left / one.life, 0.0f, 1.0f)
                           : one.kind == kSpirit ? one.light * std::clamp(left / kStruck, 0.0f, 1.0f)
                                                 : std::clamp(left / 10.0f, 0.0f, 1.0f);
        const float* light = one.kind == kStreak ? kStreakLight : one.kind == kSpirit ? kSpiritLight : kFlareLight;
        const float wide = (one.kind == kStreak ? kStreakWide : one.kind == kSpirit ? kSpiritWide : kFlareWide) * kUnit;
        const bgfx::TextureHandle sheet = one.kind == kStreak ? light_ : one.kind == kSpirit ? spirit_ : flare_;
        if (!bgfx::isValid(sheet)) continue;
        if (one.kind == kSpirit) {
            // Its ribbon from the head back through its tails, one sheet stretched along all of
            // it (its bright end at the head), narrower and dimmer toward the end, as MU lays a
            // joint's tails (ZzzEffectJoint.cpp's RenderJoints).
            // Ours: drawn through a Catmull-Rom curve, three pieces a step, so its 20-30 degree turns
            // a frame bend the ribbon rather than kink it.
            const int points = one.tailCount + 1;
            const auto point = [&](int i) -> const float* {
                i = std::clamp(i, 0, points - 1);
                return i == 0 ? one.at : one.tails[i - 1];
            };
            const float count = float(std::max(one.tailCount, 1));
            float was[3];
            for (int k = 0; k < 3; ++k) was[k] = one.at[k];
            for (int i = 0; i + 1 < points; ++i) {
                const float* p0 = point(i - 1);
                const float* p1 = point(i);
                const float* p2 = point(i + 1);
                const float* p3 = point(i + 2);
                for (int piece = 1; piece <= kBends; ++piece) {
                    const float t = float(piece) / float(kBends), t2 = t * t, t3 = t2 * t;
                    float at[3];
                    for (int k = 0; k < 3; ++k) {
                        at[k] = 0.5f * (2.0f * p1[k] + (p2[k] - p0[k]) * t +
                                        (2.0f * p0[k] - 5.0f * p1[k] + 4.0f * p2[k] - p3[k]) * t2 +
                                        (3.0f * p1[k] - p0[k] - 3.0f * p2[k] + p3[k]) * t3);
                    }
                    const float near = 1.0f - (float(i) + t - 1.0f / float(kBends)) / count;
                    const float far = 1.0f - (float(i) + t) / count;
                    strip(effects, sheet, at, was, wide * 0.5f * near, light, fade * near, far, near);
                    for (int k = 0; k < 3; ++k) was[k] = at[k];
                }
            }
        } else {
            strip(effects, sheet, one.was, one.at, wide * 0.5f, light, fade);
        }
        // A spirit's head: a light and a shine of 2 x life/10 + 2.
        if (one.kind == kSpirit && bgfx::isValid(light_)) {
            const float half = (2.0f * left / 10.0f + 2.0f) * 0.08f * kHeadSize;
            dot(effects, light_, one.at, half, kSpiritLight, fade);
            if (bgfx::isValid(shiny_)) dot(effects, shiny_, one.at, half * 0.6f, kSpiritLight, fade);
        }
    }
    // The dash's puffs, turned as the bird was, in MU's frame laid on ours.
    if (!puffMesh_.empty() && bgfx::isValid(puffSheet_)) {
        for (const Puff& one : puffs_) {
            if (!one.alive) continue;
            const float angles[3] = {-one.pitch, 0.0f, one.yaw};
            float m[3][3];
            angleMatrix(angles, m);
            const float x[3] = {m[0][0], m[2][0], -m[1][0]};
            const float y[3] = {m[0][2], m[2][2], -m[1][2]};
            const float z[3] = {m[0][1], m[2][1], -m[1][1]};
            const float colour[3] = {one.light, one.light, one.light};
            submitEffectAlong(effects, puffMesh_, puffSheet_, gfx::Blend::Additive, one.at, x, y, z,
                              one.scale * kUnit, colour, 1.0f);
        }
    }
    // The dash's flash: MU's (45, -90..89, 0), its yaw nought, in MU's frame laid on ours.
    if (flashMesh_.empty() || !bgfx::isValid(flashSheet_)) return;
    for (const Flash& one : flashes_) {
        if (!one.alive) continue;
        const float angles[3] = {45.0f, one.roll, 0.0f};
        float m[3][3];
        angleMatrix(angles, m);
        // MU's x, y and z columns into ours, as fx/fire_burst's wayToWorld on a line facing -Y.
        const float x[3] = {m[0][0], m[2][0], -m[1][0]};
        const float y[3] = {m[0][2], m[2][2], -m[1][2]};
        const float z[3] = {m[0][1], m[2][1], -m[1][1]};
        const float colour[3] = {kFlashLight[0] * one.light, kFlashLight[1] * one.light, kFlashLight[2] * one.light};
        submitEffectAlong(effects, flashMesh_, flashSheet_, gfx::Blend::Additive, one.at, x, y, z, one.scale * kUnit,
                          colour, 1.0f);
    }
}

}  // namespace mu::game
