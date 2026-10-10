#include "game/fx/set_shine.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// The ribbon: CreateJoint(BITMAP_FLARE, ..., 0, o) at its default scale of ten
// (ZzzEffectJoint.cpp:1669-1688): one joint on a ring of forty, a cross ten units wide, a
// hundred frames, climbing 0 to 1.5 units a frame (Direction[2] = rand() % 150 / 100), twenty
// tails, in his white light.
constexpr Recipe kSetRibbon{1,     40.0f, 10.0f, 100.0f, 0.0f, 1.5f, 19.0f, 0.0f,
                            false, false, false, {1.0f, 1.0f, 1.0f}};
// Invention, the level-up's (game/fx/aura.cpp kStrength): Flare added at 1 burned white in this
// engine's light.
constexpr float kStrength = 0.65f;
// Invention: the arms' lights at twice MU's half. MU adds 0.5 into 8-bit colour, where it shows
// as a glow; in this engine's light it vanished on the gold of a lit suit even at night. Four
// times was a yellow blaze; two is judged on the night shot of a +11 Brass set.
constexpr float kArmGain = 2.0f;
// Invention: the climbing flares at 0.6 of MU's 0.19 -- the user, 2026-10-10: 'make +11
// sparkles smaller'. At MU's own they read as blobs round a lit suit.
constexpr float kFlareSize = 0.6f;

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
        core::logError("set shine: no %s (tools/sync.sh)", path.c_str());
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
        // And new ones, a frame at a time: one frame in twenty, at +10 a flare, at +11 a ribbon
        // one time in eight and a flare the rest. Past +11 is MU's later sets, drawn as +11.
        if (one.plus < 10) {
            one.clock = 0.0f;
            continue;
        }
        one.clock += frames;
        while (one.clock >= 1.0f) {
            one.clock -= 1.0f;
            if (unit() >= 1.0f / 20.0f) continue;
            if (one.plus >= 11 && unit() < 1.0f / 8.0f) {
                one.ribbons.cast(kSetRibbon, one.feet, one.yaw, metresPerTile);
            } else {
                throwFlare(w);
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
