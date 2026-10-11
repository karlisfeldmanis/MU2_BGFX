// Aida's skull ghosts: MU's RenderAidaObjectVisual arm for type 70 (GMAida.cpp:243-252),
// Object71, five hidden skulls (tiles 91,174 to 242,46), cooked as world anchors of kind 22
// (tools/cook_anchors.py) with the box's Scale in the emitter's `high`.
//
// In the first 30 ms of every 1024 ms of the clock, one reference frame in five, a box raises
// a MODEL_GHOST -- about one a skull every seven seconds. Its life (ZzzEffect.cpp:572-588,
// MoveHandlers.cpp:476-505, and MoveEffect's tail, ZzzEffect.cpp:8567-8575):
//
//   created  LifeTime 1000; the box's place moved -100..99 units along each axis; its Angle
//            the box's, yawed 0-359 degrees; Direction (0, -5.0..-4.3, 0); Scale the box's
//            + 0-0.1; Velocity, Gravity and Distance 1.
//   a frame  one in 40, Velocity flips and Gravity becomes Velocity x 5-7; one in 40, it flips
//            again and Distance becomes Velocity x 1-3. s = sin(WorldTime % 10000 x 0.0001):
//            yaw += s x Gravity, roll (MU's Angle[0]) += s x Distance / 3, degrees. Its body
//            light is (0.1, 0.1, 0.25). It drops a BITMAP_LIGHT SubType 9 where it is, and
//            moves by Direction turned by its Angle (MoveParticle). Once 500 units from where
//            it was made, it has 50 frames left.
//   the mote (ZzzEffectParticle.cpp:3166-3169, 8082-8101): Effect/flare01, 64 texels, added,
//            at the ghost's light and Scale; LifeTime 40 x Scale; Scale down 0.011 and light
//            down by 1.05 a frame.
//
// The ghost itself: MuMain loads no model for MODEL_GHOST in Aida (MapManager.cpp:328-334),
// so its RenderObject draws nothing and only the motes show. Ours, marked: a soft head on Nova's
// round puff at the body light, so it reads as a blurred soul (the user, 2026-10-11: 'ghost
// sphere has to be much more blurry'; Object34's skullghost.bmd, tried first, read as a hard
// ball even on a blurred sheet). MU's numbers are per reference frame, 25 a second; they step
// on a 25 Hz clock here and the head is drawn eased between its last two steps.
// Nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class SkullGhosts {
public:
    // Opens where the world's manifest names `skull_ghosts`: the showing's `light` (MU's
    // BITMAP_LIGHT) and `nova_puff`, and the cooked town's anchors of kind SkullGhost.
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    // Says how many ghosts rose, which is what a headless run reads to know it worked.
    void shutdown();
    bool isOpen() const { return open_; }

    // One frame round `near`, the point the camera follows, in metres.
    void update(float seconds, const float near[3]);
    void gather(gfx::Effects& effects) const;

private:
    struct Ghost {
        float at[3] = {0, 0, 0};     // metres, ours
        float was[3] = {0, 0, 0};    // at the step before, for easing
        float start[3] = {0, 0, 0};
        float angle[3] = {0, 0, 0};  // MU's Angle, degrees: roll, pitch, yaw
        float speed = 0.0f;          // MU's Direction[1], units a frame (negative)
        float velocity = 1.0f, gravity = 1.0f, distance = 1.0f;
        float scale = 1.0f;
        int life = 1000;             // reference frames
        bool cut = false;            // MU's Kind: 500 units out, 50 frames left
    };
    struct Mote {
        float at[3] = {0, 0, 0};
        float scale = 1.0f;
        float light[3] = {0, 0, 0};
        float life = 0.0f;           // reference frames
    };
    struct Box {
        float at[3] = {0, 0, 0};
        float scale = 1.0f;
        bool near = false;
    };
    void step();
    void raise(const Box& box);
    float unit();
    int roll(int n) { return int(unit() * float(n)); }

    bool open_ = false;
    std::vector<Box> boxes_;
    std::vector<Ghost> ghosts_;
    std::vector<Mote> motes_;
    uint32_t raised_ = 0;
    float clock_ = 0.0f;   // seconds towards the next reference frame
    uint32_t worldMs_ = 0; // MU's WorldTime
    uint32_t seed_ = 0x5C011u;
    bgfx::TextureHandle moteSheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle haloSheet_ = BGFX_INVALID_HANDLE;  // ours: Nova's soft puff
};

}  // namespace mu::game
