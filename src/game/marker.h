// Where a click sent him: MU's MoveTargetPosEffect, ported from MU2's `client/core/Marker.cs`.
//
// Three parts, all additive, all tinted MU's (1.0, 0.7, 0.3). MU's fourth, the runic circle
// (empact01), is left out on purpose: the user wanted it simpler.
//
//   * the RINGS, cursorpin01 -- four spikes that close in on the spot, a new one every 0.6 s;
//   * the PULSE, cursorpin02 -- a star of four points that opens and closes;
//   * the PIN, MoveTargetPosEffect.obj over gra.png -- the three-bladed arrow standing on it.
//
// MU2's timings are kept as its reference frames at 25 a second, and every length is MU2's in
// metres. What is NOT MU2's, each marked "invention" where it is done:
//
//   * the flat parts lie ON the land, cut into a grid whose corners each take the ground's
//     height, where MU2 laid one flat quad at the height under the centre and let a slope eat
//     half of it;
//   * the pin drops onto the spot and turns, where MU2 stood it still;
//   * an arrival fades it in a sixth of a second, where MU2 cut it;
//   * a second click on the same tile puts it down again, where MU2's saw no change, and a
//     click within 0.4 s of the last moves it without replaying it, so spam does not strobe.
//
// It is `game`: it knows a ground and a click. The pass it draws through knows neither.
#pragma once

#include <string>
#include <vector>

#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

class Marker {
public:
    // Takes the three sheets and the pin. Returns false only when nothing at all could be had;
    // a missing part is logged and the rest are drawn.
    bool open(const std::string& assetDir, content::Textures& textures);
    void shutdown();

    // A new walk to (x, z), world metres: the whole effect starts again from its first frame.
    void show(float x, float z);
    // He got there, or the walk was given up: fade out quickly rather than pop.
    void dismiss();

    void update(float seconds);
    void gather(gfx::Effects& effects, const content::Ground& ground) const;

    bool live() const { return live_; }

private:
    // One flat part: a square `across` metres wide centred on the marker, turned by `turn`
    // radians about the vertical, cut into a grid that follows the land.
    void lay(gfx::Effects& effects, const content::Ground& ground, bgfx::TextureHandle sheet,
             float across, float turn, float alpha) const;

    bgfx::TextureHandle rings_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle pulse_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle pinSheet_ = BGFX_INVALID_HANDLE;
    // The pin's triangles, already in metres, three corners and three UVs each.
    struct Corner {
        float x, y, z, u, v;
    };
    std::vector<Corner> pin_;

    bool live_ = false;
    float x_ = 0.0f, z_ = 0.0f;          // where it is drawn
    float lived_ = 0.0f;                 // reference frames since show()
    float leaving_ = -1.0f;              // seconds of the arrival fade left, or < 0
    float sinceRing_ = 0.0f;             // reference frames since the last ring was born
    float ring_[3] = {0.0f, 0.0f, 0.0f}; // each ring's width in metres, 0 for a free slot
    float pulseAcross_ = 1.8f;
    bool pulseOpening_ = false;
    float seconds_ = 0.0f;               // real seconds since show(), for the pin's drop
    float sinceClick_ = 1.0f;            // real seconds since the last click, replayed or not
};

}  // namespace mu::game
