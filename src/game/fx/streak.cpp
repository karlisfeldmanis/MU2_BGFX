#include "game/fx/streak.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

// MU's clock, which every duration below is stated against.
constexpr float kReference = 25.0f;
// 2.9 reference frames of blade -- 116 ms. See the header.
constexpr float kSpans = 2.9f / kReference;
// And how long a ribbon takes to go once the swing stops feeding it: the client's `Short`
// lifetime of 15 reference frames, during which `MoveBlurs` also drops a point a frame, so it
// retracts from the tail as well as dimming. Both, here.
constexpr float kFades = 15.0f / kReference;
// How bright the ribbon is at the blade. Additive and white; the sheet carries the shape.
constexpr float kBrightest = 0.85f;

}  // namespace

bool Streak::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table) {
    clear();
    const content::EffectSheet* sheet = table.effect("trail_skill");
    if (sheet == nullptr) {
        // Not fatal and not silent: the skill still swings, and the log says what is missing.
        core::logError("streak: no cooked effect named 'trail_skill'; skills swing without one");
        return false;
    }
    sheet_ = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    core::logf("streak: the skill ribbon is %s (%s)", bgfx::isValid(sheet_) ? "ready" : "missing",
               sheet->path.c_str());
    return bgfx::isValid(sheet_);
}

void Streak::clear() {
    for (Ribbon& one : ribbons_) {
        one.id = 0;
        one.count = 0;
        one.idle = 0.0f;
    }
}

Streak::Ribbon* Streak::ribbonFor(uint32_t id) {
    for (Ribbon& one : ribbons_) {
        if (one.id == id) return &one;
    }
    // A free one, else the stalest -- a ribbon nobody is feeding is the right thing to take.
    Ribbon* pick = nullptr;
    for (Ribbon& one : ribbons_) {
        if (one.id == 0 || one.count == 0) {
            pick = &one;
            break;
        }
        if (pick == nullptr || one.idle > pick->idle) pick = &one;
    }
    if (pick == nullptr) return nullptr;
    pick->id = id;
    pick->count = 0;
    pick->idle = 0.0f;
    return pick;
}

void Streak::feed(uint32_t id, const float from[3], const float to[3]) {
    if (!isOpen()) return;
    Ribbon* ribbon = ribbonFor(id);
    if (ribbon == nullptr) return;
    ribbon->idle = 0.0f;
    // Newest first, so the head of the array is the blade and the tail is the oldest sample --
    // which is the order the quads are laid in and the order the fade runs along.
    if (ribbon->count < kPairs) ++ribbon->count;
    for (int i = ribbon->count - 1; i > 0; --i) ribbon->pairs[i] = ribbon->pairs[i - 1];
    Pair& head = ribbon->pairs[0];
    for (int i = 0; i < 3; ++i) {
        head.a[i] = from[i];
        head.b[i] = to[i];
    }
    head.age = 0.0f;
}

void Streak::update(float seconds) {
    for (Ribbon& one : ribbons_) {
        if (one.count == 0) continue;
        one.idle += seconds;
        for (int i = 0; i < one.count; ++i) one.pairs[i].age += seconds;
        // The span rolls off the tail: anything older than 2.9 reference frames is not blade any
        // more. While the swing is being fed this is what holds the ribbon at its length.
        while (one.count > 0 && one.pairs[one.count - 1].age > kSpans) --one.count;
        // And once nothing is feeding it, it retracts as well as dimming -- MU drops a point a
        // frame over the `Short` lifetime, which is a pair every `kFades / kPairs`.
        if (one.idle > 0.0f && one.count > 0) {
            const int left = int(float(kPairs) * (1.0f - std::min(1.0f, one.idle / kFades)));
            one.count = std::min(one.count, std::max(0, left));
        }
        if (one.count == 0) one.id = 0;
    }
}

void Streak::gather(gfx::Effects& effects) const {
    if (!isOpen()) return;
    for (const Ribbon& one : ribbons_) {
        if (one.count < 2) continue;
        // What is left of it as a whole, once the swing has stopped feeding it.
        const float leaving =
            one.idle <= 0.0f ? 1.0f : std::max(0.0f, 1.0f - one.idle / kFades);
        for (int i = 0; i + 1 < one.count; ++i) {
            const Pair& near = one.pairs[i];
            const Pair& far = one.pairs[i + 1];
            // Along the ribbon: full at the blade, nothing at the tail. Squared, because a
            // linear ramp over 29 quads reads as a band with an edge on it rather than a smear.
            const float atNear = 1.0f - float(i) / float(one.count);
            const float atFar = 1.0f - float(i + 1) / float(one.count);
            const float alpha = kBrightest * leaving * atNear * atNear;
            if (alpha <= 0.004f) continue;

            gfx::Sprite quad;
            quad.placed = true;
            quad.blend = gfx::Blend::Additive;
            quad.sheet = sheet_;
            quad.colour[0] = quad.colour[1] = quad.colour[2] = 1.0f;
            quad.colour[3] = alpha;
            // The quad's own corners: the grip end and the tip of two consecutive samples, in
            // the winding the pass wants (bottom left, bottom right, top right, top left).
            for (int k = 0; k < 3; ++k) {
                quad.corner[0][k] = near.a[k];
                quad.corner[1][k] = far.a[k];
                quad.corner[2][k] = far.b[k];
                quad.corner[3][k] = near.b[k];
                quad.position[k] = (near.a[k] + far.b[k]) * 0.5f;
            }
            // The sheet runs ALONG the ribbon -- u is how far down the streak this quad sits,
            // v is across the blade -- so one picture is stretched over the whole arc rather
            // than repeated once a quad, which is what MU's single blur bitmap is for.
            const float u0 = 1.0f - atNear, u1 = 1.0f - atFar;
            quad.cornerUv[0][0] = u0; quad.cornerUv[0][1] = 1.0f;
            quad.cornerUv[1][0] = u1; quad.cornerUv[1][1] = 1.0f;
            quad.cornerUv[2][0] = u1; quad.cornerUv[2][1] = 0.0f;
            quad.cornerUv[3][0] = u0; quad.cornerUv[3][1] = 0.0f;
            effects.add(quad);
        }
    }
}

}  // namespace mu::game
