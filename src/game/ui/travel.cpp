#include "game/ui/travel.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "content/tables.h"
#include "core/files.h"
#include "core/log.h"
#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"
#include "game/world/maps.h"
#include "sim/event.h"
#include "sim/market.h"
#include "sim/quests.h"

namespace mu::game {
namespace {

using gfx::Box;

// The proposal's measures, in tip::unit() at 1080 lines.
constexpr float kLeft = 24.0f;    // the column's left edge; it stands in the screen's middle
constexpr float kTop = 24.0f;     // and never higher than this
constexpr float kWide = 252.0f;   // a button
constexpr float kTall = 56.0f;
constexpr float kGap = 6.0f;      // between buttons
constexpr float kRuleGap = 7.0f;  // either side of the rule over the maps he cannot use
constexpr float kPadX = 10.0f;
constexpr float kLine1 = 7.0f;    // the two lines' tops inside a button, each 20 tall
constexpr float kLine2 = 29.0f;
constexpr float kLineTall = 20.0f;
constexpr float kNameSize = 16.0f;
constexpr float kLevelSize = 13.0f;
constexpr float kChipSize = 11.5f;
constexpr float kScrim = 360.0f;  // how far in from the left edge the dark reaches

// **Gold for the map he is on**, on the user's word (2026-09-30), though style.h keeps gold for
// loot: the quest marker's own old gold (game/ui/beacon.cpp), so it reads as the giver's.
constexpr uint32_t kGold = gfx::rgba(0.722f, 0.600f, 0.353f);
constexpr uint32_t kGoldHi = gfx::rgba(0.886f, 0.776f, 0.541f);
constexpr uint32_t kGoldDk = gfx::rgba(0.431f, 0.353f, 0.196f);
// A repeat's marks: the beacon's blue (game/ui/beacon.cpp), for a quest cleared once already.
constexpr uint32_t kAgainHi = gfx::rgba(0.56f, 0.80f, 1.00f);
// **The events and when they run**: OpenMU's default timetables, every period from midnight, the
// local clock (`OM/GameLogic/PlugIns/PeriodicTasks/BloodCastleStartConfiguration.cs:21-22`,
// `DevilSquareStartConfiguration.cs:21-22`), each on the map whose gatekeeper lets you in --
// the Messenger of Archangel in Devias, Charon in Noria. A card shows its map's event from half
// an hour before it starts until it ends, in place of the monsters' levels (the user,
// 2026-09-30, option A of the proposal). Neither event is open yet; only the countdown is.
// Blood Castle's is its own (sim/event.h): every hour from hh:25, and "now" while the Messenger
// lets him in (the user, 2026-10-02: 'every 1 hour BC is opened').
struct Event {
    const char* name;
    int32_t map;
    int period;  // seconds between starts
    int length;  // and how long one runs
    int offset = 0;  // the first start, seconds after midnight
};
constexpr Event kEvents[] = {
    {"Blood Castle", 2, sim::kCastlePeriod, sim::kCastleEntry, sim::kCastleOpensAt},
    {"Devil Square", 3, 14400, 1500}};
constexpr int kSoonSeconds = 1800;

// A small dot, drawn as a fan: the canvas has no circle.
void dot(gfx::Canvas& canvas, float cx, float cy, float r, uint32_t ink) {
    constexpr int kSteps = 12;
    float xy[kSteps * 2];
    for (int i = 0; i < kSteps; ++i) {
        const float a = 6.28318531f * float(i) / float(kSteps);
        xy[i * 2] = cx + std::cos(a) * r;
        xy[i * 2 + 1] = cy + std::sin(a) * r;
    }
    canvas.polygon(nullptr, xy, nullptr, kSteps, ink);
}

// A quest on offer, beside sim::QuestState's own Active and Ready in Drawn::quests.
constexpr uint8_t kOffered = 10;

// A tick `size` tall centred on (cx, cy), two strokes as quads: the canvas has no glyph for it.
void tick(gfx::Canvas& canvas, float cx, float cy, float size, uint32_t ink) {
    const float t = std::max(1.5f, size * 0.17f) * 0.5f;
    const float ax = cx - size * 0.42f, ay = cy + size * 0.02f;
    const float bx = cx - size * 0.12f, by = cy + size * 0.32f;
    const float qx = cx + size * 0.44f, qy = cy - size * 0.36f;
    const auto stroke = [&](float x0, float y0, float x1, float y1) {
        const float dx = x1 - x0, dy = y1 - y0, n = std::sqrt(dx * dx + dy * dy);
        const float nx = -dy / n * t, ny = dx / n * t;
        const float xy[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
        canvas.polygon(nullptr, xy, nullptr, 4, ink);
    };
    stroke(ax, ay, bx, by + t * 0.7f);
    stroke(bx, by, qx, qy);
}

uint32_t alpha(uint32_t abgr, float a) {
    return (abgr & 0x00FFFFFFu) | (gfx::rgbaByte(a * float(abgr >> 24) / 255.0f) << 24);
}

// A padlock `size` tall centred on (cx, cy): a shackle's arch over a solid body with a keyhole.
void padlock(gfx::Canvas& canvas, float cx, float cy, float size, uint32_t ink) {
    const float bodyW = size * 0.62f, bodyH = size * 0.46f;
    const float bodyY = cy - size * 0.5f + size * 0.44f;
    const float stroke = std::max(1.0f, std::round(size * 0.11f));
    const float shackleW = size * 0.40f;
    canvas.outline({cx - shackleW * 0.5f, cy - size * 0.5f, shackleW, size * 0.5f}, stroke, ink);
    canvas.rect({cx - bodyW * 0.5f, bodyY, bodyW, bodyH}, ink);
    const float hole = std::max(1.0f, std::round(size * 0.10f));
    canvas.rect({cx - hole * 0.5f, bodyY + bodyH * 0.30f, hole, bodyH * 0.40f},
                gfx::rgba(0.020f, 0.012f, 0.012f, 0.9f));
}

// Two packed colours mixed, `t` of the way from `a` to `b`.
uint32_t mixed(uint32_t a, uint32_t b, float t) {
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float x = float((a >> shift) & 0xFFu), y = float((b >> shift) & 0xFFu);
        out |= uint32_t(std::lround(x + (y - x) * t)) << shift;
    }
    return out;
}

// A ring `thick` wide inside radius `r`, as the strip of quads between two circles.
void hoop(gfx::Canvas& canvas, float cx, float cy, float r, float thick, uint32_t ink) {
    constexpr int kSteps = 24;
    const uint32_t inks[4] = {ink, ink, ink, ink};
    const float in = std::max(0.0f, r - thick);
    for (int i = 0; i < kSteps; ++i) {
        const float a0 = 6.28318531f * float(i) / float(kSteps);
        const float a1 = 6.28318531f * float(i + 1) / float(kSteps);
        const float quad[8] = {cx + std::cos(a0) * r,  cy + std::sin(a0) * r,
                               cx + std::cos(a1) * r,  cy + std::sin(a1) * r,
                               cx + std::cos(a1) * in, cy + std::sin(a1) * in,
                               cx + std::cos(a0) * in, cy + std::sin(a0) * in};
        canvas.polygon(quad, inks, 4);
    }
}

// A word set with its ink's middle on `mid` -- the glyph `ref` standing for its height, as the
// herald sets its line (game/ui/herald.cpp) -- over its drop. Returns its width.
float inked(gfx::Canvas& canvas, const gfx::Face* face, bgfx::TextureHandle texture, float x,
            float mid, float size, char ref, uint32_t ink, const std::string& text) {
    if (!face || text.empty()) return 0.0f;
    const gfx::FaceGlyph* g = face->glyph(ref);
    const float tall = g ? -g->y0 * face->emScale(size) : size * 0.7f;
    const float base = std::round(mid + tall * 0.5f);
    x = std::round(x);
    canvas.lettered(*face, texture, x + 1.0f, base + 1.0f, size, 0.0f, style::kDrop, text);
    return canvas.lettered(*face, texture, x, base, size, 0.0f, ink, text);
}
float inkedWidth(const gfx::Face* face, float size, const std::string& text) {
    return face && !text.empty() ? face->measure(size, text) : 0.0f;
}

std::string levels(int low, int high) {
    if (high == 0) return {};
    return low == high ? "Lv " + std::to_string(low)
                       : "Lv " + std::to_string(low) + "-" + std::to_string(high);
}

}  // namespace

bool Travel::Drawn::operator==(const Drawn& o) const {
    if (width != o.width || height != o.height || here != o.here || floor != o.floor ||
        hoverPlace != o.hoverPlace || hoverFloor != o.hoverFloor) {
        return false;
    }
    for (int i = 0; i < sim::kTravels; ++i) {
        if (refusals[i] != o.refusals[i] || lockedBy[i] != o.lockedBy[i]) return false;
    }
    for (int i = 0; i < sim::kQuests; ++i) {
        if (quests[i] != o.quests[i] || shares[i] != o.shares[i] || again[i] != o.again[i] ||
            restMinutes[i] != o.restMinutes[i]) {
            return false;
        }
    }
    for (int i = 0; i < kPlaces; ++i) {
        if (events[i] != o.events[i] || eventSeconds[i] != o.eventSeconds[i]) return false;
    }
    return pulse == o.pulse;
}

void Travel::open(const gfx::Interface& interface, const std::string& assetDir) {
    interface.adopt(canvas_);
    places_.clear();
    for (int i = 0; i < sim::kTravels; ++i) {
        const sim::TravelRow& row = sim::travelAt(i);
        const MapRow* map = mapNumbered(int(row.map));
        if (map == nullptr) continue;  // only the worlds this game has
        Place* place = nullptr;
        for (Place& one : places_) {
            if (one.map == row.map) place = &one;
        }
        if (place != nullptr) {
            place->rows.push_back(i);
            continue;
        }
        places_.push_back({});
        place = &places_.back();
        place->name = row.name;
        place->map = row.map;
        place->rows.push_back(i);
        // What the button says about the map, from its own cooked rules.
        content::Tables tables;
        std::string error;
        const std::string path =
            core::join(assetDir, std::string("cooked/") + map->world + "/" + map->world + ".mur");
        if (!content::loadTables(path, tables, error)) {
            core::logError("travel: %s", error.c_str());
            continue;
        }
        for (const content::MonsterNest& nest : tables.nests) {
            if (nest.kind >= tables.kinds.size()) continue;
            const int level = tables.kinds[nest.kind].level;
            place->low = place->low == 0 ? level : std::min(place->low, level);
            place->high = std::max(place->high, level);
        }
        for (const content::Townsperson& one : tables.folk) {
            // The map's first giver is its quest: Lorencia's is Marlon's, not the Golden
            // Archer's chain beside it.
            if (sim::questOf(one.number) >= 0 && !place->giver) {
                place->giver = true;
                place->quest = sim::questOf(one.number);
            }
            if (sim::sells(one.number)) place->merchant = true;
        }
    }
    // And the worlds MU has that this game does not yet, by name alone: always under the rule,
    // dimmed, never a trip (the user, 2026-09-30: "show all maps but not completed maps are
    // disabled"). MU's map numbers, in the order a character comes to them.
    static const struct {
        const char* name;
        int32_t map;
    } kComing[] = {{"Lost Tower", 4}, {"Atlans", 7}, {"Tarkan", 8}, {"Icarus", 10}};
    for (const auto& one : kComing) {
        if (mapNumbered(int(one.map)) != nullptr) continue;
        places_.push_back({});
        places_.back().name = one.name;
        places_.back().map = one.map;
    }
    built_ = false;
}

void Travel::close() {
    canvas_.clear();
    places_.clear();
    hits_.clear();
    up_ = false;
    built_ = false;
}

bool Travel::covers(float x, float y) const { return up_ && column_.has(x, y); }

int Travel::update(const Play& play, const Pointer& pointer, int width, int height) {
    const float u = tip::unit();
    const sim::Realm& realm = play.realm();
    Drawn now;
    now.width = width;
    now.height = height;
    now.here = int32_t(realm.tables()->map);
    now.floor = realm.travelFloor();
    // The events, off the wall clock the desk hands the realm; a run that never sets it has none.
    if (const int64_t wall = realm.wallClock(); wall > 0) {
        const time_t at = time_t(wall);
        struct tm local {};
        localtime_r(&at, &local);
        const int day = local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec;
        for (size_t p = 0; p < places_.size() && p < size_t(kPlaces); ++p) {
            for (const Event& one : kEvents) {
                if (one.map != places_[p].map) continue;
                const int phase = ((day - one.offset) % one.period + one.period) % one.period;
                if (phase < one.length) {
                    now.events[p] = 2;
                    now.eventSeconds[p] = one.length - phase;
                } else if (one.period - phase <= kSoonSeconds) {
                    now.events[p] = 1;
                    now.eventSeconds[p] = one.period - phase;
                }
            }
        }
    }
    // An open gate's dot breathes: the card is drawn again a breath's 36th at a time while one is.
    for (int p = 0; p < kPlaces; ++p) {
        if (now.events[p] != 2) continue;
        const double t = std::chrono::duration<double>(
                              std::chrono::steady_clock::now().time_since_epoch()).count();
        now.pulse = int(std::fmod(t, double(style::kLiveSeconds)) / style::kLiveSeconds * 36.0);
        break;
    }
    for (int i = 0; i < sim::kTravels; ++i) {
        now.refusals[i] = uint8_t(realm.travelRefusal(i));
        now.lockedBy[i] = int8_t(now.refusals[i] == uint8_t(sim::TravelRefusal::Quest)
                                     ? realm.travelQuest(i)
                                     : -1);
    }
    // The quests under way, and how much of their hunting is done: every Clear step's count over
    // its goal, summed.
    for (int q = 0; q < sim::kQuests; ++q) {
        const sim::QuestProgress& one = realm.quest(q);
        now.again[q] = one.completions > 0;
        if (realm.questOffered(q)) {
            now.quests[q] = kOffered;
            continue;
        }
        if (one.state == sim::QuestState::Resting) {
            // Handed in and resting: done, and the minutes until it is offered again.
            now.quests[q] = uint8_t(sim::QuestState::Resting);
            const int64_t wall = realm.wallClock();
            now.restMinutes[q] = wall > 0 && one.availableAt > wall
                                     ? int((one.availableAt - wall + 59) / 60)
                                     : 0;
            continue;
        }
        if (one.state != sim::QuestState::Active && one.state != sim::QuestState::Ready) continue;
        now.quests[q] = uint8_t(one.state);
        int done = 0, goal = 0;
        for (int step = 0; step < sim::questAt(q).stepCount; ++step) {
            if (!sim::questCounted(sim::questAt(q).steps[step].kind)) continue;
            const int g = realm.questGoal(q, step);
            goal += g;
            done += std::min<int>(one.counts[step], g);
        }
        now.shares[q] = uint8_t(goal > 0 ? std::min(100, done * 100 / goal) : 0);
    }

    // The layout, every frame (a handful of boxes): the maps he can use first, in the list's own
    // order, then under the rule the ones he cannot.
    hits_.clear();
    const auto usable = [&](const Place& place) {
        if (place.map == now.here) return true;
        for (int row : place.rows) {
            if (now.refusals[row] == uint8_t(sim::TravelRefusal::None)) return true;
        }
        return false;
    };
    float y = kTop * u;
    const float x = kLeft * u;
    bool ruled = false;
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t p = 0; p < places_.size(); ++p) {
            if (usable(places_[p]) != (pass == 0)) continue;
            if (pass == 1 && !ruled) {
                ruled = y > kTop * u;
                if (ruled) y += (kRuleGap * 2.0f - kGap) * u + 1.0f;
            }
            Hit hit;
            hit.place = int(p);
            hit.usable = pass == 0;
            hit.box = {std::round(x), std::round(y), std::round(kWide * u), std::round(kTall * u)};
            const Place& place = places_[p];
            if (hit.usable && place.rows.size() > 1) {
                float cx = hit.box.x + kPadX * u;
                for (int row : place.rows) {
                    // The floor's number, not its level (the user, 2026-09-30); a square chip.
                    const float w = std::round(kLineTall * u + 4.0f * u);
                    hit.chip[hit.floors] = {cx, std::round(hit.box.y + kLine2 * u), w,
                                            std::round(kLineTall * u)};
                    hit.chipRow[hit.floors] = row;
                    ++hit.floors;
                    cx += w + 5.0f * u;
                }
            }
            hits_.push_back(hit);
            y += (kTall + kGap) * u;
        }
    }
    // The column stands in the middle of the screen's height (the user, 2026-09-30).
    const float tall = y - kGap * u - kTop * u;
    const float shift = std::round(std::max(0.0f, (float(height) - tall) * 0.5f - kTop * u));
    for (Hit& hit : hits_) {
        hit.box.y += shift;
        for (int f = 0; f < hit.floors; ++f) hit.chip[f].y += shift;
    }
    column_ = hits_.empty() ? Box{} : Box{x, kTop * u + shift, kWide * u, tall};

    // What the pointer is over, and what a press there asks for.
    int asked = -1;
    for (const Hit& hit : hits_) {
        if (!hit.usable || !hit.box.has(pointer.x, pointer.y)) continue;
        now.hoverPlace = hit.place;
        for (int f = 0; f < hit.floors; ++f) {
            if (hit.chip[f].has(pointer.x, pointer.y)) now.hoverFloor = f;
        }
        const Place& place = places_[size_t(hit.place)];
        if (!pointer.pressed) continue;
        if (now.hoverFloor >= 0) {
            asked = hit.chipRow[now.hoverFloor];
        } else {
            for (int row : place.rows) {
                if (asked < 0 && now.refusals[row] == uint8_t(sim::TravelRefusal::None)) asked = row;
            }
        }
    }
    if (!built_ || !(now == drawn_)) {
        drawn_ = now;
        built_ = true;
        rebuild(now);
    }
    return asked;
}

void Travel::rebuild(const Drawn& now) {
    canvas_.clear();
    const float u = tip::unit();
    const float line = std::max(1.0f, std::round(u));
    const float r = style::kRadiusControl * u;

    // A soft dark behind the column alone, and no sheet: strongest at the screen's edge beside
    // the buttons, fading out rightward past them and above and below them.
    {
        const float s = kScrim * u, feather = 72.0f * u;
        const float xs[4] = {0.0f, s * 0.50f, s * 0.78f, s};
        const float ax[4] = {0.42f, 0.30f, 0.10f, 0.0f};
        const float ys[4] = {column_.y - feather, column_.y, column_.bottom(), column_.bottom() + feather};
        const float ay[4] = {0.0f, 1.0f, 1.0f, 0.0f};
        const auto dark = [](float a) { return gfx::rgba(0.020f, 0.012f, 0.012f, a); };
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                canvas_.shade({xs[i], ys[j], xs[i + 1] - xs[i], ys[j + 1] - ys[j]},
                              dark(ax[i] * ay[j]), dark(ax[i + 1] * ay[j]),
                              dark(ax[i + 1] * ay[j + 1]), dark(ax[i] * ay[j + 1]));
            }
        }
    }

    bool ruled = false;
    for (const Hit& hit : hits_) {
        const Place& place = places_[size_t(hit.place)];
        const bool here = place.map == now.here;
        // The gold one lifts only where a press takes him somewhere: not from its own safe zone.
        bool open = place.map != now.here;
        for (int row : place.rows) {
            if (now.refusals[row] == uint8_t(sim::TravelRefusal::None)) open = true;
        }
        const bool over = hit.place == now.hoverPlace && open;
        Box b = hit.box;
        if (!hit.usable && !ruled) {
            ruled = true;
            if (&hit != &hits_.front()) {
                controls::rule(canvas_, b.x, b.y - kRuleGap * u - 1.0f, b.w, u);
            }
        }
        if (!hit.usable) {
            // Out of reach: the name alone, dimmed, in a bare rim with no fill (the user, 2026-09-30).
            canvas_.outline(b, line, alpha(style::kIronLo, 0.7f));
            controls::caps(canvas_, b.x + kPadX * u, controls::middle(b.y, b.h, kNameSize * u),
                           kNameSize * u, alpha(style::kAshInk, 0.8f), place.name, 0.0f);
            continue;
        }
        if (over) b.y -= line;
        tip::shadowUnder(canvas_, b, u * (over ? 0.7f : 0.45f), r);
        const uint32_t rim = here ? kGoldDk : over ? style::kIronHi : style::kIronLo;
        tip::panel(canvas_, b, r, rim, rim);
        const Box in = b.grown(-line);
        if (over) tip::panel(canvas_, in, r - line, style::kAsh4, style::kAsh2);
        else tip::panel(canvas_, in, r - line, style::kAsh3, style::kAsh1);
        if (here) {
            // The gold: a wash from the left, a stripe down the edge.
            canvas_.shade({in.x, in.y, in.w * 0.75f, in.h}, alpha(kGold, 0.18f), alpha(kGold, 0.0f),
                          alpha(kGold, 0.0f), alpha(kGold, 0.18f));
            canvas_.rect({b.x, b.y + 8.0f * u, std::round(2.0f * u), b.h - 16.0f * u}, kGold);
        }

        // First line: the name, and the monsters' levels ranged right.
        const float nameBase = controls::middle(b.y + kLine1 * u, kLineTall * u, kNameSize * u);
        controls::caps(canvas_, b.x + kPadX * u, nameBase, kNameSize * u,
                       here ? kGoldHi : style::kBoneHi, place.name, 0.0f);
        const std::string lv = levels(place.low, place.high);
        const size_t p = size_t(hit.place);
        if (p < size_t(kPlaces) && now.events[p] != 0) {
            // Its event, near or on, where the levels were, in the herald's words (game/ui/
            // herald.h): the name and its clock in gold counting down to the start; while it is
            // on, a breathing green dot, the name, "gate open" and the time left to enter. Every
            // word centred on its own ink.
            const Event* event = nullptr;
            for (const Event& one : kEvents) {
                if (one.map == place.map) event = &one;
            }
            const bool on = now.events[p] == 2;
            const int s = now.eventSeconds[p];
            char when[16];
            std::snprintf(when, sizeof(when), "%d:%02d", s / 60, s % 60);
            const std::string name = event ? event->name : "";
            const char* const state = "gate open";
            const float nameSize = 12.5f * u, stateSize = 13.0f * u, clockSize = 13.0f * u;
            const float gap = 7.0f * u, r = 3.2f * u;
            const gfx::Face* label = controls::labelFace();
            const gfx::Face* word = controls::wordFace();
            float wide = inkedWidth(label, nameSize, name) + gap + inkedWidth(label, clockSize, when);
            if (on) wide += r * 2.0f + gap + inkedWidth(word, stateSize, state) + gap;
            const float mid = b.y + (kLine1 + kLineTall * 0.5f) * u;
            float x = b.right() - kPadX * u - wide;
            if (on) {
                const float t = float(now.pulse) / 36.0f;
                const float breath = 0.5f - 0.5f * std::cos(6.28318531f * t);
                const float wave = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
                const float cx = x + r;
                hoop(canvas_, cx, mid, r * 2.0f * (0.6f + 1.2f * wave), std::max(1.0f, u),
                     alpha(style::kLive, 0.75f * (1.0f - wave)));
                dot(canvas_, cx, mid, r * 2.2f, alpha(style::kLive, 0.10f + 0.12f * breath));
                dot(canvas_, cx, mid, r * 1.5f, alpha(style::kLive, 0.18f + 0.18f * breath));
                dot(canvas_, cx, mid, r, mixed(style::kLive, style::kLiveHi, breath));
                x += r * 2.0f + gap;
            }
            x += inked(canvas_, label, controls::labelTexture(), x, mid, nameSize, 'H',
                       style::kBone2, name) + gap;
            if (on) {
                x += inked(canvas_, word, controls::wordTexture(), x, mid, stateSize, 'x', kGoldHi,
                           state) + gap;
            }
            inked(canvas_, label, controls::labelTexture(), x, mid, clockSize, '0', kGoldHi, when);
        } else if (!lv.empty()) {
            controls::ranged(canvas_, b.right() - kPadX * u,
                             controls::middle(b.y + kLine1 * u, kLineTall * u, kLevelSize * u),
                             kLevelSize * u, style::kAshInk, lv);
        }

        // Second line: a dungeon's floors, else its giver's quest.
        if (hit.floors > 0) {
            for (int f = 0; f < hit.floors; ++f) {
                Box chip = hit.chip[f];
                if (over) chip.y -= line;
                const int row = hit.chipRow[f];
                const bool open = now.refusals[row] == uint8_t(sim::TravelRefusal::None);
                const bool lit = open && hit.place == now.hoverPlace && f == now.hoverFloor;
                // The floor he stands on is chosen, in the map's own gold (the user, 2026-09-30).
                const bool on = row == now.floor;
                const uint32_t fill = on ? kGoldDk : !open ? style::kAsh2 : lit ? style::kBloodMd : style::kAsh4;
                tip::panel(canvas_, chip, style::kRadiusSmall * u, fill, fill);
                // A floor its quest has not opened wears a padlock in place of its number, lit
                // under the pointer, where the tip below says which quest opens it (ours).
                if (now.lockedBy[row] >= 0) {
                    const bool hovered = hit.place == now.hoverPlace && f == now.hoverFloor;
                    padlock(canvas_, chip.midX(), chip.midY(), kChipSize * u,
                            hovered ? kGold : style::kAshInk);
                    continue;
                }
                const std::string word = std::to_string(f + 1);
                controls::label(canvas_,
                                std::round(chip.midX() - controls::labelWidth(kChipSize * u, word) * 0.5f),
                                controls::middle(chip.y, chip.h, kChipSize * u), kChipSize * u,
                                on ? kGoldHi : !open ? style::kAshInk2 : lit ? style::kBoneHi : style::kBone2,
                                word);
            }
        } else {
            // His quest: WoW's marks -- a gold "!" on offer, a grey "?" under way, a gold "?" to hand
            // in -- then its title, and at the right how far along or that it waits (the user,
            // 2026-09-30, in place of the giver's and the merchants' marks).
            const int q = place.quest;
            if (q >= 0 && now.quests[q] != 0) {
                const float size = 12.5f * u, markSize = 17.0f * u;
                const float base = controls::middle(b.y + kLine2 * u, kLineTall * u, size);
                const bool offered = now.quests[q] == kOffered;
                const bool ready = now.quests[q] == uint8_t(sim::QuestState::Ready);
                // Handed in and resting until its twelve hours run: a tick in the repeat's blue,
                // and when it comes back (the user, 2026-10-02: 'if we finish the quest, show it').
                const bool done = now.quests[q] == uint8_t(sim::QuestState::Resting);
                const float x0 = b.x + kPadX * u;
                const uint32_t hi = now.again[q] ? kAgainHi : kGoldHi;
                if (done) {
                    tick(canvas_, x0 + 3.5f * u, b.y + (kLine2 + kLineTall * 0.5f) * u, 11.0f * u, kAgainHi);
                } else {
                    controls::caps(canvas_, x0, controls::middle(b.y + kLine2 * u, kLineTall * u, markSize),
                                   markSize, offered || ready ? hi : style::kAshInk,
                                   offered ? "!" : "?", 0.0f);
                }
                std::string right = offered ? "" : ready ? "Hand in"
                                                         : std::to_string(now.shares[q]) + "%";
                if (done) {
                    const int m = now.restMinutes[q];
                    right = "Done";
                    if (m >= 60) right += " \xC2\xB7 " + std::to_string(m / 60) + "h " + std::to_string(m % 60) + "m";
                    else if (m > 0) right += " \xC2\xB7 " + std::to_string(m) + "m";
                }
                const float rightW = right.empty() ? 0.0f : controls::labelWidth(size, right);
                if (!right.empty()) {
                    controls::ranged(canvas_, b.right() - kPadX * u, base, size,
                                     ready ? hi : done ? kAgainHi : style::kAshInk, right);
                }
                // The title, trimmed to the room left between the mark and the figure.
                const float tx = x0 + 12.0f * u;
                std::string title = sim::questAt(q).title;
                const float room = b.right() - kPadX * u - rightW - (right.empty() ? 0.0f : 8.0f * u) - tx;
                while (title.size() > 1 && controls::labelWidth(size, title) > room) {
                    title.pop_back();
                    while (!title.empty() && (uint8_t(title.back()) & 0xC0) == 0x80) title.pop_back();
                    if (controls::labelWidth(size, title + "...") <= room) {
                        title += "...";
                        break;
                    }
                }
                controls::label(canvas_, tx, base, size, done ? style::kAshInk : style::kBone2, title);
            }
        }
    }

    // And over a locked floor, what opens it: the Golden Archer's link for that floor (ours).
    for (const Hit& hit : hits_) {
        if (hit.place != now.hoverPlace || now.hoverFloor < 0 || now.hoverFloor >= hit.floors) continue;
        const int row = hit.chipRow[now.hoverFloor];
        const int q = now.lockedBy[row];
        if (q < 0) continue;
        const sim::QuestRow& quest = sim::questAt(q);
        tip::Sheet sheet;
        sheet.name = std::string(places_[size_t(hit.place)].name) + " " +
                     std::to_string(now.hoverFloor + 1);
        sheet.nameTone = tip::Tone::White;
        sheet.wide = 250.0f;
        tip::Section section;
        tip::Row locked;
        locked.free = "Locked";
        locked.freeTone = tip::Tone::Red;
        section.rows.push_back(locked);
        tip::Row how;
        // The Golden Archer's links lock a row until taken, and he is "the"; Tersia's until
        // handed in (Realm::travelQuest).
        how.free = quest.giver == sim::kTersia
                       ? std::string("Complete \"") + quest.title + "\" for " + quest.giverName +
                             " to travel here."
                       : std::string("Take \"") + quest.title + "\" from the " + quest.giverName +
                             " to travel here.";
        how.freeTone = tip::Tone::Yellow;
        section.rows.push_back(how);
        sheet.sections.push_back(section);
        tip::draw(canvas_, sheet, hit.chip[now.hoverFloor], float(now.width), float(now.height));
    }
}

}  // namespace mu::game
