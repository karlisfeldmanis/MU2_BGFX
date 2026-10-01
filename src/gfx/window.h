// The window and bgfx on it. Knows nothing of the game: no map, no figure, no camera.
#pragma once

#include <cstdint>
#include <string>

#include <bgfx/bgfx.h>

struct GLFWwindow;

namespace mu::gfx {

struct WindowDesc {
    int width = 1920;
    int height = 1080;
    // The window takes the display and its current mode, and width/height are not asked for.
    bool fullscreen = false;
    bool vsync = false;
    // bgfx's per-view GPU timers. core::Args::views says what they cost and who asks.
    bool profileViews = false;
    const char* title = "MU2";
};

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
        // T walks the viewer's times of day: noon, dusk, night. sheets/time/.
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
    //   Potion1-4  1 to 4, the quick slots (sprint 7: skills keep Q W E R, PLAN.md)
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
        Count
    };
    bool pressed(Key key) const { return keyPressed_[size_t(key)]; }
    // And whether it is down now, pressed this frame or before: a key held to go on casting.
    bool down(Key key) const { return keyHeld_[size_t(key)]; }
    bool shift() const { return shift_; }

    // Held, rather than the edge `clicked` reports: a drag is a thing that continues.
    bool held(int button) const { return held_[button & 1]; }
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

private:
    GLFWwindow* handle_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool clicked_[2] = {false, false};  // 0 left, 1 right
    bool held_[2] = {false, false};
    bool released_[2] = {false, false};
    bool keyPressed_[size_t(Key::Count)] = {};
    bool keyHeld_[size_t(Key::Count)] = {};
    bool shift_ = false;
    bool stepped_[size_t(Step::Count)] = {};
    bool stepHeld_[size_t(Step::Count)] = {};
    float lastX_ = 0.0f, lastY_ = 0.0f;
    float deltaX_ = 0.0f, deltaY_ = 0.0f;
    bool hadPointer_ = false;
    float scroll_ = 0.0f;
    std::string typed_;
    int backspaces_ = 0;
    bool entered_ = false, escaped_ = false;
    bool typing_ = false;
    bool escapeSwallowed_ = false;
    bool escapeHeld_ = false;
    // Where the window was, and how big, before it went fullscreen.
    int windowedX_ = 80, windowedY_ = 80, windowedW_ = 1920, windowedH_ = 1080;
    uint32_t reset_ = 0;
    // Kept whole from init. A resize passes this back with a new size, because a
    // default-constructed SwapChain has a NULL window handle, which bgfx reads as a request
    // for a headless device -- and the window stops presenting.
    bgfx::SwapChain chain_;
};

}  // namespace mu::gfx
