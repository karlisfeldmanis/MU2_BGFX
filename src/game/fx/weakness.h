// Weakness: the Summoner's curse, MU's AT_SKILL_ALICE_WEAKNESS (221), as its let-go draws it under
// the caster (ZzzCharacter.cpp:4725-4740), and the red the curse sheds off a monster it holds
// (eDeBuff_AttackDown, ZzzCharacter.cpp:10902-10915). Here the Ancient rune Weakness's
// (sim/items.h kWeakness...), said as Loosed with skill 221 (sim::skill::kWeakness) on his tile.
//
// **What is MU's, and kept.** On the ground, five BITMAP_MAGIC_ZIN circles (Effect/mzine_typer2)
// laid on the land as RenderTerrainAlphaBitmap lays them, a scale being tiles across, added:
// subtype 1 at 7 in (2.0, 0.1, 0.1), 40 frames, its alpha rising 0.06 a frame to 0.7 and lit at
// alpha / 2.5; subtype 0 at 2 in (2.0, 0.4, 0.3), 50 frames, lit at alpha * 2; subtype 2 at 1.0,
// 0.2 and 0.1 in the same, 30 frames, each growing 0.1 a frame to 3.5 (ZzzEffect.cpp:1240-1250,
// :9960-9976; MoveHandlers.cpp:1487-1513); each fading 0.05 a frame (subtype 1 0.03) over its last
// twenty. Round him, Suhwanzin2, 22 and 222 at 0.6, 40 frames, their monolines added at a
// BlendMeshLight climbing 0.05 a frame to 0.5 and falling 0.03 a frame over the last twenty, 2 and
// 222 turning +3 degrees a frame and 22 -3 (MoveHandlers.cpp:1134-1165). BITMAP_SHINY + 6 for 24
// frames, each frame a star (Effect/shiny05) thrown up to 2.5 m aside and 0.5 to 1.5 m up, which
// sinks 1.3 units a frame, turns 5 degrees a frame, shrinks 0.02 a frame from 0.5 + (0..0.2), its
// light flickering, drawn with a BITMAP_LIGHT glow its size (ZzzEffect.cpp:6790-6797,
// ZzzEffectParticle.cpp:2705-2714, :7352-7364, :9234-9236). BITMAP_PIN_LIGHT for 40 frames, one
// in two frames a streak (Effect/pin_lights) thrown the same, which falls 10 units a frame for 30
// frames and shrinks 0.02 a frame from 1 + (0..0.4) (MoveHandlers.cpp:1516-1534,
// ZzzEffectParticle.cpp:2728-2750, :7373-7395). All in (1.4, 0.2, 0.2). On a cursed monster, one
// frame in two each, a star at 0.5 and a streak at 1 off a bone drawn at random
// (MoveHandlers.cpp:1536-1556). Every colour clamped to one a channel, as MU's glColor clamps.
//
// **Ours.** A particle is this engine's sprite at MU's size (a sheet's pixels times its scale,
// in MU units), not MU's particle mover. The mark is thrown off the bone itself, not 100 units
// along it. The hand sprites MU draws while the Summoner casts are not drawn: the rune casts no
// spell of its own and he has no Summoner's casting pose.
//
// Presentation only: the realm laid the field and cursed what it reached on the tick it said
// so. Pools are sized once and a frame allocates nothing.
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

class Weakness {
public:
    // The three walls out of effects/weakness and the circle, star, streak and glow sheets off
    // the cooked showing. False when the walls or the circle are missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // The let-go at `feet` (world metres, the ground there).
    void cast(const float feet[3]);
    // The curse on a body, at a point on one of its bones: one frame's worth, called once a
    // reference frame while it lasts -- a star by a coin and a streak by a coin.
    void mark(const float at[3]);

    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Circle {
        bool alive = false;
        int kind = 0;           // MU's subtype, 0, 1 or 2
        float at[3] = {};
        float scale = 1.0f;     // tiles across
        float alpha = 1.0f;
        float left = 0.0f;      // reference frames
        float light[3] = {};
    };
    struct Wall {
        bool alive = false;
        int model = 0;          // 0, 1, 2: Suhwanzin2, 22, 222
        float at[3] = {};
        float turn = 0.0f;      // degrees
        float glow = 0.0f;      // BlendMeshLight
        float left = 0.0f;
    };
    struct Spring {
        bool alive = false;
        bool pin = false;       // BITMAP_PIN_LIGHT's, else BITMAP_SHINY + 6's
        float at[3] = {};
        float left = 0.0f;
        float owed = 0.0f;
    };
    struct Mote {
        bool alive = false;
        bool pin = false;
        float at[3] = {};
        float scale = 0.5f;
        float spin = 0.0f;      // radians
        float light[3] = {};
        float left = 0.0f;
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr int kCasts = 8;
    static constexpr int kCircles = 5 * kCasts;
    static constexpr int kWalls = 3 * kCasts;
    static constexpr int kSprings = 2 * kCasts;
    static constexpr int kMotes = 768;
    static constexpr float kRed[3] = {1.4f, 0.2f, 0.2f};

    void throwMote(bool pin, const float at[3], float scale);
    void gatherCircle(gfx::Effects& effects, const Circle& one) const;
    float unit();

    const content::Ground* ground_ = nullptr;
    std::vector<EffectCorner> walls_[3];
    bgfx::TextureHandle lines_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle circle_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle pin_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glow_ = BGFX_INVALID_HANDLE;
    Circle circles_[kCircles] = {};
    Wall wallsLive_[kWalls] = {};
    Spring springs_[kSprings] = {};
    Mote motes_[kMotes] = {};
    uint32_t dice_ = 0x5EA4E55u;
};

}  // namespace mu::game
