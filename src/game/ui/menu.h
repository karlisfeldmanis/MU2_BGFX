// The game menu, on Escape: What's New, Options, Switch Character, Discord, Exit Game.
//
// MU's CNewUISystemMenu is a column of worded buttons over the world -- Exit Game, Server
// Select, Character Select, Option, Close -- and this is that column in the windows' skin,
// chosen by the user on 2026-09-27 from a page drawn over Lorencia: *"same style as other ui,
// clean, fat but with diablo 4 vibe"*, then *"cleaner with rounder radius"*, then *"a little
// bit simpler, without icons"*. A centred sheet, fat rounded slabs with an engraved
// inner line, a warm light rising under the one the pointer is on, and Exit set apart below a
// rule and lit red. Discord opens the game's server in the player's browser (core/browser.h;
// the user, 2026-10-20: *"we need to add info about our discord on website and also inside game
// itself"*).
//
// Three pages in one sheet: the menu; Options, which offers only what can change while the
// game runs -- the display (a window or the whole screen), the window's size, v-sync, the
// volume and the frame-rate counter; and Graphics, behind a button on Options, with a quality
// preset over the rows it sets -- render scale, anti-aliasing, shadows, ambient occlusion,
// bloom, reflections, grass, point lights, texture size -- and the frame-rate limit, edited
// and then applied with Apply, as a modern game's has them (the
// user, 2026-10-02: *"add more options ... so user can adjust graphics settings based on his
// machine capabilities"*, *"similar like it is on any modern game"*). Switch Character goes back to the
// character screen (game/ui/lobby.h) when the run came through one, and stands there inactive
// when it did not -- a --frames review or a bench-made hero has no screen to go back to.
//
// Modal, as the number box is: while it is up the desk gives it the pointer and every key. The
// world is NOT held -- the user, 2026-09-27: *"dont freeze character when open menu"* -- so a
// fight goes on behind it, as it does behind Diablo's.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Menu {
public:
    // Lost: the server's line is gone (Play::cut) -- Disconnected, Reconnect and Exit Game, with
    // no close and no Escape out of it.
    enum class Page : uint8_t { Main, Options, Graphics, Lost };
    struct Result {
        bool closed = false;    // Resume: the menu went down this frame
        bool quit = false;      // Exit Game, which is instant: the user, 2026-09-27
        bool clicked = false;   // a button answered, for the interface's click
        bool settings = false;  // something on the Options page changed
        bool switched = false;  // Switch Character: back to the character screen
        bool reconnect = false; // the Lost page's Reconnect: the character screen, which joins afresh
        bool whatsNew = false;  // What's New: its slides up again (game/ui/whats_new.h)
        bool discord = false;   // Discord: the invite went to the browser, the menu down
    };

    // What Options edits. The caller fills it from the window and the run, and applies it back
    // when `Result::settings` says so, and mu play's runs keep it (app/options.h).
    struct Settings {
        bool fullscreen = false;
        // The window's size, as an index into `sizes`, which are in screen points. Fullscreen
        // is the display's own mode, so the row shows `display` then and does not step.
        std::vector<std::pair<int, int>> sizes;
        int size = 0;
        std::pair<int, int> display{0, 0};
        bool vsync = false;
        int volume = 100;  // percent
        bool fps = true;
        // The Graphics page's, applied every frame by app/options.h: the world's scale in
        // percent (MetalFX under 91), MSAA samples, shadows -1 none and 0..2, the three
        // effects, grass 0 none 1 thinned 2 full, the point lights, the textures 0 half 1 full,
        // and the frame-rate limit (0 none). `texturesLoaded` is what this run loaded, which a
        // changed Textures row waits on a restart to become.
        int scale = 100;
        int msaa = 4;
        int shadows = 2;
        bool ssao = true;
        bool bloom = true;
        bool reflections = true;
        int grass = 2;
        bool lights = true;
        int textures = 1;
        int texturesLoaded = 1;
        // core::Args::renderer: 0 Auto, 1 Vulkan, 2 Direct3D 11, and what this run started with.
        // Read when the window opens, so a change waits on a restart as the textures do.
        int renderer = 0;
        int rendererLoaded = 0;
        int cap = 0;
        bool operator==(const Settings& o) const {
            return fullscreen == o.fullscreen && sizes == o.sizes && size == o.size &&
                   display == o.display && vsync == o.vsync && volume == o.volume && fps == o.fps &&
                   scale == o.scale && msaa == o.msaa && shadows == o.shadows && ssao == o.ssao &&
                   bloom == o.bloom && reflections == o.reflections && grass == o.grass &&
                   lights == o.lights && textures == o.textures &&
                   texturesLoaded == o.texturesLoaded && renderer == o.renderer &&
                   rendererLoaded == o.rendererLoaded && cap == o.cap;
        }
    };

    void open(const gfx::Interface& interface);

    // Up on `page`: Main from Escape and the HUD's button, any page from a review's --windows.
    void show(Page page = Page::Main);
    // Up on the Lost page, its first line `why` (Link::cutWhy); empty says the line was lost.
    void lose(std::string why) {
        lostWhy_ = std::move(why);
        show(Page::Lost);
    }
    // Its second line, under `why`: how the reconnect goes. Empty says he is kept on the server.
    void setLostNote(std::string note) { lostNote_ = std::move(note); }
    // Whether Switch Character answers. Only a run that came through the character screen
    // (--lobby) has one to go back to; everywhere else the button stands there inactive.
    void allowSwitch(bool on) { switchable_ = on; }
    void hide() { up_ = false; }
    bool up() const { return up_; }
    bool lost() const { return up_ && page_ == Page::Lost; }

    Settings& settings() { return settings_; }
    const Settings& settings() const { return settings_; }

    // `escape` backs out a page, and off the menu shuts it. `place` is the foot's right-hand
    // line: where he is standing.
    void update(float seconds, float width, float height, const Pointer& pointer, bool escape,
                const std::string& place, Result* out);

    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    int hitAt(float x, float y) const;
    void rebuild();

    gfx::Canvas canvas_;
    bool up_ = false;
    bool switchable_ = false;
    Page page_ = Page::Main;
    Settings settings_;
    // The Graphics page's rows as chosen and not yet applied: taken from `settings_` on the
    // page's first change (`editing_`), so a caller that fills the settings after raising the
    // page is still what it opens on; given back by Apply, and dropped by leaving the page. The
    // user, 2026-10-15: "improve UI for graphics settings with apply button, because now we
    // have textures setting which asks for restart".
    Settings pending_;
    bool editing_ = false;
    const Settings& graphicsShown() const { return editing_ ? pending_ : settings_; }
    std::string place_;
    std::string lostWhy_, lostNote_;
    float width_ = 0.0f, height_ = 0.0f;
    float x_ = 0.0f, y_ = 0.0f;  // the sheet's corner, in pixels
    int over_ = -1, pressing_ = -1;
    static constexpr int kTargets = 48;
    float lift_[kTargets] = {};
    struct Drawn {
        bool up = false;
        bool switchable = false;
        Page page = Page::Main;
        Settings settings;
        Settings pending;
        std::string place;
        float width = 0, height = 0;
        int over = -1, pressing = -1;
        float lift[kTargets] = {};
        bool operator==(const Drawn& o) const {
            if (up != o.up || switchable != o.switchable || page != o.page || !(settings == o.settings) ||
                !(pending == o.pending) ||
                place != o.place || width != o.width || height != o.height || over != o.over ||
                pressing != o.pressing) {
                return false;
            }
            for (int i = 0; i < kTargets; ++i) {
                if (lift[i] != o.lift[i]) return false;
            }
            return true;
        }
    };
    Drawn drawn_, now_;
    bool built_ = false;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
