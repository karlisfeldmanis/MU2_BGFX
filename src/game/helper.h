// **The Helper**: MU Helper's auto-hunt, made simple and free -- the user, 2026-10-20: "lets
// design proposal for simple helper to our need", the window A of
// claude.ai/artifact/DaBoq8ccx79xmoqc2j5YuX, and "helper has to be free".
//
// Started where he stands (H, the minimap's H, or Start on its window), it fights what comes
// within its range of that spot, throws his bar's skills as each one's use says, keeps his
// auras and his summon up, heals her when she is hurt, picks up what its window ticks (and Zen,
// always), and walks back to the spot when nothing is left in reach. The realm drinks for him
// under his auto-drink lines, as it always does; the window sets those lines too.
//
// A hand, not a brain, as Auto-play is (game/quest_auto.h): it raises the orders a player's own
// clicks raise and decides nothing the realm decides. It mends his worn gear as it goes. It
// stops when he dies, when he stands in a safe zone, when the healing potions he set out with or his shots run out,
// on another map, and when the player clicks the world himself. A full bag does not stop it: it
// goes on taking Zen and what tops up a stack (Helper::filled). Ours: WebZen's helper charged Zen every five
// minutes and ran on the server; this one is the client's and costs nothing.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "game/quest_auto.h"

namespace mu::sim {
struct Held;
struct SkillRow;
}

namespace mu::game {

class Play;

// What a skill on the bar is, to the Helper: which uses its window offers for it.
enum class HelperKind : uint8_t {
    Right,   // the right button's quick slot: every target, always
    Single,  // a blow or a spell at one body
    Area,    // a ring, an arc, a line, a fan or a beam: a crowd
    Aura,    // a boon or a might on himself
    Heal,    // a mend
    Summon,  // a summon
    Moves,   // a blink, a charge (Nova's leap), a mount: never, it would take him off his ground
    Held,    // a channel, held down: never
};
// How the Helper uses one. Each kind offers a few of these (helperUses), the first its default.
enum class HelperUse : uint8_t { Off, Ready, Near2, Near3, KeepUp, WhenHurt, KeepOut, Always, Never };

HelperKind helperKind(const sim::SkillRow& row, bool rightSlot);
// The uses `kind` offers, its default first.
const std::vector<HelperUse>& helperUses(HelperKind kind);
// As the window words them: "when ready", "3+ near", "keep up".
const char* helperUseName(HelperUse use);
// What the kind is, under the skill's name: "hits a crowd", "moves him · never used".
const char* helperKindName(HelperKind kind);

// What the window sets, kept beside his save as Name.helper (helperPath).
struct HelperSettings {
    // The ground it hunts, in tiles from where it started: Near, Mid, Far.
    static constexpr int kRanges[3] = {4, 7, 11};
    int range = 1;
    // Walk back to where it started when nothing is left in reach.
    bool back = true;
    // Each skill's use, by MU's skill number; one not here takes its kind's default.
    std::map<int32_t, HelperUse> uses;
    // What it picks up besides Zen, a bit each (kPick...). Pets are the Angel, the Imp and the
    // Horns, which MU flags as jewels; their own tick, off at first (Discord, 2026-10-21: "have
    // the Helper include the option to include/exclude them from auto-pickup? They blow up my
    // inventory really quick"). Boxes are the Firecracker, the Box of Luck and the Box of Kundun,
    // on at first as they always were (the user, 2026-10-21); their own tick since Crylos asked
    // (Discord, 2026-10-23: "make picking up Firecrackers optional? its also blowing up the
    // inventory"). Kept as its own line in the file, so one written before it reads as on.
    static constexpr uint8_t kPickJewels = 1, kPickRunes = 2, kPickLegendary = 4, kPickExcellent = 8,
                             kPickSockets = 16, kPickAll = 32, kPickPets = 64, kPickBoxes = 128;
    uint8_t picks = kPickJewels | kPickRunes | kPickLegendary | kPickExcellent | kPickBoxes;
    // **Pick up by name** (Discord, 2026-10-23: "type in the names of the specific items you want
    // to be collected", as MU Helper's own list): a thing whose name holds one of these, any case,
    // is picked whatever the ticks say -- "chaos" takes the Jewel of Chaos, "kundun" every Box of
    // Kundun even with Boxes off. Twelve, as MU's own list (WSclient.h's ExtraItems[12][15]), each
    // a little longer than its 15 so "Lower Refining Stone" fits. Kept as a `name` line each.
    static constexpr size_t kNameMost = 12, kNameLong = 20;
    std::vector<std::string> names;
    // The window has been up once: the first H opens it, every H after starts and stops.
    bool seen = false;
    // **Follow my quest** (the user, 2026-10-21: "helper can follow active quest, similiar like
    // auto-play"): instead of hunting round a spot, it plays the quest the tracker shows, else
    // the first taken one hunted on this map, else the first taken, as Auto-play does -- the
    // hunt, the travel, the hand-in -- and stops when that one is handed in. Its
    // other ticks still hold.
    bool quest = false;
    // While it helps (the user, the same day): a party's invite answered for him -- asked of
    // him as ever, joined, or declined; the aids on his bar (Heal, Greater Defense, Greater
    // Damage) kept on his party in reach as on himself; a trade's ask declined.
    static constexpr uint8_t kInviteAsk = 0, kInviteJoin = 1, kInviteDecline = 2;
    uint8_t invites = kInviteAsk;
    bool aidParty = true;
    bool noTrades = true;
    // **Repair my gear** (Discord, 2026-10-21: "we need auto repair in Helper!"): MU Helper's
    // bRepairItem (MuHelper.cpp's RepairEquipments). Each worn piece at half its durability or
    // under is mended by his own hand where he stands, at its self-repair price (two and a half
    // times Hanzo's), when the Zen is there for it. From kSelfRepairLevel, as the bag's hammer.
    bool repair = true;

    int tiles() const { return kRanges[range < 0 ? 0 : range > 2 ? 2 : range]; }
    HelperUse use(int32_t skill, HelperKind kind) const;
    // Whether `label` holds one of the names, any case.
    bool named(const std::string& label) const;
    // `typed` trimmed and added to the names; false when it is empty, already there, or the list
    // is full.
    bool addName(const std::string& typed);
};

std::string helperPath(const std::string& savePath);
// False when there is none or it does not read: `out` is left as it was, the defaults.
bool loadHelper(const std::string& savePath, HelperSettings& out);
bool writeHelper(const std::string& savePath, const HelperSettings& settings);

// **The pickup log** (Discord, 2026-10-23, Crylos: "I'd love to have a helper pick-up log, so i
// know what i picked up exactly with quantities while afking. bonus if log includes timestamp and
// remembers the last active helper, so i can access it even if i DC"; the user's design,
// claude.ai/artifact/BbDLeKxZrc15tAyTdF115X): what the last run took into his bag and purse, each
// thing with its count and the time, Zen summed. Kept beside his save as Name.helperlog, a line
// written as each comes, so a run cut off -- a disconnect, the game closed -- is there when he is
// played again; the next start begins it afresh.
struct HelperLog {
    struct Entry {
        int64_t at = 0;     // the clock's seconds (std::time)
        std::string label;  // as the card names it, "Jewel of Bless", "Kris +5"
        uint8_t tone = 0;   // its name's colour, a tip::Tone; nothing for Zen
        int count = 0;      // pieces, for a thing
        int64_t zen = 0;    // for Zen: a run of it between two things, summed
        bool isZen() const { return label.empty(); }
    };
    int64_t began = 0, ended = 0;  // 0: none kept, and not yet ended
    std::string place;             // the map it ran on, the last one if a quest took it on
    std::string why;               // how it ended, "You died"; empty while it runs or cut off
    double seconds = 0.0;
    int kills = 0;
    int64_t zen = 0;
    std::vector<Entry> entries;  // oldest first
    bool kept() const { return began != 0; }
    bool ending() const { return ended != 0; }
};
std::string helperLogPath(const std::string& savePath);
// The last run's log, or an empty one.
HelperLog loadHelperLog(const std::string& savePath);

class Helper {
public:
    // What the minimap's hint and the window's foot say while it helps.
    struct Stats {
        double seconds = 0.0;
        int kills = 0;
        int picked = 0;
        int quests = 0;  // handed in while following
    };
    // Begun where he stands now; `carried`, the stats of the Helper on the map he came from (a
    // followed quest's travel raises a new mode, app::Context::helperFollows).
    // A carried start goes on with the log it was keeping; a new one begins it again.
    void start(const Play& play, const Stats* carried = nullptr);
    // `why` goes in the log as how it ended.
    void stop(const std::string& why = "You stopped it");
    bool on() const { return on_; }
    const Stats& stats() const { return stats_; }
    // Where its log is kept (beside the save; none for a run with no save) and the map's name, once
    // as the mode opens: the last run's log is read back.
    void keepLog(const std::string& savePath, const std::string& place);
    const HelperLog& log() const { return log_; }
    // The last run's log let go, its file too; not while it runs.
    void clearLog();
    // Something come to him off the ground or with a kill (Play::takePicked): logged while it helps.
    void picked(const Play& play, const sim::Held& what, int64_t zen);
    // One frame. `bar` is his skill bar (the five keys, then the right button's slot, MU's skill
    // numbers, 0 for empty). Returns a line for the player when it stops by itself, else empty.
    std::string update(Play& play, const HelperSettings& settings, double seconds, const int32_t* bar,
                       int barCount, int shown = -1);
    // The quest it follows, -1 for none.
    int following() const { return on_ ? following_ : -1; }

private:
    std::string halt(const std::string& why);
    // Whether the bag has just filled (`full` now, not the beat before): the line is said once.
    bool filled(bool full);
    // The quest it follows handed in: counted, and the Helper stops.
    std::string finished();
    // Following a quest (HelperSettings::quest): `shown`, the one the tracker shows.
    std::string follow(Play& play, const HelperSettings& settings, double seconds, int shown);
    // The aids on his bar on one of his party in reach, the most hurt healed first.
    bool aid(Play& play, const HelperSettings& settings);
    bool support(Play& play, const HelperSettings& settings);
    bool loot(Play& play, const HelperSettings& settings, bool attacked);
    void mend(Play& play, const HelperSettings& settings);
    void keys(Play& play, const HelperSettings& settings, uint32_t target);
    int32_t striker(Play& play, const HelperSettings& settings) const;

    bool on_ = false;
    bool potions_ = false;  // he carried a healing potion when it started
    bool full_ = false;     // the bag was full at the last beat
    Stats stats_;
    int anchorColumn_ = 0, anchorRow_ = 0;
    double since_ = 0.0, clock_ = 0.0;
    double askedAt_ = -1e9;
    int walkedColumn_ = -1, walkedRow_ = -1;
    uint32_t target_ = 0;
    uint32_t picking_ = 0;
    int32_t bar_[8] = {};
    int barCount_ = 0;
    std::map<int32_t, double> tried_;       // a skill asked: not again before this
    // The key last asked, while it waits on his swing: no other is asked over it (Helper::keys).
    int32_t pendingSkill_ = 0;
    uint32_t pendingAt_ = 0;
    int64_t pendingSwing_ = 0;
    double pendingUntil_ = 0.0;
    std::map<uint32_t, double> lootTried_;  // a pickup asked: given up after this
    std::map<uint64_t, double> aidTried_;   // an aid asked on one of his party: not again before this
    std::map<int, double> mendTried_;       // a worn slot's repair asked: not again before this
    QuestAuto quester_;                     // Auto-play's hand, while it follows a quest
    int following_ = -1;                    // the quest it follows, -1 for none yet
    HelperLog log_;
    std::string logPath_, place_;
    // One line onto the log's file, made if `fresh`.
    void logLine(const std::string& text, bool fresh = false);
};

}  // namespace mu::game
