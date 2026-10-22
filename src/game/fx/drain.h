// The Ancient rune Drain Life (sim/items.h kDrain...): MU's MODEL_ALICE_DRAIN_LIFE, which
// ZzzCharacter lets go as the Summoner's Drain Life lands (:5214-5223), drawn by
// Move_MODEL_ALICE_DRAIN_LIFE (MoveHandlers.cpp:2066-2208) over its 70 frames:
//
//   * every frame, none, one or two red BITMAP_LIGHT + 2 motes (Effect/cra_04, (1, 0.2, 0.2),
//     scale 1.8) round the caster -- 40 units either way, 30 deep, 80 up and -100..80 about
//     that -- and from frame 10 on round the target as well;
//   * frames 0-4, none to four BITMAP_DRAIN_LIFE_GHOST joints (Effect/gostmark01, (0.8, 0.1,
//     0.2), scale 40) thrown from a hundred units before his hand at the target's middle;
//   * frame 6, BITMAP_JOINT_ENERGY strands ((1, 0, 0.1), scale 10) off the target's bones, one
//     in two, back to him 80 units up: the life coming home.
//
// MU's own: the timeline, the counts, the colours, the sheets and the places. Ours: how each
// piece moves after it is thrown, where MU's particle and joint code was not followed -- a
// mote drifts up, grows and fades over 20 frames; a ghost flies to the target in 12, swinging
// side to side as MU's turns it 90 degrees a frame; a strand is a short red streak flying home
// in 10, drawn with the light sheet (JointEnergy01 is an 8x4 scrap). SOUND_SKILL_DRAIN_LIFE is
// played by Play where the realm said it. Presentation only; the realm struck and healed on
// the tick it said so. Pools are sized once and a frame allocates nothing.
#pragma once

#include <cstdint>
#include <string>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Drain {
public:
    // The mote, ghost and light sheets off the cooked showing. False when any is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);

    // A drain from body `caster` to body `target`; `self` and `there` are their middles now
    // (world metres).
    void cast(uint32_t caster, uint32_t target, const float self[3], const float there[3]);

    // `where(id, out)` gives a body's middle while it is drawn, false once it is not.
    template <typename Where>
    void update(float seconds, Where where);

    void gather(gfx::Effects& effects) const;

private:
    struct Cast {
        bool alive = false;
        uint32_t caster = 0, target = 0;
        float self[3] = {}, there[3] = {};
        float hand[3] = {};       // a hundred units before him, where the ghosts leave
        float age = 0.0f;         // reference frames
        int done = -1;            // the last whole frame thrown
    };
    struct Mote {
        bool alive = false;
        float at[3] = {};
        float scale = 1.8f;
        float spin = 0.0f;
        float left = 0.0f;
    };
    // A ghost or a strand: a head flying from `from` to the body `to` follows.
    struct Flier {
        bool alive = false;
        bool ghost = false;
        uint32_t whom = 0;        // the body it flies to
        float from[3] = {}, to[3] = {};
        float side[3] = {};       // the swing's axis, level and across the flight
        float swing = 0.0f;       // metres either way
        float age = 0.0f, full = 1.0f;
    };

    static constexpr float kFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kFrames = 70.0f;
    static constexpr int kCasts = 8;
    static constexpr int kMotes = 384;
    static constexpr int kFliers = 160;
    static constexpr float kMoteFrames = 20.0f, kMoteFade = 10.0f;
    static constexpr float kGhostFrames = 12.0f, kStrandFrames = 10.0f;

    float unit();
    void throwMote(const float middle[3]);
    void throwFlier(bool ghost, const float from[3], const float to[3], uint32_t whom);
    void frame(Cast& one, int at);

    bgfx::TextureHandle mote_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ghost_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;
    Cast casts_[kCasts] = {};
    Mote motes_[kMotes] = {};
    Flier fliers_[kFliers] = {};
    uint32_t dice_ = 0xD4A1171Fu;
};

template <typename Where>
void Drain::update(float seconds, Where where) {
    const float frames = seconds * kFps;
    for (Cast& one : casts_) {
        if (!one.alive) continue;
        float at[3];
        if (where(one.caster, at)) {
            for (int k = 0; k < 3; ++k) one.self[k] = at[k];
        }
        if (where(one.target, at)) {
            for (int k = 0; k < 3; ++k) one.there[k] = at[k];
        }
        one.age += frames;
        while (one.done + 1 <= int(one.age) && one.done + 1 < int(kFrames)) frame(one, ++one.done);
        if (one.age >= kFrames) one.alive = false;
    }
    for (Mote& one : motes_) {
        if (!one.alive) continue;
        one.left -= frames;
        if (one.left <= 0.0f) {
            one.alive = false;
            continue;
        }
        one.at[1] += 0.4f * seconds;
        one.scale += 0.04f * frames;
        one.spin += 0.05f * frames;
    }
    for (Flier& one : fliers_) {
        if (!one.alive) continue;
        one.age += frames;
        if (one.age >= one.full) {
            one.alive = false;
            continue;
        }
        float at[3];
        if (one.whom != 0 && where(one.whom, at)) {
            for (int k = 0; k < 3; ++k) one.to[k] = at[k];
        }
    }
}

}  // namespace mu::game
