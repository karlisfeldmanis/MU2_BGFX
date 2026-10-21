// The chat, at the bottom left: what the server passes on (net::Heard) read on tabs, and a line
// typed and said (net::Say). WoW's way, as the user tuned it on claude.ai/artifact/S5h6Pt8CarQMb8bUWCyPd8
// (2026-10-21: "we need to refactor how the chat works, its seems clumsy. i like how WoW is designed"):
//
//   * **The tabs filter what the log shows**: General (everything but Global), Global, and a tab
//     for each person he whispers with (when the setting keeps whispers in tabs), with a cross. The line goes where the edit box says --
//     its channel's word at its left ("Say", "Party", "Global", "To Ilse") -- and the box keeps the
//     last channel used. Choosing a tab sets it (the user, 2026-10-21: "if i press global chat,
//     user needs to write on global chat, if i press on whisper i reply on that whisper"): Global
//     on Global, a person's tab to him, General back on its own channel. Joining a
//     party sets it to Party once (the 2026-10-09 rule, softened); leaving one puts Say back.
//     /s /y /m /p /g /1 and a space switch it as he types, the slash eaten; /w NAME and a space
//     whisper him; /r and a space answer the last whisper. Tab steps through the people he has
//     whispered with, Up and Down bring back what he said. A click on the channel's word lists
//     the channels for those who never type a slash.
//   * **Always shown**, quieter while idle (the user: "lets always show chat, but when its not
//     active make it less vissible"): the fill, the edge, the tabs and the lines dimmed by the
//     When-idle setting; a line arriving stands at full for a few seconds and settles. The
//     pointer on it, or Enter, brings it to full. Enter opens the edit box and nothing else.
//   * **The look** ("B · Edged", then "tabs has to be outside the main container", "active tab
//     has to be perfectly merged with container color with some gradient coming from top"): a
//     flat see-through fill, a 1-unit iron edge brighter at the top drawn as its own line (a
//     layer under the fill tints it), a soft drop, and the tabs standing on the panel's top edge
//     as plain words; the chosen one wears the panel's own fill and edge, the edge broken under
//     it, a warm light from its top running down into the panel. No red bar.
//   * **Lines read as WoW's**: the whole line in its channel's ink ("[Party] [Elva]: ...",
//     "[Ilse] whispers: ...", "To [Ilse]: ..."), names in brackets that answer the pointer -- a
//     click whispers, a right click asks for the player menu (Chat::takeMenuAsk). General also
//     says what he and his party pick up ("You loot [X].", "Brann loots [X]."; the user, "in
//     general we need to also show what items was pickup by player or party member") and the
//     answers to invites and trades ("also acepted,declined trades/parties/guildinvites").
//   * **Settings** dock on the chat's right, behind the sliders button at the tab row's end
//     ("settong has to be right side of chat container not overlaping the chat"): text size,
//     the fill's strength, how bright it stays idle, time stamps, lines over heads, whispers in
//     their own tab or in General, the lock, and a reset. A tab's right click lists what it
//     shows. Everything kept in chat.txt in the player's folder.
//   * **Its size and place** the player's: the corner grip at the panel's top right sets width and
//     height (it grows up and right from its foot), a double click on it puts the first size back;
//     a tab dragged moves the whole chat. The lock keeps both.
//   * **A party, guild or trade invite** stands in the middle over the HUD with Accept and Decline,
//     as it did; the log keeps its line.
//
// **Items named** (2026-10-11): a line may carry item links (sim/item_link.h), drawn "[Name]" in
// the thing's quality colour; the pointer on one shows its card. Typing "[" and a few letters lists
// what he carries so named, then what the world knows; Tab, Enter or a click puts the link in.
// Shift and a click on a thing in the bag links it. A "[Name]" typed out whole becomes a link.
#pragma once

#include <map>
#include <memory>
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
    // The server this run plays on ("host:port"), whose ignored the chat reads: before the
    // first frame, as chat.txt is read on it. Empty is the local realm.
    void setRealm(const std::string& server) { realm_ = server.empty() ? "local" : server; }

    // A frame. `bottom` is the HUD plate's top (the invite card stands over it); `right` is kept
    // for the desk's call and unused. `typed`, `enter` and `escape` are the keyboard's when no
    // other box has it; `wheel` the scroll wheel's.
    void update(float seconds, float width, float height, float bottom, float right,
                const Pointer& pointer, float wheel, const std::string& typed, int backspaces,
                bool enter, bool escape, bool toggle, Play& play, bool tab = false, int arrows = 0);

    // What the HUD covers this frame (Hud::blocks): the chat's frame stays out of it, its width
    // stopping short of the plate and its foot standing over it.
    void avoid(std::vector<gfx::Box> blocks) { blocks_ = std::move(blocks); }

    // Whether it has the keyboard: typing, so no other key is the game's.
    bool typing() const { return open_; }
    // Whether the pointer is on it: the tabs, the panel, the settings, a menu of its own.
    bool covers(float x, float y) const;

    bool showing() const { return showing_; }
    // The suggestions over the input, a hovered link's card and its menus: their own canvas,
    // drawn last.
    bool tipping() const { return tipping_; }
    const gfx::Canvas& tipCanvas() const { return tip_; }
    void useTipStage(Stage* stage) { tipStage_ = stage; }
    // A thing's link put into the line being typed (Shift and a click in the bag), the chat
    // opened for it.
    void link(const content::ItemRow& row, const sim::Held& held);
    // The open invite in the middle (its own canvas, over the plate), while there is one.
    bool inviting() const { return inviting_; }
    const gfx::Canvas& invite() const { return inviteDrawn_; }
    const gfx::Canvas& back() const { return back_; }    // the drop, the fill, the edge, the input
    const gfx::Canvas& tabs() const { return tabsDrawn_; }  // the tab row's words and icons
    const gfx::Canvas& lines() const { return lines_; }   // the log, clipped to its well
    // The settings docked on the right, while they are open.
    bool setting() const { return settingsOpen_; }
    const gfx::Canvas& settings() const { return settingsDrawn_; }

    // A whisper to `name` begun from outside (the party bar's menu, a name clicked): the edit
    // box opened on "To NAME".
    void whisperTo(const std::string& name);
    // A name right-clicked in the log: whose, and where the pointer was, once.
    bool takeMenuAsk(std::string* name, float* x, float* y);

    // A line from outside: Play's own, as a party's or the server's would be. `name` empty is a
    // line of the channel itself.
    // False when it is dropped: Global left, or its speaker ignored.
    bool hear(const net::Heard& line);
    // The server's own line, as Play says it: a trade's news ("Elora declined to trade.").
    void notice(const std::string& text) { system(net::Channel::Server, text); }
    // A trade asked of him by `name` (body `who`): a line, and the invite's card in the middle
    // with Accept and Decline, which answer the realm (Play::dealAnswer) and not the server.
    // False when the asker is ignored: nothing shown.
    bool askedToTrade(const std::string& name, uint32_t who);
    // A guild's call (net::GuildCall, docs/game/guild.md): an invite to him, or a player asking to join
    // his guild. The party's card in the middle, kicked "Guild", with the mark and a line of what
    // joining means; Accept and Decline answer the server's guild (net::GuildAsk).
    bool guildCalled(const net::GuildCall& call);  // false likewise
    // The guild's latest line from the server ("You invited Wren."), heard within `within`
    // seconds, or empty: the guild windows' foot says it under what was pressed.
    std::string guildSaid(float within) const;
    // The open invites or trades answered for him, as the Helper's settings say (desk.cpp): each
    // as Accept or Decline on its card would. The count answered.
    int answerOpen(Play& play, bool trades, bool accept);

    // A review run's lines (--chat-sample): one of every kind, so a picture shows them all.
    void sample();

    // On to another world (a gate, a warp, the travel window): the log, the chosen tab, the
    // channel and the draft go with him, since the world's screen -- this chat with it -- is
    // made anew for each map (application.cpp). The next world's chat takes them on its first
    // frame. The character screen leaves them behind.
    void carry();

private:
    // What a line is, beyond its channel: said by someone, a pickup, or an invite's or trade's
    // news. The last two are General's own ticks (kLootBit, kAnswerBit).
    enum class Kind : uint8_t { Said, Loot, Answer };
    struct Line {
        net::Channel channel = net::Channel::Map;
        Kind kind = Kind::Said;
        std::string name, to, text;
        // `text` as it reads, its links "[Name]", and where each stands in it.
        std::string shown;
        std::vector<sim::ItemLink> links;
        const content::Tables* resolved = nullptr;  // the tables `links` were read against
        bool bot = false;
        float age = 0.0f;   // seconds since heard
        std::string stamp;  // "21:04", the wall clock when heard
        // A party invite, the server's line on Party: who asks, and how it was answered (0 not
        // yet, 1 accepted, -1 declined).
        std::string inviter;
        int answer = 0;
        // A trade's ask instead (Chat::askedToTrade): the asker's body, 0 for a party's invite.
        uint32_t trader = 0;
        // A guild's call instead (Chat::guildCalled): the card's mark and its quiet line.
        bool guild = false;
        net::GuildMarkBits mark{};
        std::string meta;
        std::string guildName;  // set in the guild's green on the card
    };
    // A line as the log sets it: its stamp, its tag and name, and what was said, as one string,
    // with where its name and its links stand in it.
    struct Set {
        std::string text;
        size_t nameFrom = 0, nameTo = 0;  // the "[Name]" a pointer answers, empty for none
        std::string who;                  // whose name that is
        size_t stampTo = 0;               // the stamp's end: drawn quiet
        std::vector<sim::ItemLink> links;
    };
    // One row of the log as it is set: a line wraps into several.
    struct Row {
        size_t line = 0;
        bool first = true;
        size_t start = 0, end = 0;  // this row's bytes of the line's Set
    };
    struct Carried;
    static std::unique_ptr<Carried> carried_;  // carry()'s, until the next world's load()
    struct Tab {
        std::string key;  // "general", "global", or "w:" and the one he whispers with
        std::string word;
        gfx::Box box;     // laid this frame
        gfx::Box close;   // a whisper tab's cross, laid this frame
    };
    // The edit box's channel: where the next line goes.
    struct Sticky {
        net::Channel channel = net::Channel::Local;
        std::string to;  // a whisper's
    };
    // A line he said, for Up and Down.
    struct Sent {
        Sticky where;
        std::string text;
    };

    void send(Play& play);
    // A line's links read and its shown text set (hear), and found in these tables (update).
    static void read(Line& line);
    void resolve(Line& line) const;
    Set setOf(const Line& line) const;
    uint32_t inkOf(const Line& line) const;
    // `text` from `start` to `end` of a Set, its links in their colours and its name lit when
    // the pointer is on it; each link's and the name's box kept for the pointer when `line` >= 0.
    float spans(gfx::Canvas& canvas, float x, float base, float size, uint32_t ink, const Set& set,
                size_t start, size_t end, float alpha, float u, long line, const Pointer& pointer);
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
    void system(net::Channel channel, const std::string& text);
    bool shows(const Line& line, const std::string& tab) const;
    // The bits a tab shows: 1 << Channel, and the loot's and the answers'.
    uint32_t& filter(const std::string& tab);
    bool ignored(const std::string& name) const;
    void stick(net::Channel channel, const std::string& to = std::string());
    // A slash and a space typed: the channel switched and the slash eaten.
    void switchBySlash();
    // chat.txt in the player's folder: the ticks, the settings, the frame, Global left, each
    // realm's ignored.
    void load();
    void save() const;
    void place(float u, float width, float height);
    void layTabs(float u);
    void layRows(float u);
    void layLog(float u);
    void build(float u, const Pointer& pointer);
    void buildSettings(float u, const Pointer& pointer);
    void buildInvite(float u, float width, float bottom, const Pointer& pointer);
    long pendingInvite() const;  // the newest open invite's line, or -1
    std::string channelWord() const;
    uint32_t channelInk(net::Channel channel) const;
    float textSize() const;
    float rowTall() const;

    gfx::Canvas back_, tabsDrawn_, lines_, inviteDrawn_, tip_, settingsDrawn_;
    Stage* tipStage_ = nullptr;
    bool tipping_ = false;
    const content::Tables* tables_ = nullptr;  // his world's, this frame
    // The links and names as last drawn, for the pointer.
    struct LinkBox {
        gfx::Box box;
        size_t line = 0, link = 0;
    };
    std::vector<LinkBox> linkBoxes_;
    struct NameBox {
        gfx::Box box;
        std::string who;
    };
    std::vector<NameBox> nameBoxes_;
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
    bool open_ = false;     // the edit box up, the keyboard ours
    bool showing_ = false;
    float lit_ = 0.0f;      // 0 idle to 1 full, eased: the pointer on it or the box open
    std::string me_;        // his own name, as the server says it
    std::vector<Line> log_;
    std::vector<Tab> tabs_;
    std::vector<std::string> whispers_;  // who he whispered with, the latest last: Tab's round
    // A tab each for the people he whispers with, in the order they were opened (the user,
    // 2026-10-21: "each whisper has to be on his own tab"); closed with its cross.
    std::vector<std::string> whisperTabs_;
    std::map<std::string, int> unread_;  // by tab key
    std::string tab_ = "general";
    uint32_t generalShows_ = 0, globalShows_ = 0, whispersShows_ = 0;  // filter()'s, set by load()
    bool loaded_ = false;
    bool leftGlobal_ = false;
    std::vector<std::string> ignored_;  // lower case, this realm's
    // A name is only its realm's, so the ignored are kept a realm each (the user, 2026-10-20,
    // after a realm change): this one's by its server (setRealm), the others' carried through
    // chat.txt untouched, "realm name" each.
    std::string realm_ = "local";
    std::vector<std::string> elsewhere_;

    // The settings (chat.txt): the text's size (0 S, 1 M, 2 L), the fill's strength and how
    // bright it stays idle (percent), time stamps, lines over heads, whispers in a tab of their
    // own, and the lock.
    int size_ = 1;
    int fill_ = 62, idle_ = 50;
    bool stamps_ = false, bubbles_ = true, whisperTab_ = true, locked_ = false;
    // Its frame, in units: the left edge, the foot's height over the screen's bottom, the width
    // and the height (the tab row's in it).
    float left_ = 0.0f, foot_ = 0.0f, wide_ = 0.0f, tall_ = 0.0f;
    std::vector<gfx::Box> blocks_;  // avoid()'s

    // The edit box's channel, and how he stood with a party and a guild last frame.
    Sticky sticky_;
    Sticky generalSticky_;  // General's own: what its tab puts back (Say, Map, Party, Guild)
    bool inParty_ = false, inGuild_ = false, wasInParty_ = false;
    std::string lastFrom_;  // the last who whispered him: /r's
    std::string draft_;
    std::vector<Sent> sent_;
    int recalled_ = -1;  // Up's place in sent_, -1 for the draft
    float clock_ = 0.0f;

    // Where things were laid this frame, in pixels.
    gfx::Box frame_, row_, panel_, logBox_, input_, chip_, grip_, gear_, settingsBox_;
    float logScroll_ = 0.0f;  // pixels up from the newest line
    float logTall_ = 0.0f;    // the whole log as set
    int unseen_ = 0;          // lines come in while he reads back: the "new lines" button's
    gfx::Box newer_;          // that button, while it shows
    std::vector<Row> rows_;
    std::vector<Set> sets_;  // log_'s, by index, for the rows
    uint64_t laid_ = 0;      // what rows_ were laid for
    // The invites' buttons as last drawn, for the next press.
    struct Choice {
        gfx::Box box;
        size_t line = 0;
        bool accept = false;
    };
    std::vector<Choice> choices_;
    uint64_t built_ = 0, settingsBuilt_ = 0;
    bool inviting_ = false;
    gfx::Box inviteCard_;

    // A menu of its own, in tip_: the channels for the edit box, or what a tab shows.
    enum class Menu : uint8_t { None, Channels, Shows };
    Menu menu_ = Menu::None;
    std::string menuTab_;  // the tab whose ticks the Shows menu sets
    gfx::Box menuBox_;
    struct MenuRow {
        gfx::Box box;
        int what = 0;  // a channel, or a filter bit
    };
    std::vector<MenuRow> menuRows_;

    // The settings, docked: open, and their rows as drawn.
    bool settingsOpen_ = false;
    gfx::Box sizeBox_, fillBox_, idleBox_, stampsBox_, bubblesBox_, whispersBox_, lockBox_, resetBox_, closeBox_;

    // A name right-clicked, for the desk.
    std::string menuAsk_;
    float menuAskX_ = 0.0f, menuAskY_ = 0.0f;

    // A drag: the corner, the frame by a tab, the log's slider, a setting's slider.
    enum class Drag : uint8_t { None, Grip, Move, LogSlider, Fill, Idle };
    Drag drag_ = Drag::None;
    float dragX_ = 0.0f, dragY_ = 0.0f;  // where it began
    float startA_ = 0.0f, startB_ = 0.0f, startC_ = 0.0f, startD_ = 0.0f;
    std::string pressedTab_;  // a tab pressed, chosen on release unless it was dragged
    bool moved_ = false;
    float gripClicked_ = -10.0f;  // the clock at the grip's last press: a second inside one is a double
    bool sampleWanted_ = false;   // sample() asked: heard on the first frame with tables
};

}  // namespace mu::game
