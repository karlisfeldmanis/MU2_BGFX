// The Realm's side of the quests (sim/quests.h): what a giver offers, what a kill counts for, and
// the hand-in. All of it invention, marked in the header; none of it draws a die, so a seeded run
// that takes no quest logs what it always did.
#include <algorithm>

#include "sim/realm.h"
#include "sim/wear.h"

namespace mu::sim {

int Realm::questGoal(int index, int step) const {
    if (index < 0 || index >= kQuests || step < 0 || !tables_) return 0;
    const QuestRow& row = questAt(index);
    if (step >= row.stepCount) return 0;
    const QuestStepRow& one = row.steps[step];
    if (one.kind != QuestStepKind::Clear) return std::max(1, int(one.count));
    if (one.count > 0) return one.count;
    // Clearing: the breed's whole population on this map, every nest of it summed.
    int total = 0;
    for (const content::MonsterNest& nest : tables_->nests) {
        if (nest.kind < tables_->kinds.size() && tables_->kinds[nest.kind].number == one.target) {
            total += int(nest.count);
        }
    }
    return std::min(total, 65535);
}

bool Realm::questOffered(int index) const {
    if (index < 0 || index >= kQuests) return false;
    // Never to one born outside a giver's town who does not serve strangers.
    if (!questOpen(questAt(index), int(bodies_[0].kin))) return false;
    if (questLocked(index)) return false;
    const QuestProgress& one = quests_[index];
    if (one.state == QuestState::Untaken) return true;
    return one.state == QuestState::Resting && questAt(index).repeatSeconds > 0 &&
           wall_ >= one.availableAt;
}

int Realm::questHere(int32_t giver) const {
    int ready = -1, active = -1, untaken = -1, back = -1, rested = -1, first = -1;
    for (int i = 0; i < kQuests; ++i) {
        const QuestRow& row = questAt(i);
        // One someone else gave, which this one takes back: only its hand-in is hers.
        if (questElsewhere(row) && row.receiver == giver) {
            if (quests_[i].state == QuestState::Ready && ready < 0) ready = i;
            continue;
        }
        if (row.giver != giver) continue;
        if (first < 0) first = i;
        QuestState state = quests_[i].state;
        // And at its giver, one handed in elsewhere is under way until it is.
        if (questElsewhere(row) && state == QuestState::Ready) state = QuestState::Active;
        if (state == QuestState::Ready && ready < 0) ready = i;
        if (state == QuestState::Active && active < 0) active = i;
        if (state == QuestState::Untaken && untaken < 0 && questOffered(i)) untaken = i;
        if (state == QuestState::Resting) {
            if (back < 0 && questOffered(i)) back = i;
            rested = i;
        }
    }
    // What his mark says, with more than one of his standing: a hand-in first, then an offer,
    // then one under way. The chain moves on before a link comes back round: the Halls before
    // the Catacombs again.
    if (ready >= 0) return ready;
    if (untaken >= 0) return untaken;
    if (back >= 0) return back;
    if (active >= 0) return active;
    return rested >= 0 ? rested : first;
}

bool Realm::questUnderLevel(int index) const {
    if (index < 0 || index >= kQuests) return false;
    const QuestRow& row = questAt(index);
    if (!questOpen(row, int(bodies_[0].kin)) || bodies_[0].level >= row.minLevel) return false;
    const QuestProgress& one = quests_[index];
    if (one.state != QuestState::Untaken || one.completions > 0) return false;
    if (row.afterAny == 0) return true;
    for (int i = 0; i < kQuests; ++i) {
        if (((row.afterAny >> i) & 1u) && quests_[i].completions > 0) return true;
    }
    return false;
}

int Realm::questsAt(int32_t giver, int* out) const {
    int n = 0;
    for (int i = 0; i < kQuests; ++i) {
        const QuestRow& row = questAt(i);
        const QuestState state = quests_[i].state;
        // Its receiver lists it while it waits to be handed in to her.
        if (questElsewhere(row) && row.receiver == giver) {
            if (state == QuestState::Ready) out[n++] = i;
            continue;
        }
        if (row.giver != giver || !questOpen(row, int(bodies_[0].kin))) continue;
        // A link of a chain handed in for good is gone from the list; a repeat waits there.
        const bool listed = state == QuestState::Active || state == QuestState::Ready ||
                            questOffered(i) || questUnderLevel(i) ||
                            (state == QuestState::Resting && row.repeatSeconds > 0);
        if (listed) out[n++] = i;
    }
    return n;
}

bool Realm::questListed(int32_t giver) const {
    int list[kQuests];
    const int n = questsAt(giver, list);
    return n > 1 || (n == 1 && questUnderLevel(list[0]));
}

bool Realm::questLocked(int index) const {
    if (index < 0 || index >= kQuests) return false;
    const QuestRow& row = questAt(index);
    const QuestProgress& one = quests_[index];
    if (one.state != QuestState::Untaken || one.completions > 0) return false;
    // Below its level: Sevina's, until 200 (the user, 2026-10-04).
    if (bodies_[0].level < row.minLevel) return true;
    if (row.afterAny == 0) return false;
    for (int i = 0; i < kQuests; ++i) {
        if (((row.afterAny >> i) & 1u) && quests_[i].completions > 0) return false;
    }
    return true;
}

bool Realm::questChoiceFits(int index, int choice) const {
    if (!tables_ || index < 0 || index >= kQuests) return false;
    const QuestRow& row = questAt(index);
    if (choice < 0 || choice >= row.choiceCount) return false;
    const int32_t item = tables_->itemNamed(row.choices[choice].item);
    if (item < 0) return false;
    const content::ItemRow& one = tables_->items[size_t(item)];
    return one.classes == 0 || (one.classes & (1 << int(bodies_[0].kin))) != 0;
}

bool Realm::questItemFits(const QuestItem& what) const {
    const int32_t item = tables_ && what.item ? tables_->itemNamed(what.item) : -1;
    if (item < 0) return false;
    const content::ItemRow& one = tables_->items[size_t(item)];
    return one.classes == 0 || (one.classes & (1 << int(bodies_[0].kin))) != 0;
}

bool Realm::acceptQuest(int index) {
    if (index < 0 || index >= kQuests || questing_ < 0 || !serving(questing_)) return false;
    if (tables_->folk[size_t(questing_)].number != questAt(index).giver) return false;
    if (!questOffered(index)) return false;
    QuestProgress& one = quests_[index];
    const uint32_t completions = one.completions;
    one = QuestProgress{};
    one.state = QuestState::Active;
    one.completions = completions;
    say(What::QuestTaken, bodies_[0], index);
    return true;
}

void Realm::countKill(const Body& dead) {
    if (!tables_ || dead.kind < 0 || size_t(dead.kind) >= tables_->kinds.size()) return;
    castleKill(dead);
    const int32_t number = tables_->kinds[size_t(dead.kind)].number;
    for (int index = 0; index < kQuests; ++index) {
        QuestProgress& one = quests_[index];
        if (one.state != QuestState::Active) continue;
        const QuestRow& row = questAt(index);
        bool counted = false;
        for (int step = 0; step < row.stepCount; ++step) {
            const QuestStepRow& want = row.steps[step];
            if (want.kind != QuestStepKind::Clear || want.target != number) continue;
            const int goal = questGoal(index, step);
            if (one.counts[step] >= goal) continue;
            ++one.counts[step];
            counted = true;
            say(What::QuestStep, bodies_[0], index, one.counts[step], step);
        }
        if (counted) questSettle(index);
    }
}

// Met: every quest under way whose receiver is this one, and not its giver, settles -- its only
// uncounted step was finding her, so a quest with no counts left is ready to hand in to her.
void Realm::questMet(int32_t number) {
    for (int index = 0; index < kQuests; ++index) {
        const QuestRow& row = questAt(index);
        if (questElsewhere(row) && row.receiver == number) questSettle(index);
    }
}

// Ready once every counted step is at its goal: all at once and in any order.
void Realm::questSettle(int index) {
    QuestProgress& one = quests_[index];
    if (one.state != QuestState::Active) return;
    const QuestRow& row = questAt(index);
    for (int step = 0; step < row.stepCount; ++step) {
        if (questCounted(row.steps[step].kind) && one.counts[step] < questGoal(index, step)) {
            return;
        }
    }
    one.state = QuestState::Ready;
    say(What::QuestReady, bodies_[0], index);
}

// A thing come into the bag: whatever Find step asks for it counts it. The treasure is picked up
// as any drop is (Realm::take), so this is called from there.
void Realm::questFound(int32_t item) {
    if (!tables_ || item < 0) return;
    for (int index = 0; index < kQuests; ++index) {
        QuestProgress& one = quests_[index];
        if (one.state != QuestState::Active) continue;
        const QuestRow& row = questAt(index);
        for (int step = 0; step < row.stepCount; ++step) {
            const QuestStepRow& want = row.steps[step];
            if (want.kind != QuestStepKind::Find || !want.item ||
                tables_->itemNamed(want.item) != item) {
                continue;
            }
            if (one.counts[step] >= questGoal(index, step)) continue;
            ++one.counts[step];
            say(What::QuestStep, bodies_[0], index, one.counts[step], step);
        }
        questSettle(index);
    }
}

// The bag slot holding one of `item`, or -1. Past the worn slots, as the hand-in looks.
int Realm::carried(int32_t item) const {
    for (int slot = kWorn; slot < kSlots; ++slot) {
        if (!bag_[slot].empty() && bag_[slot].item == item) return slot;
    }
    return -1;
}

// Whether a kill here may leave a class's treasure: the Lost Tower's last floor, or Atlans (the
// user, 2026-10-04: "quest item drop which can drop in lt7 or atlans"). The seventh floor is the
// travel list's last Lost Tower row, the floor a tile is on (Realm::floorAt).
bool Realm::treasureGround(int column, int row) const {
    if (!tables_) return false;
    if (tables_->map == kAtlansMap) return true;
    if (tables_->map != kLostTowerMap) return false;
    int last = -1;
    for (int i = 0; i < kTravels; ++i) {
        if (travelAt(i).map == kLostTowerMap) last = i;
    }
    return last >= 0 && floorAt(column, row) == last;
}

// The class's treasure, at kTreasureIn10000 a kill on its ground while its Find stands and he
// does not already carry one. Off its own dice and drawn only then, so a seeded hunt without
// the quest rolls what it always did. Ours: MU drops the treasures from monsters of their maps
// while the quest stands (WebZen's QuestUtil), at its own rate; this one is a proposal.
void Realm::treasure(const Body& dead) {
    if (!tables_ || !treasureGround(dead.column(), dead.row())) return;
    for (int index = 0; index < kQuests; ++index) {
        if (quests_[index].state != QuestState::Active) continue;
        const QuestRow& row = questAt(index);
        if (!questNative(row, int(bodies_[0].kin))) continue;
        for (int step = 0; step < row.stepCount; ++step) {
            const QuestStepRow& want = row.steps[step];
            if (want.kind != QuestStepKind::Find || !want.item) continue;
            if (quests_[index].counts[step] >= questGoal(index, step)) continue;
            const int32_t item = tables_->itemNamed(want.item);
            if (item < 0 || carried(item) >= 0) continue;
            bool lying = false;
            for (const Lying& one : lying_) lying = lying || one.what.item == item;
            if (lying) continue;
            if (treasureDice_.nextInt(0, 10000) >= kTreasureIn10000) continue;
            Lying found;
            found.what = Held{item, 0, 1};
            std::tie(found.column, found.row) = clearing(dead.column(), dead.row());
            found.vanishesAt = tick_ + int64_t(kTreasureLingerSeconds) * 20;
            found.id = nextId_++;
            lying_.push_back(found);
            say(What::Dropped, dead, int32_t(found.id), item, 0);
        }
    }
}

bool Realm::completeQuest(int index, int choice, QuestPath path) {
    if (index < 0 || index >= kQuests || questing_ < 0 || !serving(questing_)) return false;
    const QuestRow& row = questAt(index);
    // Handed in to whoever takes it back: Lirien for Peia's 'The Drowned Song'.
    if (tables_->folk[size_t(questing_)].number != questReceiver(row)) return false;
    QuestProgress& one = quests_[index];
    if (one.state != QuestState::Ready) return false;
    // A Find's thing must still be in the bag: sold or thrown away, the step is open again and
    // the treasure falls again.
    int handed[kQuestSteps];
    for (int step = 0; step < row.stepCount; ++step) {
        handed[step] = -1;
        if (row.steps[step].kind != QuestStepKind::Find || !row.steps[step].item) continue;
        handed[step] = carried(tables_->itemNamed(row.steps[step].item));
        if (handed[step] < 0) {
            one.state = QuestState::Active;
            one.counts[step] = 0;
            return false;
        }
    }
    // A choice is owed whenever his class has one to make.
    bool anyFits = false;
    for (int i = 0; i < row.choiceCount; ++i) anyFits |= questChoiceFits(index, i);
    if (anyFits && !questChoiceFits(index, choice)) return false;

    // Everything into the bag or nothing: tried on the bag itself and put back on a refusal, so
    // the check is the placing and the two cannot disagree.
    const Satchel before = bag_;
    // The treasures go to the giver first, so their cells are free for the pay.
    for (int step = 0; step < row.stepCount; ++step) {
        if (handed[step] >= 0) bag_.lift(handed[step]);
    }
    const auto pay = [&](const QuestItem& what, int* slotOut) {
        const int32_t item = what.item ? tables_->itemNamed(what.item) : -1;
        if (item < 0) return true;  // a row naming nothing cooked pays nothing, and says so below
        const content::ItemRow& itemRow = tables_->items[size_t(item)];
        if (stacks(itemRow)) {
            const int slot = give(item, -1, 0, std::max(1, what.count));
            if (slotOut) *slotOut = slot;
            return slot >= 0;
        }
        // Gear comes whole at its plus, lucky with its option (kQuestOption) and its empty
        // sockets; a Rune of Creation with its power, the Magic Gladiator's one he may set.
        const uint8_t powers[kMostSockets] = {
            bodies_[0].kin == Kin::MagicGladiator ? gladiatorRune(what.power) : what.power};
        const bool gear = takesOptions(itemRow) || jewellery(itemRow);
        for (int piece = 0; piece < std::max(1, what.count); ++piece) {
            const int slot = give(item, -1, what.plus, fullDurability(itemRow, what.plus), gear,
                                  gear ? kQuestOption : 0, 0, what.sockets,
                                  creation(itemRow) ? powers : nullptr, what.affixes);
            if (slot < 0) return false;
            if (slotOut && piece == 0) *slotOut = slot;
        }
        return true;
    };
    int chosenSlot = -1;
    bool paid = true;
    const bool first = questFirst(row, int(bodies_[0].kin), one.completions);
    const int paidKin = questPaidKin(row, int(bodies_[0].kin), first, path);
    for (int i = 0; i < row.paidCount && paid; ++i) {
        if (questPays(row.paid[i], paidKin, first) && questItemFits(row.paid[i])) {
            paid = pay(row.paid[i], nullptr);
        }
    }
    if (!paid || (anyFits && !pay(row.choices[choice], &chosenSlot))) {
        bag_ = before;
        return false;
    }

    money_ += row.zen;
    Body& hero = bodies_[0];
    const int32_t chosenItem =
        anyFits ? tables_->itemNamed(row.choices[choice].item) : -1;
    say(What::QuestDone, hero, index, chosenItem, chosenSlot);
    gain(hero, int32_t(questExperience(row, first)));
    const uint32_t completions = one.completions + 1;
    one = QuestProgress{};
    one.state = QuestState::Resting;
    one.completions = completions;
    one.availableAt = wall_ + row.repeatSeconds;
    if (row.promotes) hero.second = sim::promoted(quests_, int(hero.kin));
    return true;
}

}  // namespace mu::sim
