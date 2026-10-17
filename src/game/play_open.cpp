// A realm raised and a Play built over it: the tables read, the bodies given figures, the
// town's people stood where MU stands them, and the clips each body will need found once.
//
// Found ONCE, at open, and never per frame: an attack slot, a death clip or a cry looked up by
// name while a fight is running is a string compare in the middle of the thing it is timing.
#include "game/play.h"
#include "game/roster.h"

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
#include "game/world/world_show.h"
#include "game/play_tuning.h"

namespace mu::game {

bool Play::open(const std::string& assetDir, const std::string& world,
                const content::Ground* ground, Figures* figures, uint64_t seed, int kin,
                int level, int column, int row, const std::string& weapon,
                const std::string& shield, const FigureBody* heroLook, const std::string& bareName) {
    ground_ = ground;
    figures_ = figures;
    bare_ = bareName;
    // How the world sounds underfoot and in the air is its manifest's (game/world/world_show.h).
    const WorldShow& show = worldShow(world);
    airLoop_ = show.air;
    airRoom_ = show.room;
    airBed_ = show.bed;
    airEverywhere_ = show.airEverywhere;
    underwater_ = show.swims;
    grassy_ = show.steps == "grass";
    snowy_ = show.steps == "snow";
    const std::string path = core::join(assetDir, "cooked/" + world + "/" + world + ".mur");
    std::string error;
    if (!content::loadTables(path, tables_, error)) {
        core::logError("no rules for %s: %s (pipeline/cook.py --world %s --only tables)",
                       world.c_str(), error.c_str(), world.c_str());
        return false;
    }
    // On a server (useServer): join it, and raise the mirror from its Welcome -- the seed, the
    // tile, the class and level, the hands and the config the server raised its realm with
    // (sprint 18). Nothing below may then change the realm in a way the server
    // does not: the nests are the table's own, and no arena, raid or roads are set up.
    std::string held = weapon, worn = shield;
    int castle = 0;  // the server's Blood Castle, set on the raise as it set it
    int square = 0, squareWait = 0;  // and its Devil Square
    if (!serverHost_.empty()) {
        auto remote = std::make_unique<client::RemoteLink>(realmHeld_);
        remote->setLag(lagMs_, jitterMs_);
        remote->setLoss(lossChance_);
        remote->setStall(stallMs_, stallEvery_);
        remote->setUdp(udp_);
        net::Hello hello;
        hello.world = world;
        hello.kin = uint8_t(kin);
        hello.level = level;
        hello.column = column;
        hello.row = row;
        hello.weapon = weapon;
        hello.shield = shield;
        hello.token = serverToken_;
        hello.account = game::sealedKey(serverAccount_);
        hello.items = tables_.itemsPrint();
        net::Welcome welcome;
        elsewhere_ = {};
        if (!remote->join(serverHost_, serverPort_, hello, welcome, &elsewhere_)) return false;
        serverToken_ = welcome.token;
        keptFirst_.reset();
        if (welcome.kept) keptFirst_ = std::make_unique<sim::Kept>(welcome.first);
        seed = welcome.seed;
        castle = welcome.castle;
        square = welcome.square;
        squareWait = welcome.squareWait;
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
    flying_ = show.mountsFly;
    pets_.setFlying(flying_);

    // The two copies of the attribute grid, compared -- the one the cook wrote into the tables
    // and the one the ground read out of attributes.png. This is the only run in which both
    // are in memory at once, and docs/reference/conventions.md promised the check the day there was one.
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
        if (arena_.gap > 0) {
            // A file of them N tiles east, rooted: a walk a tile takes a minute.
            one.x1 = one.x2 = column + arena_.gap;
            tables_.kinds[size_t(breed)].moveTicks = 1200;
            tables_.kinds[size_t(breed)].moveRange = 0;
        }
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
    // cooked (pipeline/cook.py --only figures), with no change here. Lorencia cooks every breed
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
    // The server's sequence (sim::raiseFrom), off what the Welcome said when remote; between its
    // raise and his hands, this run's own harness. The drawn character wears the same two names.
    const sim::RaiseOrder order{seed, column, row, sim::Kin(kin), level, realmConfig_, castle, square,
                                squareWait, held, worn, keptFirst_.get()};
    const bool raised = sim::raiseFrom(local_, &tables_, order, [&] {
        // An arena (--arena) in Devil Square stands its breed up now, not on a wave's minute.
        if (!arena_.breed.empty()) local_.freeSquare();
        if (benchPlayers_ > 0 && !remote()) raiseBench(column, row);

        // The roads, for the townsfolk's rounds to keep to (the user's, 2026-09-29: "peia has to
        // use roads"): a tile whose painted slot -- the overlay where it is laid over half or more,
        // else the base -- is one of the world's road sheets. Off the ground's own grid, which the
        // sim does not have. Noria's alone: its roads are TileRock01's cobble and nothing else is
        // laid as one -- its TileGround01 is turf in specks, not a path -- and Lorencia's Marlon
        // walks as he did. A world names its own sheets in its manifest's `roads`.
        const std::vector<std::string>& roadSheets = show.roads;
        if (!remote() && !roadSheets.empty() && ground_ != nullptr && tables_.grid.size() > 0 &&
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
                    for (const std::string& one : roadSheets) road = road || name == one;
                    if (!road) continue;
                    roads[size_t(r) * size_t(size) + size_t(c)] = 1;
                    ++paved;
                }
            }
            core::logf("play: %zu road tiles for the townsfolk's rounds", paved);
            local_.setRoads(std::move(roads));
        }
    });
    if (!raised) return false;
    // On a shared world, everything that happened in it before he came, and the mirror turned to
    // him: from here on `realm_.hero()` is his own player (sprint 19).
    if (link_) link_->catchUp();
    if (remote()) {
        const sim::Body& me = realm_.hero();
        core::logf("play: playing as #%u, class %d level %d at %d,%d, %d players here", me.id,
                   int(me.kin), me.level, me.column(), me.row(), realm_.playersHere());
    }

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

    if (arena_.spendEven && arena_.breed.empty() && realm_.hero().pointsInHand > 0) {
        const int third = realm_.hero().pointsInHand / 3;
        const bool wizard = realm_.hero().kin == sim::Kin::DarkWizard;
        local_.spend(wizard ? 0 : third, third, realm_.hero().pointsInHand - 2 * third,
                     wizard ? third : 0);
        core::logf("arena: the hero's %d points split three ways", 3 * third);
    }
    // Without --arena too: a plain run taught one skill to try by hand, on the right button.
    if (arena_.learn != 0 && local_.learn(arena_.learn)) {
        core::logf("arena: the hero is taught skill %d", arena_.learn);
    }
    for (const int32_t skill : arena_.teach) {
        if (local_.learn(skill)) core::logf("arena: the hero is taught skill %d", skill);
    }
    // And a plain run too, to explore a world without dying in it.
    if (arena_.undying) {
        local_.undying(true);
        core::logf("arena: the hero is undying");
    }
    if (arena_.unseen) {
        local_.unseen(true);
        core::logf("arena: no monster notices the hero");
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
    // On a server for every class, not only hers: another elf's summon walks the same map, and
    // a knight who came in beside one saw it as a bare health bar (the user, 2026-10-09).
    if (figures_ && (realm_.hero().kin == sim::Kin::FairyElf || remote())) {
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
        drawn_.push_back(drawnFor(body, heroLook, &bones));
        ++(drawn_.back().figure.body() != nullptr ? dressed : bare);
    }
    // On a server he may have come from another world with all he carried (sim::Kept): dressed
    // in what he wears now, not in what the lobby handed the door.
    if (remote()) redress();
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

void Play::openSound(const std::string& assetDir, bool muted, bool offline) {
    if (!showing_.isOpen() || !sound_.open(assetDir, showing_.table(), muted, offline)) return;
    sound_.load("player_level_up", false);
    heard_.levelUp = sound_.load("player_level_up", true);
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
    // The world's air: MU's wind, or the map's own played as the wind is -- looping and
    // unplaced, the whole map, as aDungeon is (SceneManager.cpp:859-861) -- on the wind's slot.
    if (!airLoop_.empty()) heard_.wind = sound_.load(airLoop_.c_str(), false);
    // Blood Castle's match bed, looped while its run is on (Play::hear).
    heard_.castleBed = airBed_.empty() ? -1 : sound_.load(airBed_.c_str(), false);
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
        } else if (one.who == "goblinmaster") {
            // The Seed Master's flare on bone 39 (Bone03, at his brow, where MU sets it), MU's Scale 2 at half:
            // a glow starts small (the user, 2026-10-16: 'Keep name, small glow').
            one.glowBone = 39;
            one.glowScale = 1.0f;
            one.glowFlare = true;
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
    heard_.invited = sound_.load("window_invite", false);
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
        // Silent until loaded: the header's list of -1s is shorter than kSkills, and the rest
        // were 0 -- a sound -- for any row with none of its own.
        heard_.skill[i] = -1;
        // Penetration's SOUND_PIERCING is not heard: its bow or crossbow is (the user,
        // 2026-10-09: "dont play penetration sound for penetration skill, crossbow is enough").
        if (row.number == sim::skill::kPenetration) continue;
        if (row.sound[0] != '\0') heard_.skill[i] = sound_.load(row.sound, true);
    }
    heard_.explosion = sound_.load("explosion", true);
    heard_.golemBurst = sound_.load("bluegolem_burst", true);
    heard_.witchThunder = sound_.load("witchqueen_thunder", true);
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
        const auto cry = [&](const char* what) { return sound_.load(named + what, true, true); };
        one.cryAttack = cry("_attack"), one.cryAttack2 = cry("_attack2");  // Hell Maine's wind, fire lay
        one.cryDie = cry("_die"), one.cryMove = cry("_move");
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
    one.seeThrough = look->has("see_through") ? kSeeThroughAlpha : 1.0f;
    one.murderer = look->has("murderer");
    // MU's CreateBlood needs a head bone (BoneHead 20 for MODEL_PLAYER), and a monster bleeds
    // only where its recipe says so: MU sets BoneHead for some breeds and not others, and leaves
    // out the Ghost, the Assassin, the Ice Queen and the skeletons (`c->Blood`).
    one.bleeds = body.player || look->has("bleeds");
    one.bled = false;
    one.bloodBone = -1;
    if (one.bleeds && look->skeletonMesh) {
        const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
        for (size_t b = 0; b < bones.size(); ++b) {
            if (bones[b].name == "Bip01 Head") one.bloodBone = int(b);
        }
    }
    one.hops = !body.player && look->has("hops");
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
            if (body.player) playerSwings(one, look, body);
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
            // And no second swing for a monster on this rig: the 1-in-3 SwordCount alternation
            // is the MONSTER branch's. A player's own cycle is in swingClips (playerSwings).
            one.deathClip = look->library->find(kPlayerDieSlot);
            // A body on the player rig that is NOT the hero still flinches in a
            // meteor's quake: MU's loop excludes the hero alone and gives everything
            // else PLAYER_SHOCK. The hero keeps none, as Play::update says.
            if (!body.player) one.shockClip = look->library->find(kPlayerShockSlot);
            // And the skeleton does not fall at all: it comes apart. Its death clip is
            // taken away here rather than left unplayed, so nothing can reach for one.
            if (!body.player && look->has("bursts_apart")) {
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
            if (look->has("sands")) one.sands = true;
            // Tarkan's: the sand while it walks, and its two eye trails by MoveEye's bones.
            one.walkSands = look->has("walk_sands");
            one.handFlameBones[0] = one.handFlameBones[1] = one.handFlameBones[2] =
                one.handFlameBones[3] = -1;
            if (look->has("hand_flame") && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    for (int h = 0; h < 4; ++h) {
                        if (look->named("hand_flame", size_t(h), bones[b].name)) {
                            one.handFlameBones[h] = int(b);
                        }
                    }
                }
            }
            one.footFireBones[0] = one.footFireBones[1] = -1;
            one.burns = false;
            if (look->skeletonMesh &&
                (look->has("foot_fire") || look->has("burning"))) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                const bool burning = look->has("burning");
                int found = 0;
                for (int i = 0; i < 35; ++i) one.burnBones[i] = -1;
                for (size_t b = 0; b < bones.size(); ++b) {
                    for (int f = 0; f < 2 && !burning; ++f) {
                        if (look->named("foot_fire", size_t(f), bones[b].name)) {
                            one.footFireBones[f] = int(b);
                        }
                    }
                    for (int i = 0; i < 35 && burning; ++i) {
                        if (look->named("burning", size_t(i), bones[b].name)) {
                            one.burnBones[i] = int(b);
                            ++found;
                        }
                    }
                }
                one.burns = burning && found == 35;
            }
            one.trailBones[0] = one.trailBones[1] = -1;
            if (!look->eyeTrail[0].empty() && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == look->eyeTrail[0]) one.trailBones[0] = int(b);
                    if (bones[b].name == look->eyeTrail[1]) one.trailBones[1] = int(b);
                }
            }
            // The Stone Golem has no corpse either: `o->Live = false`, and the stones.
            if (look->has("crumbles")) {
                one.bursts = true;
                one.crumbles = true;
                one.deathClip = -1;
            }
            if (look->has("death_cow")) {
                one.bursts = true;
                one.deathClip = -1;
            }
            // The Ice Monster keeps its death clip and shatters at its end (sandOnDeath).
            if (look->has("shatters")) one.shatters = true;
            // The Great Bahamut's Level 1 trail is the dragon's own dust, one BITMAP_SMOKE + 1
            // round it every fourth reference frame while it lives (ZzzCharacter.cpp:6169-6176),
            // and no fire: it has no head bone named for one, so the bite's spark never comes.
            if (look->has("great_bahamut")) one.breathes = true;
            if (look->has("budge_breath") && look->skeletonMesh) {
                one.breathes = true;
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (look->named("head", 0, bones[b].name)) one.headBone = int(b);
                }
            }
            // MODEL_BULL_FIGHTER's: smok_bone is MU's 24, and the Elite's eyes are
            // 22 and 23, top_bone02 and top_bone01, in RenderEye's left-right order.
            if (look->has("scorpion_light") && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (look->named("light", 0, bones[b].name)) one.lightBone = int(b);
                }
            }
            one.starBone = -1;
            if (look->has("star_staff") && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (look->named("star", 0, bones[b].name)) one.starBone = int(b);
                }
            }
            if (look->has("yeti_breath") && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (look->named("snort", 0, bones[b].name)) one.snortBone = int(b);
                }
                one.snortAlways = one.snortBone >= 0;
            }
            // The Shadows' joints, by name (show.bones.shade, fx/shadow_stars.h).
            one.shadeBones.clear();
            const bool shadow = look->has("shadow") || look->has("poison_shadow");
            one.shadePoison = look->has("poison_shadow");
            one.embers = look->has("death_gorgon") || look->has("death_knight") ||
                         look->has("great_drakan");
            one.emberBone = -1;
            one.emberEvery = look->has("death_knight")   ? kKnightEmberEveryFrames
                             : look->has("great_drakan") ? kGreatDrakanEmberEvery
                                                         : kEmberEveryFrames;
            if (look->has("great_drakan") && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (look->named("ember", 0, bones[b].name)) one.emberBone = int(b);
                }
            }
            one.handBones[0] = one.handBones[1] = -1;
            one.beams = look->has("vepar")         ? Drawn::Beams::Blur
                        : look->has("lizard_king") ? Drawn::Beams::Thunder
                        : look->has("hydra")       ? Drawn::Beams::Horn
                                                   : Drawn::Beams::Laser;
            one.beamMaterial = -1;
            for (int& head : one.headBones) head = -1;
            if (look->has("hydra") && !look->parts.empty()) {
                const auto& materials = look->parts.front()->materials();
                for (size_t m = 0; m < materials.size(); ++m) {
                    if (materials[m].name == look->beamMaterial) one.beamMaterial = int(m);
                }
                // MU's beams are never drawn: its heads throw red lightning instead.
                one.figure.hideBodyMaterial(one.beamMaterial);
                if (look->skeletonMesh) {
                    const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                    for (size_t h = 0; h < 4 && h < look->heads.size(); ++h) {
                        for (size_t b = 0; b < bones.size(); ++b) {
                            if (bones[b].name == look->heads[h].bone) one.headBones[h] = int(b);
                        }
                    }
                }
            }
            if (look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (look->has("death_knight") && look->named("ember", 0, bones[b].name)) {
                        one.emberBone = int(b);
                    }
                    // The Vepar's link bones carry the Devil's names (Monster35, 30 and 39).
                    const bool beams = look->has("devil") || look->has("vepar") ||
                                       look->has("lizard_king");
                    if (beams && look->named("hands", 0, bones[b].name)) one.handBones[0] = int(b);
                    if (beams && look->named("hands", 1, bones[b].name)) one.handBones[1] = int(b);
                }
            }
            one.starBones.clear();
            one.starRibbons = look->has("star_ribbons");
            one.glides = look->has("crust");
            one.phoenix = look->has("phoenix");
            if (one.starRibbons && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (const std::string& name : look->bonesFor("stars")) {
                    for (size_t b = 0; b < bones.size(); ++b) {
                        if (bones[b].name == name) one.starBones.push_back(int(b));
                    }
                }
            }
            one.arcBones.clear();
            one.starTint = nullptr;
            one.blizzard = look->has("queen_rainer");
            const bool drakan = look->has("drakan");
            one.clawBones.clear();
            // Any breed whose recipe has show.claws: the Forest Orc's, the Death Rider's blades.
            if (!look->claws.pairs.empty() && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (const auto& claw : look->claws.pairs) {
                    int ends[2] = {-1, -1};
                    for (int e = 0; e < 2; ++e)
                        for (size_t b = 0; b < bones.size(); ++b)
                            if (bones[b].name == claw[e]) ends[e] = int(b);
                    if (ends[0] < 0 || ends[1] < 0) continue;
                    one.clawBones.push_back(ends[0]);
                    one.clawBones.push_back(ends[1]);
                }
            }
            if (look->has("death_tree") && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (const std::string& name : look->bonesFor("stars"))
                    for (size_t b = 0; b < bones.size(); ++b)
                        if (bones[b].name == name) one.starBones.push_back(int(b));
                one.starTint = look->starTint();
            }
            if ((one.blizzard || drakan) && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                const auto find = [&](const std::string& name) {
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
                // The Queen's and the Drakan's alike, each from its own recipe: its star bones,
                // their colour (none for her, so the starlight's), and its arcs.
                for (const std::string& name : look->bonesFor("stars")) {
                    if (const int star = find(name); star >= 0) one.starBones.push_back(star);
                }
                one.starTint = look->starTint();
                arcs(look->arcPairs);
                one.arcsAFrame = look->arcsAFrame;
                one.arcHalf = look->arcHalf;
            }
            one.auraBone = -1;
            one.auraReach = look->auraTiles * (ground_ ? ground_->metresPerTile() : 1.0f);
            if (!look->auraBone.empty() && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == look->auraBone) one.auraBone = int(b);
                }
                for (int i = 0; i < 3; ++i) one.auraColour[i] = look->auraColour[i];
            }
            // Off the body's own joints: MU's rand() % NumBones takes every bone, and the
            // Gorgon's ground marker (Bip01 Footsteps), its end dummies past the toes, fingers
            // and head and its `boss` threw them wide of the body (the user, 2026-10-09: "keep
            // finetuning that effect"). Ours.
            if (one.embers && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    const std::string& name = bones[b].name;
                    if (name == "Bip01" || name == "Bip01 Footsteps" || name == "boss" ||
                        name.rfind("dummy", 0) == 0) {
                        continue;
                    }
                    one.shadeBones.push_back(int(b));
                }
            }
            if (shadow && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    for (const std::string& name : look->bonesFor("shade")) {
                        if (bones[b].name == name) one.shadeBones.push_back(int(b));
                    }
                }
            }
            const bool elite = look->has("elite_bull") || look->has("death_cow");
            if (look->has("crust") &&
                look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (look->named("eyes", 0, bones[b].name)) one.eyeBones[0] = int(b);
                    if (look->named("eyes", 1, bones[b].name)) one.eyeBones[1] = int(b);
                }
                one.eyeSize = kCrustEyeSize;
            }
            one.venomous = look->has("venomous");
            if ((elite || one.venomous || look->has("snorts")) && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                // The Death Cow has the eyes and not the snort: MoveCharacterVisual's smoke is
                // MODEL_BULL_FIGHTER's case alone.
                const bool snorts = !look->has("death_cow");
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (snorts && look->named("snort", 0, bones[b].name)) one.snortBone = int(b);
                    if (elite && look->named("eyes", 0, bones[b].name)) one.eyeBones[0] = int(b);
                    if (elite && look->named("eyes", 1, bones[b].name)) one.eyeBones[1] = int(b);
                }
            }
        }
    }
}

Play::Drawn Play::drawnFor(const sim::Body& body, const FigureBody* heroLook, size_t* bones) {
    Drawn one;
    one.id = body.id;
    one.wasX = one.nowX = body.x;
    one.wasY = one.nowY = body.y;
    one.wasFacing = one.nowFacing = body.facing;
    const FigureBody* look = nullptr;
    if (body.player && body.id == realm_.hero().id) {
        look = heroLook;
        if (!look && figures_) look = figures_->body(kHeroFigure);
    } else if (body.player && body.gone) {
        // One who has left keeps his place in the realm till a newcomer takes it (Realm::join),
        // and is drawn as nobody: dressed, every bot who had come and gone had his suit read
        // under the loading screen -- 40 pieces, 108 ms, in a Dungeon of eleven.
    } else if (body.player) {
        // Another player in a shared world, in his own class and gear -- what of it is loaded,
        // the rest on its way (Figures::setStreaming) and put on him as it comes.
        look = otherLook(body);
        one.gear = gearOf(body);
        one.waitsForGear = figures_ && figures_->lastMissed() > 0;
    } else if (body.warden >= 0) {
        // A guard wears his townsperson's figure, and is drawn here rather than among the
        // folk below because he walks and fights.
        if (figures_) look = figures_->body(tables_.folk[size_t(body.warden)].figure);
    } else if (body.summoner != 0) {
        // Her summon's slot, dormant: no figure until she raises one, when the breed she
        // called is put on it (Play::update, What::Spawned). Given its placeholder kind's
        // figure here it stood as a Bull Fighter wherever the breed was not cooked.
        // One already standing -- raised on the server before this picture came up, so its
        // Spawned was never seen here -- wears its breed now, or it fought as a bare health bar
        // (the user, 2026-10-09, "summons is invissible, only hp bar is there").
        if (body.alive() && figures_) look = figures_->body(tables_.kinds[size_t(body.kind)].figure);
    } else if (body.raider >= 0) {
        // A raider: its class body in its own kit (play_raid.cpp).
        look = raiderLook(body);
    } else if (figures_) {
        look = figures_->body(tables_.kinds[size_t(body.kind)].figure);
    }
    if (look) {
        *bones = std::max(*bones, look->boneCount());
        fit(one, body, look);
    }
    return one;
}


// --bench-players: a ten-wide square of heroes two tiles apart round him, each in a party kit in
// turn -- the raid's end-game characters, +11 sets, +11 weapons and wings. join() puts each on
// the nearest standable tile.
void Play::raiseBench(int column, int row) {
    if (benchKits_.empty()) {
        core::logError("--bench-players: no kits to dress them in; none raised");
        return;
    }
    for (int i = 0; i < benchPlayers_; ++i) {
        const sim::RaiderKit& kit = benchKits_[size_t(i) % benchKits_.size()];
        const int c = column + (i % 10 - 5) * 2 + 1;
        const int r = row + (i / 10 - 5) * 2 + 1;
        const uint32_t id = local_.benchHero(kit, c, r);
        if (id == 0) continue;
        benchIds_.push_back(id);
        benchKit_.push_back(size_t(i) % benchKits_.size());
        benchTurn_.push_back(size_t(i));
        benchHome_.push_back(c);
        benchHome_.push_back(r);
        benchSkill_.push_back(kit.skills.empty() ? 0 : kit.skills.front());
    }
    if (benchFight_) {
        // The party never falls; the monsters do in an event's square (the user, 2026-10-10:
        // 'monsters is not dying'), and nowhere else, where the bench measures a crowd.
        local_.benchUndying(true, local_.squareRun().phase != sim::SquarePhase::None);
        // One party, his and theirs (the user, 2026-10-10: 'put my on bot party'): the realm's,
        // as the server sets it (Command::PartySet), so a kill is shared as a party's is.
        local_.command({.kind = sim::Command::Kind::PartySet, .a = 1, .target = local_.hero().id});
        for (const uint32_t id : benchIds_) {
            local_.command({.kind = sim::Command::Kind::PartySet, .a = 1, .target = id});
        }
        // And he fights beside them in a kit of his own class -- a damage dealer's, else any of
        // it -- not bare-handed (the user, 2026-10-10: 'also gear that naked char').
        const sim::RaiderKit* own = nullptr;
        for (const sim::RaiderKit& kit : benchKits_) {
            if (kit.kin != local_.hero().kin) continue;
            if (own == nullptr || (kit.role == sim::RaidRole::Melee && own->role != sim::RaidRole::Melee)) {
                own = &kit;
            }
        }
        if (own != nullptr) {
            // With more energy than the kit's own (the user, 2026-10-10: 'add more energy to our
            // char'): Bran's 40 left him 250 mana, a few skills and dry. 800 in his copy alone, about 1000 mana.
            sim::RaiderKit mine = *own;
            mine.points.energy = std::max(mine.points.energy, 800);
            local_.benchDressHero(mine);
            benchRedress_ = true;
            core::logf("bench: the hero wears %s's kit", own->name.c_str());
        }
    }
    core::logf("bench: %zu heroes raised round %d,%d, %s", benchIds_.size(), column, row,
               benchFight_ ? "a party by role, each with its class's skills, nobody falling"
                           : "every other one walking");
}

bool Play::benchName(uint32_t id, std::string* name, bool* bot) const {
    if (!benchFight_ || benchIds_.empty()) return false;
    if (id == realm_.hero().id) {
        if (name) *name = "You";
        if (bot) *bot = false;
        return true;
    }
    for (size_t i = 0; i < benchIds_.size(); ++i) {
        if (benchIds_[i] != id) continue;
        if (name) *name = benchKits_[benchKit_[i]].name;
        if (bot) *bot = true;
        return true;
    }
    return false;
}

// Every four seconds the walkers -- every other bench hero -- are sent five tiles east of
// where they stand, or back again. In a fight (--bench-fight), every hero is sent at the monster
// nearest him with his kit's first skill instead, every two seconds, which keeps the crowd on
// the pack as it moves.
void Play::walkBench(float seconds) {
    if (benchIds_.empty()) return;
    // His kit, put on before his figure stood: drawn as soon as it does, or he fought in the
    // sim with the Dragon Spear and stood bare-handed on the screen (the user, 2026-10-10).
    if (benchRedress_ && heroDrawn()) {
        redress();
        benchRedress_ = false;
    }
    // The party bar's rows, as the server's PartyState would carry them.
    if (benchFight_) {
        benchParty_.members.clear();
        const auto row = [&](const sim::Body& body, const std::string& name, bool bot) {
            net::PartyState::Member m;
            m.name = name;
            m.kin = uint8_t(body.kin);
            m.level = body.level;
            m.world = "devilsquare";
            m.here = body.id;
            m.health = uint8_t(body.maxHealth > 0 ? std::clamp(body.health * 100 / body.maxHealth, 0, 100) : 0);
            m.mana = uint8_t(body.maxMana > 0 ? std::clamp(body.mana * 100 / body.maxMana, 0, 100) : 0);
            m.bot = bot;
            m.life = body.health;
            m.lifeMost = body.maxHealth;
            benchParty_.members.push_back(m);
        };
        row(local_.hero(), "You", false);
        for (size_t i = 0; i < benchIds_.size(); ++i) {
            if (const sim::Body* one = local_.find(benchIds_[i])) row(*one, benchKits_[benchKit_[i]].name, true);
        }
    }
    // A trade asked of a bench hero is taken as a player at the keys would (local play only): he
    // says yes, puts the first thing in his bag up, and accepts once you have and the hold is out.
    if (local_.tick() != benchDealTick_) {
        benchDealTick_ = local_.tick();
        const uint32_t you = local_.hero().id;
        for (const uint32_t id : benchIds_) {
            if (local_.dealAskedOf(id) == you) {
                local_.command({.kind = sim::Command::Kind::DealAnswer, .player = id, .a = 1, .target = you});
                benchOffered_ = false;
            }
        }
        if (const uint32_t with = local_.dealing();
            with != 0 && std::find(benchIds_.begin(), benchIds_.end(), with) != benchIds_.end()) {
            if (!benchOffered_) {
                benchOffered_ = true;
                if (const sim::Satchel* bag = local_.satchelOf(with)) {
                    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                        if ((*bag)[slot].empty()) continue;
                        local_.command({.kind = sim::Command::Kind::DealPut, .player = with, .a = slot, .b = -1});
                        break;
                    }
                }
            }
            if (local_.dealAccepted(false) && !local_.dealAccepted(true) && local_.dealHold() == 0) {
                local_.command({.kind = sim::Command::Kind::DealAccept, .player = with, .a = 1});
            }
        }
    }
    benchClock_ += seconds;
    if (benchFight_) {
        // A party by role (setBench), each told twice a second, as a player at the keys would:
        // the raid's priorities (Realm::raid) through a player's own orders. Its guard first --
        // Defense, Soul Barrier, an elf's Greater Defense and Greater Damage on herself -- then
        // the healer keeps by him and heals him under seven tenths, and the rest go at the
        // nearest monster with the next of their attack skills in turn, so a knight throws his
        // Twisting Slash and Rageful Blow and a wizard his spells, not one skill for ever.
        if (benchClock_ < 0.5f && benchClock_ != seconds) return;
        benchClock_ = 0.0f;
        const std::vector<sim::Body>& bodies = local_.bodies();
        const int64_t now = local_.tick();
        const sim::Body& me = local_.hero();
        for (size_t i = 0; i < benchIds_.size(); ++i) {
            const sim::Body* hero = local_.find(benchIds_[i]);
            if (!hero || !hero->alive()) continue;
            const sim::RaiderKit& kit = benchKits_[benchKit_[i]];
            const auto cast = [&](int32_t skill, uint32_t at) {
                local_.command({.kind = sim::Command::Kind::Cast, .player = benchIds_[i],
                                .a = skill, .target = at});
            };
            bool told = false;
            for (const int32_t number : kit.skills) {
                const sim::SkillRow* row = sim::skillNumbered(number);
                if (row == nullptr) continue;
                if ((number == sim::skill::kDefense || number == sim::skill::kSoulBarrier ||
                     number == sim::skill::kGreaterDefense) && hero->boonUntil <= now) {
                    cast(number, 0);
                    told = true;
                    break;
                }
                if (number == sim::skill::kGreaterDamage && hero->mightUntil <= now) {
                    cast(number, 0);
                    told = true;
                    break;
                }
            }
            if (told) continue;
            if (kit.role == sim::RaidRole::Healer) {
                const float dx = me.x - hero->x, dy = me.y - hero->y;
                const bool near = dx * dx + dy * dy <= 2.0f * 2.0f;
                if (me.alive() && me.health * 10 < me.maxHealth * 7 && near) {
                    cast(sim::skill::kHeal, me.id);
                } else if (!near) {
                    local_.command({.kind = sim::Command::Kind::Order, .player = benchIds_[i],
                                    .a = int32_t(sim::Request::Kind::WalkTo),
                                    .b = me.column() + 1, .c = me.row() + 1});
                }
                continue;
            }
            const sim::Body* nearest = nullptr;
            float best = 0.0f;
            for (const sim::Body& one : bodies) {
                if (!one.monster() || !one.alive() || one.kind < 0) continue;
                const float dx = one.x - hero->x, dy = one.y - hero->y;
                const float d = dx * dx + dy * dy;
                if (!nearest || d < best) {
                    nearest = &one;
                    best = d;
                }
            }
            if (!nearest) continue;
            std::vector<int32_t> attacks;
            for (const int32_t number : kit.skills) {
                const sim::SkillRow* row = sim::skillNumbered(number);
                if (row != nullptr && !row->onSelf()) attacks.push_back(number);
            }
            const int32_t skill = attacks.empty() ? 0 : attacks[benchTurn_[i]++ % attacks.size()];
            local_.command({.kind = sim::Command::Kind::Order,
                            .player = benchIds_[i],
                            .a = int32_t(sim::Request::Kind::Attack),
                            .d = skill,
                            .target = nearest->id});
        }
        return;
    }
    if (benchClock_ < 4.0f && benchClock_ != seconds) return;
    benchClock_ = 0.0f;
    benchOut_ = !benchOut_;
    for (size_t i = 1; i < benchIds_.size(); i += 2) {
        local_.command({.kind = sim::Command::Kind::Order,
                        .player = benchIds_[i],
                        .a = int32_t(sim::Request::Kind::WalkTo),
                        .b = benchHome_[i * 2] + (benchOut_ ? 5 : 0),
                        .c = benchHome_[i * 2 + 1]});
    }
}

}  // namespace mu::game
