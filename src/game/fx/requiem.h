// Requiem: the Summoner's Book of Neil, MU's AT_SKILL_SUMMON_REQUIEM (224) -- Neil called up before
// the caster (CSummonSystem::CreateSummonObject, SummonSystem.cpp:150-165), her blade's red
// crescent, the thorns out of the ground under the target and the burning cracks round them, and
// the magenta mist off a body it bleeds. Here the Ancient rune Requiem's (sim/items.h kRequiem...),
// said as Loosed with skill 224 (sim::skill::kRequiem); its casting circle is fx/pollution's,
// drawn in Requiem's colours (Pollution::kRequiemPalette). docs/plans/ancient-runes/requiem.md.
//
// **What is MU's, and kept** (MuMain; 1.03.35 the same). The book's top tier, +11 (subtype 2),
// as the footage shows it. Frames are MU's 25 a second, frame 0 the cast.
//   * **Neil** (MODEL_SUMMONER_SUMMON_NEIL, Data/Skill/summon_neil.bmd): 100 units before the
//     caster, lifted 10, facing the target; LifeTime 80, her one 13-key action at 0.35 a key a
//     frame, played once and held at key 12 (ZzzEffect.cpp:797-806). Alpha +0.04 a frame to 0.7,
//     -0.05 a frame over the last 20 (MoveHandlers.cpp:1268-1269). Meshes 0 and 2 ADDED at her
//     alpha while it is under 0.7, alpha-blended at 0.7 once there; the blade, mesh 1, always
//     added in pure red; nine red BITMAP_LIGHTs (flare01) at scale 1 on bones 51-59, every frame
//     (ZzzObject.cpp:1747-1769).
//   * **The crescent**: for keys 0-10, ten samples a frame of bones 51 and 59 across the frame's
//     share of her swing, laid as one CreateObjectBlur of type 2 (Effect/motion_blur_r) in red
//     (ZzzObject.cpp:1771-1797), which lives 30 frames from its first sample, gives up one sample
//     a frame and keeps at most 599, drawn added, its light (n - j) / n and its U j / n along the
//     tail (ZzzEffectBlurSpark.cpp:256-407).
//   * **The thorns** (MODEL_SUMMONER_SUMMON_NEIL_NIFE1..3, nelleff_nife01-03.bmd) at the target
//     past key 8, facing her way: LifeTime 50, Alpha 1 and -0.05 a frame over the last 20, their
//     6 keys at 0.3 a key a frame, held (EffectRegistry.cpp:58-61, EffectBehaviors.cpp:98-103);
//     mesh 0 textured, mesh 1 added in red (ZzzObject.cpp:1799-1805).
//   * **The burning ground** (MODEL_SUMMONER_SUMMON_NEIL_GROUND1..3, nell_nifegrund01-03.bmd) past
//     key 10: GROUND1 60 units before Neil and GROUND1-3 at the target; LifeTime 50, Alpha +0.3 a
//     frame and -0.05 over the last 20 (EffectBehaviors.cpp:106-111); both meshes added, mesh 1's U
//     at -WorldTime x 0.001 (ZzzObject.cpp:1806-1810). SOUND_SUMMON_REQUIEM then -- `takeThorns`.
//   * **The bleed's mist** (eDeBuff_NeilDOT, MODEL_SUMMONER_EQUIP_HEAD_NEIL subtype 1): each frame
//     one BITMAP_LIGHT + 2 subtype 5 off a random bone -- Effect/fi01, thrown 6 units a frame
//     outward and 4 up, gravity 0.4, its push x0.6 a frame after the first, life 21, scale
//     0.08-0.11 growing 0.04 a frame, alpha -0.1 a frame over its last 10, coloured (1, 0, 0.6) x
//     alpha, added (MoveHandlers.cpp:1115-1127, ZzzEffectParticle.cpp:855-883, :4015-4040).
//
// **Ours.** One Requiem drawn at a time: a roll while one plays strikes, unseen (the realm's blows
// land all the same) -- Neil, the top tier's thorns and the circle are some 5,300 triangles of the
// transparent pass's 16,384, and two inside a Pollution's ghosts and a town's lights is where the
// pass refused before. Her poses are blended vertex by vertex between MU's keys where MU blends
// bones; the blade's bones are baked (kBladeEnds, tenths of a key; kBladeBones, each key) from
// muextract export-rig, slerped as MU's Animation. Neil is drawn unlit at her sheets' colour. The
// mist rises in the world and does not ride the body as MU's does.
//
// Presentation only: the realm chose the bodies at the roll and strikes them 20 ticks later.
// Pools are sized once and a frame allocates nothing.
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

class Requiem {
public:
    // Neil's poses, the thorns', the ground's and their sheets out of effects/requiem, the
    // flare, the ribbon and the mist off the cooked showing. False when Neil is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // Neil before `caster`, facing `target`, and her thorns at `target` (world metres, the ground
    // under each). False, and nothing drawn, while one is still playing.
    bool cast(const float caster[3], const float target[3], uint32_t caster_id);
    // The thorns' moment, once: where the burning ground came up, for SOUND_SUMMON_REQUIEM.
    bool takeThorns(float at[3], uint32_t& caster_id);
    // One frame's puff of the bleed's mist off a bone of a body it bleeds, at `at`.
    void mist(const float at[3]);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

    // MU's frames a second, which `mist` is called at by its caller.
    static constexpr float kFps = 25.0f;

private:
    static constexpr float kUnit = 0.01f;
    static constexpr int kKeys = 13;
    static constexpr int kGroups = 3;        // maskreta01, gra, maskreta03
    static constexpr int kNifes = 3;
    static constexpr int kNifePoses = 3;     // the most keys a thorn model changes at
    static constexpr int kGrounds = 4;       // GROUND1 before Neil, GROUND1-3 at the target
    static constexpr int kBlurMost = 600;    // MAX_OBJECT_BLUR_TAILS
    static constexpr int kMists = 320;

    struct Placed {
        float at[3] = {};
        float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, 1};
    };
    struct Mist {
        bool alive = false;
        float at[3] = {};
        float push[3] = {};
        float scale = 0.1f;
        float alpha = 1.0f;
        float spin = 0.0f;
        float life = 21.0f;
        float owed = 0.0f;
    };

    float unit();
    void step();  // one of MU's frames
    void blurAt(float key, float out[2][3]) const;
    void placeAt(const Placed& basis, const float model[3], float scale, float out[3]) const;
    void blendPose(const std::vector<EffectCorner>& a, const std::vector<EffectCorner>& b,
                   float t, std::vector<EffectCorner>& out) const;

    const content::Ground* ground_ = nullptr;
    std::vector<EffectCorner> neil_[kKeys][kGroups];
    bgfx::TextureHandle neilSheet_[kGroups] = {};
    mutable std::vector<EffectCorner> neilBlend_[kGroups];
    std::vector<EffectCorner> nife_[kNifes][kNifePoses][2];
    int nifeKeys_[kNifes][kNifePoses] = {};
    int nifePoses_[kNifes] = {};
    mutable std::vector<EffectCorner> nifeBlend_[2];
    std::vector<EffectCorner> groundMesh_[3][2];
    bgfx::TextureHandle groundSheet_[3][2] = {};
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ribbon_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle mist_ = BGFX_INVALID_HANDLE;

    // The one Requiem playing.
    bool playing_ = false;
    float owed_ = 0.0f;       // seconds not yet stepped, as MU's frames
    int frame_ = 0;           // MU frames since the cast
    float clock_ = 0.0f;      // seconds since the cast, for the ground's streaming U
    uint32_t caster_ = 0;
    Placed neilAt_;
    Placed targetAt_;
    float target_[3] = {};
    float key_ = 0.0f;        // Neil's AnimationFrame
    float alpha_ = 0.0f;
    bool nifesUp_ = false, groundUp_ = false, thornsOwed_ = false;
    int nifeAge_ = 0, groundAge_ = 0;
    float nifeAlpha_ = 1.0f, groundAlpha_ = 0.0f;
    Placed groundAt_[kGrounds];
    // The crescent: newest first, as MU's P1/P2.
    float blur_[kBlurMost][2][3] = {};
    int blurs_ = 0;
    int blurLife_ = 0;
    bool blurLive_ = false;

    Mist mists_[kMists] = {};
    uint32_t dice_ = 0x2E91Au;
};

}  // namespace mu::game
