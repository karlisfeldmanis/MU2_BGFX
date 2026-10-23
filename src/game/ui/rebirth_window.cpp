#include "game/ui/rebirth_window.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/rebirth.h"
#include "sim/talents.h"

namespace mu::game {
namespace {

using gfx::Box;

constexpr float kWide = panel::kWidth;
constexpr float kLeft = panel::kEdge, kRight = kWide - panel::kEdge;
constexpr float kInner = kRight - kLeft;
constexpr float kText = 7.6f;  // a line, in units, as the Jeweler's
constexpr float kLine = 11.5f;
constexpr float kWords = 8.2f;  // Lugard's words, a little larger: they are read
constexpr float kLead = 11.0f;
constexpr float kParagraph = 5.0f;

// The heading under the frame's band, his words, and the rite's card down to the foot.
constexpr float kHeadTop = 44.0f;
constexpr float kWordsTop = 82.0f;
// The card stops short of the foot: the line said over it (the ask, a refusal) has its own
// strip above the rule, kFootRule - 13 (the user, 2026-10-22: it lay over the card's "Needs").
constexpr float kCardTop = 222.0f, kCardBottom = 362.0f;
constexpr float kCardPad = 7.0f;
constexpr float kRow = 11.0f;

constexpr float kButtonTop = panel::kFootTop + 2.0f, kButtonH = 22.0f;
constexpr float kButtonW = (kInner - 6.0f) * 0.5f;
constexpr Box kLeaveBox{kLeft, kButtonTop, kButtonW, kButtonH};
constexpr Box kRunBox{kRight - kButtonW, kButtonTop, kButtonW, kButtonH};
constexpr Box kNoBox = kLeaveBox;
constexpr Box kYesBox = kRunBox;

// The canvas's inks: the kicker's tan, the card's floor and rim, kept's green, the arrow's gold.
constexpr uint32_t kTan = gfx::rgba(0.659f, 0.545f, 0.424f);
constexpr uint32_t kGold = gfx::rgba(0.788f, 0.643f, 0.361f);
constexpr uint32_t kCardFloor = gfx::rgba(0.051f, 0.039f, 0.031f);
constexpr uint32_t kCardRim = gfx::rgba(0.239f, 0.192f, 0.157f);
constexpr uint32_t kCardRule = gfx::rgba(0.141f, 0.106f, 0.082f);
constexpr uint32_t kKept = gfx::rgba(0.431f, 0.620f, 0.345f);
constexpr uint32_t kShort = gfx::rgba(0.878f, 0.478f, 0.424f);

// Lugard's words: "The First Rebirth" (the lore draft of 2026-10-22, the canvas's three lines).
const char* const kReady[] = {
    "\"I know that weight. Every blow lands as hard as the last, and the world has stopped "
    "growing around you.\"",
    "\"I stood where you stand. The heavens offered me what I offer you now, and I said no. Look "
    "at what it cost me.\"",
    "\"Kneel in the light. It will take your body back to its first day, but your soul keeps "
    "everything it has learned.\"",
};
const char* const kNotReady = "\"You are not yet at the edge. Come back when your body can take no more.\"";
const char* const kAgain =
    "\"Again? Good. The light remembers you. Kneel, and it will take your body back once more.\"";

std::string ordinal(int n) {
    static const char* const kWords[] = {"First", "Second", "Third", "Fourth", "Fifth",
                                         "Sixth", "Seventh", "Eighth", "Ninth", "Tenth"};
    if (n >= 1 && n <= 10) return kWords[n - 1];
    const int tens = n % 100;
    const char* suffix = (tens >= 11 && tens <= 13) ? "th"
                         : n % 10 == 1               ? "st"
                         : n % 10 == 2               ? "nd"
                         : n % 10 == 3               ? "rd"
                                                     : "th";
    return std::to_string(n) + suffix;
}

void wrap(const std::string& text, float size, float wide, std::vector<std::string>& out) {
    std::string now, word;
    const auto flush = [&]() {
        if (word.empty()) return;
        const std::string tried = now.empty() ? word : now + " " + word;
        if (!now.empty() && controls::labelWidth(size, tried) > wide) {
            out.push_back(now);
            now = word;
        } else {
            now = tried;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!now.empty()) out.push_back(now);
}

}  // namespace

void RebirthWindow::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    arts_ = arts;
}

bool RebirthWindow::covers(float x, float y) const {
    return up_ && Box{x_, y_, kWide * panel::scale(), panel::kHeight * panel::scale()}.has(x, y);
}

void RebirthWindow::cancel() {
    confirming_ = waiting_ = false;
    said_.clear();
    pressing_ = over_ = kNone;
}

void RebirthWindow::answered(bool ok, const std::string& refusal) {
    waiting_ = false;
    confirming_ = false;
    if (!ok) said_ = refusal.empty() ? "Refused." : "Refused: " + refusal + ".";
}

int RebirthWindow::hitAt(float ux, float uy) const {
    if (confirming_) {
        if (kYesBox.has(ux, uy)) return kYes;
        if (kNoBox.has(ux, uy)) return kNo;
        return kNone;
    }
    if (kRunBox.has(ux, uy)) return kRun;
    if (kLeaveBox.has(ux, uy)) return kLeave;
    return kNone;
}

void RebirthWindow::update(float width, float height, const sim::Realm& realm, const Pointer& pointer,
                           RebirthRequests* out) {
    (void)width;
    up_ = realm.rebirthing() >= 0 && realm.tables();
    if (!up_) {
        cancel();
        return;
    }
    // The quest dialog's place: the left column (game/ui/quest_dialog.cpp).
    x_ = panel::sideMargin();
    y_ = std::round(panel::panelY(height));
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool inside = covers(pointer.x, pointer.y);
    over_ = inside ? hitAt(ux, uy) : kNone;
    overClose_ = inside && panel::frameClose().has(ux, uy);

    const sim::Body& hero = realm.hero();
    const bool ready = hero.level >= sim::kRebirthLevel && realm.money() >= sim::rebirthZen(hero.rebirths + 1) &&
                       realm.rebirthStrips() >= 0;

    if (pointer.pressed && inside) {
        if (overClose_) closing_ = true;
        else pressing_ = over_;
    }
    if (pointer.released) {
        if (out && closing_ && overClose_) out->close = true;
        if (out && pressing_ != kNone && pressing_ == over_) {
            switch (pressing_) {
                case kLeave: out->close = true; break;
                case kNo:
                    confirming_ = false;
                    out->click = true;
                    break;
                case kRun:
                    if (ready && !waiting_) {
                        said_.clear();
                        confirming_ = true;
                        out->click = true;
                    }
                    break;
                case kYes:
                    if (ready && !waiting_) {
                        waiting_ = true;
                        out->reborn = true;
                    }
                    break;
                default: break;
            }
        }
        closing_ = false;
        pressing_ = kNone;
    }

    const std::string now = key(realm);
    if (now == drawn_ && rebuilds_ > 0) return;
    drawn_ = now;
    rebuild(realm);
}

std::string RebirthWindow::key(const sim::Realm& realm) const {
    const sim::Body& hero = realm.hero();
    std::string s;
    const auto add = [&](long long v) { s += std::to_string(v) + ","; };
    add(hero.level);
    add(hero.rebirths);
    add(realm.money());
    add(realm.satchel().version());
    add(confirming_);
    add(waiting_);
    add(over_);
    add(pressing_);
    add(overClose_);
    add(closing_);
    add(std::lround(x_));
    add(std::lround(y_));
    add(std::lround(panel::scale() * 1000.0f));
    s += said_;
    return s;
}

void RebirthWindow::rebuild(const sim::Realm& realm) {
    ++rebuilds_;
    canvas_.clear();
    if (!arts_ || !up_) return;
    const sim::Body& hero = realm.hero();
    const float x = x_, y = y_, k = panel::scale();
    const float u = tip::unit();
    const float text = kText * k * 1.1f;
    const auto P = [&](const Box& units) { return panel::scaled(x, y, units); };
    const auto base = [&](float top, float tall, float size) { return controls::middle(y + top * k, tall * k, size); };
    const auto at = [&](float left, float top, uint32_t ink, const std::string& s, float size = 0.0f) {
        const float sz = size > 0.0f ? size : text;
        controls::label(canvas_, x + left * k, base(top, kLine, sz), sz, ink, s);
    };
    const auto right = [&](float edge, float top, uint32_t ink, const std::string& s) {
        controls::ranged(canvas_, x + edge * k, base(top, kLine, text), text, ink, s);
    };
    const auto centred = [&](float top, uint32_t ink, const std::string& s, float size) {
        const float w = controls::labelWidth(size, s);
        controls::label(canvas_, x + kWide * 0.5f * k - w * 0.5f, base(top, kLine, size), size, ink, s);
    };
    const auto kicker = [&](float left, float top, uint32_t ink, const std::string& s) {
        controls::caps(canvas_, x + left * k, base(top, kLine, text * 0.85f), text * 0.85f, ink, s);
    };
    const auto stateOf = [&](int which, bool off) {
        controls::State state;
        state.lift = over_ == which && !off ? 1.0f : 0.0f;
        state.held = pressing_ == which && over_ == which && !off;
        state.off = off;
        return state;
    };

    const int next = hero.rebirths + 1;
    const int64_t zen = sim::rebirthZen(next);
    const bool levelled = hero.level >= sim::kRebirthLevel;
    const bool paid = realm.money() >= zen;
    const int strips = realm.rebirthStrips();
    const bool ready = levelled && paid && strips >= 0;

    controls::frame(canvas_, P({0.0f, 0.0f, kWide, panel::kHeight}), u, "Rebirth", nullptr, panel::kHeadBand * k);

    // ---- the heading -----------------------------------------------------------------------------
    centred(kHeadTop, style::kBoneHi, "The " + ordinal(next) + " Rebirth", 10.5f * k);
    centred(kHeadTop + 13.0f, style::kAshInk, "Devias, before Lugard", text * 0.9f);

    // ---- his words ---------------------------------------------------------------------------------
    kicker(kLeft, kWordsTop - 12.0f, kTan, "Lugard, the Fallen Knight");
    {
        const float size = kWords * k;
        std::vector<const char*> said;
        if (!levelled) said = {kNotReady};
        else if (hero.rebirths > 0) said = {kAgain};
        else said = {kReady[0], kReady[1], kReady[2]};
        // As many paragraphs as fit over the card, dropping the middle one first.
        std::vector<std::string> lines;
        const auto lay = [&](const std::vector<const char*>& paragraphs) {
            lines.clear();
            for (const char* one : paragraphs) {
                if (!lines.empty()) lines.push_back("");
                wrap(one, size, kInner * k, lines);
            }
            float tall = 0.0f;
            for (const std::string& one : lines) tall += one.empty() ? kParagraph : kLead;
            return tall;
        };
        if (lay(said) > kCardTop - 8.0f - kWordsTop && said.size() == 3) lay({kReady[0], kReady[2]});
        float ty = kWordsTop;
        for (const std::string& one : lines) {
            if (one.empty()) {
                ty += kParagraph;
                continue;
            }
            controls::label(canvas_, x + kLeft * k, base(ty, kLead, size), size, style::kBone, one);
            ty += kLead;
        }
    }

    // ---- the rite's card -----------------------------------------------------------------------------
    const Box card{kLeft, kCardTop, kInner, kCardBottom - kCardTop};
    canvas_.rect(P(card), kCardFloor);
    canvas_.outline(P(card), std::max(1.0f, std::round(u)), kCardRim);
    const float cl = card.x + kCardPad, cr = card.right() - kCardPad;
    float cy = card.y + kCardPad;
    // "before -> after", ranged to `edge`: the face has no arrow, so it is drawn, a shaft and a head.
    const auto change = [&](float edge, float top, uint32_t was, const std::string& before, uint32_t ink,
                            const std::string& after) {
        const float afterW = controls::labelWidth(text, after) / k;
        right(edge, top, ink, after);
        const float tip = edge - afterW - 3.0f, mid = top + kLine * 0.5f + 0.5f;
        const float head[6] = {x + tip * k, y + mid * k, x + (tip - 3.0f) * k, y + (mid - 2.2f) * k,
                               x + (tip - 3.0f) * k, y + (mid + 2.2f) * k};
        canvas_.polygon(nullptr, head, nullptr, 3, ink);
        canvas_.rect(P({tip - 8.0f, mid - 0.5f, 5.5f, 1.0f}), ink);
        right(tip - 11.0f, top, was, before);
    };
    kicker(cl, cy, kTan, levelled ? "The rite" : "The rite asks");
    change(cr, cy, levelled ? style::kBoneHi : style::kAshInk, "Rebirth " + std::to_string(hero.rebirths),
           levelled ? kGold : style::kAshInk, std::to_string(next));
    cy += kRow + 3.0f;
    canvas_.rect(P({cl, cy, cr - cl, 1.0f}), kCardRule);
    cy += 4.0f;
    const auto row = [&](const std::string& what, uint32_t ink, const std::string& value) {
        at(cl, cy, style::kAshInk, what);
        right(cr, cy, ink, value);
        cy += kRow;
    };
    if (levelled) {
        at(cl, cy, style::kAshInk, "Level");
        change(cr, cy, style::kBoneHi, std::to_string(hero.level), kShort, "1");
        cy += kRow;
        row(hero.kin == sim::Kin::DarkLord ? "All five stats, Command too" : "Strength, Agility, Vitality, Energy",
            kKept, "kept");
        row("Skills and talents", kKept, "kept");
        at(cl, cy, style::kAshInk, "Talent points");
        change(cr, cy, style::kBoneHi, std::to_string(sim::talentPoints(hero.kin, hero.level, hero.rebirths)), kGold,
               std::to_string(sim::talentPoints(hero.kin, 1, next)));
        cy += kRow;
        row("Gear that asks for a level", strips < 0 ? kShort : style::kBone,
            strips < 0 ? "no room in the bag" : strips == 0 ? "none" : std::to_string(strips) + (strips == 1 ? " piece" : " pieces") + " to the bag");
        row("Every level gives its points", kKept, "again");
        row("The next rebirth asks", style::kAshInk, panel::commas(sim::rebirthZen(next + 1)) + " Zen");
    } else {
        row("Level", style::kAshInk, "");
        right(cr, cy - kRow, kShort, std::to_string(hero.level) + " / " + std::to_string(sim::kRebirthLevel));
        controls::meter(canvas_, P({cl, cy, cr - cl, 5.0f}),
                        std::clamp(float(hero.level) / float(sim::kRebirthLevel), 0.0f, 1.0f), style::kBloodMd,
                        style::kBloodDk, u);
        cy += 10.0f;
        row("Zen", paid ? style::kBoneHi : kShort, panel::commas(zen) + (paid ? "" : " (short)"));
        cy += 2.0f;
        at(cl, cy, style::kAshInk, "At level " + std::to_string(sim::kRebirthLevel) + " the body goes back to");
        cy += kRow;
        at(cl, cy, style::kAshInk, "level 1. Stats, skills and talents stay.");
        cy += kRow;
    }
    if (levelled) {
        cy = card.bottom() - kCardPad - kRow - 4.0f;
        canvas_.rect(P({cl, cy, cr - cl, 1.0f}), kCardRule);
        cy += 4.0f;
        at(cl, cy, style::kAshInk, "Needs");
        at(cl + 24.0f, cy, levelled ? kKept : kShort, "Level " + std::to_string(sim::kRebirthLevel));
        right(cr, cy, paid ? moneyColour(zen) : kShort, panel::commas(zen) + " Zen");
    }

    // ---- what was said, the foot ----------------------------------------------------------------------
    if (!said_.empty()) at(kLeft, panel::kFootRule - 13.0f, style::kDanger, said_);
    if (waiting_) at(kLeft, panel::kFootRule - 13.0f, style::kAshInk, "...");
    controls::foot(canvas_, P({0.0f, 0.0f, kWide, panel::kHeight}), y + panel::kFootRule * k, u);
    if (confirming_) {
        at(kLeft, panel::kFootRule - 13.0f, kGold, "Kneel before Lugard? It cannot be undone.");
        controls::button(canvas_, P(kNoBox), "No", controls::Kind::Secondary, stateOf(kNo, waiting_), u);
        controls::button(canvas_, P(kYesBox), "Kneel", controls::Kind::Primary, stateOf(kYes, !ready || waiting_), u);
    } else {
        controls::button(canvas_, P(kLeaveBox), levelled ? "Not yet" : "Leave", controls::Kind::Secondary,
                         stateOf(kLeave, false), u);
        controls::button(canvas_, P(kRunBox), "Be reborn", controls::Kind::Primary, stateOf(kRun, !ready || waiting_), u);
    }
    panel::close(canvas_, x, y, overClose_, closing_);
}

}  // namespace mu::game
