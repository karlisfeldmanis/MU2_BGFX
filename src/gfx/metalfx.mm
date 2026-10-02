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

// The scaler's input made safe. MetalFX reconstructs in blocks, and one NaN or infinity in
// its input -- a highlight that overflows half float, a normal of zero length -- turns its
// whole block black: the user's 2026-10-02 shot, two black squares beside the elf on Devias
// at a scale MetalFX served. A stretch shows such a texel as one bad pixel, which is why it
// went unseen. So the shade target is copied through this first, NaN to 0 and anything
// beyond half float's range clamped -- a read and a write a texel at the world's own size.
constexpr const char* kCleanSource = R"(
#include <metal_stdlib>
using namespace metal;
kernel void clean(texture2d<float, access::read> src [[texture(0)]],
                  texture2d<float, access::write> dst [[texture(1)]],
                  uint2 at [[thread_position_in_grid]]) {
    if (at.x >= dst.get_width() || at.y >= dst.get_height()) return;
    float4 c = src.read(at);
    c = select(c, float4(0.0), isnan(c));
    dst.write(clamp(c, float4(0.0), float4(65000.0)), at);
}
)";
id<MTLComputePipelineState> g_clean = nil;
id<MTLTexture> g_cleaned = nil;
bool g_cleanFailed = false;

// `in`, or its safe copy once the kernel is built; the copy is remade when the size or the
// format changes.
id<MTLTexture> cleaned(id<MTLCommandBuffer> cmd, id<MTLTexture> in) {
    if (g_cleanFailed) return in;
    if (g_clean == nil) {
        NSError* error = nil;
        id<MTLLibrary> library =
            [cmd.device newLibraryWithSource:[NSString stringWithUTF8String:kCleanSource]
                                     options:nil
                                       error:&error];
        id<MTLFunction> function = [library newFunctionWithName:@"clean"];
        if (function != nil) {
            g_clean = [cmd.device newComputePipelineStateWithFunction:function error:&error];
        }
        [function release];
        [library release];
        if (g_clean == nil) {
            g_cleanFailed = true;
            core::logError("metalfx: the input clean did not build (%s); MetalFX reads the shade "
                           "target as it is",
                           error ? error.localizedDescription.UTF8String : "no error");
            return in;
        }
    }
    if (g_cleaned == nil || g_cleaned.width != in.width || g_cleaned.height != in.height ||
        g_cleaned.pixelFormat != in.pixelFormat) {
        [g_cleaned release];
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:in.pixelFormat
                                                               width:in.width
                                                              height:in.height
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
        desc.storageMode = MTLStorageModePrivate;
        g_cleaned = [cmd.device newTextureWithDescriptor:desc];
        if (g_cleaned == nil) return in;
    }
    id<MTLComputeCommandEncoder> encoder = [cmd computeCommandEncoder];
    [encoder setComputePipelineState:g_clean];
    [encoder setTexture:in atIndex:0];
    [encoder setTexture:g_cleaned atIndex:1];
    [encoder dispatchThreads:MTLSizeMake(in.width, in.height, 1)
        threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [encoder endEncoding];
    return g_cleaned;
}

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

    g_scaler.colorTexture = cleaned(cmd, in);
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
