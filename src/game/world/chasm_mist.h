// Kanturu's mist: MU's RenderKanturu1stObjectVisual arms for types 62, 107 and 108
// (GM_Kanturu_1st.cpp:165-172, 273-292), three kinds of hidden box that throw BITMAP_CLOUD
// (Effect/clouds.jpg, 256 texels, drawn added) every frame they are in view. Aida turns its own
// mist boxes off with HiddenMesh -2 (GMAida.cpp:85); Kanturu never does, so these run. The port
// doc had them dead (the user, 2026-10-22: "this place seems off, do here has be black void?",
// "mayeb we are missing something", then "yes").
//
//   Rise   Object63, 60, 58 of them over the chasm: SubType 20, light 0.04. Born within 2.5 m of
//          the box and up to 20 units under it, Scale 1.8-2.0 times the box's, alpha 0.1; for
//          its first 150 frames of 300 it climbs a unit a frame while its alpha rises 0.04 a
//          frame to 1, then sinks half a unit a frame while it falls 0.025 a frame towards 0.1.
//          Drawn at light times alpha (ZzzEffectParticle.cpp:3026-3041, 7831-7845, 9190-9196).
//   Hover  Object108, 77, over the plateau's pits and rim and 30 over void: SubType 2, light
//          0.06. Born within 2.5 m, 20-40 units over the box, Scale 1.8-2.0, turning at the box's
//          Scale plus up to 0.3; it bobs 20 units on a sine of WorldTime / 5000 and turns
//          WorldTime * -0.02 * its turning degrees, and lives as long as its box is seen, then 50
//          frames (:3103-3118, 7912-7936).
//   Shade  Object109, 11: the same arm, SubType 7, light 0.2, drawn MINUS (dst * (1 - src)),
//          which darkens; it does not turn (:9171-9177).
//
// MU throws 20 a frame from every box it sees into a pool of 3,000 particles (_define.h:460)
// and stops when the pool is full. Ours throws one in kRatio of them, each kRatio times as bright
// (the minus kind: as dark as kRatio of MU's laid over each other), into a pool of kMost: the
// same sum of light in far fewer, larger draws. "Seen" is within kReach of the camera's point.
// Ours, after the user's 'vissible lines' and 'vissible' (2026-10-23): a puff 10 m across
// straddles the trench's lip and was cut on it. A rising puff is born only where the void lies
// under half its width, a few tries at MU's spread, and every puff is drawn as faint as little of
// it lies over the void, so the ones that do reach the lip thin out before it.
// Nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/ground.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class ChasmMist {
public:
    // Opens where the world's show asks for `chasm_mist`: the boxes from the world's placements
    // (assetDir/world/<w>/<w>.json), the sheet the showing's `cloud`.
    void open(const std::string& assetDir, const std::string& world, const content::Ground& ground,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    // One frame round `near`, the point the camera follows, in metres.
    void update(float seconds, const float near[3]);
    void gather(gfx::Effects& effects) const;

private:
    enum class Kind : uint8_t { Rise, Hover, Shade };
    struct Puff {
        Kind kind = Kind::Rise;
        uint16_t box = 0;
        float at[3] = {0, 0, 0};
        float bornY = 0.0f;     // Hover and Shade bob about it
        float age = 0.0f;       // reference frames
        float left = 0.0f;      // Rise: frames of its 300; the others: frames since its box was seen
        float alpha = 0.1f;     // Rise's
        float scale = 1.0f;     // MU's Scale
        float turning = 0.0f;   // Hover's
        float phase = 0.0f;     // milliseconds, MU's Gravity
        float edge = 1.0f;      // ours: how much of it lies over the void, squared (overVoid)
    };
    struct Box {
        Kind kind = Kind::Rise;
        float at[3] = {0, 0, 0};
        float scale = 1.0f;
        float owed = 0.0f;  // seconds towards its next reference frame
        bool seen = false;
    };
    void throwPuff(uint16_t index);
    bool voidAt(float x, float z) const;
    float overVoid(float x, float z, float radius) const;
    float unit();

    bool open_ = false;
    const content::Ground* ground_ = nullptr;
    std::vector<Box> boxes_;
    std::vector<Puff> puffs_;
    float clock_ = 0.0f;  // milliseconds, MU's WorldTime
    float strength_ = 1.0f;  // the show's `strength`: 1 is MU's full pool of light
    uint32_t seed_ = 0xC4A5Eu;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
