#include "gfx/window.h"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>  // GLFW_EXPOSE_NATIVE_COCOA or _WIN32 comes from CMake

#include <bgfx/bgfx.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <cstdlib>

#include "core/log.h"
#include "core/watch.h"
#include "gfx/callback.h"
#include "gfx/views.h"

namespace mu::gfx {

#ifdef __linux__
// gfx/window_linux.cpp: the frame copied into the window by hand (MU2_COPY_PRESENT=1).
bool copyPresentAsked();
void copyPresentOpen(GLFWwindow* window, int width, int height);
void copyPresentClose();
#endif
#ifndef __linux__
void offscreenOpen(int width, int height);
void offscreenClose();
#endif

namespace {

// GLFW reports the wheel through a callback and nothing else, so it is accumulated here and
// drained by the pump. One window, so one accumulator: a second would need the user pointer
// and there is not going to be a second.
double s_scroll = 0.0;

void onScroll(GLFWwindow*, double, double y) { s_scroll += y; }

// And the typing, the same way: the characters and the three editing keys, drained by the pump.
std::string s_typed;
int s_backspaces = 0;
bool s_entered = false, s_escaped = false;
// And the chat's suggestions (game/ui/chat.cpp): Tab takes one, Up and Down walk them.
bool s_tabbed = false;
int s_arrows = 0;

void onChar(GLFWwindow*, unsigned int code) {
    // As UTF-8, so a box that takes a word later has nothing to change here.
    if (code < 0x80) {
        s_typed += char(code);
    } else if (code < 0x800) {
        s_typed += char(0xC0 | (code >> 6));
        s_typed += char(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        s_typed += char(0xE0 | (code >> 12));
        s_typed += char(0x80 | ((code >> 6) & 0x3F));
        s_typed += char(0x80 | (code & 0x3F));
    } else if (code < 0x110000) {  // past the first plane: CJK's rarer letters, emoji
        s_typed += char(0xF0 | (code >> 18));
        s_typed += char(0x80 | ((code >> 12) & 0x3F));
        s_typed += char(0x80 | ((code >> 6) & 0x3F));
        s_typed += char(0x80 | (code & 0x3F));
    }
}

void onKey(GLFWwindow*, int key, int, int action, int) {
    if (action == GLFW_RELEASE) return;
    if (key == GLFW_KEY_BACKSPACE) ++s_backspaces;  // press and repeat
    if (key == GLFW_KEY_UP) --s_arrows;
    if (key == GLFW_KEY_DOWN) ++s_arrows;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) s_entered = true;
    if (key == GLFW_KEY_ESCAPE) s_escaped = true;
    if (key == GLFW_KEY_TAB) s_tabbed = true;
}

Callback g_callback;

void onGlfwError(int code, const char* what) { core::logError("glfw %d: %s", code, what); }

// A texture bound after it was destroyed (bgfx-dead-texture.patch). Logged with who bound it, as the
// crash report names frames, the first few times and then every thousandth: a window's canvas
// still holding a picture it no longer owns does it every frame.
void onDeadTexture(uint16_t idx, uint8_t stage) {
    static std::atomic<uint32_t> s_dead{0};
    const uint32_t seen = ++s_dead;
    if (seen > 8 && seen % 1000 != 0) return;
    core::logError("graphics: texture %u bound on stage %u after it was destroyed (%u so far); drawn clear. From %s",
                   idx, stage, seen, core::watchCallers(1, 12).c_str());
}

}  // namespace

namespace {

// The whole primary display in screen points, at the mode it is already in -- no mode switch,
// so no black flash and no display left in another resolution if the run dies.
bool displayArea(int* x, int* y, int* width, int* height) {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode) return false;
    glfwGetMonitorPos(monitor, x, y);
    *width = mode->width;
    *height = mode->height;
    return true;
}

}  // namespace

bool Window::open(const WindowDesc& desc) {
    glfwSetErrorCallback(onGlfwError);
    // In build/MU2.app GLFW would move the working folder into Contents/Resources, and the
    // scripts' relative --shot paths with it.
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
#ifdef __linux__
    const bool copying = copyPresentAsked();
    // X11, which every Wayland desktop also runs through XWayland, until native Wayland has been
    // judged (docs/plans/linux-release.md, X6); MU2_WAYLAND=1 asks for it now.
    {
        const char* wayland = std::getenv("MU2_WAYLAND");
        const bool native = wayland && *wayland == '1' && glfwPlatformSupported(GLFW_PLATFORM_WAYLAND);
        glfwInitHint(GLFW_PLATFORM, native ? GLFW_PLATFORM_WAYLAND : GLFW_PLATFORM_X11);
    }
#endif
    if (!glfwInit()) {
        core::logError("glfw did not start");
        return false;
    }
    // bgfx makes the Metal layer; GLFW must not make a GL context beside it.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Fullscreen is borderless: a plain window with no frame over the whole display, the Dock
    // and the menu bar hidden while the game is in front (setPresentation). The size asked for
    // on the command line is not used then: the display's. Not GLFW's monitor fullscreen, which
    // on macOS floats the window over every other one and minimises it when it loses the focus,
    // so Cmd+Tab left the game on top of the app switched to, or in the Dock with no way back
    // (the user, 2026-10-13: "allow to use alt + tab, cmd+tab on app").
    int x = 0, y = 0, width = desc.width, height = desc.height;
    fullscreen_ = desc.fullscreen && displayArea(&x, &y, &width, &height);
    if (desc.fullscreen && !fullscreen_) {
        core::logError("no display to go fullscreen on; staying in a window");
    }
    glfwWindowHint(GLFW_DECORATED, fullscreen_ ? GLFW_FALSE : GLFW_TRUE);
#ifdef __linux__
    // The copied frame is made once, at the window's size (gfx/window_linux.cpp).
    if (copying) glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
#endif
    offscreen_ = desc.offscreen;
    // Never shown and never the focus: nothing of the game appears on the screen.
    if (offscreen_) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    }
    handle_ = glfwCreateWindow(width, height, desc.title, nullptr, nullptr);
    if (!handle_) {
        core::logError("no window");
        return false;
    }
    if (fullscreen_) glfwSetWindowPos(handle_, x, y);

    glfwSetScrollCallback(handle_, onScroll);
    glfwSetCharCallback(handle_, onChar);
    glfwSetKeyCallback(handle_, onKey);

    // MU's own pointer is drawn into the picture (game/cursor.h), and the real one hidden
    // underneath: two of them a few pixels apart is worse than either. MU2's own remark on
    // Pointer.cs applies unchanged -- this is what a screenshot shows, so the desktop's own
    // cursor must not be in it too.
    //
    // Not GLFW_CURSOR_HIDDEN: on macOS that is [NSCursor hide] when GLFW sees the pointer
    // enter the window and unhide when it sees it leave, and that bookkeeping goes wrong --
    // after a focus change, or the menu bar and the Dock sliding in at a fullscreen edge, the
    // arrow came back over the game's own (the user, 2026-10-02: *"there cannot be that
    // suddenly we see system cursor together with game cursor"*). A cursor image with no
    // pixels in it is the window's own cursor wherever the pointer is, and needs no tracking.
    {
        unsigned char clear[4 * 4 * 4] = {};
        GLFWimage image{4, 4, clear};
        blank_ = glfwCreateCursor(&image, 0, 0);
        if (blank_) {
            glfwSetCursor(handle_, blank_);
        } else {
            glfwSetInputMode(handle_, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        }
    }
    setPresentation(fullscreen_);
    glfwSetWindowFocusCallback(handle_, onFocus);
    if (!offscreen_) toFront();
    armHeld(true);

    // The backbuffer is asked for in pixels, not in points: on a Retina display the two
    // differ by two, and a frame measured at the wrong size is not the frame. Offscreen, the
    // frame is the size asked for, in pixels, whatever the display's scale.
    glfwGetFramebufferSize(handle_, &width_, &height_);
    if (offscreen_) {
        width_ = desc.width;
        height_ = desc.height;
    }

    bgfx::Init init;
    // The renderer asked for, or none: the shaders are SPIR-V on Windows and Metal on the Mac, so
    // bgfx's own fallback -- Direct3D 11 and 12 on Windows -- can draw nothing of ours, and with no
    // Vulkan under it Wine's crashed at address 0 in dxgi.dll (2026-10-15). A clean refusal is what
    // lets the game say why (application.cpp, "could not start its graphics").
    init.fallback = false;
#ifdef _WIN32
    // Vulkan on Windows, the renderer Wine on this Mac runs for real (docs/plans/windows-client.md),
    // or Direct3D 11 when asked: the cards with no Vulkan or a broken one, mostly Intel's HD and
    // UHD on old drivers (docs/plans/low-end-windows.md, P7). Its DXBC shaders ship beside the
    // SPIR-V, so this fallback can draw ours.
    init.type = desc.renderer == 2 ? bgfx::RendererType::Direct3D11 : bgfx::RendererType::Vulkan;
    // A capture hook in the game -- Medal's, OBS's game capture -- and NVIDIA's Vulkan holds the
    // next back buffer for seconds, past the acquire patch's tenth of a second: every one of a
    // player's fifty 5 s freezes in Tarkan had medal-hook64.dll loaded and the main thread in
    // SwapChainVK::acquire (hang reports of 2026-10-22, RTX 4080 SUPER; OBS's on 2026-10-15).
    // On auto such a game opens on Direct3D 11, where these hooks are at home. Vulkan asked for
    // by name stays Vulkan; the hook is in the log either way. One that comes in after this
    // stays on Vulkan, and the log says when it came (application.cpp).
    {
        const char* hook = captureHook();
        if (hook != nullptr && desc.renderer == 0) {
            core::logf("graphics: %s is in the game; Direct3D 11, not Vulkan", hook);
            init.type = bgfx::RendererType::Direct3D11;
        } else if (hook != nullptr) {
            core::logf("graphics: %s is in the game, on the renderer asked for", hook);
        }
    }
#elif defined(__linux__)
    // Vulkan and nothing else on Linux, with the same SPIR-V as Windows (docs/plans/linux-release.md).
    init.type = bgfx::RendererType::Vulkan;
#else
    init.type = bgfx::RendererType::Metal;  // the Mac; see docs/reference/conventions.md
#ifndef MU2_SHIP
    // MU2_VULKAN=1: the Windows renderer, Vulkan, drawn here through MoltenVK -- with the Windows
    // build's SPIR-V (MU2_SHADER_DIR) and --no-metalfx -- so Khronos' validation layer can check
    // what NVIDIA's driver is handed (tools/vkcheck.sh). A developer's switch, never the player's.
    if (const char* vulkan = std::getenv("MU2_VULKAN"); vulkan && *vulkan == '1') {
        init.type = bgfx::RendererType::Vulkan;
    }
#endif
#endif
    // The window goes on the swap chain, not on platformData: this bgfx asks for it there,
    // and it is the swap chain that carries the size, the formats and the latency. Kept in
    // chain_ so a resize can hand the same description back with only the size changed.
    //
    // Taken FROM init, not written over it. A bare SwapChain leaves formatColor as
    // TextureFormat::Count, while Init's own constructor fills in BGRA8, D24S8 and two back
    // buffers; handing the bare one to bgfx makes CAMetalLayer throw on setPixelFormat
    // before the first frame.
    chain_ = init.swapChain;
#ifdef _WIN32
    chain_.nwh = glfwGetWin32Window(handle_);
    if (offscreen_) chain_.nwh = nullptr;
#elif defined(__linux__)
    // The window and its display, X11's or Wayland's; bgfx must be told which of the two it is.
    if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) {
        chain_.nwh = glfwGetWaylandWindow(handle_);
        chain_.ndt = glfwGetWaylandDisplay();
        init.platformData.type = bgfx::NativeWindowHandleType::Wayland;
    } else {
        chain_.nwh = reinterpret_cast<void*>(uintptr_t(glfwGetX11Window(handle_)));
        chain_.ndt = glfwGetX11Display();
    }
    // No window for bgfx at all when the frame is copied by hand: bgfx then makes no swap chain.
    if (copying) chain_.nwh = chain_.ndt = nullptr;
#else
    chain_.nwh = glfwGetCocoaWindow(handle_);
#endif
    if (offscreen_) chain_.nwh = chain_.ndt = nullptr;
    // With no window bgfx makes no backbuffer, and refuses any size for it but 0x0.
    chain_.width = chain_.nwh ? uint32_t(width_) : 0;
    chain_.height = chain_.nwh ? uint32_t(height_) : 0;
    init.swapChain = chain_;
    // `profile` is what fills bgfx::Stats::viewStats, which is where every account's time
    // comes from. It stays on: a run that cannot be priced is a run wasted.
    init.profile = true;
    reset_ = desc.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    init.reset = reset_;
    init.callback = &g_callback;
    // Before init, and it is what keeps rendering on this thread in a multithreaded build: bgfx
    // starts no render thread when this has already been called. CMakeLists.txt says why the
    // build is multithreaded at all.
    //
    // Priced on 2026-09-24 with the call taken out, so bgfx ran its own render thread: the
    // same at 2560x1273 (6.34 and 6.31 ms against 6.31 and 6.93, interleaved) and 0.08 ms
    // faster at half scale. The frame is the GPU's, not the encoder's, and a render thread
    // costs a frame of latency, so it stays on this thread. docs/reference/budget.md.
    //
    // Unless asked (--render-thread, 2026-10-09): a hundred players in Noria put the frame back
    // on the CPU, with 43% of the main thread in bgfx's Metal encoding. On its own thread that
    // runs beside the game's next frame. MetalFX goes with it -- it is a view hook bgfx calls
    // from its renderer (gfx/metalfx.mm) -- and the preloader's worker already creates through
    // bgfx's resource lock.
    if (!desc.renderThread) bgfx::renderFrame();
    bool up = bgfx::init(init);
#ifdef _WIN32
    // A renderer that does not start gives way to the other: a machine with no Vulkan, or a
    // driver whose Vulkan does not start, draws through Direct3D 11, and one where Direct3D 11
    // fails through Vulkan. Asked for by name too -- Vulkan picked on the Options page, or the
    // launcher's low-end first run writing Direct3D 11 (launcher/gpu.h rendererFor) -- since
    // options.txt is kept and a refusal there was every start's, until the file was deleted.
    // bgfx undoes a failed init whole, so it can be asked again.
    if (!up) {
        const bool wasD3d = init.type == bgfx::RendererType::Direct3D11;
        core::logError("graphics: %s did not start; trying %s", wasD3d ? "Direct3D 11" : "Vulkan",
                       wasD3d ? "Vulkan" : "Direct3D 11");
        init.type = wasD3d ? bgfx::RendererType::Vulkan : bgfx::RendererType::Direct3D11;
        if (!desc.renderThread) bgfx::renderFrame();
        up = bgfx::init(init);
    }
#endif
    if (!up) {
        core::logError("bgfx did not start");
        return false;
    }
    // A draw handed a texture an earlier frame destroyed gets one clear texel instead, and the log
    // says who bound it: on Vulkan the destroyed one is an access violation in the driver, under
    // getDescriptorSet -> TextureVK::setState (crash reports of 2026-10-17, -18 and -20).
    {
        const uint8_t clear[4] = {0, 0, 0, 0};
        deadFallback_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0, bgfx::copy(clear, 4));
        bgfx::setDeadTextureGuard(deadFallback_, onDeadTexture);
    }
#ifdef __linux__
    if (copying) copyPresentOpen(handle_, width_, height_);
#else
    if (offscreen_) offscreenOpen(width_, height_);
#endif

    // Per-view GPU times are what the budget accounts are made of, and on Metal bgfx only
    // takes them when this debug flag is set — `Init::profile` alone leaves viewStats empty
    // and every account reads 0.000, which looks like a frame that costs nothing.
    //
    // Only when asked for. With the flag set, bgfx's Metal backend ends the render pass at
    // EVERY view (renderer_mtl.cpp, `|| profileViews`) so that it can time each one, where it
    // otherwise keeps one pass per target: views 4 and 5 share the shade target, and split,
    // the 4x MSAA colour and the depth are stored after the shade and loaded again for the
    // sprites. Found in a Metal System Trace, where the transparent pass was 774 us of a
    // 6.3 ms frame; switched off, the frame measured 0.77 ms faster at 2560x1273 and drew the
    // same picture. core::Args::views.
    if (desc.profileViews) bgfx::setDebug(BGFX_DEBUG_PROFILER);

    const bgfx::Caps* caps = bgfx::getCaps();
    core::logf("%s, %dx%d, vsync %s", bgfx::getRendererName(caps->rendererType), width_, height_,
               desc.vsync ? "on" : "off");
    {
        // Which card drew, at the top of any crash report from here on (core/watch.h).
        const uint16_t vendor = caps->vendorId;
        const char* maker = vendor == BGFX_PCI_ID_NVIDIA ? "NVIDIA"
                            : vendor == BGFX_PCI_ID_AMD ? "AMD"
                            : vendor == BGFX_PCI_ID_INTEL ? "Intel"
                            : vendor == BGFX_PCI_ID_APPLE ? "Apple"
                            : vendor == BGFX_PCI_ID_MICROSOFT ? "Microsoft (software)"
                                                              : "an unknown maker";
        char note[160];
        std::snprintf(note, sizeof note, "%s on %s, gpu %04x:%04x", bgfx::getRendererName(caps->rendererType),
                      maker, vendor, caps->deviceId);
        core::watchNote(note);
        core::logf("gpu: %s", note);
    }
    // Logged because docs/reference/conventions.md refuses to assume any of them.
    core::logf("homogeneous depth %d, origin bottom left %d, max texture %u, 32-bit indices %d",
               caps->homogeneousDepth, caps->originBottomLeft, caps->limits.maxTextureSize,
               (caps->supported & BGFX_CAPS_INDEX32) != 0);
    // What this GPU does with each format the renderer asks for: sampled (2d), drawn into (fb),
    // multisampled (msaa), or only emulated by a conversion on upload. A player's log then says
    // which one their GPU lacks (docs/plans/windows-client.md: D24S8 was missing on Vulkan).
    struct Wanted { bgfx::TextureFormat::Enum format; const char* name; };
    static const Wanted kWanted[] = {
        {bgfx::TextureFormat::RGBA8, "RGBA8"},     {bgfx::TextureFormat::RGBA16, "RGBA16"},
        {bgfx::TextureFormat::RGBA16F, "RGBA16F"}, {bgfx::TextureFormat::RGBA32F, "RGBA32F"},
        {bgfx::TextureFormat::RG16F, "RG16F"},     {bgfx::TextureFormat::R16F, "R16F"},
        {bgfx::TextureFormat::R8, "R8"},           {bgfx::TextureFormat::D16, "D16"},
        {bgfx::TextureFormat::D32F, "D32F"},       {bgfx::TextureFormat::BC7, "BC7"},
        {bgfx::TextureFormat::BC5, "BC5"},
    };
    std::string formats;
    for (const Wanted& one : kWanted) {
        const uint32_t f = caps->formats[one.format];
        formats += std::string(formats.empty() ? "" : ", ") + one.name + " " +
                   (f & BGFX_CAPS_FORMAT_TEXTURE_2D ? "2d" : "-") +
                   (f & BGFX_CAPS_FORMAT_TEXTURE_2D_EMULATED ? "(emulated)" : "") +
                   (f & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER ? " fb" : "") +
                   (f & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER_MSAA ? " msaa" : "");
    }
    core::logf("formats: %s", formats.c_str());
    return true;
}

void Window::close() {
    if (handle_) armHeld(false);
#ifdef __linux__
    copyPresentClose();
#else
    offscreenClose();
#endif
    bgfx::setDeadTextureGuard(BGFX_INVALID_HANDLE, nullptr);
    if (bgfx::isValid(deadFallback_)) bgfx::destroy(deadFallback_);
    deadFallback_ = BGFX_INVALID_HANDLE;
    bgfx::shutdown();
    if (handle_) glfwDestroyWindow(handle_);
    if (blank_) glfwDestroyCursor(blank_);
    blank_ = nullptr;
    if (handle_) setPresentation(false);
    glfwTerminate();
    handle_ = nullptr;
}

bool Window::pump() {
    glfwPollEvents();
    if (glfwWindowShouldClose(handle_)) return false;

    // The blank cursor again, every frame he is over the window and it has the keys. GLFW puts
    // a window's cursor up on a cursor-update event, which macOS sends when the pointer ENTERS;
    // a fullscreen window opens under a pointer already inside it, before the app is active,
    // so no enter ever came and the arrow stood over the game's own until it left the screen
    // and came back (the user, 2026-10-02: "i see system cursor when launch game in
    // fullscreen"). glfwSetCursor sets it only while the pointer is in the window, so this is
    // one [NSCursor set] and no tracking of its own.
    //
    // **Four times a second, not every frame** (2026-10-04): GLFW_HOVERED is a round trip to
    // the WindowServer (SLSCopyWindowRoutingRecordsForScreenLocation), and asked every frame it
    // was a fifth of the main thread's samples in a Meteorite fight at 180 Hz. A quarter second
    // of the arrow after a fullscreen open is all this gives back.
    const double now = glfwGetTime();
    if (blank_ && now >= cursorDue_) {
        cursorDue_ = now + 0.25;
        if (glfwGetWindowAttrib(handle_, GLFW_FOCUSED) &&
            glfwGetWindowAttrib(handle_, GLFW_HOVERED)) {
            glfwSetCursor(handle_, blank_);
        }
    }

    // Edges, taken here and cleared here: polled state would report one press for every frame
    // it lasts, which at 500 fps is fifty walk orders for one click.
    const int buttons[2] = {GLFW_MOUSE_BUTTON_LEFT, GLFW_MOUSE_BUTTON_RIGHT};
    for (int i = 0; i < 2; ++i) {
        const bool down = glfwGetMouseButton(handle_, buttons[i]) == GLFW_PRESS;
        clicked_[i] = down && !held_[i];
        released_[i] = !down && held_[i];
        held_[i] = down;
    }

    // The game's keys. Two for the bag, because MU binds both I and V to it.
    const int keys[size_t(Key::Count)][2] = {{GLFW_KEY_I, GLFW_KEY_V}, {GLFW_KEY_C, -1},
                                             {GLFW_KEY_1, -1},         {GLFW_KEY_2, -1},
                                             {GLFW_KEY_3, -1},         {GLFW_KEY_4, -1},
                                             {GLFW_KEY_5, -1},         {GLFW_KEY_Q, -1},
                                             {GLFW_KEY_W, -1},         {GLFW_KEY_E, -1},
                                             {GLFW_KEY_R, -1},         {GLFW_KEY_T, -1},
                                             {GLFW_KEY_L, -1},         {GLFW_KEY_M, -1},
                                             {GLFW_KEY_TAB, -1},       {GLFW_KEY_G, -1},
                                             {GLFW_KEY_F11, -1},       {GLFW_KEY_F12, -1},
                                             {GLFW_KEY_SPACE, -1},     {GLFW_KEY_H, -1},
                                             {GLFW_KEY_A, -1}};
    shift_ = glfwGetKey(handle_, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
             glfwGetKey(handle_, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    alt_ = glfwGetKey(handle_, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
           glfwGetKey(handle_, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    for (size_t i = 0; i < size_t(Key::Count); ++i) {
        bool down = false;
        for (int k : keys[i]) {
            if (k >= 0 && glfwGetKey(handle_, k) == GLFW_PRESS) down = true;
        }
        keyPressed_[i] = down && !keyHeld_[i];
        keyHeld_[i] = down;
    }

    // The step keys, on the same edge rule as the buttons. Left and Right walk one, Down and
    // Up walk ten, which is what makes a list of five hundred models usable by hand.
    const int stepKeys[size_t(Step::Count)] = {GLFW_KEY_LEFT,         GLFW_KEY_RIGHT,
                                               GLFW_KEY_DOWN,         GLFW_KEY_UP,
                                               GLFW_KEY_TAB,          GLFW_KEY_LEFT_BRACKET,
                                               GLFW_KEY_RIGHT_BRACKET, GLFW_KEY_T};
    for (size_t i = 0; i < size_t(Step::Count); ++i) {
        const bool down = glfwGetKey(handle_, stepKeys[i]) == GLFW_PRESS;
        stepped_[i] = down && !stepHeld_[i];
        stepHeld_[i] = down;
    }

    // The pointer's movement since the last pump, and the wheel's. The first pump after the
    // window opens has no previous position to subtract, and using (0,0) as one throws the
    // camera across the room on the first frame a button is down.
    float px = 0.0f, py = 0.0f;
    pointer(&px, &py);
    deltaX_ = hadPointer_ ? px - lastX_ : 0.0f;
    deltaY_ = hadPointer_ ? py - lastY_ : 0.0f;
    lastX_ = px;
    lastY_ = py;
    hadPointer_ = true;
    scroll_ = float(s_scroll);
    s_scroll = 0.0;
    typed_.swap(s_typed);
    s_typed.clear();
    backspaces_ = s_backspaces;
    s_backspaces = 0;
    entered_ = s_entered;
    escaped_ = s_escaped;
    s_entered = s_escaped = false;
    tabbed_ = s_tabbed;
    arrows_ = s_arrows;
    s_tabbed = false;
    s_arrows = 0;
    // An Escape that lands while a box is open belongs to the box, until the key is let go.
    const bool escapeDown = glfwGetKey(handle_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
    if (typing_ && (escaped_ || escapeDown)) escapeSwallowed_ = true;
    if (!escapeDown) escapeSwallowed_ = false;

    int w = 0, h = 0;
    glfwGetFramebufferSize(handle_, &w, &h);
    // Minimised, Windows says 0x0: kept at the last real size, so the renderer is not rebuilt at
    // nothing and every width a HUD or a projection divides by stays a width.
    if (!offscreen_ && (w != width_ || h != height_) && w > 0 && h > 0) {
        width_ = w;
        height_ = h;
        chain_.width = chain_.nwh ? uint32_t(w) : 0;
        chain_.height = chain_.nwh ? uint32_t(h) : 0;
        bgfx::reset(reset_, swapChain());
        core::logf("resized to %dx%d", w, h);
    }
    pumpedAt_ = glfwGetTime();
    if (heldSince_) {
        core::logf("window: let go after %.1f s", pumpedAt_ - heldSince_);
        heldSince_ = 0.0;
    }
    return true;
}

void Window::heldTick() {
    if (!whileHeld_ || !handle_ || glfwGetTime() - pumpedAt_ < 0.25) return;
    if (!heldSince_) {
        heldSince_ = pumpedAt_;
        core::logf("window: held by the system (a title bar or an edge); the line is kept up");
    }
    whileHeld_();
}

void Window::hide() {
    if (handle_) glfwHideWindow(handle_);
}

bool Window::fullscreen() const { return handle_ && fullscreen_; }

bool Window::focused() const { return handle_ && glfwGetWindowAttrib(handle_, GLFW_FOCUSED); }

bool Window::minimised() const {
    return handle_ && !offscreen_ && glfwGetWindowAttrib(handle_, GLFW_ICONIFIED);
}

void Window::toFront() {
    if (!handle_ || focused()) return;
    glfwShowWindow(handle_);
    glfwFocusWindow(handle_);  // SetForegroundWindow on Windows, which the system may refuse
    if (!focused()) core::logf("window: asked to come to the front; %s has it", focusOwner().c_str());
}

void Window::onFocus(GLFWwindow*, int focused) {
    if (focused) {
        core::logf("window: focus back");
    } else {
        core::logf("window: focus lost to %s", focusOwner().c_str());
    }
}

void Window::setFullscreen(bool on) {
    if (!handle_ || on == fullscreen()) return;
    if (on) {
        int x = 0, y = 0, width = 0, height = 0;
        if (!displayArea(&x, &y, &width, &height)) {
            core::logError("no display to go fullscreen on; staying in a window");
            return;
        }
        glfwGetWindowPos(handle_, &windowedX_, &windowedY_);
        glfwGetWindowSize(handle_, &windowedW_, &windowedH_);
        // Borderless over the display, as open() makes it (and why there).
        glfwSetWindowAttrib(handle_, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowMonitor(handle_, nullptr, x, y, width, height, GLFW_DONT_CARE);
    } else {
        glfwSetWindowAttrib(handle_, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowMonitor(handle_, nullptr, windowedX_, windowedY_, windowedW_, windowedH_,
                             GLFW_DONT_CARE);
    }
    fullscreen_ = on;
    setPresentation(on);
    core::logf("window: %s", on ? "fullscreen" : "windowed");
}

void Window::windowSize(int* width, int* height) const {
    *width = *height = 0;
    if (handle_) glfwGetWindowSize(handle_, width, height);
}

void Window::setWindowSize(int width, int height) {
    if (!handle_ || width <= 0 || height <= 0) return;
    if (fullscreen()) {
        // Taken as the size to come back to: fullscreen keeps the display's own.
        windowedW_ = width;
        windowedH_ = height;
        return;
    }
    glfwSetWindowSize(handle_, width, height);
    core::logf("window: sized to %dx%d points", width, height);
}

void Window::displaySize(int* width, int* height) const {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    *width = mode ? mode->width : 0;
    *height = mode ? mode->height : 0;
}

float Window::topInset() const {
    // MU2_NOTCH=<pixels> stands one in, in a window or offscreen too: a review shot on a Mac
    // without a notch.
    if (const char* faked = std::getenv("MU2_NOTCH")) return float(std::atof(faked));
    if (!fullscreen()) return 0.0f;
    int points = 0, ignored = 0;
    glfwGetWindowSize(handle_, &ignored, &points);
    return points > 0 ? notchPoints() * float(height_) / float(points) : 0.0f;
}

void Window::setVsync(bool on) {
    reset_ = on ? (reset_ | BGFX_RESET_VSYNC) : (reset_ & ~uint32_t(BGFX_RESET_VSYNC));
    bgfx::reset(reset_, swapChain());
    core::logf("window: vsync %s", on ? "on" : "off");
}

void Window::holdVsync(bool off) {
    bgfx::reset(off ? (reset_ & ~uint32_t(BGFX_RESET_VSYNC)) : reset_, swapChain());
}

float Window::contentScale() const {
    float x = 1.0f, y = 1.0f;
    if (handle_) glfwGetWindowContentScale(handle_, &x, &y);
    return x > 0.0f ? x : 1.0f;
}

int Window::refreshHz() const {
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    return (mode && mode->refreshRate > 0) ? mode->refreshRate : 60;
}

void Window::pointer(float* x, float* y) const {
    double px = 0.0, py = 0.0;
    glfwGetCursorPos(handle_, &px, &py);
    // Points to pixels, by the ratio the framebuffer actually has. Asking GLFW for the content
    // scale is the other way to get it and is a different number on a window straddling two
    // displays; this one is the ratio of the two sizes we already hold.
    int windowWidth = 0, windowHeight = 0;
    glfwGetWindowSize(handle_, &windowWidth, &windowHeight);
    *x = float(px) * (windowWidth > 0 ? float(width_) / float(windowWidth) : 1.0f);
    *y = float(py) * (windowHeight > 0 ? float(height_) / float(windowHeight) : 1.0f);
}

bool Window::escapePressed() const {
    return !escapeHeld_ && !typing_ && !escapeSwallowed_ && glfwGetKey(handle_, GLFW_KEY_ESCAPE) == GLFW_PRESS;
}

const char* captureHook() {
#ifdef _WIN32
    for (const char* name : {"medal-hook64.dll", "graphics-hook64.dll"}) {
        if (GetModuleHandleA(name) != nullptr) return name;
    }
#endif
    return nullptr;
}

const char* shaderProfile() {
    switch (bgfx::getRendererType()) {
        case bgfx::RendererType::Metal: return "metal";
        case bgfx::RendererType::Direct3D11: return "dxbc";
        default: return "spirv";
    }
}

std::string shadersFor(const std::string& folder) {
    const size_t slash = folder.find_last_of("/\\");
    const std::string last = slash == std::string::npos ? folder : folder.substr(slash + 1);
    if (last != "metal" && last != "spirv" && last != "dxbc") return folder;
    return folder.substr(0, slash == std::string::npos ? 0 : slash + 1) + shaderProfile();
}

#ifndef __linux__
namespace {

// --offscreen. bgfx's Metal backend photographs a swap chain and nothing else, so the frame is
// read back by hand: blitted into one of a few read-back textures on the last view, read, and
// written as a PNG on its own thread once the pixels land -- two frames later, with the main
// thread never waiting unless every slot is still in flight.
constexpr bgfx::ViewId kReadView = 255;
constexpr int kSlots = 6;

struct Slot {
    bgfx::TextureHandle read = BGFX_INVALID_HANDLE;
    std::vector<uint8_t> pixels;
    uint32_t readyAt = 0;
    bool reading = false;
    std::string path;
    std::thread writer;
    uint32_t sequence = 0;
};

struct Offscreen {
    bool on = false;
    int width = 0, height = 0;
    bool flip = false;
    bgfx::TextureHandle colour = BGFX_INVALID_HANDLE, depth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle frame = BGFX_INVALID_HANDLE;
    Slot slots[kSlots];
    std::string asked;
    uint32_t lastFrame = 0;
    // --raw-frames: every shot's pixels, BGRA top row first, written in order to this file --
    // a pipe into ffmpeg (tools/mu/record.py) -- instead of a PNG. A 1080p PNG takes a quarter
    // of a second to compress, forty times what the frame takes to draw.
    std::FILE* raw = nullptr;
    uint32_t sequence = 0, written = 0;
};
Offscreen g_off;

void writePng(Slot* slot, int width, int height, bool flip) {
    g_callback.screenShot(slot->path.c_str(), uint32_t(width), uint32_t(height),
                          uint32_t(width) * 4, bgfx::TextureFormat::BGRA8, slot->pixels.data(),
                          uint32_t(slot->pixels.size()), flip);
}

void writeRaw(Slot* slot) {
    Offscreen& o = g_off;
    const size_t row = size_t(o.width) * 4;
    if (!o.flip) {
        std::fwrite(slot->pixels.data(), 1, slot->pixels.size(), o.raw);
    } else {
        for (int y = o.height - 1; y >= 0; --y) {
            std::fwrite(slot->pixels.data() + size_t(y) * row, 1, row, o.raw);
        }
    }
}

void land(uint32_t frame) {
    Offscreen& o = g_off;
    if (o.raw) {
        // In the order they were asked, whatever order the slots are in.
        for (bool more = true; more;) {
            more = false;
            for (Slot& slot : o.slots) {
                if (!slot.reading || frame < slot.readyAt || slot.sequence != o.written) continue;
                slot.reading = false;
                writeRaw(&slot);
                ++o.written;
                more = true;
            }
        }
        return;
    }
    for (Slot& slot : o.slots) {
        if (!slot.reading || frame < slot.readyAt) continue;
        slot.reading = false;
        slot.writer = std::thread(writePng, &slot, o.width, o.height, o.flip);
    }
}

}  // namespace

void offscreenOpen(int width, int height) {
    Offscreen& o = g_off;
    o.width = width;
    o.height = height;
    o.flip = bgfx::getCaps()->originBottomLeft;
    o.colour = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
                                     bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT);
    o.depth = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
                                    bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT_WRITE_ONLY);
    const bgfx::TextureHandle targets[] = {o.colour, o.depth};
    o.frame = bgfx::createFrameBuffer(2, targets, false);
    for (Slot& slot : o.slots) {
        slot.read = bgfx::createTexture2D(uint16_t(width), uint16_t(height), false, 1,
                                          bgfx::TextureFormat::BGRA8,
                                          BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
        slot.pixels.assign(size_t(width) * size_t(height) * 4, 0);
    }
    // Every view starts on the frame, as it would on the backbuffer; a view the game points at
    // a target of its own keeps it, and those that name the backbuffer ask screenTarget().
    for (int view = 0; view <= kReadView; ++view) {
        bgfx::setViewFrameBuffer(bgfx::ViewId(view), o.frame);
    }
    o.on = bgfx::isValid(o.frame);
    core::logf("offscreen: %s, %dx%d, no window on the screen", o.on ? "on" : "FAILED", width,
               height);
}

void offscreenClose() {
    Offscreen& o = g_off;
    if (!o.on) return;
    offscreenFlush();
    o.on = false;
    for (Slot& slot : o.slots) {
        if (slot.writer.joinable()) slot.writer.join();
        if (bgfx::isValid(slot.read)) bgfx::destroy(slot.read);
    }
    if (bgfx::isValid(o.frame)) bgfx::destroy(o.frame);
    if (bgfx::isValid(o.colour)) bgfx::destroy(o.colour);
    if (bgfx::isValid(o.depth)) bgfx::destroy(o.depth);
}

bgfx::FrameBufferHandle screenTarget() {
    if (g_off.on) return g_off.frame;
    return BGFX_INVALID_HANDLE;
}

bool offscreenRaw(const char* path) {
    Offscreen& o = g_off;
    if (!o.on) return false;
    o.raw = std::fopen(path, "wb");
    if (!o.raw) {
        core::logError("offscreen: cannot write the raw frames to %s", path);
        return false;
    }
    core::logf("offscreen: raw %dx%d BGRA frames to %s", o.width, o.height, path);
    return true;
}

bool offscreenShot(const char* path) {
    if (!g_off.on) return false;
    g_off.asked = path;
    return true;
}

void beforeFrame() {
    Offscreen& o = g_off;
    if (!o.on || o.asked.empty()) return;
    Slot* free = nullptr;
    // A slot whose pixels have been written; when all six are still in flight, the oldest
    // writer is waited for -- the one place this ever stalls.
    for (Slot& slot : o.slots) {
        if (slot.reading) continue;
        if (!slot.writer.joinable()) {
            free = &slot;
            break;
        }
    }
    if (!free) {
        for (Slot& slot : o.slots) {
            if (slot.reading || !slot.writer.joinable()) continue;
            slot.writer.join();
            free = &slot;
            break;
        }
    }
    if (!free) {
        core::logError("offscreen: every read-back slot is busy; %s dropped", o.asked.c_str());
        o.asked.clear();
        return;
    }
    if (free->writer.joinable()) free->writer.join();
    bgfx::touch(kReadView);
    bgfx::TextureRegion to, from, whole;
    to.init(free->read);
    from.init(o.colour);
    whole.init(free->read);
    bgfx::blit(kReadView, to, from);
    free->readyAt = bgfx::read(whole, free->pixels.data());
    free->reading = true;
    free->sequence = o.sequence++;
    free->path = o.asked;
    o.asked.clear();
}

void afterFrame(uint32_t frame) {
    if (!g_off.on) return;
    g_off.lastFrame = frame;
    land(frame);
}

void offscreenFlush() {
    Offscreen& o = g_off;
    if (!o.on) return;
    for (int i = 0; i < 16; ++i) {
        bool waiting = false;
        for (const Slot& slot : o.slots) waiting = waiting || slot.reading;
        if (!waiting) break;
        land(bgfx::frame());
    }
    for (Slot& slot : o.slots) {
        if (slot.writer.joinable()) slot.writer.join();
    }
    if (o.raw) {
        std::fclose(o.raw);
        o.raw = nullptr;
        core::logf("offscreen: %u raw frames written", o.written);
    }
}
#endif

}  // namespace mu::gfx
