#include "app/modes/lobby_mode.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>

#include "app/options.h"
#include "app/preloader.h"
#include "core/files.h"
#include "core/log.h"
#include "game/ui/controls.h"

namespace mu::app {

namespace {

// Where the tables come from for dressing the pedestals: an item's (group, number) is the same
// row in every world, and Lorencia is the world every character stands in until sprint 12.
constexpr const char* kTablesWorld = "lorencia";

// The first tile the screen's world is raised at: the middle of world 74's walkable patch, which
// is where the pedestals stand. Nothing here walks; the grass and the town cull round it.
constexpr float kFocusColumn = 80.0f, kFocusRow = 193.0f;

// The sky over world 74: black, as MuMain clears it (CharacterScene.cpp:251), with stars in it
// -- the user, 2026-09-27: "keep sky black and add some stars". Points on a plane far behind
// the set, scattered by a fixed hash so they hold still from frame to frame and allocate
// nothing, each twinkling slowly on its own phase. Depth-tested like every sprite, so they show
// only where nothing of the set stands in front of them. Invention.
void gatherStars(const gfx::Camera& eye, bgfx::TextureHandle sheet, float seconds,
                 gfx::Effects& effects) {
    if (!bgfx::isValid(sheet)) return;
    constexpr int kStars = 900;
    // At kDistance the frame sees about 44 metres below the eye to 21 above (30.75 degrees,
    // tipped 5.5 down) and some 60 either side on a 16:9 window.
    constexpr float kDistance = 120.0f, kHalfWide = 90.0f, kLow = -4.0f, kHigh = 24.0f;
    float ahead[3] = {eye.target[0] - eye.position[0], 0.0f, eye.target[2] - eye.position[2]};
    const float length = std::sqrt(ahead[0] * ahead[0] + ahead[2] * ahead[2]);
    if (length < 1e-4f) return;
    ahead[0] /= length;
    ahead[2] /= length;
    const float across[3] = {-ahead[2], 0.0f, ahead[0]};
    const auto hash = [](uint32_t n) {
        n = (n ^ 61u) ^ (n >> 16);
        n *= 9u;
        n ^= n >> 4;
        n *= 0x27d4eb2du;
        n ^= n >> 15;
        return float(n & 0xFFFFFFu) / float(0x1000000);
    };
    for (int i = 0; i < kStars; ++i) {
        const uint32_t seed = uint32_t(i) * 4u;
        const float u = hash(seed) * 2.0f - 1.0f;
        // More of them higher up, as the air thins toward the zenith.
        const float v = std::sqrt(hash(seed + 1));
        const float big = hash(seed + 2);
        const float phase = hash(seed + 3) * 6.2831853f;
        const float depth = kDistance + big * 20.0f;
        gfx::Sprite star;
        for (int a = 0; a < 3; ++a) {
            star.position[a] = eye.position[a] + ahead[a] * depth + across[a] * u * kHalfWide;
        }
        star.position[1] = eye.position[1] + kLow + v * (kHigh - kLow);
        // Small and subtle and many, the user's words: faint pinpricks about a pixel across, and
        // one in twelve a little brighter.
        const bool bright = big > 0.92f;
        const float size = bright ? 0.045f : 0.024f + big * 0.012f;
        star.halfWidth = star.halfHeight = size;
        const float twinkle = 0.75f + 0.25f * std::sin(seconds * (0.6f + big * 1.4f) + phase);
        const float level = (bright ? 1.2f : 0.45f + big * 0.45f) * twinkle;
        // A little cooler or warmer each.
        const float tint = hash(seed + 5);
        star.colour[0] = level * (0.85f + 0.15f * tint);
        star.colour[1] = level * 0.92f;
        star.colour[2] = level * (1.0f - 0.12f * tint);
        star.colour[3] = 1.0f;
        star.sheet = sheet;
        star.blend = gfx::Blend::Additive;
        effects.add(star);
    }
}

// Mist behind the pedestals: a few large, faint puffs of a generated smoke sheet drifting slowly along a
// band a few metres behind the row, each turning a little and breathing in and out. The user,
// 2026-09-27: "fog smoke has to be behind the character with visible smokes but not too visible".
// Depth-tested, so a figure stands in front of it. Invention, judged by eye.
void gatherMist(const content::Ground& ground, bgfx::TextureHandle sheet, float seconds,
                gfx::Effects& effects) {
    if (!bgfx::isValid(sheet)) return;
    constexpr int kPuffs = 14;
    // In tiles: the pedestals stand at x 80 to 83 and y 189 to 198, and the camera looks at them
    // from x 97, so behind them is a band at lower x, longer than the row: back against the
    // buildings (the user: "closer to buildings"), and hung high enough to clear the steps that
    // rise between, which hid it when it lay lower.
    constexpr float kNearX = 75.5f, kFarX = 70.0f, kFromY = 181.0f, kToY = 206.0f;
    const float mpt = ground.metresPerTile();
    for (int i = 0; i < kPuffs; ++i) {
        const float a = float(i) / float(kPuffs);
        const float lane = std::fmod(a * 7.31f, 1.0f);             // depth into the band
        const float drift = std::fmod(a + seconds * 0.006f, 1.0f); // along it, wrapping
        const float column = kNearX + (kFarX - kNearX) * lane;
        const float row = kFromY + (kToY - kFromY) * drift;
        gfx::Sprite puff;
        puff.position[0] = column * mpt;
        puff.position[2] = -row * mpt;
        puff.position[1] = ground.heightAt(puff.position[0], puff.position[2]) + 2.2f +
                           1.8f * std::fmod(a * 3.7f, 1.0f);
        puff.halfWidth = puff.halfHeight = 3.4f + 2.0f * std::fmod(a * 5.3f, 1.0f);
        puff.spin = seconds * 0.04f * (i % 2 ? 1.0f : -1.0f) + a * 6.2831853f;
        // In and out at the band's two ends, so a puff wrapping round is not seen to jump.
        const float ends = std::min(1.0f, std::min(drift, 1.0f - drift) * 6.0f);
        const float breath = 0.8f + 0.2f * std::sin(seconds * 0.35f + a * 11.0f);
        // Dim and faint, the user: "mist is too much visible and too light".
        puff.colour[0] = 0.26f;
        puff.colour[1] = 0.28f;
        puff.colour[2] = 0.32f;
        puff.colour[3] = 0.045f * ends * breath;
        puff.sheet = sheet;
        puff.blend = gfx::Blend::Alpha;
        effects.add(puff);
    }
}

}  // namespace

void LobbyMode::reread() {
    roster_ = game::readRoster(folder_);
    pedestals_.raise(roster_);
}

bool LobbyMode::open(Context& ctx) {
    core::Args& args = ctx.args;
    const std::string& assets = ctx.paths.assets;
    folder_ = args.rosterPath.empty() ? game::rosterFolder() : args.rosterPath;
    entering_ = quitting_ = false;

    world_.setFocusTile(kFocusColumn, kFocusRow);
    const bool up = Preloader::run(ctx, [&]() {
        bool ok = world_.open(assets, game::Pedestals::kMap, ctx.textures, 0, true);
        if (!ok) return false;
        // The crowd would stand a knight of its own at the focus; the pedestals are the people.
        world_.crowd().shutdown();
        const float reach = float(world_.ground().size()) * world_.ground().metresPerTile();
        ctx.renderer.setMapEdge(reach, reach, 8.0f);
        std::string error;
        const std::string tablesPath = core::join(
            assets, std::string("cooked/") + kTablesWorld + "/" + kTablesWorld + ".mur");
        if (!content::loadTables(tablesPath, tables_, error)) {
            // Not fatal: the figures stand in their bare class bodies.
            core::logError("lobby: no item tables at %s (%s); the pedestals stand undressed",
                           tablesPath.c_str(), error.c_str());
        }
        pedestals_.open(&world_.figures(), &world_.ground(), &tables_, assets, ctx.textures);
        bust_.open(&world_.figures());
        // A star is Impack03, the soft spark sheet the pick's own motes wear, drawn small.
        const std::string starSheet = core::join(assets, "effects/lobby/impack03.png");
        if (core::fileExists(starSheet)) {
            stars_ = ctx.textures.load(starSheet, content::TextureRole::Albedo);
        }
        // The mist's own sheet, generated (pipeline/mist_sheet.py): MU's smoke is 64 pixels
        // across and broke into blocks drawn this wide.
        const std::string mistSheet = core::join(assets, "effects/lobby/mist.png");
        if (core::fileExists(mistSheet)) {
            mist_ = ctx.textures.load(mistSheet, content::TextureRole::Albedo);
        }
        // The stage programs the bust is photographed with; the game's desk opens them for
        // itself, and there is no desk here.
        if (!ctx.renderer.openStages(ctx.paths.shaders)) {
            core::logError("lobby: the stage programs did not open; the bust will not draw");
        }
        roster_ = game::readRoster(folder_);
        pedestals_.raise(roster_);

        if (interface_.init(ctx.paths.shaders)) {
            interfaceUp_ = true;
            arts_.open(assets, &ctx.textures);
            game::panel::openTitleFace(interface_);
            cursor_.open(interface_, &arts_);
            lobby_.open(interface_);
            menu_.open(interface_);
            // The Sanctuary controls, which the screen's buttons and the menu are drawn with.
            game::controls::open();
        } else {
            core::logError("lobby: the interface did not open");
        }
        // The clicks: SOUND_CLICK01 on every button and on Enter and Escape, as MU plays it.
        if (content::loadShowing(core::join(assets, "cooked/showing/showing.mus"), showing_,
                                 error) &&
            sound_.open(assets, showing_, args.mute)) {
            click_ = sound_.load("window_click", false);
            refused_ = sound_.load("window_refused", false);
            // MU's main theme. MuMain plays login_theme.mp3 here, from the login screen through
            // this one until loading (LoginScene.cpp:384, LoadingScene.cpp:84); the user,
            // 2026-09-27, chose main_theme.mp3 instead. Ours.
            const std::string theme = core::join(assets, "music/main_theme.mp3");
            if (core::fileExists(theme)) sound_.music(theme);
        }
        return interfaceUp_;
    }, &quitEarly_);
    if (!up) {
        core::logError("the character screen did not open");
        world_.shutdown();
        return false;
    }
    pedestals_.aim(camera_);
    // World 74's torches, as lights on the set: the lamps' static grid, once (see PlayMode).
    if (args.lampsOn) world_.lamps().light(ctx.renderer);
    // The scene's own air: world 74 is drawn from much further off than the town, and the night
    // sheet's dust, tuned for Lorencia's street, fogs the gate behind the pedestals.
    ctx.time.setScene(core::join(ctx.paths.sheets, "lobby.json"));

    // The sheet's options, filled from the window as the game's are (app/options.h); Switch
    // Character has nowhere to go from here.
    fillSettings(ctx.window, args.fps, &menu_.settings());
    menu_.allowSwitch(false);
    // Escape is the screen's: it shuts a window, then raises the menu, whose Exit quits. A
    // --frames review keeps it as the quit it always was.
    ctx.window.holdEscape(args.frames == 0);

    // The stills' own states.
    if (args.lobbyPick >= 0) pedestals_.pick(args.lobbyPick);
    if (args.lobbyCreate >= 0) lobby_.openCreate(args.lobbyCreate);
    if (!args.lobbyName.empty()) lobby_.setTyped(args.lobbyName);
    if (args.lobbyDelete && pedestals_.picked() >= 0) lobby_.askDelete();
    // With nobody on the account the create window opens by itself (CCharSelMainWin).
    if (roster_.empty()) lobby_.openCreate(1);

    fading_ = args.frames == 0 || args.entrance;
    fadeSeconds_ = 0.0f;
    core::logf("lobby: %zu character(s) on world 74; %s", roster_.size(),
               roster_.empty() ? "the create window is up" : "pick one and enter");
    return true;
}

void LobbyMode::enter(Context& ctx, int slot) {
    const game::Seat* who = nullptr;
    for (const game::Seat& one : roster_) {
        if (one.slot == slot) who = &one;
    }
    if (!who) return;
    core::Args& args = ctx.args;
    // StartGame(): the pick's save, its world and its class go to the world as the run's own
    // arguments. PlayMode reads the save and makes a fresh one as a new character is made.
    args.savePath = who->path;
    args.world = who->world.empty() ? std::string("lorencia") : who->world;
    args.play = true;
    args.fresh = false;
    args.atSet = false;
    args.kin = int(who->kin);
    args.level = who->level;
    args.weapon.clear();
    args.shield.clear();
    entering_ = true;
    core::logf("lobby: entering %s as %s, level %d %s", args.world.c_str(), who->name.c_str(),
               who->level, game::className(who->kin));
}

void LobbyMode::frame(Context& ctx, const Frame& at) {
    core::Args& args = ctx.args;
    const float seconds = float(at.deltaSeconds);
    const float w = float(ctx.window.width()), h = float(ctx.window.height());

    float view[16], proj[16], viewProj[16];
    ctx.renderer.cameraMatrices(camera_, view, proj);
    bx::mtxMul(viewProj, view, proj);

    float px = 0.0f, py = 0.0f;
    ctx.window.pointer(&px, &py);
    if (args.pointX >= 0.0f) {
        px = args.pointX * w;
        py = args.pointY * h;
    }
    game::Pointer pointer;
    pointer.x = px;
    pointer.y = py;
    pointer.pressed = ctx.window.clicked(0);
    pointer.released = ctx.window.released(0);
    pointer.held = ctx.window.held(0);
    // --ui-click on this screen too, so its buttons and its menu can be reviewed without a
    // hand: pressed and let go on the frame asked for, where it is asked.
    for (const core::Args::UiClick& c : args.uiClicks) {
        if (at.index != c.frame || c.right) continue;
        pointer.x = px = c.x * w;
        pointer.y = py = c.y * h;
        pointer.pressed = pointer.released = pointer.held = true;
    }
    game::panel::setScreen(h);

    const auto play = [&](int handle) {
        if (handle >= 0) sound_.play(handle);
    };

    // The menu over everything, and modal while it is up.
    const bool menuWasUp = menu_.up();
    if (menuWasUp) {
        game::Menu::Result asked;
        menu_.update(seconds, w, h, pointer, ctx.window.escaped(), "Character Select", &asked);
        if (asked.clicked) play(click_);
        if (asked.quit) quitting_ = true;
        if (asked.settings) {
            const game::Menu::Settings& set = menu_.settings();
            applySettings(ctx.window, set);
            args.fps = set.fps;
            sound_.setVolume(float(set.volume) / 100.0f);
        }
    } else {
        menu_.update(seconds, w, h, game::Pointer{}, false, "Character Select", nullptr);
    }

    // The screen, when the menu is not in front of it.
    game::Lobby::View look;
    look.roster = &roster_;
    look.picked = pedestals_.picked();
    const bool figuresHear = !menuWasUp && !lobby_.takesPointer() && !lobby_.creating();
    look.hovered = figuresHear ? pedestals_.hover(viewProj, w, h, px, py) : -1;
    for (int slot = 0; slot < game::kRosterSlots; ++slot) {
        look.plateSeen[slot] =
            pedestals_.crown(slot, viewProj, w, h, &look.plate[slot][0], &look.plate[slot][1]);
    }
    game::Lobby::Result asked;
    if (!menuWasUp) {
        lobby_.update(seconds, w, h, pointer, ctx.window.typed(), ctx.window.backspaces(),
                      ctx.window.entered(), ctx.window.escaped(), look, &asked);
    } else {
        lobby_.update(seconds, w, h, game::Pointer{}, "", 0, false, false, look, nullptr);
    }
    ctx.window.setTyping(lobby_.typing());
    if (asked.clicked) play(click_);
    if (asked.refused) play(refused_);
    if (asked.pick != -2) pedestals_.pick(asked.pick);
    if (asked.menu) menu_.show();
    if (asked.create) {
        // The roster's refusals, which the client could not see: the name taken, no room.
        switch (game::refusalOf(folder_, roster_, asked.name)) {
            case game::Refusal::None:
                if (game::makeCharacter(folder_, roster_, asked.name, asked.kin)) {
                    reread();
                    for (const game::Seat& one : roster_) {
                        if (one.name == asked.name) pedestals_.pick(one.slot);
                    }
                } else {
                    lobby_.notice("The character could not be written.", true);
                    play(refused_);
                }
                break;
            case game::Refusal::NoRoom:
                lobby_.notice("No more characters can be created.");
                play(refused_);
                break;
            case game::Refusal::TooShort:
                lobby_.notice("Type more than 4 letters", true);
                play(refused_);
                break;
            case game::Refusal::Symbols:
                lobby_.notice("Cannot use symbols.", true);
                play(refused_);
                break;
            case game::Refusal::Taken:
                lobby_.notice(
                    "Incorrect character name was entered or same character name exists.", true);
                play(refused_);
                break;
        }
    }
    if (asked.drop >= 0) {
        for (const game::Seat& one : roster_) {
            if (one.slot != asked.drop) continue;
            if (game::dropCharacter(folder_, one)) {
                pedestals_.pick(-1);
                reread();
                lobby_.notice("Character was deleted successfully.");
            } else {
                lobby_.notice("The character could not be deleted.");
                play(refused_);
            }
            break;
        }
    }
    if (asked.enter >= 0) enter(ctx, asked.enter);
    if (args.lobbyEnter >= 0 && at.index == args.lobbyEnter && pedestals_.picked() >= 0) {
        enter(ctx, pedestals_.picked());
    }

    // The create window's bust: its class, photographed into the window's corner before the
    // scene is drawn, since the scene's draw is what uploads the palette its pose goes into.
    if (lobby_.creating()) {
        bust_.show(lobby_.chosen());
        const gfx::Box corner = game::Lobby::bustBox(w, h);
        bust_.render(ctx.renderer, seconds, int(corner.w), int(corner.h));
    } else {
        bust_.hide();
    }
    lobby_.setBust(bust_.picture());

    pedestals_.update(seconds);

    // ---- the scene -------------------------------------------------------------------------
    {
        gfx::PointLight lit[gfx::Renderer::kMaxTransientLights];
        const uint32_t count = pedestals_.lights(lit, gfx::Renderer::kMaxTransientLights);
        ctx.renderer.setTransientLights(lit, count);
    }
    if (args.lampsOn) {
        world_.lamps().update(seconds, world_.town(), ctx.renderer, camera_.target);
        world_.lamps().gather(ctx.renderer.effects(), camera_.target, daylightOf(ctx.lighting));
    }
    world_.sway().update(seconds, viewProj, ctx.renderer, world_.town());
    world_.ornaments().update(seconds, world_.sway());
    world_.ornaments().gather(ctx.renderer.effects(), world_.sway());
    world_.shades().gather(ctx.renderer.effects(), camera_.target);
    pedestals_.gatherEffects(ctx.renderer.effects());
    gatherStars(camera_, stars_, float(at.elapsed), ctx.renderer.effects());
    gatherMist(world_.ground(), mist_, float(at.elapsed), ctx.renderer.effects());

    drawables_.clear();
    casters_.clear();
    if (world_.town().isOpen()) {
        world_.town().gatherVisible(viewProj, drawables_);
        world_.town().gatherAll(casters_, true);
    }
    pedestals_.gather(ctx.renderer, drawables_, &casters_);
    gfx::GrassField grassField;
    const bool grassDrawn =
        world_.grass().gather(world_.ground(), ctx.lighting, viewProj, camera_.position, nullptr,
                              0, float(at.elapsed), grassField);
    ctx.renderer.draw(camera_, ctx.lighting, drawables_, &world_.ground(), &casters_,
                      grassDrawn ? &grassField : nullptr);

    // ---- the screen over it ----------------------------------------------------------------
    if (interfaceUp_) {
        cursor_.update(seconds, px, py, false, false, false);
        interface_.begin(ctx.window.width(), ctx.window.height());
        interface_.add(lobby_.canvas());
        if (menu_.up()) interface_.add(menu_.canvas());
        interface_.add(cursor_.canvas());
        interface_.submit(gfx::ViewHud);
    }
    if (args.fps) ctx.readout.draw(ctx.overlay, ctx.window.width(), ctx.window.height());
    // Up from black, as the game's own entrance comes up.
    if (fading_ && ctx.curtain.ready()) {
        if (at.index >= 2) fadeSeconds_ += seconds;
        const float t = std::min(1.0f, fadeSeconds_ / 0.35f);
        const float black = 1.0f - t * t * (3.0f - 2.0f * t);
        if (black > 0.0f) {
            ctx.curtain.begin(ctx.window.width(), ctx.window.height());
            ctx.curtain.panel(0.0f, 0.0f, w, h, uint32_t(black * 255.0f) << 24);
            ctx.curtain.submit(gfx::ViewHud);
        }
    }
}

void LobbyMode::report(Context& ctx) {
    core::logf("  lobby: %zu character(s), picked %d, %s%s; %llu screen rebuilds",
               roster_.size(), pedestals_.picked(), lobby_.creating() ? "creating" : "choosing",
               menu_.up() ? ", menu up" : "", (unsigned long long)lobby_.rebuilds());
}

void LobbyMode::shutdown(Context& ctx) {
    ctx.time.setScene("");
    bust_.shutdown();
    ctx.renderer.closeStages();
    sound_.shutdown();
    pedestals_.shutdown();
    if (interfaceUp_) {
        game::controls::close();
        game::panel::closeTitleFace();
        interface_.shutdown();
        interfaceUp_ = false;
    }
    world_.shutdown();
}

}  // namespace mu::app
