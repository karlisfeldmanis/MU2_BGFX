#include "game/fx/pet_lights.h"

#include <algorithm>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kUnit = 0.01f;
constexpr float kFps = 25.0f;
// Ours: the held weapons' quarter of MU's size and its share of the light (fx/held_lights.cpp).
constexpr float kSize = 0.25f;
constexpr float kLight = 0.45f;
// MU's CreateSprite scale over a 64-texel sheet, halved for a half width.
constexpr float halfOf(float scale) { return 0.5f * scale * 64.0f * kUnit * kSize; }
constexpr float kSparkTexels = 4.0f;  // Spark02
constexpr float kAngelGlow[3] = {0.5f, 0.8f, 0.6f};

}  // namespace

float PetLights::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000u);
}

bool PetLights::open(const std::string& assetDir, content::Textures& textures, const content::Showing& table) {
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        if (const content::EffectSheet* one = table.effect(name)) {
            return textures.load(assetDir + "/" + one->path, content::TextureRole::Albedo);
        }
        return BGFX_INVALID_HANDLE;
    };
    spark_ = sheet("spark");  // BITMAP_SPARK, Effect/Spark02
    light_ = sheet("light");  // BITMAP_LIGHT, Effect/flare01
    core::logf("pet lights: spark %s, light %s", bgfx::isValid(spark_) ? "yes" : "NO",
               bgfx::isValid(light_) ? "yes" : "NO");
    return bgfx::isValid(light_);
}

void PetLights::angel(const float at[3]) {
    if (glowCount_ < kGlows) {
        Glow& one = glows_[glowCount_++];
        for (int k = 0; k < 3; ++k) one.at[k] = at[k];
        one.half = halfOf(1.0f);
        one.light = float(int(unit() * 30.0f) + 70) * 0.01f;
        one.red = false;
    }
    if (angelCount_ < kGlows) {
        for (int k = 0; k < 3; ++k) angels_[angelCount_][k] = at[k];
        ++angelCount_;
    }
}

void PetLights::imp(const float at[3]) {
    if (glowCount_ >= kGlows) return;
    Glow& one = glows_[glowCount_++];
    for (int k = 0; k < 3; ++k) one.at[k] = at[k];
    one.half = halfOf(1.5f);
    one.light = float(int(unit() * 30.0f) + 70) * 0.01f;
    one.red = true;
}

void PetLights::update(float seconds) {
    const float frames = seconds * kFps;
    for (Mote& one : motes_) {
        if (!one.alive) continue;
        one.life -= frames;
        if (one.life < 0.0f) one.alive = false;
    }
    // Four motes about each Angel a whole frame (rand_fps_check(1)).
    owed_ = std::min(owed_ + frames, 4.0f);
    while (owed_ >= 1.0f) {
        for (int a = 0; a < angelCount_; ++a) {
            for (int j = 0; j < 4; ++j) {
                Mote* free = nullptr;
                for (Mote& one : motes_) {
                    if (!one.alive) {
                        free = &one;
                        break;
                    }
                }
                if (!free) break;
                *free = Mote{};
                free->alive = true;
                for (int k = 0; k < 3; ++k) free->at[k] = angels_[a][k] + float(int(unit() * 16.0f) - 8) * kUnit;
                free->half = 0.5f * kSparkTexels * float(int(unit() * 4.0f) + 4) * 0.1f * kUnit;
            }
        }
        owed_ -= 1.0f;
    }
    angelCount_ = 0;
    for (int i = 0; i < glowCount_; ++i) shown_[i] = glows_[i];
    shownCount_ = glowCount_;
    glowCount_ = 0;
}

void PetLights::gather(gfx::Effects& effects) const {
    const auto put = [&](bgfx::TextureHandle sheet, const float at[3], float half, float r, float g, float b) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = at[k];
        sprite.halfWidth = sprite.halfHeight = half;
        sprite.colour[0] = std::clamp(r, 0.0f, 1.0f);
        sprite.colour[1] = std::clamp(g, 0.0f, 1.0f);
        sprite.colour[2] = std::clamp(b, 0.0f, 1.0f);
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    };
    for (const Mote& one : motes_) {
        if (!one.alive) continue;
        // Its light its LifeTime / 16, grey.
        const float l = std::max(one.life, 0.0f) / 16.0f;
        put(spark_, one.at, one.half, l, l, l);
    }
    for (int i = 0; i < shownCount_; ++i) {
        const Glow& one = shown_[i];
        const float l = one.light * kLight;
        if (one.red) {
            put(light_, one.at, one.half, 0.5f * l, 0.0f, 0.0f);
        } else {
            put(light_, one.at, one.half, kAngelGlow[0] * l, kAngelGlow[1] * l, kAngelGlow[2] * l);
        }
    }
}

}  // namespace mu::game
