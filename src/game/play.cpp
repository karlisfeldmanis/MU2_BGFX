#include "game/play.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// The sim's own rate. docs/conventions.md, "Time": the tick never reads the frame's delta and
// the frame never decides how many ticks have passed except by this number.
constexpr double kTickSeconds = 1.0 / 20.0;
// The most ticks one frame may run. A window that was away -- dragged, or stalled on a
// screenshot's readback -- comes back owing seconds of simulation, and stepping all of it in
// one frame is a stall that makes the next frame owe more. MU2 called this the mirror clock's
// debt and repaid it a little at a time; here the debt is simply forgiven, because a single
// player game has nobody to be out of step with.
constexpr int kMostTicks = 5;
// The least time between two ticks taken early for a click, in seconds. See Play::update.
constexpr float kEarlyApart = 0.5f;
// How long the character takes to dissolve in when the game has loaded. Invention.
constexpr float kAppearSeconds = 1.1f;

// How far from the camera a body is drawn at all, in tiles. MU's camera is fixed and close and
// sees about twenty tiles; posing all 290 of Lorencia's bodies every frame would spend the
// crowd's whole account on figures nobody can see. Foundation 7's "ranges per kind", applied to
// the one kind this sprint draws.
constexpr float kDrawRange = 32.0f;

// The three lengths a walk is made of, all three MU2's own (`client/core/Crowd.cs`) and all
// three marked there as not MU's: MU has no fade and no wait and stands a man up on the frame
// he arrives.
//
// Setting off and stopping are not the same change, which is why they are not the same number.
constexpr float kGaiting = 0.1f;    // standing into a walk: MU2's 0.15 slid for its length
constexpr float kHalting = 0.08f;   // a walk into standing: an arrival, already late
constexpr float kCoasting = 0.1f;   // a monster's patience before a still body is a stopped one
// The most a clip may be hurried. MU2 clamps the rate to [0.25, 4]; the floor is not kept here
// because zero is a rate this engine means -- a body covering no ground has its feet stop, and
// a quarter-speed walk under a body that is not moving is the slide the floor was hiding.
constexpr float kFastestClip = 4.0f;

// MU's own action numbers for a swing, by the stance the figure holds its weapon in
// (index.json's `actions` table: 38 "Attack fist", 39 "Attack sword right 1", 43 "Attack two
// hand sword 1", 46 "Attack spear 1", 47 "Attack scythe 1"). A monster's numbers are its OWN
// table and not these -- `monster_actions` 3 is "Attack 1" -- which is the trap figures.h
// names, so the two are looked up separately below and never out of one table.
int attackSlotFor(const std::string& stance) {
    if (stance == "sword") return 39;
    if (stance == "two_hand_sword") return 43;
    if (stance == "spear") return 46;
    if (stance == "scythe") return 47;
    if (stance == "bow") return 50;
    if (stance == "crossbow") return 51;
    return 38;  // bare hands
}

// An angle folded into a half turn either side of nothing, so that a body a few degrees the
// other side of due north turns the short way. The sim has its own copy (realm.cpp's `wrapped`)
// and this is deliberately not shared with it: the sim must not grow a dependency on the
// drawing, and an angle is four lines.
float wrapped(float angle) {
    constexpr float kTurnabout = 6.28318530718f;
    angle = std::fmod(angle, kTurnabout);
    if (angle > 3.14159265359f) angle -= kTurnabout;
    if (angle < -3.14159265359f) angle += kTurnabout;
    return angle;
}

// What the character is drawn as when nobody dressed him: the cook's own armoured Dark Knight,
// which is what every run before there was a game used. A game hands `heroLook` in instead --
// the naked class body with the cradle's weapon in its hand -- and sprint 9's character select
// is what decides which one that is.
const char* kHeroFigure = "DarkKnight";

// Figures::dress's own `name` for the hero, matching World::play's first dress -- `redress`
// asks for the same key so it replaces that same FigureBody rather than piling up another one.
const char* kHeroDressName = "Hero";

}  // namespace

bool Play::open(const std::string& assetDir, const std::string& world,
                const content::Ground* ground, Figures* figures, uint64_t seed, int kin,
                int level, int column, int row, const std::string& weapon,
                const std::string& shield, const FigureBody* heroLook, const std::string& bareName) {
    ground_ = ground;
    figures_ = figures;
    bare_ = bareName;
    const std::string path = core::join(assetDir, "cooked/" + world + "/" + world + ".mur");
    std::string error;
    if (!content::loadTables(path, tables_, error)) {
        core::logError("no rules for %s: %s (tools/cook.py --world %s --only tables)",
                       world.c_str(), error.c_str(), world.c_str());
        return false;
    }

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

    if (!realm_.raise(&tables_, seed, column, row, sim::Kin(kin), level)) return false;

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

    // A figure for every body, made once. Bodies are never added or removed after the realm is
    // raised -- a dead monster is a body waiting for its respawn -- so this list is as fixed as
    // the realm's own, and a frame walks it without allocating.
    drawn_.clear();
    drawn_.reserve(realm_.bodies().size());
    size_t bones = 0, dressed = 0, bare = 0;
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
        } else if (figures_) {
            look = figures_->body(tables_.kinds[size_t(body.kind)].figure);
        }
        if (look) {
            const float at[3] = {0, 0, 0};
            one.figure.stand(look, at, 0.0f, look->scale);
            bones = std::max(bones, look->boneCount());
            ++dressed;
            // The swing, found once. A player's stance decides which of MU's attack clips it
            // is; a monster has its own two and takes the first it has.
            if (look->library) {
                if (body.player) {
                    one.attackClip = look->library->find(attackSlotFor(look->stance));
                    if (one.attackClip < 0) one.attackClip = look->library->find(38);
                } else {
                    one.attackClip = look->library->find(3);
                    if (one.attackClip < 0) one.attackClip = look->library->find(4);
                }
            }
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
    const float metresPerTile = ground_ ? ground_->metresPerTile() : 1.0f;
    for (size_t i = 0; i < tables_.folk.size(); ++i) {
        const content::Townsperson& person = tables_.folk[i];
        const FigureBody* look =
            person.figure.empty() || !figures_ ? nullptr : figures_->body(person.figure);
        if (!look) continue;
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
        one.figure.stand(look, at, yaw, look->scale, true);
        if (look->idleClip >= 0) one.figure.play(look->idleClip);
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
            one.figure.stand(look, spot.position, spot.yaw, spot.scale, true);
            if (look->idleClip >= 0) one.figure.play(look->idleClip);
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

    core::logf("play: %zu bodies, %zu of them with a figure to wear (%zu have none cooked), "
               "hero at tile %d,%d", drawn_.size(), dressed, bare, realm_.hero().column(),
               realm_.hero().row());
    return true;
}

void Play::shutdown() {
    drawn_.clear();
    scratch_.clear();
    accumulator_ = 0.0;
}

void Play::remember() {
    for (Drawn& one : drawn_) {
        one.wasX = one.nowX;
        one.wasY = one.nowY;
        one.wasFacing = one.nowFacing;
        if (const sim::Body* body = realm_.find(one.id)) {
            one.nowX = body->x;
            one.nowY = body->y;
            one.nowFacing = body->facing;
        }
        // What the tick just gone actually covered, in metres a second. Taken from the two
        // positions rather than from the body's `speed`, because those two are not the same
        // number the moment anything interferes: a tick spent turning on the spot covers no
        // ground, a step refused by the grid covers no ground, and the arrival tick covers
        // whatever was left of the tile rather than a whole one. The feet follow what happened.
        const float metresPerTile = ground_ ? ground_->metresPerTile() : 1.0f;
        const float dx = (one.nowX - one.wasX) * metresPerTile;
        const float dy = (one.nowY - one.wasY) * metresPerTile;
        one.groundSpeed = std::sqrt(dx * dx + dy * dy) / float(kTickSeconds);
    }
}

void Play::update(double seconds) {
    if (!isOpen()) return;
    accumulator_ += seconds;
    int stepped = 0;
    const int64_t started = bx::getHPCounter();
    // A click is answered on the frame it is made. Waiting for the tick that was due anyway
    // cost 0 to 50 ms, 25 on average, between the press and the first step -- the one delay
    // in the walk a hand can feel. So the tick runs now and the tick clock starts again from
    // here: the sim still ticks twenty times a second, one interval is simply cut short.
    // Invention; MU answers a click on its next frame, and so did this at 20 Hz.
    //
    // ONLY for a click that sets him off from a stand, and at most one every kEarlyApart.
    // Given to every click it was a way to run the sim faster than 20 Hz: a hand spamming
    // clicks mid-walk cut every interval short, so he walked faster than he walks and so did
    // everything else, in a stutter. Mid-walk the delay is not felt anyway -- he is already
    // moving, and the new route takes over on the next tick from where he stands.
    //
    // The picture must not jump for it. Every body is drawn part way between its last two
    // ticks, and a tick taken early would move that drawn point on by whatever was left of the
    // interval -- a few centimetres of pop for everything walking. So each body's drawn
    // position is caught first and becomes the `was` of the new tick, and the drawing carries
    // on from exactly where it was.
    sinceEarly_ += float(seconds);
    const bool early = stepNow_ && accumulator_ < kTickSeconds;
    stepNow_ = false;
    if (early) sinceEarly_ = 0.0f;
    if (early) {
        for (Drawn& one : drawn_) {
            one.caughtX = one.wasX + (one.nowX - one.wasX) * through_;
            one.caughtY = one.wasY + (one.nowY - one.wasY) * through_;
            one.caughtFacing = one.wasFacing + wrapped(one.nowFacing - one.wasFacing) * through_;
        }
        accumulator_ = kTickSeconds;
    }
    while (accumulator_ >= kTickSeconds && stepped < kMostTicks) {
        realm_.step();
        // AFTER the step, not before it. Before, `now` held the state at the START of the tick
        // and `was` the start of the one before, so the picture trailed the sim by one whole
        // tick on top of the interpolation's own -- a figure at `through_ = 0` was 100 ms
        // behind what the sim had already decided. Now the two ends really are the ticks
        // either side of where the clock stands, which is what the comment below claims.
        remember();
        if (early && stepped == 0) {
            for (Drawn& one : drawn_) {
                one.wasX = one.caughtX;
                one.wasY = one.caughtY;
                one.wasFacing = one.caughtFacing;
            }
        }
        sim::audit(realm_, findings_);
        const uint32_t heroId = realm_.hero().id;
        for (const sim::Happening& happening : realm_.happenings()) {
            // The marker, off the realm's own word for where the walk ends: `Walked` carries
            // the goal the route was planned to, after the router moved it out of any wall,
            // so the marker is where he will stand and not where the pointer was.
            if (happening.who == heroId && ground_) {
                const float metresPerTile = ground_->metresPerTile();
                const float x = (float(happening.a) + 0.5f) * metresPerTile;
                const float z = -(float(happening.b) + 0.5f) * metresPerTile;
                if (happening.what == sim::What::Walked && mark_) {
                    marker_.show(x, z);
                } else if (happening.what == sim::What::Halted ||
                           happening.what == sim::What::Died) {
                    marker_.dismiss();
                }
            }
            // A swing is drawn because it is a POSE and not an effect: the blood, the number,
            // the fall and the health plate that hang off the landing are sprint 6's, and none
            // of them is here. What a blow does to the picture today is put the attacker into
            // its attack clip, which then blends back to idle or walk when it ends.
            if (happening.what == sim::What::Hit || happening.what == sim::What::Missed) {
                if (Drawn* swinger = drawnOf(happening.who)) {
                    if (swinger->attackClip >= 0 && swinger->figure.body()) {
                        swinger->figure.play(swinger->attackClip, true);
                        // The clip has to fit between two blows, and MU's own reason is that
                        // the attack speed makes the CLIP run faster -- the swing rate follows
                        // from that, so anything that plays the animation has to apply the same
                        // scaling or the man swings at one speed and connects at another
                        // (Beast.cs:820-826). Only ever faster: a monster whose row gives it
                        // 1.4 s between blows plays its half-second swing at its own pace and
                        // waits, as MU does, rather than being smeared out to fill the gap.
                        const sim::Body* body = realm_.find(happening.who);
                        const float between =
                            body ? float(body->swingTicks) * float(kTickSeconds) : 0.0f;
                        const float clip = swinger->figure.length();
                        swinger->swingPace =
                            (between > 0.01f && clip > between) ? clip / between : 1.0f;
                        swinger->swinging = clip / swinger->swingPace;
                        ++swinger->swingToken;

                        // And the cue, which is the only thing sprint 6 adds here. The blow
                        // has ALREADY resolved -- the roll, the damage and the death are on
                        // the tick, above, in the sim -- and this decides when it is SHOWN.
                        //
                        // Halfway through the swing as it will actually be drawn, which is
                        // why the fuse comes off `swinging` and not off the clip's authored
                        // length: at haste the swing plays faster and the cue has to move
                        // with it. MU puts the sound and the number on the swing's FIRST key
                        // because ReceiveAttackDamage does all three in one handler, and on
                        // the first key the arm has not moved yet.
                        Cue cue;
                        cue.attacker = happening.who;
                        cue.target = happening.whom;
                        cue.damage = happening.a;
                        cue.miss = happening.what == sim::What::Missed;
                        cue.fuse = swinger->swinging * Showing::kLandingPoint;
                        cue.token = swinger->swingToken;
                        showing_.schedule(cue);
                    }
                }
            }
            // Everything but the tile crossings, which are most of the log and none of the
            // news. The line is what the run is read by until sprint 6 draws any of it.
            if (happening.what == sim::What::Stepped) continue;
            lastLine_ = sim::describe(happening, realm_);
        }
        // The ask was taken on this tick, whatever became of it.
        mark_ = false;
        accumulator_ -= kTickSeconds;
        ++stepped;
    }
    marker_.update(float(seconds));
    if (appearing_) {
        appearAt_ += float(seconds);
        if (appearAt_ >= kAppearSeconds) appearing_ = false;
    }
    if (stepped > 0) {
        tickMs_ = double(bx::getHPCounter() - started) * 1000.0 /
                  double(bx::getHPFrequency()) / double(stepped);
        // Whatever is left over after the cap is forgiven rather than owed. See kMostTicks.
        if (accumulator_ >= kTickSeconds * kMostTicks) accumulator_ = 0.0;
    }
    through_ = float(std::min(1.0, accumulator_ / kTickSeconds));
    follow(float(seconds));
    for (Drawn& one : drawn_) {
        // A swing runs at its own pace, a walk at the ground's, everything else at the clip's
        // own. `follow` decided which of the three this is; the crossfade runs in real seconds
        // either way, which is why the rate goes in as a rate rather than as a scaled delta.
        one.figure.update(float(seconds), one.clipRate);
        if (one.swinging > 0.0f) one.swinging -= float(seconds);
    }
    for (Standing& one : folk_) {
        one.figure.update(float(seconds));
        // A clip that has come round is a clip that has finished: the next is rolled then,
        // so the town's people are never in step with each other or with themselves.
        if (one.cycles && one.figure.clock() < one.lastClock) {
            const int next = fidget(one);
            if (next != one.figure.clip()) one.figure.play(next, true);
        }
        one.lastClock = one.figure.clock();
    }

    // --- sprint 6: the landing cue ------------------------------------------------------
    // On the DRAWING's clock and after the swings have been advanced above, so that a cue
    // and the swing it belongs to are read at the same instant. Everything a blow does, it
    // does in one frame: MU2 learned that the hard way when Struck, Hurt and Slain each
    // showed their part the moment the realm called them, and a monster began falling four
    // tenths of a second before the number that killed it appeared over the corpse.
    showing_.advance(float(seconds), due_);
    for (const Cue& cue : due_) {
        const Drawn* swinger = drawnOf(cue.attacker);
        // The gate. A cue belongs to one swing, and if the body has moved on -- a step
        // cancels a swing here -- the cue drops itself. A dropped cue costs a splash and a
        // number and never a fact: the damage was taken on the tick either way.
        if (swinger == nullptr || swinger->swingToken != cue.token || swinger->swinging <= 0.0f) {
            showing_.drop();
            continue;
        }
        const sim::Body* target = realm_.find(cue.target);
        if (target == nullptr || ground_ == nullptr) {
            showing_.drop();
            continue;
        }
        // The target where the SIM has it, not where the interpolation has it: this runs
        // before follow() has placed anything this frame, and half a tile of smoothing is
        // below the scatter the blood is thrown with anyway.
        const float metresPerTile = ground_->metresPerTile();
        const float x = (target->x + 0.5f) * metresPerTile;
        const float z = -(target->y + 0.5f) * metresPerTile;
        const float feet[3] = {x, ground_->heightAt(x, z), z};
        // How tall the thing actually is, which is what every length in the blood is taken
        // in units of. A figure with no body drawn falls back to a man's height rather than
        // to zero, because zero would collapse the whole effect to a point.
        const Drawn* hit = drawnOf(cue.target);
        const FigureBody* look = hit ? hit->figure.body() : nullptr;
        const float height = look ? look->height * look->scale : 1.2f;
        const bool onHero = cue.target == realm_.hero().id;
        showing_.land(cue, feet, height, swinger->yaw, onHero);
    }
    showing_.update(float(seconds));
}

void Play::follow(float seconds) {
    if (!ground_) return;
    const float metresPerTile = ground_->metresPerTile();
    for (Drawn& one : drawn_) {
        const sim::Body* body = realm_.find(one.id);
        if (!body || !one.figure.body()) {
            one.visible = false;
            continue;
        }
        // A dead body is not drawn at all this sprint. The fall, the corpse and how long it
        // lies there are the landing cue's business and belong to sprint 6; a monster that
        // vanishes the tick it dies is honest about that, and a monster left standing would
        // not be.
        one.visible = body->alive();
        if (!one.visible) continue;
        // Out of range is not drawn and not posed. Measured in the log beside the drawn count,
        // because foundation 7 says a culling change that is not visible in those numbers did
        // not happen.
        const sim::Body& hero = realm_.hero();
        if (std::max(std::fabs(body->x - hero.x), std::fabs(body->y - hero.y)) > kDrawRange) {
            one.visible = false;
            continue;
        }

        // Between the two ticks either side of where the clock stands. Presentation only:
        // every reach, aim and hit in the sim used the sim's own position, and this one is
        // never read back into it. docs/conventions.md, "Time".
        //
        // Not always evenly, though. The tick a walk ends on covers only what was left of the
        // last tile -- 0.74 of a step, say -- and spread over the whole 50 ms that is the body
        // slowing to a quarter of its pace under feet still striding at full, then standing
        // with the walk still playing: the slide at the end of every walk. So the arrival is
        // drawn at the body's own pace and finishes early, at `arrived` of the way through,
        // and the drawn body stands still on the spot for the rest of the tick. A jump of more
        // than two tiles is a respawn or a gate and is not walked at all.
        const float covered = one.groundSpeed * float(kTickSeconds) / metresPerTile;
        const bool jumped = covered > 2.0f;
        float through = jumped ? 1.0f : through_;
        float arrived = 1.0f;
        if (!body->walking && covered > 1e-4f && covered < body->speed * 0.999f) {
            arrived = covered / body->speed;
            through = std::min(1.0f, through_ / arrived);
        }
        // Whether the DRAWN body is covering ground at this instant. This, and not the sim's
        // `walking`, is what the walk clip answers to: the sim stops a body on a tick and the
        // drawing gets there up to a tick later.
        const bool moving = !jumped && covered > 1e-4f && through_ < arrived;
        const float tileX = one.wasX + (one.nowX - one.wasX) * through;
        const float tileY = one.wasY + (one.nowY - one.wasY) * through;
        const float x = (tileX + 0.5f) * metresPerTile;
        const float z = -(tileY + 0.5f) * metresPerTile;
        // A model looks down +z, and placementTransform negates the angle it is given, so +z
        // ends up along (sin yaw, cos yaw). The sim's facing is an angle in TILE space, where
        // +x is the column and +y is the row -- and the row runs south, which is -z. So the
        // direction of travel in the world is (cos facing, -sin facing) and the yaw that
        // points a model down it is atan2 of those two, in that order.
        // docs/conventions.md, "Space" and "Matrices".
        // The facing between the same two ticks the position is taken between, the short way
        // round. Read raw off the sim it snaps up to 45 degrees at a time; see Drawn::wasFacing.
        const float facing =
            one.wasFacing + wrapped(one.nowFacing - one.wasFacing) * through_;
        const float dx = std::cos(facing);
        const float dz = -std::sin(facing);
        one.yaw = std::atan2(dx, dz);

        const float position[3] = {x, ground_->heightAt(x, z), z};
        // The safe zone is a stance and not only a place: inside one MU carries the weapon on
        // the back and stands in the unarmed idle, and steps out of it with the weapon drawn.
        // `place` moves the weapon; the clip below is the other half of the same rule, and the
        // 0.18 s crossfade in Figure::play is what makes the change a blend rather than a cut.
        const bool safe = tables_.grid.safe(body->column(), body->row());
        one.figure.place(position, one.yaw, safe);
        // How long it has covered no ground. A walk is not always given up on purpose: a step
        // refused because something stood in it, a corner arrived at exactly on the boundary,
        // the gap while a route is replaced, and every tick spent turning on the spot all read
        // as still. Taken at face value each of them drops into the idle and comes straight
        // back out, and because the fade is longer than the blip the man goes soft in the knees
        // at every obstacle. MU2's `Crowd.Coasting` covers them with two ticks of patience.
        one.still = moving ? 0.0f : one.still + seconds;
        // A swing holds until it has played out, and then walk or idle take it back. `play`
        // ignores a request for the clip already running, so the two below are comparisons
        // rather than restarts, and the blend between them is the crossfade's.
        // A step cancels the swing; the two are never drawn at once. MU has no animation that
        // is both, and a figure that keeps swinging while it slides along the ground is the
        // most conspicuous thing in a fight -- measured on the hunt before this line existed,
        // 921 of 1217 swing-frames were played over a walking body, because a fighter re-paths
        // toward a quarry that shuffled and the swing clip outlives the halt that fed it.
        //
        // Cancelling is the whole of the rule: the blow itself already landed on the tick, and
        // nothing downstream reads a fact off the pose. What is lost is the rest of an
        // animation, which is what an attack cancel loses in any game that has one.
        if (one.swinging > 0.0f && body->walking) one.swinging = 0.0f;
        if (one.swinging > 0.0f) {
            one.clipRate = one.swingPace;
            continue;
        }
        const FigureBody* look = one.figure.body();
        int clip = look->idleClip;
        // Which walk this body walks in HERE. Inside a safe zone MU gives PLAYER_WALK_MALE
        // whatever is carried, the walking half of the rule the idle below already keeps: the
        // knight crosses the town square empty-handed with the axe on his back, and draws it
        // as he steps out. Both walks count as "the walk" everywhere below -- stepping over
        // the zone's edge mid-stride is a change of walk, crossfaded and resumed at the same
        // phase, and must not read as stopping and setting off again.
        const int walkHere =
            (safe && look->walkSafeClip >= 0) ? look->walkSafeClip : look->walkClip;
        const auto isWalk = [&](int c) {
            return c >= 0 && (c == look->walkClip || c == look->walkSafeClip);
        };
        // Walking is what the drawn body is doing, and nothing else sets a walk going: a body
        // the sim has walking but still turning on the spot stays in its idle until the first
        // step lands, rather than marching in place through the pivot. Once walking, a turn on
        // the spot mid-walk keeps the walk -- a reversal is two ticks and dropping to the idle
        // for them is a stumble.
        //
        // And the end of a walk is exactly where the drawn body stops, with no patience after
        // it. MU2's coast held the walk a tenth of a second past every stop, which was striding
        // on the spot at every arrival -- "weird walking when he has already stopped". The coast
        // is kept for monsters only, whose chase halts and re-plans between ticks and would
        // flicker to idle without it; the character's walk is replaced, never halted, when a
        // click re-aims it.
        const bool walking = moving || (body->walking && isWalk(one.figure.clip())) ||
                             (!body->player && body->walking && one.still < kCoasting);
        if (walking) {
            clip = walkHere;
        } else if (safe && look->idleSafeClip >= 0) {
            clip = look->idleSafeClip;
        }
        if (clip < 0) continue;

        const int was = one.figure.clip();
        if (clip != was) {
            if (isWalk(clip)) {
                // Setting off: the longest change in the game -- a standing pose to a
                // mid-stride one, where the legs are further apart than in any other
                // transition -- and the one MU's own key length serves worst. Resumed where
                // the cycle left off rather than restarted at one leg fully forward.
                one.figure.play(clip, false, kGaiting);
                one.figure.setClock(one.walkPhase);
            } else {
                if (isWalk(was)) one.walkPhase = one.figure.clock();
                // Coming to a stop is an arrival, drawn on the frame the body stops, and the
                // fade is only long enough not to be a cut: any longer is feet sliding under a
                // body that is no longer going anywhere.
                one.figure.play(clip, false, isWalk(was) ? kHalting : -1.0f);
            }
        }

        // And the rate the clip runs at: the gait's own speed over the ground divided by the
        // speed the clip was authored to travel at. A Dark Knight walks 2.5 m/s and his walk
        // carries 2.4288 m over a 0.933 s cycle, which is 2.60 m/s, so he runs it at 0.961 and
        // his feet keep the earth. MU2's `Crowd.Rate`, and the cook's `travel` is what makes it
        // possible at all.
        //
        // The speed is the body's NOMINAL one and not the ground it covered on the last tick,
        // and that distinction was worth a regression to learn. Measured per tick, the same
        // walk reports 2.5 m/s for most ticks, 0.74 on the tick it arrives on -- a tile is not
        // a whole number of ticks, so the last one covers a fraction -- and 0.0 for every tick
        // spent pivoting. Fed to the clip, those become a step in slow motion at the end of
        // every walk and feet that stop dead while the man turns, which is precisely what the
        // first person to see it said: "foot gets freezed, looks slow motion". The tick
        // quantises movement; a gait does not, and the animation follows the gait.
        one.clipRate = 1.0f;
        if (isWalk(one.figure.clip())) {
            const float metresPerTile = ground_->metresPerTile();
            const float gait = body->speed * metresPerTile / float(kTickSeconds);
            // The clip's own planted foot decides, and the cook's whole-cycle travel is the
            // fallback for a body that plants nothing measurable. The two disagree by 4% on
            // MU's walk, and the stance is the one to believe: `travel` counts the swinging
            // foot as well, which is in the air going the other way at twice the speed, and
            // no eye has ever judged a walk by it. tools/stride.py has both numbers and the
            // slide each leaves.
            // The plant speed of the walk actually playing: the two walks are two clips
            // with two sets of feet, and pacing the unarmed one by the armed one's stride
            // would slide it.
            const float plant =
                (one.figure.clip() == look->walkSafeClip && look->walkSafeClip != look->walkClip
                     ? look->plantSpeedSafe
                     : look->plantSpeed) *
                look->scale;
            const float travel = one.figure.travel();
            const float duration = one.figure.length();
            if (plant > 0.01f) {
                one.clipRate = std::min(gait / plant, kFastestClip);
            } else if (travel > 0.001f && duration > 0.0f) {
                one.clipRate = std::min(gait * duration / travel, kFastestClip);
            }
        }
    }
}

Play::Drawn* Play::drawnOf(uint32_t id) {
    // The bodies are made once and never reordered, and ids are handed out from 1 in that same
    // order, so this is an index and not a search.
    const size_t at = size_t(id) - 1;
    return at < drawn_.size() && drawn_[at].id == id ? &drawn_[at] : nullptr;
}

void Play::focus(float* column, float* row) const {
    if (drawn_.empty()) return;
    const Drawn& hero = drawn_[0];
    *column = hero.wasX + (hero.nowX - hero.wasX) * through_;
    *row = hero.wasY + (hero.nowY - hero.wasY) * through_;
}

void Play::point(const gfx::Camera& camera, const float* view, const float* proj, float pixelX,
                 float pixelY, int width, int height) {
    pointedColumn_ = pointedRow_ = -1;
    pointedAt_ = 0;
    pointedFolk_ = -1;
    pointedLying_ = 0;
    if (!isOpen() || !ground_ || width <= 0 || height <= 0) return;

    // The pixel into a direction. bgfx's clip space on this Metal is 0..1 in z and the origin
    // is top left -- both asked for rather than assumed, as docs/conventions.md insists -- so
    // only y is flipped here and the near plane is z = 0.
    const float ndcX = (2.0f * pixelX / float(width)) - 1.0f;
    const float ndcY = 1.0f - (2.0f * pixelY / float(height));
    float viewProj[16];
    bx::mtxMul(viewProj, view, proj);
    float inverse[16];
    bx::mtxInverse(inverse, viewProj);
    const float nearZ = bgfx::getCaps()->homogeneousDepth ? -1.0f : 0.0f;
    const bx::Vec3 nearPoint = bx::mulH({ndcX, ndcY, nearZ}, inverse);
    const bx::Vec3 farPoint = bx::mulH({ndcX, ndcY, 1.0f}, inverse);
    bx::Vec3 direction = bx::normalize(bx::sub(farPoint, nearPoint));
    if (std::fabs(direction.y) < 1e-5f) return;

    // Down the ray until it is under the land, then a bisection between the last step above and
    // the first below. A flat plane at the camera's own height is the cheap way and it is a tile
    // or two out wherever the town is not level, which is most of Lorencia.
    const float metresPerTile = ground_->metresPerTile();
    const auto below = [&](const bx::Vec3& at) {
        return at.y < ground_->heightAt(at.x, at.z);
    };
    bx::Vec3 above = nearPoint;
    if (below(above)) return;  // the camera is inside the ground: nothing to pick
    float far = 0.0f;
    bool found = false;
    for (float travelled = 0.5f; travelled < 200.0f; travelled += 0.5f) {
        const bx::Vec3 at = bx::add(nearPoint, bx::mul(direction, travelled));
        if (below(at)) {
            far = travelled;
            found = true;
            break;
        }
        above = at;
    }
    if (!found) return;
    float low = far - 0.5f, high = far;
    for (int i = 0; i < 12; ++i) {
        const float middle = (low + high) * 0.5f;
        if (below(bx::add(nearPoint, bx::mul(direction, middle)))) {
            high = middle;
        } else {
            low = middle;
        }
    }
    const bx::Vec3 hit = bx::add(nearPoint, bx::mul(direction, (low + high) * 0.5f));

    // Metres back into tiles: the centre of a tile is its integer coordinate to the sim and
    // (column + 0.5, -(row + 0.5)) in metres to everything else.
    const float column = hit.x / metresPerTile - 0.5f;
    const float row = -hit.z / metresPerTile - 0.5f;
    pointedColumn_ = int(std::lround(column));
    pointedRow_ = int(std::lround(row));
    if (!tables_.grid.inside(pointedColumn_, pointedRow_)) {
        pointedColumn_ = pointedRow_ = -1;
        return;
    }

    // And who is standing there. The nearest body within a tile of the point, which is a scan
    // over a few hundred and is the same scan the sim does: a body is picked by where it IS and
    // not by its drawn mesh, so aiming at a monster's feet and aiming at its head pick the same
    // monster, and what is clicked is what the sim will be asked to fight.
    float closest = 1.2f;
    for (const sim::Body& body : realm_.bodies()) {
        if (body.player || !body.alive()) continue;
        const float away = std::max(std::fabs(body.x - column), std::fabs(body.y - row));
        if (away < closest) {
            closest = away;
            pointedAt_ = body.id;
        }
    }
    // What lies on the ground, only where nothing living is closer: a click on a drop next
    // to a monster is a click on the monster, as MU's own picking orders it.
    if (pointedAt_ == 0) {
        float nearest = 0.8f;
        for (const sim::Lying& one : realm_.lying()) {
            const float away =
                std::max(std::fabs(float(one.column) - column), std::fabs(float(one.row) - row));
            if (away < nearest) {
                nearest = away;
                pointedLying_ = one.id;
            }
        }
    }
    // And the townsfolk, by the same reckoning, a body winning a tie: a monster in the town
    // is the more urgent thing under the pointer.
    for (size_t i = 0; i < tables_.folk.size(); ++i) {
        const content::Townsperson& one = tables_.folk[i];
        const float away =
            std::max(std::fabs(float(one.x) - column), std::fabs(float(one.y) - row));
        if (away < closest) {
            closest = away;
            pointedAt_ = 0;
            pointedFolk_ = int(i);
        }
    }
}

void Play::leftClick() {
    if (!isOpen()) return;
    sim::Request request;
    if (pointedFolk_ >= 0) {
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(pointedFolk_);
    } else if (pointedAt_ == 0 && pointedLying_ != 0) {
        request.kind = sim::Request::Kind::Pick;
        request.target = pointedLying_;
    } else if (pointedAt_ != 0) {
        request.kind = sim::Request::Kind::Attack;
        request.target = pointedAt_;
    } else if (pointedColumn_ >= 0) {
        request.kind = sim::Request::Kind::WalkTo;
        request.column = pointedColumn_;
        request.row = pointedRow_;
    } else {
        return;
    }
    realm_.ask(request);
    // Only from a stand; see Play::update for why never while walking.
    stepNow_ = !realm_.hero().walking && sinceEarly_ >= kEarlyApart &&
               (request.kind != sim::Request::Kind::WalkTo ||
                request.column != realm_.hero().column() || request.row != realm_.hero().row());
    // A walk, a pickup or a talk puts the marker where the walk ends; a fight takes it away,
    // as MU2's did -- an attack never shows one.
    mark_ = request.kind != sim::Request::Kind::Attack;
    if (!mark_) marker_.dismiss();
}

void Play::rightClick() {
    if (!isOpen()) return;
    sim::Request request;
    request.kind = sim::Request::Kind::Stop;
    realm_.ask(request);
    mark_ = false;
    marker_.dismiss();
}

void Play::gather(gfx::Renderer& renderer, const float* viewProj, std::vector<gfx::Drawable>& out,
                  std::vector<gfx::Drawable>* casters) {
    // The character's fade-in, eased at both ends so it neither pops at the start nor lands
    // with a jolt. Only his: the townsfolk and the monsters were already in the world.
    float heroFade = 1.0f;
    if (appearing_) {
        const float t = std::clamp(appearAt_ / kAppearSeconds, 0.0f, 1.0f);
        heroFade = t * t * (3.0f - 2.0f * t);
    }
    for (Drawn& one : drawn_) {
        if (!one.visible || !one.figure.body()) continue;
        if (&one == &drawn_[0] && heroFade <= 0.0f) continue;
        const int bones = one.figure.pose(scratch_.data());
        const int palette = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        if (&one == &drawn_[0] && heroFade < 1.0f) {
            const size_t outFrom = out.size(), castFrom = casters ? casters->size() : 0;
            if (casters) one.figure.gather(palette, *casters);
            one.figure.gather(palette, out);
            for (size_t i = outFrom; i < out.size(); ++i) out[i].fade = heroFade;
            if (casters) {
                for (size_t i = castFrom; i < casters->size(); ++i) (*casters)[i].fade = heroFade;
            }
            continue;
        }
        // The sun's list is every figure, as the town's is: a body behind the camera still
        // casts into the frame, and culling the shadow pass with the camera's frustum is the
        // bug foundation 7 names. The camera's own cull is Crowd's frustum test and is owed
        // here; at Lorencia's 290 bodies, of which a handful are ever near the camera, it is
        // the next thing to do and not this sprint's.
        if (casters) one.figure.gather(palette, *casters);
        one.figure.gather(palette, out);
    }
    for (Standing& one : folk_) {
        const int bones = one.figure.pose(scratch_.data());
        const int palette = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        if (casters) one.figure.gather(palette, *casters);
        one.figure.gather(palette, out);
    }
}

void Play::settle(Standing& one) {
    const FigureBody* look = one.figure.body();
    if (!look || !look->library) return;
    // Its own clips only. A townsperson on the player's rig -- the guards -- would roll
    // among dying, sitting and casting, which is why MU2 named them a single clip.
    const size_t count = look->library->clips.clips.size();
    one.cycles = look->library->name != "player" && count > 1;
    if (!one.cycles) return;
    // Seeded from where it stands, so a town comes up the same way every time and two
    // figures of a kind do not share a roll.
    const float* at = one.figure.position();
    one.dice = uint32_t(int32_t(at[0] * 37.0f) * 73856093 ^ int32_t(at[2] * 61.0f) * 19349663) | 1u;
    one.figure.play(fidget(one), true, 0.0f);
    const float length = one.figure.length();
    one.dice ^= one.dice << 13;
    one.dice ^= one.dice >> 17;
    one.dice ^= one.dice << 5;
    if (length > 0.0f) one.figure.setClock(float(one.dice % 1000u) / 1000.0f * length);
    one.lastClock = one.figure.clock();
}

int Play::fidget(Standing& one) {
    const size_t count = one.figure.body()->library->clips.clips.size();
    const auto roll = [&one](uint32_t below) {
        one.dice ^= one.dice << 13;
        one.dice ^= one.dice >> 17;
        one.dice ^= one.dice << 5;
        return one.dice % below;
    };
    if (count < 2 || roll(16) < 12) return 0;
    return 1 + int(roll(uint32_t(count - 1)));
}

bool Play::spendPoint(int stat) {
    static const char* const kStats[4] = {"strength", "agility", "vitality", "energy"};
    if (stat < 0 || stat > 3) return false;
    const bool spent = realm_.spend(stat == 0, stat == 1, stat == 2, stat == 3);
    core::logf("window: a point into %s %s", kStats[stat],
               spent ? "spent" : "refused, none in hand");
    return spent;
}

bool Play::moveItem(int from, int to) {
    const bool moved = realm_.moveItem(from, to);
    core::logf("window: move %d -> %d %s", from, to, moved ? "taken" : "refused");
    if (moved) redress();
    return moved;
}

bool Play::useItem(int slot) {
    const bool used = realm_.useItem(slot);
    core::logf("window: use %d %s", slot, used ? "taken" : "refused");
    return used;
}

// The satchel is the truth (docs/sprints/07-the-windows.md) and Realm::rearm already reads
// `hero.weapon` and `hero.shield` off it on every move that touches a worn slot; this is that
// same rule kept for the picture. Without it the figure kept whatever `Figures::dress` gave
// him at the door -- Realm::moveItem, "the satchel is the truth" -- and a weapon dragged out
// of his hand went on being drawn in it, because nothing had ever told the figure to look
// again.
void Play::redress() {
    if (!figures_ || bare_.empty() || drawn_.empty()) return;
    const sim::Body& hero = realm_.hero();
    const std::string weapon =
        hero.weapon >= 0 ? tables_.arms[size_t(hero.weapon)].name : std::string();
    const std::string shield =
        hero.shield >= 0 ? tables_.arms[size_t(hero.shield)].name : std::string();
    const FigureBody* look = figures_->dress(kHeroDressName, bare_, weapon, shield);
    if (!look) return;
    Drawn& drawn = drawn_[0];
    drawn.figure.reskin(look);
    // The swing, found again exactly as Play::open finds it the first time: the stance a new
    // weapon stands him in picks a different attack clip out of the same library.
    drawn.attackClip = -1;
    if (look->library) {
        drawn.attackClip = look->library->find(attackSlotFor(look->stance));
        if (drawn.attackClip < 0) drawn.attackClip = look->library->find(38);
    }
}

bool Play::give(const std::string& name, int count) {
    const int32_t item = tables_.itemNamed(name);
    if (item < 0) {
        core::logError("--give: no item named %s", name.c_str());
        return false;
    }
    const content::ItemRow& row = tables_.items[size_t(item)];
    const bool stacks = sim::heals(row) || sim::restores(row);
    const int durability = stacks ? std::max(1, count) : row.durability;
    const int slot = realm_.give(item, -1, 0, durability);
    core::logf("given %s%s into slot %d", row.label.c_str(),
               stacks ? (" x" + std::to_string(durability)).c_str() : "", slot);
    return slot >= 0;
}

bool Play::buy(int shelfSlot) {
    const int slot = realm_.buy(shelfSlot);
    core::logf("window: buy shelf %d %s (slot %d, %lld Zen left)", shelfSlot,
               slot >= 0 ? "taken" : "refused", slot, (long long)realm_.money());
    return slot >= 0;
}

bool Play::sell(int bagSlot) {
    const int64_t paid = realm_.sellItem(bagSlot);
    core::logf("window: sell slot %d %s (%lld paid, %lld Zen now)", bagSlot,
               paid >= 0 ? "taken" : "refused", (long long)paid, (long long)realm_.money());
    return paid >= 0;
}

bool Play::talkTo(const std::string& name) {
    for (size_t i = 0; i < tables_.folk.size(); ++i) {
        if (tables_.folk[i].name.find(name) == std::string::npos) continue;
        sim::Request request;
        request.kind = sim::Request::Kind::Talk;
        request.target = uint32_t(i);
        realm_.ask(request);
        core::logf("talk: walking to %s at (%d, %d)", tables_.folk[i].name.c_str(),
                   tables_.folk[i].x, tables_.folk[i].y);
        return true;
    }
    core::logError("--talk: nobody called %s here", name.c_str());
    return false;
}

void Play::dropsOnScreen(const float* viewProj, int width, int height,
                         std::vector<OnScreen>& out) const {
    out.clear();
    if (!ground_) return;
    const float metresPerTile = ground_->metresPerTile();
    for (const sim::Lying& one : realm_.lying()) {
        const float x = (float(one.column) + 0.5f) * metresPerTile;
        const float z = -(float(one.row) + 0.5f) * metresPerTile;
        // MU2's LabelLift, thirty units over the thing, and the thing a hand's height up.
        const float world[4] = {x, ground_->heightAt(x, z) + 0.4f, z, 1.0f};
        float clip[4];
        bx::vec4MulMtx(clip, world, viewProj);
        if (clip[3] <= 0.0f) continue;
        const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
        if (nx < -1.1f || nx > 1.1f || ny < -1.1f || ny > 1.1f) continue;
        out.push_back({one.id, (nx * 0.5f + 0.5f) * float(width), (0.5f - ny * 0.5f) * float(height)});
    }
}

}  // namespace mu::game
