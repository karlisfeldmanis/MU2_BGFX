// The Rebirth message: a gold halo with the reset count in it as a Roman numeral, REBIRTH under
// it and the character's name under that, where the map's name stands (game/ui/arrival.h).
//
// **Ours.** MU has no reset. The user chose it on 2026-10-22 from a design page ("Reborn ·
// halo", project/RebornC3.dc.html on the canvas board) and every number below is that page's,
// at 1080 lines, scaled with the screen's height:
//
//   name     Alegreya Sans SC Bold 17 px, line 20, letter-spacing 0.12 em, #D9CFBD; shadow
//            0 1 px blur 2 px black 50%; its foot at 78.5% of the screen, as the map name's is
//   word     REBIRTH 6 px above it: Cinzel 26 px, line 30, letter-spacing 0.46 em, #F3E8D4;
//            shadows 0 1 px blur 3 px black 90% and 0 0 blur 8 px black 50%. The page set it
//            in Cinzel SemiBold, which extern/ does not carry; this is Cinzel Bold.
//   halo     a 174 px box 7 px above REBIRTH: a darkening 340 px across, a flash, a glow, two
//            rings turning opposite ways, five rays falling onto it, gold motes and the numeral
//            -- Cinzel Bold 96 px in a gold gradient, a glint crossing it once.
//
// **Its clock is its sound's.** `rite_reborn` (content/sounds/sounds.json) is played on the
// frame the message starts (Play::takeReborn), and the page's timeline is the MP3's own:
// the swoosh to 0.27 s, the hit at 0.27 s -- the halo, the flash, the numeral --, a second
// thump at 0.375 s -- the outer ring --, held from 1.0 s, gone by 4.6 s. The cooked file is
// trimmed of the MP3's first 0.098 s and the game skips 8.5 ms more to -50 dBFS, so the clock
// starts at kHeard, where the sound is first heard.
#pragma once

#include <string>

#include "gfx/interface.h"

namespace mu::game {

class Reborn {
public:
    // Bakes the three faces and their shadows. Without them it draws nothing, and says so.
    void open(const gfx::Interface& interface);
    void shutdown();

    // A rebirth: `count` resets made, the latest included, and whose. The caller plays
    // `rite_reborn` on the same frame (Play::takeReborn).
    void announce(int count, const std::string& name);
    // Advances the clock and redraws while it is showing. The frame it was announced on is
    // drawn at the clock's start, not a frame into it.
    void update(float seconds, float width, float height);

    bool showing() const { return clock_ >= 0.0f && clock_ < kSeconds && !numeral_.empty(); }
    const gfx::Canvas& canvas() const { return canvas_; }

    static constexpr float kSeconds = 4.6f;  // the page's timeline, on the MP3's clock
    static constexpr float kHeard = 0.1065f;  // where on it the cooked sound is first heard

private:
    void rebuild(float width, float height);

    const gfx::Interface* interface_ = nullptr;
    gfx::Canvas canvas_;
    // The numeral and REBIRTH share the one sharp Cinzel Bold; each line has its own shadows.
    gfx::Face title_, numeralShadow_, wordTight_, wordSoft_, name_, nameShadow_;
    bgfx::TextureHandle titleTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle numeralShadowTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle wordTightTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle wordSoftTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle nameTexture_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle nameShadowTexture_ = BGFX_INVALID_HANDLE;
    std::string numeral_, who_;
    float clock_ = -1.0f;
    bool fresh_ = false;  // announced this frame: drawn at kHeard, not advanced past it
};

}  // namespace mu::game
