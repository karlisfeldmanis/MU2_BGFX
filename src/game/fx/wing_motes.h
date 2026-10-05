// Motes off a worn wing's tips: each wing its own -- the Elf's green glints, Heaven's white down,
// Satan's dull embers, the Spirits' teal twinkle, Soul's violet sparkle, the Dragon's rising
// sparks -- a few a second at rest and more while he flies, left behind in the world as he goes.
//
// **Ours, marked.** MuMain draws no effect on a 1st level wing (game/wings.h) and on the 2nd
// only the slow pulse of their bright shells, which the cook already carries. The user asked for
// 'make sense' effects on every wing (2026-10-05); kept to the monster auras' taste (few, faint,
// small, no light), as the Staff of Resurrection's fire is (fx/staff_fire.h).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class WingMotes {
public:
    // The most tips a wing feeds (WingLook::tips).
    static constexpr int kMostTips = 8;

    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Moves the motes on. Called before the feeds.
    void update(float seconds);
    // One worn wing this frame: its body's name (Wing01..06), whether its bearer flies, and its
    // tips in world metres. A wing with no motif is ignored.
    void feed(const std::string& wing, bool flying, const float tips[][3], int count);
    void gather(gfx::Effects& effects) const;

private:
    struct Mote {
        float at[3];
        float velocity[3];
        float colour[3];
        float age = 0.0f;
        float life = 1.0f;
        float half = 0.03f;
        float spin = 0.0f;
        int sheet = 0;
    };
    float unit();

    bgfx::TextureHandle sheets_[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                      BGFX_INVALID_HANDLE};  // shiny, spark, light
    std::vector<Mote> motes_;
    float seconds_ = 0.0f;  // this frame's, for the feeds' rates
    float owed_[6] = {};    // motes owed, a wing kind each
    uint32_t seed_ = 0x3A11Fu;
    bool open_ = false;
};

}  // namespace mu::game
