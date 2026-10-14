#include "game/fx/set_shine.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// The ribbon: MuMain's +11, CreateJoint(BITMAP_FLARE, o->Position, o->Position, o->Angle, 0, o)
// (ZzzCharacter.cpp:11105-11110; ZzzEffectJoint.cpp:1669-1690, 5374-5386): one joint on a ring
// of forty turning at count = (Direction[1] + LifeTime) * 0.1 -- PK -1, a key of 10, a tenth of a
// radian a frame, about one and a half turns in its hundred frames -- a cross ten units wide,
// twenty tails, white on Flare's gold (o->Light set to 1 before it is thrown). Round him once,
// rising, with its tail -- the user, 2026-10-10: 'in MuMai there was single ribbon which was going
// around body on 1 go with some tail'. Ours, two things: MU's climb is rand() % 150 / 100, 0 to
// 1.5 units a frame, and most of them stall at his knees; here every one climbs 1.6, feet to
// head in its life. And MU throws one by chance now and then (one frame in 160); here the next
// goes up a breath after the last is gone, so one is nearly always round him. At 0.9 of the
// level-up's light ('subtle but very nice'). And its ring is 58 and its cross 12, not MU's 40 and
// 10: on our second-class suits, broad at the chest and spiked at the shoulder, a ring of 40 ran
// inside the armour from the waist up and only its turn round the feet was seen.
constexpr Recipe kSetRibbon{1,     58.0f, 12.0f, 100.0f, 1.6f, 1.6f, 19.0f, 0.0f,
                            false, false, false, {0.9f, 0.9f, 0.9f}, {0.4f, 0.6f, 1.0f}, 0.15f,
                            10.0f};
// A new one this many reference ticks after the last was thrown: its hundred, and a pause.
constexpr float kRibbonEvery = 120.0f;
// A flare one frame in four (ours; MU's one in twenty -- 'sparkles has to be more').
constexpr float kFlareChance = 1.0f / 4.0f;
// Invention, the level-up's (game/fx/aura.cpp kStrength): Flare added at 1 burned white in this
// engine's light.
constexpr float kStrength = 0.65f;
// Invention: the arms' lights at twice MU's half. MU adds 0.5 into 8-bit colour, where it shows
// as a glow; in this engine's light it vanished on the gold of a lit suit even at night. Four
// times was a yellow blaze; two was judged on the night shot of a +11 Brass set, and 1.6 in the
// studio by the bonfire, to sit subtler under the rings.
constexpr float kArmGain = 1.6f;
// Invention: the climbing flares at 0.18 of MU's 0.19 -- the user, 2026-10-10: 'make +11
// sparkles smaller', then 'much smaller', then 'smaller' again. At MU's own they read as blobs
// round a lit suit; here they are motes.
constexpr float kFlareSize = 0.18f;

}  // namespace

float SetShine::unit() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return float(dice_ & 0xFFFFFFu) / float(0x1000000);
}

bool SetShine::open(const std::string& assetDir, content::Textures& textures) {
    const std::string path = assetDir + "/effects/levelup/flare.png";
    if (!core::fileExists(path)) {
        core::logError("set shine: no %s (pipeline/sync.sh)", path.c_str());
        return false;
    }
    flare_ = textures.load(path, content::TextureRole::Albedo);
    const std::string light = assetDir + "/effects/light/flare01.png";
    if (core::fileExists(light)) {
        light_ = textures.load(light, content::TextureRole::Albedo);
    } else {
        core::logError("set shine: no %s; a set's arms do not glow", light.c_str());
    }
    // One Aura opened, and its sheets lent to every wearer's: they are Textures' handles.
    Aura ribbons;
    ribbons.open(assetDir, textures);
    for (Wearer& one : wearers_) one.ribbons = ribbons;
    return bgfx::isValid(flare_);
}

void SetShine::shutdown() {
    for (Wearer& one : wearers_) {
        one.ribbons.shutdown();
        one = Wearer{};
    }
    flare_ = BGFX_INVALID_HANDLE;
    light_ = BGFX_INVALID_HANDLE;
    for (Flare& one : flares_) one.alive = false;
}

int SetShine::setPlus(const content::Tables& tables, const sim::Satchel& bag, sim::Kin kin) {
    // CheckFullSet: every piece there, one set, and the lowest plus is the set's. The gladiator's
    // from the armour down (`end = EQUIPMENT_ARMOR` for CLASS_DARK, ZzzCharacter.cpp:5404).
    int set = -1, lowest = 1 << 30;
    const int first = kin == sim::Kin::MagicGladiator ? sim::kArmour : sim::kHelm;
    for (int slot = first; slot <= sim::kBoots; ++slot) {
        const sim::Held& worn = bag[slot];
        if (worn.empty() || size_t(worn.item) >= tables.items.size()) return -1;
        const int of = sim::setOf(tables.items[size_t(worn.item)]);
        if (of < 0 || (set >= 0 && of != set)) return -1;
        set = of;
        lowest = std::min<int>(lowest, worn.refinement);
    }
    return lowest;
}

int SetShine::slotFor(uint32_t id) {
    int free = -1;
    for (int k = 0; k < kWearers; ++k) {
        if (wearers_[k].id == id) return k;
        if (free < 0 && wearers_[k].id == 0) free = k;
    }
    if (free >= 0) {
        Aura ribbons = wearers_[free].ribbons;
        wearers_[free] = Wearer{};
        wearers_[free].ribbons = ribbons;
        wearers_[free].id = id;
    } else if (!full_) {
        full_ = true;
        core::logf("set shine: more than %d wearers in sight; the rest are drawn without", kWearers);
    }
    return free;
}

void SetShine::wear(uint32_t id, int plus, const float feet[3], float yaw,
                    const float (*arms)[3], int lit, const float colour[3]) {
    if (id == 0 || plus < 9) return;
    const int at = slotFor(id);
    if (at < 0) return;
    Wearer& one = wearers_[at];
    one.seen = true;
    one.plus = plus;
    std::copy(feet, feet + 3, one.feet);
    one.yaw = yaw;
    one.lights = std::clamp(lit, 0, 6);
    for (int k = 0; k < one.lights; ++k) std::copy(arms[k], arms[k] + 3, one.lightAt[k]);
    if (colour) std::copy(colour, colour + 3, one.lightColour);
}

void SetShine::throwFlare(int wearer) {
    for (Flare& one : flares_) {
        if (one.alive) continue;
        one.alive = true;
        one.wearer = wearer;
        one.phase = std::floor(unit() * 300.0f) - 150.0f;
        one.life = kLife;
        one.climb = std::floor(unit() * 100.0f) / 100.0f * 4.0f + 1.0f;
        one.height = 0.0f;
        one.scale = 0.19f + std::floor(unit() * 6.0f) * 0.01f;
        one.spin = unit() * 6.2831853f;
        return;
    }
}

void SetShine::update(float seconds, float metresPerTile) {
    const float frames = seconds * kReferenceFps;
    per_ = metresPerTile / 100.0f;
    // The flares in the air climb and turn on MU's frames whatever the set is now.
    int flying[kWearers] = {};
    for (Flare& one : flares_) {
        if (!one.alive) continue;
        one.life -= frames;
        one.height += one.climb * frames;
        one.scale -= 0.002f * frames;
        if (one.life <= 0.0f || one.scale <= 0.0f) {
            one.alive = false;
        } else {
            ++flying[one.wearer];
        }
    }
    int wearing = 0;
    for (const Wearer& one : wearers_) wearing += one.id != 0 && one.seen ? 1 : 0;
    if (wearing != wearing_) {
        core::logf("set shine: %d wearing a set of +9 or more in sight", wearing);
        wearing_ = wearing;
    }
    for (int w = 0; w < kWearers; ++w) {
        Wearer& one = wearers_[w];
        if (one.id == 0) continue;
        one.ribbons.update(seconds);
        if (!one.seen) {
            // Not drawn this frame: no lights, nothing new, and his slot let go once what he
            // threw has gone.
            one.lights = 0;
            one.clock = 0.0f;
            if (flying[w] == 0 && one.ribbons.live() == 0) one.id = 0;
            continue;
        }
        one.seen = false;
        // Carried with him wherever he is drawn: a set's ring is his, not the ground's.
        one.ribbons.followAll(one.feet);
        // And new ones, a frame at a time: from +10 the flares, at +11 the ribbon pair as well.
        // Past +11 is drawn as +11.
        if (one.plus < 10) {
            one.clock = 0.0f;
            continue;
        }
        one.clock += frames;
        while (one.clock >= 1.0f) {
            one.clock -= 1.0f;
            if (unit() < kFlareChance) throwFlare(w);
        }
        if (one.plus >= 11) {
            one.sinceRings += frames;
            if (one.sinceRings >= kRibbonEvery) {
                one.sinceRings = 0.0f;
                one.ribbons.cast(kSetRibbon, one.feet, one.yaw, metresPerTile);
            }
        }
    }
}

void SetShine::gather(gfx::Effects& effects, const content::Ground& ground,
                      const float eye[3]) const {
    for (const Wearer& one : wearers_) {
        if (one.id == 0) continue;
        one.ribbons.gather(effects, ground, eye);
        // The arms' lights: flare01 at its 64 texels times 1.3, the boots' colour at half, added.
        if (!bgfx::isValid(light_)) continue;
        for (int k = 0; k < one.lights; ++k) {
            gfx::Sprite sprite;
            std::copy(one.lightAt[k], one.lightAt[k] + 3, sprite.position);
            sprite.halfWidth = sprite.halfHeight = kTexels * 1.3f * per_ * 0.5f;
            for (int c = 0; c < 3; ++c) sprite.colour[c] = one.lightColour[c] * 0.5f * kArmGain;
            sprite.sheet = light_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    if (!bgfx::isValid(flare_)) return;
    for (const Flare& one : flares_) {
        // Not on its first frame, as MU's (`LifeTime != 60`).
        if (!one.alive || one.life > kLife - 1.0f) continue;
        // count = (Velocity[0] + LifeTime) * 0.1 on MU's ground plane; its x is ours, its y our
        // -z, and its height our y.
        const float count = (one.phase + one.life) * 0.1f;
        const float* at = wearers_[one.wearer].feet;
        gfx::Sprite sprite;
        sprite.position[0] = at[0] + std::sin(count) * kOrbit * per_;
        sprite.position[1] = at[1] + one.height * per_;
        sprite.position[2] = at[2] + std::cos(count) * kOrbit * per_;
        sprite.halfWidth = sprite.halfHeight = kTexels * one.scale * kFlareSize * per_ * 0.5f;
        sprite.spin = one.spin;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kStrength;
        sprite.sheet = flare_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
}

}  // namespace mu::game
