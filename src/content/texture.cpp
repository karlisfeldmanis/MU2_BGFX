#include "content/texture.h"

#include <bimg/decode.h>
#include <bx/allocator.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "core/files.h"
#include "core/log.h"

namespace mu::content {
namespace {

bx::DefaultAllocator g_allocator;

// bimg hands the decoded image back through this; bgfx takes ownership of the copy and
// frees the container when the texture is destroyed.
void releaseImage(void*, void* userData) {
    bimg::imageFree(static_cast<bimg::ImageContainer*>(userData));
}

// bgfx frees a std::vector we handed it through this, for the mip chains we build ourselves.
void releaseBytes(void*, void* userData) {
    delete static_cast<std::vector<uint8_t>*>(userData);
}

bgfx::TextureHandle solid(uint32_t abgr) {
    const bgfx::Memory* mem = bgfx::alloc(4);
    std::memcpy(mem->data, &abgr, 4);
    return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, mem);
}

bool isSrgb(TextureRole role) {
    return role == TextureRole::Albedo || role == TextureRole::Emissive ||
           role == TextureRole::Cutout;
}

// Whether the bytes are sRGB, which is what the mip chain is averaged by -- a wider question
// than whether the sampler decodes them. The windows' art is painted in sRGB and drawn as it
// stands, so it takes the first and not the second. See TextureRole::Interface.
bool paintedInSrgb(TextureRole role) { return isSrgb(role) || role == TextureRole::Interface; }

bool wantsMips(TextureRole role) { return role != TextureRole::Grid; }

// sRGB to linear and back, the real curve rather than a 2.2 power. Tabulated one way
// because the decode runs once per texel per level and the table is 1 KB.
const float* srgbToLinearTable() {
    static float table[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; ++i) {
            const float c = float(i) / 255.0f;
            table[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        ready = true;
    }
    return table;
}

uint8_t linearToSrgbByte(float v) {
    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    const float s = v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
    return uint8_t(s * 255.0f + 0.5f);
}

// One level down, by a 2x2 box -- three wide at an odd edge, so nothing is dropped. RGBA8
// in, RGBA8 out.
//
// The colour space is the whole point. bimg's own imageGenerateMips averages sRGB bytes
// directly, which is not the average of the light those bytes stand for: the result is
// darker than the texture it came from, and a town reads as though the sun went in as the
// camera pulls back. Alpha is always averaged as stored, because alpha is a coverage and
// not a colour -- a flat average, with no coverage rescale: that rescale is the cook's job
// in sprint 3 and docs/conventions.md says so. Until then a cutout's leaf would thin with
// distance, and there is no cutout content in this engine to thin.
void downsample(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* dst, uint32_t dstW,
                uint32_t dstH, TextureRole role) {
    const float* toLinear = srgbToLinearTable();
    const bool srgb = paintedInSrgb(role);
    const bool normal = role == TextureRole::Normal;

    for (uint32_t y = 0; y < dstH; ++y) {
        // A level is half the one above it rounded *down*, because that is the size bgfx and
        // bimg compute for themselves when they walk a chain, and a level our arithmetic
        // sizes differently from theirs is a texture read at the wrong offsets.
        //
        // Rounding down means an odd source has one row and one column left over, and the
        // obvious 2x2 box never reads them: at srcH 3 the box covers rows 0 and 1 and row 2
        // is thrown away, level after level, so the chain walks towards the top left corner
        // of the picture. Invisible on the 384-square atlases of MU2's build, which halve
        // evenly to 3 and only then go odd, and not invisible at all on a cooked atlas whose
        // sides are not powers of two. So the *last* destination pixel takes the orphan in:
        // three source rows or columns instead of two, averaged flat.
        const uint32_t y0 = y * 2;
        const uint32_t yEnd = (y + 1 == dstH) ? srcH - 1 : std::min(y0 + 1, srcH - 1);
        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t x0 = x * 2;
            const uint32_t xEnd = (x + 1 == dstW) ? srcW - 1 : std::min(x0 + 1, srcW - 1);
            const float inv = 1.0f / float((yEnd - y0 + 1) * (xEnd - x0 + 1));

            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (uint32_t sy = y0; sy <= yEnd; ++sy) {
                for (uint32_t sx = x0; sx <= xEnd; ++sx) {
                    const uint8_t* q = src + (size_t(sy) * srcW + sx) * 4;
                    if (srgb) {
                        acc[0] += toLinear[q[0]];
                        acc[1] += toLinear[q[1]];
                        acc[2] += toLinear[q[2]];
                    } else if (normal) {
                        // Back to a signed vector before averaging, or the average is pulled
                        // towards the encoding's midpoint rather than towards the mean normal.
                        acc[0] += float(q[0]) / 127.5f - 1.0f;
                        acc[1] += float(q[1]) / 127.5f - 1.0f;
                        acc[2] += float(q[2]) / 127.5f - 1.0f;
                    } else {
                        acc[0] += float(q[0]);
                        acc[1] += float(q[1]);
                        acc[2] += float(q[2]);
                    }
                    acc[3] += float(q[3]);
                }
            }
            for (float& v : acc) v *= inv;

            uint8_t* out = dst + (size_t(y) * dstW + x) * 4;
            if (srgb) {
                out[0] = linearToSrgbByte(acc[0]);
                out[1] = linearToSrgbByte(acc[1]);
                out[2] = linearToSrgbByte(acc[2]);
            } else if (normal) {
                // Renormalised: four unit vectors average to a short one, and a short normal
                // flattens the surface and reads as a shiny patch at distance.
                const float len = std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
                const float inv = len > 1e-6f ? 1.0f / len : 0.0f;
                const float nx = len > 1e-6f ? acc[0] * inv : 0.0f;
                const float ny = len > 1e-6f ? acc[1] * inv : 0.0f;
                const float nz = len > 1e-6f ? acc[2] * inv : 1.0f;
                out[0] = uint8_t((nx + 1.0f) * 127.5f + 0.5f);
                out[1] = uint8_t((ny + 1.0f) * 127.5f + 0.5f);
                out[2] = uint8_t((nz + 1.0f) * 127.5f + 0.5f);
            } else {
                out[0] = uint8_t(acc[0] + 0.5f);
                out[1] = uint8_t(acc[1] + 0.5f);
                out[2] = uint8_t(acc[2] + 0.5f);
            }
            out[3] = uint8_t(acc[3] + 0.5f);
        }
    }
}

// Rescales one mip level's alpha so that the share of texels at or over `cutoff` matches
// what the top level had. The scale is found by bisection rather than solved, because the
// answer depends on the level's own alpha histogram and sixteen halvings settle it to well
// inside a byte -- at load, on a 256x64 sheet, which is nothing.
//
// This is the standard answer to a cut-out that thins as it minifies (Castano's, and every
// foliage renderer's since). What it buys here is not mainly that a distant tuft keeps its
// bulk: it is that the level's alpha is pushed AWAY from the threshold, so far fewer texels
// sit near enough to cross it when the camera moves half a texel. That crossing is the crawl.
void holdCoverage(uint8_t* level, size_t texels, float cutoff, float wanted) {
    if (texels == 0 || wanted <= 0.0f) return;
    float low = 0.05f, high = 24.0f;
    for (int step = 0; step < 16; ++step) {
        const float scale = (low + high) * 0.5f;
        size_t over = 0;
        for (size_t i = 0; i < texels; ++i) {
            if (float(level[i * 4 + 3]) / 255.0f * scale >= cutoff) ++over;
        }
        const float got = float(over) / float(texels);
        if (got < wanted) {
            low = scale;
        } else {
            high = scale;
        }
    }
    const float scale = (low + high) * 0.5f;
    for (size_t i = 0; i < texels; ++i) {
        const float a = float(level[i * 4 + 3]) / 255.0f * scale;
        level[i * 4 + 3] = uint8_t((a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a)) * 255.0f + 0.5f);
    }
}

// The alpha a cut-out is tested against. One number, here, because the mip chain is built to
// hold coverage at exactly the threshold the shader will test at, and the two disagreeing is
// the chain holding the wrong thing. game::Grass::kCutout is the same number and says so.
constexpr float kCutoutAlpha = 0.28f;

uint8_t mipCount(uint32_t width, uint32_t height) {
    uint8_t levels = 1;
    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

}  // namespace

void Textures::createDefaults() {
    white_ = solid(0xffffffff);
    // A flat normal is (0.5, 0.5, 1) as bytes, which is 0xffff8080 in ABGR.
    flatNormal_ = solid(0xffff8080);
    black_ = solid(0xff000000);
    // ABGR: alpha 255, blue 0 (metal), green 255 (rough), red 255 (unoccluded).
    neutralOrm_ = solid(0xff00ffff);
    // All ones, so the shader's `orm * factor` is the factor. See Textures::ormOne.
    ormOne_ = solid(0xffffffff);
}

void Textures::shutdown() {
    for (auto& [path, handle] : byPath_) {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
    byPath_.clear();
    sizes_.clear();
    for (bgfx::TextureHandle* h : {&white_, &flatNormal_, &black_, &neutralOrm_, &ormOne_}) {
        if (bgfx::isValid(*h)) bgfx::destroy(*h);
        *h = BGFX_INVALID_HANDLE;
    }
    bytes_ = 0;
    mipBytes_ = 0;
}

bgfx::TextureHandle Textures::loadFromMemory(const std::string& name, const void* data,
                                             uint32_t size, TextureRole role) {
    auto found = byPath_.find(name);
    if (found != byPath_.end()) return found->second;

    bimg::ImageContainer* image = bimg::imageParse(&g_allocator, data, size);
    if (!image) {
        core::logError("%s is not an image bimg can read", name.c_str());
        byPath_[name] = BGFX_INVALID_HANDLE;
        return BGFX_INVALID_HANDLE;
    }

    uint64_t flags = BGFX_SAMPLER_NONE;
    // sRGB is a flag on the texture, not a conversion in the shader: the hardware does it
    // on the sample and the result is linear, which is what everything downstream assumes.
    if (isSrgb(role)) flags |= BGFX_TEXTURE_SRGB;
    if (role == TextureRole::Grid) {
        // A grid is data. Point sampled in every direction, and never interpolated between
        // two tiles that mean different things.
        flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT |
                 BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    } else if (role == TextureRole::Interface) {
        // Face on and never tiled: a window's art is clamped, so a plate's edge does not
        // bleed the other edge into it, and anisotropy has no angle to work at.
        flags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    } else if (anisotropy_ > 1) {
        // MU's camera looks down a town at a shallow angle, which is what anisotropy is for:
        // trilinear alone blurs the ground into the distance instead of resolving it.
        flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
    }

    // MU2's build is full of three-channel PNGs, and Metal has no three-channel texture at
    // all: RGB8 is refused outright, and RGB8 asked for as sRGB is refused twice over. The
    // texture then comes back invalid and the material silently draws white, which is what
    // a whole house did. Widened to four channels here; the cook step in sprint 3 is where
    // this stops happening at run time.
    const bool needsWidening =
        !bgfx::isTextureValid(0, false, image->m_numLayers,
                              bgfx::TextureFormat::Enum(image->m_format), flags);
    // The mip chain is built on RGBA8, so anything that is to be mipped goes there too.
    const bool needsRgba8 = image->m_format != bimg::TextureFormat::RGBA8 &&
                            (needsWidening || (wantsMips(role) && image->m_numMips <= 1));

    if (needsRgba8) {
        bimg::ImageContainer* widened = bimg::imageConvert(
            &g_allocator, bimg::TextureFormat::RGBA8, *image, false);
        if (!widened) {
            core::logError("%s is format %d, which this Metal refuses, and it did not convert",
                           name.c_str(), int(image->m_format));
            bimg::imageFree(image);
            byPath_[name] = BGFX_INVALID_HANDLE;
            return BGFX_INVALID_HANDLE;
        }
        bimg::imageFree(image);
        image = widened;
    }

    const uint32_t width = image->m_width;
    const uint32_t height = image->m_height;
    const char* roleName = isSrgb(role) ? "srgb" : (role == TextureRole::Normal ? "normal"
                                                  : (role == TextureRole::Grid ? "grid"
                                                  : (role == TextureRole::Interface ? "interface"
                                                                                     : "data")));

    // --- the mip chain ---------------------------------------------------------------
    // Built here rather than by bimg::imageGenerateMips, which averages sRGB bytes as
    // though they were light and hands back a chain that darkens with distance.
    if (wantsMips(role) && image->m_numMips <= 1 &&
        image->m_format == bimg::TextureFormat::RGBA8 && (width > 1 || height > 1)) {
        const uint8_t levels = mipCount(width, height);
        size_t total = 0;
        for (uint8_t level = 0; level < levels; ++level) {
            const uint32_t lw = std::max(1u, width >> level);
            const uint32_t lh = std::max(1u, height >> level);
            total += size_t(lw) * lh * 4;
        }
        auto* chain = new std::vector<uint8_t>(total);
        std::memcpy(chain->data(), image->m_data, size_t(width) * height * 4);

        size_t srcOffset = 0;
        uint32_t srcW = width, srcH = height;
        for (uint8_t level = 1; level < levels; ++level) {
            const uint32_t dstW = srcW > 1 ? srcW / 2 : 1;
            const uint32_t dstH = srcH > 1 ? srcH / 2 : 1;
            const size_t dstOffset = srcOffset + size_t(srcW) * srcH * 4;
            downsample(chain->data() + srcOffset, srcW, srcH, chain->data() + dstOffset, dstW,
                       dstH, role);
            srcOffset = dstOffset;
            srcW = dstW;
            srcH = dstH;
        }

        if (role == TextureRole::Cutout) {
            // What the top level covers, which every level below it is made to match.
            size_t over = 0;
            const size_t topTexels = size_t(width) * height;
            for (size_t i = 0; i < topTexels; ++i) {
                if (float(chain->at(i * 4 + 3)) / 255.0f >= kCutoutAlpha) ++over;
            }
            const float wanted = float(over) / float(topTexels);
            size_t offset = topTexels * 4;
            for (uint8_t level = 1; level < levels; ++level) {
                const uint32_t lw = std::max(1u, width >> level);
                const uint32_t lh = std::max(1u, height >> level);
                holdCoverage(chain->data() + offset, size_t(lw) * lh, kCutoutAlpha, wanted);
                offset += size_t(lw) * lh * 4;
            }
        }

        const bgfx::Memory* mem =
            bgfx::makeRef(chain->data(), uint32_t(chain->size()), releaseBytes, chain);
        bgfx::TextureHandle handle =
            bgfx::createTexture2D(uint16_t(width), uint16_t(height), true, 1,
                                  bgfx::TextureFormat::RGBA8, flags, mem);
        const size_t topLevel = size_t(width) * height * 4;
        bimg::imageFree(image);
        if (!bgfx::isValid(handle)) {
            core::logError("%s did not become a texture", name.c_str());
        } else {
            bytes_ += topLevel;
            mipBytes_ += total - topLevel;
            core::logf("texture %s %ux%u %s %u mips %.1f KB (+%.1f KB of mips)", name.c_str(),
                       width, height, roleName, levels, double(topLevel) / 1024.0,
                       double(total - topLevel) / 1024.0);
            sizes_[handle.idx] = {width, height};
        }
        byPath_[name] = handle;
        return handle;
    }

    // --- no chain: a grid, or an image that arrived with one --------------------------
    const bgfx::Memory* mem = bgfx::makeRef(image->m_data, image->m_size, releaseImage, image);
    bgfx::TextureHandle handle = bgfx::createTexture2D(
        uint16_t(width), uint16_t(height), image->m_numMips > 1, image->m_numLayers,
        bgfx::TextureFormat::Enum(image->m_format), flags, mem);

    if (!bgfx::isValid(handle)) {
        core::logError("%s did not become a texture", name.c_str());
    } else {
        bytes_ += image->m_size;
        core::logf("texture %s %ux%u %s mips %u %.1f KB", name.c_str(), width, height, roleName,
                   image->m_numMips, double(image->m_size) / 1024.0);
        sizes_[handle.idx] = {width, height};
    }
    byPath_[name] = handle;
    return handle;
}

bool Textures::sizeOf(bgfx::TextureHandle handle, uint32_t* width, uint32_t* height) const {
    if (!bgfx::isValid(handle)) return false;
    auto found = sizes_.find(handle.idx);
    if (found == sizes_.end()) return false;
    *width = found->second.first;
    *height = found->second.second;
    return true;
}

bgfx::TextureHandle Textures::load(const std::string& path, TextureRole role) {
    auto found = byPath_.find(path);
    if (found != byPath_.end()) return found->second;

    std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) {
        byPath_[path] = BGFX_INVALID_HANDLE;
        return BGFX_INVALID_HANDLE;
    }
    return loadFromMemory(path, bytes.data(), uint32_t(bytes.size()), role);
}


}  // namespace mu::content
