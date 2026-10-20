// The quest log (L, and the HUD's quest button): every quest he has under way or has cleared and
// may take again, under the name of the map it is played on, and the one chosen read out under
// them -- its giver, its steps as they stand, its rewards with their pictures.
//
// The proposal of 2026-10-17 (claude.ai/artifact/9AKftxhxyA8TWRuz3sScZc, "A · First clear
// picked"), on the user's words as it was drawn: WoW's quest log, "quest lsit has to be simplier,
// with quests under map names", no map ("dont use miimap"), a title and a close ("we need window
// title close, and use our design system"), the rewards one grid ("dont show FIRST CLEAR ONLY
// ... just show rewards"), and no foot: "if user is at least one time completed the quest
// auto-play will be usable", its button on the row ("dont show auto-play button at footer we
// already have them"). It replaced the journal, the quest dialog read away from its giver.
//
// The side window's size, at the left where the giver's window stands; the giver's window takes
// its place while he is spoken to. Ours: MU 0.75 has no quests.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "game/ui/stage.h"
#include "gfx/interface.h"

namespace mu::game {

class Play;

class QuestLog {
public:
    struct Result {
        bool close = false;
        bool clicked = false;  // a row chosen: the window's click
        int autoPlay = -1;     // Auto-play pressed on this quest's row
        bool stop = false;     // and Stop on the one playing
    };
    void open(const gfx::Interface& interface, const std::string& assetDir);
    void close();
    // Up on the quest `prefer` chose (the one the tracker follows, or the one playing), else
    // the first listed.
    void show(int prefer);
    void hide();
    bool up() const { return up_; }
    // One frame while up. `playing` is the quest Auto-play has, -1 for none. `stage` photographs
    // the rewards; null while a counter has the shelf's.
    void update(float seconds, const Play& play, int playing, float width, float height,
                const Pointer& pointer, float wheel, Stage* stage, Result* out);
    bool covers(float x, float y) const;
    const gfx::Canvas& canvas() const { return canvas_; }
    // The list and the card, each clipped to its own pane; drawn after the frame, in that order.
    const gfx::Canvas& body() const { return body_; }
    const gfx::Canvas& card() const { return card_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }
    void useTipStage(Stage* stage) { tipStage_ = stage; }
    float pixelsPerUnit() const { return unit_; }

private:
    struct Entry {
        int quest = -1;
        int map = -1;
        bool heads = false;  // the first of its map: its name over it
        gfx::Box box;        // in window units, list-scrolled
        gfx::Box button;     // the row's Auto-play or Stop, w 0 for none
    };
    struct Cell {
        int32_t item = -1;
        int plus = 0, count = 1;
        uint8_t sockets = 0, power = 0;
        uint8_t affixes[3] = {};
        uint32_t ink = 0;
        gfx::Box box;  // in window units, card-scrolled
    };
    // The Magic Gladiator's paths over their own cells, where a first clear pays both.
    struct Path {
        const char* word = "";
        float y = 0.0f;  // the heading's baseline, in window units, card-scrolled
    };
    void layout(const Play& play, int playing);
    void rebuild(const Play& play, int playing, Stage* stage);
    void drawTip(const Play& play, int cell, float width, float height);

    gfx::Canvas canvas_, body_, card_, tip_;
    Stage* tipStage_ = nullptr;
    bool up_ = false;
    int selected_ = -1;
    int prefer_ = -1;
    std::string assetDir_;
    std::vector<Entry> entries_;
    std::vector<Cell> cells_;
    std::vector<Path> paths_;
    std::vector<Standing> standing_;
    // over_/pressing_: 0 the close, 100 + a row, 200 + a row's button, 300 + a reward.
    int over_ = -1, pressing_ = -1;
    float x_ = 0.0f, y_ = 0.0f, unit_ = 1.0f, wide_ = 325.0f, tall_ = 734.0f;
    float listTop_ = 0.0f, listTall_ = 0.0f, listContent_ = 0.0f, listScroll_ = 0.0f;
    float cardTop_ = 0.0f, cardTall_ = 0.0f, cardContent_ = 0.0f, cardScroll_ = 0.0f;
    std::vector<int64_t> drawn_;
    bool built_ = false;
};

}  // namespace mu::game
