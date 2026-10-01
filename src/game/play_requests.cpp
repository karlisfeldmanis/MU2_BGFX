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
#include "sim/swings.h"
#include "sim/wear.h"

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
    const sim::SkillRow* row = sim::skillNumbered(skill);
    // A blink is aimed at the ground under the pointer, not at a body; with no ground under it
    // there is nothing to aim at and nothing is asked.
    if (row != nullptr && row->blinks) {
        if (pointedColumn_ < 0) return;
        realm_.invokeAt(skill, pointedColumn_, pointedRow_);
        core::logf("window: %s aimed at tile %d,%d from %d,%d", row->name, pointedColumn_,
                   pointedRow_, realm_.hero().column(), realm_.hero().row());
    } else {
        realm_.invoke(skill, at);
    }
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
    // The swing before and after, so a use that moves it -- the Ale -- shows by how much.
    const int swingMs = realm_.hero().swingMs, swingTicks = realm_.hero().swingTicks;
    const bool used = realm_.useItem(slot);
    core::logf("window: use %d %s", slot, used ? "taken" : "refused");
    if (used && realm_.hero().swingMs != swingMs) {
        core::logf("window: the swing went from %d ms (%d ticks) to %d ms (%d ticks)", swingMs,
                   swingTicks, realm_.hero().swingMs, realm_.hero().swingTicks);
    }
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
        } else if (row && sim::portal(*row)) {
            // Read in silence: TryConsumeItem's scroll branch sends the use and plays nothing.
            // What it has is the arrival -- the hero put down at nought alpha, the warp's walls
            // and circle under him, and sMagic, which is ours (see Play::warped). Or, on a map
            // with no safe zone, the realm says he is owed Lorencia (`c`), and the mode takes him.
            const std::vector<sim::Happening>& told = realm_.happenings();
            if (!told.empty() && told.back().what == sim::What::Warped && told.back().c == 1)
                homeOwed_ = true;
            else
                warped();
        } else {
            // The Ale is a potion to TryConsumeItem (`ITEM_APPLE <= Type <= ITEM_ALE`), so it
            // goes down with SOUND_DRINK01 like the rest.
            sound_.play(apple ? heard_.apple : heard_.drink);
        }
        // And what it is worth, for the lane over the HUD, off the realm's own Drank: the last
        // thing it said, and gone at the next step, so it is held for the next frame's gains.
        const std::vector<sim::Happening>& said = realm_.happenings();
        if (!said.empty() && said.back().what == sim::What::Drank) {
            (said.back().b ? drankMana_ : drankHealth_) += said.back().a;
        }
    }
    return used;
}

// Two sounds, as MuMain makes them. The asking is ApplyJewels' own `SendRequestUse(...);
// PlayBuffer(SOUND_GET_ITEM01)`, and the answer is ReceiveModifyItemExtended's SOUND_JEWEL01
// (WSclient.cpp:6322) -- whether the plus went up or down, because the client has no failure
// sound. The answer is rung here and not off What::Refined in the step for the reason a
// purchase is: asked between ticks, and the next step clears what it said. The ring is the
// placed `jewel_get`, which a drop already lands with, heard where the hero stands.
bool Play::refine(int jewelSlot, int targetSlot) {
    const sim::Held thing =
        targetSlot >= 0 && targetSlot < sim::kSlots ? realm_.satchel()[targetSlot] : sim::Held{};
    const bool refined = realm_.refine(jewelSlot, targetSlot);
    // A worn thing that outgrew him is in the bag now and its slot is empty (Realm::refine); the
    // Soul cannot miss that way, since a lower plus asks less.
    const sim::Held& after = realm_.satchel()[targetSlot];
    const int now = !refined ? thing.refinement
                    : after.empty() ? thing.refinement + 1
                                    : after.refinement;
    core::logf("window: jewel %d on %d %s (+%d -> +%d)", jewelSlot, targetSlot,
               refined ? "taken" : "refused", int(thing.refinement), now);
    if (!refined) return false;
    sound_.play(heard_.take);
    if (const Drawn* hero = drawnOf(realm_.hero().id)) {
        emit(heard_.jewel, hero->crown[0], hero->crown[2], hero->id);
    }
    // A worn thing changed rung, so the figure is dressed again at its new shine.
    if (sim::wearable(targetSlot)) redress();
    return true;
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
std::string Play::quiverName() const {
    for (int slot : {sim::kWeaponRight, sim::kWeaponLeft}) {
        const sim::Held& held = realm_.satchel()[slot];
        if (held.empty() || size_t(held.item) >= tables_.items.size()) continue;
        const content::ItemRow& row = tables_.items[size_t(held.item)];
        if (sim::ammunition(row)) return row.name;
    }
    return {};
}

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
    std::vector<ShineLook> wornShine;
    for (int slot = sim::kHelm; slot <= sim::kBoots; ++slot) {
        const sim::Held& held = realm_.satchel()[slot];
        if (held.empty() || size_t(held.item) >= tables_.items.size()) continue;
        const content::ItemRow& row = tables_.items[size_t(held.item)];
        worn.push_back(row.name);
        wornShine.push_back(shineOf(row, held.refinement, held.excellent != 0));
    }
    // How each hand's plus shows: the hand slot holding the item of that name. The realm says
    // which arm swings, not which slot it came out of.
    const auto handShine = [&](const std::string& name) {
        if (name.empty()) return ShineLook{};
        for (int slot : {sim::kWeaponRight, sim::kWeaponLeft}) {
            const sim::Held& held = realm_.satchel()[slot];
            if (!held.empty() && size_t(held.item) < tables_.items.size() &&
                tables_.items[size_t(held.item)].name == name) {
                return shineOf(tables_.items[size_t(held.item)], held.refinement,
                               held.excellent != 0);
            }
        }
        return ShineLook{};
    };
    dressedQuiver_ = quiverName();
    // The left hand's own plus: two swords of one name are two items, each with its own.
    const sim::Held& left = realm_.satchel()[sim::kWeaponLeft];
    const ShineLook leftShine =
        hero.dual && !left.empty() && size_t(left.item) < tables_.items.size()
            ? shineOf(tables_.items[size_t(left.item)], left.refinement, left.excellent != 0)
            : handShine(shield);
    const FigureBody* look = figures_->dress(kHeroDressName, bare_, weapon, shield, worn,
                                             wornShine, handShine(weapon), leftShine,
                                             dressedQuiver_);
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
    dualSwings(drawn, look);
}

void Play::dualSwings(Drawn& drawn, const FigureBody* look) const {
    for (int& clip : drawn.dualClips) clip = -1;
    const sim::Body& hero = realm_.hero();
    if (!hero.dual || !look || !look->library) return;
    const auto armAt = [&](int32_t at) -> const content::Arm* {
        return at >= 0 && size_t(at) < tables_.arms.size() ? &tables_.arms[size_t(at)] : nullptr;
    };
    int32_t actions[4] = {};
    if (sim::attackActions(armAt(hero.weapon), armAt(hero.shield), actions) != 4) return;
    for (int i = 0; i < 4; ++i) {
        drawn.dualClips[i] = look->library->find(actions[i]);
        if (drawn.dualClips[i] < 0) {
            for (int& clip : drawn.dualClips) clip = -1;
            return;
        }
    }
}

void Play::restore(const sim::HeroRecord& saved) {
    if (!isOpen()) return;
    realm_.restore(saved);
    redress();
}

// --give's and --lay's `+3LO2E16`: a plus, luck, an option level, and excellent options by
// OpenMU's numbers 1 to 6 (E16 is Zen and HP on armour, mana-after-kill and the excellent
// damage rate on a weapon), and `W` to put it on him rather than in the bag -- the bench's, like
// --weapon, and asked of no requirement. Anything else is ignored.
static void readExtras(const std::string& extras, int* plus, bool* luck, int* option,
                       uint8_t* excellent, bool* worn = nullptr, int* sockets = nullptr,
                       uint8_t* powers = nullptr) {
    int nextPower = 0;
    for (size_t i = 0; i < extras.size(); ++i) {
        const char c = extras[i];
        // S<n>: that many sockets; P<n>, again for each: the powers set in them in order, or a
        // Rune of Creation's own (sim/items.h).
        if ((c == 'S' || c == 's') && sockets) *sockets = std::atoi(extras.c_str() + i + 1);
        if ((c == 'P' || c == 'p') && powers && nextPower < 3) {
            powers[nextPower++] = uint8_t(std::atoi(extras.c_str() + i + 1));
        }
        if ((c == 'W' || c == 'w') && worn) *worn = true;
        if (c == '+') *plus = std::atoi(extras.c_str() + i + 1);
        if (c == 'L' || c == 'l') *luck = true;
        if ((c == 'O' || c == 'o') && i + 1 < extras.size()) *option = extras[i + 1] - '0';
        if (c == 'E' || c == 'e') {
            for (size_t j = i + 1; j < extras.size() && extras[j] >= '1' && extras[j] <= '6'; ++j) {
                *excellent |= uint8_t(1u << (extras[j] - '1'));
            }
        }
    }
}

bool Play::give(const std::string& name, int count, const std::string& extras) {
    const int32_t item = tables_.itemNamed(name);
    if (item < 0) {
        core::logError("--give: no item named %s", name.c_str());
        return false;
    }
    const content::ItemRow& row = tables_.items[size_t(item)];
    const bool stacks = sim::stacks(row);
    const int durability = stacks ? std::max(1, count) : sim::fullDurability(row, 0);
    // A potion's count is poured in whole, onto his stacks and twenty a cell; anything else is
    // that many pieces, a cell each -- three jewels are three jewels, as only potions stack.
    const int pieces = stacks ? 1 : std::max(1, count);
    int slot = -1;
    for (int i = 0; i < pieces; ++i) {
        int plus = 0, option = 0;
        bool luck = false;
        uint8_t excellent = 0;
        bool worn = false;
        int sockets = 0;
        uint8_t powers[3] = {};
        readExtras(extras, &plus, &luck, &option, &excellent, &worn, &sockets, powers);
        const int into = worn ? sim::placeOf(row) : -1;
        // Whatever he wears there already goes into the bag first -- the arena's own sword.
        if (into >= 0 && !realm_.satchel()[into].empty()) {
            const sim::Held& on = realm_.satchel()[into];
            const content::ItemRow& onRow = tables_.items[size_t(on.item)];
            const int spare = realm_.satchel().free(tables_, onRow.width, onRow.height);
            if (spare >= 0) realm_.moveItem(into, spare);
        }
        slot = realm_.give(item, into, plus, stacks ? durability : sim::fullDurability(row, plus),
                           luck, option, excellent, uint8_t(sockets), powers);
        if (worn && slot >= 0) redress();
        core::logf("given %s%s into slot %d", row.label.c_str(),
                   stacks ? (" x" + std::to_string(durability)).c_str() : "", slot);
        if (slot < 0) break;
    }
    return slot >= 0;
}

bool Play::lay(const std::string& asked) {
    const size_t colon = asked.find(':');
    const std::string name = asked.substr(0, colon);
    int plus = 0, option = 0;
    bool luck = false;
    uint8_t excellent = 0;
    int sockets = 0;
    if (colon != std::string::npos) {
        readExtras(asked.substr(colon + 1), &plus, &luck, &option, &excellent, nullptr, &sockets);
    }
    const int32_t item = tables_.itemNamed(name);
    if (item < 0) {
        core::logError("--lay: no item named %s", name.c_str());
        return false;
    }
    const uint32_t id = realm_.lay(item, plus, luck, option, excellent, uint8_t(sockets));
    core::logf("laid %s on the ground (drop %u)", tables_.items[size_t(item)].label.c_str(), id);
    if (id == 0) return false;
    landed(id);
    return true;
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

bool Play::acceptQuest(int quest) {
    const bool taken = realm_.acceptQuest(quest);
    core::logf("window: accept quest %d %s", quest, taken ? "taken" : "refused");
    // The user's drum hit, the quest taken; refused, the window's own no.
    if (taken) sound_.play(sound_.load("quest_accept", false));
    else ui(Ui::Refused);
    return taken;
}

bool Play::travel(int index) {
    static const char* const kWhy[] = {"", "not opened", "already here", "dead", "level too low",
                                       "short of zen"};
    const sim::TravelRefusal why = realm_.travelRefusal(index);
    const bool paid = realm_.travel(index);
    core::logf("window: travel to %s %s", sim::travelAt(index).name,
               paid ? "paid" : kWhy[int(why)]);
    // A floor of this same map was set down in place by the realm; only another map is the mode's.
    if (paid && sim::travelAt(index).map != int32_t(realm_.tables()->map)) travelled_ = index;
    if (!paid) ui(Ui::Refused);
    return paid;
}

bool Play::completeQuest(int quest, int choice) {
    const size_t before = realm_.happenings().size();
    const bool paid = realm_.completeQuest(quest, choice);
    // The experience is paid here, between ticks, and the next step clears what the realm said
    // before update() reads it -- so each level the quest carried is taken off its word now.
    // Owed on no kill: the first goes up in the frame the window closes, the rest after it.
    for (size_t i = before; i < realm_.happenings().size(); ++i) {
        const sim::Happening& happening = realm_.happenings()[i];
        if (happening.what == sim::What::Levelled && happening.who == realm_.hero().id) {
            ++levelsOwed_;
            levelOn_ = 0;
        }
    }
    core::logf("window: hand in quest %d, choice %d, %s", quest, choice,
               paid ? "paid" : "refused (not ready, no choice, or no room)");
    // The user's stinger, under the "Quest complete" banner the tracker raises this same frame,
    // the world leaning back for it; refused, the window's own no.
    if (paid) {
        sound_.stinger("music/quest_complete.wav");
        sound_.duck();
    } else {
        ui(Ui::Refused);
    }
    return paid;
}

bool Play::buyBack() {
    const int slot = realm_.buyBack();
    core::logf("window: buy back %s (slot %d, %lld Zen left)", slot >= 0 ? "taken" : "refused",
               slot, (long long)realm_.money());
    // The purchase's pickup: the thing is arriving in the bag again.
    if (slot >= 0) sound_.play(heard_.take);
    return slot >= 0;
}

bool Play::sell(int bagSlot) {
    const int64_t paid = realm_.sellItem(bagSlot);
    core::logf("window: sell slot %d %s (%lld paid, %lld Zen now)", bagSlot,
               paid >= 0 ? "taken" : "refused", (long long)paid, (long long)realm_.money());
    // Coins, not the pickup: a sale is Zen arriving and the thing sold LEAVING the bag, so
    // pGetItem was the one sound in the shop that described the wrong half of the trade. MU
    // plays ReceiveSell's SOUND_GET_ITEM01 here and this deliberately does not -- the user's
    // call, 2026-09-23. A purchase keeps the pickup, because a purchase really is a thing
    // arriving in the bag.
    //
    // Placed at the hero rather than played flat, because `money_drop` is a placed event
    // (play_open loads it that way for the heap that lands on the grass) and Sound::play
    // refuses a placed one in silence. He is standing at the counter and the listener is on
    // him, so there is nothing for the distance to attenuate.
    if (paid >= 0) {
        const Drawn* hero = drawnOf(realm_.hero().id);
        if (heard_.moneyDrop >= 0 && hero && hero->placed) {
            emit(heard_.moneyDrop, hero->crown[0], hero->crown[2]);
        } else {
            sound_.play(heard_.take);
        }
    }
    return paid >= 0;
}

// The mending counter's two. ReceiveRepair plays SOUND_REPAIR on the reply that carries the new
// Zen, and only then, so a repair refused is the desk's no and not this sound.
bool Play::repair(int slot) {
    const int64_t cost = realm_.repairCost(slot);
    const bool done = realm_.repair(slot);
    core::logf("window: repair slot %d %s (%lld Zen, %lld now)", slot,
               done ? "taken" : "refused", (long long)cost, (long long)realm_.money());
    if (done) sound_.play(heard_.repair);
    return done;
}

bool Play::repairAll() {
    const int64_t before = realm_.money();
    const int mended = realm_.repairAll();
    core::logf("window: repair all, %d mended for %lld Zen", mended,
               (long long)(before - realm_.money()));
    if (mended > 0) sound_.play(heard_.repair);
    return mended > 0;
}

// The vault's moves. The item ones are heard by the desk, as the bag's are (the pickup for a
// move taken, the refusal for one refused); the Zen is heard here as the coins of a sale,
// placed at the hero for the same reason.
bool Play::deposit(int bagSlot, int cell) {
    const int at = realm_.deposit(bagSlot, cell);
    core::logf("window: vault deposit slot %d -> cell %d %s", bagSlot, at,
               at >= 0 ? "taken" : "refused");
    return at >= 0;
}

bool Play::withdraw(int cell, int bagSlot) {
    const int at = realm_.withdraw(cell, bagSlot);
    core::logf("window: vault withdraw cell %d -> slot %d %s", cell, at,
               at >= 0 ? "taken" : "refused");
    return at >= 0;
}

bool Play::rearrange(int from, int to) {
    const bool moved = realm_.rearrange(from, to);
    core::logf("window: vault move %d -> %d %s", from, to, moved ? "taken" : "refused");
    return moved;
}

bool Play::depositZen(int64_t zen) {
    const bool moved = realm_.depositZen(zen);
    core::logf("window: vault takes %lld Zen %s (%lld carried, %lld kept)", (long long)zen,
               moved ? "taken" : "refused", (long long)realm_.money(),
               (long long)realm_.vault().zen());
    if (moved) {
        const Drawn* hero = drawnOf(realm_.hero().id);
        if (heard_.moneyDrop >= 0 && hero && hero->placed) {
            emit(heard_.moneyDrop, hero->crown[0], hero->crown[2]);
        }
    }
    return moved;
}

bool Play::withdrawZen(int64_t zen) {
    const bool moved = realm_.withdrawZen(zen);
    core::logf("window: vault gives %lld Zen %s (%lld carried, %lld kept)", (long long)zen,
               moved ? "taken" : "refused", (long long)realm_.money(),
               (long long)realm_.vault().zen());
    if (moved) {
        const Drawn* hero = drawnOf(realm_.hero().id);
        if (heard_.moneyDrop >= 0 && hero && hero->placed) {
            emit(heard_.moneyDrop, hero->crown[0], hero->crown[2]);
        }
    }
    return moved;
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
