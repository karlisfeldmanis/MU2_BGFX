#include "game/ui/player_menu.h"

#include <algorithm>

#include "game/play.h"
#include "game/ui/capsule.h"
#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

// A sheet of the windows' ash, a row lit under the pointer. Measures in tip::unit().
constexpr float kWide = 150.0f, kRow = 26.0f, kHead = 24.0f, kPad = 12.0f;
constexpr uint32_t kSheet = gfx::rgba(0.051f, 0.039f, 0.031f, 0.97f);
constexpr uint32_t kLit = gfx::rgba(1.0f, 0.94f, 0.86f, 0.07f);

}  // namespace

void PlayerMenu::show(Play& play, const std::string& name, float x, float y, float width, float height) {
    if (!play.remote() || name.empty()) return;
    std::string me;
    play.playerName(play.realm().hero().id, &me, nullptr);
    if (name == me) return;
    const net::PartyState& party = play.party();
    const bool inParty = party.members.size() > 1;
    const bool leading = inParty && party.members.front().name == me;
    bool member = false;
    for (const auto& one : party.members) member = member || one.name == name;

    name_ = name;
    rows_.clear();
    rows_.push_back({"Whisper"});
    if (member && leading) {
        rows_.push_back({"Make leader"});
        rows_.push_back({"Kick", true, true});
    }
    if (!member && (!inParty || leading)) rows_.push_back({"Invite to party"});
    // The guild's (docs/guild.md): a master invites one in no guild; one in none asks a master.
    {
        const net::GuildState& guild = play.guild();
        const bool master = guild.id != 0 && !guild.members.empty() && guild.members.front().name == me;
        const uint32_t body = play.playerNamed(name);
        const uint32_t theirs = body != 0 ? play.guildOfPlayer(body) : 0;
        if (master && theirs == 0) rows_.push_back({"Invite to guild"});
        if (guild.id == 0 && theirs != 0) {
            const net::GuildMark* mark = play.guildMark(theirs);
            if (mark != nullptr && mark->master == name) rows_.push_back({"Ask to join guild"});
        }
    }
    rows_.push_back({"Inspect", false});
    // Trade lives while he is on this map (sim/realm_deal.cpp); the realm refuses one too far.
    rows_.push_back({"Trade", play.playerNamed(name) != 0});
    place(play, x, y, width, height);
}

void PlayerMenu::place(Play& play, float x, float y, float width, float height) {
    const float u = tip::unit();
    const float w = kWide * u, h = (kHead + kRow * float(rows_.size()) + 6.0f) * u;
    box_ = {std::clamp(x, 4.0f * u, std::max(4.0f * u, width - w - 4.0f * u)),
            std::clamp(y, 4.0f * u, std::max(4.0f * u, height - h - 4.0f * u)), w, h};
    hovered_ = -1;
    up_ = true;
    fresh_ = true;
    play.ui(Play::Ui::Opened);  // as a window opens
    draw();
}

void PlayerMenu::hide() {
    up_ = false;
    canvas_.clear();
}

bool PlayerMenu::update(Play& play, const Pointer& pointer) {
    if (!up_) return false;
    if (fresh_) {
        fresh_ = false;
        return pointer.pressed || pointer.rightPressed;
    }
    const float u = tip::unit();
    const int before = hovered_;
    hovered_ = -1;
    const float top = box_.y + kHead * u;
    if (box_.has(pointer.x, pointer.y) && pointer.y >= top) {
        const int row = std::min(int((pointer.y - top) / (kRow * u)), int(rows_.size()) - 1);
        if (rows_[size_t(row)].live) hovered_ = row;
    }
    bool took = false;
    if (pointer.pressed || pointer.rightPressed) {
        took = true;  // a click while it is open is the menu's, in it or shutting it
        const bool inside = box_.has(pointer.x, pointer.y);
        if (pointer.pressed && hovered_ >= 0) {
            const std::string& row = rows_[size_t(hovered_)].word;
            if (row == "Whisper") whisper_ = name_;
            else if (row == "Make leader") play.askParty({net::PartyWhat::Promote, name_});
            else if (row == "Kick") play.askParty({net::PartyWhat::Kick, name_});
            else if (row == "Invite to party") play.askParty({net::PartyWhat::Invite, name_});
            else if (row == "Trade") play.dealAsk(play.playerNamed(name_));
            else if (row == "Invite to guild" || row == "Ask to join guild") {
                net::GuildAsk asked;
                asked.what = row == "Invite to guild" ? net::GuildWhat::Invite : net::GuildWhat::Ask;
                asked.name = name_;
                play.askGuild(asked);
            }
        }
        // A greyed row holds the menu open; anything else shuts it, with the window's click.
        if (!inside || hovered_ >= 0 || (pointer.y < top && pointer.pressed)) {
            hide();
            play.ui(Play::Ui::Click);
            return took;
        }
    }
    if (hovered_ != before || u != unit_) draw();
    return took;
}

void PlayerMenu::draw() {
    const float u = tip::unit();
    unit_ = u;
    canvas_.clear();
    const gfx::Box& b = box_;
    const float r[4] = {6.0f * u, 6.0f * u, 6.0f * u, 6.0f * u};
    for (const float grow : {4.0f, 2.0f}) {
        const float g = grow * u, rr[4] = {r[0] + g, r[0] + g, r[0] + g, r[0] + g};
        tip::rounded(canvas_, {b.x - g, b.y - g + 2.0f * u, b.w + 2.0f * g, b.h + 2.0f * g}, rr, capsule::kShadow);
    }
    tip::rounded(canvas_, b, r, kSheet);
    controls::caps(canvas_, b.x + kPad * u, b.y + 16.0f * u, 10.5f * u, style::kAshInk, name_, 0.14f);
    for (size_t i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i];
        const float top = b.y + (kHead + kRow * float(i)) * u;
        if (int(i) == hovered_) canvas_.rect({b.x + 3.0f * u, top, b.w - 6.0f * u, kRow * u}, kLit);
        const uint32_t ink = !row.live ? style::kAshInk2 : row.danger ? style::kDanger : style::kBoneHi;
        controls::label(canvas_, b.x + kPad * u, controls::middle(top, kRow * u, 13.5f * u), 13.5f * u, ink, row.word);
    }
}

}  // namespace mu::game
