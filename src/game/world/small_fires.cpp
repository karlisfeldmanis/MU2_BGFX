#include "game/world/small_fires.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "core/log.h"
#include "game/world/world_show.h"

namespace mu::game {
namespace {

constexpr float kFrame = 0.04f;        // MU's reference frame, 25 a second
constexpr float kUnit = 0.01f;         // a MU unit, in metres
constexpr float kSheetMetres = 0.64f;  // Flame01 is 64 texels: Scale 1 is 0.64 m
constexpr float kReach = 28.0f;        // metres from the camera's point a box throws
constexpr size_t kMostTongues = 600;   // 11 boxes x 33 frames is 363
constexpr float kLife = 33.0f;         // reference frames
// Ours (the user, 2026-10-11, of MU's flat orange blobs: 'lets use previus one but made some
// adjustments'): each tongue drawn through the fire's heat ramp rather than added at MU's flat
// (0.3, 0.3, 0.3), so it has a hot core and a cool tip; this much narrower than MU's; eased in
// over its first frames and out over the last share of its life, so none pops.
constexpr float kHeat = 0.85f;    // a new tongue's heat, 0..1
constexpr float kGain = 0.05f;    // the flame sheet's red as its shape, 1x (fs_effect.sc: / 20)
constexpr float kNarrow = 0.7f;
constexpr float kEaseIn = 3.0f;   // reference frames
constexpr float kEaseOut = 0.35f; // of its life
constexpr float kDegree = 3.14159265f / 180.0f;

}  // namespace

float SmallFires::unit() {
    seed_ = seed_ * 1664525u + 1013904223u;
    return float(seed_ >> 8) / 16777216.0f;
}

void SmallFires::open(const std::string& assetDir, const std::string& world,
                      const content::CookedTown& town, content::Textures& textures) {
    shutdown();
    if (!worldShow(world).smallFires) return;
    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* sheet = table.effect("flame"))
            sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    }
    for (const content::TownEmitter& one : town.emitters) {
        if (one.model != content::TownEmitter::kWorld || one.kind != content::EmitterKind::SmallFire)
            continue;
        Box box;
        for (int i = 0; i < 3; ++i) box.at[i] = one.at[i];
        box.scale = one.high;
        boxes_.push_back(box);
    }
    open_ = true;
    tongues_.reserve(kMostTongues);
    core::logf("small fires: %zu boxes; flame %s", boxes_.size(), bgfx::isValid(sheet_) ? "yes" : "NO");
}

void SmallFires::shutdown() {
    open_ = false;
    boxes_.clear();
    tongues_.clear();
    sheet_ = BGFX_INVALID_HANDLE;
}

void SmallFires::throwTongue(const Box& box) {
    if (tongues_.size() >= kMostTongues) return;
    Tongue one;
    one.scale = box.scale - std::floor(unit() * 20.0f) / 100.0f;
    one.spin = (std::floor(unit() * 360.0f) - 180.0f) * kDegree;
    // rand() % 2 / 2: no nudge, or half the Scale in units, along x and the same along y.
    const float nudge = std::floor(unit() * 2.0f) * 0.5f * one.scale * kUnit;
    one.at[0] = box.at[0] + nudge;
    one.at[1] = box.at[1];
    one.at[2] = box.at[2] - nudge;
    one.turn = (nudge > 0.0f ? 2.0f : -2.0f) * kDegree / kFrame;
    one.rise = (1.8f + std::floor(unit() * 100.0f) / 100.0f) * one.scale * kUnit / kFrame;
    if (unit() >= 0.3f)
        one.drift = (std::floor(unit() * 20.0f) / 10.0f - 1.0f) * one.scale * kUnit / kFrame;
    tongues_.push_back(one);
}

void SmallFires::update(float seconds, const float near[3]) {
    if (!open_) return;
    // A long hitch would light a second's tongues in one place at once.
    seconds = std::min(seconds, 0.25f);
    const float frames = seconds / kFrame;
    for (Tongue& one : tongues_) {
        one.age += frames;
        one.spin += one.turn * seconds;
        one.at[1] += one.rise * seconds;
        one.at[2] -= one.drift * seconds;
        // Scale falls by Gravity / 95 a frame, Gravity in units a frame.
        one.scale -= (one.rise * kFrame / kUnit) / 95.0f * frames;
    }
    tongues_.erase(std::remove_if(tongues_.begin(), tongues_.end(),
                                  [](const Tongue& t) { return t.age >= kLife || t.scale <= 0.0f; }),
                   tongues_.end());
    for (Box& box : boxes_) {
        const float dx = box.at[0] - near[0], dz = box.at[2] - near[2];
        if (dx * dx + dz * dz > kReach * kReach) {
            box.owed = 0.0f;
            continue;
        }
        box.owed += seconds;
        while (box.owed >= kFrame) {  // CreateParticleFpsChecked: one a reference frame
            box.owed -= kFrame;
            throwTongue(box);
        }
    }
}

void SmallFires::gather(gfx::Effects& effects) const {
    if (!open_ || !bgfx::isValid(sheet_)) return;
    for (const Tongue& one : tongues_) {
        // MU's light falls by 1.007 a frame; here it is the heat, which the fire's ramp turns
        // from a yellow-white core through orange to a red tip (fs_effect.sc's flame).
        const float life = one.age / kLife;
        const float heat = kHeat * std::pow(1.0f / 1.007f, one.age) * (1.0f - 0.6f * life);
        const float fade = std::min(1.0f, one.age / kEaseIn) * std::min(1.0f, (1.0f - life) / kEaseOut);
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.at[i];
        sprite.colour[0] = kGain;
        sprite.colour[1] = heat;
        sprite.colour[2] = 0.0f;
        sprite.colour[3] = fade;
        sprite.halfWidth = sprite.halfHeight = 0.5f * kSheetMetres * kNarrow * one.scale;
        sprite.spin = one.spin;
        sprite.sheet = sheet_;
        sprite.blend = gfx::Blend::Flame;
        effects.add(sprite);
    }
}
}  // namespace mu::game
