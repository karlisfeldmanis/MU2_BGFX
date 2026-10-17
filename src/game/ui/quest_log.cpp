#include "game/ui/quest_log.h"

#include <algorithm>
#include <cmath>

#include "game/play.h"
#include "game/quest_auto.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "game/ui/tracker.h"
#include "sim/items.h"
#include "sim/quests.h"
#include "sim/travel.h"
#include "sim/wear.h"

namespace mu::game {
namespace {

using gfx::Box;

// The window's measures, in tip::unit(), as the proposal drew them at twice the panel's units.
constexpr float kSide = style::kPad;
constexpr float kKick = 22.0f;     // a map's name over its quests
constexpr float kRow = 31.0f;      // a quest's row
constexpr float kRowGap = 4.0f;
constexpr float kGroupGap = 6.0f;
constexpr float kButtonW = 78.0f, kButtonH = 24.0f;
constexpr float kRowWord = 15.0f;  // the row's title
constexpr float kListShare = 0.44f;  // of the body, the most the list takes before it scrolls
constexpr float kTitle = 19.0f, kLine = 13.5f;
constexpr float kStep = 13.0f, kStepRow = 19.0f;
constexpr float kIcon = 34.0f, kCellGap = 10.0f, kCellPitch = kIcon + 5.0f, kName = 12.5f;
constexpr float kWheelStep = 40.0f;
// The travel list's gold for the row chosen (game/ui/travel.cpp, the user, 2026-09-30: 'selected
// row in golden tones, not blood red'), and the card's Zen.
constexpr uint32_t kGold = gfx::rgba(0.722f, 0.600f, 0.353f);
constexpr uint32_t kGoldHi = gfx::rgba(0.886f, 0.776f, 0.541f);
constexpr uint32_t kGoldDk = gfx::rgba(0.431f, 0.353f, 0.196f);
constexpr uint32_t kZenGold = gfx::rgba(1.0f, 0.8f, 0.102f);
constexpr uint32_t kItemWhite = gfx::rgba(1.0f, 1.0f, 1.0f);

Box placed(float x, float y, const Box& b, float u) {
    return {std::round(x + b.x * u), std::round(y + b.y * u), std::round(b.w * u), std::round(b.h * u)};
}

// A reward as completeQuest puts it in the bag (game/ui/quest_dialog.cpp's own).
sim::Held rewardHeld(const content::Tables& tables, int32_t item, int plus, int count, uint8_t sockets,
                     uint8_t power, const uint8_t* affixes) {
    const content::ItemRow& row = tables.items[size_t(item)];
    sim::Held what;
    what.item = item;
    what.refinement = int16_t(plus);
    what.durability = int16_t(sim::stacks(row) ? std::max(1, count) : sim::fullDurability(row, plus));
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

// The order the maps are listed in: the Move list's, which is the order a character comes to them.
int rankOf(int map) {
    for (int i = 0; i < sim::kTravels; ++i) {
        if (sim::travelAt(i).map == map) return i;
    }
    return 100 + map;
}

// A quest listed: under way, or cleared and his to take again -- not one cleared for good.
bool listed(const sim::Realm& realm, int quest) {
    const sim::QuestProgress& at = realm.quest(quest);
    if (at.state == sim::QuestState::Active || at.state == sim::QuestState::Ready) return true;
    return at.completions > 0 && sim::questAt(quest).repeatSeconds > 0;
}

std::string clearedWords(uint32_t times) {
    if (times == 1) return "cleared once";
    if (times == 2) return "cleared twice";
    return "cleared " + std::to_string(times) + " times";
}

// The words of a line trimmed with ".." to fit `wide` at `size`.
std::string fitted(float size, const std::string& text, float wide) {
    if (controls::labelWidth(size, text) <= wide) return text;
    std::string cut = text;
    while (cut.size() > 1 && controls::labelWidth(size, cut + "..") > wide) cut.pop_back();
    return cut + "..";
}

}  // namespace

void QuestLog::open(const gfx::Interface& interface, const std::string& assetDir) {
    interface.adopt(canvas_);
    interface.adopt(body_);
    interface.adopt(card_);
    interface.adopt(tip_);
    assetDir_ = assetDir;
}

void QuestLog::close() {
    canvas_.clear();
    body_.clear();
    card_.clear();
    tip_.clear();
    up_ = false;
    built_ = false;
}

void QuestLog::show(int prefer) {
    quest_world::open(assetDir_);
    up_ = true;
    prefer_ = prefer;
    selected_ = -1;
    listScroll_ = cardScroll_ = 0.0f;
    over_ = pressing_ = -1;
    built_ = false;
}

void QuestLog::hide() {
    if (!up_) return;
    close();
}

bool QuestLog::covers(float x, float y) const {
    if (!up_) return false;
    return x >= x_ && y >= y_ && x < x_ + wide_ * unit_ && y < y_ + tall_ * unit_;
}

void QuestLog::layout(const Play& play, int playing) {
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const int here = int(tables.map);
    const float inner = wide_ - kSide * 2.0f;

    // ---- the list: by map, the Move list's order, the quests in the table's -------------------
    entries_.clear();
    for (int q = 0; q < sim::kQuests; ++q) {
        if (listed(realm, q)) entries_.push_back({q, quest_world::questMap(q, here)});
    }
    std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        return rankOf(a.map) < rankOf(b.map);
    });
    if (selected_ >= 0 && std::none_of(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.quest == selected_; })) {
        selected_ = -1;
    }
    if (selected_ < 0) {
        for (const Entry& one : entries_) {
            if (one.quest == prefer_ || one.quest == playing) selected_ = one.quest;
        }
        if (selected_ < 0 && !entries_.empty()) selected_ = entries_[0].quest;
    }
    listTop_ = style::kHead + 12.0f;
    float y = 0.0f;
    for (size_t i = 0; i < entries_.size(); ++i) {
        Entry& one = entries_[i];
        one.heads = i == 0 || entries_[i - 1].map != one.map;
        if (one.heads) y += (i == 0 ? 0.0f : kGroupGap) + kKick;
        one.box = {kSide, listTop_ + y - listScroll_, inner, kRow};
        const bool button = playing == one.quest || autoPlayableEver(realm, one.quest);
        one.button = button ? Box{one.box.right() - 4.0f - kButtonW, one.box.y + (kRow - kButtonH) * 0.5f,
                                  kButtonW, kButtonH}
                            : Box{0, 0, 0, 0};
        y += kRow + kRowGap;
    }
    listContent_ = entries_.empty() ? kKick + kRow : y - kRowGap;
    listTall_ = std::min(listContent_, (tall_ - style::kHead) * kListShare);
    listScroll_ = std::clamp(listScroll_, 0.0f, std::max(0.0f, listContent_ - listTall_));

    // ---- the card: the chosen quest's rewards, two across ------------------------------------
    cardTop_ = listTop_ + listTall_ + 16.0f;
    cardTall_ = tall_ - kSide - cardTop_;
    cells_.clear();
    float cy = 0.0f;
    if (selected_ >= 0) {
        const sim::QuestRow& row = sim::questAt(selected_);
        const sim::QuestProgress& progress = realm.quest(selected_);
        cy = kTitle + 6.0f + kLine + 10.0f;
        if (progress.state == sim::QuestState::Resting && !realm.questOffered(selected_)) cy += kLine + 6.0f;
        int counted = 0;
        for (int s = 0; s < row.stepCount; ++s) counted += sim::questCounted(row.steps[s].kind) ? 1 : 0;
        cy += float((counted + 1) / 2) * kStepRow + kStepRow + 12.0f;  // the steps, then the return
        cy += kKick;
        const int kin = int(realm.hero().kin);
        const bool first = sim::questFirst(row, kin, progress.completions);
        const int paidKin = sim::questPaidKin(row, kin, first, sim::QuestPath::Melee);
        std::vector<const sim::QuestItem*> items;
        for (int p = 0; p < row.paidCount; ++p) {
            if (sim::questPaysHero(row.paid[p], kin, paidKin, first) && realm.questItemFits(row.paid[p])) {
                items.push_back(&row.paid[p]);
            }
        }
        for (int c = 0; c < row.choiceCount; ++c) {
            if (realm.questChoiceFits(selected_, c)) items.push_back(&row.choices[c]);
        }
        const float cellWide = (inner - kCellGap) * 0.5f;
        for (size_t i = 0; i < items.size(); ++i) {
            const sim::QuestItem& what = *items[i];
            Cell cell;
            cell.item = tables.itemNamed(what.item);
            if (cell.item < 0) continue;
            cell.count = what.count;
            cell.plus = what.plus;
            cell.sockets = what.sockets;
            cell.power = kin == int(sim::Kin::MagicGladiator) ? sim::gladiatorRune(what.power) : what.power;
            for (int a = 0; a < 3; ++a) cell.affixes[a] = what.affixes[a];
            const sim::Held held =
                rewardHeld(tables, cell.item, cell.plus, cell.count, cell.sockets, cell.power, cell.affixes);
            cell.ink = tip::colourOf(describe(tables, held, realm.wearer(), realm.satchel()).nameTone);
            const int at = int(cells_.size());
            cell.box = {kSide + float(at % 2) * (cellWide + kCellGap),
                        cardTop_ + cy + float(at / 2) * kCellPitch - cardScroll_, cellWide, kIcon};
            cells_.push_back(cell);
        }
        cy += float((int(cells_.size()) + 1) / 2) * kCellPitch;
    }
    cardContent_ = cy;
    const float cardMost = std::max(0.0f, cardContent_ - cardTall_);
    if (cardScroll_ > cardMost) {
        const float back = cardScroll_ - cardMost;
        cardScroll_ = cardMost;
        for (Cell& one : cells_) one.box.y += back;
    }

    // The pictures, only those inside the card's pane.
    standing_.clear();
    for (size_t i = 0; i < cells_.size(); ++i) {
        const Box at{cells_[i].box.x, cells_[i].box.y, kIcon, kIcon};
        if (at.bottom() < cardTop_ || at.y > cardTop_ + cardTall_) continue;
        standing_.push_back({cells_[i].item, at.grown(-3.0f), cells_[i].plus, over_ == 300 + int(i)});
    }
}

void QuestLog::update(float seconds, const Play& play, int playing, float width, float height,
                      const Pointer& pointer, float wheel, Stage* stage, Result* out) {
    (void)seconds;
    if (!up_ || !play.isOpen() || play.realm().tables() == nullptr) {
        tip_.clear();
        return;
    }
    const sim::Realm& realm = play.realm();
    unit_ = tip::unit();
    wide_ = panel::kWidth * panel::scale() / unit_;
    tall_ = panel::kHeight * panel::scale() / unit_;
    x_ = panel::sideMargin();
    y_ = std::round(panel::panelY(height));
    layout(play, playing);

    const float ux = (pointer.x - x_) / unit_, uy = (pointer.y - y_) / unit_;
    const bool inside = covers(pointer.x, pointer.y);
    if (wheel != 0.0f && inside) {
        if (uy < listTop_ + listTall_) {
            listScroll_ = std::clamp(listScroll_ - wheel * kWheelStep, 0.0f, std::max(0.0f, listContent_ - listTall_));
        } else {
            cardScroll_ = std::clamp(cardScroll_ - wheel * kWheelStep, 0.0f, std::max(0.0f, cardContent_ - cardTall_));
        }
        layout(play, playing);
    }

    // What is under the pointer: the close, a row's button, a row, a reward.
    const float side = style::kSmallSquare;
    const Box closeBox{wide_ - style::kPad - side, (style::kHead - side) * 0.5f, side, side};
    int over = -1;
    if (inside) {
        if (closeBox.has(ux, uy)) over = 0;
        const bool inList = uy >= listTop_ && uy < listTop_ + listTall_;
        for (size_t i = 0; i < entries_.size() && over < 0 && inList; ++i) {
            if (entries_[i].button.w > 0.0f && entries_[i].button.has(ux, uy)) over = 200 + int(i);
            else if (entries_[i].box.has(ux, uy)) over = 100 + int(i);
        }
        const bool inCard = uy >= cardTop_ && uy < cardTop_ + cardTall_;
        for (size_t i = 0; i < cells_.size() && over < 0 && inCard; ++i) {
            if (cells_[i].box.has(ux, uy)) over = 300 + int(i);
        }
    }
    over_ = over;
    if (pointer.pressed) pressing_ = over_;
    if (pointer.released) {
        if (pressing_ >= 0 && pressing_ == over_ && out) {
            if (pressing_ == 0) {
                out->close = true;
            } else if (pressing_ >= 200 && pressing_ < 300) {
                const int q = entries_[size_t(pressing_ - 200)].quest;
                selected_ = q;
                if (q == playing) out->stop = true;
                else if (autoPlayable(realm, q)) out->autoPlay = q;
            } else if (pressing_ >= 100 && pressing_ < 200) {
                const int q = entries_[size_t(pressing_ - 100)].quest;
                if (q != selected_) {
                    selected_ = q;
                    cardScroll_ = 0.0f;
                    out->clicked = true;
                }
            }
        }
        pressing_ = -1;
    }
    layout(play, playing);
    if (stage) stage->stand(standing_, wide_, tall_);
    drawTip(play, over_ >= 300 ? over_ - 300 : -1, width, height);

    // Drawn again only when something it shows has changed.
    std::vector<int64_t> now = {selected_, over_, pressing_, playing, int64_t(x_), int64_t(y_),
                                int64_t(unit_ * 1000.0f), int64_t(listScroll_ * 8.0f),
                                int64_t(cardScroll_ * 8.0f), realm.hero().level, int64_t(realm.satchel().version()),
                                stage && stage->picture().valid() ? int64_t(stage->picture().handle.idx) : -1};
    for (const Entry& one : entries_) {
        const sim::QuestProgress& at = realm.quest(one.quest);
        now.push_back(one.quest * 16 + int(at.state) + int(realm.questOffered(one.quest)) * 8);
        now.push_back(int64_t(at.completions));
        now.push_back(std::max<int64_t>(0, at.availableAt - realm.wallClock()) / 60);
        for (int s = 0; s < sim::kQuestSteps; ++s) now.push_back(at.counts[s]);
    }
    if (built_ && now == drawn_) return;
    drawn_ = std::move(now);
    built_ = true;
    rebuild(play, playing, stage);
}

void QuestLog::drawTip(const Play& play, int cell, float width, float height) {
    tip_.clear();
    if (cell < 0 || size_t(cell) >= cells_.size()) return;
    const Cell& one = cells_[size_t(cell)];
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const sim::Held what = rewardHeld(tables, one.item, one.plus, one.count, one.sockets, one.power, one.affixes);
    tip::Sheet sheet = describe(tables, what, realm.wearer(), realm.satchel());
    if (tipStage_) tip::stand(*tipStage_, what.item, what.refinement, sheet);
    const Box at{x_ + one.box.x * unit_, y_ + one.box.y * unit_, one.box.w * unit_, one.box.h * unit_};
    tip::draw(tip_, sheet, at, width, height);
}

void QuestLog::rebuild(const Play& play, int playing, Stage* stage) {
    canvas_.clear();
    body_.clear();
    card_.clear();
    const sim::Realm& realm = play.realm();
    const content::Tables& tables = *realm.tables();
    const float u = unit_, x = x_, y = y_;
    const float inner = wide_ - kSide * 2.0f;
    const auto sx = [&](float ux) { return x + ux * u; };
    const auto sy = [&](float uy) { return y + uy * u; };
    const auto state = [&](int which, bool off) {
        return controls::State{over_ == which ? 1.0f : 0.0f, pressing_ == which && over_ == which, off};
    };

    // ---- the frame and its close --------------------------------------------------------------
    controls::frame(canvas_, placed(x, y, {0.0f, 0.0f, wide_, tall_}, u), u, "Quests");
    const float side = style::kSmallSquare;
    controls::square(canvas_, placed(x, y, {wide_ - style::kPad - side, (style::kHead - side) * 0.5f, side, side}, u),
                     controls::Glyph::Close, state(0, false), u);

    // ---- the list, clipped to its pane --------------------------------------------------------
    body_.clip(placed(x, y, {0.0f, listTop_, wide_, listTall_}, u));
    if (entries_.empty()) {
        controls::label(body_, sx(kSide), sy(listTop_ + 18.0f), kLine * u, style::kAshInk,
                        "No quests yet. Speak to the townsfolk marked !");
    }
    for (size_t i = 0; i < entries_.size(); ++i) {
        const Entry& one = entries_[i];
        const sim::QuestRow& row = sim::questAt(one.quest);
        const sim::QuestProgress& at = realm.quest(one.quest);
        if (one.heads) {
            controls::caps(body_, sx(kSide + 2.0f), sy(one.box.y - 7.0f), style::kKickerSize * u, style::kAshInk,
                           quest_world::mapName(one.map));
        }
        const Box box = placed(x, y, one.box, u);
        const bool chosen = one.quest == selected_;
        const bool hot = over_ == 100 + int(i) || over_ == 200 + int(i);
        // The row: an iron-rimmed well, the chosen one in the travel list's gold.
        body_.rect(box, chosen ? kGoldDk : hot ? style::kIronHi : style::kIronLo);
        const Box in = box.grown(-std::max(1.0f, std::round(u)));
        body_.rect(in, style::kAsh1);
        if (chosen) {
            body_.shade({in.x, in.y, in.w * 0.75f, in.h}, quest_marks::faded(kGold, 0.18f),
                        quest_marks::faded(kGold, 0.0f), quest_marks::faded(kGold, 0.0f),
                        quest_marks::faded(kGold, 0.18f));
            body_.rect({box.x, box.y + std::round(8.0f * u), std::round(2.0f * u), box.h - std::round(16.0f * u)}, kGold);
        }
        const bool resting = at.state == sim::QuestState::Resting && !realm.questOffered(one.quest);
        const float mid = one.box.y + kRow * 0.5f + 5.0f;
        // At the right, from the edge in: the button, then what stands -- a count, Ready, a wait.
        float right = one.box.right() - 10.0f;
        if (one.button.w > 0.0f) {
            const bool mine = one.quest == playing;
            const bool can = mine || autoPlayable(realm, one.quest);
            controls::button(body_, placed(x, y, one.button, u), mine ? "Stop" : "Auto-play",
                             mine ? controls::Kind::Secondary : controls::Kind::Primary,
                             state(200 + int(i), !can), u);
            right = one.button.x - 8.0f;
        }
        std::string status;
        uint32_t statusInk = style::kBone2;
        if (at.state == sim::QuestState::Active) {
            int done = 0, goal = 0;
            for (int s = 0; s < row.stepCount; ++s) {
                if (row.steps[s].kind == sim::QuestStepKind::Return) continue;
                const int want = quest_world::goal(realm, one.quest, s);
                goal += want;
                done += std::min<int>(at.counts[s], want);
            }
            status = std::to_string(done) + " / " + std::to_string(goal);
            if (chosen) statusInk = kGoldHi;
        } else if (at.state == sim::QuestState::Ready) {
            status = "Ready";
            statusInk = style::kFits;
        } else if (resting) {
            status = quest_marks::wait(std::max<int64_t>(0, at.availableAt - realm.wallClock()));
            statusInk = style::kAshInk;
        }
        float titleRoom = right - (one.box.x + 12.0f) - 6.0f;
        if (!status.empty()) {
            const float statusWide = controls::labelWidth(13.5f * u, status) / u;
            controls::ranged(body_, sx(right), sy(mid), 13.5f * u, statusInk, status);
            titleRoom -= statusWide + 8.0f;
        }
        const uint32_t titleInk = chosen ? kGoldHi : resting ? style::kAshInk : style::kBoneHi;
        controls::label(body_, sx(one.box.x + 12.0f), sy(mid), kRowWord * u, titleInk,
                        fitted(kRowWord * u, row.title, titleRoom * u));
    }
    // More of the list past its pane: a thin iron bar at its right.
    if (listContent_ > listTall_) {
        const float track = listTall_, thumb = std::max(24.0f, track * listTall_ / listContent_);
        const float at = (track - thumb) * listScroll_ / std::max(1.0f, listContent_ - listTall_);
        body_.rect(placed(x, y, {wide_ - 7.0f, listTop_ + at, 3.0f, thumb}, u), style::kIron);
    }
    controls::rule(canvas_, sx(kSide), sy(listTop_ + listTall_ + 8.0f), inner * u, u);

    // ---- the card, clipped to its pane --------------------------------------------------------
    if (selected_ < 0) return;
    // A canvas clips one box, the whole of it: the card has its own.
    gfx::Canvas& card = card_;
    gfx::Canvas& words = card_;
    const sim::QuestRow& row = sim::questAt(selected_);
    const sim::QuestProgress& progress = realm.quest(selected_);
    const Box pane = placed(x, y, {0.0f, cardTop_, wide_, cardTall_}, u);
    card_.clip(pane);
    float cy = cardTop_ - cardScroll_;
    if (const gfx::Face* face = panel::titleFace()) {
        words.lettered(*face, panel::titleTexture(), sx(kSide) + 1.0f, sy(cy + kTitle) + 1.0f, kTitle * u, 0.0f,
                       style::kDrop, row.title);
        words.lettered(*face, panel::titleTexture(), sx(kSide), sy(cy + kTitle), kTitle * u, 0.0f, kGoldHi, row.title);
    } else {
        controls::label(words, sx(kSide), sy(cy + kTitle), kTitle * u, kGoldHi, row.title);
    }
    cy += kTitle + 6.0f;
    std::string line = std::string(row.giverName) + "  \xC2\xB7  " + row.place;
    if (row.repeatSeconds > 0) {
        line += "  \xC2\xB7  ";
        line += progress.completions > 0 ? clearedWords(progress.completions) : std::string("every 12 hours");
    }
    controls::label(words, sx(kSide), sy(cy + kLine), kLine * u, style::kAshInk, line);
    cy += kLine + 10.0f;
    const bool resting = progress.state == sim::QuestState::Resting && !realm.questOffered(selected_);
    if (resting) {
        controls::label(words, sx(kSide), sy(cy + kLine), kLine * u, style::kAshInk,
                        "Ready again in " + quest_marks::wait(std::max<int64_t>(0, progress.availableAt - realm.wallClock())));
        cy += kLine + 6.0f;
    }
    // The steps, two across, the weakest breed first: struck in green, the one to hunt lit.
    const bool counting = progress.state == sim::QuestState::Active || progress.state == sim::QuestState::Ready;
    int order[sim::kQuestSteps];
    const int ordered = quest_marks::byLevel(row, &tables, order);
    const float column = (inner - 14.0f) * 0.5f;
    int at = 0;
    bool lit = false;
    for (int i = 0; i < ordered; ++i) {
        const int s = order[i];
        const sim::QuestStepRow& want = row.steps[s];
        if (!sim::questCounted(want.kind)) continue;
        const int goal = quest_world::goal(realm, selected_, s);
        const int count = counting ? std::min<int>(progress.counts[s], goal) : 0;
        const bool done = counting && count >= goal;
        const bool now = counting && !done && !lit;
        lit = lit || now;
        const float left = kSide + float(at % 2) * (column + 14.0f);
        const float base = cy + float(at / 2) * kStepRow + kStep;
        const uint32_t ink = done ? style::kFits : now ? style::kBoneHi : style::kAshInk;
        const std::string figure = std::to_string(count) + "/" + std::to_string(goal);
        const float figureWide = controls::labelWidth(kStep * u, figure) / u;
        controls::label(words, sx(left), sy(base), kStep * u, ink,
                        fitted(kStep * u, want.line, (column - figureWide - 6.0f) * u));
        controls::ranged(words, sx(left + column), sy(base), kStep * u, ink, figure);
        ++at;
    }
    cy += float((at + 1) / 2) * kStepRow;
    for (int s = 0; s < row.stepCount; ++s) {
        if (row.steps[s].kind != sim::QuestStepKind::Return) continue;
        const bool ready = progress.state == sim::QuestState::Ready;
        controls::label(words, sx(kSide), sy(cy + kStep), kStep * u, ready ? style::kBloodHi : style::kAshInk2,
                        row.steps[s].line);
    }
    cy += kStepRow + 12.0f;
    // The rewards: one heading, the experience and the Zen at its right, then every item.
    controls::caps(words, sx(kSide + 2.0f), sy(cy + 12.0f), style::kKickerSize * u, style::kAshInk, "Rewards");
    {
        const int kin = int(realm.hero().kin);
        const bool first = sim::questFirst(row, kin, progress.completions);
        if (row.promotes) {
            controls::ranged(words, sx(kSide + inner), sy(cy + 12.0f), kLine * u, kZenGold, row.boon);
        } else {
            const std::string zen = panel::commas(row.zen) + " Zen";
            const float zenWide = controls::labelWidth(kLine * u, zen) / u;
            controls::ranged(words, sx(kSide + inner), sy(cy + 12.0f), kLine * u, kZenGold, zen);
            controls::ranged(words, sx(kSide + inner - zenWide - 10.0f), sy(cy + 12.0f), kLine * u, style::kBone,
                             panel::commas(sim::questExperience(row, first)) + " experience  \xC2\xB7");
        }
    }
    // Each reward: its cell, its picture (the stage's, below), its name beside it in its own tone.
    for (size_t i = 0; i < cells_.size(); ++i) {
        const Cell& one = cells_[i];
        const Box box = placed(x, y, one.box, u);
        const Box icon{box.x, box.y, box.h, box.h};
        controls::cell(card, icon, over_ == 300 + int(i) ? controls::Cell::Over : controls::Cell::Rest, u);
        std::string name = tables.items[size_t(one.item)].label;
        if (one.plus > 0) name += " +" + std::to_string(one.plus);
        // At most two lines beside the picture, centred on it.
        std::vector<std::string> lines;
        {
            std::string lineNow, word;
            const float room = (one.box.w - kIcon - 8.0f) * u;
            const auto flush = [&]() {
                if (word.empty()) return;
                const std::string tried = lineNow.empty() ? word : lineNow + " " + word;
                if (!lineNow.empty() && controls::labelWidth(kName * u, tried) > room) {
                    lines.push_back(lineNow);
                    lineNow = word;
                } else {
                    lineNow = tried;
                }
                word.clear();
            };
            for (char c : name) {
                if (c == ' ') flush();
                else word += c;
            }
            flush();
            if (!lineNow.empty()) lines.push_back(lineNow);
        }
        if (lines.size() > 2) lines.resize(2);
        const float lead = kName * 1.2f;
        float ly = one.box.y + (kIcon - lead * float(lines.size())) * 0.5f + kName * 0.9f;
        for (const std::string& one_line : lines) {
            controls::label(card, sx(one.box.x + kIcon + 8.0f), sy(ly), kName * u, one.ink ? one.ink : kItemWhite, one_line);
            ly += lead;
        }
    }
    if (stage) {
        const gfx::Art picture = stage->picture();
        if (picture.valid()) card.image(picture, placed(x, y, {0.0f, 0.0f, wide_, tall_}, u));
    }
    // A stack's count on its picture's foot, over the model, as the bag prints it.
    for (const Cell& one : cells_) {
        if (one.count <= 1) continue;
        const Box box = placed(x, y, one.box, u);
        card.shadowed(box.x, box.bottom() - 4.0f * u, 12.0f * u, kItemWhite, gfx::rgba(0.0f, 0.0f, 0.0f, 0.8f),
                      std::max(1.0f, u), std::to_string(one.count), gfx::Align::Right, box.h - 4.0f * u);
    }
    if (cardContent_ > cardTall_) {
        const float track = cardTall_, thumb = std::max(24.0f, track * cardTall_ / cardContent_);
        const float atY = (track - thumb) * cardScroll_ / std::max(1.0f, cardContent_ - cardTall_);
        card.rect(placed(x, y, {wide_ - 7.0f, cardTop_ + atY, 3.0f, thumb}, u), style::kIron);
    }
}

}  // namespace mu::game
