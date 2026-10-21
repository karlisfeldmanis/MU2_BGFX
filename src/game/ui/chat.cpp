#include "game/ui/chat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>

#include "core/log.h"
#include "core/paths.h"
#include "game/play.h"
#include "game/roster.h"
#include "game/ui/controls.h"
#include "game/ui/describe.h"
#include "game/ui/guild_mark.h"
#include "game/ui/stage.h"
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

// The channels' inks, as built on 2026-10-09 and kept ("channel colors was fine"): told apart by
// lightness as well as hue. A line wears its channel's whole (WoW's), names and all, so no name has
// a colour of its own (the user, 2026-10-09: names one colour).
constexpr uint32_t kMapInk = hex(0xd9cfbd), kPartyInk = hex(0x7fb6f2), kGuildInk = hex(0x93d67e);
constexpr uint32_t kWhisperInk = hex(0xe7a0e4), kSystemInk = hex(0xd6ad5c);
constexpr uint32_t kLocalInk = hex(0xf3eee6), kGlobalInk = hex(0xeb9c62);
// A pickup's line, quiet, so the thing's own colour is what stands out.
constexpr uint32_t kLootInk = hex(0xa99f8f);
// An invite's asker, in the green of the name over a player's head (vitals.cpp, kTagGreen): the
// one name in the chat with a colour of its own, on the card in the middle and its line.
constexpr uint32_t kInviterInk = gfx::rgba(0.18f, 1.0f, 0.18f);
// Sanctuary's (game/ui/style.h): bone, ash, iron, blood.
constexpr uint32_t kBoneHi = hex(0xf3e8d4), kBone2 = hex(0xc9bfae), kAshInk = hex(0x8f8575), kAshInk2 = hex(0x5c5249);
constexpr uint32_t kIronHi = hex(0xa88b6c), kIron = hex(0x6b5645), kIronLo = hex(0x3d3128);
constexpr uint32_t kBloodHi = hex(0xf06150), kVoid = hex(0x050303), kDrop = hex(0x000000, 0.75f);
// A tab at rest, a step lighter than the ash so it reads over a bright floor.
constexpr uint32_t kTabQuiet = hex(0xa99f8f);
// The panel: a flat fill, its edge brighter at the top, and a warm light from the chosen tab's top
// running down into the panel ("active tab has to be perfectly merged with container color").
constexpr uint32_t kFill = hex(0x080605);
constexpr uint32_t kEdgeTop = hex(0xa88b6c, 0.5f), kEdgeFoot = hex(0x3d3128, 0.2f);
constexpr uint32_t kWarm = hex(0xffecd2);
constexpr float kTabLight = 0.09f, kSeamLight = 0.04f, kPanelLight = 48.0f;
// The whisper's count on a purple pill ("whisper number has to be on purple circle").
constexpr uint32_t kPillTop = hex(0x9d579a), kPillFoot = hex(0x7a3d77);
// And the other tabs' counts on pills of their own ("work on 2 whisper and 2 global"): Global's
// from its orange, darkened so the white figure reads; General's plain iron.
constexpr uint32_t kGlobalPillTop = hex(0xa8653a), kGlobalPillFoot = hex(0x7c4523);
constexpr uint32_t kIronPillTop = hex(0x5a4838), kIronPillFoot = hex(0x3d3128);
constexpr uint32_t kPillRing = hex(0xffffff, 0.14f);
constexpr float kPill = 18.0f, kPillFigure = 13.0f, kPillGap = 6.0f;
// A menu's sheet: PlayerMenu's, so the chat's menus are the party bar's.
constexpr uint32_t kSheet = gfx::rgba(0.051f, 0.039f, 0.031f, 0.97f);
constexpr uint32_t kRowLit = gfx::rgba(1.0f, 0.94f, 0.86f, 0.07f);

// Measures, in tip::unit() (a pixel at 1080 lines), from the canvas's 1080p boards.
constexpr float kLeftFirst = 32.0f, kFootFirst = 92.0f, kWideFirst = 460.0f, kTallFirst = 360.0f;
constexpr float kWideLeast = 340.0f, kTallLeast = 240.0f, kWideMost = 760.0f, kTallMost = 720.0f;
constexpr float kTabRow = 30.0f, kPadTop = 12.0f, kPadSide = 14.0f, kPadFoot = 14.0f;
constexpr float kInput = 40.0f, kInputGap = 12.0f, kIndent = 14.0f;
constexpr float kRadius = 4.0f;
constexpr float kWord = 15.0f, kDraft = 17.0f;
// The text's three sizes and their rows (the user, 2026-10-10: the log a step smaller, 16 on 21).
constexpr float kTextSizes[3] = {14.0f, 16.0f, 18.0f}, kRowTalls[3] = {19.0f, 21.0f, 24.0f};
constexpr float kSettingsWide = 290.0f, kSettingsGap = 6.0f, kSettingsRow = 30.0f;
// Idle: a line just come stands at full for kNewFor and settles over kSettle; the pointer on the
// chat lights it over kLight (style.h's hover).
constexpr float kNewFor = 6.0f, kSettle = 1.0f, kLight = 0.12f;
constexpr size_t kKept = 400;      // lines kept, oldest dropped
constexpr size_t kSentKept = 32;   // lines Up can bring back
// The invite in the middle: how long it stands unanswered (the log's line and /accept stay),
// its fade in and out, and how far its foot stands over the plate's top -- above where a fight's
// Zen is said (game/ui/tally.cpp, kZenAboveHud).
constexpr float kInviteLife = 60.0f, kInviteIn = 0.15f, kInviteOut = 1.0f;
// A trade's ask stands as long as the realm holds it (sim::kDealAskTicks).
constexpr float kTradeLife = float(sim::kDealAskTicks) / 20.0f;
constexpr float kInviteAbove = 44.0f, kInviteWide = 440.0f, kInviteTall = 118.0f;
// A guild's card: its quiet line's room, its mark, and the bar under its buttons.
constexpr float kGuildCardMore = 30.0f, kGuildMarkSide = 30.0f, kGuildBarWide = 236.0f;
// The suggestions for a "[" being typed: how many, and a row's height.
constexpr size_t kSuggestMost = 8;
constexpr float kSuggestRow = 26.0f;
// A menu's measures (PlayerMenu's).
constexpr float kMenuRow = 26.0f, kMenuHead = 24.0f, kMenuPad = 12.0f;

// What a tab shows, as bits: a channel's (1 << Channel), and the two kinds General adds of its own.
constexpr uint32_t bit(net::Channel channel) { return 1u << uint32_t(channel); }
constexpr uint32_t kLootBit = 1u << 16, kAnswerBit = 1u << 17;
constexpr uint32_t kChannelsAll = (1u << net::kChannels) - 1u;
constexpr uint32_t kEvery = kChannelsAll | kLootBit | kAnswerBit;
constexpr uint32_t kGeneralFirst = kEvery & ~bit(net::Channel::Global);
constexpr uint32_t kGlobalFirst = bit(net::Channel::Global) | bit(net::Channel::Server);
// The rows of a tab's list, in its order, and what each reaches.
struct Listed {
    uint32_t bits;
    const char* word;
    const char* who;
    uint32_t ink;
};
constexpr Listed kListed[] = {
    {bit(net::Channel::Local), "Say", "near you", kLocalInk},
    {bit(net::Channel::Map), "Map", "your map", kMapInk},
    {bit(net::Channel::Party), "Party", "your party", kPartyInk},
    {bit(net::Channel::Guild), "Guild", "your guild", kGuildInk},
    {bit(net::Channel::Global), "Global", "everyone online", kGlobalInk},
    {bit(net::Channel::Whisper), "Whispers", "to you", kWhisperInk},
    {bit(net::Channel::Server), "Server notices", "", kSystemInk},
    {kLootBit, "Loot", "you and party", kLootInk},
    {kAnswerBit, "Invites and answers", "", kSystemInk},
};

float body(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink, const std::string& text) {
    return controls::label(canvas, x, baseline, size, ink, text);
}
float word(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink, const std::string& text) {
    const gfx::Face* face = controls::wordFace();
    if (face == nullptr) return controls::label(canvas, x, baseline, size, ink, text);
    return canvas.lettered(*face, controls::wordTexture(), x, baseline, size, 0.0f, ink, text);
}
float bodyWidth(float size, const std::string& text) { return controls::labelWidth(size, text); }
float wordWidth(float size, const std::string& text) {
    const gfx::Face* face = controls::wordFace();
    return face ? face->measure(size, text) : controls::labelWidth(size, text);
}

// Where a line's ink stands about its pen and baseline (the glyphs' own boxes, as Canvas::lettered
// lays them): a figure is centred on what is drawn, not on the line. Alegreya's figures are
// old-style and sit low, so a pill centred on the line looked dropped.
struct Ink {
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
};
Ink inkOf(const gfx::Face& face, float size, const std::string& text) {
    Ink ink;
    bool first = true;
    float pen = 0.0f;
    const float k = face.emScale(size);
    for (const char c : text) {
        const gfx::FaceGlyph* g = face.glyph(c);
        if (g == nullptr) continue;
        if (g->x1 > g->x0 && g->y1 > g->y0) {
            const Ink one{pen + g->x0 * k, pen + g->x1 * k, g->y0 * k, g->y1 * k};
            if (first) ink = one;
            ink.left = std::min(ink.left, one.left), ink.right = std::max(ink.right, one.right);
            ink.top = std::min(ink.top, one.top), ink.bottom = std::max(ink.bottom, one.bottom);
            first = false;
        }
        pen += g->advance * k;
    }
    return ink;
}

// An unread count's pill: at least round, wider for two figures.
float pillWidth(float u, const std::string& count) {
    const gfx::Face* face = controls::wordFace();
    const float figures = face ? inkOf(*face, kPillFigure * u, count).right - inkOf(*face, kPillFigure * u, count).left
                               : controls::labelWidth(kPillFigure * u, count);
    return std::max(kPill * u, std::round(figures + 11.0f * u));
}

// The pill itself, centred on `midY` from `x`: its drop, a faint light ring, the body graded down,
// and the figure in white, centred on its ink. Returns its width.
float pill(gfx::Canvas& canvas, float x, float midY, float u, const std::string& count, uint32_t top,
           uint32_t foot, float alpha) {
    const float line = std::max(1.0f, std::round(u));
    const Box box{std::round(x), std::round(midY - kPill * u * 0.5f), pillWidth(u, count), std::round(kPill * u)};
    const float r = box.h * 0.5f;
    const float round[4] = {r, r, r, r};
    tip::rounded(canvas, {box.x, box.y + line, box.w, box.h}, round, faded(kDrop, 0.55f * alpha),
                 faded(kDrop, 0.55f * alpha));
    tip::rounded(canvas, box, round, faded(kPillRing, alpha), faded(kPillRing, alpha * 0.5f));
    const Box in = box.grown(-line);
    const float inner[4] = {r - line, r - line, r - line, r - line};
    tip::rounded(canvas, in, inner, faded(top, alpha), faded(foot, alpha));
    const float size = kPillFigure * u;
    const gfx::Face* face = controls::wordFace();
    if (face == nullptr) {
        controls::label(canvas, box.x + 5.0f * u, box.midY() + size * 0.33f, size, faded(0xFFFFFFFFu, alpha), count);
        return box.w;
    }
    const Ink ink = inkOf(*face, size, count);
    const float penX = box.midX() - (ink.left + ink.right) * 0.5f;
    const float baseline = box.midY() - (ink.top + ink.bottom) * 0.5f;
    canvas.lettered(*face, controls::wordTexture(), std::round(penX * 2.0f) * 0.5f, std::round(baseline * 2.0f) * 0.5f,
                    size, 0.0f, faded(0xFFFFFFFFu, alpha), count);
    return box.w;
}

// A stroke from one point to another, `wide` across: a cross, a corner's turn.
void stroke(gfx::Canvas& canvas, float x0, float y0, float x1, float y1, float wide, uint32_t ink) {
    const float dx = x1 - x0, dy = y1 - y0, length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0f) return;
    const float nx = -dy / length * wide * 0.5f, ny = dx / length * wide * 0.5f;
    const float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    const uint32_t inks[4] = {ink, ink, ink, ink};
    canvas.polygon(xy, inks, 4);
}

uint32_t mix(uint32_t a, uint32_t b, float t) {
    const auto at = [&](int shift) {
        const float x = float((a >> shift) & 0xFFu), y = float((b >> shift) & 0xFFu);
        return uint32_t(std::lround(x + (y - x) * std::clamp(t, 0.0f, 1.0f))) << shift;
    };
    return at(0) | at(8) | at(16) | at(24);
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

std::string clockNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char text[8];
    std::snprintf(text, sizeof text, "%02d:%02d", local.tm_hour, local.tm_min);
    return text;
}

// Whether a line of the server's is news of an invite or a trade: General's "Invites and answers".
bool answerish(const std::string& text) {
    for (const char* sign : {"accept", "declin", "joined", "did not answer", "rading with", "to trade",
                             "invite", "Traded with", "was cancelled"}) {
        if (text.find(sign) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

struct Chat::Carried {
    std::vector<Line> log;
    std::vector<std::string> whispers;
    std::map<std::string, int> unread;
    std::string tab, lastFrom, draft;
    Sticky sticky;
    std::vector<Sent> sent;
    bool wasInParty = false;
};

std::unique_ptr<Chat::Carried> Chat::carried_;

void Chat::carry() {
    auto kept = std::make_unique<Carried>();
    for (Line& line : log_) {
        // A trade's ask was to a body of this world: its card does not come along.
        if (line.trader != 0 && line.answer == 0) line.age = std::max(line.age, kTradeLife);
        line.resolved = nullptr;  // read again against the next world's tables
    }
    kept->log = std::move(log_);
    kept->whispers = std::move(whispers_);
    kept->unread = std::move(unread_);
    kept->tab = tab_;
    kept->lastFrom = lastFrom_;
    kept->draft = draft_;
    kept->sticky = sticky_;
    kept->sent = std::move(sent_);
    kept->wasInParty = wasInParty_;
    carried_ = std::move(kept);
}

void Chat::open(const gfx::Interface& interface) {
    interface.adopt(back_);
    interface.adopt(inviteDrawn_);
    interface.adopt(tabsDrawn_);
    interface.adopt(lines_);
    interface.adopt(tip_);
    interface.adopt(settingsDrawn_);
}

bool Chat::covers(float x, float y) const {
    return (showing_ && (frame_.has(x, y) || (settingsOpen_ && settingsBox_.has(x, y)) ||
                         (menu_ != Menu::None && menuBox_.has(x, y)) ||
                         (open_ && !suggestions_.empty() && suggestBox_.has(x, y)))) ||
           (inviting_ && inviteCard_.has(x, y));
}

bool Chat::takeMenuAsk(std::string* name, float* x, float* y) {
    if (menuAsk_.empty()) return false;
    *name = std::exchange(menuAsk_, std::string());
    *x = menuAskX_;
    *y = menuAskY_;
    return true;
}

uint32_t Chat::channelInk(net::Channel channel) const {
    switch (channel) {
        case net::Channel::Map: return kMapInk;
        case net::Channel::Party: return kPartyInk;
        case net::Channel::Guild: return kGuildInk;
        case net::Channel::Whisper: return kWhisperInk;
        case net::Channel::Server: return kSystemInk;
        case net::Channel::Local: return kLocalInk;
        case net::Channel::Global: return kGlobalInk;
    }
    return kMapInk;
}

uint32_t Chat::inkOf(const Line& line) const {
    if (line.kind == Kind::Loot) return kLootInk;
    if (line.kind == Kind::Answer || line.name.empty()) {
        // A whisper's own notice ("Brakka is not here.") stays in the whisper's ink.
        return line.channel == net::Channel::Whisper ? kWhisperInk : kSystemInk;
    }
    return channelInk(line.channel);
}

float Chat::textSize() const { return kTextSizes[std::clamp(size_, 0, 2)]; }
float Chat::rowTall() const { return kRowTalls[std::clamp(size_, 0, 2)]; }

uint32_t& Chat::filter(const std::string& tab) {
    if (tab == "global") return globalShows_;
    if (tab == "whispers") return whispersShows_;
    return generalShows_;
}

bool Chat::ignored(const std::string& name) const {
    return !name.empty() && std::find(ignored_.begin(), ignored_.end(), lower(name)) != ignored_.end();
}

void Chat::load() {
    loaded_ = true;
    generalShows_ = kGeneralFirst;
    globalShows_ = kGlobalFirst;
    whispersShows_ = bit(net::Channel::Whisper);
    if (carried_) {
        log_ = std::move(carried_->log);
        whispers_ = std::move(carried_->whispers);
        unread_ = std::move(carried_->unread);
        tab_ = carried_->tab;
        lastFrom_ = carried_->lastFrom;
        draft_ = carried_->draft;
        sticky_ = carried_->sticky;
        sent_ = std::move(carried_->sent);
        wasInParty_ = carried_->wasInParty;
        carried_.reset();
    }
    const std::string user = core::folders().user;
    if (user.empty()) return;
    std::ifstream in(user + "/chat.txt");
    std::string key;
    int version = 1;
    while (in >> key) {
        int n = 0;
        if (key == "general" || key == "global") {
            uint32_t shows = 0;
            if (in >> shows) filter(key) = shows & kEvery;
        } else if (key == "version") {
            in >> version;
        } else if (key == "left-global") {
            leftGlobal_ = true;
        } else if (key == "ignores") {
            // A line from before the realms ("ignore name") reads as no one's: it names a
            // character on some realm, and which is not written.
            std::string realm, name;
            if (in >> realm >> name) {
                if (realm == realm_) ignored_.push_back(lower(name));
                else elsewhere_.push_back(realm + " " + lower(name));
            }
        } else if (key == "size" && in >> n) {
            size_ = std::clamp(n, 0, 2);
        } else if (key == "fill" && in >> n) {
            fill_ = std::clamp(n, 0, 100);
        } else if (key == "idle" && in >> n) {
            idle_ = std::clamp(n, 0, 100);
        } else if (key == "stamps" && in >> n) {
            stamps_ = n != 0;
        } else if (key == "bubbles" && in >> n) {
            bubbles_ = n != 0;
        } else if (key == "whispertab" && in >> n) {
            whisperTab_ = n != 0;
        } else if (key == "lock" && in >> n) {
            locked_ = n != 0;
        } else if (key == "frame") {
            float l = 0.0f, f = 0.0f, w = 0.0f, t = 0.0f;
            if (in >> l >> f >> w >> t) left_ = l, foot_ = f, wide_ = w, tall_ = t;
        } else {
            std::getline(in, key);
        }
    }
    // A file from before the loot and the answers had their ticks: General shows both.
    if (version < 2) generalShows_ |= kLootBit | kAnswerBit;
}

void Chat::save() const {
    const std::string user = core::folders().user;
    if (user.empty()) return;
    std::ofstream out(user + "/chat.txt");
    out << "version 2\ngeneral " << generalShows_ << "\nglobal " << globalShows_ << "\n";
    out << "size " << size_ << "\nfill " << fill_ << "\nidle " << idle_ << "\nstamps " << int(stamps_)
        << "\nbubbles " << int(bubbles_) << "\nwhispertab " << int(whisperTab_) << "\nlock " << int(locked_)
        << "\n";
    if (wide_ > 0.0f) out << "frame " << left_ << " " << foot_ << " " << wide_ << " " << tall_ << "\n";
    if (leftGlobal_) out << "left-global\n";
    for (const std::string& name : ignored_) out << "ignores " << realm_ << " " << name << "\n";
    for (const std::string& line : elsewhere_) out << "ignores " << line << "\n";
}

std::string Chat::channelWord() const {
    switch (sticky_.channel) {
        case net::Channel::Map: return "Map";
        case net::Channel::Party: return "Party";
        case net::Channel::Guild: return "Guild";
        case net::Channel::Whisper: return "To " + sticky_.to;
        case net::Channel::Global: return "Global";
        case net::Channel::Local:
        case net::Channel::Server: return "Say";
    }
    return "Say";
}

void Chat::stick(net::Channel channel, const std::string& to) {
    sticky_.channel = channel;
    sticky_.to = channel == net::Channel::Whisper ? to : std::string();
    if (channel == net::Channel::Whisper && !to.empty()) {
        whispers_.erase(std::remove(whispers_.begin(), whispers_.end(), to), whispers_.end());
        whispers_.push_back(to);
    }
}

void Chat::system(net::Channel channel, const std::string& text) {
    net::Heard line;
    line.channel = channel;
    line.text = text;
    hear(line);
}

bool Chat::askedToTrade(const std::string& name, uint32_t who) {
    if (ignored(name)) return false;
    Line line;
    line.channel = net::Channel::Server;
    line.kind = Kind::Answer;
    line.stamp = clockNow();
    line.inviter = name.empty() ? std::string("Someone") : name;
    line.text = line.inviter + " wants to trade with you.";
    line.shown = line.text;
    line.trader = who;
    log_.push_back(line);
    if (log_.size() > kKept) log_.erase(log_.begin(), log_.begin() + long(log_.size() - kKept));
    return true;
}

std::string Chat::guildSaid(float within) const {
    for (size_t i = log_.size(); i-- > 0;) {
        const Line& line = log_[i];
        if (line.age > within) break;
        if (line.channel == net::Channel::Guild && line.name.empty() && !line.guild) return line.text;
    }
    return {};
}

bool Chat::guildCalled(const net::GuildCall& call) {
    if (ignored(call.from)) return false;
    Line line;
    line.channel = net::Channel::Guild;
    line.kind = Kind::Answer;
    line.stamp = clockNow();
    line.inviter = call.from.empty() ? std::string("Someone") : call.from;
    line.guild = true;
    line.mark = call.mark;
    line.guildName = call.guildName;
    line.text = call.ask ? line.inviter + " asks to join " + call.guildName + "."
                         : line.inviter + " invites you to the guild " + call.guildName + ".";
    line.shown = line.text;
    const std::string kin = className(sim::Kin(call.ask ? call.fromKin : call.masterKin));
    std::string world = call.fromWorld;
    if (!world.empty()) world[0] = char(std::toupper(uint8_t(world[0])));
    if (call.ask) {
        // What he would take in: who asks, and where he stands.
        line.meta = kin + " " + std::to_string(call.fromLevel) + (call.fromInParty ? " \xC2\xB7 in a party" : " \xC2\xB7 in no party") +
                    (world.empty() ? std::string() : " \xC2\xB7 " + world);
    } else {
        // What he would join: its size, its master and where he stands.
        line.meta = std::to_string(call.members) + " of " + std::to_string(call.cap) + " members \xC2\xB7 master " +
                    call.master + ", " + kin + " " + std::to_string(call.masterLevel) +
                    (world.empty() ? std::string() : " \xC2\xB7 " + world);
    }
    log_.push_back(line);
    if (log_.size() > kKept) log_.erase(log_.begin(), log_.begin() + long(log_.size() - kKept));
    return true;
}

bool Chat::hear(const net::Heard& heard) {
    if (!loaded_) load();
    const bool mine = !me_.empty() && heard.name == me_;
    if (heard.channel == net::Channel::Global && leftGlobal_) return false;
    if (!mine && ignored(heard.name)) return false;
    Line line;
    line.channel = heard.channel;
    line.name = heard.name;
    line.to = heard.to;
    line.text = heard.text;
    line.bot = heard.bot;
    line.stamp = clockNow();
    // The server's invite (server/src/main.cpp, partyAsk): its asker, and the line without the
    // typing it asks for, since the card wears its own buttons. A raid's is said so.
    constexpr const char* kInvites = " invites you to a party.";
    constexpr const char* kRaidInvites = " invites you to a raid.";
    const size_t partyAt = line.text.find(kInvites);
    const size_t at = partyAt != std::string::npos ? partyAt : line.text.find(kRaidInvites);
    if (line.channel == net::Channel::Party && line.name.empty() && at != std::string::npos && at > 0) {
        line.inviter = line.text.substr(0, at);
        line.text = line.inviter + (partyAt != std::string::npos ? kInvites : kRaidInvites);
        if (ignored(line.inviter)) return false;
    }
    // The server's news of an invite or a trade: General's "Invites and answers".
    if (line.name.empty() && line.channel != net::Channel::Whisper && line.channel != net::Channel::Global &&
        (!line.inviter.empty() || answerish(line.text))) {
        line.kind = Kind::Answer;
    }
    read(line);
    log_.push_back(line);
    if (log_.size() > kKept) log_.erase(log_.begin(), log_.begin() + long(log_.size() - kKept));
    if (line.channel == net::Channel::Whisper && !line.name.empty()) {
        const std::string partner = mine && !line.to.empty() ? line.to : line.name;
        whispers_.erase(std::remove(whispers_.begin(), whispers_.end(), partner), whispers_.end());
        whispers_.push_back(partner);
        if (!mine) lastFrom_ = partner;
    }
    if (mine) return true;
    for (const char* key : {"general", "global", "whispers"}) {
        if (tab_ == key || (std::string(key) == "whispers" && !whisperTab_)) continue;
        if (shows(log_.back(), key)) ++unread_[key];
    }
    return true;
}

void Chat::read(Line& line) {
    line.links = sim::findLinks(line.text);
    line.shown.clear();
    line.resolved = nullptr;
    size_t at = 0;
    for (sim::ItemLink& link : line.links) {
        line.shown += line.text.substr(at, link.from - at);
        at = link.to;
        link.from = line.shown.size();
        line.shown += "[" + link.label + "]";
        link.to = line.shown.size();
    }
    line.shown += line.text.substr(at);
}

void Chat::resolve(Line& line) const {
    if (line.resolved == tables_ || line.links.empty()) return;
    line.resolved = tables_;
    for (sim::ItemLink& link : line.links) {
        link.held.item = tables_ ? sim::linkRow(*tables_, link.group, link.number) : -1;
    }
}

uint32_t Chat::linkInk(const sim::ItemLink& link) const {
    if (!tables_ || link.held.item < 0 || size_t(link.held.item) >= tables_->items.size()) return kAshInk;
    return tip::colourOf(qualityOf(tables_->items[size_t(link.held.item)], link.held));
}

Chat::Set Chat::setOf(const Line& line) const {
    Set set;
    if (stamps_ && !line.stamp.empty()) {
        set.text = "[" + line.stamp + "] ";
        set.stampTo = set.text.size();
    }
    const auto named = [&](const std::string& before, const std::string& who, const std::string& after) {
        set.text += before;
        set.nameFrom = set.text.size();
        set.text += "[" + who + "]";
        set.nameTo = set.text.size();
        set.who = who;
        set.text += after;
    };
    if (!line.name.empty()) {
        const bool out = line.channel == net::Channel::Whisper && line.name == me_ && !line.to.empty();
        switch (line.channel) {
            case net::Channel::Whisper:
                if (out) named("To ", line.to, ": ");
                else named("", line.name, " whispers: ");
                break;
            case net::Channel::Local: named("", line.name, ": "); break;
            case net::Channel::Map: named("[Map] ", line.name, ": "); break;
            case net::Channel::Party: named("[Party] ", line.name, ": "); break;
            case net::Channel::Guild: named("[Guild] ", line.name, ": "); break;
            case net::Channel::Global: named("[1. Global] ", line.name, ": "); break;
            case net::Channel::Server: named("", line.name, ": "); break;
        }
    } else if (!line.inviter.empty() && line.shown.rfind(line.inviter, 0) == 0) {
        // An invite's asker answers the pointer too, unbracketed as the server says him.
        set.nameFrom = set.text.size();
        set.nameTo = set.nameFrom + line.inviter.size();
        set.who = line.inviter;
    }
    const size_t offset = set.text.size();
    set.text += line.shown;
    set.links = line.links;
    for (sim::ItemLink& link : set.links) {
        link.from += offset;
        link.to += offset;
    }
    return set;
}

float Chat::spans(gfx::Canvas& canvas, float x, float base, float size, uint32_t ink, const Set& set,
                  size_t start, size_t end, float alpha, float u, long line, const Pointer& pointer) {
    // The row cut where its parts change: the stamp, the name, each link.
    std::vector<size_t> cuts{start, end};
    const auto cut = [&](size_t at) {
        if (at > start && at < end) cuts.push_back(at);
    };
    cut(set.stampTo);
    if (set.nameTo > set.nameFrom) cut(set.nameFrom), cut(set.nameTo);
    for (const sim::ItemLink& link : set.links) cut(link.from), cut(link.to);
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    float advance = 0.0f;
    for (size_t k = 0; k + 1 < cuts.size(); ++k) {
        const size_t a = cuts[k], b = cuts[k + 1];
        const std::string part = set.text.substr(a, b - a);
        const float wide = bodyWidth(size, part);
        const Box box{x + advance, base - size * 0.95f, wide, size * 1.3f};
        uint32_t partInk = ink;
        if (a < set.stampTo) {
            partInk = kAshInk;
        } else if (set.nameTo > set.nameFrom && a >= set.nameFrom && b <= set.nameTo) {
            const bool inviter = !log_.empty() && line >= 0 && size_t(line) < log_.size() &&
                                 !log_[size_t(line)].inviter.empty() && log_[size_t(line)].name.empty();
            if (inviter) partInk = kInviterInk;
            if (line >= 0) {
                nameBoxes_.push_back({box, set.who});
                if (box.has(pointer.x, pointer.y) && open_ + lit_ > 0.5f) {
                    // Lit and underlined under the pointer: it answers a click.
                    partInk = mix(partInk, hex(0xffffff), 0.35f);
                    canvas.rect({box.x, base + 3.0f * u, wide, std::max(1.0f, u)}, faded(partInk, 0.6f * alpha));
                }
            }
        } else {
            for (size_t i = 0; i < set.links.size(); ++i) {
                if (a >= set.links[i].from && b <= set.links[i].to) {
                    partInk = linkInk(set.links[i]);
                    if (line >= 0) linkBoxes_.push_back({box, size_t(line), i});
                    break;
                }
            }
        }
        advance += body(canvas, x + advance, base, size, faded(partInk, alpha), part);
    }
    return advance;
}

void Chat::link(const content::ItemRow& row, const sim::Held& held) {
    const std::string token = sim::itemLink(row, held);
    std::string next = draft_;
    if (!next.empty() && next.back() != ' ') next += ' ';
    next += token + " ";
    open_ = true;
    if (next.size() > net::kMostSaid) {
        system(net::Channel::Server, "No room for another item in this line.");
        return;
    }
    draft_ = next;
}

void Chat::suggest(const sim::Realm& realm) {
    const size_t b = draft_.rfind('[');
    if (!open_ || !tables_ || b == std::string::npos || draft_.find(']', b) != std::string::npos ||
        draft_.find('}', b) != std::string::npos) {
        suggestions_.clear();
        query_.clear();
        return;
    }
    const std::string q = lower(draft_.substr(b + 1));
    if (q == query_ && b == bracket_) return;
    query_ = q;
    bracket_ = b;
    pick_ = 0;
    suggestions_.clear();
    if (q.empty()) return;
    // A name's start, or a word's in it, first; anywhere in it after.
    const auto rank = [&](const std::string& label) {
        const std::string l = lower(label);
        const size_t at = l.find(q);
        if (at == std::string::npos) return -1;
        return at == 0 ? 0 : l[at - 1] == ' ' ? 1 : 2;
    };
    struct Found {
        int rank;
        Suggestion one;
    };
    std::vector<Found> carried, known;
    std::vector<std::string> seen;
    const auto carry = [&](const sim::Held& held, const char* where) {
        if (held.empty() || size_t(held.item) >= tables_->items.size()) return;
        const content::ItemRow& row = tables_->items[size_t(held.item)];
        const std::string label = sim::linkLabel(row, held);
        const int r = rank(label);
        if (r < 0) return;
        carried.push_back({r, {label, held, where}});
        seen.push_back(lower(row.label));
    };
    for (int slot = 0; slot < sim::kSlots; ++slot) carry(realm.satchel()[slot], slot < sim::kWorn ? "worn" : "bag");
    for (int cell = 0; cell < sim::kVaultCells; ++cell) carry(realm.vault()[cell], "vault");
    for (size_t i = 0; i < tables_->items.size(); ++i) {
        const content::ItemRow& row = tables_->items[i];
        if (row.label.empty()) continue;
        const int r = rank(row.label);
        if (r < 0) continue;
        const std::string l = lower(row.label);
        if (std::find(seen.begin(), seen.end(), l) != seen.end()) continue;
        seen.push_back(l);
        sim::Held held;
        held.item = int32_t(i);
        held.durability = int16_t(std::max(1, row.durability));
        known.push_back({r, {row.label, held, ""}});
    }
    const auto byRank = [](const Found& a, const Found& b) { return a.rank < b.rank; };
    std::stable_sort(carried.begin(), carried.end(), byRank);
    std::stable_sort(known.begin(), known.end(), byRank);
    for (const auto* list : {&carried, &known}) {
        for (const Found& f : *list) {
            if (suggestions_.size() < kSuggestMost) suggestions_.push_back(f.one);
        }
    }
}

void Chat::take(size_t index) {
    if (index >= suggestions_.size() || !tables_) return;
    const Suggestion& one = suggestions_[index];
    const std::string next =
        draft_.substr(0, bracket_) + sim::itemLink(tables_->items[size_t(one.held.item)], one.held) + " ";
    suggestions_.clear();
    query_.clear();
    if (next.size() > net::kMostSaid) {
        system(net::Channel::Server, "No room for another item in this line.");
        return;
    }
    draft_ = next;
}

std::string Chat::named(const std::string& text, const sim::Realm& realm) const {
    if (!tables_) return text;
    std::string out;
    size_t at = 0;
    while (true) {
        const size_t open = text.find('[', at);
        const size_t close = open == std::string::npos ? open : text.find(']', open);
        if (close == std::string::npos) break;
        const std::string want = lower(text.substr(open + 1, close - open - 1));
        std::string token;
        for (int slot = 0; slot < sim::kSlots && token.empty(); ++slot) {
            const sim::Held& held = realm.satchel()[slot];
            if (held.empty() || size_t(held.item) >= tables_->items.size()) continue;
            const content::ItemRow& row = tables_->items[size_t(held.item)];
            if (lower(sim::linkLabel(row, held)) == want || lower(row.label) == want) token = sim::itemLink(row, held);
        }
        for (size_t i = 0; i < tables_->items.size() && token.empty(); ++i) {
            const content::ItemRow& row = tables_->items[i];
            if (row.label.empty() || lower(row.label) != want) continue;
            sim::Held held;
            held.item = int32_t(i);
            held.durability = int16_t(std::max(1, row.durability));
            token = sim::itemLink(row, held);
        }
        out += text.substr(at, open - at);
        out += token.empty() ? text.substr(open, close - open + 1) : token;
        at = close + 1;
    }
    out += text.substr(at);
    return out.size() > net::kMostSaid ? text : out;
}

bool Chat::shows(const Line& line, const std::string& tab) const {
    const uint32_t kind = line.kind == Kind::Loot     ? kLootBit
                          : line.kind == Kind::Answer ? kAnswerBit
                                                      : bit(line.channel);
    if (tab == "whispers") return line.channel == net::Channel::Whisper;
    return ((tab == "global" ? globalShows_ : generalShows_) & kind) != 0;
}

void Chat::select(const std::string& key) {
    tab_ = key;
    unread_[key] = 0;
    logScroll_ = 0.0f;
    unseen_ = 0;
    menu_ = Menu::None;
}

void Chat::whisperTo(const std::string& name) {
    if (name.empty()) return;
    stick(net::Channel::Whisper, name);
    open_ = true;
}

void Chat::switchBySlash() {
    // "/p " and its kin: the channel switched as he types, the slash eaten (WoW's).
    if (draft_.empty() || draft_[0] != '/' || draft_.back() != ' ') return;
    const std::string typed = draft_.substr(1, draft_.size() - 2);
    const size_t space = typed.find(' ');
    const std::string command = lower(typed.substr(0, space));
    const std::string rest = space == std::string::npos ? std::string() : typed.substr(space + 1);
    if (rest.empty()) {
        const auto to = [&](net::Channel channel) {
            stick(channel);
            draft_.clear();
        };
        if (command == "s" || command == "say") to(net::Channel::Local);
        else if (command == "y" || command == "yell" || command == "m" || command == "map") to(net::Channel::Map);
        else if ((command == "p" || command == "party") && inParty_) to(net::Channel::Party);
        else if ((command == "g" || command == "guild") && inGuild_) to(net::Channel::Guild);
        else if (command == "1" || command == "global") to(net::Channel::Global);
        else if ((command == "r" || command == "reply") && !lastFrom_.empty()) {
            stick(net::Channel::Whisper, lastFrom_);
            draft_.clear();
        }
        return;
    }
    if ((command == "w" || command == "whisper" || command == "t" || command == "tell") &&
        rest.find(' ') == std::string::npos) {
        stick(net::Channel::Whisper, rest);
        draft_.clear();
    }
}

void Chat::send(Play& play) {
    // The edit box's channel, unless the line's slash says another; either way the box keeps the
    // channel it was said on (WoW's sticky channels).
    net::Channel channel = sticky_.channel;
    std::string to = sticky_.to;
    std::string text = net::saidText(draft_);
    draft_.clear();
    recalled_ = -1;
    open_ = false;
    if (text.empty()) return;
    // MU's guild prefixes (GS protocol.cpp:2575-2700): "@>" the master's notice, "@" his guild.
    if (text.rfind("@>", 0) == 0) {
        net::GuildAsk asked;
        asked.what = net::GuildWhat::Notice;
        asked.text = net::saidText(text.substr(2));
        if (play.remote()) play.askGuild(asked);
        return;
    }
    if (text[0] == '@') {
        text = net::saidText(text.substr(1));
        if (text.empty()) return;
        channel = net::Channel::Guild;
    } else if (text[0] == '/') {
        const size_t space = text.find(' ');
        const std::string command = lower(text.substr(1, space == std::string::npos ? std::string::npos : space - 1));
        std::string rest = space == std::string::npos ? std::string() : net::saidText(text.substr(space + 1));
        if (command == "p" || command == "party" || command == "g" || command == "guild" || command == "m" ||
            command == "map" || command == "y" || command == "yell" || command == "s" || command == "say" ||
            command == "1" || command == "global") {
            channel = command == "p" || command == "party"    ? net::Channel::Party
                      : command == "g" || command == "guild"  ? net::Channel::Guild
                      : command == "1" || command == "global" ? net::Channel::Global
                      : command == "s" || command == "say"    ? net::Channel::Local
                                                              : net::Channel::Map;
            to.clear();
            if (rest.empty()) {
                stick(channel);
                open_ = true;
                return;
            }
        } else if ((command == "leave" || command == "join") && (lower(rest) == "global" || rest == "1")) {
            // WoW's /leave and /join of a channel: Global's lines stop coming, or come again.
            leftGlobal_ = command == "leave";
            save();
            system(net::Channel::Server, leftGlobal_ ? "You left Global. /join global to hear it again."
                                                     : "You joined Global.");
            if (leftGlobal_ && sticky_.channel == net::Channel::Global) stick(net::Channel::Local);
            return;
        } else if (command == "ignore" || command == "unignore") {
            // His lines, whispers and invites dropped as they come (hear, askedToTrade, guildCalled).
            const std::string name = rest.substr(0, rest.find(' '));
            if (name.empty()) {
                if (command == "unignore") {
                    system(net::Channel::Server, "/unignore whom? Add a name.");
                } else if (ignored_.empty()) {
                    system(net::Channel::Server, "You ignore no one. /ignore and a name.");
                } else {
                    std::string list;
                    for (const std::string& one : ignored_) list += (list.empty() ? "" : ", ") + one;
                    system(net::Channel::Server, "You ignore " + list + ".");
                }
                return;
            }
            const auto it = std::find(ignored_.begin(), ignored_.end(), lower(name));
            if (command == "ignore") {
                if (lower(name) == lower(me_)) {
                    system(net::Channel::Server, "You cannot ignore yourself.");
                    return;
                }
                if (it == ignored_.end()) ignored_.push_back(lower(name));
                system(net::Channel::Server, name + " is ignored: their lines, whispers and invites no longer reach you.");
            } else {
                if (it != ignored_.end()) ignored_.erase(it);
                system(net::Channel::Server, name + " is no longer ignored.");
            }
            save();
            return;
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
                stick(net::Channel::Whisper, name);
                open_ = true;
                return;
            }
        } else if (command == "r" || command == "reply") {
            if (lastFrom_.empty()) {
                system(net::Channel::Server, "No one has whispered you.");
                return;
            }
            channel = net::Channel::Whisper;
            to = lastFrom_;
        } else if (command == "trade") {
            // A trade asked by name (sim/realm_deal.cpp): he must be on this map and near.
            const std::string name = rest.substr(0, rest.find(' '));
            if (name.empty()) {
                system(net::Channel::Server, "/trade whom? Add a name.");
                return;
            }
            const uint32_t whom = play.playerNamed(name);
            if (whom == 0) {
                system(net::Channel::Server, name + " is not here.");
                return;
            }
            play.dealAsk(whom);
            return;
        } else if (command == "ginvite" || command == "gjoin" || command == "gleave" || command == "gnotice") {
            // The guild's asks (docs/game/guild.md): the server answers on the Guild channel.
            if (!play.remote()) {
                system(net::Channel::Server, "Guilds are kept on the server: play online to join one.");
                return;
            }
            net::GuildAsk asked;
            asked.what = command == "ginvite" ? net::GuildWhat::Invite
                         : command == "gjoin" ? net::GuildWhat::Ask
                         : command == "gleave" ? net::GuildWhat::Leave
                                               : net::GuildWhat::Notice;
            asked.name = rest.substr(0, rest.find(' '));
            asked.text = rest;
            if ((asked.what == net::GuildWhat::Invite || asked.what == net::GuildWhat::Ask) && asked.name.empty()) {
                system(net::Channel::Guild, "/" + command + " whom? Add a name.");
                return;
            }
            play.askGuild(asked);
            return;
        } else if (command == "raid") {
            // The raid (docs/game/party.md): "/raid" makes his party one, "/raid off" a party again.
            if (!play.remote()) {
                system(net::Channel::Server, "No one is here to raid with: this world is not on a server.");
                return;
            }
            const bool off = rest.rfind("off", 0) == 0;
            play.askParty({off ? net::PartyWhat::Unraid : net::PartyWhat::Raid, std::string()});
            return;
        } else if (command == "invite" || command == "leave" || command == "kick" || command == "accept" ||
                   command == "decline" || command == "promote") {
            // The party's asks (docs/game/party.md): the server answers on the Party channel.
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
    // Kept for Up, and the box stays on where it was said.
    sent_.push_back({{channel, to}, text});
    if (sent_.size() > kSentKept) sent_.erase(sent_.begin());
    stick(channel, to);
    if (channel == net::Channel::Global && leftGlobal_) {
        system(net::Channel::Server, "You left Global. /join global to speak there.");
        return;
    }
    if (channel == net::Channel::Party && !inParty_) {
        system(net::Channel::Server, "You are in no party.");
        return;
    }
    if (!play.remote()) {
        system(net::Channel::Server, "No one hears you here: this world is not on a server.");
        return;
    }
    text = named(text, play.realm());
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
    if (index >= 0) log_[size_t(index)].answer = accept ? 1 : -1;
    // A trade's ask is the realm's to answer, not the server's party.
    if (index >= 0 && log_[size_t(index)].trader != 0) {
        play.dealAnswer(log_[size_t(index)].trader, accept);
        return;
    }
    // A guild's call is the server's guild to answer (docs/game/guild.md).
    if (index >= 0 && log_[size_t(index)].guild) {
        net::GuildAsk asked;
        asked.what = accept ? net::GuildWhat::Accept : net::GuildWhat::Decline;
        asked.name = log_[size_t(index)].inviter;
        play.askGuild(asked);
        return;
    }
    net::PartyAsk asked;
    asked.what = accept ? net::PartyWhat::Accept : net::PartyWhat::Decline;
    asked.name = index >= 0 ? log_[size_t(index)].inviter : name;
    play.askParty(asked);
}

int Chat::answerOpen(Play& play, bool trades, bool accept) {
    int answered = 0;
    for (size_t i = 0; i < log_.size(); ++i) {
        const Line& line = log_[i];
        if (line.inviter.empty() || line.answer != 0 || line.guild || (line.trader != 0) != trades) continue;
        if (line.age >= (trades ? kTradeLife : kInviteLife)) continue;
        core::logf("chat: %s %s's %s for the Helper", accept ? "accepted" : "declined", line.inviter.c_str(),
                   trades ? "trade" : "invite");
        answer(play, long(i), line.inviter, accept);
        ++answered;
    }
    return answered;
}

void Chat::sample() { sampleWanted_ = true; }

void Chat::place(float u, float width, float height) {
    if (wide_ <= 0.0f) {
        left_ = kLeftFirst;
        foot_ = kFootFirst;
        wide_ = kWideFirst;
        tall_ = kTallFirst;
    }
    // Never past the screen: as small as it may be, and its whole frame on it.
    const float screenW = width / u, screenH = height / u;
    wide_ = std::clamp(wide_, kWideLeast, std::max(kWideLeast, std::min(kWideMost, screenW - 16.0f)));
    tall_ = std::clamp(tall_, kTallLeast, std::max(kTallLeast, std::min(kTallMost, screenH - 32.0f)));
    left_ = std::clamp(left_, 0.0f, std::max(0.0f, screenW - wide_));
    foot_ = std::clamp(foot_, 0.0f, std::max(0.0f, screenH - tall_));
    frame_ = {std::round(left_ * u), std::round(height - (foot_ + tall_) * u), std::round(wide_ * u),
              std::round(tall_ * u)};
    row_ = {frame_.x, frame_.y, frame_.w, std::round(kTabRow * u)};
    panel_ = {frame_.x, row_.bottom(), frame_.w, frame_.bottom() - row_.bottom()};
    input_ = {panel_.x + kPadSide * u, panel_.bottom() - (kPadFoot + kInput) * u, panel_.w - 2.0f * kPadSide * u,
              kInput * u};
    const float logTop = panel_.y + kPadTop * u;
    logBox_ = {panel_.x + kPadSide * u, logTop, panel_.w - 2.0f * kPadSide * u,
               std::max(rowTall() * u, input_.y - kInputGap * u - logTop)};
    grip_ = {panel_.right() - 18.0f * u, panel_.y + 2.0f * u, 16.0f * u, 16.0f * u};
    gear_ = {row_.right() - 30.0f * u, row_.y, 30.0f * u, row_.h};
    chip_ = {input_.x, input_.y, wordWidth(kWord * u, channelWord()) + 28.0f * u, input_.h};
}

void Chat::layTabs(float u) {
    tabs_.clear();
    const auto add = [&](const std::string& key, const std::string& name) { tabs_.push_back({key, name, {}}); };
    add("general", "General");
    if (!leftGlobal_) add("global", "Global");
    if (whisperTab_) add("whispers", "Whispers");
    if (tab_ == "whispers" && !whisperTab_) tab_ = "general";
    if (tab_ == "global" && leftGlobal_) tab_ = "general";
    float x = row_.x;
    for (Tab& tab : tabs_) {
        const bool chosen = tab.key == tab_;
        const float pad = (chosen ? 14.0f : 12.0f) * u;
        float wide = pad * 2.0f + wordWidth(kWord * u, tab.word);
        const auto it = unread_.find(tab.key);
        if (it != unread_.end() && it->second > 0 && !chosen) {
            wide += kPillGap * u + pillWidth(u, std::to_string(std::min(it->second, 99)));
        }
        tab.box = {x, row_.y, std::round(wide), row_.h};
        x += tab.box.w;
    }
}

void Chat::layRows(float u) {
    // Laid again only when what it is laid from moves: the log, the tab, the width, the size.
    uint64_t key = 1469598103934665603ull;
    const auto mixIn = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
    mixIn(log_.size());
    mixIn(log_.empty() ? 0 : std::hash<std::string>{}(log_.back().shown));
    mixIn(log_.empty() ? 0 : std::hash<std::string>{}(log_.front().shown));
    mixIn(std::hash<std::string>{}(tab_)), mixIn(std::hash<std::string>{}(me_));
    mixIn(uint64_t(std::lround(logBox_.w))), mixIn(uint64_t(size_)), mixIn(stamps_);
    mixIn(generalShows_), mixIn(globalShows_), mixIn(uint64_t(tables_ != nullptr));
    if (key == laid_) return;
    laid_ = key;
    rows_.clear();
    sets_.assign(log_.size(), Set{});
    const float size = textSize() * u;
    const float wide = logBox_.w - 8.0f * u;
    for (size_t i = 0; i < log_.size(); ++i) {
        if (!shows(log_[i], tab_)) continue;
        sets_[i] = setOf(log_[i]);
        const std::string& text = sets_[i].text;
        // Greedy, by words; a word wider than a whole row is broken by letters.
        Row row;
        row.line = i;
        float room = wide;
        size_t at = 0;
        while (true) {
            const std::string rest = text.substr(at);
            if (rest.empty() || bodyWidth(size, rest) <= room) {
                row.start = at;
                row.end = text.size();
                rows_.push_back(row);
                break;
            }
            size_t cut = 0, from = 0;
            while (from < rest.size()) {
                size_t next = rest.find(' ', from);
                if (next == std::string::npos) next = rest.size();
                if (bodyWidth(size, rest.substr(0, next)) > room) break;
                cut = next;
                from = next + 1;
            }
            if (cut == 0) {  // one long word: as many letters as fit
                cut = 1;
                while (cut < rest.size() && bodyWidth(size, rest.substr(0, cut + 1)) <= room) ++cut;
                while (cut < rest.size() && (uint8_t(rest[cut]) & 0xC0) == 0x80) ++cut;
            }
            row.start = at;
            row.end = at + cut;
            rows_.push_back(row);
            at += std::min(rest.size(), cut + (cut < rest.size() && rest[cut] == ' ' ? 1 : 0));
            row.first = false;
            room = wide - kIndent * u;
        }
    }
}

void Chat::layLog(float u) {
    const float before = logTall_;
    layRows(u);
    logTall_ = float(rows_.size()) * rowTall() * u;
    // Reading back: held where he left it as lines come in, and the new ones counted.
    if (logScroll_ > 0.0f && logTall_ > before) {
        logScroll_ += logTall_ - before;
        unseen_ += int(std::lround((logTall_ - before) / (rowTall() * u)));
    }
    logScroll_ = std::clamp(logScroll_, 0.0f, std::max(0.0f, logTall_ - logBox_.h));
    if (logScroll_ <= 0.0f) unseen_ = 0;
}

uint64_t Chat::signature(float u, const Pointer& pointer) const {
    uint64_t h = 1469598103934665603ull;
    const auto mixIn = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    const auto mixf = [&](float f) { mixIn(uint64_t(int64_t(std::lround(f * 4.0f)))); };
    const auto mixs = [&](const std::string& s) {
        for (const char c : s) mixIn(uint8_t(c));
        mixIn(0xFF);
    };
    mixIn(open_), mixf(u), mixIn(uint64_t(std::lround(lit_ * 40.0f)));
    mixf(frame_.x), mixf(frame_.y), mixf(frame_.w), mixf(frame_.h);
    mixIn(laid_), mixf(logScroll_), mixIn(uint64_t(unseen_));
    for (const Line& line : log_) mixIn(uint64_t(line.answer + 1));
    for (const auto& [key, count] : unread_) mixs(key), mixIn(uint64_t(count));
    mixs(tab_), mixs(me_), mixs(sticky_.to), mixIn(uint8_t(sticky_.channel));
    mixIn(fill_), mixIn(idle_), mixIn(size_), mixIn(locked_), mixIn(settingsOpen_), mixIn(uint8_t(menu_));
    mixIn(uint8_t(drag_)), mixIn(inParty_), mixIn(inGuild_);
    if (open_) {
        mixs(draft_);
        mixIn(std::fmod(clock_, 1.0f) < 0.6f);
    }
    // The pointer only where it lights something: on the frame, or dragging.
    if (frame_.has(pointer.x, pointer.y) || drag_ != Drag::None) mixf(pointer.x), mixf(pointer.y);
    // Idle, the newest lines settling down from full.
    if (lit_ < 1.0f) {
        for (size_t i = log_.size(); i-- > 0;) {
            const float age = log_[i].age;
            if (age >= kNewFor + kSettle) break;
            mixIn(age < kNewFor ? 0 : uint64_t((age - kNewFor) * 16.0f) + 1);
        }
    }
    return h;
}

void Chat::update(float seconds, float width, float height, float bottom, float right,
                  const Pointer& pointer, float wheel, const std::string& typed, int backspaces,
                  bool enter, bool escape, bool toggle, Play& play, bool tab, int arrows) {
    (void)right;
    if (!loaded_) load();
    clock_ += seconds;
    for (Line& line : log_) line.age += seconds;
    const float u = tip::unit();
    tables_ = play.isOpen() ? play.realm().tables() : nullptr;
    showing_ = play.isOpen();
    if (play.isOpen()) {
        std::string name;
        if (play.playerName(play.realm().hero().id, &name, nullptr)) me_ = name;
        std::vector<net::Heard> heard;
        play.takeHeard(heard);
        for (const net::Heard& line : heard) {
            core::logf("chat: %s%s%s: %s", line.name.empty() ? "(server) " : "", line.name.c_str(),
                       line.to.empty() ? "" : (" to " + line.to).c_str(), line.text.c_str());
            if (!hear(line)) continue;
            // A party's or raid's invite to him: its card's sound (window_invite).
            if (!log_.empty() && log_.back().channel == net::Channel::Party && !log_.back().inviter.empty()) {
                play.ui(Play::Ui::Invited);
            }
            // A Local, Map, Party or Guild line said by someone in view: over his head too, links and
            // all, unless the settings keep the lines in the chat (game/ui/speech.cpp). Global's
            // never: it is the server's, not the street's.
            if (bubbles_ &&
                (line.channel == net::Channel::Local || line.channel == net::Channel::Map ||
                 line.channel == net::Channel::Party || line.channel == net::Channel::Guild) &&
                line.from != 0) {
                play.bubble(line.from, line.text,
                            line.channel == net::Channel::Party   ? Play::kSaidParty
                            : line.channel == net::Channel::Guild ? Play::kSaidGuild
                                                                  : 0);
            }
        }
        // What he and his party picked up off the ground in view (Play::takeLooted).
        const uint32_t heroId = play.realm().hero().id;
        for (const Play::Looted& one : play.takeLooted()) {
            if (!tables_ || one.what.item < 0 || size_t(one.what.item) >= tables_->items.size()) continue;
            std::string who;
            if (one.who != heroId) {
                for (const auto& member : play.party().members) {
                    if (member.here == one.who) who = member.name;
                }
                if (who.empty()) continue;  // a stranger's pickup is his own business
            }
            Line line;
            line.channel = net::Channel::Server;
            line.kind = Kind::Loot;
            line.stamp = clockNow();
            const std::string token = sim::itemLink(tables_->items[size_t(one.what.item)], one.what);
            line.text = (who.empty() ? std::string("You loot ") : who + " loots ") + token + ".";
            read(line);
            core::logf("chat: %s", line.shown.c_str());
            log_.push_back(line);
            if (log_.size() > kKept) log_.erase(log_.begin(), log_.begin() + long(log_.size() - kKept));
            if (tab_ != "general" && shows(log_.back(), "general")) ++unread_["general"];
        }
        // A review run's lines, once the tables can make its links (--chat-sample).
        if (sampleWanted_ && tables_) {
            sampleWanted_ = false;
            if (me_.empty()) me_ = "Arin";
            const auto token = [&](const char* label) {
                for (const content::ItemRow& row : tables_->items) {
                    if (row.label != label) continue;
                    sim::Held held;
                    held.item = int32_t(&row - tables_->items.data());
                    held.durability = int16_t(std::max(1, row.durability));
                    return sim::itemLink(row, held);
                }
                return "[" + std::string(label) + "]";
            };
            const auto said = [&](net::Channel channel, const std::string& name, const std::string& to,
                                  const std::string& text) {
                net::Heard line;
                line.channel = channel;
                line.name = name;
                line.to = to;
                line.text = text;
                hear(line);
            };
            said(net::Channel::Server, "", "", "Golden Invasion in Lorencia in 5 minutes.");
            said(net::Channel::Local, "Sigrun", "", "WTS " + token("Jewel of Bless") + " x5, listed at the Broker");
            said(net::Channel::Local, "Thane", "", "how do you make the Blood Castle cloak?");
            said(net::Channel::Party, "Elva", "", "Archangel scroll + Blood Bone + a Chaos in the machine");
            said(net::Channel::Guild, "Orla", "", "29 and 90%, one more skeleton lol");
            said(net::Channel::Global, "Rurik", "", "LF2M Devil Square 2, need an elf");
            said(net::Channel::Whisper, "Ilse", me_, "did you get the " + token("Wings of Heaven") + "?");
            said(net::Channel::Whisper, me_, "Ilse", "not yet, 3 Chaos short");
            for (const char* thing : {"Jewel of Bless", "Wings of Heaven"}) {
                Line line;
                line.channel = net::Channel::Server;
                line.kind = Kind::Loot;
                line.stamp = clockNow();
                line.text = (std::string(thing) == "Jewel of Bless" ? std::string("You loot ") : std::string("Brann loots ")) +
                            token(thing) + ".";
                read(line);
                log_.push_back(line);
            }
            said(net::Channel::Party, "", "", "Brann accepted your party invite.");
            said(net::Channel::Party, "Brann", "", "gate in 1 min, bring pots");
            said(net::Channel::Whisper, "Ilse", me_, "ok, ping me when you have them");
            lastFrom_ = "Ilse";
        }
    }
    for (Line& line : log_) resolve(line);

    // A party joined sets the box to Party once; one left, or a guild, puts Say back.
    inParty_ = play.party().members.size() > 1;
    inGuild_ = play.guild().id != 0;
    if (inParty_ && !wasInParty_) stick(net::Channel::Party);
    if (!inParty_ && sticky_.channel == net::Channel::Party) stick(net::Channel::Local);
    if (!inGuild_ && sticky_.channel == net::Channel::Guild) stick(net::Channel::Local);
    wasInParty_ = inParty_;

    // The keyboard: Enter opens the box, says the line and shuts it; Escape shuts it, the draft kept.
    if (toggle) open_ = !open_;
    if (open_) {
        if (escape) {
            open_ = false;
            menu_ = Menu::None;
        } else if (enter && !suggestions_.empty()) {
            take(size_t(pick_));  // Enter takes the suggestion; the next one says the line
        } else if (enter) {
            send(play);
        } else {
            for (int i = 0; i < backspaces && !draft_.empty(); ++i) {
                // A link goes whole.
                const std::vector<sim::ItemLink> links = draft_.back() == '}' ? sim::findLinks(draft_)
                                                                               : std::vector<sim::ItemLink>{};
                if (!links.empty() && links.back().to == draft_.size()) draft_.resize(links.back().from);
                else draft_.resize(draft_.size() - lastLetter(draft_));
            }
            for (const char c : typed) {
                if (uint8_t(c) < 0x20 || uint8_t(c) == 0x7F) continue;
                if (draft_.size() < net::kMostSaid) draft_.push_back(c);
            }
            if (!typed.empty()) recalled_ = -1;
            switchBySlash();
        }
    } else if (enter) {
        open_ = true;
    }
    if (play.isOpen()) suggest(play.realm());
    if (open_ && !suggestions_.empty()) {
        const int n = int(suggestions_.size());
        pick_ = ((pick_ + arrows) % n + n) % n;
        if (tab) take(size_t(pick_));
    } else if (open_) {
        // Tab: the next of the people he has whispered with. Up and Down: what he said before.
        if (tab && !whispers_.empty()) {
            const auto here = std::find(whispers_.begin(), whispers_.end(), sticky_.to);
            const size_t at = sticky_.channel != net::Channel::Whisper || here == whispers_.end()
                                  ? whispers_.size() - 1
                                  : (size_t(here - whispers_.begin()) + whispers_.size() - 1) % whispers_.size();
            sticky_ = {net::Channel::Whisper, whispers_[at]};
        }
        if (arrows != 0 && !sent_.empty()) {
            recalled_ = std::clamp(recalled_ - arrows, -1, int(sent_.size()) - 1);
            if (recalled_ < 0) {
                draft_.clear();
            } else {
                const Sent& one = sent_[sent_.size() - 1 - size_t(recalled_)];
                draft_ = one.text;
                sticky_ = one.where;
            }
        }
    }

    place(u, width, height);
    layTabs(u);
    layLog(u);
    if (settingsOpen_) {
        const float x = std::min(panel_.right() + kSettingsGap * u, width - kSettingsWide * u - 8.0f * u);
        settingsBox_ = {x, panel_.y, kSettingsWide * u, (40.0f + kSettingsRow * 7.0f + 14.0f) * u};
    }

    // Lit: the pointer on it, the box open, a drag, a menu or the settings.
    const bool over = covers(pointer.x, pointer.y);
    const bool wantLit = open_ || over || drag_ != Drag::None || menu_ != Menu::None || settingsOpen_;
    lit_ = std::clamp(lit_ + (wantLit ? 1.0f : -1.0f) * seconds / kLight, 0.0f, 1.0f);

    // The pointer. A press is taken by the first thing that wants it, top first.
    bool taken = false;
    const auto click = [&]() {
        play.ui(Play::Ui::Click);
        taken = true;
    };
    // A menu of its own: a row acts; a press anywhere else shuts it.
    if (menu_ != Menu::None && (pointer.pressed || pointer.rightPressed)) {
        if (menuBox_.has(pointer.x, pointer.y)) {
            for (const MenuRow& row : menuRows_) {
                if (!row.box.has(pointer.x, pointer.y)) continue;
                if (menu_ == Menu::Channels) {
                    stick(net::Channel(row.what));
                    menu_ = Menu::None;
                    open_ = true;
                } else {
                    filter(menuTab_) ^= uint32_t(row.what);
                    laid_ = 0;
                    save();
                }
                break;
            }
            click();
        } else {
            menu_ = Menu::None;
        }
    }
    // The suggestions over the input.
    for (size_t k = 0; k < suggestBoxes_.size() && k < suggestions_.size() && !taken; ++k) {
        if (!suggestBoxes_[k].has(pointer.x, pointer.y)) continue;
        if (pointer.x != pointerX_ || pointer.y != pointerY_) pick_ = int(k);
        if (pointer.pressed) {
            take(k);
            taken = true;
        }
        break;
    }
    pointerX_ = pointer.x;
    pointerY_ = pointer.y;
    // The settings, docked.
    if (settingsOpen_ && pointer.pressed && !taken && settingsBox_.has(pointer.x, pointer.y)) {
        const auto half = [&](const Box& box) { return pointer.x >= box.midX(); };
        if (closeBox_.has(pointer.x, pointer.y)) {
            settingsOpen_ = false;
        } else if (resetBox_.has(pointer.x, pointer.y)) {
            wide_ = 0.0f;
        } else if (sizeBox_.has(pointer.x, pointer.y)) {
            size_ = std::clamp(int((pointer.x - sizeBox_.x) / (sizeBox_.w / 3.0f)), 0, 2);
            laid_ = 0;
        } else if (stampsBox_.has(pointer.x, pointer.y)) {
            stamps_ = half(stampsBox_);
            laid_ = 0;
        } else if (bubblesBox_.has(pointer.x, pointer.y)) {
            bubbles_ = half(bubblesBox_);
        } else if (whispersBox_.has(pointer.x, pointer.y)) {
            whisperTab_ = !half(whispersBox_);
        } else if (lockBox_.has(pointer.x, pointer.y)) {
            locked_ = half(lockBox_);
        } else if (fillBox_.has(pointer.x, pointer.y)) {
            drag_ = Drag::Fill;
        } else if (idleBox_.has(pointer.x, pointer.y)) {
            drag_ = Drag::Idle;
        }
        save();
        click();
    }
    // The invites' Accept and Decline, open or shut.
    if (pointer.pressed && !taken) {
        for (const Choice& choice : choices_) {
            if (!choice.box.has(pointer.x, pointer.y) || choice.line >= log_.size() ||
                log_[choice.line].inviter.empty()) {
                continue;
            }
            answer(play, long(choice.line), {}, choice.accept);
            click();
            break;
        }
    }
    if (showing_ && pointer.pressed && !taken && frame_.has(pointer.x, pointer.y)) {
        if (gear_.has(pointer.x, pointer.y)) {
            settingsOpen_ = !settingsOpen_;
            menu_ = Menu::None;
            click();
        } else if (!locked_ && grip_.has(pointer.x, pointer.y)) {
            if (clock_ - gripClicked_ < 0.4f) {
                // A double click: the first size back, the foot where it stands.
                wide_ = kWideFirst;
                tall_ = kTallFirst;
                save();
            } else {
                drag_ = Drag::Grip;
                dragX_ = pointer.x, dragY_ = pointer.y, startC_ = wide_, startD_ = tall_;
            }
            gripClicked_ = clock_;
            taken = true;
        } else if (row_.has(pointer.x, pointer.y)) {
            for (const Tab& one : tabs_) {
                if (!one.box.has(pointer.x, pointer.y)) continue;
                pressedTab_ = one.key;
                moved_ = false;
                if (!locked_) {
                    drag_ = Drag::Move;
                    dragX_ = pointer.x, dragY_ = pointer.y, startA_ = left_, startB_ = foot_;
                }
            }
            taken = true;
        } else if (open_ && chip_.has(pointer.x, pointer.y)) {
            menu_ = menu_ == Menu::Channels ? Menu::None : Menu::Channels;
            click();
        } else if (unseen_ > 0 && newer_.has(pointer.x, pointer.y)) {
            logScroll_ = 0.0f;
            unseen_ = 0;
            click();
        } else if (logTall_ > logBox_.h && pointer.x >= logBox_.right() - 8.0f * u && logBox_.has(pointer.x, pointer.y)) {
            drag_ = Drag::LogSlider;
            dragY_ = pointer.y, startA_ = logScroll_;
            taken = true;
        } else {
            for (const NameBox& name : nameBoxes_) {
                if (!name.box.has(pointer.x, pointer.y) || name.who.empty()) continue;
                if (name.who != me_) whisperTo(name.who);
                click();
                break;
            }
            taken = true;
        }
    }
    if (showing_ && pointer.rightPressed && frame_.has(pointer.x, pointer.y) && menu_ == Menu::None) {
        bool done = false;
        for (const Tab& one : tabs_) {
            if (!one.box.has(pointer.x, pointer.y) || one.key == "whispers") continue;
            menu_ = Menu::Shows;
            menuTab_ = one.key;
            menuBox_ = {one.box.x, 0.0f, 230.0f * u, 0.0f};  // laid in buildTip
            done = true;
        }
        for (const NameBox& name : nameBoxes_) {
            if (done || !name.box.has(pointer.x, pointer.y) || name.who.empty() || name.who == me_) continue;
            menuAsk_ = name.who;
            menuAskX_ = pointer.x, menuAskY_ = pointer.y;
            done = true;
        }
    }
    // The drags.
    if (drag_ != Drag::None && pointer.held) {
        if (drag_ == Drag::Grip) {
            wide_ = startC_ + (pointer.x - dragX_) / u;
            tall_ = startD_ - (pointer.y - dragY_) / u;  // up from its foot
        } else if (drag_ == Drag::Move) {
            if (std::abs(pointer.x - dragX_) + std::abs(pointer.y - dragY_) > 5.0f * u) moved_ = true;
            if (moved_) {
                left_ = startA_ + (pointer.x - dragX_) / u;
                foot_ = startB_ - (pointer.y - dragY_) / u;
            }
        } else if (drag_ == Drag::LogSlider) {
            const float thumb = std::max(24.0f * u, logBox_.h * logBox_.h / std::max(1.0f, logTall_));
            const float travel = std::max(1.0f, logBox_.h - thumb);
            logScroll_ = startA_ - (pointer.y - dragY_) * (logTall_ - logBox_.h) / travel;
        } else if (drag_ == Drag::Fill || drag_ == Drag::Idle) {
            const Box& box = drag_ == Drag::Fill ? fillBox_ : idleBox_;
            const float track = box.w - 44.0f * u - 12.0f * u;
            const int value = std::clamp(int(std::lround((pointer.x - box.x) / std::max(1.0f, track) * 100.0f)), 0, 100);
            (drag_ == Drag::Fill ? fill_ : idle_) = value;
        }
    }
    if (!pointer.held || pointer.released) {
        if (drag_ == Drag::Grip || (drag_ == Drag::Move && moved_) || drag_ == Drag::Fill || drag_ == Drag::Idle) save();
        if (!pressedTab_.empty() && !moved_) {
            select(pressedTab_);
            play.ui(Play::Ui::Click);
        }
        pressedTab_.clear();
        drag_ = Drag::None;
    }
    if (wheel != 0.0f && logBox_.has(pointer.x, pointer.y)) logScroll_ += wheel * 1.5f * rowTall() * u;
    place(u, width, height);
    layTabs(u);
    layLog(u);

    const long pending = pendingInvite();
    inviting_ = pending >= 0;
    // A guild's card stands taller, for its line of what joining means (docs/game/guild.md).
    const bool guildCard = inviting_ && log_[size_t(pending)].guild;
    const float inviteTall = kInviteTall + (guildCard ? kGuildCardMore : 0.0f);
    inviteCard_ = {width * 0.5f - kInviteWide * 0.5f * u, bottom - (kInviteAbove + inviteTall) * u,
                   kInviteWide * u, inviteTall * u};
    const uint64_t key = signature(u, pointer) ^ (uint64_t(pending + 1) * 0x9E3779B97F4A7C15ull) ^
                         (inviting_ && inviteCard_.has(pointer.x, pointer.y)
                              ? uint64_t(std::lround(pointer.x)) * 31 + uint64_t(std::lround(pointer.y))
                              : 0) ^
                         (inviting_ ? uint64_t(std::min(log_[size_t(pending)].age, kInviteIn) * 100.0f) +
                                          (uint64_t(std::max(0.0f, log_[size_t(pending)].age -
                                                                       (kInviteLife - kInviteOut)) * 30.0f) << 8)
                                    : 0) ^
                         // A guild card's bar runs down its minute: redrawn each second.
                         (guildCard ? uint64_t(log_[size_t(pending)].age) << 20 : 0);
    if (key != built_) {
        built_ = key;
        back_.clear();
        tabsDrawn_.clear();
        lines_.clear();
        inviteDrawn_.clear();
        choices_.clear();
        linkBoxes_.clear();
        nameBoxes_.clear();
        if (showing_) build(u, pointer);
        if (inviting_) buildInvite(u, width, bottom, pointer);
    }
    if (settingsOpen_) buildSettings(u, pointer);
    if (play.isOpen()) buildTip(u, width, height, pointer, play.realm());
}

void Chat::build(float u, const Pointer& pointer) {
    const float line = std::max(1.0f, std::round(u));
    // Idle, the chrome and the lines dimmed by the setting; lit, at full.
    const float idle = float(idle_) / 100.0f;
    const float chrome = idle + 0.05f + (1.0f - idle - 0.05f) * lit_;
    const float fillA = float(fill_) / 100.0f * chrome;
    const uint32_t fill = faded(kFill, fillA);
    const float r = kRadius * u;
    const Tab* chosen = nullptr;
    for (const Tab& one : tabs_) {
        if (one.key == tab_) chosen = &one;
    }

    // The drop under the panel only while it is active, fading in with the pointer or the box (the
    // user: "dont use drop shadow when chat is not active for container"); then the fill and the warm
    // light from the top.
    if (lit_ > 0.0f) tip::shadowUnder(back_, panel_, u, r, 0.55f * lit_);
    const float panelR[4] = {0.0f, 0.0f, r, r};
    tip::rounded(back_, panel_, panelR, fill, fill);
    back_.shade({panel_.x, panel_.y, panel_.w, std::min(panel_.h, kPanelLight * u)}, faded(kWarm, kSeamLight * chrome),
                faded(kWarm, kSeamLight * chrome), faded(kWarm, 0.0f), faded(kWarm, 0.0f));
    // The chosen tab: the panel's own fill, its light from its top down to the seam's.
    if (chosen) {
        const float tabR[4] = {r, r, 0.0f, 0.0f};
        tip::rounded(back_, chosen->box, tabR, fill, fill);
        tip::rounded(back_, chosen->box, tabR, faded(kWarm, kTabLight * chrome), faded(kWarm, kSeamLight * chrome));
    }
    // The edge: a line of its own, never a layer under the see-through fill (which tinted the
    // panel apart from the tab). Along the row's foot but under the chosen tab, up and over it,
    // down the panel's sides brightening to its foot's dark, and its foot.
    const uint32_t top = faded(kEdgeTop, chrome), foot = faded(kEdgeFoot, chrome);
    const float seam = panel_.y;
    if (chosen) {
        const Box& t = chosen->box;
        back_.rect({row_.x, seam, t.x - row_.x, line}, top);
        back_.rect({t.right(), seam, row_.right() - t.right(), line}, top);
        back_.rect({t.x, t.y + r, line, seam - t.y - r + line}, top);
        back_.rect({t.right() - line, t.y + r, line, seam - t.y - r + line}, top);
        back_.rect({t.x + r, t.y, t.w - 2.0f * r, line}, top);
        stroke(back_, t.x + line * 0.5f, t.y + r, t.x + r, t.y + line * 0.5f, line, top);
        stroke(back_, t.right() - r, t.y + line * 0.5f, t.right() - line * 0.5f, t.y + r, line, top);
    } else {
        back_.rect({row_.x, seam, row_.w, line}, top);
    }
    const float sideTall = panel_.h - r;
    back_.shade({panel_.x, seam, line, sideTall}, top, top, foot, foot);
    back_.shade({panel_.right() - line, seam, line, sideTall}, top, top, foot, foot);
    back_.rect({panel_.x + r, panel_.bottom() - line, panel_.w - 2.0f * r, line}, foot);
    stroke(back_, panel_.x + line * 0.5f, panel_.bottom() - r, panel_.x + r, panel_.bottom() - line * 0.5f, line, foot);
    stroke(back_, panel_.right() - r, panel_.bottom() - line * 0.5f, panel_.right() - line * 0.5f, panel_.bottom() - r,
           line, foot);

    // The tabs' words, plain; the chosen one lit. An unread count after the word, on a pill.
    const float tabBase = controls::middle(row_.y, row_.h, kWord * u);
    for (const Tab& one : tabs_) {
        const bool isChosen = &one == chosen;
        const bool hovered = one.box.has(pointer.x, pointer.y);
        const uint32_t ink = isChosen ? kBoneHi : hovered ? mix(kTabQuiet, kBoneHi, 0.6f) : kTabQuiet;
        float x = one.box.x + (isChosen ? 14.0f : 12.0f) * u;
        x += word(tabsDrawn_, x, tabBase, kWord * u, faded(ink, chrome), one.word);
        const auto it = unread_.find(one.key);
        if (it == unread_.end() || it->second <= 0 || isChosen) continue;
        const std::string count = std::to_string(std::min(it->second, 99));
        // The whispers' purple, Global's from its orange, General's iron.
        const bool whispers = one.key == "whispers", global = one.key == "global";
        pill(tabsDrawn_, x + kPillGap * u, row_.midY(), u, count,
             whispers ? kPillTop : global ? kGlobalPillTop : kIronPillTop,
             whispers ? kPillFoot : global ? kGlobalPillFoot : kIronPillFoot, chrome);
    }
    // The settings' sliders at the row's end, lit while they are open or under the pointer.
    {
        const bool lit = settingsOpen_ || gear_.has(pointer.x, pointer.y);
        if (settingsOpen_) {
            tabsDrawn_.shade(gear_, faded(kWarm, 0.0f), faded(kWarm, 0.0f), faded(kWarm, 0.10f * chrome),
                             faded(kWarm, 0.10f * chrome));
        }
        const uint32_t rail = faded(lit ? kBone2 : kAshInk, chrome), knob = faded(lit ? kBoneHi : kBone2, chrome);
        const float x0 = gear_.midX() - 7.0f * u, x1 = gear_.midX() + 7.0f * u;
        const float knobs[3] = {0.25f, 0.7f, 0.45f};
        for (int k = 0; k < 3; ++k) {
            const float y = std::round(gear_.midY() + float(k - 1) * 3.5f * u);
            tabsDrawn_.rect({x0, y, x1 - x0, std::max(1.0f, std::round(1.2f * u))}, rail);
            const float kx = std::round(x0 + (x1 - x0) * knobs[k] - 1.2f * u);
            tabsDrawn_.rect({kx, y - 1.5f * u, 2.4f * u, 3.0f * u}, knob);
        }
    }

    // The log, clipped to its well, the newest at the foot.
    lines_.clip(logBox_);
    const float size = textSize() * u, rowH = rowTall() * u;
    const float lineIdle = std::min(1.0f, idle + 0.1f);
    float y = logBox_.bottom() - logTall_ + logScroll_;
    for (size_t k = 0; k < rows_.size(); ++k) {
        const Row& row = rows_[k];
        const float rowTop = y;
        y += rowH;
        if (y < logBox_.y || rowTop > logBox_.bottom()) continue;
        const Line& one = log_[row.line];
        // Idle, a line just come stands at full and settles.
        const float fresh = one.age < kNewFor ? 1.0f : std::max(0.0f, 1.0f - (one.age - kNewFor) / kSettle);
        const float idleA = lineIdle + (1.0f - lineIdle) * fresh;
        const float alpha = idleA + (1.0f - idleA) * lit_;
        const float base = std::round(rowTop + rowH * 0.74f);
        const float x = logBox_.x + (row.first ? 0.0f : kIndent * u);
        spans(lines_, x, base, size, inkOf(one), sets_[row.line], row.start, row.end, alpha, u, long(row.line), pointer);
    }
    // Its slider, when there is more than the well holds: a hairline of iron, its thumb a step lighter.
    if (logTall_ > logBox_.h && lit_ > 0.0f) {
        const float thumb = std::max(24.0f * u, logBox_.h * logBox_.h / logTall_);
        const float at = logBox_.y + (logBox_.h - thumb) * (1.0f - logScroll_ / (logTall_ - logBox_.h));
        const float x = logBox_.right() + 6.0f * u;
        back_.rect({x, logBox_.y, 2.0f * u, logBox_.h}, faded(kIronLo, lit_));
        back_.rect({x, at, 2.0f * u, thumb}, faded(drag_ == Drag::LogSlider ? kIronHi : kIron, lit_));
    }
    // The new lines waiting while he reads back: a small button that jumps to the newest.
    newer_ = {};
    if (unseen_ > 0) {
        const std::string said = std::to_string(unseen_) + (unseen_ == 1 ? " new line" : " new lines");
        const float wide = wordWidth(13.0f * u, said) + 52.0f * u;
        newer_ = {std::round(logBox_.midX() - wide * 0.5f), std::round(logBox_.bottom() - 30.0f * u), std::round(wide),
                  std::round(28.0f * u)};
        controls::State state;
        state.lift = newer_.has(pointer.x, pointer.y) ? 1.0f : 0.0f;
        controls::button(back_, newer_, said, controls::Kind::Secondary, state, u, 13.0f * u);
    }
    // The corner grip: shown while the chat is lit and not locked.
    if (!locked_ && lit_ > 0.0f) {
        const bool lit = drag_ == Drag::Grip || grip_.has(pointer.x, pointer.y);
        const uint32_t ink = faded(lit ? kBoneHi : kIronHi, lit_);
        // Two corners, one inside the other: it pulls up and to the right.
        const float x1 = std::round(grip_.right() - 3.0f * u), y0 = std::round(grip_.y + 3.0f * u);
        const float w = std::max(1.0f, std::round(1.3f * u));
        for (const float in : {0.0f, 4.0f * u}) {
            const float arm = 9.0f * u - in;
            back_.rect({std::round(x1 - in - arm), std::round(y0 + in), std::round(arm), w}, ink);
            back_.rect({std::round(x1 - in - w), std::round(y0 + in), w, std::round(arm)}, ink);
        }
    }

    // The edit box (controls::field, focused), or in its place the key that opens it.
    if (open_) {
        controls::field(back_, input_, std::string(), std::string(), true, false, false, false, u);
        const float base = controls::middle(input_.y, input_.h, kWord * u);
        const uint32_t ink = channelInk(sticky_.channel == net::Channel::Server ? net::Channel::Local : sticky_.channel);
        float x = input_.x + 14.0f * u;
        const bool chipLit = chip_.has(pointer.x, pointer.y) || menu_ == Menu::Channels;
        x += word(back_, x, base, kWord * u, chipLit ? mix(ink, hex(0xffffff), 0.3f) : ink, channelWord());
        back_.rect({std::round(x + 10.0f * u), std::round(input_.midY() - 9.0f * u), line, std::round(18.0f * u)}, kIronLo);
        const float textX = x + 21.0f * u;
        const float room = input_.right() - textX - 12.0f * u;
        // The tail of a draft wider than the field: its end, where he is typing.
        Line typedLine;
        typedLine.text = draft_;
        read(typedLine);
        resolve(typedLine);
        Set set;
        set.text = typedLine.shown;
        set.links = typedLine.links;
        size_t from = 0;
        while (from < set.text.size() && bodyWidth(kDraft * u, set.text.substr(from)) > room) ++from;
        while (from < set.text.size() && (uint8_t(set.text[from]) & 0xC0) == 0x80) ++from;
        const float textBase = controls::middle(input_.y, input_.h, kDraft * u);
        const float drawn = set.text.empty() ? 0.0f
                                             : spans(back_, textX, textBase, kDraft * u, kBoneHi, set, from,
                                                     set.text.size(), 1.0f, u, -1, Pointer{});
        if (std::fmod(clock_, 1.0f) < 0.6f) {
            back_.rect({std::round(textX + drawn + 1.0f * u), std::round(input_.midY() - 10.0f * u),
                        std::max(1.0f, std::round(2.0f * u)), std::round(20.0f * u)},
                       kBloodHi);
        }
    } else {
        // Quiet while idle, but always readable: it is how the chat is found.
        const float a = 0.75f + 0.25f * lit_;
        const size_t mark = back_.mark();
        const float x = input_.x + 2.0f * u;
        const float wide = controls::keycap(back_, x, input_.midY(), "Enter", u);
        if (a < 1.0f) back_.fadeSince(mark, a, 0.0f);
        body(back_, x + wide + 8.0f * u, controls::middle(input_.y, input_.h, 14.0f * u), 14.0f * u,
             faded(kBone2, a), "to chat");
    }
}

void Chat::buildSettings(float u, const Pointer& pointer) {
    uint64_t key = 1469598103934665603ull;
    const auto mixIn = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
    mixIn(uint64_t(std::lround(settingsBox_.x))), mixIn(uint64_t(std::lround(settingsBox_.y)));
    mixIn(uint64_t(std::lround(u * 100.0f))), mixIn(size_), mixIn(fill_), mixIn(idle_), mixIn(stamps_);
    mixIn(bubbles_), mixIn(whisperTab_), mixIn(locked_);
    if (settingsBox_.has(pointer.x, pointer.y)) mixIn(uint64_t(std::lround(pointer.x))), mixIn(uint64_t(std::lround(pointer.y)));
    if (key == settingsBuilt_) return;
    settingsBuilt_ = key;
    gfx::Canvas& c = settingsDrawn_;
    c.clear();
    const Box& b = settingsBox_;
    const float line = std::max(1.0f, std::round(u)), r = kRadius * u;
    // The chat's own fill, edge, light and drop: one piece with it.
    const uint32_t fill = faded(kFill, std::max(0.62f, float(fill_) / 100.0f));
    tip::shadowUnder(c, b, u, r, 0.55f);
    const float all[4] = {r, r, r, r};
    tip::rounded(c, b, all, fill, fill);
    c.shade({b.x, b.y, b.w, kPanelLight * u}, faded(kWarm, kSeamLight), faded(kWarm, kSeamLight), faded(kWarm, 0.0f),
            faded(kWarm, 0.0f));
    c.rect({b.x + r, b.y, b.w - 2.0f * r, line}, kEdgeTop);
    c.shade({b.x, b.y + r, line, b.h - 2.0f * r}, kEdgeTop, kEdgeTop, kEdgeFoot, kEdgeFoot);
    c.shade({b.right() - line, b.y + r, line, b.h - 2.0f * r}, kEdgeTop, kEdgeTop, kEdgeFoot, kEdgeFoot);
    c.rect({b.x + r, b.bottom() - line, b.w - 2.0f * r, line}, kEdgeFoot);
    stroke(c, b.x + line * 0.5f, b.y + r, b.x + r, b.y + line * 0.5f, line, kEdgeTop);
    stroke(c, b.right() - r, b.y + line * 0.5f, b.right() - line * 0.5f, b.y + r, line, kEdgeTop);
    stroke(c, b.x + line * 0.5f, b.bottom() - r, b.x + r, b.bottom() - line * 0.5f, line, kEdgeFoot);
    stroke(c, b.right() - r, b.bottom() - line * 0.5f, b.right() - line * 0.5f, b.bottom() - r, line, kEdgeFoot);

    // Its head: the kicker, the reset as a quiet link, the cross.
    const float pad = 14.0f * u;
    controls::caps(c, b.x + pad, b.y + 22.0f * u, 11.0f * u, kAshInk, "CHAT SETTINGS", 0.14f);
    closeBox_ = {b.right() - pad - 16.0f * u, b.y + 8.0f * u, 18.0f * u, 18.0f * u};
    {
        const bool lit = closeBox_.has(pointer.x, pointer.y);
        const float m = 4.5f * u;
        const uint32_t ink = lit ? kBoneHi : kAshInk;
        stroke(c, closeBox_.midX() - m, closeBox_.midY() - m, closeBox_.midX() + m, closeBox_.midY() + m, 1.4f * u, ink);
        stroke(c, closeBox_.midX() + m, closeBox_.midY() - m, closeBox_.midX() - m, closeBox_.midY() + m, 1.4f * u, ink);
    }
    {
        const std::string reset = "Reset size and place";
        const float wide = bodyWidth(13.0f * u, reset);
        resetBox_ = {closeBox_.x - 10.0f * u - wide, b.y + 8.0f * u, wide, 18.0f * u};
        const bool lit = resetBox_.has(pointer.x, pointer.y);
        const uint32_t ink = lit ? kBoneHi : kTabQuiet;
        body(c, resetBox_.x, b.y + 22.0f * u, 13.0f * u, ink, reset);
        c.rect({resetBox_.x, b.y + 25.0f * u, wide, line}, faded(ink, 0.4f));
    }

    // Its rows: a word at the left, its control at the right.
    float y = b.y + 36.0f * u;
    const float labelSize = 14.0f * u;
    const auto label = [&](const std::string& text) {
        body(c, b.x + pad, controls::middle(y, kSettingsRow * u, labelSize), labelSize, kBoneHi, text);
    };
    const auto at = [&](float wide) {
        return Box{b.right() - pad - wide * u, y + 4.0f * u, wide * u, kSettingsRow * u - 8.0f * u};
    };
    const auto over = [&](const Box& box, int parts) {
        return box.has(pointer.x, pointer.y) ? std::clamp(int((pointer.x - box.x) / (box.w / float(parts))), 0, parts - 1)
                                             : -1;
    };
    label("Text size");
    sizeBox_ = at(96.0f);
    {
        const char* const words[3] = {"S", "M", "L"};
        controls::choice(c, sizeBox_, words, 3, size_, over(sizeBox_, 3), u, 12.0f * u);
    }
    y += kSettingsRow * u;
    label("Background");
    fillBox_ = {b.x + pad + 96.0f * u, y, b.w - 2.0f * pad - 96.0f * u, kSettingsRow * u};
    controls::slider(c, fillBox_, float(fill_) / 100.0f, std::to_string(fill_) + "%", u);
    y += kSettingsRow * u;
    label("When idle");
    idleBox_ = {b.x + pad + 96.0f * u, y, b.w - 2.0f * pad - 96.0f * u, kSettingsRow * u};
    controls::slider(c, idleBox_, float(idle_) / 100.0f, std::to_string(idle_) + "%", u);
    y += kSettingsRow * u;
    c.rect({b.x + pad, y + 2.0f * u, b.w - 2.0f * pad, line}, hex(0x241b15));
    y += 6.0f * u;
    label("Time stamps");
    stampsBox_ = at(88.0f);
    controls::toggle(c, stampsBox_, "Off", "On", stamps_, u);
    y += kSettingsRow * u;
    label("Over heads");
    bubblesBox_ = at(88.0f);
    controls::toggle(c, bubblesBox_, "Off", "On", bubbles_, u);
    y += kSettingsRow * u;
    label("Whispers");
    whispersBox_ = at(150.0f);
    controls::toggle(c, whispersBox_, "Own tab", "General", !whisperTab_, u);
    y += kSettingsRow * u;
    label("Lock");
    lockBox_ = at(88.0f);
    controls::toggle(c, lockBox_, "Off", "On", locked_, u);
}

void Chat::buildTip(float u, float width, float height, const Pointer& pointer, const sim::Realm& realm) {
    tip_.clear();
    suggestBoxes_.clear();
    menuRows_.clear();
    tipping_ = false;
    // A menu of its own: PlayerMenu's sheet, its head's kicker, a row each.
    if (menu_ != Menu::None) {
        struct Item {
            int what;
            std::string word, who;
            uint32_t ink;
            bool ticked;
        };
        std::vector<Item> items;
        std::string head;
        if (menu_ == Menu::Channels) {
            head = "SAY ON";
            const auto add = [&](net::Channel channel, const char* word) {
                items.push_back({int(channel), word, "", channelInk(channel), sticky_.channel == channel});
            };
            add(net::Channel::Local, "Say");
            add(net::Channel::Map, "Map");
            if (inParty_) add(net::Channel::Party, "Party");
            if (inGuild_) add(net::Channel::Guild, "Guild");
            if (!leftGlobal_) add(net::Channel::Global, "Global");
        } else {
            head = (menuTab_ == "global" ? std::string("GLOBAL") : std::string("GENERAL")) + " SHOWS";
            const uint32_t shows = filter(menuTab_);
            for (const Listed& one : kListed) items.push_back({int(one.bits), one.word, one.who, one.ink, (shows & one.bits) != 0});
        }
        const float wide = (menu_ == Menu::Channels ? 150.0f : 236.0f) * u;
        const float tall = (kMenuHead + kMenuRow * float(items.size()) + 6.0f) * u;
        const float x = menu_ == Menu::Channels ? chip_.x : std::min(menuBox_.x, width - wide - 8.0f * u);
        const float y = menu_ == Menu::Channels ? input_.y - 6.0f * u - tall : row_.y - 6.0f * u - tall;
        menuBox_ = {x, std::max(8.0f * u, y), wide, tall};
        const float r = 6.0f * u;
        const float radii[4] = {r, r, r, r};
        tip::shadowUnder(tip_, menuBox_, u, r, 0.8f);
        tip::rounded(tip_, menuBox_, radii, kSheet, kSheet);
        controls::caps(tip_, menuBox_.x + kMenuPad * u, menuBox_.y + 16.0f * u, 10.5f * u, kAshInk, head, 0.14f);
        for (size_t i = 0; i < items.size(); ++i) {
            const Item& one = items[i];
            const Box rowBox{menuBox_.x, menuBox_.y + (kMenuHead + kMenuRow * float(i)) * u, menuBox_.w, kMenuRow * u};
            menuRows_.push_back({rowBox, one.what});
            if (rowBox.has(pointer.x, pointer.y)) tip_.rect({rowBox.x + 3.0f * u, rowBox.y, rowBox.w - 6.0f * u, rowBox.h}, kRowLit);
            const float base = controls::middle(rowBox.y, rowBox.h, 13.5f * u);
            float tx = menuBox_.x + kMenuPad * u;
            if (menu_ == Menu::Shows) {
                // A tick: an iron well, filled with the accent when on.
                const Box tick{tx, rowBox.midY() - 6.5f * u, 13.0f * u, 13.0f * u};
                tip_.rect(tick, kIronLo);
                tip_.rect(tick.grown(-std::max(1.0f, u)), kVoid);
                if (one.ticked) tip_.rect(tick.grown(-3.0f * u), hex(0xc2362b));
                tx += 23.0f * u;
            } else if (one.ticked) {
                tip_.rect({menuBox_.x + 3.0f * u, rowBox.y + 6.0f * u, 2.0f * u, rowBox.h - 12.0f * u}, hex(0xc2362b));
            }
            body(tip_, tx, base, 13.5f * u, one.ink, one.word);
            if (!one.who.empty()) {
                const float w = bodyWidth(12.0f * u, one.who);
                body(tip_, menuBox_.right() - kMenuPad * u - w, base, 12.0f * u, kAshInk2, one.who);
            }
        }
        tipping_ = true;
        return;
    }
    if (!open_ && !logBox_.has(pointer.x, pointer.y)) return;
    if (!tables_) return;
    const auto card = [&](const sim::Held& held, const Box& under) {
        if (held.item < 0 || size_t(held.item) >= tables_->items.size()) return;
        tip::Sheet sheet = describe(*tables_, held, realm.wearer(), realm.satchel());
        if (tipStage_) tip::stand(*tipStage_, held.item, held.refinement, sheet);
        tip::draw(tip_, sheet, under, width, height);
    };
    if (open_ && !suggestions_.empty()) {
        // Over the input from the typing's start: each a name in its quality's ink, where it is
        // carried quiet at the right, the chosen one lit.
        const float size = 16.0f * u, row = kSuggestRow * u;
        float wide = 260.0f * u;
        for (const Suggestion& one : suggestions_) {
            wide = std::max(wide, bodyWidth(size, one.label) + wordWidth(13.0f * u, one.where) + 44.0f * u);
        }
        const float tall = row * float(suggestions_.size()) + 8.0f * u;
        const float x = std::min(chip_.right(), input_.right() - wide);
        suggestBox_ = {x, input_.y - 4.0f * u - tall, wide, tall};
        const float r = 6.0f * u;
        const float radii[4] = {r, r, r, r};
        tip::shadowUnder(tip_, suggestBox_, u, r, 0.8f);
        tip::rounded(tip_, suggestBox_, radii, kSheet, kSheet);
        for (size_t k = 0; k < suggestions_.size(); ++k) {
            const Suggestion& one = suggestions_[k];
            const Box at{suggestBox_.x + 4.0f * u, suggestBox_.y + 4.0f * u + row * float(k), wide - 8.0f * u, row};
            suggestBoxes_.push_back(at);
            if (int(k) == pick_) tip_.rect(at, kRowLit);
            const float base = controls::middle(at.y, at.h, size);
            const uint32_t ink = tip::colourOf(qualityOf(tables_->items[size_t(one.held.item)], one.held));
            body(tip_, at.x + 8.0f * u, base, size, ink, one.label);
            if (*one.where) {
                word(tip_, at.right() - 8.0f * u - wordWidth(13.0f * u, one.where), base, 13.0f * u, kAshInk, one.where);
            }
        }
        // Names only, no card (the user, 2026-10-11: "very simple, without pictures").
        tipping_ = true;
        return;
    }
    if (!logBox_.has(pointer.x, pointer.y)) return;
    for (const LinkBox& one : linkBoxes_) {
        if (!one.box.has(pointer.x, pointer.y) || one.line >= log_.size() || one.link >= log_[one.line].links.size()) {
            continue;
        }
        card(log_[one.line].links[one.link].held, one.box);
        tipping_ = true;
        return;
    }
}

long Chat::pendingInvite() const {
    for (size_t i = log_.size(); i-- > 0;) {
        const Line& line = log_[i];
        if (!line.inviter.empty() && line.answer == 0 && line.age < (line.trader != 0 ? kTradeLife : kInviteLife)) {
            return long(i);
        }
    }
    return -1;
}

void Chat::buildInvite(float u, float width, float bottom, const Pointer& pointer) {
    (void)width;
    (void)bottom;
    const long index = pendingInvite();
    if (index < 0) return;
    const Line& line = log_[size_t(index)];
    const float life = line.trader != 0 ? kTradeLife : kInviteLife;
    const float alpha = std::min(line.age / kInviteIn, 1.0f) * std::clamp((life - line.age) / kInviteOut, 0.0f, 1.0f);
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
    const auto set = [&](float x, float baseline, float size, uint32_t ink, const std::string& text) {
        controls::label(inviteDrawn_, x + u, baseline + u, size, faded(kDrop, float(ink >> 24) / 255.0f), text);
        return controls::label(inviteDrawn_, x, baseline, size, ink, text);
    };
    // "Party", the question, and the two answers.
    const std::string kicker = line.guild ? "Guild" : line.trader != 0 ? "Trade" : "Party";
    word(inviteDrawn_, card.midX() - wordWidth(13.0f * u, kicker) * 0.5f, card.y + 18.0f * u, 13.0f * u,
         faded(line.guild ? kGuildInk : kPartyInk, alpha), kicker);
    // The asker's name in the players' green, the rest in bone, the whole centred; a guild's mark
    // before it, and its quiet line under it.
    const std::string asks = line.guild ? line.text.substr(std::min(line.inviter.size(), line.text.size()))
                             : line.trader != 0 ? " wants to trade with you."
                             : line.text.size() > line.inviter.size() ? line.text.substr(line.inviter.size())
                                                                      : " invites you to a party.";
    const float markRoom = line.guild ? (kGuildMarkSide + 10.0f) * u : 0.0f;
    const float wide = markRoom + bodyWidth(21.0f * u, line.inviter) + bodyWidth(21.0f * u, asks);
    const float qx = card.midX() - wide * 0.5f;
    if (line.guild) {
        const Box well{qx, card.y + 50.0f * u - 22.0f * u, kGuildMarkSide * u, kGuildMarkSide * u};
        inviteDrawn_.rect({well.x - 2.0f * u, well.y - 2.0f * u, well.w + 4.0f * u, well.h + 4.0f * u}, faded(kIron, alpha));
        inviteDrawn_.rect({well.x - 1.0f * u, well.y - 1.0f * u, well.w + 2.0f * u, well.h + 2.0f * u}, faded(kVoid, alpha));
        guildmark::draw(inviteDrawn_, well, line.mark, alpha);
        const float metaWide = bodyWidth(15.0f * u, line.meta);
        set(card.midX() - metaWide * 0.5f, card.y + 78.0f * u, 15.0f * u, faded(kAshInk, alpha), line.meta);
        // The minute running down, under the buttons.
        const float left = std::clamp(1.0f - line.age / life, 0.0f, 1.0f);
        const Box bar{card.midX() - kGuildBarWide * 0.5f * u, card.bottom() - 3.0f * u, kGuildBarWide * u, 2.0f * u};
        inviteDrawn_.rect(bar, faded(hex(0x241b15), alpha));
        inviteDrawn_.rect({bar.x, bar.y, bar.w * left, bar.h}, faded(hex(0xa88b6c), alpha));
    }
    const float qy = card.y + 50.0f * u;
    const float named = set(qx + markRoom, qy, 21.0f * u, faded(kInviterInk, alpha), line.inviter);
    // The guild's own name in its green, the rest of the line in bone.
    const size_t guildAt = line.guild && !line.guildName.empty() ? asks.rfind(line.guildName) : std::string::npos;
    if (guildAt == std::string::npos) {
        set(qx + markRoom + named, qy, 21.0f * u, faded(kBoneHi, alpha), asks);
    } else {
        float x = qx + markRoom + named;
        x += set(x, qy, 21.0f * u, faded(kBoneHi, alpha), asks.substr(0, guildAt));
        x += set(x, qy, 21.0f * u, faded(kGuildInk, alpha), line.guildName);
        set(x, qy, 21.0f * u, faded(kBoneHi, alpha), asks.substr(guildAt + line.guildName.size()));
    }
    const float bw = 112.0f * u, bh = 36.0f * u, gap = 12.0f * u;
    for (const bool accept : {true, false}) {
        const Box box{card.midX() + (accept ? -gap * 0.5f - bw : gap * 0.5f),
                      card.bottom() - bh - (line.guild ? 12.0f : 6.0f) * u, bw, bh};
        controls::State state;
        state.lift = box.has(pointer.x, pointer.y) ? 1.0f : 0.0f;
        const size_t mark = inviteDrawn_.mark();
        controls::button(inviteDrawn_, box, accept ? "Accept" : "Decline",
                         accept ? controls::Kind::Primary : controls::Kind::Secondary, state, u, 16.0f * u);
        if (alpha < 1.0f) inviteDrawn_.fadeSince(mark, alpha, 0.0f);
        choices_.push_back({box, size_t(index), accept});
    }
}

}  // namespace mu::game
