#include "game/fx/power_slash.h"

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kTwoPi = 6.28318531f;

float clampOne(float v) { return std::clamp(v, 0.0f, 1.0f); }

}  // namespace

float PowerSlash::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool PowerSlash::open(const std::string& assetDir, content::Textures& textures,
                      const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    smokeSheet_ = sheet("smoke01");   // Effect/smoke01, BITMAP_SMOKE
    flareSheet_ = sheet("light");     // Effect/flare01, BITMAP_LIGHT
    shinySheet_ = sheet("shiny_02");  // Effect/Shiny02, BITMAP_SHINY + 1
    core::logf("power slash: smoke %s, flare %s, shiny %s", bgfx::isValid(smokeSheet_) ? "yes" : "NO",
               bgfx::isValid(flareSheet_) ? "yes" : "NO", bgfx::isValid(shinySheet_) ? "yes" : "NO");
    return bgfx::isValid(smokeSheet_);
}

void PowerSlash::clear() {
    for (Wave& one : waves_) one = Wave{};
    for (Puff& one : puffs_) one = Puff{};
    for (Puff& one : hazes_) one = Puff{};
}

void PowerSlash::cast(uint32_t owner, float yaw, float wait) {
    const float metre = perUnit();
    for (int n = 0; n < kWaves; ++n) {
        Wave* slot = nullptr;
        for (Wave& one : waves_) {
            if (!one.alive) {
                slot = &one;
                break;
            }
        }
        if (slot == nullptr) return;
        *slot = Wave{};
        slot->alive = true;
        slot->owner = owner;
        slot->wait = std::max(0.0f, wait);
        slot->sounds = n == 0;
        // MU's (0, -60, 0) turned by the wave's angle off his facing: -Y is ahead, so a step is
        // (sin, cos) of the turned yaw in our (x, z) (fx/fire_slash.h's `local`).
        const float turned = yaw + kWaveTurns[n] * kDegrees;
        slot->step[0] = std::sin(turned) * kWaveStep * metre;
        slot->step[1] = std::cos(turned) * kWaveStep * metre;
    }
}

void PowerSlash::smoke(Puff* pool, int size, const float at[3], float scale, const float tint[3]) {
    const float metre = perUnit();
    for (int i = 0; i < size; ++i) {
        Puff& one = pool[i];
        if (one.alive) continue;
        one = Puff{};
        one.alive = true;
        for (int k = 0; k < 3; ++k) {
            one.from[k] = at[k];
            one.tint[k] = tint[k];
        }
        // Angle (pitch rand +-45, yaw 0-359) on Velocity (0, -(40..47), 0): MovePosition.
        const float pitch = between(-45.0f, 45.0f) * kDegrees, turn = unit() * kTwoPi;
        one.way[0] = std::cos(pitch) * std::sin(turn);
        one.way[1] = std::sin(pitch);
        one.way[2] = std::cos(pitch) * std::cos(turn);
        one.speed = between(40.0f, 47.0f) * metre;
        one.scale = scale;
        // The haze turned at random, the puffs not (sub 3 sets no Rotation).
        if (pool == hazes_) one.spin = unit() * kTwoPi;
        return;
    }
}

void PowerSlash::stepWave(Wave& one) {
    const float metre = perUnit();
    // L, and its last four frames dimming (ZzzEffect.cpp:6645-6650).
    one.light = between(kLeast, kMost);
    if (one.life < 5) one.light = std::max(0.0f, one.light - float(5 - one.life) * 0.2f);
    for (int k = 0; k < 3; ++k) one.shown[k] = one.at[k];
    for (int n = 0; n < kPuffsAFrame; ++n) smoke(puffs_, kPuffs, one.at, between(0.80f, 1.11f), kPuffTint);
    // The haze set off it, +-32 aside and 32-95 up, lit by the wave as it is made.
    const float off[3] = {one.at[0] + between(-32.0f, 32.0f) * metre,
                          one.at[1] + between(32.0f, 95.0f) * metre,
                          one.at[2] + between(-32.0f, 32.0f) * metre};
    const float tint[3] = {kBlue[0] * one.light, kBlue[1] * one.light, kBlue[2] * one.light};
    smoke(hazes_, kHazes, off, between(80.0f, 111.0f) * 0.015f, tint);
    for (int i = 0; i < 2; ++i) one.shinyRoll[i] = unit() * kTwoPi;
    one.flareRoll = unit() * kTwoPi;
    // Then on its 60 units, over the ground it crosses (ours: MU keeps the height it left at).
    one.at[0] += one.step[0];
    one.at[2] += one.step[1];
    if (ground_ != nullptr) one.at[1] = ground_->heightAt(one.at[0], one.at[2]);
    if (--one.life <= 0) one.alive = false;
}

uint32_t PowerSlash::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    const float metres = ground_ ? ground_->metresPerTile() : 1.0f;
    for (const Wave& one : waves_) {
        if (!one.alive || !one.gone || one.light <= 0.0f || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = one.shown[k];
        light.reach = kLightTiles * metres;
        light.height = 1.0f;
        for (int k = 0; k < 3; ++k) light.colour[k] = kBlue[k] * one.light;
    }
    return count;
}

void PowerSlash::gatherSmoke(gfx::Effects& effects, const Puff* pool, int size, bool haze) const {
    const float metre = perUnit();
    for (int i = 0; i < size; ++i) {
        const Puff& one = pool[i];
        if (!one.alive) continue;
        const float life = haze ? kHazeLife : kPuffLife;
        const float left = life - one.age;
        // Its way so far: v (1 - 0.4^age) / 0.6, MU's speed four tenths of itself a frame.
        const float gone = one.speed * (1.0f - std::pow(kSlows, one.age)) / (1.0f - kSlows);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.from[k] + one.way[k] * gone;
        if (haze) sprite.position[1] -= kHazeSinks * one.age * metre;
        const float scale = one.scale + (haze ? kHazeGrows : kPuffGrows) * one.age;
        sprite.halfWidth = sprite.halfHeight = kSmokePixels * scale * metre * 0.5f;
        sprite.spin = one.spin;
        // The bright puffs their tint times LifeTime / 8 (MU's is (0.8, 0.8, 1), overbright at
        // first and clamped; ours `kPuffTint`); the haze its wave's blue times LifeTime / 50.
        for (int k = 0; k < 3; ++k) {
            sprite.colour[k] = clampOne(one.tint[k] * left / (haze ? kHazeLife : 8.0f));
        }
        sprite.sheet = smokeSheet_;
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) return;
    }
}

void PowerSlash::gatherEffects(gfx::Effects& effects) const {
    const float metre = perUnit();
    // The haze under, the puffs over it.
    if (bgfx::isValid(smokeSheet_)) {
        gatherSmoke(effects, hazes_, kHazes, true);
        gatherSmoke(effects, puffs_, kPuffs, false);
    }
    // Each wave's sprites, 50 up from where this frame's smoke was made.
    for (const Wave& one : waves_) {
        if (!one.alive || !one.gone) continue;
        const float at[3] = {one.shown[0], one.shown[1] + kSpriteLift * metre, one.shown[2]};
        if (bgfx::isValid(flareSheet_)) {
            gfx::Sprite flare;
            for (int k = 0; k < 3; ++k) flare.position[k] = at[k];
            flare.halfWidth = flare.halfHeight = kFlarePixels * kFlareScale * metre * 0.5f;
            flare.spin = one.flareRoll;
            for (int k = 0; k < 3; ++k) flare.colour[k] = clampOne(kBlue[k] * one.light);
            flare.sheet = flareSheet_;
            flare.blend = gfx::Blend::Additive;
            if (!effects.add(flare)) return;
        }
        if (bgfx::isValid(shinySheet_)) {
            for (int i = 0; i < 2; ++i) {
                gfx::Sprite shiny;
                for (int k = 0; k < 3; ++k) shiny.position[k] = at[k];
                shiny.halfWidth = kShinyWide * kShinyScale * metre * 0.5f;
                shiny.halfHeight = kShinyTall * kShinyScale * metre * 0.5f;
                shiny.spin = one.shinyRoll[i];
                // Ours: the wave's blue and its fade, not a full white glint.
                for (int k = 0; k < 3; ++k) shiny.colour[k] = clampOne(kBlue[k] * one.light);
                shiny.sheet = shinySheet_;
                shiny.blend = gfx::Blend::Additive;
                if (!effects.add(shiny)) return;
            }
        }
    }
}

}  // namespace mu::game
