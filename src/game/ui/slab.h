// The fat rounded button: the game menu's, and the character screen's.
//
// Chosen by the user for the game menu on 2026-09-27 -- *"clean, fat but with diablo 4 vibe"*,
// *"cleaner with rounder radius"*, *"a little bit simpler, without icons"* -- and asked for again
// on the character screen in place of MU's leather plates: *"we need that but with our button
// designs"*. A rounded slab under a rim lit from above, an engraved line a few pixels inside it,
// a breath of light over its top half, and its word in the windows' title face. Under the pointer
// the rim and the word go gold -- red for a danger -- and a warm light rises from its foot.
#pragma once

#include <cstdint>
#include <string>

#include "gfx/interface.h"

namespace mu::game::slab {

struct Tone {
    float r, g, b, a;
    Tone mix(const Tone& o, float t) const {
        return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t};
    }
    Tone times(float k) const { return {r, g, b, a * k}; }
    uint32_t packed() const { return gfx::rgba(r, g, b, a); }
};

inline constexpr Tone kBronze{0.643f, 0.573f, 0.404f, 1.0f};
inline constexpr Tone kPale{0.941f, 0.847f, 0.604f, 1.0f};  // sheet::ink::kTitle
inline constexpr Tone kRed{1.0f, 0.451f, 0.373f, 1.0f};     // sheet.cpp's kBlockedEdge
inline constexpr Tone kEmber{0.886f, 0.290f, 0.220f, 1.0f}; // and its kBlockedBack
inline constexpr Tone kWarm{1.0f, 0.745f, 0.314f, 1.0f};
inline constexpr Tone kInkRest{0.808f, 0.792f, 0.737f, 0.82f};
inline constexpr Tone kSlabTop{0.110f, 0.102f, 0.090f, 0.98f};
inline constexpr Tone kSlabFoot{0.059f, 0.055f, 0.051f, 0.98f};

enum class Kind : uint8_t { Plain, Danger, Inactive };

// One button. `lift` is the eased hover, 0 to 1 -- a chosen button is drawn at 1 whether or not
// the pointer is on it. `size` is the word's height and `radius` the corner, both already in
// pixels' worth of `u`, which is the caller's unit (the menu's is tip::unit() at 85%).
void draw(gfx::Canvas& canvas, const gfx::Box& box, float radius, const std::string& word,
          float size, Kind kind, float lift, bool pressed, float u);

// The hover eased for drawing: smoothstep on the raw 0-to-1 lift.
inline float eased(float lift) { return lift * lift * (3.0f - 2.0f * lift); }

}  // namespace mu::game::slab
