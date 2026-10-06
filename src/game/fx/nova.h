// Nova: the Soul Master's charge and its burst.
//
// What is MU's (MuMain, AT_SKILL_NOVA_BEGIN and AT_SKILL_NOVA):
//   * **the charge** (ZzzCharacter.cpp:5728-5745): while PLAYER_SKILL_HELL_BEGIN plays, every
//     second of his first forty bones sheds `m_bySkillCount + 1` BITMAP_LIGHTs a frame at sub-type
//     6, blue (0.3, 0.3, 1), at 1.3 + 0.08 a stage: the more it has charged, the more and the
//     bigger the lights round him;
//   * **the burst** (MoveHandlers.cpp:3758-3785, ZzzEffectJoint.cpp:730-765): MODEL_CIRCLE at
//     sub-type 1, its own mesh hidden, lets go thirty-six BITMAP_JOINT_SPIRITs a frame a metre
//     over his feet, at `i * (10 + rand() % 10)` degrees round and ten below level, for one frame
//     a stage it charged -- sub-type 6: alpha-blended, fifty units a frame for twenty frames, a
//     five-point tail on one in five (`rand() % 5`) and a head alone on the rest, blue; and on the
//     last of those frames a ring of thirty-six more at sub-type 7, `i * 10` exactly, ten units a
//     frame -- the slow ring left where he stood.
// Ours: a stage's lights are capped so a full charge does not fill the frame. Not the Golden
// Dragon's: Nova is not a fire spell (the user, 2026-10-07: 'dont use nova to dragon its not fire
// spell').
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
    // for a charge at `stage` (0 to 12), in `tint`.
    void charge(const float* points, int count, int stage, const float tint[3]);
    // The burst over `feet`, for a charge at `stage`, in `tint`. Its frames are let go over the
    // next `stage` reference frames, as MU's.
    void release(const float feet[3], int stage, const float tint[3]);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

    static constexpr float kBlue[3] = {0.3f, 0.3f, 1.0f};

private:
    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kLife = 20.0f;              // frames, sub-types 6 and 7
    static constexpr float kFast = 50.0f * kUnit;      // metres a frame, sub-type 6
    static constexpr float kSlow = 10.0f * kUnit;      // sub-type 7
    static constexpr float kOver = 100.0f * kUnit;     // let go this high
    static constexpr float kPitch = -10.0f;            // degrees
    static constexpr float kWidth = 60.0f * kUnit;     // CreateJoint's Scale 60
    static constexpr int kTails = 5;
    static constexpr int kPerFrame = 36;
    static constexpr int kMost = 640;
    // The charge's lights: BITMAP_LIGHT at 1.3 + 0.08 a stage (MU's Scale is the quad's whole
    // width in metres here, as fx/held_lights reads it), living a few frames each. Ours: at most
    // kChargeMost alive, the stage's count kept.
    static constexpr float kLightLife = 6.0f;
    static constexpr int kChargeMost = 160;

    struct Joint {
        bool alive = false;
        bool tailed = false;
        float at[3] = {};
        float step[3] = {};
        float tint[3] = {};
        float left = kLife;
        float tail[kTails][3] = {};
        int tails = 0;
    };
    struct Glow {
        bool alive = false;
        float at[3] = {};
        float size = 0.0f;
        float tint[3] = {};
        float left = 0.0f;
    };
    struct Burst {
        bool alive = false;
        float feet[3] = {};
        float tint[3] = {};
        int framesLeft = 0;  // frames still to let joints go
        float owed = 0.0f;
    };
    void emit(Burst& burst);
    void spawn(const float from[3], float yaw, float speed, bool tailed, const float tint[3]);
    uint32_t roll();

    bgfx::TextureHandle joint_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    Joint joints_[kMost];
    Glow glows_[kChargeMost];
    Burst bursts_[4];
    float owed_ = 0.0f;
    uint32_t dice_ = 0x2545f491u;
};

}  // namespace mu::game
