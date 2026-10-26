// Flame Strike: the Duel Master's Season 4 sword skill, MU's AT_SKILL_FLAME_STRIKE (236) -- a
// flaming blade whirled round him at shoulder height, MuMain's MODEL_EFFECT_FLAME_STRIKE and the
// ribbons it lays, and the fire on each body it struck. Here the Ancient rune Flame Strike's
// (sim/items.h kFlameStrike...), said as Loosed with skill 236 (sim::skill::kFlameStrike) at his
// feet with his heading. docs/plans/ancient-runes/flame-strike.md.
//
// **What is MU's, and kept** (MuMain; 1.03.35 the same). Frames are MU's 25 a second, frame 0 the
// effect's birth.
//   * **The blade** (Data/Effect/FlameStrike.bmd, 280 triangles): placed at his bone 33, the
//     weapon bone, keyed with his PLAYER_SKILL_FLAMESTRIKE (action 184, 14 keys at 0.69 a key a
//     frame, MU's base PlaySpeed; ZzzCharacter.cpp:996). LifeTime 35; Alpha +0.1 a frame to 1, -0.1
//     a frame once LifeTime is under 20, gone under 0.1 (MoveHandlers.cpp:7706-7795). Mesh 0
//     (firehik02) added at Alpha; mesh 1 (flamestani) added at Alpha, its V a four-frame flip-book
//     a quarter every 80 ms (ZzzEffect.cpp:9248-9274). The flame is 2.5 m at keys 0 and 10-13 and
//     4.5 m at keys 1-9 (its bones shoot out as he raises the blade): two poses, blended.
//   * **The ribbons**: while his key is between 5 and 13, ten samples a frame of two of the
//     flame's bones -- A and B, Sphere04 to Sphere02 on Effect/motion_blur_r, C Sphere01 to
//     Sphere03 on Effect/lava -- each a CreateObjectBlur that lives 30 frames from its first
//     sample, gives up one a frame and keeps at most 599, drawn added, its light (n - j) / n and
//     its U j / n along the tail (ZzzEffectBlurSpark.cpp:256-407).
//   * **The fire on a struck body** (eDeBuff_FlameStrikeDamage 83, BITMAP_FIRE_CURSEDLICH subtype
//     2): for 20 frames, every frame at random bones, an orange (1, 0.2, 0) BITMAP_LIGHT (flare01)
//     at scale 4 for that frame and a rising flame -- firehik01, firehik03 or firehik02, life
//     27-31, 17-21 or 12-16, rising 6.4-8.8 units a frame, shrinking 0.05-0.07, alpha in 0.2-0.3 a
//     frame and out 0.2 over its last 15 (MoveHandlers.cpp:902-940, ZzzEffectParticle.cpp:1843-1852,
//     :1920-1929, :318-326, :6090-6125).
//   * flame_strike.wav, played by the caller at the cast.
//
// **Ours, and marked.** No pose on him: a rune plays none, so the blade is a ghost carried round
// his feet on MU's bone-33 path, baked from muextract export-rig on Player/player.bmd action 184
// (kBlade, kRibbonWhite, kRibbonLava in the .cpp: tenths of a key, MU's positions lerped and
// rotations slerped down the skeleton), facing the heading the realm struck along. Subtle and
// clear (the user's rule for skill effects): ribbon B is not drawn and A is dimmer (MU's two white
// strips wash out four metres of screen), and a struck body takes 4 flares and flames a frame, not
// 8. A warm light on the ground under the ring, fading with it, as Fire Slash's. One drawn at a
// time: a roll while one plays strikes unseen.
//
// Presentation only: the realm chose the bodies at the roll and struck them twice on the same
// tick. Pools are sized once and a frame allocates nothing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class FlameStrike {
public:
    // The blade's two poses and their sheets, the lava and the body flames out of
    // effects/flame_strike; the flare and the white ribbon off the cooked showing. False when the
    // blade is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // The blade whirled round `feet` (world metres, the ground under him), MU's front laid along
    // `way` (world x and z, unit length). False, and nothing drawn, while one is still playing.
    bool cast(const float feet[3], const float way[2], uint32_t caster_id);
    // A body it struck set burning for MU's 20 frames; a second restarts it.
    void burn(uint32_t id);
    // `follow(id, feet)` puts the caster's feet in `feet`, false once he is not drawn;
    // `bone(id, pick, at)` a bone of a burning body chosen by `pick`, false once it is not.
    template <typename Follow, typename Bone>
    void update(float seconds, Follow follow, Bone bone);
    void gather(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

    static constexpr float kFps = 25.0f;

private:
    static constexpr float kUnit = 0.01f;
    static constexpr int kFlips = 4;          // flamestani's quarters
    static constexpr int kBlurMost = 600;     // MAX_OBJECT_BLUR_TAILS
    static constexpr int kBurningMost = 16;   // kFlameMost and room
    static constexpr int kFlames = 1024;

    struct Burning {
        uint32_t id = 0;
        int left = 0;  // frames
    };
    struct Flame {
        bool alive = false;
        uint8_t sheet = 0;  // firehik01, 03, 02
        float at[3] = {};
        float rise = 0.0f;  // MU units a frame
        float scale = 1.0f;
        float shrink = 0.0f;
        float alpha = 0.0f;
        float fadeIn = 0.2f;
        float spin = 0.0f;
        int life = 20;
    };
    struct Flare {
        float at[3] = {};
    };
    struct Ribbon {
        float edges[kBlurMost][2][3] = {};  // newest first, world metres
        int count = 0;
        int life = 0;
        bool live = false;
    };

    float unit();
    void step();
    void stepBody(const float at[3]);
    void ribbonAt(const float table[][2][3], float key, float out[2][3]) const;
    void lay(Ribbon& ribbon, const float table[][2][3], float from);
    void age(Ribbon& ribbon);
    void drawRibbon(gfx::Effects& effects, const Ribbon& ribbon, bgfx::TextureHandle sheet,
                    float light) const;

    const content::Ground* ground_ = nullptr;
    std::vector<EffectCorner> blade_[2];            // firehik02, short and long
    std::vector<EffectCorner> flip_[2][kFlips];     // flamestani, each quarter
    mutable std::vector<EffectCorner> bladeBlend_;
    mutable std::vector<EffectCorner> flipBlend_;
    bgfx::TextureHandle bladeSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flipSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;  // motion_blur_r
    bgfx::TextureHandle lava_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;  // flare01
    bgfx::TextureHandle fire_[3] = {};                 // firehik01, 03, 02

    // The one playing.
    bool playing_ = false;
    uint32_t caster_ = 0;
    float owed_ = 0.0f;
    float clock_ = 0.0f;  // seconds since the cast, for the flip-book
    int frame_ = 0;
    float key_ = 0.0f;
    float alpha_ = 0.0f;
    float feet_[3] = {};
    float x_[3] = {1, 0, 0}, y_[3] = {0, 1, 0}, z_[3] = {0, 0, 1};  // his right, up, MU's +Y
    Ribbon whiteBlur_, lavaBlur_;

    Burning burning_[kBurningMost] = {};
    float bodyOwed_ = 0.0f;
    Flare flares_[kBurningMost * 4] = {};  // this frame's
    int flareCount_ = 0;
    Flame flames_[kFlames] = {};
    uint32_t dice_ = 0xF1A3E236u;
};

template <typename Follow, typename Bone>
void FlameStrike::update(float seconds, Follow follow, Bone bone) {
    seconds = seconds < 0.1f ? seconds : 0.1f;
    if (playing_) {
        float at[3];
        if (follow(caster_, at)) {
            for (int k = 0; k < 3; ++k) feet_[k] = at[k];
        }
        owed_ += seconds * kFps;
        clock_ += seconds;
        while (owed_ >= 1.0f && playing_) {
            owed_ -= 1.0f;
            step();
        }
    }
    bodyOwed_ += seconds * kFps;
    while (bodyOwed_ >= 1.0f) {
        bodyOwed_ -= 1.0f;
        flareCount_ = 0;
        for (Burning& one : burning_) {
            if (one.left <= 0) continue;
            --one.left;
            // Ours: 4 a frame, MU's 8.
            for (int i = 0; i < 4; ++i) {
                float at[3];
                if (!bone(one.id, uint32_t(unit() * 1048576.0f), at)) {
                    one.left = 0;
                    break;
                }
                stepBody(at);
            }
        }
        for (Flame& one : flames_) {
            if (!one.alive) continue;
            one.at[1] += one.rise * kUnit;
            one.scale -= one.shrink;
            --one.life;
            if (one.life < 15) {
                one.alpha -= 0.2f;
            } else if (one.alpha < 1.0f) {
                one.alpha += one.fadeIn;
            }
            if (one.life <= 0 || one.scale <= 0.0f || (one.life < 15 && one.alpha <= 0.0f)) {
                one.alive = false;
            }
        }
    }
}

}  // namespace mu::game
