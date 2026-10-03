// The wizard's Ice: MU's `MODEL_ICE` block closing on the body and five `MODEL_ICE_SMALL` shards
// thrown off it (ZzzCharacter.cpp:4956; the effect's life at ZzzEffect.cpp:2187 and :7637).
//
// MU2's `client/core/Ice.cs` is the second source, ported: its numbers are MU's, traced there.
// What is MU's: the block at 0.8, stepping one key a frame through Ice01's six poses and holding
// on the last, then going out a twentieth a frame with a wisp of vapour on a coin every frame;
// fifty frames at most; additive on ice.png. The shards: Ice02 at 0.8-1.1, lifted fifty units,
// thrown flat at 6.4-32 units a frame under a 0.9 drag, rising 8-23 and falling three a frame
// squared, bouncing at half and tumbling end over end, 32-47 frames, dimmed to 0.3, a wisp one
// frame in ten. Not ported: MU's negative ground light under the block (this renderer adds only).
//
// Presentation only. The blow and the chill are the realm's (`SkillRow::chillTicks`).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "gfx/effects.h"

namespace mu::game {

class Ice {
public:
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    // A block on the body standing at `feet` (world metres), turned to `yaw` -- the caster's, as
    // MU turns it -- and its five shards. `floor` is the ground there, for the shards' bounce.
    void freeze(const float feet[3], float yaw);
    // An Ice Monster's death: its body out and ten shards off its feet, no block.
    void shatter(const float feet[3]);
    // **Cold on the caster** while he casts it (the user, 2026-09-28: "character need some ice
    // smoke effect on cast"), ours, as Meteorite's burn is: frosty wisps born round his body --
    // `feet` and his drawn `tall` -- rising off him. Called every frame the cast runs.
    void chill(const float feet[3], float tall, float seconds);
    // **Rime on an iced body** for as long as it is iced, ours (beside the glaze, kIcedChrome in
    // play_tuning.h): the caster's frost, sparser and closer, smoking off it. No clock of its own
    // -- a coin per call -- so any number of bodies can carry it in one frame.
    void rime(const float feet[3], float tall, float seconds);
    void update(float seconds);
    void gather(gfx::Effects& effects) const;

private:
    struct Block {
        bool alive = false;
        float at[3] = {};
        float yaw = 0.0f;
        float frame = 0.0f;  // the key it is on, 0 to 5
        float alpha = 1.0f;
        float left = 0.0f;   // reference frames
        float vapour = 0.0f;
    };
    struct Shard {
        bool alive = false;
        float at[3] = {};
        float velocity[3] = {};  // metres a second, flat
        float rise = 0.0f;       // metres a second, up
        float floor = 0.0f;
        float size = 1.0f;
        float yaw = 0.0f;
        float tumble = 0.0f;     // degrees
        float left = 0.0f;
        float vapour = 0.0f;
    };
    struct Wisp {
        bool alive = false;
        float at[3] = {};
        float size = 0.5f;
        float age = 0.0f;  // reference frames
        float spin = 0.0f;
        bool frost = false;  // off the caster: bluer, brighter, rising faster
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kBlockScale = 0.8f;
    static constexpr float kHoldsAt = 5.0f;
    static constexpr float kFadeStep = 0.05f;
    static constexpr float kBlockFrames = 50.0f;
    static constexpr int kShards = 5;
    static constexpr int kShatterShards = 10;
    static constexpr float kShardLift = 50.0f;
    static constexpr float kShardLight = 0.3f;
    static constexpr float kWispFrames = 28.0f, kWispFades = 20.0f;
    static constexpr float kBreath[3] = {0.26f, 0.30f, 0.36f};
    // The caster's frost: a wisp every 1.5 reference frames within 0.35 m of him, from his knees
    // to his shoulders, smaller than the block's and a paler, colder blue.
    static constexpr float kFrostEvery = 1.5f;
    static constexpr float kFrostRadius = 0.35f;
    static constexpr float kFrost[3] = {0.42f, 0.58f, 0.82f};
    float frostDue_ = 0.0f;
    // The rime: a wisp every four reference frames a body, within 0.25 m, shins to crown.
    static constexpr float kRimeEvery = 4.0f;
    static constexpr float kRimeRadius = 0.25f;

    static constexpr int kBlocks = 24;  // the spell's 24 bodies at most (sim kVictims)
    static constexpr int kMaxShards = 144;
    static constexpr int kWisps = 144;

    std::vector<EffectCorner> keys_[6];
    int keyCount_ = 0;
    std::vector<EffectCorner> shard_;
    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    Block blocks_[kBlocks] = {};
    Shard shards_[kMaxShards] = {};
    Wisp wisps_[kWisps] = {};
    uint32_t dice_ = 0x1CE1CE1Cu;
    float unit();
    float between(float a, float b) { return a + (b - a) * unit(); }
    void shards(const float feet[3], int count);
    Wisp* puff(const float at[3]);
};

}  // namespace mu::game
