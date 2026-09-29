// What the game sounds like: the cooked showing's sound events, played through miniaudio.
// The level-up is at the ears; everything a monster says is placed in the world. MU's own
// model (client/core/Sounds.cs in MU2, DSPlaySound.cpp in the client) is where this began:
//   * **A sound is an event, not a file.** One of an event's files is chosen at random per
//     play -- two bull roars, a budge dragon's grumble and bite -- which is what stops a fight
//     from ticking.
//   * **Two voices an event, and a new play steals the older.** LoadWaveFile's channel count
//     is two for every sound in the monster family and PlayBuffer walks them round-robin, so
//     six spiders biting at once are two spiders' worth of noise.
//   * **Loud by distance from the character**, 1/d past 2.5 m: MU scales the offset by 0.004
//     before DirectSound, whose minimum distance is 1.0. kCarry in sound.cpp.
//
// What an ARPG does over that, and MU does not -- **every one of these is an invention**, from
// docs/spatial-sound.md, steps A to D:
//   * **Left and right come from the screen.** A placed voice is not spatialised by miniaudio:
//     its pan is where the frame shows it, projected through the shot, narrowed so nothing
//     sits in one ear alone. MU turned the offset by the camera's yaw, which approximates the
//     same thing from the ground; this is the screen itself, so a thing high on a wall pans
//     where it is drawn.
//   * **The frame's edge is a fade, not a wall.** A voice goes to silence as it leaves the
//     picture rather than at 1/d for ever, and a new one is refused off the frame
//     (Play::emit). Before this a monster one step inside the frame was at its full 1/d and
//     one step out was nothing.
//   * **Far is duller as well as quieter.** Each placed voice runs through its own low-pass
//     that closes with distance: air absorption, the cheapest depth cue there is.
//   * **A budget and an importance.** At most kVoicesTotal placed voices sound at once. The
//     hero's own sounds outrank what is near him, which outranks the crowd, and when the
//     budget is full a new sound takes the least important, quietest voice or is refused.
//     The same event twice on one frame at one place is one voice a little louder -- six
//     spiders biting on a tick -- rather than two plays stealing from each other.
//   * **Buses.** The interface, the world and the ambience are mixed apart, and the hero's
//     big moments -- the level-up, a skill, his death -- duck the world and the ambience under
//     themselves for a moment.
//   * **A room.** The world's bus is sent to one reverb, a Freeverb written here: short and
//     mostly dry in the open, a small room's worth under a roof, eased between the two as the
//     wind is. Step E.
//   * **Walls.** A voice whose straight line to the character crosses a wall on the tile grid
//     is quieter and duller, eased so a monster stepping round a corner opens up rather than
//     clicks. The rules' own line of sight answers it, through walls(). Step F.
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
//     The level-up's alone: every other sound here is short and front-loaded, and skipping
//     the buffer cut off its impact (sound.cpp's start()).
// Not the cooked `onset`: that is where a HIT lands in its file, a third of the way up this
// one's swell, and skipping to it would cut off the swell the flares rise with.
//
// It is `game`, as PLAN.md's layers put sound. It holds no state the rules read.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
    // `table` must outlive this; it is the showing's own. `offline` opens no device at all:
    // the mix is pulled by render(), which is how tests/sound_test.cpp hears it.
    bool open(const std::string& assetDir, const content::Showing& table, bool muted,
              bool offline = false);
    void shutdown();

    // Preloads an event, decoded whole, and answers its handle, or -1 for one nothing cooked
    // (which a caller treats as silence). Loading the same name twice answers the same handle.
    // `placed` is whether it is heard from somewhere; the level-up is not. `quietly` leaves a
    // missing event out of the error log -- a breed with no cry is the ordinary case.
    int load(const std::string& event, bool placed, bool quietly = false);

    // Starts an unplaced event now, from where its sound begins. Playing it while it is still
    // sounding starts it again, which is MU's own `LoadWaveFile(..., 1)` for the level-up.
    // It also ducks the world under itself: this is the level-up's call and nothing else's.
    void play(const std::string& event);
    // The same, by the handle an unplaced load() gave: the interface's noises, which are the
    // player's own and heard at the ears.
    void play(int event);

    // An ambient: one unplaced sound, looping, on or off -- PlayBuffer(SOUND_WIND01, NULL,
    // true) against StopBuffer. Called every frame with what the world wants and does nothing
    // when that is already what is playing, the client's own shape. `event` is a handle from
    // an unplaced load().
    void loop(int event, bool wanted);
    // How loud an ambient is, 0 to 1 of its own level: the rain, which comes and goes by degrees
    // where the wind is only on or off. Under loop()'s fade, not instead of it.
    void level(int event, float level);
    // How long an event's first file runs, in seconds; 0 for none. What a caller holds a
    // sound busy for when MU would not start it again while it still sounds.
    float seconds(int event) const;

    // What one file of an event says over its length: its loudness in dBFS, one value every
    // kLoudStep seconds from the file's first sample, measured at load from the same decode
    // that finds the lead. Empty for a handle or file it lacks. The thunder's flash is read off
    // it (game/world/weather.cpp), so the light is cut from the samples the ear gets.
    static constexpr float kLoudStep = 0.01f;
    int files(int event) const;
    const std::vector<float>& loudness(int event, int file) const;
    // Which file of an unplaced event is sounding, and where in it the ear is when the frame
    // being drawn reaches the screen: the voice's own cursor, in seconds from the file's first
    // sample, less the device's buffer, plus `ahead` -- how long the frame takes to be shown.
    // Read off the mixer's clock rather than counted in frames, so a hitch cannot pull the two
    // apart. -1 when none sounds.
    int heard(int event, float ahead, float* seconds) const;

    // Starts a placed event at a point, in world metres. `following` is the body it belongs
    // to, whose position follow() keeps it on, or 0 for a blow that lands at a point and
    // belongs to nothing -- MU's NULL.
    void playAt(int event, float x, float y, float z, uint32_t following = 0);

    // Where the ears are and what the screen is: `hero` is the character's body, whose own
    // sounds come first; `at` is where he stands; `shot` is the frame's view times projection
    // in bx's row-vector order, which is what pans. Once a frame, before the plays.
    void listen(uint32_t hero, const float at[3], const float shot[16]);

    // Moves every voice that is still sounding and follows something to where `where` says
    // that body is now, then sets every sounding voice's pan, level and filter for this
    // frame. A body `where` does not know keeps the voice where it last was, which is the
    // death cry outliving the monster that made it.
    using Where = bool (*)(void* context, uint32_t id, float* x, float* y, float* z);
    void follow(Where where, void* context);

    // The hero's big moment: the world and the ambience lean back and come in again after.
    // play(name) does it itself; a caller does it for what only it knows is big.
    void duck();

    // A track of music, looping, streamed from `path` at `gain`: PlayMp3. Asking again for the
    // track already playing does nothing, as MU's PlayMp3 no-ops; another track replaces it.
    // Under the whole mix's level, so the menu's volume turns it down too.
    void music(const std::string& path, float gain = 0.6f);
    void stopMusic();

    // A spoken line, `relative` to the assets, played once and unplaced: a quest giver reading
    // his page. One at a time: a new line cuts the last.
    // Ours: MU 0.75 speaks no lines. A missing file is logged and nothing plays.
    void voice(const std::string& relative);
    void stopVoice();

    // The whole mix's level, 0 to 1: the game menu's volume. A muted run stays silent.
    void setVolume(float level);

    // The room the character is in, eased into over a moment. Dry is no reverb at all, which
    // nothing in the game asks for; it is how the test hears the pan unmixed.
    enum class Room { Dry, Open, Roofed };
    void room(Room which);

    // Whether the straight line between two points in world metres is clear of walls. Set
    // once and kept; `context` must outlive the Sound or be replaced. Null hears through
    // everything.
    using Clear = bool (*)(void* context, const float from[3], const float to[3]);
    void walls(Clear clear, void* context);

    // The mix, pulled rather than heard: `frames` stereo frames of float into `out`. Offline
    // only; answers how many were written.
    uint64_t render(float* out, uint64_t frames);

    // What the budget has done since open(): voices sounding now, and plays refused, merged
    // into one already sounding, and stolen from a less important voice.
    struct Tally {
        int sounding = 0;
        int refused = 0;
        int merged = 0;
        int stolen = 0;
    };
    Tally tally() const;

    bool isOpen() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mu::game
