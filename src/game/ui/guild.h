// The guild's two windows (docs/guild.md; the user's picks on the proposal page,
// claude.ai/artifact/2qCQPGfwWcWmi9XCRvSVvq, 2026-10-13):
//
//   * **GuildWindow**, G: "B · Online first". The mark, the name, members over the cap and how
//     many are online; the notice, which the master edits in place (60 letters, Enter keeps it);
//     Online and All tabs over two-line cards -- the master first, then those on his map, then by
//     level, offline last with when they were seen -- eight to a page; a click on a card is the
//     player menu's (Whisper, Invite to party, Trade, Remove or Leave), each removal asked first.
//     The master's foot invites by name and disbands, the name typed; a member's leaves.
//   * **GuildMake**, the Guild Master's: MuMain's three steps (NewUIGuildMakeWindow) -- what he
//     needs and what a guild gives, ticked against his own figures; a name and an 8x8 mark in MU's
//     16 colours, with Mirror, Undo and Clear and the mark at its real size beside the grid; and
//     the guild as it will look, founded on a word to the server.
//
// Nothing here keeps a guild: every button is a request (net::GuildAsk), and the windows redraw
// off the server's GuildState (the interface is a mirror).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/ui/hud.h"
#include "game/ui/panel.h"
#include "gfx/interface.h"
#include "net/wire.h"

namespace mu::game {

class Play;

// What the keyboard did this frame, for a window's field.
struct GuildKeys {
    std::string typed;
    int backspaces = 0;
    bool enter = false;
    bool escape = false;
};

// What a window asked this frame; the desk sends them.
struct GuildRequests {
    std::vector<net::GuildAsk> asks;
    std::string whisper, party, trade;  // a member's menu: whom
    bool clicked = false;               // a button pressed: the desk's click
};

class GuildWindow {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);
    // A frame. `said` is the guild's latest line from the server (the chat's), for the foot.
    void update(float seconds, float width, float height, int column, Play& play, const Pointer& pointer,
                const GuildKeys& keys, const std::string& said, GuildRequests* out);
    bool up() const { return up_; }
    void show() { up_ = true; }
    void hide();
    void toggle() { up_ ? hide() : show(); }
    bool covers(float x, float y) const;
    // A field has the keyboard: the invite's name, the notice or the disband's name.
    bool typing() const { return up_ && focus_ != Focus::None; }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    enum class Focus : uint8_t { None, Invite, Notice, Disband };
    enum class Confirm : uint8_t { None, Remove, Leave, Disband };
    struct Row {
        size_t member = 0;  // into the state's members
        gfx::Box box;       // in units
    };
    void layout(const net::GuildState& state, const std::string& me);
    void draw(const net::GuildState& state, const std::string& me, const std::string& said, float seconds);

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    bool up_ = false;
    float x_ = 0.0f, y_ = 0.0f;
    bool allTab_ = false;
    int page_ = 0;
    std::vector<Row> rows_;
    std::vector<size_t> order_;  // the members in the roll's order, for this tab
    int pages_ = 1;
    // A member's menu: whose card, and its rows as drawn.
    long menu_ = -1;
    std::vector<std::string> menuRows_;
    gfx::Box menuBox_;
    Focus focus_ = Focus::None;
    std::string invite_, notice_, disband_;
    Confirm confirm_ = Confirm::None;
    std::string confirmName_;
    float caret_ = 0.0f;
    std::string hovered_;  // what the pointer is on, for the redraw
    uint64_t drawnKey_ = 0;
    bool drawnOnce_ = false;
    std::string ownSaid_;  // a line of the window's own (a menu's "Party invite sent")
    float ownSaidAge_ = 99.0f;
};

class GuildMake {
public:
    void open(const gfx::Interface& interface, panel::Arts* arts);
    void update(float seconds, float width, float height, int column, Play& play, const Pointer& pointer,
                const GuildKeys& keys, const std::string& said, GuildRequests* out);
    bool up() const { return up_; }
    void show();
    void hide();
    bool covers(float x, float y) const;
    bool typing() const { return up_ && step_ == 2 && nameFocus_; }
    const gfx::Canvas& canvas() const { return canvas_; }

private:
    void draw(Play& play, const std::string& said);
    void paint(int x, int y, int colour);

    gfx::Canvas canvas_;
    panel::Arts* arts_ = nullptr;
    bool up_ = false;
    float x_ = 0.0f, y_ = 0.0f;
    int step_ = 1;        // 1 what it takes, 2 name and mark, 3 the look; 4 founded
    bool asked_ = false;  // Found sent, the server's word awaited
    std::string name_;
    bool nameFocus_ = true;
    net::GuildMarkBits mark_{};
    std::vector<net::GuildMarkBits> undo_;
    int colour_ = 4;      // red
    bool mirror_ = true;
    bool painting_ = false;
    int paintWith_ = 0;
    std::string hovered_;
    std::string refused_;  // the server's no to a Found, shown under the name
    std::string saidAtAsk_;  // the guild's last line when Found went: a later one is its answer
    float caret_ = 0.0f;
    uint64_t drawnKey_ = 0;
    bool drawnOnce_ = false;
};

}  // namespace mu::game
