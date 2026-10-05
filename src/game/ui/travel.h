// The travel list: Tab shows it and shuts it, a column of maps down the left edge of the screen,
// and a click on one takes him there (sim/travel.h has the rows and what they ask).
//
// The proposal of 2026-09-30 (claude.ai/artifact/CeRcbNmeWq31yAA2WUL15X), as the user tuned it:
// no window and no header, the screen darkening from the left edge behind a column of map
// buttons all one height; each a thin iron rim, the name and the monster levels on its first
// line and on its second the marks for a quest giver and merchants, or a dungeon's floors. The
// map he is on is gold. A map he cannot use -- not opened, or out of his reach -- is its name
// alone, dimmed, under a rule. Nothing is hidden behind a hover.
//
// Ours: 0.75 has no Move window at all, only the `/move` command.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"
#include "sim/travel.h"

namespace mu::game {

class Play;

class Travel {
public:
    // Reads what each map's row says -- its monsters' levels, whether a quest giver and a
    // merchant stand in it -- from each world's cooked rules, once.
    void open(const gfx::Interface& interface, const std::string& assetDir);
    void close();

    bool up() const { return up_; }
    void toggle() { up_ = !up_; }
    void hide() { up_ = false; }

    // One frame while it is up: rebuilt when what it shows moved. Returns the travel row the
    // pointer pressed on, or -1.
    int update(const Play& play, const Pointer& pointer, int width, int height);
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    // One button: a map, and its rows in the travel list (a town has one, the Dungeon three). A
    // world this game does not have yet has none, and is only its name.
    struct Place {
        std::string name;
        int32_t map = 0;
        std::vector<int> rows;
        int low = 0, high = 0;  // its monsters' levels, 0 for none
        bool giver = false, merchant = false;
        int quest = -1;  // the quest its giver gives (sim/quests.h), or -1
    };
    std::vector<Place> places_;
    static constexpr int kPlaces = 16;

    // What a frame showed, so an unchanged one draws nothing new.
    struct Drawn {
        int width = 0, height = 0;
        int32_t here = -1;
        int floor = -1;  // the row of the floor he stands on, on a map of several
        // Each place's event, as shown: 0 none, 1 soon, 2 on; and its seconds, to the start or
        // to the end.
        uint8_t events[kPlaces] = {};
        int eventSeconds[kPlaces] = {};
        // While a gate is open, where its dot is in its breath, in 36ths (style::kLiveSeconds).
        int pulse = 0;
        uint8_t refusals[sim::kTravels] = {};
        // The quest a row refused for its quest waits on (Realm::travelQuest), or -1.
        int8_t lockedBy[sim::kTravels] = {};
        // Each quest under way or done and not handed in, and how far along, in percent.
        uint8_t quests[sim::kQuests] = {};
        uint8_t shares[sim::kQuests] = {};
        // A Resting quest's minutes until its giver offers it again, 0 for unknown.
        int restMinutes[sim::kQuests] = {};
        // Each quest cleared before: its marks in the beacon's repeat blue.
        bool again[sim::kQuests] = {};
        int hoverPlace = -1, hoverFloor = -1;
        bool operator==(const Drawn& o) const;
    };
    void rebuild(const Drawn& now);

    gfx::Canvas canvas_;
    bool up_ = false;
    bool built_ = false;
    Drawn drawn_;
    // Laid out by the last rebuild, for the pointer: a place's button, and its floors' chips.
    struct Hit {
        int place = -1;
        gfx::Box box;
        int floors = 0;
        gfx::Box chip[sim::kTravels];
        int chipRow[sim::kTravels] = {};
        bool usable = false;
    };
    std::vector<Hit> hits_;
    gfx::Box column_;
};

}  // namespace mu::game
