#include "game/ui/tracker.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

#include "game/play.h"
#include "game/ui/controls.h"
#include "game/ui/panel.h"
#include "game/ui/style.h"
#include "game/ui/tip.h"

namespace mu::game {
namespace {

using gfx::Box;
using quest_marks::faded;
using quest_marks::StepMark;

// The proposal's measures, in tip::unit() at 1080 lines.
constexpr float kWide = 250.0f;  // 300 until 2026-10-03, "reduce the width of quest tasks window"
// And everything in it a size down, the anchors kept: "scale it down little bit" (the same day).
constexpr float kScale = 0.9f;
constexpr float kRight = 44.0f;   // the right edge, in from the screen's
constexpr float kTop = 250.0f;   // under the minimap, which stands over it (game/ui/minimap.h)
constexpr float kTitle = 18.0f, kTitleTrack = 0.08f;
constexpr float kStep = 16.0f;    // a step's words
constexpr float kRowGap = 16.0f;  // between rows
constexpr float kBarGap = 9.0f;   // a count's bar under its row
// And a step that is not a count -- the walk back to the giver -- stands apart from the kills.
constexpr float kTurnInGap = 8.0f;
constexpr float kRuleGap = 16.0f;  // from the title's line to the first row
constexpr float kMarkRoom = 24.0f;
constexpr float kScrimWide = 260.0f;
constexpr float kScrimAlpha = 0.55f;
// The moments, in seconds.
constexpr float kFadeSeconds = style::kOpenSeconds;  // in and out of a window's way
constexpr float kBarSeconds = 0.25f;
constexpr float kEmberSeconds = 0.6f;
constexpr float kBannerIn = style::kOpenSeconds, kBannerOut = 0.4f;
// Awake: how long a kill holds the tracker up, how slowly it then goes, and the steps not just
// counted. The user, 2026-09-29: fade out when nothing is killed, the killed row alone at full.
constexpr float kWakeHold = 5.0f, kWakeFade = 0.8f;
constexpr float kDim = 0.45f;
// Struck off, in seconds from the count reaching its goal: the bar has filled by kFlareAt, a
// light runs along the row and the words turn gold and are struck through, it holds, and from
// kFoldAt the row fades and folds away, gone at kStruckSeconds.
constexpr float kFlareAt = 0.2f, kFlareSeconds = 0.5f;
constexpr float kFoldAt = 1.3f, kStruckSeconds = 1.8f;
// The edge pointer: how far in from the frame's edge it stands, and when the giver counts as
// on the frame (the blade over his head takes over).
constexpr float kEdgeInset = 64.0f;
// The pointer's old gold, the quest marker's own two faces (game/ui/beacon.cpp).
constexpr uint32_t kGoldLit = gfx::rgba(0.784f, 0.635f, 0.345f);
constexpr uint32_t kGoldShade = gfx::rgba(0.612f, 0.478f, 0.235f);
// MU's Zen, the loot tone: gold is loot (style.h).
constexpr uint32_t kZenGold = gfx::rgba(1.0f, 0.8f, 0.102f);

// A line in the label face over its drop, both at `alpha`: the controls' own label with the drop
// faded too, so a banner going out leaves no dark ghost of itself.
float line(gfx::Canvas& canvas, float x, float baseline, float size, uint32_t ink, float alpha,
           const std::string& text) {
    const gfx::Face* face = controls::labelFace();
    if (!face) return 0.0f;
    const bgfx::TextureHandle texture = controls::labelTexture();
    canvas.lettered(*face, texture, x + 1.0f, baseline + 1.0f, size, 0.0f,
                    faded(style::kDrop, alpha), text);
    return canvas.lettered(*face, texture, x, baseline, size, 0.0f, faded(ink, alpha), text);
}

float lineWidth(float size, const std::string& text) {
    const gfx::Face* face = controls::labelFace();
    return face ? face->measure(size, text) : 0.0f;
}

// A title in the windows' Cinzel, tracked, over its drop.
float title(gfx::Canvas& canvas, float x, float baseline, float size, float track, uint32_t ink,
            float alpha, const std::string& text) {
    const gfx::Face* face = panel::titleFace();
    if (!face) return 0.0f;
    const float tracking = size * track;
    canvas.lettered(*face, panel::titleTexture(), x + 1.0f, baseline + 1.0f, size, tracking,
                    faded(style::kDrop, alpha), text);
    return canvas.lettered(*face, panel::titleTexture(), x, baseline, size, tracking,
                           faded(ink, alpha), text);
}

float titleWidth(float size, float track, const std::string& text) {
    const gfx::Face* face = panel::titleFace();
    if (!face || text.empty()) return 0.0f;
    return face->measure(size, text) + size * track * float(text.size() - 1);
}

std::string grouped(int64_t n) { return panel::commas(n); }

// Two packed colours mixed, `t` of the way from `a` to `b`, alpha and all.
uint32_t mixed(uint32_t a, uint32_t b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    uint32_t out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        const float x = float((a >> shift) & 0xFFu), y = float((b >> shift) & 0xFFu);
        out |= uint32_t(std::lround(x + (y - x) * t)) << shift;
    }
    return out;
}

float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// How much of a struck-off row is left standing, 1 until it folds and 0 when it is gone.
float standing(float struck) {
    return struck < 0.0f ? 1.0f : 1.0f - smooth((struck - kFoldAt) / (kStruckSeconds - kFoldAt));
}

// A step struck off, `t` seconds in, its row at `top`: a gold glow rising from its foot, a light
// running along it, the check popping in gold, the words and the count turning gold and struck
// through, a few motes lifting off -- then all of it fading at `alpha` as the row folds away.
void strike(gfx::Canvas& canvas, int step, float t, float left, float right, float top,
            float rowTall, float alpha, const std::string& words, int goal) {
    const float u = tip::unit() * kScale;
    const float wide = right - left;
    const float flare = (t - kFlareAt) / kFlareSeconds;  // 0..1 while the light runs
    const float gold = smooth(flare * 2.0f);              // the words' turn to gold
    // The glow: up fast with the light, then down to a warmth that goes with the row.
    const float glow = t < kFlareAt ? 0.0f
                                    : std::exp(-2.5f * std::max(0.0f, t - kFlareAt - 0.15f)) *
                                          smooth((t - kFlareAt) / 0.15f);
    if (glow > 0.0f) {
        const uint32_t hot = faded(kGoldLit, 0.42f * glow * alpha), clear = faded(kGoldLit, 0.0f);
        canvas.shade({left - 8.0f * u, top - 6.0f * u, wide + 16.0f * u, rowTall + 14.0f * u},
                     clear, clear, hot, hot);
    }
    // The light: a soft band of pale gold sweeping left to right across the row.
    if (flare > 0.0f && flare < 1.0f) {
        const float band = 70.0f * u;
        const float cx = left - band + (wide + band * 2.0f) * smooth(flare);
        const float strength = std::sin(flare * 3.14159265f) * alpha;
        const uint32_t lit = faded(style::kBoneHi, 0.55f * strength);
        const uint32_t clear = faded(style::kBoneHi, 0.0f);
        const float x0 = std::max(left - 8.0f * u, cx - band), x1 = std::min(right + 8.0f * u, cx + band);
        const float y0 = top - 3.0f * u, h = rowTall + 6.0f * u;
        if (cx > x0) canvas.shade({x0, y0, cx - x0, h}, clear, lit, lit, clear);
        if (x1 > cx) canvas.shade({cx, y0, x1 - cx, h}, lit, clear, clear, lit);
    }
    // The check, popping in as the light passes the mark and settling to its size.
    const float popT = std::clamp((t - kFlareAt) / 0.3f, 0.0f, 1.0f);
    const float pop = popT <= 0.0f ? 0.0f : 1.0f + 0.7f * std::sin(popT * 3.14159265f) * (1.0f - popT);
    const uint32_t ink = mixed(style::kBoneHi, kGoldLit, gold);
    if (pop > 0.0f) {
        quest_marks::mark(canvas, StepMark::Done, left + 7.0f * u, top + rowTall * 0.5f, u * pop,
                          alpha * std::min(1.0f, popT * 3.0f), ink);
    } else {
        quest_marks::mark(canvas, StepMark::Live, left + 7.0f * u, top + rowTall * 0.5f, u, alpha);
    }
    const float size = kStep * u;
    const float baseline = controls::middle(top, rowTall, size);
    const float textX = left + kMarkRoom * u;
    line(canvas, textX, baseline, size, ink, alpha, words);
    const std::string figure = std::to_string(goal) + " / " + std::to_string(goal);
    const float fw = lineWidth(size, figure);
    line(canvas, right - fw, baseline, size, ink, alpha, figure);
    // The stroke through the words, drawn left to right behind the passing light.
    const float through = smooth((t - kFlareAt - 0.1f) / 0.4f);
    if (through > 0.0f) {
        const float tw = lineWidth(size, words);
        const float h = std::max(1.0f, 1.5f * u);
        const float sy = baseline - size * 0.32f;
        canvas.rect({textX - 2.0f * u, sy + 1.0f, (tw + 4.0f * u) * through, h},
                    faded(style::kDrop, alpha));
        canvas.rect({textX - 2.0f * u, sy, (tw + 4.0f * u) * through, h}, faded(kGoldLit, alpha));
    }
    // The motes: a few sparks of gold lifting off the row and going out, each on its own path.
    constexpr int kMotes = 9;
    for (int m = 0; m < kMotes; ++m) {
        const uint32_t seed = uint32_t(step * 131 + m * 977) * 2654435761u;
        const float r0 = float(seed & 0xFFFu) / 4095.0f, r1 = float((seed >> 12) & 0xFFFu) / 4095.0f;
        const float born = kFlareAt + 0.08f + r0 * 0.35f;
        const float life = (t - born) / (0.8f + r1 * 0.4f);
        if (life <= 0.0f || life >= 1.0f) continue;
        const float x = left + wide * (0.08f + 0.84f * (float(m) + r1) / float(kMotes));
        const float y = top + rowTall * 0.6f - (14.0f + 26.0f * r0) * u * smooth(life);
        const float s = (1.5f + 1.5f * r1) * u * (1.0f - life * 0.5f);
        const float a = alpha * std::sin(life * 3.14159265f);
        canvas.rect({x - s * 0.5f, y - s * 0.5f, s, s}, faded(mixed(kGoldLit, style::kBoneHi, r0), a));
    }
}

}  // namespace

namespace quest_marks {

uint32_t faded(uint32_t abgr, float alpha) {
    const float a = float((abgr >> 24) & 0xFFu) * std::clamp(alpha, 0.0f, 1.0f);
    return (abgr & 0x00FFFFFFu) | (uint32_t(std::lround(a)) << 24);
}

void mark(gfx::Canvas& canvas, StepMark kind, float cx, float cy, float u, float alpha,
          uint32_t tint) {
    constexpr int kSides = 20;
    constexpr float kTau = 6.2831853f;
    if (kind == StepMark::Done) {
        // The check: two strokes, each a thin quad, in ash ink.
        const uint32_t ink = faded(tint ? tint : style::kAshInk, alpha);
        const float pts[3][2] = {{cx - 4.0f * u, cy + 0.4f * u}, {cx - 1.2f * u, cy + 3.0f * u},
                                 {cx + 4.0f * u, cy - 3.0f * u}};
        const float half = 0.9f * u;
        for (int s = 0; s < 2; ++s) {
            const float dx = pts[s + 1][0] - pts[s][0], dy = pts[s + 1][1] - pts[s][1];
            const float len = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
            const float nx = -dy / len * half, ny = dx / len * half;
            const float ex = dx / len * half * 0.6f, ey = dy / len * half * 0.6f;
            const float xy[8] = {pts[s][0] + nx - ex,     pts[s][1] + ny - ey,
                                 pts[s + 1][0] + nx + ex, pts[s + 1][1] + ny + ey,
                                 pts[s + 1][0] - nx + ex, pts[s + 1][1] - ny + ey,
                                 pts[s][0] - nx - ex,     pts[s][1] - ny - ey};
            const uint32_t c[4] = {ink, ink, ink, ink};
            canvas.polygon(xy, c, 4);
        }
        return;
    }
    if (kind == StepMark::Ready) {
        float xy[kSides * 2];
        uint32_t c[kSides];
        for (int i = 0; i < kSides; ++i) {
            const float a = kTau * float(i) / float(kSides);
            xy[i * 2] = cx + std::cos(a) * 4.0f * u;
            xy[i * 2 + 1] = cy + std::sin(a) * 4.0f * u;
            c[i] = faded(style::kBlood, alpha);
        }
        canvas.polygon(xy, c, kSides);
        return;
    }
    // A ring, 1.6u wide at 4.6u, as quads round its circumference.
    const uint32_t ink = faded(kind == StepMark::Live ? style::kIronHi : style::kIronLo, alpha);
    const float outer = 5.4f * u, inner = 3.8f * u;
    for (int i = 0; i < kSides; ++i) {
        const float a0 = kTau * float(i) / float(kSides), a1 = kTau * float(i + 1) / float(kSides);
        const float xy[8] = {cx + std::cos(a0) * outer, cy + std::sin(a0) * outer,
                             cx + std::cos(a1) * outer, cy + std::sin(a1) * outer,
                             cx + std::cos(a1) * inner, cy + std::sin(a1) * inner,
                             cx + std::cos(a0) * inner, cy + std::sin(a0) * inner};
        const uint32_t c[4] = {ink, ink, ink, ink};
        canvas.polygon(xy, c, 4);
    }
}

std::string wait(int64_t seconds) {
    if (seconds < 60) return "under a minute";
    const int64_t minutes = seconds / 60;
    char text[48];
    if (minutes < 60) std::snprintf(text, sizeof(text), "%lldm", (long long)minutes);
    else std::snprintf(text, sizeof(text), "%lldh %02lldm", (long long)(minutes / 60),
                       (long long)(minutes % 60));
    return text;
}

}  // namespace quest_marks

bool Tracker::Drawn::operator==(const Drawn& o) const {
    if (quest != o.quest || shown != o.shown || width != o.width || height != o.height ||
        minutesLeft != o.minutesLeft || pointing != o.pointing || pointX != o.pointX ||
        pointY != o.pointY || pointMetres != o.pointMetres ||
        focus != o.focus || progress.state != o.progress.state ||
        eventPhase != o.eventPhase || eventSeconds != o.eventSeconds ||
        eventKills != o.eventKills || eventSorcerers != o.eventSorcerers ||
        eventStatue != o.eventStatue ||
        eventShown != o.eventShown) {
        return false;
    }
    for (int i = 0; i < sim::kQuestSteps; ++i) {
        if (counts[i] != o.counts[i] || embers[i] != o.embers[i] || lit[i] != o.lit[i] ||
            struck[i] != o.struck[i] || progress.counts[i] != o.progress.counts[i]) {
            return false;
        }
    }
    return true;
}

bool Tracker::here(const sim::Realm& realm, int quest) {
    const content::Tables* tables = realm.tables();
    if (tables == nullptr || quest < 0) return false;
    const sim::QuestRow& row = sim::questAt(quest);
    const sim::QuestProgress& progress = realm.quest(quest);
    for (int s = 0; s < row.stepCount && s < sim::kQuestSteps; ++s) {
        if (row.steps[s].kind != sim::QuestStepKind::Clear) continue;
        if (progress.counts[s] >= realm.questGoal(quest, s)) continue;
        for (const content::MonsterNest& nest : tables->nests) {
            if (nest.kind < tables->kinds.size() &&
                tables->kinds[nest.kind].number == row.steps[s].target) {
                return true;
            }
        }
    }
    return false;
}

void Tracker::open(const gfx::Interface& interface) {
    interface.adopt(canvas_);
    interface.adopt(banner_);
    opened_ = true;
}

void Tracker::close() {
    canvas_.clear();
    banner_.clear();
    seen_ = false;
    built_ = false;
    bannerAge_ = -1.0f;
    bannerDrawn_ = -1;
}

void Tracker::update(float seconds, const Play& play, bool hidden, const float* viewProj,
                     int width, int height) {
    if (!opened_ || !play.isOpen()) return;
    const sim::Realm& realm = play.realm();
    const float u = tip::unit();

    // ---- what moved, since last frame: the banners and the embers ------------------------------
    for (int q = 0; q < sim::kQuests; ++q) {
        const sim::QuestProgress& now = realm.quest(q);
        if (seen_) {
            const sim::QuestProgress& was = last_[q];
            const sim::QuestRow& row = sim::questAt(q);
            if (now.state == sim::QuestState::Active && was.state != sim::QuestState::Active) {
                bannerKicker_ = "Quest accepted";
                bannerTitle_ = row.title;
                bannerLine_.clear();
                bannerZen_.clear();
                bannerNext_.clear();
                bannerHold_ = 2.2f;
                bannerAge_ = 0.0f;
                for (int s = 0; s < sim::kQuestSteps; ++s) counts_[s] = float(now.counts[s]);
            } else if (now.state == sim::QuestState::Ready && was.state == sim::QuestState::Active) {
                bannerKicker_ = row.title;
                bannerTitle_ = std::string("Return to ") + row.giverName;
                bannerLine_.clear();
                bannerZen_.clear();
                bannerNext_.clear();
                bannerHold_ = 2.2f;
                bannerAge_ = 0.0f;
            } else if (now.state == sim::QuestState::Resting &&
                       was.state == sim::QuestState::Ready) {
                bannerKicker_ = "Quest complete";
                bannerTitle_ = row.title;
                bannerLine_ = grouped(sim::questExperience(
                                  row, sim::questFirst(row, int(realm.hero().kin),
                                                       was.completions))) +
                              " experience      ";
                bannerZen_ = grouped(row.zen) + " Zen";
                bannerNext_ = row.next;
                // Longer than the others: the stinger is still going, and the way on is to read.
                bannerHold_ = bannerNext_.empty() ? 5.0f : 6.5f;
                bannerAge_ = 0.0f;
            }
            if (now.state != was.state) {
                awake_ = kWakeHold;
                focus_ = -1;
            }
            // A kill that counted for this quest pins it (pinned_), whichever is shown.
            for (int s = 0; s < sim::kQuestSteps; ++s) {
                if (now.counts[s] > was.counts[s]) pinned_ = q;
            }
            if (q == quest_) {
                for (int s = 0; s < sim::kQuestSteps; ++s) {
                    if (now.counts[s] > was.counts[s]) {
                        ember_[s] = 1.0f;
                        awake_ = kWakeHold;
                        focus_ = s;
                    }
                }
            }
        }
        last_[q] = now;
    }
    seen_ = true;

    // The quest followed: the one his last counted kill went to, while it is live and belongs to
    // this map or no live quest does; else the first live one on this map; else the first live
    // one; then the first resting one (for its wait).
    const auto live = [&](int q) {
        const sim::QuestState state = realm.quest(q).state;
        return state == sim::QuestState::Active || state == sim::QuestState::Ready;
    };
    int firstHere = -1, firstLive = -1;
    for (int q = 0; q < sim::kQuests; ++q) {
        if (!live(q)) continue;
        if (firstLive < 0) firstLive = q;
        if (firstHere < 0 && here(realm, q)) firstHere = q;
    }
    if (pinned_ >= 0 && !live(pinned_)) pinned_ = -1;
    quest_ = -1;
    if (pinned_ >= 0 && (firstHere < 0 || here(realm, pinned_))) quest_ = pinned_;
    if (quest_ < 0) quest_ = firstHere >= 0 ? firstHere : firstLive;
    for (int q = 0; q < sim::kQuests && quest_ < 0; ++q) {
        if (realm.quest(q).state == sim::QuestState::Resting && !realm.questOffered(q)) quest_ = q;
    }

    // ---- the eased pieces -----------------------------------------------------------------------
    const float step = seconds / kFadeSeconds;
    const bool wanted = quest_ >= 0 && !hidden;
    shown_ = wanted ? std::min(1.0f, shown_ + step) : std::max(0.0f, shown_ - step);
    if (quest_ >= 0) {
        const sim::QuestProgress& now = realm.quest(quest_);
        for (int s = 0; s < sim::kQuestSteps; ++s) {
            const float target = float(now.counts[s]);
            const float goal = float(std::max(1, realm.questGoal(quest_, s)));
            const float speed = goal * seconds / kBarSeconds;
            counts_[s] = counts_[s] < target ? std::min(target, counts_[s] + std::max(speed, seconds))
                                             : target;
            ember_[s] = std::max(0.0f, ember_[s] - seconds / kEmberSeconds);
        }
        // Struck off: a count at its goal starts its flare the frame it gets there, and one found
        // there when the tracker takes the quest up (a load, a restart) is gone from the start.
        const sim::QuestRow& row = sim::questAt(quest_);
        const bool fresh = quest_ != struckQuest_;
        struckQuest_ = quest_;
        for (int s = 0; s < sim::kQuestSteps; ++s) {
            const bool counted = s < row.stepCount && sim::questCounted(row.steps[s].kind);
            const bool done = counted && now.counts[s] >= realm.questGoal(quest_, s);
            if (!done) struck_[s] = -1.0f;
            else if (fresh) struck_[s] = kStruckSeconds;
            else if (struck_[s] < 0.0f) {
                struck_[s] = 0.0f;
                awake_ = std::max(awake_, kWakeHold);
            } else {
                const float was = struck_[s];
                struck_[s] = std::min(kStruckSeconds, struck_[s] + seconds);
                // The blade's sound, as the light starts along the row (game/ui/desk.cpp).
                if (was < kFlareAt && struck_[s] >= kFlareAt) strikeHeard_ = true;
            }
        }
    } else {
        struckQuest_ = -1;
    }

    // Awake while a kill is fresh, and always while the giver waits for the hand-in.
    awake_ = std::max(0.0f, awake_ - seconds);
    const bool ready = quest_ >= 0 && realm.quest(quest_).state == sim::QuestState::Ready;
    wake_ = awake_ > 0.0f || ready ? std::min(1.0f, wake_ + step)
                                   : std::max(0.0f, wake_ - seconds / kWakeFade);
    if (quest_ >= 0) {
        const sim::QuestRow& row = sim::questAt(quest_);
        for (int s = 0; s < sim::kQuestSteps; ++s) {
            const bool counted = s < row.stepCount && sim::questCounted(row.steps[s].kind);
            // A row being struck off stays at full through its flare, even as the quest turns
            // Ready under it.
            const bool striking = struck_[s] >= 0.0f && struck_[s] < kStruckSeconds;
            const bool full = striking || (ready ? !counted : focus_ < 0 || s == focus_);
            const float target = full ? 1.0f : kDim;
            // Faded away, a row takes its new weight at once: the next kill shows it already set.
            if (wake_ <= 0.0f) lit_[s] = target;
            else lit_[s] = lit_[s] < target ? std::min(target, lit_[s] + step)
                                            : std::max(target, lit_[s] - step);
        }
    }

    // ---- the edge pointer: the giver, when he is the next step and off the frame ---------------
    pointing_ = false;
    if (quest_ >= 0 && realm.quest(quest_).state == sim::QuestState::Ready && viewProj) {
        const sim::QuestRow& row = sim::questAt(quest_);
        const content::Tables* tables = realm.tables();
        for (size_t f = 0; tables && f < tables->folk.size(); ++f) {
            if (tables->folk[f].number != row.giver) continue;
            float x = 0.0f, y = 0.0f;
            if (!play.folkCrownOf(int(f), viewProj, width, height, &x, &y)) break;
            const float inset = kEdgeInset * u;
            if (x >= 0.0f && x <= float(width) && y >= 0.0f && y <= float(height)) break;
            const float cx = float(width) * 0.5f, cy = float(height) * 0.5f;
            const float dx = x - cx, dy = y - cy;
            const float sx = dx != 0.0f ? (cx - inset) / std::fabs(dx) : 1e9f;
            const float sy = dy != 0.0f ? (cy - inset) / std::fabs(dy) : 1e9f;
            const float s = std::min(sx, sy);
            pointX_ = cx + dx * s;
            pointY_ = cy + dy * s;
            pointAngle_ = std::atan2(dy, dx);
            const sim::Body& hero = realm.hero();
            const float mx = float(tables->folk[f].x) - hero.x, my = float(tables->folk[f].y) - hero.y;
            pointMetres_ = int(std::lround(std::sqrt(mx * mx + my * my)));
            pointing_ = true;
            break;
        }
    }

    // ---- the banner's life ------------------------------------------------------------------
    int bannerStep = -1;
    if (bannerAge_ >= 0.0f) {
        bannerAge_ += seconds;
        float alpha = 1.0f;
        if (bannerAge_ < kBannerIn) alpha = bannerAge_ / kBannerIn;
        else if (bannerAge_ > kBannerIn + bannerHold_) {
            alpha = 1.0f - (bannerAge_ - kBannerIn - bannerHold_) / kBannerOut;
        }
        if (alpha <= 0.0f) bannerAge_ = -1.0f;
        else bannerStep = int(std::lround(alpha * 64.0f));
    }
    if (bannerStep != bannerDrawn_) {
        bannerDrawn_ = bannerStep;
        rebuildBanner(width, height);
    }

    // Blood Castle's run: up the whole time it lasts, in the quest's place (the user, 2026-10-03:
    // 'Lets use already UI which is for quests, but also sho timer there').
    const sim::CastleRun& run = realm.castleRun();
    const bool inRun = run.phase != sim::CastlePhase::None;
    eventShown_ = inRun && !hidden ? std::min(1.0f, eventShown_ + step)
                                   : std::max(0.0f, eventShown_ - step);

    Drawn now;
    if (eventShown_ > 0.0f) {
        now.eventPhase = int(run.phase);
        now.eventSeconds = realm.castleSecondsLeft();
        now.eventKills = run.kills;
        now.eventSorcerers = run.sorcerers;
        now.eventStatue = run.statueBroken;
        now.eventShown = int(std::lround(eventShown_ * 64.0f));
    }
    now.quest = shown_ * wake_ > 0.0f && eventShown_ <= 0.0f ? quest_ : -1;
    now.shown = int(std::lround(shown_ * wake_ * 64.0f));
    now.width = width;
    now.height = height;
    now.pointing = pointing_;
    if (pointing_) {
        now.pointX = int(pointX_);
        now.pointY = int(pointY_);
        now.pointMetres = pointMetres_;
    }
    if (now.quest >= 0) {
        now.progress = realm.quest(quest_);
        for (int s = 0; s < sim::kQuestSteps; ++s) {
            now.counts[s] = int(counts_[s] * 8.0f);
            now.embers[s] = int(ember_[s] * 32.0f);
            now.lit[s] = int(std::lround(lit_[s] * 32.0f));
            now.struck[s] = struck_[s] < 0.0f ? -1 : int(struck_[s] * 60.0f);
        }
        now.focus = ready ? -1 : focus_;
        if (now.progress.state == sim::QuestState::Resting) {
            now.minutesLeft = std::max<int64_t>(0, now.progress.availableAt - realm.wallClock()) / 60;
        }
    }
    if (built_ && now == drawn_) return;
    drawn_ = now;
    built_ = true;
    rebuild(play, width, height);
}

void Tracker::rebuild(const Play& play, int width, int height) {
    canvas_.clear();
    const float pu = tip::unit(), u = pu * kScale;
    const sim::Realm& realm = play.realm();

    // The pointer is the giver's, and shows whether the tracker is in a window's way or not.
    if (drawn_.pointing) {
        const float s = 11.0f * pu;
        const float c = std::cos(pointAngle_), sn = std::sin(pointAngle_);
        // A slim blade pointing along the angle: tip, the two back corners, the notch.
        const auto at = [&](float fx, float fy, float* out) {
            out[0] = pointX_ + (fx * c - fy * sn) * s;
            out[1] = pointY_ + (fx * sn + fy * c) * s;
        };
        float lit[6], shade[6];
        at(1.0f, 0.0f, lit + 0);
        at(-0.9f, -0.75f, lit + 2);
        at(-0.45f, 0.0f, lit + 4);
        at(1.0f, 0.0f, shade + 0);
        at(-0.45f, 0.0f, shade + 2);
        at(-0.9f, 0.75f, shade + 4);
        const uint32_t cl[3] = {kGoldLit, kGoldLit, kGoldLit};
        const uint32_t cs[3] = {kGoldShade, kGoldShade, kGoldShade};
        canvas_.polygon(lit, cl, 3);
        canvas_.polygon(shade, cs, 3);
        // His name and the distance, on the side of the pointer away from the edge.
        const bool left = pointX_ > float(width) * 0.5f;
        const std::string name = sim::questAt(std::max(0, drawn_.quest)).giverName;
        const std::string metres = std::to_string(drawn_.pointMetres) + " m";
        const float nameSize = 14.0f * pu, metreSize = 13.0f * pu;
        const float gap = 16.0f * pu;
        const float nameW = titleWidth(nameSize, 0.06f, name), metreW = lineWidth(metreSize, metres);
        const float wide = std::max(nameW, metreW);
        const float x = left ? pointX_ - gap - wide : pointX_ + gap;
        const float top = std::clamp(pointY_ - 14.0f * pu, 8.0f * pu, float(height) - 40.0f * pu);
        title(canvas_, left ? x + wide - nameW : x, top + 12.0f * pu, nameSize, 0.06f,
              style::kBoneHi, 1.0f, name);
        line(canvas_, left ? x + wide - metreW : x, top + 28.0f * pu, metreSize, style::kBone2, 1.0f,
             metres);
    }

    if (drawn_.eventShown > 0) {
        rebuildEvent(width);
        return;
    }
    if (drawn_.quest < 0) return;
    const float alpha = float(drawn_.shown) / 64.0f;
    const int q = drawn_.quest;
    const sim::QuestRow& row = sim::questAt(q);
    const sim::QuestProgress& now = drawn_.progress;
    const float right = float(width) - kRight * pu;
    const float left = right - kWide * u;
    float y = kTop * pu;

    // The scrim: the banners' own soft cloud (a Gaussian each way, as the map name's), its
    // middle at the screen's right edge and the tracker's middle, so it has no edge anywhere. A
    // rectangle fading one way only showed its top and bottom over bright paving.
    {
        float rows = 0.0f;
        float apart = 0.0f;
        for (int s = 0; s < row.stepCount; ++s) {
            const float keep = standing(float(drawn_.struck[s]) / 60.0f);
            rows += keep;
            if (s > 0 && !sim::questCounted(row.steps[s].kind)) apart += kTurnInGap * keep;
        }
        const float tall = now.state == sim::QuestState::Resting
                               ? (*row.next ? 92.0f : 70.0f)
                               : 64.0f + rows * (kStep + kRowGap) + apart;
        const float cx = float(width), cy = y + tall * 0.5f * u;
        const float sx = kScrimWide * u * 0.55f, sy = tall * u * 0.42f;
        constexpr int kColumns = 12, kRows = 12;
        const auto at = [&](int i, int j) {
            const float gx = -3.0f + 3.0f * float(i) / kColumns;  // the left half only
            const float gy = -3.0f + 6.0f * float(j) / kRows;
            return faded(style::kVoid, kScrimAlpha * alpha * std::exp(-0.5f * (gx * gx + gy * gy)));
        };
        for (int j = 0; j < kRows; ++j) {
            for (int i = 0; i < kColumns; ++i) {
                const float x0 = cx + sx * (-3.0f + 3.0f * float(i) / kColumns);
                const float y0 = cy + sy * (-3.0f + 6.0f * float(j) / kRows);
                canvas_.shade({x0, y0, sx * 3.0f / kColumns, sy * 6.0f / kRows}, at(i, j),
                              at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
            }
        }
    }

    if (now.state == sim::QuestState::Resting) {
        // Handed in, and not his to give again yet: one quiet line and the wait.
        controls::caps(canvas_, left, y + 12.0f * u, style::kKickerSize * u,
                       faded(style::kAshInk, alpha), std::string(row.giverName));
        title(canvas_, left, y + 36.0f * u, 15.0f * u, 0.06f, style::kBone2, alpha, row.title);
        line(canvas_, left, y + 58.0f * u, 15.0f * u, style::kAshInk, alpha,
             "Offered again in " + quest_marks::wait(drawn_.minutesLeft * 60 + 59));
        if (*row.next) line(canvas_, left, y + 80.0f * u, 15.0f * u, style::kBone2, alpha, row.next);
        return;
    }

    controls::caps(canvas_, left, y + 12.0f * u, style::kKickerSize * u,
                   faded(style::kAshInk, alpha), "Tracked quest");
    y += 24.0f * u;
    title(canvas_, left, y + kTitle * u, kTitle * u, kTitleTrack, style::kBoneHi, alpha, row.title);
    y += kTitle * u + 10.0f * u;
    {
        // The only line: iron, fading in over its first third.
        const float third = kWide * u / 3.0f;
        const uint32_t iron = faded(style::kIron, alpha), clear = faded(style::kIron, 0.0f);
        canvas_.shade({left, y, third, std::max(1.0f, u)}, clear, iron, iron, clear);
        canvas_.rect({left + third, y, kWide * u - third, std::max(1.0f, u)}, iron);
    }
    y += kRuleGap * u;

    const bool ready = now.state == sim::QuestState::Ready;
    for (int s = 0; s < row.stepCount; ++s) {
        const sim::QuestStepRow& want = row.steps[s];
        const int goal = realm.questGoal(q, s);
        const bool counted = sim::questCounted(want.kind);
        const bool done = counted ? now.counts[s] >= goal : false;
        StepMark kind = StepMark::Live;
        uint32_t ink = style::kBoneHi;
        if (counted && done) {
            kind = StepMark::Done;
            ink = style::kAshInk;
        } else if (!counted) {
            kind = ready ? StepMark::Ready : StepMark::Waiting;
            // Waiting, it still has to be read over bright ground: ash, not the darker ash.
            ink = ready ? style::kBloodHi : style::kAshInk;
        }
        const float rowTall = kStep * u;
        if (s > 0 && !counted) y += kTurnInGap * u * standing(float(drawn_.struck[s]) / 60.0f);
        // Struck off and folded away: gone from the list, the rows under it closed up.
        const float struck = float(drawn_.struck[s]) / 60.0f;
        const float keep = standing(struck);
        if (keep <= 0.0f) continue;
        // The step just counted at full, the rest dimmed (Tracker::update).
        const float rowAlpha = alpha * float(drawn_.lit[s]) / 32.0f;
        if (struck >= 0.0f) {
            strike(canvas_, s, struck, left, right, y, rowTall, alpha * keep, want.line, goal);
            y += (rowTall + kRowGap * u) * keep;
            continue;
        }
        // The ember under a count that just moved: the hover's own, rising from the row's foot.
        const float ember = float(drawn_.embers[s]) / 32.0f;
        if (ember > 0.0f) {
            const uint32_t hot = faded(style::kBlood, style::kEmberAlpha * ember * alpha);
            const uint32_t clear = faded(style::kBlood, 0.0f);
            canvas_.shade({left - 8.0f * u, y - 4.0f * u, kWide * u + 16.0f * u, rowTall + 12.0f * u},
                          clear, clear, hot, hot);
        }
        quest_marks::mark(canvas_, kind, left + 7.0f * u, y + rowTall * 0.5f, u, rowAlpha);
        const float baseline = controls::middle(y, rowTall, kStep * u);
        line(canvas_, left + kMarkRoom * u, baseline, kStep * u, ink, rowAlpha, want.line);
        if (counted) {
            const float shownCount = float(drawn_.counts[s]) / 8.0f;
            const std::string figure = std::to_string(int(std::floor(shownCount))) + " / " +
                                       std::to_string(goal);
            const float fw = lineWidth(kStep * u, figure);
            line(canvas_, right - fw, baseline, kStep * u, done ? style::kAshInk : style::kBoneHi,
                 rowAlpha, figure);
            // One bar, under the step just counted only, in the row gap so nothing shifts.
            if (!done && s == drawn_.focus) {
                const float barY = y + rowTall + kBarGap * u * 0.5f;
                const float barLeft = left + kMarkRoom * u, barWide = right - barLeft;
                const float h = std::max(1.0f, 2.0f * u);
                // The whole run faintly, so the share reads as a share and not as an underline.
                canvas_.rect({barLeft, barY, barWide, h}, faded(style::kIron, rowAlpha * 0.5f));
                const float share = std::clamp(shownCount / float(std::max(1, goal)), 0.0f, 1.0f);
                if (share > 0.0f) {
                    canvas_.rect({barLeft, barY, std::round(barWide * share), h},
                                 faded(style::kBone, rowAlpha));
                }
            }
        }
        y += rowTall + kRowGap * u;
    }
}

// Blood Castle's run in the tracker's place, in its own words and marks: the castle, the clock --
// the court's wait before it starts, then its time -- and the run's steps, MU's in order
// (docs/blood-castle-port.md §5): the road's 40 kills drop the bridge, two Spirit Sorcerers open
// the door, the statue gives up the staff, and the Archangel takes it back. The step being
// fought for is live, the rest wait; a quota met is a check.
void Tracker::rebuildEvent(int width) {
    const float pu = tip::unit(), u = pu * kScale;
    const float alpha = float(drawn_.eventShown) / 64.0f;
    const float right = float(width) - kRight * pu;
    const float left = right - kWide * u;
    float y = kTop * pu;
    const auto phase = sim::CastlePhase(drawn_.eventPhase);
    const bool waiting = phase == sim::CastlePhase::Waiting;
    const bool ended = phase == sim::CastlePhase::Ended;
    const bool won = phase == sim::CastlePhase::Won;

    controls::caps(canvas_, left, y + 12.0f * u, style::kKickerSize * u,
                   faded(style::kAshInk, alpha), "Event");
    y += 24.0f * u;
    title(canvas_, left, y + kTitle * u, kTitle * u, kTitleTrack, style::kBoneHi, alpha,
          "Blood Castle 1");
    // The clock on the title's line, ranged right: gold counting down to the start, then the
    // run's time in bone, red in its last minute.
    {
        char clock[16];
        std::snprintf(clock, sizeof(clock), "%d:%02d", drawn_.eventSeconds / 60,
                      drawn_.eventSeconds % 60);
        const std::string text = won     ? std::string("Complete")
                                 : ended ? std::string("Time is up")
                                         : std::string(clock);
        const uint32_t ink = won ? kGoldLit
                             : ended || (!waiting && drawn_.eventSeconds < 60) ? style::kBloodHi
                             : waiting                                          ? kGoldLit
                                                                                : style::kBoneHi;
        const float w = lineWidth(kStep * u, text);
        line(canvas_, right - w, y + kTitle * u, kStep * u, ink, alpha, text);
    }
    y += kTitle * u + 10.0f * u;
    {
        const float third = kWide * u / 3.0f;
        const uint32_t iron = faded(style::kIron, alpha), clear = faded(style::kIron, 0.0f);
        canvas_.shade({left, y, third, std::max(1.0f, u)}, clear, iron, iron, clear);
        canvas_.rect({left + third, y, kWide * u - third, std::max(1.0f, u)}, iron);
    }
    y += kRuleGap * u;

    struct Row {
        std::string text;
        std::string figure;
        StepMark mark;
    };
    const bool bridge = won || drawn_.eventKills >= sim::kCastleKills;
    const bool door = won || drawn_.eventSorcerers >= sim::kCastleSorcerers;
    const Row rows[] = {
        {waiting ? "Wait for the gate to open" : "Slay the castle's guards",
         waiting ? std::string()
                 : std::to_string(std::min(drawn_.eventKills, sim::kCastleKills)) + " / " +
                       std::to_string(sim::kCastleKills),
         waiting ? StepMark::Live : bridge ? StepMark::Done : StepMark::Live},
        {"Slay the Spirit Sorcerers",
         std::to_string(std::min(drawn_.eventSorcerers, sim::kCastleSorcerers)) + " / " +
             std::to_string(sim::kCastleSorcerers),
         door ? StepMark::Done : bridge && !waiting ? StepMark::Live : StepMark::Waiting},
        {"Destroy the Statue of Saint", "",
         won || drawn_.eventStatue ? StepMark::Done : door ? StepMark::Live : StepMark::Waiting},
        {"Return the weapon to the Archangel", "",
         won ? StepMark::Done : drawn_.eventStatue ? StepMark::Live : StepMark::Waiting},
        // The run over: a minute's rest, then out to Devias (sim kCastleRest).
        {"Back to Devias",
         std::to_string(drawn_.eventSeconds / 60) + ":" + (drawn_.eventSeconds % 60 < 10 ? "0" : "") +
             std::to_string(drawn_.eventSeconds % 60),
         StepMark::Live},
    };
    // In the court's wait only the wait itself: the run's steps show once the gate opens (the
    // user, 2026-10-03: 'dont show other quests tasks before gate is not opened').
    const size_t allRows = sizeof(rows) / sizeof(rows[0]);
    const size_t shownRows = waiting ? 1 : won || ended ? allRows : allRows - 1;
    for (size_t r = 0; r < shownRows; ++r) {
        const Row& row = rows[r];
        const float rowTall = kStep * u;
        const bool live = row.mark == StepMark::Live;
        const bool done = row.mark == StepMark::Done;
        const uint32_t ink = live ? style::kBoneHi : style::kAshInk;
        quest_marks::mark(canvas_, row.mark, left + 7.0f * u, y + rowTall * 0.5f, u, alpha);
        const float baseline = controls::middle(y, rowTall, kStep * u);
        line(canvas_, left + kMarkRoom * u, baseline, kStep * u, ink, alpha, row.text);
        if (!row.figure.empty()) {
            const float fw = lineWidth(kStep * u, row.figure);
            line(canvas_, right - fw, baseline, kStep * u,
                 done ? style::kAshInk : live ? style::kBoneHi : style::kAshInk, alpha, row.figure);
        }
        y += rowTall + kRowGap * u;
    }
}

void Tracker::rebuildBanner(int width, int height) {
    banner_.clear();
    if (bannerDrawn_ <= 0) return;
    (void)height;
    const float alpha = float(bannerDrawn_) / 64.0f;
    const float u = tip::unit();
    const float cx = float(width) * 0.5f;
    // In the arrival banner's grammar (game/ui/arrival.h) but in the upper band, over the town
    // and clear of the hero's head: the map's name holds the lower third, and at 300u the first
    // try sat across the character and the tile over him.
    const float top = 130.0f * u;
    const float rise = (1.0f - std::min(1.0f, alpha * 2.0f)) * style::kOpenRise * u;

    // The map name's own shadow behind the whole stack (game/ui/arrival.cpp): a Gaussian each
    // way at 52% in its middle, laid as a grid of shaded quads so it has no edge and fades with
    // the words. The user, 2026-09-28: "use similar shadow behind quest messages as map names".
    // Wider and a little taller than the map name's, for the longer title and the reward line.
    {
        const float cy = top + (bannerLine_.empty() ? 40.0f : bannerNext_.empty() ? 52.0f : 64.0f) * u;
        const float sx = 290.0f * u,
                    sy = (bannerLine_.empty() ? 50.0f : bannerNext_.empty() ? 60.0f : 70.0f) * u;
        constexpr int kColumns = 16, kRows = 8;
        const auto at = [&](int i, int j) {
            const float gx = -3.0f + 6.0f * float(i) / kColumns;
            const float gy = -3.0f + 6.0f * float(j) / kRows;
            return quest_marks::faded(gfx::rgba(0.0f, 0.0f, 0.0f, 1.0f),
                                      0.52f * alpha * std::exp(-0.5f * (gx * gx + gy * gy)));
        };
        for (int j = 0; j < kRows; ++j) {
            for (int i = 0; i < kColumns; ++i) {
                const float x0 = cx + sx * (-3.0f + 6.0f * float(i) / kColumns);
                const float y0 = cy + sy * (-3.0f + 6.0f * float(j) / kRows);
                banner_.shade({x0, y0, sx * 6.0f / kColumns, sy * 6.0f / kRows}, at(i, j),
                              at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
            }
        }
    }

    const float kickerSize = 13.0f * u;
    const float kw = controls::capsWidth(kickerSize, bannerKicker_);
    const float kickerBase = top + 14.0f * u + rise;
    controls::caps(banner_, cx - kw * 0.5f, kickerBase, kickerSize, faded(style::kAshInk, alpha),
                   bannerKicker_);
    const float ruleWide = 70.0f * u, ruleGap = 14.0f * u;
    const float ruleY = kickerBase - kickerSize * 0.35f;
    const uint32_t iron = faded(style::kIron, alpha), clear = faded(style::kIron, 0.0f);
    const float h = std::max(1.0f, u);
    banner_.shade({cx - kw * 0.5f - ruleGap - ruleWide, ruleY, ruleWide, h}, clear, iron, iron, clear);
    banner_.shade({cx + kw * 0.5f + ruleGap, ruleY, ruleWide, h}, iron, clear, clear, iron);

    const float titleSize = 30.0f * u;
    const float tw = titleWidth(titleSize, 0.1f, bannerTitle_);
    title(banner_, cx - tw * 0.5f, top + 56.0f * u + rise, titleSize, 0.1f, style::kBoneHi, alpha,
          bannerTitle_);

    if (!bannerLine_.empty()) {
        const float size = 16.0f * u;
        const float lw = lineWidth(size, bannerLine_), zw = lineWidth(size, bannerZen_);
        const float x = cx - (lw + zw) * 0.5f;
        const float base = top + 88.0f * u + rise;
        line(banner_, x, base, size, style::kBone, alpha, bannerLine_);
        line(banner_, x + lw, base, size, kZenGold, alpha, bannerZen_);
    }
    if (!bannerNext_.empty()) {
        // The way on, under the reward: quieter, the resting tracker keeps it after.
        const float size = 15.0f * u;
        const float nw = lineWidth(size, bannerNext_);
        line(banner_, cx - nw * 0.5f, top + 114.0f * u + rise, size, style::kBone2, alpha,
             bannerNext_);
    }
}

}  // namespace mu::game
