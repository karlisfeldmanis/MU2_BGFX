#include "sim/quests.h"

namespace mu::sim {
namespace {

// Marlon's, and Lorencia's only: clear the map. The eight breeds are Lorencia's own (mu.db's
// spawns for map 0), weakest first, which is the order a character meets them walking out of
// town; their counts are left at 0 so the realm takes each breed's population, 290 in all.
//
// The reward is set against that hunt and is invention: a clear is hours for a new character and
// minutes for a strong one, and it comes back every twelve hours, so it pays well without
// replacing the hunt it asks for -- experience worth about a third of the kills again, a purse,
// five large potions, and a choice of the class's +4 starting weapon or armour or two jewels.
QuestRow marlon() {
    QuestRow row;
    row.giver = 229;
    row.giverName = "Marlon";
    row.title = "Clear Lorencia";
    row.offer[0] =
        "\"The guards hold the walls, and that is all they hold. Outside them the fields belong "
        "to whatever crawls out of the hills.\"";
    row.offer[1] =
        "\"I want them cleared. Not thinned: cleared. Every spider in the grass, every dragon "
        "in the fields, every one of the dead that walk by the old road.\"";
    row.offer[2] =
        "\"They will come back, they always do. But for half a day Lorencia will breathe. Come "
        "back to me when it is done.\"";
    row.underway = "\"The fields are not clear yet. I can hear them from here.\"";
    row.handIn[0] =
        "\"Quiet. I have not heard the fields this quiet since before the war.\"";
    row.handIn[1] =
        "\"It will not last past nightfall, so take your pay while it does. Choose what suits "
        "your hand.\"";
    row.resting =
        "\"You have done enough for today. Come back when they have crept down from the hills "
        "again.\"";
    row.steps[0] = {QuestStepKind::Clear, 3, 0, "Spiders"};
    row.steps[1] = {QuestStepKind::Clear, 2, 0, "Budge Dragons"};
    row.steps[2] = {QuestStepKind::Clear, 0, 0, "Bull Fighters"};
    row.steps[3] = {QuestStepKind::Clear, 1, 0, "Hounds"};
    row.steps[4] = {QuestStepKind::Clear, 4, 0, "Elite Bull Fighters"};
    row.steps[5] = {QuestStepKind::Clear, 6, 0, "Liches"};
    row.steps[6] = {QuestStepKind::Clear, 7, 0, "Giants"};
    row.steps[7] = {QuestStepKind::Clear, 14, 0, "Skeleton Warriors"};
    row.steps[8] = {QuestStepKind::Return, 0, 1, "Return to Marlon"};
    row.stepCount = 9;
    row.repeatSeconds = 12 * 60 * 60;
    row.experience = 25000;
    row.zen = 30000;
    row.always = {"Potion04", 5, 0};
    row.choices[0] = {"Sword01", 1, 4};      // Kris
    row.choices[1] = {"Staff01", 1, 4};      // Skull Staff
    row.choices[2] = {"Bow01", 1, 4};        // Short Bow
    row.choices[3] = {"ArmorMale06", 1, 4};  // Leather Armor
    row.choices[4] = {"ArmorMale03", 1, 4};  // Pad Armor
    row.choices[5] = {"ArmorElf01", 1, 4};   // Vine Armor
    row.choices[6] = {"Jewel01", 2, 0};      // two Jewels of Bless
    row.choiceCount = 7;
    return row;
}

const QuestRow kTable[kQuests] = {marlon()};

}  // namespace

const QuestRow& questAt(int index) { return kTable[index]; }

int questOf(int32_t giver) {
    for (int i = 0; i < kQuests; ++i) {
        if (kTable[i].giver == giver) return i;
    }
    return -1;
}

}  // namespace mu::sim
