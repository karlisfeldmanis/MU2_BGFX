// Images into bgfx textures, with the one rule that matters: which of them are sRGB.
// docs/conventions.md has the table.
#pragma once

#include <string>
#include <unordered_map>

#include <bgfx/bgfx.h>

namespace mu::content {

enum class ColourSpace {
    Srgb,    // albedo and emissive: the hardware converts on the sample
    Linear,  // normal, ORM, and every grid MU stores as a picture
};

// Keeps one handle per path, so a texture two materials share is loaded once.
class Textures {
public:
    void shutdown();

    // The 1x1 stand-ins, so a material with a missing map still draws: white, a flat
    // normal, white ORM (unoccluded, rough, not metal) and black.
    void createDefaults();
    bgfx::TextureHandle white() const { return white_; }
    bgfx::TextureHandle flatNormal() const { return flatNormal_; }
    bgfx::TextureHandle black() const { return black_; }

    // Loads from disk, or returns the handle already loaded. Invalid on failure, and the
    // failure is in the log with the path.
    bgfx::TextureHandle load(const std::string& path, ColourSpace space);

    // From bytes already in hand, for a texture embedded in a .glb. `name` is for the log
    // and is what the handle is remembered by.
    bgfx::TextureHandle loadFromMemory(const std::string& name, const void* data, uint32_t size,
                                       ColourSpace space);

    size_t count() const { return byPath_.size(); }
    uint64_t bytes() const { return bytes_; }

private:
    std::unordered_map<std::string, bgfx::TextureHandle> byPath_;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flatNormal_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle black_ = BGFX_INVALID_HANDLE;
    uint64_t bytes_ = 0;
};

}  // namespace mu::content
