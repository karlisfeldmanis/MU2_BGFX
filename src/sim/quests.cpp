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
// At kExperienceRate 10 the ladder walked weakest first from level 1 ended near level 28 (higher
// at 100, since 2026-10-01), so he can hold it at once. (Its note once said 13 and "a level or two short", but that was at the
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
    row.paid[0] = {.item = "Sword08", .kin = knight, .sockets = 1,
                   .firstOnly = true};  // Falchion
    row.paid[1] = {.item = "Jewel22", .kin = knight, .power = uint8_t(Power::Stormcall),
                   .firstOnly = true};  // Rune of Creation, Stormcall's lightning
    // The wizard's (the user, 2026-09-29): a lucky Serpent Staff with a socket, and the rune
    // that echoes his spells.
    row.paid[2] = {.item = "Staff03", .kin = wizard, .sockets = 1,
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
    // The elves' born; a knight or a wizard may come for the jewels, Zen and experience, and goes
    // without the bow, the rune and the first clear's experience, as an elf does at Marlon's (the
    // user, 2026-09-30).
    row.natives = uint8_t(1u << elf);
    row.strangers = true;
    row.experience = 25000;
    row.firstExperience = 100000;  // Marlon's, for the same ladder
    row.zen = 50000;
    row.paid[0] = {.item = "Bow04", .kin = elf, .sockets = 1,
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
// its socket: a Rune of Creation carrying Renewal (health back anywhere, sim/items.h; it was the
// Undying's +20% maximum health until 2026-09-30), the same for every class.
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
        "\"Take these. Your road goes on to the Lost Tower. Find Tersia in its hall. She will "
        "tell you the rest.\"";
    row.resting = "\"Rest now. By morning, the Queens will call the storms back.\"";
    // The way on (the user, 2026-10-01): the Lost Tower and its keeper, Tersia's chain.
    row.next = "Seek Tersia in the Lost Tower";
    // Read by hand, not by voice.py's main(): source/voice/devin/recorded_with.py.txt. The
    // hand-in was read again for Tersia on 2026-10-02, both paragraphs, the same way.
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
    // And the rune for that socket, the same for every class (the user, 2026-09-30): Renewal,
    // health back anywhere, in the Undying's place since the same day ("life regeneration rune
    // which is much more important i think than just max hp").
    row.paid[4] = {.item = "Jewel22", .power = uint8_t(Power::Renewal),
                   .firstOnly = true};  // Rune of Creation, Renewal
    row.paidCount = 5;
    return row;
}

// The Golden Archer's chain, the Dungeon's (docs/golden-archer.md): three links, a floor each,
// each unlocked by the one before, every one paying and the first time of each also a socketed
// item and a rune (the user, 2026-09-30). MU's NPC 236, a gold skeleton at the Dungeon's arch;
// the story is ours: the knights' archer, the last of the oath's last company, who fell to the
// Gorgon and keeps the gate. The steps are each floor's breeds weakest first (dungeon-port.md
// A §1.6), counts the doc's proposals. The rewards are the user's of 2026-09-30: a socketed helm,
// pants and boots with the Dungeon's three runes (sim/items.h), three Bless each, a Soul at the Pit;
// the Zen and experience are proposals.
QuestRow archer() {
    QuestRow row;
    row.giver = 236;
    row.giverName = "Golden Archer";
    row.place = "Lorencia";
    row.repeatSeconds = 12 * 60 * 60;
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    // Every class's, as Devin's: the Dungeon is under Lorencia, but all three go down.
    row.natives = uint8_t((1u << knight) | (1u << wizard) | (1u << elf));
    row.strangers = true;
    return row;
}

QuestRow catacombs() {
    QuestRow row = archer();
    row.title = "The Catacombs";
    row.offer[0] =
        "\"Stop there. You are alive, and you mean to go down. Few who do come back.\"";
    row.offer[1] =
        "\"Do not be afraid of me. I was a knight of Lorencia once. What you see is what the "
        "Dungeon left.\"";
    row.offer[2] =
        "\"Below us are the Catacombs, where Lorencia buried its dead. They do not stay buried "
        "now. Every night they climb, and every night I shoot them back down.\"";
    row.offer[3] =
        "\"Go down the stair. Kill the Skeleton Warriors, the Larvae and the Cyclopes. Then the "
        "Ghosts, and the Skeleton Archers in the deep tombs.\"";
    row.offer[4] = "\"I will count them as they fall. Come back when I am done counting.\"";
    row.underway = "\"I am still counting. They still climb.\"";
    row.handIn[0] = "\"The Catacombs are quiet. I have not heard that in a long time.\"";
    row.handIn[1] = "\"Take these. You fight like one of us. When you are ready, there is more below.\"";
    row.resting = "\"They will climb again by morning. I will be here, counting.\"";
    row.next = "The Golden Archer has more below: the Knights' Halls";
    row.voice = "golden_archer_1";
    row.steps[0] = {QuestStepKind::Clear, 14, 30, "Skeleton Warriors"};
    row.steps[1] = {QuestStepKind::Clear, 12, 25, "Larvae"};
    row.steps[2] = {QuestStepKind::Clear, 17, 25, "Cyclopes"};
    row.steps[3] = {QuestStepKind::Clear, 11, 30, "Ghosts"};
    row.steps[4] = {QuestStepKind::Clear, 15, 7, "Skeleton Archers"};
    row.steps[5] = {QuestStepKind::Return, 0, 1, "Return to the Golden Archer"};
    row.stepCount = 6;
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    row.experience = 50000;
    row.firstExperience = 150000;
    row.zen = 60000;
    // The user's (2026-09-30): a helm with a socket, and Keen Eye for it, every class's rune;
    // three Jewels of Bless. The helms are each class's near the floor's level.
    row.paid[0] = {.item = "HelmMale09", .kin = knight, .sockets = 1, .firstOnly = true};  // Brass
    row.paid[1] = {.item = "HelmMale08", .kin = wizard, .sockets = 1, .firstOnly = true};  // Sphinx
    row.paid[2] = {.item = "HelmElf03", .kin = elf, .sockets = 1, .firstOnly = true};      // Wind
    row.paid[3] = {.item = "Jewel22", .power = uint8_t(Power::KeenEye), .firstOnly = true};
    row.paid[4] = {.item = "Jewel01", .count = 3};  // Jewels of Bless
    row.paidCount = 5;
    return row;
}

QuestRow halls() {
    QuestRow row = archer();
    row.title = "The Knights' Halls";
    row.offer[0] =
        "\"Under the Catacombs are the Halls. We built them, when Kundun fell, to hold what he "
        "left beneath Lorencia.\"";
    row.offer[1] =
        "\"The traps are ours. Lances, iron and fire, set for his brood. They cannot tell you "
        "from it. Watch the floor.\"";
    row.offer[2] =
        "\"His creatures hold our halls now. Kill the Hell Hounds, the Hell Spiders and the Elite "
        "Skeletons. Then the Thunder Liches, and the Poison Bulls at the far end.\"";
    row.offer[3] = "\"Take our halls back. I cannot go down to do it myself.\"";
    row.underway = "\"The halls are not ours yet. Go back down.\"";
    row.handIn[0] =
        "\"The Halls are ours again, for tonight. You have done what a whole company could "
        "not.\"";
    row.handIn[1] = "\"Take these. When you come back, I will tell you why I stand here.\"";
    row.resting = "\"Rest. The halls will fill again, and the traps will still be waiting.\"";
    row.next = "The Golden Archer will tell you why he keeps the gate";
    row.voice = "golden_archer_2";
    row.steps[0] = {QuestStepKind::Clear, 5, 20, "Hell Hounds"};
    row.steps[1] = {QuestStepKind::Clear, 13, 15, "Hell Spiders"};
    row.steps[2] = {QuestStepKind::Clear, 16, 25, "Elite Skeletons"};
    row.steps[3] = {QuestStepKind::Clear, 9, 15, "Thunder Liches"};
    row.steps[4] = {QuestStepKind::Clear, 8, 10, "Poison Bulls"};
    row.steps[5] = {QuestStepKind::Return, 0, 1, "Return to the Golden Archer"};
    row.stepCount = 6;
    row.afterAny = 1u << 3;  // the Catacombs handed in
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    row.experience = 70000;
    row.firstExperience = 250000;
    row.zen = 80000;
    // The user's: pants with a socket, and Bloodwell for them; three Jewels of Bless. The knight's
    // are Plate, the oath-knights' own set.
    row.paid[0] = {.item = "PantMale10", .kin = knight, .sockets = 1, .firstOnly = true};  // Plate
    row.paid[1] = {.item = "PantMale04", .kin = wizard, .sockets = 1, .firstOnly = true};  // Legendary
    row.paid[2] = {.item = "PantElf04", .kin = elf, .sockets = 1, .firstOnly = true};      // Spirit
    row.paid[3] = {.item = "Jewel22", .power = uint8_t(Power::Bloodwell), .firstOnly = true};
    row.paid[4] = {.item = "Jewel01", .count = 3};  // Jewels of Bless
    row.paidCount = 5;
    return row;
}

QuestRow pit() {
    QuestRow row = archer();
    row.title = "The Pit";
    row.offer[0] =
        "\"The last company of the oath went down into the Pit to hold it. I went with them. None "
        "of us came back as we were.\"";
    row.offer[1] =
        "\"The Gorgon waits at the bottom. It looked on my brothers, and they rose as Dark "
        "Knights. It looked on me, and I rose as this.\"";
    row.offer[2] =
        "\"Kill the Poison Bulls and the Thunder Liches on the way down. Then find my brothers, "
        "and give them rest. Then the Gorgon.\"";
    row.offer[3] =
        "\"Marlon thinks he is the last of us. Do not tell him what you find down there. Not "
        "yet.\"";
    row.underway = "\"My brothers still walk. Go back down.\"";
    row.handIn[0] = "\"It is done. I felt it when they fell. My brothers are at rest.\"";
    // The card holds two hand-in paragraphs, so the doc's third joins the second.
    row.handIn[1] =
        "\"My name was Aldric. Remember it, even if Marlon cannot. Take these, with my thanks. "
        "I will keep the gate. It is what I swore.\"";
    row.resting = "\"The Gorgon will rise again. They always do. I will be here, counting.\"";
    row.voice = "golden_archer_3";
    row.steps[0] = {QuestStepKind::Clear, 8, 15, "Poison Bulls"};
    row.steps[1] = {QuestStepKind::Clear, 9, 10, "Thunder Liches"};
    row.steps[2] = {QuestStepKind::Clear, 10, 7, "Dark Knights"};
    row.steps[3] = {QuestStepKind::Clear, 18, 1, "The Gorgon"};
    row.steps[4] = {QuestStepKind::Return, 0, 1, "Return to the Golden Archer"};
    row.stepCount = 5;
    row.afterAny = 1u << 4;  // the Halls handed in
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    row.experience = 100000;
    row.firstExperience = 400000;
    row.zen = 120000;
    // The user's: boots with a socket, and Frenzy for them; three Jewels of Bless and a Jewel of
    // Soul on top, the chain's last. Each class's top set, about the Gorgon's level. Two sockets
    // since 2026-10-02 ("give 2 sockets for boots. same for all classes").
    row.paid[0] = {.item = "BootMale02", .kin = knight, .sockets = 2, .firstOnly = true};  // Dragon
    row.paid[1] = {.item = "BootMale04", .kin = wizard, .sockets = 2, .firstOnly = true};  // Legendary
    row.paid[2] = {.item = "BootElf05", .kin = elf, .sockets = 2, .firstOnly = true};      // Guardian
    row.paid[3] = {.item = "Jewel22", .power = uint8_t(Power::Frenzy), .firstOnly = true};
    row.paid[4] = {.item = "Jewel01", .count = 3};  // Jewels of Bless
    row.paid[5] = {.item = "Jewel02"};              // Jewel of Soul
    // And for every class a Ring of Ice with one socket, a ring's most (sim::mostSocketsOf; the
    // user, 2026-10-02: "keep only 1 socket for the rings"). It came with Evil Spirit, the
    // legendary rune, to set in it, until the user, 2026-10-02: "in this quest we give to much.
    // we dont give epic runes so early, but we can give that ring with +socket", then "keep 1
    // rune, but not the legendary one".
    row.paid[6] = {.item = "Ring01", .sockets = 1, .firstOnly = true};  // of Ice
    row.paidCount = 7;
    return row;
}

// The Lost Tower's way in (docs/lost-tower-quest.md, the user's of 2026-10-01): Devin's hand-in
// sends the hero to the tower, where its last keeper gives the first quest and her hall opens on
// the travel list. She is MU's own, not 0.75's: Tersia (566, Npc/tersia.bmd, MuMain's MODEL_TERSIA;
// its Korean ID 길드관리인 테르시아, "Guild Manager Tersia", which OpenMU and the English client
// call Mercenary Guild Felicia) -- a purple-haired woman in dark leather armour, the user's pick
// on 2026-10-01 after Oracle Layla (Npc/kalnpc.bmd, sat on a boat of potions) and Senatus.
// Tersia as the shrine's last guard is ours.
//
// Tersia's chain, the Lost Tower's (the user, 2026-10-01: "similar like golden archer where
// char has to clear each floor and gets rewards"): seven links, a floor each, each offered once the
// one before is handed in, the first once Devin's is. Each link's steps are its floor's breeds
// (docs/lost-tower-port.md A §4.1), counts about half the floor; the last ends on the Balrog. The
// travel row to floors 2-7 opens once its link is handed in, not only taken as the Dungeon's do
// (Realm::travelQuest); the hall's opens as she is spoken to; the stairs ask only their levels. Every class's, each link repeatable every twelve hours.
// What she says of the tower is Webzen's (above) or the game's own (the Cursed Wizards' reach,
// the Devils' push, the Gorgons' rolling fire, the burning plates); the rest is ours. The rewards
// are proposals: every clear experience, Zen and jewels rising floor by floor; the first clear of
// the first link each class's top gloves with a socket and the Undying (no quest has paid it since
// Devin's became Renewal); the Balrog's a Jewel of Chaos on top.
QuestRow tersiaLink(const char* title, int64_t experience, int64_t first, int64_t zen, int bless,
                   int soul) {
    QuestRow row;
    row.giver = 566;
    row.giverName = "Tersia";
    row.place = "the Lost Tower";
    row.title = title;
    row.repeatSeconds = 12 * 60 * 60;
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    row.natives = uint8_t((1u << knight) | (1u << wizard) | (1u << elf));
    row.strangers = true;
    row.experience = experience;
    row.firstExperience = first;
    row.zen = zen;
    row.paid[row.paidCount++] = {.item = "Jewel01", .count = bless};  // Jewels of Bless
    if (soul > 0) row.paid[row.paidCount++] = {.item = "Jewel02", .count = soul};  // of Soul
    return row;
}

QuestRow tersiaDoor() {
    QuestRow row = tersiaLink("The Last Contract", 100000, 400000, 100000, 2, 0);
    row.offer[0] =
        "\"Devin sent you? Good. Sit down before you fall down. I know that look. I wear it "
        "myself.\"";
    row.offer[1] =
        "\"My name is Tersia. The Mercenary Guild took a contract to guard this shrine. "
        "Twelve of us came up the road. I am what is left.\"";
    row.offer[2] =
        "\"The contract was never closed, and I cannot close it alone. I have not slept a "
        "whole night since the shrine fell.\"";
    row.offer[3] =
        "\"The Shadows come out of the walls on this floor, and the Poison Shadows behind "
        "them. Thin them out for me. I will pay what the guild owes.\"";
    row.underway = "\"I can still hear them in the walls. Keep going.\"";
    row.handIn[0] = "\"Quiet. I had forgotten what that sounds like.\"";
    row.handIn[1] =
        "\"Here is your pay. Guild rates, and a little more, because nobody else would come.\"";
    row.resting =
        "\"Let me sleep an hour. Just one. Then we talk about the next floor.\"";
    row.next = "Tersia has another contract: the second floor";
    row.steps[0] = {QuestStepKind::Clear, 36, 40, "Shadows"};
    row.steps[1] = {QuestStepKind::Clear, 39, 15, "Poison Shadows"};
    row.steps[2] = {QuestStepKind::Return, 0, 1, "Return to Tersia"};
    row.stepCount = 3;
    row.afterAny = 1u << 2;  // Devin's handed in: his hand-in sends the hero to her
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    row.paid[row.paidCount++] = {.item = "GloveMale02", .kin = knight, .sockets = 1,
                                 .firstOnly = true};  // Dragon
    row.paid[row.paidCount++] = {.item = "GloveMale04", .kin = wizard, .sockets = 1,
                                 .firstOnly = true};  // Legendary
    row.paid[row.paidCount++] = {.item = "GloveElf05", .kin = elf, .sockets = 1,
                                 .firstOnly = true};  // Guardian
    row.paid[row.paidCount++] = {.item = "Jewel22", .power = uint8_t(Power::Undying),
                                 .firstOnly = true};
    row.voice = "tersia_1";  // tools/voice.py, VOICES["tersia"]
    return row;
}

QuestRow tersiaSecond() {
    QuestRow row = tersiaLink("Bad Air", 110000, 350000, 110000, 2, 0);
    row.offer[0] =
        "\"Brann held the second floor. A big man who laughed at everything. The poison took "
        "him slowly.\"";
    row.offer[1] =
        "\"The Poison Shadows nest there now, and Cursed Wizards walk among them. Mind the "
        "plates in the floor. They still burn.\"";
    row.offer[2] =
        "\"Clear it, and I will write Brann's name in the guild book as avenged.\"";
    row.underway = "\"Is the air still thick up there? Then it is not done.\"";
    row.handIn[0] = "\"I will write his name tonight. Thank you.\"";
    row.handIn[1] =
        "\"Your pay. Do not spend it all on potions. No. Spend it all on potions. You will "
        "need them.\"";
    row.resting = "\"I am writing names. Leave me a while.\"";
    row.next = "Tersia has another contract: the third floor";
    row.steps[0] = {QuestStepKind::Clear, 39, 35, "Poison Shadows"};
    row.steps[1] = {QuestStepKind::Clear, 34, 12, "Cursed Wizards"};
    row.steps[2] = {QuestStepKind::Return, 0, 1, "Return to Tersia"};
    row.stepCount = 3;
    row.afterAny = 1u << 6;
    // The first clears of links 2-7 (the user, 2026-10-02: "we made bunch of new runes but we
    // need to also armors or weapons/shield/rings also for rewards"; "lets do it we can always
    // change rewards later"): what the set from Devin and the Dungeon lacks -- a second ring, a
    // better weapon, the pendant, a shield, the class runes, and the Balrog's top weapon. Ours.
    // Here the second ring, and the rune for a pet's keeper.
    row.paid[row.paidCount++] = {.item = "Ring02", .sockets = 1, .firstOnly = true};  // of Poison
    row.paid[row.paidCount++] = {.item = "Jewel22", .power = uint8_t(Power::Kinship),
                                 .firstOnly = true};
    row.voice = "tersia_2";  // tools/voice.py, VOICES["tersia"]
    return row;
}

QuestRow tersiaThird() {
    QuestRow row = tersiaLink("Fire from Afar", 130000, 400000, 130000, 3, 1);
    row.offer[0] =
        "\"Our archers held the third floor, until the Cursed Wizards came.\"";
    row.offer[1] =
        "\"Their fire reaches farther than any bow. Do not trade shots with them. Get close, "
        "and fast.\"";
    row.offer[2] =
        "\"The Death Cows will charge you while you do it. Put them down as well.\"";
    row.underway = "\"I can still see firelight on the stairs.\"";
    row.handIn[0] = "\"Dark up there again. Good. Dark is good.\"";
    row.handIn[1] =
        "\"Pay, as promised. Past that floor the stone turns red. I never went that far.\"";
    row.resting = "\"My hands keep shaking. It passes. Come back later.\"";
    row.next = "Tersia has another contract: the fourth floor";
    row.steps[0] = {QuestStepKind::Clear, 34, 15, "Cursed Wizards"};
    row.steps[1] = {QuestStepKind::Clear, 41, 20, "Death Cows"};
    row.steps[2] = {QuestStepKind::Return, 0, 1, "Return to Tersia"};
    row.stepCount = 3;
    row.afterAny = 1u << 7;
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    // A better weapon, two sockets, and a rune for it: the knight's one-handed, so a shield fits.
    row.paid[row.paidCount++] = {.item = "Sword14", .kin = knight, .sockets = 2,
                                 .firstOnly = true};  // Double Blade
    row.paid[row.paidCount++] = {.item = "Staff04", .kin = wizard, .sockets = 2,
                                 .firstOnly = true};  // Thunder Staff
    row.paid[row.paidCount++] = {.item = "Bow05", .kin = elf, .sockets = 2,
                                 .firstOnly = true};  // Tiger Bow
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = knight,
                                 .power = uint8_t(Power::Meteor), .firstOnly = true};
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = wizard,
                                 .power = uint8_t(Power::Inferno), .firstOnly = true};
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = elf, .power = uint8_t(Power::Glacier),
                                 .firstOnly = true};
    row.voice = "tersia_3";  // tools/voice.py, VOICES["tersia"]
    return row;
}

QuestRow tersiaFourth() {
    QuestRow row = tersiaLink("The Red Floor", 150000, 450000, 150000, 3, 1);
    row.offer[0] =
        "\"Nobody from my company came back from the fourth floor. Nobody.\"";
    row.offer[1] =
        "\"Devils walk there. Their lightning throws a grown man across the room. Plant your "
        "feet.\"";
    row.offer[2] =
        "\"Clear the Devils, and what is left of the Death Cows. Then tell me what you saw.\"";
    row.underway =
        "\"You are back already. Is it done? No. I can see in your face that it is not.\"";
    row.handIn[0] = "\"You came back. From the fourth floor. You came back.\"";
    row.handIn[1] =
        "\"Your pay. And my thanks, which is worth less, but you have it anyway.\"";
    row.resting = "\"I dream about the red floor. Give me until morning.\"";
    row.next = "Tersia has another contract: the fifth floor";
    row.steps[0] = {QuestStepKind::Clear, 41, 12, "Death Cows"};
    row.steps[1] = {QuestStepKind::Clear, 37, 10, "Devils"};
    row.steps[2] = {QuestStepKind::Return, 0, 1, "Return to Tersia"};
    row.stepCount = 3;
    row.afterAny = 1u << 8;
    // The pendant, every class's.
    row.paid[row.paidCount++] = {.item = "Necklace01", .sockets = 1,
                                 .firstOnly = true};  // Pendant of Lightning
    row.paid[row.paidCount++] = {.item = "Jewel22", .power = uint8_t(Power::Bloodwell),
                                 .firstOnly = true};
    row.voice = "tersia_4";  // tools/voice.py, VOICES["tersia"]
    return row;
}

QuestRow tersiaFifth() {
    QuestRow row = tersiaLink("Knights Who Do Not Sleep", 170000, 500000, 170000, 4, 1);
    row.offer[0] =
        "\"A Death Knight came down the stairs once, from the fifth floor. It wore armour like "
        "ours. Some of it was ours.\"";
    row.offer[1] =
        "\"I do not want to know whose. Just put them down. The Devils are up there with them, "
        "so mind the lightning.\"";
    row.underway = "\"They are still standing. I can feel it.\"";
    row.handIn[0] =
        "\"If you found a guild badge on any of them, keep it. I do not want to see it.\"";
    row.handIn[1] = "\"Here. Your pay. Two floors left.\"";
    row.resting = "\"Not now. I need to be alone for a while.\"";
    row.next = "Tersia has another contract: the sixth floor";
    row.steps[0] = {QuestStepKind::Clear, 37, 12, "Devils"};
    row.steps[1] = {QuestStepKind::Clear, 40, 12, "Death Knights"};
    row.steps[2] = {QuestStepKind::Return, 0, 1, "Return to Tersia"};
    row.stepCount = 3;
    row.afterAny = 1u << 9;
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    // A shield; the elf's bow takes both hands, so hers is a Ring of Ice.
    row.paid[row.paidCount++] = {.item = "Shield12", .kin = knight, .sockets = 1,
                                 .firstOnly = true};  // Serpent Shield
    row.paid[row.paidCount++] = {.item = "Shield15", .kin = wizard, .sockets = 1,
                                 .firstOnly = true};  // Legendary Shield
    row.paid[row.paidCount++] = {.item = "Ring01", .kin = elf, .sockets = 1, .firstOnly = true};
    row.paid[row.paidCount++] = {.item = "Jewel22", .power = uint8_t(Power::Undying),
                                 .firstOnly = true};
    row.voice = "tersia_5";  // tools/voice.py, VOICES["tersia"]
    return row;
}

QuestRow tersiaSixth() {
    QuestRow row = tersiaLink("Rolling Fire", 200000, 600000, 200000, 4, 2);
    row.offer[0] =
        "\"I know the sixth floor only from the shrine's old records. The Death Gorgons live "
        "there, and they roll fire along the ground.\"";
    row.offer[1] =
        "\"Do not stand where the fire is going to be. That is all the advice I have.\"";
    row.offer[2] =
        "\"Clear the Devils, the Knights and the Gorgons, and come back to me.\"";
    row.underway = "\"Something is still burning up there.\"";
    row.handIn[0] = "\"One floor left. One. I never thought I would say that.\"";
    row.handIn[1] = "\"Your pay. Sleep before the last one. One of us should.\"";
    row.resting = "\"The last floor waits. So do I.\"";
    row.next = "Tersia has the last contract: the Balrog";
    row.steps[0] = {QuestStepKind::Clear, 37, 8, "Devils"};
    row.steps[1] = {QuestStepKind::Clear, 40, 8, "Death Knights"};
    row.steps[2] = {QuestStepKind::Clear, 35, 8, "Death Gorgons"};
    row.steps[3] = {QuestStepKind::Return, 0, 1, "Return to Tersia"};
    row.stepCount = 4;
    row.afterAny = 1u << 10;
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    // No item: each class's legendary rune. The elf has only Frost Arrow, so a second.
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = knight,
                                 .power = uint8_t(Power::FireRing), .firstOnly = true};
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = wizard,
                                 .power = uint8_t(Power::Pyroblast), .firstOnly = true};
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = elf, .power = uint8_t(Power::Frost),
                                 .firstOnly = true};
    row.voice = "tersia_6";  // tools/voice.py, VOICES["tersia"]
    return row;
}

QuestRow tersiaBalrog() {
    QuestRow row = tersiaLink("The Scythe", 250000, 1000000, 250000, 5, 2);
    // Webzen's words for the Balrog, as near as she says them.
    row.offer[0] =
        "\"The last floor. Kundun left his Balrog there, a foul beast with a scythe.\"";
    row.offer[1] =
        "\"Few warriors in all of MU can stand against it. The guild sent twelve to close "
        "this contract. None of them reached it.\"";
    row.offer[2] =
        "\"Clear the Death Gorgons and the Death Knights around it, and bring it down. Then "
        "the contract is closed.\"";
    row.offer[3] =
        "\"If you do not come back, I will write your name in the book with the others.\"";
    row.underway = "\"It is still up there. I can hear the scythe on the stone.\"";
    row.handIn[0] =
        "\"The contract is closed. Eleven names in the book, and every one of them avenged.\"";
    row.handIn[1] =
        "\"Full pay, and the guild's bonus. Tonight I am going to sleep. All night.\"";
    row.resting =
        "\"Another Balrog will crawl up there. When it does, the contract opens again.\"";
    row.steps[0] = {QuestStepKind::Clear, 35, 25, "Death Gorgons"};
    row.steps[1] = {QuestStepKind::Clear, 40, 20, "Death Knights"};
    row.steps[2] = {QuestStepKind::Clear, 38, 1, "The Balrog"};
    row.steps[3] = {QuestStepKind::Return, 0, 1, "Return to Tersia"};
    row.stepCount = 4;
    row.afterAny = 1u << 11;
    row.paid[row.paidCount++] = {.item = "Jewel15"};  // Jewel of Chaos
    constexpr int8_t knight = int8_t(Kin::DarkKnight);
    constexpr int8_t wizard = int8_t(Kin::DarkWizard);
    constexpr int8_t elf = int8_t(Kin::FairyElf);
    // The top weapon, two sockets -- the knight's the Balrog's own Bill -- and a legendary rune.
    row.paid[row.paidCount++] = {.item = "Spear10", .kin = knight, .sockets = 2,
                                 .firstOnly = true};  // Bill of Balrog
    row.paid[row.paidCount++] = {.item = "Staff06", .kin = wizard, .sockets = 2,
                                 .firstOnly = true};  // Legendary Staff
    row.paid[row.paidCount++] = {.item = "CrossBow06", .kin = elf, .sockets = 2,
                                 .firstOnly = true};  // Bluewing Crossbow
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = knight,
                                 .power = uint8_t(Power::Fireburst), .firstOnly = true};
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = wizard, .power = uint8_t(Power::Echo),
                                 .firstOnly = true};
    row.paid[row.paidCount++] = {.item = "Jewel22", .kin = elf, .power = uint8_t(Power::Frost),
                                 .firstOnly = true};
    row.voice = "tersia_7";  // tools/voice.py, VOICES["tersia"]
    return row;
}

const QuestRow kTable[kQuests] = {marlon(),      peia(),        devin(),      catacombs(),
                                  halls(),       pit(),         tersiaDoor(),  tersiaSecond(),
                                  tersiaThird(),  tersiaFourth(), tersiaFifth(), tersiaSixth(),
                                  tersiaBalrog()};

}  // namespace

const QuestRow& questAt(int index) { return kTable[index]; }

int questOf(int32_t giver) {
    for (int i = 0; i < kQuests; ++i) {
        if (kTable[i].giver == giver) return i;
    }
    return -1;
}

}  // namespace mu::sim
