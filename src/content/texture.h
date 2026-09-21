// Images into bgfx textures, with the two rules that matter: which of them are sRGB, and
// which of them are pictures rather than data. docs/conventions.md has the table.
#pragma once

#include <string>
#include <unordered_map>

#include <bgfx/bgfx.h>

namespace mu::content {

// What a texture is for. This decides its colour space, whether it carries a mip chain, how
// that chain is filtered, and how it is sampled. Getting it wrong is quiet every time.
enum class TextureRole {
    // The hardware converts on the sample; the mip chain is filtered in linear light.
    Albedo,
    Emissive,
    // Linear, and its mips are renormalised: averaging two unit vectors gives a short one,
    // and a short normal reads as a flat, shiny patch at distance.
    Normal,
    // Occlusion, roughness and metal. Linear, averaged as stored.
    Data,
    // height.png, attributes.png, light.png, the tile grid. Linear, point sampled, and
    // never mipped: a mipped attribute grid averages walkable together with blocked.
    Grid,
    // The windows' art. Drawn after the tonemap into the backbuffer, so it is sampled as the
    // bytes it was painted in and NOT decoded to linear -- the hardware would decode it and
    // nothing would encode it again, and every plate would come out dark. Its mips are
    // still averaged in linear light, because the bytes are sRGB and the average of two sRGB
    // bytes is darker than the colour they make. Clamped, trilinear, no anisotropy: a window
    // is drawn face on.
    Interface,
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
    // Occlusion 1, roughness 1, metal 0 -- what a surface with no ORM map should be. NOT
    // white: white's blue is metal 1.0, and a metal surface has no diffuse, so a material
    // whose ORM failed to load drew black rather than rough and unlit. MU2's own ground
    // shader defaults the same way, to vec3(1, 1, 0).
    bgfx::TextureHandle neutralOrm() const { return neutralOrm_; }
    // All ones, for a material that carries its roughness and metal in glTF's factors rather
    // than in a map. The shader multiplies, so 1 * factor is the factor, and the surface gets
    // the number its material actually asked for instead of the fallback above.
    //
    // The two are not interchangeable and the difference is the whole point: neutralOrm's
    // blue is 0, so it would multiply any metal factor away to nothing. It stays for the
    // callers with no factor to offer -- the ground and the bench -- and this one is for the
    // materials that have one.
    bgfx::TextureHandle ormOne() const { return ormOne_; }

    // How many texels across the anisotropic filter may reach. 1 turns it off. A sheet
    // value, set before anything loads.
    void setAnisotropy(int level) { anisotropy_ = level; }

    // Loads from disk, or returns the handle already loaded. Invalid on failure, and the
    // failure is in the log with the path.
    bgfx::TextureHandle load(const std::string& path, TextureRole role);

    // From bytes already in hand, for a texture embedded in a .glb. `name` is for the log
    // and is what the handle is remembered by.
    bgfx::TextureHandle loadFromMemory(const std::string& name, const void* data, uint32_t size,
                                       TextureRole role);

    // A loaded texture's size in texels, for a caller that cuts regions out of it in the
    // picture's own pixels -- the windows do, because MU2's tables are written that way.
    // False for a handle this did not load.
    bool sizeOf(bgfx::TextureHandle handle, uint32_t* width, uint32_t* height) const;

    size_t count() const { return byPath_.size(); }
    uint64_t bytes() const { return bytes_; }
    uint64_t mipBytes() const { return mipBytes_; }

private:
    std::unordered_map<std::string, bgfx::TextureHandle> byPath_;
    std::unordered_map<uint16_t, std::pair<uint32_t, uint32_t>> sizes_;
    bgfx::TextureHandle white_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flatNormal_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle black_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle neutralOrm_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ormOne_ = BGFX_INVALID_HANDLE;
    uint64_t bytes_ = 0;
    uint64_t mipBytes_ = 0;
    int anisotropy_ = 8;
};

}  // namespace mu::content
