// A thrown Firecracker's firework: what `0xF3 0x40` with CmdType 0 throws at the tile, which
// MuMain's ReceiveServerCommand answers with `CreateEffect(BITMAP_FIRECRACKER0001, ...)`
// (WSclient.cpp:8150-8162). The realm opened the cracker (sim::Realm::crack); this is the show.
//
// MU's chain, at its reference 25 frames a second, every number from the client:
//
//   * **the launcher**, BITMAP_FIRECRACKER0001 (ZzzEffect.cpp:2909, MoveHandlers.cpp:4931): 31
//     frames, sending a rocket up on its frames 31, 24, 17, 9 and 1 from a point up to a metre
//     off the tile either way -- five rockets in a little over a second;
//   * **a rocket**, `BITMAP_JOINT_SPIRIT` sub-type 25 (ZzzEffectJoint.cpp:919-932, 4165-4190): a
//     ribbon of `Shiny01` thirty points long, added, lit (0.9, 0.8, 1). Twenty-six frames. It
//     climbs 9 units a frame twice over -- the joint's own Velocity along its pitch of -90, and
//     the sub-type's `Position[2] += Velocity` -- jitters up to 8 units sideways and widens 3 a
//     frame, a flare (0.5) and a shock ring (0.15) at its head; at frame 10 from the end it
//     bursts, and dims by 1.45 a frame after, still rising on the joint's own nine;
//   * **the burst**, BITMAP_FIRECRACKER0002 (ZzzEffect.cpp:2915-2951): a blast of
//     `explotion01mono` at 0.6 (BITMAP_EXPLOTION_MONO, which rings eExplosion.wav as it is born),
//     sixty sparks flung every way at 12 a frame and thirty that fall, bounce and die (Spark03,
//     sub-types 27 and 28), a shock ring 1.5 to 2.4 across for its one frame, sixty specks of
//     glitter in colours of their own that fly, slow, fall and twinkle (Shiny01 sub-type 6),
//     and SOUND_XMAS_FIRECRACKER. Then for its thirty frames, one frame in five at the client's
//     sixty, a star (BITMAP_FIRECRACKER0003) a metre and a half off it either way: the seven
//     `firecracker000N` frames over its first eight frames, then held, sinking, dimming by 1.05
//     and swelling by 1.02 a frame.
//
// A sprite's size is its sheet's width in MU's units times its Scale (RenderParticles,
// ZzzEffectParticle.cpp:8923). Everything is added (EnableAlphaBlend). Not built: the BITMAP_SHINY
// ribbon's own texture coordinates along the tail, drawn here as the spirits' ribbons are.
#pragma once

#include <cstdint>
#include <string>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Firework {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);
    // Thrown at `at`, world metres on the ground.
    void launch(const float at[3]);
    // Ages everything on MU's clock and calls `burst(at)` once for each rocket that bursts
    // this frame, at the burst's own point -- where the sound belongs.
    template <typename Burst>
    void update(float seconds, Burst burst);
    void gather(gfx::Effects& effects) const;
    bool live() const;

private:
    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;  // metres in one of MU's units
    static constexpr int kTails = 30;
    static constexpr int kLaunchers = 4;
    static constexpr int kRockets = kLaunchers * 5;
    static constexpr int kSparks = kLaunchers * 5 * 90;
    static constexpr int kGlitter = kLaunchers * 5 * 60;
    static constexpr int kStars = kLaunchers * 5 * 16;
    static constexpr int kBlasts = kLaunchers * 5;
    static constexpr int kFlashes = kLaunchers * 5;

    struct Launcher {
        bool alive = false;
        float at[3] = {};
        float left = 0.0f;  // frames
    };
    struct Rocket {
        bool alive = false;
        float at[3] = {};
        float width = 0.0f;   // units
        float light[3] = {};
        float left = 0.0f;
        float tail[kTails][3] = {};
        int tails = 0;
    };
    // A spark, a speck of glitter: MU's particle, in metres a frame.
    struct Mote {
        bool alive = false;
        float at[3] = {};
        float velocity[3] = {};
        float light[3] = {};
        float scale = 0.0f, base = 0.0f, gravity = 0.0f, left = 0.0f;
        bool falls = false;   // a sub-type 28 spark rather than a 27
        bool shown = true;    // glitter's twinkle
    };
    struct Star {
        bool alive = false;
        float at[3] = {};
        float light[3] = {};
        float scale = 0.0f, spin = 0.0f, left = 0.0f;
    };
    struct Blast {
        bool alive = false;
        float at[3] = {};
        float light[3] = {};
        float left = 0.0f;   // frames: the burst's thirty, the blast's twenty within them
    };
    struct Flash {
        bool alive = false;
        float at[3] = {};
        float light[3] = {};
        float scale = 0.0f;
    };

    template <typename Burst>
    void step(Burst& burst);
    void fly(Rocket& rocket, bool& bursts);
    void pop(const Rocket& rocket);
    void ribbon(gfx::Effects& effects, const Rocket& rocket) const;
    float roll(float low, float high);  // [low, high)
    int dice(int n);                    // rand() % n

    const content::Ground* ground_ = nullptr;
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shock_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spark_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle blast_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle stars_[7] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                     BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                     BGFX_INVALID_HANDLE};
    Launcher launchers_[kLaunchers];
    Rocket rockets_[kRockets];
    Mote sparks_[kSparks];
    Mote glitter_[kGlitter];
    Star starList_[kStars];
    Blast blasts_[kBlasts];
    Flash flashes_[kFlashes];
    float owed_ = 0.0f;     // part of a frame not yet stepped
    uint32_t dice_ = 0x2545f491u;
};

template <typename Burst>
void Firework::update(float seconds, Burst burst) {
    if (!live()) {
        owed_ = 0.0f;
        return;
    }
    owed_ += seconds * kFps;
    while (owed_ >= 1.0f) {
        owed_ -= 1.0f;
        step(burst);
    }
}

template <typename Burst>
void Firework::step(Burst& burst) {
    // A sprite lives one frame: last frame's shock rings go before this frame's bursts.
    for (Flash& flash : flashes_) flash.alive = false;
    // The launchers' rockets first, then the rockets, so one sent up this frame flies from it.
    for (Launcher& launcher : launchers_) {
        if (!launcher.alive) continue;
        const int frame = int(launcher.left + 0.5f);
        if (frame == 31 || frame == 24 || frame == 17 || frame == 9 || frame == 1) {
            for (Rocket& rocket : rockets_) {
                if (rocket.alive) continue;
                rocket = Rocket{};
                rocket.alive = true;
                // `rand() % 200 - 100` units either way on the ground, at its height.
                rocket.at[0] = launcher.at[0] + float(dice(200) - 100) * kUnit;
                rocket.at[1] = launcher.at[1];
                rocket.at[2] = launcher.at[2] + float(dice(200) - 100) * kUnit;
                rocket.width = 1.0f;  // CreateJoint's Scale, 1
                rocket.light[0] = 0.9f;
                rocket.light[1] = 0.8f;
                rocket.light[2] = 1.0f;
                rocket.left = 26.0f;
                break;
            }
        }
        launcher.left -= 1.0f;
        if (launcher.left <= 0.0f) launcher.alive = false;
    }
    for (Rocket& rocket : rockets_) {
        if (!rocket.alive) continue;
        bool bursts = false;
        fly(rocket, bursts);
        if (bursts) {
            pop(rocket);
            burst(rocket.at);
        }
    }
    // The burst's stars: one frame in five at the client's sixty is twelve a second, so on a
    // reference frame of 1/25 s, 0.48 of a star -- drawn as a chance.
    for (Blast& blast : blasts_) {
        if (!blast.alive) continue;
        if (roll(0.0f, 1.0f) < 12.0f / kFps) {
            for (Star& star : starList_) {
                if (star.alive) continue;
                star = Star{};
                star.alive = true;
                star.at[0] = blast.at[0] + float(dice(300) - 150) * kUnit;
                star.at[1] = blast.at[1];
                star.at[2] = blast.at[2] + float(dice(300) - 150) * kUnit;
                for (int k = 0; k < 3; ++k) star.light[k] = 0.3f + float(dice(700)) * 0.001f;
                star.scale = 0.5f + float(dice(5)) * 0.1f;
                star.spin = float(dice(360));
                star.left = 15.0f;
                break;
            }
        }
        blast.left -= 1.0f;
        if (blast.left <= 0.0f) blast.alive = false;
    }
    for (Star& star : starList_) {
        if (!star.alive) continue;
        star.left -= 1.0f;
        if (star.left <= 15.0f - 8.0f) {
            star.at[1] -= 1.0f * kUnit;
            for (float& c : star.light) c /= 1.05f;
            star.scale *= 1.02f;
        }
        if (star.left <= 0.0f) star.alive = false;
    }
    for (Mote& spark : sparks_) {
        if (!spark.alive) continue;
        spark.left -= 1.0f;
        for (int k = 0; k < 3; ++k) spark.at[k] += spark.velocity[k];
        for (float& c : spark.light) c /= 1.02f;
        if (!spark.falls) {
            spark.scale /= 1.03f;
        } else {
            spark.scale /= 1.01f;
            if (spark.light[0] <= 0.03f) spark.alive = false;
            spark.at[1] += spark.gravity * 0.5f * kUnit;
            spark.gravity -= 1.5f;
            const float floor = ground_ ? ground_->heightAt(spark.at[0], spark.at[2]) : 0.0f;
            if (spark.at[1] < floor + 3.0f * kUnit) {
                spark.at[1] = floor + 3.0f * kUnit;
                spark.gravity = -spark.gravity * 0.3f;
                spark.left -= 2.0f;
            }
        }
        if (spark.left <= 0.0f) spark.alive = false;
    }
    for (Mote& speck : glitter_) {
        if (!speck.alive) continue;
        speck.left -= 1.0f;
        for (int k = 0; k < 3; ++k) speck.at[k] += speck.velocity[k];
        for (float& c : speck.light) c /= 1.01f;
        if (speck.light[0] < 0.2f) speck.alive = false;
        speck.base /= 1.01f;
        // The twinkle: past its first frames, `rand_fps_check(2)` blanks it one frame in two.
        speck.shown = !(speck.left < 60.0f && roll(0.0f, 1.0f) < 0.5f);
        speck.scale = speck.base;
        if (speck.left < 60.0f) {
            for (float& v : speck.velocity) v /= 1.04f;
            speck.at[1] -= speck.gravity * 0.1f * kUnit;
            speck.gravity += 0.5f;
        }
        if (speck.left <= 0.0f) speck.alive = false;
    }
}

}  // namespace mu::game
