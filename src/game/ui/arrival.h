// The map's name as the character comes in: "LORENCIA" in wide Roman capitals over a hairline
// with a diamond, "TOWN · SAFE ZONE" beneath, just above the HUD. It fades in, holds and goes.
//
// MU's CUIMapName (MuMain UI/Legacy/UIMapName.cpp) is what it stands for: a 166x90 picture per
// map from Local/Eng/ImgsMapName, centred at y 220 of 480, alpha 0.2 to 1 at 0.015 a frame, five
// seconds held, and out again. The user chose a flat redrawing of it on 2026-09-22 from a design
// page (style B, "Inscription", placed above the HUD) and asked for it exactly as drawn there,
// so every number below is that page's, at 1080 lines, and scales with the screen's height:
//
//   name     Cinzel Medium, 35.5 px, uppercase, letter-spacing 0.34 em (0.6 em as it comes in),
//            #ECE8DD; shadows 0 1.9 px blur 17.3 px black 75% and 0 0 blur 3.8 px black 60%
//   rule     two 1 px hairlines 201.6 px long, clear to 85% silver toward a 11.1 px diamond,
//            17.3 px either side of it; each with a box-shadow blur 7.7 px black 80%
//   caption  Open Sans SemiBold 11.1 px, uppercase, letter-spacing 0.42 em, #C2B48F; shadow
//            blur 9.6 px black 90%
//   stack    12.5 px between the three, the caption's foot at 78.5% of the screen's height
//   time     5.4 s: opacity in over the first 13%, out over the last 20%; the letters close
//            up over 22%, the rules draw out from 6% to 24%, the diamond turns in from 12% to
//            26% and the caption comes in from 18% to 32%
//
// One thing is not the page's: a soft black scrim behind the whole stack (52% at its middle, a
// Gaussian 250 by 50 px), which the user asked for once it was seen over Lorencia's bright paving.
//
// A browser's blur is a Gaussian of half its radius; the halos here are the same Gaussian,
// baked once into a blurred copy of each face (Face::blur), so they are that shadow and not
// an imitation of it. The one thing the page drew that this does not is the name's own brief
// blur as it comes in (0.25 cqw, gone by 22%), which is under the fade-in and not missed.
//
// Shown on the way into a world, never on a respawn in the same one: MU calls ShowMapName on
// entering and on changing map.
#pragma once

#include <string>

#include "gfx/interface.h"

namespace mu::game {

class Arrival {
public:
    // Bakes the title face and the three halos. Without them it draws nothing, and says so.
    void open(const gfx::Interface& interface);
    void shutdown();

    // The world's name comes up after `delay` seconds. A world with no entry in the table is
    // named by its own id, capitalised, and has no caption.
    void announce(const std::string& world, float delay);
    // Advances the clock and redraws while it is showing.
    void update(float seconds, float width, float height);

    bool showing() const { return clock_ >= 0.0f && clock_ < kSeconds && !name_.empty(); }
    const gfx::Canvas& canvas() const { return canvas_; }

    static constexpr float kSeconds = 5.4f;

private:
    void rebuild(float width, float height);

    const gfx::Interface* interface_ = nullptr;
    gfx::Canvas canvas_;
    // The name in its face, and three halos: the name's tight and soft shadows and the
    // caption's, each a blurred bake of its own face.
    gfx::Face title_, titleTight_, titleSoft_, captionSoft_;
    bgfx::TextureHandle titleTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle tightTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle softTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle captionTexture_ = BGFX_INVALID_HANDLE;
    std::string name_, caption_;
    float clock_ = -1.0f;  // seconds into the showing; negative while it waits
    bool shown_ = false;   // the canvas holds something, so it must be cleared when done
};

}  // namespace mu::game
