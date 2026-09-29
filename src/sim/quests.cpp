#include "sim/quests.h"

#include "sim/items.h"

namespace mu::sim {
namespace {

// Marlon's, and Lorencia's only: clear the map. The eight breeds are Lorencia's own (mu.db's
// spawns for map 0), weakest first, which is the order a character meets them walking out of
// town; their counts are left at 0 so the realm takes each breed's population, 290 in all.
//
// The reward is invention, the user's of 2026-09-29, and class by class: the Dark Knight's first,
// the Dark Wizard's with his runes later. Every clear pays experience worth about a third of the
// kills again, 50,000 Zen, three Jewels of Bless and twenty large potions. The first, and only
// the first, adds a one-handed weapon with luck and a socket, and a Rune of Creation to set in
// it -- the quest's one rune, never a repeat (sim/items.h). The weapon is one he can almost
// hold: a clear walked weakest first from level 1 ends near level 13, and the hand-in's
// experience takes him to 16, 103 strength spent all on strength; the Falchion asks 106 (MU's
// formula over its raw 120 at drop level 24), a level or two more. It is also the next built
// one-hander past the Gladius, which he could already hold, and it carries Uppercut.
QuestRow marlon() {
    QuestRow row;
    row.giver = 229;
    row.giverName = "Marlon";
    row.title = "Lorencia, Once More";
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
    // The hand-in is thanks, not more story, on the user's word of 2026-09-29: he is grateful,
    // and hopes the reward helps the hero save the continent from the dark.
    row.handIn[0] =
        "\"Quiet. The fields have not been this quiet since the seal began to crack. You did "
        "what my knights could not, and Lorencia will not forget it. Thank you.\"";
    row.handIn[1] =
        "\"Take these, with an old knight's gratitude. Your road runs far beyond these walls, "
        "and Kundun's darkness is waiting all along it. May they serve you well, until the "
        "whole continent of MU stands free of him.\"";
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
    row.zen = 50000;
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    row.paid[0] = {.item = "Sword08", .kin = knight, .luck = true, .sockets = 1,
                   .firstOnly = true};  // Falchion
    row.paid[1] = {.item = "Jewel22", .kin = knight, .power = uint8_t(Power::Stormcall),
                   .firstOnly = true};  // Rune of Creation, Stormcall's lightning
    row.paid[2] = {.item = "Jewel01", .count = 3};    // Jewels of Bless
    row.paid[3] = {.item = "Potion04", .count = 20};  // Large Healing Potions
    row.paidCount = 4;
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
