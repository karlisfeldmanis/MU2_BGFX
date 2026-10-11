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
//   * **Closed**, the last lines stay over the world a while and fade -- as many as his height
//     for it holds -- the cloud under them half as dark, with "Enter to chat" and the whispers
//     waiting.
//   * **A party invite** stands in the middle over the HUD, above where a fight's Zen is said
//     (game/ui/tally.h), with Accept and Decline, open or shut ("party invites has to be middle
//     of center similiar position where zen is gained"). The log keeps its line.
//
// Enter opens it with the keyboard (Desk::typing, so I and C and Q type and do nothing else);
// Enter again says the line and shuts it, Escape shuts it, the draft kept.
//
// **Where a line goes** (the user, 2026-10-09: "default chat channel has to be /all and only when
// char is inside part it should go to party not provates messages", "whispering is something
// whuch need addiitonal click"): on All, to his party when he has one and to his map when not; on
// Map, Party or Guild, there; a whisper only on its player's tab. A whisper heard never moves it.
// A slash is a command for one line, the default left as it was: /p /g /m TEXT, /w NAME TEXT, /r
// TEXT; one with nothing after it goes to that tab instead.
// A Map line said by someone in view goes up over his head too, as MU's did (Play::bubble).
//
// **Items named** (the user, 2026-10-11: "allow bots to write on chat [item name] with working
// tooltips", then "autocompletion ... when they write [item name]"): a line may carry item links
// (sim/item_link.h), drawn "[Name]" in the thing's quality colour; the pointer on one, the chat
// open, shows its card. Typing "[" and a few letters lists what he carries that is so named, then
// every thing the world knows by it; Tab, Enter or a click puts the link in, Up and Down choose.
// Shift and a click on a thing in the bag links it. A "[Name]" typed out whole and sent becomes
// the link of the first thing so named, his own before the catalogue's.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "gfx/interface.h"
#include "net/wire.h"
#include "sim/item_link.h"

namespace mu::game {

class Play;
class Stage;

class Chat {
public:
    void open(const gfx::Interface& interface);

    // A frame. `bottom` is where its foot stands (over the HUD's plate) and `right` the most it
    // may reach (short of the buffs); `typed`, `enter` and `escape` are the keyboard's when no
    // other box has it; `wheel` the scroll wheel's.
    void update(float seconds, float width, float height, float bottom, float right,
                const Pointer& pointer, float wheel, const std::string& typed, int backspaces,
                bool enter, bool escape, bool toggle, Play& play, bool tab = false, int arrows = 0);

    // Whether it has the keyboard: typing, so no other key is the game's.
    bool typing() const { return open_; }
    // Whether the pointer is on it: only while it is open. Closed, the lines take no clicks.
    bool covers(float x, float y) const;

    bool showing() const { return showing_; }
    // The suggestions over the input and a hovered link's card: their own canvas, drawn last.
    bool tipping() const { return tipping_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }
    void useTipStage(Stage* stage) { tipStage_ = stage; }
    // A thing's link put into the line being typed (Shift and a click in the bag), the chat
    // opened for it.
    void link(const content::ItemRow& row, const sim::Held& held);
    // The open invite in the middle (its own canvas, over the plate), while there is one.
    bool inviting() const { return inviting_; }
    const gfx::Canvas& invite() const { return inviteDrawn_; }
    const gfx::Canvas& back() const { return back_; }    // the cloud, the rules, the input
    const gfx::Canvas& tabs() const { return tabsDrawn_; }  // the tab row, clipped to it
    const gfx::Canvas& lines() const { return lines_; }   // the log, clipped to its well

    // A whisper to `name` begun from outside (the party bar's menu): his tab opened and chosen,
    // and the line open to type, as /w and a name alone does.
    void whisperTo(const std::string& name);

    // A line from outside: Play's own, as a party's or the server's would be. `name` empty is a
    // line of the channel itself.
    void hear(const net::Heard& line);
    // The server's own line, as Play says it: a trade's news ("Elora declined to trade.").
    void notice(const std::string& text) { system(net::Channel::Server, text); }
    // A trade asked of him by `name` (body `who`): a line, and the invite's card in the middle
    // with Accept and Decline, which answer the realm (Play::dealAnswer) and not the server.
    void askedToTrade(const std::string& name, uint32_t who);

private:
    struct Line {
        net::Channel channel = net::Channel::Map;
        std::string name, to, text;
        // `text` as it reads, its links "[Name]", and where each stands in it.
        std::string shown;
        std::vector<sim::ItemLink> links;
        const content::Tables* resolved = nullptr;  // the tables `links` were read against
        bool bot = false;
        float age = 0.0f;  // seconds since heard
        // A party invite, the server's line on Party: who asks, and how it was answered (0 not
        // yet, 1 accepted, -1 declined). An open one wears Accept and Decline under it.
        std::string inviter;
        int answer = 0;
        // A trade's ask instead (Chat::askedToTrade): the asker's body, 0 for a party's invite.
        uint32_t trader = 0;
    };
    // One row of the log as it is set: a line wraps into several.
    struct Row {
        size_t line = 0;
        bool first = true;
        std::string text;  // this row's part of the line's shown text
        size_t start = 0;  // ... from this byte of it
    };
    struct Tab {
        std::string key;    // "all", "map", "party", "guild", or "w:" and the name
        std::string word;
        bool closable = false;
        float x = 0.0f, w = 0.0f;  // laid on the row, before its scroll
    };

    void send(Play& play);
    // A line's links read and its shown text set (hear), and found in these tables (update).
    static void read(Line& line);
    void resolve(Line& line) const;
    // `text` from `start`, its links' parts in their colours and the rest in `ink`; each link
    // part's box kept in linkBoxes_ for `line` when `hits`. Its advance.
    float spans(gfx::Canvas& canvas, float x, float base, float size, uint32_t ink, const std::string& text,
                size_t start, const std::vector<sim::ItemLink>& links, float alpha, float u, long line);
    uint32_t linkInk(const sim::ItemLink& link) const;
    // The suggestions for the "[" being typed, and one taken.
    void suggest(const sim::Realm& realm);
    void take(size_t index);
    // A "[Name]" typed whole made the first thing's link so named.
    std::string named(const std::string& text, const sim::Realm& realm) const;
    void buildTip(float u, float width, float height, const Pointer& pointer, const sim::Realm& realm);
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
    int closedRows(float u) const;  // the lines shown while it is shut, from its height
    void buildInvite(float u, float width, float bottom, const Pointer& pointer);
    long pendingInvite() const;  // the newest open invite's line, or -1
    void cloud(gfx::Canvas& canvas, float middle, float tall, float strength, float u);
    std::string channelWord() const;
    uint32_t channelInk(net::Channel channel) const;

    gfx::Canvas back_, tabsDrawn_, lines_, inviteDrawn_, tip_;
    Stage* tipStage_ = nullptr;
    bool tipping_ = false;
    const content::Tables* tables_ = nullptr;  // his world's, this frame
    // The links as last drawn, for the pointer: the box, the line, which of its links.
    struct LinkBox {
        gfx::Box box;
        size_t line = 0, link = 0;
    };
    std::vector<LinkBox> linkBoxes_;
    // The "[" being typed: what it would be, the one chosen, and where each was drawn.
    struct Suggestion {
        std::string label;
        sim::Held held;
        const char* where = "";  // "worn", "bag", "vault", or nothing for the catalogue's
    };
    std::vector<Suggestion> suggestions_;
    std::string query_ = "\x01";  // what they were found for
    size_t bracket_ = 0;          // the "[" in the draft
    int pick_ = 0;
    std::vector<gfx::Box> suggestBoxes_;
    gfx::Box suggestBox_;
    float pointerX_ = 0.0f, pointerY_ = 0.0f;  // last frame's: a still pointer leaves the arrows' pick
    bool open_ = false;
    bool showing_ = false;
    std::string me_;  // his own name, as the server says it
    std::vector<Line> log_;
    std::vector<Tab> tabs_;
    std::vector<std::string> whispers_;  // the open whisper tabs, by name, in the order opened
    std::map<std::string, int> unread_;  // by tab key
    std::string tab_ = "all";
    net::Channel channel_ = net::Channel::Map;  // where the next line goes, as the tab says (update)
    // The channel's word pressed on All: Map, Party or Guild in its place until the tab changes.
    bool chipSet_ = false;
    // Party and Guild show as tabs only while he is in one (the user, 2026-10-09: "dont show
    // guild, party tabs if there is no guild or party"). No guilds yet: never in one.
    bool inParty_ = false, inGuild_ = false;
    net::Channel chipChannel_ = net::Channel::Map;
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
    bool inviting_ = false;
    gfx::Box inviteCard_;

    // A drag: the grip, the tabs' slider or the log's.
    enum class Drag : uint8_t { None, Grip, TabSlider, LogSlider };
    Drag drag_ = Drag::None;
    float dragFrom_ = 0.0f, dragStart_ = 0.0f;
};

}  // namespace mu::game
