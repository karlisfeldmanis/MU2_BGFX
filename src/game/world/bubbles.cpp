#include "game/world/bubbles.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"
#include "game/sound.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;          // MU's reference frame, 25 a second
constexpr float kCycle = 100.0f * kFrame; // a vent's Timer: 0.1 a frame to 10, 4 s
constexpr float kOnFrom = 50.0f * kFrame; // and on while it is past 5, the second half
// Ours: a vent throws one bubble in four of MU's frames while it is on, not every frame, and
// only within this many metres of him.
constexpr float kVentEvery = 4.0f * kFrame;
constexpr float kNear = 14.0f;
// His head: one a frame for the first second of every ten (WorldTime % 10000 < 1000), MU's
// rate, at about his head's height.
constexpr float kHeadCycle = 10.0f;
constexpr float kHeadFor = 1.0f;
constexpr float kHeadEvery = kFrame;
constexpr float kHeadHeight = 1.7f;
// MU's Width is the 64-texel sheet times Scale, in units; ours in metres, half of it.
constexpr float kSheetMetres = 0.64f;
// Ours: added at this, not at MU's full white.
constexpr float kLevel = 0.55f;
constexpr size_t kMost = 600;
// Ours: a vent is heard coming on within this many metres of him.
constexpr float kHear = 8.0f;
// And no closer together than this: a field of vents came on about once a second, every voice
// still sounding (2026-10-03).
constexpr float kGurgleGap = 1.5f;

}  // namespace

float Bubbles::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void Bubbles::open(const std::string& assetDir, const std::string& world,
                   const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (world != "atlans") return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("bubble"))
            sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model != content::TownEmitter::kWorld || one.kind != content::EmitterKind::Bubble)
            continue;
        Vent vent;
        for (int k = 0; k < 3; ++k) vent.at[k] = one.at[k];
        vent.timer = unit() * kCycle;  // ours: MU's all start at 0
        vents_.push_back(vent);
    }
    bubbles_.reserve(kMost);
    open_ = true;
    core::logf("bubbles: %zu vents, sheet %s", vents_.size(), bgfx::isValid(sheet_) ? "yes" : "NO");
}

void Bubbles::setSound(Sound* sound) {
    sound_ = sound;
    gurgle_ = sound && open_ ? sound->load("world_bubbles", true, true) : -1;
    if (sound && gurgle_ >= 0) sound->vary(gurgle_, 4.0f, 4.0f, 0.5f);
}

void Bubbles::shutdown() {
    open_ = false;
    vents_.clear();
    bubbles_.clear();
    head_ = headOwed_ = 0.0f;
    sheet_ = BGFX_INVALID_HANDLE;
    sound_ = nullptr;
    gurgle_ = -1;
}

void Bubbles::throwOne(const float at[3]) {
    if (bubbles_.size() >= kMost) return;
    Bubble one;
    for (int k = 0; k < 3; ++k) one.at[k] = at[k];
    one.life = (30.0f + float(int(unit() * 10.0f))) * kFrame;
    one.scale = float(int(unit() * 6.0f) + 4) * 0.03f;
    // The first column's three: the only column with a bubble in every row of drop01, so the
    // pick holds whichever way up the sheet is read.
    one.frame = (int(unit() * 3.0f) % 3) * 4;
    bubbles_.push_back(one);
}

void Bubbles::update(float seconds, const float hero[3]) {
    if (!open_) return;
    // SubType 0, a reference frame's step scaled to this frame's share of one: up (10..29) *
    // 2.5 * Scale units, and (-10..9) * 2.5 * Scale across in each of x and y.
    const float share = seconds / kFrame;
    for (Bubble& one : bubbles_) {
        one.age += seconds;
        const float unitsUp = (unit() * 20.0f + 10.0f) * 2.5f * one.scale;
        const float unitsX = (unit() * 20.0f - 10.0f) * 2.5f * one.scale;
        const float unitsY = (unit() * 20.0f - 10.0f) * 2.5f * one.scale;
        one.at[1] += unitsUp * 0.01f * share;
        one.at[0] += unitsX * 0.01f * share;
        one.at[2] -= unitsY * 0.01f * share;
    }
    bubbles_.erase(std::remove_if(bubbles_.begin(), bubbles_.end(),
                                  [](const Bubble& b) { return b.age >= b.life; }),
                   bubbles_.end());

    quiet_ = std::max(0.0f, quiet_ - seconds);
    for (Vent& vent : vents_) {
        vent.timer = std::fmod(vent.timer + seconds, kCycle);
        const float dx = vent.at[0] - hero[0], dz = vent.at[2] - hero[2];
        const bool on = vent.timer >= kOnFrom;
        if (on && !vent.on && sound_ && gurgle_ >= 0 && quiet_ <= 0.0f &&
            dx * dx + dz * dz < kHear * kHear) {
            sound_->playAt(gurgle_, vent.at[0], vent.at[1], vent.at[2]);
            quiet_ = kGurgleGap;
        }
        vent.on = on;
        if (!on || dx * dx + dz * dz > kNear * kNear) {
            vent.owed = 0.0f;
            continue;
        }
        vent.owed += seconds;
        while (vent.owed >= kVentEvery) {
            vent.owed -= kVentEvery;
            throwOne(vent.at);
        }
    }

    const float wasHead = head_;
    head_ = std::fmod(head_ + seconds, kHeadCycle);
    // Heard as each stream starts, the first one included.
    if ((head_ < wasHead || wasHead == 0.0f) && sound_ && gurgle_ >= 0) {
        sound_->playAt(gurgle_, hero[0], hero[1] + kHeadHeight, hero[2]);
    }
    if (head_ < kHeadFor) {
        headOwed_ += seconds;
        const float at[3] = {hero[0], hero[1] + kHeadHeight, hero[2]};
        while (headOwed_ >= kHeadEvery) {
            headOwed_ -= kHeadEvery;
            throwOne(at);
        }
    } else {
        headOwed_ = 0.0f;
    }
}

void Bubbles::gather(gfx::Effects& effects) const {
    if (!open_ || !bgfx::isValid(sheet_)) return;
    for (const Bubble& one : bubbles_) {
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = one.at[k];
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * one.scale;
        // drop01 is 64 by 48: four 16-texel bubbles to a row, the ninth alone on the third.
        // `frame` is row * 4 + column.
        const int column = one.frame % 4, row = one.frame / 4;
        sprite.u0 = float(column) * 0.25f;
        sprite.u1 = sprite.u0 + 0.25f;
        sprite.v0 = float(row) / 3.0f;
        sprite.v1 = sprite.v0 + 1.0f / 3.0f;
        // Out at the end of its life rather than gone in a frame.
        const float t = one.age / one.life;
        const float level = kLevel * std::min(1.0f, (1.0f - t) * 4.0f);
        for (int k = 0; k < 3; ++k) sprite.colour[k] = level;
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
