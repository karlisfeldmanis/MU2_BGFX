// The Mac's probe (docs/plans/launcher.md, L5): Metal says which card draws the screen. Apple's
// own GPU shares the machine's memory but is fast -- classed here as a card with memory of its
// own, so High; an Intel Mac's integrated graphics is Medium, as on Windows. No driver date to
// be old: macOS carries the GPU in the system update.
#include "launcher/gpu.h"

#import <Metal/Metal.h>

#include <cstdio>

#include "core/log.h"

namespace mu::launcher {

Gpu probeGpu() {
    Gpu gpu;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) return gpu;
    gpu.known = true;
    gpu.name = [device.name UTF8String];
    gpu.vulkan = true;  // the renderer the game needs, which on a Mac is always Metal
    if (gpu.name.find("Apple") != std::string::npos) {
        gpu.vendorId = 0x106b;                 // Apple
        gpu.dedicated = uint64_t(8) << 30;     // unified memory, but the card: High
    } else if (gpu.name.find("Radeon") != std::string::npos) {
        gpu.vendorId = 0x1002;                 // AMD
        gpu.dedicated = uint64_t(4) << 30;     // a card of its own: High
    } else if (gpu.name.find("Intel") != std::string::npos) {
        gpu.vendorId = 0x8086;                 // integrated: Low, Lowest for UHD (gpu.h)
    }
    gpu.preset = presetFor(gpu);
    core::logf("launcher: gpu: %s, %s graphics", gpu.name.c_str(),
               gpu.preset >= 0 ? core::kGraphicsPresets[gpu.preset].name : "no preset");
    return gpu;
}

}  // namespace mu::launcher