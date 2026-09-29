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
    // His story, on the user's word (2026-09-29: "some kind of story ... based on MU story
    // line"). MU's own premise: Kundun, sealed in Kalima, and his minions loose on the continent
    // as the seal fails (Blood Castle's "Kundun minions", the Lost Kalima gates), with the
    // Dungeon east of Lorencia (Webzen's Continent of MU map). Marlon as the last of Lorencia's
    // sworn knights, and the clear as a warning sent below, are ours.
    row.offer[0] =
        "\"When Kundun was bound in Kalima, the knights of Lorencia swore to keep this land "
        "while the seal held. I am what is left of that oath.\"";
    row.offer[1] =
        "\"But the seal is cracking! Every night more of his brood climbs out of the Dungeon to "
        "the east. The dead that walk the old road. The Liches that drive them. The Giants that "
        "follow the noise. And the beasts of the fields have gone mad with it: the spiders, the "
        "Budge Dragons, and the Bull Fighters in the hills.\"";
    row.offer[2] =
        "\"The guards hold the walls, and that is all they hold. I want the fields cleared. Not "
        "thinned. Cleared! Every breed of them. It will not mend the seal. But it tells whatever "
        "stirs under Kalima that Lorencia still has a sword.\"";
    // What to do and why, in his words, on the user's of 2026-09-29 ("he has to read what
    // character has to do and why"): the steps below, in their order, as he asks for them.
    row.offer[3] =
        "\"So hear me, and I beg you, do not fail me. Hunt the spiders at the edge of town. Slay "
        "the Budge Dragons, and the Bull Fighters in the hills. Put down the Hounds, and the "
        "Elite Bull Fighters that lead them. Then go east, to the old road, and destroy the "
        "Liches, the Giants, and the Skeleton Warriors. Every last one of them, until the fields "
        "fall silent. Do this, and Lorencia lives to see one more dawn. I have no one else to "
        "ask.\"";
    row.offer[4] =
        "\"They will come back. They always do. Come back to me when it is done.\"";
    row.underway =
        "\"Not yet! I can still hear them in the fields, and the dead still walk the old road.\"";
    row.handIn[0] =
        "\"Quiet. The fields have not been this quiet since the seal began to crack.\"";
    row.handIn[1] =
        "\"It will not last past nightfall. Kundun's brood never stays gone. Take your pay while "
        "it does, and choose what suits your hand.\"";
    row.resting =
        "\"Rest while you can. By morning they will have crept up out of the Dungeon again, and "
        "I will need you.\"";
    // Read by Chatterbox (Resemble AI, MIT) off Kokoro's bm_george, dramatic: source/voice.
    row.voice = "marlon";
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
