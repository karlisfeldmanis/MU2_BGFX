// The character screen: MU's CHARACTER_SCENE, raised by `--lobby` and handed to by the world's
// Switch Character.
//
// World 74 stands behind it -- MU's own set for this scene, cooked here as `charscene` -- with
// the account's characters on its five pedestals (game/pedestals.h) and the screen over them
// (game/ui/lobby.h). A character picked and entered hands the run to PlayMode on his save; the
// menu's Switch Character hands it back here. MuMain's StartGame() copies the pick into
// CharacterAttribute and goes to LOADING_SCENE, and this is that: the arguments are the
// attribute, and the preloader is the loading scene.
//
// It owns what lives exactly as long as the screen: the world, the roster, the pedestals, the
// interface and its menu, and a sound for the clicks.
#pragma once

#include <string>
#include <vector>

#include "app/mode.h"
#include "content/showing.h"
#include "content/tables.h"
#include "game/bust.h"
#include "game/pedestals.h"
#include "game/roster.h"
#include "game/sound.h"
#include "game/ui/cursor.h"
#include "game/ui/lobby.h"
#include "game/ui/menu.h"
#include "game/ui/panel.h"
#include "game/world/world.h"

namespace mu::app {

class LobbyMode : public Mode {
public:
    bool open(Context& ctx) override;
    bool quitEarly() const override { return quitEarly_; }
    bool quitting() const override { return quitting_; }
    Next next() const override { return entering_ ? Next::Play : Next::None; }
    void frame(Context& ctx, const Frame& at) override;
    const gfx::Camera& camera() const override { return camera_; }
    void report(Context& ctx) override;
    void shutdown(Context& ctx) override;

private:
    // Reads the folder again and stands whoever is in it. After every creation and deletion,
    // so what is drawn is always what is on disk.
    void reread();
    void enter(Context& ctx, int slot);

    game::World world_;
    content::Tables tables_;
    game::Pedestals pedestals_;
    std::string folder_;
    std::vector<game::Seat> roster_;

    gfx::Interface interface_;
    game::panel::Arts arts_;
    game::Cursor cursor_;
    game::Lobby lobby_;
    game::Menu menu_;
    bool interfaceUp_ = false;

    content::Showing showing_;
    game::Sound sound_;
    int click_ = -1, refused_ = -1;

    gfx::Camera camera_;
    std::vector<gfx::Drawable> drawables_;
    std::vector<gfx::Drawable> casters_;

    // The create window's bust (game/bust.h).
    game::Bust bust_;
    bgfx::TextureHandle stars_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle mist_ = BGFX_INVALID_HANDLE;

    float fadeSeconds_ = 0.0f;
    bool fading_ = false;
    bool quitEarly_ = false;
    bool quitting_ = false;
    bool entering_ = false;
};

}  // namespace mu::app
