// What an elite monster looks at you with. MuMain's RenderEye (ZzzCharacter.cpp:8465):
//
//   void RenderEye(OBJECT* o, int Left, int Right, float fSize = 1.0f) {
//       float Luminosity = sinf(WorldTime * 0.002f) * 0.3f + 0.8f;
//       Vector(5.f, 0.f, 0.f, p);  b->TransformPosition(o->BoneTransform[Left], p, Position, true);
//       CreateSprite(BITMAP_SHINY + 3, Position, fSize, Light, NULL);
//       Vector(-5.f, 0.f, 0.f, p); b->TransformPosition(o->BoneTransform[Right], p, Position, true);
//       CreateSprite(BITMAP_SHINY + 3, Position, fSize, Light, NULL);
//   }
//
// called from the render switch as
//
//   case MODEL_BULL_FIGHTER:
//       if ((o->Type == MODEL_BULL_FIGHTER && c->Level == 1) || ...) RenderEye(o, 22, 23);
//
// and Level is 1 on the Elite Bull Fighter alone, so the plain bull has no eyes lit. Bones 22
// and 23 are top_bone02 and top_bone01. The caller finds them and feeds the two points here
// every frame; this draws them.
//
// BITMAP_SHINY + 3 is Effect/eye01.jpg, a 32x16 almond of red on black. A sprite of subtype 0
// goes through EnableAlphaBlend, (ONE, ONE), and is its sheet's size times the sprite's Scale
// -- 32 by 16 units, a third of a metre across -- whatever the size of the thing wearing it.
// The light is grey, so the sheet is the only colour.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Eyes {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Forgets the last frame's eyes and moves the pulse on. Called before the feed.
    void update(float seconds);
    // One eye, in world metres, for this frame only, at RenderEye's fSize.
    void feed(const float at[3], float size = 1.0f);
    void gather(gfx::Effects& effects) const;

private:
    struct Eye {
        float position[3];
        float size = 1.0f;
    };
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    std::vector<Eye> eyes_;
    // MU's WorldTime, in seconds; only its sine is read.
    float clock_ = 0.0f;
    bool open_ = false;
};

}  // namespace mu::game
