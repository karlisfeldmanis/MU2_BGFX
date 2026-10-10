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
//         BITMAP_JOINT_THUNDER sub 10 from there to (0, -133, 7), S*20 + 20 wide: each a
//         one-frame line in ten steps, every step nudged rand() % 8 - 4 across, in (0.3, 0.3, 1)
//         (ZzzEffectJoint.cpp:1188-1224) -- three crackling blue lines up the blade.
//     MODEL_LEGENDARY_SHIELD (:10277-10282): at (20, 0, 0) BITMAP_SHINY + 1 at 1.5,
//         L*(0.4, 0.6, 1.5).
//     MODEL_DARK_BREAKER (:10218-10236): two BITMAP_FLARE + 1 sub 4 streaks every frame,
//         white, sin(WorldTime*0.004)*10 + 20 wide (ZzzEffectJoint.cpp:2103-2115), from
//         (0, -20, -40) to (0, -160, -10) and from (0, -10, 28) to (0, -145, 18).
//
//   And the strongest of each line (2026-10-10), RenderLinkObject's:
//     MODEL_KNIGHT_BLADE (:7663-7674): on bones 1 and 2, L = sin(WorldTime*0.002)*0.35 + 0.65,
//         BITMAP_LIGHT at L*0.9, L*(0.43, 0.14, 0.6), and a grey one at sin(..)*0.5 + 0.4.
//     MODEL_STAFF_OF_KUNDUN (:7446-7473): L = sin(WorldTime*0.002)*0.3 + 0.7; on bones 5 and 6
//         BITMAP_LIGHT at 1, L*(0, 0.5, 1), and two white BITMAP_SHINY + 1 at 0.5 turning
//         opposite ways; on bone 8 the same at 2 and 1.
//     MODEL_ARROW_VIPER_BOW (:7676-7698): S = sin(WorldTime*0.001) + 1, on bones 6 and 2
//         BITMAP_LIGHT at S*0.8, (3L, L, L) on the link roll; and off bone 9, outside a safe
//         zone, a grey light at S*0.8 and two red BITMAP_SHINY + 1 at 0.8 turning opposite ways,
//         grey G = sin(WorldTime*0.001)*0.3 + 0.3 and red (3G, G, G). The safe zone is not asked.
//
//   And the last of the sheen list (2026-10-10), RenderCharacter's held switch:
//     MODEL_GORGON_STAFF (:10270-10276): at (0, -90, 0) in the hand BITMAP_SHINY + 1 at 2,
//         L*(0.4, 0.8, 0.6).
//     MODEL_HELIACAL_SWORD (:10214-10216, :10466-10470): at (0, -160, 0), the tip, BITMAP_LIGHT
//         at 1.4 in (L, L, L); and while it is held at all, AddTerrainLight (0.8, 0.64, 0.4) over
//         three tiles round its wielder (:5659-5663) -- a warm pool, given to the renderer as a
//         moving light from the hilt (lights()). The tip is the one of MU's ten sword tips
//         (fx/gleam.h, turned down) the user asked for: 'Heliacal Sword ... its tip light'.
//   And the Archangel's three (2026-10-10), RenderLinkObject's, L its link roll:
//     MODEL_DIVINE_SWORD_OF_ARCHANGEL (:7491-7521): on bones 2-8, up the blade, BITMAP_LIGHT
//         at 2 in L*(1, 0.3, 0.1), and one frame in three a grey (0.4) BITMAP_SHINY + 1 at 0.6.
//     MODEL_DIVINE_STAFF_OF_ARCHANGEL (:7062-7077): the same at (i*30 - 180, -40, 0) on its
//         bone 0 for i of 0-9, ten down the staff.
//     MODEL_DIVINE_CB_OF_ARCHANGEL (:7523-7539): its sprites are commented out in MuMain.
//   and all three, and the Divine Scepter, AddTerrainLight (0.64, 0.4, 0.24) over two tiles
//   round their wielder (:5664-5667), as the Heliacal's (lights()).
//   And the audit's last held sparkles (2026-10-10), on the items' own bones at rest (muextract
//   export-rig; MuMain's RenderBrightEffect reads the item's bones the link just posed):
//     RenderCharacter's held switch, L its hand roll:
//     MODEL_CRYSTAL_SWORD (:10417-10429): eight BITMAP_LIGHT at 1, L*(1, 0.6, 0.4), at
//         (0, -j*20 - 30, 0), each drawn three frames in four.
//     MODEL_CHAOS_DRAGON_AXE (:10431-10447): S = sin(WorldTime*0.004)*0.3 + 0.7; at (0, -84, 0)
//         BITMAP_SHINY + 1 at S + 1.5 in S*(1, 0.2, 0.1), five more at 1 down the haft, and on its
//         bones 2 and 6 at 1 in S*(0.5, 0.1, 0.05).
//     MODEL_LEGENDARY_STAFF (:10284-10291): at (0, -145, 0) BITMAP_SHINY + 1 at 1.5 and
//         BITMAP_LIGHTNING + 1 at 0.3, L*(0.4, 0.6, 1).
//     MODEL_CHAOS_LIGHTNING_STAFF (:10292-10300): L*(0.4, 0.6, 1), BITMAP_SHINY + 1 at 2 on its bone
//         27 and BITMAP_LIGHT at 1.5 on bones 28-37.
//     MODEL_BLUEWING_CROSSBOW, MODEL_AQUAGOLD_CROSSBOW (:10449-10469): six BITMAP_LIGHT at 2 at
//         (0, -20, -j*20), L*(0.2, 0.4, 0.6) and L*(0.6, 0.4, 0.2).
//     RenderLinkObject's, L its link roll (:7031-7058): BITMAP_SHINY + 1 at 1 on the Tiger and
//         Silver Bows' bones 2 and 6 in L*(1, 0.6, 0.2), the Chaos Nature Bow's 13-18 in
//         L*(0.6, 1, 0.8), and the Celestial Bow's 13-18 and 5-8 in L*(0.5, 0.5, 0.8).
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
#include "gfx/renderer.h"

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
        KnightBlade,
        StaffOfKundun,
        ArrowViperBow,
        GorgonStaff,
        HeliacalSword,
        DivineSword,
        DivineStaff,
        DivineCrossbow,
        CrystalSword,
        ChaosDragonAxe,
        LegendaryStaff,
        ChaosLightningStaff,
        BluewingCrossbow,
        AquagoldCrossbow,
        TigerBow,
        SilverBow,
        ChaosNatureBow,
        CelestialBow,
    };
    static constexpr int kMostPoints = 11;
    static constexpr Item kItems[27] = {Item::SaintCrossbow,   Item::GrandSoulShield,
                                        Item::DragonSpear,     Item::DragonSoulStaff,
                                        Item::ElementalMace,   Item::GreatReignCrossbow,
                                        Item::ThunderBlade,    Item::LegendaryShield,
                                        Item::DarkBreaker,     Item::KnightBlade,
                                        Item::StaffOfKundun,   Item::ArrowViperBow,
                                        Item::GorgonStaff,     Item::HeliacalSword,
                                        Item::DivineSword,     Item::DivineStaff,
                                        Item::DivineCrossbow,  Item::CrystalSword,
                                        Item::ChaosDragonAxe,  Item::LegendaryStaff,
                                        Item::ChaosLightningStaff, Item::BluewingCrossbow,
                                        Item::AquagoldCrossbow, Item::TigerBow,
                                        Item::SilverBow,       Item::ChaosNatureBow,
                                        Item::CelestialBow};

    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    void shutdown();

    // Forgets the last frame's items and rolls MU's luminosities. Called before the feeds.
    void update(float seconds);
    // One held item this frame: its points (as many as points() gave) in world metres.
    void feed(Item item, const float at[][3], int count);
    void gather(gfx::Effects& effects) const;
    // The lights a held item throws on what is round it, this frame: the Heliacal Sword's. Into
    // `out`, at most `max`; how many were written.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;

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
    bgfx::TextureHandle thunder_ = BGFX_INVALID_HANDLE;  // BITMAP_JOINT_THUNDER
    bgfx::TextureHandle lightning_ = BGFX_INVALID_HANDLE;  // BITMAP_LIGHTNING + 1, lightning2
    std::vector<Held> held_;
    // MU's per-frame rolls, held between its frames: RenderCharacter's and RenderLinkObject's.
    float handLuminosity_ = 0.85f;
    float linkLuminosity_ = 0.42f;
    float clock_ = 0.0f;  // seconds, for the mace's sin(WorldTime*0.002) light
    float step_ = 0.0f;
    uint32_t seed_ = 0x4E1D5u;
    // The thunder's crackle, re-rolled each reference frame as MU's joint is made anew each one:
    // the frame's number seeds it.
    uint32_t crackleFrame_ = 0;
    bool open_ = false;
};

}  // namespace mu::game
