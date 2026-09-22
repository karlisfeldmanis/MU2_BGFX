// What the game sounds like: the cooked showing's sound events, played through miniaudio.
// The first of them is the level-up's, and this is only as much player as that needs -- a
// named event, preloaded, not placed in the world. Placed sounds (a blow on a spider across
// the clearing) are sprint 6's remaining half and will want a listener; nothing here stops
// that being added.
//
// **Sync is the point of it.** A sound that fires on the same frame as its effect still lands
// late by two things this corrects, both measured rather than guessed:
//   * the file's silent lead: MU's wavs open with up to a quarter second of nothing. Measured
//     at load, per file, as the first sample over -50 dBFS, and skipped;
//   * the device's own buffer: what is queued now is heard a period or two from now. The
//     start point is moved on by that much too, so what reaches the ear at the moment the
//     picture changes is the part of the sound that belongs to that moment.
// Not the cooked `onset`: that is where a HIT lands in its file, a third of the way up this
// one's swell, and skipping to it would cut off the swell the flares rise with.
//
// It is `game`, as PLAN.md's layers put sound. It holds no state the rules read.
#pragma once

#include <memory>
#include <string>

#include "content/showing.h"

namespace mu::game {

class Sound {
public:
    Sound();
    ~Sound();
    Sound(const Sound&) = delete;
    Sound& operator=(const Sound&) = delete;

    // Opens the device and preloads the events named in `wanted` (comma free, one per entry of
    // the null-terminated list). `muted` keeps everything working and logged at zero volume,
    // for review runs. False when there is no device, which is not fatal: the game is silent.
    bool open(const std::string& assetDir, const content::Showing& table, const char* const* wanted,
              bool muted);
    void shutdown();

    // Starts `event` now, from where its sound begins. One voice an event: playing it while it
    // is still sounding starts it again, which is MU's own `LoadWaveFile(..., 1)` for the
    // level-up -- two in the same instant are one sound.
    void play(const std::string& event);

    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mu::game
