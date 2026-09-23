// What a blow looks like: the landing cue, the blood it throws and the number it puts up.
//
// The sprint's spine, and the rule that holds it together is that **nothing here is ever a
// fact**. The roll, the damage and the death all resolve on the tick, in src/sim, exactly as
// sprint 5 built them. This decides only WHEN those are drawn and what they look like, and a
// cue that gets dropped costs a sound and a splash and never a number that mattered.
//
// It is `game` and not `gfx`: it knows about blows and targets. It is not `sim` either -- it
// holds no state the rules read, and the headless run, which draws nothing, never builds one.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "content/showing.h"
#include "content/texture.h"
#include "gfx/effects.h"

namespace mu::game {

// What kind of figure a blow puts up, which is the whole of the style's language: the size and
// the colour come from this and from nothing else. The design page of 2026-09-23 (concept B2)
// settled it -- a ramp rather than a set of events, so a critical is a step up and never a
// flourish, because at a real luck roll it lands several times a second.
enum class Mark : uint8_t {
    Swing,     // his own weapon, bone
    Skill,     // a skill's blow, amber -- heavier, and 2-5 of them from an area skill
    Critical,  // the top of the band, gold. Unreachable until an item rolls luck
    Taken,     // a blow on him, MU's red
    Absorbed,  // what his shield ate of it, blue and small, beside the red
    Miss,      // the word
};

// A blow that has landed on the tick and has not yet been shown.
//
// MU fires the swing, the sound and the number together on the swing's FIRST key, because
// `ReceiveAttackDamage` does all three in one handler while `AnimationFrame == 0`. That reads
// badly: on the first key the body is still blended most of the way into whatever it was
// doing, so the sound arrives before the arm does and the number appears while he is still
// standing. MU2 moved the hit sound and the number to halfway through the swing, and this
// copies that -- MU2's `docs/combat.md`, not this tree's -- and it is chosen rather than
// transcribed, because MU has no impact frame for an ordinary melee attack at all.
struct Cue {
    uint32_t attacker = 0;
    uint32_t target = 0;
    int32_t damage = 0;  // as rolled, which is the number shown
    // What it took off the target's health: the damage, cut to what was left when it killed.
    // What the bar adds back -- a 7 on a spider with 3 left takes 3, and adding back 7 had the
    // bar climb from 3 to 7 before the fall.
    int32_t taken = 0;
    bool miss = false;
    // What the shield took of it, which is only ever the hero's: the blow's damage less what
    // came off his health. Worked out on the tick, where the health before it is still known.
    int32_t absorbed = 0;
    // The skill the blow was thrown with, or 0 for a weapon's swing -- the `Swung` happening's
    // own number, kept on the swing so that the blow settling half a swing later still knows
    // what threw it. And the critical, which `Blow::critical` decides and no content reaches.
    int32_t skill = 0;
    bool critical = false;
    // Seconds left on the drawing's own clock. NOT the wall clock: MU2 found that at haste
    // every timed thing fell behind the simulation, because the animation was scaled and the
    // fuses were not. See Showing::advance.
    float fuse = 0.0f;
    // Which swing this cue belongs to. The attacker's swing is counted up as it starts one,
    // and a cue whose token no longer matches the swing the attacker is in drops itself --
    // which is what "gated on the clip still being the swing" means. A step cancels a swing
    // in this engine, so this case happens in ordinary play and is not theoretical.
    uint32_t token = 0;
};

class Showing {
public:
    // Reads the cooked .mus and takes the sheets it needs. Returns false only when the table
    // will not open; a sheet that is missing is reported and drawn as nothing, because a
    // fight with no blood in it is still a fight.
    bool open(const std::string& assetDir, content::Textures& textures);
    void shutdown();
    bool isOpen() const { return open_; }
    // The cooked table, which the sound player reads its events from.
    const content::Showing& table() const { return table_; }

    // A blow resolved on the tick. Nothing is shown yet.
    void schedule(const Cue& cue);

    // Burns every fuse down by the drawing's own scaled delta and appends the ones that came
    // due to `due`, which the caller owns so that this allocates nothing. The caller then
    // checks each one against the attacker's clip and either drops it or lands it.
    void advance(float seconds, std::vector<Cue>& due);

    // The cue survived its gate: throw the blood and put up the number. `feet` is the
    // target's position in world metres and `height` is how tall it is, also in metres.
    //
    // **Every length is taken in units of the target**, which is the one departure from the
    // client this makes and MU2 made it first. MU's own blood is thrown into a band 90 to 154
    // units above the target's feet and its splashes are half a metre across -- chosen
    // against a 120-unit player, and put on an 80-unit spider it is a grey cloud bigger than
    // the animal, which reads as smoke rather than as a wound. MU knows each model's height
    // (`CreateCharacter` hands out the boxes by hand) and simply does not use it here.
    //
    // `onHero` is whether the BLOW landed on the hero, not who threw it -- a number is coloured
    // by whose health it came off, so he reads his own pain in red and everyone else's in the
    // ramp's own bone, and his own misses paler than everyone else's.
    // `man` is the hero's own drawn height in metres: the length MU's blood numbers were
    // chosen against, so a blow on a man is thrown at MU's size and nothing else is guessed.
    void land(const Cue& cue, const float feet[3], float height, float man, float attackerYaw,
              bool onHero);

    // Ages everything alive, on the same scaled clock as the fuses.
    void update(float seconds);

    // Writes the blood into the transparent pass. The figures are NOT here: since the design
    // page of 2026-09-23 they are drawn by the interface, in a real face, over the world --
    // see game/ui/tally.cpp. This holds them and moves them; what they look like is the
    // interface's business, as a monster's health plate already is.
    void gather(gfx::Effects& effects) const;

    // One figure over a body: where it is anchored in the world, how old it is, and which step
    // of the ramp it is. The motion -- the pop, the rise, the lean, the fade -- is worked out
    // from `age` where it is drawn, because all of it is in screen pixels.
    struct Figure {
        float world[3] = {0, 0, 0};  // the point over the target it hangs from
        float age = 0.0f;            // seconds since it went up
        float life = 0.0f;           // seconds it lives for
        float lean = 0.0f;           // its sideways drift, in interface units, -1 to 1
        int32_t value = 0;
        Mark mark = Mark::Swing;
        bool onHero = false;         // pales a miss, and nothing else
        // How many figures were already standing over this body when this one went up: the
        // drawing lifts it by that many rows so two blows in one tick are two readings and not
        // one smudge. Taken from concept C into B2, where a Cyclone catching a body twice and
        // two spiders missing him on the same tick both drew one word over another.
        uint8_t slot = 0;
    };
    const std::vector<Figure>& figures() const { return figures_; }

    uint32_t liveParticles() const { return uint32_t(particles_.size()); }
    uint32_t liveNumbers() const { return uint32_t(figures_.size()); }
    uint32_t pending() const { return uint32_t(cues_.size()); }
    // The damage already taken off `target` on the tick and not shown yet: the sum of its
    // cues still burning. What the health bar adds back, so the red does not drop before the
    // number that took it goes up -- MU2's Crowd.Shown. A few cues at most, so a walk.
    int32_t owed(uint32_t target) const {
        int32_t sum = 0;
        for (const Cue& cue : cues_) {
            if (cue.target == target && !cue.miss) sum += cue.taken;
        }
        return sum;
    }
    // Whether any blow on `target` is still waiting to be shown, a miss included. What a death
    // waits on before the body falls: Play::fallWhenLanded.
    bool awaits(uint32_t target) const {
        for (const Cue& cue : cues_) {
            if (cue.target == target) return true;
        }
        return false;
    }
    uint32_t dropped() const { return dropped_; }
    void drop() { ++dropped_; }
    // Zeroes every cue from `attacker`, so the next advance() sees them due. The meteor's
    // impact: the blow was scheduled with a long fuse, and this is how it lands on impact
    // rather than ten seconds later. Does nothing when there are none.
    void rush(uint32_t attacker) {
        for (Cue& cue : cues_) {
            if (cue.attacker == attacker) cue.fuse = 0.0f;
        }
    }

    // How far into the swing the hit sound and the number go, as a fraction of the clip.
    // MU2's own half, and the nearest thing the client commits to is its melee SKILL effects
    // being gated on `AnimationFrame >= 3` of a seven-key attack.
    static constexpr float kLandingPoint = 0.5f;

private:
    // One blood splash. MU's own particle, in metres, with its lengths already scaled by the
    // target it came off.
    struct Particle {
        float position[3] = {0, 0, 0};
        float velocity[3] = {0, 0, 0};
        float size = 0.1f;
        // Reference frames left of MU's twelve. The sheet's quadrant is `(12 - life) / 3` in
        // integer arithmetic, so four quadrants are stepped at one every three frames and the
        // last is showing as the particle dies.
        float life = 12.0f;
    };

    bool open_ = false;
    content::Showing table_;
    bgfx::TextureHandle blood_ = BGFX_INVALID_HANDLE;

    std::vector<Cue> cues_;
    std::vector<Particle> particles_;
    std::vector<Figure> figures_;
    uint32_t dropped_ = 0;

    // The same rule as the transparent pass: reserved once, and a full pool refuses rather
    // than growing. A fight that allocates is a fight that hitches.
    uint32_t seed_ = 0x9E3779B9u;
    // MU's own rand() stands in for nothing here -- the SIM's dice are seeded and reproducible
    // and these are not, deliberately: where a splash flies is not a fact and must never reach
    // the seeded log. A cheap xorshift, drawn only by the drawing.
    uint32_t next();
    float unit();  // 0..1
};

}  // namespace mu::game
