// A Blue Golem's water: the drips off its hands and head, and its first blow's drain.
//
// MuMain, RenderAidaMonsterVisual's MODEL_GOLDEN_STONE_GOLEM (GMAida.cpp:797-870), the Blue
// Golem's body (MONSTER_BLUE_GOLEM, GMAida.cpp:355):
//
//   Every frame, on Monster101_L_Arm, _R_Arm and _Head (bones 12, 20, 6: Bip01 L Hand, R Hand,
//   Head), CreateParticleFpsChecked(BITMAP_WATERFALL_2, ..., 1): waterFall2, LifeTime 50, Light
//   (0, 0.4, 0.4), Scale 0.6-1.1, set off up to 10 units aside and 20 up or down, falling 3-5
//   units a frame (ZzzEffectParticle.cpp:3499-3537). A frame: moved by its velocity, which eases
//   0.1 upward, wanders up to 1 unit aside, Scale up 0.03, Light / 1.05 and / 1.1 more in its
//   last ten (:3883, 8476-8507).
//
//   ATTACK1, from AnimationFrame 4: every frame BITMAP_SMOKE sub 25 at 30 units along the left
//   hand, white, standing still, LifeTime 25, Light LifeTime / 20, Scale 0.64-1.27 up 0.09
//   (:1359-1363, 5507-5510); and while AnimationFrame is 5 to 5.5, BITMAP_SHOCK_WAVE sub 3 there,
//   Light 5 / 1.8 a frame, Scale 0.5 up 0.3, LifeTime 7 (:3729-3732, 8692-8700).
//
//   ATTACK1 from AnimationFrame 6.9, with a target: every frame CreateJoint(BITMAP_JOINT_ENERGY
//   (JointLaser01), the target + 100 up, aimed at the golem, sub 16, Scale 20): eight tails,
//   LifeTime 120; for its first twenty frames it turns and drifts 3-7 units a frame to one side
//   and rises 8; then it hums home to the golem + 120 up at a speed that climbs 7 a frame to
//   40, and is gone inside 35 units of it; every frame a BITMAP_SMOKE sub 31 in (0.4, 0.8, 1) at
//   0.8, shrinking 0.05 and its light / 1.1 a frame for 15 (ZzzEffectJoint.cpp:210-243,
//   3237-3393; ZzzEffectParticle.cpp:1400-1404, 5551-5557). A life drawn back to it.
//
// Left out: the ground lights (the joint's AddTerrainLight, and its landing's BITMAP_LIGHTNING+1
// sub 1, which is Scale 0 and only a ground light) and SOUND_GET_ENERGY, as Charon's wisps leave
// it out. MoveHumming's turn is a straight line home, as theirs is. MU's numbers are per
// reference frame, 25 a second, and its units are centimetres. Fed by Play::shade from the
// golem's own clip; nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Figure;

class GolemWater {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One golem this frame: whether its first blow is the clip, how far into it (MU's
    // AnimationFrame, keyOf), where its target stands now (its feet, world metres) or null, how
    // far it has faded in or out, and the frame's seconds.
    void watch(uint32_t id, const Figure& figure, bool attack1, float key, const float* target,
               float fade, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Mote {
        float at[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float scale = 1.0f, spin = 0.0f, light = 1.0f, age = 0.0f, life = 1.0f;
        enum class Kind : uint8_t { Drip, Smoke, Wave, Trail } kind = Kind::Drip;
    };
    struct Joint {
        float at[3] = {0, 0, 0};
        float home[3] = {0, 0, 0};   // the golem + 120 up, followed while it lives
        float side[2] = {0, 0};      // its first twenty frames' drift, metres a frame
        float speed = 10.0f;         // MU units a frame
        float age = 0.0f;
        float fade = 1.0f;
        uint32_t golem = 0;
        float tail[8][3] = {};
        int tails = 0;
        bool landed = false;
    };
    struct Golem {
        int bones[4] = {-1, -1, -1, -1};  // left hand, right hand, head, footsteps
        bool found = false;
        float dripOwed = 0.0f, blowOwed = 0.0f;
        float home[3] = {0, 0, 0};
        bool seen = false;
    };

    float unit();
    void mote(Mote::Kind kind, const float at[3], float fade);

    std::vector<Mote> motes_;
    std::vector<Joint> joints_;
    std::unordered_map<uint32_t, Golem> golems_;
    float tailStep_ = 0.0f;
    uint32_t seed_ = 0x51ed2705u;
    bgfx::TextureHandle drip_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle wave_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle laser_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
