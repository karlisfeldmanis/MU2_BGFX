// The character screen's drawing: the name plates over the pedestals, the bar along the foot,
// the create window and the box a notice or a deletion is asked in.
//
// MU2's Lobby.cs in behaviour, and the user's buttons in look -- *"MU2 godot has pretty good
// character selection screen, we need that but with our button designs"* (2026-09-27). So every
// rule of when a button answers, what Enter and Escape do and what each refusal says is MuMain's
// by way of MU2 (CharSelMainWin, CharMakeWin, MsgWin, CharInfoBalloon), and every plate is the
// game menu's slab (game/ui/slab.h) on the windows' glass instead of MU's leather.
//
// **The bar** (CCharSelMainWin): Create and Menu on the left, Connect and Delete on the right,
// here worded "Create Character", "Menu", "Enter World" and "Delete". Create answers while a
// pedestal is empty, Connect and Delete while somebody is picked; with nobody on the account the
// create window opens by itself. A click on a figure picks it, a click on the ground drops the
// pick, a double click or Enter starts.
//
// **The create window** (CCharMakeWin): a button a class, the class's four starting points, its
// description (texts 1705-1707), a name box that stops at ten, OK and Cancel. The client's own
// refusals come first -- "Type more than 4 letters", "Cannot use symbols." -- and the roster's
// after (game/roster.h). A refused creation re-opens the window when its notice is dismissed, as
// MsgWin.cpp:430 does.
//
// **The deletion**, as MU2 chains it: "Would you like to delete X character?", then the name
// typed into a box -- MU asks the account's password, and this machine has no account, so the
// name is what guards against a misclick, compared without case -- then the file moved aside.
//
// The screen raises requests and redraws; LobbyMode does what is asked of the roster and answers
// with a notice. The interface is a mirror.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/roster.h"
#include "game/ui/hud.h"
#include "gfx/interface.h"

namespace mu::game {

class Lobby {
public:
    // The class buttons' order, which is MU's create window's (Screens.Trades), the Magic
    // Gladiator fourth as MU's CLASS_DARK, once a character on the account has reached
    // kGladiatorLevel (the user, 2026-10-07: both the set and the gate; docs/mg-port.md).
    static constexpr int kClassRows = 4;
    // The level a character on the account must have reached before the Magic Gladiator may be
    // made: OpenMU's LevelRequirementByCreation (ClassMagicGladiator.cs:36). Under it his button
    // stands greyed, as CharMakeWin disables a class it does not offer (CharMakeWin.cpp:292-302).
    static constexpr int kGladiatorLevel = sim::kGladiatorLevel;
    bool gladiatorOpen() const;
    static constexpr sim::Kin kClasses[kClassRows] = {sim::Kin::DarkWizard, sim::Kin::DarkKnight,
                                                      sim::Kin::FairyElf,
                                                      sim::Kin::MagicGladiator};

    // What the scene behind the screen is showing this frame, handed in by the mode.
    struct View {
        const std::vector<Seat>* roster = nullptr;
        int picked = -1;
        int hovered = -1;  // the figure under the pointer, -1 for none
        // Each slot's plate anchor in pixels, and whether it is on screen.
        float plate[kRosterSlots][2] = {};
        bool plateSeen[kRosterSlots] = {};
    };

    struct Result {
        bool clicked = false;   // a button answered: the interface's click
        bool refused = false;   // a notice went up: the refusal's sound
        int pick = -2;          // -2 no change, -1 picked nobody, else the slot picked
        int enter = -1;         // the slot to play
        bool create = false;    // make `name` a `kin`
        std::string name;
        sim::Kin kin = sim::Kin::DarkKnight;
        int drop = -1;          // the slot to delete: its name was typed back
        bool menu = false;      // the Menu button, or Escape with nothing up
    };

    void open(const gfx::Interface& interface);

    void update(float seconds, float width, float height, const Pointer& pointer,
                const std::string& typed, int backspaces, bool enter, bool escape,
                const View& view, Result* out);

    // A notice with one OK. `thenCreate` re-opens the create window, with what was typed, once
    // it is dismissed; `thenName` goes back to the deletion's name box.
    void notice(const std::string& text, bool thenCreate = false, bool thenName = false);
    void openCreate(int classRow = 1);
    void closeCreate() { creating_ = false; }
    void askDelete();
    void setTyped(const std::string& name) { typed_ = name.substr(0, kNameLetters); }
    // The bust's picture (game/bust.h), drawn into the create window's corner, and where that
    // corner is in pixels on a frame this size -- what the picture is to be taken at.
    void setBust(const gfx::Art& art) { bust_ = art; }
    static gfx::Box bustBox(float width, float height);

    bool creating() const { return creating_; }
    sim::Kin chosen() const { return kClasses[classRow_]; }
    // A box or the create window is up and has the keyboard: Escape is not the game's.
    bool typing() const { return creating_ || box_ == Ask::Name; }
    // A box is up over everything, or the pointer is over the create window or the bar: the
    // figures do not hear the pointer.
    bool takesPointer() const { return box_ != Ask::None || overUi_; }

    const gfx::Canvas& canvas() const { return canvas_; }
    uint64_t rebuilds() const { return rebuilds_; }

private:
    enum class Ask : uint8_t { None, Notice, Confirm, Name };
    enum class Then : uint8_t { None, Create, Name };
    enum Target : int {
        kCreate = 0, kMenu, kEnter, kDelete,        // the bar
        kClass0, kClass1, kClass2, kClass3, kMake, kCancel, kShut,  // the create window
        kBoxOk, kBoxCancel,                          // the box
        kTargets
    };

    int hitAt(float x, float y) const;
    bool enabled(int target) const;
    gfx::Box boxOf(int target) const;
    void dismissed();
    void rebuild();

    gfx::Canvas canvas_;
    float width_ = 0.0f, height_ = 0.0f;
    View view_;
    std::vector<Seat> roster_;  // a copy, so the drawn state compares by value

    bool creating_ = false;
    int classRow_ = 1;
    std::string typed_;
    Ask box_ = Ask::None;
    Then then_ = Then::None;
    std::string said_;
    gfx::Art bust_;
    float clock_ = 0.0f;
    bool overUi_ = false;
    int over_ = -1, pressing_ = -1;
    float lift_[kTargets] = {};
    // The last click on a figure, for the double click that starts: which slot and how long ago.
    int lastClickSlot_ = -1;
    float sinceClick_ = 10.0f;

    struct Drawn {
        float width = 0, height = 0;
        int picked = -1, hovered = -1;
        std::vector<std::string> names;
        float plate[kRosterSlots][2] = {};
        bool plateSeen[kRosterSlots] = {};
        bool creating = false;
        int classRow = 1;
        std::string typed;
        Ask box = Ask::None;
        std::string said;
        bool caret = false;
        uint16_t bust = UINT16_MAX;
        int over = -1, pressing = -1;
        float lift[kTargets] = {};
        bool operator==(const Drawn& o) const;
    };
    Drawn drawn_, now_;
    bool built_ = false;
    uint64_t rebuilds_ = 0;
};

}  // namespace mu::game
