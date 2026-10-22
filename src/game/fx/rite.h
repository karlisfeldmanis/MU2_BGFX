// The Rebirth rite: what a reset looks like where it is held, in front of Lugard in Devias.
//
// **Ours, all of it.** MU has no reset and no rite. The light from heaven, its pool and the gold
// runes are our own sheets (pipeline/rite_sheets.py); the flare and the Town Portal's walls are
// MU's. The show the user asked for (2026-10-22): "character walks in front of npc, kneels the light comes to character and
// explodes and character is rebirth", and "we will use ... soundreality-intro-sound-effect
// perfectly synced with light beam".
//
// **Its clock is the sound's.** The rite starts on the kneel, with the `rite` sound, and every
// beat below is a time into that one file, found by onset and loudness over 0.1 s bins:
//
//   0.0 - 3.0   the swell builds: the ground darkens, the rune circle draws itself, its walls
//               rise and gold motes drift up out of it;
//   3.0 - 3.15  a new layer enters, the clearest onset: the light falls from the sky onto him --
//               "like light coming from heaven", a wide stage beam ("more like in opera");
//   3.15 - 6.8  the plateau, brightening: the light holds, a dust of motes turning in it;
//   6.9         the first bass hit: the burst -- a flash, sparks thrown every way, and the first
//               of the ground explosion's two gold waves;
//   8.2 - 8.45  the loudest bass: its second wave, wider, dying with the bass by 9.9 s;
//   8.5         the bass drops away: the light lifts back into the sky;
//   9.5 - 10.5  he stands (clip 217's rise), a gold glow on him;
//   10.8 - 12   every light on him fades out together, the sound's tail playing on to 14 s.
//
// The hero's kneel is MU's PLAYER_RESPECT1, player.muc's clip 217, held at its low key by Play.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

class Rite {
public:
    // The beats, seconds after the kneel: the `rite` sound's own clock.
    static constexpr float kFalls = 3.0f;    // the light leaves the sky
    static constexpr float kLands = 3.15f;   // and strikes him
    static constexpr float kBursts = 6.9f;   // the burst, on the first bass hit: his level becomes 1
    static constexpr float kLifts = 8.5f;    // the light goes back up
    static constexpr float kStands = 9.5f;   // he rises off his knee
    static constexpr float kEnds = 12.0f;    // all of it gone off him, the sound's tail or not

    // Takes its sheets: its own (pipeline/rite_sheets.py), the Town Portal's wall and level-up's
    // flare. False when one is missing.
    bool open(const std::string& assetDir, content::Textures& textures);
    // Begun at `feet`, world metres, on the kneel. A second begins it again.
    void begin(const float feet[3]);
    void update(float seconds);
    // `eye` is the camera, world metres: the beam is turned to it.
    void gather(gfx::Effects& effects, const content::Ground& ground, const float eye[3]) const;
    bool live() const { return living_; }
    // The light it throws, which lights him and the ground for real (the user, 2026-10-22: "character
    // is not reacting to light, it has to"): a warm glow out of the circle, a strong gold from
    // above while the beam stands, a spike on the burst, his own glow after. One light.
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    float age() const { return age_; }

private:
    struct Mote {
        bool alive = false;
        float at[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float left = 0.0f, life = 1.0f, size = 0.05f;
        bool falls = false;  // a burst's spark: thrown, falls and bounces
    };

    bool ready() const;
    void spawnMote(bool fromBody);
    void burst();
    float roll(float lo, float hi);
    void gatherDisc(gfx::Effects& effects, const content::Ground& ground, bgfx::TextureHandle sheet,
                    gfx::Blend blend, float radius, float turn, const float colour[3], float alpha) const;
    void gatherWalls(gfx::Effects& effects, float turn, float alpha) const;
    void gatherBeam(gfx::Effects& effects, const float eye[3]) const;

    bgfx::TextureHandle runes_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle beam_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle rays_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle pool_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle ring_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle wall_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    bool living_ = false;
    bool burst_ = false;
    float feet_[3] = {0, 0, 0};
    float age_ = 0.0f;
    float owed_ = 0.0f;  // motes owed this frame, as a fraction
    uint32_t dice_ = 0x9e3779b9u;
    std::array<Mote, 256> motes_{};
};

}  // namespace mu::game
