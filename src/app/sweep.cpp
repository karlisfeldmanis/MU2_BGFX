#include "app/sweep.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "app/context.h"
#include "app/modes/play_mode.h"
#include "core/json.h"
#include "core/log.h"
#include "game/play.h"

namespace mu::app {
namespace {

// A JSON string's own escapes, enough for a log line and a breed's label.
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (uint8_t(c) < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// The happenings worth naming beside a hitch. Steps, routes and halts are every tick's and say
// nothing about one frame; a spawn, a death, a blow begun, a spell, a trap or a landing might.
const char* worthNaming(sim::What what) {
    switch (what) {
        case sim::What::Spawned: return "spawned";
        case sim::What::Rose: return "rose";
        case sim::What::Roused: return "roused";
        case sim::What::Died: return "died";
        case sim::What::Swung: return "swung";
        case sim::What::Cast: return "cast";
        case sim::What::Loosed: return "loosed";
        case sim::What::Hit: return "hit";
        case sim::What::Dropped: return "dropped";
        case sim::What::Levelled: return "levelled";
        case sim::What::Shoved: return "shoved";
        case sim::What::Trapped: return "trap";
        case sim::What::Spirits: return "spirits";
        case sim::What::Climbed: return "climbed";
        case sim::What::Warped: return "warped";
        case sim::What::Blinked: return "blinked";
        default: return nullptr;
    }
}

// The frame-rate line, the effects pool's line and the mode's report are the Application's
// once a second and land outside the window anyway; these are the per-frame chatter a hitch
// is not about either.
bool chatter(const char* line) {
    return !std::strncmp(line, "frame ", 6) || !std::strncmp(line, "  effects:", 10);
}

}  // namespace

void Sweep::tap(const char* line, void* self) {
    Sweep* sweep = static_cast<Sweep*>(self);
    if (chatter(line)) return;
    std::lock_guard<std::mutex> hold(sweep->lock_);
    // Six a frame is plenty to name a cause; a frame that logs hundreds is its own answer.
    if (sweep->said_.size() >= 6) return;
    std::string one(line);
    if (one.size() > 140) one.resize(140);
    sweep->said_.push_back(std::move(one));
}

bool Sweep::open(Context& ctx, Mode& mode) {
    PlayMode* played = dynamic_cast<PlayMode*>(&mode);
    if (!played || !played->world().played().isOpen()) {
        core::logError("sweep: needs a played world (--world NAME --play)");
        return false;
    }
    play_ = &played->world().played();

    const core::Json list = core::parseJsonFile(ctx.args.sweepPath);
    if (list.isNull()) {
        core::logError("sweep: could not read %s", ctx.args.sweepPath.c_str());
        play_ = nullptr;
        return false;
    }
    settle_ = std::max(1, int(list["settle"].numberOr(90)));
    frames_ = std::max(30, int(list["frames"].numberOr(400)));
    passes_ = std::max(1, int(list["passes"].numberOr(1)));
    fight_ = list["fight"].boolOr(false);
    fightReach_ = float(list["fight_reach"].numberOr(6.0));

    const content::Grid& grid = play_->realm().tables()->grid;
    const core::Json& hand = list["spots"];
    for (size_t i = 0; i < hand.size(); ++i) {
        const core::Json& one = hand.at(i);
        Spot spot;
        spot.name = one["name"].stringOr("");
        spot.column = int(one["at"].at(0).numberOr(-1));
        spot.row = int(one["at"].at(1).numberOr(-1));
        spot.hot = true;
        if (!grid.inside(spot.column, spot.row)) {
            core::logError("sweep: %s at %d,%d is off the map", spot.name.c_str(), spot.column,
                           spot.row);
            continue;
        }
        spots_.push_back(spot);
    }
    // The grid's tiles: every `step` tiles, the ones a person may stand on. Read off the cooked
    // attribute grid the realm walks on, so a wall or the void is never measured as a place.
    const int step = int(list["grid"].numberOr(0));
    if (step > 0) {
        for (int row = step / 2; row < grid.size(); row += step) {
            for (int column = step / 2; column < grid.size(); column += step) {
                if (!grid.open(column, row)) continue;
                Spot spot;
                spot.column = column;
                spot.row = row;
                spots_.push_back(spot);
            }
        }
    }
    if (spots_.empty()) {
        core::logError("sweep: no tiles to measure");
        play_ = nullptr;
        return false;
    }

    out_ = std::fopen(ctx.args.sweepOut.c_str(), "w");
    if (!out_) {
        core::logError("sweep: cannot write %s", ctx.args.sweepOut.c_str());
        play_ = nullptr;
        return false;
    }
    // The header: the run's conditions and the map's standable tiles at a quarter, which is
    // what the heat map is drawn over.
    constexpr int kMaskStep = 4;
    std::fprintf(out_,
                 "{\"header\": true, \"world\": %s, \"width\": %d, \"height\": %d, "
                 "\"peaceful\": %s, \"fight\": %s, \"level\": %d, \"settle\": %d, "
                 "\"frames\": %d, \"passes\": %d, \"size\": %d, \"mask_step\": %d, \"mask\": [",
                 quoted(ctx.args.world).c_str(), ctx.window.width(), ctx.window.height(),
                 ctx.args.peaceful ? "true" : "false", fight_ ? "true" : "false",
                 ctx.args.level, settle_, frames_, passes_, grid.size(), kMaskStep);
    for (int row = 0; row < grid.size(); row += kMaskStep) {
        std::string line;
        for (int column = 0; column < grid.size(); column += kMaskStep) {
            line += grid.open(column, row) ? '1' : '0';
        }
        std::fprintf(out_, "%s%s", row ? ", " : "", quoted(line).c_str());
    }
    std::fprintf(out_, "]}\n");
    std::fflush(out_);

    core::logf("sweep: %zu tiles, %d passes, %d settling and %d measured frames a tile%s",
               spots_.size(), passes_, settle_, frames_, fight_ ? ", fighting back" : "");
    core::logTap(&Sweep::tap, this);
    samples_.reserve(size_t(frames_));
    return true;
}

void Sweep::putDown(const Spot& spot) {
    play_->setDown(spot.column, spot.row);
    landedColumn_ = play_->realm().hero().column();
    landedRow_ = play_->realm().hero().row();
    target_ = 0;
}

void Sweep::fightHand() {
    // The arena's hand (PlayMode::arenaHand), held to what is near the tile: the nearest living
    // monster in reach, asked once and again only when that one is down. A far one is left
    // alone, or the hero would chase across the map and measure somewhere else.
    const sim::Realm& realm = play_->realm();
    const sim::Body& hero = realm.hero();
    if (!hero.alive()) return;
    const sim::Body* held = target_ ? realm.find(target_) : nullptr;
    if (held && held->alive()) return;
    const sim::Body* nearest = nullptr;
    float best = fightReach_ * fightReach_;
    for (const sim::Body& body : realm.bodies()) {
        if (!body.monster() || !body.alive()) continue;
        const float dx = body.x - float(landedColumn_), dy = body.y - float(landedRow_);
        if (dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            nearest = &body;
        }
    }
    target_ = nearest ? nearest->id : 0;
    if (nearest) play_->fight(target_);
}

void Sweep::before() {
    if (!active()) return;
    {
        std::lock_guard<std::mutex> hold(lock_);
        said_.clear();
    }
    if (frame_ == 0) putDown(spots_[spot_]);
    if (fight_) fightHand();
}

std::string Sweep::happeningsNow() {
    // Only on a frame a tick ran in: the realm keeps the last step's list until the next step,
    // so reading it on every frame would name one blow on every frame of its tick.
    const sim::Realm& realm = play_->realm();
    if (realm.tick() == lastTick_) return {};
    lastTick_ = realm.tick();
    tickMsSum_ += play_->tickMs();
    ++ticks_;

    struct Named {
        std::string what;
        int count;
    };
    std::vector<Named> named;
    const content::Tables& tables = *realm.tables();
    auto label = [&](uint32_t id) -> std::string {
        const sim::Body* body = id ? realm.find(id) : nullptr;
        if (!body) return "";
        if (body->player) return "hero";
        if (body->kind >= 0 && size_t(body->kind) < tables.kinds.size())
            return tables.kinds[size_t(body->kind)].label;
        return "folk";
    };
    for (const sim::Happening& one : realm.happenings()) {
        const sim::Body* who = realm.find(one.who);
        const bool hero = who && who->player;
        if (one.what == sim::What::Hit || one.what == sim::What::Missed) {
            const sim::Body* whom = realm.find(one.whom);
            if (hero || (whom && whom->player)) ++blows_;
        }
        if (one.what == sim::What::Died && who && who->player) ++deaths_;
        if (one.what == sim::What::Died && who && who->monster()) ++kills_;
        const char* name = worthNaming(one.what);
        if (!name) continue;
        std::string what = std::string(name) + " " + label(one.who);
        if (one.what == sim::What::Cast || one.what == sim::What::Loosed)
            what += " skill " + std::to_string(one.a);
        bool counted = false;
        for (Named& n : named) {
            if (n.what == what) {
                ++n.count;
                counted = true;
                break;
            }
        }
        if (!counted) named.push_back({what, 1});
    }
    std::string out;
    for (const Named& n : named) {
        if (!out.empty()) out += "; ";
        out += "tick: " + n.what;
        if (n.count > 1) out += " x" + std::to_string(n.count);
    }
    return out;
}

void Sweep::after(double frameMs) {
    if (!active()) return;
    std::string cause = happeningsNow();
    {
        std::lock_guard<std::mutex> hold(lock_);
        for (const std::string& line : said_) {
            if (!cause.empty()) cause += "; ";
            cause += "log: " + line;
        }
    }
    int causeIndex = -1;
    if (!cause.empty()) {
        causeIndex = int(causes_.size());
        causes_.push_back(std::move(cause));
    }

    if (frame_ < settle_) {
        if (float(frameMs) > settleWorst_) {
            settleWorst_ = float(frameMs);
            settleWorstCause_ = causeIndex;
        }
    } else {
        const bgfx::Stats* s = bgfx::getStats();
        Sample one;
        one.ms = float(frameMs);
        one.gpu = float(double(s->gpuTimeEnd - s->gpuTimeBegin) * 1000.0 /
                        double(s->gpuTimerFreq));
        one.draws = s->numDraw;
        one.tris = s->numPrims[bgfx::Topology::TriList] + s->numPrims[bgfx::Topology::TriStrip];
        one.cause = causeIndex;
        samples_.push_back(one);

        const sim::Realm& realm = play_->realm();
        const sim::Body& hero = realm.hero();
        int awake = 0, near = 0;
        for (const sim::Body& body : realm.bodies()) {
            if (!body.monster() || !body.alive()) continue;
            if (body.temper != sim::Temper::Asleep) ++awake;
            const float dx = body.x - hero.x, dy = body.y - hero.y;
            if (dx * dx + dy * dy < 12.0f * 12.0f) ++near;
        }
        awakeMost_ = std::max(awakeMost_, awake);
        nearSum_ += near;
    }

    if (++frame_ < settle_ + frames_) return;
    writeSpot();
    frame_ = 0;
    if (++spot_ >= spots_.size()) {
        spot_ = 0;
        if (++pass_ >= passes_) {
            done_ = true;
            core::logf("sweep: done, %d passes over %zu tiles", passes_, spots_.size());
        }
    }
}

void Sweep::writeSpot() {
    const Spot& spot = spots_[spot_];
    std::vector<float> ms;
    ms.reserve(samples_.size());
    double sum = 0.0, gpu = 0.0, draws = 0.0, tris = 0.0;
    for (const Sample& one : samples_) {
        ms.push_back(one.ms);
        sum += one.ms;
        gpu += one.gpu;
        draws += one.draws;
        tris += one.tris;
    }
    const double n = std::max<size_t>(1, samples_.size());
    const double mean = sum / n;
    std::vector<float> sorted = ms;
    std::sort(sorted.begin(), sorted.end());
    const float p99 = sorted.empty() ? 0.0f : sorted[std::min(sorted.size() - 1,
                                                              size_t(0.99 * double(sorted.size())))];
    const float worst = sorted.empty() ? 0.0f : sorted.back();

    std::fprintf(out_,
                 "{\"pass\": %d, \"spot\": %s, \"hot\": %s, \"asked\": [%d, %d], "
                 "\"tile\": [%d, %d], \"frames\": %zu, \"mean\": %.4f, \"p99\": %.4f, "
                 "\"worst\": %.4f, \"gpu\": %.4f, \"draws\": %.1f, \"tris\": %.0f, "
                 "\"tick_ms\": %.4f, \"ticks\": %d, \"awake\": %d, \"near\": %.2f, "
                 "\"blows\": %d, \"kills\": %d, \"deaths\": %d, \"settle_worst\": %.3f, "
                 "\"settle_cause\": %s, \"hitches\": [",
                 pass_, quoted(spot.name).c_str(), spot.hot ? "true" : "false", spot.column,
                 spot.row, landedColumn_, landedRow_, samples_.size(), mean, p99, worst, gpu / n,
                 draws / n, tris / n, ticks_ ? tickMsSum_ / ticks_ : 0.0, ticks_, awakeMost_,
                 nearSum_ / n, blows_, kills_, deaths_, settleWorst_,
                 settleWorstCause_ >= 0 ? quoted(causes_[size_t(settleWorstCause_)]).c_str()
                                        : "null");
    // A hitch is a frame over twice the tile's mean: what happened on it, and on the frame
    // before, since a tick's happening is drawn -- and its sheet first read -- a frame later.
    bool first = true;
    int listed = 0;
    for (size_t i = 0; i < samples_.size() && listed < 24; ++i) {
        if (samples_[i].ms <= 2.0 * mean) continue;
        std::string cause;
        if (samples_[i].cause >= 0) cause = causes_[size_t(samples_[i].cause)];
        if (i > 0 && samples_[i - 1].cause >= 0) {
            if (!cause.empty()) cause += "; ";
            cause += "(frame before) " + causes_[size_t(samples_[i - 1].cause)];
        }
        std::fprintf(out_, "%s{\"frame\": %zu, \"ms\": %.3f, \"x\": %.2f, \"cause\": %s}",
                     first ? "" : ", ", i, samples_[i].ms, samples_[i].ms / mean,
                     cause.empty() ? "null" : quoted(cause).c_str());
        first = false;
        ++listed;
    }
    std::fprintf(out_, "]}\n");
    std::fflush(out_);
    core::logf("sweep: pass %d tile %s %d,%d: mean %.3f ms, p99 %.3f, worst %.3f, %.0f draws",
               pass_ + 1, spot.name.empty() ? "grid" : spot.name.c_str(), spot.column, spot.row,
               mean, p99, worst, draws / n);

    samples_.clear();
    causes_.clear();
    settleWorst_ = 0.0f;
    settleWorstCause_ = -1;
    tickMsSum_ = 0.0;
    ticks_ = 0;
    awakeMost_ = 0;
    nearSum_ = 0.0;
    blows_ = kills_ = deaths_ = 0;
}

void Sweep::abort(const char* why) {
    if (!active()) return;
    core::logError("sweep: cut short at pass %d tile %zu of %zu: %s", pass_ + 1, spot_ + 1,
                   spots_.size(), why);
    done_ = true;
    close();
}

void Sweep::close() {
    core::logTap(nullptr, nullptr);
    play_ = nullptr;
    if (out_) std::fclose(out_);
    out_ = nullptr;
}

}  // namespace mu::app
