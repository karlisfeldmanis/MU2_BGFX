// The wizard's Teleport: MU's `BITMAP_SPARK + 1` pillar where he fades out and again where he is
// put down (CreateTeleportBegin and CreateTeleportEnd, ZzzEffectMagicSkill.cpp:153-172).
//
// What MU draws (ZzzEffect.cpp:6854, ZzzEffectParticle.cpp:2119 and :6608): an effect ten frames
// long that, every frame, throws eighteen Spark03 sparks up a column from his feet -- one every
// twenty-four units, 4.3 m in all, the bottom one twice the size -- each flying off in a random
// direction at fifty units a frame and shrinking two scale a frame until it goes, three frames for
// most. Their light is the effect's life, a tenth a frame, so the pillar dims as it ends. The
// shape and the timing are kept; how many, how high, how fast and how big are ours, calmed on the
// user's word (below).
//
// Presentation only: the realm put him down, and this is what the eye sees at the two ends.
#pragma once

#include <cstdint>
#include <string>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Blink {
public:
    // Takes Spark03 (`spark_flash`) off the cooked showing. False when it is missing, and then
    // nothing is drawn.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    // A pillar at `feet`, world metres.
    void cast(const float feet[3]);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Pillar {
        bool alive = false;
        float feet[3] = {};
        float left = 0.0f;  // reference frames of the effect's life
        float due = 0.0f;   // reference frames to its next row of sparks
    };
    struct Spark {
        bool alive = false;
        float at[3] = {};
        float velocity[3] = {};  // metres a second
        float scale = 0.0f;      // MU's, shrinking to 0.2
        float light = 1.0f;
        float spin = 0.0f;
    };

    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kLifeFrames = 10.0f;  // the effect's LifeTime
    // **Calmed, ours** (the user, 2026-09-28: "sparkles to crazy"): MU's eighteen a frame up
    // 4.3 m, flying fifty units a frame, was a fountain of glitter. Six a frame up the height of
    // a man, drifting at a fifth of the speed and dimmer; the shape -- a column that flashes out
    // and dies -- is MU's.
    static constexpr int kRows = 6;              // sparks a frame, up the column (MU 18)
    static constexpr float kRowUnits = 32.0f;    // the step up (MU 24, over 18)
    static constexpr float kScale = 6.0f;        // and twice it for the bottom one
    static constexpr float kFlyUnits = 10.0f;    // a frame (MU 50)
    static constexpr float kShrink = 1.2f;       // scale a frame (MU 2)
    static constexpr float kGone = 0.2f;
    static constexpr float kBrightness = 0.6f;
    // Ours: metres across a scale of one -- 0.1 was "sparkles to big".
    static constexpr float kMetresPerScale = 0.05f;

    static constexpr int kPillars = 4;
    static constexpr int kSparks = 256;

    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    Pillar pillars_[kPillars] = {};
    Spark sparks_[kSparks] = {};
    uint32_t dice_ = 0x5EED1234u;
    float unit();
    void row(const Pillar& pillar);
};

}  // namespace mu::game
