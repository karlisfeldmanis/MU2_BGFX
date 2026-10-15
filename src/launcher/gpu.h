// What the machine has to draw with (docs/plans/launcher.md, L6): the adapter the game will run
// on, the driver behind it, and whether Vulkan -- the game's renderer on Windows (src/gfx/
// window.cpp) -- is there at all. The probe is Windows' own (gpu_win.cpp); where there is none
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

// What this machine's first run opens on. No Vulkan or no hardware adapter is Lowest; two
// gigabytes of the adapter's own memory or more is High, less (an integrated card sharing the
// machine's) Low, and Lowest for Intel's UHD and HD Graphics; a driver kDriverYears old or more
// costs one step, because a card on an old driver runs below what it is. -1 where nothing was
// probed.
inline int presetFor(const Gpu& gpu) {
    using namespace core;
    if (!gpu.known) return -1;
    if (!gpu.vulkan || gpu.software) return kPresetLowest;
    int preset = kPresetHigh;
    if (gpu.dedicated < (uint64_t(2) << 30)) {
        // Integrated. Intel's UHD and HD Graphics, the most common laptop chip, did not hold 60
        // at 100% (the user, 2026-10-15); Iris Xe and AMD's APUs are about twice it.
        const bool plainIntel = gpu.vendorId == 0x8086 &&
                                (gpu.name.find("UHD") != std::string::npos ||
                                 gpu.name.find("HD Graphics") != std::string::npos);
        preset = plainIntel ? kPresetLowest : kPresetLow;
    }
    return gpu.driverOld && preset > kPresetLowest ? preset - 1 : preset;
}

// Where this card's driver is downloaded, or "" when the vendor is not one that can be named.
inline std::string driverUrl(const Gpu& gpu) {
    if (gpu.vendorId == 0x8086) return "https://www.intel.com/content/www/us/en/download-center/home.html";
    if (gpu.vendorId == 0x10de) return "https://www.nvidia.com/Download/index.aspx";
    if (gpu.vendorId == 0x1002) return "https://www.amd.com/en/support";
    return "";
}

}  // namespace mu::launcher