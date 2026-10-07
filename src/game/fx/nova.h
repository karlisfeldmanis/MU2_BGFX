// Nova: the Soul Master's charge and its burst.
//
// What is MU's (MuMain, AT_SKILL_NOVA_BEGIN and AT_SKILL_NOVA):
//   * **the charge's lights** (ZzzCharacter.cpp:5728-5745): while PLAYER_SKILL_HELL_BEGIN plays,
//     and on through PLAYER_SKILL_HELL_START after he lets go, every second of his first forty
//     bones sheds `m_bySkillCount + 1` BITMAP_LIGHTs a frame at sub-type 6, blue (0.3, 0.3, 1), at
//     1.3 + 0.08 a stage. Sub-type 6 lives two frames, rises two and a half units a frame and
//     loses a twentieth of its size and light a frame (ZzzEffectParticle.cpp:3159-3215, 8012-8034);
//   * **the force** (CreateForce, ZzzEffect.cpp:90-103, from the same block): three
//     BITMAP_JOINT_HEALING streaks a frame at sub-type 8, each from five metres out at a random
//     bearing and 0-90 degrees up, plus 1.2 m, homing on a metre over his feet -- seventeen frames,
//     still at first and four units a frame faster each frame, a three-point tail ten wide, light
//     (0.5, 0.5, 0.5-1) dimmed by 1.8 a frame in its last six, and a BITMAP_LIGHT at scale 1 on its
//     head (ZzzEffectJoint.cpp:503-511, 3620-3655);
//   * **the burst** (MoveHandlers.cpp:3758-3785, ZzzEffectJoint.cpp:730-765, 3620-3685): MODEL_CIRCLE
//     at sub-type 1, its own mesh hidden, lets go thirty-six BITMAP_JOINT_SPIRITs a frame a metre
//     over his feet, at `i * (10 + rand() % 10)` degrees round and ten below level, for one frame
//     a stage it charged -- sub-type 6: added (RENDER_TYPE_ALPHA_BLEND, which is MU's
//     GL_ONE/GL_ONE), fifty units a frame and two faster each frame, for twenty frames, one flat
//     face (RENDER_FACE_TWO, the ribbon's sideways axis, which at ten below level lies near flat),
//     blue at full for its life. A five-point tail on one in five (`rand() % 5`); **the other four
//     make no tail and so draw nothing** -- their only mark is the shiny below. Each of the
//     thirty-six, in its last ten frames, lays a BITMAP_SHINY + 1 (Shiny02) where it was let go,
//     at (8 to 15) * 0.05, grey at (6 - |LifeTime - 6|) * 0.15: the flash at his chest as the
//     spokes leave it;
//   * **the slow ring**: on the last of those frames thirty-six more at sub-type 7, `i * 10`
//     exactly, ten units a frame and two faster each frame. MuMain gives sub-type 7 RenderFace 0,
//     which draws no face at all, but the clip (MU2/docs/reference/skill-clips/dark-wizard/
//     nova.mp4) shows the ring going out from him in the same blue: drawn here as the burst's
//     ribbons are, all thirty-six, which at that pitch overlap into one band.
// What the clip shows of the burst and MuMain does not say (MU2/docs/reference/skill-clips/
// dark-wizard/nova.mp4, the same as the wiki's SM-nova.gif; the user, 2026-10-07: 'look at the
// release explosion part we dont have that'): as he lands, a white-blue ball of light round him;
// then a soft, wispy cloud swelling out past five metres, puffs round its rim; then a thick soft
// ring round him, rays going out through it, and the ring going out to the edge of the screen and
// fading. That client plainly draws every spirit's ribbon, not one in five. Drawn here:
//   * every one of the thirty-six a frame with its tail (kAllTailed): their wisps are the rays;
//   * every spirit's head a soft puff (`nova_puff`, our sheet), kPuffSize across at kPuffDim
//     with a little white, fading with its life: hundreds overlapping are the cloud, white where
//     they crowd at its heart and puffed at its rim, as the clip's;
//   * the slow ring's heads the same at kRingPuffSize and kRingPuffDim: its thirty-six overlap
//     into the thick soft ring, dark inside; begun kRingFrom out, as the clip's ring is already
//     round him when the middle goes dark, where MU's leave from his feet;
//   * the rays thin and whiter (kSpokeWidth, kSpokeWhite), as the clip's streaks are;
//   * the flash, a puff at his chest swelling from kFlashFrom to kFlashSize over kFlashLife
//     frames and going out, a whiter heart under it;
//   * the cooked `shockwave` sheet laid flat at his feet under the ring, from kShockFrom to
//     kShockTo metres across over kShockLife frames, for its rays.
// Subtle but still the explosion (the user, 2026-10-07: 'i want the oppsite, that its not so
// bright and more subtle but still effecetive, also for thos lines add some blurriness'): the
// puffs, ring, flash, rays and shock at about half of the clip-matched light, and the rays and
// the force drawn on our blurred lines (`nova_spirit_soft`, `nova_streak_soft`), the force wider.
// Ours:
//   * the charge's lights a bone are capped at kLightsPerBone a frame, not `m_bySkillCount + 1`,
//     and dimmed to kLightDim: MU's sixteen stacked by stage seven read as a white figure; and
//     they grow no further than kLightStages' size, as the twelfth's 2.26 read as blobs;
//   * of the thirty-six shinies a burst frame lays, one in kShinyEvery is drawn, at kShinyDim:
//     stacked whole at one point they are one white blot;
//   * the force's head lights at kForceHead of their light;
//   * the burst's ribbons at kSpokeDim of MU's blue and kSpokeWidth of its width, the ring at
//     kRingDim: every one leaves the same point and thirty-six a frame stacked there burnt white
//     (the user, 2026-10-07: 'more polisded and more subtle');
//   * both fade over their last kFadeFrames, and the ring over its whole life, where MU's hold
//     their light and vanish;
//   * the force at kForceDim of its light;
//   * sprites are a quarter of MU's Scale for their half-width, as fx/held_lights draws
//     BITMAP_LIGHT (MU's is Width * Scale whole, flare01 being 64 wide: 0.32 a Scale).
// Not the Golden Dragon's: Nova is not a fire spell (the user, 2026-10-07: 'dont use nova to
// dragon its not fire spell').
#pragma once

#include <cstdint>
#include <string>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Nova {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    // This frame's charge: the lights shed at `points` (`count` bone positions, world metres)
    // for a charge at `stage` (0 to 12), and the force homing on `feet`, in `tint`. Called from
    // the first frame he gathers until the burst's clip has played out.
    void charge(const float* points, int count, int stage, const float feet[3],
                const float tint[3]);
    // The burst over `feet`, for a charge at `stage`, in `tint`. Its frames are let go over the
    // next `stage` reference frames, as MU's.
    void release(const float feet[3], int stage, const float tint[3]);
    // The stage the last burst went at: the lights keep it through PLAYER_SKILL_HELL_START.
    int lastStage() const { return lastStage_; }
    // A body Nova killed, at its fall: ten BITMAP_LIGHTs a frame at sub-type 5 on random bones
    // of its first thirty-two, blue, for its first thirty frames dead (ZzzCharacter.cpp:3299-3308).
    // `points` are this frame's bone positions; capped at kSparksMost a frame (ours).
    void sparkle(const float* points, int count, const float tint[3]);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

    static constexpr float kBlue[3] = {0.3f, 0.3f, 1.0f};
    // MU's fling of what Nova killed (WSclient.cpp:5871-5892, ZzzCharacter.cpp:3283-3297): turned
    // to its killer and pushed straight back, (40 + rand() % 15) units a frame less a pace that
    // starts at (10 + rand() % 5) * 0.1 and grows one a frame, for its first fifteen frames dead.
    // The distance it has gone by `frames` (25 fps reference frames), in metres.
    static float flung(float frames, float push, float pace);
    static constexpr int kSparkFrames = 30;
    static constexpr int kFlingFrames = 15;

private:
    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kLife = 20.0f;              // frames, sub-types 6 and 7
    static constexpr float kFast = 50.0f * kUnit;      // metres a frame, sub-type 6
    static constexpr float kSlow = 10.0f * kUnit;      // sub-type 7
    static constexpr float kFaster = 2.0f * kUnit;     // and each frame, both
    static constexpr float kOver = 100.0f * kUnit;     // let go this high
    static constexpr float kPitch = -10.0f;            // degrees
    static constexpr float kWidth = 60.0f * kUnit;     // CreateJoint's Scale 60
    // MU's MaxTails 5 holds four behind the head; ours seven, so a ray is one unbroken streak and
    // not dashes (the user, 2026-10-07: 'i dont like that those spell expolodiing lines are cuted').
    static constexpr int kTails = 7;
    static constexpr int kPerFrame = 36;
    static constexpr int kMost = 900;
    static constexpr int kShinyEvery = 9;              // ours
    static constexpr float kShinyDim = 0.6f;           // ours
    static constexpr float kSpokeDim = 0.16f;          // ours
    static constexpr float kSpokeWidth = 0.8f;         // ours, of MU's 60
    static constexpr float kSpokeWhite = 0.0f;         // ours
    static constexpr float kRingDim = 0.06f;           // ours
    static constexpr float kFadeFrames = 10.0f;        // ours
    // Each ray its own pace and life, so they end ragged and each fades out alone, where MU's all
    // stop on one circle (ours, the same complaint).
    static constexpr float kRayPaceSpread = 0.25f;     // +- of MU's pace
    static constexpr float kRayLifeFrom = 0.75f;       // of kLife, to
    static constexpr float kRayLifeTo = 1.1f;
    // The burst's look off the clip (above), all ours.
    static constexpr bool kAllTailed = true;
    static constexpr float kPuffSize = 3.2f;           // metres across
    static constexpr float kPuffDim = 0.11f;
    static constexpr float kPuffWhite = 0.0f;
    // The puffs' blue, deeper than MU's (0.3, 0.3, 1): bright, ours went lavender through the
    // tonemap where the clip's rim is a deep blue.
    static constexpr float kPuffBlue[3] = {0.16f, 0.2f, 1.0f};
    static constexpr float kRingPuffSize = 3.0f;
    static constexpr float kRingPuffDim = 0.38f;
    static constexpr float kRingPuffWhite = 0.02f;
    static constexpr float kRingFrom = 1.2f;           // metres out it begins
    static constexpr float kFlashLife = 9.0f;          // frames
    static constexpr float kFlashFrom = 1.5f;          // metres across
    static constexpr float kFlashSize = 7.0f;
    static constexpr float kFlashDim = 0.45f;
    static constexpr float kFlashOver = 1.0f;          // at his chest, over his feet
    static constexpr float kShockLife = 30.0f;         // frames
    static constexpr float kShockFrom = 3.0f;          // metres across
    static constexpr float kShockTo = 12.0f;
    static constexpr float kShockDim = 0.4f;
    static constexpr float kShockOver = 0.15f;         // over the ground
    // The charge's lights: BITMAP_LIGHT sub-type 6.
    static constexpr float kLightLife = 2.0f;
    static constexpr float kLightRise = 2.5f * kUnit;
    static constexpr int kLightsPerBone = 2;           // ours
    static constexpr float kLightDim = 0.45f;          // ours
    static constexpr int kLightStages = 4;             // ours
    static constexpr int kChargeMost = 160;
    // The force: BITMAP_JOINT_HEALING sub-type 8.
    static constexpr int kForcePerFrame = 3;
    static constexpr float kForceFrom = 500.0f * kUnit;
    static constexpr float kForceLift = 120.0f * kUnit;
    static constexpr float kForceAim = 100.0f * kUnit;
    static constexpr float kForceGain = 4.0f * kUnit;
    static constexpr float kForceLife = 17.0f;
    static constexpr float kForceWidth = 34.0f * kUnit;  // ours, MU's 10: soft, it wants width
    static constexpr int kForceTails = 2;              // MaxTails 3
    static constexpr float kForceHead = 0.3f;          // ours
    static constexpr float kForceDim = 0.35f;          // ours
    static constexpr int kForceMost = 64;
    // Death's sparks: BITMAP_LIGHT sub-type 5, fifty frames, light 0.9 and size 0.95 a frame.
    static constexpr float kSparkLife = 50.0f;
    static constexpr int kSparksMost = 3;              // ours, of MU's ten a frame
    static constexpr int kSparkPool = 240;

    struct Joint {
        bool alive = false;
        bool tailed = false;
        bool ring = false;  // sub-type 7
        float at[3] = {};
        float from[3] = {};
        float way[3] = {};  // unit
        float speed = 0.0f;
        float tint[3] = {};
        float left = kLife;
        float tail[kTails][3] = {};
        int tails = 0;
        uint32_t dice = 0;
    };
    struct Glow {
        bool alive = false;
        float at[3] = {};
        float size = 0.0f;
        float light = 1.0f;
        float rise = 0.0f;   // metres a frame
        float shrink = 1.0f; // size and light kept a frame
        float dim = 1.0f;    // light kept a frame, over `shrink`
        float tint[3] = {};
        float left = 0.0f;
    };
    struct Force {
        bool alive = false;
        float at[3] = {};
        float to[3] = {};
        float speed = 0.0f;
        float light[3] = {};
        float left = kForceLife;
        float tail[kForceTails][3] = {};
        int tails = 0;
    };
    struct Burst {
        bool alive = false;
        float feet[3] = {};
        float tint[3] = {};
        int framesLeft = 0;  // frames still to let joints go
    };
    struct Flash {  // and the ring's shock, the same shape
        bool alive = false;
        float at[3] = {};
        float tint[3] = {};
        float age = 0.0f;  // frames, drawn smoothly between update's whole ones
    };
    void emit(Burst& burst);
    void spawn(const float from[3], float yaw, float speed, bool tailed, const float tint[3]);
    Glow* glowSlot(Glow* pool, int size);
    void ribbon(gfx::Effects& effects, const float (*points)[3], int count, float half,
                const float* side, const float colour[3], bgfx::TextureHandle sheet) const;
    uint32_t roll();
    float unit() { return float(roll() % 10000u) / 10000.0f; }

    bgfx::TextureHandle joint_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle streak_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shock_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle puff_ = BGFX_INVALID_HANDLE;
    Joint joints_[kMost];
    Glow glows_[kChargeMost];
    Glow sparks_[kSparkPool];
    Force forces_[kForceMost];
    Burst bursts_[4];
    Flash flashes_[4];
    Flash shocks_[4];
    float owed_ = 0.0f;   // reference frames owed to update()
    float frames_ = 0.0f; // and how many this drawn frame stood for, which charge() and
                          // sparkle() shed by
    int many(float perFrame);
    int lastStage_ = 0;
    uint32_t dice_ = 0x2545f491u;
};

}  // namespace mu::game
