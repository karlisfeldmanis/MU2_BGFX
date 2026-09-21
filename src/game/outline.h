// The gold ring round whatever the pointer is over: MU2's Outline.cs, migrated off Godot.
//
// What stays the same as the C# it was ported from: a silhouette rather than an inflated
// hull, measured by drawing the hovered thing a second time into a mask of its own rather
// than guessed from the geometry; the ring sized in screen pixels, not world ones, so it
// reads the same on a spider at your feet and one across the square; and a shadow under the
// ring for a dropped item alone, since a monster or a townsperson already casts its own.
//
// What is this project's own: the mask is fitted with bgfx's asymmetric projection rather
// than Godot's off-axis Camera3D, it is one fixed square rather than Godot's viewport
// resized in steps, and the "same mesh in two pictures" trick is done by handing the SAME
// Drawables gathered this frame to a second render rather than by a shared visual layer --
// see game/play.cpp's `hover` parameter of Play::gather and Litter::gatherOne.
#pragma once

#include <vector>

#include "gfx/renderer.h"

namespace mu::game {

class Outline {
public:
    // Fits a box on screen round `hovered`'s own meshes and asks `renderer` to ring it.
    // `view` and `proj` must be the SAME matrices the frame was drawn with -- the mask's own
    // camera is built from them, not recomputed, so the two pictures agree on where
    // everything is. Does nothing when `hovered` is empty or lands entirely off screen.
    void show(gfx::Renderer& renderer, const gfx::Camera& camera, const float* view,
              const float* proj, int width, int height, const std::vector<gfx::Drawable>& hovered,
              bool shadow);
};

}  // namespace mu::game
