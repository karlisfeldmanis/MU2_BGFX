#include "game/fx/showing.h"

#include <algorithm>
#include <cmath>

#include "core/log.h"

namespace mu::game {
namespace {

// MU's reference clock. Every rate in this file is per frame of it, and docs/conventions.md
// carries the trap: a speed takes one factor of 25 and an acceleration two.
constexpr float kReference = 25.0f;
// MU's units in a metre. A tile is a hundred of them and a tile is a metre here.
constexpr float kPerMetre = 100.0f;

// --- the blood, from Wounds.cs and ZzzCharacter.cpp's MoveCharacter -----------------------
// Six, not MU's ten, and the throw a third of MU's: MU tints its blood (0.1, 0, 0), near
// black, so ten splashes thrown two metres read as a faint dark smear. Drawn in the sheet's
// own red, as this does, ten at MU's reach were a red cloud across the tile -- too much, and
// not on the wound. Invention, judged in play.
constexpr int kSpatters = 6;
constexpr float kThrowShare = 0.35f;
constexpr float kScatter = 20.0f;   // MU's rand() % 64 - 32, drawn in to stay on the body
constexpr float kLowestSpray = 90.0f;
constexpr float kHighestSpray = 154.0f;  // rand() % 64 + 90
constexpr float kBloodLife = 12.0f;      // reference frames
constexpr float kFramesHeld = 3.0f;      // o->Frame = (12 - LifeTime) / 3, integer
constexpr float kSlowestThrow = 8.0f;    // rand() % 16 + 8
constexpr float kFastestThrow = 24.0f;
constexpr float kThrowLift = 3.0f;       // rand() % 6 - 3
constexpr float kDamping = 0.95f;        // per reference frame
// Spark02.jpg is four pixels square. The splash is the sheet's own quadrant, 64 of 128.
constexpr float kSplash = 50.0f;         // MU units across, at a man's size

// --- the figure ---------------------------------------------------------------------------
// Where it hangs, which is the one thing MU's own arrangement kept: a constant over the
// target's FEET, so a spider's number and a giant's land at the same height and the eye can
// follow a fight without reading each body's size. ZzzEffectPoint.cpp's own 140 units.
//
// Everything else about a figure -- how big it is, how it moves, how long it lasts -- belongs
// to the style and lives in game/ui/tally.cpp. Here it is only anchored and aged.
constexpr float kNumberHeight = 140.0f;
constexpr float kFigureLife = 0.95f;      // the design page's own, at 1080
constexpr float kCriticalLife = 1.05f;
// A figure going up over a body another figure has just gone up over is put in the row above
// it. Only just: after this long the first has risen clear on its own and the second belongs
// where the blow was.
constexpr float kStackWindow = 0.30f;   // seconds
constexpr float kStackNear = 0.60f;     // metres between the two anchors
constexpr int kStackHighest = 4;        // a Cyclone in a nest, and no higher

}  // namespace

uint32_t Showing::next() {
    // xorshift32. The sim's dice are seeded and reproducible and these deliberately are not:
    // where a splash flies is not a fact, nothing downstream may read one, and drawing from
    // the sim's own generator would make the picture change the seeded log.
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

float Showing::unit() { return float(next() & 0xFFFFFF) / float(0x1000000); }

bool Showing::open(const std::string& assetDir, content::Textures& textures) {
    std::string error;
    const std::string path = assetDir + "/cooked/showing/showing.mus";
    if (!content::loadShowing(path, table_, error)) {
        core::logError("the showing did not open: %s (tools/cook.py --only showing)",
                       error.c_str());
        return false;
    }

    // The return type is spelled out because BGFX_INVALID_HANDLE is a braced initialiser and
    // cannot be deduced from -- the same trap overlay.cpp records about a ternary.
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table_.effect(name);
        if (sheet == nullptr) {
            core::logError("no cooked effect named '%s'; a blow will draw without it", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };
    blood_ = take("blood");

    // Reserved once and never grown, which is sprint 6's proving sentence. Ten particles a
    // blow and a fight of thirty monsters cannot approach these.
    cues_.reserve(64);
    particles_.reserve(512);
    figures_.reserve(64);
    open_ = true;
    core::logf("showing: %zu effect sheets, %zu sound events; blood %s",
               table_.effects.size(), table_.events.size(),
               bgfx::isValid(blood_) ? "in hand" : "MISSING");
    return true;
}

void Showing::shutdown() {
    cues_.clear();
    particles_.clear();
    figures_.clear();
    open_ = false;
}

void Showing::schedule(const Cue& cue) {
    if (cues_.size() >= cues_.capacity()) {
        // Refused rather than grown. A blow nobody sees is better than a hitch in the middle
        // of a fight, and the count says it happened.
        ++dropped_;
        return;
    }
    cues_.push_back(cue);
}

void Showing::advance(float seconds, std::vector<Cue>& due) {
    due.clear();
    for (size_t i = 0; i < cues_.size();) {
        cues_[i].fuse -= seconds;
        if (cues_[i].fuse > 0.0f) {
            ++i;
            continue;
        }
        due.push_back(cues_[i]);
        // Swap with the back rather than erase from the middle: the order cues come due in
        // does not matter, and this is what keeps the vector from shuffling under a fight.
        cues_[i] = cues_.back();
        cues_.pop_back();
    }
}

void Showing::land(const Cue& cue, const float feet[3], float height, float man,
                    float attackerYaw, bool onHero) {
    // Units of the target against the hero's own drawn height, not MU's 120-unit box: the
    // figures here stand about 1.8 m, so against 120 a man came out at one and a half and a
    // Giant at three, and every length in the blood grew with it.
    const float like = (height > 0.01f && man > 0.01f) ? height / man : 1.0f;

    // The figure first, because it goes up whether or not the blow landed: a miss is a word
    // and not a zero.
    //
    // Which step of the ramp, in the order the design page argues: what landed on HIM is red
    // whatever threw it -- his own pain is one reading and not four -- and only his own blows
    // are told apart by what threw them.
    const auto markOf = [&]() {
        if (cue.miss) return Mark::Miss;
        if (onHero) return Mark::Taken;
        if (cue.critical) return Mark::Critical;
        return cue.skill != 0 ? Mark::Skill : Mark::Swing;
    };
    const auto raise = [&](Mark mark, int32_t value) {
        if (figures_.size() >= figures_.capacity()) return;
        Figure figure;
        // FLAT, not scaled by `like`: see kNumberHeight.
        figure.world[0] = feet[0];
        figure.world[1] = feet[1] + kNumberHeight / kPerMetre;
        figure.world[2] = feet[2];
        figure.value = value;
        figure.mark = mark;
        figure.onHero = onHero;
        figure.life = mark == Mark::Critical ? kCriticalLife : kFigureLife;
        // Which row over the body: how many are already standing there, just put up.
        int stacked = 0;
        for (const Figure& other : figures_) {
            if (other.age > kStackWindow) continue;
            const float dx = other.world[0] - figure.world[0];
            const float dy = other.world[1] - figure.world[1];
            const float dz = other.world[2] - figure.world[2];
            if (dx * dx + dy * dy + dz * dz < kStackNear * kStackNear) ++stacked;
        }
        figure.slot = uint8_t(std::min(stacked, kStackHighest));
        // The lean, so two blows a fifth of a second apart are not one figure drawn twice.
        // Drawn from the picture's own xorshift and never from the sim's dice.
        figure.lean = unit() * 2.0f - 1.0f;
        figures_.push_back(figure);
    };
    raise(markOf(), cue.damage);
    // And what his shield ate of it, small and blue beside the red. Only ever his: nothing
    // else in 0.75 carries a shield pool.
    if (!cue.miss && cue.absorbed > 0) raise(Mark::Absorbed, cue.absorbed);

    if (cue.miss) return;  // nothing bleeds from a blow that did not land

    // And the blood, thrown by the ATTACKER at the target -- `MoveCharacter`, not the thing
    // being hit. Nothing in MU asks the target to show that it was hurt.
    const float sinYaw = std::sin(attackerYaw), cosYaw = std::cos(attackerYaw);
    for (int i = 0; i < kSpatters; ++i) {
        if (particles_.size() >= particles_.capacity()) break;
        Particle one;
        // rand() % 64 - 32 on both ground axes, in units of the target.
        const float offsetX = (unit() * 2.0f - 1.0f) * kScatter * like;
        const float offsetZ = (unit() * 2.0f - 1.0f) * kScatter * like;
        const float up = (kLowestSpray + unit() * (kHighestSpray - kLowestSpray)) * like;
        one.position[0] = feet[0] + offsetX / kPerMetre;
        one.position[1] = feet[1] + up / kPerMetre;
        one.position[2] = feet[2] + offsetZ / kPerMetre;

        // Vector(0, -(rand() % 16 + 8), rand() % 6 - 3) rotated by the attacker's own angle:
        // at a yaw of zero that -Y is the way he is facing, so the blood goes along the blow
        // and away from the man swinging it.
        const float along =
            -(kSlowestThrow + unit() * (kFastestThrow - kSlowestThrow)) * like * kThrowShare;
        const float lift = (unit() * 2.0f - 1.0f) * kThrowLift * like * kThrowShare;
        one.velocity[0] = (along * sinYaw) / kPerMetre * kReference;
        one.velocity[1] = lift / kPerMetre * kReference;
        one.velocity[2] = (along * cosYaw) / kPerMetre * kReference;

        one.size = kSplash * like / kPerMetre * 0.5f;  // a half-extent, as the sprite wants
        one.life = kBloodLife;
        particles_.push_back(one);
    }
}

void Showing::update(float seconds) {
    const float frames = seconds * kReference;

    for (size_t i = 0; i < particles_.size();) {
        Particle& one = particles_[i];
        one.life -= frames;
        if (one.life <= 0.0f) {
            particles_[i] = particles_.back();
            particles_.pop_back();
            continue;
        }
        for (int c = 0; c < 3; ++c) one.position[c] += one.velocity[c] * seconds;
        // 0.95 of itself every reference frame, which over twelve carries it under two metres.
        const float damp = std::pow(kDamping, frames);
        for (int c = 0; c < 3; ++c) one.velocity[c] *= damp;
        ++i;
    }

    // A figure does not move in the world at all: it hangs where the blow landed and does its
    // rising in screen pixels, which is what keeps it the same size and the same speed whether
    // the camera is over the body or across the field. Here it only gets older.
    for (size_t i = 0; i < figures_.size();) {
        figures_[i].age += seconds;
        if (figures_[i].age >= figures_[i].life) {
            figures_[i] = figures_.back();
            figures_.pop_back();
            continue;
        }
        ++i;
    }
}

void Showing::gather(gfx::Effects& effects) const {
    if (bgfx::isValid(blood_)) {
        for (const Particle& one : particles_) {
            gfx::Sprite sprite;
            for (int c = 0; c < 3; ++c) sprite.position[c] = one.position[c];
            sprite.halfWidth = sprite.halfHeight = one.size;
            sprite.sheet = blood_;
            // The quadrant: (12 - life) / 3 in integer arithmetic, so the four are stepped at
            // one every three frames and the last is showing as the particle dies. The SHEET
            // does the fading -- its alpha peaks at 160 in the first quadrant and 58 in the
            // fourth -- so nothing here touches the colour.
            int quadrant = int((kBloodLife - one.life) / kFramesHeld);
            quadrant = quadrant < 0 ? 0 : (quadrant > 3 ? 3 : quadrant);
            sprite.u0 = float(quadrant % 2) * 0.5f;
            sprite.v0 = float(quadrant / 2) * 0.5f;
            sprite.u1 = sprite.u0 + 0.5f;
            sprite.v1 = sprite.v0 + 0.5f;
            // White, not the client's 0.1 red. MU multiplies its own blood's red away -- the
            // sheet is already painted blood, 114 in the red channel, and a tenth of that is
            // four percent, which is black. Drawn at one is the sheet as MU painted it with
            // the light taken off, not a colour put on. See Wounds.cs, which made this
            // departure first and argues it at length.
            sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = sprite.colour[3] = 1.0f;
            sprite.blend = gfx::Blend::Alpha;
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
