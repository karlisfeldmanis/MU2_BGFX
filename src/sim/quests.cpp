#include "sim/quests.h"

#include "sim/items.h"

namespace mu::sim {
namespace {

// Marlon's, and Lorencia's only: clear the map. The eight breeds are Lorencia's own (mu.db's
// spawns for map 0), weakest first, which is the order a character meets them walking out of
// town.
//
// The counts are one ladder shared with Peia's, balanced on the user's word of 2026-09-29: 40,
// 35, 30, 30, 25, 20, 15, 15 by the breeds' order, 210 kills. The two maps' breeds stand level
// for level (Lorencia's 2-19, Noria's 3-18), so one ladder asks about the same hunt of each:
// 34,000 health cut down here, 37,000 there. It was each breed's whole population here (290,
// but 45 Spiders to 15 Skeletons, 43,000) and forty of each there (320, with forty Golems,
// 69,000) -- Noria half again as long. Lorencia's last three still ask for about all of them.
//
// The reward is invention, the user's of 2026-09-29, and class by class: the Dark Knight's first,
// the Dark Wizard's with his runes later. Every clear pays 25,000 experience, 50,000 Zen, three
// Jewels of Bless and twenty large potions. The first, and only the first -- and only to one
// born in Lorencia -- raises the experience to 100,000 and adds a one-handed
// weapon with luck and a socket, and a Rune of Creation to set in it -- the quest's one rune,
// never a repeat (sim/items.h). The Falchion asks 106 strength (MU's formula over its raw 120
// at drop level 24). It is the next built one-hander past the Gladius, and it carries Uppercut.
// At kExperienceRate 10 the ladder walked weakest first from level 1 ends near level 28, so he
// can hold it at once. (Its note once said 13 and "a level or two short", but that was at the
// original rate of 1.)
QuestRow marlon() {
    QuestRow row;
    row.giver = 229;
    row.giverName = "Marlon";
    row.place = "Lorencia";
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
        "the east, and the dead walk the old road again. Even the beasts of the fields have gone "
        "mad with it.\"";
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
    // and hopes the reward helps the hero save the continent from the dark. Short: the first
    // take, four sentences, was too long. And it opens surprised: he did not think it could be
    // done so soon (the user, the same day).
    row.handIn[0] =
        "\"Back already? I did not believe it could be done so quickly. The fields are quiet, "
        "and Lorencia thanks you.\"";
    row.handIn[1] =
        "\"Take these. May they serve you well, until all of MU is free of Kundun's darkness. "
        "And when you are ready, ride for Devias. Apostle Devin holds the line there, and he "
        "will have need of a sword like yours.\"";
    row.resting =
        "\"Rest while you can. By morning they will have crept up out of the Dungeon again, and "
        "I will need you.\"";
    // The way on, once Lorencia is clear (the user, 2026-09-29): Devias, and its quest giver.
    // Apostle Devin is MU's own there (MONSTER 406, the third class's quest in Season Two and
    // after, not 0.75); Devias is not built yet, so for now this is only the tip.
    row.next = "Seek Apostle Devin in Devias";
    // Read by Chatterbox (Resemble AI, MIT) off Kokoro's bm_lewis, heroic: source/voice.
    row.voice = "marlon";
    row.steps[0] = {QuestStepKind::Clear, 3, 40, "Spiders"};
    row.steps[1] = {QuestStepKind::Clear, 2, 35, "Budge Dragons"};
    row.steps[2] = {QuestStepKind::Clear, 0, 30, "Bull Fighters"};
    row.steps[3] = {QuestStepKind::Clear, 1, 30, "Hounds"};
    row.steps[4] = {QuestStepKind::Clear, 4, 25, "Elite Bull Fighters"};
    row.steps[5] = {QuestStepKind::Clear, 6, 20, "Liches"};
    row.steps[6] = {QuestStepKind::Clear, 7, 15, "Giants"};
    row.steps[7] = {QuestStepKind::Clear, 14, 15, "Skeleton Warriors"};
    row.steps[8] = {QuestStepKind::Return, 0, 1, "Return to Marlon"};
    row.stepCount = 9;
    row.repeatSeconds = 12 * 60 * 60;
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    // Lorencia's born, the knight and the wizard; an elf may come for the jewels, Zen and
    // experience, and goes without the weapon, the rune and the first clear's experience.
    row.natives = uint8_t((1u << knight) | (1u << wizard));
    row.strangers = true;
    row.experience = 25000;
    // The first clear's, raised on the user's word of 2026-09-29: at the level-28 end of the
    // ladder 100,000 carries about three levels (neededExperience: 262,440 at 28, 351,000 at 31),
    // where the repeat's 25,000 is most of one.
    row.firstExperience = 100000;
    row.zen = 50000;
    row.paid[0] = {.item = "Sword08", .kin = knight, .luck = true, .sockets = 1,
                   .firstOnly = true};  // Falchion
    row.paid[1] = {.item = "Jewel22", .kin = knight, .power = uint8_t(Power::Stormcall),
                   .firstOnly = true};  // Rune of Creation, Stormcall's lightning
    // The wizard's (the user, 2026-09-29): a lucky Serpent Staff with a socket, and the rune
    // that echoes his spells.
    row.paid[2] = {.item = "Staff03", .kin = wizard, .luck = true, .sockets = 1,
                   .firstOnly = true};  // Serpent Staff
    row.paid[3] = {.item = "Jewel22", .kin = wizard, .power = uint8_t(Power::Echo),
                   .firstOnly = true};  // Rune of Creation, Arcane Echo
    row.paid[4] = {.item = "Jewel01", .count = 3};    // Jewels of Bless
    row.paid[5] = {.item = "Potion04", .count = 20};  // Large Healing Potions
    row.paidCount = 6;
    return row;
}

// Peia's, and Noria's only: the elves' forest, sick since Kundun's seal began to crack. Marlon's
// quest shaped for the Fairy Elf (the user, 2026-09-29: "basically its same rewards as Marlon but
// for elfs"), with a story of Noria's own.
//
// Its counts are Marlon's ladder (see marlon()), never Noria's whole population: Noria holds
// 1005, far too long a hunt for a clear that comes back every twelve hours. Walked weakest
// first from level 1 the 210 end near level 29, well past the Battle Bow's 90 agility and 43
// strength. Invention, all of it.
QuestRow peia() {
    QuestRow row;
    row.giver = 257;
    row.giverName = "Peia";
    row.place = "Noria";
    row.title = "Noria's Song";
    // Her story, ours: the same seal as Marlon's cracking, seen from the elves' forest -- a song
    // in the trees gone quiet, its old keepers the Forest Monsters turned, the Stone Golems
    // woken, the goblins at the gates. Noria's own breeds (mu.db's spawns for map 3).
    row.offer[0] = "\"Walk softly, traveller. This is Noria, the elves' own forest, and it is sick.\"";
    row.offer[1] =
        "\"Its trees carry a song older than any kingdom. Since Kundun's seal began to crack, the "
        "song has gone quiet. The Forest Monsters were its keepers once; now they are rotten "
        "wood. The Stone Golems have woken in the old stones, and the goblins grow bold enough "
        "to raid our gates.\"";
    row.offer[2] =
        "\"Hunt them for me: the Goblins and their Elites, the Chain Scorpions, the Beetle "
        "Monsters, the Hunters, the Forest Monsters, Agon and the Golems. Enough of each that "
        "the trees can sing again. Come back to me when it is done.\"";
    row.underway = "\"Not yet. Listen -- the forest is still silent.\"";
    row.handIn[0] =
        "\"Already? I did not think a stranger's bow could do so much, so soon. Listen -- the "
        "forest is singing again.\"";
    row.handIn[1] =
        "\"Take these, with the elves' blessing. May they guide your arrows, until all of MU "
        "is free of Kundun's darkness. When you are ready, go to Devias, beyond Lorencia. "
        "Apostle Devin waits there; he will know what must be done next.\"";
    row.resting =
        "\"Rest, and listen to it while it lasts. The dark will creep back into the roots by "
        "morning.\"";
    row.next = "Seek Apostle Devin in Devias";  // as Marlon's
    row.voice = "peia";
    row.steps[0] = {QuestStepKind::Clear, 26, 40, "Goblins"};
    row.steps[1] = {QuestStepKind::Clear, 27, 35, "Chain Scorpions"};
    row.steps[2] = {QuestStepKind::Clear, 33, 30, "Elite Goblins"};
    row.steps[3] = {QuestStepKind::Clear, 28, 30, "Beetle Monsters"};
    row.steps[4] = {QuestStepKind::Clear, 29, 25, "Hunters"};
    row.steps[5] = {QuestStepKind::Clear, 30, 20, "Forest Monsters"};
    row.steps[6] = {QuestStepKind::Clear, 31, 15, "Agon"};
    row.steps[7] = {QuestStepKind::Clear, 32, 15, "Stone Golems"};
    row.steps[8] = {QuestStepKind::Return, 0, 1, "Return to Peia"};
    row.stepCount = 9;
    row.repeatSeconds = 12 * 60 * 60;
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    // The elves' alone: one born in Lorencia is turned away (quests.h, `natives`).
    row.natives = uint8_t(1u << elf);
    row.strangers = false;
    row.stranger =
        "\"The forest does not know your step, traveller. Its song is for the elves to mend. Go "
        "back to Lorencia -- Marlon will have work for you there.\"";
    row.experience = 25000;
    row.firstExperience = 100000;  // Marlon's, for the same ladder
    row.zen = 50000;
    row.paid[0] = {.item = "Bow04", .kin = elf, .luck = true, .sockets = 1,
                   .firstOnly = true};  // Battle Bow
    row.paid[1] = {.item = "Jewel22", .kin = elf, .power = uint8_t(Power::Frost),
                   .firstOnly = true};  // Rune of Creation, Frost Arrow
    row.paid[2] = {.item = "Jewel01", .count = 3};    // Jewels of Bless
    row.paid[3] = {.item = "Potion04", .count = 20};  // Large Healing Potions
    row.paidCount = 4;
    return row;
}

// Apostle Devin's, and Devias's: "The White Silence", for every class (docs/devin-quest.md, the
// user's of 2026-09-29). Devin is MU's own (Priest Devin 406, Season Six), his story is ours:
// serious, short and clear, the Ice Queens the real danger and the Lost Tower named once, at the
// hand-in. The steps are Devias's breeds weakest first on Marlon's ladder shape (Version075
// Devias.cs's populations in the doc); the Elite Yeti stands for "Yetis", as Devias's plain Yeti
// is a single camp.
//
// The reward is the doc's proposal, not yet agreed: every clear 3 Jewels of Bless, 100,000 Zen and
// 60,000 experience, the first 250,000 and each class's top 0.75 armour with an empty socket --
// Dragon (knight), Legendary (wizard), Guardian (elf) -- and, the first time too, the rune for
// its socket: a Rune of Creation carrying the Undying (+20% maximum health, sim/items.h), the
// same for every class.
QuestRow devin() {
    QuestRow row;
    row.giver = 406;
    row.giverName = "Apostle Devin";
    row.place = "Devias";
    row.title = "The White Silence";
    row.offer[0] =
        "\"So you are the one. Lorencia is quiet, and Noria's trees sing again. Word travels, "
        "even this far north.\"";
    row.offer[1] =
        "\"I am Devin, and this is Devias. Snow buries the roads, Yetis roam the passes, and "
        "Kundun's Assassins hunt anyone who walks alone.\"";
    row.offer[2] =
        "\"But the Ice Queens are the real danger. They call the storms down on us, and while "
        "they live, the storms never stop.\"";
    row.offer[3] =
        "\"Clear the Worms, Ice Monsters, Hommerds, Assassins and Yetis. Then bring down the "
        "Queens.\"";
    row.offer[4] = "\"Dress for the cold, and come back alive.\"";
    row.underway = "\"Not yet. The storms still rage, and the Queens still live.\"";
    row.handIn[0] = "\"The sky over Devias is clear again. Thank you.\"";
    row.handIn[1] =
        "\"Take these. You will need them for what comes next: the Lost Tower. But that is a "
        "story for another day.\"";
    row.resting = "\"Rest now. By morning, the Queens will call the storms back.\"";
    // None until the Lost Tower is decided (the doc's "Next").
    row.next = "";
    // Read by hand, not by voice.py's main(): source/voice/devin/recorded_with.py.txt.
    row.voice = "devin";
    row.steps[0] = {QuestStepKind::Clear, 24, 40, "Worms"};
    row.steps[1] = {QuestStepKind::Clear, 22, 35, "Ice Monsters"};
    row.steps[2] = {QuestStepKind::Clear, 23, 30, "Hommerds"};
    row.steps[3] = {QuestStepKind::Clear, 21, 25, "Assassins"};
    row.steps[4] = {QuestStepKind::Clear, 20, 30, "Elite Yetis"};
    row.steps[5] = {QuestStepKind::Clear, 25, 10, "Ice Queens"};
    row.steps[6] = {QuestStepKind::Return, 0, 1, "Return to Apostle Devin"};
    row.stepCount = 7;
    row.repeatSeconds = 12 * 60 * 60;
    // Only to one who has cleared Lorencia or Noria (the user, 2026-09-30): both of those
    // hand-ins send the hero on to him.
    row.afterAny = (1u << 0) | (1u << 1);
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    // Every class's: no 0.75 class is born in Devias, and all three come to it.
    row.natives = uint8_t((1u << knight) | (1u << wizard) | (1u << elf));
    row.strangers = true;
    row.experience = 60000;
    row.firstExperience = 250000;
    row.zen = 100000;
    row.paid[0] = {.item = "ArmorMale02", .kin = knight, .sockets = 1,
                   .firstOnly = true};  // Dragon Armor
    row.paid[1] = {.item = "ArmorMale04", .kin = wizard, .sockets = 1,
                   .firstOnly = true};  // Legendary Armor
    row.paid[2] = {.item = "ArmorElf05", .kin = elf, .sockets = 1,
                   .firstOnly = true};  // Guardian Armor
    row.paid[3] = {.item = "Jewel01", .count = 3};  // Jewels of Bless
    // And the rune for that socket, the same for every class (the user, 2026-09-30).
    row.paid[4] = {.item = "Jewel22", .power = uint8_t(Power::Undying),
                   .firstOnly = true};  // Rune of Creation, the Undying
    row.paidCount = 5;
    return row;
}

const QuestRow kTable[kQuests] = {marlon(), peia(), devin()};

}  // namespace

const QuestRow& questAt(int index) { return kTable[index]; }

int questOf(int32_t giver) {
    for (int i = 0; i < kQuests; ++i) {
        if (kTable[i].giver == giver) return i;
    }
    return -1;
}

}  // namespace mu::sim
