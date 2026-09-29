#include "game/world/weather.h"

#include <algorithm>

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

}  // namespace

float Weather::random01() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ & 0xFFFFFFu) / float(0x1000000u);
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
    if (force == "rain" || force == "dry") {
        forced_ = true;
        rains_ = force == "rain";
        wet_ = rains_;
        share_ = wet_ ? 1.0f : 0.0f;
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
    }
    if (rains_ || forest_) {
        core::logf("weather %s: %s%s", world.c_str(),
                   forced_ ? (wet_ ? "raining, held by --weather" : "dry, held by --weather")
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
        }
    }
    const float target = rains_ && wet_ ? 1.0f : 0.0f;
    const float step = seconds / kTurnSeconds;
    share_ = share_ < target ? std::min(target, share_ + step) : std::max(target, share_ - step);

    if (!sound_) return;
    if (rainSound_ >= 0) {
        sound_->loop(rainSound_, share_ > 0.0f && !indoors);
        sound_->level(rainSound_, pour());
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
