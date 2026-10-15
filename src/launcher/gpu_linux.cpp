// Linux's probe (docs/plans/linux-release.md, X5): Vulkan itself says which card the game will draw
// on, since Vulkan is the game's only renderer here. libvulkan.so.1 is opened by hand, as bgfx opens
// it, so a machine without it still opens the launcher's sign-in -- and is told why the game
// cannot start. The card is the one bgfx would take: a discrete one before an integrated one, and
// either before a CPU's (llvmpipe). Mesa and NVIDIA name their own driver and version through
// VkPhysicalDeviceDriverProperties; no date to be old by, so no warning is drawn.
#include "launcher/gpu.h"

#include <dlfcn.h>

#include <cstring>
#include <vector>

#include <vulkan-local/vulkan_core.h>

#include "core/log.h"

namespace mu::launcher {

namespace {

// Better first: what bgfx's renderer_vk.cpp picks among the cards it finds.
int rank(VkPhysicalDeviceType type) {
    switch (type) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return 4;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 3;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return 2;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: return 1;
        default: return 0;
    }
}

}  // namespace

Gpu probeGpu() {
    Gpu gpu;
    gpu.known = true;  // a probe ran: no Vulkan at all is something to say, and a preset (Lowest)
    void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        gpu.name = "No Vulkan driver";
        gpu.preset = presetFor(gpu);
        core::logError("launcher: gpu: no libvulkan.so.1; the game cannot start here");
        return gpu;
    }
    const auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(lib, "vkGetInstanceProcAddr"));
    const auto create = getProc ? reinterpret_cast<PFN_vkCreateInstance>(getProc(nullptr, "vkCreateInstance")) : nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "MU2Launcher";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &app;
    if (!create || create(&info, nullptr, &instance) != VK_SUCCESS) {
        gpu.name = "Vulkan did not start";
        gpu.preset = presetFor(gpu);
        core::logError("launcher: gpu: vkCreateInstance failed");
        dlclose(lib);
        return gpu;
    }
    const auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getProc(instance, "vkEnumeratePhysicalDevices"));
    const auto properties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(getProc(instance, "vkGetPhysicalDeviceProperties"));
    const auto properties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(getProc(instance, "vkGetPhysicalDeviceProperties2"));
    const auto memory = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(getProc(instance, "vkGetPhysicalDeviceMemoryProperties"));
    const auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(getProc(instance, "vkDestroyInstance"));

    uint32_t count = 0;
    std::vector<VkPhysicalDevice> devices;
    if (enumerate && enumerate(instance, &count, nullptr) == VK_SUCCESS && count > 0) {
        devices.resize(count);
        enumerate(instance, &count, devices.data());
        devices.resize(count);
    }
    VkPhysicalDevice best = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties bestProps{};
    for (VkPhysicalDevice d : devices) {
        VkPhysicalDeviceProperties p{};
        properties(d, &p);
        if (best == VK_NULL_HANDLE || rank(p.deviceType) > rank(bestProps.deviceType)) {
            best = d;
            bestProps = p;
        }
    }
    if (best != VK_NULL_HANDLE) {
        gpu.name = bestProps.deviceName;
        gpu.vendorId = bestProps.vendorID;
        gpu.software = bestProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU;
        gpu.vulkan = !gpu.software && bestProps.apiVersion >= VK_API_VERSION_1_1;
        // The card's own memory: the largest device-local heap of a discrete card. An integrated
        // one's heap is the machine's memory, shared, and counts as none (gpu.h presetFor).
        if (bestProps.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && memory) {
            VkPhysicalDeviceMemoryProperties m{};
            memory(best, &m);
            for (uint32_t i = 0; i < m.memoryHeapCount; ++i) {
                if ((m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) && m.memoryHeaps[i].size > gpu.dedicated) {
                    gpu.dedicated = m.memoryHeaps[i].size;
                }
            }
        }
        // "Mesa 24.2.8" or NVIDIA's "550.120": the driver's own words, Vulkan 1.2's.
        if (properties2 && bestProps.apiVersion >= VK_API_VERSION_1_2) {
            VkPhysicalDeviceDriverProperties driver{};
            driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
            VkPhysicalDeviceProperties2 p2{};
            p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            p2.pNext = &driver;
            properties2(best, &p2);
            gpu.driver = std::string(driver.driverName) + " " + driver.driverInfo;
            while (!gpu.driver.empty() && gpu.driver.back() == ' ') gpu.driver.pop_back();
        }
    } else {
        gpu.name = "No Vulkan graphics adapter";
    }
    if (destroy) destroy(instance, nullptr);
    dlclose(lib);
    gpu.preset = presetFor(gpu);
    gpu.renderer = 0;  // Vulkan, the only renderer the Linux game has
    core::logf("launcher: gpu: %s, driver %s, vulkan %s, %llu MB of its own, %s graphics", gpu.name.c_str(),
               gpu.driver.empty() ? "unsaid" : gpu.driver.c_str(), gpu.vulkan ? "yes" : "NO",
               (unsigned long long)(gpu.dedicated >> 20),
               gpu.preset >= 0 ? core::kGraphicsPresets[gpu.preset].name : "no preset");
    return gpu;
}

}  // namespace mu::launcher
