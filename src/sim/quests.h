// The quests: who gives them, what they ask, what they pay, and the words the dialog says.
//
// **All of it is invention.** MU 0.75 has no quests: Marlon is not in its Lorencia and the
// client's quest window came with later versions (MuMain's quest dialogue is flagged for
// MONSTER_MARLON alone, ZzzInterface.cpp). The shape is the proposal the user asked for on
// 2026-09-28 (claude.ai/artifact/8nQVewJ3f2VkKd74T2ktn2): a giver offers a quest in a dialog,
// its steps are counted by the realm, the tracker shows them, and the hand-in pays a fixed
// reward plus one item the player chooses, from those his class can use.
//
// And the user's rule for Lorencia, the same day: **one quest, repeatable every twelve hours,
// whose goal is to clear Lorencia** -- every breed, and a lot of each. So its steps are one
// Clear a breed, each counted at that breed's whole population on the map (read from the nests
// when the realm is raised, so a change of spawns changes the quest with it), all at once and in
// any order, then the return. Handed in, it comes back twelve real hours later.
//
// The words live beside the rules because the rules name them -- a step's line is what the
// tracker prints for it -- and nothing here draws.
#pragma once

#include <cstdint>

namespace mu::sim {

// How many quests the table holds. A save carries one progress a quest by this index.
inline constexpr int kQuests = 1;
inline constexpr int kQuestSteps = 9;
inline constexpr int kQuestChoices = 7;

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

// A thing paid: an item by its file name (Tables::itemNamed), how many, at what plus.
struct QuestItem {
    const char* item = nullptr;
    int32_t count = 1;
    int32_t plus = 0;
};

struct QuestRow {
    int32_t giver = 0;          // the giver's MU NPC number (Tables::folk)
    const char* giverName = "";
    const char* title = "";
    // What he says: offering it, while it is under way, taking it back done, and while it is
    // not yet his to give again. Each a paragraph; unused ones are null.
    const char* offer[4] = {};
    const char* underway = "";
    const char* handIn[2] = {};
    const char* resting = "";
    QuestStepRow steps[kQuestSteps];
    int stepCount = 0;
    // Seconds between a hand-in and the offer standing again; 0 for once only.
    int64_t repeatSeconds = 0;
    int64_t experience = 0;
    int64_t zen = 0;
    QuestItem always;
    // One of these, the player's choice, from those his class may use. The table lists every
    // class's candidates; the dialog shows his.
    QuestItem choices[kQuestChoices];
    int choiceCount = 0;
};

const QuestRow& questAt(int index);
// The quest a giver hands out, by NPC number, or -1. One a giver.
int questOf(int32_t giver);

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
