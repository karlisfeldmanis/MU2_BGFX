#include "game/fx/held_lights.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kFrame = 1.0f / 25.0f;  // MU's reference frame
constexpr float kUnit = 0.01f;          // a MU unit in metres
// Ours: a quarter of MU's size and the Staff of Resurrection's fraction of its light
// (fx/staff_fire.cpp). At the staff's tenth of the size the spear's and the crossbow's lights
// were too small to see in a fight at all (shot, 2026-10-06).
constexpr float kSize = 0.25f;
constexpr float kLight = 0.45f;
// MU's CreateSprite scale over a 64-texel sheet, in metres, halved for a half width.
constexpr float halfOf(float scale) { return 0.5f * scale * 64.0f * kUnit * kSize; }

uint32_t roll(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return seed >> 8;
}

// A joint's tail as MU lays it, a band flat across its run and one upright (CreateTailAxis),
// from `from` to `to`, `half` either side, added in `r, g, b` at the held lights' kLight.
void band(gfx::Effects& effects, const float from[3], const float to[3], float half,
          bgfx::TextureHandle sheet, float r, float g, float b) {
    float run[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const float length = std::sqrt(run[0] * run[0] + run[1] * run[1] + run[2] * run[2]);
    if (length < 1e-5f) return;
    for (float& one : run) one /= length;
    float flat[3] = {run[2], 0.0f, -run[0]};
    const float f = std::sqrt(flat[0] * flat[0] + flat[2] * flat[2]);
    if (f < 1e-4f) {
        flat[0] = 1.0f;
        flat[2] = 0.0f;
    } else {
        flat[0] /= f;
        flat[2] /= f;
    }
    const float up[3] = {run[1] * flat[2] - run[2] * flat[1], run[2] * flat[0] - run[0] * flat[2],
                         run[0] * flat[1] - run[1] * flat[0]};
    const float colour[3] = {r, g, b};
    for (const float* across : {static_cast<const float*>(flat), up}) {
        gfx::Sprite sprite;
        sprite.placed = true;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        for (int k = 0; k < 3; ++k) {
            sprite.colour[k] = colour[k] * kLight;
            sprite.corner[0][k] = from[k] - across[k] * half;
            sprite.corner[1][k] = from[k] + across[k] * half;
            sprite.corner[2][k] = to[k] + across[k] * half;
            sprite.corner[3][k] = to[k] - across[k] * half;
            sprite.position[k] = 0.5f * (from[k] + to[k]);
        }
        sprite.colour[3] = 1.0f;
        const float uv[4][2] = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
        for (int c = 0; c < 4; ++c) {
            sprite.cornerUv[c][0] = uv[c][0];
            sprite.cornerUv[c][1] = uv[c][1];
        }
        effects.add(sprite);
    }
}

// The Heliacal Sword's ground light: MU's (0.8, 0.64, 0.4) over three tiles (ZzzCharacter.cpp:
// 5659-5663), at full -- MU's own colour, as the wizard's Flame's is (fx/flame.h). A tile is a
// metre here. Hung at the hilt, which a hand holds about a metre up (gfx::PointLight::height).
constexpr float kHeliacalLight[3] = {0.8f, 0.64f, 0.4f};
constexpr float kHeliacalReach = 3.0f;
constexpr float kHeliacalHeight = 1.0f;

}  // namespace

// And the Archangel's three: MU's (0.8, 0.5, 0.3) at its 0.8 over two tiles (ZzzCharacter.cpp:
// 5664-5667), from the grip.
constexpr float kArchangelLight[3] = {0.64f, 0.4f, 0.24f};
constexpr float kArchangelReach = 2.0f;

uint32_t HeldLights::lights(gfx::PointLight* out, uint32_t max) const {
    uint32_t count = 0;
    for (const Held& one : held_) {
        const bool heliacal = one.item == Item::HeliacalSword;
        const bool archangel = one.item == Item::DivineSword || one.item == Item::DivineStaff ||
                               one.item == Item::DivineCrossbow;
        if ((!heliacal && !archangel) || one.count < 1 || count >= max) continue;
        // The grip is each one's last point.
        const float* grip = one.at[one.count - 1];
        gfx::PointLight& light = out[count++];
        light = gfx::PointLight{};
        for (int k = 0; k < 3; ++k) {
            light.position[k] = grip[k];
            light.colour[k] = heliacal ? kHeliacalLight[k] : kArchangelLight[k];
        }
        light.reach = heliacal ? kHeliacalReach : kArchangelReach;
        light.height = kHeliacalHeight;
    }
    return count;
}

const char* HeldLights::mesh(Item item) {
    switch (item) {
        case Item::SaintCrossbow: return "CrossBow17";
        case Item::GrandSoulShield: return "Shield16";
        case Item::DragonSpear: return "Spear11";
        case Item::DragonSoulStaff: return "Staff10";
        case Item::ElementalMace: return "Mace08";
        case Item::GreatReignCrossbow: return "CrossBow20";
        case Item::ThunderBlade: return "Sword19";
        case Item::LegendaryShield: return "Shield15";
        case Item::DarkBreaker: return "Sword18";
        case Item::KnightBlade: return "Sword21";
        case Item::StaffOfKundun: return "Staff12";
        case Item::ArrowViperBow: return "Bow20";
        case Item::GorgonStaff: return "Staff05";
        case Item::HeliacalSword: return "Sword13";
        case Item::DivineSword: return "Sword20";
        case Item::DivineStaff: return "Staff11";
        case Item::DivineCrossbow: return "Bow19";
        case Item::CrystalSword: return "Mace06";
        case Item::ChaosDragonAxe: return "Mace07";
        case Item::LegendaryStaff: return "Staff06";
        case Item::ChaosLightningStaff: return "Staff08";
        case Item::BluewingCrossbow: return "CrossBow06";
        case Item::AquagoldCrossbow: return "CrossBow07";
        case Item::TigerBow: return "Bow05";
        case Item::SilverBow: return "Bow06";
        case Item::ChaosNatureBow: return "Bow07";
        case Item::CelestialBow: return "Bow18";
        case Item::Daybreak: return "Sword25";
        case Item::PlatinaStaff: return "Staff14";
        case Item::AlbatrossBow: return "Bow23";
        case Item::SwordDancer: return "Sword26";
        case Item::Flamberge: return "Sword27";
        case Item::SwordBreaker: return "Sword28";
        case Item::ImperialSword: return "Sword29";
        case Item::FrostMace: return "Mace17";
        case Item::StingerBow: return "Bow24";
        case Item::DeadlyStaff: return "Staff31";
        case Item::ImperialStaff: return "Staff32";
        case Item::CrimsonGlory: return "Shield18";
        case Item::SalamanderShield: return "Shield19";
        case Item::GuardianShield: return "Shield21";
        case Item::BoneBlade: return "Sword23";
        case Item::ExplosionBlade: return "Sword24";
        case Item::SylphWindBow: return "Bow22";
        case Item::GrandViperStaff: return "Staff13";
    }
    return "";
}

int HeldLights::points(Item item, float out[kMostPoints][3]) {
    // Each glb is MU's model space in metres. A rig bone at (x, y, z) in the .bmd's rest pose
    // stands at (x, z, -y) in it -- worked out against the Staff of Resurrection's swirl cards,
    // whose bones carry vertices; these bones carry none.
    switch (item) {
        case Item::SaintCrossbow:
            // Ours: six lights 20 apart down the stock from the grip, 12 over its spine (the
            // stock runs the glb's -y; MU's (0, -10, -20 j) is in the hand's frame).
            for (int j = 0; j < 6; ++j) {
                out[j][0] = 0.0f;
                out[j][1] = -20.0f * float(j) * kUnit;
                out[j][2] = 12.0f * kUnit;
            }
            return 6;
        case Item::GrandSoulShield:
            // (15, -15, 0) on Bone02, from Shield16.bmd's rest: (26.3, -3.4, -19.3).
            out[0][0] = 26.3f * kUnit;
            out[0][1] = -19.3f * kUnit;
            out[0][2] = 3.4f * kUnit;
            return 1;
        case Item::DragonSpear: {
            // Bone02 to Bone09 of Spear11.bmd at rest, along the back of the dragon's swirl.
            static const float kBones[8][2] = {{9.3f, 224.1f},  {-9.1f, 216.7f},
                                               {-23.3f, 203.5f}, {-28.2f, 186.8f},
                                               {-27.7f, 169.2f}, {-26.0f, 152.0f},
                                               {-24.7f, 133.4f}, {-23.0f, 114.7f}};
            for (int i = 0; i < 8; ++i) {
                out[i][0] = 0.0f;
                out[i][1] = kBones[i][0] * kUnit;
                out[i][2] = kBones[i][1] * kUnit;
            }
            return 8;
        }
        case Item::DragonSoulStaff:
            // Ours: the hand's (0, -120, 5) and (0, 100, 10) at the head and the foot, as the
            // Staff of Resurrection's (0, -145, 0) is its glb's (0, 0, 1.45) (fx/staff_fire).
            out[0][0] = out[0][1] = 0.0f;
            out[0][2] = 120.0f * kUnit;
            out[1][0] = out[1][1] = 0.0f;
            out[1][2] = -100.0f * kUnit;
            return 2;
        case Item::ElementalMace:
            // Bone02 of Mace08.bmd at rest, in the head.
            out[0][0] = 0.0f;
            out[0][1] = 2.6f * kUnit;
            out[0][2] = 80.9f * kUnit;
            return 1;
        case Item::GreatReignCrossbow: {
            // (0, 0, 10) on Bone02 to Bone06 of CrossBow20.bmd at rest: the four limb tips and
            // the nose.
            static const float kBones[5][3] = {{59.7f, -41.9f, 25.2f},  {-67.2f, -40.4f, 25.2f},
                                               {92.1f, -59.6f, 5.0f},   {-99.1f, -58.1f, 5.0f},
                                               {-2.4f, -119.5f, 21.0f}};
            for (int i = 0; i < 5; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 5;
        }
        case Item::ThunderBlade:
            // Ours: the hand's (0, -20, 15) as 20 up the blade from the grip, at its guard, as
            // the Staff of Resurrection's (0, -145, 0) is its glb's (0, 0, 1.45); and the
            // thunder's end, (0, -133, 7), as 133 up it, short of the tip at 145.
            out[0][0] = out[0][1] = 0.0f;
            out[0][2] = 20.0f * kUnit;
            out[1][0] = out[1][1] = 0.0f;
            out[1][2] = 133.0f * kUnit;
            return 2;
        case Item::GorgonStaff:
            // The hand's (0, -90, 0) as 90 up the staff, under its head (the model reaches 138).
            out[0][0] = out[0][1] = 0.0f;
            out[0][2] = 90.0f * kUnit;
            return 1;
        case Item::HeliacalSword:
            // The tip, MU's (0, -160, 0), 160 up a blade that reaches 177; and the hilt, where
            // its ground light hangs.
            out[0][0] = out[0][1] = 0.0f;
            out[0][2] = 160.0f * kUnit;
            out[1][0] = out[1][1] = out[1][2] = 0.0f;
            return 2;
        case Item::DivineSword: {
            // Bone03, 04, 02, 05, 06, 07 and 08 of Sword20.bmd at rest -- MU's bones 2 to 8 in
            // its order -- up the blade (muextract export-rig); and the hilt, for its ground light.
            static const float kAlong[7] = {51.7f, 83.6f, 18.2f, -6.4f, 115.5f, 146.5f, 184.2f};
            for (int i = 0; i < 7; ++i) {
                out[i][0] = 0.0f;
                out[i][1] = 4.3f * kUnit;
                out[i][2] = kAlong[i] * kUnit;
            }
            out[7][0] = out[7][1] = out[7][2] = 0.0f;
            return 8;
        }
        case Item::DivineStaff:
            // (i*30 - 180, -40, 0) on Staff11.bmd's bone 0, Box01, at rest: ten 30 apart down the
            // staff from -127.5 to 142.5 (muextract export-rig); and the grip, for its ground light.
            for (int i = 0; i < 10; ++i) {
                out[i][0] = 0.0f;
                out[i][1] = 0.9f * kUnit;
                out[i][2] = (float(i) * 30.0f - 127.5f) * kUnit;
            }
            out[10][0] = out[10][1] = out[10][2] = 0.0f;
            return 11;
        case Item::DivineCrossbow:
            // Its grip, for its ground light alone: MuMain comments its sprites out.
            out[0][0] = out[0][1] = out[0][2] = 0.0f;
            return 1;
        case Item::CrystalSword:
            // The hand's (0, -j*20 - 30, 0) as j*20 + 30 up the blade (it reaches 194).
            for (int j = 0; j < 8; ++j) {
                out[j][0] = out[j][1] = 0.0f;
                out[j][2] = (float(j) * 20.0f + 30.0f) * kUnit;
            }
            return 8;
        case Item::ChaosDragonAxe: {
            // (0, -84, 0) in the hand, then (0, -j*20 - 10, 0) for five, up the haft; and Box02
            // and Box04, its bones 2 and 6, either side of the head.
            out[0][0] = out[0][1] = 0.0f;
            out[0][2] = 84.0f * kUnit;
            for (int j = 0; j < 5; ++j) {
                out[1 + j][0] = out[1 + j][1] = 0.0f;
                out[1 + j][2] = (float(j) * 20.0f + 10.0f) * kUnit;
            }
            const float kBones[2][3] = {{0.0f, 28.2f, 77.0f}, {0.0f, -27.6f, 76.9f}};
            for (int i = 0; i < 2; ++i) {
                for (int k = 0; k < 3; ++k) out[6 + i][k] = kBones[i][k] * kUnit;
            }
            return 8;
        }
        case Item::LegendaryStaff:
            // The hand's (0, -145, 0), the head (it reaches 156).
            out[0][0] = out[0][1] = 0.0f;
            out[0][2] = 145.0f * kUnit;
            return 1;
        case Item::ChaosLightningStaff: {
            // `light` to `light10`, its bones 27 to 37 at rest: the head's star and ten round the
            // two ends.
            static const float kBones[11][3] = {
                {2.9f, -4.0f, 96.3f},    {-23.3f, -4.0f, 130.3f}, {33.0f, -4.0f, 73.5f},
                {-29.5f, -4.0f, 74.4f},  {27.8f, -4.0f, 130.5f},  {6.7f, -4.0f, 174.8f},
                {-4.0f, -4.0f, 175.4f},  {-3.2f, -4.0f, -130.3f}, {6.8f, -4.0f, -130.6f},
                {19.8f, -4.0f, -77.5f},  {-15.3f, -4.0f, -78.0f}};
            for (int i = 0; i < 11; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 11;
        }
        case Item::BluewingCrossbow:
        case Item::AquagoldCrossbow:
            // Ours, as the Saint Crossbow's: MU's (0, -20, -j*20) laid down the stock, which runs
            // the glb's -y, 20 over its spine.
            for (int j = 0; j < 6; ++j) {
                out[j][0] = 0.0f;
                out[j][1] = -20.0f * float(j) * kUnit;
                out[j][2] = 20.0f * kUnit;
            }
            return 6;
        case Item::TigerBow:
        case Item::SilverBow:
            // Bone03 and Bone07, its bones 2 and 6: the two limb tips.
            out[0][0] = 0.0f;
            out[0][1] = -1.8f * kUnit;
            out[0][2] = -44.4f * kUnit;
            out[1][0] = 0.0f;
            out[1][1] = -1.0f * kUnit;
            out[1][2] = 59.8f * kUnit;
            return 2;
        case Item::ChaosNatureBow: {
            // `light` to `light06`, its bones 13 to 18.
            static const float kBones[6][3] = {{2.1f, 35.5f, 96.5f},  {2.1f, 6.9f, 140.9f},
                                               {2.1f, 6.4f, -127.4f}, {2.1f, 36.6f, -83.5f},
                                               {2.5f, -24.9f, 45.8f}, {2.5f, -25.2f, -32.3f}};
            for (int i = 0; i < 6; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 6;
        }
        case Item::CelestialBow: {
            // Its bones 13 to 18 and 5 to 8: Bone16 to 19, 10 and 11, then 12 to 15.
            static const float kBones[10][3] = {
                {-15.5f, 18.2f, 117.1f}, {15.3f, 18.2f, 117.1f}, {9.3f, 40.7f, 78.0f},
                {-10.1f, 40.7f, 78.0f},  {0.0f, -46.6f, 6.2f},   {0.0f, 18.2f, 5.8f},
                {10.3f, 39.6f, -60.1f},  {-10.0f, 39.6f, -60.1f}, {15.2f, 18.2f, -98.3f},
                {-15.5f, 18.2f, -98.3f}};
            for (int i = 0; i < 10; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 10;
        }
        case Item::LegendaryShield:
            // Ours: the hand's (20, 0, 0) as 20 out of the grip along the glb's x, on the face.
            out[0][0] = 20.0f * kUnit;
            out[0][1] = out[0][2] = 0.0f;
            return 1;
        case Item::DarkBreaker: {
            // The hand's (0, -y, z) as the glb's (0, z, y): up the blade, and across its width.
            static const float kEnds[4][2] = {{-40.0f, 20.0f}, {-10.0f, 160.0f},
                                              {28.0f, 10.0f},  {18.0f, 145.0f}};
            for (int i = 0; i < 4; ++i) {
                out[i][0] = 0.0f;
                out[i][1] = kEnds[i][0] * kUnit;
                out[i][2] = kEnds[i][1] * kUnit;
            }
            return 4;
        }
        case Item::KnightBlade:
            // Bone04 and Bone05 of Sword21.bmd at rest, either side of the guard.
            out[0][0] = 6.4f * kUnit;
            out[0][1] = -4.4f * kUnit;
            out[0][2] = 38.6f * kUnit;
            out[1][0] = -7.0f * kUnit;
            out[1][1] = -4.4f * kUnit;
            out[1][2] = 38.6f * kUnit;
            return 2;
        case Item::StaffOfKundun: {
            // Bone05, Bone06 and Bone08 of Staff12.bmd at rest: the head's two eyes and the foot.
            static const float kBones[3][3] = {
                {5.6f, -7.6f, 95.1f}, {-5.5f, -7.6f, 95.1f}, {0.0f, 1.0f, -68.8f}};
            for (int i = 0; i < 3; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 3;
        }
        case Item::ArrowViperBow: {
            // Bone07 and Bone03 of Bow20.bmd at rest, the limb tips (MU's bones 6 and 2), and
            // Bone19 (MU's 9) at the grip; MU's (10, 0, 0) off it is left out.
            static const float kBones[3][3] = {
                {0.0f, -1.1f, 59.8f}, {0.0f, -1.9f, -44.4f}, {0.0f, -12.0f, 6.9f}};
            for (int i = 0; i < 3; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 3;
        }
        case Item::Daybreak:
            // zx01 (bone 2) of CW_Sword.bmd at rest, at the guard; MU's (0, 0, +-3) off it are
            // left out.
            out[0][0] = 0.0f;
            out[0][1] = 5.1f * kUnit;
            out[0][2] = 34.3f * kUnit;
            return 1;
        case Item::PlatinaStaff: {
            // CW_Staff.bmd at rest: zx01 (bone 9, the head), zx03, zx02, zx05, zx04 (5-8, round
            // the head's gem), zx12, zx09, zx11, zx10 (10-13, the four prongs), zx06 (4, down
            // the shaft) and zx08 (2, the foot).
            static const float kBones[11][3] = {
                {0.0f, 0.0f, 106.0f},   {0.0f, 3.4f, 78.3f},    {0.0f, 3.4f, 87.0f},
                {0.0f, -3.1f, 78.3f},   {0.0f, -3.1f, 87.0f},   {22.0f, 1.1f, 123.9f},
                {0.0f, 30.7f, 110.0f},  {-22.9f, 1.1f, 123.9f}, {0.0f, -30.7f, 110.0f},
                {0.0f, 0.0f, 42.3f},    {0.0f, 0.0f, -99.5f}};
            for (int i = 0; i < 11; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 11;
        }
        case Item::AlbatrossBow: {
            // CW_Bow.bmd at rest: zx08 (bone 10) and zx11 (28), the limb tips, and Bone34 (16)
            // and Bone28 (34), either side of the grip.
            static const float kBones[4][3] = {
                {0.0f, 42.4f, 117.1f}, {0.0f, 40.8f, -104.1f},
                {0.0f, -9.2f, 48.4f},  {0.0f, -12.9f, -37.2f}};
            for (int i = 0; i < 4; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 4;
        }
        case Item::SwordDancer: {
            // CW_Sword2.bmd at rest: bone 1, then bones 2-6 up the blade.
            static const float kBones[6][3] = {{0.0f, 4.3f, -31.6f}, {0.0f, 4.3f, 33.1f}, {0.0f, 4.3f, 62.6f}, {-1.3f, 3.8f, 91.5f}, {0.0f, 6.2f, 121.2f}, {0.0f, 13.2f, 158.1f}};
            for (int i = 0; i < 6; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 6;
        }
        case Item::Flamberge: {
            // Sword_27.bmd at rest: Gold01 (bone 11), b01 and n02 (12, 13), then Zx01-Zx07 (1-7) up the blade.
            static const float kBones[10][3] = {{0.0f, 0.0f, -34.5f}, {6.8f, 0.0f, 17.7f}, {-6.8f, 1.1f, 17.7f}, {0.0f, 0.0f, 34.3f}, {0.0f, 0.0f, 51.7f}, {0.0f, 0.0f, 68.9f}, {0.0f, 0.0f, 86.2f}, {0.0f, 0.0f, 102.9f}, {0.0f, 0.0f, 120.8f}, {0.0f, 0.0f, 136.6f}};
            for (int i = 0; i < 10; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 10;
        }
        case Item::SwordBreaker: {
            // Sword_28.bmd at rest: Zx01-Zx07 (bones 1-7) up the blade.
            static const float kBones[7][3] = {{0.0f, 0.6f, 21.0f}, {0.0f, -1.6f, 44.3f}, {0.0f, -8.6f, 68.7f}, {0.0f, -6.9f, 88.4f}, {0.0f, -5.3f, 110.3f}, {0.0f, -4.3f, 133.5f}, {0.0f, -2.1f, 155.8f}};
            for (int i = 0; i < 7; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 7;
        }
        case Item::ImperialSword: {
            // Sword_29.bmd at rest: Zx01-Zx04 (bones 8-11), the guard and the tip, then rx01-Zx06 (2-7) up the blade.
            static const float kBones[10][3] = {{10.1f, -0.3f, 30.4f}, {-10.1f, -0.3f, 30.4f}, {3.7f, -0.3f, 148.9f}, {-3.5f, -0.3f, 148.9f}, {0.0f, -0.3f, 49.1f}, {0.0f, -0.3f, 74.4f}, {0.0f, -0.3f, 101.7f}, {0.0f, -0.3f, 125.9f}, {0.0f, -0.3f, 148.8f}, {0.0f, -0.3f, 168.4f}};
            for (int i = 0; i < 10; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 10;
        }
        case Item::FrostMace: {
            // Mace_17.bmd at rest (its loop moves them): bone 24, the crystal; 1; 8-11; then 12-23 up the spike.
            static const float kBones[18][3] = {{0.0f, 0.0f, 97.0f}, {0.0f, 0.0f, -31.6f}, {1.6f, 0.2f, -14.6f}, {-1.6f, 0.2f, -14.7f}, {3.5f, 0.2f, 12.9f}, {-2.9f, 0.2f, 12.9f}, {0.0f, 0.2f, 26.3f}, {0.0f, 0.2f, 31.2f}, {0.0f, 0.2f, 36.2f}, {0.0f, 0.2f, 41.1f}, {0.0f, 0.2f, 46.1f}, {0.0f, 0.2f, 51.0f}, {0.0f, 0.2f, 56.0f}, {0.0f, 0.2f, 60.9f}, {0.0f, 0.2f, 65.9f}, {0.0f, 0.2f, 70.8f}, {0.0f, 0.2f, 75.8f}, {0.0f, 0.2f, 80.7f}};
            for (int i = 0; i < 18; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 18;
        }
        case Item::StingerBow: {
            // Bow_24.bmd at rest: every bone, 0-43.
            static const float kBones[44][3] = {{0.0f, -2.8f, 0.1f}, {0.0f, -8.7f, -0.1f}, {0.0f, 4.8f, -39.2f}, {0.0f, 25.0f, -80.5f}, {0.0f, 41.0f, -97.4f}, {0.1f, 29.2f, -91.1f}, {6.4f, -5.7f, -20.6f}, {20.0f, 10.4f, -34.3f}, {29.9f, 24.1f, -44.6f}, {5.1f, -7.1f, -17.2f}, {20.1f, 2.0f, -18.8f}, {39.9f, 17.9f, -21.7f}, {7.2f, -8.6f, -12.6f}, {16.5f, -0.7f, -6.1f}, {22.1f, 5.9f, -0.4f}, {-7.7f, -8.2f, -12.3f}, {-16.9f, -2.1f, -5.6f}, {-23.6f, 5.3f, 0.4f}, {-7.1f, -5.5f, -17.2f}, {-27.1f, 4.2f, -21.0f}, {-46.2f, 19.2f, -25.3f}, {-6.9f, -4.8f, -17.8f}, {-26.8f, 10.5f, -31.8f}, {-41.8f, 23.3f, -43.0f}, {0.6f, 0.3f, -20.8f}, {0.0f, -8.7f, -0.1f}, {0.0f, 7.6f, 48.2f}, {0.0f, 25.6f, 85.5f}, {0.0f, 40.6f, 102.6f}, {-0.1f, 28.8f, 94.8f}, {-6.7f, -6.5f, 23.3f}, {-16.6f, 1.1f, 25.3f}, {-33.8f, 17.3f, 31.1f}, {-6.1f, -5.3f, 27.0f}, {-14.1f, 4.0f, 35.1f}, {-30.1f, 31.1f, 52.9f}, {6.9f, -7.9f, 23.3f}, {18.3f, 2.6f, 25.7f}, {34.0f, 20.0f, 31.7f}, {5.8f, -5.5f, 26.7f}, {13.8f, 5.1f, 34.2f}, {32.4f, 30.6f, 51.9f}, {-0.6f, -0.9f, 23.5f}, {0.0f, 28.8f, 1.8f}};
            for (int i = 0; i < 44; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 44;
        }
        case Item::DeadlyStaff: {
            // Staff_31.bmd at rest: Red01 and Red02 (bones 2, 3), the eyes, then Rx01-Zx05 (4-9).
            static const float kBones[8][3] = {{6.4f, 0.1f, 98.5f}, {-6.2f, 0.1f, 98.5f}, {0.0f, 0.3f, 118.4f}, {0.0f, 0.3f, 135.5f}, {0.0f, 0.3f, -84.6f}, {0.0f, 0.3f, 82.2f}, {0.0f, 15.0f, 77.8f}, {0.0f, -14.9f, 77.8f}};
            for (int i = 0; i < 8; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 8;
        }
        case Item::ImperialStaff: {
            // Staff_32.bmd at rest: Zx01-Zx04 (bones 2-5).
            static const float kBones[4][3] = {{0.0f, -0.7f, -87.2f}, {0.0f, -0.7f, 63.9f}, {0.0f, -0.7f, 128.1f}, {0.0f, -0.7f, 158.8f}};
            for (int i = 0; i < 4; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 4;
        }
        case Item::CrimsonGlory: {
            // Shield_18.bmd at rest: zx01, zx02 and zx002 (bones 1-3), the flares, then zx03-zx09
            // (4-10) round the rim.
            static const float kBones[10][3] = {{20.8f, 39.7f, -23.5f}, {20.0f, 19.9f, -2.9f}, {20.0f, 12.1f, -16.0f}, {11.7f, 56.2f, 0.7f}, {12.6f, 24.8f, 13.8f}, {12.3f, -12.8f, 26.1f}, {12.2f, -46.0f, 28.0f}, {12.3f, -28.8f, -0.6f}, {12.6f, -0.2f, -28.0f}, {11.7f, 26.1f, -49.5f}};
            for (int i = 0; i < 10; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 10;
        }
        case Item::SalamanderShield:
            // zx01 (bone 1) of Shield_19.bmd at rest, the gem: (27.2, 0, 0.6).
            out[0][0] = 27.2f * kUnit;
            out[0][1] = 0.6f * kUnit;
            out[0][2] = 0.0f;
            return 1;
        case Item::GuardianShield: {
            // Shield_21.bmd at rest: b01, zx01, zx05, zx04, Object04, Object05, zx02, Object01 and
            // Object03 (bones 1-9), the crystals.
            static const float kBones[9][3] = {{17.1f, -27.1f, 14.4f}, {15.3f, -1.3f, 14.7f}, {17.5f, -20.5f, 5.0f}, {15.3f, -12.9f, -7.1f}, {14.2f, 33.3f, 8.7f}, {14.2f, 11.6f, -32.3f}, {17.5f, -15.6f, 14.2f}, {14.4f, 53.4f, -12.5f}, {14.4f, 40.4f, -37.0f}};
            for (int i = 0; i < 9; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 9;
        }
        case Item::BoneBlade: {
            // Box01 (bone 0) of HDK_Sword.bmd at rest, MU's (+-5, -22, -10) off it: the guard.
            static const float kBones[2][3] = {{5.0f, -10.0f, 23.7f}, {-5.0f, -10.0f, 23.7f}};
            for (int i = 0; i < 2; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 2;
        }
        case Item::ExplosionBlade: {
            // HDK_Sword2.bmd at rest: Bone05 (bone 4), 30 out from it, then Bone07-Bone09 (6-8).
            static const float kBones[5][3] = {{0.0f, -2.1f, 58.3f}, {0.0f, -2.1f, 88.3f}, {0.0f, -4.2f, 118.5f}, {0.0f, -4.4f, 149.2f}, {0.0f, -1.7f, 176.0f}};
            for (int i = 0; i < 5; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 5;
        }
        case Item::SylphWindBow: {
            // HDK_Bow.bmd at rest: zx03 and zx06 (bones 7, 15), the limb tips, then 20 out from
            // Bone10 (bone 0), the grip.
            static const float kBones[3][3] = {{0.0f, 38.2f, 133.0f}, {0.0f, 38.2f, -124.8f}, {0.0f, -20.0f, 6.3f}};
            for (int i = 0; i < 3; ++i) {
                for (int k = 0; k < 3; ++k) out[i][k] = kBones[i][k] * kUnit;
            }
            return 3;
        }
        case Item::GrandViperStaff:
            // Object01 (bone 0) of HDK_Staff.bmd at rest, 170 along it: the serpent's head.
            out[0][0] = 0.0f;
            out[0][1] = 0.2f * kUnit;
            out[0][2] = 170.3f * kUnit;
            return 1;
    }
    return 0;
}

bool HeldLights::open(const std::string& assetDir, content::Textures& textures,
                      const content::Showing& table) {
    shutdown();
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("held lights: no cooked effect named '%s'", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    light_ = take("light");
    shiny_ = take("shiny_02");
    flare_ = take("pierce");
    thunder_ = take("joint_thunder");
    lightning_ = take("lightning_2");
    open_ = bgfx::isValid(light_);
    return open_;
}

void HeldLights::shutdown() {
    held_.clear();
    open_ = false;
}

void HeldLights::update(float seconds) {
    held_.clear();
    clock_ = std::fmod(clock_ + seconds, 3600.0f);
    step_ -= seconds;
    if (step_ <= 0.0f) {
        step_ += kFrame;
        if (step_ < 0.0f) step_ = kFrame;
        ++crackleFrame_;
        handLuminosity_ = float(roll(seed_) % 30u + 70u) * 0.01f;
        linkLuminosity_ = float(roll(seed_) % 30u + 70u) * 0.005f;
    }
}

void HeldLights::feed(Item item, const float at[][3], int count) {
    if (!open_) return;
    Held one;
    one.item = item;
    one.count = std::min(count, kMostPoints);
    for (int i = 0; i < one.count; ++i) {
        for (int k = 0; k < 3; ++k) one.at[i][k] = at[i][k];
    }
    held_.push_back(one);
}

void HeldLights::gather(gfx::Effects& effects) const {
    if (!open_) return;
    const auto put = [&](bgfx::TextureHandle sheet, const float at[3], float half,
                         float r, float g, float b) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = at[k];
        sprite.halfWidth = sprite.halfHeight = half;
        sprite.colour[0] = r * kLight;
        sprite.colour[1] = g * kLight;
        sprite.colour[2] = b * kLight;
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };
    for (const Held& one : held_) {
        switch (one.item) {
            case Item::SaintCrossbow: {
                const float l = handLuminosity_;
                for (int j = 0; j < one.count; ++j) {
                    put(light_, one.at[j], halfOf(2.0f), 0.4f * l, 0.6f * l, l);
                }
                break;
            }
            case Item::GrandSoulShield: {
                const float l = linkLuminosity_;
                put(shiny_, one.at[0], halfOf(1.5f), 0.6f * l, 0.6f * l, 2.0f * l);
                put(light_, one.at[0], halfOf(l + 1.5f), 0.6f * l, 0.6f * l, 2.0f * l);
                break;
            }
            case Item::DragonSpear: {
                const float l = linkLuminosity_;
                for (int i = 0; i < one.count; ++i) {
                    put(light_, one.at[i], halfOf(1.3f), 0.2f * l, 0.1f * l, 0.8f * l);
                }
                break;
            }
            case Item::DragonSoulStaff: {
                const float l = handLuminosity_;
                put(shiny_, one.at[0], halfOf(1.5f), 0.6f * l, 0.6f * l, 2.0f * l);
                put(light_, one.at[0], halfOf(l + 1.0f), 0.6f * l, 0.6f * l, 2.0f * l);
                put(light_, one.at[1], halfOf(l + 1.0f), 0.6f * l, 0.6f * l, 2.0f * l);
                break;
            }
            case Item::ElementalMace: {
                const float l = linkLuminosity_;
                put(light_, one.at[0], halfOf(2.0f), l, 0.9f * l, 0.0f);
                // MU's second light, sin(WorldTime*0.002) + 0.5 across: nothing while negative.
                const float grey = std::sin(clock_ * 2.0f) + 0.5f;
                if (grey > 0.0f) put(light_, one.at[0], halfOf(grey), 0.5f, 0.5f, 0.5f);
                break;
            }
            case Item::GreatReignCrossbow: {
                const float l = linkLuminosity_;
                for (int i = 0; i < one.count; ++i) {
                    put(shiny_, one.at[i], halfOf(1.0f), 0.5f * l, 0.5f * l, 0.8f * l);
                    if (i == 4) {
                        put(light_, one.at[i], halfOf(2.0f), 1.0f, 1.0f, 1.0f);
                    } else {
                        put(light_, one.at[i], halfOf(2.0f), 0.5f * l, 0.5f * l, 0.8f * l);
                    }
                }
                break;
            }
            case Item::ThunderBlade: {
                const float s = std::sin(clock_ * 4.0f) * 0.3f + 0.3f;
                put(shiny_, one.at[0], halfOf(s + 1.0f), 0.2f * s, 0.2f * s, s);
                if (one.count < 2 || !bgfx::isValid(thunder_)) break;
                // Three joints, each a line of ten steps from the guard to the end, every inner
                // step nudged up to four units across (MU's x and y, the ground's two axes), at
                // S*20 + 20 wide. Seeded by the frame and the blade, so it crackles at MU's 25 a
                // second and two blades do not crackle alike.
                uint32_t seed = crackleFrame_ * 2654435761u ^
                                uint32_t(std::lround(one.at[0][0] * 1000.0f));
                // At the held lights' quarter (kSize): at MU's full width the three lay as two
                // solid blue bars across the blade (shot, 2026-10-10).
                const float half = (s * 20.0f + 20.0f) * kUnit * 0.5f * kSize;
                const float* a = one.at[0];
                const float* b = one.at[1];
                for (int joint = 0; joint < 3; ++joint) {
                    float from[3] = {a[0], a[1], a[2]};
                    for (int step = 1; step <= 10; ++step) {
                        float to[3];
                        for (int k = 0; k < 3; ++k) to[k] = a[k] + (b[k] - a[k]) * float(step) / 10.0f;
                        if (step < 10) {
                            to[0] += (float(roll(seed) % 8u) - 4.0f) * kUnit;
                            to[2] += (float(roll(seed) % 8u) - 4.0f) * kUnit;
                        }
                        band(effects, from, to, half, thunder_, 0.3f, 0.3f, 1.0f);
                        for (int k = 0; k < 3; ++k) from[k] = to[k];
                    }
                }
                break;
            }
            case Item::GorgonStaff: {
                const float l = handLuminosity_;
                put(shiny_, one.at[0], halfOf(2.0f), 0.4f * l, 0.8f * l, 0.6f * l);
                break;
            }
            case Item::HeliacalSword: {
                const float l = handLuminosity_;
                put(light_, one.at[0], halfOf(1.4f), l, l, l);
                break;
            }
            case Item::DivineSword:
            case Item::DivineStaff: {
                // Orange lights, and MU's one-in-three grey spark on each, rolled per reference
                // frame and point (rand_fps_check(3)).
                const float l = linkLuminosity_;
                for (int i = 0; i < one.count - 1; ++i) {
                    put(light_, one.at[i], halfOf(2.0f), l, 0.3f * l, 0.1f * l);
                    uint32_t h = (crackleFrame_ * 2654435761u) ^ uint32_t(i * 40503) ^
                                 uint32_t(std::lround(one.at[i][0] * 1000.0f));
                    h ^= h >> 15;
                    h *= 2246822519u;
                    h ^= h >> 13;
                    if (h % 3u == 0u) put(shiny_, one.at[i], halfOf(0.6f), 0.4f, 0.4f, 0.4f);
                }
                break;
            }
            case Item::DivineCrossbow:
                break;
            case Item::CrystalSword: {
                const float l = handLuminosity_;
                for (int j = 0; j < one.count; ++j) {
                    // MU's rand() % 4 < 3, rolled per reference frame and light.
                    uint32_t h = (crackleFrame_ * 2246822519u) ^ uint32_t(j * 2654435761u);
                    h ^= h >> 13;
                    h *= 2654435761u;
                    h ^= h >> 16;
                    if (h % 4u < 3u) put(light_, one.at[j], halfOf(1.0f), l, 0.6f * l, 0.4f * l);
                }
                break;
            }
            case Item::ChaosDragonAxe: {
                const float s = std::sin(clock_ * 4.0f) * 0.3f + 0.7f;
                put(shiny_, one.at[0], halfOf(s + 1.5f), s, 0.2f * s, 0.1f * s);
                for (int j = 1; j < 6 && j < one.count; ++j) {
                    put(shiny_, one.at[j], halfOf(1.0f), s, 0.2f * s, 0.1f * s);
                }
                for (int j = 6; j < one.count; ++j) {
                    put(shiny_, one.at[j], halfOf(1.0f), 0.5f * s, 0.1f * s, 0.05f * s);
                }
                break;
            }
            case Item::LegendaryStaff: {
                const float l = handLuminosity_;
                put(shiny_, one.at[0], halfOf(1.5f), 0.4f * l, 0.6f * l, l);
                put(lightning_, one.at[0], halfOf(0.3f), 0.4f * l, 0.6f * l, l);
                break;
            }
            case Item::ChaosLightningStaff: {
                const float l = handLuminosity_;
                put(shiny_, one.at[0], halfOf(2.0f), 0.4f * l, 0.6f * l, l);
                for (int j = 1; j < one.count; ++j) {
                    put(light_, one.at[j], halfOf(1.5f), 0.4f * l, 0.6f * l, l);
                }
                break;
            }
            case Item::BluewingCrossbow:
            case Item::AquagoldCrossbow: {
                const float l = handLuminosity_;
                const bool blue = one.item == Item::BluewingCrossbow;
                for (int j = 0; j < one.count; ++j) {
                    put(light_, one.at[j], halfOf(2.0f), (blue ? 0.2f : 0.6f) * l, 0.4f * l,
                        (blue ? 0.6f : 0.2f) * l);
                }
                break;
            }
            case Item::TigerBow:
            case Item::SilverBow:
            case Item::ChaosNatureBow:
            case Item::CelestialBow: {
                const float l = linkLuminosity_;
                const float r = one.item == Item::ChaosNatureBow ? 0.6f
                                : one.item == Item::CelestialBow ? 0.5f
                                                                 : 1.0f;
                const float g = one.item == Item::ChaosNatureBow ? 1.0f
                                : one.item == Item::CelestialBow ? 0.5f
                                                                 : 0.6f;
                const float b = one.item == Item::ChaosNatureBow ? 0.8f
                                : one.item == Item::CelestialBow ? 0.8f
                                                                 : 0.2f;
                for (int j = 0; j < one.count; ++j) {
                    put(shiny_, one.at[j], halfOf(1.0f), r * l, g * l, b * l);
                }
                break;
            }
            case Item::LegendaryShield: {
                const float l = handLuminosity_;
                put(shiny_, one.at[0], halfOf(1.5f), 0.4f * l, 0.6f * l, 1.5f * l);
                break;
            }
            case Item::DarkBreaker: {
                if (!bgfx::isValid(flare_) || one.count < 4) break;
                // Two crossed faces a streak, as MU's joint lays a band across and one up.
                const float half = (std::sin(clock_ * 4.0f) * 10.0f + 20.0f) * kUnit * 0.5f;
                for (int s = 0; s < 2; ++s) {
                    const float* a = one.at[2 * s];
                    const float* b = one.at[2 * s + 1];
                    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
                    const float flat = std::sqrt(d[0] * d[0] + d[2] * d[2]);
                    const float across[2][3] = {
                        {flat > 1e-4f ? -d[2] / flat * half : half, 0.0f,
                         flat > 1e-4f ? d[0] / flat * half : 0.0f},
                        {0.0f, half, 0.0f}};
                    for (const auto& side : across) {
                        gfx::Sprite sprite;
                        for (int k = 0; k < 3; ++k) sprite.position[k] = 0.5f * (a[k] + b[k]);
                        sprite.placed = true;
                        for (int k = 0; k < 3; ++k) {
                            sprite.corner[0][k] = a[k] - side[k];
                            sprite.corner[1][k] = b[k] - side[k];
                            sprite.corner[2][k] = b[k] + side[k];
                            sprite.corner[3][k] = a[k] + side[k];
                        }
                        const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
                        for (int k = 0; k < 4; ++k) {
                            sprite.cornerUv[k][0] = uv[k][0];
                            sprite.cornerUv[k][1] = uv[k][1];
                        }
                        for (int k = 0; k < 3; ++k) sprite.colour[k] = kLight;
                        sprite.colour[3] = 1.0f;
                        sprite.sheet = flare_;
                        sprite.blend = gfx::Blend::Additive;
                        effects.add(sprite);
                    }
                }
                break;
            }
            case Item::KnightBlade: {
                const float l = std::sin(clock_ * 2.0f) * 0.35f + 0.65f;
                const float grey = std::sin(clock_ * 2.0f) * 0.5f + 0.4f;
                for (int i = 0; i < one.count; ++i) {
                    put(light_, one.at[i], halfOf(l * 0.9f), 0.43f * l, 0.14f * l, 0.6f * l);
                    if (grey > 0.0f) put(light_, one.at[i], halfOf(grey), 0.3f, 0.3f, 0.3f);
                }
                break;
            }
            case Item::StaffOfKundun: {
                // MU turns its two sparks by rand() % 360 a frame; a sprite here has no turn,
                // so the pair is drawn as it stands.
                const float l = std::sin(clock_ * 2.0f) * 0.3f + 0.7f;
                for (int i = 0; i < one.count; ++i) {
                    const float big = i == 2 ? 2.0f : 1.0f;
                    put(light_, one.at[i], halfOf(big), 0.0f, 0.5f * l, l);
                    put(shiny_, one.at[i], halfOf(0.5f * big), 0.5f, 0.5f, 0.5f);
                }
                break;
            }
            case Item::ArrowViperBow: {
                const float l = linkLuminosity_;
                const float s = std::sin(clock_) + 1.0f;
                for (int i = 0; i < std::min(one.count, 2); ++i) {
                    put(light_, one.at[i], halfOf(s * 0.8f), 3.0f * l, l, l);
                }
                if (one.count > 2) {
                    const float g = std::sin(clock_) * 0.3f + 0.3f;
                    put(light_, one.at[2], halfOf(s * 0.8f), g, g, g);
                    put(shiny_, one.at[2], halfOf(0.8f), 3.0f * g, g, g);
                }
                break;
            }
            case Item::Daybreak: {
                // The hand roll, 0.7 to 1, as MU's flicker's -1 to 1.
                const float swing = (handLuminosity_ - 0.85f) / 0.15f;
                const float f = swing * 0.25f + 0.6f;
                put(light_, one.at[0], halfOf(0.45f), 1.2f, 1.2f, 1.2f);
                put(shiny_, one.at[0], halfOf(0.7f), 0.6f, 0.6f, 0.6f);
                put(shiny_, one.at[0], halfOf(0.5f + 0.55f * (linkLuminosity_ - 0.35f) / 0.15f),
                    0.6f, 0.6f, 0.6f);
                put(light_, one.at[0], halfOf(3.0f), f, f, f);
                break;
            }
            case Item::PlatinaStaff: {
                if (one.count < 11) break;
                const float swing = (handLuminosity_ - 0.85f) / 0.15f;
                const float f = swing * 0.2f + 0.3f;
                const float* head = one.at[0];
                const float* foot = one.at[10];
                put(light_, head, halfOf(0.3f), 0.7f, 0.7f, 0.7f);
                put(light_, head, halfOf(4.0f), 0.2f, 0.6f + f, 0.2f);
                put(shiny_, head, halfOf(1.45f), 0.4f, 0.5f + f, 0.4f);
                for (int i = 1; i <= 4; ++i) put(light_, one.at[i], halfOf(0.2f), 0.1f, 0.8f, 0.1f);
                for (int i = 5; i <= 8; ++i) put(light_, one.at[i], halfOf(0.3f), 0.5f, 0.5f, 0.5f);
                put(shiny_, one.at[9], halfOf(0.6f), 0.1f, 0.8f, 0.1f);
                put(light_, foot, halfOf(0.7f), 0.8f, 0.8f, 0.8f);
                put(shiny_, foot, halfOf(0.5f), 0.1f, 1.0f, 0.1f);
                put(light_, foot, halfOf(2.0f), 0.2f, 0.6f + f, 0.2f);
                break;
            }
            case Item::AlbatrossBow: {
                for (int i = 0; i < one.count; ++i) {
                    put(light_, one.at[i], halfOf(1.2f), 0.0f, 0.7f, 0.0f);
                }
                break;
            }
            case Item::SwordDancer: {
                if (one.count < 6) break;
                const float swing = (handLuminosity_ - 0.85f) / 0.15f;
                const float f = swing * 0.2f + 0.3f;
                put(light_, one.at[0], halfOf(1.0f), 1.0f, 0.1f, 0.0f);
                put(shiny_, one.at[0], halfOf(0.2f + (linkLuminosity_ - 0.35f) / 0.3f), 0.1f + f, 0.2f, 0.0f);
                const float red = swing * 0.25f + 0.2f;
                for (int i = 1; i < 6; ++i) put(light_, one.at[i], halfOf(1.1f), 0.8f + red, 0.1f, 0.0f);
                break;
            }
            case Item::Flamberge: {
                if (one.count < 10) break;
                put(light_, one.at[0], halfOf(0.6f), 1.6f, 1.2f, 0.4f);
                put(light_, one.at[1], halfOf(0.5f), 0.3f, 0.8f, 0.7f);
                put(light_, one.at[2], halfOf(0.5f), 0.3f, 0.8f, 0.7f);
                put(light_, one.at[1], halfOf(0.3f), 0.3f, 0.8f, 0.7f);
                put(light_, one.at[2], halfOf(0.3f), 0.3f, 0.8f, 0.7f);
                static const float kMarks[7] = {0.8f, 0.7f, 0.7f, 0.7f, 0.6f, 0.5f, 0.5f};
                for (int i = 0; i < 7; ++i) put(light_, one.at[3 + i], halfOf(kMarks[i]), 0.9f, 0.1f, 0.1f);
                break;
            }
            case Item::SwordBreaker: {
                static const float kMarks[7] = {0.6f, 0.5f, 0.5f, 0.5f, 0.4f, 0.3f, 0.3f};
                for (int i = 0; i < std::min(one.count, 7); ++i) {
                    put(light_, one.at[i], halfOf(kMarks[i]), 0.1f, 0.9f, 0.1f);
                }
                break;
            }
            case Item::ImperialSword: {
                if (one.count < 10) break;
                const float big = 0.5f + (linkLuminosity_ - 0.35f) / 0.3f;  // MU's 0.5 to 1
                for (int i = 0; i < 4; ++i) {
                    put(light_, one.at[i], halfOf(0.4f), 0.6f, 0.7f, 1.0f);
                    put(shiny_, one.at[i], halfOf(i < 2 ? big : 0.4f), 0.1f, 0.4f, 0.9f);
                }
                static const float kMarks[6] = {1.0f, 0.8f, 0.6f, 0.4f, 0.2f, 0.1f};
                for (int i = 0; i < 6; ++i) put(light_, one.at[4 + i], halfOf(kMarks[i]), 0.0f, 0.3f, 0.7f);
                break;
            }
            case Item::FrostMace: {
                if (one.count < 18) break;
                put(shiny_, one.at[0], halfOf(0.3f), 0.5f, 0.8f, 0.6f);
                put(light_, one.at[0], halfOf(2.0f), 0.5f, 0.8f, 0.6f);
                put(shiny_, one.at[1], halfOf(0.2f), 0.9f, 0.1f, 0.3f);
                put(light_, one.at[1], halfOf(0.8f), 0.9f, 0.1f, 0.3f);
                put(shiny_, one.at[2], halfOf(0.2f), 0.9f, 0.1f, 0.3f);
                put(shiny_, one.at[3], halfOf(0.2f), 0.9f, 0.1f, 0.3f);
                put(shiny_, one.at[4], halfOf(0.2f), 0.5f, 0.8f, 0.5f);
                put(shiny_, one.at[5], halfOf(0.2f), 0.5f, 0.8f, 0.5f);
                for (int i = 6; i < 18; ++i) {
                    const float size = i < 10 ? 0.2f : i < 15 ? 0.3f : 0.4f;
                    put(light_, one.at[i], halfOf(size), 0.5f, 0.8f, 0.6f);
                }
                break;
            }
            case Item::StingerBow: {
                for (int i = 0; i < one.count; ++i) put(light_, one.at[i], halfOf(0.8f), 0.2f, 0.25f, 0.3f);
                break;
            }
            case Item::DeadlyStaff: {
                if (one.count < 8) break;
                const float lumi = std::fabs(std::sin(clock_)) * 0.8f + 0.2f;
                const float shine = 0.5f + (linkLuminosity_ - 0.35f) / 0.3f;
                for (int i = 0; i < 2; ++i) {
                    put(light_, one.at[i], halfOf(2.0f), 0.8f * lumi, 0.1f * lumi, 0.3f * lumi);
                    put(shiny_, one.at[i], halfOf(shine), 0.8f, 0.2f, 0.4f);
                }
                static const float kSizes[6] = {0.8f, 0.4f, 0.8f, 0.8f, 2.0f, 0.8f};
                for (int i = 0; i < 6; ++i) put(light_, one.at[2 + i], halfOf(kSizes[i]), 0.8f, 0.3f, 0.1f);
                break;
            }
            case Item::ImperialStaff: {
                if (one.count < 4) break;
                put(light_, one.at[0], halfOf(2.0f), 0.6f, 0.6f, 1.8f);
                put(light_, one.at[1], halfOf(2.5f), 0.6f, 0.6f, 1.8f);
                put(light_, one.at[2], halfOf(3.0f), 0.3f, 0.3f, 0.9f);
                put(light_, one.at[3], halfOf(2.0f), 0.7f, 0.1f, 0.2f);
                put(shiny_, one.at[3], halfOf(0.8f), 0.9f, 0.3f, 0.5f);
                break;
            }
            case Item::CrimsonGlory: {
                if (one.count < 10) break;
                for (int i = 3; i < 10; ++i) put(light_, one.at[i], halfOf(1.5f), 0.8f, 0.6f, 0.2f);
                // BITMAP_FLARE_RED under white: here the light in its red.
                const float lumi = std::fabs(std::sin(clock_ * 0.5f));
                static const float kFlares[3] = {0.5f, 0.3f, 0.3f};
                for (int i = 0; i < 3; ++i) put(light_, one.at[i], halfOf(kFlares[i]), lumi, 0.15f * lumi, 0.1f * lumi);
                break;
            }
            case Item::SalamanderShield: {
                put(light_, one.at[0], halfOf(2.0f), 0.9f, 0.0f, 0.2f);
                break;
            }
            case Item::GuardianShield: {
                const float lumi = std::fabs(std::sin(clock_)) + 0.1f;
                for (int i = 0; i < one.count; ++i) put(light_, one.at[i], halfOf(1.5f), 0.8f * lumi, 0.3f * lumi, 0.8f * lumi);
                break;
            }
            case Item::BoneBlade: {
                // MU's sin(WorldTime*0.4)*0.25 + 0.7 turns faster than its frame: the hand roll.
                const float f = 0.45f + handLuminosity_ * 0.5f;
                for (int i = 0; i < one.count; ++i) put(light_, one.at[i], halfOf(0.75f), f, f - 0.5f, f - 0.5f);
                break;
            }
            case Item::ExplosionBlade: {
                if (one.count < 5) break;
                const float f = 0.3f + linkLuminosity_ * 0.5f;
                put(shiny_, one.at[0], halfOf(2.2f), 0.2f, 0.2f, f);
                put(shiny_, one.at[0], halfOf(1.8f), 0.2f, 0.2f, f);
                put(light_, one.at[0], halfOf(2.3f), 0.2f, 0.2f, f);
                static const float kSizes[4] = {2.0f, 2.0f, 2.0f, 1.0f};
                for (int i = 0; i < 4; ++i) put(light_, one.at[1 + i], halfOf(kSizes[i]), 0.2f, 0.2f, f);
                break;
            }
            case Item::SylphWindBow: {
                if (one.count < 3) break;
                put(light_, one.at[0], halfOf(2.0f), 0.5f, 0.5f, 0.1f);
                put(light_, one.at[1], halfOf(2.0f), 0.5f, 0.5f, 0.1f);
                for (const float size : {0.8f, 1.3f, 2.0f}) put(light_, one.at[2], halfOf(size), 1.0f, 1.0f, 0.4f);
                break;
            }
            case Item::GrandViperStaff: {
                const float size = 1.0f + handLuminosity_ * 0.5f;
                put(shiny_, one.at[0], halfOf(size + 0.8f), 0.4f, 0.4f, 0.4f);
                put(light_, one.at[0], halfOf(size), 1.0f, 0.4f, 1.0f);
                put(shiny_, one.at[0], halfOf(size), 1.0f, 0.4f, 1.0f);
                put(shiny_, one.at[0], halfOf(size - 0.3f), 1.0f, 0.4f, 1.0f);
                break;
            }
        }
    }
}

}  // namespace mu::game
