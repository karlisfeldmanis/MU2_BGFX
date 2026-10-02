// Rageful Blow's ground: the knight's own weapon flung up and brought down, and the earth
// breaking where it lands. MuMain's MODEL_SKILL_FURY_STRIKE and its eight EarthQuake pieces.
//
// **What MU does, and all of it is kept.** `AT_SKILL_RAGEFUL_BLOW` plays
// PLAYER_ATTACK_SKILL_FURY_STRIKE (action 66, 0.38) and, once its AnimationFrame passes one, makes
// a FURY_STRIKE at his feet with SOUND_FURY_STRIKE1 (ZzzCharacter.cpp:3046, :4161-4167). It lives
// twenty reference frames, one LifeTime each (Move_MODEL_SKILL_FURY_STRIKE, MoveHandlers.cpp:
// 2940-3175, births at ZzzEffect.cpp:1866-1945, movers at :7103-7258):
//
//   * **20 to 12, the weapon in the air.** His right hand's weapon (`Weapon[0]`) is drawn at the
//     effect while LifeTime is over ten (RenderFuryStrike, ZzzEffect.cpp:8650), tumbling 80
//     degrees a frame end over end, turned 330 off his facing. It stands at his feet plus
//     `sin(angle) * 260` along an axis pitched 80 degrees up out of his front, plus a Gravity
//     that starts at 50 and grows 8 a frame, plus 200: up five metres in five frames. At 15
//     the Gravity flips and the angle holds at 135, so it drops to under three metres and
//     sinks 8 more a frame. His hand holds nothing while the clip is under its fourth key
//     (ZzzCharacter.cpp:10079), so the weapon is in the air and not in his hand.
//   * **13, the streaks.** Eight MODEL_TAILs (tail.bmd, ring2, faint blue) under the weapon,
//     two columns of four, falling 80 units a frame and 60 faster each, six frames;
//     SOUND_FURY_STRIKE2.
//   * **11, the impact**, 80 units ahead of where the weapon is and 25 to his left: a
//     BITMAP_EXPLOTION at half size, eight JOINT_SPARKs thrown up, MODEL_WAVE's flash
//     (flashing.bmd, growing fast to twice then slowly, fading), and the crater -- EarthQuake03's
//     crack lines (solid), 01's molten glow under them (camera shake every third frame over its
//     first twenty), 02's fire standing up out of it -- all at 1.5. Then five broken patches
//     round it, 100 to 250 units out at 72 degrees apart, each EarthQuake04's glow and 05's fire
//     at 0.4 to 0.9, none on a tile that is no ground or no move.
//   * **10, the cracks**: five branches of four segments, each 85 to 99 units on from the last
//     and turned 50 to 79 degrees, alternately one way and the other (the fourth on a coin),
//     each an EarthQuake07 glow and an 08 fire. SOUND_FURY_STRIKE3, and the effect itself ends.
//   * The fires (02, 05, 08) light their ground red, scroll their sheet `-LifeTime * 0.01`, and
//     02 and 05 kick up a MODEL_STONE now and then -- the meteor's stones (fx/meteor.h).
//     Everything sinks half a unit a frame over its last frames.
//
// **Ours, and marked.** MU lays the crater at its impact's height less two units, under the
// ground, and the patches three over it; here every piece is three centimetres over the ground,
// since this ground is not MU's and two units under it is hidden. The sparks are calmed as the
// wheel's are: three of MU's eight. MU's red terrain light off every fire (up to 27 of them, a
// tile each) is two point lights at the impact, warmer and wider (kGlowTiles); MODEL_WAVE's
// darkening of the ground is not drawn. And a thin smoke where the fires go out, which MU has not
// (kSmokeAlpha). The weapon's scale is the
// wheel's, ItemObjectAttribute's 0.8 (0.7 for a polearm).
//
// Presentation only, `game` and not `sim`: the realm resolved the blow on the tick, and nothing
// rolled here reaches the seeded log. Pools are sized once and a frame allocates nothing.
#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/mesh.h"
#include "content/showing.h"
#include "content/texture.h"
#include "game/fx/effect_mesh.h"
#include "game/fx/forge.h"
#include "game/shine.h"
#include "gfx/effects.h"
#include "gfx/renderer.h"

namespace mu::game {

class Fury {
public:
    // One key of PLAYER_ATTACK_SKILL_FURY_STRIKE at MU's 0.38, in seconds at the authored pace:
    // the weapon leaves his hand one key in, and his hand is empty for the first four.
    static constexpr float kKeySeconds = 1.0f / (0.38f * 25.0f);
    static constexpr float kEmptyKeys = 4.0f;

    // The eight pieces and their sheets out of effects/fury, and the forge's sparks. False only
    // when the crater itself could not be had.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Showing& table, const content::Ground* ground);

    // A blow for `owner`, standing at `feet` facing `yaw`, to leave his hand once his clip passes
    // its first key: `wait` seconds from now. `weapon` is what his right hand holds; null breaks
    // the ground alone.
    void cast(uint32_t owner, const float feet[3], float yaw, float wait,
              const content::Mesh* weapon, const ShineLook& shine, bool polearm);

    // `sound(which, at)` is called with 2 and 3 when MU plays SOUND_FURY_STRIKE2 and 3, and
    // `stone(at)` when a fire kicks up a stone; `blast(at)` once, at the impact, for the
    // half-size BITMAP_EXPLOTION.
    template <typename Sound, typename Stone, typename Blast>
    void update(float seconds, Sound sound, Stone stone, Blast blast);

    // The flying weapon, into the camera's list only: it casts no shadow.
    void gather(std::vector<gfx::Drawable>& out) const;
    void gatherEffects(gfx::Effects& effects, const float eye[3], const float near[3],
                       float daylight) const;
    uint32_t lights(gfx::PointLight* out, uint32_t max) const;
    // MU's EarthQuake, in degrees of camera pitch, as fx/meteor.h's.
    float quakeDegrees() const { return quake_; }
    void clear();

private:
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    static constexpr float kDegrees = 3.14159265f / 180.0f;

    // ---- the weapon (MODEL_SKILL_FURY_STRIKE) -----------------------------------------------
    static constexpr int kLife = 20;
    static constexpr float kFirstGravity = 50.0f, kGravityStep = 8.0f;
    static constexpr float kSwing = 260.0f;     // Direction[1] = sin(angle) * 260
    static constexpr float kPitchUp = 80.0f;    // HeadAngle[0] += 80
    static constexpr float kLift = 200.0f;
    static constexpr float kTumble = 80.0f;     // Angle[0] += 80 a frame
    static constexpr float kTurned = 330.0f;    // Angle[2] += 330
    static constexpr float kScale = 0.8f, kPoleScale = 0.7f;
    // ---- the impact, 80 ahead and 25 to his left of the weapon -------------------------------
    static constexpr float kAhead = 80.0f, kLeft = 25.0f;
    static constexpr float kCrater = 1.5f;      // PKKey 150 / 100
    static constexpr float kOverGround = 0.03f;  // ours: see the header
    static constexpr int kPatches = 5;
    static constexpr int kBranches = 5, kSegments = 4;
    // ---- the streaks (MODEL_TAIL) ------------------------------------------------------------
    static constexpr int kTails = 8;
    static constexpr float kTailFrames = 6.0f, kTailFall = 80.0f, kTailFaster = 60.0f;
    static constexpr float kTailLight = 0.5f;
    // ---- the flash (MODEL_WAVE) --------------------------------------------------------------
    static constexpr float kFlashFrames = 15.0f;
    static constexpr int kSparks = 3;           // ours, of MU's eight
    // Its light, ours (the user, 2026-10-02: "we need some light emiter for this one"): MU's
    // pure red off each fire hardly shows on green ground, so two lights at the impact -- the
    // fires' glow in a warm orange, rising and falling as they do, four tiles; and a white-hot
    // flash as the weapon lands, five tiles, gone in twelve frames.
    static constexpr float kGlowTiles = 4.0f;
    static constexpr float kGlowColour[3] = {1.3f, 0.55f, 0.15f};
    static constexpr float kFlashTiles = 5.0f, kFlashLightFrames = 12.0f;
    static constexpr float kFlashColour[3] = {1.8f, 1.3f, 0.8f};
    // The smoke, ours (the user, 2026-10-02: "some minimal smoke after spell is done", then
    // "smoke has to happen same time when earth cracks come, and some has to come from earth
    // cracks"): the lamps' smoke02, two puffs off the crater and one off every other crack
    // segment as the ground breaks, then a thin trickle off the burning cracks -- a puff a
    // segment every five seconds or so -- each rising slowly for three seconds.
    static constexpr int kCrackSmokeOdds = 2;
    static constexpr float kTricklePerSecond = 0.2f;
    static constexpr float kSmokeAlpha = 0.4f;

    // One drawn piece of the ground: which model, where, how big, how it lives.
    enum class Kind : uint8_t { Crater, CraterLines, CraterFire, Patch, PatchFire, Crack, CrackFire };
    struct Piece {
        bool alive = false;
        Kind kind = Kind::Crater;
        float at[3] = {};
        float yaw = 0.0f;  // radians
        float scale = 1.0f;
        float left = 0.0f;  // LifeTime, reference frames
        float frames = 0.0f;  // what it was born with
        float sunk = 0.0f;  // metres
    };
    struct Tail {
        bool alive = false;
        float at[3] = {};
        float fall = kTailFall;
        float left = kTailFrames;
    };
    struct Flash {
        bool alive = false;
        float at[3] = {};
        float scale = 0.5f;
        float left = kFlashFrames;
        float yaw = 0.0f;
    };
    struct Blow {
        bool alive = false;
        uint32_t owner = 0;
        const content::Mesh* weapon = nullptr;
        ShineLook shine;
        bool polearm = false;
        float feet[3] = {};
        float yaw = 0.0f;
        float wait = 0.0f;   // seconds to the weapon leaving his hand
        bool flying = false;
        int life = kLife;    // MU's LifeTime, whole frames
        float owed = 0.0f;   // reference frames not yet stepped
        float wasAt[3] = {};  // the weapon a frame before, drawn between the two
        float gravity = kFirstGravity;
        float tumble = 0.0f;  // degrees
        float weaponAt[3] = {};
        float impact[3] = {};
        float transform[16] = {};
        int subType = 0;     // rand() % 100: the patches' first angle
    };
    struct Puff {
        bool alive = false;
        float at[3] = {};
        float rise = 0.0f;   // metres a second
        float drift[2] = {};
        float size = 0.5f;   // metres across at birth
        float spin = 0.0f;
        float age = 0.0f, life = 3.0f;  // seconds
    };
    struct Glow {
        bool alive = false;
        float at[3] = {};
        float left = 0.0f;
        float light = 0.0f;
        float flash = 0.0f;  // reference frames of the landing's flash left
    };

    static constexpr int kBlows = 6;
    static constexpr int kPieces = kBlows * (3 + 2 * kPatches + 2 * kBranches * kSegments);
    static constexpr int kTailPool = kBlows * kTails;
    static constexpr int kSmokePool = kBlows * 40;
    static constexpr float kQuakeDecay = 0.2f, kQuakeEpsilon = 0.0001f;

    struct Model {
        std::vector<EffectCorner> tris;
        bgfx::TextureHandle sheet = BGFX_INVALID_HANDLE;
    };
    Model crater_, craterLines_, fire_, patch_, crack_, crackFire_, patchFire_, flash_, tail_;

    const content::Ground* ground_ = nullptr;
    Forge sparks_;
    Blow blows_[kBlows] = {};
    Piece pieces_[kPieces] = {};
    Tail tails_[kTailPool] = {};
    Flash flashes_[kBlows] = {};
    Glow glows_[kBlows] = {};
    Puff smoke_[kSmokePool] = {};
    bgfx::TextureHandle smokeSheet_ = BGFX_INVALID_HANDLE;
    float quake_ = 0.0f;
    uint32_t dice_ = 0x5ad7e11fu;

    uint32_t roll();
    float floorAt(float x, float z) const;
    void puff(const float at[3]);
    bool groundAt(float x, float z) const;
    void place(Blow& blow, float frames);
    template <typename Sound, typename Blast>
    void step(Blow& blow, Sound& sound, Blast& blast);
    void lay(Kind kind, const float at[3], float yaw, float scale, float frames);
    void agePieces(float frames, float& craterLight);
    template <typename Stone>
    void kick(Stone& stone, float frames);
};

template <typename Sound, typename Stone, typename Blast>
void Fury::update(float seconds, Sound sound, Stone stone, Blast blast) {
    // A long hitch is capped, as the wheel caps its own.
    seconds = std::fmin(seconds, 0.1f);
    const float frames = seconds * kReferenceFps;
    sparks_.update(seconds);
    for (Blow& blow : blows_) {
        if (!blow.alive) continue;
        if (!blow.flying) {
            blow.wait -= seconds;
            if (blow.wait > 0.0f) continue;
            blow.flying = true;
            blow.owed = -blow.wait * kReferenceFps;
            // MU moves an effect on the frame it is made.
            step(blow, sound, blast);
        } else {
            blow.owed += frames;
        }
        while (blow.alive && blow.owed >= 1.0f) {
            blow.owed -= 1.0f;
            step(blow, sound, blast);
        }
        if (blow.alive) place(blow, blow.owed);
    }
    float craterLight = 0.0f;
    agePieces(frames, craterLight);
    kick(stone, frames);
    for (Tail& tail : tails_) {
        if (!tail.alive) continue;
        tail.left -= frames;
        if (tail.left <= 0.0f) {
            tail.alive = false;
            continue;
        }
        // Move_MODEL_TAIL: down by its Gravity, which grows 60 a frame.
        tail.at[1] -= (tail.fall + 0.5f * kTailFaster * frames) * kUnit * frames;
        tail.fall += kTailFaster * frames;
    }
    for (Flash& flash : flashes_) {
        if (!flash.alive) continue;
        flash.left -= frames;
        if (flash.left <= 0.0f) {
            flash.alive = false;
            continue;
        }
        // Move_MODEL_WAVE: grows 1.2 a frame to twice its size, then 0.1, sinking as it goes.
        const bool slow = flash.scale > 2.0f;
        flash.scale += (slow ? 0.1f : 1.2f) * frames;
        flash.at[1] -= (slow ? 1.5f : 1.8f) * kUnit * frames;
    }
    for (Puff& one : smoke_) {
        if (!one.alive) continue;
        one.age += seconds;
        if (one.age >= one.life) {
            one.alive = false;
            continue;
        }
        one.at[0] += one.drift[0] * seconds;
        one.at[1] += one.rise * seconds;
        one.at[2] += one.drift[1] * seconds;
    }
    quake_ *= std::pow(kQuakeDecay, frames);
    if (std::fabs(quake_) < kQuakeEpsilon) quake_ = 0.0f;
}

template <typename Stone>
void Fury::kick(Stone& stone, float frames) {
    // 02 kicks a stone on one frame in ten between its fifth and tenth, 05 on one in fifteen
    // from its fifth, each within 150 units of it.
    for (const Piece& piece : pieces_) {
        if (!piece.alive) continue;
        int odds = 0;
        if (piece.kind == Kind::CraterFire && piece.left >= 5.0f && piece.left < 10.0f) odds = 10;
        if (piece.kind == Kind::PatchFire && piece.left >= 5.0f && piece.left < 30.0f) odds = 15;
        if (odds == 0) continue;
        if (float(roll() % 10000u) / 10000.0f >= frames / float(odds)) continue;
        const float reach = float(roll() % 150u) * kUnit;
        const float turn = float(roll() % 360u) * kDegrees;
        const float at[3] = {piece.at[0] + std::sin(turn) * reach, piece.at[1],
                             piece.at[2] + std::cos(turn) * reach};
        stone(at);
    }
}

template <typename Sound, typename Blast>
void Fury::step(Blow& blow, Sound& sound, Blast& blast) {
    const int life = blow.life;
    const float fs = std::sin(blow.yaw), fc = std::cos(blow.yaw);
    if (life == 11) {
        // The impact: 80 ahead of the weapon and 25 to his left (MU's (-25, -80, 0) turned by his
        // angle), on the ground.
        float at[3] = {blow.weaponAt[0] + fs * kAhead * kUnit - fc * kLeft * kUnit, 0.0f,
                       blow.weaponAt[2] + fc * kAhead * kUnit + fs * kLeft * kUnit};
        at[1] = floorAt(at[0], at[2]);
        for (int k = 0; k < 3; ++k) blow.impact[k] = at[k];
        const float burst[3] = {at[0], at[1] + 0.25f, at[2]};
        blast(burst);
        // JOINT_SPARKs on (rand() % 60 - 60, 0, rand() % 30 + 90) plus the effect's own angle.
        const float yawDegrees = blow.yaw / kDegrees + kTurned;
        for (int i = 0; i < kSparks; ++i) {
            const float from[3] = {burst[0] + float(int(roll() % 20u) - 10) * kUnit, burst[1],
                                   burst[2] + float(int(roll() % 20u) - 10) * kUnit};
            sparks_.fling(from, -60.0f, yawDegrees + 90.0f, true, roll() % 8u == 0u);
        }
        for (Flash& flash : flashes_) {
            if (flash.alive) continue;
            flash = Flash{};
            flash.alive = true;
            for (int k = 0; k < 3; ++k) flash.at[k] = at[k];
            flash.at[1] += (25.0f - 15.0f) * kUnit;
            flash.yaw = blow.yaw;
            break;
        }
        // The crater, drawn in MU's order: the lines, the glow, the fire.
        lay(Kind::CraterLines, at, 0.0f, kCrater, 35.0f);
        lay(Kind::Crater, at, 0.0f, kCrater, 35.0f);
        lay(Kind::CraterFire, at, 0.0f, kCrater, 20.0f);
        for (Glow& glow : glows_) {
            if (glow.alive) continue;
            glow = Glow{};
            glow.alive = true;
            for (int k = 0; k < 3; ++k) glow.at[k] = at[k];
            glow.left = 40.0f;
            glow.flash = kFlashLightFrames;
            break;
        }
        // Five patches 100 to 250 units out, 72 degrees apart from a rolled start, each turned
        // 45 give or take 15.
        for (int i = 0; i < kPatches; ++i) {
            const float reach = float(roll() % 150u + 100u) * kUnit;
            const float turn = (float(blow.subType) + float(i) * 72.0f) * kDegrees;
            const float x = at[0] + std::sin(turn) * reach, z = at[2] + std::cos(turn) * reach;
            if (!groundAt(x, z)) continue;
            const float scale = float(roll() % 50u + 40u) * 0.01f;
            const float yaw = (45.0f + float(int(roll() % 30u) - 15)) * kDegrees;
            const float spot[3] = {x, floorAt(x, z), z};
            lay(Kind::Patch, spot, yaw, scale, 35.0f);
            lay(Kind::PatchFire, spot, yaw, scale, 40.0f);
        }
        // The camera: EarthQuake01 shakes it over its first twenty frames, see agePieces.
    } else if (life == 10) {
        // The cracks: five branches from the impact, four segments each.
        float ends[kBranches][3];
        float turned[kBranches] = {};
        for (auto& end : ends) {
            for (int k = 0; k < 3; ++k) end[k] = blow.impact[k];
        }
        uint32_t count = 0;
        for (int j = 0; j < kSegments; ++j) {
            const float length = (float(roll() % 15u) + 85.0f) * kUnit;
            if (j >= 3) count = roll();
            for (int i = 0; i < kBranches; ++i) {
                const float bend = float(roll() % 30u + 50u);
                turned[i] += (count % 2u == 0u) ? bend : -bend;
                const float angle = turned[i] + float(i) * float(roll() % 10u + 62u);
                const float turn = angle * kDegrees;
                // MU's (0, length, 0) turned by the angle: our south, turned.
                ends[i][0] += -std::sin(turn) * length;
                ends[i][2] += -std::cos(turn) * length;
                ends[i][1] = floorAt(ends[i][0], ends[i][2]);
                const float yaw = (angle + 270.0f) * kDegrees;
                lay(Kind::Crack, ends[i], yaw, 1.0f, 40.0f);
                lay(Kind::CrackFire, ends[i], yaw, 1.0f, 40.0f);
                if (roll() % kCrackSmokeOdds == 0u) puff(ends[i]);
            }
            ++count;
        }
        puff(blow.impact);
        puff(blow.impact);
        sound(3, blow.impact);
        blow.alive = false;
        return;
    } else {
        if (life == 13) {
            // The streaks: two columns of four under the weapon, 40 ahead and 25 to his left.
            const float x = blow.weaponAt[0] + fs * 40.0f * kUnit - fc * kLeft * kUnit;
            const float z = blow.weaponAt[2] + fc * 40.0f * kUnit + fs * kLeft * kUnit;
            const float y = blow.weaponAt[1];
            const float shift = float(roll() % 30u + 20u) * kUnit;
            const float drop = float(int(roll() % 500u) - 250) * kUnit;
            int made = 0;
            for (Tail& tail : tails_) {
                if (tail.alive) continue;
                tail = Tail{};
                tail.alive = true;
                const bool second = made >= 4;
                const int i = made % 4;
                tail.at[0] = x + (second ? shift : 0.0f);
                tail.at[1] = y + (second ? drop - float(i) * 30.0f * kUnit
                                         : -float(i) * 50.0f * kUnit);
                tail.at[2] = z;
                if (++made == kTails) break;
            }
            sound(2, blow.weaponAt);
        }
        // The swing: the angle runs 15 a frame, but held at 135 from 15 to 10, where Gravity
        // also turns over and runs the other way.
        float count = float(life), add = 15.0f;
        if (life > 9 && life < 16) {
            count = 12.5f;
            add = 18.0f;
            if (life == 15) blow.gravity = -blow.gravity;
        }
        blow.tumble += kTumble;
        blow.gravity += (count != 12.5f) ? kGravityStep : -kGravityStep;
        for (int k = 0; k < 3; ++k) blow.wasAt[k] = blow.weaponAt[k];
        const float angle = (20.0f - count) * add * kDegrees;
        const float swing = std::sin(angle) * kSwing;
        const float ahead = swing * std::cos(kPitchUp * kDegrees);
        const float up = swing * std::sin(kPitchUp * kDegrees) + blow.gravity + kLift;
        blow.weaponAt[0] = blow.feet[0] + fs * ahead * kUnit;
        blow.weaponAt[1] = blow.feet[1] + up * kUnit;
        blow.weaponAt[2] = blow.feet[2] + fc * ahead * kUnit;
        if (life == kLife) {
            for (int k = 0; k < 3; ++k) blow.wasAt[k] = blow.weaponAt[k];
        }
    }
    --blow.life;
}

}  // namespace mu::game
