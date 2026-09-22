// What the game sounds like: the cooked showing's sound events, played through miniaudio.
// The level-up is at the ears; everything a monster says is placed in the world and heard from
// the character, on MU2's own model (client/core/Sounds.cs), which is MU's DSPlaySound.cpp:
//   * **A sound is an event, not a file.** One of an event's files is chosen at random per
//     play -- two bull roars, a budge dragon's grumble and bite -- which is what stops a fight
//     from ticking.
//   * **Two voices an event, and a new play steals the older.** LoadWaveFile's channel count
//     is two for every sound in the monster family and PlayBuffer walks them round-robin, so
//     six spiders biting at once are two spiders' worth of noise.
//   * **The ears are the character's, turned by the camera's yaw.** Update3DPositions takes
//     the vector from the hero to the emitter and turns it by the camera's heading: what is
//     heard is what the character can hear, panned to match what the screen shows. Done here
//     by moving and turning miniaudio's one listener -- the same arithmetic, said once.
//   * **It is flat.** Heights are dropped: MU hands SetPosition a zero for them.
// And the distance: MU scales the offset by 0.004 before DirectSound, whose minimum distance
// is 1.0, so a sound is at full volume within 250 units -- two and a half metres -- and falls
// as 1/d beyond, with no cull, no low-pass and no doppler. kCarry in sound.cpp.
//
// A voice may FOLLOW a body: PlayBuffer keeps the OBJECT* and Update3DPositions re-reads its
// position every frame while the voice sounds, so a bull that roars and charges takes the
// roar with it. See follow().
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

#include <cstdint>
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

    // Opens the device. `muted` keeps everything working and logged at zero volume, for
    // review runs. False when there is no device, which is not fatal: the game is silent.
    // `table` must outlive this; it is the showing's own.
    bool open(const std::string& assetDir, const content::Showing& table, bool muted);
    void shutdown();

    // Preloads an event, decoded whole, and answers its handle, or -1 for one nothing cooked
    // (which a caller treats as silence). Loading the same name twice answers the same handle.
    // `placed` is whether it is heard from somewhere; the level-up is not. `quietly` leaves a
    // missing event out of the error log -- a breed with no cry is the ordinary case.
    int load(const std::string& event, bool placed, bool quietly = false);

    // Starts an unplaced event now, from where its sound begins. Playing it while it is still
    // sounding starts it again, which is MU's own `LoadWaveFile(..., 1)` for the level-up.
    void play(const std::string& event);

    // Starts a placed event at a point on the ground, in world metres. `following` is the body
    // it belongs to, whose position follow() keeps it on, or 0 for a blow that lands at a
    // point and belongs to nothing -- MU's NULL.
    void playAt(int event, float x, float z, uint32_t following = 0);

    // Where the ears are: on the ground under the character, looking along `forward` (the
    // camera's heading, flattened). Once a frame, before or after the plays.
    void listen(float x, float z, float forwardX, float forwardZ);

    // Moves every voice that is still sounding and follows something to where `where` says
    // that body is now. A body `where` does not know keeps the voice where it last was, which
    // is the death cry outliving the monster that made it.
    using Where = bool (*)(void* context, uint32_t id, float* x, float* z);
    void follow(Where where, void* context);

    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mu::game
