// A Death Tree's second blow: the roots it throws up and the green smoke it stamps out.
//
// MuMain, on MONSTER_DEATH_TREE's ATTACK2:
//
//   AttackEffectAidaMonster (GMAida.cpp:1475-1484), CheckAttackTime(10), so once a blow:
//     CreateEffect(MODEL_TREE_ATTACK, pObject->Position, pObject->Angle, (1, 1, 1))
//   which is Object34's tree_eff.bmd (MapManager.cpp:331-332): made 20 units over the ground at
//   Scale 0.05, LifeTime 20, unlit (ZzzEffect.cpp:590-595); a frame, Scale up 0.1 to 1, and
//   from there Alpha down 0.07 (MoveHandlers.cpp:509-524); drawn as a model (:8751-8753).
//
//   RenderAidaMonsterVisual (:988-993), every frame of the clip:
//     CreateParticleFpsChecked(BITMAP_SMOKE, the Footsteps bone + (0, -100, 20), Angle,
//                              (0.5, 0.7, 0.5), 26)
//   smoke01, LifeTime 25, Scale 0.64-1.27, turned at random (ZzzEffectParticle.cpp:1366-1370);
//   a frame its light times LifeTime / 20 and Scale up 0.15 (:5516-5520).
//
// So a burst of bark roots with teal tips at its feet for 0.8 s, and green smoke rolling out a
// metre in front of it while the blow lasts. MU's numbers are per reference frame, 25 a second.
// Fed by Play::shade from the tree's own clip; nothing here reaches the sim.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class Figure;

class TreeRoots {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // One Death Tree this frame: whether its second blow is the clip playing, where it stands
    // and which way it faces (radians, the body's yaw), how far it has faded in or out, and the
    // frame's seconds.
    void watch(uint32_t id, bool blowing, const Figure& figure, float yaw, float fade,
               float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Burst {
        float at[3] = {0, 0, 0};
        float yaw = 0.0f;
        float age = 0.0f;  // reference frames
    };
    struct Puff {
        float at[3] = {0, 0, 0};
        float scale = 1.0f, spin = 0.0f, light = 1.0f;
        float age = 0.0f;
    };
    struct Tree {
        bool blowing = false;
        float owed = 0.0f;
    };
    float unit();

    std::vector<EffectCorner> roots_;
    std::vector<Burst> bursts_;
    std::vector<Puff> puffs_;
    std::unordered_map<uint32_t, Tree> trees_;
    uint32_t seed_ = 0x7EE5u;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
};

}  // namespace mu::game
