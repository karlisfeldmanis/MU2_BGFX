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
// **invention**, the user's, 2026-09-29: a plain blow's ribbon is quieter and soft-edged -- "clean,
// a little blurry". About half the skill's light, and laid in bands across the blade that fade to
// nothing at the grip and the tip, since blur01 is flat across and would stop on a hard line.
constexpr float kPlainBrightest = 0.45f;
constexpr int kBands = 5;

}  // namespace

bool Streak::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table) {
    clear();
    // In `Sheet`'s order. The skill's is the one the rest cannot do without; a missing plain or
    // spear sheet leaves that swing bare and says so.
    static constexpr const char* kNames[3] = {"trail_skill", "trail", "trail_spear"};
    for (int i = 0; i < 3; ++i) {
        sheets_[i] = BGFX_INVALID_HANDLE;
        const content::EffectSheet* sheet = table.effect(kNames[i]);
        if (sheet == nullptr) {
            // Not fatal and not silent: the blow still swings, and the log says what is missing.
            core::logError("streak: no cooked effect named '%s'; its swings lay nothing", kNames[i]);
            continue;
        }
        sheets_[i] = textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        core::logf("streak: the %s ribbon is %s (%s)", kNames[i],
                   bgfx::isValid(sheets_[i]) ? "ready" : "missing", sheet->path.c_str());
    }
    return isOpen();
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

void Streak::feed(uint32_t id, const float from[3], const float to[3], Sheet sheet,
                  const float colour[3]) {
    if (!bgfx::isValid(sheets_[int(sheet)])) return;
    Ribbon* ribbon = ribbonFor(id);
    if (ribbon == nullptr) return;
    ribbon->idle = 0.0f;
    // A skill straight after a swing is the same body's ribbon: it takes the new look whole.
    ribbon->sheet = sheet;
    for (int i = 0; i < 3; ++i) ribbon->colour[i] = colour[i];
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
    for (const Ribbon& one : ribbons_) {
        if (one.count < 2 || !bgfx::isValid(sheets_[int(one.sheet)])) continue;
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
            const bool plain = one.sheet != Sheet::Skill;
            const float alpha =
                (plain ? kPlainBrightest : kBrightest) * leaving * atNear * atNear;
            if (alpha <= 0.004f) continue;
            const float u0 = 1.0f - atNear, u1 = 1.0f - atFar;
            // Across the blade: one band for a skill, kBands for a plain blow, each lit by a
            // sine bump so the ribbon has no edge at either end of the blade.
            const int bands = plain ? kBands : 1;
            for (int band = 0; band < bands; ++band) {
                const float s0 = float(band) / float(bands), s1 = float(band + 1) / float(bands);
                const float across =
                    plain ? std::sin(3.14159265f * (s0 + s1) * 0.5f) : 1.0f;
                gfx::Sprite quad;
                quad.placed = true;
                quad.blend = gfx::Blend::Additive;
                quad.sheet = sheets_[int(one.sheet)];
                for (int k = 0; k < 3; ++k) quad.colour[k] = one.colour[k];
                quad.colour[3] = alpha * across;
                // The quad's own corners: two points along the grip-to-tip line of two
                // consecutive samples, in the winding the pass wants (bottom left, bottom
                // right, top right, top left).
                for (int k = 0; k < 3; ++k) {
                    const float nearAt0 = near.a[k] + (near.b[k] - near.a[k]) * s0;
                    const float nearAt1 = near.a[k] + (near.b[k] - near.a[k]) * s1;
                    const float farAt0 = far.a[k] + (far.b[k] - far.a[k]) * s0;
                    const float farAt1 = far.a[k] + (far.b[k] - far.a[k]) * s1;
                    quad.corner[0][k] = nearAt0;
                    quad.corner[1][k] = farAt0;
                    quad.corner[2][k] = farAt1;
                    quad.corner[3][k] = nearAt1;
                    quad.position[k] = (nearAt0 + farAt1) * 0.5f;
                }
                // The sheet runs ALONG the ribbon -- u is how far down the streak this quad
                // sits, v is across the blade, 1 at the grip and 0 at the tip -- so one picture
                // is stretched over the whole arc rather than repeated once a quad, which is
                // what MU's single blur bitmap is for.
                quad.cornerUv[0][0] = u0; quad.cornerUv[0][1] = 1.0f - s0;
                quad.cornerUv[1][0] = u1; quad.cornerUv[1][1] = 1.0f - s0;
                quad.cornerUv[2][0] = u1; quad.cornerUv[2][1] = 1.0f - s1;
                quad.cornerUv[3][0] = u0; quad.cornerUv[3][1] = 1.0f - s1;
                effects.add(quad);
            }
        }
    }
}

}  // namespace mu::game
