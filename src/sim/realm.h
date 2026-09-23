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
#include <utility>
#include <vector>

#include "content/tables.h"
#include "sim/items.h"
#include "sim/market.h"
#include "sim/random.h"
#include "sim/route.h"
#include "sim/rules.h"
#include "sim/skills.h"

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
    Drank,     // a potion's whole worth begun: a: the total, b: 1 for mana, 0 for health
    Served,    // a merchant's counter opened: a: the townsperson's index, b: MU's NPC number
    Bought,    // a: the item row, b: the price, c: the bag slot
    Sold,      // a: the item row, b: what was paid, c: the bag slot it left
    Dropped,   // something left on the ground: a: its id, b: the item row or -1 for Zen,
               // c: the Zen or the plus
    Picked,    // a: its id, b: the bag slot or -1 for Zen, c: the Zen
    Vanished,  // a: its id: it lay too long
    Swung,     // a blow BEGUN, at the top of the swing: a: the skill's number or 0, whom: at whom
    Cast,      // a skill thrown: a: its number, b: the cooldown it set in ticks, whom: at whom
    Shoved,    // the knock: a: the column it was put on, b: the row
    Learned,   // a: the skill's number
};

// A thing on the ground: an item or a pile of Zen, where a death left it, until it is picked
// up or it has lain its minute. MU2's `Lying`: an id the drawing knows it by, a tile, and a
// tick it vanishes on -- none of which survive being picked up, which is why it is not a Held.
struct Lying {
    uint32_t id = 0;
    Held what;            // empty for Zen
    int64_t zen = 0;
    int32_t column = 0, row = 0;
    int64_t vanishesAt = 0;
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
    // The player's mana. Zero on a monster, which casts nothing in 0.75's Lorencia.
    int32_t mana = 0;
    int32_t maxMana = 0;
    // SD, the shield: Season 3's pool in front of health, MU2's Player.Shield. Zero on a
    // monster, so a blow on one lands whole. 0.75 has none; this is the hybrid MU2 chose.
    int32_t sd = 0;
    int32_t maxSd = 0;
    float sdCarry = 0.0f;  // the fraction of a point a recovery tick owes and did not give
    float manaCarry = 0.0f;  // and the same for mana, whose share of a small pool is under one
    Fighter stats;
    // What the player's worn pieces add, off the satchel at the last rearm: the armour and
    // shield's defence with their plus counted, and the weapon's plus on its damage band.
    int32_t wornDefense = 0;
    int32_t wornDefenseRate = 0;
    int32_t weaponBonus = 0;

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
    // Until when it stands over what it has just killed instead of turning away. See
    // Realm::think and kStandOverTicks; it is the one thing in this layer put there for the
    // sake of what the screen shows.
    int64_t standsUntil = 0;
    int32_t swingTicks = 20;  // how often he may swing: the length of the clip he swings with
    int32_t swingMs = 0;      // the same before it was rounded to ticks, for the log
    // Where the quarry was when this chase was last planned, in tiles and NOT in whole tiles.
    // See Realm::drifted.
    float chaseX = 0.0f, chaseY = 0.0f;

    uint64_t experience = 0;
    int32_t pointsInHand = 0;  // won by levelling and not yet spent

    // ---- skills (docs/skills-dk.md) --------------------------------------------------------
    // What he has learned, as a bit per row of the skill table and NOT per skill number: the
    // mask is six wide and the save writes it whole. Learned permanently -- a skill is not a
    // property of what is in his hands, which is 0.75's shape and is the one the design leaves
    // on purpose.
    uint32_t learned = 0;
    // When each learned skill may be thrown again, by the same index. The cooldown is the one
    // thing 0.75 has no equivalent of, so it lives beside the swing clock rather than through
    // it -- a cast pays BOTH, which is what stops haste outrunning the animation.
    int64_t cools[kSkills] = {};
    // A boon: what it multiplies incoming damage by and when it lapses. One at a time, because
    // 0.75 has exactly one skill that grants one and the elf's two are the same shape when they
    // arrive.
    // While a skill's own clip is running he does not turn and is not moved: the blow is thrown
    // where he was standing and facing when he threw it. The user's rule, 2026-09-22 -- a body
    // that swivels or slides mid-skill reads as a teleport, which is the same objection that took
    // the gap-closers out.
    // A blow in the air. The swing is DECIDED on one tick and LANDS on another, half a clip
    // later, which is where the drawing has always shown it (`Showing::kLandingPoint`). Until
    // 2026-09-23 the sim resolved it at the top of the swing instead, so a click that cancelled
    // the attack still did its damage -- the player's own report, and it was the two clocks
    // disagreeing rather than anything about skills.
    int64_t blowAt = 0;        // 0 for nothing in the air
    uint32_t blowTarget = 0;
    float blowForce = 1.0f;    // a skill's multiplier, 1 for a swing
    int32_t blowSkill = 0;     // which skill it belongs to, 0 for a swing
    int64_t castUntil = 0;
    int64_t boonUntil = 0;
    float boonDamageTaken = 1.0f;
    int32_t boonSkill = 0;

    bool alive() const { return health > 0; }
    int column() const { return int(x + (x < 0.0f ? -0.5f : 0.5f)); }
    int row() const { return int(y + (y < 0.0f ? -0.5f : 0.5f)); }
};

// What a save keeps of the hero: everything the player earned and chose, and nothing the sim
// works out again from it. Health, stats and swing speed are reckoned from level, points and
// what is worn (Realm::rearm), so they are not here; health and mana ARE, because a hero who
// quits hurt comes back hurt. Where he stands and his class go to raise(), which already
// finds a free tile and dresses the class -- this is the rest, laid on top.
struct HeroRecord {
    Kin kin = Kin::DarkKnight;
    int32_t column = 0, row = 0;
    float facing = 0.0f;
    int32_t level = 1;
    uint64_t experience = 0;
    int32_t pointsInHand = 0;
    HeroPoints points;
    int32_t health = 0, mana = 0;
    int64_t money = 0;
    // What he has learned, by the skill table's own index. Saved because learning is permanent
    // in this design and is the one thing about a skill that is his rather than his weapon's.
    // Cooldowns are NOT saved: a character who quits mid-fight is not owed his four seconds.
    uint32_t learned = 0;
    Held slots[kSlots];
};

// What the game asks the sim for. Nothing here is a skill, and that is on purpose: PLAN.md
// decided the skill system is Diablo 3's shape -- learned permanently, four keys, real
// cooldowns -- and the one thing this sprint owes it is not baking in MU's assumptions. An
// attack is a request to fight a body, not a swing fired from an item, and the swing clock it
// runs on is per-body and already separate from the thinking clock, so a per-skill cooldown
// goes beside it rather than through it.
struct Request {
    // Talk: walk to a townsperson (`target` is his index in Tables::folk) and, within the
    // counter's reach, be served. Any other order closes the counter.
    // Pick: walk to a thing on the ground (`target` is its id) and take it on arrival.
    enum class Kind : uint8_t { None, WalkTo, Attack, Stop, Talk, Pick } kind = Kind::None;
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

    // ---- skills (docs/skills-dk.md) --------------------------------------------------------
    // A key pressed: throw this skill at this body. NOT a Request, and that is the design and
    // not a shortcut -- a press does not replace the standing attack order, it spends the next
    // swing on the skill and leaves the order standing, so the knight goes on fighting
    // afterwards without a second click. MU2's `Realm.Cast` keeps the ask in `Player.Wants` for
    // the same reason and its remark is worth reading.
    //
    // Remembered rather than dropped when it arrives mid-swing: the tick rate is 20 and a key is
    // pressed on a frame, so refusing an ask that lands inside the swing it is waiting for would
    // lose most presses. It is held for `kWishTicks` and thrown by the first tick that can.
    // Every refusal is silent, as `Swing`'s and `Move`'s are: the interface asks, and a no is a
    // message that does not come back.
    void invoke(int32_t skill, uint32_t at);
    // Learning, which in this design is permanent and saved: an orb consumed sets a bit. Nothing
    // in 0.75 does this -- the knight's skills were carried by the weapon in his hand -- so it
    // is `invention`, argued in the doc's §3.3.
    bool learn(int32_t skill);
    bool knows(int32_t skill) const;
    // Ticks left on a skill's cooldown, and the whole cooldown it was set to, which is what the
    // frame needs to draw a sweep. Zero and zero when it is ready.
    int64_t cooling(int32_t skill) const;
    int32_t coolsFor(int32_t skill) const;

    // Points the player has won and not yet spent, put into his four stats. A real action --
    // sprint 7's stat window raises it, and sprint 9's save writes what came of it -- and it
    // is here rather than in raise() because spending a point is a choice and the sim does not
    // make choices. Refused, whole, when it asks for more points than are in hand.
    bool spend(int strength, int agility, int vitality, int energy);

    // The hero as a save keeps him, and the same laid back on a hero just raised at that
    // record's tile and class. restore() empties the satchel first, puts back what was carried
    // and worn, re-reckons him off it, and only then sets health and mana, clamped to what he
    // can now hold. A record from a hero who died is brought back full, as reviveHero would.
    HeroRecord record() const;
    void restore(const HeroRecord& saved);

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

    // ---- the satchel (sprint 7) -----------------------------------------------------------
    // What the player carries and wears. The satchel is the truth and his hands are read off
    // it: every change to a worn slot re-reckons him (Beast.Rearm).
    const Satchel& satchel() const { return bag_; }
    int64_t money() const { return money_; }
    Wearer wearer() const;
    // A thing put straight into a slot, with no gate but the slot being free: the cradle's
    // axe, and a purchase into the first place it fits. -1 for anywhere in the bag. The slot
    // it went to, or -1 when there was nowhere.
    int give(int32_t item, int slot = -1, int refinement = 0, int durability = 0);
    // A drag from one slot to another, equipping and unequipping included. Refused, whole,
    // where `movable` says no -- the same answer the window colours the cell by.
    bool moveItem(int from, int to);
    // A right-click on a carried thing: drink it. Only potions this sprint. Refused where it
    // is nothing drinkable or the half-second cooldown has not run (RecoverConsumeHandler's
    // CooldownTime); the heal arrives over the next second in three instalments.
    bool useItem(int slot);
    // Zen in and out, for the merchants. `pay` refuses, whole, what he cannot afford.
    void earn(int64_t zen) { money_ += zen; }
    bool pay(int64_t zen);
    // Takes a carried thing out of the bag and hands it back: a sale. Worn things are not
    // sold (Shelf.Offer refuses a source outside the bag, and so does this).
    Held sell(int slot);

    // ---- the merchants (sprint 7) ---------------------------------------------------------
    // The townsperson whose counter is open, as an index into Tables::folk, or -1. Opened by a
    // Talk order arriving within `kCounter` of a merchant, closed by any other order.
    int trading() const { return trading_; }
    void closeTrade() { trading_ = -1; }
    // Buys whatever sits in one of the open shop's slots: priced, the room looked for at its
    // own footprint BEFORE anything is taken, then paid and placed together. The bag slot, or
    // -1 refused. Realm.Buy.
    int buy(int shelfSlot);
    // Sells a carried thing to the open shop: bag slots only, never what is worn. What was
    // paid, or -1 refused. Realm.Sell.
    int64_t sellItem(int slot);
    // Whether he is close enough to be served by this townsperson right now. Asked again on
    // every purchase and sale, not once when the counter opened.
    bool serving(int folk) const;

    // ---- the ground (sprint 7) ------------------------------------------------------------
    const std::vector<Lying>& lying() const { return lying_; }
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
    // The boon's own field put back over what `reckon` just wrote: reckon starts a fighter from
    // his class and his points, so it clears `damageTaken`, and a guard raised before a level-up
    // or a change of armour would lapse silently in the middle of its four seconds.
    void keepBoon(Body& hero);
    void advance(Body& one);
    bool turn(Body& one);
    void rouse(Body& beast);
    void think(Body& beast);
    void accept();  // the pending order becomes the order, before the hero moves
    void press();   // and what the order does once he has
    void wander(Body& beast);
    void retreat(Body& beast);
    void engage(Body& one, const Body& target);
    // `force` is the skill multiplier on the blow, 1 for an ordinary swing. It multiplies the
    // damage after the roll, the defence and the level floor, which is where OpenMU's own
    // `SkillMultiplier` falls (AttackableExtensions.cs:226-247).
    void strikeAt(Body& attacker, Body& target, float force = 1.0f);
    // The player's blow: begun now, landing half a swing from now, and dropped whole if he is
    // given another order before it lands. `land` is what the tick calls when it is due.
    void begin(Body& hero, uint32_t at, float force, int32_t skill, int32_t overTicks);
    void land(Body& hero);
    void dropBlow(Body& hero) { hero.blowAt = 0; hero.blowTarget = 0; }
    // A skill thrown, with the refusals in OpenMU's own order. False and silent for each.
    bool throwSkill(Body& hero, const SkillRow& row, uint32_t at);
    // Whom an area skill catches, in the order it strikes them: nearest first, then clockwise
    // from north, then by id. Written into `victims` and the count returned, never more than
    // `room` -- a fixed array on the caller's stack, because this runs inside a tick.
    int gather(const Body& hero, const SkillRow& row, uint32_t* victims, int room) const;
    // And the blow itself, once the arm comes down: one roll a target, in that order.
    void strikeAround(Body& hero, const SkillRow& row, float force);
    // The knock: one tile at random, onto something standable. 0.75's `movesTarget`.
    void shove(Body& target);
    // How long the clip this skill plays takes, and so what its cooldown cannot go under.
    int32_t clipTicksOf(const Body& hero, const SkillRow& row) const;
    void kill(Body& beast, Body& killer);
    void gain(Body& hero, int32_t award);
    void raiseBeast(Body& beast);
    void reviveHero();
    void rearm(Body& hero);
    void leave(const Body& dead, const Body& killer);
    std::pair<int, int> clearing(int column, int row) const;
    bool bare(int column, int row) const;
    bool take(size_t index);
    void sip();
    void recover(Body& hero);
    bool send(Body& one, int column, int row);
    void halt(Body& one);
    void settle(Body& one);
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
    // The skill a key asked for and whom it was aimed at, held for a few ticks so a press inside
    // the swing it waits for is not lost. Cleared the moment it is thrown or it goes stale.
    int32_t wants_ = skill::kNone;
    uint32_t wantsAt_ = 0;
    int64_t wantsUntil_ = 0;
    int64_t tick_ = 0;
    std::string refusal_;
    uint32_t nextId_ = 1;

    Satchel bag_;
    int64_t money_ = 0;
    int trading_ = -1;
    std::vector<Lying> lying_;
    int64_t potionUntil_ = 0;
    // A potion's worth arrives in three instalments, 20% 60% 20% at 200, 600 and 200 ms
    // (MU2's Realm.Consume, off OpenMU's handler). A fixed ring: a potion every half second
    // and three instalments a potion is at most six in flight.
    struct Sip {
        int64_t due = 0;
        int32_t amount = 0;
        bool mana = false;
    };
    Sip sips_[8];
    int sipCount_ = 0;
};

// The one line a happening becomes in the seeded log. Fixed precision throughout: a `%g` of a
// float is a byte difference waiting for a compiler change, and the log's own formatting is
// part of the contract the two runs are compared under.
std::string describe(const Happening& happening, const Realm& realm);

}  // namespace mu::sim
