#include "game/ui/quest_dialog.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "game/ui/tracker.h"
#include "sim/items.h"
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
constexpr float kPathTall = 30.0f;  // the Magic Gladiator's Melee and Magic (ours)
constexpr float kButtonW = 120.0f, kButtonWide = 180.0f;
constexpr uint32_t kZenGold = gfx::rgba(1.0f, 0.8f, 0.102f);
constexpr uint32_t kItemWhite = gfx::rgba(1.0f, 1.0f, 1.0f);

// A giver's list (kList): a row a quest, its mark at the left and its state ranged right.
constexpr float kEntry = 34.0f;
constexpr uint32_t kRepeatBlue = gfx::rgba(0.56f, 0.80f, 1.0f);  // beacon.cpp's kLitAgain

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
// The floor only: a castle below his band lets him in (Realm::castleRefusal).
std::string bandOf(int castle) {
    return "Level " + std::to_string(sim::kCastleBands[castle - 1][0]) + " and over";
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
    if (quest != o.quest || mode != o.mode || chosen != o.chosen || over != o.over || path != o.path ||
        pressing != o.pressing || x != o.x || y != o.y || unit != o.unit || scroll != o.scroll ||
        overThumb != o.overThumb || dragging != o.dragging || version != o.version ||
        minutesLeft != o.minutesLeft || picture != o.picture || reading != o.reading ||
        pageAt != o.pageAt || pages != o.pages || turn != o.turn || level != o.level ||
        listed != o.listed) {
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
    for (int i = 0; i < sim::kQuests; ++i) {
        if (entries[i] != o.entries[i]) return false;
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

int QuestDialog::pathAt(float ux, float uy) const {
    if (!paths_ || uy < paneTop() || uy > paneTop() + paneTall()) return -1;
    const float by = uy - paneTop() + scroll_;
    for (int i = 0; i < 2; ++i) {
        if (pathBox_[i].has(ux, by)) return i;
    }
    return -1;
}

int QuestDialog::entryAt(float ux, float uy) const {
    if (mode_ != Mode::List || uy < paneTop() || uy > paneTop() + paneTall()) return -1;
    const float by = uy - paneTop() + scroll_;
    for (size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].box.has(ux, by)) return int(i);
    }
    return -1;
}

void QuestDialog::layout(const Play& play) {
    const sim::Realm& realm = play.realm();
    const bool gate = mode_ == Mode::Gate;
    const bool angel = mode_ == Mode::Angel;
    const bool list = mode_ == Mode::List;
    const sim::QuestRow& row = sim::questAt(quest_ >= kGate ? 0 : quest_);
    const content::Tables& tables = *realm.tables();
    lines_.clear();
    cells_.clear();
    clip_.clear();

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
        case Mode::List:
            break;  // the rows are the whole of it
        case Mode::Gate: {
            const std::string said = gateWords(why_, castle_);
            words(said.c_str());
            // Read in silence: the Messenger has no voice, only the Archangel inside the castle
            // speaks (the user, 2026-10-05: 'completly delete this audio ... we keep only inside
            // BC audio').
            break;
        }
        case Mode::Angel: {
            const int32_t weapon = realm.castleWeaponItem();
            const std::string label = weapon >= 0 ? tables.items[size_t(weapon)].label
                                                  : std::string("Divine Staff of Archangel");
            const std::string said = angelWords(angel_, realm.castleRun().phase, label);
            words(said.c_str());
            // Read in silence too: the Archangel has no voice (the user, 2026-10-05: 'lets not
            // use audio on archangel at all').
            break;
        }
    }

    // The body, top down in its own units: the kicker and his words, the steps, the rewards.
    float y = 12.0f + 16.0f;
    for (const std::string& one : lines_) y += one.empty() ? kParagraph : kLead;
    y += kSection;
    if (list) {
        // Under the kicker, a row a quest, full width.
        y = 12.0f + 16.0f + 10.0f;
        for (Entry& one : entries_) {
            one.box = {kInset - 6.0f, y, wide + 12.0f, kEntry};
            y += kEntry + 4.0f;
        }
    } else if (gate) {
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
        y += kSection * 0.5f;
        if (angel_ == sim::AngelState::Done) {
            // As a quest's: the purse's line, then a cell for each of the castle's jewels, put
            // in his bag (Realm::castleTick).
            y += 16.0f + kPurse;
            const int across = int(kChoiceColumns);
            const float cellWide = (wide - kCellGap * (kChoiceColumns - 1.0f)) / kChoiceColumns;
            // The jewels, then the runes the win drew (CastleRun::paidRunes), with their powers.
            // A jewel paid more than once is one cell with its count on the picture.
            struct Paid {
                int32_t item;
                uint8_t power;
                int count;
            };
            std::vector<Paid> paid;
            for (const auto& jewel : sim::kCastleRewardJewels[std::clamp(castle_, 1, sim::kCastles) - 1]) {
                const int32_t item = jewel[0] < 0 ? -1 : tables.itemAt(jewel[0], jewel[1]);
                if (item >= 0) paid.push_back({item, 0, jewel[2]});
            }
            for (uint8_t power : realm.castleRun().paidRunes) {
                const int32_t rune = tables.itemAt(14, 22);
                if (power && rune >= 0) paid.push_back({rune, power, 1});
            }
            int n = 0;
            for (const auto& [item, power, count] : paid) {
                Cell cell;
                cell.item = item;
                cell.power = power;
                cell.count = count;
                cell.ink = tip::colourOf(
                    describe(tables, rewardHeld(tables, item, 0, 1, 0, power), realm.wearer(),
                             realm.satchel())
                        .nameTone);
                cell.box = {kInset + float(n % across) * (cellWide + kCellGap),
                            y + float(n / across) * (kIcon + kCellGap), cellWide, kIcon};
                cells_.push_back(cell);
                ++n;
            }
            y += float((n + across - 1) / across) * (kIcon + kCellGap) + kSection;
        } else {
            y += 24.0f;
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
            if (underLevel_) y += kStepRow;  // the level it asks, over the steps
            // In the order they are drawn: the weakest breed first (quest_marks::byLevel).
            int order[sim::kQuestSteps];
            const int listed = quest_marks::byLevel(row, &tables, order);
            for (int i = 0; i < listed; ++i) {
                const int s = order[i];
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
        const int kin = int(realm.hero().kin);
        const bool first = sim::questFirst(row, kin, realm.quest(quest_).completions);
        // The Magic Gladiator's Melee and Magic, over what each pays him, where the giver pays
        // the knight and the wizard their own things.
        paths_ = kin == int(sim::Kin::MagicGladiator) && !row.promotes &&
                 sim::questOffersPaths(row, first);
        if (paths_) {
            const float half = wide * 0.5f;  // edge to edge, the two tabs one strip
            pathBox_[0] = {kInset, y, half, kPathTall};
            pathBox_[1] = {kInset + half, y, wide - half, kPathTall};
            y += kPathTall + kCellGap * 2.0f;
        }
        const int paidKin = sim::questPaidKin(row, kin, first, path_);
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
                // The Magic Gladiator's rune as he is paid it (sim::gladiatorRune).
                cell.power = kin == int(sim::Kin::MagicGladiator) ? sim::gladiatorRune(what.power)
                                                                  : what.power;
                for (int a = 0; a < 3; ++a) cell.affixes[a] = what.affixes[a];
                // Its name in its own label's colour, as the bag shows it (the user, 2026-10-05:
                // "not correct colors based on actual label colors"; the one legendary ink of
                // 2026-10-04 is gone).
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
        for (int p = 0; p < row.paidCount; ++p) {
            if (sim::questPays(row.paid[p], paidKin, first) && realm.questItemFits(row.paid[p])) {
                paid.push_back({-1, &row.paid[p]});
            }
        }
        grid(paid);
        for (int c = 0; c < row.choiceCount; ++c) {
            if (realm.questChoiceFits(quest_, c)) fits.push_back({c, &row.choices[c]});
        }
        if (!fits.empty()) y += kAsk;
        grid(fits);
    } else if (!list) {
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
    } else if (mode_ == Mode::HandIn || angel) {
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
        path_ = sim::QuestPath::Melee;
        over_ = pressing_ = -1;
        scroll_ = 0.0f;
        lift_[0] = lift_[1] = lift_[2] = 0.0f;
    }
    const bool gate = quest_ == kGate;
    const bool angel = quest_ == kArchangel;
    const bool list = quest_ == kList;
    const sim::QuestProgress& progress = quest_ >= kGate ? kNoProgress : realm.quest(quest_);
    underLevel_ = quest_ < kGate && !reading_ && realm.questUnderLevel(quest_);
    Mode mode = Mode::Offer;
    if (progress.state == sim::QuestState::Active) mode = Mode::Underway;
    // Read from the journal, away from him, a quest ready to hand in is still under way.
    // And at its giver, one another takes back (sim::QuestRow::receiver) is under way until then.
    else if (progress.state == sim::QuestState::Ready) {
        const bool atReceiver =
            quest_ < kGate && realm.questing() >= 0 &&
            realm.tables()->folk[size_t(realm.questing())].number ==
                sim::questReceiver(sim::questAt(quest_));
        mode = reading_ || !atReceiver ? Mode::Underway : Mode::HandIn;
    }
    else if (progress.state == sim::QuestState::Resting && !realm.questOffered(quest_)) {
        mode = Mode::Resting;
    }
    if (list) {
        mode = Mode::List;
        entries_.clear();
        if (realm.questing() >= 0) {
            giver_ = realm.tables()->folk[size_t(realm.questing())].number;
            int quests[sim::kQuests];
            const int n = realm.questsAt(giver_, quests);
            for (int i = 0; i < n; ++i) entries_.push_back({quests[i], {}});
        }
    } else if (angel) {
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
                            // Complete on his thanks is live too: off only while the weapon is
                            // still to be found (the user, 2026-10-05: 'when i press complete
                            // nothing happened').
                            (mode_ == Mode::Angel && angel_ != sim::AngelState::Ready &&
                             angel_ != sim::AngelState::Done) ||
                            (mode_ == Mode::Offer && underLevel_);

    int over = dragging_ ? -1 : buttonAt(ux, uy);
    if (over < 0 && !dragging_) {
        const int cell = cellAt(ux, uy);
        if (cell >= 0 && mode_ == Mode::HandIn) over = 10 + cell;
        // A row waiting on his level is shown, not opened: its words and his voice wait for it
        // too (the user, 2026-10-07: 'dont allow to open quest and read text/listen which
        // character is under lvl').
        if (const int entry = entryAt(ux, uy);
            entry >= 0 && !realm.questUnderLevel(entries_[size_t(entry)].quest)) {
            over = 20 + entry;
        }
        if (const int path = pathAt(ux, uy); path >= 0 && !reading_) over = 30 + path;
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
    bool back = false;
    int pick = -1;
    int turn = 0;
    if (pointer.released) {
        if (pressing_ >= 0 && pressing_ == over_) {
            if (pressing_ == 0 && !primaryOff && !reading_) primary = true;
            else if (pressing_ == 1 && listed_) back = true;
            else if (pressing_ == 1 || pressing_ == 2) cancel = true;
            else if (pressing_ >= 30) {
                path_ = sim::QuestPath(pressing_ - 30);
                chose = true;
            }
            else if (pressing_ >= 20) pick = entries_[size_t(pressing_ - 20)].quest;
            else if (pressing_ == 3) turn = -1;
            else if (pressing_ == 4) turn = 1;
            // The Messenger's castles turn here, the new page fading in from the arrow's side.
            if (turn != 0 && mode_ == Mode::Gate) {
                castle_ = (castle_ - 1 + turn + sim::kCastles) % sim::kCastles + 1;
                turnDir_ = turn;
                turn_ = 0.0f;
            }
            else if (pressing_ >= 10 && pressing_ < 20) {
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
        out->pick = pick;
        out->back = back;
        if (cancel) out->close = true;
        else if (primary && mode_ == Mode::Angel) {
            if (angel_ == sim::AngelState::Done) out->claim = true;
            else out->give = true;
        }
        else if (primary && mode_ == Mode::Gate) {
            out->enter = true;
            out->castle = castle_;
        }
        else if (primary && mode_ == Mode::Offer) out->accept = true;
        else if (primary && mode_ == Mode::HandIn) {
            out->complete = true;
            out->choice = owed ? chosen_ : -1;
            out->path = path_;
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
    now.path = int(path_);
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
    now.level = realm.hero().level;
    now.listed = listed_;
    for (size_t i = 0; i < entries_.size() && i < size_t(sim::kQuests); ++i) {
        const sim::QuestProgress& one = realm.quest(entries_[i].quest);
        now.entries[i] = 1 + entries_[i].quest + int(one.state) * 64 +
                         int(realm.questOffered(entries_[i].quest)) * 512 +
                         int(std::max<int64_t>(0, one.availableAt - realm.wallClock()) / 60) * 1024;
    }
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
    const bool list = mode_ == Mode::List;
    // The list is headed with its giver, whose name and town every row of his carries.
    const sim::QuestRow& row = sim::questAt(quest_ < kGate ? quest_
                                            : list && !entries_.empty() ? entries_[0].quest
                                                                        : 0);
    const sim::QuestProgress& progress = quest_ >= kGate ? kNoProgress : realm.quest(quest_);
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
                                 : list  ? std::string(row.giverName)
                                         : row.title;
        const size_t turning = canvas_.mark();
        controls::label(canvas_, sx(kWide * 0.5f) - controls::labelWidth(title, name) * 0.5f,
                        wy(top + 30.0f), title, style::kBoneHi, name);
        // The Messenger's page between its arrows: which castle of the six, and no more -- the
        // kicker under it names him (the user, 2026-10-03: 'to much text under title does not fit').
        std::string where = gate    ? std::to_string(castle_) + " of " + std::to_string(sim::kCastles)
                            : angel ? std::string("Archangel")
                            : list  ? std::string(row.place)
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
            controls::button(canvas_, placed(x, y, buttons_[1], u), listed_ ? "Back" : "Not now",
                             controls::Kind::Secondary, state(1, false), u);
            controls::button(canvas_, placed(x, y, buttons_[0], u), "Accept", controls::Kind::Primary,
                             state(0, underLevel_), u);
            break;
        case Mode::Gate:
            controls::button(canvas_, placed(x, y, buttons_[1], u), "Not now", controls::Kind::Secondary,
                             state(1, false), u);
            controls::button(canvas_, placed(x, y, buttons_[0], u), "Enter", controls::Kind::Primary,
                             state(0, why_ != sim::CastleRefusal::None), u);
            break;
        case Mode::Angel:
            // Given back, one answer: Complete takes the win and sends him to Devias (the user,
            // 2026-10-05: 'here we need button complete').
            if (angel_ == sim::AngelState::Done) {
                controls::button(canvas_, placed(x, y, buttons_[0], u), "Complete",
                                 controls::Kind::Primary, state(0, false), u);
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
            controls::button(canvas_, placed(x, y, buttons_[1], u),
                             reading_ ? "Close" : listed_ ? "Back" : "Farewell",
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
    const bool offered =
        mode_ != Mode::Stranger && mode_ != Mode::Resting && !gate && !angel && !list;
    // A thing asked for, one look wherever a quest or an event asks one (the user, 2026-10-04:
    // 'we need unificied UI when there is item requirments in quests'): its picture in a cell, its
    // name in its tone, and under it whether he carries it -- green, or red. The Messenger's
    // ticket, the Archangel's weapon and Sevina's treasures.
    const auto needRow = [&](const Cell& one, const std::string& name, bool met,
                             const std::string& status, uint32_t statusInk = 0,
                             bool questItem = false) {
        const Box box = cellBox(one);
        const Box icon{box.x, box.y, box.h, box.h};
        const int at = int(&one - cells_.data());
        controls::cell(body_, icon, over_ == 10 + at ? controls::Cell::Over : controls::Cell::Rest, u);
        const float nx = sx(one.box.x + kIcon + kNameGap);
        const float ly = one.box.y + kIcon * 0.5f;
        // A quest item says so under its name, quiet and grey, as its card's second line does
        // (describe.cpp kindOf; the user, 2026-10-06: 'under the item show "Quest item"').
        // Not yet found, it says no more than that (the user, 2026-10-06: 'dont show not in bag
        // text for quest items'); found, "in your bag" under it in green.
        const bool said = !questItem || met;
        const float drop = questItem && said ? 6.0f : 0.0f;
        controls::label(body_, nx, by(ly - 3.0f - drop), kName * u, one.ink ? one.ink : kItemWhite, name);
        if (questItem) {
            controls::label(body_, nx, by(ly + (said ? 7.0f : 13.0f)), 12.0f * u, style::kAshInk,
                            "Quest item");
        }
        if (said) {
            controls::label(body_, nx, by(ly + 15.0f + drop), kName * u,
                            statusInk ? statusInk : met ? style::kFits : style::kDanger, status);
        }
    };
    // A reward in its cell: the picture's frame, the chosen one's rim, the name beside it.
    const auto rewardCell = [&](size_t i) {
        const Cell& one = cells_[i];
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
    };
    const auto bagWords = [](bool carried) {
        return carried ? std::string("in your bag") : std::string("not in your bag");
    };

    float cy = 12.0f;
    controls::caps(body_, sx(kInset), by(cy + 10.0f), style::kKickerSize * u, style::kAshInk,
                   list    ? std::string("What ") + row.giverName + " has for you"
                   : gate  ? std::string("Messenger of Archangel of Devias")
                   : angel ? std::string("Archangel of Blood Castle")
                   // Her hand-in, in Lirien's words: hers is the name over them.
                   : mode_ == Mode::HandIn && sim::questElsewhere(row)
                       ? std::string(row.receiverName) + " of " + row.receiverPlace
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

    if (list) {
        // A row a quest: WoW's mark at the left -- a gold "!" offered, a gold "?" to hand in, in
        // the beacon's blue once cleared before, a grey "?" under way, a grey "!" waiting on his
        // level -- the title, and at the right what stands between him and it.
        for (size_t i = 0; i < entries_.size(); ++i) {
            const Entry& one = entries_[i];
            const sim::QuestRow& quest = sim::questAt(one.quest);
            const sim::QuestProgress& at = realm.quest(one.quest);
            const bool again = at.completions > 0;
            const bool low = realm.questUnderLevel(one.quest);
            // Ready to hand in here only; at a giver whose quest another takes back, under way.
            const bool here = sim::questReceiver(quest) == giver_;
            const bool ready = at.state == sim::QuestState::Ready && here;
            const bool underway = at.state == sim::QuestState::Active ||
                                  (at.state == sim::QuestState::Ready && !here);
            const bool offer = realm.questOffered(one.quest);
            const char* glyph = ready || underway ? "?" : "!";
            const uint32_t lit = again ? kRepeatBlue : kZenGold;
            const uint32_t ink = ready || offer ? lit : style::kAshInk;
            std::string status;
            uint32_t statusInk = style::kAshInk;
            if (low) {
                status = "Level " + std::to_string(quest.minLevel);
                statusInk = style::kDanger;
            } else if (ready) {
                status = "Complete";
                statusInk = lit;
            } else if (underway) {
                status = "In progress";
            } else if (!offer) {
                status = "Done \xC2\xB7 " +
                         quest_marks::wait(std::max<int64_t>(0, at.availableAt - realm.wallClock()));
            } else if (again) {
                status = "Repeatable";
                statusInk = kRepeatBlue;
            }
            const Box box = {std::round(sx(one.box.x)), std::round(by(one.box.y)),
                             std::round(one.box.w * u), std::round(one.box.h * u)};
            if (over_ == 20 + int(i)) {
                const uint32_t hot = quest_marks::faded(style::kBlood, style::kEmberAlpha);
                const uint32_t clear = quest_marks::faded(style::kBlood, 0.0f);
                body_.shade({box.x, box.y + box.h * 0.3f, box.w, box.h * 0.7f}, clear, clear, hot, hot);
            }
            const float mid = one.box.y + kEntry * 0.5f + 5.0f;
            controls::label(body_, sx(kInset + 2.0f), by(mid + 1.0f), 19.0f * u, ink, glyph);
            const bool bright = (offer || ready || underway) && !low;
            controls::label(body_, sx(kInset + 22.0f), by(mid), kBody * u,
                            bright ? style::kBoneHi : style::kAshInk, quest.title);
            if (!status.empty()) {
                controls::ranged(body_, sx(kInset + inner()), by(mid), 13.5f * u, statusInk, status);
            }
        }
    } else if (angel) {
        controls::rule(body_, sx(kInset), by(cy - kSection * 0.5f), inner() * u, u);
        cy += kSection * 0.5f;
        if (angel_ == sim::AngelState::Done) {
            // Given back: what GiveReward_Win paid, as a quest's rewards are shown -- the purse's
            // line, then the castle's jewels, already in his bag, in their cells.
            controls::kicker(body_, sx(kInset), by(cy + 10.0f), "Rewards", u);
            cy += 16.0f;
            controls::label(body_, sx(kInset), by(cy + 18.0f), kBody * u, style::kBone,
                            panel::commas(paidExperience_) + " experience");
            controls::ranged(body_, sx(kInset + inner()), by(cy + 18.0f), kBody * u, kZenGold,
                             panel::commas(paidZen_) + " Zen");
            for (size_t i = 0; i < cells_.size(); ++i) rewardCell(i);
        } else {
            controls::kicker(body_, sx(kInset), by(cy + 10.0f), "Requirements", u);
            cy += 24.0f;
            for (const Cell& one : cells_) {
                // The run's own weapon: the staff, the sword or the crossbow. Not yet carried,
                // it is named for what it is, in its own purple, not as a want (the user,
                // 2026-10-05: 'show QUest item not "not in your bag"').
                needRow(one,
                        one.item >= 0 && size_t(one.item) < tables.items.size()
                            ? tables.items[size_t(one.item)].label
                            : std::string("Divine Staff of Archangel"),
                        staffHeld_, staffHeld_ ? bagWords(true) : std::string("Quest item"),
                        staffHeld_ ? 0u : (one.ink ? one.ink : kItemWhite));
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
            {sim::kCastlePeriod == 3600 ? "The gate open, hh:25 to hh:30"
                                        : "The gate open, a minute in two (test)",
             door, doorSeconds_ >= 0},
            {band.c_str(), std::to_string(level_), level_ >= sim::kCastleBands[castle_ - 1][0]},
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
            if (underLevel_) {
                // The level it asks, as the Messenger's band is shown: his own, red, at the right.
                quest_marks::mark(body_, StepMark::Live, sx(kInset + 7.0f), by(cy + 8.0f), u);
                controls::label(body_, sx(kInset + 24.0f), by(cy + 13.0f), kBody * u,
                                style::kBoneHi, "Level " + std::to_string(row.minLevel) + " and over");
                controls::ranged(body_, sx(kInset + columnWide), by(cy + 13.0f), kBody * u,
                                 style::kDanger, std::to_string(realm.hero().level));
                cy += kStepRow;
            }
            // The weakest breed at the top, the strongest at the bottom (quest_marks::byLevel).
            int order[sim::kQuestSteps];
            const int listed = quest_marks::byLevel(row, &tables, order);
            for (int i = 0; i < listed; ++i) {
                const int s = order[i];
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
                            const content::ItemRow& thing = tables.items[size_t(item)];
                            needRow(one, thing.label, carried, bagWords(carried), 0u,
                                    sim::classTreasure(thing) || sim::archangelWeapon(thing));
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
        // The Magic Gladiator's path, two tabs over what each pays him, the chosen one lit red.
        if (paths_) {
            const char* const kWords[2] = {"Melee", "Magic"};
            for (int i = 0; i < 2; ++i) {
                const float left = std::round(sx(pathBox_[i].x));
                const Box at{left, std::round(by(pathBox_[i].y)),
                             std::round(sx(pathBox_[i].right())) - left,
                             std::round(pathBox_[i].h * u)};
                controls::tab(body_, at, kWords[i], int(path_) == i,
                              {over_ == 30 + i ? 1.0f : 0.0f,
                               pressing_ == 30 + i && over_ == 30 + i, reading_},
                              u);
            }
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
            if (!cells_[i].need) rewardCell(i);  // a need is drawn with the objectives (needRow)
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
        if ((!offered && !(angel && angel_ == sim::AngelState::Done)) || one.count <= 1) continue;
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
