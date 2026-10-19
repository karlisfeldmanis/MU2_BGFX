// **Auto-play**: a repeatable quest played for him, start to finish -- the user, 2026-10-17:
// "if quest is already done it become repeatable and user can press button to auto-play that
// quest", and "if user is at least one time completed the quest auto-play will be usable".
//
// A hand, not a brain: it raises the same orders a player's clicks raise (Play::orderHero,
// fight, walkTo, travel, acceptQuest, completeQuest) and decides nothing the realm decides. It
// walks to the giver and takes the quest, hunts each breed where it lives (the weakest first, as
// the steps are listed), travels between maps by the Move list or a gate, walks back and hands
// it in, and stops. A click of his own on the world, or Stop on the quest log, gives the hero
// back. tools/bot/brain.h does the same for the bots, server side; it is not linked into the
// game, and this is the part of it a player's own character needs.
//
// The quest it plays is the Application's (app::Context::autoQuest), as a map change raises a
// new mode, a new Play and a new one of these; nothing it keeps is needed past a frame.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace mu::sim {
class Realm;
}

namespace mu::game {

class Play;

// Where the quests' breeds live on every map, read once from the cooked tables: the quest log
// groups by it, Auto-play travels by it, and a whole-population step's goal on another map is
// counted by it.
namespace quest_world {
// Reads every map's tables, the first time only.
void open(const std::string& assetDir);
// The map a breed (MU's monster number) is hunted on: `here` when it has a nest here, else the
// first map that has one; -1 for none.
int huntMap(int32_t breed, int here);
// The map a quest is played on, for the log's grouping: its first counted step's, else its
// giver's.
int questMap(int quest, int here);
// A step's goal, wherever its breed lives: Realm::questGoal counts a whole population (count 0)
// on the map the realm stands in only.
int goal(const sim::Realm& realm, int quest, int step);
// As the arrival and the minimap say it: "Lorencia", "Lost Tower".
std::string mapName(int map);
}  // namespace quest_world

// Whether Auto-play may play this quest now: repeatable, cleared at least once, every step one
// it can hunt (a thing to find is not), and not resting.
bool autoPlayable(const sim::Realm& realm, int quest);
// And whether it ever may, resting or not: the log greys its button while the quest rests.
bool autoPlayableEver(const sim::Realm& realm, int quest);

class QuestAuto {
public:
    // One frame. `quest` is the one playing, -1 for none; set to -1 here when it is done or can
    // go no further. Returns a line for the player when it stops, else empty. `bar` is his skill
    // bar (the four keys and the right button's slot, MU's skill numbers, 0 for empty): the
    // defensive auras on it are kept up while he fights.
    std::string update(Play& play, int* quest, double seconds, const int32_t* bar = nullptr,
                       int barCount = 0);
    // Forget what this map's hand was doing: a new quest, or Stop.
    void reset();

private:
    std::string goTo(Play& play, int map, const std::function<std::string(const std::string&)>& stop);
    void visit(Play& play, int folk, bool hand);
    // False when none of the breed on this map can be got to from where he stands.
    bool hunt(Play& play, int32_t breed);
    bool guard(Play& play);
    bool loot(Play& play);
    bool resupply(Play& play, const std::function<std::string(const std::string&)>& stop,
                  std::string* said);
    int32_t striker(Play& play) const;

    double since_ = 0.0;
    int quest_ = -1;
    uint32_t target_ = 0;
    int nest_ = -1;
    double askedAt_ = -1e9;     // the last order of a walk or a talk, in seconds of this hand
    double travelAt_ = -1e9;    // the last trip asked
    double clock_ = 0.0;
    int tries_ = 0;             // answers sent to a giver without the state moving
    int lastState_ = -1;
    bool handedIn_ = false;
    int walkedColumn_ = -1, walkedRow_ = -1;
    int still_ = 0;             // walks asked that never got him walking
    int32_t hunting_ = -1;      // the breed last hunted, for the log
    int32_t bar_[8] = {};
    int barCount_ = 0;
    std::map<int32_t, double> guardTried_;  // a guard asked, by skill: not again before this
    double boughtAt_ = -1e9;    // the last quiver asked for at a counter
    bool stocking_ = false;     // an archer out of shots, on her way to a counter and buying
    std::map<uint32_t, double> lootTried_;  // a pickup asked, by lying id: given up after this
};

}  // namespace mu::game
