// A Whirlwind rune's pull (sim Realm::whirl), drawn as a chain: thrown from the knight to each
// body it hauls in, reeled in with it as it slides -- its links ride with the body, so they run
// into his hands -- and let go as it lands beside him, on the crash (whirl_pull). Ours: MU has no
// such rune (the user, 2026-10-25: "dont like pufs, can we make chains?"). The chain is Season
// 17's Object132/so_chain, a 128x64 strip of links that tiles end to end, drawn as a ribbon
// facing the eye, a quad for each length of the strip, so a link keeps its size at any reach.
// Presentation only: the realm slid and struck them on its own ticks. A frame allocates nothing.
#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Chains {
public:
    // The chain sheet off the cooked showing. False without it, and then no chain is drawn.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);

    // A chain from body `knight` to body `target`, let go `hold` seconds from now.
    void cast(uint32_t knight, uint32_t target, float hold);

    // `where(id, out)` gives a body's middle while it is drawn, false once it is not.
    template <typename Where>
    void update(float seconds, Where where);

    void gather(gfx::Effects& effects, const float eye[3]) const;

private:
    struct Chain {
        bool alive = false;
        uint32_t knight = 0, target = 0;
        float from[3] = {}, to[3] = {};
        float age = 0.0f;   // seconds since it was thrown
        float hold = 0.0f;  // seconds it holds before it is let go
    };

    static constexpr int kChains = 32;
    static constexpr float kThrow = 0.08f;    // seconds out to the body
    static constexpr float kLetGo = 0.15f;    // seconds fading once let go
    static constexpr float kHalf = 0.16f;     // metres either side of its line
    static constexpr float kRepeat = 0.64f;   // metres of chain to one length of the strip

    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    Chain chains_[kChains] = {};
};

template <typename Where>
void Chains::update(float seconds, Where where) {
    for (Chain& one : chains_) {
        if (!one.alive) continue;
        one.age += seconds;
        // Held, it follows both ends; a body no longer drawn lets it go where it was.
        if (one.age < one.hold) {
            float at[3];
            if (where(one.knight, at)) std::copy(at, at + 3, one.from);
            if (where(one.target, at)) {
                std::copy(at, at + 3, one.to);
            } else {
                one.hold = one.age;
            }
        }
        if (one.age >= std::max(one.hold, kThrow) + kLetGo) one.alive = false;
    }
}

}  // namespace mu::game
