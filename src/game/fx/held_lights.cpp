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

uint32_t HeldLights::lights(gfx::PointLight* out, uint32_t max) const {
    uint32_t count = 0;
    for (const Held& one : held_) {
        if (one.item != Item::HeliacalSword || one.count < 2 || count >= max) continue;
        gfx::PointLight& light = out[count++];
        light = gfx::PointLight{};
        for (int k = 0; k < 3; ++k) {
            light.position[k] = one.at[1][k];
            light.colour[k] = kHeliacalLight[k];
        }
        light.reach = kHeliacalReach;
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
        }
    }
}

}  // namespace mu::game
