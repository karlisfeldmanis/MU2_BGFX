// The quest marker: a gold exclamation over the head of a townsperson who has a quest to give,
// bobbing slowly.
//
// Ours: MU has no marker. MuMain's quest NPC is found by walking up to him. The shape follows the
// Sanctuary taste (docs of 2026-09-27): one clean glyph, round where it is round, no diamond, no
// frame and no ornament -- a tapered bar and a round dot, with a dark rim so it reads over
// bright stone as well as over the night. Flat, as the user asked (2026-09-28): no gradient and
// no glow; the detail is in flat shapes -- two facets split down the middle, an engraved line
// just inside the edge, one short highlight and a hard shadow.
//
// The picture is baked in code, as the controls' stone is, at the size it is drawn: a signed
// distance per pixel, sixteen samples a pixel, rebuilt only when the interface unit changes. So
// it is sharp at any resolution and carries no asset.
#pragma once

#include <bgfx/bgfx.h>

#include "gfx/interface.h"

namespace mu::game {

class Play;

class Beacon {
public:
    void open(const gfx::Interface& interface) { interface.adopt(canvas_); }
    void close();
    // A frame. Rebuilt every frame one is up, since it bobs and follows the camera; cleared and
    // left alone when nobody on screen has a quest.
    void update(float seconds, const Play& play, const float* viewProj, int width, int height);
    void dismiss() { canvas_.clear(); showing_ = false; }

    bool showing() const { return showing_; }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    // The mark at `unit` pixels to the interface unit.
    bool bake(float unit);

    gfx::Canvas canvas_;
    bool showing_ = false;
    float clock_ = 0.0f;
    float bakedUnit_ = 0.0f;
    bgfx::TextureHandle texture_ = BGFX_INVALID_HANDLE;
    gfx::Art art_;
    int cellW_ = 0, cellH_ = 0;
};

}  // namespace mu::game
