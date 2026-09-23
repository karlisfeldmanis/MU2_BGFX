// The figures a fight puts up: the damage over each body, and what he gained in a lane over
// the HUD.
//
// The user chose this on 2026-09-23 from a design page -- concept **B2**, "weighted by the blow,
// flat" -- and then replaced its typeface on sight of the first build: everything here is set in
// **Cinzel Bold**, a weight up from the map name's own face, at four sizes. What survives of B2
// is everything the page argued. The whole of the style is three rules, and they are worth
// stating here because every number below is one of them made exact:
//
//   * **Size is force, hue is kind.** A swing, a skill and a critical are one ramp: 21, 27 and
//     32 interface units in the same face and the same weight. Nothing else about a figure
//     changes -- no second typeface, no outline, no bloom, no tilt, no ring.
//   * **A critical is a step and not an event.** The user's word, and the reason is arithmetic:
//     with the luck option rolled a critical lands several times a second, and anything that
//     announces itself would be the loudest thing on screen for the whole hunt.
//   * **A gain is not a blow.** Experience, Zen and a potion belong to him and to no body on
//     the map, so they leave the fight entirely and stack in a quiet lane over the HUD.
//
// Where it sits in the drawing: `Showing` owns the figures -- it anchors one over the body a
// blow landed on and ages it (game/fx/showing.h) -- and this owns what they LOOK like. That is
// the same division the monster's health plate already has, and it is why the pop, the rise,
// the lean and the fade are all here: every one of them is in screen pixels, on a point
// projected from the world, and none of them is a fact the realm could read.
//
// What this replaces: MU's own bitmap digits out of Data/Interface/FontTest.OZT, drawn as
// camera-facing quads in the transparent pass. A sheet of ten cells cannot carry a ramp -- one
// size, one weight, one word -- and that is the whole reason the figures moved into the
// interface. The blood did not; it is still sprites, still in the pass, still MU's.
#pragma once

#include <cstdint>
#include <vector>

#include "gfx/interface.h"

namespace mu::game {

class Play;

class Tally {
public:
    // Bakes the condensed face twice: sharp, and blurred for the halo every figure is read
    // against over grass. Without them it draws nothing and says so.
    void open(const gfx::Interface& interface);
    void shutdown();

    // A frame, after Play::update and after the bodies have been placed -- the figures hang on
    // points in the world, so this wants the camera that drew them. `hudTop` is the top edge of
    // the HUD plate in backbuffer pixels, which the gain lane stacks up from.
    void update(float seconds, const Play& play, const float* viewProj, int width, int height,
                float hudTop);
    // Takes everything down now: leaving the world, not the fight.
    void dismiss();

    bool showing() const { return !canvas_.empty(); }
    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    // One line in the lane over the HUD. `Died` is the one that is not a gain and does not
    // carry a figure: it is said in the same three square inches because that is where the eye
    // already is when it happens.
    struct Row {
        enum class Kind : uint8_t { Experience, Zen, Health, Mana, Died };
        Kind kind = Kind::Experience;
        int64_t value = 0;
        float age = 0.0f;
    };

    void collect(const Play& play, float seconds);
    // The hairline and diamond under the death, drawn as the arrival draws its own: two rules
    // fading outward from a turned square, each over its Gaussian box-shadow.
    void rule(float centre, float middle, float unit, float opacity, float drawn, float mark);
    // The scrim behind the whole death block -- the arrival's own soft cloud, in the death's
    // red rather than its black, laid as a grid of shaded quads so it has no edge anywhere.
    void scrim(float centre, float middle, float unit, float opacity);
    void rebuild(const Play& play, const float* viewProj, int width, int height, float hudTop);

    gfx::Canvas canvas_;
    // The figures' own face and its halo: the same bake blurred, drawn under every figure in
    // black. A halo rather than a drop shadow because a drop only guarantees contrast on two of
    // a glyph's four sides -- Vitals wrote that down first, over the same grass.
    gfx::Face face_, halo_;
    bgfx::TextureHandle faceTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle haloTexture_ = BGFX_INVALID_HANDLE;
    // The death's second, wider halo: the map name's own soft shadow, so the one line the game
    // says when he falls is lit exactly as the line it says when he arrives somewhere.
    gfx::Face soft_;
    bgfx::TextureHandle softTexture_ = BGFX_INVALID_HANDLE;
    // And the lane's own face, Cinzel Medium: every row in it -- experience, Zen, a potion --
    // is one size and one weight, and that weight is not the fight's.
    gfx::Face quiet_;
    bgfx::TextureHandle quietTexture_ = BGFX_INVALID_HANDLE;

    std::vector<Row> lane_;
    // Zen rolled up: a good hunt pays a pile a second, and a figure for each reads as a slot
    // machine. They are summed for a second and posted once -- the design page's question 4.
    int64_t heldZen_ = 0;
    float zenAge_ = 0.0f;
    uint64_t rebuilds_ = 0;
    bool drawn_ = false;  // the canvas holds something, so it must be cleared when empty
};

}  // namespace mu::game
