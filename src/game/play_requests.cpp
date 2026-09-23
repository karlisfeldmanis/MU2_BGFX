// What the windows ask for, and what the realm answers.
//
// Sprint 7's rule, and the reason every one of these returns a bool: THE INTERFACE IS A MIRROR.
// A window never changes what it shows by itself. It raises a request here, the realm decides,
// and the window redraws from the realm afterwards. The bool is the realm's answer, not this
// layer's -- nothing here may refuse on the realm's behalf.
#include "game/play.h"

#include <bx/math.h>
#include <bx/timer.h>

#include <algorithm>
#include <cctype>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "game/frustum.h"
#include "game/play_tuning.h"

namespace mu::game {

bool Play::spendPoint(int stat) {
    static const char* const kStats[4] = {"strength", "agility", "vitality", "energy"};
    if (stat < 0 || stat > 3) return false;
    const bool spent = realm_.spend(stat == 0, stat == 1, stat == 2, stat == 3);
    core::logf("window: a point into %s %s", kStats[stat],
               spent ? "spent" : "refused, none in hand");
    return spent;
}

void Play::castSkill(int32_t skill, uint32_t at) {
    // Aimed at what the window named, else at what he is already fighting -- which the realm
    // works out for itself, because the standing order is its own. A press on a skill he has not
    // learned, or one that is cooling, is refused down there and says nothing: the box's sweep is
    // the answer. See Realm::invoke.
    realm_.invoke(skill, at);
    const sim::SkillRow* row = sim::skillNumbered(skill);
    core::logf("window: %s asked (cooling %lld ticks, %d mana of %d)",
               row ? row->name : "a skill", (long long)realm_.cooling(skill),
               row ? row->mana : 0, realm_.hero().mana);
}

bool Play::moveItem(int from, int to) {
    const bool moved = realm_.moveItem(from, to);
    core::logf("window: move %d -> %d %s", from, to, moved ? "taken" : "refused");
    if (moved) redress();
    return moved;
}

bool Play::useItem(int slot) {
    const int32_t item = slot >= 0 && slot < sim::kSlots ? realm_.satchel()[slot].item : -1;
    const bool used = realm_.useItem(slot);
    core::logf("window: use %d %s", slot, used ? "taken" : "refused");
    // The potion going down, or the apple: TryConsumeItem's own split, by what was used. And
    // the third arm, which is this project's and not MuMain's, because MuMain has no orb read
    // from the bag to answer for: an orb is not swallowed, so the gulp is wrong on it. It is
    // the one use that is a picture as well as a noise -- `learned` throws the ribbons and the
    // swoosh together, where a potion is heard and not seen.
    if (used) {
        const content::ItemRow* row =
            item >= 0 && size_t(item) < tables_.items.size() ? &tables_.items[size_t(item)] : nullptr;
        const bool apple = row && row->group == 14 && row->number == 0;
        if (row && row->teaches != 0) {
            learned();
        } else {
            sound_.play(apple ? heard_.apple : heard_.drink);
        }
    }
    return used;
}

// The noise belongs to the thing LANDING and is made where it lies, which is `Play::landed` --
// the same call a kill's drop is heard through. It is rung from HERE and not from the
// What::Dropped branch in Play::step, because a discard is asked between ticks and the next
// step clears what it said before that loop could read it; a purchase and a sale are heard off
// their own answers for the same reason.
bool Play::discard(int slot) {
    const bool worn = slot >= 0 && sim::wearable(slot);
    const uint32_t thrown = realm_.discard(slot);
    core::logf("window: %d thrown on the ground %s", slot, thrown ? "taken" : "refused");
    if (thrown == 0) return false;
    if (worn) redress();
    landed(thrown);
    return true;
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
    // And what he wears: the five armour slots, by the asset each item row names. Without
    // these the figure only ever changed its hands, and gloves put on stayed bare hands.
    std::vector<std::string> worn;
    for (int slot = sim::kHelm; slot <= sim::kBoots; ++slot) {
        const sim::Held& held = realm_.satchel()[slot];
        if (held.empty() || size_t(held.item) >= tables_.items.size()) continue;
        worn.push_back(tables_.items[size_t(held.item)].name);
    }
    const FigureBody* look = figures_->dress(kHeroDressName, bare_, weapon, shield, worn);
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

void Play::restore(const sim::HeroRecord& saved) {
    if (!isOpen()) return;
    realm_.restore(saved);
    redress();
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
    // ReceiveBuy's SOUND_GET_ITEM01: a purchase is a thing arriving in the bag. MU2 rang coins
    // here, which MuMain does not -- pDropMoney is only ever a heap landing.
    if (slot >= 0) sound_.play(heard_.take);
    return slot >= 0;
}

bool Play::sell(int bagSlot) {
    const int64_t paid = realm_.sellItem(bagSlot);
    core::logf("window: sell slot %d %s (%lld paid, %lld Zen now)", bagSlot,
               paid >= 0 ? "taken" : "refused", (long long)paid, (long long)realm_.money());
    // ReceiveSell's, the same SOUND_GET_ITEM01.
    if (paid >= 0) sound_.play(heard_.take);
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

}  // namespace mu::game
