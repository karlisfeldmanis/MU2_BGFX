// The Summoner's casting circle round the caster (CSummonSystem::CreateCastingEffect,
// SummonSystem.cpp:191-230), drawn under the Ancient rune Requiem's caster in Requiem's colours
// (fx/requiem.h). It was the Ancient rune Pollution's, the Book of Lagle's (AT_SKILL_SUMMON_POLLUTION,
// 225); that rune was retired 2026-10-26 (one Ancient a class) and its ghosts went with it, so
// only the circle is left here.
//
// **What is MU's, and kept.** Round the caster: BITMAP_MAGIC subtype 10, BITMAP_LIGHT twelve tiles
// across laid on the land and SUBTRACTED, its alpha rising 0.05 a frame and falling 0.03 over the
// last twenty of 44, drawn up to five times over itself as its life runs out (ZzzEffect.cpp:9755-
// 9767); subtype 9, two empact01 circles at 2.4 and 2.88 tiles in the book's impact light, turning
// -8 and +4 degrees a frame, 40 frames; Suhwanzin1, 11 and 111 (testmsop) and 2, 22 and 222
// (monolines, out of effects/weakness) in the book's two ring lights, at 0.9, turning +-3 degrees
// a frame at a BlendMeshLight climbing to 0.5, 40 frames; Suhwanzin4 in its fourth light opening
// 0.6 a frame for 25.
//
// Presentation only. Pools are sized once and a frame allocates nothing.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class Pollution {
public:
    // The casting rings and their sheets out of effects/pollution and effects/weakness, the
    // ground's light and impact off the cooked showing. False when a ring is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // The casting circle at `caster` (world metres, the ground under him), in a book's colours:
    // CreateCastingEffect's lights for BITMAP_MAGIC 9, Suhwanzin1/11/111, 2/22/222 and 4
    // (SummonSystem.cpp:190-231). Requiem's (fx/requiem.h) is drawn by this.
    struct Palette {
        float impact[3];
        float rings1[3];
        float rings2[3];
        float ring4[3];
    };
    static constexpr Palette kRequiemPalette = {
        {0.7f, 0.7f, 1.0f}, {0.0f, 0.7f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.8f, 0.5f, 1.0f}};
    void circle(const float caster[3], const Palette& palette);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr int kCasts = 4;          // four let go at once, at most
    static constexpr int kRingModels = 7;     // Suhwanzin1, 11, 111, 2, 22, 222, 4

    struct Circle {
        bool alive = false;
        float at[3] = {};
        float left = 0.0f;
        float darkAlpha = 0.0f;   // subtype 10
        float ringAlpha = 1.0f;   // subtype 9
        float ringTurn[2] = {};   // degrees
        float impact[3] = {};     // subtype 9's light, the book's
    };
    struct Ring {
        bool alive = false;
        int model = 0;
        float at[3] = {};
        float turn = 0.0f;
        float glow = 0.0f;
        float scale = 0.9f;
        float left = 0.0f;
        float light[3] = {};      // the book's
    };

    void gatherGround(gfx::Effects& effects, bgfx::TextureHandle sheet, gfx::Blend blend,
                      const float at[3], float across, float turnDegrees, const float colour[3]) const;

    const content::Ground* ground_ = nullptr;
    std::vector<EffectCorner> rings_[kRingModels];
    bgfx::TextureHandle ringSheet_[kRingModels] = {};
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle impact_ = BGFX_INVALID_HANDLE;
    Circle circles_[kCasts] = {};
    Ring ringsLive_[kRingModels * kCasts] = {};
};

}  // namespace mu::game
