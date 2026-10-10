#include "game/ui/capsule.h"

#include <algorithm>

#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game::capsule {

// The class's letters in its cell (the user, 2026-10-10: "we need to find place ehre to show
// class"), each in a muted tint of its own so the elf and the tank are found at a glance.
const char* const kKinShort[4] = {"DW", "FE", "DK", "MG"};
const uint32_t kKinInk[4] = {
    gfx::rgba(0.627f, 0.757f, 1.0f),    // Dark Wizard, a cold blue
    gfx::rgba(0.682f, 0.918f, 0.604f),  // Fairy Elf, a leaf green
    gfx::rgba(1.0f, 0.651f, 0.529f),    // Dark Knight, rust
    gfx::rgba(0.859f, 0.714f, 1.0f),    // Magic Gladiator, violet
};

namespace {

// The rest from "work on paddings,lineheight,spacing,colors,gradients, shadows,outlines" (the
// user, 2026-10-10).
constexpr uint32_t kWellTop = gfx::rgba(0.010f, 0.006f, 0.006f, 0.86f);  // the empty capsule,
constexpr uint32_t kWellFoot = gfx::rgba(0.040f, 0.026f, 0.022f, 0.80f); // a touch lighter at its foot
constexpr uint32_t kRim = gfx::rgba(0.0f, 0.0f, 0.0f, 0.72f);             // a hairline round it
constexpr uint32_t kBand = gfx::rgba(0.071f, 0.016f, 0.016f, 0.86f);      // behind the name, fading out
constexpr uint32_t kSeam = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);            // between the bands
constexpr uint32_t kSheen = gfx::rgba(1.0f, 0.93f, 0.85f, 0.14f);         // the health's upper light
constexpr uint32_t kGleam = gfx::rgba(1.0f, 0.95f, 0.88f, 0.10f);         // the capsule's top edge
// The fills, each a top and a foot.
constexpr uint32_t kShieldTop = gfx::rgba(1.0f, 0.780f, 0.420f), kShieldFoot = gfx::rgba(0.851f, 0.525f, 0.180f);
constexpr uint32_t kLifeTop = gfx::rgba(0.800f, 0.200f, 0.157f), kLifeFoot = gfx::rgba(0.420f, 0.071f, 0.055f);
constexpr uint32_t kLowTop = gfx::rgba(0.937f, 0.388f, 0.259f), kLowFoot = gfx::rgba(0.588f, 0.145f, 0.086f);
constexpr uint32_t kAwayTop = gfx::rgba(0.459f, 0.380f, 0.365f), kAwayFoot = gfx::rgba(0.255f, 0.204f, 0.196f);
constexpr uint32_t kManaTop = gfx::rgba(0.302f, 0.553f, 0.957f), kManaFoot = gfx::rgba(0.118f, 0.267f, 0.612f);
constexpr uint32_t kTrail = gfx::rgba(0.965f, 0.784f, 0.682f, 0.80f);  // health just lost
constexpr uint32_t kPulse = gfx::rgba(1.0f, 0.478f, 0.333f);           // under 30%, breathing
constexpr uint32_t kDead = gfx::rgba(0.878f, 0.400f, 0.349f);          // the word
constexpr uint32_t kNumber = gfx::rgba(0.980f, 0.945f, 0.890f);
constexpr uint32_t kTag = gfx::rgba(0.788f, 0.733f, 0.659f, 0.85f);    // the level after the name
constexpr uint32_t kCell = gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f);         // the class cell
constexpr uint32_t kGhost = gfx::rgba(0.553f, 0.518f, 0.471f);         // a dead one's name
constexpr float kAway = 0.6f;  // a member on another map, this much of himself

// A band's fill from the left, rounded at the capsule's corners only where it reaches them.
void fill(gfx::Canvas& canvas, const gfx::Box& band, float share, uint32_t top, uint32_t foot, float a,
          float topRadius, float footRadius) {
    if (share <= 0.0f) return;
    const float w = band.w * std::min(share, 1.0f);
    const bool full = share >= 0.999f;
    const float r[4] = {topRadius, full ? topRadius : 0.0f, full ? footRadius : 0.0f, footRadius};
    if (topRadius > 0.0f || footRadius > 0.0f) {
        tip::rounded(canvas, {band.x, band.y, w, band.h}, r, faded(top, a), faded(foot, a));
    } else {
        canvas.shade({band.x, band.y, w, band.h}, faded(top, a), faded(top, a), faded(foot, a), faded(foot, a));
    }
}

}  // namespace

uint32_t faded(uint32_t abgr, float alpha) {
    const float a = float(abgr >> 24) / 255.0f * std::clamp(alpha, 0.0f, 1.0f);
    return (abgr & 0xFFFFFFu) | (uint32_t(a * 255.0f + 0.5f) << 24);
}

void crown(gfx::Canvas& canvas, float x, float base, float s, uint32_t ink) {
    canvas.rect({x, base - 3.0f * s, 12.0f * s, 3.0f * s}, ink);
    canvas.rect({x, base - 9.0f * s, 2.5f * s, 6.0f * s}, ink);
    canvas.rect({x + 4.75f * s, base - 10.0f * s, 2.5f * s, 7.0f * s}, ink);
    canvas.rect({x + 9.5f * s, base - 9.0f * s, 2.5f * s, 6.0f * s}, ink);
}

float draw(gfx::Canvas& canvas, float x, float y, float u, float wide, const Make& make,
           const Look& c, float pulse) {
    const float a = c.away ? kAway : 1.0f;
    const float r = make.radius * u;
    const float sh = make.shield * u, lh = make.life * u, mh = make.mana * u, tall = sh + lh + mh;
    const gfx::Box whole{x, y, wide, tall};
    // Two dark rounded layers under it, dropped and grown a little: tip::shadowUnder's falloffs
    // are a card's, and under a see-through capsule they read as a grey box round it.
    for (const float grow : {3.0f, 1.5f}) {
        const float g = grow * u, rr[4] = {r + g, r + g, r + g, r + g};
        tip::rounded(canvas, {x - g, y - g + 1.5f * u, wide + 2.0f * g, tall + 2.0f * g}, rr,
                     faded(kShadow, a * (grow > 2.0f ? 0.5f : 1.0f)));
    }
    const float rim[4] = {r + u, r + u, r + u, r + u};
    tip::rounded(canvas, {x - u, y - u, wide + 2.0f * u, tall + 2.0f * u}, rim, faded(kRim, a));
    const float corners[4] = {r, r, r, r};
    tip::rounded(canvas, whole, corners, faded(kWellTop, a), faded(kWellFoot, a));
    // SD along the top, health the body, mana the foot.
    if (c.shield >= 0.0f) fill(canvas, {x, y, wide, sh}, c.shield, kShieldTop, kShieldFoot, a, r, 0.0f);
    const gfx::Box body{x, y + sh, wide, lh};
    const bool low = !c.dead && !c.away && c.life * 100.0f < kLow;
    fill(canvas, body, c.life, c.away ? kAwayTop : low ? kLowTop : kLifeTop,
         c.away ? kAwayFoot : low ? kLowFoot : kLifeFoot, a, 0.0f, 0.0f);
    if (c.trail > c.life + 0.002f) {
        canvas.rect({x + wide * c.life, body.y, wide * (std::min(c.trail, 1.0f) - c.life), lh}, faded(kTrail, a));
    }
    if (low && pulse > 0.0f) canvas.rect({x, body.y, wide * c.life, lh}, faded(kPulse, 0.42f * pulse));
    // The sheen over the health's upper half: light, fading down.
    if (c.life > 0.0f) {
        const float lit = wide * std::min(c.life, 1.0f);
        canvas.shade({x, body.y, lit, lh * 0.45f}, faded(kSheen, a), faded(kSheen, a), 0u, 0u);
    }
    if (c.mana >= 0.0f) fill(canvas, {x, y + sh + lh, wide, mh}, c.mana, kManaTop, kManaFoot, a, 0.0f, r);
    canvas.rect({x, body.y, wide, 0.75f * u}, kSeam);
    canvas.rect({x, body.y + lh - 0.75f * u, wide, 0.75f * u}, kSeam);
    canvas.rect({x + r, y, wide - 2.0f * r, 0.75f * u}, faded(kGleam, a));
    // The dark band behind the name, fading into the fill.
    const float band = wide * make.band;
    canvas.shade({x, body.y, band * 0.4f, lh}, faded(kBand, a), faded(kBand, a * 0.85f), faded(kBand, a * 0.85f),
                 faded(kBand, a));
    canvas.shade({x + band * 0.4f, body.y, band * 0.6f, lh}, faded(kBand, a * 0.85f), 0u, 0u, faded(kBand, a * 0.85f));

    // The class's cell at the left end, a square of the health's height.
    float at = x + make.pad * u;
    if (make.cell > 0.0f && c.kin >= 0 && c.kin < 4) {
        canvas.rect({x, body.y, lh, lh}, faded(kCell, a));
        const float size = make.cell * u;
        const std::string letters = kKinShort[c.kin];
        const float wideLetters = controls::capsWidth(size, letters, 0.06f);
        controls::caps(canvas, x + (lh - wideLetters) * 0.5f, controls::middle(body.y, lh, size), size,
                       faded(c.dead ? kGhost : kKinInk[c.kin], a), letters, 0.06f);
        at = x + lh + 6.0f * u;
    }
    // The leader's badge (the user, 2026-10-10: "we need clear icon who is the party leader"):
    // a crown a size up, standing on the capsule's top left corner over a dark drop.
    if (c.leader && c.crown) {
        // Never dimmed: whoever leads is said in full, even from another map.
        const float s = make.name / 14.5f * 1.5f * u, bx = x - 4.0f * u, foot = y + 4.0f * u;
        crown(canvas, bx + 1.2f * u, foot + 1.5f * u, s, kCrownDrop);
        crown(canvas, bx, foot, s, kGold);
    }
    // The words, their capitals centred on the health.
    const float base = controls::middle(body.y, lh, make.name * u);
    const uint32_t ink = c.dead ? kGhost : c.leader ? kGold : style::kBoneHi;
    const float named = controls::label(canvas, at, base, make.name * u, c.leader ? ink : faded(ink, a), c.name);
    if (c.dead) canvas.rect({at, base - make.name * u * 0.3f, named, 1.0f * u}, faded(kGhost, a));
    if (!c.tag.empty() && make.tag > 0.0f) {
        controls::caps(canvas, at + named + 6.0f * u, base, make.tag * u, faded(kTag, a), c.tag, 0.1f);
    }
    const float right = x + wide - make.pad * u;
    const float numberBase = controls::middle(body.y, lh, make.number * u);
    if (c.dead) {
        controls::caps(canvas, right - controls::capsWidth(make.number * u, "Dead"), numberBase, make.number * u,
                       kDead, "Dead");
    } else if (!c.number.empty()) {
        controls::ranged(canvas, right, numberBase, make.number * u, faded(c.away ? style::kBone2 : kNumber, a), c.number);
    }
    return tall + make.gap * u;
}

}  // namespace mu::game::capsule
