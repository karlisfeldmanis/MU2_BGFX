#include "game/ui/whats_new.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;

// The canvas's own measures (the design's artboard, 1600x900 around a 1240x640 window), in
// tip::unit() at 1080 lines. The picture fills the left the whole height; the right column holds
// a small head with Skip, the topics as a recessed bar of tabs, the prose, the facts on a lit
// panel, and the foot: a gold progress line, the count, Back and Next.
constexpr float kWide = 1240.0f, kTall = 640.0f;
constexpr float kRadius = 6.0f;
constexpr float kPicWide = 700.0f;
constexpr float kColX = kPicWide + 32.0f, kColWide = kWide - kColX - 32.0f;
constexpr float kHeadTall = 64.0f;
constexpr float kBarTop = kHeadTall, kBarTall = 46.0f, kBarPad = 4.0f;
constexpr float kBodyTop = kBarTop + kBarTall + 22.0f;
constexpr float kLeadSize = 18.0f, kLeadStep = 27.0f;
constexpr float kRowTall = 41.0f, kRowTight = 27.0f, kTopicTall = 35.0f, kWellPad = 2.0f, kRowInset = 16.0f;
constexpr float kProgressTop = kTall - 80.0f;
constexpr float kButtonTop = kProgressTop + 2.0f + 17.0f, kButtonTall = 44.0f;
constexpr float kNextWide = 156.0f, kBackWide = 110.0f;
constexpr float kSkipWide = 56.0f;
constexpr float kTitleSize = 48.0f;

// Targets: 0-7 the tabs, then Back, Next, Skip, then the welcome's eight topics.
constexpr int kTabs = 8;
constexpr int kBack = 8, kNext = 9, kSkip = 10, kTopic = 11;

// The canvas's colours.
constexpr uint32_t kGold = gfx::rgba(0.886f, 0.741f, 0.447f);
constexpr uint32_t kGoldLit = gfx::rgba(0.937f, 0.824f, 0.561f);
constexpr uint32_t kSheetTop = gfx::rgba(0.110f, 0.098f, 0.090f), kSheetFoot = gfx::rgba(0.071f, 0.063f, 0.063f);
constexpr uint32_t kLead = gfx::rgba(0.827f, 0.792f, 0.737f);
constexpr uint32_t kName = gfx::rgba(0.937f, 0.906f, 0.851f);
constexpr uint32_t kValue = gfx::rgba(0.639f, 0.604f, 0.553f);
constexpr uint32_t kQuiet = gfx::rgba(0.588f, 0.553f, 0.502f);
constexpr uint32_t kHair = gfx::rgba(1.0f, 0.941f, 0.843f, 0.06f);

struct Row {
    const char* name;
    const char* value;
    uint32_t dot = 0;  // a rune's rarity, 0 for none
};

struct Slide {
    const char* art;  // its key in index.json's effects (pipeline/index.py)
    const char* kicker;
    const char* title;
    const char* lead;
    std::vector<Row> rows;
    const char* note = "";
    bool tight = false;
};

// The slides, the welcome first; its rows are the topics, one a tab. Written to the canvas the
// user signed off; every "never had" on the last was checked against MuMain (LEGACY/reference).
const std::vector<Slide>& deck() {
    static const std::vector<Slide> slides = {
        {"whatsnew_welcome", "Welcome back", "A New Era",
         "MU as you remember it, with new things to learn. Pick one, or walk through them in order.",
         {{"Talents", "A tree for every class"},
          {"Elites", "Golden, tougher, richer"},
          {"Runes", "Sockets and their powers"},
          {"Raids", "Ten together, anywhere"},
          {"Quests", "Voiced, with great rewards"},
          {"Market", "Trade without meeting"},
          {"Scaling", "Points that matter"},
          {"More", "Things MU never had"}},
         "Open this again any time from the game menu."},
        {"whatsnew_talents", "New", "Talents",
         "Earn a talent point every five levels and spend it on your class's own tree: build a "
         "two-handed knight, a fire wizard or a pure support elf.",
         {{"Six columns", "A build in each, passives only"},
          {"Small talents", "Up to 10 points, a stat each"},
          {"Majors", "One point, opened by 10 above"},
          {"Reset", "Free, any time"}},
         "Press A to open your talents."},
        {"whatsnew_elites", "New", "Elite monsters",
         "Bigger, golden Elites roam every field map. A pulsing gold dot on the minimap, and its "
         "bell, tell you one is near.",
         {{"Every field map", "Lorencia to Aida, one or two each"},
          {"Tough", "Many times the life, hits harder"},
          {"Unshaken", "Freezing and slowing don't hold"},
          {"Spoils", "Always an excellent item, ten times the exp"}}},
        {"whatsnew_runes", "New", "Runes and sockets",
         "Gear can drop with +Socket, up to three. Drop a rune on the item to set it, and its power "
         "is yours. The greatest runes come from finishing quest lines.",
         {{"Common", "Faint powers; three fuse into a Rare", gfx::rgba(0.949f, 0.949f, 0.949f)},
          {"Rare", "+20% fire, ice, poison, lightning", gfx::rgba(0.184f, 0.549f, 1.0f)},
          {"Epic", "On-hit: burn, poison, bleed", gfx::rgba(0.698f, 0.361f, 0.961f)},
          {"Legendary", "Class signatures: Stormcall, Arcane Echo", gfx::rgba(1.0f, 0.502f, 0.0f)}}},
        {"whatsnew_raids", "New", "Raids",
         "Turn your party into a raid of up to ten and take it anywhere: Blood Castle, Devil "
         "Square, the hunting grounds, or the Golden Dragon when it lands.",
         {{"Golden Dragon", "Four stages \xC2\xB7 Kundun boxes, runes"},
          {"Blood Castle", "Castles 1-6"},
          {"Devil Square", "Squares 1-4"},
          {"Open world", "Any hunting ground, together"}},
         "Make a raid from the party menu, or type /raid."},
        {"whatsnew_quests", "New", "Quests",
         "Every quest giver is voiced and has a story to tell. They send you to clear their lands, "
         "and the rewards are worth it: socketed weapons, whole armour sets, Legendary runes.",
         {{"First clear", "Weapons, set pieces, runes"},
          {"Every clear", "Experience, Zen, jewels, boxes"},
          {"Repeatable", "Back every 12 hours"}}},
        {"whatsnew_market", "New", "The Market",
         "A Broker in each city trades with every player. List from your bag or vault; collect "
         "your Zen from any Broker.",
         {{"Orsin", "Lorencia"}, {"Kesra", "Devias"}, {"Liane", "Noria"}},
         "No fee \xC2\xB7 20 listings an account \xC2\xB7 Collect keeps 30 days \xC2\xB7 bound items "
         "can't be sold."},
        {"whatsnew_scaling", "Changed", "Skills that scale",
         "Skills are learned for good and sit on Q, W, E and R, each with a cooldown. Every point "
         "you spend makes them stronger.",
         {{"Strength", "Knight skill damage"},
          {"Energy", "Spells and rune strikes"},
          {"Agility", "Shorter cooldowns and attack speed"},
          {"Level and stats", "The Elf's summons"}}},
        {"whatsnew_more", "And", "& more", "And things MU never had before.",
         {{"Powered jewellery", "Rings and pendants with up to three powers"},
          {"Jeweler", "Moss in Noria recuts, salvages, combines"},
          {"Loot beams", "Legendary and quest drops under a light"},
          {"Gear score", "One number for everything you wear"},
          {"Item compare", "Every card shows the change to what you wear"},
          {"Quest markers", "A mark over every giver with work for you"},
          {"Go Back!", "Return to Dungeon or Lost Tower in 5 min"},
          {"Thunderstorms", "Lightning and thunder over the maps"},
          {"Real-time shadows", "Sun, torches and bonfires cast them"},
          {"HDR light and PBR", "Metal shines, leather stays dull"},
          {"Reflections and AO", "Soft shading where things meet"},
          {"New maps", "You have to discover that yourself"}},
         "and much more...", true},
    };
    return slides;
}

const char* const kTabNames[kTabs] = {"Talents", "Elites", "Runes", "Raids", "Quests", "Market", "Scaling", "More"};

Box placed(float x, float y, const Box& b, float u) {
    return {std::round(x + b.x * u), std::round(y + b.y * u), std::round(b.w * u), std::round(b.h * u)};
}

// Prose broken at its spaces into lines no wider than `wide`, measured at `size`; both in the
// same measure, so a layout wrapped in units holds at any scale.
std::vector<std::string> wrap(const std::string& text, float wide, float size) {
    std::vector<std::string> lines;
    std::string line;
    size_t at = 0;
    while (at < text.size()) {
        const size_t space = std::min(text.find(' ', at), text.size());
        const std::string word = text.substr(at, space - at);
        const std::string tried = line.empty() ? word : line + " " + word;
        if (line.empty() || controls::labelWidth(size, tried) <= wide) {
            line = tried;
        } else {
            lines.push_back(line);
            line = word;
        }
        at = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

float rowTall(const Slide& slide, bool topics) { return topics ? kTopicTall : slide.tight ? kRowTight : kRowTall; }

// How tall a slide's body is, prose to note, in units.
float bodyTall(const Slide& slide) {
    const bool topics = &slide == &deck()[0];
    const float lead = float(wrap(slide.lead, kColWide, kLeadSize).size()) * kLeadStep + 12.0f;
    const float well = rowTall(slide, topics) * float(slide.rows.size()) + kWellPad * 2.0f;
    const float note = slide.note[0] != '\0' ? 14.0f + 21.0f * float(wrap(slide.note, kColWide, 15.0f).size()) : 0.0f;
    return lead + well + note;
}

// Where a slide's prose starts: a short slide sits a little lower, so the room it leaves above the
// foot is shared rather than all under it.
float bodyTop(const Slide& slide) {
    const float room = kProgressTop - 24.0f - kBodyTop;
    return kBodyTop + std::max(0.0f, (room - bodyTall(slide)) * 0.3f);
}

// Where a slide's panel starts, under its prose, in units.
float wellTop(const Slide& slide) {
    const size_t lines = wrap(slide.lead, kColWide, kLeadSize).size();
    return bodyTop(slide) + float(lines) * kLeadStep + 12.0f;
}

Box tabBox(int i) {
    const float wide = (kColWide - kBarPad * 2.0f - kBarPad * float(kTabs - 1)) / float(kTabs);
    return {kColX + kBarPad + (wide + kBarPad) * float(i), kBarTop + kBarPad, wide, kBarTall - kBarPad * 2.0f};
}
Box nextBox() { return {kWide - 32.0f - kNextWide, kButtonTop, kNextWide, kButtonTall}; }
Box backBox() {
    const Box next = nextBox();
    return {next.x - 10.0f - kBackWide, kButtonTop, kBackWide, kButtonTall};
}
Box skipBox() { return {kWide - 22.0f - kSkipWide, (kHeadTall - 44.0f) * 0.5f + 4.0f, kSkipWide, 44.0f}; }
Box rowBox(const Slide& slide, bool topics, int i) {
    const float tall = rowTall(slide, topics);
    return {kColX, wellTop(slide) + kWellPad + tall * float(i), kColWide, tall};
}

// A rounded box, one radius all round, graded top to foot.
void roundBox(gfx::Canvas& canvas, const Box& box, float radius, uint32_t top, uint32_t foot) {
    const float corners[4] = {radius, radius, radius, radius};
    tip::rounded(canvas, box, corners, top, foot);
}

// The button words' face, small capitals with no tracking, centred in a box.
void word(gfx::Canvas& canvas, const Box& box, float size, uint32_t ink, const std::string& text) {
    const gfx::Face* face = controls::wordFace();
    if (face == nullptr) return;
    const float wide = face->measure(size, text);
    canvas.lettered(*face, controls::wordTexture(), std::round(box.midX() - wide * 0.5f),
                    controls::middle(box.y, box.h, size), size, 0.0f, ink, text);
}

// Blends two colours, `t` of the way from `a` to `b`, channel by channel.
uint32_t mix(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float ca = float((a >> shift) & 0xFFu), cb = float((b >> shift) & 0xFFu);
        out |= uint32_t(std::lround(ca + (cb - ca) * t)) << shift;
    }
    return out;
}

}  // namespace

int WhatsNew::slides() { return int(deck().size()); }

void WhatsNew::open(const gfx::Interface& interface, panel::Arts* arts) {
    interface.adopt(canvas_);
    arts_ = arts;
}

void WhatsNew::show() {
    up_ = true;
    go(0);
}

void WhatsNew::go(int slide) {
    slide_ = std::clamp(slide, 0, slides() - 1);
    over_ = pressing_ = -1;
    lift_.fill(0.0f);
}

int WhatsNew::targetAt(float ux, float uy) const {
    for (int i = 0; i < kTabs; ++i) {
        if (tabBox(i).has(ux, uy)) return i;
    }
    if (slide_ > 0 && backBox().has(ux, uy)) return kBack;
    if (nextBox().has(ux, uy)) return kNext;
    if (skipBox().has(ux, uy)) return kSkip;
    if (slide_ == 0) {
        const Slide& welcome = deck()[0];
        for (int i = 0; i < int(welcome.rows.size()); ++i) {
            if (rowBox(welcome, true, i).has(ux, uy)) return kTopic + i;
        }
    }
    return -1;
}

void WhatsNew::update(float seconds, float width, float height, const Pointer& pointer, bool escape,
                      Result* out) {
    Result result;
    if (up_) {
        const float k = tip::unit();
        x_ = std::round((width - kWide * k) * 0.5f);
        y_ = std::round(height * 0.46f - kTall * k * 0.5f);
        const float ux = (pointer.x - x_) / k, uy = (pointer.y - y_) / k;
        over_ = targetAt(ux, uy);
        const float step = seconds / style::kHoverSeconds;
        for (int i = 0; i < kTargets; ++i) {
            lift_[i] = over_ == i ? std::min(1.0f, lift_[i] + step) : std::max(0.0f, lift_[i] - step);
        }
        if (pointer.pressed) pressing_ = over_;
        int fired = -1;
        if (pointer.released) {
            if (pressing_ >= 0 && pressing_ == over_) fired = pressing_;
            pressing_ = -1;
        }
        const bool last = slide_ == slides() - 1;
        if (escape || fired == kSkip || (fired == kNext && last)) {
            up_ = false;
            result.closed = result.clicked = true;
        } else if (fired == kNext) {
            go(slide_ + 1);
            result.clicked = true;
        } else if (fired == kBack) {
            go(slide_ - 1);
            result.clicked = true;
        } else if (fired >= 0 && fired < kTabs) {
            go(fired + 1);
            result.clicked = true;
        } else if (fired >= kTopic) {
            go(fired - kTopic + 1);
            result.clicked = true;
        }
    }
    if (out) *out = result;

    now_ = Drawn{};
    now_.up = up_;
    if (up_) {
        now_.slide = slide_;
        now_.x = x_;
        now_.y = y_;
        now_.scale = tip::unit();
        now_.over = over_;
        now_.pressing = pressing_;
        now_.lift = lift_;
    }
    if (built_ && now_ == drawn_) return;
    drawn_ = now_;
    built_ = true;
    rebuild();
}

void WhatsNew::rebuild() {
    canvas_.clear();
    if (!up_) return;
    const float x = x_, y = y_, u = tip::unit();
    const Slide& slide = deck()[size_t(slide_)];
    const int count = slides();
    const auto lift = [&](int which) {
        const float t = lift_[which];
        return t * t * (3.0f - 2.0f * t);
    };

    // The game dimmed behind it, darker toward the edges.
    canvas_.rect({0.0f, 0.0f, 1e5f, 1e5f}, gfx::rgba(0.03f, 0.027f, 0.024f, 0.55f));

    // The window: its shadow, a gradient outline (gold at the top, a dark red at the foot) and
    // the body inside it, warmer under the head.
    const Box window = placed(x, y, {0.0f, 0.0f, kWide, kTall}, u);
    const float radius = kRadius * u;
    tip::shadowUnder(canvas_, window, u, radius, 1.4f);
    roundBox(canvas_, window, radius, gfx::rgba(0.871f, 0.729f, 0.439f, 0.55f), gfx::rgba(0.431f, 0.118f, 0.102f, 0.5f));
    const float edge = std::max(1.0f, u);
    const Box body{window.x + edge, window.y + edge, window.w - edge * 2.0f, window.h - edge * 2.0f};
    roundBox(canvas_, body, radius - edge, kSheetTop, kSheetFoot);
    {
        const uint32_t none = gfx::rgba(0.839f, 0.667f, 0.376f, 0.0f), warm = gfx::rgba(0.839f, 0.667f, 0.376f, 0.07f);
        const Box glow{body.x + body.w * 0.55f, body.y + radius, body.w * 0.45f - radius, 220.0f * u};
        canvas_.shade(glow, none, warm, none, none);
    }

    // The picture, the whole height on the left, cut to the box's shape from its middle, then
    // faded into the body toward the prose and darkened at its foot under the title.
    const Box pic{body.x, body.y, std::round(kPicWide * u), body.h};
    canvas_.rect(pic, style::kAsh0);
    if (arts_ != nullptr) {
        const gfx::Art& art = arts_->get(slide.art);
        if (art.valid()) {
            const float want = pic.w / pic.h;
            Box from{0.0f, 0.0f, art.width, art.height};
            if (art.width / art.height > want) {
                from.w = art.height * want;
                from.x = (art.width - from.w) * 0.5f;
            } else {
                from.h = art.width / want;
                from.y = (art.height - from.h) * 0.5f;
            }
            canvas_.region(art, pic, from);
        }
    }
    {
        // Into the body's own grade, top to foot, so the picture ends with no seam.
        const uint32_t clearTop = kSheetTop & 0x00FFFFFFu, clearFoot = kSheetFoot & 0x00FFFFFFu;
        const uint32_t halfTop = clearTop | 0xD8000000u, halfFoot = clearFoot | 0xD8000000u;
        const float fade = pic.w * 0.3f;
        const float mid = fade * 0.75f;
        canvas_.shade({pic.right() - fade, pic.y, mid, pic.h}, clearTop, halfTop, halfFoot, clearFoot);
        canvas_.shade({pic.right() - fade + mid, pic.y, fade - mid, pic.h}, halfTop, kSheetTop, kSheetFoot, halfFoot);
        const uint32_t none = gfx::rgba(0.035f, 0.031f, 0.027f, 0.0f);
        const uint32_t dusk = gfx::rgba(0.035f, 0.031f, 0.027f, 0.55f);
        const uint32_t dark = gfx::rgba(0.035f, 0.031f, 0.027f, 0.92f);
        const float foot = pic.h * 0.46f;
        const float lower = pic.h * 0.22f;
        canvas_.shade({pic.x, pic.bottom() - foot, pic.w, foot - lower}, none, none, dusk, dusk);
        canvas_.shade({pic.x, pic.bottom() - lower, pic.w, lower}, dusk, dusk, dark, dark);
    }
    // Where he is in the slides, on glass in the picture's corner.
    {
        const std::string where = slide_ == 0 ? "Welcome" : std::to_string(slide_) + " / " + std::to_string(count - 1);
        const float size = 13.0f * u;
        const float wide = std::round(controls::labelWidth(size, where) + 20.0f * u);
        const Box chip{std::round(pic.x + 24.0f * u), std::round(pic.y + 22.0f * u), wide, std::round(26.0f * u)};
        roundBox(canvas_, chip, 4.0f * u, gfx::rgba(0.047f, 0.043f, 0.039f, 0.62f), gfx::rgba(0.047f, 0.043f, 0.039f, 0.62f));
        canvas_.outline(chip, edge, gfx::rgba(1.0f, 0.941f, 0.843f, 0.08f));
        controls::label(canvas_, chip.x + 10.0f * u, controls::middle(chip.y, chip.h, size), size,
                        gfx::rgba(0.812f, 0.776f, 0.722f), where);
    }
    // The kicker over the title, on the picture's foot: a short gold line, the word in gold small
    // capitals, and the title in Cinzel over its own shadow.
    {
        const float left = pic.x + 40.0f * u;
        const float titleBase = std::round(pic.bottom() - 36.0f * u - 8.0f * u);
        const float kickBase = std::round(titleBase - kTitleSize * u * 0.92f - 8.0f * u);
        const float kick = 16.0f * u;
        const float lineY = std::round(kickBase - kick * 0.35f);
        canvas_.shade({left, lineY, std::round(22.0f * u), std::max(1.0f, 2.0f * u)}, kGold,
                      gfx::rgba(0.886f, 0.741f, 0.447f, 0.0f), gfx::rgba(0.886f, 0.741f, 0.447f, 0.0f), kGold);
        controls::caps(canvas_, left + 32.0f * u, kickBase, kick, kGold, slide.kicker, 0.06f);
        const gfx::Face* face = panel::titleFace();
        if (face != nullptr) {
            const bgfx::TextureHandle texture = panel::titleTexture();
            canvas_.lettered(*face, texture, left, titleBase + 2.0f * u, kTitleSize * u, 0.0f,
                             gfx::rgba(0.0f, 0.0f, 0.0f, 0.65f), slide.title);
            canvas_.lettered(*face, texture, left, titleBase, kTitleSize * u, 0.0f,
                             gfx::rgba(0.965f, 0.937f, 0.886f), slide.title);
        }
    }

    // The column's head: What's New in Cinzel with a quiet line beside it, and Skip on the right.
    const float col = x + kColX * u;
    {
        const float base = y + 40.0f * u;
        float after = col;
        const gfx::Face* face = panel::titleFace();
        if (face != nullptr) {
            const float size = 21.0f * u;
            canvas_.lettered(*face, panel::titleTexture(), col, base, size, 0.0f, gfx::rgba(0.937f, 0.906f, 0.847f),
                             "What's New");
            after = col + face->measure(size, "What's New");
        }
        controls::label(canvas_, after + 12.0f * u, base, 14.0f * u, gfx::rgba(0.522f, 0.490f, 0.447f),
                        "since you last played");
        const uint32_t ink = mix(gfx::rgba(0.604f, 0.569f, 0.518f), gfx::rgba(0.910f, 0.886f, 0.847f), lift(kSkip));
        word(canvas_, placed(x, y, skipBox(), u), 15.0f * u, ink, "Skip");
    }

    // The topics: one recessed bar, the chosen tab lifted in gold.
    {
        const Box bar = placed(x, y, {kColX, kBarTop, kColWide, kBarTall}, u);
        roundBox(canvas_, bar, 5.0f * u, gfx::rgba(0.047f, 0.039f, 0.035f), gfx::rgba(0.067f, 0.055f, 0.051f));
        canvas_.rect({bar.x + 3.0f * u, bar.y, bar.w - 6.0f * u, edge}, gfx::rgba(0.0f, 0.0f, 0.0f, 0.5f));
        canvas_.rect({bar.x + 3.0f * u, bar.bottom(), bar.w - 6.0f * u, edge}, gfx::rgba(1.0f, 0.941f, 0.843f, 0.04f));
        for (int i = 0; i < kTabs; ++i) {
            const Box tab = placed(x, y, tabBox(i), u);
            const bool chosen = slide_ == i + 1;
            if (chosen) {
                tip::shadowUnder(canvas_, tab, u, 3.0f * u, 0.5f);
                roundBox(canvas_, tab, 3.0f * u, gfx::rgba(0.231f, 0.188f, 0.114f), gfx::rgba(0.165f, 0.133f, 0.086f));
                canvas_.outline(tab, edge, gfx::rgba(0.886f, 0.741f, 0.447f, 0.38f));
                canvas_.rect({tab.x + 3.0f * u, tab.y + edge, tab.w - 6.0f * u, edge}, gfx::rgba(1.0f, 0.882f, 0.627f, 0.12f));
            }
            const uint32_t ink = chosen ? gfx::rgba(0.937f, 0.824f, 0.561f)
                                        : mix(gfx::rgba(0.604f, 0.569f, 0.518f), gfx::rgba(0.937f, 0.902f, 0.839f), lift(i));
            word(canvas_, tab, 13.0f * u, ink, kTabNames[i]);
        }
    }

    // The prose, then the facts on a lit panel, then a note under it.
    const std::vector<std::string> lead = wrap(slide.lead, kColWide, kLeadSize);
    for (size_t i = 0; i < lead.size(); ++i) {
        const float base = y + (bodyTop(slide) + kLeadStep * float(i)) * u + kLeadSize * u * 0.95f;
        controls::label(canvas_, col, base, kLeadSize * u, kLead, lead[i]);
    }
    const bool topics = slide_ == 0;
    const int rows = int(slide.rows.size());
    const float tall = rowTall(slide, topics);
    const float top = wellTop(slide);
    {
        const Box panelBox = placed(x, y, {kColX, top, kColWide, tall * float(rows) + kWellPad * 2.0f}, u);
        roundBox(canvas_, panelBox, 5.0f * u, gfx::rgba(1.0f, 0.957f, 0.882f, 0.035f), gfx::rgba(1.0f, 0.957f, 0.882f, 0.012f));
        canvas_.outline(panelBox, edge, gfx::rgba(1.0f, 0.941f, 0.843f, 0.06f));
        canvas_.rect({panelBox.x + 5.0f * u, panelBox.y + edge, panelBox.w - 10.0f * u, edge},
                     gfx::rgba(1.0f, 0.941f, 0.843f, 0.05f));
    }
    const float nameSize = (slide.tight ? 15.0f : 16.0f) * u, valueSize = (slide.tight ? 14.0f : 15.0f) * u;
    const float inset = kRowInset * u;
    for (int i = 0; i < rows; ++i) {
        const Row& row = slide.rows[size_t(i)];
        const Box box = placed(x, y, rowBox(slide, topics, i), u);
        if (i > 0) canvas_.rect({box.x + inset, box.y, box.w - inset * 2.0f, edge}, kHair);
        float nameX = box.x + inset;
        if (row.dot != 0) {
            const float side = std::round(9.0f * u);
            const Box dot{nameX, std::round(box.y + (box.h - side) * 0.5f), side, side};
            // A soft glow in its own colour, two rings fading out, so it reads as light, not a rim.
            for (const auto& [grow, alpha] : {std::pair<float, uint32_t>{5.0f, 0x14000000u}, {2.5f, 0x2C000000u}}) {
                const float g = grow * u;
                const uint32_t glow = (row.dot & 0x00FFFFFFu) | alpha;
                roundBox(canvas_, {dot.x - g, dot.y - g, dot.w + g * 2.0f, dot.h + g * 2.0f}, side * 0.5f + g, glow, glow);
            }
            roundBox(canvas_, dot, side * 0.5f, row.dot, row.dot);
            nameX += side + 10.0f * u;
        }
        const float t = topics ? lift(kTopic + i) : 0.0f;
        controls::label(canvas_, nameX, controls::middle(box.y, box.h, nameSize), nameSize, mix(kName, kGoldLit, t), row.name);
        float right = box.right() - inset;
        if (topics) {
            const float side = 10.0f * u;
            controls::chevron(canvas_, {right - side, box.y + (box.h - side) * 0.5f, side, side}, true, t, u);
            right -= side + 12.0f * u;
        }
        controls::ranged(canvas_, right, controls::middle(box.y, box.h, valueSize), valueSize, kValue, row.value);
    }
    if (slide.note[0] != '\0') {
        const float noteTop = top + tall * float(rows) + kWellPad * 2.0f + 14.0f;
        const float size = 15.0f * u;
        const std::vector<std::string> note = wrap(slide.note, kColWide, 15.0f);
        for (size_t i = 0; i < note.size(); ++i) {
            controls::label(canvas_, col, y + (noteTop + 21.0f * float(i)) * u + size * 0.95f, size, kQuiet, note[i]);
        }
    }

    // The foot: the gold progress line, the count in small capitals, Back and Next.
    {
        const Box track = placed(x, y, {kColX, kProgressTop, kColWide, 2.0f}, u);
        const float thick = std::max(1.0f, track.h);
        canvas_.rect({track.x, track.y, track.w, thick}, gfx::rgba(1.0f, 0.941f, 0.843f, 0.06f));
        const float share = float(slide_) / float(std::max(1, count - 1));
        if (share > 0.0f) {
            const Box fill{track.x, track.y, std::round(track.w * share), thick};
            canvas_.rect({fill.x, fill.y - thick, fill.w, thick * 3.0f}, gfx::rgba(0.886f, 0.741f, 0.447f, 0.12f));
            const uint32_t faint = gfx::rgba(0.886f, 0.741f, 0.447f, 0.35f);
            canvas_.shade(fill, faint, kGold, kGold, faint);
        }
        const Box next = placed(x, y, nextBox(), u);
        const float size = 16.0f * u;
        const std::string counted =
            slide_ == 0 ? std::to_string(count - 2) + " new things and more" : std::to_string(slide_) + " of " + std::to_string(count - 1);
        controls::caps(canvas_, col, controls::middle(next.y, next.h, size), size, gfx::rgba(0.659f, 0.624f, 0.569f),
                       counted, 0.02f);
        if (slide_ > 0) {
            const Box back = placed(x, y, backBox(), u);
            const float t = lift(kBack);
            roundBox(canvas_, back, 3.0f * u, mix(gfx::rgba(0.169f, 0.153f, 0.141f), gfx::rgba(0.212f, 0.192f, 0.176f), t),
                  mix(gfx::rgba(0.129f, 0.118f, 0.106f), gfx::rgba(0.161f, 0.145f, 0.133f), t));
            canvas_.rect({back.x + 3.0f * u, back.y, back.w - 6.0f * u, edge}, gfx::rgba(1.0f, 0.941f, 0.863f, 0.06f));
            word(canvas_, back, 17.0f * u, gfx::rgba(0.867f, 0.835f, 0.784f), "Back");
        }
        const float t = lift(kNext);
        tip::shadowUnder(canvas_, next, u, 3.0f * u, 0.8f);
        roundBox(canvas_, next, 3.0f * u, mix(gfx::rgba(0.702f, 0.157f, 0.165f), gfx::rgba(0.788f, 0.192f, 0.192f), t),
              mix(gfx::rgba(0.455f, 0.078f, 0.082f), gfx::rgba(0.525f, 0.098f, 0.098f), t));
        canvas_.rect({next.x + 3.0f * u, next.y, next.w - 6.0f * u, edge}, gfx::rgba(1.0f, 0.824f, 0.745f, 0.28f));
        const char* label = slide_ == 0 ? "Show me" : slide_ == count - 1 ? "Start playing" : "Next";
        word(canvas_, next, 17.0f * u, gfx::rgba(1.0f, 0.961f, 0.925f), label);
    }
}

}  // namespace mu::game
