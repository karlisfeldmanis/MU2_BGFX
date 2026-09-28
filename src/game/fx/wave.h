// The wizard's Power Wave: a curtain of light that sweeps out along the ground and keeps going.
//
// MU's whole spell is `CreateEffect(MODEL_MAGIC2, o->Position, Angle, o->Light)` and
// `PlayBuffer(SOUND_MAGIC)` (ZzzCharacter.cpp:5045). MU2's `client/core/Wave.cs` read the rest
// and is ported here; `source/effects/wave/Magic02.json` carries every number with its line.
//
// **The one spell that does not arrive.** The bolt and the fireball end on their target; this
// one's mover never calls `CheckTargetRange`, so it runs its twenty frames out -- twelve tiles,
// twice the spell's reach -- sweeping through the body and away. The blow is the realm's and
// lands when the wave passes (`Realm::loose`, at the row's fifteen tiles a second); nothing here
// decides it.
//
// What is MU's: Magic02.bmd at nine tenths, one additive mesh standing on the ground at his feet;
// sixty units a reference frame along the heading, flat; the sheet streaming along it
// (`BlendMeshTexCoordU = -LifeTime * 0.2`), the only effect here that scrolls; its brightness
// `LifeTime * 0.1`, saturating, so full for half the flight and fading over the rest; four puffs
// of smoke01 a frame in a forty-five degree cone, tinted faintly blue, doubling and stopping hard
// (`Velocity *= 0.4` a frame); and a blue light on the ground three tiles wide.
//
// Ours: it leaves from where every spell leaves (Play::castFrom), on the ground under that point.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Wave {
public:
    // Magic02.obj and its sheet out of the effects folder, and smoke01 off the cooked showing.
    // False only when the curtain itself could not be had.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // A wave let go from `from` -- the ground it stands on -- toward `to`. Only the heading is
    // taken from `to`; it flies flat and does not stop there.
    void cast(const float from[3], const float to[3]);

    void update(float seconds);
    void gather(gfx::Effects& effects) const;
    // The blue each wave throws on the ground. Up to `max`.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

    uint32_t flying() const;
    uint32_t refused() const { return refused_; }

private:
    struct Sweep {
        bool alive = false;
        float at[3];
        float along[3];   // unit, flat
        float left;       // reference frames
        float blown;      // metres since the last puff
        float glow;       // this frame's 0.7-1.0 roll, for the ground light
    };
    struct Puff {
        bool alive = false;
        float at[3];
        float velocity[3];  // metres a reference frame
        float size;         // metres across
        float spin;
        float left;         // reference frames
    };

    // ---- MU's numbers (Magic02.json) ---------------------------------------------------------
    static constexpr float kReference = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kScale = 0.9f;          // CreateEffect's default, no Scale set
    static constexpr float kSpeed = 60.0f;         // units a reference frame
    static constexpr float kFrames = 20.0f;        // LifeTime
    static constexpr float kScrollsBy = 0.2f;      // BlendMeshTexCoordU = -LifeTime * 0.2
    static constexpr float kLightsBy = 0.1f;       // BlendMeshLight = LifeTime * 0.1
    static constexpr float kDaylight[3] = {1.0f, 0.95f, 0.9f};
    static constexpr float kGlow[3] = {0.3f, 0.6f, 1.0f};
    static constexpr float kGlowTiles = 3.0f;
    static constexpr float kDimmestGlow = 0.7f, kBrightestGlow = 1.0f;
    static constexpr float kFadesUnder = 5.0f, kFadeStep = 0.2f;
    // The smoke: four a frame, BITMAP_SMOKE subtype 3.
    static constexpr int kPuffs = 4;
    static constexpr float kPuffSpacing = kSpeed / float(kPuffs);  // units
    static constexpr float kPuffFrames = 10.0f;
    static constexpr float kSmallestPuff = 0.80f, kLargestPuff = 1.11f;
    static constexpr float kPuffGrows = 0.1f;      // of the sheet, a frame
    static constexpr float kPuffSpread = 45.0f;    // degrees off the heading, both ways
    static constexpr float kSlowestPuff = 40.0f, kFastestPuff = 47.0f;  // units a frame
    static constexpr float kPuffDrag = 0.4f;       // what a frame leaves of the velocity
    static constexpr float kSmokeWidth = 0.64f;    // smoke01 is 64 pixels
    static constexpr float kSoot[3] = {0.8f, 0.8f, 1.0f};
    static constexpr float kSootFrames = 8.0f;     // Light = LifeTime / 8

    // Ours, on the bench (*"make it little bit more blurry"*): the curtain drawn twice more,
    // each copy wider and taller and dimmer, so the sheet's hard white spikes soften into a haze;
    // and its brightness held under white, where MU's saturates at 2.0 for half the flight.
    static constexpr int kGhosts = 2;
    static constexpr float kGhostGrow[kGhosts] = {1.12f, 1.26f};
    static constexpr float kGhostLight[kGhosts] = {0.35f, 0.18f};
    static constexpr float kBrightest = 0.8f;

    static constexpr int kMaxWaves = 8;
    static constexpr int kMaxPuffs = 200;

    std::vector<EffectCorner> curtain_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    Sweep waves_[kMaxWaves] = {};
    Puff puffs_[kMaxPuffs] = {};
    uint32_t refused_ = 0;

    // The drawing's own dice, never the sim's.
    uint32_t dice_ = 0x3A7E5EEDu;
    float unit();
    float between(float a, float b) { return a + unit() * (b - a); }
    void puff(const Sweep& wave);
};

}  // namespace mu::game
