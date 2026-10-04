#include "game/ui/quest_dialog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "game/ui/tracker.h"
#include "sim/quests.h"
#include "sim/wear.h"

namespace mu::game {
namespace {

using gfx::Box;
using quest_marks::StepMark;

// The window, in tip::unit(): the size of every other window, the bag's and the shelf's -- MU's
// 190 by 429 at panel::scale() (game/ui/panel.h), which is what the user asked of it on
// 2026-09-28 ("has to be same width as other windows"; the proposal's 560 was too wide). Set
// from the panel's scale each frame, as the screen may change; everything else below is in the
// dialog's own unit and fits inside it. It stands on the left, the bag's margin mirrored.
float kWide = 325.0f, kTall = 734.0f;
constexpr float kInset = 22.0f;           // the body's words from the window's side
constexpr float kPaneSide = 12.0f;        // the pane from the window's side
constexpr float kPaneTopGap = 10.0f;      // under the head's rule
constexpr float kFootTall = 18.0f + style::kButtonM + 17.0f + 10.0f;  // the answers' band
constexpr float kBarRight = 14.0f, kBarWide = 4.0f;  // the scrollbar, in from the right
constexpr float kWheelStep = 48.0f;       // units a notch of the wheel
constexpr float kEdgeFade = 18.0f;        // the pane's edges soften where more lies past them
constexpr float kBody = 15.0f, kLead = 23.0f, kParagraph = 9.0f;
constexpr float kSection = 18.0f;
constexpr float kStepRow = 23.0f;
// A reward as WoW lists one: a small picture with its name beside it, two across, the stack's
// count on the picture's corner (the user, 2026-09-29: the tall cells were "very big").
constexpr float kCellGap = 10.0f, kIcon = 44.0f, kNameGap = 8.0f, kName = 14.0f;
constexpr float kChoiceColumns = 2.0f;
constexpr float kPurse = 30.0f;  // the experience and the Zen, one line over the paid grid
constexpr float kAsk = 22.0f;    // the choice's line over its grid
constexpr float kButtonW = 120.0f, kButtonWide = 180.0f;
constexpr uint32_t kZenGold = gfx::rgba(1.0f, 0.8f, 0.102f);
constexpr uint32_t kItemWhite = gfx::rgba(1.0f, 1.0f, 1.0f);

float inner() { return kWide - kInset * 2.0f - 10.0f; }  // leaving room for the bar
// The header band under the frame's head: the quest's name, its map and its giver, and in the
// journal the arrows to the next live quest either side (the user, 2026-09-30).
constexpr float kBanner = 64.0f;
// A page turn's two halves, out and in (the user, 2026-09-30: "nice fadein/out animation when
// we switch between quests"), timed to quest_page.wav, which plays on the arrow ("sync with
// sound"): the page lifts on its first stroke and lands on its second, 0.14 s in, where the new
// page starts to come up; it is in by the rustle's end. And how far a page slides as it goes.
constexpr float kTurnOut = 0.14f;
constexpr float kTurnIn = 0.20f;
constexpr float kTurnSlide = 22.0f;
float paneTop() { return style::kHead + kBanner + kPaneTopGap; }
float paneTall() { return kTall - paneTop() - kFootTall; }

float widthOf(float size, const std::string& text) {
    const gfx::Face* face = controls::labelFace();
    return face ? face->measure(size, text) : float(text.size()) * size * 0.5f;
}

void wrap(const std::string& text, float size, float wide, std::vector<std::string>& out) {
    std::string lineNow, word;
    const auto flush = [&]() {
        if (word.empty()) return;
        const std::string tried = lineNow.empty() ? word : lineNow + " " + word;
        if (!lineNow.empty() && widthOf(size, tried) > wide) {
            out.push_back(lineNow);
            lineNow = word;
        } else {
            lineNow = tried;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flush();
        else word += c;
    }
    flush();
    if (!lineNow.empty()) out.push_back(lineNow);
}

// The Messenger's words for what stands between him and the castle: MU's own sentences
// (Localization/Game.en.resx), ours where MU has none (the band, a castle not built).
std::string gateWords(sim::CastleRefusal why, int castle) {
    switch (why) {
        case sim::CastleRefusal::None:
            return "Your will to help the Archangel is appreciated. But be careful, young warrior "
                   "for Blood Castle is a dangerous place. May God be with you.";  // :3117
        case sim::CastleRefusal::NotYet:
            return "I see that you have the Cloak of Invisibility. But you need to wait till the "
                   "gate opens to enter the Blood Castle.";  // :3109
        case sim::CastleRefusal::NotBuilt:
            return "The Archangel has not called anyone to Blood Castle " +
                   std::to_string(castle) + " yet. Its gate is sealed.";  // ours
        case sim::CastleRefusal::TooLow:
        case sim::CastleRefusal::TooHigh:
            return "I see that you have the Cloak of Invisibility. But this castle is not for "
                   "warriors of your strength.";  // ours
        case sim::CastleRefusal::NoCloak:
            break;
    }
    // :3113, and :3149's where to find what makes one.
    return "Your courage is admirable but you need a Cloak of Invisibility to enter Blood Castle. "
           "You need more than just courage, warrior. You'll find the 'Scroll of Archangel' and "
           "'Blood Bone' by hunting monsters on the Continent of Mu.";
}
// The Archangel's words, ours (the user, 2026-10-04: 'we need better text for archangel'): one
// for each place the run can stand, naming the weapon this run's statue holds and the steps
// the tracker lists. MU's two were ServerCmd 1,24 'You're a warrior in training...' while the
// weapon is out (Game.en.resx legacy 834) and 1,23 'Ah! Great warrior...' once given (833).
std::string angelWords(sim::AngelState state, sim::CastlePhase phase, const std::string& weapon) {
    switch (state) {
        case sim::AngelState::Done:
            return "Ah, my " + weapon + "! Thanks to your courage, Blood Castle is free of "
                   "Kundun's soldiers once more. Take this as a token of our thanks, and with it "
                   "what I have learned in this long war.";
        case sim::AngelState::Ready:
            return "You carry my " + weapon + "! Give it to me, warrior, and Blood Castle is "
                   "ours again.";
        case sim::AngelState::NoStaff:
            return "My " + weapon + " is still in the Statue of Saint's grip. Cut down the "
                   "guards until the drawbridge falls, slay the Spirit Sorcerers who hold the "
                   "door, and break the statue. Then bring my weapon to me, before the time "
                   "runs out.";
        case sim::AngelState::NotYet:
            break;
    }
    if (phase == sim::CastlePhase::Ended) {
        return "The time has run out, and Kundun's soldiers hold the castle still. Rest, "
               "warrior, and come back stronger when the gate opens again.";
    }
    return "Kundun's soldiers have taken this castle, and a Statue of Saint holds my " + weapon +
           " beyond its door. When the gate opens, cut through the guards, slay the Spirit "
           "Sorcerers and break the statue. Bring my weapon back to me, and you will not go "
           "unrewarded.";
}
std::string bandOf(int castle) {
    const int* band = sim::kCastleBands[castle - 1];
    return band[1] == 0 ? "Level " + std::to_string(band[0]) + " and over"
                        : "Level " + std::to_string(band[0]) + " to " + std::to_string(band[1]);
}
constexpr int kGateRows = 3;
const sim::QuestProgress kNoProgress{};

Box placed(float x, float y, const Box& b, float u) {
    return {std::round(x + b.x * u), std::round(y + b.y * u), std::round(b.w * u), std::round(b.h * u)};
}

// A reward as completeQuest will put it in the bag: a stack is one piece of its count, gear comes
// whole at its plus, lucky with its option (sim::kQuestOption) and its empty sockets, a Rune of
// Creation with its power, a powered ring or pendant with its further powers.
sim::Held rewardHeld(const content::Tables& tables, int32_t item, int plus, int count,
                     uint8_t sockets, uint8_t power, const uint8_t* affixes = nullptr) {
    const content::ItemRow& row = tables.items[size_t(item)];
    sim::Held what;
    what.item = item;
    what.refinement = int16_t(plus);
    what.durability =
        int16_t(sim::stacks(row) ? std::max(1, count) : sim::fullDurability(row, plus));
    if (sim::takesOptions(row) || sim::takesSockets(row)) {
        const bool gear = sim::takesOptions(row) || sim::jewellery(row);
        what.luck = gear;
        what.option = int8_t(gear ? sim::kQuestOption : 0);
        what.sockets = uint8_t(std::min<int>(sockets, sim::kMostSockets));
    }
    if (sim::creation(row)) what.powers[0] = power;
    if (sim::powered(row) && affixes) {
        for (int i = 0; i < 3; ++i) what.affixes[i] = affixes[i];
    }
    return what;
}

}  // namespace

bool QuestDialog::Drawn::operator==(const Drawn& o) const {
    if (quest != o.quest || mode != o.mode || chosen != o.chosen || over != o.over ||
        pressing != o.pressing || x != o.x || y != o.y || unit != o.unit || scroll != o.scroll ||
        overThumb != o.overThumb || dragging != o.dragging || version != o.version ||
        minutesLeft != o.minutesLeft || picture != o.picture || reading != o.reading ||
        pageAt != o.pageAt || pages != o.pages || turn != o.turn) {
        return false;
    }
    for (int i = 0; i < 6; ++i) {
        if (gate[i] != o.gate[i]) return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (angel[i] != o.angel[i]) return false;
    }
    for (int i = 0; i < kButtons; ++i) {
        if (lift[i] != o.lift[i]) return false;
    }
    for (int i = 0; i < 16; ++i) {
        if (counts[i] != o.counts[i]) return false;
    }
    return true;
}

void QuestDialog::open(const gfx::Interface& interface) {
    interface.adopt(canvas_);
    interface.adopt(body_);
    interface.adopt(tip_);
}

void QuestDialog::close() {
    canvas_.clear();
    body_.clear();
    tip_.clear();
    quest_ = -1;
    built_ = false;
    dragging_ = false;
}

bool QuestDialog::covers(float x, float y) const {
    if (quest_ < 0) return false;
    return x >= x_ && x < x_ + kWide * unit_ && y >= y_ && y < y_ + kTall * unit_;
}

float QuestDialog::scrollMost() const { return std::max(0.0f, bodyTall_ - paneTall()); }

Box QuestDialog::thumb() const {
    const float trackTop = paneTop() + 6.0f, trackTall = paneTall() - 12.0f;
    const float share = bodyTall_ > 0.0f ? std::min(1.0f, paneTall() / bodyTall_) : 1.0f;
    const float tall = std::max(28.0f, trackTall * share);
    const float most = scrollMost();
    const float at = most > 0.0f ? (scroll_ / most) * (trackTall - tall) : 0.0f;
    return {kWide - kBarRight - kBarWide, trackTop + at, kBarWide, tall};
}

float QuestDialog::turnAlpha() const {
    const float t = std::clamp(std::fabs(turn_), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float QuestDialog::turnShift() const {
    // In, from the side of the arrow pressed; out, toward the other.
    const float away = 1.0f - turnAlpha();
    return (turn_ < 0.0f ? -float(turnDir_) : float(turnDir_)) * away * kTurnSlide;
}

int QuestDialog::buttonAt(float ux, float uy) const {
    for (int which = 0; which < kButtons; ++which) {
        if (buttons_[which].w > 0.0f && buttons_[which].has(ux, uy)) return which;
    }
    return -1;
}

int QuestDialog::cellAt(float ux, float uy, bool anyCell) const {
    // Only inside the pane: a cell scrolled out of it is not there to be chosen.
    if (uy < paneTop() || uy > paneTop() + paneTall()) return -1;
    const float by = uy - paneTop() + scroll_;
    for (size_t i = 0; i < cells_.size(); ++i) {
        if ((anyCell || cells_[i].choice >= 0) && cells_[i].box.has(ux, by)) return int(i);
    }
    return -1;
}

void QuestDialog::layout(const Play& play) {
    const sim::Realm& realm = play.realm();
    const bool gate = mode_ == Mode::Gate;
    const bool angel = mode_ == Mode::Angel;
    const sim::QuestRow& row = sim::questAt(gate || angel ? 0 : quest_);
    const content::Tables& tables = *realm.tables();
    lines_.clear();
    cells_.clear();

    const float wide = inner();
    const auto words = [&](const char* text) {
        if (!text || !*text) return;
        if (!lines_.empty()) lines_.push_back("");  // a paragraph's break
        wrap(text, kBody, wide, lines_);
    };
    switch (mode_) {
        case Mode::Offer:
            for (const char* one : row.offer) words(one);
            break;
        case Mode::Underway:
            words(row.underway);
            break;
        case Mode::HandIn:
            for (const char* one : row.handIn) words(one);
            break;
        case Mode::Resting:
            words(row.resting);
            break;
        case Mode::Stranger:
            words(row.stranger);
            break;
        case Mode::Gate: {
            const std::string said = gateWords(why_, castle_);
            words(said.c_str());
            break;
        }
        case Mode::Angel: {
            const int32_t weapon = realm.castleWeaponItem();
            const std::string said = angelWords(
                angel_, realm.castleRun().phase,
                weapon >= 0 ? tables.items[size_t(weapon)].label : std::string("Divine Staff of Archangel"));
            words(said.c_str());
            break;
        }
    }

    // The body, top down in its own units: the kicker and his words, the steps, the rewards.
    float y = 12.0f + 16.0f;
    for (const std::string& one : lines_) y += one.empty() ? kParagraph : kLead;
    y += kSection;
    if (gate) {
        // The ticket as the rewards are shown, a picture and its name (the user, 2026-10-03: 'if
        // requirments is items, show items thumbs, similiar like you show rewards'); then the
        // gate's hour and the band as lines.
        y += kSection * 0.5f + 24.0f;
        Cell cell;
        cell.item = tables.itemAt(13, 18);
        cell.plus = castle_;
        if (cell.item >= 0) {
            const sim::Held held = rewardHeld(tables, cell.item, castle_, 1, 0, 0);
            cell.ink = tip::colourOf(describe(tables, held, realm.wearer(), realm.satchel()).nameTone);
            cell.box = {kInset, y, wide, kIcon};
            cells_.push_back(cell);
            y += kIcon + kCellGap;
        }
        y += float(kGateRows - 1) * kStepRow + kSection;
    } else if (angel) {
        // What he asks for, the staff, as the rewards are shown; given back, what it paid.
        y += kSection * 0.5f + 24.0f;
        if (angel_ == sim::AngelState::Done) {
            // Experience, Zen and each of the castle's jewels, as the page draws them.
            int jewels = 0;
            for (const auto& jewel : sim::kCastleRewardJewels[std::clamp(castle_, 1, sim::kCastles) - 1]) {
                jewels += jewel[0] >= 0 && tables.itemAt(jewel[0], jewel[1]) >= 0;
            }
            y += float(2 + jewels) * kStepRow + kSection;
        } else {
            Cell cell;
            // The weapon this run's statue holds: the staff, the sword or the crossbow.
            cell.item = realm.castleWeaponItem();
            if (cell.item >= 0) {
                const sim::Held held = rewardHeld(tables, cell.item, 0, 1, 0, 0);
                cell.ink = tip::colourOf(describe(tables, held, realm.wearer(), realm.satchel()).nameTone);
                cell.box = {kInset, y, wide, kIcon};
                cells_.push_back(cell);
                y += kIcon + kCellGap;
            }
            y += kSection;
        }
    } else if (mode_ != Mode::Resting && mode_ != Mode::Stranger) {
        // The hand-in leaves the objectives out: every one is done, and the room is the
        // reward's (the user, 2026-09-29).
        if (mode_ == Mode::HandIn) {
            y += kSection * 0.5f;
        } else {
            y += kSection * 0.5f + 24.0f;
            for (int s = 0; s < row.stepCount; ++s) {
                const sim::QuestStepRow& want = row.steps[s];
                if (!sim::questCounted(want.kind)) continue;
                const int32_t item = want.kind == sim::QuestStepKind::Find && want.item
                                         ? tables.itemNamed(want.item)
                                         : -1;
                if (item < 0) {
                    y += kStepRow;
                    continue;
                }
                // A thing to find is shown as the rewards are, its picture and its name (the
                // user, 2026-10-04: 'on second quest show quest item model as requirment').
                Cell cell;
                cell.item = item;
                cell.need = true;
                const sim::Held held = rewardHeld(tables, item, 0, 1, 0, 0);
                cell.ink = tip::colourOf(describe(tables, held, realm.wearer(), realm.satchel()).nameTone);
                cell.box = {kInset, y, wide, kIcon};
                cells_.push_back(cell);
                y += kIcon + kCellGap;
            }
            y += kStepRow + kSection;  // the return
        }
        y += 16.0f + kPurse;
        // Two grids of the same cells: what his class is paid at this completion, then the
        // choice, when there is one, under its own line.
        const int across = int(kChoiceColumns);
        const float cellWide = (wide - kCellGap * (kChoiceColumns - 1.0f)) / kChoiceColumns;
        const auto grid = [&](const std::vector<std::pair<int, const sim::QuestItem*>>& items) {
            for (size_t i = 0; i < items.size(); ++i) {
                const int column = int(i) % across, rowOf = int(i) / across;
                const sim::QuestItem& what = *items[i].second;
                Cell cell;
                cell.choice = items[i].first;
                cell.item = tables.itemNamed(what.item);
                cell.count = what.count;
                cell.plus = what.plus;
                cell.sockets = what.sockets;
                cell.power = what.power;
                for (int a = 0; a < 3; ++a) cell.affixes[a] = what.affixes[a];
                if (cell.item >= 0) {
                    const sim::Held held = rewardHeld(tables, cell.item, cell.plus, cell.count,
                                                      cell.sockets, cell.power, cell.affixes);
                    cell.ink = tip::colourOf(
                        describe(tables, held, realm.wearer(), realm.satchel()).nameTone);
                }
                cell.box = {kInset + float(column) * (cellWide + kCellGap),
                            y + float(rowOf) * (kIcon + kCellGap), cellWide, kIcon};
                cells_.push_back(cell);
            }
            y += float((int(items.size()) + across - 1) / across) * (kIcon + kCellGap);
        };
        std::vector<std::pair<int, const sim::QuestItem*>> paid, fits;
        const bool first =
            sim::questFirst(row, int(realm.hero().kin), realm.quest(quest_).completions);
        for (int p = 0; p < row.paidCount; ++p) {
            if (sim::questPays(row.paid[p], int(realm.hero().kin), first) &&
                tables.itemNamed(row.paid[p].item) >= 0) {
                paid.push_back({-1, &row.paid[p]});
            }
        }
        grid(paid);
        for (int c = 0; c < row.choiceCount; ++c) {
            if (realm.questChoiceFits(quest_, c)) fits.push_back({c, &row.choices[c]});
        }
        if (!fits.empty()) y += kAsk;
        grid(fits);
    } else {
        y += kLead;
    }
    bodyTall_ = y + 8.0f;
    scroll_ = std::clamp(scroll_, 0.0f, scrollMost());

    // The answers at the foot, centred on the window (the user, 2026-09-29: "center quest
    // buttons"), the primary last: a pair as one group, a lone answer on its own.
    const float buttonTop = kTall - 18.0f - style::kButtonM;
    const float middle = kWide * 0.5f;
    for (Box& one : buttons_) one = {0, 0, 0, 0};
    if ((mode_ == Mode::Offer || gate || (angel && angel_ != sim::AngelState::Done)) && !reading_) {
        const float left = middle - kButtonW - style::kGap * 0.5f;
        buttons_[1] = {left, buttonTop, kButtonW, style::kButtonM};
        buttons_[0] = {left + kButtonW + style::kGap, buttonTop, kButtonW, style::kButtonM};
    } else if (mode_ == Mode::HandIn) {
        buttons_[0] = {middle - kButtonWide * 0.5f, buttonTop, kButtonWide, style::kButtonM};
    } else {
        buttons_[1] = {middle - kButtonW * 0.5f, buttonTop, kButtonW, style::kButtonM};
    }
    const float side = style::kSmallSquare;
    buttons_[2] = {kWide - style::kPad - side, (style::kHead - side) * 0.5f, side, side};
    // The journal's arrows, at the band's two ends, while there is more than one live quest.
    if ((reading_ && pages_ > 1) || gate) {
        const float arrow = style::kSmallSquare + 6.0f;
        const float top = style::kHead + (kBanner - arrow) * 0.5f;
        buttons_[3] = {kInset - 6.0f, top, arrow, arrow};
        buttons_[4] = {kWide - kInset + 6.0f - arrow, top, arrow, arrow};
    }

    // The pictures, in window units, only those at least partly in the pane: each item inside its
    // picture's square, the one under the pointer or chosen turning.
    standing_.clear();
    for (size_t i = 0; i < cells_.size(); ++i) {
        const Cell& one = cells_[i];
        Box at{one.box.x, paneTop() + one.box.y - scroll_, kIcon, kIcon};
        if (at.bottom() < paneTop() || at.y > paneTop() + paneTall()) continue;
        standing_.push_back({one.item, at.grown(-4.0f), one.plus,
                             over_ == 10 + int(i) || (one.choice >= 0 && one.choice == chosen_)});
    }
}

void QuestDialog::update(float seconds, const Play& play, int quest, bool reading, float width,
                         float height, const Pointer& pointer, float wheel, bool enter,
                         bool escape, Stage* stage, Result* out) {
    if (quest < 0) {
        if (quest_ >= 0 || !canvas_.empty()) close();
        return;
    }
    const sim::Realm& realm = play.realm();
    // A page turn in the journal: the old page out, then the new one in. The quest the desk hands
    // over waits in pending_ while the old page leaves.
    if (reading && reading_ && quest_ >= 0 && quest != quest_) {
        if (pending_ != quest) {
            pending_ = quest;
            if (turn_ >= 0.0f) turn_ = -std::min(1.0f, turn_);  // out from wherever it stood
        }
    } else if (!(reading && reading_) && quest != kGate) {
        pending_ = -1;
        turn_ = 1.0f;
    }
    if (turn_ < 1.0f) turn_ = std::min(1.0f, turn_ + seconds / (turn_ < 0.0f ? kTurnOut : kTurnIn));
    if (pending_ >= 0) {
        if (turn_ < 0.0f) quest = quest_;  // the old page, still going
        else pending_ = -1;
    }
    if (quest != quest_ || reading != reading_) {
        quest_ = quest;
        reading_ = reading;
        chosen_ = -1;
        over_ = pressing_ = -1;
        scroll_ = 0.0f;
        lift_[0] = lift_[1] = lift_[2] = 0.0f;
    }
    const bool gate = quest_ == kGate;
    const bool angel = quest_ == kArchangel;
    const sim::QuestProgress& progress = gate || angel ? kNoProgress : realm.quest(quest_);
    Mode mode = Mode::Offer;
    if (progress.state == sim::QuestState::Active) mode = Mode::Underway;
    // Read from the journal, away from him, a quest ready to hand in is still under way.
    else if (progress.state == sim::QuestState::Ready) mode = reading_ ? Mode::Underway : Mode::HandIn;
    else if (progress.state == sim::QuestState::Resting && !realm.questOffered(quest_)) {
        mode = Mode::Resting;
    }
    if (angel) {
        mode = Mode::Angel;
        angel_ = realm.angelState();
        castle_ = realm.castleRun().castle;
        staffHeld_ = realm.staffSlot() >= 0;
        paidExperience_ = realm.castleRun().paidExperience;
        paidZen_ = realm.castleRun().paidZen;
    } else if (gate) {
        mode = Mode::Gate;
        if (mode_ != Mode::Gate) castle_ = sim::castleFor(realm.hero().level);
        why_ = realm.castleRefusal(castle_);
        int slot = realm.cloakSlot(castle_);
        if (slot < 0) slot = realm.cloakSlot();
        cloakPlus_ = slot >= 0 ? int(realm.satchel()[slot].refinement) : -1;
        level_ = realm.hero().level;
        doorSeconds_ = -1;
        opensIn_ = 0;
        if (realm.castleDoorHeld()) {
            doorSeconds_ = 0;
        } else if (const int64_t wall = realm.wallClock(); wall > 0) {
            const time_t at = time_t(wall);
            struct tm local {};
            localtime_r(&at, &local);
            const int day = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
            if (const int left = sim::castleEntryLeft(day); left > 0) {
                doorSeconds_ = left;
            } else {
                const int phase = ((day - sim::kCastleOpensAt) % sim::kCastlePeriod +
                                   sim::kCastlePeriod) % sim::kCastlePeriod;
                opensIn_ = (sim::kCastlePeriod - phase + 59) / 60;
            }
        }
    } else if (!sim::questOpen(sim::questAt(quest_), int(realm.hero().kin))) {
        mode = Mode::Stranger;
    }
    if (mode != mode_) {
        mode_ = mode;
        chosen_ = -1;
        scroll_ = 0.0f;
    }

    unit_ = tip::unit();
    kWide = panel::kWidth * panel::scale() / unit_;
    kTall = panel::kHeight * panel::scale() / unit_;
    x_ = panel::sideMargin();
    y_ = std::round(panel::panelY(height));
    layout(play);

    const float ux = (pointer.x - x_) / unit_, uy = (pointer.y - y_) / unit_;

    // The scroll: the wheel over the window, and the thumb dragged.
    if (wheel != 0.0f && covers(pointer.x, pointer.y)) {
        scroll_ = std::clamp(scroll_ - wheel * kWheelStep, 0.0f, scrollMost());
    }
    const Box bar = thumb();
    const Box barHit{bar.x - 8.0f, bar.y, bar.w + 16.0f, bar.h};
    overThumb_ = scrollMost() > 0.0f && barHit.has(ux, uy);
    if (pointer.pressed && overThumb_) {
        dragging_ = true;
        grab_ = uy - bar.y;
    }
    if (dragging_) {
        if (!pointer.held) {
            dragging_ = false;
        } else {
            const float trackTop = paneTop() + 6.0f, trackTall = paneTall() - 12.0f;
            const float room = std::max(1.0f, trackTall - bar.h);
            scroll_ = std::clamp((uy - grab_ - trackTop) / room * scrollMost(), 0.0f, scrollMost());
        }
    }

    bool owed = false;
    for (const Cell& one : cells_) owed |= one.choice >= 0;
    const bool primaryOff = (mode_ == Mode::HandIn && owed && chosen_ < 0) ||
                            (mode_ == Mode::Gate && why_ != sim::CastleRefusal::None) ||
                            (mode_ == Mode::Angel && angel_ != sim::AngelState::Ready);

    int over = dragging_ ? -1 : buttonAt(ux, uy);
    if (over < 0 && !dragging_) {
        const int cell = cellAt(ux, uy);
        if (cell >= 0 && mode_ == Mode::HandIn) over = 10 + cell;
    }
    over_ = over;
    for (int which = 0; which < kButtons; ++which) {
        const float step = seconds / style::kHoverSeconds;
        lift_[which] = over_ == which ? std::min(1.0f, lift_[which] + step)
                                      : std::max(0.0f, lift_[which] - step);
    }
    if (pointer.pressed && !dragging_) pressing_ = over_;
    bool primary = enter && !primaryOff && !reading_ &&
                   (mode_ == Mode::Offer || mode_ == Mode::HandIn || mode_ == Mode::Gate ||
                    mode_ == Mode::Angel);
    bool cancel = escape;
    bool chose = false;
    int turn = 0;
    if (pointer.released) {
        if (pressing_ >= 0 && pressing_ == over_) {
            if (pressing_ == 0 && !primaryOff && !reading_) primary = true;
            else if (pressing_ == 1 || pressing_ == 2) cancel = true;
            else if (pressing_ == 3) turn = -1;
            else if (pressing_ == 4) turn = 1;
            // The Messenger's castles turn here, the new page fading in from the arrow's side.
            if (turn != 0 && mode_ == Mode::Gate) {
                castle_ = (castle_ - 1 + turn + sim::kCastles) % sim::kCastles + 1;
                turnDir_ = turn;
                turn_ = 0.0f;
            }
            else if (pressing_ >= 10) {
                const int picked = cells_[size_t(pressing_ - 10)].choice;
                chosen_ = chosen_ == picked ? -1 : picked;
                chose = picked >= 0;
            }
        }
        pressing_ = -1;
    }
    if (out) {
        out->picked = chose;
        out->turn = turn;
        if (cancel) out->close = true;
        else if (primary && mode_ == Mode::Angel) out->give = true;
        else if (primary && mode_ == Mode::Gate) {
            out->enter = true;
            out->castle = castle_;
        }
        else if (primary && mode_ == Mode::Offer) out->accept = true;
        else if (primary && mode_ == Mode::HandIn) {
            out->complete = true;
            out->choice = owed ? chosen_ : -1;
        }
    }
    // The standing list again, now the hover, the choice and the scroll are this frame's.
    layout(play);
    if (stage) stage->stand(standing_, kWide, kTall);
    // The card over whatever reward is under the pointer, every frame as the bag's is: it waits
    // a frame on its own stage's picture, which the window's cache knows nothing of.
    drawTip(play, dragging_ ? -1 : cellAt(ux, uy, true), width, height);

    Drawn now;
    now.quest = quest_;
    now.mode = int(mode_);
    now.chosen = chosen_;
    now.over = over_;
    now.pressing = pressing_;
    for (int i = 0; i < kButtons; ++i) now.lift[i] = int(lift_[i] * 32.0f);
    now.pageAt = pageAt_;
    now.pages = pages_;
    now.turn = int(turn_ * 64.0f);
    now.x = x_;
    now.y = y_;
    now.unit = unit_;
    now.scroll = scroll_;
    now.overThumb = overThumb_;
    now.dragging = dragging_;
    now.version = realm.satchel().version();
    now.minutesLeft = std::max<int64_t>(0, progress.availableAt - realm.wallClock()) / 60;
    for (int s = 0; s < sim::kQuestSteps && s < 16; ++s) now.counts[s] = progress.counts[s];
    now.picture = stage && stage->picture().valid() ? stage->picture().handle.idx : 0xFFFF;
    now.reading = reading_;
    if (gate) {
        now.gate[0] = int(why_);
        now.gate[1] = cloakPlus_;
        now.gate[2] = level_;
        now.gate[3] = doorSeconds_;
        now.gate[4] = opensIn_;
        now.gate[5] = castle_;
    }
    if (angel) {
        now.angel[0] = int64_t(angel_) + int64_t(realm.castleRun().phase) * 8 + int64_t(castle_) * 64;
        now.angel[1] = paidExperience_;
        now.angel[2] = paidZen_ * 2 + (staffHeld_ ? 1 : 0);
    }
    if (built_ && now == drawn_) return;
    drawn_ = now;
    built_ = true;
    rebuild(play, stage);
}

void QuestDialog::drawTip(const Play& play, int cell, float width, float height) {
    tip_.clear();
    if (cell < 0) return;
    const Cell& one = cells_[size_t(cell)];
    if (one.item < 0) return;
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Held what =
        rewardHeld(tables, one.item, one.plus, one.count, one.sockets, one.power, one.affixes);
    tip::Sheet sheet = describe(tables, what, realm.wearer(), realm.satchel());
    if (tipStage_) tip::stand(*tipStage_, what.item, what.refinement, sheet);
    // Over the cell as it stands on screen, scrolled.
    const Box at{x_ + one.box.x * unit_, y_ + (paneTop() + one.box.y - scroll_) * unit_,
                 one.box.w * unit_, one.box.h * unit_};
    tip::draw(tip_, sheet, at, width, height);
}

void QuestDialog::rebuild(const Play& play, Stage* stage) {
    canvas_.clear();
    body_.clear();
    if (quest_ < 0) return;
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const bool gate = mode_ == Mode::Gate;
    const bool angel = mode_ == Mode::Angel;
    const sim::QuestRow& row = sim::questAt(gate || angel ? 0 : quest_);
    const sim::QuestProgress& progress = gate || angel ? kNoProgress : realm.quest(quest_);
    const float u = unit_, x = x_, y = y_;
    // Window units to the screen, and the body's own units (which scroll) to the screen.
    const auto sx = [&](float ux) { return x + ux * u; };
    const auto wy = [&](float uy) { return y + uy * u; };
    const auto by = [&](float uy) { return y + (paneTop() + uy - scroll_) * u; };

    // ---- the frame, the scrollbar and the answers -------------------------------------------
    canvas_.rect({0.0f, 0.0f, 1e5f, 1e5f}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.22f));
    controls::frame(canvas_, placed(x, y, {0.0f, 0.0f, kWide, kTall}, u), u,
                    // The Messenger's page is an event's, not a quest's (the user, 2026-10-03:
                    // 'call it Event, not quest').
                    gate || angel ? "Event" : reading_ ? "Quest Journal" : "Quest");
    const auto state = [&](int which, bool off) {
        const float t = lift_[which] * lift_[which] * (3.0f - 2.0f * lift_[which]);
        return controls::State{t, pressing_ == which && over_ == which, off};
    };
    controls::square(canvas_, placed(x, y, buttons_[2], u), controls::Glyph::Close, state(2, false), u);

    // ---- the header band: the quest's name, its map and its giver, the arrows at its ends ----
    {
        const float top = style::kHead;
        canvas_.rect(placed(x, y, {kPaneSide, top + 4.0f, kWide - kPaneSide * 2.0f, kBanner - 8.0f}, u),
                     style::kAsh1);
        const float title = std::round(22.0f * u);
        const float small = std::round(13.5f * u);
        const std::string name = gate    ? "Blood Castle " + std::to_string(castle_)
                                 : angel ? "Blood Castle " + std::to_string(castle_)
                                         : row.title;
        const size_t turning = canvas_.mark();
        controls::label(canvas_, sx(kWide * 0.5f) - controls::labelWidth(title, name) * 0.5f,
                        wy(top + 30.0f), title, style::kBoneHi, name);
        // The Messenger's page between its arrows: which castle of the six, and no more -- the
        // kicker under it names him (the user, 2026-10-03: 'to much text under title does not fit').
        std::string where = gate    ? std::to_string(castle_) + " of " + std::to_string(sim::kCastles)
                            : angel ? std::string("Archangel")
                                    : std::string(row.place) + "   \xC2\xB7   " + row.giverName;
        if (!gate && !angel && reading_ && pages_ > 1) {
            where += "   \xC2\xB7   " + std::to_string(pageAt_) + " of " + std::to_string(pages_);
        }
        controls::label(canvas_, sx(kWide * 0.5f) - controls::labelWidth(small, where) * 0.5f,
                        wy(top + 50.0f), small, style::kAshInk, where);
        canvas_.fadeSince(turning, turnAlpha(), turnShift() * u);
        if (buttons_[3].w > 0.0f) {
            controls::square(canvas_, placed(x, y, buttons_[3], u), controls::Glyph::Left,
                             state(3, false), u);
            controls::square(canvas_, placed(x, y, buttons_[4], u), controls::Glyph::Right,
                             state(4, false), u);
        }
        controls::rule(canvas_, x + 18.0f * u, wy(top + kBanner), (kWide - 36.0f) * u, u);
    }
    if (scrollMost() > 0.0f) {
        // A thin iron track and its thumb, lit under the pointer or held: WoW's bar, without its
        // arrows and in Sanctuary's iron.
        const Box track{kWide - kBarRight - kBarWide, paneTop() + 6.0f, kBarWide, paneTall() - 12.0f};
        canvas_.rect(placed(x, y, track, u), style::kIronDk);
        canvas_.rect(placed(x, y, thumb(), u),
                     overThumb_ || dragging_ ? style::kIronHi : style::kIron);
    }
    controls::rule(canvas_, x + 18.0f * u, wy(kTall - kFootTall + 4.0f), (kWide - 36.0f) * u, u);
    bool owed = false;
    for (const Cell& one : cells_) owed |= one.choice >= 0;
    switch (reading_ ? Mode::Resting : mode_) {
        case Mode::Offer:
            controls::button(canvas_, placed(x, y, buttons_[1], u), "Not now", controls::Kind::Secondary,
                             state(1, false), u);
            controls::button(canvas_, placed(x, y, buttons_[0], u), "Accept", controls::Kind::Primary,
                             state(0, false), u);
            break;
        case Mode::Gate:
            controls::button(canvas_, placed(x, y, buttons_[1], u), "Not now", controls::Kind::Secondary,
                             state(1, false), u);
            controls::button(canvas_, placed(x, y, buttons_[0], u), "Enter", controls::Kind::Primary,
                             state(0, why_ != sim::CastleRefusal::None), u);
            break;
        case Mode::Angel:
            if (angel_ == sim::AngelState::Done) {
                controls::button(canvas_, placed(x, y, buttons_[1], u), "Farewell",
                                 controls::Kind::Secondary, state(1, false), u);
                break;
            }
            controls::button(canvas_, placed(x, y, buttons_[1], u), "Not now", controls::Kind::Secondary,
                             state(1, false), u);
            controls::button(canvas_, placed(x, y, buttons_[0], u), "Give", controls::Kind::Primary,
                             state(0, angel_ != sim::AngelState::Ready), u);
            break;
        case Mode::HandIn:
            controls::button(canvas_, placed(x, y, buttons_[0], u), "Complete quest",
                             controls::Kind::Primary, state(0, owed && chosen_ < 0), u);
            break;
        default:
            controls::button(canvas_, placed(x, y, buttons_[1], u), reading_ ? "Close" : "Farewell",
                             controls::Kind::Secondary, state(1, false), u);
            break;
    }

    // ---- the body, clipped to its pane --------------------------------------------------------
    const Box pane = placed(x, y, {kPaneSide, paneTop(), kWide - kPaneSide * 2.0f, paneTall()}, u);
    body_.clip(pane);
    const auto cellBox = [&](const Cell& one) {
        return Box{std::round(sx(one.box.x)), std::round(by(one.box.y)), std::round(one.box.w * u),
                   std::round(one.box.h * u)};
    };
    const bool offered = mode_ != Mode::Stranger && mode_ != Mode::Resting && !gate && !angel;
    // A thing asked for, one look wherever a quest or an event asks one (the user, 2026-10-04:
    // 'we need unificied UI when there is item requirments in quests'): its picture in a cell, its
    // name in its tone, and under it whether he carries it -- green, or red. The Messenger's
    // ticket, the Archangel's weapon and Sevina's treasures.
    const auto needRow = [&](const Cell& one, const std::string& name, bool met,
                             const std::string& status) {
        const Box box = cellBox(one);
        const Box icon{box.x, box.y, box.h, box.h};
        const int at = int(&one - cells_.data());
        controls::cell(body_, icon, over_ == 10 + at ? controls::Cell::Over : controls::Cell::Rest, u);
        const float nx = sx(one.box.x + kIcon + kNameGap);
        const float ly = one.box.y + kIcon * 0.5f;
        controls::label(body_, nx, by(ly - 3.0f), kName * u, one.ink ? one.ink : kItemWhite, name);
        controls::label(body_, nx, by(ly + 15.0f), kName * u, met ? style::kFits : style::kDanger,
                        status);
    };
    const auto bagWords = [](bool carried) {
        return carried ? std::string("in your bag") : std::string("not in your bag");
    };

    float cy = 12.0f;
    controls::caps(body_, sx(kInset), by(cy + 10.0f), style::kKickerSize * u, style::kAshInk,
                   gate    ? std::string("Messenger of Archangel of Devias")
                   : angel ? std::string("Archangel of Blood Castle")
                           : std::string(row.giverName) + " of " + row.place);
    cy += 16.0f;
    for (const std::string& one : lines_) {
        if (one.empty()) {
            cy += kParagraph;
            continue;
        }
        controls::label(body_, sx(kInset), by(cy + kLead * 0.72f), kBody * u, style::kBone, one);
        cy += kLead;
    }
    cy += kSection;

    if (angel) {
        controls::rule(body_, sx(kInset), by(cy - kSection * 0.5f), inner() * u, u);
        cy += kSection * 0.5f;
        if (angel_ == sim::AngelState::Done) {
            // Given back: what GiveReward_Win paid, a line each, and each of this castle's
            // jewels laid at his feet (sim kCastleRewardJewels).
            controls::kicker(body_, sx(kInset), by(cy + 10.0f), "Rewards", u);
            cy += 24.0f;
            std::vector<std::array<std::string, 2>> lines = {
                {"Experience", panel::commas(paidExperience_)},
                {"Zen", panel::commas(paidZen_)},
            };
            for (const auto& jewel : sim::kCastleRewardJewels[std::clamp(castle_, 1, sim::kCastles) - 1]) {
                const int32_t item = jewel[0] < 0 ? -1 : tables.itemAt(jewel[0], jewel[1]);
                if (item >= 0) lines.push_back({tables.items[size_t(item)].label, "at your feet"});
            }
            for (size_t i = 0; i < lines.size(); ++i) {
                const float rowY = cy + float(i) * kStepRow;
                quest_marks::mark(body_, StepMark::Done, sx(kInset + 7.0f), by(rowY + 8.0f), u);
                controls::label(body_, sx(kInset + 24.0f), by(rowY + 13.0f), kBody * u,
                                style::kBone, lines[i][0]);
                controls::ranged(body_, sx(kInset + inner()), by(rowY + 13.0f), kBody * u,
                                 i == 1 ? kZenGold : style::kBoneHi, lines[i][1]);
            }
        } else {
            controls::kicker(body_, sx(kInset), by(cy + 10.0f), "Requirements", u);
            cy += 24.0f;
            for (const Cell& one : cells_) {
                // The run's own weapon: the staff, the sword or the crossbow.
                needRow(one,
                        one.item >= 0 && size_t(one.item) < tables.items.size()
                            ? tables.items[size_t(one.item)].label
                            : std::string("Divine Staff of Archangel"),
                        staffHeld_, bagWords(staffHeld_));
            }
        }
    } else if (gate) {
        // What he asks for, as objectives: each struck when it is met, its figure beside it.
        controls::rule(body_, sx(kInset), by(cy - kSection * 0.5f), inner() * u, u);
        cy += kSection * 0.5f;
        controls::kicker(body_, sx(kInset), by(cy + 10.0f), "Requirements", u);
        cy += 24.0f;
        const auto clock = [](int seconds) {
            char text[16];
            std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
            return std::string(text);
        };
        const std::string door = doorSeconds_ > 0    ? clock(doorSeconds_) + " left"
                                 : doorSeconds_ == 0 ? std::string("open")
                                 : opensIn_ > 0      ? "in " + std::to_string(opensIn_) + " min"
                                                     : std::string("shut");
        struct Need {
            const char* line;
            std::string figure;
            bool met;
        };
        const std::string band = bandOf(castle_);
        // The ticket: its picture in a cell, its name in its tone, and whether he holds it.
        for (const Cell& one : cells_) {
            needRow(one, "Invisibility Cloak +" + std::to_string(castle_), cloakPlus_ == castle_,
                    cloakPlus_ < 0 ? bagWords(false)
                    : cloakPlus_ == castle_ ? bagWords(true)
                                            : "a +" + std::to_string(cloakPlus_) + " in your bag");
            cy = one.box.y + kIcon + kCellGap;
        }
        const Need needs[kGateRows - 1] = {
            {"The gate open, hh:25 to hh:30", door, doorSeconds_ >= 0},
            {band.c_str(), std::to_string(level_),
             level_ >= sim::kCastleBands[castle_ - 1][0] &&
                 (sim::kCastleBands[castle_ - 1][1] == 0 ||
                  level_ <= sim::kCastleBands[castle_ - 1][1])},
        };
        for (int i = 0; i < kGateRows - 1; ++i) {
            const float rowY = cy + float(i) * kStepRow;
            quest_marks::mark(body_, needs[i].met ? StepMark::Done : StepMark::Live,
                              sx(kInset + 7.0f), by(rowY + 8.0f), u);
            controls::label(body_, sx(kInset + 24.0f), by(rowY + 13.0f), kBody * u,
                            needs[i].met ? style::kAshInk : style::kBoneHi, needs[i].line);
            controls::ranged(body_, sx(kInset + inner()), by(rowY + 13.0f), kBody * u,
                             needs[i].met ? style::kFits : style::kDanger, needs[i].figure);
        }
    } else if (mode_ == Mode::Stranger) {
        // His words are the whole of it: nothing offered, so nothing to count or pay.
    } else if (mode_ == Mode::Resting) {
        const int64_t left = std::max<int64_t>(0, progress.availableAt - realm.wallClock());
        controls::label(body_, sx(kInset), by(cy), 15.0f * u, style::kAshInk,
                        "He will have work again in " + quest_marks::wait(left) + ".");
    } else {
        controls::rule(body_, sx(kInset), by(cy - kSection * 0.5f), inner() * u, u);
        cy += kSection * 0.5f;
        if (mode_ != Mode::HandIn) {
            controls::kicker(body_, sx(kInset), by(cy + 10.0f), "Objectives", u);
            cy += 24.0f;
            const bool shownCounts = mode_ != Mode::Offer;
            const float columnWide = inner();
            for (int s = 0; s < row.stepCount; ++s) {
                const sim::QuestStepRow& want = row.steps[s];
                if (!sim::questCounted(want.kind)) continue;
                const int goal = realm.questGoal(quest_, s);
                const int count = shownCounts ? int(progress.counts[s]) : 0;
                const bool done = shownCounts && count >= goal;
                const float colX = kInset;
                // A thing to find: its cell (layout) draws the picture and the name; the count
                // stands at the right of it, level with the picture's middle.
                const int32_t item = want.kind == sim::QuestStepKind::Find && want.item
                                         ? tables.itemNamed(want.item)
                                         : -1;
                if (item >= 0) {
                    bool carried = false;
                    for (int slot = sim::kWorn; slot < sim::kSlots; ++slot) {
                        carried = carried || realm.satchel()[slot].item == item;
                    }
                    for (const Cell& one : cells_) {
                        if (one.need && one.item == item) {
                            needRow(one, tables.items[size_t(item)].label, carried,
                                    bagWords(carried));
                        }
                    }
                    cy += kIcon + kCellGap;
                    continue;
                }
                const float rowY = cy;
                quest_marks::mark(body_, done ? StepMark::Done : StepMark::Live, sx(colX + 7.0f),
                                  by(rowY + 8.0f), u);
                controls::label(body_, sx(colX + 24.0f), by(rowY + 13.0f), kBody * u,
                                done ? style::kAshInk : style::kBoneHi, want.line);
                const std::string figure = shownCounts
                                               ? std::to_string(count) + " / " + std::to_string(goal)
                                               : std::to_string(goal);
                controls::ranged(body_, sx(colX + columnWide), by(rowY + 13.0f), kBody * u,
                                 done ? style::kAshInk : style::kBone2, figure);
                cy += kStepRow;
            }
            for (int s = 0; s < row.stepCount; ++s) {
                if (row.steps[s].kind != sim::QuestStepKind::Return) continue;
                const bool ready = progress.state == sim::QuestState::Ready;
                quest_marks::mark(body_, ready ? StepMark::Ready : StepMark::Waiting,
                                  sx(kInset + 7.0f), by(cy + 8.0f), u);
                controls::label(body_, sx(kInset + 24.0f), by(cy + 13.0f), kBody * u,
                                ready ? style::kBloodHi : style::kAshInk, row.steps[s].line);
                cy += kStepRow;
            }
            cy += kSection;
        }

        controls::kicker(body_, sx(kInset), by(cy + 10.0f), "Rewards", u);
        cy += 16.0f;
        if (row.promotes) {
            // Sevina's treasure pays the class itself and nothing else: that, on the purse's line.
            controls::label(body_, sx(kInset), by(cy + 18.0f), kBody * u, kZenGold, row.boon);
        } else {
            // The purse, one line: the experience, and the Zen ranged to the right.
            controls::label(body_, sx(kInset), by(cy + 18.0f), kBody * u, style::kBone,
                            panel::commas(sim::questExperience(
                                row, sim::questFirst(row, int(realm.hero().kin),
                                                     progress.completions))) +
                                " experience");
            controls::ranged(body_, sx(kInset + inner()), by(cy + 18.0f), kBody * u, kZenGold,
                             panel::commas(row.zen) + " Zen");
        }
        // The choice's line stands over the first of its cells, under the paid grid.
        for (const Cell& one : cells_) {
            if (one.choice >= 0) {
                cy = one.box.y - kAsk;
                break;
            }
        }
        if (owed) {
            std::string ask = "And one of these when you return:";
            uint32_t ink = style::kAshInk;
            if (mode_ == Mode::HandIn) {
                ask = chosen_ < 0 ? "Choose one to complete the quest:" : "Your choice:";
                ink = chosen_ < 0 ? style::kBloodHi : style::kAshInk;
            }
            controls::label(body_, sx(kInset), by(cy + 12.0f), 15.0f * u, ink, ask);
        }
        for (size_t i = 0; i < cells_.size(); ++i) {
            const Cell& one = cells_[i];
            if (one.need) continue;  // drawn with the objectives (needRow)
            const Box box = cellBox(one);
            const Box icon{box.x, box.y, box.h, box.h};
            controls::cell(body_, icon, over_ == 10 + int(i) ? controls::Cell::Over : controls::Cell::Rest, u);
            if (one.choice >= 0 && one.choice == chosen_) {
                // The chosen one: the hover's ember from its foot and a blood rim.
                const uint32_t hot = quest_marks::faded(style::kBlood, style::kEmberAlpha);
                const uint32_t clear = quest_marks::faded(style::kBlood, 0.0f);
                body_.shade({box.x, box.y + box.h * 0.4f, box.w, box.h * 0.6f}, clear, clear, hot, hot);
                body_.outline(box, std::max(2.0f, 2.0f * u), style::kBlood);
            }
            const content::ItemRow& item = tables.items[size_t(one.item)];
            std::string name = item.label;
            if (one.plus > 0) name += " +" + std::to_string(one.plus);
            // The name beside the picture, at most two lines, centred on it.
            std::vector<std::string> lines;
            wrap(name, kName, one.box.w - kIcon - kNameGap, lines);
            if (lines.size() > 2) lines.resize(2);
            const float lead = kName * 1.2f;
            float ly = one.box.y + (kIcon - lead * float(lines.size())) * 0.5f + kName * 0.9f;
            for (const std::string& line : lines) {
                controls::label(body_, sx(one.box.x + kIcon + kNameGap), by(ly), kName * u,
                                one.ink ? one.ink : kItemWhite, line);
                ly += lead;
            }
        }
    }

    // The item models, photographed for the window's whole rectangle and clipped with the rest.
    if (stage) {
        const gfx::Art picture = stage->picture();
        if (picture.valid()) body_.image(picture, placed(x, y, {0.0f, 0.0f, kWide, kTall}, u));
    }
    // A stack's count on its picture's foot, after the picture so the model cannot cover it, and
    // shadowed as the bag prints it.
    for (const Cell& one : cells_) {
        if (!offered || one.count <= 1) continue;
        const Box box = cellBox(one);
        const Box icon{box.x, box.y, box.h, box.h};
        body_.shadowed(icon.x, icon.bottom() - 4.0f * u, 13.0f * u, kItemWhite,
                       gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f), std::max(1.0f, u),
                       std::to_string(one.count), gfx::Align::Right, icon.w - 4.0f * u);
    }

    // The page turning, when it is: everything on it faded and slid, the pane's edges not.
    if (turn_ < 1.0f) body_.fadeSince(0, turnAlpha(), turnShift() * u);

    // And the pane's edges soften into the sheet where more lies past them, so a line cut by the
    // pane reads as scrolled rather than broken.
    const uint32_t sheet = style::kAsh1, clear = quest_marks::faded(style::kAsh1, 0.0f);
    if (scroll_ > 0.0f) {
        body_.shade({pane.x, pane.y, pane.w, kEdgeFade * u}, sheet, sheet, clear, clear);
    }
    if (scroll_ < scrollMost()) {
        body_.shade({pane.x, pane.bottom() - kEdgeFade * u, pane.w, kEdgeFade * u}, clear, clear,
                    sheet, sheet);
    }
}

}  // namespace mu::game
