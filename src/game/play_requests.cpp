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
    // The potion going down, or the apple: TryConsumeItem's own split, by what was used.
    if (used) {
        const bool apple = item >= 0 && size_t(item) < tables_.items.size() &&
                           tables_.items[size_t(item)].group == 14 &&
                           tables_.items[size_t(item)].number == 0;
        sound_.play(apple ? heard_.apple : heard_.drink);
    }
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
