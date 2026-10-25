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
//   And the 100-140 wave's first three (2026-10-22), RenderLinkObject's, on the items' own
//   bones at rest; MU's two flickers, sin(WorldTime*0.4) and sin(WorldTime*0.7), turn faster
//   than its frame, so here they are the hand roll's swing:
//     MODEL_DAYBREAK (:7226-7255): on bone 2 two BITMAP_LIGHT + 1 at 0.45 and BITMAP_SHINY + 1
//         at 0.7 and at 0.5-1.05, grey 0.6; and BITMAP_LIGHT at 3 in (f, f, f), f the flicker
//         *0.25 + 0.6.
//     MODEL_PLATINA_STAFF (:7379-7444), f the flicker *0.2 + 0.3: on bone 9, the head,
//         BITMAP_LIGHT at 0.3 grey 0.7 and at 4 in (0.2, 0.6 + f, 0.2), BITMAP_SHINY + 1 at
//         1.2-1.7 in (0.4, 0.5 + f, 0.4); on bones 5-8 green (0.1, 0.8, 0.1) lights at 0.2, on
//         10-13 grey 0.5 ones at 0.3; on bone 4 a green BITMAP_SHINY + 1 at 0.6; on bone 2, the
//         foot, BITMAP_LIGHT at 0.7 grey 0.8 and at 2 in (0.2, 0.6 + f, 0.2) and a green shiny
//         at 0.5. Its BITMAP_MAGIC rings on bones 9 and 2 are not drawn.
//     MODEL_ALBATROSS_BOW (:7346-7377): green (0, 0.7, 0) BITMAP_LIGHT at 1.2 on bones 10, 28,
//         34 and 16, the limb tips and either side of the grip; its green sparks not thrown.
//   And the 100-140 wave's second eight (2026-10-22), RenderLinkObject's, on the items' bones at
//   rest. MU's BITMAP_LIGHTMARKS and BITMAP_FLARE_BLUE are drawn here with BITMAP_LIGHT, and its
//   BITMAP_SHINY + 6 with BITMAP_SHINY + 1, in MU's colours and sizes; its pin lights, waterfall
//   sprites, clouds, joints and feathers are not thrown.
//     MODEL_SWORD_DANCER (:7257-7300): a red light at 1 and a flash on bone 1; red lights at 1.1
//         on bones 2-6, (0.8 + f, 0.1, 0).
//     MODEL_FLAMBERGE (:7733-7766): a gold light on Gold01, green-blue ones on b01 and n02, and
//         red marks (0.9, 0.1, 0.1) down bones 1-7, 0.8 to 0.5.
//     MODEL_SWORD_BREAKER (:7767-7797): green marks (0.1, 0.9, 0.1) down bones 1-7, 0.6 to 0.3.
//     MODEL_IMPERIAL_SWORD (:7798-7841): blue flares and shinies on bones 8-11, and blue marks
//         (0, 0.3, 0.7) down bones 2-7, 1 to 0.1.
//     MODEL_FROST_MACE (:7842-7890): its crystal (bone 24) lit (0.5, 0.8, 0.6) at 2, red shinies on
//         bones 1, 8 and 9, green on 10 and 11, and small lights up bones 12-23.
//     MODEL_STINGER_BOW (:7972-7999): a dim (0.2, 0.25, 0.3) light at 0.8 on every bone.
//     MODEL_DEADLY_STAFF (:8000-8036): orange lights (0.8, 0.3, 0.1) on bones 4-9, and on its two
//         red eyes (bones 2, 3) a light at 2 breathing |sin(WorldTime*0.001)|*0.8 + 0.2.
//     MODEL_IMPERIAL_STAFF (:8037-8075): blue lights (0.3, 0.3, 0.9) at 2-3 on bones 2-4, and on
//         bone 5 a red light and pink shinies.
//   And three of the wave's shields (2026-10-22), on their bones at rest; MU's BITMAP_FLARE_RED
//   is drawn with BITMAP_LIGHT in red. The Frost Barrier has no case.
//     MODEL_CRIMSONGLORY (:8118-8144): gold (0.8, 0.6, 0.2) lights at 1.5 on bones 4-10, and red
//         flares at 0.5, 0.3 and 0.3 on bones 1-3 breathing |sin(WorldTime*0.0005)|.
//     MODEL_SALAMANDER_SHIELD (:8146-8167): a red (0.9, 0, 0.2) light at 2 on bone 1; its fire
//         particles not thrown.
//     MODEL_GUARDIAN_SHILED (:8169-8191): violet (0.8, 0.3, 0.8) lights at 1.5 on bones 1-9,
//         breathing |sin(WorldTime*0.001)| + 0.1.
//   And Kanturu's four (2026-10-22), on their bones at rest, MU's offsets turned by the bone:
//     MODEL_BONE_BLADE (:7131-7143): two orange BITMAP_LIGHT + 1 at 0.75 either side of the guard,
//         (f, f - 0.5, f - 0.5), f flickering about 0.7.
//     MODEL_EXPLOSION_BLADE (:7145-7176): blue (0.2, 0.2, f) lights up the blade, bones 4 and 6-8
//         and 30 out from 4, and two shinies on 4; MU turns them, here they stand.
//     MODEL_SYLPH_WIND_BOW (:7079-7101): yellow (0.5, 0.5, 0.1) lights at 2 on the limb tips (bones
//         7, 15) and three (1, 1, 0.4) at 0.8, 1.3 and 2 at the grip; its sparks not thrown.
//     MODEL_GRAND_VIPER_STAFF (:7178-7200): at the head, 170 up bone 0, a grey shiny and a pink
//         (1, 0.4, 1) light and two shinies; its energy joint not thrown.
//   And the three gambling-shop weapons (2026-10-23), on their bones at rest; MU's BITMAP_MAGIC
//   rings, its moon and the bow's shock waves not thrown, BITMAP_SHINY + 6 drawn as + 1:
//     MODEL_CHROMATIC_STAFF (:8223-8252): on zx01 (bone 8), the head, a pink (0.9, 0.1, 0.4)
//         light at 3.5 and a yellow shiny at 1; on bones 1-7, the beads, pink lights at 0.6 and
//         yellow shinies at 0.3.
//     MODEL_BEUROBA (:8293-8310): pink-violet (0.9, 0.2, 0.7) shinies at 0.8 and 0.3 on zx01 and
//         zx02 (bones 3, 4), the blade's gems; a green (0.2, 0.9, 0.1) light at 1.5 on bone 5.
//     MODEL_AIR_LYN_BOW (:8347-8380): two teal (0.2, 0.8, 0.5) lights at 0.8 on each limb tip,
//         zx02 and zx03 (bones 7, 11); its flares on rx01-rx08 in a shot not thrown.
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

//   And the Dark Lord's first scepter (2026-10-25), RenderLinkObject's, L its link roll:
//     MODEL_BATTLE_SCEPTER (:7578-7595): S = sin(WorldTime*0.001) + 1; BITMAP_LIGHT at S in
//         L*(0.2, 0.1, 3) at (-15, 0, 0) and (10, 0, 0) on bone 1, the head; at the second two
//         BITMAP_SHINY + 1 at 0.6, grey sin(WorldTime*0.001)*0.5 + 0.5. MU turns them, here they
//         stand.
//   And the next three (2026-10-25):
//     MODEL_MASTER_SCEPTER (:7596-7612): on bones 2-5 (MU's loop skips bone 1) at (-10, 0, 0),
//         BITMAP_LIGHT at 1 in S*(0.6, 0.8, 1), S = sin(WorldTime*0.002)*0.35 + 0.65; on bone 4,
//         the centre, BITMAP_SHINY + 1 at 0.6 in 0.5 grey.
//     MODEL_GREAT_SCEPTER (:7614-7631): S = sin(WorldTime*0.001)*0.5 + 0.7; on bone 2, the
//         pommel, BITMAP_LIGHT at 1.5 and BITMAP_SHINY + 1 at S*0.5, both in S*(1, 0.8, 0.6); on
//         bone 1 at (S*-10, 0, 0), riding the shaft with S, BITMAP_LIGHT at 0.7 and two
//         BITMAP_SHINY + 1 at 0.5 and 0.4 in (0.6, 0.8, 1).
//     MODEL_LORD_SCEPTER (:7633-7647): in (1, 0.6, 0.3), BITMAP_LIGHT at 1.3 on bone 1, the
//         pommel, and at (-10, 0, 0), (5, 0, 0) and (20, 0, 0) on bone 2 a BITMAP_LIGHT at 1.3
//         and a BITMAP_SHINY + 1 at 0.4.

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
        Daybreak,
        PlatinaStaff,
        AlbatrossBow,
        SwordDancer,
        Flamberge,
        SwordBreaker,
        ImperialSword,
        FrostMace,
        StingerBow,
        DeadlyStaff,
        ImperialStaff,
        CrimsonGlory,
        SalamanderShield,
        GuardianShield,
        BoneBlade,
        ExplosionBlade,
        SylphWindBow,
        GrandViperStaff,
        ChromaticStaff,
        Beuroba,
        AirLynBow,
        BattleScepter,
        MasterScepter,
        GreatScepter,
        LordScepter,
    };
    // The Stinger Bow's 44 bones, each with its light (ZzzCharacter.cpp:7972-7999).
    static constexpr int kMostPoints = 44;
    static constexpr Item kItems[52] = {Item::SaintCrossbow,   Item::GrandSoulShield,
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
                                        Item::CelestialBow,    Item::Daybreak,
                                        Item::PlatinaStaff,    Item::AlbatrossBow,
                                        Item::SwordDancer,     Item::Flamberge,
                                        Item::SwordBreaker,    Item::ImperialSword,
                                        Item::FrostMace,       Item::StingerBow,
                                        Item::DeadlyStaff,     Item::ImperialStaff,
                                        Item::CrimsonGlory,    Item::SalamanderShield,
                                        Item::GuardianShield,  Item::BoneBlade,
                                        Item::ExplosionBlade,  Item::SylphWindBow,
                                        Item::GrandViperStaff, Item::ChromaticStaff,
                                        Item::Beuroba,         Item::AirLynBow,
                                        Item::BattleScepter,   Item::MasterScepter,
                                        Item::GreatScepter,    Item::LordScepter};

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
