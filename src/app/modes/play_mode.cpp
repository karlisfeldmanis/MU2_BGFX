#include "app/modes/play_mode.h"

#include "net/socket.h"
#include "net/wire.h"

#include <bgfx/bgfx.h>
#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <unordered_map>

#include "app/options.h"
#ifdef MU2_CRASH_UPLOAD
#include <thread>

#include "build_version.h"
#include "launcher/bug_report.h"
#include "launcher/crash_upload.h"
#endif
#include "app/preloader.h"
#include "core/args.h"
#include "core/files.h"
#include "core/loading.h"
#include "core/log.h"
#include "core/paths.h"
#include "game/roster.h"
#include "game/shine.h"
#include "game/wings.h"
#include "game/world/maps.h"
#include "game/world/world_show.h"
#include "gfx/views.h"
#include "gfx/window.h"
#include "net/regions.h"
#include "sim/gates.h"
#include "sim/items.h"
#include "sim/market.h"
#include "sim/raid_party.h"
#include "sim/quests.h"
#include "sim/travel.h"

namespace mu::app {
namespace {
// The moving lights gathered each frame before the renderer's four are chosen from them.
constexpr uint32_t kGatheredLights = 32;

// Keeps the `keep` moving lights that reach nearest `target`, the hero's ground, in front, and
// returns how many that is. A light is as near as the edge of its pool: a wide wall of fire
// three metres out outranks a bolt's faint blue one metre out only when its edge comes closer.
// A line light is measured from the nearer of its ends. Ties go to the brighter.
uint32_t keepNearest(gfx::PointLight* lights, uint32_t count, uint32_t keep, const float target[3]) {
    if (count <= keep) return count;
    auto gap = [&](const gfx::PointLight& one) {
        auto flat = [&](const float at[3]) {
            const float dx = at[0] - target[0], dz = at[2] - target[2];
            return std::sqrt(dx * dx + dz * dz);
        };
        const float d = one.line ? std::min(flat(one.position), flat(one.to)) : flat(one.position);
        return std::max(0.0f, d - one.reach);
    };
    auto bright = [](const gfx::PointLight& one) {
        return std::max(one.colour[0], std::max(one.colour[1], one.colour[2]));
    };
    std::partial_sort(lights, lights + keep, lights + count,
                      [&](const gfx::PointLight& a, const gfx::PointLight& b) {
                          const float ga = gap(a), gb = gap(b);
                          return ga != gb ? ga < gb : bright(a) > bright(b);
                      });
    return keep;
}

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
// And when each was last heard playing: a world's own `rest` counts from there, so the silence
// between hearings is the same however long the track runs.
std::unordered_map<std::string, double>& musicHeard() {
    static std::unordered_map<std::string, double> heard;
    return heard;
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
        !args.questProgress.empty() || args.raid > 0;
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
    // the realm (sim::Realm::setRaid) -- whatever a save said (docs/game/golden-dragon-raid.md).
    if (args.raid > 0) {
        resumed_ = false;
        const std::string party = core::folders().content + "/raid/party.json";
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
    // On a server the character is the server's (sprint 18): nothing played
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
        // And without her summon: a gate dismisses it (realm_summon.cpp). A Tab trip carries it.
        if (!arriveSummon_) now.hero.summonSkill = 0;
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
    // And CreateShiny's star: Shiny01, the showing table's "shiny".
    const content::EffectSheet* shiny = world_.played().showing().table().effect("shiny");
    litter_.open(&itemModels_, &world_.ground(),
                 core::fileExists(beam) ? ctx.textures.load(beam, content::TextureRole::Albedo)
                                        : bgfx::TextureHandle BGFX_INVALID_HANDLE,
                 shiny ? ctx.textures.load(core::join(ctx.paths.assets, shiny->path),
                                           content::TextureRole::Albedo)
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
    hudHidden_ = ctx.args.noHud;  // --no-hud: a scripted picture's F11
    core::Args& args = ctx.args;
    const std::string& assets = ctx.paths.assets;

    readSave(ctx);
    if (!args.script.empty() && !director_.open(args.script)) return false;
    if (!args.strideLog.empty() && !(strideLog_ = std::fopen(args.strideLog.c_str(), "w"))) {
        core::logError("cannot write --stride-log %s", args.strideLog.c_str());
    }

    // The world's own light over the base sheet -- Noria's tropical day over Lorencia's night --
    // or the base alone when the world has no sheet (game/world/maps.h).
    ctx.time.setScene(game::mapSheet(ctx.paths.sheets, args.world));
    // And its rain, when it has one: content/sheets/worlds/<world>_rain.json, blended in by the share.
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
            if (!args.arena.empty() || args.peaceful || args.arenaLearn != 0 || !args.teach.empty() || args.spendEven || args.arenaUndying ||
                args.wingDemo || args.unseen) {
                game::Play::Arena arena;
                arena.breed = args.arena;
                arena.count = args.arenaCount;
                arena.learn = args.arenaLearn;
                arena.teach.assign(args.teach.begin(), args.teach.end());
                arena.spendEven = args.spendEven;
                arena.undying = args.arenaUndying;
                arena.unseen = args.unseen;
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
            if (args.squarePeriod > 0) {
                config.square = {args.squarePeriod, args.squarePeriod / 2, args.squarePeriod / 2};
            }
            config.questDemo = args.questDemo;
            world_.played().configure(config);
            world_.played().setSyncGear(args.syncGear);
            if (args.benchPlayers > 0) {
                std::vector<sim::RaiderKit> kits;
                const std::string party = core::folders().content + "/raid/party.json";
                if (!sim::readParty(party, &kits)) core::logError("--bench-players: %s did not read", party.c_str());
                world_.played().setBench(args.benchPlayers, std::move(kits), args.benchFight, args.benchParty);
            }
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
                world_.played().stallLine(args.stallMs, args.stallEvery, args.coast);
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
            world_.played().rite().open(assets, ctx.textures);
            if (world_.played().showing().isOpen()) {
                if (const content::EffectSheet* light =
                        world_.played().showing().table().effect("light")) {
                    world_.played().setFolkLight(ctx.textures.load(
                        core::join(assets, light->path), content::TextureRole::Albedo));
                }
                const content::Showing& shown = world_.played().showing().table();
                if (const content::EffectSheet* flare = shown.effect("flare")) {
                    world_.played().setFolkFlare(ctx.textures.load(
                        core::join(assets, flare->path), content::TextureRole::Albedo));
                }
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
                world_.played().blizzard().open(assets, ctx.textures,
                                                world_.played().showing().table(),
                                                &world_.ground());
                world_.played().weakness().open(assets, ctx.textures,
                                                world_.played().showing().table(),
                                                &world_.ground());
                world_.played().pollution().open(assets, ctx.textures,
                                                 world_.played().showing().table(),
                                                 &world_.ground());
                world_.played().drain().open(assets, ctx.textures,
                                             world_.played().showing().table());
                world_.played().ravenShow().open(assets, ctx.textures,
                                                 world_.played().showing().table());
                world_.played().petLights().open(assets, ctx.textures,
                                                 world_.played().showing().table());
                world_.played().requiem().open(assets, ctx.textures,
                                               world_.played().showing().table(),
                                               &world_.ground());
                world_.played().sleep().open(assets, ctx.textures,
                                             world_.played().showing().table(),
                                             &world_.ground());
                world_.played().chains().open(assets, ctx.textures,
                                              world_.played().showing().table());
                world_.played().stains().open(assets, ctx.textures,
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
                world_.played().fireSlash().open(assets, ctx.textures,
                                                 world_.played().showing().table(),
                                                 &world_.ground());
                world_.played().powerSlash().open(assets, ctx.textures,
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
                world_.played().force().open(assets, ctx.textures, &world_.ground());
                world_.played().fireBurst().open(assets, ctx.textures, &world_.ground());
                world_.played().electricSpike().open(assets, ctx.textures, &world_.ground());
                world_.played().criticalDamage().open(assets, ctx.textures, &world_.ground());
                world_.played().recall().open(assets, ctx.textures, &world_.ground());
                world_.played().fireScream().open(assets, ctx.textures, &world_.ground());
                world_.played().earthshake().open(assets, ctx.textures, world_.played().showing().table(),
                                                  &world_.ground());
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
            // A tape is heard whole whatever --mute says: it plays into no room.
            world_.played().openSound(assets, args.mute && args.tape.empty(),
                                      !args.tape.empty());
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
            // The Helper's settings, kept beside the save as Name.helper (game/helper.h).
            {
                game::HelperSettings helper;
                game::loadHelper(savePath_, helper);
                desk_.setHelperSettings(helper);
                helper_.keepLog(savePath_, game::worldShow(args.world).name);
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
    }, look_);

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
        desk_.setHeroName(saved_.name);
        // Switch Character goes back to the screen this run came through, and only then.
        desk_.allowSwitch(args.lobby);
        desk_.setRealm(args.server);
        fillSettings(ctx.window, args, &desk_.settings());
        const bool held = args.frames == 0;
        desk_.holdEscape(held);
        ctx.window.holdEscape(held);
        // What's New, the first time a player enters the world after an update: only a played
        // run (mu play, the launcher) keeps options.txt, so no review or measurement raises it.
        // Marked seen at once, so a map change in the same run does not raise it again; written
        // to options.txt when it goes down.
        if (args.remember && args.frames == 0 && args.whatsNew < game::kWhatsNewEdition) {
            desk_.showWhatsNew();
            args.whatsNew = game::kWhatsNewEdition;
            core::logf("window: what's new up, edition %d", game::kWhatsNewEdition);
        }
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
    // Into a square by Charon: which one, and its fight on the gate's hour.
    if (world_.played().isOpen() && ctx.squareNext > 0) {
        world_.played().setSquare(ctx.squareNext, ctx.squareNextWait);
        ctx.squareNext = ctx.squareNextWait = 0;
    }
    if (world_.played().isOpen() && args.squareOpen) world_.played().openSquareDoor();
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
    if (world_.played().isOpen() && args.squareAt >= 0) world_.played().jumpSquare(args.squareAt);
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
    if (world_.played().isOpen() && args.autoHealth >= 0) {
        world_.played().setAutoDrink(args.autoHealth, args.autoMana);
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
        // --auto-play: the quest log's button pressed for him, once.
        if (args.autoPlay >= 0 && args.autoPlay < sim::kQuests) {
            ctx.autoQuest = args.autoPlay;
            args.autoPlay = -1;
        }
        // --quest-progress: one quest part-way, for a recording's montage.
        if (const size_t colon = args.questProgress.find(':'); colon != std::string::npos) {
            const int q = std::atoi(args.questProgress.c_str());
            if (q >= 0 && q < sim::kQuests) {
                sim::HeroRecord part = world_.played().record();
                sim::QuestProgress& one = part.quests[q];
                one = sim::QuestProgress{};
                bool met = true;
                int step = 0;
                for (const char* at = args.questProgress.c_str() + colon + 1;
                     *at && step < sim::kQuestSteps; ++step) {
                    one.counts[step] = uint16_t(std::atoi(at));
                    met = met && one.counts[step] >= world_.played().realm().questGoal(q, step);
                    while (*at && *at != ',') ++at;
                    if (*at == ',') ++at;
                }
                one.state = met ? sim::QuestState::Ready : sim::QuestState::Active;
                world_.played().restore(part);
            }
        }
        if (!args.talk.empty()) world_.played().talkTo(args.talk);
        if (args.perch >= 0) world_.played().perch(args.perch);
        // Once: a gate's next world is opened on these same arguments.
        if (args.walkColumn >= 0) world_.played().walkTo(args.walkColumn, args.walkRow);
        args.walkColumn = -1;
    }
    if (args.windows.find("inventory") != std::string::npos) desk_.setInventoryOpen(true);
    if (args.windows.find("character") != std::string::npos) desk_.setCharacterOpen(true);
    if (args.windows.find("inspect") != std::string::npos) desk_.setInspectFirst(true);
    if (args.windows.find("sanctuary") != std::string::npos) desk_.setSpecimenOpen(true);
    if (args.windows.find("outdated") != std::string::npos) {
        desk_.showLost(true);
    } else if (args.windows.find("lost") != std::string::npos) {
        desk_.showLost(false);
    } else if (args.windows.find("graphics") != std::string::npos) {
        desk_.showMenu(game::Menu::Page::Graphics);
    } else if (args.windows.find("options") != std::string::npos) {
        desk_.showMenu(game::Menu::Page::Options);
    } else if (args.windows.find("menu") != std::string::npos) {
        desk_.showMenu(game::Menu::Page::Main);
    }
    if (args.windows.find("whatsnew") != std::string::npos) desk_.showWhatsNew();
    if (args.windows.find("quests") != std::string::npos) desk_.showQuestLog();
    if (args.windows.find("helper") != std::string::npos) desk_.showHelper();
    if (args.windows.find("talents") != std::string::npos) desk_.showTalents();
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
    // The shadow probe's csv. docs/reference/shadow-probe.md reads it.
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

// F12: the frame as presented, with the HUD or without as F11 left it, into the player's own
// screenshots folder beside their options (core::userFolder), named by the time it was taken.
// bgfx writes it when the frame ends (gfx::Callback::screenShot), a PNG at the window's size.
void PlayMode::takeShot() {
    const std::string folder = core::userFolder() + "/screenshots";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", std::localtime(&now));
    std::string path = folder + "/MU Classic " + stamp + ".png";
    for (int n = 2; std::filesystem::exists(path, error) && n < 100; ++n) {
        path = folder + "/MU Classic " + stamp + " (" + std::to_string(n) + ").png";
    }
    bgfx::requestScreenShot(gfx::screenTarget(), path.c_str());
    core::logf("screenshot asked: %s", path.c_str());
}

void PlayMode::frame(Context& ctx, const Frame& at) {
    core::Args& args = ctx.args;
    const double deltaSeconds = at.deltaSeconds;
    if (args.lobby && args.lobbyBack >= 0 && at.index >= args.lobbyBack) backNow_ = true;
    // A world opened on a server whose join was refused -- a reconnect the server would not
    // have yet, a map change into a restarted one -- has no realm to play: the character screen,
    // which says what the server says, rather than a still world with nothing in it.
    if (args.lobby && !args.server.empty() && !world_.played().isOpen() && travelTo_.empty() && !backNow_) {
        core::logError("play: the server would not have him in %s; back to the character screen",
                       args.world.c_str());
        backNow_ = true;
    }

    // --invite: whoever named is not in his party yet is asked again, every three seconds.
    if (!args.invite.empty() && world_.played().isOpen() && world_.played().remote() &&
        (invited_ += deltaSeconds) >= 3.0) {
        invited_ = 0.0;
        const net::PartyState& party = world_.played().party();
        for (const std::string& name : args.invite) {
            const bool in = std::any_of(party.members.begin(), party.members.end(),
                                        [&](const net::PartyState::Member& m) { return m.name == name; });
            if (!in) world_.played().askParty({net::PartyWhat::Invite, name});
        }
    }

    rejoin(ctx, deltaSeconds);

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
                if (sim::squareEnterGateNumbered(number)) {
                    ctx.squareNext = world_.played().realm().squarePassed();
                    ctx.squareNextWait = world_.played().realm().squarePassedWait();
                }
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
            arriveSummon_ = true;  // her summon comes too (Realm::travel)
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
        // --script: the director's hand, and the mouse's not at all, buttons included.
        Director::Hand hand;
        const bool directed = director_.on();
        if (directed) {
            const content::Ground& land = world_.ground();
            float viewProjNow[16];
            bx::mtxMul(viewProjNow, view, proj);
            const float w = float(ctx.window.width()), h = float(ctx.window.height());
            Director::Eyes eyes;
            eyes.toScreen = [&](float column, float row, float* x, float* y) {
                const float metres = land.metresPerTile();
                const float wx = column * metres, wz = -row * metres;
                const float world4[4] = {wx, land.heightAt(wx, wz), wz, 1.0f};
                float clip[4];
                bx::vec4MulMtx(clip, world4, viewProjNow);
                if (clip[3] <= 0.0f) return false;
                *x = (clip[0] / clip[3] * 0.5f + 0.5f) * w;
                *y = (0.5f - clip[1] / clip[3] * 0.5f) * h;
                return true;
            };
            const sim::Realm& realm = world_.played().realm();
            eyes.hero = realm.hero().id;
            eyes.nearestMonster = [&]() {
                const sim::Body& me = realm.hero();
                uint32_t id = 0;
                float best = 1e9f;
                for (const sim::Body& body : realm.bodies()) {
                    if (!body.monster() || !body.alive()) continue;
                    const float dx = body.x - me.x, dy = body.y - me.y;
                    if (dx * dx + dy * dy < best) {
                        best = dx * dx + dy * dy;
                        id = body.id;
                    }
                }
                return id;
            };
            eyes.nearestElite = [&]() {
                const sim::Body& me = realm.hero();
                uint32_t id = 0;
                float best = 1e9f;
                for (const sim::Body& body : realm.bodies()) {
                    if (!body.elite || !body.alive()) continue;
                    const float dx = body.x - me.x, dy = body.y - me.y;
                    if (dx * dx + dy * dy < best) {
                        best = dx * dx + dy * dy;
                        id = body.id;
                    }
                }
                return id;
            };
            eyes.shownAt = [&](uint32_t id, float* column, float* row) {
                return world_.played().shownAt(id, column, row);
            };
            eyes.nearestLoot = [&](float* column, float* row) {
                const sim::Body& me = realm.hero();
                float best = 12.0f * 12.0f;
                bool found = false;
                for (const sim::Lying& one : realm.lying()) {
                    const float dx = float(one.column) - me.x, dy = float(one.row) - me.y;
                    if (dx * dx + dy * dy < best) {
                        best = dx * dx + dy * dy;
                        *column = float(one.column);
                        *row = float(one.row);
                        found = true;
                    }
                }
                return found;
            };
            hand = director_.step(at.elapsed, w, h, eyes);
            pointerX = hand.x;
            pointerY = hand.y;
            game::Play& played = world_.played();
            for (const std::string& one : hand.wear) {
                // NAME:COUNT:EXTRAS, as --give takes it.
                const size_t first = one.find(':');
                const size_t second = first == std::string::npos ? first : one.find(':', first + 1);
                const std::string name = one.substr(0, first);
                const int count = first == std::string::npos ? 1 : std::atoi(one.c_str() + first + 1);
                const std::string extras = second == std::string::npos ? "W" : one.substr(second + 1);
                played.give(name, std::max(1, count), extras);
            }
            // After the wear: in Icarus the Dinorant comes off only once the wings are on, since
            // the realm will not take away the last thing holding him up (Realm::moveItem).
            if (hand.unwear >= 0) played.takeOff(hand.unwear);
            if (hand.flare) played.rise();
            if (hand.walk) played.walkTo(hand.walkColumn, hand.walkRow);
            if (hand.pin) {
                game::Play::Stride pin;
                pin.clip = hand.pinClip;
                pin.hero = hand.pinHero;
                pin.beat = hand.pinBeat;
                pin.pulse = hand.pinPulse;
                pin.gallop = hand.pinGallop;
                played.pinStride(pin);
            }
        }
        // A --ui-click no window takes is the world's, as a hand's is: aimed where it presses.
        bool uiPress = false, uiRight = false;
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
            if (directed) {
                game::Pointer p;
                p.x = hand.x;
                p.y = hand.y;
                p.pressed = hand.pressed;
                p.released = hand.released;
                p.held = hand.held;
                p.rightPressed = hand.rightPressed;
                desk_.scriptHand(p);
                if (hand.potion >= 0) desk_.scriptKey(hand.potion, hand.shifted);
                if (hand.skill >= 0) desk_.scriptSkill(hand.skill);
                if (hand.holdSkill >= 0) desk_.scriptHold(hand.holdSkill);
                if (!hand.typed.empty()) desk_.scriptType(hand.typed);
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
            for (const core::Args::HoverAt& one : args.hoversAt) {
                if (at.index == one.frame) args.hoverX = one.x, args.hoverY = one.y;
            }
            if (args.hoverX >= 0.0f) desk_.script(args.hoverX * w, args.hoverY * h, false, false);
            // --rite: the Rebirth rite's show where he stands, on its frame (fx/rite.h).
            if (at.index == args.riteFrame && world_.played().isOpen()) world_.played().startRite();
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
            for (int frame : args.uiPicks) {
                if (at.index == frame && world_.played().isOpen()) world_.played().pickNearest(true);
            }
            for (const core::Args::UiClick& c : args.uiClicks) {
                const bool drag = c.x2 != c.x || c.y2 != c.y;
                const int last = c.frame + (drag ? 3 : 1);
                if (at.index < c.frame || at.index > last) continue;
                // Pressed at the first point, carried to the second, let go there.
                const bool here = at.index == c.frame;
                desk_.script((here ? c.x : c.x2) * w, (here ? c.y : c.y2) * h, here,
                             at.index == last, c.right, c.shift);
                if (here) {
                    pointerX = c.x * w;
                    pointerY = c.y * h;
                    uiPress = true;
                    uiRight = c.right;
                }
            }
            for (const auto& [frame, text] : args.uiTyped) {
                if (at.index == frame) desk_.scriptType(text);
            }
            if (args.chatSample && at.index == 2) desk_.sampleChat();
            feedLine(ctx, float(deltaSeconds));
            desk_.update(float(deltaSeconds), ctx.window, world_.played(), pointerX, pointerY);
            // The chip's list opened: the regions timed to fill it, once a run. Its note closed:
            // never again on this machine.
            if (desk_.takeLineOpened()) net::pingAllOnce();
            if (desk_.takeLineDismissed()) {
                args.pingNoteClosed = true;
                core::saveOptions(args);
                core::logf("window: the nearer region's note closed for good");
            }
            // A bug report written: sent on a thread, and its answer back to the window.
            if (game::Desk::BugAsk ask; desk_.takeReport(&ask)) sendBug(ctx, ask);
            if (bugSending_) {
                std::lock_guard<std::mutex> hold(bugSending_->lock);
                if (bugSending_->done) {
                    desk_.reportAnswered(bugSending_->sent, bugSending_->why);
                    bugSending_.reset();
                }
            }
            // A box that has the keyboard has Escape too, which otherwise quits.
            ctx.window.setTyping(desk_.typing());
            // What the menu's Options changed: the display, the window's size, v-sync, the
            // volume and the counter in the corner. Kept in options.txt when mu play ran us
            // (--remember). The window's new size reaches the renderer on the next pump.
            if (desk_.takeWhatsNewSeen()) {
                args.whatsNew = game::kWhatsNewEdition;
                core::saveOptions(args);
            }
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
                              ctx.window.width(), ctx.window.height(), ctx.window.alt());
        // A name plate over the drop outranks what the ray found behind it, a monster included.
        if (desk_.ready() && !windowed) {
            world_.played().pointAtLabel(desk_.labelUnder(pointerX, pointerY));
        }
        const bool mouseLeft = directed ? hand.pressed : ctx.window.clicked(0);
        const bool mouseRight = directed ? hand.rightPressed : ctx.window.clicked(1);
        const bool mouseRightHeld = directed ? hand.rightHeld : ctx.window.held(1);
        const bool mouseLeftHeld = directed ? hand.held : ctx.window.held(0);
        // **Auto-play** (game/quest_auto.h): asked or stopped on the quest log, and stopped by a
        // click of his own on the world -- the hero is his again.
        if (const int asked = desk_.takeAutoPlay(); asked != -2) {
            if (asked >= 0) {
                ctx.autoQuest = asked;
                world_.played().notice(std::string("Auto-play: ") + sim::questAt(asked).title + ".", false);
            } else if (ctx.autoQuest >= 0) {
                ctx.autoQuest = -1;
                world_.played().notice("Auto-play stopped.", false);
            }
        }
        if (ctx.autoQuest >= 0 && !windowed && (mouseLeft || mouseRight)) {
            ctx.autoQuest = -1;
            world_.played().notice("Auto-play stopped.", false);
        }
        // **The Helper** (game/helper.h): started and stopped by H, the minimap's H and its
        // window, stopped by a click of his own on the world, as Auto-play is; the two never run
        // together. In Blood Castle and Devil Square its Follow my quest is set aside: it hunts
        // where he stands, as with the box unticked, and travels nowhere (the user, 2026-10-25:
        // "dont teleport character away from instances if follow quest is enabled").
        game::HelperSettings helping = desk_.helperSettings();
        if (world_.played().isOpen() && world_.played().realm().event() != sim::EventKind::None) {
            helping.quest = false;
        }
        int helperAsked = desk_.takeHelperAsk();
        helperClock_ += deltaSeconds;
        if (ctx.args.helperAfter >= 0.0f && helperClock_ >= ctx.args.helperAfter) {
            ctx.args.helperAfter = -1.0f;
            helperAsked = 1;
        }
        // Following a quest, it goes on from the map it travelled from, where it left off.
        if (ctx.helperFollows && helperAsked == -2 && !helper_.on() && world_.played().isOpen()) {
            ctx.helperFollows = false;
            game::Helper::Stats carried;
            carried.seconds = ctx.helperSeconds;
            carried.kills = ctx.helperKills;
            carried.picked = ctx.helperPicked;
            carried.quests = ctx.helperQuests;
            if (ctx.autoQuest < 0 && desk_.helperSettings().quest) helper_.start(world_.played(), &carried);
        }
        if (const int asked = helperAsked; asked != -2 && world_.played().isOpen()) {
            const game::Play& played = world_.played();
            const sim::Body& hero = played.realm().hero();
            if (asked == 0) {
                if (helper_.on()) world_.played().notice("Helper stopped.", false);
                helper_.stop();
            } else if (!helping.quest && played.realm().tables()->grid.safe(hero.column(), hero.row())) {
                world_.played().notice("The Helper works outside safe zones.", true);
                core::logf("helper: not started on a safe tile, %d,%d", hero.column(), hero.row());
            } else if (hero.alive()) {
                if (ctx.autoQuest >= 0) ctx.autoQuest = -1;
                helper_.start(played);
                world_.played().notice("Helper started.", false);
            }
        }
        // A warp on the same map -- a floor's gate, a Tab trip to another floor, Go Back! -- stops
        // it, as ReceiveTeleport's g_MuHelper.TriggerStop() does on every move (bug #51,
        // 2026-10-21: "it remains active when switching maps"); its spot is on the floor left.
        // Following a quest, the travel is its own.
        if (world_.played().isOpen() && world_.played().takeLanding() && helper_.on() &&
            !desk_.helperSettings().quest) {
            helper_.stop("You left the map");
            world_.played().notice("Helper stopped.", false);
        }
        // A scripted press on the world (--ui-click) counts as his own click.
        if (helper_.on() && (ctx.autoQuest >= 0 || (!windowed && (mouseLeft || mouseRight || uiPress)))) {
            helper_.stop();
            world_.played().notice("Helper stopped.", false);
        }
        if (desk_.takeHelperChanged()) game::writeHelper(savePath_, desk_.helperSettings());
        if (desk_.takeHelperClear()) helper_.clearLog();
        if ((mouseLeft && !windowed) || clickNow || (uiPress && !uiRight && !windowed)) {
            world_.played().leftClick();
        } else if (mouseLeft || !mouseLeftHeld) {
            // A press on a window, or the button up: nothing held walks on.
            world_.played().leftLetGo();
        } else if (!windowed) {
            world_.played().leftHeld();
        }
        if (ctx.window.down(gfx::Window::Key::Pick)) {
            world_.played().pickNearest(ctx.window.pressed(gfx::Window::Key::Pick));
        }
        if ((mouseRight || (uiPress && uiRight)) && !windowed) {
            world_.played().rightClick();
        } else if (mouseRightHeld && !windowed) {
            world_.played().rightHeld();
        }
        // Not in Blood Castle or Devil Square (a player, 2026-10-21: "deactivating the auto quest
        // function while you are in devil square or blood castle since it can take you out of it"):
        // Auto-play travels by the Move list, and would walk him out of his run. The Helper hunts
        // on there, its quest set aside (`helping`, above).
        if (world_.played().isOpen() && world_.played().realm().event() != sim::EventKind::None &&
            ctx.autoQuest >= 0) {
            ctx.autoQuest = -1;
            questAuto_.reset();
            world_.played().notice("Auto-play is off in Blood Castle and Devil Square.", true);
        }
        if (ctx.autoQuest >= 0 || (helper_.on() && helping.quest)) {
            game::quest_world::open(ctx.paths.assets);  // once, where it lives
        }
        int32_t bar[game::Hud::kSkillBoxes] = {};
        for (int k = 0; k < game::Hud::kSkillBoxes; ++k) bar[k] = desk_.bound(k);
        questAuto_.pickBoxes((desk_.helperSettings().picks & game::HelperSettings::kPickBoxes) != 0);
        if (const std::string said = questAuto_.update(world_.played(), &ctx.autoQuest, deltaSeconds, bar,
                                                       game::Hud::kSkillBoxes);
            !said.empty()) {
            const bool done = said.size() > 10 && said.compare(said.size() - 10, 10, " complete.") == 0;
            world_.played().notice(said, !done);
        }
        desk_.setAutoPlaying(ctx.autoQuest);
        for (const game::Play::Picked& one : world_.played().takePicked()) helper_.picked(world_.played(), one.what, one.zen);
        if (const std::string said = helper_.update(world_.played(), helping, deltaSeconds, bar,
                                                    game::Hud::kSkillBoxes, desk_.shownQuest());
            !said.empty()) {
            const bool done = said.size() > 10 && said.compare(said.size() - 10, 10, " complete.") == 0;
            world_.played().notice(said, !done);
        }
        if (world_.played().isOpen()) {
            ctx.helperFollows = helper_.on() && desk_.helperSettings().quest;
            ctx.helperSeconds = helper_.stats().seconds;
            ctx.helperKills = helper_.stats().kills;
            ctx.helperPicked = helper_.stats().picked;
            ctx.helperQuests = helper_.stats().quests;
        }
        desk_.setHelping(helper_.on(), helper_.stats(), &helper_.log());
        world_.played().update(deltaSeconds);
        // --stride-log: the clocks a recording's cut carries, as this frame is drawn.
        if (strideLog_) {
            const game::Play::Stride now = world_.played().stride();
            std::fprintf(strideLog_, "%d %d %.6f %.6f %.6f %.6f\n", at.index, now.clip, now.hero,
                         now.beat, now.pulse, now.gallop);
        }
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
    // texel of 29 mm. docs/reference/shadow-probe.md.
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
        // And Rageful Blow's crater, which shakes it the same way (fx/fury.h), Hellfire's
        // wall (fx/hellfire.h) and Hell Maine's storm (fx/hell_maine.h).
        const float pitch = world_.played().meteor().quakeDegrees() +
                            world_.played().fury().quakeDegrees() +
                            world_.played().earthshake().quakeDegrees() +
                            world_.played().hellfire().quakeDegrees() +
                            world_.played().shadowStars().hellMaine().quakeDegrees();
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
    // And Devil Square's match (the user, 2026-10-20: "lets use dragon raid music for devil suare
    // also"), the song begun from its top 39.05 s before the fight, each wave and the bosses
    // (0, 5, 12 and 15 minutes in, sim/square.h), so its drop -- the bass in full after its
    // build-up and two-second riser -- lands as they rise ("time this music with exact moment
    // when song skips the intro and active part starts", "song has to start from begining so
    // there is same effect when monsters appear"). Between, it plays on: the square's cut of the
    // track is 21:12 ("2 minute is quite short DS ir pretty long event"), its 0-104.25 s, then
    // the 32 bars from 40.25 s to 104.25 s eighteen times over, then its outro, each seam a 30 ms
    // crossfade on a bar line where the music matches itself (120 bpm, 16-bar phrases of 32.00 s).
    // Who comes in between hears it from the top too, in step again at the next wave. Never for
    // the dead, nor once he is sent out of the square ("if char dies music has to stop").
    constexpr int64_t kFightDropTicks = int64_t(39.05 * sim::kSquareTicksPerSecond);
    const sim::SquareRun& square = world_.played().realm().squareRun();
    // The fight's clock as the waves keep it (SquareInstance::tick): from its end back, which
    // --square-at moves; startsAt only in the wait.
    const int64_t fightStart = square.phase == sim::SquarePhase::Running
                                   ? square.endsAt - int64_t(sim::kSquarePlay) * sim::kSquareTicksPerSecond
                                   : square.startsAt;
    const int64_t fightTick = world_.played().realm().tick() - fightStart;
    const bool squareFighting =
        world_.played().isOpen() && world_.played().realm().event() == sim::EventKind::Square &&
        world_.played().realm().hero().alive() &&
        (square.phase == sim::SquarePhase::Running ||
         (square.phase == sim::SquarePhase::Waiting && fightTick >= -kFightDropTicks));
    // The latest of the drops whose build-up has begun: the cue the song was last begun on.
    int squareCue = -1;
    if (squareFighting) {
        const int drops[] = {sim::kSquareWaveMinutes[0][0] * 60, sim::kSquareWaveMinutes[1][0] * 60,
                             sim::kSquareWaveMinutes[2][0] * 60, sim::kSquareBossMinute * 60};
        for (int k = 0; k < 4; ++k) {
            if (fightTick >= int64_t(drops[k]) * sim::kSquareTicksPerSecond - kFightDropTicks) squareCue = k;
        }
    }
    static int64_t cuedRun = -1;  // the run (its fight's first tick) and cue the song was last begun on
    static int cued = -1;
    const std::string fightTrack =
        ctx.paths.assets + (squareFighting ? "/music/devil_square.mp3" : "/music/dragon_fight.mp3");
    if (world_.played().isOpen() && (world_.played().raidFighting() || squareFighting) &&
        core::fileExists(fightTrack)) {
        game::Sound& sound = world_.played().sound();
        if (squareFighting && squareCue >= 0 && (cuedRun != fightStart || cued != squareCue)) {
            // A wave's build-up: from the top again, the last bars fading under it.
            const bool first = cuedRun != fightStart || !sound.musicPlaying(fightTrack);
            cuedRun = fightStart;
            cued = squareCue;
            if (!first) sound.stopMusic();
        }
        if (!sound.musicPlaying(fightTrack)) sound.music(fightTrack, 0.8f, !squareFighting);
        sound.stopBed();
    } else if (world_.played().isOpen()) {
        // The world's track and where it plays are its manifest's `music`
        // (game/world/world_show.h): in the safe zone, under a roof, both, or everywhere.
        const game::WorldShow& show = game::worldShow(args.world);
        const std::string& where = show.musicWhere;
        const sim::Body& hero = world_.played().realm().hero();
        const content::Tables* tables = world_.played().realm().tables();
        const bool safe = tables && tables->grid.safe(hero.column(), hero.row());
        float feetX = 0.0f, feetZ = 0.0f;
        world_.characterAt(&feetX, &feetZ);
        const bool roofed = world_.indoors(feetX, feetZ);
        bool pub = !show.music.empty() &&
                   (where == "everywhere" || (where == "safe" && safe) ||
                    (where == "indoors" && roofed) || (where == "safe_indoors" && safe && roofed));
        // A wet spell's own track, a bed under the world's, which goes on as it would
        // (world_show.h musicRain); a roof that has its own music keeps it alone.
        const bool roofMusic = pub && (where == "indoors" || where == "safe_indoors");
        const bool rainy = !show.musicRain.empty() && world_.weather().wet() && !roofMusic;
        game::Sound& sound = world_.played().sound();
        if (rainy) {
            sound.bed(ctx.paths.assets + show.musicRain, 0.6f);
        } else {
            sound.stopBed();
        }
        // Out on the hunt, where the world's own track is not playing: its `wild` loop.
        const bool wild = !show.musicWild.empty() && !safe && !pub;
        if (wild) pub = true;
        const bool loops = wild || show.musicLoops;  // round and round, not once and rested
        const char* roofTrack = wild ? show.musicWild.c_str() : show.music.c_str();
        // No music out on the hunt, and none for fights (the user, 2026-09-30: "we dont need
        // fight music anymore").
        const std::string path = ctx.paths.assets + (pub ? roofTrack : "");
        // He is back in the safe zone: the wild loop goes now, not when the world's own track
        // has rested.
        if (!wild && !show.musicWild.empty() && sound.musicPlaying(ctx.paths.assets + show.musicWild)) {
            sound.stopMusic();
        }
        if (pub && sound.musicPlaying(path)) musicHeard()[path] = runSeconds();
        if (pub && loops && core::fileExists(path) && !sound.musicPlaying(path)) {
            sound.music(path, 0.6f, true);
        } else if (pub && core::fileExists(path) && !sound.musicPlaying(path)) {
            // Once through, and only when it has rested: the world's own `rest` after it ended
            // (the Dungeon's, the user, 2026-10-16: "make some intervals after the music plays
            // so it dont get to repeatative"), or the towns' kMusicRestSeconds since it began.
            const double now = runSeconds();
            const auto last = musicBegan().find(path);
            const auto heard = musicHeard().find(path);
            const bool rested = show.musicRest >= 0.0f
                                    ? heard == musicHeard().end() || now - heard->second >= show.musicRest
                                    : last == musicBegan().end() || now - last->second >= kMusicRestSeconds;
            if (rested) {
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
            &world_, world_.town().isOpen() ? &world_.town().cooked() : nullptr);
    }
    // The lamps flicker, the fires burn, and the glows' levels go into the town before
    // it is gathered, since each rides in its instance. Sprint 8a.
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
        // Gathered into a pool larger than the four the renderer takes, and the four that matter to
        // the hero chosen after (keepNearest): filled first come, first served, a far bot's
        // meteor took a slot before the hero's own bolt, and with twenty bots fighting the
        // lights round him popped in and out with whatever happened to be cast first.
        gfx::PointLight falling[kGatheredLights];
        uint32_t count =
            world_.played().meteor().lights(falling, kGatheredLights);
        // And Cometfall's comets, MU's blue under each as it falls and where it lands.
        count += world_.played().comet().lights(falling + count,
                                                kGatheredLights - count);
        // And the wizard's bolts, the blue each throws on the ground it crosses.
        count += world_.played().bolt().lights(falling + count,
                                               kGatheredLights - count);
        // And Power Wave's, three tiles of blue.
        count += world_.played().wave().lights(falling + count,
                                               kGatheredLights - count);
        count += world_.played().thunder().lights(falling + count,
                                                  kGatheredLights - count);
        // And the Rebirth rite's light from heaven, which lights him (fx/rite.h).
        count += world_.played().rite().lights(falling + count, kGatheredLights - count);
        // And a Poison cloud's green miasma, two tiles.
        count += world_.played().poison().lights(falling + count,
                                                 kGatheredLights - count);
        // And a Flame's orange, three tiles.
        count += world_.played().flame().lights(falling + count,
                                                kGatheredLights - count);
        // And Twisting Slash's wheel, the grey MU lays under each copy, as one.
        count += world_.played().wheel().lights(falling + count,
                                                kGatheredLights - count);
        // And Rageful Blow's broken ground, MU's red under its fires, as one.
        count += world_.played().fury().lights(falling + count,
                                               kGatheredLights - count);
        // And Earthshake's spikes, MU's warm light under them, as one.
        count += world_.played().earthshake().lights(falling + count, kGatheredLights - count);
        // And Power Slash's waves, MU's blue under each (fx/power_slash.h).
        count += world_.played().powerSlash().lights(falling + count,
                                                     kGatheredLights - count);
        // And Fire Slash's warm light under its ribbon -- ours (fx/fire_slash.h).
        count += world_.played().fireSlash().lights(falling + count, kGatheredLights - count);
        // And Hellfire's wall, MU's orange four tiles round (fx/hellfire.h).
        count += world_.played().hellfire().lights(falling + count,
                                                   kGatheredLights - count);
        // And Summon's wall, white by its light four tiles round (fx/recall.h).
        count += world_.played().recall().lights(falling + count, kGatheredLights - count);
        // And Twister's storm, a faint cool light under it -- ours (fx/storm.h).
        count += world_.played().storm().lights(falling + count,
                                                kGatheredLights - count);
        // And Inferno's ring, one warm light for its blasts (fx/inferno.h).
        count += world_.played().inferno().lights(falling + count,
                                                  kGatheredLights - count);
        // And the Dinorant's breath, MU's cool blue under it (fx/firebreath.h).
        count += world_.played().fireBreath().lights(falling + count,
                                                     kGatheredLights - count);
        // And Icarus's flash, MU's dim yellow under him (game/world/sky_clouds.h).
        count += world_.skyClouds().lights(falling + count,
                                           kGatheredLights - count);
        // And Aqua Beam's line, three blue lights along it (fx/aqua.h).
        count += world_.played().aqua().lights(falling + count,
                                               kGatheredLights - count);
        // And a steel or saw bolt's faint cold light (fx/arrow.h; ours).
        count += world_.played().arrows().lights(falling + count,
                                                 kGatheredLights - count);
        // And a Fire Trap's burst on the floor, two tiles.
        count += world_.played().trapShow().lights(falling + count,
                                                   kGatheredLights - count);
        // And the nearest Poison Shadows' faint green (fx/shadow_stars.h; ours).
        count += world_.played().shadowStars().lights(
            falling + count, kGatheredLights - count, eye.target);
        // And the Heliacal Sword's warm pool round whoever holds it (fx/held_lights.h).
        count += world_.played().heldLights().lights(falling + count,
                                                     kGatheredLights - count);
        count += world_.played().gleam().lights(falling + count,
                                                kGatheredLights - count,
                                                daylightOf(ctx.lighting));
        // And last, what is left to the nearest jewels lying down (fx/litter.h; ours).
        count += litter_.lights(falling + count, kGatheredLights - count,
                                eye.target);
        count = keepNearest(falling, count, gfx::Renderer::kMaxTransientLights, eye.target);
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
    world_.throwAround(float(deltaSeconds), eye.target, ctx.renderer.effects());
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
    // And what the Dark Horse breathed under the sea (Play::horseBreath).
    for (const auto& at : world_.played().takeBreathBubbles()) world_.bubbles().throwOne(at.data());
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
        // Devil Square's rain, held the whole fight, eases out as the time is up.
        if (world_.played().realm().squareRun().phase == sim::SquarePhase::Ended) {
            world_.weather().dry();
        }
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
            ctx.renderer.setGpuScenery(&world_.town().gpuScenery());
            casters = &townCasters_;
        } else {
            world_.town().gatherAll(townDrawables_);
        }
    }
    // The birds cast as well. MU gives every boid outside Heaven a shadow of its own:
    // RenderBoids ends each bird with RenderBodyShadow, laid on the terrain under it at a
    // fifth black (GOBoid.cpp:1586). Here the sun's split carries it like any other caster; Icarus's
    // dragons cast none.
    if (!world_.played().invasionStorm()) {
        world_.boids().gather(townDrawables_);
        if (casters && world_.boids().castsShadow()) world_.boids().gather(townCasters_);
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
            // His target's face, off the pose just gathered (game/portrait.h).
            desk_.portrait(ctx.renderer, world_.played());
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
        world_.played().gatherStains(ctx.renderer.effects());
        world_.played().gatherAura(ctx.renderer.effects(), eye.position);
        world_.played().gatherWarp(ctx.renderer.effects());
        world_.played().gatherRite(ctx.renderer.effects(), eye.position);
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
        world_.played().blizzard().gather(ctx.renderer.effects());
        world_.played().weakness().gather(ctx.renderer.effects());
        world_.played().pollution().gather(ctx.renderer.effects());
        world_.played().drain().gather(ctx.renderer.effects());
        world_.played().ravenShow().gather(ctx.renderer.effects());
        world_.played().petLights().gather(ctx.renderer.effects());
        world_.played().requiem().gather(ctx.renderer.effects());
        world_.played().sleep().gather(ctx.renderer.effects());
        world_.played().chains().gather(ctx.renderer.effects(), eye.position);
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
        world_.played().fireSlash().gatherEffects(ctx.renderer.effects());
        world_.played().powerSlash().gatherEffects(ctx.renderer.effects());
        world_.played().impale().gatherEffects(ctx.renderer.effects());
        world_.played().force().gatherEffects(ctx.renderer.effects());
        world_.played().fireBurst().gatherEffects(ctx.renderer.effects());
        world_.played().electricSpike().gatherEffects(ctx.renderer.effects());
        world_.played().criticalDamage().gatherEffects(ctx.renderer.effects());
        world_.played().recall().gatherEffects(ctx.renderer.effects());
        world_.played().fireScream().gatherEffects(ctx.renderer.effects());
        world_.played().earthshake().gatherEffects(ctx.renderer.effects(), daylightOf(ctx.lighting),
                                                   eye.position);
        world_.played().fireBreath().gatherEffects(ctx.renderer.effects(), eye.position,
                                                   eye.target, daylightOf(ctx.lighting));
        // And what is lying on the grass: MU2's Drops, tossed up out of the corpse and
        // laid down where they land.
        openItems(ctx);
        litter_.update(world_.played().realm(), deltaSeconds, world_.played().heldDrops(),
                       world_.played().tossedDrops());
        world_.played().setSettledDrops(litter_.settled());
        for (uint32_t id : litter_.hopsLanded()) world_.played().hopLanded(id);
        litter_.gather(townDrawables_, casters ? &townCasters_ : nullptr);
        litter_.gatherBeams(ctx.renderer.effects(), eye.position);
        litter_.gatherSparkle(ctx.renderer.effects());
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
    // split. docs/reference/grass.md.
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
    if (ctx.window.pressed(gfx::Window::Key::HideHud)) {
        hudHidden_ = !hudHidden_;
        core::logf("HUD %s", hudHidden_ ? "hidden (F11 shows it)" : "shown");
    }
    if (!hoverDrawables_.empty() && !hudHidden_) {
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
    if (!flashes_.empty() && !hudHidden_) {
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
    if (desk_.ready() && !hudHidden_) desk_.submit(gfx::ViewHud, ctx.window.width(), ctx.window.height());
    // Over a played world the counter is the line's chip, top right (feedLine).
    if (args.fps && !hudHidden_ && !desk_.ready()) ctx.readout.draw(ctx.overlay, ctx.window.width(), ctx.window.height(),
                                                  world_.played().rttMs());
    // Asked now, taken by bgfx when this frame is presented: the picture is what is on screen.
    if (ctx.window.pressed(gfx::Window::Key::Shot)) takeShot();
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

void PlayMode::graphicsEased(Context& ctx, const char* to) {
    fillSettings(ctx.window, ctx.args, &desk_.settings());
    world_.played().notice(std::string("Graphics lowered to ") + to +
                                " to keep the game smooth. Options > Graphics changes it back.",
                            false);
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
    arriveSummon_ = false;
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

void PlayMode::rejoin(Context& ctx, double seconds) {
    game::Play& play = world_.played();
    if (!play.isOpen() || !play.cut() || !travelTo_.empty() || !desk_.ready()) return;
    const core::Args& args = ctx.args;
    // Told this game is another build than the server's: the page says update, and nothing knocks.
    if (outdated_) return;
    const double in = play.rejoinIn();
    if (in < 0.0) {
        desk_.rejoining(false, {});
        return;
    }
    // Reconnect pressed: knocks at once, though not before the server would have him.
    if (desk_.takeRejoin()) knockWait_ = 0.0;
    if (knock_) {
        const int said = knock_->said.load();
        if (said == 0) {
            desk_.rejoining(true, "Reconnecting...");
            return;
        }
        knock_.reset();
        if (said == 3) {
            core::logError("reconnect: the server is another build than this game's; update through the launcher");
            outdated_ = true;
            desk_.outdated();
            return;
        }
        if (said == 1) {
            const sim::HeroRecord hero = play.record();
            core::logf("reconnect: the server answers after %d knock(s); %s again at %d,%d", knocks_ + 1,
                       args.world.c_str(), hero.column, hero.row);
            travel(ctx, args.world, hero.column, hero.row, hero.facing);
            return;
        }
        ++knocks_;
        knockWait_ = kKnockEvery;
    }
    knockWait_ = std::max(0.0, knockWait_ - seconds);
    const double wait = std::max(in, knockWait_);
    if (wait > 0.0) {
        char note[64];
        std::snprintf(note, sizeof note, "Reconnecting in %d s...", int(std::ceil(wait)));
        desk_.rejoining(true, note, int(std::ceil(wait)));
        return;
    }
    const size_t colon = args.server.rfind(':');
    const bool hasPort = colon != std::string::npos;
    const std::string host = hasPort ? args.server.substr(0, colon) : args.server;
    const int port = hasPort ? std::atoi(args.server.c_str() + colon + 1) : net::kDefaultPort;
    if (knocks_ == 0) core::logf("reconnect: knocking on %s:%d", host.c_str(), port);
    knock_ = std::make_shared<Knock>();
    desk_.rejoining(true, "Reconnecting...");
    // The knock asks, as the character screen does, whether this game is the server's build (an
    // Account keyed net::kKnockKey, its items and rules: server/src/accounts.cpp outdated). A server
    // restarted on other rules under the running game answers Refused::Outdated, and the page
    // says update; going in would be a Hello refused and a world that never opens. Anything
    // else -- the line shut on the knock, or no word in a few seconds -- is the way in.
    // The prints are the lobby's, Lorencia's tables, as the server checks an Account
    // (server/src/accounts.cpp, g_items): this world's own, the Dungeon's say, is another print,
    // and the knock was told to update under a game that was the server's build (the user,
    // 2026-10-23: dual cut in the Dungeon, "showed there is updated launcher").
    if (!knockPrints_) {
        content::Tables lobby;
        std::string error;
        const std::string path = core::join(ctx.paths.assets, "cooked/lorencia/lorencia.mur");
        if (content::loadTables(path, lobby, error)) {
            knockItems_ = lobby.itemsPrint();
            knockRules_ = net::rulesPrint(lobby);
        } else {
            core::logError("reconnect: no lobby tables at %s (%s); knocking with this world's",
                           path.c_str(), error.c_str());
            knockItems_ = play.realm().tables()->itemsPrint();
            knockRules_ = net::rulesPrint(*play.realm().tables());
        }
        knockPrints_ = true;
    }
    net::Account probe;
    probe.key = net::kKnockKey;
    probe.items = knockItems_;
    probe.rules = knockRules_;
    std::thread([knock = knock_, host, port, probe] {
        net::Socket socket;
        std::string error;
        const bool up = socket.connect(host, port, kKnockEvery, error);
        if (!up) {
            core::logf("reconnect: no answer yet (%s)", error.c_str());
            knock->said = 2;
            return;
        }
        std::vector<uint8_t> out, in, body;
        net::put(out, probe);
        bool stale = false;
        if (socket.send(out) && socket.flush()) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (std::chrono::steady_clock::now() < until) {
                const bool open = socket.receive(in);
                net::Kind kind{};
                const int took = net::take(in, kind, body);
                net::Roster roster;
                if (took == 1 && kind == net::Kind::Roster && net::parse(body, roster)) {
                    stale = roster.refused == net::Refused::Outdated;
                    break;
                }
                if (took != 0 || !open) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
        knock->said = stale ? 3 : 1;
    }).detach();
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
        ctx.goBack.dismissedMap = -1;
        desk_.goBack(false, 0, false, {});
        return;
    }
    if (desk_.takeGoBack() && way.open()) {
        world_.played().goBack();
        desk_.goBack(false, 0, false, {});
        return;
    }
    if (desk_.takeGoBackDismissed()) {
        ctx.goBack.dismissedMap = way.map;
        ctx.goBack.dismissedColumn = way.column;
        ctx.goBack.dismissedRow = way.row;
        core::logf("go back: put away, %lld s left", (long long)(way.ticksLeft / 20));
    }
    if (ctx.goBack.dismissedMap == way.map && ctx.goBack.dismissedColumn == way.column &&
        ctx.goBack.dismissedRow == way.row) {
        desk_.goBack(false, 0, false, {});
        return;
    }
    // Up once the map's name has come and gone (game/ui/arrival.h's 5.4 s), so the two never
    // stand on the screen together; the clock is running from the landing all the same.
    std::string where = game::placeName(to->world, way.column, way.row);
    if (!where.empty()) where[0] = char(std::toupper(static_cast<unsigned char>(where[0])));
    where += " \xC2\xB7 " + std::to_string(way.column) + ", " + std::to_string(way.row);
    const bool shown = !way.open() || sinceLanded_ >= kGoBackWaits;
    desk_.goBack(shown, int((way.ticksLeft + 19) / 20), !way.open(), where);
}

void PlayMode::shutdown(Context& ctx) {
    if (strideLog_) std::fclose(strideLog_);
    strideLog_ = nullptr;
    // Out of the game or back to the character screen: the way back goes into the save, which
    // gives it back when he is played again, and out of the session. On to another world it
    // comes along.
    keep(ctx);
    if (!savePath_.empty()) core::logf("save: kept in %s", savePath_.c_str());
    // The Helper's log says how it ended; a followed quest's travel takes it on to the next map.
    if (helper_.on() && !(ctx.helperFollows && !travelTo_.empty())) {
        const bool cut = world_.played().isOpen() && world_.played().cut();
        helper_.stop(cut ? "Disconnected" : !travelTo_.empty() ? "You left the map" : "You left the world");
    }
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
    if (!travelTo_.empty()) desk_.carryChat();
    desk_.shutdown();
    world_.shutdown();
}

}  // namespace mu::app

namespace mu::app {

// **The line's chip** (the user, 2026-10-20, docs in game/ui/line_chip.h): the frame rate when
// Options' counter is on, the ping when there is a server, and his region among net::regions().
// A line slow for kSlowFor has every region timed once, in the background, and when one answers
// much sooner than his own (net::nearer) the chip's note names it, until he closes it for good.
void PlayMode::feedLine(Context& ctx, float seconds) {
    const core::Args& args = ctx.args;
    game::LineChip::Line& line = line_;
    line.here = line.nearer = -1;
    const int fps = ctx.readout.sample();
    line.fps = args.fps ? fps : -1;
    line.ping = world_.played().rttMs();
    if (line.ping >= 0.0f) {
        line.here = net::regionOfServer(args.server);
        if (!args.region.empty()) {
            for (size_t i = 0; i < net::regions().size(); ++i) {
                if (net::regions()[i].id == args.region) line.here = int(i);
            }
        }
        if (line.ping >= kSlowMs) slowFor_ += seconds;
        else if (line.ping < kSteadyMs) slowFor_ = 0.0f;
    }
    const bool slow = slowFor_ >= kSlowFor;
    if (slow && line.here >= 0 && !args.pingNoteClosed) net::pingAllOnce();
    if (net::pingAllTaken(&line.pings) && slow && !args.pingNoteClosed) {
        line.nearer = net::nearer(line.pings, line.here);
    }
    desk_.setLine(line);
}

// The player's bug report to the website, on a thread of its own: the game goes on while it is
// out, and the window says "Sending..." until the answer comes (game/ui/report.h). The site is
// the crash reports' (application.cpp): the player's build always, a developer's only when
// MU2_CRASH_SITE names one to try it against.
void PlayMode::sendBug(const Context& ctx, const game::Desk::BugAsk& ask) {
    bugSending_ = std::make_shared<BugSending>();
#ifdef MU2_CRASH_UPLOAD
    const char* asked = std::getenv("MU2_CRASH_SITE");
#ifdef MU2_SHIP
    const std::string site = asked && *asked ? asked : launcher::kCrashSite;
#else
    const std::string site = asked ? asked : "";
#endif
    if (site.empty()) {
        core::logError("bug report not sent: a developer's build sends only to MU2_CRASH_SITE");
        bugSending_->done = true;
        bugSending_->why = "This build has no website to send to (MU2_CRASH_SITE).";
        return;
    }
    launcher::BugReport report;
    report.text = ask.text;
    report.world = ask.world;
    report.x = ask.x;
    report.y = ask.y;
    report.character = ask.character;
    report.kind = ask.kind;
    report.level = ask.level;
#ifdef _WIN32
    report.platform = "windows";
#elif defined(__linux__)
    report.platform = "linux";
#else
    report.platform = "macos";
#endif
    report.build = MU2_BUILD_VERSION;
    report.renderer = bgfx::getRendererName(bgfx::getRendererType());
    if (const bgfx::Caps* caps = bgfx::getCaps()) {
        char gpu[16];
        std::snprintf(gpu, sizeof gpu, "%04x:%04x", unsigned(caps->vendorId), unsigned(caps->deviceId));
        report.gpu = gpu;
    }
    report.server = ctx.args.server;
    const std::string token = launcher::ticketFromEnvironment();
    std::thread([site, token, report, sending = bugSending_] {
        std::string why;
        const bool sent = launcher::sendBug(site, token, report, &why);
        std::lock_guard<std::mutex> hold(sending->lock);
        sending->done = true;
        sending->sent = sent;
        sending->why = why;
    }).detach();
#else
    (void)ctx;
    (void)ask;
    bugSending_->done = true;
    bugSending_->why = "This build cannot send reports.";
#endif
}

}  // namespace mu::app
