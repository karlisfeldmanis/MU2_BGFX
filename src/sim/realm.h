// The realm: one map, its bodies, and the fixed 20 Hz tick that moves them.
//
// This layer has never heard of the renderer (PLAN.md foundation 8). It speaks in two lists:
// requests in -- walk here, attack that -- and happenings out -- spawned, stepped, hit, died,
// gained, levelled. The game draws the happenings; windows raise the requests. That is the
// mirror rule kept without a wire, and it is what lets the whole thing run with no window at
// all under `--headless`.
//
// Three rules hold inside a step, and each is a thing that has gone wrong in a sim before:
//
//   * **No allocation, once each vector has seen its worst case.** Every vector here is sized
//     when the realm is raised: the bodies, the router's whole scratch, and a route's worth of
//     tiles on every body. What is NOT a hard bound is a route longer than any that body has
//     yet walked, and `happenings` in a tick busier than any before it -- both grow once and
//     then never again, and neither has been seen to grow after the first few hundred ticks of
//     a run. The claim used to be "and to nothing else", which was simply untrue.
//   * **No clock but the tick.** Nothing reads a wall clock, nothing reads a frame's delta, and
//     every delay was converted to ticks once, in the cook.
//   * **A fixed order, and it is written down.** The player walks and swings, then monsters are
//     roused, think and move in index order, then the dead are considered for respawn. No
//     hash container is ever walked to produce a happening, and every id comes from one
//     monotonic counter.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/tables.h"
#include "sim/random.h"
#include "sim/route.h"
#include "sim/rules.h"

namespace mu::sim {

// What a body is doing about a target, as one field and a switch rather than an object. MU2
// wrote down that a per-monster intelligence object cost an allocation and a virtual call per
// monster per tick for a state machine with one implementation, and at 290 monsters that is
// the whole budget.
enum class Temper : uint8_t {
    Asleep,    // nobody near: not thinking at all
    Wandering, // awake, nothing worth attacking
    Chasing,
    Fighting,
    Homing,    // led too far from its nest, walking back
    Dead,
};

enum class What : uint8_t {
    Spawned,   // a: level, b: health
    Rose,      // respawned
    Roused,    // a: awake, b: the distance that woke it
    Walked,    // a route was planned. a: goal column, b: goal row, c: steps
    Refused,   // and a route that could not be. a: goal column, b: goal row
    Stepped,   // a tile crossed. a: column, b: row
    Halted,
    Missed,    // whom
    Hit,       // a: damage, b: the roll, c: what is left of the defender
    Died,      // whom killed it
    Gained,    // a: experience, b: the total
    Levelled,  // a: the new level, b: points in hand
};

// One thing that happened, flat and copyable. The numbers mean what the enum above says they
// mean; a happening with a body in it carries its id and not a pointer, because ids survive a
// vector that grows and pointers do not.
struct Happening {
    uint32_t tick = 0;
    What what = What::Spawned;
    uint32_t who = 0;
    uint32_t whom = 0;
    int32_t a = 0, b = 0, c = 0;
    // Where it happened, in tiles. Written for everything that has a place, because a log line
    // with a position in it is the one that catches a sim drifting apart from itself.
    float x = 0.0f, y = 0.0f;
};

// A body on the map: the player, or one monster. One struct for both, because the fight reads
// the same six numbers off either side and a second body type is a second damage path.
struct Body {
    uint32_t id = 0;
    int32_t kind = -1;  // index into Tables::kinds; -1 is the player
    bool player = false;

    Kin kin = Kin::DarkKnight;  // the player's class; meaningless on a monster
    HeroPoints points;          // likewise
    // What is in his hands, as indices into Tables::arms, or -1. A monster's weapon is part of
    // its row and not an item: `monster_kinds` carries the damage band whole.
    int32_t weapon = -1;
    int32_t shield = -1;
    int32_t level = 1;
    int32_t health = 0;
    int32_t maxHealth = 0;
    Fighter stats;

    // Tiles, and a tile's centre is its integer coordinate -- MU2's own reckoning
    // (Things.cs:71-82, `Column => (int)MathF.Round(X)`). The world's metres and the negation
    // of the row belong to whoever draws this, not here.
    float x = 0.0f, y = 0.0f;
    // Where it is looking and where it wants to look. Two fields and not one, because MU2
    // found that a body which snaps its facing reads as cheap and a body which turns at the
    // DRAWING's own rate is worse: the movement depends on the turn -- a thing does not set off
    // until it has roughly come round -- so the turn belongs down here and the viewer reads it.
    // Walker.cs:70-97.
    float facing = 0.0f;  // radians, where it is looking now
    float aim = 0.0f;     // radians, where it means to look
    bool turning = false; // too far off its aim to cover ground this tick

    // Where it was put down, which is what a leash measures from. Not the nest rectangle:
    // Lorencia's spiders share one 47 by 155 tiles across, and a rectangle leash would let a
    // spider be led from one end of the field to the other and still count as home.
    int32_t homeColumn = 0, homeRow = 0;

    // The walk. `route` is the tiles left to cross and keeps its capacity between plans.
    std::vector<Step> route;
    size_t onStep = 0;
    bool walking = false;
    float speed = 0.125f;  // tiles a tick: one over the breed's own moveTicks

    Temper temper = Temper::Asleep;
    uint32_t quarry = 0;  // an id, 0 for nobody
    bool provoked = false;
    int64_t swingsAt = 0;
    int64_t thinksAt = 0;
    int64_t repathsAt = 0;
    int64_t risesAt = 0;
    int32_t swingTicks = 20;  // how often he may swing: the length of the clip he swings with
    int32_t swingMs = 0;      // the same before it was rounded to ticks, for the log
    // Where the quarry was when this chase was last planned, in tiles and NOT in whole tiles.
    // See Realm::drifted.
    float chaseX = 0.0f, chaseY = 0.0f;

    uint64_t experience = 0;
    int32_t pointsInHand = 0;  // won by levelling and not yet spent

    bool alive() const { return health > 0; }
    int column() const { return int(x + (x < 0.0f ? -0.5f : 0.5f)); }
    int row() const { return int(y + (y < 0.0f ? -0.5f : 0.5f)); }
};

// What the game asks the sim for. Nothing here is a skill, and that is on purpose: PLAN.md
// decided the skill system is Diablo 3's shape -- learned permanently, four keys, real
// cooldowns -- and the one thing this sprint owes it is not baking in MU's assumptions. An
// attack is a request to fight a body, not a swing fired from an item, and the swing clock it
// runs on is per-body and already separate from the thinking clock, so a per-skill cooldown
// goes beside it rather than through it.
struct Request {
    enum class Kind : uint8_t { None, WalkTo, Attack, Stop } kind = Kind::None;
    int32_t column = 0, row = 0;
    uint32_t target = 0;
};

struct RealmCounts {
    uint32_t monsters = 0;
    uint32_t alive = 0;
    uint32_t roused = 0;
    uint32_t walking = 0;
};

class Realm {
public:
    // Raises the map: every nest placed, the player put down on a standable tile near
    // (column, row), the router opened. Allocates here and, apart from happenings and the
    // route vectors growing once to their high-water mark, nowhere else.
    bool raise(const content::Tables* tables, uint64_t seed, int playerColumn, int playerRow,
               Kin kin = Kin::DarkKnight, int level = 1);

    void ask(const Request& request) { pending_ = request; }

    // Points the player has won and not yet spent, put into his four stats. A real action --
    // sprint 7's stat window raises it, and sprint 9's save writes what came of it -- and it
    // is here rather than in raise() because spending a point is a choice and the sim does not
    // make choices. Refused, whole, when it asks for more points than are in hand.
    bool spend(int strength, int agility, int vitality, int energy);

    // Puts a weapon and a shield in the character's hands, by index into the cooked arms, -1
    // for an empty hand. Refused, whole and with a reason in the log, when his class may not
    // hold it or his strength and agility do not meet what it asks. There is no bag and no
    // durability behind this -- both are sprint 7's -- and no swing speed either: MU paces a
    // swing by the attack clip's authored length, and inventing a mapping from a weapon's
    // `attack_speed` is the one thing this sprint will not do.
    // `given` is the cradle's exemption and nothing else: a character is CREATED holding what
    // his class is given, and the strength and agility the item asks are not tested then. That
    // is OpenMU's own shape -- AddSmallAxeForDarkKnight writes the axe straight into the hand
    // slot, and the requirement check lives in whatever moves an item into a slot afterwards --
    // and it is load-bearing rather than a convenience: a level-one Dark Knight has 28 strength
    // and the Small Axe wants 50, so a knight made the strict way starts the game punching.
    // MU2's `Beast.Wield` records the same decision. Everything a player does later goes
    // through the check.
    bool equip(int32_t weapon, int32_t shield, bool given = false);
    // Why the last equip was refused, or empty.
    const std::string& refusal() const { return refusal_; }
    void step();

    int64_t tick() const { return tick_; }
    const std::vector<Happening>& happenings() const { return happenings_; }
    const std::vector<Body>& bodies() const { return bodies_; }
    const Body* find(uint32_t id) const;
    const Body& hero() const { return bodies_[0]; }
    const content::Tables* tables() const { return tables_; }
    const Router& router() const { return router_; }
    uint64_t draws() const { return dice_.draws(); }
    RealmCounts counts() const;

private:
    Body* body(uint32_t id);
    Arms armsOf(const Body& one) const;
    void reswing(Body& hero);
    void advance(Body& one);
    bool turn(Body& one);
    void rouse(Body& beast);
    void think(Body& beast);
    void press();
    void wander(Body& beast);
    void retreat(Body& beast);
    void engage(Body& one, const Body& target);
    void strikeAt(Body& attacker, Body& target);
    void kill(Body& beast, Body& killer);
    void gain(Body& hero, int32_t award);
    void raiseBeast(Body& beast);
    void reviveHero();
    bool send(Body& one, int column, int row);
    void halt(Body& one);
    bool beside(const Body& target, int radius, const Body& walker, int* column, int* row);
    bool drifted(const Body& chaser, const Body& target) const;
    bool worth(const Body& beast, const Body& target, int range) const;
    void say(What what, const Body& who, int32_t a = 0, int32_t b = 0, int32_t c = 0,
             uint32_t whom = 0);

    const content::Tables* tables_ = nullptr;
    Random dice_{0};
    Router router_;
    std::vector<Body> bodies_;  // [0] is the player; the rest are monsters, in spawn order
    // Who is a player, by index, and where an id lives. Both are lists and not maps: an
    // unordered_map walked to produce a happening is the first thing the census warns about,
    // and ids here are handed out by one counter from 1, so the second is a plain lookup.
    // Without them, rousing 1005 monsters scanned 1005 bodies apiece -- a million comparisons
    // a tick, measured at 0.33 ms on Noria against Lorencia's 0.03, which is the whole budget
    // for a map with nothing happening on it.
    std::vector<uint32_t> players_;
    std::vector<uint32_t> indexOfId_;
    std::vector<Happening> happenings_;
    std::vector<Step> scratch_;
    Request pending_;
    Request order_;  // what the player is doing until told otherwise
    int64_t tick_ = 0;
    std::string refusal_;
    uint32_t nextId_ = 1;
};

// The one line a happening becomes in the seeded log. Fixed precision throughout: a `%g` of a
// float is a byte difference waiting for a compiler change, and the log's own formatting is
// part of the contract the two runs are compared under.
std::string describe(const Happening& happening, const Realm& realm);

}  // namespace mu::sim
