// Pollution: the Summoner's Book of Lagle, MU's AT_SKILL_SUMMON_POLLUTION (225) -- its casting circle
// round the caster (CSummonSystem::CreateCastingEffect, SummonSystem.cpp:191-230) and Lagle's
// ghosts let loose on the target (CreateSummonObject, :165-185). Here the Ancient rune Pollution's
// (sim/items.h kPollution...), said as Loosed with skill 225 (sim::skill::kPollution).
//
// **What is MU's, and kept.** Round the caster: BITMAP_MAGIC subtype 10, BITMAP_LIGHT twelve tiles
// across laid on the land and SUBTRACTED, its alpha rising 0.05 a frame and falling 0.03 over the
// last twenty of 44, drawn up to five times over itself as its life runs out (ZzzEffect.cpp:9755-
// 9767); subtype 9, two empact01 circles at 2.4 and 2.88 tiles in (0.6, 0.6, 0.9), turning -8 and
// +4 degrees a frame, 40 frames; Suhwanzin1, 11 and 111 (testmsop) in (0.6, 0.3, 0.9), and 2, 22
// and 222 (monolines, fx/weakness's) in (0.8, 0.1, 0.6), at 0.9, turning +-3 degrees a frame at a
// BlendMeshLight climbing to 0.5, 40 frames; Suhwanzin4 in (0.9, 0.1, 1.0) opening 0.6 a frame
// for 25. On the target: the unseen spring (MODEL_SUMMONER_SUMMON_LAGUL subtype 0, 160 frames, one
// frame in three at the book's top tier) throwing a smoke 2.5 m about it and a violet cloud
// (0.6, 0.1, 1.0) 2 m about it; two wide BITMAP_JOINT_SPIRIT joints (Scale 100) and two thin
// (20) at subtype 24 -- the +7 book's count, kJointsACast -- born 1.5 m about it at headings 0 and 90, flying ten units a frame,
// turned at most five degrees a frame toward a point 80 units over it, with MU's wobble, their
// 40-point tails of JointSpirit01 added in (0.7, 0.7, 0.9) as their light rises over 16 frames and
// falls over the last 30 of 160 (ZzzEffectJoint.cpp:896-914, :4130-4163); on each wide one a Lagul
// at 100 / 70, facing where it flies, its alpha (160 - life) / 20 as it comes and life / 40 over
// the last 30, playing its one action at half a key a frame, six of its bones lit with a blue
// (0.2, 0.3, 1.0) BITMAP_SHINY + 6 at 0.3 its scale, one frame in five a smoke off it
// (ZzzEffect.cpp:8922-8948, MoveHandlers.cpp:1306-1345). SOUND_SUMMOM_RARGLE is the caller's.
//
// **Ours.** Smoke and cloud are this engine's sprites at sizes and lives chosen to read as MU's
// (BITMAP_SMOKE 57 and BITMAP_CLOUD's movers are not ported), and the twin-tail water particles are
// not drawn. The Lagul blends every other key of its action rather than every key, its six bone
// lights sit on the body corners nearest those bones at key 0, and it is drawn in its sheets'
// colour where MU hands it (0, 0, 0.1). The joint's BITMAP_FLARE + 1 sprite is not drawn.
//
// Presentation only: the realm struck everything on the tick it let the ghosts go. Pools are
// sized once and a frame allocates nothing.
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
    // Lagul01's poses, the casting rings and their sheets out of effects/pollution (and
    // effects/weakness's Suhwanzin2/22/222), the rest off the cooked showing. False when the
    // Lagul or its body sheet is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // The casting circle at `caster` and the ghosts let loose at `target` (world metres, the
    // ground under each).
    void cast(const float caster[3], const float target[3]);
    // The casting circle alone, in another book's colours: CreateCastingEffect's lights for
    // BITMAP_MAGIC 9, Suhwanzin1/11/111, 2/22/222 and 4 (SummonSystem.cpp:190-231). Requiem's
    // (fx/requiem.h) is drawn by this.
    struct Palette {
        float impact[3];
        float rings1[3];
        float rings2[3];
        float ring4[3];
    };
    static constexpr Palette kPollutionPalette = {
        {0.6f, 0.6f, 0.9f}, {0.6f, 0.3f, 0.9f}, {0.8f, 0.1f, 0.6f}, {0.9f, 0.1f, 1.0f}};
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
    static constexpr int kPoses = 6;
    static constexpr int kGroups = 3;         // lagulbody, lagulpinna, watermarkhik01
    // MU's book at +7..+10 (anRargle[1], 2): two quarters, a wide joint with its Lagul and a
    // thin one each. Ours: its top tier's four Laguls are 5,020 triangles and ran the transparent
    // pass past its 8,192 sprites over Aida's own.
    static constexpr int kJointsACast = 4;
    static constexpr int kTails = 40;
    static constexpr int kPuffs = 384;
    static constexpr float kLife = 160.0f;

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
    struct Joint {
        bool alive = false;
        bool wide = false;      // carries a Lagul
        float aim[3] = {};
        float at[3] = {};
        float yaw = 0.0f, pitch = 0.0f;
        float turnYaw = 0.0f, turnPitch = 0.0f;
        float left = kLife;
        float owed = 0.0f;
        float tail[kTails][3] = {};
        int tails = 0;
        float key = 0.0f;       // the Lagul's place in its action, in keys
        float smokeOwed = 0.0f;
    };
    struct Spring {
        bool alive = false;
        float at[3] = {};
        float left = 0.0f;
        float owed = 0.0f;
    };
    struct Puff {
        bool alive = false;
        bool cloud = false;
        float at[3] = {};
        float size = 1.0f;
        float age = 0.0f;
        float life = 30.0f;
        float spin = 0.0f;
        float tint[3] = {};
    };

    float unit();
    void fly(Joint& joint);
    void puff(bool cloud, const float at[3], float size, const float tint[3]);
    void gatherGround(gfx::Effects& effects, bgfx::TextureHandle sheet, gfx::Blend blend,
                      const float at[3], float across, float turnDegrees, const float colour[3]) const;
    void gatherLagul(gfx::Effects& effects, const Joint& joint) const;
    void gatherTail(gfx::Effects& effects, const Joint& joint) const;
    float lightOf(const Joint& joint) const;

    const content::Ground* ground_ = nullptr;
    std::vector<EffectCorner> rings_[kRingModels];
    bgfx::TextureHandle ringSheet_[kRingModels] = {};
    std::vector<EffectCorner> lagul_[kPoses][kGroups];
    bgfx::TextureHandle lagulSheet_[kGroups] = {};
    // The pose drawn this frame, blended into once per Lagul.
    mutable std::vector<EffectCorner> blend_[kGroups];
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle impact_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle spirit_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle cloud_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;
    Circle circles_[kCasts] = {};
    Ring ringsLive_[kRingModels * kCasts] = {};
    Joint joints_[kJointsACast * kCasts];
    Spring springs_[kCasts] = {};
    Puff puffs_[kPuffs] = {};
    uint32_t dice_ = 0x9A6E1u;
};

}  // namespace mu::game
