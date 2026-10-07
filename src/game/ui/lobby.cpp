#include "game/ui/lobby.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/sheet.h"
#include "game/ui/slab.h"
#include "game/ui/tip.h"
#include "sim/quests.h"

namespace mu::game {
namespace {

using gfx::Box;

// ---- the words -----------------------------------------------------------------------------
//
// MU's, verbatim from Game.en.resx by way of MsgWin.cpp and MU2's Lobby.cs.

constexpr const char* kDeleteAsk = "Would you like to delete %s character?";
constexpr const char* kDeleteName = "Please type the character's name to delete it.";

// The class descriptions, texts 1705-1708, as the create window's strip shows them. MU's own
// spelling and spacing, "a inferior" and "Lorencia.With" included.
const char* describe(sim::Kin kin) {
    switch (kin) {
        case sim::Kin::DarkWizard:
            return "Kingdom of wizards, descendant of Arka. He has a inferior physical condition "
                   "but has a enormous power and can command attacking spells freely.";
        case sim::Kin::FairyElf:
            return "Kingdom of elves, descendants of Noria. A master of arrows and bows and "
                   "commands various spells.";
        case sim::Kin::DarkKnight:
            return "Kingdom of knights, descendant of Lorencia.With a powerful strength and "
                   "swordsmanship he can handle most of the close-range weapons.";
        // Text 1708.
        case sim::Kin::MagicGladiator:
            return "Complex character that has a characteristics of the Dark knight and Dark "
                   "wizard. Master in a close-range combat and can command spells freely.";
    }
    return "";
}

// ---- the layout ----------------------------------------------------------------------------
//
// In the 1080-line pixels the menu is drawn in (tip::unit), at the menu's own 85%, so a button
// here is the size of a button there.

constexpr float kScale = 0.85f;
float unit() { return tip::unit() * kScale; }

// The bar along the foot: MU's four, Create and Menu from the left, Connect and Delete from the
// right (CharSelMainWin.cpp:141), sat on the bottom edge as MU2 sits them.
// Smaller boxes and a larger word than the slabs had: the user, 2026-09-28, "bigger font size in
// lobby bottom buttons and scale down size".
constexpr float kBarTall = 46.0f, kBarFoot = 40.0f, kBarInset = 48.0f, kBarGap = 10.0f;
constexpr float kBarWord = 20.0f;
constexpr float kCreateWide = 236.0f, kMenuWide = 116.0f, kEnterWide = 210.0f,
                kDeleteWide = 128.0f;
// How far up the screen the foot's shade reaches: MU's strip was black at alpha 143 between the
// button pairs; MU2 made it a fade up from the edge, and so does this.
constexpr float kFootShade = 230.0f;

// The create window: CCharMakeWin exactly as MuMain lays it out (CharMakeWin.cpp:148-236, the
// numbers MU2's Lobby.CreateWindow carried), in MU's own units at MU2's reference of one and a
// half to a 1080-line pixel (Lobby.PlateScale): 454x406 centred; the bust photographed into the
// top-left 410x335; the stat panel 108x80 at (346, 24); the class buttons 108x26 down from
// (346, 131); the name plate 346x38 at (0, 317) with the letters at 78; OK and Cancel 54x30 at
// (346, 325) and (400, 325); the description strip 454x51 at (0, 355). The user, 2026-09-27:
// "try to stick with MuMain actual char creation screen". What is ours is the look, and no MU art
// is drawn in it (the user: "dont use MU art in character creation, lets use our styling"): the
// slabs for MU's plates, the windows' field for its name plate, and a shaded pool the bust fades
// into instead of MU's hard-edged viewport.
constexpr float kMakeW = 454.0f, kMakeH = 406.0f;
constexpr float kBustX = 0.0f, kBustY = 0.0f, kBustW = 410.0f, kBustH = 335.0f;
constexpr float kPad = 36.0f;  // the box's own inset, in 1080-line pixels
constexpr float kRadius = 20.0f;
// Smaller than MU2's reference of one and a half: the user, 2026-09-27, "scale down char window
// size". Everything in the window is measured off this, its buttons' type included.
float mu() { return tip::unit() * 1.2f; }
// A cell of the create window, in MU's units from its corner, to pixels.
Box makeCell(float width, float height, float x, float y, float w, float h) {
    const float m = mu();
    const float left = std::floor((width - kMakeW * m) * 0.5f);
    const float top = std::floor((height - kMakeH * m) * 0.5f);
    return {std::round(left + x * m), std::round(top + y * m), std::round(w * m), std::round(h * m)};
}

// The box: a sheet in the middle, a line or two, a field for the deletion, and its buttons.
constexpr float kBoxWide = 480.0f, kBoxInner = kBoxWide - kPad * 2.0f;
constexpr float kFieldTall = 50.0f, kButtonTall = 54.0f;

constexpr float kLiftSeconds = 0.12f;
constexpr float kDoubleClick = 0.4f;

// Lines of `text` at `px` no wider than `wide`, broken at spaces.
std::vector<std::string> wrapped(const gfx::Face& face, float px, float wide,
                                 const std::string& text) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush = [&]() {
        if (word.empty()) return;
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && face.measure(px, trial) > wide) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

float boxTall(bool field) { return 186.0f + (field ? 66.0f : 0.0f); }

// The field a name is typed into: a dark rounded well, the letters, and a caret while it blinks.
void field(gfx::Canvas& canvas, const Box& box, float u, const std::string& text, bool caret,
           bool masked = false) {
    const float line = std::max(1.0f, std::round(u));
    tip::panel(canvas, box, 10.0f * u, slab::kBronze.times(0.30f).packed(),
               slab::kBronze.times(0.16f).packed());
    tip::panel(canvas, box.grown(-line), 10.0f * u - line, gfx::rgba(0.040f, 0.037f, 0.034f, 1.0f),
               gfx::rgba(0.055f, 0.051f, 0.047f, 1.0f));
    const gfx::Face& face = canvas.face();
    const float px = 19.0f * u;
    const float baseline = tip::middle(face, box.y, box.h, px);
    const std::string shown = masked ? std::string(text.size(), '*') : text;
    const float x = box.x + 16.0f * u;
    sheet::printed(canvas, x, baseline, px, sheet::ink::kFigure, shown);
    if (caret) {
        const float at = x + face.measure(px, shown) + 2.0f * u;
        canvas.rect({std::round(at), std::round(box.y + box.h * 0.26f), std::max(1.0f, 2.0f * u),
                     std::round(box.h * 0.48f)},
                    sheet::ink::kTitle);
    }
}

}  // namespace

bool Lobby::Drawn::operator==(const Drawn& o) const {
    if (width != o.width || height != o.height || picked != o.picked || hovered != o.hovered ||
        names != o.names || creating != o.creating || classRow != o.classRow ||
        typed != o.typed || box != o.box || said != o.said || caret != o.caret ||
        bust != o.bust ||
        over != o.over || pressing != o.pressing) {
        return false;
    }
    for (int s = 0; s < kRosterSlots; ++s) {
        if (plateSeen[s] != o.plateSeen[s] || plate[s][0] != o.plate[s][0] ||
            plate[s][1] != o.plate[s][1]) {
            return false;
        }
    }
    for (int i = 0; i < kTargets; ++i) {
        if (lift[i] != o.lift[i]) return false;
    }
    return true;
}

void Lobby::open(const gfx::Interface& interface) { interface.adopt(canvas_); }

gfx::Box Lobby::bustBox(float width, float height) {
    return makeCell(width, height, kBustX, kBustY, kBustW, kBustH);
}

void Lobby::notice(const std::string& text, bool thenCreate, bool thenName) {
    box_ = Ask::Notice;
    said_ = text;
    then_ = thenCreate ? Then::Create : (thenName ? Then::Name : Then::None);
    creating_ = false;
    over_ = pressing_ = -1;
}

void Lobby::openCreate(int classRow) {
    creating_ = true;
    classRow_ = std::clamp(classRow, 0, 2);
    over_ = pressing_ = -1;
}

void Lobby::askDelete() {
    box_ = Ask::Confirm;
    over_ = pressing_ = -1;
}

void Lobby::dismissed() {
    const Then then = then_;
    box_ = Ask::None;
    then_ = Then::None;
    if (then == Then::Create) {
        creating_ = true;
    } else if (then == Then::Name) {
        box_ = Ask::Name;
        typed_.clear();
    }
}

Box Lobby::boxOf(int target) const {
    // In pixels, laid out from this frame's size.
    const float u = unit();
    const float w = width_, h = height_;
    const auto px = [&](float x, float y, float bw, float bh) {
        return Box{std::round(x), std::round(y), std::round(bw * u), std::round(bh * u)};
    };
    const float barY = h - (kBarFoot + kBarTall) * u;
    switch (target) {
        case kCreate: return px(kBarInset * u, barY, kCreateWide, kBarTall);
        case kMenu: return px((kBarInset + kCreateWide + kBarGap) * u, barY, kMenuWide, kBarTall);
        case kDelete: return px(w - (kBarInset + kDeleteWide) * u, barY, kDeleteWide, kBarTall);
        case kEnter:
            return px(w - (kBarInset + kDeleteWide + kBarGap + kEnterWide) * u, barY, kEnterWide,
                      kBarTall);
        default: break;
    }
    switch (target) {
        case kClass0:
        case kClass1:
        case kClass2:
            return makeCell(w, h, 346.0f, 131.0f + float(target - kClass0) * 26.0f, 108.0f, 26.0f);
        // Inside the name bar (see the drawing), at the input's own height.
        case kMake: return makeCell(w, h, 344.0f, 322.0f, 52.0f, 28.0f);
        case kCancel: return makeCell(w, h, 399.0f, 322.0f, 51.0f, 28.0f);
        case kShut: return {};  // MU's window has none: Cancel and Escape shut it
        default: break;
    }
    const bool withField = box_ == Ask::Name;
    const float tall = boxTall(withField);
    const float boxX = std::round((w - kBoxWide * u) * 0.5f);
    const float boxY = std::round(h * 0.44f - tall * u * 0.5f);
    const float buttonsY = boxY + (tall - kButtonTall - 26.0f) * u;
    const bool pair = box_ != Ask::Notice;
    switch (target) {
        case kBoxOk:
            return pair ? px(boxX + kPad * u, buttonsY, (kBoxInner - 10.0f) * 0.5f, kButtonTall)
                        : px(boxX + (kBoxWide - 180.0f) * 0.5f * u, buttonsY, 180.0f, kButtonTall);
        case kBoxCancel:
            return pair ? px(boxX + (kPad + (kBoxInner + 10.0f) * 0.5f) * u, buttonsY,
                             (kBoxInner - 10.0f) * 0.5f, kButtonTall)
                        : Box{};
        default: break;
    }
    return {};
}

bool Lobby::enabled(int target) const {
    const int count = view_.roster ? int(view_.roster->size()) : 0;
    switch (target) {
        case kCreate: return count < kRosterSlots;
        case kMenu: return true;
        case kEnter:
        case kDelete: return view_.picked >= 0 && view_.picked < kRosterSlots;
        default: return true;
    }
}

int Lobby::hitAt(float x, float y) const {
    const auto in = [&](int target) {
        const Box box = boxOf(target);
        return box.w > 0.0f && box.has(x, y);
    };
    if (box_ != Ask::None) {
        for (int t : {kBoxOk, kBoxCancel}) {
            if (in(t)) return t;
        }
        return -1;
    }
    if (creating_) {
        for (int t = kClass0; t <= kShut; ++t) {
            if (in(t)) return t;
        }
    }
    for (int t = kCreate; t <= kDelete; ++t) {
        if (in(t) && enabled(t)) return t;
    }
    return -1;
}

void Lobby::update(float seconds, float width, float height, const Pointer& pointer,
                   const std::string& typed, int backspaces, bool enter, bool escape,
                   const View& view, Result* out) {
    Result result;
    width_ = width;
    height_ = height;
    view_ = view;
    roster_ = view.roster ? *view.roster : std::vector<Seat>{};
    view_.roster = &roster_;
    clock_ += seconds;
    sinceClick_ += seconds;

    over_ = hitAt(pointer.x, pointer.y);
    // Whether the pointer is on the screen's own furniture, so the figures do not hear it: the
    // create window's whole sheet, and every bar button whether or not it answers.
    {
        const Box make = makeCell(width, height, 0.0f, 0.0f, kMakeW, kMakeH);
        overUi_ = over_ >= 0 || (creating_ && make.has(pointer.x, pointer.y));
        for (int t = kCreate; t <= kDelete && !overUi_; ++t) {
            if (boxOf(t).has(pointer.x, pointer.y)) overUi_ = true;
        }
    }
    const float step = seconds / kLiftSeconds;
    for (int i = 0; i < kTargets; ++i) {
        lift_[i] = over_ == i ? std::min(1.0f, lift_[i] + step) : std::max(0.0f, lift_[i] - step);
    }
    if (pointer.pressed) pressing_ = over_;
    int fired = -1;
    if (pointer.released) {
        if (pressing_ >= 0 && pressing_ == over_) fired = pressing_;
        pressing_ = -1;
    }

    const auto create = [&]() {
        // RequestCreateCharacter: the length, then the symbols, then the ask.
        std::string name = typed_;
        if (name.size() < 4) {
            notice("Type more than 4 letters", true);
            result.refused = true;
            return;
        }
        for (const char c : name) {
            if (!std::isalnum(static_cast<unsigned char>(c))) {
                notice("Cannot use symbols.", true);
                result.refused = true;
                return;
            }
        }
        creating_ = false;
        result.create = true;
        result.name = name;
        result.kin = kClasses[classRow_];
    };
    const auto deleteNamed = [&]() {
        const int slot = view_.picked;
        const Seat* who = nullptr;
        for (const Seat& one : roster_) {
            if (one.slot == slot) who = &one;
        }
        if (!who) {
            box_ = Ask::None;
            return;
        }
        std::string a = typed_, b = who->name;
        for (char& c : a) c = char(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : b) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (a != b) {
            notice("The name you have entered is not this character's.", false, true);
            result.refused = true;
            return;
        }
        box_ = Ask::None;
        result.drop = slot;
    };

    // The keys first: they belong to whatever is on top.
    const auto typeInto = [&]() {
        for (int i = 0; i < backspaces && !typed_.empty(); ++i) typed_.pop_back();
        for (const char c : typed) {
            if (int(typed_.size()) < kNameLetters && c >= 32 && c < 127) typed_ += c;
        }
    };
    if (box_ == Ask::Name) {
        typeInto();
    } else if (creating_ && box_ == Ask::None) {
        typeInto();
    }
    if (escape) {
        // Shut whatever is up, with its click; with nothing up, the menu.
        result.clicked = true;
        if (box_ != Ask::None) {
            box_ = Ask::None;
            then_ = Then::None;
        } else if (creating_) {
            creating_ = false;
        } else {
            result.menu = true;
        }
    } else if (enter) {
        if (box_ == Ask::Notice) {
            result.clicked = true;
            dismissed();
        } else if (box_ == Ask::Confirm) {
            result.clicked = true;
            box_ = Ask::Name;
            typed_.clear();
        } else if (box_ == Ask::Name) {
            result.clicked = true;
            deleteNamed();
        } else if (creating_) {
            result.clicked = true;
            create();
        } else if (view_.picked >= 0) {
            result.clicked = true;
            result.enter = view_.picked;
        }
    } else if (fired >= 0) {
        result.clicked = true;
        switch (fired) {
            case kCreate:
                typed_.clear();
                openCreate(1);
                break;
            case kMenu: result.menu = true; break;
            case kEnter: result.enter = view_.picked; break;
            case kDelete: askDelete(); break;
            case kClass0:
            case kClass1:
            case kClass2: classRow_ = fired - kClass0; break;
            case kMake: create(); break;
            case kCancel:
            case kShut: creating_ = false; break;
            case kBoxOk:
                if (box_ == Ask::Notice) dismissed();
                else if (box_ == Ask::Confirm) {
                    box_ = Ask::Name;
                    typed_.clear();
                } else if (box_ == Ask::Name) {
                    deleteNamed();
                }
                break;
            case kBoxCancel:
                box_ = Ask::None;
                then_ = Then::None;
                break;
            default: break;
        }
    } else if (pointer.pressed && over_ < 0 && !takesPointer() && !creating_) {
        // NewMoveCharacterScene: a click picks or drops the pick, a double click starts.
        const int slot = view_.hovered;
        if (slot >= 0 && slot == lastClickSlot_ && sinceClick_ < kDoubleClick) {
            result.pick = slot;
            result.enter = slot;
            lastClickSlot_ = -1;
        } else {
            result.pick = slot;
            lastClickSlot_ = slot;
            sinceClick_ = 0.0f;
        }
    }
    if (out) *out = result;

    now_ = Drawn{};
    now_.width = width_;
    now_.height = height_;
    now_.picked = view_.picked;
    now_.hovered = view_.hovered;
    for (const Seat& one : roster_) {
        now_.names.push_back(std::to_string(one.slot) + one.name + ":" + std::to_string(one.level) +
                             ":" + std::to_string(int(one.kin)));
    }
    std::copy(&view_.plate[0][0], &view_.plate[0][0] + kRosterSlots * 2, &now_.plate[0][0]);
    std::copy(view_.plateSeen, view_.plateSeen + kRosterSlots, now_.plateSeen);
    now_.creating = creating_;
    now_.classRow = classRow_;
    now_.typed = typed_;
    now_.box = box_;
    now_.said = said_;
    now_.bust = bust_.handle.idx;
    now_.caret = typing() && std::fmod(clock_, 1.0f) < 0.55f;
    now_.over = over_;
    now_.pressing = pressing_;
    std::copy(std::begin(lift_), std::end(lift_), std::begin(now_.lift));
    if (built_ && now_ == drawn_) return;
    drawn_ = now_;
    built_ = true;
    rebuild();
}

void Lobby::rebuild() {
    canvas_.clear();
    ++rebuilds_;
    const float u = unit();
    const float w = width_, h = height_;
    const float line = std::max(1.0f, std::round(u));
    const gfx::Face& face = canvas_.face();
    const auto lit = [&](int t) { return slab::eased(lift_[t]); };
    const auto held = [&](int t) { return pressing_ == t && over_ == t; };

    // A window puts the scene behind glass: MU2's Veil, black at 165 -- not MU's, whose windows
    // were drawn straight over the scene, but here the window is a third of the frame and the set
    // behind it is lit.
    if (creating_ || box_ != Ask::None) canvas_.rect({0.0f, 0.0f, w, h}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.65f));

    // ---- the foot's shade, under the bar -------------------------------------------------
    {
        const float tall = kFootShade * u;
        const uint32_t dark = gfx::rgba(0.0f, 0.0f, 0.0f, 0.62f), none = gfx::rgba(0.0f, 0.0f, 0.0f, 0.0f);
        canvas_.shade({0.0f, h - tall, w, tall}, none, none, dark, dark);
    }

    // ---- the name plates (CCharInfoBalloon) ----------------------------------------------
    // Hung by the bottom middle over each figure: the name, and the class and level under it.
    // MU prints the guild status between them, "(Commoner)" for none; there are no guilds here
    // and the line is left out.
    for (const Seat& one : roster_) {
        if (one.slot < 0 || one.slot >= kRosterSlots || !view_.plateSeen[one.slot]) continue;
        const bool picked = view_.picked == one.slot;
        const bool hovered = view_.hovered == one.slot && !picked;
        const float namePx = 17.0f * u, linePx = 12.5f * u;
        const std::string name = one.name;
        const std::string sub = "LEVEL " + std::to_string(one.level) + "  " +
                                sheet::shouted(sim::className(int(one.kin), one.second));
        const gfx::Face* gothic = panel::titleFace();
        const gfx::Face& nf = gothic ? *gothic : face;
        const float nameTrack = namePx * 0.06f;
        const float nameWide = nf.measure(namePx, name) + nameTrack * float(name.size() - 1);
        const float subWide = tip::trackedWidth(face, linePx, 0.12f, sub);
        const float plateW = std::round(std::max(nameWide, subWide) + 36.0f * u);
        const float plateH = std::round(56.0f * u);
        const Box plate{std::round(view_.plate[one.slot][0] - plateW * 0.5f),
                        std::round(view_.plate[one.slot][1] - plateH), plateW, plateH};
        tip::shadowUnder(canvas_, plate, 0.6f, 12.0f * u + line);
        const slab::Tone rim = picked ? slab::kPale.times(0.85f)
                               : hovered ? slab::kBronze.times(0.65f)
                                         : slab::kBronze.times(0.38f);
        tip::panel(canvas_, plate.grown(line), 12.0f * u + line, rim.packed(),
                   rim.times(0.4f).packed());
        tip::panel(canvas_, plate, 12.0f * u, gfx::rgba(0.075f, 0.069f, 0.062f, 0.93f),
                   gfx::rgba(0.035f, 0.032f, 0.029f, 0.93f));
        const float nameBase = std::round(plate.y + 25.0f * u);
        const float nameX = std::round(plate.midX() - nameWide * 0.5f);
        const uint32_t nameInk = picked ? sheet::ink::kTitle : sheet::ink::kFigure;
        if (gothic) {
            canvas_.lettered(nf, panel::titleTexture(), nameX + 1.0f, nameBase + 1.0f, namePx,
                             nameTrack, tip::ink::kDrop, name);
            canvas_.lettered(nf, panel::titleTexture(), nameX, nameBase, namePx, nameTrack,
                             nameInk, name);
        } else {
            sheet::printed(canvas_, nameX, nameBase, namePx, nameInk, name);
        }
        // "class level" in orange, as MU prints it.
        tip::tracked(canvas_, std::round(plate.midX() - subWide * 0.5f),
                     std::round(plate.y + 45.0f * u), linePx, 0.12f,
                     picked ? sheet::ink::kGold : sheet::ink::kMark, sub, 1.0f);
    }

    // ---- the bar (CCharSelMainWin) --------------------------------------------------------
    {
        // Sanctuary's buttons since 2026-09-28 (game/ui/controls.h), in the slabs' own places:
        // Enter World the primary, Delete the danger, and each inactive while it cannot answer.
        const auto bar = [&](int t, const char* word, controls::Kind kind) {
            controls::button(canvas_, boxOf(t), word, kind,
                             {lit(t), held(t), !enabled(t)}, u, kBarWord * u);
        };
        bar(kCreate, "Create Character", controls::Kind::Secondary);
        bar(kMenu, "Menu", controls::Kind::Secondary);
        bar(kEnter, "Enter World", controls::Kind::Primary);
        bar(kDelete, "Delete", controls::Kind::Danger);
    }

    // ---- the create window (CCharMakeWin) --------------------------------------------------
    if (creating_) {
        const float m = mu();
        // The window's own pixel unit, which the slabs and corners inside it are drawn at.
        const float k = m / 1.5f;
        const auto cell = [&](float x, float y, float cw, float ch) {
            return makeCell(w, h, x, y, cw, ch);
        };
        const uint32_t strip = gfx::rgba(0.0f, 0.0f, 0.0f, 143.0f / 255.0f);  // MU's own black
        // The bust's corner, ours: a pool of shade under it that fades out toward every edge,
        // so the bust sits in the dimmed scene rather than on a card, and the bust itself faded
        // where its picture ends -- its left edge and its foot -- instead of cut off there.
        const Box well = cell(kBustX, kBustY, kBustW, kBustH);
        const float plateTop = cell(0.0f, 317.0f, 0.0f, 0.0f).y;
        // One backdrop for the whole window, and no box anywhere: a linear shadow, nothing above
        // the head and darkest behind the name bar and the description, the window's full width
        // with both sides and its foot feathered out -- the user, "gradient bg behind char has to
        // be linear, darker at bottom and transparent at top", then "not well integrated /
        // cropped" when it was the bust's corner alone and stopped short of the window's width.
        {
            const Box whole = cell(0.0f, 0.0f, kMakeW, kMakeH);
            const uint32_t clear = gfx::rgba(0.0f, 0.0f, 0.0f, 0.0f);
            const uint32_t deep = gfx::rgba(0.0f, 0.0f, 0.0f, 0.82f);
            const float feather = 60.0f * m;
            const float top = whole.y + 10.0f * m;
            const float ramp = plateTop - top;
            const float rest = whole.bottom() + 8.0f * m - plateTop;
            const float tail = 30.0f * m;
            const float x0 = whole.x - feather * 0.5f;
            const float core = whole.w - feather;
            const float x1 = x0 + feather + core;
            // Three rows -- the ramp, the solid band, the tail -- each a core and two feathers.
            canvas_.shade({x0 + feather, top, core, ramp}, clear, clear, deep, deep);
            canvas_.shade({x0, top, feather, ramp}, clear, clear, deep, clear);
            canvas_.shade({x1, top, feather, ramp}, clear, clear, clear, deep);
            canvas_.shade({x0 + feather, plateTop, core, rest}, deep, deep, deep, deep);
            canvas_.shade({x0, plateTop, feather, rest}, clear, deep, deep, clear);
            canvas_.shade({x1, plateTop, feather, rest}, deep, clear, clear, deep);
            const float y2 = plateTop + rest;
            canvas_.shade({x0 + feather, y2, core, tail}, deep, deep, clear, clear);
            canvas_.shade({x0, y2, feather, tail}, clear, deep, clear, clear);
            canvas_.shade({x1, y2, feather, tail}, deep, clear, clear, clear);
        }
        if (bust_.valid()) {
            const float shown = std::max(0.0f, plateTop - well.y);
            canvas_.region(bust_, {well.x, well.y, well.w, shown},
                           {0.0f, 0.0f, bust_.width, bust_.height * shown / well.h});
            const uint32_t clear = gfx::rgba(0.0f, 0.0f, 0.0f, 0.0f);
            const uint32_t dark = gfx::rgba(0.012f, 0.011f, 0.010f, 0.97f);
            // Only its foot is faded, into the shadow under it: a fade over the left edge or the
            // top darkens the scene behind as much as the bust, and drew the corner as a box.
            const float foot = shown * 0.26f;
            canvas_.shade({well.x - 1.0f, plateTop - foot, well.w + 1.0f, foot + 1.0f}, clear,
                          clear, dark, dark);
        }

        // The stat panel, black at 143, the labels at +22, +10 down and seventeen a line, the
        // values at +76 in orange.
        const Box stats = cell(346.0f, 24.0f, 108.0f, 80.0f);
        tip::panel(canvas_, stats, 6.0f * k, strip, strip);
        const sim::HeroPoints points = sim::startingPoints(kClasses[classRow_]);
        const char* names[4] = {"Strength", "Agility", "Vitality", "Energy"};
        const int values[4] = {points.strength, points.agility, points.vitality, points.energy};
        const float px = 9.0f * m;
        for (int i = 0; i < 4; ++i) {
            const float baseline =
                std::round(stats.y + (10.0f + float(i) * 17.0f) * m + face.ascent(px));
            sheet::printed(canvas_, stats.x + 14.0f * m, baseline, px, sheet::ink::kFigure,
                           names[i]);
            sheet::ranged(canvas_, stats.right() - 12.0f * m, baseline, px, sheet::ink::kGold,
                          std::to_string(values[i]));
        }

        // The class buttons, the chosen one the primary's red.
        for (int row = 0; row < 3; ++row) {
            const int t = kClass0 + row;
            controls::button(canvas_, boxOf(t).grown(-std::round(1.5f * k)), className(kClasses[row]),
                             row == classRow_ ? controls::Kind::Primary : controls::Kind::Secondary,
                             {lit(t), held(t), false}, k);
        }

        // The name row, one container across the window where MU's plate and its two buttons
        // stand (0 to 454 at 317, 38 tall): the word, the input and OK and Cancel inside it, all
        // at one height and on one line -- the user, "one container with aligned input and
        // buttons".
        {
            const Box bar = cell(0.0f, 317.0f, 454.0f, 38.0f);
            const float br = 10.0f * k;
            tip::panel(canvas_, bar.grown(line), br + line, slab::kBronze.times(0.42f).packed(),
                       slab::kBronze.times(0.16f).packed());
            tip::panel(canvas_, bar, br, gfx::rgba(0.070f, 0.064f, 0.058f, 0.96f),
                       gfx::rgba(0.040f, 0.037f, 0.034f, 0.96f));
            sheet::kicker(canvas_, bar.x + 12.0f * m, tip::middle(face, bar.y, bar.h, 8.0f * m),
                          8.0f * m, "NAME");
            const Box input = cell(50.0f, 322.0f, 290.0f, 28.0f);
            // The input sunk into the bar: darker, a hairline, the letters and the caret.
            tip::panel(canvas_, input, 7.0f * k, gfx::rgba(0.0f, 0.0f, 0.0f, 0.55f),
                       gfx::rgba(0.0f, 0.0f, 0.0f, 0.40f));
            canvas_.outline(input, line, gfx::rgba(1.0f, 1.0f, 1.0f, 0.06f));
            const float px = 12.0f * m;
            const float baseline = tip::middle(face, input.y, input.h, px);
            const float x = input.x + 10.0f * m;
            sheet::printed(canvas_, x, baseline, px, sheet::ink::kFigure, typed_);
            if (drawn_.caret && box_ == Ask::None) {
                const float at = x + face.measure(px, typed_) + 2.0f * k;
                canvas_.rect({std::round(at), std::round(input.y + input.h * 0.22f),
                              std::max(1.0f, 2.0f * k), std::round(input.h * 0.56f)},
                             sheet::ink::kTitle);
            }
        }
        controls::button(canvas_, boxOf(kMake), "OK", controls::Kind::Primary,
                         {lit(kMake), held(kMake), false}, k);
        controls::button(canvas_, boxOf(kCancel), "Cancel", controls::Kind::Secondary,
                         {lit(kCancel), held(kCancel), false}, k);

        // The description strip, black at 143, the text from (10, 12).
        // Straight on the backdrop, where MU's black strip was: a box of its own here was one
        // more edge in a window that should have none.
        const Box desc = cell(0.0f, 355.0f, 454.0f, 51.0f);
        {
            const float about = 9.0f * m;
            const std::vector<std::string> lines =
                wrapped(face, about, desc.w - 20.0f * m, describe(kClasses[classRow_]));
            for (size_t i = 0; i < lines.size() && i < 2; ++i) {
                sheet::printed(canvas_, desc.x + 10.0f * m,
                               std::round(desc.y + 10.0f * m + face.ascent(about) +
                                          float(i) * 15.0f * m),
                               about, sheet::ink::kFigure, lines[i]);
            }
        }
    }

    // ---- the box (MsgWin) -------------------------------------------------------------------
    if (box_ != Ask::None) {
        const bool withField = box_ == Ask::Name;
        const float tall = boxTall(withField);
        const Box sheetBox{std::round((w - kBoxWide * u) * 0.5f),
                           std::round(h * 0.44f - tall * u * 0.5f), std::round(kBoxWide * u),
                           std::round(tall * u)};
        sheet::glass(canvas_, sheetBox, kRadius * u);
        sheet::band(canvas_, {sheetBox.x, sheetBox.y, sheetBox.w, std::round(80.0f * u)}, true,
                    kRadius * u);
        std::string text = said_;
        if (box_ == Ask::Confirm || box_ == Ask::Name) {
            std::string who;
            for (const Seat& one : roster_) {
                if (one.slot == view_.picked) who = one.name;
            }
            if (box_ == Ask::Confirm) {
                char buffer[160];
                std::snprintf(buffer, sizeof buffer, kDeleteAsk, who.c_str());
                text = buffer;
            } else {
                text = kDeleteName;
            }
        }
        const float px = 17.0f * u;
        const std::vector<std::string> lines = wrapped(face, px, kBoxInner * u, text);
        const float lineTall = 25.0f * u;
        const float textTop = sheetBox.y + 34.0f * u;
        for (size_t i = 0; i < lines.size() && i < 3; ++i) {
            const float wide = face.measure(px, lines[i]);
            sheet::printed(canvas_, std::round(sheetBox.midX() - wide * 0.5f),
                           std::round(textTop + 18.0f * u + float(i) * lineTall), px,
                           sheet::ink::kFigure, lines[i]);
        }
        if (withField) {
            field(canvas_, {std::round(sheetBox.x + kPad * u), std::round(sheetBox.y + 100.0f * u),
                            std::round(kBoxInner * u), std::round(kFieldTall * u)},
                  u, typed_, drawn_.caret);
        }
        const bool pair = box_ != Ask::Notice;
        // The answer the box expects is the primary; the deletion's last word is a danger, and
        // never the primary, so Return alone cannot delete a character.
        controls::button(canvas_, boxOf(kBoxOk), box_ == Ask::Confirm ? "Yes" : "OK",
                         box_ == Ask::Name ? controls::Kind::Danger : controls::Kind::Primary,
                         {lit(kBoxOk), held(kBoxOk), false}, u);
        if (pair) {
            controls::button(canvas_, boxOf(kBoxCancel), box_ == Ask::Confirm ? "No" : "Cancel",
                             controls::Kind::Secondary, {lit(kBoxCancel), held(kBoxCancel), false}, u);
        }
    }
}

}  // namespace mu::game
