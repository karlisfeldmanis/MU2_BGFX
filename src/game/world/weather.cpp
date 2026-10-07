#include "game/world/weather.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "game/sound.h"

namespace mu::game {
namespace {
// Tarkan's steady sandstorm: always this far into the storm's light, wind and sound. Ours.
constexpr float kSandShare = 0.2f;
// Icarus's rain: MU's never stops (CreateHeavenRain, RainTarget = MAX_LEAVES / 2,
// ZzzEffectFireLeave.cpp:431-434), every leaf slot a drop; ours a faint steady share of the
// pool, under Lorencia's drizzle (the user's 'rain faint'). docs/icarus-port.md.
constexpr float kSkyShare = 0.25f;


// How long each of Noria's spells lasts, in seconds, drawn afresh each time between these.
// Invention, judged: long enough that a spell is a mood and not a flicker, short enough that a
// session of hunting sees both. Lengthened 2026-09-29 (the user: "rain is happening longer and
// daytime also is longer"), from 2-4 min dry and 1-2 min wet; the dry spell doubled again the
// same day (the user: "we need bigger intervals between daytime and rain"), from 5-9 min.
constexpr float kDryLow = 600.0f, kDryHigh = 1080.0f;
constexpr float kWetLow = 180.0f, kWetHigh = 300.0f;

// Dry to full rain: MU steps RainCurrent by one a frame at 25 frames a second, and a hundred
// steps fills the pool (CreateHeavenRain's `RainCurrent * MAX_LEAVES / 100`).
constexpr float kTurnSeconds = 100.0f / 25.0f;

// Devias's blizzard, the rain's shape for snow and all of it this game's (the user, 2026-09-29:
// "logic has to be similar like rain, lighting change it becomes darker ... strong snow wind
// effect, still playable"). The calm is the dry spell's length; the storm is shorter than a
// wet spell, two to four minutes, and it rolls in over eighteen seconds where rain takes MU's
// four -- a storm that arrives in a blink reads as a switch. Invention, judged.
constexpr float kStormLow = 120.0f, kStormHigh = 240.0f;
constexpr float kBuildSeconds = 18.0f;
// Under a roof the howl is muffled to this share rather than cut, over this long a turn, so a
// doorway is a step into shelter and not a click. The light takes the same turn (shelter()).
constexpr float kShelteredLevel = 0.25f;
constexpr float kShelterSeconds = 0.6f;

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

void Weather::summon(bool on) {
    if (!rains_ || snows_ || sky_ || forced_ || on == summoned_) return;
    summoned_ = on;
    if (on) {
        ownPeak_ = peak_;
        // Twice Lorencia's drizzle and no more: the user found its full pool too much
        // (2026-09-29), and an invasion is a storm, not a flood. Ours.
        peak_ = std::max(peak_, std::min(1.0f, 2.0f * peak_));
        wet_ = true;
        thunderIn_ = 2.0f;
        core::logf("weather: the invasion's storm");
    } else {
        peak_ = ownPeak_;
        wet_ = false;
        left_ = kDryLow * 0.5f;
        core::logf("weather: the invasion's storm passes");
    }
}

void Weather::sync(int serverRain) {
    if (serverRain < 0 || !rains_ || forced_ || summoned_ || cycle_ || steady_ > 0.0f) return;
    const bool serverWet = serverRain > 0;
    // The server's spell, not this client's: the local clock must never run out under it, or
    // update() turns the weather over on its own every minute and the next frame's sync turns it
    // back -- a frame of the wrong weather, a log line and a thunder clock rolled again each time.
    left_ = 1.0e9f;
    if (wet_ != serverWet) {
        wet_ = serverWet;
        if (wet_) thunderIn_ = 30.0f + random01() * 60.0f;
        core::logf("weather: synced to server, %s", wet_ ? (snows_ ? "blizzard" : "rain") : (snows_ ? "calm" : "dry"));
    }
}

void Weather::strikeLater() {
    thunderIn_ = storm_ || summoned_ ? 8.0f + random01() * 8.0f
                        : kThunderLow + random01() * (kThunderHigh - kThunderLow);
}

void Weather::open(const std::string& world, Sound* sound, const std::string& force) {
    shutdown();
    sound_ = sound;
    // Lorencia too since 2026-09-29 (the user: "enable also rain in lorencia"), on Noria's
    // spells; its night has no wet sheet, so the rain there is drops, rings and sound only.
    // And thin: a third of the pool at its heaviest (the user: "rain in lorencia too much
    // visible"), a drizzle under the moon rather than Noria's downpour.
    // Devias's wet spell is the blizzard: no drops (its peak is nought, so pour() is), no
    // thunder, and its own loop; its light is sheets/worlds/devias_rain.json like any wet spell.
    // Tarkan's sandstorm is Devias's blizzard's shape too, its sand blown by the same pool
    // (Leaves::setSand) and its light sheets/worlds/tarkan_rain.json. Ours, as Devias's is (the
    // user, 2026-10-05: 'dust effect which has devias, this is a desert style map').
    snows_ = world == "devias" || world == "tarkan";
    sky_ = world == "icarus";
    rains_ = world == "noria" || world == "lorencia" || snows_ || sky_;
    peak_ = world == "lorencia" ? 0.33f : snows_ ? 0.0f : 1.0f;
    jungle_ = world == "noria";
    // Tarkan's never stops, and never builds to Devias's full blizzard: the wind always up at
    // kSandShare, no calm and no spell (the user, 2026-10-05: 'tarkan storm has to happpen all the
    // time but not so strong like its in devias'). Held as a forced spell; --weather still wins.
    if (world == "tarkan" && force.empty()) {
        forced_ = true;
        wet_ = true;
        steady_ = kSandShare;
        share_ = kSandShare;
    }
    // Icarus's never stops either, and is silent: MU plays no rain and its thunder lines are
    // commented out (SceneManager.cpp:885-895); aHeaven is the whole of its air.
    if (sky_ && force.empty()) {
        forced_ = true;
        wet_ = true;
        steady_ = kSkyShare;
        share_ = kSkyShare;
    }
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
    } else if (steady_ <= 0.0f) {
        // A world that rains starts dry, with its first spell short, so a walk in finds leaves
        // and does not wait the whole of a long dry spell to see the rain.
        wet_ = false;
        left_ = rains_ ? kDryLow * 0.5f : 0.0f;
    }
    if (sound_) {
        if (snows_) blizzardSound_ = sound_->load("world_blizzard", false);
        if (rains_ && !snows_ && !sky_) rainSound_ = sound_->load("world_rain", false);
        if (jungle_) jungleSound_ = sound_->load("world_jungle", false);
        if (rains_ && !snows_ && !sky_) thunderSound_ = sound_->load("world_thunder", false);
        for (int f = 0; f < sound_->files(thunderSound_); ++f) {
            flashes_.push_back(flashOf(sound_->loudness(thunderSound_, f)));
        }
    }
    if (rains_ || jungle_) {
        core::logf("weather %s: %s%s", world.c_str(),
                   steady_ > 0.0f ? (sky_ ? "a steady faint rain" : "a steady sandstorm")
                   : forced_ ? (storm_ ? "a storm, held by --weather"
                           : wet_ ? (snows_ ? "a blizzard, held by --weather"
                                            : "raining, held by --weather")
                                  : "dry, held by --weather")
                           : (snows_ ? "calm and blizzard in turn"
                              : rains_ ? "dry and wet spells in turn" : "never rains"),
                   jungle_ ? ", the jungle's day" : "");
    }
}

void Weather::shutdown() {
    if (sound_ && rainSound_ >= 0) sound_->loop(rainSound_, false);
    if (sound_ && jungleSound_ >= 0) sound_->loop(jungleSound_, false);
    if (sound_ && blizzardSound_ >= 0) sound_->loop(blizzardSound_, false);
    *this = Weather();
}

void Weather::update(float seconds, bool indoors) {
    if (rains_ && !forced_ && !summoned_) {
        left_ -= seconds;
        if (left_ <= 0.0f) {
            wet_ = !wet_;
            // --weather cycle: a blizzard is held a minute, so its eighteen-second build leaves
            // forty at full before it eases back into the calm.
            left_ = cycle_ ? (wet_ ? (snows_ ? 60.0f : 30.0f) : 20.0f)
                    : wet_ ? (snows_ ? kStormLow + random01() * (kStormHigh - kStormLow)
                                     : kWetLow + random01() * (kWetHigh - kWetLow))
                           : kDryLow + random01() * (kDryHigh - kDryLow);
            core::logf("weather: %s for %.0f s",
                       wet_ ? (snows_ ? "blizzard" : "rain") : (snows_ ? "calm" : "dry"), left_);
            // The first clap a while after the rain is in, and not always inside a short spell.
            if (wet_) thunderIn_ = 30.0f + random01() * 60.0f;
        }
    }
    const float target = rains_ && wet_ ? (steady_ > 0.0f ? steady_ : 1.0f) : 0.0f;
    const float step = seconds / (snows_ ? kBuildSeconds : kTurnSeconds);
    share_ = share_ < target ? std::min(target, share_ + step) : std::max(target, share_ - step);
    const float shelter = indoors ? 1.0f : 0.0f;
    const float turn = seconds / kShelterSeconds;
    sheltered_ = sheltered_ < shelter ? std::min(shelter, sheltered_ + turn)
                                      : std::max(shelter, sheltered_ - turn);

    if (!sound_) return;
    if (blizzardSound_ >= 0) {
        sound_->loop(blizzardSound_, share_ > 0.0f);
        sound_->level(blizzardSound_, share_ * (1.0f - (1.0f - kShelteredLevel) * sheltered_));
    }
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

    // The jungle is what the rain is not: all of it dry, none of it once the rain is in, and
    // the four-second turn between is the crossfade.
    if (jungleSound_ >= 0) {
        sound_->loop(jungleSound_, share_ < 1.0f && !indoors);
        sound_->level(jungleSound_, 1.0f - share_);
    }
}

}  // namespace mu::game
