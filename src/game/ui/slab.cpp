#include "game/ui/slab.h"

#include <algorithm>
#include <cmath>

#include "game/ui/panel.h"
#include "game/ui/sheet.h"
#include "game/ui/tip.h"

namespace mu::game::slab {

namespace {
constexpr float kInset = 4.0f;  // the engraved line inside a slab
}

void draw(gfx::Canvas& canvas, const gfx::Box& box, float radius, const std::string& word,
          float size, Kind kind, float lift, bool pressed, float u) {
    using gfx::Box;
    const bool live = kind != Kind::Inactive;
    const float t = live ? lift : 0.0f;
    const Tone accent = kind == Kind::Danger ? kRed : kPale;
    const Tone glow = kind == Kind::Danger ? kEmber : kWarm;
    const float line = std::max(1.0f, std::round(u));
    const Box body = pressed && live ? Box{box.x, box.y + line, box.w, box.h} : box;

    const Tone rimTop = live ? kBronze.times(0.55f).mix(accent.times(0.90f), t)
                             : kBronze.times(0.22f);
    const Tone rimFoot = live ? kBronze.times(0.18f).mix((kind == Kind::Danger ? kEmber : kBronze)
                                                              .times(0.45f), t)
                              : kBronze.times(0.08f);
    const Tone ink = !live ? kInkRest.times(0.36f)
                   : pressed ? Tone{1.0f, 1.0f, 1.0f, 0.95f}
                             : kInkRest.mix(accent.times(0.97f), t);

    // The drop under it, and the light spilling round it while lifted.
    if (!pressed) tip::shadowUnder(canvas, body, 0.6f, radius + line);
    if (t > 0.0f) {
        for (const float spread : {9.0f, 5.0f, 2.5f}) {
            const float g = spread * u;
            tip::panel(canvas, body.grown(line + g), radius + line + g,
                       glow.times(0.07f * t).packed(), glow.times(0.12f * t).packed());
        }
    }
    tip::panel(canvas, body.grown(line), radius + line, rimTop.packed(), rimFoot.packed());
    tip::panel(canvas, body, radius, kSlabTop.packed(), kSlabFoot.packed());

    // The engraved line: a ring of bronze a few pixels in, cut back to the slab inside it.
    const float in = kInset * u;
    const Box inner{body.x + in, body.y + in, body.w - in * 2.0f, body.h - in * 2.0f};
    const float innerRadius = std::max(2.0f, radius - in);
    const Tone engraved = live ? kBronze.times(0.14f).mix(accent.times(0.24f), t)
                               : kBronze.times(0.07f);
    tip::panel(canvas, inner, innerRadius, engraved.packed(), engraved.packed());
    tip::panel(canvas, inner.grown(-line), innerRadius - line,
               kSlabTop.mix(kSlabFoot, in / body.h).packed(),
               kSlabFoot.mix(kSlabTop, in / body.h).packed());

    // A breath of light over the top half, and the warm pool rising from the foot.
    const Box upper{inner.x + line, inner.y + line, inner.w - line * 2.0f,
                    (inner.h - line * 2.0f) * 0.5f};
    const float top[4] = {innerRadius - line, innerRadius - line, 0.0f, 0.0f};
    tip::rounded(canvas, upper, top, gfx::rgba(1.0f, 0.98f, 0.92f, pressed ? 0.02f : 0.06f),
                 gfx::rgba(1.0f, 0.98f, 0.92f, 0.0f));
    if (t > 0.0f) {
        const float lowTall = (inner.h - line * 2.0f) * 0.7f;
        const Box lower{inner.x + line, inner.bottom() - line - lowTall, inner.w - line * 2.0f,
                        lowTall};
        const float foot[4] = {0.0f, 0.0f, innerRadius - line, innerRadius - line};
        tip::rounded(canvas, lower, foot, glow.times(0.0f).packed(), glow.times(0.20f * t).packed());
    }

    // The word, in the windows' title face where it baked, centred on its capitals' height.
    const std::string caps = sheet::shouted(word);
    const float px = size * u;
    const gfx::Face* title = panel::titleFace();
    const gfx::Face& face = title ? *title : canvas.face();
    const float tracking = px * 0.14f;
    const float wide = face.measure(px, caps) + tracking * float(caps.size() - 1);
    const float x = std::round(body.midX() - wide * 0.5f);
    const float baseline = std::round(body.y + (body.h + face.ascent(px) * 0.72f) * 0.5f);
    if (title) {
        canvas.lettered(face, panel::titleTexture(), x + 1.0f, baseline + 1.0f, px, tracking,
                        tip::ink::kDrop, caps);
        canvas.lettered(face, panel::titleTexture(), x, baseline, px, tracking, ink.packed(),
                        caps);
    } else {
        tip::tracked(canvas, x, baseline, px, 0.14f, ink.packed(), caps, 1.0f);
    }
}

}  // namespace mu::game::slab
