// The chat, at the bottom left over the HUD's left wing (the user's mockup,
// claude.ai/artifact/AMHkjGrUWpJVkPmAbuDvGX, 2026-10-09): what the server passes on (net::Heard)
// read on tabs, and a line typed and said (net::Say).
//
// MU 0.75's chat was one box with its lines over the heads. Ours, as the user tuned the page:
//
//   * **Tabs.** All, Map, Party and Guild stay; each player who whispers him, or whom he
//     whispers, opens a tab of his own with a close. The row scrolls sideways on a thin slider
//     when it is too long ("slider has to be for whole head").
//   * **No box.** The quest tracker's soft dark cloud behind it, centred on the screen's left
//     edge, and two iron rules fading out over their last third ("similiar like we do for
//     quests", "gradient has to be darker"). The chosen tab lit gold and rising from its bar.
//   * **Its height** the player's: an up-and-down grip on its top edge; and the lines scroll
//     inside, on a slider of their own at the right.
//   * **Closed**, the last six lines stay over the world a while and fade, the cloud under them
//     half as dark, with "Enter to chat" and the whispers waiting.
//
// Enter opens it with the keyboard (Desk::typing, so I and C and Q type and do nothing else);
// Enter again says the line and shuts it, Escape shuts it, the draft kept. A line starting with
// a slash is a command: /p /g /m change the channel and say what follows, /w NAME and /r whisper.
// A Map line said by someone in view goes up over his head too, as MU's did (Play::bubble).
#pragma once

#include <map>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"
#include "net/wire.h"

namespace mu::game {

class Play;

class Chat {
public:
    void open(const gfx::Interface& interface);

    // A frame. `bottom` is where its foot stands (over the HUD's plate) and `right` the most it
    // may reach (short of the buffs); `typed`, `enter` and `escape` are the keyboard's when no
    // other box has it; `wheel` the scroll wheel's.
    void update(float seconds, float width, float height, float bottom, float right,
                const Pointer& pointer, float wheel, const std::string& typed, int backspaces,
                bool enter, bool escape, bool toggle, Play& play);

    // Whether it has the keyboard: typing, so no other key is the game's.
    bool typing() const { return open_; }
    // Whether the pointer is on it: only while it is open. Closed, the lines take no clicks.
    bool covers(float x, float y) const;

    bool showing() const { return showing_; }
    const gfx::Canvas& back() const { return back_; }    // the cloud, the rules, the input
    const gfx::Canvas& tabs() const { return tabsDrawn_; }  // the tab row, clipped to it
    const gfx::Canvas& lines() const { return lines_; }   // the log, clipped to its well

    // A line from outside: Play's own, as a party's or the server's would be. `name` empty is a
    // line of the channel itself.
    void hear(const net::Heard& line);

private:
    struct Line {
        net::Channel channel = net::Channel::Map;
        std::string name, to, text;
        bool bot = false;
        float age = 0.0f;  // seconds since heard
        // A party invite, the server's line on Party: who asks, and how it was answered (0 not
        // yet, 1 accepted, -1 declined). An open one wears Accept and Decline under it.
        std::string inviter;
        int answer = 0;
    };
    // One row of the log as it is set: a line wraps into several.
    struct Row {
        size_t line = 0;
        bool first = true;
        bool buttons = false;  // an open invite's Accept and Decline
        std::string text;  // this row's part of the line's text
    };
    struct Tab {
        std::string key;    // "all", "map", "party", "guild", or "w:" and the name
        std::string word;
        bool closable = false;
        float x = 0.0f, w = 0.0f;  // laid on the row, before its scroll
    };

    void send(Play& play);
    // An invite answered, by its line, or the latest open one for `index` -1 and `name` empty.
    void answer(Play& play, long index, const std::string& name, bool accept);
    uint64_t signature(float u, const Pointer& pointer) const;
    void select(const std::string& key);
    void closeTab(const std::string& key);
    void system(net::Channel channel, const std::string& text);
    bool shows(const Line& line, const std::string& tab) const;
    void layTabs(float u);
    void layRows(float u, float wide, const std::string& tab, size_t from);
    void build(float u, const Pointer& pointer);
    void buildClosed(float u);
    void cloud(gfx::Canvas& canvas, float middle, float tall, float strength, float u);
    std::string channelWord() const;
    uint32_t channelInk(net::Channel channel) const;

    gfx::Canvas back_, tabsDrawn_, lines_;
    bool open_ = false;
    bool showing_ = false;
    std::string me_;  // his own name, as the server says it
    std::vector<Line> log_;
    std::vector<Tab> tabs_;
    std::vector<std::string> whispers_;  // the open whisper tabs, by name, in the order opened
    std::map<std::string, int> unread_;  // by tab key
    std::string tab_ = "all";
    net::Channel channel_ = net::Channel::Map;
    std::string target_;    // a whisper's
    std::string lastFrom_;  // the last who whispered him: /r's
    std::string draft_;
    float clock_ = 0.0f;
    float tall_ = 0.0f;  // the panel's height in units, the player's (0: the default)

    // Where things were laid this frame, in pixels.
    gfx::Box panel_, grip_, tabRow_, logBox_, input_, chip_;
    float tabScroll_ = 0.0f;   // pixels the tab row is scrolled on
    float tabsWide_ = 0.0f;    // the whole row
    float logScroll_ = 0.0f;   // pixels up from the newest line
    float logTall_ = 0.0f;     // the whole log as set
    std::vector<Row> rows_;
    std::vector<size_t> closedLines_;  // the lines shown while it is shut, newest last
    // The invites' buttons as last drawn, for the next press.
    struct Choice {
        gfx::Box box;
        size_t line = 0;
        bool accept = false;
    };
    std::vector<Choice> choices_;
    // What the canvases were last built from: rebuilt only when it changes (a caret's blink, a
    // line, the pointer over it, a closed line fading), as every window here is.
    uint64_t built_ = 0;

    // A drag: the grip, the tabs' slider or the log's.
    enum class Drag : uint8_t { None, Grip, TabSlider, LogSlider };
    Drag drag_ = Drag::None;
    float dragFrom_ = 0.0f, dragStart_ = 0.0f;
};

}  // namespace mu::game
