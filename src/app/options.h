// The game menu's Options between the menu and the window: what the rows open on, and what a
// change does to the display. One pair for both screens that raise the menu -- the game and the
// character screen -- which kept a copy each until 2026-09-28, and the character screen's
// offered one size, its own, and never resized.
#pragma once

#include <algorithm>
#include <utility>

#include "core/args.h"
#include "game/ui/menu.h"
#include "gfx/window.h"

namespace mu::app {

// Filled from what the window is now, and the counter and the volume from the run. The sizes a
// window may take are the common 16:9 ones that fit the display, and the window's own size among
// them whatever it is, so the row opens on the truth.
inline void fillSettings(const gfx::Window& window, const core::Args& args,
                         game::Menu::Settings* set) {
    set->fullscreen = window.fullscreen();
    set->vsync = window.vsync();
    set->fps = args.fps;
    set->volume = args.volume;
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
// v-sync. The volume is the caller's to hand its sound. All of it goes back into the run's args,
// so the other screen opens on it too, and into options.txt when the run is main.sh's
// --remember one. The window's new size reaches the renderer on the next pump.
inline void applySettings(gfx::Window& window, const game::Menu::Settings& set,
                          core::Args& args) {
    if (window.fullscreen() != set.fullscreen) window.setFullscreen(set.fullscreen);
    if (!set.fullscreen && !set.sizes.empty()) {
        const auto& want = set.sizes[size_t(std::clamp(set.size, 0, int(set.sizes.size()) - 1))];
        int ww = 0, wh = 0;
        window.windowSize(&ww, &wh);
        if (ww != want.first || wh != want.second) window.setWindowSize(want.first, want.second);
    }
    if (window.vsync() != set.vsync) window.setVsync(set.vsync);
    args.fullscreen = set.fullscreen;
    args.vsync = set.vsync;
    args.fps = set.fps;
    args.volume = set.volume;
    // The window's size as asked for, not as the window reports it yet, and kept through
    // fullscreen so going back to a window after a restart comes back to it.
    if (!set.sizes.empty()) {
        const auto& want = set.sizes[size_t(std::clamp(set.size, 0, int(set.sizes.size()) - 1))];
        if (!set.fullscreen) {
            args.width = want.first;
            args.height = want.second;
        }
    }
    core::saveOptions(args);
}

}  // namespace mu::app
