#include "app/modes/play_mode.h"

#include "net/wire.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <unordered_map>

#include "app/options.h"
#include "app/preloader.h"
#include "core/files.h"
#include "core/loading.h"
#include "core/log.h"
#include "game/roster.h"
#include "game/shine.h"
#include "game/wings.h"
#include "game/world/maps.h"
#include "gfx/views.h"
#include "sim/gates.h"
#include "sim/items.h"
#include "sim/market.h"
#include "sim/raid_party.h"
#include "sim/travel.h"

namespace mu::app {
namespace {
// **A town's theme rests between hearings** (the user, 2026-10-05: 'we need some timeout rule
// for all safezone musics so its not played to often which is anoying'): each plays once
// through as he comes into its place, and not again until this long after it last began,
// however often he steps in and out. Ours: MU loops them for as long as he stands there.
constexpr double kMusicRestSeconds = 10.0 * 60.0;
// When each track last began, in seconds of the run, across worlds and modes.
// The raid's party (--raid), read with the save and handed to the play before it opens.
std::vector<mu::sim::RaiderKit>& raidParty() {
    static std::vector<mu::sim::RaiderKit> party;
    return party;
}

std::unordered_map<std::string, double>& musicBegan() {
    static std::unordered_map<std::string, double> began;
    return began;
}
double runSeconds() {
    return double(bx::getHPCounter()) / double(bx::getHPFrequency());
}
}  // namespace

void PlayMode::readSave(Context& ctx) {
    core::Args& args = ctx.args;
    if (!args.play) return;
    // An arena has no save of its own and must never touch the player's: it is a level-80
    // hero standing in a field of one breed, and writing that over the character somebody
    // is playing would be the worst kind of helpful. Nor a --quest-ready demo, whose quest was
    // never walked. A named `--save` is still obeyed, because then the caller asked for a file
    // by name.
    const bool unsaved =
        args.frames != 0 || !args.arena.empty() || args.questReady || !args.questsDone.empty() ||
        args.raid > 0;
    savePath_ = !args.savePath.empty() ? args.savePath
                : !unsaved             ? game::defaultSavePath()
                                       : std::string();
    if (!savePath_.empty() && !args.fresh && game::loadSave(savePath_, saved_)) {
        if (saved_.fresh) {
            // Made on the character screen and not yet played: his class is all there is, and
            // he is made as a new character is -- level one, the class's own weapon, at the
            // town's gate -- into this file, which keeps his name and slot.
            args.kin = int(saved_.hero.kin);
            args.level = 1;
            args.weapon = game::cradleWeapon(saved_.hero.kin);
            args.shield.clear();
        } else if (saved_.world == args.world) {
            resumed_ = true;
            args.kin = int(saved_.hero.kin);
            args.level = saved_.hero.level;
            args.weapon.clear();
            args.shield.clear();
            // A way back he quit with is his again, with the seconds it had (the user,
            // 2026-10-03: "after restart there is option to Go Back!"). One already in hand is
            // this session's own, carried across a map change, and the newer. One saved before
            // Go Back! was kept to the dungeons of floors is let go.
            // Into the record the realm restores (sim::WayBack): its clock is the realm's.
            const game::MapRow* backTo = game::mapOf(saved_.goBackWorld);
            if (!saved_.goBackWorld.empty() && saved_.goBackLeft > 0.0 && backTo != nullptr &&
                backTo->floors) {
                saved_.hero.wayBack = {int32_t(backTo->number), saved_.goBackColumn, saved_.goBackRow,
                                       saved_.goBackFacing,
                                       std::min(int64_t(saved_.goBackLeft * 20.0), sim::kGoBackTicks), 0};
                core::logf("go back: kept, %.0f s left, to %s %d,%d", saved_.goBackLeft,
                           saved_.goBackWorld.c_str(), saved_.goBackColumn, saved_.goBackRow);
            }
        } else {
            core::logError("save: the hero is in %s and this run is %s; starting new here",
                           saved_.world.c_str(), args.world.c_str());
        }
    }
    if (!savePath_.empty()) {
        core::logf("save: %s %s", resumed_ ? "resuming from" : "a new character, saving to",
                   savePath_.c_str());
        // The account's, so a new character opens it too -- and `--fresh` as well, which is a
        // new character and not a new account.
        game::loadVault(game::vaultPathBeside(savePath_), saved_);
    }
    // --raid: the party's first kit is who he is -- its class and level, its gear laid on him by
    // the realm (sim::Realm::setRaid) -- whatever a save said (docs/golden-dragon-raid.md).
    if (args.raid > 0) {
        resumed_ = false;
        const std::string party = ctx.paths.assets + "/../source/raid/party.json";
        if (sim::readParty(party, &raidParty()) && !raidParty().empty()) {
            args.kin = int(raidParty()[0].kin);
            args.level = raidParty()[0].level;
            args.weapon.clear();
            args.shield.clear();
            core::logf("raid: %zu in the party, tough for %d, landing %c", raidParty().size(), args.raid,
                       args.raidBox ? args.raidBox : '?');
        } else {
            core::logError("--raid: %s did not read; no raid", party.c_str());
            args.raid = 0;
        }
    }
}

void PlayMode::keep(Context& ctx) {
    if (savePath_.empty() || !world_.played().isOpen()) return;
    // On a server the character is the server's (docs/sprints/18-the-wire.md): nothing played
    // there is written over the characters kept here -- but his windows' layout is the client's
    // own, kept beside the save (game::writeLayout).
    if (world_.played().remote()) {
        int32_t quick[5], bar[6];
        for (int key = 0; key < 5; ++key) quick[key] = desk_.quick(key);
        for (int key = 0; key < 6; ++key) bar[key] = desk_.bound(key);
        game::writeLayout(savePath_, *world_.played().realm().tables(), quick, bar, desk_.followedQuest());
        return;
    }
    game::Saved now;
    now.name = saved_.name;
    now.slot = saved_.slot;
    now.world = ctx.args.world;
    now.hero = world_.played().record();
    now.machine = world_.played().realm().machine();
    // On the way to another world the arguments already name it, so he is written standing
    // where he comes in: the next world resumes him there, and a quit mid-load finds him there.
    if (!travelTo_.empty()) {
        now.hero.column = arriveColumn_;
        now.hero.row = arriveRow_;
        if (arriveFaced_) now.hero.facing = arriveFacing_;
        // And without her summon: a map change dismisses it (realm_summon.cpp).
        now.hero.summonSkill = 0;
    }
    // Inside an event he is written in its town, at the spawn gate: a quit or a crash in Blood
    // Castle comes back in Devias (WebZen user.cpp:3147-3150), the run lost.
    const game::MapRow* map = game::mapOf(now.world);
    const game::MapRow* home = map && map->event ? game::mapNumbered(map->home) : nullptr;
    if (travelTo_.empty() && home != nullptr) {
        now.world = home->world;
        now.hero.column = home->arrive[0];
        now.hero.row = home->arrive[1];
        now.hero.summonSkill = 0;
    }
    for (int key = 0; key < 5; ++key) now.quick[key] = desk_.quick(key);
    for (int key = 0; key < 6; ++key) now.bar[key] = desk_.bound(key);
    now.followed = desk_.followedQuest();
    if (const sim::WayBack& way = now.hero.wayBack; way.open()) {
        if (const game::MapRow* to = game::mapNumbered(way.map)) {
            now.goBackWorld = to->world;
            now.goBackColumn = way.column;
            now.goBackRow = way.row;
            now.goBackFacing = way.facing;
            now.goBackLeft = double(way.ticksLeft) / 20.0;
        }
    }
    game::writeSave(savePath_, *world_.played().realm().tables(), now);
    game::writeVault(game::vaultPathBeside(savePath_), *world_.played().realm().tables(),
                     world_.played().realm().vault());
}

void PlayMode::openItems(Context& ctx) {
    if (itemModels_.tables()) return;
    itemModels_.open(world_.played().realm().tables(), ctx.paths.assets, &ctx.textures);
    // The quest items' column (fx/litter.h): MU's chasellight, the lobby's own copy.
    const std::string beam = core::join(ctx.paths.assets, "effects/lobby/chasellight.png");
    litter_.open(&itemModels_, &world_.ground(),
                 core::fileExists(beam) ? ctx.textures.load(beam, content::TextureRole::Albedo)
                                        : bgfx::TextureHandle BGFX_INVALID_HANDLE);
    desk_.useModels(&itemModels_);
}

void PlayMode::warmItems() {
    const content::Tables& tables = *world_.played().realm().tables();
    // **Why the merchants and not the whole item table.** ItemModels reads a row's mesh the
    // first time somebody asks for it, which for a drop on the grass is one model in a frame
    // and unnoticeable. A shelf is not: Hanzo's counter asks for thirty-four in the frame the
    // window opens, each one a file read, a parse and a texture upload, and that is the hitch.
    // So every shelf in this world is read here instead, where the spinner is up and a load
    // costs nothing that shows. The whole table would be the same thing again for the rows
    // nobody can reach -- a session sees a few dozen kinds of item, and these are the kinds.
    int warmed = 0;
    // Counted first, so the spinner can say how far through the shelves it is.
    size_t shelved = 0, asked = 0;
    for (const content::Townsperson& person : tables.folk) {
        int count = 0;
        sim::stockOf(person.number, &count);
        shelved += size_t(count);
    }
    for (const content::Townsperson& person : tables.folk) {
        int count = 0;
        const sim::Offer* stock = sim::stockOf(person.number, &count);
        for (int i = 0; i < count; ++i) {
            core::Loading::part(asked++, shelved);
            const int32_t item = tables.itemAt(stock[i].group, stock[i].number);
            if (item >= 0 && itemModels_.of(item)) ++warmed;
        }
    }
    // And what he already carries, because a merchant opens the sheet beside his shelf and
    // the bag's own pictures are read the same way (game/ui/desk.cpp's `trading_`).
    const sim::Satchel& bag = world_.played().realm().satchel();
    for (int slot = 0; slot < sim::kSlots; ++slot) {
        if (!bag[slot].empty() && itemModels_.of(bag[slot].item)) ++warmed;
    }
    // Zen's own heap, the one row no shelf and no bag lists: the first coin to drop.
    itemModels_.coin();
    core::logf("items: %d shelf and bag pictures read ahead of the windows", warmed);
}

bool PlayMode::open(Context& ctx) {
    core::Args& args = ctx.args;
    const std::string& assets = ctx.paths.assets;

    readSave(ctx);

    // The world's own light over the base sheet -- Noria's tropical day over Lorencia's night --
    // or the base alone when the world has no sheet (game/world/maps.h).
    ctx.time.setScene(game::mapSheet(ctx.paths.sheets, args.world));
    // And its rain, when it has one: sheets/worlds/<world>_rain.json, blended in by the share.
    ctx.time.setWet(game::mapSheet(ctx.paths.sheets, args.world + "_rain"));

    // The game's own entrance, only when somebody is playing.
    entrance_ = args.play && (args.frames == 0 || args.entrance);

    if (args.atSet) world_.setFocusTile(args.atColumn, args.atRow);
    else if (args.raid > 0) {
        // The party in the town square; the dragon lands on a field outside it (sim::kRaidLandings).
        world_.setFocusTile(float(sim::kRaidTownColumn), float(sim::kRaidTownRow));
    } else if (!args.arena.empty()) {
        // The arena's own patch, unless the caller named a tile. Why that one is in
        // Play::Arena beside the constants; the short of it is that it is the flattest,
        // emptiest, non-safe square on the only cooked world.
        world_.setFocusTile(float(game::Play::Arena::kColumn), float(game::Play::Arena::kRow));
    } else if (resumed_) {
        world_.setFocusTile(float(saved_.hero.column), float(saved_.hero.row));
    }

    // Everything below runs on the preloader's worker while this thread draws the spinner.
    // See app/preloader.h for why that is legal.
    const bool up = Preloader::run(ctx, [&]() {
        // No crowd when the realm is going to be raised: the crowd stands monsters where
        // it chooses and the sim stands them where they are, and raising both means
        // loading, posing and then throwing away 45 figures a run.
        // The shares below are what each part took on a cold start, which is the load
        // anybody waits long enough to read them on (core/loading.h).
        core::Loading::span(0.0f, 0.48f);
        const bool ok = world_.open(assets, args.world, ctx.textures,
                                    args.play ? 0 : args.crowd, args.figuresOn);
        core::Loading::span(0.0f, 1.0f);
        // Where this map stops, so the last metres of it can go dark instead of ending at a
        // line. MU draws nothing past the last tile and lets the player walk to within three
        // of it, so the border was lit ground against the cleared frame; eight metres is wide
        // enough to read as dark rather than as a wall, and the nearest tile anyone may stand
        // on is far enough inside it that the ground he is standing on is untouched.
        if (ok) {
            constexpr float kEdgeBand = 8.0f;
            const float reach = float(world_.ground().size()) * world_.ground().metresPerTile();
            ctx.renderer.setMapEdge(reach, reach, kEdgeBand);
        }
        // And the realm behind it, when there is somebody playing. A world that cannot
        // raise one -- no cooked tables yet -- says so and is still a world to look at.
        if (ok && args.play) {
            core::Loading::stage("the realm", 0.48f, 0.49f);
            // Before the realm is raised, because all the arena is is the nest table the
            // realm is about to be handed. See Play::Arena.
            // --arena-learn alone teaches in a plain play run too, for a hand-played try.
            if (!args.arena.empty() || args.peaceful || args.arenaLearn != 0 || args.arenaUndying ||
                args.wingDemo) {
                game::Play::Arena arena;
                arena.breed = args.arena;
                arena.count = args.arenaCount;
                arena.learn = args.arenaLearn;
                arena.undying = args.arenaUndying;
                arena.wingDemo = args.wingDemo;
                arena.peaceful = args.peaceful;
                arena.gap = args.arenaGap;
                world_.played().setArena(arena);
                world_.played().setArenaLeft(args.arenaLeft);
            }
            sim::RealmConfig config;
            if (args.castlePeriod > 0) {
                config.castle = {args.castlePeriod, args.castlePeriod / 2, args.castlePeriod / 2};
            }
            config.questDemo = args.questDemo;
            world_.played().configure(config);
            if (args.raid > 0) {
                world_.played().setRaid(args.raid, raidParty(), args.raidWatch,
                                        args.raidBox ? std::clamp(args.raidBox - 'A', 0, sim::kRaidLandingCount - 1) : -1);
            }
            // On a server: Play joins it as it opens, and raises the mirror from its answer.
            const size_t colon = args.server.rfind(':');
            const bool hasPort = colon != std::string::npos;
            const std::string host = hasPort ? args.server.substr(0, colon) : args.server;
            const int port = hasPort ? std::atoi(args.server.c_str() + colon + 1) : net::kDefaultPort;
            const std::string server = host + ":" + std::to_string(port);
            if (!args.server.empty()) {
                // The game's first world on this server: his token from beside his save, kept from
                // the last run, so the server brings him back (game::loadServerToken). A fresh run
                // is a new character.
                // Not a character of an account: the screen handed his token over, and the server
                // keeps him (game/roster.h).
                if (args.account.empty() && args.serverToken == 0 && !savePath_.empty() && !args.fresh) {
                    args.serverToken = game::loadServerToken(savePath_, server);
                }
                world_.played().useServer(host, port, args.serverToken, args.account);
                world_.played().lagLine(args.lag, args.jitter, args.loss, args.udp);
            }
            world_.play(assets, args.world, args.seed, args.kin, args.level, args.weapon,
                        args.shield);
            // His character is in another world (net::Elsewhere): that one, opened as a map
            // change opens the next, at the tile the server keeps him on. This world, already
            // loaded, stands for a frame with no realm in it.
            if (const net::Elsewhere* there = world_.played().elsewhere()) {
                if (game::mapOf(there->world) != nullptr) {
                    travel(ctx, there->world, there->column, there->row, 0.0f, true);
                } else {
                    core::logError("server: he is in %s, a world this game has no row for",
                                   there->world.c_str());
                }
            }
            // The server's token for him, for the next world's Hello and the next run.
            if (world_.played().remote()) {
                args.serverToken = world_.played().serverToken();
                if (args.account.empty() && !savePath_.empty() && args.serverToken != 0) {
                    game::keepServerToken(savePath_, server, args.serverToken);
                }
            }
        }
        // And everything that hangs off a realm, only when there IS one. This used to run
        // on the answer to `args.play` alone, which is what was ASKED for and not what
        // happened: a play that opened its tables and then failed -- no cooked realm, or a
        // breed `--arena` could not find -- left a Play with no bodies in it, and
        // `openSound` reads `bodies_[0]` for the hero's own death cry. That is a read off
        // the front of an empty vector, and it is a crash and not a missing sound.
        if (world_.played().isOpen()) {
            // The raid's hero in his kit, and the stage asked for (play_raid.cpp).
            if (args.raid > 0) {
                world_.played().redress();
                world_.played().raidSkip(args.raidStage);
            }
            if (resumed_) {
                game::resolveSave(*world_.played().realm().tables(), saved_);
                world_.played().restore(saved_.hero);
                world_.played().restoreMachine(saved_.machine);
            }
            if (!savePath_.empty()) {
                world_.played().restoreVault(
                    game::resolveVault(*world_.played().realm().tables(), saved_));
            }
            // What a blow looks like and where a click sent him. Not fatal: open() has
            // said why in the log.
            core::Loading::stage("effects", 0.49f, 0.53f);
            world_.played().showing().open(assets, ctx.textures);
            world_.played().marker().open(assets, ctx.textures);
            world_.played().aura().open(assets, ctx.textures);
            world_.played().setShine().open(assets, ctx.textures);
            world_.played().warp().open(assets, ctx.textures);
            if (world_.played().showing().isOpen()) {
                if (const content::EffectSheet* light =
                        world_.played().showing().table().effect("light")) {
                    world_.played().setFolkLight(ctx.textures.load(
                        core::join(assets, light->path), content::TextureRole::Albedo));
                }
                const content::Showing& shown = world_.played().showing().table();
                if (const content::EffectSheet* star = shown.effect("shiny_02")) {
                    world_.played().setStarSheet(ctx.textures.load(
                        core::join(assets, star->path), content::TextureRole::Albedo));
                }
                const content::EffectSheet* orb = shown.effect("lightning_2");
                const content::EffectSheet* wisp = shown.effect("joint_energy");
                if (orb && wisp) {
                    world_.played().setFolkOrb(
                        ctx.textures.load(core::join(assets, orb->path), content::TextureRole::Albedo),
                        ctx.textures.load(core::join(assets, wisp->path), content::TextureRole::Albedo));
                }
                world_.played().breath().open(assets, ctx.textures,
                                              world_.played().showing().table(),
                                              &world_.ground());
                world_.played().snorts().open(assets, ctx.textures,
                                              world_.played().showing().table());
                world_.played().dust().open(assets, ctx.textures,
                                            world_.played().showing().table(), &world_.ground());
                world_.played().eyes().open(assets, ctx.textures,
                                            world_.played().showing().table());
                world_.played().eyeTrails().open(assets, ctx.textures,
                                                 world_.played().showing().table());
                world_.played().bodyFlames().open(assets, ctx.textures,
                                                  world_.played().showing().table());
                world_.played().staffRing().open(assets, ctx.textures, &world_.ground());
                world_.played().staffFire().open(assets, ctx.textures,
                                                 world_.played().showing().table());
                world_.played().heldLights().open(assets, ctx.textures,
                                                  world_.played().showing().table());
                world_.played().wingMotes().open(assets, ctx.textures,
                                                 world_.played().showing().table());
                world_.played().shadowStars().open(assets, ctx.textures,
                                                   world_.played().showing().table());
                world_.played().omen().open(assets, ctx.textures);
                world_.played().meteor().open(assets, ctx.textures,
                                              world_.played().showing().table(),
                                              &world_.ground());
                // The Golden Invasion's dragons: their breath glowing MU's lightning2, the heat
                // off their wings its soft flare01.
                {
                    const content::EffectSheet* glow = shown.effect("lightning_2");
                    const content::EffectSheet* haze = shown.effect("light");
                    world_.played().openInvasionSky(
                        glow ? ctx.textures.load(core::join(assets, glow->path),
                                                 content::TextureRole::Albedo)
                             : bgfx::TextureHandle BGFX_INVALID_HANDLE,
                        haze ? ctx.textures.load(core::join(assets, haze->path),
                                                 content::TextureRole::Albedo)
                             : bgfx::TextureHandle BGFX_INVALID_HANDLE);
                }
                world_.played().comet().open(assets, ctx.textures,
                                             world_.played().showing().table(), &world_.ground());
                world_.played().bolt().open(assets, ctx.textures,
                                            world_.played().showing().table());
                world_.played().wave().open(assets, ctx.textures,
                                            world_.played().showing().table());
                world_.played().arrows().open(assets, ctx.textures,
                                              world_.played().showing().table(),
                                              world_.ground().metresPerTile());
                world_.played().thunder().open(assets, ctx.textures,
                                               world_.played().showing().table());
                world_.played().blink().open(assets, ctx.textures,
                                             world_.played().showing().table());
                world_.played().ice().open(assets, ctx.textures,
                                           world_.played().showing().table());
                world_.played().trapShow().open(assets, args.world, ctx.textures,
                                                world_.played().showing().table());
                world_.played().trapShow().stand(world_.played().realm().traps(), world_.ground());
                world_.played().poison().open(assets, ctx.textures,
                                              world_.played().showing().table());
                world_.played().flame().open(assets, ctx.textures,
                                             world_.played().showing().table(), &world_.ground(),
                                             &world_.played().meteor());
                world_.played().spirits().open(assets, ctx.textures,
                                               world_.played().showing().table(),
                                               &world_.ground());
                world_.played().nova().open(assets, ctx.textures, world_.played().showing().table());
                world_.played().firework().open(assets, ctx.textures,
                                                world_.played().showing().table(),
                                                &world_.ground());
                world_.played().bones().open(assets, ctx.textures, &world_.ground());
                world_.played().streak().open(assets, ctx.textures,
                                              world_.played().showing().table());
                world_.played().forge().open(assets, ctx.textures,
                                             world_.played().showing().table(),
                                             &world_.ground());
                world_.played().wheel().open(assets, ctx.textures,
                                             world_.played().showing().table(),
                                             &world_.ground());
                world_.played().fury().open(assets, ctx.textures,
                                            world_.played().showing().table(),
                                            &world_.ground());
                world_.played().hellfire().open(assets, ctx.textures, &world_.ground());
                world_.played().storm().open(assets, ctx.textures,
                                             world_.played().showing().table(), &world_.ground());
                world_.played().inferno().open(assets, ctx.textures,
                                               world_.played().showing().table(), &world_.ground());
                world_.played().aqua().open(assets, ctx.textures,
                                            world_.played().showing().table(), &world_.ground());
                world_.played().deathStab().open(assets, ctx.textures,
                                                 world_.played().showing().table(),
                                                 &world_.ground());
                world_.played().impale().open(assets, ctx.textures, &world_.ground());
                world_.played().fireBreath().open(assets, ctx.textures,
                                                  world_.played().showing().table(),
                                                  &world_.ground());
                // The refinement shine's two sheets: Chrome01 for +7, Shiny01 for +9.
                game::lendShine(world_.played().showing().table(), assets, ctx.textures,
                                ctx.renderer);
                // And the Wings of Darkness's sparks' two sheets (game/wings.h).
                game::WingLook::lendSparks(world_.played().showing().table(), assets,
                                           ctx.textures);
            }
            // Hanzo's coals, into the lamps' static set before it goes to the renderer below.
            if (args.lampsOn) world_.played().lightForges(world_.lamps());
            core::Loading::stage("sounds", 0.53f, 0.58f);
            world_.played().openSound(assets, args.mute);
            world_.played().sound().setVolume(float(args.volume) / 100.0f);
            core::Loading::stage("the weather", 0.58f, 0.60f);
            // And only now the air: the birds' calls come off the sound above and the leaves'
            // sheet off the showing's table. See World::raiseAirs.
            if (args.airOn) world_.raiseAirs(assets, args.world, args.weather);
            // Not fatal either: a game with no HUD is still a game.
            core::Loading::stage("the interface", 0.60f, 0.675f);
            if (args.windows != "off" && !desk_.open(ctx.paths.shaders, assets, &ctx.textures)) {
                core::logError("the windows did not open; playing without a HUD");
            }
            desk_.setWatching(args.raid > 0 && args.raidWatch);
            // On a server, his windows' layout from beside his save (game::loadLayout).
            const bool laidOut = world_.played().isOpen() && world_.played().remote() &&
                                 !savePath_.empty() && !args.fresh &&
                                 game::loadLayout(savePath_, *world_.played().realm().tables(), saved_);
            if (resumed_ || laidOut) {
                for (int key = 0; key < 5; ++key) desk_.setQuick(key, saved_.quick[key]);
                desk_.restoreBar(saved_.bar, 6);
                desk_.followQuest(saved_.followed);
            }
            // And the pictures the windows will ask for, last of all: after restore(), so the
            // bag being warmed is the one he is carrying and not an empty one.
            core::Loading::stage("items", 0.675f, 1.0f);
            openItems(ctx);
            warmItems();
        }
        // An arena whose realm did not rise is a failed run and not a world to look at.
        // Everywhere else a realm that cannot be raised leaves a still world standing,
        // which is the right answer for a cook that has not been run; here the realm IS
        // what was asked for, and `--arena Wyvren` drawing 300 frames of empty grass and
        // exiting 0 is the same fault `--at 9999,9999` was fixed for.
        const bool arenaUp = args.arena.empty() || world_.played().isOpen();
        return ok && arenaUp;
    }, &quitEarly_, false, [&](double seconds) {
        // On a server, the line warmed up under the spinner (Play::settle): what the server sent
        // while the world loaded is stepped unseen, and unheard -- the mix is held silent and
        // given back its level when the world is let in.
        if (!world_.played().isOpen()) return true;
        game::Sound& sound = world_.played().sound();
        sound.setVolume(0.0f);
        const bool done = world_.played().settle(seconds);
        if (done) sound.setVolume(float(args.volume) / 100.0f);
        return done;
    });

    if (!up) {
        core::logError("the world did not open");
        // The world may have failed half-open -- `--at` off the map is refused after the
        // ground's buffers are already made -- and a failure path that skips the world's
        // own shutdown leaks a vertex and an index buffer past bgfx's own shutdown.
        world_.shutdown();
        return false;
    }

    // The lamps' static set, once: the renderer lays its light grid over the ground here.
    if (args.lampsOn) world_.lamps().light(ctx.renderer);
    // Placed once before the first frame: the loop answers the pointer against the camera
    // already on screen, and on frame zero there has to be one.
    world_.update(0.0, args.still);
    // He comes in once the frame has faded most of the way up. Long enough a wait to
    // outlast the first frame, which compiles every pipeline and takes a tenth of a second.
    if (entrance_ && world_.played().isOpen()) world_.played().appear(0.3f);
    // And the map's name with him, MU's ShowMapName on entering a world.
    if (entrance_ && world_.played().isOpen() && desk_.ready()) {
        const sim::Body& hero = world_.played().realm().hero();
        desk_.arrive(game::placeName(args.world, hero.column(), hero.row()), 0.3f,
                     world_.played().zoneLevels());
    }

    runScript(ctx);

    // The game menu (game/ui/menu.h). In a world somebody is playing, Escape is the game's: it
    // shuts the open windows and then raises the menu, and quitting is the menu's Exit. A run
    // with --frames keeps Escape as the quit it always was, so a review can be stopped.
    if (desk_.ready() && world_.played().isOpen()) {
        desk_.setWorld(args.world);
        // Switch Character goes back to the screen this run came through, and only then.
        desk_.allowSwitch(args.lobby);
        fillSettings(ctx.window, args, &desk_.settings());
        const bool held = args.frames == 0;
        desk_.holdEscape(held);
        ctx.window.holdEscape(held);
    }

    // And the played world, for the tile over the character's head: the numbers `--at` takes,
    // so a screenshot of something to fix says where to go back to.
    if (world_.played().isOpen() && !ctx.overlay.init(ctx.paths.shaders)) {
        core::logError("the world opened without the tile over the character's head");
    }

    openProbes(ctx);
    keptAt_ = bx::getHPCounter();
    return true;
}

void PlayMode::runScript(Context& ctx) {
    core::Args& args = ctx.args;
    // Into a castle by the Messenger: its garrison and its statue are that castle's.
    if (world_.played().isOpen() && ctx.castleNext > 0) {
        world_.played().setCastle(ctx.castleNext);
        ctx.castleNext = 0;
    }
    if (world_.played().isOpen() && args.castle > 0) world_.played().setCastle(args.castle);
    if (world_.played().isOpen() && args.castleOpen) world_.played().openCastleDoor();
    if (world_.played().isOpen() && args.invasion && !world_.played().invade()) {
        core::logError("--invasion: this map has no invasion");
    }
    if (world_.played().isOpen() && args.raid > 0 && !world_.played().invade(args.raidNow)) {
        core::logError("--raid: this map has no invasion");
    }
    if (world_.played().isOpen() && args.castleFree) world_.played().freeCastle();
    if (world_.played().isOpen() && args.castleBridge >= 0) {
        world_.played().dropCastleBridge(args.castleBridge);
    }
    if (world_.played().isOpen() && !args.give.empty()) {
        size_t from = 0;
        while (from <= args.give.size()) {
            const size_t comma = args.give.find(',', from);
            const std::string one = args.give.substr(from, comma - from);
            const size_t colon = one.find(':');
            if (!one.empty()) {
                const size_t second =
                    colon == std::string::npos ? std::string::npos : one.find(':', colon + 1);
                world_.played().give(one.substr(0, colon),
                                     colon == std::string::npos
                                         ? 1
                                         : std::atoi(one.c_str() + colon + 1),
                                     second == std::string::npos ? "" : one.substr(second + 1));
            }
            if (comma == std::string::npos) break;
            from = comma + 1;
        }
    }
    if (world_.played().isOpen()) {
        if (args.zen > 0) world_.played().earn(args.zen);
        // --quest-ready: every clear already walked, so the giver's mark is a ? and his talk is
        // the hand-in -- the reward's demo, which is why it has no save.
        if (args.questReady) {
            sim::HeroRecord ready = world_.played().record();
            for (int q = 0; q < sim::kQuests; ++q) {
                ready.quests[q].state = sim::QuestState::Ready;
                for (int s = 0; s < sim::kQuestSteps; ++s) {
                    ready.quests[q].counts[s] = uint16_t(world_.played().realm().questGoal(q, s));
                }
            }
            world_.played().restore(ready);
        }
        // --quests-done: those handed in once and resting, the demo of what waits on them.
        if (!args.questsDone.empty()) {
            sim::HeroRecord done = world_.played().record();
            for (const char* at = args.questsDone.c_str(); *at;) {
                const int q = std::atoi(at);
                if (q >= 0 && q < sim::kQuests) {
                    done.quests[q] = sim::QuestProgress{};
                    done.quests[q].state = sim::QuestState::Resting;
                    done.quests[q].completions = 1;
                }
                while (*at && *at != ',') ++at;
                if (*at == ',') ++at;
            }
            world_.played().restore(done);
        }
        if (!args.talk.empty()) world_.played().talkTo(args.talk);
        if (args.perch >= 0) world_.played().perch(args.perch);
        // Once: a gate's next world is opened on these same arguments.
        if (args.walkColumn >= 0) world_.played().walkTo(args.walkColumn, args.walkRow);
        args.walkColumn = -1;
    }
    if (args.windows.find("inventory") != std::string::npos) desk_.setInventoryOpen(true);
    if (args.windows.find("character") != std::string::npos) desk_.setCharacterOpen(true);
    if (args.windows.find("sanctuary") != std::string::npos) desk_.setSpecimenOpen(true);
    if (args.windows.find("graphics") != std::string::npos) {
        desk_.showMenu(game::Menu::Page::Graphics);
    } else if (args.windows.find("options") != std::string::npos) {
        desk_.showMenu(game::Menu::Page::Options);
    } else if (args.windows.find("menu") != std::string::npos) {
        desk_.showMenu(game::Menu::Page::Main);
    }
}

void PlayMode::openProbes(Context& ctx) {
    core::Args& args = ctx.args;
    if (args.shadowView || args.shadowNoise >= 0) {
        ctx.renderer.setShadowDebug(args.shadowView ? 1 : 0, args.shadowNoise);
    }
    // The pan test's grid: fixed ground points around where the camera starts, and a csv
    // row a frame of the pixel each lands on. tools/pan.py samples the shots there.
    if (!args.shadowPoints.empty()) {
        shadowPoints_ = std::fopen(args.shadowPoints.c_str(), "w");
        if (!shadowPoints_) {
            core::logError("could not open --shadow-points %s", args.shadowPoints.c_str());
        } else {
            constexpr int kSide = 60;
            constexpr float kStep = 0.3f;
            const gfx::Camera& cam = world_.camera();
            for (int j = 0; j < kSide; ++j) {
                for (int i = 0; i < kSide; ++i) {
                    const float x = cam.target[0] + (float(i) - kSide * 0.5f) * kStep;
                    const float z = cam.target[2] + (float(j) - kSide * 0.5f) * kStep;
                    pointGrid_.insert(pointGrid_.end(), {x, world_.ground().heightAt(x, z), z});
                }
            }
            std::fprintf(shadowPoints_, "# %zu points; frame then x,y pixel pairs, -1 when off "
                                        "screen\n", pointGrid_.size() / 3);
        }
    }
    // The shadow probe's csv. docs/shadow-probe.md reads it.
    if (!args.shadowLog.empty()) {
        shadowLog_ = std::fopen(args.shadowLog.c_str(), "w");
        if (shadowLog_) {
            std::fprintf(shadowLog_,
                         "frame,dt_ms,focus_x,focus_z,texel_mm,texel_x,texel_y,phase_x,phase_y,"
                         "depth_quanta,depth_phase,hero_x,hero_z,camera_lag_mm\n");
        } else {
            core::logError("could not open --shadow-log %s", args.shadowLog.c_str());
        }
    }
}

void PlayMode::arenaHand() {
    // Fight the nearest of them, and the next one when that one is down. It raises the same
    // Attack request a click raises (Play::fight) and decides nothing else -- the walk to it,
    // the reach, the roll and the damage are all the realm's, which is the whole point of an
    // arena over a bench.
    //
    // Only when he has nobody: the request is not re-raised every frame. Re-asking is not free
    // -- Realm::accept takes the pending order at the top of a tick and an Attack order
    // re-taken resets the chase's re-plan -- and a hand that only speaks when the hero is idle
    // is also the one a person would be.
    const sim::Realm& realm = world_.played().realm();
    const sim::Body& hero = realm.hero();
    const sim::Body* held = arenaTarget_ != 0 ? realm.find(arenaTarget_) : nullptr;
    const bool rearmed = world_.played().quickSkill() != arenaSkill_;
    if (hero.alive() && held && held->alive() && rearmed) {
        arenaSkill_ = world_.played().quickSkill();
        world_.played().fight(arenaTarget_);
    }
    if (hero.alive() && (!held || !held->alive())) {
        const sim::Body* nearest = nullptr;
        float best = 1e9f;
        for (const sim::Body& body : realm.bodies()) {
            if (!body.monster() || !body.alive()) continue;
            const float dx = body.x - hero.x, dy = body.y - hero.y;
            if (dx * dx + dy * dy < best) {
                best = dx * dx + dy * dy;
                nearest = &body;
            }
        }
        if (nearest) {
            arenaTarget_ = nearest->id;
            arenaSkill_ = world_.played().quickSkill();
            world_.played().fight(arenaTarget_);
        }
    }
}

bool PlayMode::scriptedPointer(Context& ctx, const Frame& at, const float* view,
                               const float* proj, float* pointerX, float* pointerY) {
    // The scripted pointer, for a run with nobody at the mouse. It goes through the same
    // unprojection, the same tile, the same request: what it skips is the hand and nothing else.
    core::Args& args = ctx.args;
    if (args.demoClicks <= 0 || at.index % args.demoClicks != 0) return false;
    static const float kSpots[6][2] = {{0.50f, 0.50f}, {0.62f, 0.38f},
                                       {0.38f, 0.60f}, {0.70f, 0.55f},
                                       {0.44f, 0.34f}, {0.56f, 0.66f}};
    const int spot = (at.index / args.demoClicks) % 6;
    *pointerX = kSpots[spot][0] * float(ctx.window.width());
    *pointerY = kSpots[spot][1] * float(ctx.window.height());

    // Aimed at the nearest living monster when there is one, and at the spot above when
    // there is not.
    //
    // The six spots wander: the camera follows the hero, so the middle of the screen is
    // roughly his own tile and a scripted click mostly walks a step and comes back. That is
    // fine for proving a walk and useless for proving a fight -- over 900 frames in a spider
    // field it produced blows landing ON the hero and not one landing on a monster, which is
    // the half of sprint 6 worth looking at. The click still goes through the same
    // unprojection, the same tile and the same request; only where it points is chosen.
    const sim::Realm& realm = world_.played().realm();
    const sim::Body& hero = realm.hero();
    const sim::Body* nearest = nullptr;
    float best = 1e9f;
    for (const sim::Body& body : realm.bodies()) {
        if (!body.monster() || !body.alive()) continue;
        const float dx = body.x - hero.x, dy = body.y - hero.y;
        const float away = dx * dx + dy * dy;
        if (away < best) {
            best = away;
            nearest = &body;
        }
    }
    // With --loot, what lies on the ground comes first: the scripted hand picks up before it
    // fights again, which is the half of sprint 7 a run with nobody at the mouse can otherwise
    // never show.
    float aimX = nearest ? nearest->x : 0.0f, aimY = nearest ? nearest->y : 0.0f;
    if (args.loot) {
        float closest = 12.0f * 12.0f;
        for (const sim::Lying& one : realm.lying()) {
            const float dx = float(one.column) - hero.x;
            const float dy = float(one.row) - hero.y;
            if (dx * dx + dy * dy < closest) {
                closest = dx * dx + dy * dy;
                aimX = float(one.column);
                aimY = float(one.row);
                best = closest;
                nearest = &hero;  // anything non-null: there is an aim
            }
        }
    }
    // Only when it is close enough to walk to and fight in a few ticks; anything further and
    // the run is a march rather than a fight.
    if (nearest != nullptr && best < 12.0f * 12.0f) {
        const content::Ground& land = world_.ground();
        const float metresPerTile = land.metresPerTile();
        const float wx = (aimX + 0.5f) * metresPerTile;
        const float wz = -(aimY + 0.5f) * metresPerTile;
        const float wy = land.heightAt(wx, wz);
        float clip[4] = {0, 0, 0, 0};
        const float world4[4] = {wx, wy, wz, 1.0f};
        float viewProjNow[16];
        bx::mtxMul(viewProjNow, view, proj);
        bx::vec4MulMtx(clip, world4, viewProjNow);
        if (clip[3] > 0.0f) {
            // Clip space to pixels. y is flipped because clip space runs up the screen and
            // the pointer runs down it, which is the same flip vs_overlay.sc makes for the
            // same reason.
            const float ndcX = clip[0] / clip[3];
            const float ndcY = clip[1] / clip[3];
            *pointerX = (ndcX * 0.5f + 0.5f) * float(ctx.window.width());
            *pointerY = (0.5f - ndcY * 0.5f) * float(ctx.window.height());
        }
    }
    return true;
}

void PlayMode::frame(Context& ctx, const Frame& at) {
    core::Args& args = ctx.args;
    const double deltaSeconds = at.deltaSeconds;
    if (args.lobby && args.lobbyBack >= 0 && at.index >= args.lobbyBack) backNow_ = true;

    // Through a gate on the last tick: on to the map it leads to, at the tile the realm chose
    // there, facing the way the exit gate says (sim/gates.h).
    int gateColumn = 0, gateRow = 0;
    if (world_.played().isOpen() && travelTo_.empty()) {
        if (const int32_t number = world_.played().gated(&gateColumn, &gateRow)) {
            const sim::EnterGate* in = sim::enterGateNumbered(number);
            const sim::ExitGate* out = in ? sim::exitGate(in->target) : nullptr;
            const game::MapRow* map = out ? game::mapNumbered(int(out->map)) : nullptr;
            if (map != nullptr) {
                // And the Messenger's: which castle, for the castle's realm (Realm::setCastle).
                ctx.castleNext = number == sim::kCastleEnterGate
                                     ? world_.played().realm().castlePassed()
                                     : 0;
                travel(ctx, map->world, gateColumn, gateRow,
                       std::atan2(float(out->dy), float(out->dx)));
            } else {
                core::logError("gate %d leads to a map this game has no world for", number);
            }
        }
    }

    // Go Back! to another map, which the realm said (What::WentBack): there, by magic, as a gate's
    // map change. The way back itself -- opened by a Town Portal or a Tab trip out of a dungeon's
    // field, its clock, its closing -- is the realm's (sim::WayBack).
    if (world_.played().isOpen() && travelTo_.empty()) {
        int map = -1, column = 0, row = 0;
        if (world_.played().takeWentBack(&map, &column, &row)) {
            if (const game::MapRow* to = game::mapNumbered(map)) {
                ctx.goBack.landing = true;
                travel(ctx, to->world, column, row, 0.0f, true);
            }
        }
    }

    // A Town Portal read, or a death, on a map with no safe zone: to its town's spawn gate
    // (MapRow arrive) -- Lorencia's from the Dungeon, as OpenMU's SafezoneMap for a map with no
    // spawn gate (BaseMapInitializer.cs:91), Devias's from Blood Castle (MapRow home).
    if (world_.played().isOpen() && travelTo_.empty() && world_.played().takeHome()) {
        const game::MapRow* here = game::mapOf(args.world);
        if (const game::MapRow* home = game::mapNumbered(here ? here->home : 0)) {
            // Blood Castle won and paid: its banner and stinger are owed in Devias (goBack).
            const sim::CastleRun& run = world_.played().realm().castleRun();
            if (run.phase == sim::CastlePhase::Won && run.claimed) {
                ctx.castleDone = run.castle;
                ctx.castleDoneExperience = run.paidExperience;
                ctx.castleDoneZen = run.paidZen;
            }
            ctx.goBack.landing = true;
            travel(ctx, home->world);
        }
    }

    // --travel-at: on to the next world in the table, for a scripted run. No key does this; the
    // gates above and Tab's list below are how a player changes map.
    const bool scripted = args.travelAt >= 0 && at.index >= args.travelAt;
    if (world_.played().isOpen() && travelTo_.empty() && scripted) {
        args.travelAt = -1;  // once: the next world is opened with these same arguments
        travel(ctx, game::mapAfter(args.world)->world);
    }

    // Tab's travel list (game/ui/travel.h): paid for in the realm, the map changed here as a
    // gate's is. A Dungeon floor asked from inside the Dungeon is the same world opened again.
    if (world_.played().isOpen() && travelTo_.empty()) {
        const int row = world_.played().takeTravel();
        const sim::TravelRow* to = row >= 0 ? &sim::travelAt(row) : nullptr;
        const game::MapRow* map = to ? game::mapNumbered(int(to->map)) : nullptr;
        if (map != nullptr) {
            // By magic, and heard landing (the user, 2026-10-01: 'we need also teleport sound
            // effect when we use TAB teleport'). The way back it opens or gives up is the realm's.
            ctx.goBack.landing = true;
            const bool faced = to->dx != 0 || to->dy != 0;
            travel(ctx, map->world, to->column, to->row,
                   faced ? std::atan2(float(to->dy), float(to->dx)) : 0.0f, !faced);
        }
    }

    goBack(ctx, deltaSeconds);

    if (!savePath_.empty() &&
        double(bx::getHPCounter() - keptAt_) / double(bx::getHPFrequency()) > 15.0) {
        keep(ctx);
        keptAt_ = bx::getHPCounter();
    }

    // The pointer and what it is over, before the sim is stepped: a click is taken at
    // the start of the next tick and walked on that same tick (Realm::accept).
    if (world_.played().isOpen()) {
        float view[16];
        float proj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        float pointerX = 0.0f, pointerY = 0.0f;
        ctx.window.pointer(&pointerX, &pointerY);
        if (args.pointX >= 0.0f) {
            pointerX = args.pointX * float(ctx.window.width());
            pointerY = args.pointY * float(ctx.window.height());
        }
        // --no-hand leaves the arena's hero to the player, for a hand-played try in a crowd.
        if (!args.arena.empty() && !args.noHand) arenaHand();
        const bool clickNow = scriptedPointer(ctx, at, view, proj, &pointerX, &pointerY);
        // The windows first: a click that lands on one is the interface's, and the
        // world only hears the clicks that land on none. A scripted click is the
        // world's by construction -- it is aimed at a monster.
        if (desk_.ready()) {
            float viewProjNow[16];
            bx::mtxMul(viewProjNow, view, proj);
            desk_.setView(viewProjNow);
            desk_.setCamera(view);
            desk_.setGround(&world_.ground());
            for (const auto& [f, k] : args.uiKeys) {
                if (at.index == f) desk_.scriptKey(k - 1);
            }
            for (const auto& [f, k] : args.uiSkills) {
                if (at.index == f) desk_.scriptSkill(k - 1);
            }
            for (const core::Args::Hold& hold : args.uiHolds) {
                if (at.index == hold.frame) desk_.scriptSkill(hold.key - 1);
                if (at.index >= hold.frame && at.index < hold.frame + hold.frames) desk_.scriptHold(hold.key - 1);
            }
            if (args.boltEvery > 40 && at.index % args.boltEvery == args.boltEvery - 40) {
                world_.played().benchFace(view[0], view[8]);
            }
            if (args.boltEvery > 0 && at.index > 0 && at.index % args.boltEvery == 0) {
                // Screen-right, off the view's own first row, so the whole flight and its
                // impact are across the picture rather than toward the camera.
                world_.played().benchBolt(args.boltTiles, view[0], view[8], args.boltSkill);
            }
            const float w = float(ctx.window.width()), h = float(ctx.window.height());
            // A pointer parked where the run asked, pressing nothing: what photographing a
            // tooltip needs, since a click on an item in the bag picks it up instead of
            // describing it. A scripted click this frame replaces it, script() keeping the last.
            if (args.hoverX >= 0.0f) desk_.script(args.hoverX * w, args.hoverY * h, false, false);
            // --lay: the bench's drops, on their frame, one after another as a kill's pile lands.
            if (at.index == args.layFrame && world_.played().isOpen()) {
                size_t from = 0;
                while (from <= args.lay.size()) {
                    const size_t comma = args.lay.find(',', from);
                    const std::string one = args.lay.substr(from, comma - from);
                    if (!one.empty()) world_.played().lay(one);
                    if (comma == std::string::npos) break;
                    from = comma + 1;
                }
            }
            for (const core::Args::UiClick& c : args.uiClicks) {
                const bool drag = c.x2 != c.x || c.y2 != c.y;
                const int last = c.frame + (drag ? 3 : 1);
                if (at.index < c.frame || at.index > last) continue;
                // Pressed at the first point, carried to the second, let go there.
                const bool here = at.index == c.frame;
                desk_.script((here ? c.x : c.x2) * w, (here ? c.y : c.y2) * h, here,
                             at.index == last, c.right);
            }
            for (const auto& [frame, text] : args.uiTyped) {
                if (at.index == frame) desk_.scriptType(text);
            }
            desk_.update(float(deltaSeconds), ctx.window, world_.played(), pointerX, pointerY);
            // A box that has the keyboard has Escape too, which otherwise quits.
            ctx.window.setTyping(desk_.typing());
            // What the menu's Options changed: the display, the window's size, v-sync, the
            // volume and the counter in the corner. Kept in options.txt when main.sh ran us
            // (--remember). The window's new size reaches the renderer on the next pump.
            if (desk_.settingsChanged()) {
                const game::Menu::Settings& set = desk_.settings();
                applySettings(ctx.window, set, args);
                world_.played().sound().setVolume(float(set.volume) / 100.0f);
            }
            // And the pictures for whatever the windows now hold: MU2's Panel.Repaint,
            // which redraws a stage only when what stands on it changed or turns.
            desk_.photograph(ctx.renderer, deltaSeconds);
        }
        const bool windowed = desk_.ready() && desk_.takesPointer();
        world_.played().point(world_.camera(), view, proj, pointerX, pointerY,
                              ctx.window.width(), ctx.window.height());
        // A name plate over the drop outranks what the ray found behind it, a monster included.
        if (desk_.ready() && !windowed) {
            world_.played().pointAtLabel(desk_.labelUnder(pointerX, pointerY));
        }
        if ((ctx.window.clicked(0) && !windowed) || clickNow) world_.played().leftClick();
        if (ctx.window.clicked(1) && !windowed) {
            world_.played().rightClick();
        } else if (ctx.window.held(1) && !windowed) {
            world_.played().rightHeld();
        }
        world_.played().update(deltaSeconds);
        // The colour goes out of the world while he is down. Half a second out and a second
        // back: a fall should land and a recovery should feel like one. The renderer drains the
        // scene's own pass, so the HUD and the message over it stay in colour -- which is the
        // point, and is why this is a renderer setting and not a grade in the sheet.
        // Down as the DRAWING has it, not the realm: the realm kills him on the tick, but the
        // blow lands on screen up to half a swing later, and that landing is what starts his
        // fall and pushes "You Died" (Play::fall). Read off the realm, the colour went before
        // the message did. shownAlive flips on the same cue the message is pushed on.
        {
            constexpr float kDrainIn = 0.5f, kDrainBack = 1.0f;
            const game::Play& played = world_.played();
            const bool down = !played.shownAlive(played.realm().hero().id);
            const float rate = float(deltaSeconds) / (down ? kDrainIn : kDrainBack);
            drain_ = down ? std::min(1.0f, drain_ + rate) : std::max(0.0f, drain_ - rate);
            // And the revive is a cut made in the dark. The realm moves him from his corpse to
            // the gate in one tick; seen, that is the body vanishing and the whole map jumping
            // under the camera. So the world goes down to black over the last moments he lies
            // there -- black a frame or two BEFORE the tick is due, since a frame is not a tick
            // -- and comes back up with him fading in at the gate. Invention; MU hard-cuts.
            constexpr float kDipOut = 0.45f, kDipEarly = 0.08f, kDipBack = 0.7f;
            const float left = played.heroRisesIn();
            if (down && left >= 0.0f) {
                const float t = std::clamp((kDipOut + kDipEarly - left) / kDipOut, 0.0f, 1.0f);
                dim_ = std::max(dim_, t * t * (3.0f - 2.0f * t));
            } else if (!down) {
                dim_ = std::max(0.0f, dim_ - float(deltaSeconds) / kDipBack);
            }
        }
        for (const int f : args.rises) {
            if (at.index == f) world_.played().rise();
        }
        for (const int f : args.learns) {
            if (at.index == f) world_.played().learned();
        }
        for (const int f : args.guards) {
            if (at.index == f) world_.played().showGuard();
        }
    }
    // And only THEN the camera, onto where the character is drawn this frame. Placed
    // before the step, it followed where he stood a frame ago: the town and every
    // shadow in it slid under him by his step length times the frame time, which
    // changes every frame -- 16 mm median and 79 mm worst over a walk, on a shadow
    // texel of 29 mm. docs/shadow-probe.md.
    world_.update(at.elapsed, args.still);
    // What the frame is drawn from, taken off the camera just placed. It was taken at the top of
    // the frame, which drew every frame with the one before's camera -- the lag the note above
    // was written against, back again -- and a Town Portal's first frame at the gate showed the
    // field he had left, with nobody in it.
    gfx::Camera eye = world_.camera();
    // The Lich's EarthQuake, and it is a TILT and not a slide: MU adds it to
    // `m_State.Angle[0]` (DefaultCamera.cpp:700), which is the camera's pitch in
    // degrees, and decays it by 0.2 a frame (MainScene.cpp:199). Carried here as what
    // it is -- a rotation of the eye about what it is looking at, which is the same
    // orbit MU's camera has. Slid instead, as this was first written, the shake was
    // the quarter of a MU unit it says it is: four millimetres, on a camera six
    // metres out, which is nothing at all.
    {
        // And Rageful Blow's crater, which shakes it the same way (fx/fury.h), and Hellfire's
        // wall (fx/hellfire.h).
        const float pitch = world_.played().meteor().quakeDegrees() +
                            world_.played().fury().quakeDegrees() +
                            world_.played().hellfire().quakeDegrees();
        if (pitch != 0.0f) {
            float ahead[3], right[3], up[3];
            for (int a = 0; a < 3; ++a) ahead[a] = eye.position[a] - eye.target[a];
            // The axis to tilt about: across the view, level with the ground.
            right[0] = -ahead[2];
            right[1] = 0.0f;
            right[2] = ahead[0];
            const float length = std::sqrt(right[0] * right[0] + right[2] * right[2]);
            if (length > 1e-6f) {
                for (int a = 0; a < 3; ++a) right[a] /= length;
                const float radians = pitch * 3.14159265f / 180.0f;
                const float c = std::cos(radians), s = std::sin(radians);
                // Rodrigues about `right`, which is a unit vector in the XZ plane.
                const float dot = ahead[0] * right[0] + ahead[2] * right[2];
                up[0] = right[1] * ahead[2] - right[2] * ahead[1];
                up[1] = right[2] * ahead[0] - right[0] * ahead[2];
                up[2] = right[0] * ahead[1] - right[1] * ahead[0];
                for (int a = 0; a < 3; ++a) {
                    eye.position[a] =
                        eye.target[a] + ahead[a] * c + up[a] * s + right[a] * dot * (1.0f - c);
                }
            }
        }
    }
    // The music: MuMain's ManageBackgroundMusic (SceneManager.cpp:992), the tavern half only.
    // In Lorencia's safe zone MU plays Pub.mp3 while he stands on the tavern floor -- HeroTile 4,
    // which is what World::indoors asks -- and main_theme.mp3 everywhere else. Not here (the
    // user, 2026-09-27: "don't play main theme anymore in game ... but keep pub logic"), so off
    // the tavern floor the town is silent of music.
    // **The dragon's fight has its own music** (the user, 2026-10-06: 'lets use ... taiko-invasion
    // ... for dragon fight scene when character is fighting dragon'): looping while he is in it
    // (Play::raidFighting), and the map's own rule again after. The one fight with music.
    // A little louder than the town's tracks (the user, 2026-10-08: "music has to be little bit
    // louder on boss fight"): 0.8, about 2.5 dB over their 0.6.
    const std::string fightTrack = ctx.paths.assets + "/music/dragon_fight.mp3";
    if (world_.played().isOpen() && world_.played().raidFighting() && core::fileExists(fightTrack)) {
        game::Sound& sound = world_.played().sound();
        if (!sound.musicPlaying(fightTrack)) sound.music(fightTrack, 0.8f, true);
    } else if (world_.played().isOpen()) {
        bool pub = false;
        bool loops = false;  // played round and round, not once and rested
        const char* roofTrack = nullptr;
        if (args.world == "lorencia") {
            const sim::Body& hero = world_.played().realm().hero();
            const content::Tables* tables = world_.played().realm().tables();
            float feetX = 0.0f, feetZ = 0.0f;
            world_.characterAt(&feetX, &feetZ);
            pub = tables && tables->grid.safe(hero.column(), hero.row()) &&
                  world_.indoors(feetX, feetZ);
            // Looped while he is inside, never rested, as Devias's roofs are (the user,
            // 2026-10-08: "lorencia tavern always has to play music when inside, same as devias
            // in-doors rules").
            loops = true;
            roofTrack = "/music/Pub.mp3";
        } else if (args.world == "devias") {
            // Devias's theme under its roofs -- the planks and patterned floors World::indoors
            // reads -- and silence in the snow. MU plays MUSIC_DEVIAS on the whole map
            // (SceneManager.cpp); ours, the user's (2026-09-30: "when we go inside devias
            // buildings play devias theme music"). Looped under the roof, never rested (the user,
            // 2026-10-07: 'if character goes inside devias buildings we always play music').
            float feetX = 0.0f, feetZ = 0.0f;
            world_.characterAt(&feetX, &feetZ);
            pub = world_.indoors(feetX, feetZ);
            loops = true;
            roofTrack = "/music/Devias.mp3";
        } else if (args.world == "noria") {
            // MUSIC_NORIA in its safe zone, as MU plays it (SceneManager.cpp:1028-1036: `if
            // (Hero->SafeZone)`) -- the user, 2026-10-05: 'play noria music on safezone', where
            // it had been left off since 2026-09-30.
            const sim::Body& hero = world_.played().realm().hero();
            const content::Tables* tables = world_.played().realm().tables();
            pub = tables && tables->grid.safe(hero.column(), hero.row());
            roofTrack = "/music/Noria.mp3";
        } else if (args.world == "atlans" || args.world == "losttower" || args.world == "tarkan") {
            // Their themes in their safe zones -- Atlans's basin, the Lost Tower's hall -- and
            // silence out on the hunt. MU plays MUSIC_ATLANS and MUSIC_LOSTTOWER_A on the whole
            // map (SceneManager.cpp:1047-1073); ours, the user's (2026-10-05: 'lost tower music
            // and atlans music has to play in safe zones'). Atlans's water bed stays everywhere.
            const sim::Body& hero = world_.played().realm().hero();
            const content::Tables* tables = world_.played().realm().tables();
            pub = tables && tables->grid.safe(hero.column(), hero.row());
            // And Tarkan's MUSIC_TARKAN in its town (SceneManager.cpp:1061-1066), the same way.
            roofTrack = args.world == "atlans"   ? "/music/atlans.mp3"
                        : args.world == "tarkan" ? "/music/tarkan.mp3"
                                                 : "/music/lost_tower_a.mp3";
        } else if (args.world == "icarus") {
            // Ours: a horror ambient on loop over the whole map (the user, 2026-10-06: 'lets play
            // freesound_community-horror-ambient-14590.mp3 in icarus on loop'), under which the
            // air, aHeaven, goes on. MU plays MUSIC_ICARUS there; Icarus has no safe zone.
            pub = true;
            loops = true;
            roofTrack = "/music/icarus_ambient.mp3";
        }
        // No music out on the hunt, and none for fights (the user, 2026-09-30: "we dont need
        // fight music anymore").
        const std::string path = ctx.paths.assets + (pub ? roofTrack : "");
        game::Sound& sound = world_.played().sound();
        if (pub && loops && core::fileExists(path) && !sound.musicPlaying(path)) {
            sound.music(path, 0.6f, true);
        } else if (pub && core::fileExists(path) && !sound.musicPlaying(path)) {
            // Once through, and only when it has rested since it last began.
            const double now = runSeconds();
            const auto last = musicBegan().find(path);
            if (last == musicBegan().end() || now - last->second >= kMusicRestSeconds) {
                sound.music(path, 0.6f, false);
                musicBegan()[path] = now;
            }
        } else if (!pub) {
            sound.stopMusic();
        }
    }
    // The ears, onto the camera just placed: its heading is what the stereo field turns by.
    if (world_.played().isOpen()) {
        world_.played().hear(world_.camera(),
                             world_.indoors(world_.camera().target[0],
                                            world_.camera().target[2]));
        world_.played().hearWorld(
            args.lampsOn ? &world_.lamps() : nullptr, world_.ornaments(),
            [](void* self, float x, float z) {
                return static_cast<const game::World*>(self)->indoors(x, z);
            },
            &world_);
    }
    // The lamps flicker, the fires burn, and the glows' levels go into the town before
    // it is gathered, since each rides in its instance. docs/sprints/08a-the-lamps.md.
    if (args.lampsOn) {
        world_.lamps().update(float(deltaSeconds), world_.town(), ctx.renderer, eye.target);
        // The Lost Tower's vents that caught this frame burn the wizard's Flame, whose light is
        // the vent's own lamp rather than a transient slot.
        if (world_.played().isOpen()) {
            for (const game::Lamps::VentStart& vent : world_.lamps().ventsLit()) {
                world_.played().flame().light(vent.at, vent.yaw, false, game::Flame::kVentStrength);
            }
        }
        // What the day gives an unlit puff of smoke: the ambient and the sun on a flat
        // surface, over what the default sheet's noon gives it.
        world_.lamps().gather(ctx.renderer.effects(), eye.target, daylightOf(ctx.lighting));
    }
    // And what is burning and MOVING, which the lamps' static grid cannot hold: a
    // Lich's meteor lights the ground it is falling towards. Handed over every frame,
    // including the frame it becomes none, which is what clears it.
    // And after them, in what slots are left, the light a +7 or +9 hero carries at night
    // (fx/gleam.h; ours, not MU's).
    {
        gfx::PointLight falling[gfx::Renderer::kMaxTransientLights];
        uint32_t count =
            world_.played().meteor().lights(falling, gfx::Renderer::kMaxTransientLights);
        // And Cometfall's comets, MU's blue under each as it falls and where it lands.
        count += world_.played().comet().lights(falling + count,
                                                gfx::Renderer::kMaxTransientLights - count);
        // And the wizard's bolts, the blue each throws on the ground it crosses.
        count += world_.played().bolt().lights(falling + count,
                                               gfx::Renderer::kMaxTransientLights - count);
        // And Power Wave's, three tiles of blue.
        count += world_.played().wave().lights(falling + count,
                                               gfx::Renderer::kMaxTransientLights - count);
        count += world_.played().thunder().lights(falling + count,
                                                  gfx::Renderer::kMaxTransientLights - count);
        // And a Poison cloud's green miasma, two tiles.
        count += world_.played().poison().lights(falling + count,
                                                 gfx::Renderer::kMaxTransientLights - count);
        // And a Flame's orange, three tiles.
        count += world_.played().flame().lights(falling + count,
                                                gfx::Renderer::kMaxTransientLights - count);
        // And Twisting Slash's wheel, the grey MU lays under each copy, as one.
        count += world_.played().wheel().lights(falling + count,
                                                gfx::Renderer::kMaxTransientLights - count);
        // And Rageful Blow's broken ground, MU's red under its fires, as one.
        count += world_.played().fury().lights(falling + count,
                                               gfx::Renderer::kMaxTransientLights - count);
        // And Hellfire's wall, MU's orange four tiles round (fx/hellfire.h).
        count += world_.played().hellfire().lights(falling + count,
                                                   gfx::Renderer::kMaxTransientLights - count);
        // And Twister's storm, a faint cool light under it -- ours (fx/storm.h).
        count += world_.played().storm().lights(falling + count,
                                                gfx::Renderer::kMaxTransientLights - count);
        // And Inferno's ring, one warm light for its blasts (fx/inferno.h).
        count += world_.played().inferno().lights(falling + count,
                                                  gfx::Renderer::kMaxTransientLights - count);
        // And the Dinorant's breath, MU's cool blue under it (fx/firebreath.h).
        count += world_.played().fireBreath().lights(falling + count,
                                                     gfx::Renderer::kMaxTransientLights - count);
        // And Icarus's flash, MU's dim yellow under him (game/world/sky_clouds.h).
        count += world_.skyClouds().lights(falling + count,
                                           gfx::Renderer::kMaxTransientLights - count);
        // And Aqua Beam's line, three blue lights along it (fx/aqua.h).
        count += world_.played().aqua().lights(falling + count,
                                               gfx::Renderer::kMaxTransientLights - count);
        // And a steel or saw bolt's faint cold light (fx/arrow.h; ours).
        count += world_.played().arrows().lights(falling + count,
                                                 gfx::Renderer::kMaxTransientLights - count);
        // And a Fire Trap's burst on the floor, two tiles.
        count += world_.played().trapShow().lights(falling + count,
                                                   gfx::Renderer::kMaxTransientLights - count);
        // And the nearest Poison Shadows' faint green (fx/shadow_stars.h; ours).
        count += world_.played().shadowStars().lights(
            falling + count, gfx::Renderer::kMaxTransientLights - count, eye.target);
        count += world_.played().gleam().lights(falling + count,
                                                gfx::Renderer::kMaxTransientLights - count,
                                                daylightOf(ctx.lighting));
        // And last, what is left to the nearest jewels lying down (fx/litter.h; ours).
        count += litter_.lights(falling + count, gfx::Renderer::kMaxTransientLights - count,
                                eye.target);
        ctx.renderer.setTransientLights(falling, count);
        // And the refined gear's own glow, the other half of it being a light source.
        ctx.renderer.setShineGlow(game::Gleam::nightOf(daylightOf(ctx.lighting)) *
                                  ctx.lighting.refineGlow);
    }
    // The town's own animation: before the town is gathered, since each placement's
    // pose rides in its own instance the same way a glow's level does. Only what the
    // camera can see is posed -- see Sway::update.
    {
        float view[16];
        float proj[16];
        float viewProj[16];
        ctx.renderer.cameraMatrices(eye, view, proj);
        bx::mtxMul(viewProj, view, proj);
        // In Devias's blizzard the town's own clips -- the firs' sway above all -- run up to two
        // and a half times their rate, so the trees thrash rather than drift. **Invention.**
        const float storm = world_.weather().snows() ? world_.weather().rain() : 0.0f;
        world_.sway().update(float(deltaSeconds) * (1.0f + 1.5f * storm),
                             args.cullChunks ? viewProj : nullptr, ctx.renderer, world_.town());
        // The street lamps' lights, carried with their lanterns on the pose just taken.
        if (args.lampsOn) world_.lamps().follow(world_.sway(), ctx.renderer);
    }
    // What rides those bones, on this frame's pose: the fountain's spray and the
    // merchant animal's lanterns. See game/world/ornaments.h.
    world_.ornaments().update(float(deltaSeconds), world_.sway());
    // The Chaos Machine's bursts, thrown through the smith's forge: eight pairs is two of its
    // strikes (game/world/ornaments.h).
    if (world_.played().isOpen()) {
        for (size_t i = 0; i < world_.ornaments().strikeCount(); ++i) {
            world_.played().forge().strike(world_.ornaments().strikeAt(i));
            world_.played().forge().strike(world_.ornaments().strikeAt(i));
        }
    }
    world_.ornaments().gather(ctx.renderer.effects(), world_.sway());
    // The smoke lying on the Lost Tower's lava, round the point the camera follows.
    world_.lavaSmoke().update(float(deltaSeconds), eye.target);
    world_.lavaSmoke().gather(ctx.renderer.effects());
    world_.voidClouds().update(float(deltaSeconds), eye.target);
    world_.voidClouds().gather(ctx.renderer.effects());
    world_.castleSparks().update(float(deltaSeconds), eye.target);
    world_.castleSparks().gather(ctx.renderer.effects());
    // Tarkan's steam vents, sand geysers (their stones the meteor's), falling sand and glow
    // sprites (game/world/desert_vents.h).
    world_.desertVents().update(float(deltaSeconds), eye.target, [&](const float* at) {
        if (world_.played().isOpen()) world_.played().meteor().stones(at[0], at[2], at[1], 1);
    });
    world_.desertVents().gather(ctx.renderer.effects());
    // Icarus's cloud road and the lightning in it (game/world/sky_clouds.h).
    {
        // The camera's point is the hero's: MU throws the flash and the glints round him.
        world_.skyClouds().update(float(deltaSeconds), eye.target, eye.target,
                                  [&](const float* from, const float* to) {
                                      if (world_.played().isOpen())
                                          world_.played().thunder().fork(from, to);
                                  });
    }
    world_.skyClouds().gather(ctx.renderer.effects());
    world_.boids().gatherTrails(ctx.renderer.effects());
    // And its sandstorm, MU's two screen layers (game/world/sand_haze.h).
    world_.sandHaze().update(float(deltaSeconds));
    world_.sandHaze().gather(ctx.renderer.effects(), eye);
    world_.bubbles().update(float(deltaSeconds), eye.target);
    world_.bubbles().gather(ctx.renderer.effects());
    world_.portal().update(float(deltaSeconds));
    world_.portal().gather(ctx.renderer.effects(), eye.target);
    // The shade under the bridges, which MU draws as a blended mesh. See game/world/shades.h.
    world_.shades().gather(ctx.renderer.effects(), eye.target);
    // What flies over the town and what blows through it. Both follow the character, both
    // stop where he is under a roof, and the birds read the frame twice -- a flock arrives
    // from off it and a bird is taken off only once it has left it -- so both are given this
    // frame's view-projection, the same one the town is about to be culled against. See
    // game/world/boids.h and leaves.h.
    if ((world_.boids().isOpen() || world_.leaves().isOpen()) && world_.played().isOpen()) {
        float feetX = 0.0f, feetZ = 0.0f;
        world_.characterAt(&feetX, &feetZ);
        const float hero[3] = {feetX, world_.ground().heightAt(feetX, feetZ), feetZ};
        const bool inside = world_.indoors(feetX, feetZ);
        float view[16], proj[16], viewProj[16];
        ctx.renderer.cameraMatrices(eye, view, proj);
        bx::mtxMul(viewProj, view, proj);
        // Whether he is moving, which is what startles a perched bird off the ground -- the
        // realm's own answer, not the figure's clip, because a man turning on the spot plays a
        // walk and has not gone anywhere.
        if (args.birdsNow) world_.boids().hurry();
        const bool walking = world_.played().realm().hero().walking;
        world_.boids().stepGlow(float(deltaSeconds));
        // The Golden Invasion takes the birds out of the sky while it lasts: MU's ReceiveEvent
        // calls DeleteBoids() and its slots fill with dragons (GOBoid.cpp:1275).
        const bool birds = !world_.played().invasionStorm();
        if (birds) world_.boids().glow(ctx.renderer.effects());
        if (birds) {
            world_.boids().update(float(deltaSeconds), hero, walking, inside, world_.ground(),
                                  viewProj, ctx.renderer);
        }
        // The Golden Invasion comes with the rain (sim/invasion.h), and its storm is held while
        // it is on (Weather::summon).
        world_.played().invasionRain(world_.weather().stormy());
        if (!world_.played().remote()) world_.weather().settle();
        world_.weather().summon(world_.played().invasionStorm());
        world_.weather().sync(world_.played().serverRain());
        // The weather first: how much of the leaves' pool is rain this frame. weather.h.
        // Under the open sky where the map is "underground" only for its air (Tarkan's sand).
        world_.weather().update(float(deltaSeconds), inside && !world_.leaves().openAir());
        // Under a roof the light is the dry spell's, eased over the doorway (Weather::shelter).
        const float open = 1.0f - world_.weather().shelter();
        ctx.time.rain(world_.weather().rain() * open, world_.weather().flash() * open);
        // Devias's blizzard drives the snow; everywhere else the storm is nought.
        world_.leaves().setStorm(world_.weather().snows() ? world_.weather().rain() : 0.0f);
        world_.leaves().update(float(deltaSeconds), hero, eye.position, inside, world_.ground(),
                               world_.weather().pour());
        world_.leaves().gather(ctx.renderer.effects(), eye.position);
    }
    // The town's drawables are gathered fresh each frame into one vector that keeps
    // its capacity: a frame appends to a flat array, as foundation 7 says, and
    // allocates nothing after the first.
    townDrawables_.clear();
    townCasters_.clear();
    hoverDrawables_.clear();
    flashDrawables_.clear();
    flashes_.clear();
    const std::vector<gfx::Drawable>* casters = nullptr;
    if (world_.town().isOpen()) {
        if (args.cullChunks) {
            float view[16];
            float proj[16];
            ctx.renderer.cameraMatrices(eye, view, proj);
            float viewProj[16];
            bx::mtxMul(viewProj, view, proj);
            world_.town().gatherVisible(viewProj, townDrawables_);
            // The sun gets its own list, and for now it is all of them. A chunk
            // behind the camera still casts into the frame, so the camera's frustum
            // is the wrong test for the split -- foundation 7's named bug. Culling
            // the split against its own box is the next step and it is measured
            // separately; drawing every caster is the honest baseline to measure it
            // against.
            // On the GPU: the town's casters stand in a buffer of their own and only what
            // changed is uploaded (Town::residentCasters). townCasters_ carries the rest.
            ctx.renderer.setResidentCasters(&world_.town().residentCasters());
            ctx.renderer.setGpuCasters(&world_.town().gpuCasters());
            casters = &townCasters_;
        } else {
            world_.town().gatherAll(townDrawables_);
        }
    }
    // The birds cast as well. MU gives every boid outside Heaven a shadow of its own:
    // RenderBoids ends each bird with RenderBodyShadow, laid on the terrain under it at a
    // fifth black (GOBoid.cpp). Here the sun's split carries it like any other caster.
    if (!world_.played().invasionStorm()) {
        world_.boids().gather(townDrawables_);
        if (casters) world_.boids().gather(townCasters_);
    }
    // The Dungeon's traps, posed and drawn with the scenery they stand among.
    {
        float feetX = 0.0f, feetZ = 0.0f;
        world_.characterAt(&feetX, &feetZ);
        const float hero[3] = {feetX, world_.ground().heightAt(feetX, feetZ), feetZ};
        world_.played().trapShow().update(float(deltaSeconds), hero, ctx.renderer);
    }
    world_.played().trapShow().gather(townDrawables_);
    if (casters) world_.played().trapShow().gather(townCasters_);
    if (world_.played().isOpen()) {
        float view[16];
        float proj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        float viewProj[16];
        bx::mtxMul(viewProj, view, proj);
        ctx.renderer.setDrain(drain_);
        ctx.renderer.setDim(dim_);
        world_.played().gather(ctx.renderer, viewProj, townDrawables_,
                               casters ? &townCasters_ : nullptr, &hoverDrawables_,
                               &flashDrawables_, &flashes_);
        if (desk_.ready()) {
            desk_.overhead(float(deltaSeconds), world_.played(), viewProj, ctx.window.width(),
                           ctx.window.height());
        }
        // And the blood the blows have thrown, into the transparent pass. The figures
        // are not here any more: since the design page of 2026-09-23 they are drawn in
        // a real face by the interface, over the world -- game/ui/tally.cpp, which
        // Desk::overhead above has just placed on this same camera.
        world_.played().showing().gather(ctx.renderer.effects());
        world_.played().gatherMarker(ctx.renderer.effects());
        world_.played().gatherOmen(ctx.renderer.effects());
        world_.played().gatherAura(ctx.renderer.effects(), eye.position);
        world_.played().gatherWarp(ctx.renderer.effects());
        world_.played().breath().gather(ctx.renderer.effects());
        world_.played().snorts().gather(ctx.renderer.effects());
        world_.played().dust().gather(ctx.renderer.effects());
        world_.played().eyes().gather(ctx.renderer.effects());
        world_.played().eyeTrails().gather(ctx.renderer.effects(), eye.position);
        world_.played().bodyFlames().gather(ctx.renderer.effects());
        world_.played().staffRing().gather(ctx.renderer.effects());
        world_.played().staffFire().gather(ctx.renderer.effects());
        world_.played().heldLights().gather(ctx.renderer.effects());
        world_.played().wingMotes().gather(ctx.renderer.effects());
        world_.played().shadowStars().gather(ctx.renderer.effects());
        world_.played().gatherMeteor(ctx.renderer.effects(), eye.position);
        world_.played().glowInvasion(ctx.renderer.effects());
        world_.played().gatherBolt(ctx.renderer.effects(), eye.position);
        world_.played().wave().gather(ctx.renderer.effects());
        world_.played().arrows().gather(ctx.renderer.effects());
        world_.played().thunder().gather(ctx.renderer.effects());
        world_.played().blink().gather(ctx.renderer.effects());
        world_.played().ice().gather(ctx.renderer.effects());
        world_.played().trapShow().gatherEffects(ctx.renderer.effects());
        world_.played().poison().gather(ctx.renderer.effects());
        world_.played().flame().gather(ctx.renderer.effects());
        world_.played().spirits().gather(ctx.renderer.effects());
        world_.played().nova().gather(ctx.renderer.effects());
        world_.played().firework().gather(ctx.renderer.effects());
        world_.played().gatherStreak(ctx.renderer.effects());
        world_.played().gatherFolkLights(ctx.renderer.effects());
        world_.played().gatherForge(ctx.renderer.effects(), eye.position, eye.target,
                                    daylightOf(ctx.lighting));
        world_.played().wheel().gatherEffects(ctx.renderer.effects(), eye.position, eye.target,
                                              daylightOf(ctx.lighting));
        world_.played().fury().gatherEffects(ctx.renderer.effects(), eye.position, eye.target,
                                             daylightOf(ctx.lighting));
        world_.played().hellfire().gatherEffects(ctx.renderer.effects());
        world_.played().storm().gatherEffects(ctx.renderer.effects());
        world_.played().inferno().gatherEffects(ctx.renderer.effects());
        world_.played().aqua().gatherEffects(ctx.renderer.effects());
        world_.played().deathStab().gatherEffects(ctx.renderer.effects());
        world_.played().impale().gatherEffects(ctx.renderer.effects());
        world_.played().fireBreath().gatherEffects(ctx.renderer.effects(), eye.position,
                                                   eye.target, daylightOf(ctx.lighting));
        // And what is lying on the grass: MU2's Drops, tossed up out of the corpse and
        // laid down where they land.
        openItems(ctx);
        litter_.update(world_.played().realm(), deltaSeconds, world_.played().heldDrops());
        world_.played().setSettledDrops(litter_.settled());
        litter_.gather(townDrawables_, casters ? &townCasters_ : nullptr);
        litter_.gatherBeams(ctx.renderer.effects(), eye.position);
        // The hover ring's own subject, if a drop is what is pointed at rather than a
        // body or a townsperson -- see Play::gather's `hover` for the other two, and
        // leftClick's own ladder, which this stays behind: pointedLying() can be set
        // beside a monster or a townsperson that outranks it.
        if (world_.played().pointedFolk() < 0 && world_.played().pointedAt() == 0) {
            litter_.gatherOne(world_.played().pointedLying(), hoverDrawables_);
        }
    }

    // The crowd goes into the same two lists as the town, and through the same two
    // passes. A figure is not a special case of a drawable: it is a drawable whose
    // mesh carries a skin and whose instance names a palette row.
    world_.crowd().update(float(deltaSeconds));
    if (world_.crowd().figureCount() > 0) {
        float view[16];
        float proj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        float viewProj[16];
        bx::mtxMul(viewProj, view, proj);
        world_.crowd().gather(ctx.renderer, args.cullChunks ? viewProj : nullptr,
                              townDrawables_, casters ? &townCasters_ : nullptr);
    }
    // Along both of the ground's axes, and not a whole texel's worth of either in one
    // step, so the split crosses texel boundaries in x and in y at different frames.
    if (args.shadowSlideMm != 0.0f) {
        const float metres = float(at.index) * args.shadowSlideMm * 0.001f;
        const float slide[3] = {metres, 0.0f, metres * 0.618f};
        ctx.renderer.slideSplit(slide);
    }
    // The near field's blades, gathered last because the field is measured from where the
    // camera ended up standing this frame and the follow spring has only just decided that.
    // One instanced draw in the shade pass; never in the prepass and never in the sun's
    // split. docs/grass.md.
    gfx::GrassField grassField;
    bool grassDrawn = false;
    // The Options page's Graphics rows: the targets' quality, and the sheet as drawn.
    applyGraphics(ctx.renderer, ctx.args);
    const gfx::Lighting look = graphicsLook(ctx.lighting, ctx.args);
    {
        float view[16];
        float proj[16];
        float viewProj[16];
        ctx.renderer.cameraMatrices(eye, view, proj);
        bx::mtxMul(viewProj, view, proj);
        // Whoever is standing in the field, hero first: the sward parts round each of them.
        // Nobody is walking when nobody is played.
        float walkers[gfx::GrassField::kMaxWalkers * 4];
        const int walking = world_.played().isOpen()
                                ? world_.played().walkers(walkers, gfx::GrassField::kMaxWalkers)
                                : 0;
        grassDrawn = world_.grass().gather(world_.ground(), look, viewProj, eye.position,
                                           walkers, walking, float(at.elapsed), grassField);
        // Devias's blizzard bows the grass the way its snow flies, harder in each gust: at 0.6
        // of the wind a gust lays the sward over by about half, an ordinary blow by a quarter.
        // "procedural grass was not reacting so good as storm snow flakes" (2026-09-30).
        world_.leaves().stormWind(grassField.storm);
        grassField.storm[0] *= 0.6f;
    }
    ctx.renderer.draw(eye, look, townDrawables_, &world_.ground(), casters,
                      grassDrawn ? &grassField : nullptr);
    // The gold ring: over the world the frame above just drew, under the windows the
    // line below is about to -- so a window drawn over a ringed monster still covers
    // it, the same order Godot's CanvasLayer(-1) kept the ring in. Shown whenever
    // Play::point found something, exactly as MU2's own Ringed did: it is not gated
    // on the pointer being clear of a window, only the click a monster answers to is.
    if (!hoverDrawables_.empty()) {
        float outlineView[16], outlineProj[16];
        ctx.renderer.cameraMatrices(eye, outlineView, outlineProj);
        const bool player = world_.played().pointedPlayer() != 0;
        const bool shadow = world_.played().pointedFolk() < 0 && world_.played().pointedAt() == 0 && !player;
        // Another player in green, not the gold a thing to fight or take wears: Sanctuary's live
        // green (style::kLive), as his plate is his (the user, 2026-10-08).
        const float green[4] = {0.498f, 0.839f, 0.416f, 1.0f};
        outline_.show(ctx.renderer, eye, outlineView, outlineProj, ctx.window.width(),
                      ctx.window.height(), hoverDrawables_, shadow, 0, player ? green : nullptr);
    }
    // And a monster that has just turned on him flashes red, in rings of its own after the
    // gold, so a hovered one blinks red over its gold and back (Play::watchAggro). A halo
    // of kFlashGlow pixels past the ring, "add some glow to that flash" (2026-09-30).
    if (!flashes_.empty()) {
        constexpr float kFlashGlow = 12.0f;
        float outlineView[16], outlineProj[16];
        ctx.renderer.cameraMatrices(eye, outlineView, outlineProj);
        for (size_t i = 0; i < flashes_.size() && i < size_t(game::Play::kFlashRings); ++i) {
            const game::Play::Flash& flash = flashes_[i];
            flashOne_.assign(flashDrawables_.begin() + ptrdiff_t(flash.from),
                             flashDrawables_.begin() + ptrdiff_t(flash.to));
            const float red[4] = {1.0f, 0.16f, 0.10f, flash.strength};
            outline_.show(ctx.renderer, eye, outlineView, outlineProj, ctx.window.width(),
                          ctx.window.height(), flashOne_, false, int(i) + 1, red, kFlashGlow);
        }
    }
    if (desk_.ready()) desk_.submit(gfx::ViewHud, ctx.window.width(), ctx.window.height());
    if (args.fps) ctx.readout.draw(ctx.overlay, ctx.window.width(), ctx.window.height(),
                                    world_.played().rttMs());
    if (entrance_ && ctx.curtain.ready()) {
        if (at.index >= 2) entranceSeconds_ += float(deltaSeconds);
        const float t = std::min(1.0f, entranceSeconds_ / 0.1f);
        const float black = 1.0f - t * t * (3.0f - 2.0f * t);
        if (black > 0.0f) {
            // After the HUD in the same view: the HUD draws in submission order, so
            // this lies over it and the whole picture comes up together.
            ctx.curtain.begin(ctx.window.width(), ctx.window.height());
            ctx.curtain.panel(0.0f, 0.0f, float(ctx.window.width()),
                              float(ctx.window.height()), uint32_t(black * 255.0f) << 24);
            ctx.curtain.submit(gfx::ViewHud);
        }
    }
    writeProbes(ctx, at);
}

void PlayMode::writeProbes(Context& ctx, const Frame& at) {
    if (shadowPoints_) {
        float view[16], proj[16], viewProj[16];
        ctx.renderer.cameraMatrices(world_.camera(), view, proj);
        bx::mtxMul(viewProj, view, proj);
        const float w = float(ctx.window.width()), h = float(ctx.window.height());
        std::fprintf(shadowPoints_, "%d", at.index);
        for (size_t p = 0; p < pointGrid_.size(); p += 3) {
            const float point[4] = {pointGrid_[p], pointGrid_[p + 1], pointGrid_[p + 2], 1.0f};
            float clip[4];
            bx::vec4MulMtx(clip, point, viewProj);
            float px = -1.0f, py = -1.0f;
            if (clip[3] > 0.0f) {
                const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
                if (nx > -1.0f && nx < 1.0f && ny > -1.0f && ny < 1.0f) {
                    px = (nx * 0.5f + 0.5f) * w;
                    py = (0.5f - ny * 0.5f) * h;
                }
            }
            std::fprintf(shadowPoints_, ",%.3f,%.3f", px, py);
        }
        std::fprintf(shadowPoints_, "\n");
    }
    if (shadowLog_) {
        const gfx::Renderer::SplitRecord& split = ctx.renderer.lastSplit();
        const gfx::Camera& cam = world_.camera();
        auto phase = [](float v) { return v - std::floor(v); };
        float heroX = cam.target[0], heroZ = cam.target[2];
        const bool played = world_.characterAt(&heroX, &heroZ);
        const float* followed = world_.followed();
        const float lagMm =
            played ? 1000.0f * std::hypot(heroX - followed[0], heroZ - followed[2]) : 0.0f;
        std::fprintf(shadowLog_,
                     "%d,%.3f,%.4f,%.4f,%.2f,%.4f,%.4f,%.4f,%.4f,%.3f,%.4f,%.4f,%.4f,%.2f\n",
                     at.index, at.deltaSeconds * 1000.0, cam.target[0], cam.target[2],
                     split.texel * 1000.0f, split.texelX, split.texelY, phase(split.texelX),
                     phase(split.texelY), split.depthQuanta, phase(split.depthQuanta), heroX,
                     heroZ, lagMm);
    }
}

void PlayMode::report(Context& ctx) {
    // Foundation 7: the drawn and the culled go in the log, for the camera and for
    // the sun separately, or a culling change cannot be seen to have happened. The
    // sun's line says "all" while its casters are not culled at all, which is the
    // honest way to say that half of this is not built yet.
    if (world_.town().isOpen()) {
        const game::TownCounts& counts = world_.town().counts();
        const game::TownCounts& sun = world_.town().casterCounts();
        core::logf("  town: camera %u of %u chunks and %u of %zu placements; "
                   "sun %u chunks and %u placements, unculled",
                   counts.chunksDrawn, counts.chunksDrawn + counts.chunksCulled,
                   counts.instancesDrawn, world_.town().instanceCount(), sun.chunksDrawn,
                   sun.instancesDrawn);
    }
    // The air, which is two numbers and is the only way to tell an empty sky from a broken
    // one: a flock is a pass and the sky is meant to be empty between them, so "0 flying" on
    // its own says nothing. Logged whenever either pool is up. See game/world/boids.h.
    if (world_.boids().isOpen() || world_.leaves().isOpen()) {
        core::logf("  air: %u bird(s) flying, %u leaf/leaves on the wind, %u drop(s) falling "
                   "at rain %.2f; %u forge sparks and smoke live",
                   world_.boids().flying(), world_.leaves().blowing(), world_.leaves().falling(),
                   world_.weather().rain(),
                   world_.played().isOpen() ? world_.played().forge().live() : 0u);
    }
    if (desk_.ready()) core::logf("%s", desk_.line().c_str());
    if (world_.played().isOpen()) {
        const game::Play& play = world_.played();
        const sim::Body& hero = play.realm().hero();
        const sim::RealmCounts counts = play.realm().counts();
        core::logf("  play: tick %lld, %.3f ms a tick, hero level %d at %.1f,%.1f with "
                   "%d of %d health; %u monsters, %u alive, %u awake",
                   (long long)play.ticks(), play.tickMs(), hero.level, hero.x, hero.y,
                   hero.health, hero.maxHealth, counts.monsters, counts.alive, counts.roused);
        // What the ring is round, as well as what a click would take: the three are
        // the same question and the ladder Play::leftClick answers it with.
        core::logf("  pointer: tile %d,%d%s | %s", play.pointedColumn(), play.pointedRow(),
                   play.pointedFolk() >= 0 ? " (on a townsperson)"
                   : play.pointedAt()      ? " (on a monster)"
                   : play.pointedLying()   ? " (on a drop)"
                   : play.pointedPerch() >= 0 ? " (on a perch)"
                                           : "",
                   play.lastLine().c_str());
        if (play.findings().total() > 0) {
            core::logError("  play: %llu invariants broken",
                           (unsigned long long)play.findings().total());
            // What broke, once a kind: the count alone ran to 865 over a freeze on
            // 2026-10-03 and never said which rule it was.
            const std::vector<std::string>& first = play.findings().first;
            if (findingsSaid_ > first.size()) findingsSaid_ = 0;  // a new world's audit
            for (; findingsSaid_ < first.size(); ++findingsSaid_)
                core::logError("  play: broken: %s", first[findingsSaid_].c_str());
        }
    }
    if (world_.crowd().figureCount() > 0) {
        const game::Crowd& crowd = world_.crowd();
        core::logf("  crowd: %u of %zu figures drawn, %u culled, %zu bones, "
                   "pose %.3f ms, %d palette rows of %d%s",
                   crowd.drawn(), crowd.figureCount(), crowd.culled(), crowd.boneCount(),
                   crowd.poseMs(), ctx.renderer.paletteRowsUsed(),
                   gfx::Renderer::kMaxPaletteRows,
                   ctx.renderer.paletteRowsRefused() ? " -- FULL, the rest stand in bind pose"
                                                     : "");
        if (ctx.renderer.paletteRowsRefused()) {
            core::logError("%d figures found no palette row this frame and stood in "
                           "bind pose; the palette holds %d",
                           ctx.renderer.paletteRowsRefused(), gfx::Renderer::kMaxPaletteRows);
        }
    }
}

void PlayMode::travel(Context& ctx, const std::string& world, int column, int row,
                      float facing, bool unfaced) {
    const game::MapRow* map = game::mapOf(world);
    if (map == nullptr || (world == ctx.args.world && column < 0)) return;
    core::Args& args = ctx.args;
    arriveFaced_ = column >= 0 && !unfaced;
    arriveColumn_ = column >= 0 ? column : map->arrive[0];
    arriveRow_ = column >= 0 ? row : map->arrive[1];
    arriveFacing_ = facing;
    core::logf("travel: %s to %s, coming in at %d,%d", args.world.c_str(), world.c_str(),
               arriveColumn_, arriveRow_);
    travelTo_ = world;
    args.world = world;
    args.play = true;
    // He is the same character: what the save holds is what the next world raises. A run with
    // no save (a --frames review) has only these arguments, so the tile goes in them as well,
    // and a --fresh run stays fresh only until it has a file to resume from.
    args.atSet = true;
    args.atColumn = float(arriveColumn_);
    args.atRow = float(arriveRow_);
    if (!savePath_.empty()) {
        args.savePath = savePath_;
        args.fresh = false;
    }
}

void PlayMode::goBack(Context& ctx, double seconds) {
    if (!world_.played().isOpen()) return;
    // Come into this world by magic: the warp's sound and ring as he stands in it.
    if (!landed_) {
        landed_ = true;
        if (ctx.goBack.landing) world_.played().landed();
        ctx.goBack.landing = false;
        // Home from a Blood Castle won: "Event complete" over the quest's stinger, as a quest's
        // hand-in is heard (the user, 2026-10-05: 'char has to be teleported back to devias and
        // play quest done music but with window event done').
        if (ctx.castleDone > 0) {
            desk_.announce("Event complete", "Blood Castle " + std::to_string(ctx.castleDone),
                           ctx.castleDoneExperience, ctx.castleDoneZen);
            world_.played().sound().stinger("music/quest_complete.wav");
            world_.played().sound().duck();
            core::logf("event: home from Blood Castle %d, its banner and stinger", ctx.castleDone);
            ctx.castleDone = 0;
        }
    }
    sinceLanded_ += seconds;
    // His way back, as the realm keeps it (sim::WayBack): open with its seconds, or the closed line.
    const sim::WayBack& way = world_.played().realm().wayBack();
    const game::MapRow* to = game::mapNumbered(way.map);
    if (way.map < 0 || to == nullptr) {
        desk_.goBack(false, 0, false, {});
        return;
    }
    if (desk_.takeGoBack() && way.open()) {
        world_.played().goBack();
        desk_.goBack(false, 0, false, {});
        return;
    }
    // Up once the map's name has come and gone (game/ui/arrival.h's 5.4 s), so the two never
    // stand on the screen together; the clock is running from the landing all the same.
    std::string where = game::placeName(to->world, way.column, way.row);
    if (!where.empty()) where[0] = char(std::toupper(static_cast<unsigned char>(where[0])));
    where += " " + std::to_string(way.column) + ", " + std::to_string(way.row);
    const bool shown = !way.open() || sinceLanded_ >= kGoBackWaits;
    desk_.goBack(shown, int((way.ticksLeft + 19) / 20), !way.open(), where);
}

void PlayMode::shutdown(Context& ctx) {
    // Out of the game or back to the character screen: the way back goes into the save, which
    // gives it back when he is played again, and out of the session. On to another world it
    // comes along.
    keep(ctx);
    if (!savePath_.empty()) core::logf("save: kept in %s", savePath_.c_str());
    ctx.time.setScene("");
    ctx.time.setWet("");
    if (shadowLog_) std::fclose(shadowLog_);
    if (shadowPoints_) std::fclose(shadowPoints_);
    shadowLog_ = shadowPoints_ = nullptr;
    // The renderer outlives the mode, and a character switched out while he lay dead would
    // take the lobby's world grey and dark with him.
    ctx.renderer.setDrain(0.0f);
    ctx.renderer.setDim(0.0f);
    // This order is the one main() kept and it is not arbitrary: the stages go back to the
    // renderer before the models they photographed are let go.
    ctx.renderer.closeStages();
    itemModels_.shutdown();
    desk_.shutdown();
    world_.shutdown();
}

}  // namespace mu::app
