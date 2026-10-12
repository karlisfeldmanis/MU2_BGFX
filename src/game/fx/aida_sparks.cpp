#include "game/fx/aida_sparks.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;   // MU's reference frame
constexpr float kUnit = 0.01f;    // a MU unit, in metres
// Spark03 is 32 texels, a unit each, so MU's spark is 32-45 cm across. Ours at a third (the
// user, 2026-10-12, of the Death Rider's: 'green bubles to large ahs to be much smaller').
constexpr float kSpark = 0.32f * 0.33f;
constexpr float kImpact = 0.64f;  // Impack03, 64
constexpr float kHeadScale = 1.0f;  // MU's 3, a green disc a metre across round the head; ours a third
constexpr size_t kMostSparks = 600;

}  // namespace

float AidaSparks::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool AidaSparks::open(const std::string& assetDir, content::Textures& textures,
                      const content::Showing& table) {
    shutdown();
    const auto load = [&](const char* name) {
        const content::EffectSheet* sheet = table.effect(name);
        return sheet ? textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo)
                     : bgfx::TextureHandle BGFX_INVALID_HANDLE;
    };
    spark_ = load("spark_flash");  // BITMAP_SPARK + 1, Effect/Spark03
    impact_ = load("impact");      // BITMAP_LIGHT + 1, Object9/Impack03
    core::logf("aida sparks: spark %s, light %s", bgfx::isValid(spark_) ? "yes" : "NO",
               bgfx::isValid(impact_) ? "yes" : "NO");
    return bgfx::isValid(spark_);
}

void AidaSparks::shutdown() {
    sparks_.clear();
    glows_.clear();
    spark_ = impact_ = BGFX_INVALID_HANDLE;
}

void AidaSparks::head(const float at[3], float fade, float seconds) {
    if (fade <= 0.0f) return;
    glows_.push_back({{at[0], at[1], at[2]}, fade * (std::floor(unit() * 10.0f) / 100.0f + 0.2f)});
    // One a reference frame, as many as this frame holds.
    for (float owed = seconds / kFrame + unit(); owed >= 1.0f && sparks_.size() < kMostSparks; owed -= 1.0f) {
        Spark one;
        // MU's y and z: across, and up or down; its x stays.
        one.at[0] = at[0];
        one.at[1] = at[1] + (std::floor(unit() * 60.0f) - 30.0f) * kUnit;
        one.at[2] = at[2] + (std::floor(unit() * 60.0f) - 30.0f) * kUnit;
        one.life = 30.0f;
        one.gravity = 1.3f;
        one.scale = std::floor(unit() * 5.0f) / 10.0f + 1.0f;
        one.fade = fade;
        sparks_.push_back(one);
    }
}

void AidaSparks::horn(const float at[3], float fade, float seconds) {
    // rand_fps_check(50): one reference frame in fifty.
    if (fade <= 0.0f || sparks_.size() >= kMostSparks || unit() >= seconds / kFrame / 50.0f) return;
    Spark one;
    for (int i = 0; i < 3; ++i) one.at[i] = at[i];
    one.life = 50.0f;
    one.gravity = 1.0f + std::floor(unit() * 20.0f) / 5.0f;
    one.scale = std::floor(unit() * 5.0f) / 10.0f + 1.0f;
    one.tint[0] = 0.4f;
    one.tint[2] = 0.5f;
    one.fade = fade;
    one.horn = true;
    sparks_.push_back(one);
}

void AidaSparks::update(float seconds) {
    const float frames = std::min(seconds, 0.25f) / kFrame;
    glows_.clear();
    for (Spark& one : sparks_) {
        one.age += frames;
        one.at[1] -= one.gravity * kUnit * frames;
        if (one.horn) {
            one.at[2] += 1.0f * kUnit * frames;  // 1 a frame down MU's y
            one.gravity += 0.1f * frames;
            one.green = std::floor(unit() * 10.0f) / 100.0f + 0.7f;
        } else {
            one.scale -= 0.02f * frames;
            one.green = std::floor(unit() * 50.0f) / 100.0f + 0.2f;
        }
    }
    sparks_.erase(std::remove_if(sparks_.begin(), sparks_.end(),
                                 [](const Spark& s) { return s.age >= s.life || s.scale <= 0.0f; }),
                  sparks_.end());
}

void AidaSparks::gather(gfx::Effects& effects) const {
    const auto add = [&](bgfx::TextureHandle sheet, const float at[3], float half, const float colour[3]) {
        if (!bgfx::isValid(sheet)) return;
        gfx::Sprite one;
        one.sheet = sheet;
        for (int i = 0; i < 3; ++i) {
            one.position[i] = at[i];
            one.colour[i] = colour[i];
        }
        one.colour[3] = 1.0f;
        one.halfWidth = one.halfHeight = half;
        one.blend = gfx::Blend::Additive;
        effects.add(one);
    };
    for (const Glow& one : glows_) {
        const float colour[3] = {0.0f, one.light, 0.0f};
        add(impact_, one.at, 0.5f * kImpact * kHeadScale, colour);
    }
    for (const Spark& one : sparks_) {
        const float colour[3] = {one.tint[0] * one.fade, one.green * one.fade, one.tint[2] * one.fade};
        add(spark_, one.at, 0.5f * kSpark * one.scale, colour);
    }
}

}  // namespace mu::game
