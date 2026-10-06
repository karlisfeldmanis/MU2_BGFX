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

}  // namespace

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
            // the Staff of Resurrection's (0, -145, 0) is its glb's (0, 0, 1.45).
            out[0][0] = out[0][1] = 0.0f;
            out[0][2] = 20.0f * kUnit;
            return 1;
        case Item::LegendaryShield:
            // Ours: the hand's (20, 0, 0) as 20 out of the grip along the glb's x, on the face.
            out[0][0] = 20.0f * kUnit;
            out[0][1] = out[0][2] = 0.0f;
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
                break;
            }
            case Item::LegendaryShield: {
                const float l = handLuminosity_;
                put(shiny_, one.at[0], halfOf(1.5f), 0.4f * l, 0.6f * l, 1.5f * l);
                break;
            }
        }
    }
}

}  // namespace mu::game
