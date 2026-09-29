// The weather: how much of the air is rain, and what the air sounds like.
//
// MU's own rain is one number, `RainCurrent`, walked a step a frame toward `RainTarget`
// (ZzzEffectFireLeave.cpp, MoveLeaves), and read by every pool that falls: in CreateHeavenRain
// the first `RainCurrent` share of the leaf slots are drops and the rest stay leaves. So the
// rain does not arrive on top of the leaves -- it takes their places, and the leaves come back
// as it goes. That is the rule kept here, as `rain()`, a share from 0 to 1, which Leaves reads.
//
// Where the rain is set is this game's and not MU's. MU rains on Heaven always, on Chaos Castle
// always and on Lorencia never (its `RainTarget` is never raised outside those worlds), and
// Noria has leaves only. **Noria's spells are an invention**, the user's (2026-09-28): the
// map is tropical, so it has a dry spell with its leaves and a wet spell with drops, taking
// turns. The spell lengths are judged, not traced. The walk between them is MU's own: a
// hundred steps at 25 a second, four seconds from dry to full rain.
//
// And the sounds that go with it, which are unplaced ambients: the rain loop, whose level is
// the share (sounds.json's `world_rain`, an invention with the rain), and Noria's forest
// birdsong -- `world_forest`, MU's SOUND_FOREST01, which PlayWorldAmbientSounds fires for
// WD_3NORIA as a one-shot on rand_fps_check(512), about every eight and a half seconds. The
// birds keep quiet while it rains, which is this game's.
#pragma once

#include <cstdint>
#include <string>

namespace mu::game {

class Sound;

class Weather {
public:
    // `force`: "" for the world's own spells, "rain" to rain from the first frame, "dry" to
    // never rain -- `--weather`, so a review shot does not wait out a dry spell -- or "cycle"
    // for the world's own spells cut short, 20 s dry and 30 s wet.
    void open(const std::string& world, Sound* sound, const std::string& force);
    void shutdown();

    // One frame. `indoors` takes the rain's sound off under a roof, with the wind's switch.
    void update(float seconds, bool indoors);

    // How wet the spell is, 0 dry to 1 raining: what the light blends to its wet sheet by.
    float rain() const { return share_; }
    // How much of the pool falls as drops, 0 to 1: the wetness times the world's peak, so
    // Lorencia's drizzle darkens the night fully with a third of Noria's drops.
    float pour() const { return share_ * peak_; }
    bool rains() const { return rains_; }

private:
    float random01();

    Sound* sound_ = nullptr;
    bool rains_ = false;    // this world has a wet spell at all
    bool forest_ = false;   // this world has MU's forest birdsong
    bool forced_ = false;   // --weather held the spell
    bool cycle_ = false;    // --weather cycle: short spells, to watch the turn
    bool wet_ = false;      // the spell now: dry or wet
    float peak_ = 1.0f;     // the share a wet spell rises to
    float left_ = 0.0f;     // seconds of it left
    float share_ = 0.0f;    // RainCurrent, as a share
    float birdIn_ = 0.0f;   // seconds to the next birdsong
    int rainSound_ = -1;
    int forestSound_ = -1;
    uint32_t seed_ = 0x9E3779B9u;
};

}  // namespace mu::game
