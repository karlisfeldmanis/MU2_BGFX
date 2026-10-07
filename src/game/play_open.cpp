// A realm raised and a Play built over it: the tables read, the bodies given figures, the
// town's people stood where MU stands them, and the clips each body will need found once.
//
// Found ONCE, at open, and never per frame: an attack slot, a death clip or a cry looked up by
// name while a fight is running is a string compare in the middle of the thing it is timing.
#include "game/play.h"

#include "sim/cradle.h"
#include "sim/quests.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/frustum.h"
#include "game/world/maps.h"
#include "game/play_tuning.h"

namespace mu::game {

bool Play::open(const std::string& assetDir, const std::string& world,
                const content::Ground* ground, Figures* figures, uint64_t seed, int kin,
                int level, int column, int row, const std::string& weapon,
                const std::string& shield, const FigureBody* heroLook, const std::string& bareName) {
    ground_ = ground;
    figures_ = figures;
    bare_ = bareName;
    // Noria's and the Dungeon's air is not wind: MU plays its jungle and aDungeon instead.
    const MapRow* map = mapOf(world);
    windy_ = world != "noria" && !(map && map->underground);
    dungeonAir_ = world == "dungeon" || world == "losttower";
    towerAir_ = world == "losttower";
    castleAir_ = world == "bloodcastle";
    // Atlans's is aWater, the same way: the whole map, under no roof (SceneManager.cpp:879-881).
    waterAir_ = world == "atlans";
    desertAir_ = world == "tarkan";
    // Icarus's is aHeaven, over the whole map and under no roof (SceneManager.cpp:885-895).
    heavenAir_ = world == "icarus";
    underwater_ = world == "atlans";
    grassy_ = world == "lorencia" || world == "noria";
    snowy_ = world == "devias";
    const std::string path = core::join(assetDir, "cooked/" + world + "/" + world + ".mur");
    std::string error;
    if (!content::loadTables(path, tables_, error)) {
        core::logError("no rules for %s: %s (tools/cook.py --world %s --only tables)",
                       world.c_str(), error.c_str(), world.c_str());
        return false;
    }
    // On a server (useServer): join it, and raise the mirror from its Welcome -- the seed, the
    // tile, the class and level, the hands and the config the server raised its realm with
    // (docs/sprints/18-the-wire.md). Nothing below may then change the realm in a way the server
    // does not: the nests are the table's own, and no arena, raid or roads are set up.
    std::string held = weapon, worn = shield;
    if (!serverHost_.empty()) {
        auto remote = std::make_unique<RemoteLink>(realmHeld_);
        net::Hello hello;
        hello.world = world;
        hello.kin = uint8_t(kin);
        hello.level = level;
        hello.column = column;
        hello.row = row;
        hello.weapon = weapon;
        hello.shield = shield;
        net::Welcome welcome;
        if (!remote->join(serverHost_, serverPort_, hello, welcome)) return false;
        seed = welcome.seed;
        kin = welcome.kin;
        level = welcome.level;
        column = welcome.column;
        row = welcome.row;
        held = welcome.weapon;
        worn = welcome.shield;
        realmConfig_ = welcome.config;
        arena_ = Arena{};
        raidPlayers_ = 0;
        link_ = std::move(remote);
    }
    // A townsperson's plus becomes its chrome here, the first moment the item table is in
    // hand: Marlon's plate at +7 and Berdysh at +8.
    if (figures_) figures_->shine(tables_.items);
    if (figures_) pets_.open(*figures_);
    flying_ = dinorantFlies(world);
    pets_.setFlying(flying_);

    // The two copies of the attribute grid, compared -- the one the cook wrote into the tables
    // and the one the ground read out of attributes.png. This is the only run in which both
    // are in memory at once, and docs/conventions.md promised the check the day there was one.
    if (ground_ && !ground_->grid().empty()) {
        if (ground_->grid().size() != tables_.grid.size()) {
            core::logError("the ground's grid is %d tiles a side and the tables' is %d",
                           ground_->grid().size(), tables_.grid.size());
        } else {
            size_t differ = 0;
            for (int r = 0; r < tables_.grid.size(); ++r) {
                for (int c = 0; c < tables_.grid.size(); ++c) {
                    if (ground_->grid().at(c, r) != tables_.grid.at(c, r)) ++differ;
                }
            }
            if (differ > 0) {
                core::logError("the ground's attribute grid and the cooked one differ on %zu "
                               "tiles -- one of them is stale", differ);
            }
        }
    }

    // ---- the arena (--arena) --------------------------------------------------------------
    //
    // The whole of it: the map's nest table becomes ONE nest, of one breed, in a small box
    // round where the hero is about to be put down. Everything after this line is the ordinary
    // played run -- the realm raises the table it is given, places each body with its own
    // seeded dice, and the rules decide every blow -- which is what makes an arena shot a
    // picture of the game rather than of a pose.
    //
    // It draws from the seeded stream no differently than a normal run does, because it is the
    // same code drawing: Realm::raise spends two draws an attempt on every nest member and one
    // on its start delay, whatever the table says. A run with a different table is a different
    // run and reproduces itself, which is what `--seed` promises; it was never a promise that
    // two different tables agree.
    if (!arena_.breed.empty()) {
        const int32_t breed = arenaBreed(tables_, arena_.breed);
        if (breed < 0) {
            std::string had;
            for (const content::MonsterKind& kind : tables_.kinds) {
                if (!had.empty()) had += ", ";
                had += kind.figure.empty() ? kind.label : kind.figure;
            }
            core::logError("--arena %s: %s has no such breed. It has: %s", arena_.breed.c_str(),
                           world.c_str(), had.c_str());
            return false;
        }
        const content::MonsterKind& kind = tables_.kinds[size_t(breed)];
        const uint32_t population = tables_.population();
        content::MonsterNest one;
        one.kind = uint32_t(breed);
        one.count = uint32_t(std::max(1, arena_.count));
        one.x1 = column - Arena::kSpread;
        one.x2 = column + Arena::kSpread;
        one.y1 = row - Arena::kSpread;
        one.y2 = row + Arena::kSpread;
        tables_.nests.clear();
        tables_.nests.push_back(one);
        core::logf("arena: %u %s on tiles %d..%d by %d..%d, and nothing else of %s's %u spawns "
                   "-- under --fixed-dt 16.667 a tick is three frames",
                   one.count, kind.label.c_str(), one.x1, one.x2, one.y1, one.y2, world.c_str(),
                   population);
    }

    if (arena_.peaceful) {
        core::logf("peaceful: none of %s's %u spawns", world.c_str(), tables_.population());
        tables_.nests.clear();
    }

    zoneLevels_ = game::zoneLevels(tables_);

    // A breed with no cooked figure is not raised. The rules would still walk it and swing it,
    // and a monster nobody can see that hits the hero from the grass is a bug in any world --
    // it is what Noria's goblins were on the day its land was first stood up bare (2026-09-28),
    // with not one of its 16 breeds cooked. The nests come back breed by breed as each is
    // cooked (tools/cook.py --only figures), with no change here. Lorencia cooks every breed
    // it spawns, so this takes nothing from it. The headless hunt raises the whole table: it
    // draws nothing, so there is nothing to be invisible in. Nor on a server, whose realm has
    // every nest: a breed not cooked here is drawn as nothing and still fought.
    if (figures_ && !remote()) {
        uint32_t held = 0;
        std::string names;
        std::vector<content::MonsterNest> kept;
        for (const content::MonsterNest& nest : tables_.nests) {
            const content::MonsterKind& kind = tables_.kinds[nest.kind];
            if (!kind.figure.empty() && figures_->body(kind.figure)) {
                kept.push_back(nest);
                continue;
            }
            held += nest.count;
            const std::string& name = kind.figure.empty() ? kind.label : kind.figure;
            if (names.find(name) == std::string::npos) names += (names.empty() ? "" : ", ") + name;
        }
        if (held > 0) {
            core::logf("play: %u monsters of %s held back, their figures not cooked: %s", held,
                       world.c_str(), names.c_str());
            tables_.nests = std::move(kept);
        }
    }

    // The Golden Dragon's raid, when one was asked for (--raid): handed to the realm before it
    // is raised, as the arena's nests are (play_raid.cpp).
    if (raidPlayers_ > 0) {
        local_.setRaid(raidPlayers_, raidParty_, raidWatched_);
        local_.setRaidLanding(raidLanding_);
    }
    local_.configure(realmConfig_);
    if (!local_.raise(&tables_, seed, column, row, sim::Kin(kin), level)) return false;

    // The roads, for the townsfolk's rounds to keep to (the user's, 2026-09-29: "peia has to
    // use roads"): a tile whose painted slot -- the overlay where it is laid over half or more,
    // else the base -- is one of the world's road sheets. Off the ground's own grid, which the
    // sim does not have. Noria's alone: its roads are TileRock01's cobble and nothing else is
    // laid as one -- its TileGround01 is turf in specks, not a path -- and Lorencia's Marlon
    // walks as he did. A world added here names its own sheets.
    static const char* const kNoriaRoads[] = {"TileRock01", "TileRock02", "TileWood01"};
    if (!remote() && world == "noria" && ground_ != nullptr && tables_.grid.size() > 0 &&
        ground_->floorAt(0, 0) >= 0) {
        const int size = tables_.grid.size();
        std::vector<uint8_t> roads(size_t(size) * size_t(size), 0);
        size_t paved = 0;
        for (int r = 0; r < size; ++r) {
            for (int c = 0; c < size; ++c) {
                const int over = ground_->overlayAt(c, r);
                const int slot = over >= 0 && ground_->blendAt(c, r) >= 0.5f ? over
                                                                              : ground_->floorAt(c, r);
                if (slot < 0) continue;
                const std::string& name = ground_->floorName(slot);
                bool road = false;
                for (const char* one : kNoriaRoads) road = road || name == one;
                if (!road) continue;
                roads[size_t(r) * size_t(size) + size_t(c)] = 1;
                ++paved;
            }
        }
        core::logf("play: %zu road tiles for the townsfolk's rounds", paved);
        local_.setRoads(std::move(roads));
    }

    // His points for what he holds, and the weapon and shield in his hands: the rules' own
    // (sim/cradle.h), so a server raising the same realm outfits him the same. The drawn
    // character is dressed from the same two names (Figures::dress, by way of World::play).
    sim::outfit(local_, held, worn);

    // And the arena hero spends what is left, which nobody else does -- after what he holds,
    // so the damage this prints is the damage he will do. The reason is that an arena is
    // watched rather than played: a hero with his points in hand is a level-one knight with
    // more health, and against Lorencia's own Skeleton Warrior -- 525 health, 66 a blow -- that
    // is a fight he loses in eight swings without ever taking a quarter of it down, which
    // photographs a defeat and not a monster (measured, 2026-09-22). Half into strength and
    // half into vitality is the pair that makes the fight readable from both ends: enough
    // damage that the breed dies while the run is still going, enough health that it gets to
    // swing, breathe or throw first. Invention, like the level beside it (core/args.cpp), and
    // both move together with `--level`.
    // A wizard's half goes into energy instead, which is his force: his Energy Ball rolls off it
    // and his strength does nothing for a spell.
    if (!arena_.breed.empty() && realm_.hero().pointsInHand > 0) {
        const int points = realm_.hero().pointsInHand;
        const int intoForce = points / 2;
        const bool wizard = realm_.hero().kin == sim::Kin::DarkWizard;
        local_.spend(wizard ? 0 : intoForce, 0, points - intoForce, wizard ? intoForce : 0);
        const sim::Body& hero = realm_.hero();
        core::logf("arena: the hero is level %d, %d points into %s and %d into vitality "
                   "-- %d to %d damage, %d health", hero.level, intoForce,
                   wizard ? "energy" : "strength", points - intoForce,
                   hero.stats.minimumDamage, hero.stats.maximumDamage, hero.maxHealth);
    }

    // Without --arena too: a plain run taught one skill to try by hand, on the right button.
    if (arena_.learn != 0 && local_.learn(arena_.learn)) {
        core::logf("arena: the hero is taught skill %d", arena_.learn);
    }
    // And a plain run too, to explore a world without dying in it.
    if (arena_.undying) {
        local_.undying(true);
        core::logf("arena: the hero is undying");
    }
    if (arena_.wingDemo) {
        local_.wingDemo(true);
        core::logf("arena: every kill in Icarus leaves the first wings");
    }

    // A figure for every body, made once. Bodies are never added or removed after the realm is
    // raised -- a dead monster is a body waiting for its respawn -- so this list is as fixed as
    // the realm's own, and a frame walks it without allocating.
    drawn_.clear();
    drawn_.reserve(realm_.bodies().size());
    size_t bones = 0, dressed = 0, bare = 0;
    // **Her summons' breeds, on any map** (the user, 2026-09-28): a Goblin is Noria's figure,
    // and a world's table carries its own breeds alone, so the ones a summon raises are borrowed
    // from whichever world has them (Figures::borrow). Counted into the pose buffer as well.
    if (figures_ && realm_.hero().kin == sim::Kin::FairyElf) {
        for (int i = 0; i < sim::skillCount(); ++i) {
            const sim::SkillRow& row = sim::skillAt(i);
            if (row.summons <= 0 || !row.built) continue;
            for (const content::MonsterKind& kind : tables_.kinds) {
                if (kind.number != row.summons) continue;
                if (figures_->borrow(kind.figure)) {
                    if (const FigureBody* look = figures_->body(kind.figure)) {
                        bones = std::max(bones, look->boneCount());
                    }
                }
            }
        }
    }
    for (const sim::Body& body : realm_.bodies()) {
        Drawn one;
        one.id = body.id;
        one.wasX = one.nowX = body.x;
        one.wasY = one.nowY = body.y;
        one.wasFacing = one.nowFacing = body.facing;
        const FigureBody* look = nullptr;
        if (body.player) {
            look = heroLook;
            if (!look && figures_) look = figures_->body(kHeroFigure);
        } else if (body.warden >= 0) {
            // A guard wears his townsperson's figure, and is drawn here rather than among the
            // folk below because he walks and fights.
            if (figures_) look = figures_->body(tables_.folk[size_t(body.warden)].figure);
        } else if (body.summoner != 0) {
            // Her summon's slot, dormant: no figure until she raises one, when the breed she
            // called is put on it (Play::update, What::Spawned). Given its placeholder kind's
            // figure here it stood as a Bull Fighter wherever the breed was not cooked.
        } else if (body.raider >= 0) {
            // A raider: its class body in its own kit (play_raid.cpp).
            look = raiderLook(body);
        } else if (figures_) {
            look = figures_->body(tables_.kinds[size_t(body.kind)].figure);
        }
        if (look) {
            bones = std::max(bones, look->boneCount());
            ++dressed;
            fit(one, body, look);
        } else {
            ++bare;
        }
        drawn_.push_back(std::move(one));
    }
    // The townsfolk: a figure each where the cook named one, facing where MU faces them.
    //
    // NOT the Look's name read literally. MU2 measured this against a landmark (Folk.cs,
    // Townsfolk.Looking): Harold stands at 183,137 facing his campfire at 184,134.5, and his
    // entry says South -- so the table's values are three eighths off their names, and
    // `(look - 3) x 45 degrees` is the bearing from north (row decreasing) toward east (column
    // increasing). Read literally, as this first was, every townsperson stood turned away:
    // Lumen faced the back wall of her bar instead of the room.
    folk_.clear();
    questGivers_.clear();
    for (size_t i = 0; i < tables_.folk.size(); ++i) {
        // Whoever hands out a quest (sim/quests.cpp): Marlon, Peia, Devin, Sevina and the rest.
        const int32_t number = tables_.folk[i].number;
        // And Lirien, who takes back Peia's 'The Drowned Song'.
        if (sim::questOf(number) >= 0 || sim::questReceives(number)) questGivers_.push_back(int(i));
    }
    const float metresPerTile = ground_ ? ground_->metresPerTile() : 1.0f;
    for (size_t i = 0; i < tables_.folk.size(); ++i) {
        const content::Townsperson& person = tables_.folk[i];
        const FigureBody* look =
            person.figure.empty() || !figures_ ? nullptr : figures_->body(person.figure);
        if (!look) continue;
        // A guard is a body in the realm and is drawn with the bodies above.
        if (wardenBody(int(i)) != 0) continue;
        const int facing = person.look >= 1 && person.look <= 8 ? person.look : 3;
        const float bearing = float(((facing - 3) % 8 + 8) % 8) * (bx::kPi / 4.0f);
        // The bearing as a direction on the tile grid, then as the sim's own facing angle,
        // then the yaw -- the same two lines follow() turns a body's facing with.
        const float angle = std::atan2(-std::cos(bearing), std::sin(bearing));
        const float yaw = std::atan2(std::cos(angle), -std::sin(angle));
        const float x = (float(person.x) + 0.5f) * metresPerTile;
        const float z = -(float(person.y) + 0.5f) * metresPerTile;
        const float at[3] = {x, ground_ ? ground_->heightAt(x, z) : 0.0f, z};
        Standing one;
        one.folk = int(i);
        one.who = person.figure;
        one.smith = person.figure == kSmithFigure;
        // **The tile decides, not a literal `true`.** MU recomputes `c->SafeZone` from each
        // character's own tile every frame (ZzzCharacter.cpp:5607, :11676) and its NPCs are
        // CHARACTERs like any other, so a guard inside the zone stands unarmed with the
        // weapon slung (`SetPlayerStop`, :341, and `RenderCharacterBackItem`, :15239) and one
        // at the gate posts stands armed. Four of Lorencia's six guards are one tile OUTSIDE
        // the bit, so both halves of that show in the same town.
        //
        // And the `play(idleClip)` that used to follow threw away the clip `stand` had just
        // chosen for exactly this: it is the ARMED idle, so every townsperson stood in the
        // combat stance with empty hands and the weapon on the back at the same time.
        // Tersia at MuMain's 0.93 (ZzzCharacter.cpp:15009); an NPC recipe carries no scale.
        const float scale = look->scale * (person.number == sim::kTersia ? 0.93f : 1.0f);
        one.figure.stand(look, at, yaw, scale, tables_.grid.safe(person.x, person.y));
        settle(one);
        if (!look->wings.empty()) one.wing.wear(figures_->body(look->wings), one.figure);
        bones = std::max(bones, look->boneCount());
        folk_.push_back(std::move(one));
    }
    // And the ones the table leaves to the world: Hanzo is Smith01, Pasi Wizard01 and Baz
    // Storage01 in the town's own placements, which only the measuring crowd stood, so in play
    // the square was empty. Matched by tile to the table's blank entries, so each keeps its
    // person, and stood as the placement stands them.
    size_t placed = 0;
    if (figures_) {
        for (const FigurePlacement& spot : figures_->placements()) {
            const int column = int(std::floor(spot.position[0] / metresPerTile));
            const int row = int(std::floor(-spot.position[2] / metresPerTile));
            int who = -1;
            for (size_t i = 0; i < tables_.folk.size() && who < 0; ++i) {
                const content::Townsperson& person = tables_.folk[i];
                if (person.figure.empty() && person.x == column && person.y == row) who = int(i);
            }
            const FigureBody* look = who < 0 ? nullptr : figures_->body(spot.figure);
            if (!look) continue;
            Standing one;
            one.folk = who;
            one.who = spot.figure;
            one.smith = spot.figure == kSmithFigure;
            one.figure.stand(look, spot.position, spot.yaw, spot.scale,
                             tables_.grid.safe(column, row));
            settle(one);
            bones = std::max(bones, look->boneCount());
            folk_.push_back(std::move(one));
            ++placed;
        }
    }
    size_t cycling = 0;
    for (const Standing& one : folk_) cycling += one.cycles ? 1 : 0;
    core::logf("play: %zu townsfolk, %zu of them drawn here, %zu of those by the town's "
               "placements, %zu taking turns among their own clips",
               tables_.folk.size(), folk_.size(), placed, cycling);

    scratch_.assign(std::max<size_t>(128, bones) * 12, 0.0f);
    remember();
    // World::play dressed him before the realm armed him, so it could not know the quiver the
    // cradle hands an elf beside her bow: dressed again, once, now that the hands are known.
    if (!quiverName().empty()) redress();

    core::logf("play: %zu bodies, %zu of them with a figure to wear (%zu have none cooked), "
               "hero at tile %d,%d", drawn_.size(), dressed, bare, realm_.hero().column(),
               realm_.hero().row());
    return true;
}

void Play::shutdown() {
    // Before the bodies go: a voice may still be following one, and the log is still open.
    sound_.shutdown();
    drawn_.clear();
    scratch_.clear();
    accumulator_ = 0.0;
}

void Play::openSound(const std::string& assetDir, bool muted) {
    if (!showing_.isOpen() || !sound_.open(assetDir, showing_.table(), muted)) return;
    sound_.load("player_level_up", false);
    heard_.swing = sound_.load("player_swing", true);
    heard_.swingLong = sound_.load("player_swing_long", true);
    heard_.bow = sound_.load("player_bow", true);
    heard_.crossbow = sound_.load("player_crossbow", true);
    if (const content::SoundEvent* e = showing().table().event("player_bow")) {
        heard_.bowOnset = e->onset;
    }
    if (const content::SoundEvent* e = showing().table().event("player_crossbow")) {
        heard_.crossbowOnset = e->onset;
    }
    heard_.hit = sound_.load("melee_hit", true);
    heard_.missile = sound_.load("missile_hit", true);
    // The Dungeon's traps (ZzzCharacter.cpp:1223-1237): aGrate for the Lance and the Iron Stick,
    // sFlame for the Fire Trap. Loaded wherever there is a trap to fire them.
    if (!realm_.traps().empty()) {
        heard_.grate = sound_.load("trap_grate", true);
        heard_.trapFlame = sound_.load("spell_flame", true);
    }
    heard_.die = sound_.load("player_die", true);
    heard_.dieFemale = sound_.load("player_die_female", true);
    heard_.deathBell = sound_.load("player_death_stinger", false);
    heard_.shock = sound_.load("player_shock", true);
    heard_.shockFemale = sound_.load("player_shock_female", true);
    heard_.grass = sound_.load("player_step_grass", true);
    heard_.soil = sound_.load("player_step_soil", true);
    heard_.hoof = sound_.load("mount_hoof", true);
    heard_.swim = underwater_ ? sound_.load("player_step_swim", true) : -1;
    for (int step : {heard_.grass, heard_.soil}) {
        sound_.vary(step, kStepSemitones, kStepDropDb, kStepDarken);
    }
    sound_.vary(heard_.swim, kSwimSemitones, kSwimDropDb, kSwimDarken);
    if (windy_) heard_.wind = sound_.load("world_wind", false);
    // The Dungeon's air is aDungeon, played as the wind is: looping and unplaced, the whole map
    // (SceneManager.cpp:859-861). It rides the wind's slot, which the Dungeon has no use for.
    // The tower's is aTower (SceneManager.cpp:873-875), the same way.
    else if (dungeonAir_) heard_.wind = sound_.load(towerAir_ ? "world_tower" : "world_dungeon", false);
    else if (waterAir_) heard_.wind = sound_.load("world_water", false);
    else if (desertAir_) heard_.wind = sound_.load("world_desert", false);
    else if (heavenAir_) heard_.wind = sound_.load("world_heaven", false);
    // Blood Castle's match bed, looped while its run is on (Play::hear).
    heard_.castleBed = castleAir_ ? sound_.load("world_bloodcastle", false) : -1;
    heard_.fire = sound_.load("world_bonfire", false);
    heard_.fountain = sound_.load("world_fountain", false);
    heard_.hammer = sound_.load("npc_blacksmith", true);
    // The townsfolk with a voice of their own. rand_fps_check(N) is one frame in N at the 60
    // the client's rolls are written against, so the roll comes up every N/60 seconds.
    for (Standing& one : folk_) {
        if (one.who == "MixNpc01") {
            one.voice = sound_.load("npc_mix", true);
            one.every = 64.0f / 60.0f;
            one.glowBone = 32;
            one.glowScale = 1.5f;
        } else if (one.who == "DevilNpc01") {
            // Charon's light: bone 20, Bone01, the thing held out in front of him.
            one.orbBone = kCharonOrbBone;
        } else if (one.who == "ElfWizard01") {
            one.voice = sound_.load("npc_harp", true);
            one.every = 256.0f / 60.0f;
        }
    }
    heard_.itemDrop = sound_.load("item_drop", true);
    heard_.moneyDrop = sound_.load("money_drop", true);
    heard_.jewel = sound_.load("jewel_get", true);
    heard_.firework = sound_.load("firework", true);
    heard_.take = sound_.load("item_get", false);
    heard_.drink = sound_.load("player_drink", false);
    heard_.warp = sound_.load("spell_magic", false);
    heard_.apple = sound_.load("player_eat_apple", false);
    // eGem.wav a second time under its own event name, and not `jewel_get`'s handle: `load`
    // hands back the event it already has, so asking for `jewel_get` unplaced would have
    // returned the placed one and `play` refuses a placed event in silence. See useItem.
    heard_.orb = sound_.load("player_learn_skill", false);
    heard_.click = sound_.load("window_click", false);
    heard_.refused = sound_.load("window_refused", false);
    heard_.opened = sound_.load("window_open", false);
    heard_.repair = sound_.load("window_repair", false);
    heard_.mix = sound_.load("machine_mix", false);
    heard_.mixBreak = sound_.load("machine_break", false);
    heard_.meteorite = sound_.load("meteorite", true);
    heard_.evil = sound_.load("devil_evil", true);
    heard_.boltThunder = sound_.load("spell_thunder", true);
    heard_.rage2 = sound_.load("rage_blow_2", true);
    heard_.rage3 = sound_.load("rage_blow_3", true);
    heard_.hellfire = sound_.load("balrog_hellfire", true);
    novaBurstSound_ = sound_.load("nova_burst", true);
    heard_.iceCast = sound_.load("spell_ice", true);
    // The knight's skills, by the table's own index, so a cast asks for its wave by the same
    // number its cooldown is kept under. Both `sKnightSkill4` names are the same file: MU plays
    // SWORD4 for Cyclone and for Slash alike.
    for (int i = 0; i < sim::skillCount(); ++i) {
        const sim::SkillRow& row = sim::skillAt(i);
        if (row.sound[0] != '\0') heard_.skill[i] = sound_.load(row.sound, true);
    }
    heard_.explosion = sound_.load("explosion", true);
    // The knight dies to the other branch of the same test a monster does: SOUND_HUMAN_SCREAM04,
    // pMaleDie.wav. The elf is IsFemale's other side of it, pFemaleScream2 -- the figure's own
    // `female`, the flag her poses are chosen by.
    if (Drawn* hero = drawnOf(realm_.hero().id)) {
        const FigureBody* look = hero->figure.body();
        hero->cryDie = look && look->female ? heard_.dieFemale : heard_.die;
    }
    int breeds = 0;
    for (size_t i = 0; i < drawn_.size() && i < realm_.bodies().size(); ++i) {
        const sim::Body& body = realm_.bodies()[i];
        if (body.player || body.kind < 0 || size_t(body.kind) >= tables_.kinds.size()) continue;
        // MU2's Crowd.Named: the breed's label, lowered, with its spaces taken out --
        // "Bull Fighter" is bullfighter_attack. Spelled here, once a body, and never on a cry.
        std::string named;
        for (char c : tables_.kinds[size_t(body.kind)].label) {
            if (c != ' ') named += char(std::tolower(static_cast<unsigned char>(c)));
        }
        Drawn& one = drawn_[i];
        const int before = one.cryAttack;
        one.cryAttack = sound_.load(named + "_attack", true, true);
        one.cryDie = sound_.load(named + "_die", true, true);
        one.cryMove = sound_.load(named + "_move", true, true);
        if (before < 0 && one.cryAttack >= 0) ++breeds;
    }
    core::logf("sound: cries for %d monster bodies", breeds);
}


// A figure put on a drawn body, and every clip and bone it will ask for found once: what open
// does for each body, and what a summon's body is given again when she raises one of another
// breed (sprint 15) -- the one body in `drawn_` whose figure changes.
void Play::fit(Drawn& one, const sim::Body& body, const FigureBody* look) {
    const float at[3] = {0, 0, 0};
    one.figure.stand(look, at, 0.0f, look->scale);
    one.seeThrough = look->name == kSeeThroughFigure ? kSeeThroughAlpha : 1.0f;
    one.murderer = look->name == kMurdererFigure;
    // The swing, found once, and **which TABLE it is looked up in is decided by the
    // rig and not by whether the body is the player**. MU draws its Skeleton Warrior
    // as a MODEL_PLAYER with a skeleton sub-type, so `SetPlayerAttack` takes the
    // player branch for it and picks a clip by the weapon in its hands -- and
    // `monster_actions` 3, "Attack 1", is simply not a slot its library has. Asked
    // for one anyway it found nothing and the skeleton fought without ever swinging,
    // which is what this looked like in play.
    //
    // A figure that has a STANCE is a figure on the player rig: the cook writes one
    // from index.json's row ("stance": "sword" for the Skeleton Warrior) and no
    // monster on its own rig has one.
    const bool onPlayerRig = body.player || !look->stance.empty();
    if (look->library) {
        if (onPlayerRig) {
            one.attackClip = look->library->find(attackSlotFor(look->stance));
            if (one.attackClip < 0) one.attackClip = look->library->find(38);
            one.attackClip2 = -1;
            if (body.player) dualSwings(one, look);
            // A monster on the player rig whose blow is a spell casts it as a player does: MU
            // hands a MODEL_PLAYER the skill's SetPlayerMagic, PLAYER_SKILL_HAND1 + rand() % 2
            // (ZzzCharacter.cpp:1339) -- the Lost Tower's Cursed Wizard and his Meteorite --
            // rather than his weapon's swing. Both hands, and the swing counter's one-in-three
            // stands in for the coin.
            if (!body.player && body.kind >= 0 && size_t(body.kind) < tables_.kinds.size()) {
                const sim::SkillRow* spell =
                    sim::skillNumbered(tables_.kinds[size_t(body.kind)].attackSkill);
                if (spell != nullptr && spell->clip != 0) {
                    const int hand = look->library->find(spell->clip);
                    if (hand >= 0) {
                        one.attackClip = hand;
                        if (spell->clipOther != 0) {
                            one.attackClip2 = look->library->find(spell->clipOther);
                        }
                    }
                }
            }
            // And no second swing: the 1-in-3 SwordCount alternation is the MONSTER
            // branch's, and the player branch picks one clip by the stance. Left as
            // -1, `swordCount` counts on and always chooses this one.
            one.deathClip = look->library->find(kPlayerDieSlot);
            // A body on the player rig that is NOT the hero still flinches in a
            // meteor's quake: MU's loop excludes the hero alone and gives everything
            // else PLAYER_SHOCK. The hero keeps none, as Play::update says.
            if (!body.player) one.shockClip = look->library->find(kPlayerShockSlot);
            // And the skeleton does not fall at all: it comes apart. Its death clip is
            // taken away here rather than left unplayed, so nothing can reach for one.
            if (!body.player && (look->name == kBurstingFigure || look->name == kBurstingArcher ||
                                 look->name == kBurstingElite)) {
                one.bursts = true;
                one.deathClip = -1;
            }
        } else {
            one.attackClip  = look->library->find(3);  // Attack 1
            one.attackClip2 = look->library->find(4);  // Attack 2
            // A breed with no Attack 1 takes Attack 2 as its only swing.
            if (one.attackClip < 0) one.attackClip = one.attackClip2;
            one.deathClip = look->library->find(kMonsterDieSlot);
            one.shockClip = look->library->find(kMonsterShockSlot);
            // MODEL_BUDGE_DRAGON's own case in the effect switch. Its bone 7 is
            // Bip01 Head, found by name so the number is not a coincidence kept.
            // MODEL_GIANT's case, which is one call to MonsterDieSandSmoke. No bone and
            // no clip to find: the sand comes off the body's own position and the death
            // clip it already has.
            for (const char* sanding : kSandingFigures) {
                if (look->name == sanding) one.sands = true;
            }
            // Tarkan's: the sand while it walks, and its two eye trails by MoveEye's bones.
            one.walkSands = false;
            for (const char* walking : kWalkSandFigures) {
                if (look->name == walking) one.walkSands = true;
            }
            one.handFlameBones[0] = one.handFlameBones[1] = one.handFlameBones[2] =
                one.handFlameBones[3] = -1;
            if (look->name == kHandFlameFigure && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    for (int h = 0; h < 4; ++h) {
                        if (bones[b].name == kHandFlameBones[h]) one.handFlameBones[h] = int(b);
                    }
                }
            }
            one.footFireBones[0] = one.footFireBones[1] = -1;
            one.burns = false;
            if (look->skeletonMesh &&
                (look->name == kFootFireFigure || look->name == kBurningFigure)) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                const bool burning = look->name == kBurningFigure;
                int found = 0;
                for (int i = 0; i < 35; ++i) one.burnBones[i] = -1;
                for (size_t b = 0; b < bones.size(); ++b) {
                    for (int f = 0; f < 2 && !burning; ++f) {
                        if (bones[b].name == kFootFireBones[f]) one.footFireBones[f] = int(b);
                    }
                    for (int i = 0; i < 35 && burning; ++i) {
                        if (bones[b].name == kBurningBones[i]) {
                            one.burnBones[i] = int(b);
                            ++found;
                        }
                    }
                }
                one.burns = burning && found == 35;
            }
            one.trailBones[0] = one.trailBones[1] = -1;
            for (const EyeTrailRow& row : kEyeTrailRows) {
                if (look->name != row.figure || !look->skeletonMesh) continue;
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == row.right) one.trailBones[0] = int(b);
                    if (bones[b].name == row.left) one.trailBones[1] = int(b);
                }
            }
            // The Stone Golem has no corpse either: `o->Live = false`, and the stones.
            if (look->name == kCrumblingFigure) {
                one.bursts = true;
                one.crumbles = true;
                one.deathClip = -1;
            }
            if (look->name == kDeathCowFigure) {
                one.bursts = true;
                one.deathClip = -1;
            }
            // The Ice Monster keeps its death clip and shatters at its end (sandOnDeath).
            if (look->name == kShatteringFigure) one.shatters = true;
            // The Great Bahamut's Level 1 trail is the dragon's own dust, one BITMAP_SMOKE + 1
            // round it every fourth reference frame while it lives (ZzzCharacter.cpp:6169-6176),
            // and no fire: it has no head bone named for one, so the bite's spark never comes.
            if (look->name == kGreatBahamutFigure) one.breathes = true;
            if (look->name == kBreathingFigure && look->skeletonMesh) {
                one.breathes = true;
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == "Bip01 Head") one.headBone = int(b);
                }
            }
            // MODEL_BULL_FIGHTER's: smok_bone is MU's 24, and the Elite's eyes are
            // 22 and 23, top_bone02 and top_bone01, in RenderEye's left-right order.
            if (look->name == kScorpionFigure && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == "light_point") one.lightBone = int(b);
                }
            }
            one.starBone = -1;
            if (look->name == kStarStaffFigure && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == kStarStaffBone) one.starBone = int(b);
                }
            }
            if (look->name == kYetiFigure && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == kYetiBreathBone) one.snortBone = int(b);
                }
                one.snortAlways = one.snortBone >= 0;
            }
            // The Shadows' joints, by name (kShadowJoints, fx/shadow_stars.h).
            one.shadeBones.clear();
            const bool shadow = look->name == kShadowFigure || look->name == kPoisonShadowFigure;
            one.shadePoison = look->name == kPoisonShadowFigure;
            one.embers = look->name == kDeathGorgonFigure || look->name == kDeathKnightFigure ||
                         look->name == kGreatDrakanFigure;
            one.emberBone = -1;
            one.emberEvery = look->name == kDeathKnightFigure    ? kKnightEmberEveryFrames
                             : look->name == kGreatDrakanFigure ? kGreatDrakanEmberEvery
                                                                : kEmberEveryFrames;
            if (look->name == kGreatDrakanFigure && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == kGreatDrakanFireBone) one.emberBone = int(b);
                }
            }
            one.handBones[0] = one.handBones[1] = -1;
            one.beams = look->name == kVeparFigure        ? Drawn::Beams::Blur
                        : look->name == kLizardKingFigure ? Drawn::Beams::Thunder
                        : look->name == kHydraFigure      ? Drawn::Beams::Horn
                                                          : Drawn::Beams::Laser;
            one.beamMaterial = -1;
            for (int& head : one.headBones) head = -1;
            if (look->name == kHydraFigure && !look->parts.empty()) {
                const auto& materials = look->parts.front()->materials();
                for (size_t m = 0; m < materials.size(); ++m) {
                    if (materials[m].name == kHydraBeamMaterial) one.beamMaterial = int(m);
                }
                // MU's beams are never drawn: its heads throw red lightning instead.
                one.figure.hideBodyMaterial(one.beamMaterial);
                if (look->skeletonMesh) {
                    const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                    for (size_t h = 0; h < 4; ++h) {
                        for (size_t b = 0; b < bones.size(); ++b) {
                            if (bones[b].name == kHydraHeads[h].bone) one.headBones[h] = int(b);
                        }
                    }
                }
            }
            if (look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (look->name == kDeathKnightFigure && bones[b].name == "Bip01 Pelvis") {
                        one.emberBone = int(b);
                    }
                    // The Vepar's link bones carry the Devil's names (Monster35, 30 and 39).
                    const bool beams = look->name == kDevilFigure || look->name == kVeparFigure ||
                                       look->name == kLizardKingFigure;
                    if (beams && bones[b].name == "knife_gdf") one.handBones[0] = int(b);
                    if (beams && bones[b].name == "hand_bofdgne01") one.handBones[1] = int(b);
                }
            }
            one.starBones.clear();
            one.starRibbons = look->name == kAlquamosFigure;
            one.glides = look->name == kCrustFigure || look->name == kAlphaCrustFigure;
            one.phoenix = look->name == kDarkPhoenixFigure;
            if (one.starRibbons && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (const char* name : kStarlightBones) {
                    for (size_t b = 0; b < bones.size(); ++b) {
                        if (bones[b].name == name) one.starBones.push_back(int(b));
                    }
                }
            }
            one.arcBones.clear();
            one.starTint = nullptr;
            one.blizzard = look->name == kQueenRainerFigure;
            const bool drakan = look->name == kDrakanFigure;
            if ((one.blizzard || drakan) && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                const auto find = [&](const char* name) {
                    for (size_t b = 0; b < bones.size(); ++b) {
                        if (bones[b].name == name) return int(b);
                    }
                    return -1;
                };
                const auto arcs = [&](const auto& pairs) {
                    for (const auto& pair : pairs) {
                        const int a = find(pair[0]), b = find(pair[1]);
                        if (a < 0 || b < 0) continue;
                        one.arcBones.push_back(a);
                        one.arcBones.push_back(b);
                    }
                };
                if (one.blizzard) {
                    if (const int head = find(kQueenLightBone); head >= 0) {
                        one.starBones.push_back(head);
                    }
                    arcs(kQueenArcs);
                    one.arcsAFrame = kQueenArcsAFrame;
                    one.arcHalf = kQueenArcHalf;
                } else {
                    for (const char* name : kDrakanStars) {
                        if (const int star = find(name); star >= 0) one.starBones.push_back(star);
                    }
                    one.starTint = kDrakanBlue;
                    arcs(kDrakanArcs);
                    one.arcsAFrame = kDrakanArcsAFrame;
                    one.arcHalf = kDrakanArcHalf;
                }
            }
            one.auraBone = -1;
            for (const AuraLight& aura : kAuraLights) {
                if (look->name != aura.figure || !look->skeletonMesh) continue;
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == aura.bone) one.auraBone = int(b);
                }
                for (int i = 0; i < 3; ++i) one.auraColour[i] = aura.colour[i];
            }
            if (one.embers && look->skeletonMesh) {
                for (size_t b = 0; b < look->skeletonMesh->bones().size(); ++b) {
                    one.shadeBones.push_back(int(b));
                }
            }
            if (shadow && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    for (const char* name : kShadowJoints) {
                        if (bones[b].name == name) one.shadeBones.push_back(int(b));
                    }
                }
            }
            const bool elite = look->name == kEliteBullFigure || look->name == kDeathCowFigure;
            if ((look->name == kCrustFigure || look->name == kAlphaCrustFigure) &&
                look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == "eye00") one.eyeBones[0] = int(b);
                    if (bones[b].name == "eye01") one.eyeBones[1] = int(b);
                }
                one.eyeSize = kCrustEyeSize;
            }
            one.venomous = look->name == kVenomousFigure;
            if ((elite || one.venomous || look->name == kSnortingFigure) && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                // The Death Cow has the eyes and not the snort: MoveCharacterVisual's smoke is
                // MODEL_BULL_FIGHTER's case alone.
                const bool snorts = look->name != kDeathCowFigure;
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (snorts && bones[b].name == "smok_bone") one.snortBone = int(b);
                    if (elite && bones[b].name == "top_bone02") one.eyeBones[0] = int(b);
                    if (elite && bones[b].name == "top_bone01") one.eyeBones[1] = int(b);
                }
            }
        }
    }
}

}  // namespace mu::game
