#include "game/ui/chat.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "core/log.h"
#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/tip.h"

namespace mu::game {

namespace {

using gfx::Box;

// 0xRRGGBB as the canvas's ABGR, with an alpha: the mockup's colours as written there.
constexpr uint32_t hex(uint32_t rgb, float a = 1.0f) {
    return ((rgb >> 16) & 0xFFu) | (((rgb >> 8) & 0xFFu) << 8) | ((rgb & 0xFFu) << 16) |
           (uint32_t(a * 255.0f + 0.5f) << 24);
}

uint32_t faded(uint32_t abgr, float alpha) {
    const float a = float(abgr >> 24) / 255.0f * std::clamp(alpha, 0.0f, 1.0f);
    return (abgr & 0xFFFFFFu) | (uint32_t(a * 255.0f + 0.5f) << 24);
}

// The channels' inks (the mockup's): told apart by lightness as well as hue.
constexpr uint32_t kMapInk = hex(0xd9cfbd), kPartyInk = hex(0x7fb6f2), kGuildInk = hex(0x93d67e);
constexpr uint32_t kWhisperInk = hex(0xe7a0e4), kSystemInk = hex(0xd6ad5c);
// A speaker's name, on every channel: a brighter bone than what he said, one tone apart and no
// hue (the user, 2026-10-09: "dont use green for usenames in chat, only on special exections like
// labels or party invites"). The channel is in the small word before it.
constexpr uint32_t kNameInk = hex(0xf3e8d4);
// And the one exception in the chat: an invite's asker, in the green of the name over a player's
// head (vitals.cpp, kTagGreen), on the card in the middle and on the log's line.
constexpr uint32_t kInviterInk = gfx::rgba(0.18f, 1.0f, 0.18f);
constexpr uint32_t kBody = hex(0xd9cfbd), kWhisperBody = hex(0xefd3ee), kQuiet = hex(0x8f8575);
constexpr uint32_t kTabIdle = hex(0x7d7366), kTabWhisper = hex(0xa77ea4), kTabLit = hex(0xfbe3a4);
constexpr uint32_t kGoldBar = hex(0xe0b65c), kGoldGlow = hex(0xc8a258);
constexpr uint32_t kIron = hex(0x6b5645), kIronLit = hex(0xa8843c), kGripInk = hex(0x8a7560);
constexpr uint32_t kHintInk = hex(0x6f665b), kPlaceholder = hex(0x5c5249), kDraft = hex(0xf3e8d4);
constexpr uint32_t kVoid = hex(0x050303), kDrop = hex(0x000000, 0.75f);

// Measures, in tip::unit() (a pixel at 1080 lines), from the mockup's 810 lines.
constexpr float kLeft = 20.0f, kWide = 560.0f;
constexpr float kTallDefault = 400.0f, kTallLeast = 230.0f, kTallMost = 760.0f;
constexpr float kGrip = 26.0f, kTabRow = 44.0f, kInput = 44.0f, kFoot = 30.0f;
// The log's text 16 and its row 21, from 18 and 24, its tags 13 from 14 (the user, 2026-10-10:
// "scale down chat text little bit").
constexpr float kRow = 21.0f, kText = 16.0f, kTag = 13.0f, kWord = 18.0f, kHint = 14.0f;
// The cloud's peak, and closed, what is left of it under the fading lines.
constexpr float kCloud = 0.97f, kCloudClosed = 0.55f;
// Closed: how long each line stays, and the fewest it shows; as many more as the height he gave
// it holds (the user, 2026-10-09: "if i chancge chant heoght i exptect that also closed chat will
// be woth more messages"), Chat::closedRows.
constexpr float kStays = 20.0f, kFades = 4.0f;
constexpr int kClosedLeast = 3;
constexpr size_t kKept = 400;  // lines kept, oldest dropped
// The invite in the middle: how long it stands unanswered (the log's line and /accept stay),
// its fade in and out, and how far its foot stands over the chat's own foot -- above where a
// fight's Zen is said (game/ui/tally.cpp, kZenAboveHud).
constexpr float kInviteLife = 60.0f, kInviteIn = 0.15f, kInviteOut = 1.0f;
constexpr float kInviteAbove = 44.0f, kInviteWide = 440.0f, kInviteTall = 118.0f;

// A line in a face over its drop, the drop faded with the ink; its advance.
float set(gfx::Canvas& canvas, const gfx::Face* face, bgfx::TextureHandle texture, float x,
          float baseline, float size, uint32_t ink, const std::string& text, float u) {
    if (face == nullptr) return controls::label(canvas, x, baseline, size, ink, text);
    const float alpha = float(ink >> 24) / 255.0f;
    canvas.lettered(*face, texture, x + u, baseline + u, size, 0.0f, faded(kDrop, alpha), text);
    return canvas.lettered(*face, texture, x, baseline, size, 0.0f, ink, text);
}
float body(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink, const std::string& text,
           float u) {
    return set(canvas, controls::labelFace(), controls::labelTexture(), x, baseline, size, ink, text, u);
}
float word(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink, const std::string& text,
           float u) {
    return set(canvas, controls::wordFace(), controls::wordTexture(), x, baseline, size, ink, text, u);
}
float bodyWidth(float size, const std::string& text) { return controls::labelWidth(size, text); }
float wordWidth(float size, const std::string& text) {
    const gfx::Face* face = controls::wordFace();
    return face ? face->measure(size, text) : controls::labelWidth(size, text);
}

// A stroke from one point to another, `wide` across: the close's cross, the grip's arrow.
void stroke(gfx::Canvas& canvas, float x0, float y0, float x1, float y1, float wide, uint32_t ink) {
    const float dx = x1 - x0, dy = y1 - y0, length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) return;
    const float nx = -dy / length * wide * 0.5f, ny = dx / length * wide * 0.5f;
    const float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    const uint32_t inks[4] = {ink, ink, ink, ink};
    canvas.polygon(xy, inks, 4);
}

// An iron rule solid from its left and fading out over its last third (the tracker's, mirrored:
// this one stands at the screen's left edge).
void rule(gfx::Canvas& canvas, float x, float y, float wide, float u, uint32_t ink = kIron) {
    const float tall = std::max(1.0f, u);
    const float solid = wide * 2.0f / 3.0f;
    canvas.rect({x, y, solid, tall}, ink);
    canvas.shade({x + solid, y, wide - solid, tall}, ink, faded(ink, 0.0f), faded(ink, 0.0f), ink);
}

// UTF-8: the bytes of the last letter in `s`.
size_t lastLetter(const std::string& s) {
    size_t at = s.size();
    while (at > 0) {
        --at;
        if ((uint8_t(s[at]) & 0xC0) != 0x80) break;
    }
    return s.size() - at;
}

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
}

}  // namespace

void Chat::open(const gfx::Interface& interface) {
    interface.adopt(back_);
    interface.adopt(inviteDrawn_);
    interface.adopt(tabsDrawn_);
    interface.adopt(lines_);
}

bool Chat::covers(float x, float y) const {
    return (open_ && (panel_.has(x, y) || grip_.has(x, y))) || (inviting_ && inviteCard_.has(x, y));
}

uint32_t Chat::channelInk(net::Channel channel) const {
    switch (channel) {
        case net::Channel::Map: return kMapInk;
        case net::Channel::Party: return kPartyInk;
        case net::Channel::Guild: return kGuildInk;
        case net::Channel::Whisper: return kWhisperInk;
        case net::Channel::Server: return kSystemInk;
    }
    return kMapInk;
}

std::string Chat::channelWord() const {
    switch (channel_) {
        case net::Channel::Map: return "Map";
        case net::Channel::Party: return "Party";
        case net::Channel::Guild: return "Guild";
        case net::Channel::Whisper: return "To " + target_;
        case net::Channel::Server: return "Map";
    }
    return "Map";
}

void Chat::system(net::Channel channel, const std::string& text) {
    net::Heard line;
    line.channel = channel;
    line.text = text;
    hear(line);
}

void Chat::hear(const net::Heard& heard) {
    Line line;
    line.channel = heard.channel;
    line.name = heard.name;
    line.to = heard.to;
    line.text = heard.text;
    line.bot = heard.bot;
    // The server's invite (server/src/main.cpp, partyAsk): its asker, and the line without the
    // typing it asks for, since the line wears its own buttons.
    constexpr const char* kInvites = " invites you to a party.";
    const size_t at = line.text.find(kInvites);
    if (line.channel == net::Channel::Party && line.name.empty() && at != std::string::npos && at > 0) {
        line.inviter = line.text.substr(0, at);
        line.text = line.inviter + kInvites;
    }
    log_.push_back(line);
    if (log_.size() > kKept) log_.erase(log_.begin(), log_.begin() + long(log_.size() - kKept));
    if (line.name.empty()) return;
    const bool mine = !me_.empty() && line.name == me_;
    std::string key;
    if (line.channel == net::Channel::Whisper) {
        const std::string partner = mine && !line.to.empty() ? line.to : line.name;
        if (std::find(whispers_.begin(), whispers_.end(), partner) == whispers_.end()) {
            whispers_.push_back(partner);
        }
        if (!mine) lastFrom_ = partner;
        key = "w:" + partner;
    } else if (line.channel == net::Channel::Party) {
        key = "party";
    } else if (line.channel == net::Channel::Guild) {
        key = "guild";
    }
    if (!key.empty() && !mine && tab_ != key) ++unread_[key];
}

bool Chat::shows(const Line& line, const std::string& tab) const {
    if (tab == "all" || line.channel == net::Channel::Server) return true;
    if (tab.rfind("w:", 0) == 0) {
        if (line.channel != net::Channel::Whisper) return false;
        if (line.name.empty()) return true;  // "Brakka is not here."
        const std::string partner = line.name == me_ && !line.to.empty() ? line.to : line.name;
        return partner == tab.substr(2);
    }
    if (tab == "map") return line.channel == net::Channel::Map;
    if (tab == "party") return line.channel == net::Channel::Party;
    if (tab == "guild") return line.channel == net::Channel::Guild;
    return false;
}

void Chat::select(const std::string& key) {
    tab_ = key;
    unread_[key] = 0;
    logScroll_ = 0.0f;
    chipSet_ = false;
}

void Chat::closeTab(const std::string& key) {
    const std::string name = key.substr(2);
    whispers_.erase(std::remove(whispers_.begin(), whispers_.end(), name), whispers_.end());
    unread_.erase(key);
    if (tab_ == key) select("all");
}

void Chat::whisperTo(const std::string& name) {
    if (name.empty()) return;
    if (std::find(whispers_.begin(), whispers_.end(), name) == whispers_.end()) whispers_.push_back(name);
    select("w:" + name);
    open_ = true;
}

void Chat::send(Play& play) {
    // The tab's channel, unless the line's slash says another.
    net::Channel channel = channel_;
    std::string to = target_;
    std::string text = net::saidText(draft_);
    draft_.clear();
    open_ = false;
    if (text.empty()) return;
    if (text[0] == '/') {
        const size_t space = text.find(' ');
        const std::string command = lower(text.substr(1, space == std::string::npos ? std::string::npos : space - 1));
        std::string rest = space == std::string::npos ? std::string() : net::saidText(text.substr(space + 1));
        // A channel for this line alone; with nothing to say, its tab instead.
        if (command == "p" || command == "party" || command == "g" || command == "guild" || command == "m" ||
            command == "map" || command == "s" || command == "say") {
            const char kind = command[0];
            if (rest.empty()) {
                select(kind == 'p' ? "party" : kind == 'g' ? "guild" : "map");
                return;
            }
            channel = kind == 'p' ? net::Channel::Party : kind == 'g' ? net::Channel::Guild : net::Channel::Map;
        } else if (command == "w" || command == "whisper" || command == "t" || command == "tell") {
            const size_t gap = rest.find(' ');
            const std::string name = rest.substr(0, gap);
            if (name.empty()) {
                system(net::Channel::Server, "Whisper whom? /w and a name.");
                return;
            }
            channel = net::Channel::Whisper;
            to = name;
            rest = gap == std::string::npos ? std::string() : net::saidText(rest.substr(gap + 1));
            if (rest.empty()) {
                if (std::find(whispers_.begin(), whispers_.end(), name) == whispers_.end()) {
                    whispers_.push_back(name);
                }
                select("w:" + name);
                return;
            }
        } else if (command == "r" || command == "reply") {
            if (lastFrom_.empty()) {
                system(net::Channel::Server, "No one has whispered you.");
                return;
            }
            channel = net::Channel::Whisper;
            to = lastFrom_;
        } else if (command == "invite" || command == "leave" || command == "kick" || command == "accept" ||
                   command == "decline" || command == "promote") {
            // The party's asks (docs/party.md): the server answers on the Party channel.
            net::PartyAsk asked;
            asked.what = command == "invite"   ? net::PartyWhat::Invite
                         : command == "leave"  ? net::PartyWhat::Leave
                         : command == "kick"   ? net::PartyWhat::Kick
                         : command == "promote" ? net::PartyWhat::Promote
                         : command == "accept" ? net::PartyWhat::Accept
                                               : net::PartyWhat::Decline;
            asked.name = rest.substr(0, rest.find(' '));
            if ((asked.what == net::PartyWhat::Invite || asked.what == net::PartyWhat::Kick ||
                 asked.what == net::PartyWhat::Promote) &&
                asked.name.empty()) {
                system(net::Channel::Party, "/" + command + " whom? Add a name.");
                return;
            }
            if (!play.remote()) {
                system(net::Channel::Server, "No one is here to party with: this world is not on a server.");
                return;
            }
            if (asked.what == net::PartyWhat::Accept || asked.what == net::PartyWhat::Decline) {
                answer(play, -1, asked.name, asked.what == net::PartyWhat::Accept);
                return;
            }
            play.askParty(asked);
            return;
        } else {
            system(net::Channel::Server, "There is no /" + command + ".");
            return;
        }
        text = rest;
        if (text.empty()) return;
    }
    if (channel == net::Channel::Whisper && to.empty()) {
        system(net::Channel::Server, "Whisper whom? /w and a name.");
        return;
    }
    if (!play.remote()) {
        system(net::Channel::Server, "No one hears you here: this world is not on a server.");
        return;
    }
    net::Say said;
    said.channel = channel;
    said.to = channel == net::Channel::Whisper ? to : std::string();
    said.text = text;
    play.say(said);
}

void Chat::answer(Play& play, long index, const std::string& name, bool accept) {
    // The line it answers: the one pressed, or the latest open one from `name` (any, for none).
    if (index < 0) {
        for (size_t i = log_.size(); i-- > 0;) {
            if (!log_[i].inviter.empty() && log_[i].answer == 0 && (name.empty() || log_[i].inviter == name)) {
                index = long(i);
                break;
            }
        }
    }
    net::PartyAsk asked;
    asked.what = accept ? net::PartyWhat::Accept : net::PartyWhat::Decline;
    asked.name = index >= 0 ? log_[size_t(index)].inviter : name;
    if (index >= 0) log_[size_t(index)].answer = accept ? 1 : -1;
    play.askParty(asked);
}

uint64_t Chat::signature(float u, const Pointer& pointer) const {
    uint64_t h = 1469598103934665603ull;
    const auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    const auto mixf = [&](float f) { mix(uint64_t(int64_t(std::lround(f * 4.0f)))); };
    const auto mixs = [&](const std::string& s) {
        for (const char c : s) mix(uint8_t(c));
        mix(0xFF);
    };
    mix(open_);
    mixf(u);
    mixf(panel_.x), mixf(panel_.y), mixf(panel_.w), mixf(panel_.h);
    mix(log_.size());
    mix(log_.empty() ? 0 : std::hash<std::string>{}(log_.back().text));
    for (const Line& line : log_) mix(uint64_t(line.answer + 1));
    for (const auto& [key, count] : unread_) mixs(key), mix(uint64_t(count));
    mixs(tab_), mixs(me_), mixs(target_), mix(uint8_t(channel_));
    mix(inParty_), mix(inGuild_);
    for (const std::string& name : whispers_) mixs(name);
    if (open_) {
        mixs(draft_);
        mix(std::fmod(clock_, 1.0f) < 0.6f);
        mixf(logScroll_), mixf(tabScroll_), mix(uint8_t(drag_));
        // The pointer only where it lights something: on the panel or dragging.
        if (covers(pointer.x, pointer.y) || drag_ != Drag::None) mixf(pointer.x), mixf(pointer.y);
    } else {
        // Closed, the fading lines: each one's step down its fade.
        size_t n = 0;
        for (size_t i = log_.size(); i-- > 0 && n < size_t(closedRows(u)); ++n) {
            const float age = log_[i].age;
            mix(age < kStays ? 0 : age < kStays + kFades ? uint64_t((age - kStays) * 16.0f) + 1 : 1000);
        }
    }
    return h;
}

void Chat::layTabs(float u) {
    tabs_.clear();
    const auto add = [&](const std::string& key, const std::string& name, bool closable) {
        Tab tab;
        tab.key = key;
        tab.word = name;
        tab.closable = closable;
        tabs_.push_back(tab);
    };
    add("all", "All", false);
    add("map", "Map", false);
    if (inParty_) add("party", "Party", false);
    if (inGuild_) add("guild", "Guild", false);
    for (const std::string& name : whispers_) add("w:" + name, name, true);
    float x = 4.0f * u;
    for (Tab& tab : tabs_) {
        if (tab.closable && tab.key == "w:" + whispers_.front()) x += 7.0f * u;  // the divider
        const auto it = unread_.find(tab.key);
        const bool counted = it != unread_.end() && it->second > 0 && tab.key != tab_;
        tab.x = x;
        tab.w = 9.0f * u + wordWidth(kWord * u, tab.word) + 9.0f * u + (counted ? 22.0f * u : 0.0f) +
                (tab.closable ? 18.0f * u : 0.0f);
        x += tab.w;
    }
    tabsWide_ = x + 4.0f * u;
}

void Chat::layRows(float u, float wide, const std::string& tab, size_t from) {
    rows_.clear();
    const float size = kText * u;
    const float indent = 14.0f * u;
    for (size_t i = from; i < log_.size(); ++i) {
        const Line& line = log_[i];
        if (!shows(line, tab)) continue;
        float prefix = 0.0f;
        if (!line.name.empty()) {
            const bool out = line.channel == net::Channel::Whisper && line.name == me_ && !line.to.empty();
            const std::string who = out ? line.to : line.name;
            std::string tag = line.channel == net::Channel::Whisper ? (out ? "To" : "From")
                              : line.channel == net::Channel::Map  ? "Map"
                              : line.channel == net::Channel::Party ? "Party"
                                                                    : "Guild";
            prefix = wordWidth(kTag * u, tag) + 6.0f * u + bodyWidth(size, who) + bodyWidth(size, ": ");
        }
        // Greedy, by words; a word wider than a whole row is broken by letters.
        Row row;
        row.line = i;
        float room = wide - prefix;
        std::string rest = line.text;
        while (true) {
            if (bodyWidth(size, rest) <= room || rest.empty()) {
                row.text = rest;
                rows_.push_back(row);
                break;
            }
            size_t cut = 0, at = 0;
            while (at < rest.size()) {
                size_t next = rest.find(' ', at);
                if (next == std::string::npos) next = rest.size();
                if (bodyWidth(size, rest.substr(0, next)) > room) break;
                cut = next;
                at = next + 1;
            }
            if (cut == 0) {  // one long word: as many letters as fit
                cut = 1;
                while (cut < rest.size() && bodyWidth(size, rest.substr(0, cut + 1)) <= room) ++cut;
                while (cut < rest.size() && (uint8_t(rest[cut]) & 0xC0) == 0x80) ++cut;
            }
            row.text = rest.substr(0, cut);
            rows_.push_back(row);
            rest = rest.substr(std::min(rest.size(), cut + (cut < rest.size() && rest[cut] == ' ' ? 1 : 0)));
            row.first = false;
            room = wide - indent;
        }
    }
    logTall_ = float(rows_.size()) * kRow * u;
}

void Chat::cloud(gfx::Canvas& canvas, float middle, float tall, float strength, float u) {
    // The tracker's Gaussian, its middle on the screen's left edge: no edge anywhere.
    const float sx = 930.0f * u, sy = std::max(160.0f * u, tall * 0.5f / 0.72f);
    constexpr int kColumns = 12, kRows = 16;
    const auto at = [&](int i, int j) {
        const float gx = 3.0f * float(i) / kColumns;
        const float gy = -3.0f + 6.0f * float(j) / kRows;
        return faded(kVoid, kCloud * strength * std::exp(-0.5f * (gx * gx + gy * gy)));
    };
    for (int j = 0; j < kRows; ++j) {
        for (int i = 0; i < kColumns; ++i) {
            const float x0 = sx * 3.0f * float(i) / kColumns;
            const float y0 = middle + sy * (-3.0f + 6.0f * float(j) / kRows);
            canvas.shade({x0, y0, sx * 3.0f / kColumns, sy * 6.0f / kRows}, at(i, j), at(i + 1, j),
                         at(i + 1, j + 1), at(i, j + 1));
        }
    }
}

void Chat::update(float seconds, float width, float height, float bottom, float right,
                  const Pointer& pointer, float wheel, const std::string& typed, int backspaces,
                  bool enter, bool escape, bool toggle, Play& play) {
    (void)height;
    clock_ += seconds;
    for (Line& line : log_) line.age += seconds;
    const float u = tip::unit();
    if (play.isOpen()) {
        std::string name;
        if (play.playerName(play.realm().hero().id, &name, nullptr)) me_ = name;
        std::vector<net::Heard> heard;
        play.takeHeard(heard);
        for (const net::Heard& line : heard) {
            core::logf("chat: %s%s%s: %s", line.name.empty() ? "(server) " : "", line.name.c_str(),
                       line.to.empty() ? "" : (" to " + line.to).c_str(), line.text.c_str());
            hear(line);
            if (line.channel == net::Channel::Map && line.from != 0) {
                play.bubble(line.from, line.text.size() > 64 ? line.text.substr(0, 61) + "..." : line.text);
            }
        }
    }

    // Open and shut: Enter, the HUD's button, Escape.
    if (toggle) open_ = !open_;
    if (open_) {
        if (escape) {
            open_ = false;
        } else if (enter) {
            send(play);
        } else {
            for (int i = 0; i < backspaces && !draft_.empty(); ++i) draft_.resize(draft_.size() - lastLetter(draft_));
            for (const char c : typed) {
                if (uint8_t(c) < 0x20 || uint8_t(c) == 0x7F) continue;
                if (draft_.size() < net::kMostSaid) draft_.push_back(c);
            }
        }
    } else if (enter) {
        open_ = true;
    }

    // A party or guild he is no longer in takes its tab with it.
    inParty_ = play.party().members.size() > 1;
    if ((tab_ == "party" && !inParty_) || (tab_ == "guild" && !inGuild_)) select("all");

    // Where the next line goes: the tab's channel; on All his party's when he has one, else his
    // map's, or the word he pressed in their place.
    if (tab_.rfind("w:", 0) == 0) {
        channel_ = net::Channel::Whisper;
        target_ = tab_.substr(2);
    } else if (tab_ == "map") {
        channel_ = net::Channel::Map;
    } else if (tab_ == "party") {
        channel_ = net::Channel::Party;
    } else if (tab_ == "guild") {
        channel_ = net::Channel::Guild;
    } else {
        channel_ = chipSet_ ? chipChannel_ : inParty_ ? net::Channel::Party : net::Channel::Map;
    }

    // Where it stands: its foot over the HUD's plate, as wide as there is room for.
    const float left = kLeft * u;
    const float wide = std::max(240.0f * u, std::min(kWide * u, right - left));
    const float most = std::max(kTallLeast * u, std::min(kTallMost * u, bottom - 120.0f * u));
    const float tall = std::clamp((tall_ > 0.0f ? tall_ : kTallDefault) * u, kTallLeast * u, most);
    panel_ = {left, bottom - tall, wide, tall};
    grip_ = {left, panel_.y - kGrip * u, wide, kGrip * u};
    tabRow_ = {left, panel_.y, wide, kTabRow * u};
    input_ = {left + 8.0f * u, panel_.bottom() - kFoot * u - kInput * u - 2.0f * u, wide - 16.0f * u,
              kInput * u};
    logBox_ = {left + 12.0f * u, tabRow_.bottom() + 8.0f * u, wide - 20.0f * u,
               input_.y - 6.0f * u - (tabRow_.bottom() + 8.0f * u)};
    chip_ = {input_.x, input_.y, wordWidth(kWord * u, channelWord()) + 24.0f * u, kInput * u};

    layTabs(u);

    // The pointer, open only.
    const bool over = open_ && covers(pointer.x, pointer.y);
    bool chosen = false;
    if (pointer.pressed) {
        for (const Choice& choice : choices_) {
            if (!choice.box.has(pointer.x, pointer.y) || choice.line >= log_.size() ||
                log_[choice.line].inviter.empty()) {
                continue;
            }
            answer(play, long(choice.line), {}, choice.accept);
            chosen = true;
            break;
        }
    }
    if (open_ && pointer.pressed && over && !chosen) {
        if (grip_.has(pointer.x, pointer.y)) {
            drag_ = Drag::Grip;
            dragFrom_ = pointer.y;
            dragStart_ = tall;
        } else if (tabRow_.has(pointer.x, pointer.y) && tabsWide_ > tabRow_.w &&
                   pointer.y >= tabRow_.bottom() - 6.0f * u) {
            drag_ = Drag::TabSlider;
            dragFrom_ = pointer.x;
            dragStart_ = tabScroll_;
        } else if (tabRow_.has(pointer.x, pointer.y)) {
            const float x = pointer.x - tabRow_.x + tabScroll_;
            for (const Tab& tab : tabs_) {
                if (x < tab.x || x >= tab.x + tab.w) continue;
                if (tab.closable && x >= tab.x + tab.w - 22.0f * u) {
                    closeTab(tab.key);
                } else {
                    select(tab.key);
                }
                break;
            }
        } else if (chip_.has(pointer.x, pointer.y)) {
            // On All, the next of Map, Party and Guild in its place; elsewhere, the next tab. Only
            // those he is in: a party's or a guild's when he has one.
            std::vector<net::Channel> open{net::Channel::Map};
            if (inParty_) open.push_back(net::Channel::Party);
            if (inGuild_) open.push_back(net::Channel::Guild);
            const auto here = std::find(open.begin(), open.end(), channel_);
            const net::Channel next = here == open.end() || here + 1 == open.end() ? open.front() : *(here + 1);
            if (tab_ == "all") {
                chipSet_ = true;
                chipChannel_ = next;
            } else {
                select(next == net::Channel::Map ? "map" : next == net::Channel::Party ? "party" : "guild");
            }
        } else if (logTall_ > logBox_.h && pointer.x >= logBox_.right() - 10.0f * u &&
                   logBox_.has(pointer.x, pointer.y)) {
            drag_ = Drag::LogSlider;
            dragFrom_ = pointer.y;
            dragStart_ = logScroll_;
        }
    }
    if (drag_ != Drag::None && pointer.held) {
        if (drag_ == Drag::Grip) {
            tall_ = std::clamp(dragStart_ + (dragFrom_ - pointer.y), kTallLeast * u, most) / u;
        } else if (drag_ == Drag::TabSlider) {
            const float thumb = tabRow_.w * tabRow_.w / tabsWide_;
            const float travel = std::max(1.0f, tabRow_.w - thumb);
            tabScroll_ = dragStart_ + (pointer.x - dragFrom_) * (tabsWide_ - tabRow_.w) / travel;
        } else if (drag_ == Drag::LogSlider) {
            const float thumb = std::max(24.0f * u, logBox_.h * logBox_.h / std::max(1.0f, logTall_));
            const float travel = std::max(1.0f, logBox_.h - thumb);
            logScroll_ = dragStart_ - (pointer.y - dragFrom_) * (logTall_ - logBox_.h) / travel;
        }
    }
    if (!pointer.held || pointer.released) drag_ = Drag::None;
    if (open_ && wheel != 0.0f) {
        if (tabRow_.has(pointer.x, pointer.y)) tabScroll_ -= wheel * 40.0f * u;
        else if (logBox_.has(pointer.x, pointer.y)) logScroll_ += wheel * 1.5f * kRow * u;
    }

    // The log as set on the chosen tab, its scroll held where he left it as lines come in.
    const float before = logTall_;
    layRows(u, logBox_.w - 10.0f * u, tab_, 0);
    if (logScroll_ > 0.0f && logTall_ > before) logScroll_ += logTall_ - before;
    logScroll_ = std::clamp(logScroll_, 0.0f, std::max(0.0f, logTall_ - logBox_.h));
    tabScroll_ = std::clamp(tabScroll_, 0.0f, std::max(0.0f, tabsWide_ - tabRow_.w));

    const long pending = pendingInvite();
    inviting_ = pending >= 0;
    inviteCard_ = {width * 0.5f - kInviteWide * 0.5f * u, bottom - (kInviteAbove + kInviteTall) * u,
                   kInviteWide * u, kInviteTall * u};
    const uint64_t key = signature(u, pointer) ^ (uint64_t(pending + 1) * 0x9E3779B97F4A7C15ull) ^
                         (inviting_ && inviteCard_.has(pointer.x, pointer.y)
                              ? uint64_t(std::lround(pointer.x)) * 31 + uint64_t(std::lround(pointer.y))
                              : 0) ^
                         (inviting_ ? uint64_t(std::min(log_[size_t(pending)].age, kInviteIn) * 100.0f) +
                                          (uint64_t(std::max(0.0f, log_[size_t(pending)].age -
                                                                       (kInviteLife - kInviteOut)) * 30.0f) << 8)
                                    : 0);
    if (key == built_) return;
    built_ = key;
    back_.clear();
    tabsDrawn_.clear();
    lines_.clear();
    inviteDrawn_.clear();
    choices_.clear();
    if (open_) {
        build(u, pointer);
        showing_ = true;
    } else {
        buildClosed(u);
    }
    if (inviting_) buildInvite(u, width, bottom, pointer);
}

void Chat::build(float u, const Pointer& pointer) {
    cloud(back_, panel_.midY(), panel_.h, 1.0f, u);

    // The grip: a faint line along the top running into the up-and-down arrow at its right.
    const bool gripLit = drag_ == Drag::Grip || grip_.has(pointer.x, pointer.y);
    const uint32_t gripInk = gripLit ? kGoldBar : kGripInk;
    const float arrowX = grip_.right() - 14.0f * u, arrowMid = grip_.midY();
    back_.shade({grip_.x + 8.0f * u, arrowMid, arrowX - 12.0f * u - grip_.x - 8.0f * u, std::max(1.0f, u)},
                faded(gripInk, 0.0f), faded(gripInk, 0.6f), faded(gripInk, 0.6f), faded(gripInk, 0.0f));
    const float reach = 8.0f * u, head = 4.0f * u, line = 1.6f * u;
    for (const float drop : {1.0f * u, 0.0f}) {  // its drop first, then itself
        const uint32_t ink = drop > 0.0f ? kDrop : gripInk;
        const float x = arrowX + drop, y = arrowMid + drop;
        stroke(back_, x, y - reach, x, y + reach, line, ink);
        stroke(back_, x - head, y - reach + head, x, y - reach, line, ink);
        stroke(back_, x + head, y - reach + head, x, y - reach, line, ink);
        stroke(back_, x - head, y + reach - head, x, y + reach, line, ink);
        stroke(back_, x + head, y + reach - head, x, y + reach, line, ink);
    }

    // The tabs, on their own canvas clipped to the row.
    tabsDrawn_.clip(tabRow_);
    const float baseline = controls::middle(tabRow_.y, tabRow_.h, kWord * u);
    for (size_t i = 0; i < tabs_.size(); ++i) {
        const Tab& tab = tabs_[i];
        const float x = tabRow_.x + tab.x - tabScroll_;
        const bool chosen = tab.key == tab_;
        const bool whisper = tab.closable;
        if (whisper && i > 0 && !tabs_[i - 1].closable) {
            tabsDrawn_.rect({x - 4.0f * u, tabRow_.y + 12.0f * u, std::max(1.0f, u), tabRow_.h - 24.0f * u},
                            hex(0x2e241c));
        }
        if (chosen) {
            // The tracker's warm gold, rising from the bar.
            tabsDrawn_.shade({x, tabRow_.y, tab.w, tabRow_.h}, faded(kGoldGlow, 0.06f), faded(kGoldGlow, 0.06f),
                             faded(kGoldGlow, 0.38f), faded(kGoldGlow, 0.38f));
            tabsDrawn_.rect({x, tabRow_.bottom() - 3.0f * u, tab.w, 3.0f * u}, kGoldBar);
        }
        const bool hovered = tabRow_.has(pointer.x, pointer.y) && pointer.x >= x && pointer.x < x + tab.w;
        const uint32_t ink = chosen ? kTabLit : hovered ? kBody : whisper ? kTabWhisper : kTabIdle;
        float cx = x + 9.0f * u;
        cx += word(tabsDrawn_, cx, baseline, kWord * u, ink, tab.word, u);
        const auto it = unread_.find(tab.key);
        if (it != unread_.end() && it->second > 0 && !chosen) {
            const std::string count = std::to_string(std::min(it->second, 99));
            const float pill = std::max(16.0f * u, wordWidth(12.0f * u, count) + 8.0f * u);
            const Box badge{cx + 5.0f * u, tabRow_.midY() - 8.0f * u, pill, 16.0f * u};
            tabsDrawn_.rect(badge, whisper ? kWhisperInk : tab.key == "party" ? kPartyInk : kGuildInk);
            tabsDrawn_.lettered(*controls::wordFace(), controls::wordTexture(),
                                badge.midX() - wordWidth(12.0f * u, count) * 0.5f,
                                controls::middle(badge.y, badge.h, 12.0f * u), 12.0f * u, 0.0f, hex(0x0d0a08), count);
            cx = badge.right();
        }
        if (whisper) {
            const float mx = x + tab.w - 12.0f * u, my = tabRow_.midY(), r = 3.5f * u;
            const bool onClose = hovered && pointer.x >= x + tab.w - 22.0f * u;
            const uint32_t cross = onClose ? kTabLit : kHintInk;
            stroke(tabsDrawn_, mx - r, my - r, mx + r, my + r, 1.5f * u, cross);
            stroke(tabsDrawn_, mx + r, my - r, mx - r, my + r, 1.5f * u, cross);
        }
    }
    // The row's slider, when the row is longer than the panel.
    if (tabsWide_ > tabRow_.w) {
        const float thumb = tabRow_.w * tabRow_.w / tabsWide_;
        const float at = tabRow_.x + (tabRow_.w - thumb) * tabScroll_ / (tabsWide_ - tabRow_.w);
        const bool lit = drag_ == Drag::TabSlider ||
                         (tabRow_.has(pointer.x, pointer.y) && pointer.y >= tabRow_.bottom() - 6.0f * u);
        tabsDrawn_.rect({at, tabRow_.bottom() - 3.0f * u, thumb, 3.0f * u}, lit ? kIronLit : kIron);
    }
    rule(back_, tabRow_.x + 8.0f * u, tabRow_.bottom(), tabRow_.w - 16.0f * u, u);

    // The log, clipped to its well, the newest at the foot.
    lines_.clip(logBox_);
    const float size = kText * u;
    float y = logBox_.bottom() - logTall_ + logScroll_;
    for (size_t r = 0; r < rows_.size(); ++r) {
        const Row& row = rows_[r];
        const float top = y;
        y += kRow * u;
        const Line& line = log_[row.line];
        if (y < logBox_.y || top > logBox_.bottom()) continue;
        const float base = top + kRow * u * 0.74f;
        float x = logBox_.x + (row.first ? 0.0f : 14.0f * u);
        if (line.name.empty()) {
            // An invite's asker in the players' green, the rest in the server's gold.
            if (row.first && !line.inviter.empty() && row.text.rfind(line.inviter, 0) == 0) {
                x += body(lines_, x, base, size, kInviterInk, line.inviter, u);
                body(lines_, x, base, size, kSystemInk, row.text.substr(line.inviter.size()), u);
            } else {
                body(lines_, x, base, size, kSystemInk, row.text, u);
            }
            continue;
        }
        const uint32_t ink = channelInk(line.channel);
        if (row.first) {
            const bool out = line.channel == net::Channel::Whisper && line.name == me_ && !line.to.empty();
            const std::string tag = line.channel == net::Channel::Whisper ? (out ? "To" : "From")
                                    : line.channel == net::Channel::Map  ? "Map"
                                    : line.channel == net::Channel::Party ? "Party"
                                                                          : "Guild";
            x += word(lines_, x, base, kTag * u, faded(ink, 0.75f), tag, u) + 6.0f * u;
            x += body(lines_, x, base, size, kNameInk, out ? line.to : line.name, u);
            x += body(lines_, x, base, size, kQuiet, ": ", u);
        }
        body(lines_, x, base, size, line.channel == net::Channel::Whisper ? kWhisperBody : kBody, row.text, u);
    }
    // Its slider, when there is more than the well holds.
    if (logTall_ > logBox_.h) {
        const float thumb = std::max(24.0f * u, logBox_.h * logBox_.h / logTall_);
        const float at = logBox_.y + (logBox_.h - thumb) * (1.0f - logScroll_ / (logTall_ - logBox_.h));
        const bool lit = drag_ == Drag::LogSlider ||
                         (logBox_.has(pointer.x, pointer.y) && pointer.x >= logBox_.right() - 10.0f * u);
        back_.rect({logBox_.right() - 3.0f * u, at, 3.0f * u, thumb}, lit ? kIronLit : kIron);
    }

    // The input: the channel's word, what he has typed and the caret, on an iron rule.
    const float inputBase = controls::middle(input_.y, input_.h, kWord * u);
    const bool chipLit = chip_.has(pointer.x, pointer.y);
    const uint32_t chipInk = channelInk(channel_);
    float x = chip_.x + 4.0f * u;
    x += word(back_, x, inputBase, kWord * u, chipLit ? faded(chipInk, 1.0f) : chipInk, channelWord(), u);
    const float cy = input_.midY();
    stroke(back_, x + 5.0f * u, cy - 2.0f * u, x + 8.0f * u, cy + 1.5f * u, 1.5f * u, chipInk);
    stroke(back_, x + 11.0f * u, cy - 2.0f * u, x + 8.0f * u, cy + 1.5f * u, 1.5f * u, chipInk);
    const float textX = chip_.right() + 4.0f * u;
    const float room = input_.right() - textX - 6.0f * u;
    if (draft_.empty()) {
        const std::string hint = channel_ == net::Channel::Whisper ? "Whisper to " + target_
                                                                   : "Say to " + lower(channelWord());
        body(back_, textX, inputBase, size, kPlaceholder, hint, u);
    }
    // The tail of a draft wider than the field: its end, where he is typing.
    std::string shown = draft_;
    while (!shown.empty() && bodyWidth(size, shown) > room) shown.erase(0, 1);
    const float drawn = shown.empty() ? 0.0f : body(back_, textX, inputBase, size, kDraft, shown, u);
    if (std::fmod(clock_, 1.0f) < 0.6f) {
        back_.rect({textX + drawn + 1.0f * u, input_.midY() - 10.0f * u, 1.5f * u, 20.0f * u}, kDraft);
    }
    rule(back_, input_.x, input_.bottom() + 2.0f * u, input_.w, u);
    body(back_, panel_.x + 12.0f * u, panel_.bottom() - 9.0f * u, kHint * u, kHintInk,
         "/p party  \xC2\xB7  /g guild  \xC2\xB7  /w name  \xC2\xB7  /r reply  \xC2\xB7  /invite name  \xC2\xB7  /promote name  \xC2\xB7  /leave", u);
}

long Chat::pendingInvite() const {
    for (size_t i = log_.size(); i-- > 0;) {
        const Line& line = log_[i];
        if (!line.inviter.empty() && line.answer == 0 && line.age < kInviteLife) return long(i);
    }
    return -1;
}

void Chat::buildInvite(float u, float width, float bottom, const Pointer& pointer) {
    (void)width;
    (void)bottom;
    const long index = pendingInvite();
    if (index < 0) return;
    const Line& line = log_[size_t(index)];
    const float alpha = std::min(line.age / kInviteIn, 1.0f) *
                        std::clamp((kInviteLife - line.age) / kInviteOut, 0.0f, 1.0f);
    const Box& card = inviteCard_;
    // The arrival's soft cloud, a Gaussian each way about the card: no edge anywhere.
    const float sx = card.w * 0.42f, sy = card.h * 0.55f;
    constexpr int kColumns = 14, kRows = 8;
    const auto at = [&](int i, int j) {
        const float gx = -3.0f + 6.0f * float(i) / kColumns, gy = -3.0f + 6.0f * float(j) / kRows;
        return faded(kVoid, 0.85f * alpha * std::exp(-0.5f * (gx * gx + gy * gy)));
    };
    for (int j = 0; j < kRows; ++j) {
        for (int i = 0; i < kColumns; ++i) {
            inviteDrawn_.shade({card.midX() + sx * (-3.0f + 6.0f * float(i) / kColumns),
                                card.midY() + sy * (-3.0f + 6.0f * float(j) / kRows), sx * 6.0f / kColumns,
                                sy * 6.0f / kRows},
                               at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
        }
    }
    // "Party", the question, and the two answers.
    const std::string kicker = "Party";
    word(inviteDrawn_, card.midX() - wordWidth(kTag * u, kicker) * 0.5f, card.y + 18.0f * u, kTag * u,
         faded(kPartyInk, alpha), kicker, u);
    // The asker's name in the players' green, the rest in bone, the whole centred.
    const std::string asks = " invites you to a party.";
    const float wide = bodyWidth(21.0f * u, line.inviter) + bodyWidth(21.0f * u, asks);
    const float qx = card.midX() - wide * 0.5f;
    const float named = body(inviteDrawn_, qx, card.y + 50.0f * u, 21.0f * u, faded(kInviterInk, alpha), line.inviter, u);
    body(inviteDrawn_, qx + named, card.y + 50.0f * u, 21.0f * u, faded(hex(0xf3e8d4), alpha), asks, u);
    const float bw = 112.0f * u, bh = 36.0f * u, gap = 12.0f * u;
    for (const bool accept : {true, false}) {
        const Box box{card.midX() + (accept ? -gap * 0.5f - bw : gap * 0.5f), card.bottom() - bh - 6.0f * u, bw, bh};
        controls::State state;
        state.lift = box.has(pointer.x, pointer.y) ? 1.0f : 0.0f;
        const size_t mark = inviteDrawn_.mark();
        controls::button(inviteDrawn_, box, accept ? "Accept" : "Decline",
                         accept ? controls::Kind::Primary : controls::Kind::Secondary, state, u, 16.0f * u);
        if (alpha < 1.0f) inviteDrawn_.fadeSince(mark, alpha, 0.0f);
        choices_.push_back({box, size_t(index), accept});
    }
}

int Chat::closedRows(float u) const {
    // The open well's rows, less one for the key line under them.
    return std::max(kClosedLeast, int(logBox_.h / (kRow * u)) - 1);
}

void Chat::buildClosed(float u) {
    // The last lines on All, each as long as it has been up allows, the newest at full.
    lines_.clip({0.0f, 0.0f, 0.0f, 0.0f});
    const int rows = closedRows(u);
    std::vector<size_t> last;
    for (size_t i = log_.size(); i-- > 0 && last.size() < size_t(rows);) {
        if (log_[i].age < kStays + kFades) last.push_back(i);
        else break;
    }
    std::reverse(last.begin(), last.end());
    int waiting = 0;
    for (const auto& [key, count] : unread_) {
        if (key.rfind("w:", 0) == 0) waiting += count;
    }
    showing_ = !last.empty() || waiting > 0;
    if (!showing_) return;

    const float size = kText * u;
    const float wide = panel_.w - 24.0f * u;
    float strongest = 0.0f;
    std::vector<float> alphas;
    for (size_t k = 0; k < last.size(); ++k) {
        const float age = log_[last[k]].age;
        const float life = age < kStays ? 1.0f : std::max(0.0f, 1.0f - (age - kStays) / kFades);
        // The newest at full, each older one fainter, down to 0.3 at the top of the height.
        const float place = float(last.size() - 1 - k);  // 0 for the newest
        const float alpha = (1.0f - 0.7f * place / std::max(1.0f, float(rows - 1))) * life;
        alphas.push_back(alpha);
        strongest = std::max(strongest, alpha);
    }
    if (waiting > 0) strongest = std::max(strongest, 0.6f);
    const float foot = panel_.bottom() - 34.0f * u;
    // One row a line here, cut to the width: closed, it is a glance, not a read.
    const float tall = float(last.size()) * kRow * u + 40.0f * u;
    cloud(back_, foot - tall * 0.5f, std::max(tall, 180.0f * u), kCloudClosed * strongest, u);
    float y = foot - float(last.size()) * kRow * u;
    for (size_t k = 0; k < last.size(); ++k) {
        const Line& line = log_[last[k]];
        const float a = alphas[k];
        const float base = y + kRow * u * 0.74f;
        y += kRow * u;
        float x = panel_.x + 12.0f * u;
        std::string text = line.text;
        if (line.name.empty()) {
            while (!text.empty() && bodyWidth(size, text) > wide) text.pop_back();
            if (!line.inviter.empty() && text.rfind(line.inviter, 0) == 0) {
                x += body(lines_, x, base, size, faded(kInviterInk, a), line.inviter, u);
                text = text.substr(line.inviter.size());
            }
            body(lines_, x, base, size, faded(kSystemInk, a), text, u);
            continue;
        }
        const uint32_t ink = channelInk(line.channel);
        const bool out = line.channel == net::Channel::Whisper && line.name == me_ && !line.to.empty();
        const std::string tag = line.channel == net::Channel::Whisper ? (out ? "To" : "From")
                                : line.channel == net::Channel::Map  ? "Map"
                                : line.channel == net::Channel::Party ? "Party"
                                : line.channel == net::Channel::Guild ? "Guild"
                                                                      : "";
        if (!tag.empty()) x += word(lines_, x, base, kTag * u, faded(ink, a), tag, u) + 6.0f * u;
        x += body(lines_, x, base, size, faded(kNameInk, a), out ? line.to : line.name, u);
        x += body(lines_, x, base, size, faded(kQuiet, a), ": ", u);
        const float room = panel_.x + 12.0f * u + wide - x;
        bool cut = false;
        while (!text.empty() && bodyWidth(size, text) > room) {
            text.resize(text.size() - lastLetter(text));
            cut = true;
        }
        if (cut) text += "...";
        body(lines_, x, base, size, faded(line.channel == net::Channel::Whisper ? kWhisperBody : kBody, a), text, u);
    }
    // Enter to chat, and the whispers waiting.
    const float keyMid = foot + 16.0f * u;
    float x = panel_.x + 12.0f * u;
    x += controls::keycap(back_, x, keyMid, "Enter", u) + 8.0f * u;
    x += body(back_, x, keyMid + 5.0f * u, 15.0f * u, hex(0xa39888), "to chat", u) + 12.0f * u;
    if (waiting > 0) {
        body(back_, x, keyMid + 5.0f * u, 15.0f * u, kWhisperInk,
             waiting == 1 ? "1 whisper" : std::to_string(waiting) + " whispers", u);
    }
}

}  // namespace mu::game
