// Apple's MetalFX spatial upscaler, run between the transparent pass and the present.
//
// A world drawn at `--scale` under 1 used to reach the screen through the present's own
// bilinear read and a four-tap sharpen. MetalFX reconstructs edges instead of stretching
// them, so a 1080p world on a 4K screen keeps lines a stretch would smear. bgfx knows
// nothing of it; the bgfx patch patches/bgfx-metal.patch hands its command
// buffer to this file just before the present view runs.
#pragma once

#include <bgfx/bgfx.h>

namespace mu::gfx::metalfx {

// Whether this GPU runs the spatial scaler (every Apple silicon Mac does).
bool supported();

// Before `view`'s first pass each frame, upscale `src` (HDR, linear) into `dst`. The sizes
// are read off the textures. attach() again with new handles after a resize.
void attach(bgfx::ViewId view, bgfx::TextureHandle src, bgfx::TextureHandle dst);
void detach();

}  // namespace mu::gfx::metalfx
