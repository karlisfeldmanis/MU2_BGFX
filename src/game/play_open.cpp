// A realm raised and a Play built over it: the tables read, the bodies given figures, the
// town's people stood where MU stands them, and the clips each body will need found once.
//
// Found ONCE, at open, and never per frame: an attack slot, a death clip or a cry looked up by
// name while a fight is running is a string compare in the middle of the thing it is timing.
#include "game/play.h"
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
    grassy_ = world == "lorencia" || world == "noria";
    snowy_ = world == "devias";
    const std::string path = core::join(assetDir, "cooked/" + world + "/" + world + ".mur");
    std::string error;
    if (!content::loadTables(path, tables_, error)) {
        core::logError("no rules for %s: %s (tools/cook.py --world %s --only tables)",
                       world.c_str(), error.c_str(), world.c_str());
        return false;
    }
    // A townsperson's plus becomes its chrome here, the first moment the item table is in
    // hand: Marlon's plate at +7 and Berdysh at +8.
    if (figures_) figures_->shine(tables_.items);
    if (figures_) pets_.open(*figures_);

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
    // draws nothing, so there is nothing to be invisible in.
    if (figures_) {
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

    if (!realm_.raise(&tables_, seed, column, row, sim::Kin(kin), level)) return false;

    // The roads, for the townsfolk's rounds to keep to (the user's, 2026-09-29: "peia has to
    // use roads"): a tile whose painted slot -- the overlay where it is laid over half or more,
    // else the base -- is one of the world's road sheets. Off the ground's own grid, which the
    // sim does not have. Noria's alone: its roads are TileRock01's cobble and nothing else is
    // laid as one -- its TileGround01 is turf in specks, not a path -- and Lorencia's Marlon
    // walks as he did. A world added here names its own sheets.
    static const char* const kNoriaRoads[] = {"TileRock01", "TileRock02", "TileWood01"};
    if (world == "noria" && ground_ != nullptr && tables_.grid.size() > 0 &&
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
        realm_.setRoads(std::move(roads));
    }

    // A character made above level 1 arrives with his points in hand, and unspent he cannot
    // lift the weapon he was asked to carry. So the courtesy the headless hand does itself
    // (game/headless.cpp): pay for what he is about to hold. The REST stays in hand since
    // sprint 7, because the character window is where it is spent now; this used to pour it
    // into strength for want of one. What is left of this goes when the items land and the
    // requirement is MU's formula rather than the raw row (docs/sprints/07-the-windows.md).
    if (realm_.hero().pointsInHand > 0) {
        int points = realm_.hero().pointsInHand;
        int wantsStrength = 0, wantsAgility = 0;
        for (const std::string& name : {weapon, shield}) {
            const int32_t index = name.empty() ? -1 : tables_.armNamed(name);
            if (index < 0) continue;
            const content::Arm& arm = tables_.arms[size_t(index)];
            wantsStrength = std::max(wantsStrength, arm.wantsStrength);
            wantsAgility = std::max(wantsAgility, arm.wantsAgility);
        }
        const int intoStrength =
            std::min(points, std::max(0, wantsStrength - realm_.hero().points.strength));
        points -= intoStrength;
        const int intoAgility =
            std::min(points, std::max(0, wantsAgility - realm_.hero().points.agility));
        points -= intoAgility;
        if (intoStrength + intoAgility > 0) realm_.spend(intoStrength, intoAgility, 0, 0);
    }

    // What he holds. The drawn character is dressed from the same two names (Figures::dress, by
    // way of World::play), so the picture and the fight agree by construction rather than by a
    // warning -- which is what this used to have to settle for, when the cook chose the hands.
    if (!weapon.empty() || !shield.empty()) {
        const int32_t held = weapon.empty() ? -1 : tables_.armNamed(weapon);
        const int32_t worn = shield.empty() ? -1 : tables_.armNamed(shield);
        if ((!weapon.empty() && held < 0) || (!shield.empty() && worn < 0)) {
            core::logError("no arm called %s%s%s", weapon.c_str(), shield.empty() ? "" : " or ",
                           shield.c_str());
        } else if (!realm_.equip(held, worn)) {
            // Refused on the strength or the agility, and then given anyway: what the character
            // starts holding is the cradle's gift and the requirement belongs to the bag that
            // picks one up. See Realm::equip. The shortfall is printed rather than hidden --
            // a level-one knight is 22 strength short of his own axe, and that is the character
            // the first hour is balanced around, not a fault in the run.
            const std::string why = realm_.refusal();
            if (!realm_.equip(held, worn, true)) {
                core::logError("he cannot hold that: %s", realm_.refusal().c_str());
            } else {
                core::logf("play: %s -- given anyway, as a new character is given what his "
                           "class starts with", why.c_str());
            }
        } else {
            core::logf("play: holding %s%s%s -- damage %d to %d, defence %d, a swing every "
                       "%d ms (%d ticks)", weapon.c_str(), shield.empty() ? "" : " and ",
                       shield.c_str(), realm_.hero().stats.minimumDamage,
                       realm_.hero().stats.maximumDamage, realm_.hero().stats.defense,
                       realm_.hero().swingMs, realm_.hero().swingTicks);
        }
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
        realm_.spend(wizard ? 0 : intoForce, 0, points - intoForce, wizard ? intoForce : 0);
        const sim::Body& hero = realm_.hero();
        core::logf("arena: the hero is level %d, %d points into %s and %d into vitality "
                   "-- %d to %d damage, %d health", hero.level, intoForce,
                   wizard ? "energy" : "strength", points - intoForce,
                   hero.stats.minimumDamage, hero.stats.maximumDamage, hero.maxHealth);
    }

    if (!arena_.breed.empty() && arena_.learn != 0 && realm_.learn(arena_.learn)) {
        core::logf("arena: the hero is taught skill %d", arena_.learn);
    }
    if (!arena_.breed.empty() && arena_.undying) {
        realm_.undying(true);
        core::logf("arena: the hero is undying");
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
        // Whoever hands out a quest (sim/quests.cpp): Marlon, Peia and Devin. And Sevina, whose
        // quest is still to come, under a grey mark (Beacon).
        const int32_t number = tables_.folk[i].number;
        if (sim::questOf(number) >= 0 || number == sim::kSevina) questGivers_.push_back(int(i));
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
        one.figure.stand(look, at, yaw, look->scale, tables_.grid.safe(person.x, person.y));
        settle(one);
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
    if (windy_) heard_.wind = sound_.load("world_wind", false);
    // The Dungeon's air is aDungeon, played as the wind is: looping and unplaced, the whole map
    // (SceneManager.cpp:859-861). It rides the wind's slot, which the Dungeon has no use for.
    // The tower's is aTower (SceneManager.cpp:873-875), the same way.
    else if (dungeonAir_) heard_.wind = sound_.load(towerAir_ ? "world_tower" : "world_dungeon", false);
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
    heard_.meteorite = sound_.load("meteorite", true);
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
            if (body.player) dualSwings(one, look);
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
            if (look->name == kSandingFigure) one.sands = true;
            // The Stone Golem has no corpse either: `o->Live = false`, and the stones.
            if (look->name == kCrumblingFigure) {
                one.bursts = true;
                one.crumbles = true;
                one.deathClip = -1;
            }
            // The Ice Monster keeps its death clip and shatters at its end (sandOnDeath).
            if (look->name == kShatteringFigure) one.shatters = true;
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
            const bool elite = look->name == kEliteBullFigure;
            one.venomous = look->name == kVenomousFigure;
            if ((elite || one.venomous || look->name == kSnortingFigure) && look->skeletonMesh) {
                const std::vector<content::Bone>& bones = look->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == "smok_bone") one.snortBone = int(b);
                    if (elite && bones[b].name == "top_bone02") one.eyeBones[0] = int(b);
                    if (elite && bones[b].name == "top_bone01") one.eyeBones[1] = int(b);
                }
            }
        }
    }
}

}  // namespace mu::game
