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

namespace mu::sim {

// How many quests the table holds. A save carries one progress a quest by this index.
inline constexpr int kQuests = 2;
inline constexpr int kQuestSteps = 9;
inline constexpr int kQuestChoices = 7;
inline constexpr int kQuestPaid = 6;

enum class QuestStepKind : uint8_t {
    // Kill `count` of breed `target` (MU's monster number); a count of 0 is the breed's whole
    // population on the quest's map, which is what "clear" means.
    Clear,
    // Back to the giver, once every other step is done.
    Return,
};

struct QuestStepRow {
    QuestStepKind kind = QuestStepKind::Clear;
    int32_t target = 0;
    int32_t count = 0;
    const char* line = "";  // the tracker's words: the breed's plural, or "Return to Marlon"
};

// A thing paid: an item by its file name (Tables::itemNamed), how many, at what plus, and to
// whom -- on the user's word of 2026-09-29 the reward is the class's own, the Dark Knight's
// first and the others' as their runes are made.
struct QuestItem {
    const char* item = nullptr;
    int32_t count = 1;
    int32_t plus = 0;
    int8_t kin = -1;          // the class it is paid to (sim::Kin), -1 every class
    bool luck = false;
    uint8_t sockets = 0;
    uint8_t power = 0;        // a Rune of Creation's (sim::Power)
    bool firstOnly = false;   // paid on the first completion and never again
};

// Whether this thing is paid to this class; `first` is questFirst's, the first clear of one born
// in the giver's town.
inline bool questPays(const QuestItem& what, int kin, bool first) {
    return what.item && (what.kin < 0 || what.kin == kin) && (!what.firstOnly || first);
}

struct QuestRow {
    int32_t giver = 0;          // the giver's MU NPC number (Tables::folk)
    const char* giverName = "";
    const char* place = "";     // the giver's town, for the dialog's "Marlon of Lorencia"
    const char* title = "";
    // What he says: offering it, while it is under way, taking it back done, and while it is
    // not yet his to give again. Each a paragraph; unused ones are null.
    const char* offer[5] = {};
    const char* underway = "";
    const char* handIn[2] = {};
    const char* resting = "";
    // His voice: assets/voice/<voice>/<voice>_offer.wav, _underway, _handin and _resting, one
    // file a page, each the page's words read whole. Empty for a giver who is not voiced.
    const char* voice = "";
    QuestStepRow steps[kQuestSteps];
    int stepCount = 0;
    // Seconds between a hand-in and the offer standing again; 0 for once only.
    int64_t repeatSeconds = 0;
    int64_t experience = 0;
    int64_t zen = 0;
    // Every one of these his class is paid (questPays), all of them.
    // Who was born in the giver's town (1 << sim::Kin, each class its starting map). One born
    // elsewhere may take the quest only if `strangers`, and is then never paid the first clear's
    // things or experience -- the user's rule of 2026-09-29: an elf may clear Lorencia for its
    // jewels, Zen and experience, but the weapon and the rune are Marlon's to his own, and Noria's
    // quest is the elves' alone. `stranger` is what the giver says to one he will not serve.
    uint8_t natives = 0;
    bool strangers = false;
    const char* stranger = "";
    QuestItem paid[kQuestPaid];
    // Paid in place of `experience` on a native's first clear.
    int64_t firstExperience = 0;
    int paidCount = 0;
    // One of these, the player's choice, from those his class may use. The table lists every
    // class's candidates; the dialog shows his. None, when the whole reward is `paid`.
    QuestItem choices[kQuestChoices];
    int choiceCount = 0;
};

const QuestRow& questAt(int index);
// The quest a giver hands out, by NPC number, or -1. One a giver.
int questOf(int32_t giver);
inline bool questNative(const QuestRow& row, int kin) { return (row.natives >> kin) & 1u; }
// Whether a class may take the quest at all.
inline bool questOpen(const QuestRow& row, int kin) { return row.strangers || questNative(row, kin); }
// Whether the clear after `completions` is the one that pays the first clear's rewards.
inline bool questFirst(const QuestRow& row, int kin, uint32_t completions) {
    return completions == 0 && questNative(row, kin);
}
inline int64_t questExperience(const QuestRow& row, bool first) {
    return first && row.firstExperience > 0 ? row.firstExperience : row.experience;
}


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

}  // namespace mu::sim
