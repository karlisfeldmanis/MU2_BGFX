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
//   And the early wave's last three (2026-10-06):
//     RenderCharacter's MODEL_DRAGON_SOUL_STAFF (:10261-10271): at (0, -120, 5) in the hand
//         BITMAP_SHINY + 1 at 1.5 and BITMAP_LIGHT at L + 1, L*(0.6, 0.6, 2); at (0, 100, 10)
//         BITMAP_LIGHT at L + 1.
//     RenderLinkObject's MODEL_ELEMENTAL_MACE (:7568-7576): on bone 1 BITMAP_LIGHT at 2,
//         L*(1, 0.9, 0), and a grey one at sin(WorldTime*0.002) + 0.5.
//     RenderLinkObject's MODEL_GREAT_REIGN_CROSSBOW (:7541-7558): (0, 0, 10) on bones 1-5,
//         BITMAP_SHINY + 1 at 1 and BITMAP_LIGHT at 2, L*(0.5, 0.5, 0.8), the fifth light white.
//
//   And Icarus's Crusts' two (2026-10-06), RenderCharacter's held switch:
//     MODEL_THUNDER_BLADE (:10238-10256): S = sin(WorldTime*0.004)*0.3 + 0.3, at (0, -20, 15)
//         in the hand BITMAP_SHINY + 1 at S + 1, S*(0.2, 0.2, 1); and three
//         BITMAP_JOINT_THUNDER sub 10 from there to (0, -133, 7), not carried.
//     MODEL_LEGENDARY_SHIELD (:10277-10282): at (20, 0, 0) BITMAP_SHINY + 1 at 1.5,
//         L*(0.4, 0.6, 1.5).
//     MODEL_DARK_BREAKER (:10218-10236): two BITMAP_FLARE + 1 sub 4 streaks every frame,
//         white, sin(WorldTime*0.004)*10 + 20 wide (ZzzEffectJoint.cpp:2103-2115), from
//         (0, -20, -40) to (0, -160, -10) and from (0, -10, 28) to (0, -145, 18).
//
// A blue string of lights down the crossbow, a blue spark on the shield's face, eight
// violet-blue lights along the spear's swirl; the staff's two blue ends, the mace's yellow
// head, and lights at the Great Reign's four limb tips and nose.
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
    enum class Item : uint8_t {
        SaintCrossbow,
        GrandSoulShield,
        DragonSpear,
        DragonSoulStaff,
        ElementalMace,
        GreatReignCrossbow,
        ThunderBlade,
        LegendaryShield,
        DarkBreaker,
    };
    static constexpr int kMostPoints = 8;
    static constexpr Item kItems[9] = {Item::SaintCrossbow,   Item::GrandSoulShield,
                                       Item::DragonSpear,     Item::DragonSoulStaff,
                                       Item::ElementalMace,   Item::GreatReignCrossbow,
                                       Item::ThunderBlade,    Item::LegendaryShield,
                                       Item::DarkBreaker};

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
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;  // Flare02, BITMAP_FLARE + 1
    std::vector<Held> held_;
    // MU's per-frame rolls, held between its frames: RenderCharacter's and RenderLinkObject's.
    float handLuminosity_ = 0.85f;
    float linkLuminosity_ = 0.42f;
    float clock_ = 0.0f;  // seconds, for the mace's sin(WorldTime*0.002) light
    float step_ = 0.0f;
    uint32_t seed_ = 0x4E1D5u;
    bool open_ = false;
};

}  // namespace mu::game
