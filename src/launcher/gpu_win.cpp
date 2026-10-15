// The probe on Windows (docs/plans/launcher.md, L6): DXGI lists the machine's adapters and which
// of them is real; the display class' driver key (SetupAPI, then RegQueryValueExW) holds the
// version and the date of the driver running it; vulkan-1.dll is the loader the game's renderer
// opens. The driver key is matched to the adapter by name, because a two-card machine has a key
// a card and the wrong one would name the other card's driver; when the names do not agree but
// the machine has one display device, that one is taken.
#include "launcher/gpu.h"

#include <windows.h>
#include <dxgi.h>
#include <setupapi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <strings.h>
#include <vector>

#include "core/log.h"

namespace mu::launcher {
namespace {

// A driver this many years old is behind what the hardware ships now: the preset is held back a
// step and the screens say so. Laptop cards in particular sit on the vendor's first release.
constexpr int kDriverYears = 3;

std::string utf8(const wchar_t* wide) {
    if (!wide || !*wide) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(size_t(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, out.data(), size, nullptr, nullptr);
    return out;
}

// A date as the screens say it: yyyy-mm-dd. The FILETIME path above already lands in that shape;
// a REG_SZ key arrives as the system wrote it -- "10-14-2026", month, day, year (Wine's own).
std::string dateOf(const std::string& raw) {
    unsigned first = 0, second = 0, third = 0;
    if (std::sscanf(raw.c_str(), "%u-%u-%u", &first, &second, &third) == 3 && third > 999) {
        char dated[16];
        std::snprintf(dated, sizeof dated, "%04u-%02u-%02u", third, first, second);
        return dated;
    }
    return raw;
}

// IID_IDXGIFactory1 and GUID_DEVCLASS_DISPLAY, spelled out so the probe needs no guid header and
// no guid library: the factory to ask for the adapters, and the display class under
// SYSTEM\CurrentControlSet\Control\Class whose subkeys are the machine's display drivers.
constexpr GUID kFactory1 = {0x770aae78, 0xf26f, 0x4dba, {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}};
constexpr GUID kDisplayClass = {0x4d36e968, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};

struct Adapter {
    std::string name;
    unsigned vendor = 0;
    uint64_t dedicated = 0;
    bool software = false;
};

std::vector<Adapter> adapters() {
    std::vector<Adapter> found;
    IDXGIFactory1* factory = nullptr;
    const HRESULT asked = CreateDXGIFactory1(kFactory1, reinterpret_cast<void**>(&factory));
    if (FAILED(asked) || !factory) {
        core::logf("launcher: gpu: no DXGI factory (%08lx)", static_cast<unsigned long>(asked));
        return found;
    }
    for (UINT i = 0;; ++i) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        found.push_back(Adapter{utf8(desc.Description), desc.VendorId, uint64_t(desc.DedicatedVideoMemory),
                                (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0});
        adapter->Release();
    }
    factory->Release();
    return found;
}

// One display device's driver key, read once through: its description (to match the adapter),
// its version, and its date -- a FILETIME, the day the driver was built.
struct DriverKey {
    std::string description;
    std::string version;
    std::string date;
};

std::vector<DriverKey> driverKeys() {
    std::vector<DriverKey> keys;
    const HDEVINFO devices = SetupDiGetClassDevsW(&kDisplayClass, nullptr, nullptr, DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) return keys;
    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof info;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(devices, i, &info); ++i) {
        wchar_t description[256] = {};
        // The device's own words: the friendly name when it has one, the description under it.
        if (!SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_FRIENDLYNAME, nullptr,
                                               reinterpret_cast<PBYTE>(description), sizeof description, nullptr) &&
            !SetupDiGetDeviceRegistryPropertyW(devices, &info, SPDRP_DEVICEDESC, nullptr,
                                               reinterpret_cast<PBYTE>(description), sizeof description, nullptr)) {
            continue;
        }
        DriverKey one;
        one.description = utf8(description);
        const HKEY key = SetupDiOpenDevRegKey(devices, &info, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        if (key != INVALID_HANDLE_VALUE) {
            BYTE raw[128] = {};
            DWORD type = 0, size = sizeof raw - sizeof(wchar_t);  // room for the terminator
            if (RegQueryValueExW(key, L"DriverVersion", nullptr, &type, raw, &size) == ERROR_SUCCESS &&
                type == REG_SZ) {
                one.version = utf8(reinterpret_cast<const wchar_t*>(raw));
            }
            type = 0;
            size = sizeof raw;
            if (RegQueryValueExW(key, L"DriverDate", nullptr, &type, raw, &size) == ERROR_SUCCESS) {
                if (type == REG_BINARY && size >= sizeof(FILETIME)) {
                    FILETIME stamp{};
                    std::memcpy(&stamp, raw, sizeof stamp);
                    SYSTEMTIME when{};
                    if (FileTimeToSystemTime(&stamp, &when)) {
                        char dated[16];
                        std::snprintf(dated, sizeof dated, "%04u-%02u-%02u", when.wYear, when.wMonth, when.wDay);
                        one.date = dated;
                    }
                } else if (type == REG_SZ) {
                    one.date = dateOf(utf8(reinterpret_cast<const wchar_t*>(raw)));
                }
            }
            RegCloseKey(key);
        }
        keys.push_back(std::move(one));
    }
    SetupDiDestroyDeviceInfoList(devices);
    return keys;
}

// A vendor's own words where DXGI is not there to give its id: the name is the only hint a
// display class fallback has.
unsigned vendorNamed(const std::string& name) {
    if (name.find("NVIDIA") != std::string::npos) return 0x10de;
    if (name.find("Intel") != std::string::npos) return 0x8086;
    if (name.find("AMD") != std::string::npos || name.find("Radeon") != std::string::npos) return 0x1002;
    return 0;
}

// The driver's date against the clock: three years or more behind, and the preset is held back
// a step while the screens say so.
void ageDriver(Gpu& gpu) {
    if (gpu.date.size() < 4) return;
    const int year = std::atoi(gpu.date.substr(0, 4).c_str());
    const std::time_t now = std::time(nullptr);
    const std::tm* today = std::localtime(&now);
    gpu.driverOld = today != nullptr && year > 1990 && today->tm_year + 1900 - year >= kDriverYears;
}

}  // namespace

Gpu probeGpu() {
    Gpu gpu;
    const std::vector<DriverKey> keys = driverKeys();
    const std::vector<Adapter> found = adapters();
    // The card with memory of its own is the one the game draws on: src/main.cpp exports the two
    // symbols that make Windows start it on that card of a two-card machine, and the software
    // adapter draws nothing at all.
    const Adapter* pick = nullptr;
    for (const Adapter& one : found) {
        if (one.software) continue;
        if (!pick || one.dedicated > pick->dedicated) pick = &one;
    }
    if (!pick && !found.empty()) pick = &found.front();
    if (pick) {
        gpu.known = true;
        gpu.name = pick->name;
        gpu.vendorId = pick->vendor;
        gpu.dedicated = pick->dedicated;
        gpu.software = pick->software;
    } else if (!keys.empty()) {
        // No DXGI answer -- a Wine prefix whose DXVK cannot open the card, or a machine that
        // answers none: the display class' devices still name what runs the display.
        gpu.known = true;
        gpu.name = keys.front().description;
        gpu.vendorId = vendorNamed(gpu.name);
        core::logf("launcher: gpu: no DXGI adapter; taken from the display class");
    } else {
        return gpu;
    }
    if (HMODULE loader = LoadLibraryW(L"vulkan-1.dll")) {
        gpu.vulkan = true;
        FreeLibrary(loader);
    }
    const DriverKey* run = nullptr;
    for (const DriverKey& one : keys) {
        if (!one.description.empty() && strcasecmp(one.description.c_str(), gpu.name.c_str()) == 0) {
            run = &one;
            break;
        }
    }
    if (!run && (keys.size() == 1 || gpu.name.empty())) run = &keys.front();
    if (run) {
        gpu.driver = run->version;
        gpu.date = run->date;
        ageDriver(gpu);
    }
    gpu.preset = presetFor(gpu);
    gpu.renderer = rendererFor(gpu);
    for (const Adapter& one : found) {
        core::logf("launcher: gpu: %s%s, %llu MB%s", one.name.c_str(), one.software ? " (software)" : "",
                   static_cast<unsigned long long>(one.dedicated >> 20),
                   one.name == gpu.name ? " <- the game's" : "");
    }
    core::logf("launcher: gpu: driver %s%s, vulkan %s, %s", gpu.driver.empty() ? "unsaid" : gpu.driver.c_str(),
               gpu.date.empty() ? "" : (" of " + gpu.date).c_str(), gpu.vulkan ? "yes" : "MISSING",
               gpu.preset >= 0 ? core::kGraphicsPresets[gpu.preset].name : "no preset");
    return gpu;
}

}  // namespace mu::launcher