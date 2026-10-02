#include "gfx/metalfx.h"

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include "core/log.h"

// The hook patches/bgfx-metal.patch adds to bgfx's Metal backend.
namespace bgfx::mtl {
typedef void (*ViewHookFn)(void* commandBuffer, void* src, void* dst, void* userData);
void setViewHook(ViewId view, TextureHandle src, TextureHandle dst, ViewHookFn fn,
                 void* userData);
}  // namespace bgfx::mtl

namespace mu::gfx::metalfx {
namespace {

// Built on the first frame and again whenever a size or format changes: a scaler is fixed
// to its descriptor.
id<MTLFXSpatialScaler> g_scaler = nil;
bool g_failed = false;

void upscale(void* commandBuffer, void* src, void* dst, void*) {
    id<MTLCommandBuffer> cmd = (id<MTLCommandBuffer>)commandBuffer;
    id<MTLTexture> in = (id<MTLTexture>)src;
    id<MTLTexture> out = (id<MTLTexture>)dst;
    if (cmd == nil || in == nil || out == nil || g_failed) return;

    if (g_scaler == nil || g_scaler.inputWidth != in.width ||
        g_scaler.inputHeight != in.height || g_scaler.outputWidth != out.width ||
        g_scaler.outputHeight != out.height || g_scaler.colorTextureFormat != in.pixelFormat ||
        g_scaler.outputTextureFormat != out.pixelFormat) {
        [g_scaler release];
        g_scaler = nil;
        MTLFXSpatialScalerDescriptor* desc = [[MTLFXSpatialScalerDescriptor alloc] init];
        desc.inputWidth = in.width;
        desc.inputHeight = in.height;
        desc.outputWidth = out.width;
        desc.outputHeight = out.height;
        desc.colorTextureFormat = in.pixelFormat;
        desc.outputTextureFormat = out.pixelFormat;
        // The shade target holds light before the tone curve, so HDR: the scaler maps it
        // into a range it can filter and back, and a lamp's 8.0 does not ring.
        desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModeHDR;
        g_scaler = [desc newSpatialScalerWithDevice:cmd.device];
        [desc release];
        if (g_scaler == nil) {
            g_failed = true;
            core::logError("metalfx: no spatial scaler for %lux%lu -> %lux%lu; the present stretches",
                           (unsigned long)in.width, (unsigned long)in.height,
                           (unsigned long)out.width, (unsigned long)out.height);
            return;
        }
        core::logf("metalfx: spatial scaler %lux%lu -> %lux%lu", (unsigned long)in.width,
                   (unsigned long)in.height, (unsigned long)out.width,
                   (unsigned long)out.height);
    }

    g_scaler.colorTexture = in;
    g_scaler.outputTexture = out;
    g_scaler.inputContentWidth = in.width;
    g_scaler.inputContentHeight = in.height;
    [g_scaler encodeToCommandBuffer:cmd];
}

}  // namespace

bool supported() {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    const bool yes = device != nil && [MTLFXSpatialScalerDescriptor supportsDevice:device];
    [device release];
    return yes;
}

void attach(bgfx::ViewId view, bgfx::TextureHandle src, bgfx::TextureHandle dst) {
    g_failed = false;
    bgfx::mtl::setViewHook(view, src, dst, upscale, nullptr);
}

void detach() {
    bgfx::mtl::setViewHook(0, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, nullptr, nullptr);
}

}  // namespace mu::gfx::metalfx
