// The Jeweler's answers, heard (sim/jeweler.h). Combine is heard as the Chaos Machine's success
// is -- eMix, then eGem at the hero -- the user's ask of 2026-10-20 ("we need sound for combine,
// same sound which we use for chaos machine when there is success"). A recut is the mix alone. A
// salvage is heard as its prize, never as a break (the user, 2026-10-20: "when there is succesfull
// jewel salvage dont play destroy sound, but only jewel sound or item pickup sound for potions"):
// eGem at the hero for a jewel, pGetItem for the potions.

#include "game/play.h"

#include "core/log.h"

namespace mu::game {

void Play::jewelHeard(sim::Command::Kind kind, bool ok, int32_t value) {
    using Kind = sim::Command::Kind;
    const char* what = kind == Kind::JewelRecut ? "recut" : kind == Kind::JewelSalvage ? "salvage" : "combine";
    if (!ok) {
        core::logf("window: jeweler %s refused (%s)", what, realm_.refusal().c_str());
        return;
    }
    core::logf("window: jeweler %s answered %d", what, int(value));
    const auto gem = [&] {
        if (const Drawn* hero = drawnOf(realm_.hero().id)) {
            emit(heard_.jewel, hero->crown[0], hero->crown[2], hero->id);
        }
    };
    switch (kind) {
        case Kind::JewelRecut: sound_.play(heard_.mix); break;
        case Kind::JewelSalvage:
            if (value > 1) gem();  // 1 is the potions
            else sound_.play(heard_.take);
            break;
        default:
            sound_.play(heard_.mix);
            gem();
            break;
    }
}

}  // namespace mu::game
