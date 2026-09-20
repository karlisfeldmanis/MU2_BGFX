#include "content/texture.h"

#include <bimg/decode.h>
#include <bx/allocator.h>

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

bgfx::TextureHandle solid(uint32_t abgr) {
    const bgfx::Memory* mem = bgfx::alloc(4);
    std::memcpy(mem->data, &abgr, 4);
    return bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, mem);
}

}  // namespace

void Textures::createDefaults() {
    white_ = solid(0xffffffff);
    // A flat normal is (0.5, 0.5, 1) as bytes, which is 0xffff8080 in ABGR.
    flatNormal_ = solid(0xffff8080);
    black_ = solid(0xff000000);
}

void Textures::shutdown() {
    for (auto& [path, handle] : byPath_) {
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
    byPath_.clear();
    for (bgfx::TextureHandle* h : {&white_, &flatNormal_, &black_}) {
        if (bgfx::isValid(*h)) bgfx::destroy(*h);
        *h = BGFX_INVALID_HANDLE;
    }
    bytes_ = 0;
}

bgfx::TextureHandle Textures::loadFromMemory(const std::string& name, const void* data,
                                             uint32_t size, ColourSpace space) {
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
    if (space == ColourSpace::Srgb) flags |= BGFX_TEXTURE_SRGB;

    // MU2's build is full of three-channel PNGs, and Metal has no three-channel texture at
    // all: RGB8 is refused outright, and RGB8 asked for as sRGB is refused twice over. The
    // texture then comes back invalid and the material silently draws white, which is what
    // the whole house did. Widened to four channels here; the cook step in sprint 3 is where
    // this stops happening at run time.
    if (!bgfx::isTextureValid(0, false, image->m_numLayers,
                              bgfx::TextureFormat::Enum(image->m_format), flags)) {
        const bgfx::TextureFormat::Enum wanted = bgfx::TextureFormat::RGBA8;
        bimg::ImageContainer* widened =
            bimg::imageConvert(&g_allocator, bimg::TextureFormat::Enum(wanted), *image, false);
        if (!widened) {
            core::logError("%s is %d, which this Metal refuses, and it did not convert",
                           name.c_str(), int(image->m_format));
            bimg::imageFree(image);
            byPath_[name] = BGFX_INVALID_HANDLE;
            return BGFX_INVALID_HANDLE;
        }
        core::logf("  %s widened from format %d to RGBA8", name.c_str(), int(image->m_format));
        bimg::imageFree(image);
        image = widened;
    }

    const bgfx::Memory* mem = bgfx::makeRef(image->m_data, image->m_size, releaseImage, image);

    bgfx::TextureHandle handle = bgfx::createTexture2D(
        uint16_t(image->m_width), uint16_t(image->m_height), image->m_numMips > 1,
        image->m_numLayers, bgfx::TextureFormat::Enum(image->m_format), flags, mem);

    if (!bgfx::isValid(handle)) {
        core::logError("%s did not become a texture", name.c_str());
    } else {
        bytes_ += image->m_size;
        core::logf("texture %s %ux%u %s mips %u %.1f KB", name.c_str(), image->m_width,
                   image->m_height, space == ColourSpace::Srgb ? "srgb" : "linear",
                   image->m_numMips, double(image->m_size) / 1024.0);
    }
    byPath_[name] = handle;
    return handle;
}

bgfx::TextureHandle Textures::load(const std::string& path, ColourSpace space) {
    auto found = byPath_.find(path);
    if (found != byPath_.end()) return found->second;

    std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) {
        byPath_[path] = BGFX_INVALID_HANDLE;
        return BGFX_INVALID_HANDLE;
    }
    return loadFromMemory(path, bytes.data(), uint32_t(bytes.size()), space);
}

}  // namespace mu::content
