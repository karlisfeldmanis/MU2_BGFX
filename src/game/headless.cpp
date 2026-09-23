#include "game/headless.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "content/tables.h"
#include "core/files.h"
#include "core/log.h"
#include "game/play_tuning.h"
#include "sim/audit.h"
#include "sim/realm.h"
#include "sim/skills.h"

namespace mu::game {
namespace {

// The scripted hand: what the player does when there is nobody at the keyboard.
//
// It draws from its OWN generator, seeded separately from the realm's. The census names this
// as a determinism surface and it is the easy one to get wrong: a hand that drew from the
// sim's dice would change every monster's wander the moment the script changed, and two runs
// of different scripts could not be compared at all. Seeded from the run's seed so the hand is
// still reproducible, and never touched by the realm.
class Hand {
public:
    Hand(uint64_t seed, int column, int row)
        : dice_(seed ^ 0x48414e44ull /* 'HAND' */), groundColumn_(column), groundRow_(row) {}

    // One order a tick at most, and only when the last one is finished: the sim is not a queue
    // and a request raised every tick would re-plan the walk every tick.
    void play(sim::Realm& realm) {
        const sim::Body& hero = realm.hero();
        if (!hero.alive()) return;

        // Fight what is nearest and alive within sight, as a player hunting would.
        uint32_t nearest = 0;
        float closest = 1e30f;
        for (const sim::Body& one : realm.bodies()) {
            if (one.player || !one.alive()) continue;
            const float far = std::max(std::fabs(one.x - hero.x), std::fabs(one.y - hero.y));
            if (far <= kSight && far < closest) {
                closest = far;
                nearest = one.id;
            }
        }
        if (nearest != 0) {
            if (nearest != fighting_) {
                fighting_ = nearest;
                sim::Request request;
                request.kind = sim::Request::Kind::Attack;
                request.target = nearest;
                realm.ask(request);
            }
            // And it presses its skill whenever the key would light up, which is what a player
            // does: the realm refuses it while it cools and the ordinary swing lands instead, so
            // the hunt is auto-attack with a skill folded into it -- exactly the rhythm
            // docs/skills-dk.md §3.1a describes, and the reason the cast path is exercised by
            // every headless run rather than by a test of its own.
            // In turn, and not always the first one ready: mana is the real limiter at low level,
            // so a hand that scanned from the top of the table every time threw Falling Slash
            // twenty-five times and Cyclone never -- and a skill the hunt never presses is a skill
            // the seeded log never covers. Starting after the last one pressed costs nothing and
            // spreads the casts over the four.
            for (int n = 1; n <= sim::skillCount(); ++n) {
                const int i = (pressed_ + n) % sim::skillCount();
                const sim::SkillRow& row = sim::skillAt(i);
                if (!realm.knows(row.number) || realm.cooling(row.number) > 0) continue;
                realm.invoke(row.number, nearest);
                pressed_ = i;
                break;
            }
            return;
        }

        // Nothing in sight. Back to the hunting ground if he is off it -- which is where a
        // death puts him, since a character stands up in town and the town has nothing in it to
        // kill -- and otherwise a step to somewhere else in the field. Without the walk back, a
        // hunt is one fight and then twenty minutes of a man wandering around Lorencia.
        fighting_ = 0;
        if (hero.walking) {
            refused_ = 0;
            return;
        }
        // Asked last tick and still not walking: the realm refused that tile, so the next ask
        // is a random one. Without this the hand asks for the same unreachable tile forever.
        ++refused_;
        sim::Request request;
        request.kind = sim::Request::Kind::WalkTo;
        const int away = std::max(std::abs(hero.column() - groundColumn_),
                                  std::abs(hero.row() - groundRow_));
        if (away > kStride && refused_ < 2) {
            // Toward it in strides rather than in one plan: a route the length of the map is
            // the router's worst case and a hand that asked for one every time would be
            // measuring the router rather than the hunt.
            const int dx = groundColumn_ - hero.column(), dy = groundRow_ - hero.row();
            const int longest = std::max(std::abs(dx), std::abs(dy));
            request.column = hero.column() + dx * kStride / longest;
            request.row = hero.row() + dy * kStride / longest;
        } else {
            // Two draws, the same two every time.
            request.column = hero.column() + dice_.nextInt(-kStride, kStride + 1);
            request.row = hero.row() + dice_.nextInt(-kStride, kStride + 1);
        }
        realm.ask(request);
    }

private:
    // How far the hand looks for something to kill, in tiles: MU's own InfoRange, which
    // GameConfigurationInitializerBase sets to twelve. It is the hand's rule and not the sim's.
    static constexpr float kSight = 12.0f;
    static constexpr int kStride = 10;

    sim::Random dice_;
    int groundColumn_ = 0, groundRow_ = 0;  // where the hand came to hunt
    int refused_ = 0;
    uint32_t fighting_ = 0;
    int pressed_ = 0;  // the last skill it threw, by the table's index: the next scan starts after it
};

// FNV-1a over the whole log. Not a cryptographic anything: it is a short thing to put in the
// sprint file beside the build, so two runs can be said to have agreed without pasting twenty
// megabytes into it. `cmp` on the files themselves is still the gate.
uint64_t fingerprint(const std::string& text) {
    uint64_t hash = 0xcbf29ce484222325ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 0x100000001b3ull;
    }
    return hash;
}

double milliseconds(std::chrono::steady_clock::time_point from,
                    std::chrono::steady_clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

}  // namespace

int runHeadless(const core::Args& args, const char* assetDir) {
    const std::string world = args.world.empty() ? "lorencia" : args.world;
    const std::string path =
        std::string(assetDir) + "/cooked/" + world + "/" + world + ".mur";

    content::Tables tables;
    std::string error;
    if (!content::loadTables(path, tables, error)) {
        core::logError("%s: %s", path.c_str(), error.c_str());
        return 1;
    }
    core::logf("tables: %zu breeds, %zu nests holding %u monsters, grid %d tiles a side "
               "(%zu blocked), map %u at %u Hz", tables.kinds.size(), tables.nests.size(),
               tables.population(), tables.grid.size(), tables.grid.blocked(), tables.map,
               tables.hz);

    sim::Realm realm;
    const int column = args.atSet ? int(args.atColumn) : 138;
    const int row = args.atSet ? int(args.atRow) : 124;
    if (!realm.raise(&tables, args.seed, column, row, sim::Kin(args.kin), args.level)) {
        core::logError("the realm did not raise");
        return 1;
    }

    // What he is to hold, resolved before his points are spent -- because a weapon asks for
    // strength and agility, and a hand that had already poured everything into strength could
    // not pick up a Kris.
    int32_t weapon = -1, shield = -1;
    if (!args.weapon.empty()) {
        weapon = tables.armNamed(args.weapon);
        if (weapon < 0) {
            core::logError("no arm called %s", args.weapon.c_str());
            return 1;
        }
    }
    if (!args.shield.empty()) {
        shield = tables.armNamed(args.shield);
        if (shield < 0) {
            core::logError("no arm called %s", args.shield.c_str());
            return 1;
        }
    }

    // The points a levelled character arrived with, spent. Which stat is the hand's choice and
    // not a rule: a knight made at level 20 has 95 of them, and with none of them spent he
    // swings his fists for two against a Budge Dragon's three of defence and loses to it.
    // What he is about to hold is paid for first, and the rest goes where --spend says.
    if (realm.hero().pointsInHand > 0 && args.spend != "none") {
        int points = realm.hero().pointsInHand;
        const sim::HeroPoints& has = realm.hero().points;
        int wantsStrength = 0, wantsAgility = 0;
        for (int32_t index : {weapon, shield}) {
            if (index < 0) continue;
            const content::Arm& arm = tables.arms[size_t(index)];
            wantsStrength = std::max(wantsStrength, arm.wantsStrength);
            wantsAgility = std::max(wantsAgility, arm.wantsAgility);
        }
        int intoStrength = std::max(0, wantsStrength - has.strength);
        int intoAgility = std::max(0, wantsAgility - has.agility);
        if (intoStrength + intoAgility > points) {
            core::logError("%s wants %d strength and %d agility, and a level %d character has "
                           "only %d points to spend", args.weapon.c_str(), wantsStrength,
                           wantsAgility, args.level, points);
            return 1;
        }
        points -= intoStrength + intoAgility;
        const int rest = points;
        realm.spend(intoStrength + (args.spend == "strength" ? rest : 0),
                    intoAgility + (args.spend == "agility" ? rest : 0),
                    args.spend == "vitality" ? rest : 0, args.spend == "energy" ? rest : 0);
        if (intoStrength + intoAgility > 0) {
            core::logf("hand: %d points to meet what his arms ask (%d strength, %d agility), "
                       "%d into %s", intoStrength + intoAgility, intoStrength, intoAgility,
                       rest, args.spend.c_str());
        } else {
            core::logf("hand: %d points into %s", rest, args.spend.c_str());
        }
    }

    if ((weapon >= 0 || shield >= 0) && !realm.equip(weapon, shield)) {
        // Refused on the requirement, and then given anyway -- the same courtesy the window
        // does, and for the same reason: a character is CREATED holding what his class is
        // given, and a level-one knight is 22 strength short of the Small Axe he is created
        // with. If the headless run refused it, the one character the game actually starts
        // could not be hunted with here, which is the character the seeded run most wants.
        // See Realm::equip. Anything else refused -- a class that may not hold it, a shield
        // asked for as a weapon -- is still a refusal, because `given` does not waive those.
        const std::string why = realm.refusal();
        if (!realm.equip(weapon, shield, true)) {
            core::logError("he cannot hold that: %s", realm.refusal().c_str());
            return 1;
        }
        core::logf("hand: %s -- given anyway, as a new character is given what his class "
                   "starts with", why.c_str());
    }
    {
        const sim::Fighter& stats = realm.hero().stats;
        const sim::HeroPoints& has = realm.hero().points;
        core::logf("hero: level %d %s%s%s -- str %d agi %d vit %d, damage %d to %d, defence %d, "
                   "attack rate %.2f, defence rate %.2f, %d health, a swing every %d ms "
                   "(%d ticks)", realm.hero().level,
                   args.weapon.empty() ? "bare-handed" : args.weapon.c_str(),
                   args.shield.empty() ? "" : " with ", args.shield.c_str(),
                   has.strength, has.agility, has.vitality, stats.minimumDamage,
                   stats.maximumDamage, stats.defense, double(stats.attackRate),
                   double(stats.defenseRate), realm.hero().maxHealth, realm.hero().swingMs,
                   realm.hero().swingTicks);
    }

    // The whole log is built in memory and written once. A run that wrote as it went would
    // have the file system's own buffering inside the measurement, and the numbers below are
    // the tick's cost and nothing else.
    std::string log;
    log.reserve(size_t(args.ticks) * 64);
    const auto write = [&](const std::vector<sim::Happening>& happenings) {
        for (const sim::Happening& happening : happenings) {
            if (happening.what == sim::What::Stepped && !args.simSteps) continue;
            log += sim::describe(happening, realm);
            log += '\n';
        }
    };
    write(realm.happenings());  // the spawns, which raise() wrote

    Hand hand(args.seed, realm.hero().column(), realm.hero().row());
    sim::Findings findings;
    std::vector<double> stepMs;
    stepMs.reserve(size_t(args.ticks));
    uint64_t kills = 0, blows = 0, misses = 0;

    // The gait tally. What the drawing has to pace a walk cycle by is the ground a body covers
    // in a tick, and every tick a body is `walking` and covers none is a tick of feet striding
    // over earth that does not pass: a slide. They are not a bug on their own -- a turn on the
    // spot is one -- but their number and their longest run is what says whether the picture
    // can be right. Read-only, off the sim's own fields, and out of the seeded dice.
    struct Gait {
        uint64_t walking = 0, stalled = 0, turning = 0, partial = 0, orders = 0, halts = 0;
        uint64_t longestStall = 0;
        // What the drawing would do with all this: the clip chooser of Play::follow, whose
        // whole input is what is tallied above -- ground covered this tick, `walking`, and the
        // coast's patience. A change of clip is a crossfade, and a body that changes twice a
        // second is a body whose legs never finish a stride. This is the only line here that
        // models the picture rather than the sim, and it is modelled because the picture is
        // what the complaint is about.
        uint64_t swaps = 0, inWalk = 0, blips = 0;
        // A dead tick taken MID-STRIDE: the body was covering ground, and this tick it covered
        // none while the sim still has it walking. That is the one the eye catches, because the
        // walk clip is already running and goes on running over a body that has stopped dead for
        // 50 ms. A dead tick before the body has set off is not one: the drawing holds the idle
        // until the first step lands.
        uint64_t hitches = 0;
    };
    Gait heroGait, beastGait;
    std::vector<float> wasX(realm.bodies().size()), wasY(realm.bodies().size());
    std::vector<uint32_t> stallRun(realm.bodies().size(), 0);
    std::vector<uint8_t> inWalk(realm.bodies().size(), 0);
    std::vector<float> still(realm.bodies().size(), 0.0f);
    std::vector<int64_t> swappedAt(realm.bodies().size(), -1000);
    std::vector<uint8_t> underway(realm.bodies().size(), 0);

    for (int tick = 0; tick < args.ticks; ++tick) {
        if (!args.noHand) hand.play(realm);
        for (size_t i = 0; i < realm.bodies().size(); ++i) {
            wasX[i] = realm.bodies()[i].x;
            wasY[i] = realm.bodies()[i].y;
        }
        const auto started = std::chrono::steady_clock::now();
        realm.step();
        stepMs.push_back(milliseconds(started, std::chrono::steady_clock::now()));
        for (size_t i = 0; i < realm.bodies().size(); ++i) {
            const sim::Body& one = realm.bodies()[i];
            Gait& tally = one.player ? heroGait : beastGait;
            {
                // The chooser, tick by tick: a walk while the body covers ground, held while
                // the sim has it walking, and for a monster held a little past that by the
                // coast. Play::follow, and the tuning is play_tuning.h's kCoasting.
                const float dx = one.x - wasX[i], dy = one.y - wasY[i];
                const float covered = std::sqrt(dx * dx + dy * dy);
                // A jump of more than two tiles is a respawn or a gate, and the drawing does
                // not walk it (Play::follow's `jumped`).
                const bool moves = one.alive() && covered > 1e-4f && covered < 2.0f;
                still[i] = moves ? 0.0f : still[i] + float(kTickSeconds);
                const bool walk = one.alive() && (moves || (one.walking && inWalk[i] != 0) ||
                                                  (!one.player && one.walking && still[i] < kCoasting));
                if (walk != (inWalk[i] != 0)) {
                    ++tally.swaps;
                    // A blip: in and out again inside a quarter of a second, which is shorter
                    // than the two crossfades it costs. This is what reads as a stutter.
                    if (tick - swappedAt[i] <= 5) ++tally.blips;
                    swappedAt[i] = tick;
                    inWalk[i] = walk ? 1 : 0;
                }
                if (inWalk[i]) ++tally.inWalk;
            }
            if (!one.alive() || !one.walking) {
                stallRun[i] = 0;
                underway[i] = 0;
                continue;
            }
            ++tally.walking;
            if (one.turning) ++tally.turning;
            const float dx = one.x - wasX[i], dy = one.y - wasY[i];
            const float covered = std::sqrt(dx * dx + dy * dy);
            if (covered < one.speed * 0.01f) {
                ++tally.stalled;
                if (underway[i]) ++tally.hitches;
                ++stallRun[i];
                tally.longestStall = std::max(tally.longestStall, uint64_t(stallRun[i]));
            } else {
                stallRun[i] = 0;
                underway[i] = 1;
                if (covered < one.speed * 0.99f) ++tally.partial;
            }
        }
        for (const sim::Happening& happening : realm.happenings()) {
            const sim::Body* who = realm.find(happening.who);
            if (who == nullptr) continue;
            Gait& tally = who->player ? heroGait : beastGait;
            if (happening.what == sim::What::Walked) ++tally.orders;
            if (happening.what == sim::What::Halted) ++tally.halts;
        }
        sim::audit(realm, findings);
        for (const sim::Happening& happening : realm.happenings()) {
            if (happening.what == sim::What::Hit) ++blows;
            if (happening.what == sim::What::Missed) ++misses;
            if (happening.what == sim::What::Died) ++kills;
        }
        write(realm.happenings());
    }

    const std::string logPath = args.simLog.empty()
                                    ? std::string(MU2_ROOT_DIR) + "/build/hunt.log"
                                    : args.simLog;
    FILE* handle = std::fopen(logPath.c_str(), "wb");
    if (!handle) {
        core::logError("%s did not open for writing", logPath.c_str());
        return 1;
    }
    std::fwrite(log.data(), 1, log.size(), handle);
    std::fclose(handle);

    std::vector<double> sorted = stepMs;
    std::sort(sorted.begin(), sorted.end());
    const double median = sorted.empty() ? 0.0 : sorted[sorted.size() / 2];
    const double ninetyNine =
        sorted.empty() ? 0.0 : sorted[size_t(double(sorted.size()) * 0.99)];
    double total = 0.0;
    for (double one : stepMs) total += one;

    const sim::RealmCounts counts = realm.counts();
    const sim::Body& hero = realm.hero();
    core::logf("hunt: %d ticks, seed %llu, %u monsters (%u alive, %u awake)", args.ticks,
               (unsigned long long)args.seed, counts.monsters, counts.alive, counts.roused);
    core::logf("  step: %.4f ms median, %.4f ms 99th, %.3f ms worst, %.1f ms in all", median,
               ninetyNine, sorted.empty() ? 0.0 : sorted.back(), total);
    core::logf("  routes: %llu searched (%llu found nothing), %llu tiles expanded, worst %u",
               (unsigned long long)realm.router().searches(),
               (unsigned long long)realm.router().failures(),
               (unsigned long long)realm.router().expansions(),
               realm.router().worstExpansions());
    core::logf("  fight: %llu blows landed, %llu missed, %llu deaths; hero level %d with "
               "%llu experience and %d points, %d of %d health",
               (unsigned long long)blows, (unsigned long long)misses,
               (unsigned long long)kills, hero.level, (unsigned long long)hero.experience,
               hero.pointsInHand, hero.health, hero.maxHealth);
    for (int which = 0; which < 2; ++which) {
        const Gait& tally = which == 0 ? heroGait : beastGait;
        core::logf("  gait (%s): %llu ticks walking, %llu covering no ground (%.1f%%, longest "
                   "run %llu), %llu of those turning, %llu part-ticks; %llu walk orders, %llu "
                   "halts",
                   which == 0 ? "hero" : "monsters", (unsigned long long)tally.walking,
                   (unsigned long long)tally.stalled,
                   tally.walking ? 100.0 * double(tally.stalled) / double(tally.walking) : 0.0,
                   (unsigned long long)tally.longestStall, (unsigned long long)tally.turning,
                   (unsigned long long)tally.partial, (unsigned long long)tally.orders,
                   (unsigned long long)tally.halts);
        core::logf("  clips (%s): %llu ticks in the walk, %llu changes of clip (%llu of them "
                   "inside a quarter second of the last), %llu hitches mid-stride",
                   which == 0 ? "hero" : "monsters", (unsigned long long)tally.inWalk,
                   (unsigned long long)tally.swaps, (unsigned long long)tally.blips,
                   (unsigned long long)tally.hitches);
    }
    core::logf("  log: %zu bytes, fingerprint %016llx -> %s", log.size(),
               (unsigned long long)fingerprint(log), logPath.c_str());
    core::logf("  draws: %llu from the realm's own dice", (unsigned long long)realm.draws());

    if (findings.total() == 0) {
        core::logf("  invariants: all kept");
        return 0;
    }
    core::logError("  invariants: %llu on a blocked tile, %llu below zero health, %llu blows on "
                   "the dead, %llu past the leash, %llu unpaid levels, %llu skills unlearned, "
                   "%llu cast early, %llu guards that never lapse",
                   (unsigned long long)findings.onBlocked,
                   (unsigned long long)findings.belowZero,
                   (unsigned long long)findings.hitTheDead,
                   (unsigned long long)findings.pastTheLeash,
                   (unsigned long long)findings.unpaidLevel,
                   (unsigned long long)findings.castUnlearned,
                   (unsigned long long)findings.castEarly,
                   (unsigned long long)findings.castForever);
    for (const std::string& line : findings.first) core::logError("    %s", line.c_str());
    return 1;
}

}  // namespace mu::game
