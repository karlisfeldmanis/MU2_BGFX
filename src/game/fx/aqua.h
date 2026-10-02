// Aqua Beam's beam: what MU lays out ahead of the wizard as he lets the spell go. MuMain's
// BITMAP_BOSS_LASER, sub-type 0, made at his hand with SOUND_FLASH (ZzzCharacter.cpp:4551-4554).
//
// **What MU does.** The hand is `CalcAddPosition(o, -20, -90, 100)`: twenty units to his side,
// ninety ahead and a hundred up. The beam is born there with LifeTime 20, Light (0.5, 0.7, 1),
// Scale 16 and a Direction of (0, -50, 0) turned to his angle (ZzzEffect.cpp:978-1001), and it
// does not move -- MoveEffect's tail passes the three lasers by (:8532). Every frame it is drawn
// as twenty BITMAP_SPARK + 1 sprites (spark_flash) at that scale, stepped fifty units along the
// direction -- a straight line ten metres long -- each lighting the ground two tiles round in the
// beam's colour (:8967-9004).
//
// **Ours, and marked**: each sprite at half MU's light, since twenty of them overlap several
// deep and at full light added to one white blot (Inferno's lesson, 2026-10-02: "explosions are
// to crazy") -- a quarter was tried first and left only the core, a thin tube; a third larger, so
// it stands taller than him; a fade over the last five of its twenty frames, where MU's stops dead; and three
// point lights along it standing for MU's twenty terrain lights. And a little smoke after it
// (the user, 2026-10-02: "lets add minimal smoke effect after beam is casted"): eight faint puffs
// of smoke01 strung along the beam as it fades, rising slowly, gone in two seconds -- Inferno's
// approved smoke, cooled a little toward the beam's blue. MU leaves nothing behind.
//
// Presentation only, `game` and not `sim`: the realm struck the beam's four circles on the tick
// (`Spread::Beam`). Pools are sized once and a frame allocates nothing.
#pragma once

#include <cstdint>
#include <string>

#include "content/ground.h"
#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Aqua {
public:
    // spark_flash off the table. False when it is missing.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A beam from his `feet`, along the flat world direction (`wayX`, `wayZ`), unit length.
    void cast(const float feet[3], float wayX, float wayZ);
    void update(float seconds);
    void gatherEffects(gfx::Effects& effects) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kFrames = 20.0f;        // LifeTime
    static constexpr int kSprites = 20;            // the render's loop
    static constexpr float kStep = 50.0f;          // units between them, Direction (0, -50, 0)
    static constexpr float kAhead = 90.0f;         // CalcAddPosition's -90: ahead of him
    static constexpr float kSide = 20.0f;          // and its -20: to his side
    static constexpr float kUp = 100.0f;
    static constexpr float kScale = 16.0f;
    static constexpr float kMetresPerScale = 0.32f;  // spark_flash, as fx/bolt.h measures it
    static constexpr float kLight[3] = {0.5f, 0.7f, 1.0f};
    // Ours (the head of this file).
    static constexpr float kShare = 0.5f;
    // And a third larger than MU's scale 16, so the beam stands taller than he does (the user,
    // 2026-10-02: "also i remember it was taller"; the wiki's Season clip has it so).
    static constexpr float kTaller = 1.35f;
    // And stretched up again, its middle lifted from MU's hundred units to a hundred and forty
    // so the height is above the grass rather than in it (the user, the same day again: "beam has
    // to be taller"): 1.9 of MU's height at the same width, then that by 4/3, 2.5 -- MU's beam was
    // sized for a 4:3 screen and this one is 16:9 ("we have 16:9 game not 4:3").
    static constexpr float kTallerUp = 2.5f;
    static constexpr float kLift = 140.0f;
    // **Not a box** (the user, 2026-10-02: "beam is very rectacngleish"): spark_flash does not
    // fall to black at its edges, so stretched tall its quad's top and bottom showed as straight
    // lines, and twenty alike ended square. So the tall body is flare01 (the table's `light`,
    // which does fall to nothing), with MU's spark_flash kept inside it as the core at its own
    // square size; and the line swells over its first three sprites and narrows and fades over
    // its last third, ending in a point.
    static constexpr float kCoreShare = 0.6f;    // the core's light, of the body's
    static constexpr int kSwell = 3;             // sprites to full
    static constexpr float kTaperFrom = 0.65f;   // of the line, where it starts to narrow
    static constexpr float kTaperEnd = 0.35f;    // and its size at the very end
    static constexpr float kFadeFrames = 5.0f;
    static constexpr int kGlows = 3;
    static constexpr float kGlowTiles = 2.5f;
    // The smoke after it (fx/inferno.h's numbers, its strength the user's "minimal").
    static constexpr int kPuffsPerBeam = 10;
    static constexpr float kPuffWait = 14.0f;    // reference frames after the cast
    static constexpr float kPuffFrames = 50.0f;
    static constexpr float kPuffBorn = 1.2f, kPuffGrown = 2.8f;  // metres across
    static constexpr float kPuffRise = 0.45f;   // metres a second
    // A pale blue mist, not grey: grey at 0.15 sank into the grass and was not seen at all
    // (the user asked for it again, 2026-10-02: "lets add minimal smoke after aqua beam cast").
    static constexpr float kPuffGrey[3] = {0.62f, 0.68f, 0.78f};
    static constexpr float kPuffAlpha = 0.30f;

    struct Beam {
        bool alive = false;
        float from[3] = {};
        float way[2] = {};
        float left = 0.0f;  // reference frames
    };
    static constexpr int kBeams = 6;
    struct Puff {
        bool alive = false;
        float at[3] = {};
        float spin = 0.0f;
        float wait = 0.0f;  // reference frames before it shows
        float age = 0.0f;
    };
    static constexpr int kPuffs = kBeams * kPuffsPerBeam;

    bgfx::TextureHandle sheet_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle glow_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;
    const content::Ground* ground_ = nullptr;
    Beam beams_[kBeams] = {};
    Puff puffs_[kPuffs] = {};
    uint32_t dice_ = 0x0a9ab3e1u;

    float unit();

    float metres() const { return ground_ ? ground_->metresPerTile() : 1.0f; }
    float lit(const Beam& beam) const;
};

}  // namespace mu::game
