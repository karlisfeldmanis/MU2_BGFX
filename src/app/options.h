// The game menu's Options between the menu and the window: what the rows open on, and what a
// change does to the display. One pair for both screens that raise the menu -- the game and the
// character screen -- which kept a copy each until 2026-09-28, and the character screen's
// offered one size, its own, and never resized.
#pragma once

#include <algorithm>
#include <utility>

#include "game/ui/menu.h"
#include "gfx/window.h"

namespace mu::app {

// Filled from what the window is now. The sizes a window may take are the common 16:9 ones that
// fit the display, and the window's own size among them whatever it is, so the row opens on the
// truth.
inline void fillSettings(const gfx::Window& window, bool fps, game::Menu::Settings* set) {
    set->fullscreen = window.fullscreen();
    set->vsync = window.vsync();
    set->fps = fps;
    int dw = 0, dh = 0;
    window.displaySize(&dw, &dh);
    set->display = {dw, dh};
    int ww = 0, wh = 0;
    window.windowSize(&ww, &wh);
    const std::pair<int, int> common[] = {{1280, 720}, {1600, 900}, {1920, 1080},
                                          {2560, 1440}, {3200, 1800}, {3840, 2160}};
    set->sizes.clear();
    for (const auto& one : common) {
        if (dw <= 0 || (one.first <= dw && one.second <= dh)) set->sizes.push_back(one);
    }
    const std::pair<int, int> now{ww, wh};
    if (!set->fullscreen && ww > 0 &&
        std::find(set->sizes.begin(), set->sizes.end(), now) == set->sizes.end()) {
        set->sizes.push_back(now);
        std::sort(set->sizes.begin(), set->sizes.end());
    }
    set->size = 0;
    for (size_t i = 0; i < set->sizes.size(); ++i) {
        if (set->sizes[i] == now) set->size = int(i);
    }
}

// What the rows changed, onto the window: the display, the window's size while it is one, and
// v-sync. The volume and the counter are the caller's. Not saved; a new run starts from its own
// arguments. The window's new size reaches the renderer on the next pump.
inline void applySettings(gfx::Window& window, const game::Menu::Settings& set) {
    if (window.fullscreen() != set.fullscreen) window.setFullscreen(set.fullscreen);
    if (!set.fullscreen && !set.sizes.empty()) {
        const auto& want = set.sizes[size_t(std::clamp(set.size, 0, int(set.sizes.size()) - 1))];
        int ww = 0, wh = 0;
        window.windowSize(&ww, &wh);
        if (ww != want.first || wh != want.second) window.setWindowSize(want.first, want.second);
    }
    if (window.vsync() != set.vsync) window.setVsync(set.vsync);
}

}  // namespace mu::app
