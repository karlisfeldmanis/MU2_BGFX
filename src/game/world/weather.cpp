#include "game/world/weather.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/sound.h"

namespace mu::game {
namespace {

// How long each of Noria's spells lasts, in seconds, drawn afresh each time between these.
// Invention, judged: long enough that a spell is a mood and not a flicker, short enough that a
// session of hunting sees both. Lengthened 2026-09-29 (the user: "rain is happening longer and
// daytime also is longer"), from 2-4 min dry and 1-2 min wet.
constexpr float kDryLow = 300.0f, kDryHigh = 540.0f;
constexpr float kWetLow = 180.0f, kWetHigh = 300.0f;

// Dry to full rain: MU steps RainCurrent by one a frame at 25 frames a second, and a hundred
// steps fills the pool (CreateHeavenRain's `RainCurrent * MAX_LEAVES / 100`).
constexpr float kTurnSeconds = 100.0f / 25.0f;

// rand_fps_check(512) against the 60-a-second frame it is written for: one frame in 512.
constexpr float kBirdEvery = 512.0f / 60.0f;

// How loud the rain's loop is at full rain, the same in every world: what Lorencia's drizzle
// was heard at when the loop followed pour(), a third of the file's level. It follows the
// wetness alone now, not how many drops fall -- the user, 2026-09-29, hearing Noria's downpour
// some 10 dB over Lorencia's: "both has to be same".
constexpr float kRainLevel = 0.33f;

// How far apart the claps are, in seconds of full rain: a storm heard now and then under the
// rain rather than a barrage -- one to three in a wet spell of three to five minutes. The
// user, 2026-09-29: "lightning has to be not too often", against 20 to 50 s at first.
// Invention, judged. --weather storm closes it to 8 to 16.
constexpr float kThunderLow = 60.0f, kThunderHigh = 180.0f;
// The share the rain must be in at before the sky cracks: the storm is inside the rain.
constexpr float kThunderShare = 0.8f;

// A clap's loudness made into its flash, a value every Sound::kLoudStep. A hit is a rise of
// 6 dB over the mean of the 0.3 s before it, louder than -30 dBFS; it lights by how loud it is,
// nothing at -30 dB to all at -8 dB, and dies away over about a tenth of a second. The bolt is
// the first hit over -22 dB: for 0.7 s from just before it the hits light at their full, so
// the crack flickers with its own strokes, and outside that at a quarter, so the rumble's
// swells are a glow in the cloud. Measured on the seven claps, where every swell lit alike
// was a five-second strobe. The claps share one level (tools/thunder.py), so a far one is a
// dimmer flash as well as a quieter sound.
std::vector<float> flashOf(const std::vector<float>& loud) {
    constexpr float kStep = Sound::kLoudStep;
    const size_t back = size_t(0.3f / kStep + 0.5f);
    const size_t n = loud.size();
    const auto power = [](float db) { return std::pow(10.0, double(db) / 10.0); };
    std::vector<float> hit(n, 0.0f);
    double sum = 0.0;
    size_t bolt = n;
    for (size_t i = 0; i < n; ++i) {
        const size_t count = std::min(i, back);
        const double before = count > 0 ? sum / double(count) : 1e-12;
        const float rise = loud[i] - float(10.0 * std::log10(before + 1e-12));
        if (rise > 6.0f && loud[i] > -30.0f) {
            hit[i] = std::min(1.0f, (loud[i] + 30.0f) / 22.0f);
            if (bolt == n && loud[i] > -22.0f) bolt = i;
        }
        sum += power(loud[i]);
        if (i >= back) sum -= power(loud[i - back]);
    }
    const size_t from = bolt == n ? n : bolt - std::min(bolt, size_t(0.1f / kStep));
    const size_t to = bolt == n ? n : bolt + size_t(0.7f / kStep);
    const float decay = std::exp(-kStep / 0.12f);
    std::vector<float> flash(n, 0.0f);
    float now = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float weight = i >= from && i < to ? 1.0f : 0.25f;
        now = std::max(now * decay, hit[i] * weight);
        flash[i] = now;
    }
    return flash;
}

}  // namespace

float Weather::random01() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ & 0xFFFFFFu) / float(0x1000000u);
}

void Weather::strikeLater() {
    thunderIn_ = storm_ ? 8.0f + random01() * 8.0f
                        : kThunderLow + random01() * (kThunderHigh - kThunderLow);
}

void Weather::open(const std::string& world, Sound* sound, const std::string& force) {
    shutdown();
    sound_ = sound;
    // Lorencia too since 2026-09-29 (the user: "enable also rain in lorencia"), on Noria's
    // spells; its night has no wet sheet, so the rain there is drops, rings and sound only.
    // And thin: a third of the pool at its heaviest (the user: "rain in lorencia too much
    // visible"), a drizzle under the moon rather than Noria's downpour.
    rains_ = world == "noria" || world == "lorencia";
    peak_ = world == "lorencia" ? 0.33f : 1.0f;
    forest_ = world == "noria";
    if (force == "rain" || force == "dry" || force == "storm") {
        forced_ = true;
        rains_ = force != "dry";
        storm_ = force == "storm";
        wet_ = rains_;
        share_ = wet_ ? 1.0f : 0.0f;
        strikeLater();
        if (storm_) thunderIn_ = 3.0f;
    } else if (force == "cycle") {
        // Short spells for watching the whole turn: 20 s dry, then 30 s wet, again and again.
        cycle_ = rains_;
        wet_ = false;
        left_ = 20.0f;
    } else {
        // A world that rains starts dry, with its first spell short, so a walk in finds leaves
        // and does not wait the whole of a long dry spell to see the rain.
        wet_ = false;
        left_ = rains_ ? kDryLow * 0.5f : 0.0f;
    }
    birdIn_ = kBirdEvery * (0.5f + random01());
    if (sound_) {
        if (rains_) rainSound_ = sound_->load("world_rain", false);
        if (forest_) forestSound_ = sound_->load("world_forest", false);
        if (rains_) thunderSound_ = sound_->load("world_thunder", false);
        for (int f = 0; f < sound_->files(thunderSound_); ++f) {
            flashes_.push_back(flashOf(sound_->loudness(thunderSound_, f)));
        }
    }
    if (rains_ || forest_) {
        core::logf("weather %s: %s%s", world.c_str(),
                   forced_ ? (storm_ ? "a storm, held by --weather"
                           : wet_ ? "raining, held by --weather"
                                  : "dry, held by --weather")
                           : (rains_ ? "dry and wet spells in turn" : "never rains"),
                   forest_ ? ", the forest's birdsong" : "");
    }
}

void Weather::shutdown() {
    if (sound_ && rainSound_ >= 0) sound_->loop(rainSound_, false);
    *this = Weather();
}

void Weather::update(float seconds, bool indoors) {
    if (rains_ && !forced_) {
        left_ -= seconds;
        if (left_ <= 0.0f) {
            wet_ = !wet_;
            left_ = cycle_ ? (wet_ ? 30.0f : 20.0f)
                    : wet_ ? kWetLow + random01() * (kWetHigh - kWetLow)
                           : kDryLow + random01() * (kDryHigh - kDryLow);
            core::logf("weather: %s for %.0f s", wet_ ? "rain" : "dry", left_);
            // The first clap a while after the rain is in, and not always inside a short spell.
            if (wet_) thunderIn_ = 30.0f + random01() * 60.0f;
        }
    }
    const float target = rains_ && wet_ ? 1.0f : 0.0f;
    const float step = seconds / kTurnSeconds;
    share_ = share_ < target ? std::min(target, share_ + step) : std::max(target, share_ - step);

    if (!sound_) return;
    if (rainSound_ >= 0) {
        sound_->loop(rainSound_, share_ > 0.0f && !indoors);
        sound_->level(rainSound_, share_ * kRainLevel);
    }
    // The thunder: counted down only while the rain is in, and heard under a roof as well.
    if (thunderSound_ >= 0 && wet_ && share_ >= kThunderShare) {
        thunderIn_ -= seconds;
        if (thunderIn_ <= 0.0f) {
            sound_->play(thunderSound_);
            strikeLater();
            core::logf("weather: thunder, the next in %.0f s", thunderIn_);
        }
    }
    // And its light, where the ear is in the clap when this frame is shown -- a frame from
    // now, which is the one thing counted rather than read.
    flash_ = 0.0f;
    float at = 0.0f;
    const int clap = sound_->heard(thunderSound_, seconds, &at);
    if (clap >= 0 && size_t(clap) < flashes_.size() && at >= 0.0f) {
        const std::vector<float>& curve = flashes_[size_t(clap)];
        const float index = at / Sound::kLoudStep;
        const size_t i = size_t(index);
        if (i + 1 < curve.size()) {
            const float t = index - float(i);
            flash_ = curve[i] + (curve[i + 1] - curve[i]) * t;
        }
    }

    if (forestSound_ >= 0) {
        birdIn_ -= seconds;
        if (birdIn_ <= 0.0f) {
            birdIn_ = kBirdEvery * (0.5f + random01());
            if (!indoors && share_ < 0.5f) sound_->play(forestSound_);
        }
    }
}

}  // namespace mu::game
