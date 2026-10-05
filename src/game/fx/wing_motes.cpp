#include "game/fx/wing_motes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"

namespace mu::game {
namespace {

enum Sheet { kShiny = 0, kSpark = 1, kLight = 2 };

// One wing's motif. Ours, every number: colour before the faint factor, the sheet, the drift
// (metres a second, + up), how long one lasts, its half width, and how many a second at rest
// and in the air.
struct Motif {
    const char* wing;
    float colour[3];
    int sheet;
    float rise;
    float life;
    float half;
    float rest;
    float flying;
    bool twinkle;  // spins and holds still, rather than drifting
};
constexpr Motif kMotifs[6] = {
    {"Wing01", {0.55f, 0.95f, 0.50f}, kShiny, -0.12f, 1.2f, 0.030f, 1.8f, 4.0f, false},  // Elf
    {"Wing02", {0.95f, 0.95f, 1.00f}, kLight, -0.20f, 1.5f, 0.035f, 1.8f, 4.0f, false},  // Heaven
    {"Wing03", {1.00f, 0.28f, 0.10f}, kSpark, 0.25f, 0.8f, 0.025f, 2.0f, 5.0f, false},   // Satan
    {"Wing04", {0.30f, 0.95f, 0.85f}, kShiny, 0.0f, 0.6f, 0.040f, 2.0f, 5.0f, true},     // Spirits
    {"Wing05", {0.70f, 0.50f, 1.00f}, kShiny, -0.12f, 1.2f, 0.035f, 1.8f, 5.0f, false},  // Soul
    {"Wing06", {1.00f, 0.50f, 0.15f}, kSpark, 0.40f, 0.9f, 0.028f, 2.0f, 6.0f, false},   // Dragon
};
constexpr float kFaint = 0.45f;  // as the staff's fire: a fraction of full light
constexpr int kMostMotes = 160;

}  // namespace

float WingMotes::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

bool WingMotes::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    shutdown();
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("wing motes: no cooked effect named '%s'", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    sheets_[kShiny] = take("shiny");
    sheets_[kSpark] = take("spark");
    sheets_[kLight] = take("light");
    open_ = bgfx::isValid(sheets_[kShiny]) && bgfx::isValid(sheets_[kSpark]) &&
            bgfx::isValid(sheets_[kLight]);
    return open_;
}

void WingMotes::shutdown() {
    motes_.clear();
    std::memset(owed_, 0, sizeof(owed_));
    open_ = false;
}

void WingMotes::update(float seconds) {
    const float dt = std::min(seconds, 0.1f);
    seconds_ = dt;
    for (Mote& one : motes_) {
        one.age += dt;
        for (int k = 0; k < 3; ++k) one.at[k] += one.velocity[k] * dt;
    }
    motes_.erase(std::remove_if(motes_.begin(), motes_.end(),
                                [](const Mote& m) { return m.age >= m.life; }),
                 motes_.end());
}

void WingMotes::feed(const std::string& wing, bool flying, const float tips[][3], int count) {
    if (!open_ || count <= 0) return;
    int kind = -1;
    for (int i = 0; i < 6; ++i) {
        if (wing == kMotifs[i].wing) kind = i;
    }
    if (kind < 0) return;
    const Motif& motif = kMotifs[kind];
    owed_[kind] += seconds_ * (flying ? motif.flying : motif.rest);
    while (owed_[kind] >= 1.0f && int(motes_.size()) < kMostMotes) {
        owed_[kind] -= 1.0f;
        Mote mote;
        const int tip = std::min(count - 1, int(unit() * float(count)));
        for (int k = 0; k < 3; ++k) mote.at[k] = tips[tip][k] + (unit() - 0.5f) * 0.08f;
        if (motif.twinkle) {
            for (float& v : mote.velocity) v = 0.0f;
            mote.spin = unit() * 6.28f;
        } else {
            mote.velocity[0] = (unit() - 0.5f) * 0.12f;
            mote.velocity[1] = motif.rise * (0.6f + 0.8f * unit());
            mote.velocity[2] = (unit() - 0.5f) * 0.12f;
        }
        for (int k = 0; k < 3; ++k) mote.colour[k] = motif.colour[k] * kFaint;
        mote.life = motif.life * (0.7f + 0.6f * unit());
        mote.half = motif.half * (0.7f + 0.6f * unit());
        mote.sheet = motif.sheet;
        motes_.push_back(mote);
    }
    owed_[kind] = std::min(owed_[kind], 1.0f);
}

void WingMotes::gather(gfx::Effects& effects) const {
    if (!open_) return;
    for (const Mote& one : motes_) {
        // In and out on a half sine, so none pops on or off.
        const float fade = std::sin(3.14159265f * std::min(1.0f, one.age / one.life));
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) {
            sprite.position[k] = one.at[k];
            sprite.colour[k] = one.colour[k] * fade;
        }
        sprite.colour[3] = 1.0f;
        sprite.halfWidth = sprite.halfHeight = one.half;
        sprite.spin = one.spin + one.age * 2.0f;
        sprite.sheet = sheets_[one.sheet];
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
