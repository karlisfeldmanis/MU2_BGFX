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
// the share, the same in every world (sounds.json's `world_rain`, an invention with the rain), and Noria's
// jungle -- `world_jungle`, the user's loop (2026-09-29) in place of MU's SOUND_FOREST01, the
// birdsong PlayWorldAmbientSounds fired for WD_3NORIA as a one-shot on rand_fps_check(512).
// The jungle is the dry spell's: its level is what the rain is not, so it goes as the rain
// comes in and is back as it clears (the user: "when its raining then we dont use this
// sounds").
//
// And the thunder, which is this game's and not MU's (the user, 2026-09-29: "on top of the
// rain ... also thunderstorm effect with some screen lighting effect perfectly synced with
// sound"). Every wet spell is a storm: once the rain is in, a clap every one to three minutes,
// one of `world_thunder`'s seven (tools/thunder.py cut them from the user's recording). The
// lightning is not timed beside the sound but read off it: each clap's loudness curve, which
// Sound measured from its samples at load, is made into a flash curve here once, and every
// frame the flash is that curve at the point the ear is in the playing clap -- the mixer's own
// cursor. The crack flashes, flickering with its own hits, and the rumble after it only glows
// faintly in the cloud. flash() is what the light takes (app/context.cpp, TimeOfDay::rain).
//
// And Devias's blizzard, which is the rain's shape for snow and this game's too (the user,
// 2026-09-29): a calm and a storm in turn, the share walked between them over eighteen seconds,
// the light blended to sheets/worlds/devias_rain.json by it -- darker, not a whiteout, so
// the game stays playable -- and `world_blizzard` howling at the share, muffled under a roof
// rather than cut. No drops and no thunder. The wind in the snow is docs/devias-blizzard.md's
// second pass.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::game {

class Sound;

class Weather {
public:
    // `force`: "" for the world's own spells, "rain" to rain from the first frame, "dry" to
    // never rain -- `--weather`, so a review shot does not wait out a dry spell -- or "cycle"
    // for the world's own spells cut short, 20 s dry and 30 s wet; "storm" to rain from the
    // first frame with a clap every 8 to 16 seconds, to watch the lightning.
    void open(const std::string& world, Sound* sound, const std::string& force);
    void shutdown();

    // The Golden Invasion's storm (game/invasion_sky.h): the rain brought in at once over MU's
    // four-second walk and held, heavier than Lorencia's drizzle, with a clap close behind it
    // and then every 8 to 16 seconds; `false` gives the world its own spells back, starting
    // dry. Ours, the user's (2026-10-06): the dragon comes only in the rain. A world with no
    // rain (or Devias's and Tarkan's sand and snow, Icarus's mist) is left as it is.
    void summon(bool on);
    bool summoned() const { return summoned_; }

    // One frame. `indoors` takes the rain's and the jungle's sounds off under a roof, with the
    // wind's switch.
    void update(float seconds, bool indoors);

    // How wet the spell is, 0 dry to 1 raining: what the light blends to its wet sheet by.
    float rain() const { return share_; }
    // How much of the pool falls as drops, 0 to 1: the wetness times the world's peak, so
    // Lorencia's drizzle darkens the night fully with a third of Noria's drops.
    float pour() const { return share_ * peak_; }
    bool rains() const { return rains_; }
    // Whether this is a wet spell now, its own or the invasion's (summon).
    bool wet() const { return rains_ && wet_ && !snows_ && !sky_; }
    // Devias: the wet spell is a blizzard, and rain() is how far the storm is in.
    bool snows() const { return snows_; }
    // The lightning now, 0 none to 1 a near strike at its brightest: the playing clap's flash
    // curve where the ear is in it. update() reads it.
    float flash() const { return flash_; }

private:
    float random01();
    void strikeLater();

    Sound* sound_ = nullptr;
    bool rains_ = false;    // this world has a wet spell at all
    bool snows_ = false;    // and it is Devias's blizzard, not rain
    float sheltered_ = 0.0f;  // how far under a roof the ear is, for the blizzard's muffle
    bool jungle_ = false;   // this world has the jungle's daytime bed
    bool sky_ = false;      // Icarus: a steady faint rain, silent
    bool forced_ = false;   // --weather held the spell
    bool cycle_ = false;    // --weather cycle: short spells, to watch the turn
    bool storm_ = false;    // --weather storm: claps close together, to watch the lightning
    bool summoned_ = false;  // the invasion's storm, held (summon)
    float ownPeak_ = 1.0f;  // the world's own peak, given back when it ends
    bool wet_ = false;      // the spell now: dry or wet
    float peak_ = 1.0f;     // the share a wet spell rises to
    float left_ = 0.0f;     // seconds of it left
    float share_ = 0.0f;    // RainCurrent, as a share
    float steady_ = 0.0f;   // a world whose storm holds at one share: Tarkan's (weather.cpp)
    float thunderIn_ = 0.0f;  // seconds of full rain to the next clap
    float flash_ = 0.0f;
    int rainSound_ = -1;
    int jungleSound_ = -1;
    int thunderSound_ = -1;
    int blizzardSound_ = -1;
    std::vector<std::vector<float>> flashes_;  // each clap's flash, a value every Sound::kLoudStep
    uint32_t seed_ = 0x9E3779B9u;
};

}  // namespace mu::game
