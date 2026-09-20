#include "game/headless.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "content/tables.h"
#include "core/files.h"
#include "core/log.h"
#include "sim/audit.h"
#include "sim/realm.h"

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
        core::logError("he cannot hold that: %s", realm.refusal().c_str());
        return 1;
    }
    {
        const sim::Fighter& stats = realm.hero().stats;
        const sim::HeroPoints& has = realm.hero().points;
        core::logf("hero: level %d %s%s%s -- str %d agi %d vit %d, damage %d to %d, defence %d, "
                   "attack rate %.2f, defence rate %.2f, %d health", realm.hero().level,
                   args.weapon.empty() ? "bare-handed" : args.weapon.c_str(),
                   args.shield.empty() ? "" : " with ", args.shield.c_str(),
                   has.strength, has.agility, has.vitality, stats.minimumDamage,
                   stats.maximumDamage, stats.defense, double(stats.attackRate),
                   double(stats.defenseRate), realm.hero().maxHealth);
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

    for (int tick = 0; tick < args.ticks; ++tick) {
        if (!args.noHand) hand.play(realm);
        const auto started = std::chrono::steady_clock::now();
        realm.step();
        stepMs.push_back(milliseconds(started, std::chrono::steady_clock::now()));
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
    core::logf("  log: %zu bytes, fingerprint %016llx -> %s", log.size(),
               (unsigned long long)fingerprint(log), logPath.c_str());
    core::logf("  draws: %llu from the realm's own dice", (unsigned long long)realm.draws());

    if (findings.total() == 0) {
        core::logf("  invariants: all kept");
        return 0;
    }
    core::logError("  invariants: %llu on a blocked tile, %llu below zero health, %llu blows on "
                   "the dead, %llu past the leash, %llu unpaid levels",
                   (unsigned long long)findings.onBlocked,
                   (unsigned long long)findings.belowZero,
                   (unsigned long long)findings.hitTheDead,
                   (unsigned long long)findings.pastTheLeash,
                   (unsigned long long)findings.unpaidLevel);
    for (const std::string& line : findings.first) core::logError("    %s", line.c_str());
    return 1;
}

}  // namespace mu::game
