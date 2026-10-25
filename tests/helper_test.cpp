// The helper's keys without a window: a Play opened on the cooked Lorencia with no figures, one
// monster rooted six tiles east of the hero (the arena's --arena-gap), the helper on, and where
// he stood each time a key's skill went off.
//
// Piddy on Discord, 2026-10-24: "While using the helper, Twisting Slash doesn't take you in range
// of the mob when not assigned to the right click button", and "power wave and other aiming
// spells auto-play is missing the monsters". A key was pressed the moment he was set on a monster,
// six tiles out, and a press is thrown from where he stands (Realm::press): the whirl spun on air
// and Power Wave went off from Energy Ball's nine with its six.
#include <algorithm>
#include <cstdio>
#include <string>

#include "game/helper.h"
#include "game/play.h"
#include "game/play_tuning.h"
#include "sim/realm.h"
#include "sim/realm_tuning.h"
#include "sim/skills.h"

using namespace mu;
using namespace mu::game;

namespace {

int g_checks = 0, g_failures = 0;

void check(bool held, const std::string& what) {
    ++g_checks;
    if (held) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
}

struct Run {
    int casts = 0;       // the key's skill thrown
    int outOfReach = 0;  // thrown with the monster farther than its reach
    float farthest = 0.0f;
};

// The hero raised in the arena with `skill` on his first key and `right` on the right button,
// the helper run for `seconds`, and each throw of `skill` measured off the monster.
Run hunt(sim::Kin kin, const std::string& weapon, int32_t skill, int32_t right, double seconds) {
    Run run;
    Play play;
    Play::Arena arena;
    arena.breed = "Spider";
    arena.gap = 6;
    arena.undying = true;
    arena.teach = {skill};
    if (right != 0) arena.teach.push_back(right);
    play.setArena(arena);
    if (!play.open(MU2_ASSET_DIR, "lorencia", nullptr, nullptr, 7, int(kin), 80, Play::Arena::kColumn,
                   Play::Arena::kRow, weapon)) {
        check(false, "Play opens on the cooked Lorencia");
        return run;
    }
    play.setQuickSkill(right);
    Helper helper;
    HelperSettings settings;
    // Thrown whenever it is ready: an area's "3+ near" with one monster in the arena is never.
    settings.uses[skill] = HelperUse::Ready;
    helper.start(play);
    const int32_t bar[5] = {skill, 0, 0, 0, 0};
    const sim::SkillRow* row = sim::skillNumbered(skill);
    uint32_t seen = 0;  // the last tick whose happenings were read
    for (double t = 0.0; t < seconds; t += kTickSeconds) {
        helper.update(play, settings, kTickSeconds, bar, 5);
        play.update(kTickSeconds);
        const sim::Realm& realm = play.realm();
        const sim::Body& hero = realm.hero();
        for (const sim::Happening& one : realm.happenings()) {
            if (one.tick <= seen || one.what != sim::What::Cast || one.who != hero.id || one.a != skill) continue;
            // Thrown: how far the nearest monster stood, against the skill's reach and its bulk.
            float nearest = 1e9f;
            int bulk = 0;
            for (const sim::Body& body : realm.bodies()) {
                if (!body.monster() || !body.alive() || body.gone) continue;
                const float away = sim::reach(hero, body);
                if (away < nearest) {
                    nearest = away;
                    bulk = sim::bulkOf(realm.tables()->kinds[size_t(body.kind)].number);
                }
            }
            if (nearest > 1e8f) continue;
            ++run.casts;
            if (nearest > run.farthest) run.farthest = nearest;
            if (nearest > row->reach + float(bulk)) ++run.outOfReach;
        }
        if (!realm.happenings().empty()) seen = std::max(seen, realm.happenings().back().tick);
    }
    play.shutdown();
    return run;
}

void say(const char* what, const Run& run) {
    std::printf("%s: %d thrown, %d out of reach, farthest %.1f tiles\n", what, run.casts, run.outOfReach,
                double(run.farthest));
}

}  // namespace

int main() {
    // A knight with Twisting Slash on a key and nothing on the right button.
    const Run knight = hunt(sim::Kin::DarkKnight, "Sword01", sim::skill::kTwistingSlash, 0, 20.0);
    say("Twisting Slash on a key", knight);
    check(knight.casts > 0, "the knight's Twisting Slash is thrown from a key");
    check(knight.outOfReach == 0, "the knight's Twisting Slash is thrown only within its three tiles");

    // A wizard with Energy Ball on the right button, which stands him nine tiles off, and Power
    // Wave, six, on a key.
    const Run wizard = hunt(sim::Kin::DarkWizard, "", sim::skill::kPowerWave, sim::skill::kEnergyBall, 20.0);
    say("Power Wave on a key, Energy Ball on the right", wizard);
    check(wizard.casts > 0, "the wizard's Power Wave is thrown from a key");
    check(wizard.outOfReach == 0, "the wizard's Power Wave is thrown only within its six tiles");

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
