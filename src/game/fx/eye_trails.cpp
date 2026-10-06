#include "game/fx/eye_trails.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kUnit = 0.01f;
constexpr float kFrames = 25.0f;
// Scale 30: the ribbon 30 units across.
constexpr float kHalfWidth = 30.0f * kUnit * 0.5f;
// Light (0.5, 0.1, 1.0), subtypes 2 and 3 (ZzzEffectJoint.cpp:443-444).
constexpr float kColour[3] = {0.5f, 0.1f, 1.0f};
// Two a monster, for every Tarkan monster in sight.
constexpr size_t kTrails = 128;

}  // namespace

bool EyeTrails::open(const std::string& assetDir, content::Textures& textures,
                     const content::Showing& table) {
    const content::EffectSheet* sheet = table.effect("joint_energy");
    if (sheet == nullptr) {
        core::logError("eye trails: no cooked effect named 'joint_energy'");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    trails_.reserve(kTrails);
    open_ = bgfx::isValid(sheet_);
    return open_;
}

void EyeTrails::shutdown() {
    trails_.clear();
    open_ = false;
}

void EyeTrails::update(float seconds) {
    trails_.erase(std::remove_if(trails_.begin(), trails_.end(),
                                 [](const Trail& one) { return !one.fed; }),
                  trails_.end());
    for (Trail& one : trails_) {
        one.fed = false;
        // MoveJoint lays a tail each reference frame; the head is the eye's own point.
        one.clock += seconds;
        while (one.clock >= 1.0f / kFrames) {
            one.clock -= 1.0f / kFrames;
            for (int t = std::min(one.count, one.most - 1); t > 0; --t) {
                for (int k = 0; k < 3; ++k) one.tails[t][k] = one.tails[t - 1][k];
            }
            for (int k = 0; k < 3; ++k) one.tails[0][k] = one.head[k];
            one.count = std::min(one.count + 1, one.most);
        }
    }
}

void EyeTrails::feed(uint32_t id, bool left, const float at[3]) {
    if (!open_) return;
    Trail* found = nullptr;
    for (Trail& one : trails_) {
        if (one.id == id && one.left == left) found = &one;
    }
    if (found == nullptr) {
        if (trails_.size() >= kTrails) return;
        trails_.push_back(Trail{});
        found = &trails_.back();
        found->id = id;
        found->left = left;
        found->most = left ? 20 : 8;
    }
    found->fed = true;
    for (int k = 0; k < 3; ++k) found->head[k] = at[k];
}

void EyeTrails::gather(gfx::Effects& effects, const float* eye) const {
    if (!open_) return;
    for (const Trail& one : trails_) {
        // The head, then the tails laid behind it.
        const int points = one.count + 1;
        if (points < 2) continue;
        auto pointAt = [&one](int i) { return i == 0 ? one.head : one.tails[i - 1]; };
        for (int t = 0; t + 1 < points; ++t) {
            const float* a = pointAt(t);
            const float* b = pointAt(t + 1);
            const float along[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            // Across the segment and the line to the eye, so the ribbon faces the camera.
            float toEye[3] = {0.0f, 1.0f, 0.0f};
            if (eye != nullptr) {
                for (int k = 0; k < 3; ++k) toEye[k] = eye[k] - a[k];
            }
            float side[3] = {along[1] * toEye[2] - along[2] * toEye[1],
                             along[2] * toEye[0] - along[0] * toEye[2],
                             along[0] * toEye[1] - along[1] * toEye[0]};
            const float length =
                std::sqrt(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
            if (length < 1e-6f) continue;
            for (float& s : side) s *= kHalfWidth / length;
            gfx::Sprite sprite;
            sprite.placed = true;
            const float* ends[2] = {a, b};
            for (int c = 0; c < 4; ++c) {
                const float* at = ends[c == 0 || c == 3 ? 0 : 1];
                const float s = (c == 0 || c == 1) ? -1.0f : 1.0f;
                for (int k = 0; k < 3; ++k) sprite.corner[c][k] = at[k] + side[k] * s;
                sprite.cornerUv[c][0] = float(t + (c == 0 || c == 3 ? 0 : 1)) / float(one.most);
                sprite.cornerUv[c][1] = s < 0.0f ? 0.0f : 1.0f;
            }
            for (int k = 0; k < 3; ++k) sprite.position[k] = (a[k] + b[k]) * 0.5f;
            // Ours: thinning to nothing at the last tail, so the end does not cut off square.
            const float fade = 1.0f - float(t) / float(one.most);
            for (int k = 0; k < 3; ++k) sprite.colour[k] = kColour[k] * fade;
            sprite.colour[3] = 1.0f;
            sprite.sheet = sheet_;
            sprite.blend = gfx::Blend::Additive;
            if (!effects.add(sprite)) return;
        }
    }
}

}  // namespace mu::game
