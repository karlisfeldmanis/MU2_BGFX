// The window and bgfx on it. Knows nothing of the game: no map, no figure, no camera.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <bgfx/bgfx.h>

struct GLFWwindow;
struct GLFWcursor;

namespace mu::gfx {

struct WindowDesc {
    int width = 1920;
    int height = 1080;
    // The window takes the display and its current mode, and width/height are not asked for.
    bool fullscreen = false;
    bool vsync = false;
    // bgfx's per-view GPU timers. core::Args::views says what they cost and who asks.
    bool profileViews = false;
    // bgfx's own render thread (--render-thread): the Metal encoding of frame N runs beside the
    // game's frame N+1, for a frame of latency. Off, rendering stays on this thread.
    bool renderThread = false;
    // core::Args::renderer: 0 Vulkan and then Direct3D 11, 1 Vulkan, 2 Direct3D 11. Windows only.
    int renderer = 0;
    const char* title = "MU2";
    // --offscreen: no window on the screen and no swap chain. Every frame is drawn into a
    // framebuffer of exactly width x height pixels, and shots are read back from it
    // (offscreenShot). A recording's, made while the Mac is used for something else.
    bool offscreen = false;
};

// The shaders' folder for the renderer bgfx is running: metal, spirv or dxbc (CMakeLists.txt,
// "shaders"). Asked after Window::open, since on Windows the renderer is known only then.
const char* shaderProfile();
// A capture hook that holds NVIDIA's Vulkan back buffer (Medal's, OBS's game capture) loaded in
// the game, or null; always null off Windows. Window::open opens on Direct3D 11 for one.
const char* captureHook();
// `folder` with its last part made shaderProfile() when that part names a profile: what the
// build and the packs call spirv is dxbc on a Direct3D 11 run. Any other folder is kept.
std::string shadersFor(const std::string& folder);

// The screen the last views draw to: the backbuffer (BGFX_INVALID_HANDLE), or, on a Linux run
// with MU2_COPY_PRESENT=1, the frame copied into the window by hand (gfx/window_linux.cpp) -- a
// developer's way to watch the Linux client on a GPU that can draw but not present, as the Mac's
// own through Venus in a libkrun machine (docs/plans/linux-release.md).
bgfx::FrameBufferHandle screenTarget();
// Around each bgfx::frame(): the copy's blit and read asked before, its picture put in the window
// after. Nothing at all on any other run.
void beforeFrame();
void afterFrame(uint32_t frame);
// An --offscreen run's screenshot: the frame being drawn, read back a few frames later and
// written as a PNG off the main thread, so no frame waits for it. False when not offscreen.
bool offscreenShot(const char* path);
// Shots as raw BGRA frames, in order, into this file or pipe rather than as PNGs.
bool offscreenRaw(const char* path);
// Every shot asked for is on disk when this returns: frames are run until the reads land.
void offscreenFlush();

class Window {
public:
    bool open(const WindowDesc& desc);
    void close();

    // Pumps the OS. False once the window wants to go.
    bool pump();

    // Vsync off for a while and back to what the window was opened with. The preloader
    // presents without it: bgfx holds its resource lock for the whole of a frame, the wait for
    // the display included, and a spinner paced by vsync starved the loading thread of it --
    // 3.6 s to load what takes 0.35 s. See app/preloader.h.
    void holdVsync(bool off);

    // The display's own refresh, in hertz, or 60 when it cannot be had. The preloader paces
    // itself by this: it presents unsynced, so it has to know what it is pacing to.
    int refreshHz() const;

    // The display's scale: 2 on a Retina Mac, 1.5 on a Windows display at 150%, 1 at 100%.
    // GLFW's content scale, 1 when it cannot be had.
    float contentScale() const;

    // Escape as the application quits on: held down, and not while something is typing --
    // an Escape that cancels a number box must not also end the game. Once swallowed it stays
    // swallowed until the key comes up, so the frame after the box closes does not quit either.
    bool escapePressed() const;
    // Escape as the game's own key: in a played world it raises the menu, and quitting is the
    // menu's Exit. Held, escapePressed() never answers; the edge `escaped()` still does.
    void holdEscape(bool held) { escapeHeld_ = held; }

    // **Typing**, for the few boxes that take a number or a word. What was typed since the last
    // pump, as UTF-8 off GLFW's character callback (which is the layout's own character, not a
    // key code), and three edges beside it: Backspace (counted, so the OS's key repeat deletes a
    // run), Enter and Escape. Read every frame whether anything types or not; `setTyping` says
    // whether the game has a box open, which is what takes Escape away from quitting.
    const std::string& typed() const { return typed_; }
    int backspaces() const { return backspaces_; }
    bool entered() const { return entered_; }
    bool escaped() const { return escaped_; }
    // Tab pressed, and Up and Down as a sum (-1 a press of Up, with repeat): the chat's
    // suggestions read them while it types.
    bool tabbed() const { return tabbed_; }
    int arrows() const { return arrows_; }
    void setTyping(bool typing) { typing_ = typing; }
    bool typing() const { return typing_; }

    // The pointer, in FRAMEBUFFER pixels rather than in the points GLFW reports: on a Retina
    // display the two differ by two, and a pick that unprojects points against a 1920-wide
    // backbuffer lands half way up the screen.
    void pointer(float* x, float* y) const;
    // True once for each press, cleared by the pump that reports it. An edge and not a state,
    // because a click is a thing that happens and a frame at 500 fps sees one press as fifty.
    bool clicked(int button) const { return clicked_[button & 1]; }

    // The same edge rule for the few keys the benches steer by. Named rather than given as
    // GLFW codes so that including this header does not drag GLFW into the game: `gfx` owns
    // the window and nothing above it should know which library opens one.
    //
    // A frame here runs at 400 to 900 fps, so a key held for the shortest press a hand can
    // make is down for several hundred frames. Read as a state, one tap of Right walks the
    // whole model list and lands wherever it ran out.
    enum class Step {
        Previous, Next, PreviousTen, NextTen,
        // Tab walks the viewer's categories -- world objects, monsters, people -- and the two
        // bracket keys walk the clips of whatever figure is standing there.
        Category, PreviousClip, NextClip,
        // T walks the viewer's times of day: noon, dusk, night. content/sheets/time/.
        Time,
        Count
    };
    bool stepped(Step step) const { return stepped_[size_t(step)]; }

    // The other edge: true once for each let-go. A drag ends here, and a button in a window
    // fires here, on the release that lands on the box it went down on -- MU's own rule, which
    // is what lets a press be taken back by sliding off.
    bool released(int button) const { return released_[button & 1]; }

    // The game's keys, on the same edge rule. Named for what they do rather than which key
    // they are, so the binding is written down once, here and in window.cpp.
    //   Inventory  I and V, MU's two
    //   Character  C
    //   Potion1-4  1 to 4, the quick slots (sprint 7: skills keep Q W E R, skills-dk §0)
    // Q W E R T are the skill bar, decided on 2026-09-21 and printed on the plate under its
    // five boxes; 1 to 5 are the potions. MuDream paints the other arrangement and the HUD
    // covers it. T was painted from the first day and did nothing until 2026-09-23, when the
    // user tried to drag a skill onto it.
    enum class Key {
        Inventory, Character,
        Potion1, Potion2, Potion3, Potion4, Potion5,
        Skill1, Skill2, Skill3, Skill4, Skill5,
        Repair,  // L: a mending counter's repair mode, Shift+L its repair-all (CNewUINPCShop); with
                 // the bag down, the quest journal (game/ui/desk.cpp)
        Travel,  // M: the travel list, shown and shut (game/ui/travel.h)
        Map,     // Tab, held: the whole map over the middle of the screen (game/ui/minimap.h)
        Guild,   // G: the guild window, shown and shut (game/ui/guild.h, docs/game/guild.md)
        HideHud, // F11: the HUD and its readout hidden and shown, for a clean picture (H was its
                 // second key until the Helper took H, 2026-10-20)
        Shot,    // F12: the frame as drawn, a PNG in the player's screenshots folder
        Pick,    // Space: the nearest drop picked up (game/play_pointer.cpp, Play::pickNearest)
        Helper,  // H: the Helper started and stopped, its window the first time (game/helper.h)
        Talents, // A: Talents, shown and shut -- MU's master-tree key since Season 4 (the user,
                 // 2026-10-20; game/ui/talent_window.h)
        Count
    };
    // Neither while a box has the keyboard (setTyping): an I typed into the chat is a letter,
    // not the bag.
    bool pressed(Key key) const { return !typing_ && keyPressed_[size_t(key)]; }
    // And whether it is down now, pressed this frame or before: a key held to go on casting.
    bool down(Key key) const { return !typing_ && keyHeld_[size_t(key)]; }
    bool shift() const { return shift_; }
    // Alt (Option on a Mac), held: the pointer passes through monsters (Play::point).
    bool alt() const { return !typing_ && alt_; }

    // Held, rather than the edge `clicked` reports: a drag is a thing that continues.
    bool held(int button) const { return held_[button & 1]; }
    // This frame's buttons taken from whatever reads them after: the studio's panels have the
    // pointer, and the game played under them (app/modes/studio_arena.h) must not walk to it.
    void swallowPointer() {
        for (int i = 0; i < 2; ++i) clicked_[i] = released_[i] = held_[i] = false;
    }
    // How far the pointer moved since the last pump, in framebuffer pixels.
    void pointerDelta(float* x, float* y) const { *x = deltaX_; *y = deltaY_; }
    // The wheel since the last pump, in whatever units the OS reports. Positive is towards
    // the screen, which every viewer in the world means as "closer".
    float scroll() const { return scroll_; }

    int width() const { return width_; }
    int height() const { return height_; }

    // **What the game menu's Options changes while the game runs.** Fullscreen is the display
    // at its own mode, as open() takes it -- no mode switch -- and leaving it puts the window
    // back where and how big it was. The window's size is in screen points, which is what a
    // player picks; the backbuffer follows on the next pump, as any resize does. V-sync is the
    // reset flag, handed to bgfx at once.
    bool fullscreen() const;
    void setFullscreen(bool on);
    void windowSize(int* width, int* height) const;
    void setWindowSize(int width, int height);
    // The primary display's own mode, in screen points: the largest a window can be.
    void displaySize(int* width, int* height) const;
    bool vsync() const { return (reset_ & BGFX_RESET_VSYNC) != 0; }
    void setVsync(bool on);

    // **In front** (the user, 2026-10-15, on a Windows PC: the game opened behind the launcher's
    // desktop, and again behind something after entering the world, each time until Alt+Tab).
    // open() brings the window forward once it is made; the launcher has let it (engine.cpp
    // play, AllowSetForegroundWindow). focused() is whether it has the keys. Every loss of the
    // focus is logged with who took it (focusOwner), which is what says why on the next report.
    bool focused() const;
    // On the taskbar: nothing is seen, and the frame is paced down to kMinimisedCap (application.cpp).
    bool minimised() const;
    void toFront();
    // Off the screen and the taskbar, still pumped: a window closed during a load is gone for the
    // player at once, while the load behind it comes back (app/preloader.cpp).
    void hide();

    // **While the OS holds this thread** (2026-10-15): on Windows a title bar held or dragged,
    // a window edge dragged, or the system menu open puts glfwPollEvents inside the system's own
    // loop, and no frame runs until it lets go. The server's line went unread -- it lets a line
    // go after 20 s of owing it (server/src/host.h, kStalledSeconds) -- and the freeze watch
    // filed a report at 5 s. A timer those loops hand to the thread's message filter calls `call`
    // about ten times a second once no pump has come back for a quarter of one; never while
    // frames run.
    // The Mac's drags do not stop the loop, so it is never called there.
    void whileHeld(std::function<void()> call) { whileHeld_ = std::move(call); }
    // The message filter's own (window_win.cpp).
    void heldTick();

private:
    // The window that has the focus when this one has not: "title (program.exe)" on Windows,
    // the front app's name on the Mac. window_win.cpp, window_mac.mm.
    static std::string focusOwner();
    // What bgfx::reset is handed: the window's swap chain, or none when bgfx was given no window
    // (MU2_COPY_PRESENT), which it refuses one for.
    const bgfx::SwapChain* swapChain() const { return chain_.nwh ? &chain_ : nullptr; }
    bool offscreen_ = false;
    static void onFocus(GLFWwindow* window, int focused);

    // Fullscreen hides the menu bar and the Dock outright rather than letting them slide in
    // at an edge, which brought the system's arrow with them. window_mac.mm.
    static void setPresentation(bool fullscreen);
    // Arms and disarms whileHeld's timer and filter on the window. window_win.cpp; nothing on the Mac.
    void armHeld(bool on);

    GLFWwindow* handle_ = nullptr;
    std::function<void()> whileHeld_;
    double pumpedAt_ = 0.0;  // glfwGetTime when the last pump came back
    double heldSince_ = 0.0;  // when a hold began, 0 when none is on
    // A fully transparent cursor the window wears in place of the system's. See open().
    GLFWcursor* blank_ = nullptr;
    double cursorDue_ = 0.0;  // when pump() next re-asserts blank_ (glfwGetTime seconds)
    // What a destroyed texture is drawn as instead: one clear texel. See open().
    bgfx::TextureHandle deadFallback_ = BGFX_INVALID_HANDLE;
    int width_ = 0;
    int height_ = 0;
    bool clicked_[2] = {false, false};  // 0 left, 1 right
    bool held_[2] = {false, false};
    bool released_[2] = {false, false};
    bool keyPressed_[size_t(Key::Count)] = {};
    bool keyHeld_[size_t(Key::Count)] = {};
    bool shift_ = false;
    bool alt_ = false;
    bool stepped_[size_t(Step::Count)] = {};
    bool stepHeld_[size_t(Step::Count)] = {};
    float lastX_ = 0.0f, lastY_ = 0.0f;
    float deltaX_ = 0.0f, deltaY_ = 0.0f;
    bool hadPointer_ = false;
    float scroll_ = 0.0f;
    std::string typed_;
    int backspaces_ = 0;
    bool entered_ = false, escaped_ = false;
    bool tabbed_ = false;
    int arrows_ = 0;
    bool typing_ = false;
    bool escapeSwallowed_ = false;
    bool escapeHeld_ = false;
    // Where the window was, and how big, before it went fullscreen.
    int windowedX_ = 80, windowedY_ = 80, windowedW_ = 1920, windowedH_ = 1080;
    bool fullscreen_ = false;  // borderless over the display (window.cpp, open)
    uint32_t reset_ = 0;
    // Kept whole from init. A resize passes this back with a new size, because a
    // default-constructed SwapChain has a NULL window handle, which bgfx reads as a request
    // for a headless device -- and the window stops presenting.
    bgfx::SwapChain chain_;
};

}  // namespace mu::gfx
