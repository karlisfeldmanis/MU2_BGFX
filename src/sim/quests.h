// The quests: who gives them, what they ask, what they pay, and the words the dialog says.
//
// **All of it is invention.** MU 0.75 has no quests: Marlon is not in its Lorencia and the
// client's quest window came with later versions (MuMain's quest dialogue is flagged for
// MONSTER_MARLON alone, ZzzInterface.cpp). The shape is the proposal the user asked for on
// 2026-09-28 (claude.ai/artifact/8nQVewJ3f2VkKd74T2ktn2): a giver offers a quest in a dialog,
// its steps are counted by the realm, the tracker shows them, and the hand-in pays a fixed
// reward, some of it his class's own, plus any item the player chooses from those his class can
// use.
//
// And the user's rule for Lorencia, the same day: **one quest, repeatable every twelve hours,
// whose goal is to clear Lorencia** -- every breed, and a lot of each. So its steps are one
// Clear a breed, all at once and in any order, then the return. A step's count was each breed's
// whole population at first. Since 2026-09-29 Marlon and Peia share one fixed ladder, so the two
// maps ask the same hunt (quests.cpp). Handed in, it comes back twelve real hours later.
//
// The words live beside the rules because the rules name them -- a step's line is what the
// tracker prints for it -- and nothing here draws.
#pragma once

#include <cstdint>

#include "sim/rules.h"

namespace mu::sim {

// How many quests the table holds. A save carries one progress a quest by this index.
inline constexpr int kQuests = 20;
inline constexpr int kQuestSteps = 9;
inline constexpr int kQuestChoices = 7;
inline constexpr int kQuestPaid = 12;

enum class QuestStepKind : uint8_t {
    // Kill `count` of breed `target` (MU's monster number); a count of 0 is the breed's whole
    // population on the quest's map, which is what "clear" means.
    Clear,
    // Carry one of `item` back: picked up, it counts, and the hand-in takes it from the bag.
    // Sevina's treasures, which fall only while the quest stands (Realm::treasure).
    Find,
    // Back to the giver, once every other step is done.
    Return,
};

// Whether a step has a count the tracker and the dialog show: every one but the return.
inline bool questCounted(QuestStepKind kind) { return kind != QuestStepKind::Return; }

struct QuestStepRow {
    QuestStepKind kind = QuestStepKind::Clear;
    int32_t target = 0;
    int32_t count = 0;
    const char* line = "";  // the tracker's words: the breed's plural, or "Return to Marlon"
    const char* item = nullptr;  // a Find's item, by file name (Tables::itemNamed)
};

// A thing paid: an item by its file name (Tables::itemNamed), how many, at what plus, and to
// whom -- on the user's word of 2026-09-29 the reward is the class's own, the Dark Knight's
// first and the others' as their runes are made.
struct QuestItem {
    const char* item = nullptr;
    int32_t count = 1;
    int32_t plus = 0;
    int8_t kin = -1;          // the class it is paid to (sim::Kin), -1 every class
    uint8_t sockets = 0;
    uint8_t power = 0;        // a Rune of Creation's (sim::Power)
    bool firstOnly = false;   // paid on the first completion and never again
    // A powered ring's or pendant's further powers (sim::Affix), past its signature.
    uint8_t affixes[3] = {};
};

// Every weapon, piece of armour and shield a quest pays comes with luck and the additional
// option at this level, +4 (the user, 2026-10-02: "all armors and weapon we give from quests is
// with +luck and +opt"); a ring or a pendant too since they take luck (2026-10-03), its option
// +1% life regeneration; whatever else takesOptions refuses, a jewel, goes without. Ours.
constexpr int kQuestOption = 1;

// Whether this thing is paid to this class; `first` is questFirst's, the first clear of one born
// in the giver's town.
inline bool questPays(const QuestItem& what, int kin, bool first) {
    return what.item && (what.kin < 0 || what.kin == kin) && (!what.firstOnly || first);
}

struct QuestRow {
    int32_t giver = 0;          // the giver's MU NPC number (Tables::folk)
    const char* giverName = "";
    const char* place = "";     // the giver's town, for the dialog's "Marlon of Lorencia"
    // Who takes it back, when that is not the giver: Lirien in Atlans for Peia's 'The Drowned
    // Song' (the user, 2026-10-05: 'first quest is just to go to atlans and meet other elf').
    // Her number, name and map; 0 for the giver himself. Talking to her settles it (its steps
    // all counted, Realm::questMet) and her window opens on the hand-in, in her words.
    int32_t receiver = 0;
    const char* receiverName = "";
    const char* receiverPlace = "";
    // Her voice for the hand-in page, as `voice` is the giver's: assets/voice/<it>/<it>_handin.wav.
    const char* receiverVoice = "";
    const char* title = "";
    // What he says: offering it, while it is under way, taking it back done, and while it is
    // not yet his to give again. Each a paragraph; unused ones are null.
    const char* offer[5] = {};
    const char* underway = "";
    const char* handIn[2] = {};
    const char* resting = "";
    // Where to go once it is done: the tip under the "Quest complete" banner and on the resting
    // tracker, the written reminder of what the giver says at the end of his hand-in. Empty for
    // none.
    const char* next = "";
    // His voice: assets/voice/<voice>/<voice>_offer.wav, _underway, _handin and _resting, one
    // file a page, each the page's words read whole. Empty for a giver who is not voiced.
    const char* voice = "";
    QuestStepRow steps[kQuestSteps];
    int stepCount = 0;
    // Seconds between a hand-in and the offer standing again; 0 for once only.
    int64_t repeatSeconds = 0;
    // Offered only once one of these quests has been handed in (bit i, quest i); 0 for none.
    // Until then the giver wears a grey "!" and says he is not ready for him (Realm::questLocked).
    uint32_t afterAny = 0;
    // The level it asks; below it the giver says he is not ready (Realm::questLocked). 0 none.
    int32_t minLevel = 0;
    // Handed in, it makes him his class's second: Blade Knight, Soul Master, Muse Elf
    // (Realm::promoted). Sevina's treasures; `boon` is what the dialog's rewards say of it.
    bool promotes = false;
    const char* boon = "";
    // Who was born in the giver's town (1 << sim::Kin, each class its starting map). One born
    // elsewhere may take the quest only if `strangers`, and is then never paid the first clear's
    // things or experience -- the user's rule of 2026-09-29: an elf may clear Lorencia for its
    // jewels, Zen and experience, but the weapon and the rune are Marlon's to his own; Peia's the
    // same for a knight or a wizard (2026-09-30). `stranger` is what the giver says to one he
    // will not serve.
    uint8_t natives = 0;
    bool strangers = false;
    const char* stranger = "";
    int64_t experience = 0;
    // Paid in place of `experience` on a native's first clear.
    int64_t firstExperience = 0;
    int64_t zen = 0;
    // Every one of these his class is paid (questPays), all of them.
    QuestItem paid[kQuestPaid];
    int paidCount = 0;
    // One of these, the player's choice, from those his class may use. The table lists every
    // class's candidates; the dialog shows his. None, when the whole reward is `paid`.
    QuestItem choices[kQuestChoices];
    int choiceCount = 0;
};

// The Magic Gladiator is native wherever the knight or the wizard is, on a quest open to
// strangers: born in Lorencia, Marlon's is his (the user, 2026-10-07: 'mg start in lorencia, that
// means he will do lorencia quest'). Not a class change's treasure, whose row is one class's alone.
inline bool questNative(const QuestRow& row, int kin) {
    if ((row.natives >> kin) & 1u) return true;
    constexpr uint8_t kMeleeOrMagic =
        uint8_t((1u << int(Kin::DarkKnight)) | (1u << int(Kin::DarkWizard)));
    return kin == int(Kin::MagicGladiator) && row.strangers && (row.natives & kMeleeOrMagic) != 0;
}

// **The Magic Gladiator's path** (the user, 2026-10-07: 'MG can choose between melee & magic'):
// where a giver pays the knight one thing and the wizard another, he chooses whose -- Melee the
// knight's, Magic the wizard's -- and is paid that class's things. Ours.
enum class QuestPath : uint8_t { Melee = 0, Magic = 1 };
// Whether this hand-in pays the knight and the wizard things of their own, so he has to choose.
inline bool questPathPays(const QuestRow& row, QuestPath path, bool first) {
    const int kin = int(path == QuestPath::Magic ? Kin::DarkWizard : Kin::DarkKnight);
    for (int i = 0; i < row.paidCount; ++i) {
        if (row.paid[i].kin == kin && questPays(row.paid[i], kin, first)) return true;
    }
    return false;
}
inline bool questOffersPaths(const QuestRow& row, bool first) {
    return questPathPays(row, QuestPath::Melee, first) && questPathPays(row, QuestPath::Magic, first);
}
// The class whose things this hero is paid: his own, or for the Magic Gladiator his path's --
// whichever one the giver pays, when he pays only one of the two.
inline int questPaidKin(const QuestRow& row, int kin, bool first, QuestPath path) {
    if (kin != int(Kin::MagicGladiator)) return kin;
    if (!questOffersPaths(row, first)) {
        path = questPathPays(row, QuestPath::Magic, first) ? QuestPath::Magic : QuestPath::Melee;
    }
    return int(path == QuestPath::Magic ? Kin::DarkWizard : Kin::DarkKnight);
}
// Who takes it back: its receiver, or the giver himself.
inline int32_t questReceiver(const QuestRow& row) { return row.receiver ? row.receiver : row.giver; }
inline const char* questReceiverName(const QuestRow& row) {
    return row.receiver ? row.receiverName : row.giverName;
}
// Whether someone other than its giver takes it back.
inline bool questElsewhere(const QuestRow& row) { return row.receiver && row.receiver != row.giver; }
// Whether a class may take the quest at all.
inline bool questOpen(const QuestRow& row, int kin) { return row.strangers || questNative(row, kin); }
// Whether the clear after `completions` is the one that pays the first clear's rewards.
inline bool questFirst(const QuestRow& row, int kin, uint32_t completions) {
    return completions == 0 && questNative(row, kin);
}
inline int64_t questExperience(const QuestRow& row, bool first) {
    return first && row.firstExperience > 0 ? row.firstExperience : row.experience;
}

const QuestRow& questAt(int index);
// A demo quest of Peia's, given by nobody until --quest-demo hands it to her, so her window
// opens on its list (QuestDialog::kList): the user, 2026-10-04, 'show me demo for noria quest
// giver which has multiple quests'. Its fellow at 18 became 'The Drowned Song', hers for good
// (2026-10-05).
inline constexpr int kDemoQuests[1] = {17};
// Peia's 'The Drowned Song', handed in to Lirien in Atlans.
inline constexpr int kDrownedSong = 18;
// Lirien, the elf envoy in Atlans's safe basin: our own NPC number, no MU NPC's (tools/cook.py's
// Atlans folk, source/npc/ElfEnvoy.json).
inline constexpr int32_t kLirienNumber = 700;
// Lirien's own: Atlans cleared, the Hydra last, offered once 'The Drowned Song' is handed in.
inline constexpr int kDrownedHalls = 19;
void enableQuestDemo();
// The quest a giver hands out, by NPC number, or -1. One a giver.
int questOf(int32_t giver);
// Whether this NPC takes back a quest someone else gave (QuestRow::receiver): Lirien.
bool questReceives(int32_t number);


enum class QuestState : uint8_t {
    Untaken = 0,
    Active = 1,
    Ready = 2,    // every Clear done: back to the giver
    Resting = 3,  // handed in; offered again at `availableAt`
};

// Where a hero stands on one quest. Saved whole.
struct QuestProgress {
    QuestState state = QuestState::Untaken;
    uint16_t counts[kQuestSteps] = {};  // each Clear's kills so far
    // Unix seconds when a Resting quest is offered again. The realm has no clock of its own
    // for this: the game hands it the wall clock (Realm::setWallClock), because twelve hours
    // is the player's day and not the realm's ticks.
    int64_t availableAt = 0;
    uint32_t completions = 0;
};

// Sevina's class change (docs/class-change-quest.md): the trial, a hunt in the Lost Tower's
// seventh floor and Atlans from level 200, then each class's treasure, found on the same ground,
// whose hand-in makes him the second class. By sim::Kin, the treasure quest's index.
inline constexpr int kSevinaTrial = 13;
inline constexpr int kTreasureQuests[3] = {15, 16, 14};  // wizard, elf, knight
// Whether a hero of this class with these quests is his class's second.
inline bool promoted(const QuestProgress* quests, int kin) {
    return kin >= 0 && kin < 3 && quests[kTreasureQuests[kin]].completions > 0;
}
// The class's name, first or second: "Dark Knight", or "Blade Knight" once promoted.
const char* className(int kin, bool second);

}  // namespace mu::sim
