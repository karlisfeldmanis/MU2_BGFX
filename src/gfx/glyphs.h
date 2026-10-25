// The letters no face bakes: everything past ASCII that a player types, rasterised the first
// time a line asks for it into one shared texture, from the asking face's own file or, when that
// has no such letter, a font of the system's (Chinese, Japanese, Korean). The baked atlases stay
// as they are; a line draws its baked letters from them and the rest from here (gfx/face.h).
#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

#include "gfx/face.h"

namespace mu::gfx::glyphs {

// The em the cache draws at, in its texels. A glyph found here is in these pixels, which the
// face scales to its own baked em.
constexpr float kEm = 44.0f;

// The letter `code` as `face` (its file) draws it, or the first fallback that has it; false
// when none does, or the cache is full. Any thread: a line may be measured off the frame.
bool find(const std::string& facePath, uint32_t code, FaceGlyph* out);

// The cache's texture, made by make(); invalid before, and then the cached letters are skipped.
bgfx::TextureHandle texture();
// On bgfx's thread: the texture once, the letters rasterised since the last upload each frame
// (before bgfx::frame, so a letter found this frame draws this frame), and the end.
void make();
void upload();
void destroy();

}  // namespace mu::gfx::glyphs
