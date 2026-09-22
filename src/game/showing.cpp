#include "game/showing.h"

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

// --- the number, from Points.cs and ZzzEffectPoint.cpp ------------------------------------
constexpr float kNumberHeight = 140.0f;  // above the thing that was hit, and FLAT: a spider
                                         // and a giant put their numbers at the same height,
                                         // because the client adds a constant to the origin
constexpr float kRiseStart = 10.0f;      // MU units a reference frame
constexpr float kRiseSlowing = 0.3f;     // a frame, so it dies after 33 1/3 of them
constexpr float kAlphaOfRise = 0.4f;
constexpr float kPlainScale = 15.0f;
// A digit is a square of the scale and the next is placed Scale / 0.7071 / 2 along the
// camera's horizontal, so digits overlap by nearly a third. MU's own spacing, and it is not
// a mistake -- it is what makes a three-digit number read as one object.
constexpr float kDigitSpacing = 0.7071f;
// The `Miss` sprite is a fixed 45 by 20 with no centring and no scaling, so a miss is LARGER
// than a plain hit and never grows or shrinks. MU passes -1 rather than a zero.
constexpr float kMissWidth = 45.0f;
constexpr float kMissHeight = 20.0f;
// Points.cs's `Add`: (colour, scale) by `missed` and `onHero`. Only the plain-hit and miss
// branches are reachable today -- Critical and Excellent wait on a roll this content version
// never makes, and Poison has no spell to throw it. Those stay unported rather than drawn from
// a flag nothing ever sets.
constexpr float kMissOnHero[3] = {1.0f, 1.0f, 1.0f};
constexpr float kMissOnOther[3] = {0.5f, 0.5f, 0.5f};
constexpr float kHitOnHero[3] = {1.0f, 0.0f, 0.0f};
constexpr float kHitOnOther[3] = {1.0f, 0.6f, 0.0f};

// The digit sheet, measured off Data/Interface/FontTest.OZT rather than assumed: 256x32, ten
// 16-pixel cells along the top, and `Miss` on a second row. Row 18 is blank across the whole
// sheet, which is what separates the two bands.
constexpr float kSheetW = 256.0f, kSheetH = 32.0f;
constexpr float kDigitW = 16.0f, kDigitBand = 18.0f;
constexpr float kMissU1 = 34.0f;  // the word ends at x 32; 34 clears it

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
    digits_ = take("damage_digits");

    // Reserved once and never grown, which is sprint 6's proving sentence. Ten particles a
    // blow and a fight of thirty monsters cannot approach these.
    cues_.reserve(64);
    particles_.reserve(512);
    numbers_.reserve(64);
    open_ = true;
    core::logf("showing: %zu effect sheets, %zu sound events; blood and digits %s",
               table_.effects.size(), table_.events.size(),
               (bgfx::isValid(blood_) && bgfx::isValid(digits_)) ? "in hand" : "INCOMPLETE");
    return true;
}

void Showing::shutdown() {
    cues_.clear();
    particles_.clear();
    numbers_.clear();
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

    // The number first, because it goes up whether or not the blow landed: a miss is a
    // sprite of its own and not a zero.
    if (numbers_.size() < numbers_.capacity()) {
        Number number;
        // FLAT, not scaled by `like`. The client adds a constant to the target's origin and
        // nothing scales it by the animal, and it is not an oversight -- the number sits at a
        // readable height above the TILE, so a spider's and a giant's land together.
        number.position[0] = feet[0];
        number.position[1] = feet[1] + kNumberHeight / kPerMetre;
        number.position[2] = feet[2];
        number.value = cue.damage;
        number.miss = cue.miss;
        number.scale = kPlainScale;
        const float* picked = cue.miss ? (onHero ? kMissOnHero : kMissOnOther)
                                        : (onHero ? kHitOnHero : kHitOnOther);
        for (int c = 0; c < 3; ++c) number.colour[c] = picked[c];
        numbers_.push_back(number);
    }

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

    for (size_t i = 0; i < numbers_.size();) {
        Number& one = numbers_[i];
        // A speed takes one factor of the reference rate and an acceleration two. Here the
        // rise is carried in MU's own units-a-frame, so it is decremented per frame and the
        // position takes the extra factor.
        one.position[1] += one.rise * frames / kPerMetre;
        one.rise -= kRiseSlowing * frames;
        if (one.rise <= 0.0f) {
            numbers_[i] = numbers_.back();
            numbers_.pop_back();
            continue;
        }
        ++i;
    }
}

void Showing::gather(gfx::Effects& effects, const float right[3]) const {
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

    if (!bgfx::isValid(digits_)) return;
    for (const Number& one : numbers_) {
        // alpha is the falling rise x 0.4, clamped -- so it holds full opacity for about a
        // second and fades over the last third of one, rather than fading the whole way.
        float alpha = one.rise * kAlphaOfRise;
        if (alpha > 1.0f) alpha = 1.0f;

        if (one.miss) {
            gfx::Sprite sprite;
            for (int c = 0; c < 3; ++c) sprite.position[c] = one.position[c];
            sprite.halfWidth = kMissWidth / kPerMetre * 0.5f;
            sprite.halfHeight = kMissHeight / kPerMetre * 0.5f;
            sprite.sheet = digits_;
            sprite.u0 = 0.0f;
            sprite.u1 = kMissU1 / kSheetW;
            sprite.v0 = kDigitBand / kSheetH;
            sprite.v1 = 1.0f;
            for (int c = 0; c < 3; ++c) sprite.colour[c] = one.colour[c];
            sprite.colour[3] = alpha;
            sprite.blend = gfx::Blend::Alpha;
            effects.add(sprite);
            continue;
        }

        // The digits, most significant first, laid along the camera's own horizontal.
        char text[16];
        const int written = std::snprintf(text, sizeof(text), "%d", one.value < 0 ? 0 : one.value);
        const float half = one.scale / kPerMetre * 0.5f;
        const float step = one.scale / kDigitSpacing / 2.0f / kPerMetre;
        for (int d = 0; d < written; ++d) {
            const char c = text[d];
            if (c < '0' || c > '9') continue;
            gfx::Sprite sprite;
            const float along = (float(d) - float(written - 1) * 0.5f) * step;
            for (int k = 0; k < 3; ++k) sprite.position[k] = one.position[k] + right[k] * along;
            sprite.halfWidth = sprite.halfHeight = half;
            sprite.sheet = digits_;
            sprite.u0 = float(c - '0') * kDigitW / kSheetW;
            sprite.u1 = sprite.u0 + kDigitW / kSheetW;
            sprite.v0 = 0.0f;
            sprite.v1 = kDigitBand / kSheetH;
            for (int c = 0; c < 3; ++c) sprite.colour[c] = one.colour[c];
            sprite.colour[3] = alpha;
            sprite.blend = gfx::Blend::Alpha;
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
