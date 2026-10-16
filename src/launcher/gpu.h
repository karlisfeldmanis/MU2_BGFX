// What the machine has to draw with (docs/plans/launcher.md, L6): the adapter the game will run
// on, the driver behind it, and whether Vulkan -- the game's first renderer on Windows, before
// Direct3D 11 (src/gfx/window.cpp) -- is there at all. The probe is Windows' own (gpu_win.cpp); where there is none
// `known` is false, nothing is tuned and nothing is said on the screens.
#pragma once

#include <cstdint>
#include <string>

#include "core/args.h"  // core::kGraphicsPresets: the presets the game's Graphics page offers

namespace mu::launcher {

struct Gpu {
    bool known = false;      // a probe ran
    std::string name;        // the adapter's own words: "NVIDIA GeForce RTX 3060"
    unsigned vendorId = 0;   // 0x8086 Intel, 0x10de NVIDIA, 0x1002 AMD
    std::string driver;      // the driver key's version: "31.0.15.4601"
    std::string date;        // the driver key's date: "2024-05-31"
    uint64_t dedicated = 0;  // the adapter's own video memory in bytes; 0 on an integrated card
    bool software = false;   // only Microsoft's Basic Render Adapter answered
    bool vulkan = false;     // the renderer the game needs is there: vulkan-1.dll on Windows, Metal on a Mac
    bool driverOld = false;  // kDriverYears or more behind the newest driver
    int preset = -1;         // index into core::kGraphicsPresets: what this machine should open on
    int renderer = 0;        // core::Args::renderer: 0 Vulkan then Direct3D 11, 2 Direct3D 11

    // One line for the top of the home screen. The face draws ASCII and the middle dot (gfx/face.h
    // kMiddleDot) and nothing else, so the separators are \xB7 and not UTF-8.
    std::string line() const {
        if (!known) return {};
        std::string out = name.empty() ? std::string("Graphics adapter") : name;
        if (!date.empty()) out += " \xB7 driver " + date;
        else if (!driver.empty()) out += " \xB7 driver " + driver;
        return out;
    }
};

// The machine as Windows sees it: one probe of the adapters, the driver key behind the chosen
// one and the Vulkan loader.
Gpu probeGpu();

// Intel's integrated cards before Xe: HD and UHD Graphics, Iris Plus.
inline bool olderIntel(const Gpu& gpu) {
    return gpu.vendorId == 0x8086 && gpu.dedicated < (uint64_t(2) << 30) &&
           gpu.name.find("Xe") == std::string::npos && gpu.name.find("Arc") == std::string::npos;
}

// What this machine's first run opens on. No Vulkan or no hardware adapter is Lowest; two
// gigabytes of the adapter's own memory or more is High, less (an integrated card sharing the
// machine's) Low, and Lowest for Intel's older integrated cards; a driver kDriverYears old or
// more costs one step, because a card on an old driver runs below what it is. -1 where nothing
// was probed.
inline int presetFor(const Gpu& gpu) {
    using namespace core;
    if (!gpu.known) return -1;
    if (!gpu.vulkan || gpu.software) return kPresetLowest;
    int preset = kPresetHigh;
    if (gpu.dedicated < (uint64_t(2) << 30)) {
        // Integrated. Intel's UHD and HD Graphics, the most common laptop chip, did not hold 60
        // at 100% (the user, 2026-10-15), and Salvi's Iris Plus G7 lost its device on Low (55-63
        // ms a frame in Lorencia's rain, then a driver reset): every Intel card before Xe opens
        // on Lowest. Iris Xe, Intel's Arc and AMD's APUs are about twice it.
        preset = olderIntel(gpu) ? kPresetLowest : kPresetLow;
    }
    return gpu.driverOld && preset > kPresetLowest ? preset - 1 : preset;
}

// What this machine draws with (docs/plans/low-end-windows.md, P7): Direct3D 11 on a low-end PC --
// the user, 2026-10-15: "if we know that pc is low-end we should use direct3d" -- which is what
// presetFor already calls one: Lowest or Low, so an integrated card, no Vulkan loader or only
// the software adapter. Their drivers' Direct3D 11 is the path most tested; Intel's integrated
// Vulkan is the weak part of a driver it no longer develops. Everyone else is Vulkan, and
// Direct3D 11 still if Vulkan does not start (gfx/window.cpp).
inline int rendererFor(const Gpu& gpu) {
    if (!gpu.known || gpu.preset < 0) return 0;
    return !gpu.vulkan || gpu.preset <= core::kPresetLow ? 2 : 0;
}

// The set the first run writes: the low-end one for a machine presetFor calls Lowest or Low
// (core::kLowEndFirstRun), the preset itself for the rest.
inline const core::GraphicsPreset& firstRunSet(const Gpu& gpu) {
    return gpu.preset <= core::kPresetLow ? core::kLowEndFirstRun : core::kGraphicsPresets[gpu.preset];
}

// Where this card's driver is downloaded, or "" when the vendor is not one that can be named.
inline std::string driverUrl(const Gpu& gpu) {
    if (gpu.vendorId == 0x8086) return "https://www.intel.com/content/www/us/en/download-center/home.html";
    if (gpu.vendorId == 0x10de) return "https://www.nvidia.com/Download/index.aspx";
    if (gpu.vendorId == 0x1002) return "https://www.amd.com/en/support";
    return "";
}

}  // namespace mu::launcher