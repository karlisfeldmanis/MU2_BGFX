// Kanturu's tank arcs and plant spit: two of MU's RenderKanturu1stObjectVisual arms
// (GM_Kanturu_1st.cpp:214-247).
//
//   Arcs  Object93, the blue tanks (type 92), 2: on a roll of one in 30 each rendered frame, a
//         BITMAP_JOINT_THUNDER sub-type 18 at scale 50 from bone 1 to bone 2 -- a jagged bolt
//         fifty units wide across the tank, kinked +-32 units (ZzzEffectJoint.cpp:4927-4931).
//         Drawn as Thunder's wide fork, which has no spark, light or smoke (fx/thunder.h).
//   Spit  Object99, the carnivorous plants (type 98), 46: one frame in three a
//         BITMAP_TWINTAIL_WATER sub-type 2 (Effect/water, 32 texels) at bone 1, light
//         (0.5, 0.6, 0.1), added: born within 20 units across and 40 up, Scale 1.0-1.62
//         shrinking 0.026 a frame and gone at nothing, rising 2-3 units a frame, its light
//         dimming 1.02 times a frame (ZzzEffectParticle.cpp:1133-1139, 5258-5266, 9006-9010).
//
//   Tower The Relics' jade towers, Object09 (type 8), 18: at bone 4 MU draws a BITMAP_SPARK+1
//         (Spark03) of Scale 10 lit fLumi = (sin(WorldTime * 0.001) + 1) / 2 in white, added
//         -- not drawn here (the user, 2026-10-24: "remove that blue sphere") -- and one
//         frame in 20 a BITMAP_JOINT_THUNDER sub-type 18 at scale 40 between two points thrown
//         round it: from 50-150 units west, 0-50 north and 10 up, to 0-80 east, 0-50 south
//         and 10 up (RenderKanturu2ndObjectVisual, GM_Kanturu_2nd.cpp:350-374). And each frame
//         a BITMAP_ENERGY sub-type 0 (Effect/Thunder01, 64 texels) at Scale 1.5 * 0.6-1.3,
//         turned at random and spinning 20 degrees a frame, lit fLumi, living MU's default 2
//         frames (ZzzEffectParticle.cpp:721-724, 4937-4939): about two crackles over the spark,
//         re-rolled every reference frame.
//
//   Bolts The Relics' hidden lightning boxes, the world's anchors of kinds 30-32
//         (MoveKanturu2ndObject, GM_Kanturu_2nd.cpp:489-520): each a BITMAP_JOINT_THUNDER+1
//         to the box, at scale 30-40, living 20 frames (ZzzEffectJoint.cpp:1436-1470). Type 54
//         (18, in the void), one frame in 20, from 11 m above it, 0-5 units off; type 56 (13, on
//         the walls), one in 10, from 50 units west; type 65 (2), one in 10, from 3.5 m north.
//         Drawn as Thunder's wide fork.
//   Flash The Relics' Fenrir thunder boxes, kind 33 (type 55, 32): one frame in 5 a
//         MODEL_FENRIR_THUNDER sub-type 1 (Effect/lightning_type01.bmd) within 20 units, turned at
//         random, Scale 0.3-0.5, born at full alpha and fading 0.3 a frame (gone in 4), spinning,
//         lit (0.8, 0.8, 1.0), with a BITMAP_LIGHT of Scale 2 lit 0.3 less on its first frame
//         (ZzzEffect.cpp:4314-4336, MoveHandlers.cpp:6679-6710). Ours, marked: the lightning
//         model is drawn as a Thunder01 crackle of its size, the engine drawing no effect models.
//   Spray The Relics' water boxes, kind 34 (type 53, Object54, 34 on the walls 1.8-2.7 m up): one
//         frame in three the plants' particle at sub-type 1, light (0.4, 0.6, 0.7): born within
//         40 units across and 80 up, unscaled, Scale 0.5-0.81 shrinking 0.013 a frame, living
//         20-29 frames (GM_Kanturu_2nd.cpp:480-487, ZzzEffectParticle.cpp:1124-1130, 5255-5266).
//
// The points are the recipes' `emitters` (kinds "arc_from", "arc_to", "spit", "tower"), the rigs' rest
// positions in the model's frame, laid once per placement turned and scaled as MU's bones are.
// Ours, marked: the plants sway (E2c) and the spit leaves from bone 1 at rest, which is the
// middle of the plant's foot, so it does not follow the sway. MU's roll of one in 30 is per
// rendered frame; here it is per reference frame, 25 a second. Only points within kReach of
// the camera's point throw. Nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/cooked.h"
#include "content/texture.h"
#include "game/fx/thunder.h"
#include "gfx/effects.h"

namespace mu::game {

class RuinEffects {
public:
    // Opens where the world's show has ruin_effects; takes the showing's `plant_spit` and
    // Thunder's sheets.
    void open(const std::string& assetDir, const std::string& world, const content::CookedTown& town,
              content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }

    void update(float seconds, const float near[3]);
    void gather(gfx::Effects& effects) const;

private:
    struct Tank {
        float from[3] = {0, 0, 0}, to[3] = {0, 0, 0};
    };
    struct Plant {
        float at[3] = {0, 0, 0};
        float colour[3] = {0.5f, 0.6f, 0.1f};
        float scale = 1.0f;  // the placement's, which MU's bones carry
        float owed = 0.0f;
        bool spray = false;  // a Relics water box (sub-type 1), not a plant
    };
    struct Blob {
        float at[3] = {0, 0, 0};
        float rise = 0.0f;   // metres a second
        float scale = 1.0f;  // MU's Scale, shrinking to nothing
        float shrink = 0.0f;  // Scale a second
        float age = 0.0f, life = 1.0f;
        float colour[3] = {0, 0, 0};
    };
    float unit();

    struct Tower {
        float at[3] = {0, 0, 0};
    };
    struct Flash {
        float at[3] = {0, 0, 0};
        float age = 0.0f;  // seconds
        float size = 0.4f, spin = 0.0f;
    };
    struct Bolt {
        float at[3] = {0, 0, 0};
        content::EmitterKind kind = content::EmitterKind::VoidStrike;
    };
    std::vector<Tank> tanks_;
    std::vector<Plant> plants_;
    std::vector<Tower> towers_;
    std::vector<Bolt> bolts_;
    std::vector<Flash> flashes_;
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;  // Effect/flare01, the showing's `flare`
    float clock_ = 0.0f;  // seconds, for the towers' sparks
    bgfx::TextureHandle energy_ = BGFX_INVALID_HANDLE;  // Effect/Thunder01, the showing's `energy`
    std::vector<Blob> blobs_;
    Thunder thunder_;
    bool thunderOpen_ = false;
    float near_[3] = {0, 0, 0};
    uint32_t seed_ = 0x4A17u;
    bool open_ = false;
    bgfx::TextureHandle spit_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
