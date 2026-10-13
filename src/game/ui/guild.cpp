#include "game/ui/guild.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "core/log.h"
#include "game/play.h"
#include "game/roster.h"
#include "game/ui/controls.h"
#include "game/ui/guild_mark.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;

// ---- the guild window, in MU's panel units (190 x 429) ----------------------------------------
constexpr float kLeft = panel::kEdge, kRight = panel::kWellRight;
constexpr float kWideAll = kRight - kLeft;
constexpr float kHeadTop = 44.0f, kCrest = 30.0f;
constexpr float kNoticeTop = 82.0f, kNoticeTall = 34.0f;
constexpr float kTabsTop = 122.0f, kTabsTall = 18.0f;
constexpr float kRollTop = 145.0f, kCardTall = 24.0f, kCardGap = 2.5f;
constexpr int kPerPage = 8;
constexpr float kPagerTop = 362.0f, kPagerTall = 13.0f;
constexpr float kFootRow = 387.0f, kFootRowTall = 20.0f, kSaidBase = 421.0f;
constexpr Box kInviteField{kLeft, kFootRow, 112.0f, kFootRowTall};
constexpr Box kInviteButton{kLeft + 116.0f, kFootRow, kWideAll - 116.0f, kFootRowTall};
constexpr Box kQuietButton{kRight - 46.0f, kSaidBase - 10.0f, 46.0f, 13.0f};
constexpr Box kPagerLeft{95.0f - 34.0f, kPagerTop, 16.0f, kPagerTall};
constexpr Box kPagerRight{95.0f + 18.0f, kPagerTop, 16.0f, kPagerTall};
constexpr size_t kNoticeMost = 60, kNameMost = 8;
// The confirm box over the foot.
constexpr Box kAskBox{kLeft, 268.0f, kWideAll, 104.0f};

// Inks the page names (the chat's: game/ui/chat.cpp).
constexpr uint32_t kGuildInk = gfx::rgba(0.576f, 0.839f, 0.494f);
constexpr uint32_t kFitsInk = style::kFits;

uint64_t mixed(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ull; }
uint64_t mixs(uint64_t h, const std::string& s) { return mixed(h, std::hash<std::string>{}(s)); }

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

// A line cut to its words within `wide` pixels; at most `most` lines.
std::vector<std::string> wrapped(const std::string& text, float size, float wide, size_t most) {
    std::vector<std::string> lines;
    std::string line, word;
    const auto flush = [&]() {
        if (word.empty()) return;
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && controls::labelWidth(size, tried) > wide) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
        word.clear();
    };
    for (const char c : text) {
        if (c == ' ' || c == '\n') {
            flush();
            if (c == '\n' && !line.empty()) {
                lines.push_back(line);
                line.clear();
            }
        } else {
            word += c;
        }
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    if (lines.size() > most) lines.resize(most);
    return lines;
}

// A stroke from one point to another, `wide` across: a tick's or a cross's (chat.cpp's).
void stroke(gfx::Canvas& canvas, float x0, float y0, float x1, float y1, float wide, uint32_t ink) {
    const float dx = x1 - x0, dy = y1 - y0, length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) return;
    const float nx = -dy / length * wide * 0.5f, ny = dx / length * wide * 0.5f;
    const float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    const uint32_t inks[4] = {ink, ink, ink, ink};
    canvas.polygon(xy, inks, 4);
}

// A map's name as the minimap says it: the folder's, its first letter up (party_bar.cpp's).
std::string mapName(std::string world) {
    if (!world.empty()) world[0] = char(std::toupper(uint8_t(world[0])));
    return world;
}

std::string seenWords(int32_t seconds) {
    if (seconds < 3600) return "seen " + std::to_string(std::max(1, seconds / 60)) + " min ago";
    if (seconds < 86400) return "seen " + std::to_string(seconds / 3600) + " h ago";
    if (seconds < 2 * 86400) return "seen yesterday";
    return "seen " + std::to_string(seconds / 86400) + " days ago";
}

// The keys typed into a field: printable letters in, backspaces out, `most` at the most.
void typeInto(std::string& text, const GuildKeys& keys, size_t most, bool lettersOnly = false) {
    for (int i = 0; i < keys.backspaces && !text.empty(); ++i) {
        do {
            text.pop_back();
        } while (!text.empty() && (uint8_t(text.back()) & 0xC0) == 0x80);
    }
    for (const char c : keys.typed) {
        if (uint8_t(c) < 32) continue;
        if (lettersOnly && !std::isalnum(uint8_t(c))) continue;
        if (text.size() >= most) break;
        text += c;
    }
}

}  // namespace

// ================================================================================================
// GuildWindow
// ================================================================================================

void GuildWindow::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    arts_ = arts;
}

void GuildWindow::hide() {
    up_ = false;
    focus_ = Focus::None;
    confirm_ = Confirm::None;
    menu_ = -1;
    canvas_.clear();
    drawnOnce_ = false;
}

bool GuildWindow::covers(float x, float y) const {
    if (!up_) return false;
    const float k = panel::scale();
    if (menu_ >= 0 && panel::scaled(x_, y_, menuBox_).has(x, y)) return true;
    return Box{x_, y_, panel::kWidth * k, panel::kHeight * k}.has(x, y);
}

void GuildWindow::layout(const net::GuildState& state, const std::string& me) {
    (void)me;
    order_.clear();
    for (size_t i = 0; i < state.members.size(); ++i) {
        if (!allTab_ && state.members[i].world.empty()) continue;
        order_.push_back(i);
    }
    std::stable_sort(order_.begin(), order_.end(), [&](size_t a, size_t b) {
        if (a == 0 || b == 0) return a == 0 && b != 0;
        const net::GuildState::Member &ma = state.members[a], &mb = state.members[b];
        const bool oa = !ma.world.empty(), ob = !mb.world.empty();
        if (oa != ob) return oa;
        if (!oa) return ma.seen < mb.seen;
        const bool ha = ma.here != 0, hb = mb.here != 0;
        if (ha != hb) return ha;
        return ma.level > mb.level;
    });
    pages_ = std::max(1, int((order_.size() + kPerPage - 1) / kPerPage));
    page_ = std::clamp(page_, 0, pages_ - 1);
    rows_.clear();
    for (size_t i = size_t(page_) * kPerPage; i < order_.size() && rows_.size() < size_t(kPerPage); ++i) {
        const float top = kRollTop + float(rows_.size()) * (kCardTall + kCardGap);
        rows_.push_back({order_[i], {kLeft, top, kWideAll, kCardTall}});
    }
}

void GuildWindow::update(float seconds, float width, float height, int column, Play& play, const Pointer& pointer,
                         const GuildKeys& keys, const std::string& said, GuildRequests* out) {
    if (!up_) return;
    const net::GuildState& state = play.guild();
    std::string me;
    play.playerName(play.realm().hero().id, &me, nullptr);
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    caret_ += seconds;
    ownSaidAge_ += seconds;
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const bool master = state.id != 0 && !state.members.empty() && state.members.front().name == me;
    if (state.id == 0) {
        focus_ = Focus::None;
        confirm_ = Confirm::None;
        menu_ = -1;
    }
    layout(state, me);

    const auto ask = [&](net::GuildWhat what, const std::string& name, const std::string& text = {}) {
        core::logf("guild window: asks %d of %s", int(what), name.c_str());
        if (!out) return;
        net::GuildAsk a;
        a.what = what;
        a.name = name;
        a.text = text;
        out->asks.push_back(a);
        out->clicked = true;
    };

    // ---- the keyboard, for the field that has it
    if (focus_ == Focus::Invite) {
        typeInto(invite_, keys, 16);
        if (keys.enter) {
            if (!invite_.empty()) ask(net::GuildWhat::Invite, invite_);
            invite_.clear();
            focus_ = Focus::None;
        }
        if (keys.escape) focus_ = Focus::None;
    } else if (focus_ == Focus::Notice) {
        typeInto(notice_, keys, kNoticeMost);
        if (keys.enter) {
            ask(net::GuildWhat::Notice, {}, notice_);
            focus_ = Focus::None;
        }
        if (keys.escape) focus_ = Focus::None;
    } else if (focus_ == Focus::Disband) {
        typeInto(disband_, keys, kNameMost);
        if (keys.enter && lower(disband_) == lower(state.name)) {
            ask(net::GuildWhat::Disband, state.name);
            confirm_ = Confirm::None;
            focus_ = Focus::None;
        }
        if (keys.escape) {
            confirm_ = Confirm::None;
            focus_ = Focus::None;
        }
    }

    // ---- what the pointer is on
    std::string on;
    const bool inside = covers(pointer.x, pointer.y);
    if (inside) {
        if (panel::frameClose().has(ux, uy)) on = "close";
        else if (menu_ >= 0 && menuBox_.has(ux, uy)) {
            const int row = int((uy - menuBox_.y - 18.0f) / 14.0f);
            if (row >= 0 && row < int(menuRows_.size())) on = "menu:" + std::to_string(row);
            else on = "menu";
        } else if (confirm_ != Confirm::None) {
            const Box no{kAskBox.x + 8.0f, kAskBox.bottom() - 26.0f, kAskBox.w * 0.5f - 12.0f, 18.0f};
            const Box yes{kAskBox.midX() + 4.0f, no.y, no.w, no.h};
            if (no.has(ux, uy)) on = "no";
            else if (yes.has(ux, uy)) on = "yes";
            else on = "ask";
        } else if (state.id != 0) {
            if (Box{kLeft, kNoticeTop, kWideAll, kNoticeTall}.has(ux, uy)) on = "notice";
            else if (Box{kLeft, kTabsTop, kWideAll * 0.5f, kTabsTall}.has(ux, uy)) on = "tab:on";
            else if (Box{kLeft + kWideAll * 0.5f, kTabsTop, kWideAll * 0.5f, kTabsTall}.has(ux, uy)) on = "tab:all";
            else if (kPagerLeft.has(ux, uy)) on = "prev";
            else if (kPagerRight.has(ux, uy)) on = "next";
            else if (master && kInviteField.has(ux, uy)) on = "field";
            else if (master && kInviteButton.has(ux, uy)) on = "invite";
            else if (kQuietButton.has(ux, uy)) on = master ? "disband" : "leave";
            else {
                for (const Row& r : rows_) {
                    if (r.box.has(ux, uy)) on = "card:" + std::to_string(r.member);
                }
            }
        }
    }
    hovered_ = on;

    if ((pointer.pressed || pointer.rightPressed) && inside) {
        const auto said = [&](const std::string& text) {
            ownSaid_ = text;
            ownSaidAge_ = 0.0f;
        };
        if (on == "close") {
            hide();
            if (out) out->clicked = true;
            return;
        }
        if (on.rfind("menu:", 0) == 0 && menu_ >= 0 && size_t(menu_) < state.members.size()) {
            const std::string& who = state.members[size_t(menu_)].name;
            const std::string& row = menuRows_[size_t(std::stoi(on.substr(5)))];
            if (row == "Whisper" && out) out->whisper = who;
            else if (row == "Invite to party" && out) {
                out->party = who;
                said("Party invite sent to " + who + ".");
            } else if (row == "Trade" && out) out->trade = who;
            else if (row == "Remove from guild") {
                confirm_ = Confirm::Remove;
                confirmName_ = who;
            } else if (row == "Leave the guild") {
                confirm_ = Confirm::Leave;
            }
            if (out) out->clicked = true;
            menu_ = -1;
        } else if (on == "menu") {
            // the menu's head: nothing
        } else if (confirm_ != Confirm::None) {
            if (on == "no") {
                confirm_ = Confirm::None;
                focus_ = Focus::None;
                if (out) out->clicked = true;
            } else if (on == "yes") {
                if (confirm_ == Confirm::Remove) ask(net::GuildWhat::Remove, confirmName_);
                if (confirm_ == Confirm::Leave) ask(net::GuildWhat::Leave, {});
                if (confirm_ == Confirm::Disband && lower(disband_) == lower(state.name)) {
                    ask(net::GuildWhat::Disband, state.name);
                }
                if (confirm_ != Confirm::Disband || lower(disband_) == lower(state.name)) {
                    confirm_ = Confirm::None;
                    focus_ = Focus::None;
                }
            }
        } else {
            menu_ = -1;
            if (focus_ == Focus::Notice && on != "notice") focus_ = Focus::None;
            if (focus_ == Focus::Invite && on != "field") focus_ = Focus::None;
            if (on == "notice" && master) {
                if (focus_ != Focus::Notice) notice_ = state.notice;
                focus_ = Focus::Notice;
            } else if (on == "tab:on" || on == "tab:all") {
                allTab_ = on == "tab:all";
                page_ = 0;
                if (out) out->clicked = true;
            } else if (on == "prev" && page_ > 0) {
                --page_;
                if (out) out->clicked = true;
            } else if (on == "next" && page_ + 1 < pages_) {
                ++page_;
                if (out) out->clicked = true;
            } else if (on == "field") {
                focus_ = Focus::Invite;
            } else if (on == "invite") {
                if (!invite_.empty()) ask(net::GuildWhat::Invite, invite_);
                else focus_ = Focus::Invite;
                invite_.clear();
            } else if (on == "disband") {
                confirm_ = Confirm::Disband;
                disband_.clear();
                focus_ = Focus::Disband;
                if (out) out->clicked = true;
            } else if (on == "leave") {
                confirm_ = Confirm::Leave;
                if (out) out->clicked = true;
            } else if (on.rfind("card:", 0) == 0) {
                const size_t member = size_t(std::stoul(on.substr(5)));
                if (member < state.members.size()) {
                    const net::GuildState::Member& m = state.members[member];
                    menuRows_.clear();
                    if (m.name == me) {
                        menuRows_.push_back(member == 0 ? "Masters disband" : "Leave the guild");
                    } else {
                        menuRows_.push_back("Whisper");
                        if (!m.world.empty()) menuRows_.push_back("Invite to party");
                        if (m.here != 0) menuRows_.push_back("Trade");
                        if (master) menuRows_.push_back("Remove from guild");
                    }
                    menu_ = long(member);
                    for (const Row& r : rows_) {
                        if (r.member != member) continue;
                        const float tall = 18.0f + 14.0f * float(menuRows_.size()) + 4.0f;
                        float top = r.box.bottom() + 2.0f;
                        if (top + tall > panel::kFootRule) top = r.box.y - tall - 2.0f;
                        menuBox_ = {kRight - 96.0f, top, 96.0f, tall};
                    }
                    if (out) out->clicked = true;
                }
            }
        }
    } else if (pointer.pressed && !inside) {
        menu_ = -1;
        if (focus_ == Focus::Invite || focus_ == Focus::Notice) focus_ = Focus::None;
    }

    // Redrawn on a change: the state, the pointer's place, the fields, the caret's blink.
    uint64_t key = 1469598103934665603ull;
    key = mixed(key, state.id);
    key = mixs(key, state.name + "|" + state.notice);
    key = mixed(key, uint64_t(state.cap));
    for (const net::GuildState::Member& m : state.members) {
        key = mixs(key, m.name + m.world);
        key = mixed(key, uint64_t(m.level) * 131 + m.here + uint64_t(m.seen / 60) * 7);
    }
    key = mixs(key, on + "|" + invite_ + "|" + notice_ + "|" + disband_ + "|" + confirmName_ + "|" + said + "|" +
                        (ownSaidAge_ < 4.0f ? ownSaid_ : std::string()));
    key = mixed(key, uint64_t(focus_) * 17 + uint64_t(confirm_) * 5 + uint64_t(allTab_) + uint64_t(page_) * 1000 +
                         uint64_t(menu_ + 1) * 100000);
    key = mixed(key, focus_ != Focus::None ? uint64_t(std::fmod(caret_, 1.0f) < 0.55f) : 0);
    key = mixed(key, uint64_t(std::lround(x_)) * 31 + uint64_t(std::lround(y_)) + uint64_t(std::lround(k * 1000.0f)));
    if (drawnOnce_ && key == drawnKey_) return;
    drawnKey_ = key;
    drawnOnce_ = true;
    draw(state, me, ownSaidAge_ < 4.0f ? ownSaid_ : said, seconds);
}

void GuildWindow::draw(const net::GuildState& state, const std::string& me, const std::string& said, float seconds) {
    (void)seconds;
    canvas_.clear();
    if (!arts_ || !up_) return;
    const float x = x_, y = y_, k = panel::scale(), u = tip::unit();
    const auto at = [&](const Box& units) { return panel::scaled(x, y, units); };
    const bool master = state.id != 0 && !state.members.empty() && state.members.front().name == me;
    panel::frame(canvas_, *arts_, x, y, "Guild");
    panel::close(canvas_, x, y, hovered_ == "close", false);

    if (state.id == 0) {
        // In none: how to join one, and where the Guild Master stands.
        float base = y + 150.0f * k;
        const std::string head = "You are in no guild.";
        controls::label(canvas_, x + (95.0f * k) - controls::labelWidth(19.0f * u, head) * 0.5f, base, 19.0f * u,
                        style::kBoneHi, head);
        base += 34.0f * u;
        const char* const steps[] = {"Right-click a guild master and choose Ask to join guild.",
                                     "Or accept a master's invite: it stands for 60 s.",
                                     "Or found your own at level 100, with the Guild Master in Devias."};
        int n = 1;
        for (const char* step : steps) {
            const Box dot{x + kLeft * k, base - 13.0f * u, 18.0f * u, 18.0f * u};
            canvas_.rect(dot, style::kAsh3);
            const std::string number = std::to_string(n++);
            controls::label(canvas_, dot.midX() - controls::labelWidth(13.0f * u, number) * 0.5f, base, 13.0f * u,
                            style::kBone2, number);
            for (const std::string& line : wrapped(step, 14.0f * u, (kWideAll - 26.0f) * k, 2)) {
                controls::label(canvas_, dot.right() + 8.0f * u, base, 14.0f * u, style::kBone, line);
                base += 19.0f * u;
            }
            base += 12.0f * u;
        }
        return;
    }

    // ---- the head: the mark, the name, how many
    {
        const Box well = at({kLeft, kHeadTop, kCrest, kCrest});
        controls::well(canvas_, well, u);
        const float pad = 3.0f * k;
        guildmark::draw(canvas_, {well.x + pad, well.y + pad, well.w - 2 * pad, well.h - 2 * pad}, state.mark);
        const float tx = well.right() + 8.0f * k;
        if (const gfx::Face* face = panel::titleFace()) {
            canvas_.lettered(*face, panel::titleTexture(), tx, y + (kHeadTop + 14.0f) * k, 21.0f * u,
                             21.0f * u * 0.06f, style::kBoneHi, state.name);
        } else {
            controls::label(canvas_, tx, y + (kHeadTop + 14.0f) * k, 21.0f * u, style::kBoneHi, state.name);
        }
        int online = 0;
        for (const auto& m : state.members) online += m.world.empty() ? 0 : 1;
        const std::string count = std::to_string(state.members.size()) + " / " + std::to_string(state.cap) +
                                  " members  \xC2\xB7  " + std::to_string(online) + " online";
        controls::label(canvas_, tx, y + (kHeadTop + 26.0f) * k, 13.0f * u, style::kAshInk, count);
    }

    // ---- the notice, written in place by the master
    {
        const Box box = at({kLeft, kNoticeTop, kWideAll, kNoticeTall});
        canvas_.rect(box, style::kVoid);
        canvas_.outline(box, std::max(1.0f, u),
                        focus_ == Focus::Notice ? style::kIron : hovered_ == "notice" && master ? style::kIronLo : style::kIronDk);
        const float left = box.x + 6.0f * u;
        controls::caps(canvas_, left, box.y + 13.0f * u, 10.0f * u, style::kAshInk, "Notice");
        const std::string right = focus_ == Focus::Notice
                                      ? std::to_string(notice_.size()) + "/60  \xC2\xB7  Enter keeps it"
                                      : master ? "Click to edit" : "";
        if (!right.empty()) {
            controls::ranged(canvas_, box.right() - 6.0f * u, box.y + 13.0f * u, 11.0f * u,
                             focus_ == Focus::Notice && notice_.size() >= kNoticeMost ? style::kDanger : style::kAshInk,
                             right);
        }
        const std::string text = focus_ == Focus::Notice ? notice_ : state.notice;
        const std::vector<std::string> lines = wrapped(text, 14.0f * u, box.w - 12.0f * u, 2);
        float base = box.y + 29.0f * u;
        if (lines.empty() && focus_ != Focus::Notice) {
            controls::label(canvas_, left, base, 14.0f * u, style::kAshInk2, "No notice.");
        }
        float end = left;
        for (const std::string& line : lines) {
            end = left + controls::label(canvas_, left, base, 14.0f * u, kGuildInk, line);
            base += 17.0f * u;
        }
        if (focus_ == Focus::Notice && std::fmod(caret_, 1.0f) < 0.55f) {
            const float cy = (lines.empty() ? box.y + 29.0f * u : base - 17.0f * u) - 12.0f * u;
            canvas_.rect({end + 1.0f * u, cy, 1.5f * u, 15.0f * u}, style::kBloodHi);
        }
    }

    // ---- Online and All
    {
        int online = 0;
        for (const auto& m : state.members) online += m.world.empty() ? 0 : 1;
        const Box onBox = at({kLeft, kTabsTop, kWideAll * 0.5f, kTabsTall});
        const Box allBox = at({kLeft + kWideAll * 0.5f, kTabsTop, kWideAll * 0.5f, kTabsTall});
        controls::State s;
        s.lift = hovered_ == "tab:on" ? 1.0f : 0.0f;
        controls::tab(canvas_, onBox, "Online " + std::to_string(online), !allTab_, s, u);
        s.lift = hovered_ == "tab:all" ? 1.0f : 0.0f;
        controls::tab(canvas_, allBox, "All " + std::to_string(state.members.size()), allTab_, s, u);
    }

    // ---- the cards
    for (const Row& r : rows_) {
        const net::GuildState::Member& m = state.members[r.member];
        const Box box = at(r.box);
        const bool lit = hovered_ == "card:" + std::to_string(r.member) || menu_ == long(r.member);
        canvas_.rect(box, lit ? style::kAsh3 : style::kAsh1);
        canvas_.outline(box, std::max(1.0f, u), r.member == 0 ? style::kIronLo : lit ? style::kIron : style::kIronDk);
        const bool on = !m.world.empty();
        const float left = box.x + 8.0f * u, right = box.right() - 8.0f * u;
        const float top = box.y + box.h * 0.46f, low = box.y + box.h * 0.86f;
        float nx = left + controls::label(canvas_, left, top, 15.0f * u, on ? style::kBoneHi : style::kAshInk, m.name);
        const auto chip = [&](const std::string& word) {
            const float w = controls::capsWidth(9.0f * u, word, 0.1f) + 8.0f * u;
            const Box c{nx + 5.0f * u, top - 11.0f * u, w, 13.0f * u};
            canvas_.outline(c, std::max(1.0f, u), style::kIronLo);
            controls::caps(canvas_, c.x + 4.0f * u, top - 1.5f * u, 9.0f * u, style::kAshInk, word, 0.1f);
            nx = c.right();
        };
        if (r.member == 0) chip("Master");
        else if (m.name == me) chip("You");
        controls::label(canvas_, left, low, 12.0f * u, style::kAshInk, className(sim::Kin(m.kin)));
        if (on) {
            const std::string where = m.here != 0 ? mapName(m.world) + "  \xC2\xB7  here" : mapName(m.world);
            const float w = controls::labelWidth(12.0f * u, where);
            const float d = 5.0f * u;
            canvas_.rect({right - w - d - 5.0f * u, top - 7.0f * u, d, d}, style::kLive);
            controls::label(canvas_, right - w, top, 12.0f * u, m.here != 0 ? style::kBoneHi : style::kBone2, where);
            controls::ranged(canvas_, right, low, 12.0f * u, style::kAshInk, "Level " + std::to_string(m.level));
        } else {
            controls::ranged(canvas_, right, top, 12.0f * u, style::kAshInk2, "Offline");
            controls::ranged(canvas_, right, low, 12.0f * u, style::kAshInk2, seenWords(m.seen));
        }
    }
    if (rows_.empty()) {
        const std::string none = allTab_ ? "No members." : "No one else is online.";
        controls::label(canvas_, x + 95.0f * k - controls::labelWidth(14.0f * u, none) * 0.5f, y + (kRollTop + 30.0f) * k,
                        14.0f * u, style::kAshInk, none);
    }

    // ---- the pager
    {
        controls::State s;
        s.lift = hovered_ == "prev" ? 1.0f : 0.0f;
        s.off = page_ == 0;
        const Box l = at(kPagerLeft), r = at(kPagerRight);
        controls::square(canvas_, {l.x, l.y, l.h, l.h}, controls::Glyph::Left, s, u);
        s.lift = hovered_ == "next" ? 1.0f : 0.0f;
        s.off = page_ + 1 >= pages_;
        controls::square(canvas_, {r.right() - r.h, r.y, r.h, r.h}, controls::Glyph::Right, s, u);
        const std::string pages = std::to_string(page_ + 1) + " / " + std::to_string(pages_);
        controls::label(canvas_, x + 95.0f * k - controls::labelWidth(12.0f * u, pages) * 0.5f,
                        controls::middle(l.y, l.h, 12.0f * u), 12.0f * u, style::kAshInk, pages);
    }

    // ---- the foot
    controls::foot(canvas_, at({0.0f, 0.0f, panel::kWidth, panel::kHeight}), y + panel::kFootRule * k, u);
    if (master) {
        controls::field(canvas_, at(kInviteField), invite_, "Invite a player by name", focus_ == Focus::Invite, false,
                        focus_ == Focus::Invite && std::fmod(caret_, 1.0f) < 0.55f, false, u);
        controls::State s;
        s.lift = hovered_ == "invite" ? 1.0f : 0.0f;
        controls::button(canvas_, at(kInviteButton), "Invite", controls::Kind::Primary, s, u);
    }
    {
        const std::string line = !said.empty() ? said
                                 : master    ? "Click a member for whisper, party or trade."
                                             : "Click a member for whisper, party or trade.";
        const bool bad = said.find("not ") != std::string::npos || said.find("full") != std::string::npos ||
                         said.find("No one") != std::string::npos || said.find("already") != std::string::npos ||
                         said.find("must") != std::string::npos;
        const std::vector<std::string> fit = wrapped(line, 12.0f * u, (kWideAll - 52.0f) * k, 1);
        controls::label(canvas_, x + kLeft * k, y + kSaidBase * k, 12.0f * u,
                        said.empty() ? style::kAshInk : bad ? style::kDanger : kFitsInk, fit.empty() ? "" : fit[0]);
        const std::string quiet = master ? "Disband\xE2\x80\xA6" : "Leave\xE2\x80\xA6";
        controls::ranged(canvas_, x + kQuietButton.right() * k, y + kSaidBase * k, 12.0f * u,
                         hovered_ == "disband" || hovered_ == "leave" ? style::kDanger : style::kAshInk, quiet);
    }

    // ---- a member's menu
    if (menu_ >= 0 && size_t(menu_) < state.members.size()) {
        const Box box = at(menuBox_);
        canvas_.rect(box.grown(1.0f), style::kSeam);
        canvas_.rect(box, style::kAsh1);
        canvas_.outline(box, std::max(1.0f, u), style::kIron);
        controls::label(canvas_, box.x + 8.0f * u, box.y + 13.0f * k * 0.9f, 13.0f * u, style::kBoneHi,
                        state.members[size_t(menu_)].name);
        canvas_.rect({box.x, box.y + 18.0f * k - 1.0f, box.w, 1.0f}, style::kIronDk);
        for (size_t i = 0; i < menuRows_.size(); ++i) {
            const Box row{box.x, box.y + (18.0f + 14.0f * float(i)) * k, box.w, 14.0f * k};
            const bool lit = hovered_ == "menu:" + std::to_string(i);
            if (lit) canvas_.rect(row, style::kAsh3);
            const bool danger = menuRows_[i] == "Remove from guild" || menuRows_[i] == "Leave the guild";
            const bool off = menuRows_[i] == "Masters disband";
            controls::label(canvas_, row.x + 8.0f * u, controls::middle(row.y, row.h, 13.0f * u), 13.0f * u,
                            off ? style::kAshInk2 : danger ? style::kDanger : lit ? style::kBoneHi : style::kBone,
                            menuRows_[i]);
        }
    }

    // ---- a removal asked first
    if (confirm_ != Confirm::None) {
        canvas_.rect(at({4.0f, panel::kHeadBand + 2.0f, panel::kWidth - 8.0f, panel::kHeight - panel::kHeadBand - 6.0f}),
                     gfx::rgba(0.02f, 0.012f, 0.012f, 0.62f));
        const Box box = at(kAskBox);
        canvas_.rect(box.grown(1.0f), style::kSeam);
        canvas_.rect(box, style::kAsh2);
        canvas_.outline(box, std::max(1.0f, u), style::kIron);
        std::string text;
        if (confirm_ == Confirm::Remove) text = "Remove " + confirmName_ + " from " + state.name + "?";
        if (confirm_ == Confirm::Leave) text = "Leave " + state.name + "? You can come back only if the master takes you again.";
        if (confirm_ == Confirm::Disband) text = "Disband " + state.name + "? All " + std::to_string(state.members.size()) + " members leave it. Type its name.";
        float base = box.y + 20.0f * u;
        for (const std::string& line : wrapped(text, 14.0f * u, box.w - 16.0f * u, 3)) {
            controls::label(canvas_, box.midX() - controls::labelWidth(14.0f * u, line) * 0.5f, base, 14.0f * u,
                            style::kBone, line);
            base += 18.0f * u;
        }
        if (confirm_ == Confirm::Disband) {
            controls::field(canvas_, at({kAskBox.x + 8.0f, kAskBox.bottom() - 50.0f, kAskBox.w - 16.0f, 18.0f}), disband_,
                            state.name, true, false, std::fmod(caret_, 1.0f) < 0.55f, false, u);
        }
        const Box no{kAskBox.x + 8.0f, kAskBox.bottom() - 26.0f, kAskBox.w * 0.5f - 12.0f, 18.0f};
        const Box yes{kAskBox.midX() + 4.0f, no.y, no.w, no.h};
        controls::State s;
        s.lift = hovered_ == "no" ? 1.0f : 0.0f;
        controls::button(canvas_, at(no), confirm_ == Confirm::Disband ? "Keep it" : "Cancel", controls::Kind::Secondary, s, u);
        s.lift = hovered_ == "yes" ? 1.0f : 0.0f;
        s.off = confirm_ == Confirm::Disband && lower(disband_) != lower(state.name);
        controls::button(canvas_, at(yes),
                         confirm_ == Confirm::Remove ? "Remove" : confirm_ == Confirm::Leave ? "Leave" : "Disband",
                         controls::Kind::Danger, s, u);
    }
}

// ================================================================================================
// GuildMake
// ================================================================================================

namespace {
// The founding window's places, in units.
constexpr float kStepsTop = 42.0f;
constexpr float kNameTop = 58.0f, kNameTall = 20.0f;
constexpr float kGridTop = 106.0f, kGridSide = 120.0f, kPixel = kGridSide / 8.0f;
constexpr float kToolsX = kLeft + kGridSide + 8.0f, kToolsW = kRight - kToolsX;
constexpr float kPaletteTop = 236.0f, kSwatch = 19.0f, kSwatchGap = 2.0f;
constexpr Box kBack{kLeft, panel::kFootTop + 2.0f, 74.0f, 24.0f};
constexpr Box kNext{kLeft + 80.0f, panel::kFootTop + 2.0f, kWideAll - 80.0f, 24.0f};
const char* const kColourNames[16] = {"Clear", "Black", "Grey", "White", "Red", "Orange", "Yellow", "Lime",
                                      "Green", "Spring", "Cyan", "Azure", "Blue", "Violet", "Magenta", "Rose"};
std::string nameRefusal(const std::string& name) {
    if (name.empty()) return "Name your guild.";
    for (const char c : name) {
        if (!std::isalnum(uint8_t(c))) return "Letters and digits only.";
    }
    if (name.size() < 4) return "At least 4 letters (" + std::to_string(name.size()) + " so far).";
    if (name.size() > kNameMost) return "At most 8 letters.";
    return {};
}
}  // namespace

void GuildMake::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    arts_ = arts;
}

void GuildMake::show() {
    up_ = true;
    step_ = 1;
    asked_ = false;
    refused_.clear();
    drawnOnce_ = false;
}

void GuildMake::hide() {
    up_ = false;
    painting_ = false;
    canvas_.clear();
    drawnOnce_ = false;
}

bool GuildMake::covers(float x, float y) const {
    const float k = panel::scale();
    return up_ && Box{x_, y_, panel::kWidth * k, panel::kHeight * k}.has(x, y);
}

void GuildMake::paint(int x, int y, int colour) {
    const auto set = [&](int px, int py) {
        const size_t at = size_t((py * 8 + px) / 2);
        if ((px % 2) == 0) mark_[at] = uint8_t((mark_[at] & 0x0F) | (colour << 4));
        else mark_[at] = uint8_t((mark_[at] & 0xF0) | colour);
    };
    set(x, y);
    if (mirror_) set(7 - x, y);
}

void GuildMake::update(float seconds, float width, float height, int column, Play& play, const Pointer& pointer,
                       const GuildKeys& keys, const std::string& said, GuildRequests* out) {
    if (!up_) return;
    caret_ += seconds;
    x_ = panel::columnX(width, column);
    y_ = panel::panelY(height);
    const float k = panel::scale();
    const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
    const sim::Body& hero = play.realm().hero();
    const net::GuildState& guild = play.guild();
    const bool can = hero.level >= 100 && guild.id == 0;

    // The server's word on a Found: founded, or why not.
    if (asked_) {
        if (guild.id != 0 && lower(guild.name) == lower(name_)) {
            step_ = 4;
            asked_ = false;
        } else if (!said.empty() && said != saidAtAsk_ && said.rfind("You founded", 0) != 0) {
            refused_ = said;
            asked_ = false;
            step_ = 2;
        }
    }
    if (step_ == 2 && nameFocus_) {
        const std::string before = name_;
        typeInto(name_, keys, 12);
        if (name_ != before) refused_.clear();
        if (keys.escape) nameFocus_ = false;
    }

    std::string on;
    const bool inside = covers(pointer.x, pointer.y);
    int cellX = -1, cellY = -1;
    if (inside) {
        if (panel::frameClose().has(ux, uy)) on = "close";
        else if (kBack.has(ux, uy)) on = "back";
        else if (kNext.has(ux, uy)) on = "next";
        else if (step_ == 2) {
            if (Box{kLeft, kNameTop, kWideAll, kNameTall}.has(ux, uy)) on = "name";
            else if (Box{kLeft, kGridTop, kGridSide, kGridSide}.has(ux, uy)) {
                on = "grid";
                cellX = std::clamp(int((ux - kLeft) / kPixel), 0, 7);
                cellY = std::clamp(int((uy - kGridTop) / kPixel), 0, 7);
            } else {
                for (int t = 0; t < 3; ++t) {
                    if (Box{kToolsX, kGridTop + float(t) * 19.0f, kToolsW, 16.0f}.has(ux, uy)) on = "tool:" + std::to_string(t);
                }
                for (int c = 0; c < 16; ++c) {
                    const Box sw{kLeft + float(c % 8) * (kSwatch + kSwatchGap) + 1.0f,
                                 kPaletteTop + float(c / 8) * (kSwatch + kSwatchGap), kSwatch, kSwatch};
                    if (sw.has(ux, uy)) on = "swatch:" + std::to_string(c);
                }
            }
        }
    }
    hovered_ = on;

    const auto clicked = [&]() {
        if (out) out->clicked = true;
    };
    std::string nameWhy = nameRefusal(name_);
    const bool blank = std::all_of(mark_.begin(), mark_.end(), [](uint8_t b) { return b == 0; });
    if ((pointer.pressed || pointer.rightPressed) && inside) {
        if (on == "grid" && cellX >= 0) {
            undo_.push_back(mark_);
            if (undo_.size() > 40) undo_.erase(undo_.begin());
            painting_ = pointer.pressed;
            paintWith_ = pointer.rightPressed ? 0 : colour_;
            paint(cellX, cellY, paintWith_);
        } else if (pointer.pressed) {
            if (on == "close") {
                hide();
                clicked();
                return;
            }
            if (on == "name") nameFocus_ = true;
            if (on.rfind("swatch:", 0) == 0) colour_ = std::stoi(on.substr(7));
            if (on == "tool:0") mirror_ = !mirror_;
            if (on == "tool:1" && !undo_.empty()) {
                mark_ = undo_.back();
                undo_.pop_back();
            }
            if (on == "tool:2") {
                undo_.push_back(mark_);
                mark_.fill(0);
            }
            if (on == "back") {
                if (step_ == 1 || step_ == 4) hide();
                else step_ = std::max(1, step_ - 1);
            }
            if (on == "next") {
                if (step_ == 1 && can) step_ = 2;
                else if (step_ == 2 && nameWhy.empty() && !blank) step_ = 3;
                else if (step_ == 3 && !asked_ && out) {
                    net::GuildAsk a;
                    a.what = net::GuildWhat::Found;
                    a.name = name_;
                    a.mark = mark_;
                    out->asks.push_back(a);
                    asked_ = true;
                    saidAtAsk_ = said;
                    refused_.clear();
                } else if (step_ == 4) {
                    hide();
                }
            }
            if (!on.empty()) clicked();
        }
    }
    if (painting_ && pointer.held && on == "grid" && cellX >= 0) paint(cellX, cellY, paintWith_);
    if (!pointer.held) painting_ = false;
    if (step_ == 2 && keys.enter && nameWhy.empty() && !blank) step_ = 3;

    uint64_t key = 1469598103934665603ull;
    for (const uint8_t b : mark_) key = mixed(key, b);
    key = mixs(key, on + "|" + name_ + "|" + refused_ + "|" + guild.name);
    key = mixed(key, uint64_t(step_) * 7 + uint64_t(colour_) * 101 + uint64_t(mirror_) + uint64_t(asked_) * 3 +
                         uint64_t(nameFocus_) * 13 + undo_.size() * 1009);
    key = mixed(key, uint64_t(hero.level) * 17 + guild.id);
    key = mixed(key, step_ == 2 && nameFocus_ ? uint64_t(std::fmod(caret_, 1.0f) < 0.55f) : 0);
    key = mixed(key, uint64_t(std::lround(x_)) * 31 + uint64_t(std::lround(y_)) + uint64_t(std::lround(k * 1000.0f)));
    if (drawnOnce_ && key == drawnKey_) return;
    drawnKey_ = key;
    drawnOnce_ = true;
    draw(play, said);
}

void GuildMake::draw(Play& play, const std::string& said) {
    (void)said;
    canvas_.clear();
    if (!arts_ || !up_) return;
    const float x = x_, y = y_, k = panel::scale(), u = tip::unit();
    const auto at = [&](const Box& units) { return panel::scaled(x, y, units); };
    const sim::Body& hero = play.realm().hero();
    const net::GuildState& guild = play.guild();
    std::string me;
    play.playerName(hero.id, &me, nullptr);
    panel::frame(canvas_, *arts_, x, y, "Guild Master");
    panel::close(canvas_, x, y, hovered_ == "close", false);

    // ---- the three steps
    {
        const char* const words[3] = {"Begin", "Name and mark", "Found"};
        const float w = (kWideAll - 6.0f) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            const Box bar = at({kLeft + float(i) * (w + 3.0f), kStepsTop, w, 1.5f});
            const int s = std::min(step_, 3);
            canvas_.rect(bar, i + 1 == s ? style::kBlood : i + 1 < s ? style::kBloodDk : style::kIronDk);
            const float cx = bar.midX();
            controls::caps(canvas_, cx - controls::capsWidth(9.0f * u, words[i], 0.04f) * 0.5f, bar.y + 13.0f * u,
                           9.0f * u, i + 1 == s ? style::kBone2 : style::kAshInk2, words[i], 0.04f);
        }
    }
    const auto row = [&](float& top, const std::string& icon, int tone, const std::string& what,
                         const std::string& value, bool bad) {
        const Box line = at({kLeft, top, kWideAll, 17.0f});
        if (top > 0.0f) canvas_.rect({line.x, line.y, line.w, 1.0f}, style::kIronDk);
        const float d = 13.0f * u;
        const Box dot{line.x + 2.0f * u, line.midY() - d * 0.5f, d, d};
        canvas_.rect(dot, tone == 1 ? gfx::rgba(0.43f, 0.62f, 0.35f, 0.25f)
                          : tone == 2 ? gfx::rgba(0.76f, 0.21f, 0.17f, 0.3f)
                                      : style::kAsh3);
        const float w = std::max(1.2f, 1.6f * u);
        if (icon == "yes") {
            stroke(canvas_, dot.x + d * 0.25f, dot.y + d * 0.52f, dot.x + d * 0.43f, dot.y + d * 0.72f, w, style::kLive);
            stroke(canvas_, dot.x + d * 0.43f, dot.y + d * 0.72f, dot.x + d * 0.76f, dot.y + d * 0.3f, w, style::kLive);
        } else if (icon == "no") {
            stroke(canvas_, dot.x + d * 0.3f, dot.y + d * 0.3f, dot.x + d * 0.7f, dot.y + d * 0.7f, w, style::kBloodHi);
            stroke(canvas_, dot.x + d * 0.7f, dot.y + d * 0.3f, dot.x + d * 0.3f, dot.y + d * 0.7f, w, style::kBloodHi);
        } else if (icon == "mark") {
            guildmark::draw(canvas_, dot.grown(-1.5f * u), mark_);
        } else {
            controls::label(canvas_, dot.midX() - controls::labelWidth(10.0f * u, icon) * 0.5f,
                            controls::middle(dot.y, dot.h, 10.0f * u), 10.0f * u, style::kBone2, icon);
        }
        const float base = controls::middle(line.y, line.h, 13.0f * u);
        controls::label(canvas_, dot.right() + 7.0f * u, base, 14.0f * u, style::kBone, what);
        controls::ranged(canvas_, line.right(), base, 13.0f * u, bad ? style::kDanger : style::kAshInk, value);
        top += 17.0f;
    };

    controls::foot(canvas_, at({0.0f, 0.0f, panel::kWidth, panel::kHeight}), y + panel::kFootRule * k, u);
    controls::State back, next;
    back.lift = hovered_ == "back" ? 1.0f : 0.0f;
    next.lift = hovered_ == "next" ? 1.0f : 0.0f;

    if (step_ == 1) {
        const bool lvOk = hero.level >= 100, inOk = guild.id == 0;
        const std::string line = !inOk ? "You already stand with " + guild.name + ". One banner is enough for anyone."
                                 : !lvOk ? "A guild needs a master the others will follow. Come back when you are stronger."
                                         : "So you would raise a banner of your own? Name it, and give it a mark the others will know.";
        float base = y + 76.0f * k;
        for (const std::string& l : wrapped(line, 15.0f * u, (kWideAll - 8.0f) * k, 4)) {
            controls::label(canvas_, x + 95.0f * k - controls::labelWidth(15.0f * u, l) * 0.5f, base, 15.0f * u,
                            style::kBone, l);
            base += 20.0f * u;
        }
        controls::caps(canvas_, x + 95.0f * k - controls::capsWidth(9.0f * u, "The Guild Master") * 0.5f,
                       base + 4.0f * u, 9.0f * u, style::kAshInk, "The Guild Master");
        float top = 158.0f;
        controls::caps(canvas_, x + kLeft * k, y + (top - 4.0f) * k, 10.0f * u, style::kAshInk, "What you need");
        row(top, lvOk ? "yes" : "no", lvOk ? 1 : 2, "Level 100 or more",
            lvOk ? "yours " + std::to_string(hero.level)
                 : std::to_string(hero.level) + "  \xC2\xB7  " + std::to_string(100 - hero.level) + " to go",
            !lvOk);
        row(top, inOk ? "yes" : "no", inOk ? 1 : 2, "In no guild", inOk ? "none" : guild.name, !inOk);
        row(top, "yes", 1, "Cost", "free", false);
        top += 18.0f;
        controls::caps(canvas_, x + kLeft * k, y + (top - 4.0f) * k, 10.0f * u, style::kAshInk, "A guild gives");
        const int cap = std::clamp(std::max(hero.level, 100) / 10, 1, 40);
        row(top, "@", 0, "A guild chat", "@ or /g", false);
        row(top, "mark", 0, "Your mark over every head", "8 by 8", false);
        row(top, std::to_string(cap), 0, "Members, at your level", "40 at most", false);
        row(top, "!", 0, "A notice for all", "60 letters", false);
        controls::button(canvas_, at(kBack), "Not now", controls::Kind::Secondary, back, u);
        next.off = !(lvOk && inOk);
        controls::button(canvas_, at(kNext), "Found a guild", controls::Kind::Primary, next, u);
        return;
    }

    if (step_ == 2) {
        const std::string why = !refused_.empty() ? refused_ : nameRefusal(name_);
        controls::field(canvas_, at({kLeft, kNameTop, kWideAll, kNameTall}), name_, "Your guild's name", nameFocus_,
                        !why.empty() && !name_.empty(), nameFocus_ && std::fmod(caret_, 1.0f) < 0.55f, false, u);
        const Box field = at({kLeft, kNameTop, kWideAll, kNameTall});
        controls::ranged(canvas_, field.right() - 6.0f * u, controls::middle(field.y, field.h, 11.0f * u), 11.0f * u,
                         name_.size() > kNameMost ? style::kDanger : style::kAshInk,
                         std::to_string(name_.size()) + "/8");
        controls::label(canvas_, x + kLeft * k, y + (kNameTop + kNameTall + 11.0f) * k, 12.0f * u,
                        why.empty() ? kFitsInk : style::kDanger,
                        why.empty() ? std::string("The server checks the name when you found it.") : why);
        controls::caps(canvas_, x + kLeft * k, y + (kGridTop - 5.0f) * k, 10.0f * u, style::kAshInk,
                       std::string("Mark  \xC2\xB7  ") + kColourNames[colour_]);
        controls::ranged(canvas_, x + kRight * k, y + (kGridTop - 5.0f) * k, 10.0f * u, style::kAshInk2, "right-click erases");
        // The grid on a chequer, so clear reads as clear.
        const Box grid = at({kLeft, kGridTop, kGridSide, kGridSide});
        canvas_.rect(grid.grown(1.0f), style::kIronLo);
        for (int py = 0; py < 8; ++py) {
            for (int px = 0; px < 8; ++px) {
                const Box cell{grid.x + float(px) * kPixel * k, grid.y + float(py) * kPixel * k, kPixel * k, kPixel * k};
                const int p = net::guildPixel(mark_, px, py);
                if (p == 0) {
                    canvas_.rect(cell, (px + py) % 2 ? gfx::rgba(0.09f, 0.08f, 0.075f) : gfx::rgba(0.125f, 0.11f, 0.1f));
                } else {
                    canvas_.rect(cell, guildmark::colour(p));
                }
                canvas_.outline(cell, 1.0f, gfx::rgba(0.2f, 0.17f, 0.14f, 0.6f));
            }
        }
        if (mirror_) canvas_.rect({grid.midX() - 0.5f, grid.y, 1.0f, grid.h}, gfx::rgba(0.94f, 0.38f, 0.31f, 0.55f));
        // The tools, and the mark at its real sizes.
        const char* const tools[3] = {"Mirror", "Undo", "Clear"};
        for (int t = 0; t < 3; ++t) {
            controls::State s;
            s.lift = hovered_ == "tool:" + std::to_string(t) ? 1.0f : 0.0f;
            s.off = t == 1 && undo_.empty();
            controls::button(canvas_, at({kToolsX, kGridTop + float(t) * 19.0f, kToolsW, 16.0f}), tools[t],
                             t == 0 && mirror_ ? controls::Kind::Primary : controls::Kind::Secondary, s, u, 12.0f * u);
        }
        {
            const Box shown = at({kToolsX, kGridTop + 62.0f, kToolsW, kGridSide - 62.0f});
            canvas_.rect(shown, gfx::rgba(0.06f, 0.07f, 0.05f));
            canvas_.outline(shown, 1.0f, style::kIronDk);
            const float big = 24.0f * k * 0.9f;
            guildmark::draw(canvas_, {shown.midX() - big * 0.5f, shown.y + 6.0f * k, big, big}, mark_);
            const float small = std::max(8.0f, 11.0f * u);
            guildmark::draw(canvas_, {shown.midX() - small * 0.5f, shown.y + 6.0f * k + big + 6.0f * k, small, small},
                            mark_, 1.0f, true);
            controls::caps(canvas_, shown.midX() - controls::capsWidth(8.0f * u, "Actual") * 0.5f,
                           shown.bottom() - 5.0f * u, 8.0f * u, style::kAshInk, "Actual");
        }
        for (int c = 0; c < 16; ++c) {
            const Box sw = at({kLeft + float(c % 8) * (kSwatch + kSwatchGap) + 1.0f,
                               kPaletteTop + float(c / 8) * (kSwatch + kSwatchGap), kSwatch, kSwatch});
            if (c == 0) {
                const float h = sw.w * 0.5f;
                canvas_.rect(sw, gfx::rgba(0.09f, 0.08f, 0.075f));
                canvas_.rect({sw.x, sw.y, h, h}, gfx::rgba(0.23f, 0.19f, 0.16f));
                canvas_.rect({sw.x + h, sw.y + h, h, h}, gfx::rgba(0.23f, 0.19f, 0.16f));
            } else {
                canvas_.rect(sw, guildmark::colour(c));
            }
            canvas_.outline(sw, std::max(1.0f, u), c == colour_ ? style::kBoneHi
                                                    : hovered_ == "swatch:" + std::to_string(c) ? style::kIronHi
                                                                                                : style::kIronDk);
        }
        controls::label(canvas_, x + kLeft * k, y + (kPaletteTop + 2.0f * (kSwatch + kSwatchGap) + 12.0f) * k, 12.0f * u,
                        style::kAshInk, "Left-drag paints. The name and mark can't change later.");
        controls::button(canvas_, at(kBack), "Back", controls::Kind::Secondary, back, u);
        const bool blank = std::all_of(mark_.begin(), mark_.end(), [](uint8_t b) { return b == 0; });
        next.off = !nameRefusal(name_).empty() || blank;
        controls::button(canvas_, at(kNext), blank && nameRefusal(name_).empty() ? "Draw a mark" : "Next",
                         controls::Kind::Primary, next, u);
        return;
    }

    // ---- 3: the look, and 4: founded
    {
        const float side = 52.0f;
        const Box well = at({95.0f - side * 0.5f, 74.0f, side, side});
        controls::well(canvas_, well, u);
        const float pad = 4.0f * k;
        guildmark::draw(canvas_, {well.x + pad, well.y + pad, well.w - 2 * pad, well.h - 2 * pad}, mark_);
        const std::string name = name_;
        if (const gfx::Face* face = panel::titleFace()) {
            const float size = 24.0f * u;
            const float w = face->measure(size, name) + size * 0.06f * float(name.size());
            canvas_.lettered(*face, panel::titleTexture(), x + 95.0f * k - w * 0.5f, y + 152.0f * k, size, size * 0.06f,
                             style::kBoneHi, name);
        }
        // As it will stand over his head.
        const Box plate = at({kLeft + 20.0f, 166.0f, kWideAll - 40.0f, 40.0f});
        canvas_.rect(plate, gfx::rgba(0.06f, 0.07f, 0.05f));
        canvas_.outline(plate, 1.0f, style::kIronDk);
        const uint32_t tag = gfx::rgba(0.18f, 1.0f, 0.18f);
        const float nameSize = 16.0f * u, guildSize = 12.0f * u;
        const float nw = controls::labelWidth(nameSize, me);
        controls::label(canvas_, plate.midX() - nw * 0.5f, plate.y + plate.h * 0.48f, nameSize, tag, me);
        const float mark = guildSize * 0.95f;
        const float gw = mark + 4.0f * u + controls::capsWidth(guildSize, name, 0.0f);
        guildmark::draw(canvas_, {plate.midX() - gw * 0.5f, plate.y + plate.h * 0.82f - mark * 0.88f, mark, mark}, mark_,
                        1.0f, true);
        controls::caps(canvas_, plate.midX() - gw * 0.5f + mark + 4.0f * u, plate.y + plate.h * 0.82f, guildSize, tag,
                       name, 0.0f);
        float base = y + 232.0f * k;
        const int cap = std::clamp(hero.level / 10, 1, 40);
        const std::vector<std::string> lines =
            step_ == 4 ? std::vector<std::string>{name + " is founded.", "Press G to open your guild and invite",
                                                  "your first member."}
                       : std::vector<std::string>{"You lead it, alone at first: 1 of " + std::to_string(cap) + ".",
                                                  "The name and the mark can't change later."};
        for (size_t i = 0; i < lines.size(); ++i) {
            const uint32_t ink = step_ == 4 && i == 0 ? kFitsInk : i == 1 && step_ == 3 ? style::kDanger : style::kAshInk;
            const float size = step_ == 4 && i == 0 ? 16.0f * u : 13.0f * u;
            controls::label(canvas_, x + 95.0f * k - controls::labelWidth(size, lines[i]) * 0.5f, base, size, ink, lines[i]);
            base += 19.0f * u;
        }
        if (asked_) {
            const std::string wait = "Asking the Guild Master\xE2\x80\xA6";
            controls::label(canvas_, x + 95.0f * k - controls::labelWidth(13.0f * u, wait) * 0.5f, base + 10.0f * u, 13.0f * u,
                            style::kBone2, wait);
        }
        if (step_ == 4) {
            controls::button(canvas_, at({kLeft, panel::kFootTop + 2.0f, kWideAll, 24.0f}), "Done", controls::Kind::Primary,
                             next, u);
        } else {
            controls::button(canvas_, at(kBack), "Back", controls::Kind::Secondary, back, u);
            next.off = asked_;
            controls::button(canvas_, at(kNext), "Found " + name_, controls::Kind::Primary, next, u);
        }
    }
}

}  // namespace mu::game
