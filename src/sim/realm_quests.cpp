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
    if (one.kind != QuestStepKind::Clear) return 1;
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
    int untaken = -1, back = -1, rested = -1, first = -1;
    for (int i = 0; i < kQuests; ++i) {
        if (questAt(i).giver != giver) continue;
        if (first < 0) first = i;
        const QuestState state = quests_[i].state;
        if (state == QuestState::Active || state == QuestState::Ready) return i;
        if (state == QuestState::Untaken && untaken < 0 && questOffered(i)) untaken = i;
        if (state == QuestState::Resting) {
            if (back < 0 && questOffered(i)) back = i;
            rested = i;
        }
    }
    // The chain moves on before a link comes back round: the Halls before the Catacombs again.
    if (untaken >= 0) return untaken;
    if (back >= 0) return back;
    return rested >= 0 ? rested : first;
}

bool Realm::questLocked(int index) const {
    if (index < 0 || index >= kQuests) return false;
    const QuestRow& row = questAt(index);
    const QuestProgress& one = quests_[index];
    if (row.afterAny == 0 || one.state != QuestState::Untaken || one.completions > 0) return false;
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
        if (!counted) continue;
        // Ready once every Clear is at its goal: all at once and in any order.
        bool done = true;
        for (int step = 0; step < row.stepCount; ++step) {
            if (row.steps[step].kind == QuestStepKind::Clear &&
                one.counts[step] < questGoal(index, step)) {
                done = false;
            }
        }
        if (done) {
            one.state = QuestState::Ready;
            say(What::QuestReady, bodies_[0], index);
        }
    }
}

bool Realm::completeQuest(int index, int choice) {
    if (index < 0 || index >= kQuests || questing_ < 0 || !serving(questing_)) return false;
    const QuestRow& row = questAt(index);
    if (tables_->folk[size_t(questing_)].number != row.giver) return false;
    QuestProgress& one = quests_[index];
    if (one.state != QuestState::Ready) return false;
    // A choice is owed whenever his class has one to make.
    bool anyFits = false;
    for (int i = 0; i < row.choiceCount; ++i) anyFits |= questChoiceFits(index, i);
    if (anyFits && !questChoiceFits(index, choice)) return false;

    // Everything into the bag or nothing: tried on the bag itself and put back on a refusal, so
    // the check is the placing and the two cannot disagree.
    const Satchel before = bag_;
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
        // sockets; a Rune of Creation with its power.
        const uint8_t powers[kMostSockets] = {what.power};
        const bool gear = takesOptions(itemRow);
        for (int piece = 0; piece < std::max(1, what.count); ++piece) {
            const int slot = give(item, -1, what.plus, fullDurability(itemRow, what.plus), gear,
                                  gear ? kQuestOption : 0, 0, what.sockets,
                                  creation(itemRow) ? powers : nullptr);
            if (slot < 0) return false;
            if (slotOut && piece == 0) *slotOut = slot;
        }
        return true;
    };
    int chosenSlot = -1;
    bool paid = true;
    const bool first = questFirst(row, int(bodies_[0].kin), one.completions);
    for (int i = 0; i < row.paidCount && paid; ++i) {
        if (questPays(row.paid[i], int(bodies_[0].kin), first)) {
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
    return true;
}

}  // namespace mu::sim
