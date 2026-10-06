// Tarkan's hidden emitters: MU's RenderObjectVisual arm for WD_8TARKAN (ZzzObject.cpp:2994-3065),
// four kinds of box MU never draws, cooked as world anchors (tools/cook.py ANCHOR_KINDS_BY_WORLD):
//
//   SandFall   Object71, 3: one frame in five a BITMAP_SMOKE SubType 7, sand-coloured
//              (0.725, 0.572, 0.333), that falls faster as it goes and swells, 30 frames.
//   SteamVent  Object77, 19: every frame of the last half second of each five, all in step
//              (WorldTime % 5000 > 4500), a SubType 4 puff, dun (120, 100.7, 80) / 255, that
//              climbs faster as it goes, swells and fades over 16 frames.
//   Geyser     Object84, 10: for half a second of every ten, a SubType 8 plume shooting up,
//              white, 24 frames; and one frame in three a half-size SubType 4 puff within 64
//              units and a thrown stone (MODEL_STONE1/2, here the meteor's). MU staggers them by
//              the box's turn (inter = Angle[2] * 10 ms); an anchor carries no turn, so the
//              stagger is a hash of where it stands, over the same 0-3.6 s.
//   DustCloud  Object61, 82: on its first drawn frame, 20 BITMAP_SMOKE SubType 6 scattered
//              +-1 m and 20-40 units up, then the box hidden. SubType 6 resets its LifeTime to 10
//              every frame (ZzzEffectParticle.cpp:5412-5418), so the puffs never die: a standing
//              cloud, each bobbing sin((WorldTime + g) / 5000) * 20 units and swelling
//              sin(((g + WorldTime) % 1800) * 0.1 deg) * 0.5 + 1.8, lit (0.36, 0.3, 0.24).
//   GlowSprite Object64, 18: a BITMAP_IMPACT (Object9/Impack03) at bone 2, cyan
//              (L / 1.7, L, L) at Scale 1.5 L, L = sin((WorldTime + Angle[2] * 5) * 0.002) * 0.3
//              + 0.7. Bone 2 stands 0.34 m over the box's origin.
//
// MU's particle rates are per reference frame, 25 a second. Ours: only the emitters within
// kReach of the camera's point throw anything, the smoke and the plumes are drawn at
// kPuffLevel of MU's light (the user likes effects faint), and the glow's phase is hashed.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class DesertVents {
public:
    // Opens in Tarkan: the showing's `smoke` (MU's BITMAP_SMOKE) and `impact`, and the cooked
    // town's world anchors of the four kinds.
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // One frame round `near`, the point the camera follows, in metres. `stone` is asked to throw
    // one stone up from a geyser at that point (its height is the ground there).
    void update(float seconds, const float near[3], const std::function<void(const float*)>& stone);
    void gather(gfx::Effects& effects) const;

private:
    enum class Kind : uint8_t { Sand, Steam, Plume };
    struct Puff {
        Kind kind = Kind::Steam;
        float at[3] = {0, 0, 0};
        float speed = 0.0f;  // metres a second, up (negative down)
        float age = 0.0f, life = 1.0f;
        float scale = 1.0f;  // MU's Scale
        float spin = 0.0f;
        float half = 1.0f;   // a geyser's side puff is half size
    };
    struct Mote {  // one of a dust cloud's standing puffs
        float at[3] = {0, 0, 0};
        float g = 0.0f;  // MU's Gravity, rand() % 1000, its own phase in ms
        float spin = 0.0f;
    };
    std::vector<Mote> motes_;
    struct Vent {
        content::EmitterKind kind;
        float at[3] = {0, 0, 0};
        float owed = 0.0f;   // seconds towards its next frame of throwing
        float phase = 0.0f;  // a geyser's stagger and a glow's, seconds
        int frames = 0;
    };
    void throwPuff(Kind kind, const float at[3], float scaleBy);
    float unit();

    bool open_ = false;
    std::vector<Vent> vents_;
    std::vector<Puff> puffs_;
    float clock_ = 0.0f;  // seconds since the world opened: MU's WorldTime / 1000
    float near_[3] = {0, 0, 0};  // the camera's point, for the dust clouds' reach
    uint32_t seed_ = 0x7A2CA9u;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle impact_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
