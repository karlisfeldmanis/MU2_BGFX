// The lights MuMain hangs on three held items, every frame they are in hand:
//
//   RenderCharacter's held switch (ZzzCharacter.cpp:10406-10415), Luminosity rolled
//   (rand() % 30 + 70) * 0.01 a frame (:9989):
//     case MODEL_SAINT_CROSSBOW:
//         for j in 0..5: Position = (0, -10, -20 j) in the hand
//             CreateSprite(BITMAP_LIGHT, p, 2.f, L*(0.4, 0.6, 1));
//
//   RenderLinkObject's (:7031-7731), Luminosity (rand() % 30 + 70) * 0.005 a frame:
//     case MODEL_GRAND_SOUL_SHIELD:  Position = (15, -15, 0) on its bone 1
//         CreateSprite(BITMAP_SHINY + 1, p, 1.5f, L*(0.6, 0.6, 2));
//         CreateSprite(BITMAP_LIGHT, p, L + 1.5f, L*(0.6, 0.6, 2));
//     case MODEL_DRAGON_SPEAR:  for i in 1..8: its bone i
//         CreateSprite(BITMAP_LIGHT, p, 1.3f, L*(0.2, 0.1, 0.8));
//
// A blue string of lights down the crossbow, a blue spark on the shield's face, and eight
// violet-blue lights along the spear's swirl.
//
// **Ours, marked.** Kept subtle (fx/staff_fire.h: the user, 2026-10-04, 'Yes, subtle'): a
// quarter of MU's size and under half its light. The spear's and the shield's bones are dummies with no
// vertices, so their points are worked out once from the rigs and written in each glb's space
// (HeldLights::points); the crossbow's are given in MU's hand frame, which the glb does not
// share, and are laid down its stock instead, where MU's lights fall.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class HeldLights {
public:
    enum class Item : uint8_t { SaintCrossbow, GrandSoulShield, DragonSpear };
    static constexpr int kMostPoints = 8;
    static constexpr Item kItems[3] = {Item::SaintCrossbow, Item::GrandSoulShield,
                                       Item::DragonSpear};

    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Forgets the last frame's items and rolls MU's luminosities. Called before the feeds.
    void update(float seconds);
    // One held item this frame: its points (as many as points() gave) in world metres.
    void feed(Item item, const float at[][3], int count);
    void gather(gfx::Effects& effects) const;

    // The item's glb name, and its points in that glb's own space in metres; the count.
    static const char* mesh(Item item);
    static int points(Item item, float out[kMostPoints][3]);

private:
    struct Held {
        Item item;
        int count = 0;
        float at[kMostPoints][3];
    };

    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;  // BITMAP_LIGHT
    bgfx::TextureHandle shiny_ = BGFX_INVALID_HANDLE;  // BITMAP_SHINY + 1
    std::vector<Held> held_;
    // MU's per-frame rolls, held between its frames: RenderCharacter's and RenderLinkObject's.
    float handLuminosity_ = 0.85f;
    float linkLuminosity_ = 0.42f;
    float step_ = 0.0f;
    uint32_t seed_ = 0x4E1D5u;
    bool open_ = false;
};

}  // namespace mu::game
