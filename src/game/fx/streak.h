// The ribbon a blade leaves behind a skill swing.
//
// **This is the one effect a knight's skill has of its own**, and it is MU's, not an invention:
// `CreateWeaponBlur` (ZzzCharacter.cpp) tests `PLAYER_ATTACK_SKILL_SWORD1..5` **before** it asks
// what is in the hand, so a skill swing lays a streak whatever the weapon is -- and groups 1 and
// 2, the axes and the maces, are tested nowhere else. An axe going about its ordinary business
// leaves nothing behind it and the same axe swinging a skill streaks. That distinction is the
// whole of what the client draws differently for a skill, and MU2's own `docs/skills.md` §8b
// records it as the rung that was knowingly left out of its first pass.
//
// The numbers are MU2's `client/core/Trails.cs`, a judged Godot port of the same call, so the two
// engines draw the same ribbon:
//
//   * **29 pairs.** `MAX_BLUR_TAILS - 1`. The client lays ten a frame and retires one, so any
//     swing longer than three frames sits pinned at 29.
//   * **2.9 reference frames of span**, 116 ms. Kept as a DURATION and not as a count, because
//     the count here is a count of rendered frames and MU's was not: at 180 fps a 29-frame
//     ribbon would be a sixth of a second of blade, which is not what the client draws.
//   * **A sixth of the blade to the tip.** MU's `Pos1 = (0,-20,0)`, `Pos2 = (0,-120,0)` for
//     `BlurType 1`, read as fractions of the weapon's own measured length rather than as absolute
//     units -- the same correction MU2 made and for the same reason: 120 units fits MU's longest
//     blade, and run absolutely on a Sword01 it trails half a metre of nothing past the tip.
//   * **White.** `BlurMapping` is 2 throughout on the skill rung, which resolves to
//     `motion_blur_r` -- cooked here as `trail_skill` -- and the refinement colours belong to
//     mapping 0 alone. A +9 axe streaks the same as a plain one the moment it is running a skill.
//   * **Three keys of wind-up.** The client's outermost guard is `AnimationFrame >= 3`, so the
//     gathering of the swing leaves nothing and the ribbon appears as the blade comes round.
//
// **And the ordinary swing's**, which MU keeps to swords (mapping 0, `Type >= MODEL_SWORD` on the
// sword actions) and which this engine lays for every melee weapon on a basic blow -- the user's
// call, 2026-09-29, and so an **invention** past the swords: an axe, a mace, a staff and a spear
// streak on a plain swing too. Everything else is MU's own:
//
//   * **blur01** (`BITMAP_BLUR`, cooked `trail`) for a blade, axe or club, from a sixth of it to
//     the tip, as `BlurType 1`; **blur02** (`BITMAP_BLUR2`, `trail_spear`) over the head alone
//     for a spear or a scythe, from 100 of 120, as `BlurType 3`.
//   * **Coloured by the plus**, mapping 0's ladder and no other's: grey, red from +3, blue from
//     +5, orange from +7.
//   * The same span, fade and wind-up as the skill ribbon, since `CreateBlur` is one call.

#pragma once

#include <cstdint>
#include <string>

#include <bgfx/bgfx.h>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

class Streak {
public:
    // Which of MU's sheets a ribbon is drawn with. See the header.
    enum class Sheet : uint8_t { Skill, Plain, Spear };

    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table);
    bool isOpen() const { return bgfx::isValid(sheets_[0]); }

    // One body's blade, this frame: the two ends of the ribbon in world metres. Called every
    // frame a skill clip is running on it and not at all otherwise, which is what starts and
    // feeds a ribbon -- there is no begin() and no end(), because a swing's own clip is the
    // only thing that decides either.
    // `colour` is the ribbon's light: white on a skill, the plus's ladder on a plain swing.
    void feed(uint32_t id, const float from[3], const float to[3], Sheet sheet,
              const float colour[3]);
    // Ages every ribbon: the span rolls off the tail, and one that stopped being fed retracts
    // and dims rather than hanging in the air.
    void update(float seconds);
    void gather(gfx::Effects& effects) const;
    void clear();

private:
    // MU's `MAX_BLUR_TAILS - 1`.
    static constexpr int kPairs = 29;
    // One per body swinging a skill. A fight has one knight in it; eight is a ceiling that costs
    // nothing and never has to be thought about again.
    static constexpr int kRibbons = 8;

    struct Pair {
        float a[3] = {0, 0, 0};
        float b[3] = {0, 0, 0};
        float age = 0.0f;
    };
    struct Ribbon {
        uint32_t id = 0;
        Pair pairs[kPairs];
        int count = 0;
        float idle = 0.0f;  // seconds since it was last fed
        Sheet sheet = Sheet::Skill;
        float colour[3] = {1, 1, 1};
    };

    Ribbon* ribbonFor(uint32_t id);

    Ribbon ribbons_[kRibbons];
    bgfx::TextureHandle sheets_[3] = {BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
                                      BGFX_INVALID_HANDLE};
};

}  // namespace mu::game
