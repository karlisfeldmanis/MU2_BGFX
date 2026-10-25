// What a level looks like: fifteen flares climbing out of the ground around whoever earned it.
// Ported from MU2's `client/core/Aura.cs`, the level-up recipe first and its Defense barrier
// once the knight had the skill. See Recipe for those. A skill learned is this engine's own and
// is not a burst of flares at all but a puff of blue smoke; see Aura::learn.
//
// MuMain's `ReceiveLevelUp` is the whole of it:
//
//     for (int i = 0; i < 15; ++i)
//         CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 0, o, 40, 2);
//     CreateEffect(BITMAP_MAGIC + 1, o->Position, o->Angle, o->Light, 0, o);
//     PlayBuffer(SOUND_LEVEL_UP);
//
// Fifteen joints on a ring of 40 units, each at its own phase and its own climb of 2.00 to
// 4.49 units a tick, living fifty ticks and dimming by 1/1.3 a tick over the last ten. Each
// trail is MU's CROSS -- two flat strips 40 units wide at right angles, laid in the hero's
// facing at the moment it is thrown -- and not a camera-facing ribbon. The fade down a trail
// is Flare's own painting, walked across by U, and the colour is the character's white.
//
// Carried over from MU2, each a departure it made and argued in Aura.cs:
//   * the ring's centre is the character, which is MU's call with its one missing line put
//     back (`TargetPosition` is never assigned for this joint);
//   * the trail is nineteen reference ticks long at any frame rate, not nineteen frames;
//   * the blue ground circle is written and switched OFF in play: MU2's crowd silenced it at
//     the user's asking, a three-tile wash of blue under a character being bitten.
//
// And this engine's own, three inventions made for a clean picture (user, 2026-09-22):
//   * **the trail is not stored, it is sampled.** MU lays one tail point a frame and MU2 one
//     a tick; the ring turns half a radian a tick, so either way the trail is a polygon of
//     29-degree chords and its U steps each time a point is laid. The path is closed-form --
//     a circle and a straight climb -- so the trail is drawn from the formula at kSamples a
//     tick, the same curve MU walks, with its corners taken off and its U continuous;
//   * **a strip fades as it turns edge-on.** MU's fixed cross goes to a hard bright line
//     wherever the path runs along one of its strips; those slivers are faded by how squarely
//     the strip faces the eye;
//   * **the flares are dimmer.** At full light, in this engine's HDR, the core of the burst
//     burned white; see kStrength in aura.cpp, judged on fixed-step shots.
//
// The sound is not here, and never was: a burst is thrown and a wave is played on the same
// frame by whoever threw it -- Play::rise for the level and Play::learned for a skill -- which
// is MU's own shape, where ReceiveLevelUp does both in one block.
//
// It stays where it was thrown and does not follow a character who walks away -- MU's, since
// `TargetPosition` is a point that nothing updates. It is `game`: it knows a ground.
#pragma once

#include <cstdint>
#include <string>

#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::content {
class Ground;
}

namespace mu::game {

// What a burst is made of: MU's level-up flares, the knight's Defense barrier -- which this
// engine left out in so many words, "a bench invention on a skill this engine does not have
// yet", and has the skill now -- and the elf's two casts.
//
// The barrier's numbers are MU2's Defense recipe (`Shape.Barrier, 5, 20, 100, Circle, Follows,
// Tails 30, Light (0.4, 0.8, 0.2)`), and what they are taken FROM is worth repeating here
// because it is the one thing about this effect that is not traced: MuMain gives skill 18 a
// clip and a sound and nothing else -- no buff, no joints, no icon. The vocabulary is borrowed
// from the one defensive skill MU does draw, the elf's Greater Defense
// (`ZzzCharacter.cpp:4925`), which throws `CreateEffect(BITMAP_MAGIC + 1)` and five
// `CreateJoint(MODEL_SPEARSKILL, ..., 4, to, 20.0f)` -- subtype 4 being MU's barrier.
struct Recipe {
    int joints = 15;
    float orbit = 40.0f;      // the ring's radius, in MU units
    float wide = 40.0f;       // the cross's own width
    float ticks = 50.0f;      // how long one lives, in reference ticks
    float slowRise = 2.00f;   // MU units a reference tick
    float fastRise = 4.49f;
    float trail = 19.0f;      // how many ticks of tail it grows to
    float band = 0.0f;        // MU units of body the ribbons are spread up, 0 to climb instead
    bool spread = false;      // phases laid evenly round the ring rather than drawn
    bool follows = false;     // whether it walks with the body
    bool circle = false;      // MU's ground circle thrown with it
    float light[3] = {1.0f, 1.0f, 1.0f};
    // The circle's colour and how fast it opens, in tiles a tick: MU's blue at 0.15, three
    // tiles across over its twenty ticks. A learned skill's is half as wide.
    float ring[3] = {0.4f, 0.6f, 1.0f};
    float ringOpens = 0.15f;
    // MU's PKKey: count = (Direction[1] + LifeTime) / key, so the ring turns 1/key radians a
    // tick. 2 is the level-up's; a joint made without one turns at count * 0.1, a key of 10.
    float key = 2.0f;
    // MU units a tick the ring opens by and the cross widens by as it lives: the +12 set's
    // ribbon (subtype 18, ZzzEffectJoint.cpp:5385-5390), Velocity += 0.1 and Scale += 0.2.
    float orbitGrows = 0.0f;
    float wideGrows = 0.0f;
    // Ours: at the end of its life the head stops where it got to and the tail runs on up into
    // it, fading as it shortens, over the trail's own ticks -- rather than MU's ten ticks of
    // dimming and then the whole strip gone in one frame, which on the +11 ribbon read as a pop
    // at his head (the user, 2026-10-16).
    bool runsOut = false;
    // Drawn on MU's BITMAP_FLARE_BLUE (`flare_blue.png`) rather than the gold Flare: the sheet
    // MU's own SPEARSKILL ribbons are made of, the wizard's Soul Barrier's among them.
    bool blue = false;
    // Thrown again and again for as long as it is held -- a guard while its boon stands -- each
    // climb starting a breath after the last has run out, as a +11 set's ribbon does
    // (game/fx/set_shine.cpp). Only the first lap flashes the circle.
    bool loops = false;
    // **One strip turned to the eye** in place of MU's fixed cross (ours, 2026-10-25): a
    // ring of crosses laid in the facing goes edge-on over most of its round from this
    // camera, and the edge-on fade left two short arcs on one side -- the user, of Swell
    // Life's cast: "do you see that aura is cuted/croped?". Each segment's strip lies across
    // the path and the view, so the whole ring reads from any angle.
    bool faces = false;
};

// MU's level-up: fifteen flares on a ring of forty, climbing away and dimming. The circle is
// switched on from outside (`setCircle`), which is where it has always been.
inline constexpr Recipe kRising{};
// And the guard: the +11 set's ribbon (game/fx/set_shine.cpp kSetRibbon) three times over, laid
// evenly round him -- a ring of fifty, a cross twelve wide, climbing 1.85 units a tick from his
// feet to over his head in a hundred ticks, about one and a half turns at a key of 10, its tail
// running up into the stopped head (Recipe::runsOut) -- and thrown again a breath after, for as
// long as the guard stands (the user, 2026-10-21: "use similiar ribbon which we use for +11
// effect for defense aura animations", "it is active all the time when its active on char").
// It was a cage of five standing ribbons of MU2's `Shape.Barrier`, shown two seconds at the cast.
// The knight's and the elf's in green, pushed past MU2's (0.4, 0.8, 0.2), which over Flare's own
// gold came out olive -- the sheet is the colour, and what is passed here is what is left of it.
// Lower and fainter than the +11's own (the user, 2026-10-21: "going little bit to height over
// the char heads", "also make it more subtle"): 1.6 a tick, 160 units, stops at his crown rather
// than a head over it, and both guards' light is a little over half of what it was.
inline constexpr Recipe kGuarding{3,    50.0f, 12.0f, 100.0f, 1.6f, 1.6f, 19.0f, 0.0f,
                                  true, true,  true,  {0.1f, 0.55f, 0.19f},
                                  {0.4f, 0.6f, 1.0f}, 0.15f, 10.0f, 0.0f, 0.0f, true, false, true};
// The wizard's Magic Shield (MU's Soul Barrier): the same ribbons in MU's own look for it --
// `CreateJoint(MODEL_SPEARSKILL, ..., 0, to, 20.0f)` (ZzzCharacter.cpp:4984), subtype 0 being
// BITMAP_FLARE_BLUE in white light (ZzzEffectJoint.cpp:1542-1546), where the guard's subtype 4
// tints the same sheet green. The blue circles a tester missed (2026-10-21, "when i put magic
// shield i dont see the blue circles effect"), shown the user in MuMain.
inline constexpr Recipe kBarrier{3,    50.0f, 12.0f, 100.0f, 1.6f, 1.6f, 19.0f, 0.0f,
                                 true, true,  true,  {0.55f, 0.55f, 0.55f},
                                 {0.4f, 0.6f, 1.0f}, 0.15f, 10.0f, 0.0f, 0.0f, true, true, true};
// The elf's Greater Damage standing on a player: the guard's ribbons in red (bug report #69; the
// user, 2026-10-25: "it has to be red"). MU throws its green SPEARSKILL sub 4 for eBuff_Attack as
// for eBuff_Defense; ours tells the two apart.
inline constexpr Recipe kFury{3,    50.0f, 12.0f, 100.0f, 1.6f, 1.6f, 19.0f, 0.0f,
                              true, true,  true,  {0.75f, 0.06f, 0.05f},
                              {0.4f, 0.6f, 1.0f}, 0.15f, 10.0f, 0.0f, 0.0f, true, false, true};
// Swell Life's and Critical Damage's colours in a shared swirl (Play::auraKinds): neither raises
// ribbons alone -- the embers and the coils are their looks -- but beside another aura each
// lends the swirl its ribbons, Swell Life's in its embers' orange and Critical Damage's in its
// coils' violet (the user, 2026-10-25: "this has to work on all classes if there is mixed auras
// together we need to mix colors on ribbons").
inline constexpr Recipe kSwellRibbons{3,    50.0f, 12.0f, 100.0f, 1.6f, 1.6f, 19.0f, 0.0f,
                                      true, true,  true,  {0.75f, 0.36f, 0.05f},
                                      {0.4f, 0.6f, 1.0f}, 0.15f, 10.0f, 0.0f, 0.0f, true, false, true};
inline constexpr Recipe kKeenRibbons{3,    50.0f, 12.0f, 100.0f, 1.6f, 1.6f, 19.0f, 0.0f,
                                     true, true,  true,  {0.50f, 0.22f, 0.75f},
                                     {0.4f, 0.6f, 1.0f}, 0.15f, 10.0f, 0.0f, 0.0f, true, false, true};
// The elf's Heal and Greater Damage (sprint 15), ours: MuMain draws neither -- ReceiveMagic plays
// the elf's cast and SOUND_SKILL_DEFENSE and registers the buff, and nothing renders
// eBuff_Attack. Eight ribbons spread evenly round a ring wider than she is, thrown once at the
// cast (the shape a learned skill had until 2026-09-29): a slower green-white climb
// for a wound closing, a quicker red-orange one for her blows hardening.
// Both on one strip turned to the eye (Recipe::faces) since 2026-10-25, the user of Swell Life's
// cast, which borrows kMight's until its own is drawn: "do you see that aura is cuted/croped?",
// "also ribons going to high". A ring of 34 and of 44 ran through the body, which hid half of
// it, and the climbs -- 12-16 and 20-22 units a tick -- went to 300 units and more, well over his
// head. Now a ring of 70 clears him all round, and each climbs to about his crown (170) over
// its life, as the guard's ribbons stop there (kGuarding).
inline constexpr Recipe kMending{8,    70.0f, 30.0f, 22.0f, 7.5f, 7.5f, 12.0f, 0.0f,
                                 true, false, false, {0.45f, 1.70f, 0.60f},
                                 {0.4f, 0.6f, 1.0f}, 0.15f, 2.0f, 0.0f, 0.0f, false, false, false, true};
// Greater Damage's in red since 2026-10-25 (the user: "it has to be red"); Swell Life keeps the
// red-orange it borrowed (kSwelling).
inline constexpr Recipe kMight{8,    70.0f, 34.0f, 16.0f, 10.5f, 10.5f, 10.0f, 0.0f,
                               true, false, false, {2.20f, 0.20f, 0.12f},
                               {0.4f, 0.6f, 1.0f}, 0.15f, 2.0f, 0.0f, 0.0f, false, false, false, true};
inline constexpr Recipe kSwelling{8,    70.0f, 34.0f, 16.0f, 10.5f, 10.5f, 10.0f, 0.0f,
                                  true, false, false, {2.20f, 0.55f, 0.15f},
                                  {0.4f, 0.6f, 1.0f}, 0.15f, 2.0f, 0.0f, 0.0f, false, false, false, true};
// The Dark Phoenix's shield going up, ours: WebZen raises it every six seconds and MuMain
// answers with the wizard's Soul Barrier buff on the bird (ZzzCharacter.cpp:4976-4979), whose
// look this game does not draw. Ten ember-red ribbons round a ring the bird's width, rising
// through its body for a second, inside a red circle; in the bird's own fire (the user,
// 2026-10-07: 'use red tone lighting for phoenix').
inline constexpr Recipe kPhoenixBarrier{10,   90.0f, 30.0f, 28.0f, 6.0f, 9.0f, 12.0f, 0.0f,
                                        true, false, true, {1.80f, 0.40f, 0.18f},
                                        {1.0f, 0.30f, 0.12f}};

class Aura {
public:
    // Takes the flare and the circle. False only when the flare is missing, which is the part
    // that is the effect; a missing circle is logged and the flares are drawn alone.
    bool open(const std::string& assetDir, content::Textures& textures);
    void shutdown();

    // Throws one at `feet` (world metres), in the facing `yaw` (the figure's own, forward along
    // (sin yaw, cos yaw)). Lengths are MU's units at `metresPerTile / 100`, a world length and
    // not the character's: a ring on the ground is a ring on the ground.
    void rise(const float feet[3], float yaw, float metresPerTile);

    // A skill read off an orb or a scroll: a puff of blue smoke out of the body, and no
    // flares. The user's picture (2026-09-29), cut to `player_learn_skill`, which Play::learned
    // plays on the same frame:
    //
    //   * **two puffs, on the two stabs.** The wave stabs at 0.08 s and 0.16 s, ticks two and
    //     four, so most of the smoke is born over the first two ticks and the rest around the
    //     fourth, and each one blooms fast -- most of its growth in its first six ticks -- so
    //     the puff lands on the hit and not after it;
    //   * **out of the whole body, not round it.** Born inside him, a hand from his middle,
    //     one to a band from his feet to his crown, let go a little outward and up, and
    //     slowed by a drag of 0.9 a tick, so it drifts a hand past him and then rises;
    //   * **subtle.** Seventeen small puffs, faint, and he stays readable through all of them:
    //     the first try, a cloud of 22 puffs at 0.62, hid him in yellow and was too much;
    //   * **held while the sound holds.** Full until the wave's tonal body gives way at 0.75 s,
    //     nineteen ticks, and gone by 1.2 s while the tail rings on;
    //   * **a circle under him.** MU's level-up circle in its own blue, which play has off,
    //     thrown for this alone at the user's asking: half its width, and its twenty ticks end
    //     as the wave's body does;
    //   * **real smoke, blue.** Twelve puffs of smoke02 mixed in (`Blend::Smoke`), tinted a pale
    //     cool blue, are the smoke; five of smoke01 added are a faint cold light in it. Gold
    //     came first and the user asked for realistic blue smoke instead (2026-09-29).
    //
    // Never following: the moment is over in a second and he cannot walk out of it.
    void learn(const float feet[3], float metresPerTile);
    // Any recipe thrown once where she stands: her Heal and Greater Damage.
    void cast(const Recipe& recipe, const float feet[3], float yaw, float metresPerTile) {
        throwOne(recipe, feet, yaw, metresPerTile);
    }

    // The guard: thrown like a burst, but it loops (Recipe::loops) and follows the body until
    // it is released -- when the boon lapses, which play reads off the realm (Play::guardStep).
    // Thrown again while one stands replaces it, which is what the realm does with the boon.
    // `owner` is the body it stands round: every player's guard is drawn, one to a body.
    // `recipe` is the knight's and the elf's green kGuarding or the wizard's blue kBarrier.
    void guard(uint32_t owner, const float feet[3], float yaw, float metresPerTile,
               const Recipe& recipe = kGuarding);
    // Several auras standing on one body share one swirl (the user, 2026-10-25: "if there is
    // multiple auras we need combine colors from each aura"): the first kind's ribbons times
    // `count`, laid evenly round him and dealt out in turn, each in its own kind's light and
    // sheet -- a guard and Greater Damage, three green and three red, alternating. `key` says
    // which set it is, so a change in it can be told (guardKey).
    void guard(uint32_t owner, const float feet[3], float yaw, float metresPerTile,
               const Recipe* const* kinds, int count, uint32_t key);
    // The key of the set standing on `owner`, 0 for none.
    uint32_t guardKey(uint32_t owner) const;
    // Where the body is now, every frame it is drawn: the one thing a following burst needs.
    void follow(uint32_t owner, const float feet[3]);
    // Every living burst moved to where the body is now: an Aura of one body's own, as a set's
    // ribbons are (game/fx/set_shine.h).
    void followAll(const float feet[3]);
    // And the guard put down early -- a boon that lapsed, or a character who died.
    void release(uint32_t owner);
    // Whether `owner` has a guard standing.
    bool guarding(uint32_t owner) const;

    // **Swell Life standing on a body** (the user, 2026-10-25: "1", MU's own glow): MU's
    // BITMAP_LIGHT subtype 1 on each body with eBuff_Life throws, every frame, two orange
    // (1, 0.5, 0.1) BITMAP_LIGHT particles at subtype 4 off two of seven upper-body bones -- index
    // n and 6 - n of g_byUpperBoneLocation {25, 26, 27, 20, 34, 35, 36}, his arms and neck
    // (ZzzEffect.cpp:34, :7004-7024). Each lives ten frames, climbs by a pace that gains 5.7-9.5
    // units a frame, shrinks 0.04-0.08 of its scale of two and loses its light by 1/1.35 a
    // frame (ZzzEffectParticle.cpp:3196-3201, :8058-8082): embers streaming up off his shoulders
    // and arms. `points` are those bones in the world this frame, `count` of them.
    // **Ours**: an ember stays where it was born and climbs from there rather than riding its
    // bone; it is a soft round dot (ember_soft.png) and not MU's rayed flare01; and its size is
    // judged on shots (kEmberAcross), not MU's sprite scale.
    void swell(const float (*points)[3], int count, float seconds, float metresPerTile);
    // Greater Damage standing on a player: MU's eBuff_Attack look (ZzzCharacter.cpp:10772-10795),
    // every frame a BITMAP_SHINY + 1 sprite at 1.5 on each hand's grip and the two bones up that
    // arm (Weapon[i].LinkBone, - 6, - 7), lit L*(1, 0.3, 0.2) with L rolled (rand() % 30 + 70) *
    // 0.01 each frame -- a flickering glow on both arms. `points` are those bones this frame
    // (FigureBody::armBones), `count` of them; called once a drawn frame per body, after
    // update(). Its ribbons, SPEARSKILL sub 4, are the guard's (Play::guardStep, kFury).
    // **Ours**: the soft dot of the embers and not MU's rayed shiny02, red where MU's is
    // orange (the user, 2026-10-25: "it has to be red"), at a size judged on shots.
    void might(uint32_t who, const float (*points)[3], int count);

    // Ages every burst on MU's clock. `seconds` is the frame's own.
    void update(float seconds);
    // `eye` is the camera's position in world metres, which the edge-on fade is taken from.
    void gather(gfx::Effects& effects, const content::Ground& ground, const float eye[3]) const;

    // MU's circle under the flares. Off in play, as MU2 had it; see the header note.
    void setCircle(bool on) { circle_ = on; }
    int live() const;

    static constexpr int kJoints = 15;  // the most any recipe asks for
    static constexpr int kKinds = 4;    // auras one swirl shares, at most
    // MaxTails - 1 for BITMAP_FLARE: how many ticks long the level-up's trail grows.
    static constexpr int kTrailTicks = 19;
    // Points sampled a tick: the ring turns 0.5 rad a tick, so four is a 7-degree chord.
    static constexpr int kSamples = 4;
    // Every player's in sight, not the hero's alone: a street's levels and guards at once
    // (the user, 2026-10-16: "i dont see other player lvl gain effect and auras"). A burst
    // thrown with none free is not drawn. A set's Aura (game/fx/set_shine.h) is one of these
    // too, a wearer's own.
    static constexpr int kBursts = 24;
    // Room for two skills read inside a second of each other; a third's puffs are not drawn.
    static constexpr int kPuffs = 64;
    // Swell Life's embers: three a reference frame (kEmberRate), ten frames each, so thirty a
    // body; room for a party of five and a few more in sight. One with none free is not drawn.
    static constexpr int kEmbers = 256;

private:
    struct Joint {
        float phase = 0.0f;   // rand() % 500 - 250
        float rise = 0.0f;    // metres a reference tick
        float height = 0.0f;  // metres it stands at, for a band that does not climb
    };
    struct Burst {
        bool living = false;
        uint32_t owner = 0;  // the body a guard stands round, 0 for any other burst
        Recipe r;
        float feet[3] = {0.0f, 0.0f, 0.0f};
        float across[3] = {1.0f, 0.0f, 0.0f};  // the joint's local X, turned by the hero's yaw
        float yaw = 0.0f;
        float per = 0.01f;    // metres in one of MU's units
        float age = 0.0f;     // reference ticks since it was thrown, or since its lap began
        int laps = 0;         // laps run, for a burst that loops
        Joint joints[kJoints];
        // A shared guard's kinds (Aura::guard): joint k is drawn in kind k % kinds; none, the
        // recipe's own.
        int kinds = 0;
        float kindLight[kKinds][3] = {};
        bool kindBlue[kKinds] = {};
        uint32_t key = 0;
    };

    // One wisp of a learned skill's smoke. Lengths in world metres, times in reference ticks.
    struct Puff {
        bool living = false;
        bool glow = false;     // smoke01 added, rather than smoke02 mixed
        float at[3] = {0.0f, 0.0f, 0.0f};
        float velocity[3] = {0.0f, 0.0f, 0.0f};  // metres a tick, slowed by the drag
        float age = 0.0f;      // negative until it is born
        float holds = 19.0f;   // full until this age...
        float life = 29.0f;    // ...and gone by this one
        float size = 0.5f;     // across, at birth
        float per = 0.01f;     // metres in one of MU's units
        float spin = 0.0f;
        float turns = 0.0f;    // radians a tick
    };

    // One of Swell Life's embers. Times in reference frames, lengths in world metres.
    struct Ember {
        bool living = false;
        float at[3] = {0.0f, 0.0f, 0.0f};  // where it was born
        float age = 0.0f;
        float climb = 0.0f;  // metres over where it was born
        float pace = 0.0f;   // metres a frame it climbs by, gaining every frame
        float scale = 2.0f;  // MU's Scale, two at birth
        float per = 0.01f;
    };
    Ember embers_[kEmbers];
    float emberDebt_ = 0.0f;  // embers owed by the frames fed so far
    void gatherEmbers(gfx::Effects& effects) const;
    bgfx::TextureHandle light_ = BGFX_INVALID_HANDLE;  // the embers' soft dot

    // Greater Damage's glints, this frame's only: six a body, room for a party and the street.
    struct Glint {
        float at[3] = {0.0f, 0.0f, 0.0f};
        float light = 1.0f;  // MU's Luminosity, 0.7-1.0
    };
    static constexpr int kGlints = 6 * 24;
    Glint glints_[kGlints];
    int glintCount_ = 0;
    float frame_ = 0.0f;  // reference frames, so the flicker rolls at MU's 25 a second
    void gatherGlints(gfx::Effects& effects) const;

    // Where joint `j` of `b` was `back` ticks ago, as an offset from the feet.
    static void at(const Burst& b, const Joint& j, float back, float out[3]);
    void gatherCircle(gfx::Effects& effects, const content::Ground& ground, const Burst& b) const;

    bgfx::TextureHandle flare_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle flareBlue_ = BGFX_INVALID_HANDLE;  // Recipe::blue; flare_ if missing
    bgfx::TextureHandle ground_ = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle smoke_ = BGFX_INVALID_HANDLE;  // smoke02, the body of the puff
    bgfx::TextureHandle wisp_ = BGFX_INVALID_HANDLE;   // smoke01, its glow
    Puff puffs_[kPuffs];
    void gatherPuffs(gfx::Effects& effects) const;
    bool circle_ = false;
    Burst bursts_[kBursts];
    Burst* throwOne(const Recipe& recipe, const float feet[3], float yaw, float metresPerTile);

    // Drawn only by the drawing and never seeded: where a flare starts on its ring is not a
    // fact, and must not reach the sim's log. The same xorshift the showing uses.
    uint32_t seed_ = 0x2545F491u;
    float unit();
};

}  // namespace mu::game
