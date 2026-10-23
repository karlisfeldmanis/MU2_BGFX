#include "game/ui/helper_window.h"

#include <algorithm>
#include <cmath>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "sim/realm.h"
#include "sim/skills.h"
#include "sim/wear.h"

namespace mu::game {
namespace {

using gfx::Box;

// The window's measures, in tip::unit(): the proposal's pixels over 1.17, as the quest log's are.
constexpr float kSide = style::kPad;
constexpr float kKick = 20.0f;      // a group's heading, down to its first row
constexpr float kGroupGap = 13.0f;
constexpr float kSkillRow = 39.0f, kSkillGap = 3.0f;
constexpr float kIconW = 24.0f, kIconH = 34.0f;
constexpr float kChipW = 96.0f, kChipH = 27.0f;
constexpr float kMeterRow = 24.0f, kMeterLabel = 56.0f, kMeterValue = 36.0f, kSegHeight = 12.0f;
constexpr float kPickH = 27.0f, kPickGap = 3.0f;
constexpr float kGoH = 40.0f;
constexpr float kTabsTop = style::kHead + 6.0f, kTabsH = 30.0f;
// A setting's row, as the settings window's (controls::row) but lower, and its choice's box.
constexpr float kSetRow = 34.0f, kSetGap = 4.0f, kSwitchBoxW = 84.0f, kRangeBoxW = 156.0f, kInviteBoxW = 168.0f;
constexpr float kWord = 14.5f, kSmall = 11.5f, kChipWord = 13.0f;

// The quest log's gold for what is chosen (the user, 2026-09-30: 'selected row in golden tones').
constexpr uint32_t kGold = gfx::rgba(0.722f, 0.600f, 0.353f);
constexpr uint32_t kGoldHi = gfx::rgba(0.886f, 0.776f, 0.541f);
constexpr uint32_t kGoldDk = gfx::rgba(0.431f, 0.353f, 0.196f);
// The picks in their names' own colours (game/ui/describe.cpp's ladder).
constexpr uint32_t kJewelInk = gfx::rgba(1.0f, 0.820f, 0.0f);
constexpr uint32_t kRuneInk = gfx::rgba(0.298f, 0.608f, 1.0f);
constexpr uint32_t kLegendInk = gfx::rgba(1.0f, 0.502f, 0.0f);
constexpr uint32_t kExcellentInk = gfx::rgba(0.239f, 0.863f, 0.290f);

constexpr const char* kKeyNames[6] = {"Q", "W", "E", "R", "T", "RMB"};
constexpr const char* kRangeNames[3] = {"Near", "Mid", "Far"};
constexpr const char* kInviteNames[3] = {"Ask", "Join", "Decline"};
constexpr const char* kOffOn[2] = {"Off", "On"};
// The caps' ampersand reads as a scrawl at the tab's size (the user, 2026-10-21).
constexpr const char* kPageNames[2] = {"Hunt", "Loot \xC2\xB7 Party"};
struct Pick {
    const char* name;
    uint8_t bit;
    uint32_t ink;
};
// Four a row, two rows: the window keeps its height (Pets made seven, Boxes eight).
constexpr int kPickCount = 8, kPickColumns = 4;
constexpr Pick kPicks[kPickCount] = {{"Jewels", HelperSettings::kPickJewels, kJewelInk},
                                     {"Runes", HelperSettings::kPickRunes, kRuneInk},
                                     {"Pets", HelperSettings::kPickPets, style::kBoneHi},
                                     {"+Socket", HelperSettings::kPickSockets, style::kBoneHi},
                                     {"Legendary", HelperSettings::kPickLegendary, kLegendInk},
                                     {"Excellent", HelperSettings::kPickExcellent, kExcellentInk},
                                     {"Boxes", HelperSettings::kPickBoxes, style::kBoneHi},
                                     {"All", HelperSettings::kPickAll, style::kBoneHi}};

Box placed(float x, float y, const Box& b, float u) {
    return {std::round(x + b.x * u), std::round(y + b.y * u), std::round(b.w * u), std::round(b.h * u)};
}

uint32_t faded(uint32_t abgr, float alpha) {
    return (abgr & 0x00FFFFFFu) | (uint32_t(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f)) << 24);
}

// A word centred in a box, in the buttons' face.
void centred(gfx::Canvas& canvas, const Box& box, float size, uint32_t ink, const std::string& text) {
    const float base = controls::middle(box.y, box.h, size);
    const gfx::Face* face = controls::wordFace();
    if (face == nullptr) {
        controls::label(canvas, box.x + 4.0f, base, size, ink, text);
        return;
    }
    const float wide = face->measure(size, text);
    canvas.lettered(*face, controls::wordTexture(), std::round(box.midX() - wide * 0.5f), base, size, 0.0f, ink, text);
}

// A chip: a rim and its fill, gold when it is on.
void chip(gfx::Canvas& canvas, const Box& box, bool on, bool hot, float u) {
    const float line = std::max(1.0f, std::round(u));
    canvas.rect(box, on ? (hot ? kGold : kGoldDk) : hot ? style::kIronHi : style::kIronLo);
    const Box in = box.grown(-line);
    canvas.rect(in, style::kAsh1);
    if (on) canvas.shade(in, faded(kGold, 0.22f), faded(kGold, 0.22f), faded(kGold, 0.07f), faded(kGold, 0.07f));
}

std::string clockOf(double seconds) {
    const int all = int(seconds);
    char text[16];
    if (all >= 3600) std::snprintf(text, sizeof text, "%d:%02d:%02d", all / 3600, all / 60 % 60, all % 60);
    else std::snprintf(text, sizeof text, "%d:%02d", all / 60, all % 60);
    return text;
}

}  // namespace

void HelperWindow::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    arts_ = arts;
}

void HelperWindow::close() {
    canvas_.clear();
    up_ = false;
    built_ = false;
}

void HelperWindow::show() {
    up_ = true;
    over_ = pressing_ = -1;
    built_ = false;
}

void HelperWindow::hide() {
    if (up_) close();
}

bool HelperWindow::covers(float x, float y) const {
    if (!up_) return false;
    return x >= x_ && y >= y_ && x < x_ + wide_ * unit_ && y < y_ + tall_ * unit_;
}

void HelperWindow::layout(const Play& play, const HelperSettings& settings, const int32_t* bar) {
    const float inner = wide_ - kSide * 2.0f;
    for (Box& b : ranges_) b = {};
    for (Box& b : picks_) b = {};
    for (Box& b : invites_) b = {};
    for (auto& meter : segs_) {
        for (Box& b : meter) b = {};
    }
    back_ = quest_ = aid_ = trades_ = repair_ = {};
    rangeRow_ = rangeBox_ = inviteRow_ = inviteBox_ = {};
    rows_.clear();
    for (int t = 0; t < 2; ++t) tabs_[t] = {kSide + float(t) * inner * 0.5f, kTabsTop, inner * 0.5f, kTabsH};
    go_ = {kSide, tall_ - kSide - kGoH, inner, kGoH};
    float y = kTabsTop + kTabsH + 14.0f;
    const auto setRow = [&](float choiceW, Box& row, Box& choice) {
        row = {kSide, y, inner, kSetRow};
        choice = {kSide + inner - 5.0f - choiceW, y + 5.0f, choiceW, kSetRow - 10.0f};
        y += kSetRow + kSetGap;
    };
    const auto toggle = [&](Toggle& t) { setRow(kSwitchBoxW, t.row, t.knob); };
    // A choice's parts as controls::choice lays them: equal shares of its box.
    const auto parts = [](const Box& choice, Box* out, int count) {
        for (int i = 0; i < count; ++i) {
            out[i] = {choice.x + choice.w * float(i) / float(count), choice.y, choice.w / float(count), choice.h};
        }
    };

    if (page_ == 1) {
        pickTop_ = y;
        y += kKick;
        const float pickW = (inner - kPickGap * float(kPickColumns - 1)) / float(kPickColumns);
        for (int i = 0; i < kPickCount; ++i) {
            picks_[i] = {kSide + float(i % kPickColumns) * (pickW + kPickGap),
                         y + float(i / kPickColumns) * (kPickH + kPickGap), pickW, kPickH};
        }
        y += 2.0f * kPickH + kPickGap + kGroupGap;

        partyTop_ = y;
        y += kKick;
        setRow(kInviteBoxW, inviteRow_, inviteBox_);
        parts(inviteBox_, invites_, 3);
        toggle(aid_);
        toggle(trades_);
        toggle(repair_);
        return;
    }

    huntTop_ = y;
    y += kKick;
    setRow(kRangeBoxW, rangeRow_, rangeBox_);
    parts(rangeBox_, ranges_, 3);
    toggle(quest_);
    toggle(back_);
    y += kGroupGap - kSetGap;

    skillsTop_ = y;
    y += kKick;
    const sim::Realm& realm = play.realm();
    for (int n = 0; n < Hud::kSkillBoxes; ++n) {
        const int key = n == 0 ? Hud::kRightSlot : n - 1;  // the right button first, then Q to T
        Row row;
        row.key = key;
        row.skill = bar ? bar[key] : 0;
        const sim::SkillRow* skill = row.skill > 0 ? sim::skillNumbered(row.skill) : nullptr;
        if (skill == nullptr || !realm.knows(row.skill)) row.skill = 0;
        if (row.skill != 0) {
            row.kind = helperKind(*skill, key == Hud::kRightSlot);
            row.use = settings.use(row.skill, row.kind);
            const sim::Wearer wearer = realm.wearer();
            row.suits = skill->suits(skill->onSelf() ? wearer.offHand : wearer.hand);
        }
        row.box = {kSide, y, inner, kSkillRow};
        row.chip = {row.box.right() - 6.0f - kChipW, y + (kSkillRow - kChipH) * 0.5f, kChipW, kChipH};
        rows_.push_back(row);
        y += kSkillRow + kSkillGap;
    }
    y += kGroupGap - kSkillGap;

    drinkTop_ = y;
    y += kKick;
    const float segLeft = kSide + kMeterLabel, segWide = inner - kMeterLabel - kMeterValue - 8.0f;
    const float pitch = (segWide + 3.0f) / 9.0f;
    for (int m = 0; m < 2; ++m) {
        for (int s = 0; s < 9; ++s) {
            segs_[m][s] = {segLeft + float(s) * pitch, y + (kMeterRow - kSegHeight) * 0.5f, pitch - 3.0f, kSegHeight};
        }
        y += kMeterRow + 4.0f;
    }
}

void HelperWindow::update(float seconds, const Play& play, HelperSettings& settings, const int32_t* bar,
                          bool helping, const Helper::Stats& stats, float width, float height,
                          const Pointer& pointer, Result* out) {
    (void)seconds;
    (void)width;
    if (!up_ || !play.isOpen() || play.realm().tables() == nullptr) return;
    const sim::Realm& realm = play.realm();
    unit_ = tip::unit();
    wide_ = panel::kWidth * panel::scale() / unit_;
    tall_ = panel::kHeight * panel::scale() / unit_;
    x_ = panel::sideMargin();
    y_ = std::round(panel::panelY(height));
    layout(play, settings, bar);

    const float ux = (pointer.x - x_) / unit_, uy = (pointer.y - y_) / unit_;
    const float side = style::kSmallSquare;
    const Box closeBox{wide_ - style::kPad - side, (style::kHead - side) * 0.5f, side, side};
    int over = -1;
    if (covers(pointer.x, pointer.y)) {
        if (closeBox.has(ux, uy)) over = 0;
        for (int i = 0; i < 3 && over < 0; ++i) {
            if (ranges_[i].has(ux, uy)) over = 10 + i;
        }
        if (over < 0 && back_.row.has(ux, uy)) over = 20;
        if (over < 0 && quest_.row.has(ux, uy)) over = 21;
        if (over < 0 && aid_.row.has(ux, uy)) over = 22;
        if (over < 0 && trades_.row.has(ux, uy)) over = 23;
        if (over < 0 && repair_.row.has(ux, uy)) over = 27;
        for (int i = 0; i < 3 && over < 0; ++i) {
            if (invites_[i].has(ux, uy)) over = 24 + i;
        }
        for (int t = 0; t < 2 && over < 0; ++t) {
            if (t != page_ && tabs_[t].has(ux, uy)) over = 95 + t;
        }
        for (size_t i = 0; i < rows_.size() && over < 0; ++i) {
            if (rows_[i].skill != 0 && helperUses(rows_[i].kind).size() > 1 && rows_[i].box.has(ux, uy)) {
                over = 30 + int(i);
            }
        }
        for (int m = 0; m < 2 && over < 0; ++m) {
            for (int s = 0; s < 9 && over < 0; ++s) {
                if (segs_[m][s].grown(2.0f).has(ux, uy)) over = 50 + m * 10 + s;
            }
        }
        for (int i = 0; i < kPickCount && over < 0; ++i) {
            if (picks_[i].has(ux, uy)) over = 80 + i;
        }
        if (over < 0 && go_.has(ux, uy)) over = 90;
    }
    over_ = over;
    if (pointer.pressed) pressing_ = over_;
    if (pointer.released) {
        if (pressing_ >= 0 && pressing_ == over_ && out) {
            const int at = pressing_;
            out->clicked = true;
            if (at == 0) {
                out->close = true;
            } else if (at >= 10 && at < 13) {
                settings.range = at - 10;
                out->changed = true;
            } else if (at == 20) {
                settings.back = !settings.back;
                out->changed = true;
            } else if (at == 21) {
                settings.quest = !settings.quest;
                out->changed = true;
            } else if (at == 22) {
                settings.aidParty = !settings.aidParty;
                out->changed = true;
            } else if (at == 23) {
                settings.noTrades = !settings.noTrades;
                out->changed = true;
            } else if (at == 27) {
                settings.repair = !settings.repair;
                out->changed = true;
            } else if (at >= 24 && at < 27) {
                settings.invites = uint8_t(at - 24);
                out->changed = true;
            } else if (at >= 95 && at < 97) {
                page_ = at - 95;
                over_ = -1;
            } else if (at >= 30 && at < 30 + int(rows_.size())) {
                const Row& row = rows_[size_t(at - 30)];
                const std::vector<HelperUse>& offered = helperUses(row.kind);
                const auto now = std::find(offered.begin(), offered.end(), row.use);
                const size_t next = now == offered.end() ? 0 : size_t(now - offered.begin() + 1) % offered.size();
                settings.uses[row.skill] = offered[next];
                out->changed = true;
            } else if (at >= 50 && at < 70) {
                const int meter = (at - 50) / 10, line = ((at - 50) % 10 + 1) * 10;
                const sim::AutoDrink drink = realm.autoDrink();
                const int was = meter == 0 ? drink.health : drink.mana;
                const int now = was == line ? 0 : line;
                out->health = meter == 0 ? now : drink.health;
                out->mana = meter == 1 ? now : drink.mana;
            } else if (at >= 80 && at < 80 + kPickCount) {
                settings.picks ^= kPicks[at - 80].bit;
                out->changed = true;
            } else if (at == 90) {
                if (helping) out->stop = true;
                else out->start = true;
            }
        }
        pressing_ = -1;
    }
    layout(play, settings, bar);

    // Drawn again only when something it shows has changed.
    const sim::AutoDrink drink = realm.autoDrink();
    std::vector<int64_t> now = {over_, pressing_, int64_t(x_), int64_t(y_), int64_t(unit_ * 1000.0f), settings.range,
                                settings.back, settings.picks, drink.health, drink.mana, helping,
                                helping ? int64_t(stats.seconds) : 0, stats.kills, stats.picked, page_,
                                settings.quest, settings.aidParty, settings.noTrades, settings.invites,
                                stats.quests, settings.repair, realm.selfMending()};
    for (const Row& row : rows_) {
        now.push_back(row.skill);
        now.push_back(int(row.use) + (row.suits ? 0 : 16));
    }
    if (built_ && now == drawn_) return;
    drawn_ = std::move(now);
    built_ = true;
    rebuild(play, settings, helping, stats);
}

void HelperWindow::rebuild(const Play& play, const HelperSettings& settings, bool helping, const Helper::Stats& stats) {
    canvas_.clear();
    const sim::Realm& realm = play.realm();
    const float u = unit_, x = x_, y = y_;
    const float inner = wide_ - kSide * 2.0f;
    const float line = std::max(1.0f, std::round(u));
    const auto sx = [&](float ux) { return x + ux * u; };
    const auto sy = [&](float uy) { return y + uy * u; };
    const auto at = [&](const Box& b) { return placed(x, y, b, u); };
    const auto state = [&](int which, bool off) {
        return controls::State{over_ == which ? 1.0f : 0.0f, pressing_ == which && over_ == which, off};
    };
    const auto kicker = [&](float top, const char* text) {
        controls::caps(canvas_, sx(kSide + 2.0f), sy(top + 12.0f), style::kKickerSize * u, style::kAshInk, text);
    };

    controls::frame(canvas_, at({0.0f, 0.0f, wide_, tall_}), u, "Helper");
    const float side = style::kSmallSquare;
    controls::square(canvas_, at({wide_ - style::kPad - side, (style::kHead - side) * 0.5f, side, side}),
                     controls::Glyph::Close, state(0, false), u);

    // ---- the pages' tabs -------------------------------------------------------------------------
    for (int t = 0; t < 2; ++t) controls::tab(canvas_, at(tabs_[t]), kPageNames[t], page_ == t, state(95 + t, false), u);

    // A setting as the settings window sets one (game/ui/menu.cpp): its row, the mark down its
    // left red under the pointer, and its choice at the right, the chosen part in blood; the other
    // part lit under the pointer. `dim` veils it when it does not hold now.
    const auto setting = [&](const Box& row, const Box& box, const char* word, const char* const* words,
                             int count, int on, int over, bool dim) {
        controls::row(canvas_, at(row), word, over >= 0 ? 1.0f : 0.0f, u);
        const int lit = over < 0 ? -1 : count == 2 ? 1 - on : over;
        controls::choice(canvas_, at(box), words, count, on, lit, u);
        if (dim) canvas_.rect(at(row), faded(style::kVoid, 0.5f));
    };
    const auto toggle = [&](const Toggle& t, int which, bool on, const char* word, bool dim) {
        setting(t.row, t.knob, word, kOffOn, 2, on ? 1 : 0, over_ == which ? 0 : -1, dim);
    };

    if (page_ == 1) {
        // ---- pick up -----------------------------------------------------------------------------
        kicker(pickTop_, "Pick up");
        for (int i = 0; i < kPickCount; ++i) {
            const bool on = (settings.picks & kPicks[i].bit) != 0;
            const Box box = at(picks_[i]);
            const bool hot = over_ == 80 + i;
            canvas_.rect(box, on ? (hot ? kGold : kGoldDk) : hot ? style::kIronHi : style::kIronLo);
            canvas_.rect(box.grown(-line), style::kAsh1);
            if (on) {
                canvas_.shade(box.grown(-line), faded(kGold, 0.16f), faded(kGold, 0.16f), faded(kGold, 0.04f),
                              faded(kGold, 0.04f));
            }
            centred(canvas_, box, kChipWord * u, on ? kPicks[i].ink : style::kAshInk, kPicks[i].name);
        }

        // ---- while helping: the party and the trades ---------------------------------------------
        kicker(partyTop_, "While helping");
        setting(inviteRow_, inviteBox_, "Party invites", kInviteNames, 3, settings.invites,
                over_ >= 24 && over_ < 27 ? over_ - 24 : -1, false);
        toggle(aid_, 22, settings.aidParty, "Buff and heal my party", false);
        toggle(trades_, 23, settings.noTrades, "Decline trades", false);
        // His own hand mends from kSelfRepairLevel, as the bag's hammer does; under it, veiled.
        const bool young = !realm.selfMending() && realm.hero().alive();
        const std::string mendWord =
            young ? "Repair my gear \xC2\xB7 level " + std::to_string(sim::kSelfRepairLevel) : "Repair my gear";
        toggle(repair_, 27, settings.repair, mendWord.c_str(), young);
    } else {
        // ---- hunt --------------------------------------------------------------------------------
        kicker(huntTop_, "Hunt");
        setting(rangeRow_, rangeBox_, "Range", kRangeNames, 3, settings.range,
                over_ >= 10 && over_ < 13 ? over_ - 10 : -1, false);
        toggle(quest_, 21, settings.quest, "Follow my quest", false);
        // Following a quest it has no spot to walk back to.
        toggle(back_, 20, settings.back, "Walk back to my spot", settings.quest);
    }

    if (page_ == 0) {
        // ---- skills: the bar's keys, each its icon, its name, its kind and its use ------------------
        kicker(skillsTop_, "Skills");
        controls::ranged(canvas_, sx(kSide + inner), sy(skillsTop_ + 12.0f), kSmall * u, style::kAshInk, "click to change");
        for (size_t i = 0; i < rows_.size(); ++i) {
            const Row& row = rows_[i];
            const Box box = at(row.box);
            const bool right = row.key == Hud::kRightSlot;
            const bool hot = over_ == 30 + int(i);
            canvas_.rect(box, right && row.skill ? kGoldDk : hot ? style::kIronHi : style::kIronLo);
            canvas_.rect(box.grown(-line), style::kAsh1);
            const Box icon = at({row.box.x + 9.0f, row.box.y + (kSkillRow - kIconH) * 0.5f, kIconW, kIconH});
            const bool off = row.skill == 0 || row.use == HelperUse::Off || row.use == HelperUse::Never || !row.suits;
            if (row.skill != 0 && arts_) {
                const gfx::Art& art = arts_->get("skill_" + std::to_string(row.skill));
                canvas_.rect(icon.grown(line), style::kSeam);
                if (art.valid()) canvas_.image(art, icon, off ? faded(0x00999999u, 0.45f) : 0xFFFFFFFFu);
            } else {
                canvas_.rect(icon, style::kVoid);
                canvas_.outline(icon, line, style::kIronDk);
            }
            // The key it is on, in a cap over the icon's foot.
            const float capW = right ? 26.0f : 14.0f;
            controls::keycap(canvas_, at({row.box.x + 4.0f, row.box.bottom() - 15.0f, capW, 12.0f}), kKeyNames[row.key], u);
            const float textX = row.box.x + 9.0f + kIconW + 12.0f;
            if (row.skill == 0) {
                controls::label(canvas_, sx(textX), sy(controls::middle(row.box.y, kSkillRow, kWord)), kWord * u,
                                style::kAshInk2, "Empty");
                continue;
            }
            const sim::SkillRow* skill = sim::skillNumbered(row.skill);
            controls::label(canvas_, sx(textX), sy(row.box.y + 17.0f), kWord * u, off ? style::kAshInk : style::kBoneHi,
                            skill ? skill->name : "");
            // A skill his weapon cannot throw says so, and is passed over while it fights.
            controls::label(canvas_, sx(textX), sy(row.box.y + 31.0f), kSmall * u, style::kAshInk,
                            row.suits ? helperKindName(row.kind) : "not with this weapon");
            const Box chipBox = at(row.chip);
            const bool fixed = helperUses(row.kind).size() < 2;
            if (row.use == HelperUse::Never) {
                centred(canvas_, chipBox, kChipWord * u, style::kAshInk2, helperUseName(row.use));
            } else if (fixed) {
                chip(canvas_, chipBox, true, false, u);
                centred(canvas_, chipBox, kChipWord * u, kGoldHi, helperUseName(row.use));
            } else {
                canvas_.rect(chipBox, hot ? style::kIronHi : off ? style::kIronDk : style::kIron);
                canvas_.rect(chipBox.grown(-line), off ? style::kVoid : style::kAsh2);
                centred(canvas_, chipBox, kChipWord * u, off ? style::kAshInk : style::kBoneHi, helperUseName(row.use));
            }
        }

        // ---- drink below: the auto-drink lines ------------------------------------------------------
        kicker(drinkTop_, "Drink below");
        const sim::AutoDrink drink = realm.autoDrink();
        for (int m = 0; m < 2; ++m) {
            const int value = m == 0 ? drink.health : drink.mana;
            const float rowTop = segs_[m][0].y - (kMeterRow - kSegHeight) * 0.5f;
            controls::label(canvas_, sx(kSide), sy(controls::middle(rowTop, kMeterRow, kWord)), kWord * u, style::kBoneHi,
                            m == 0 ? "Health" : "Mana");
            for (int s = 0; s < 9; ++s) {
                const bool lit = (s + 1) * 10 <= value;
                const bool hot = over_ == 50 + m * 10 + s;
                const Box seg = at(segs_[m][s]);
                canvas_.rect(seg, lit ? (m == 0 ? style::kLife : style::kMana) : hot ? style::kIron : style::kIronLo);
            }
            controls::ranged(canvas_, sx(kSide + inner), sy(controls::middle(rowTop, kMeterRow, kWord)), kWord * u,
                             style::kBoneHi, value > 0 ? std::to_string(value) + "%" : std::string("Off"));
        }
    }

    // ---- the foot: Start, or what it has done and Stop -------------------------------------------
    if (helping) {
        const float base = go_.y - 10.0f;
        float cx = kSide;
        cx += controls::capsWidth(kSmall * u, "Helping") / u;
        controls::caps(canvas_, sx(kSide), sy(base), kSmall * u, style::kLive, "Helping");
        cx += 10.0f;
        cx += controls::label(canvas_, sx(cx), sy(base), kWord * u, style::kBoneHi, clockOf(stats.seconds)) / u + 10.0f;
        controls::label(canvas_, sx(cx), sy(base), kWord * u, style::kBone2,
                        settings.quest ? std::to_string(stats.quests) + (stats.quests == 1 ? " quest" : " quests")
                                       : std::to_string(stats.kills) + (stats.kills == 1 ? " kill" : " kills"));
        controls::ranged(canvas_, sx(kSide + inner), sy(base), kWord * u, style::kBone2,
                         std::to_string(stats.picked) + " picked up");
        controls::button(canvas_, at(go_), "Stop  \xC2\xB7  H", controls::Kind::Secondary, state(90, false), u);
    } else {
        controls::button(canvas_, at(go_), "Start helping  \xC2\xB7  H", controls::Kind::Primary, state(90, false), u);
    }
}

}  // namespace mu::game
