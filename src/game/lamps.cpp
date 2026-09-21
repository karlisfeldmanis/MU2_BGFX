#include "game/lamps.h"

#include <algorithm>
#include <cmath>

#include "content/placement.h"
#include "content/showing.h"
#include "core/log.h"
#include "game/town.h"

namespace mu::game {
namespace {

// MU's reference clock and its units. Every rate below is per frame of it, and
// docs/conventions.md carries the trap: a speed takes one factor of 25, an acceleration two.
constexpr float kReference = 25.0f;
constexpr float kPerMetre = 100.0f;

// --- the flame: BITMAP_FIRE out of CreateFire(0), ZzzEffectFireLeave.cpp:61 ---------------
// Spawned on half the reference frames (rand_fps_check(2)), born within +-8 units of the
// fire, with the fire's own light (L, 0.6L, 0.4L), L = rand[0.6, 1.1), and a subtype of
// Random::RangeInt(0, 3) -- inclusive, so four kinds.
constexpr float kJitter = 8.0f;
constexpr float kSpawnChance = 0.5f;
// ZzzEffectParticle.cpp:385-398 (create) and 4537-4545, 4688-4696 (move). Subtype 0 is the
// flame: 24 frames, born at 1.28-1.92, shrinking 0.04 a frame. Subtype 1 is born at a tenth
// and grows on its lift into a whole flame. Subtypes 2 and 3 have no case at creation, so they
// take CreateParticle's defaults -- two frames, scale 1, standing still -- and flash. All of
// them drift at 3.2-4.7 units a frame along the object's -y and rise on a lift that builds by
// 0.004 a frame. This is Season 6's MuMain, the only client source here; 0.75's own
// CreateFire is not checked.
constexpr float kLifeFlame = 24.0f;
constexpr float kLifeFlash = 2.0f;
constexpr float kLift = 0.004f;
constexpr float kLiftToRise = 10.0f;  // Position[2] += Gravity * 10
constexpr float kShrink = 0.04f;
constexpr float kDriftDecay = 0.98f;
// Effect/Fire01 is four 64-pixel cells in a 256 strip; RenderSprite draws a quarter of the
// strip's width by its full height, and halves both inside. So a cell is 64 units square
// times the scale.
constexpr float kCellUnits = 64.0f;
constexpr int kCells = 4;
constexpr float kFramesPerCell = 6.0f;  // Frame = (23 - LifeTime) / 6

// Room for every fire in Lorencia at once, reserved at open and never grown. A fire keeps
// about seven alive; 88 fires is some six hundred.
constexpr size_t kMostFlames = 2048;

}  // namespace

uint32_t Lamps::next() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

float Lamps::unit() { return float(next() & 0xFFFFFF) / float(0x1000000); }

bool Lamps::open(const std::string& assetDir, const Town& town, const content::Ground& ground,
                 content::Textures& textures) {
    shutdown();
    const content::CookedTown& cooked = town.cooked();

    // Which emitters each model carries, found once.
    std::vector<std::vector<const content::TownEmitter*>> carried(cooked.models.size());
    std::vector<const content::TownEmitter*> anchors;
    for (const content::TownEmitter& one : cooked.emitters) {
        if (one.model == content::TownEmitter::kWorld) {
            anchors.push_back(&one);
        } else {
            carried[one.model].push_back(&one);
        }
    }
    std::vector<const content::TownGlow*> glowOf(cooked.models.size(), nullptr);
    for (const content::TownGlow& one : cooked.glows) glowOf[one.model] = &one;

    // One emitter, at a world point, turned the way its holder is turned.
    auto place = [&](const content::TownEmitter& one, const float at[3], const float drift[3],
                     float spin) {
        if (one.kind != content::EmitterKind::Smoke && one.reach > 0.0f) {
            gfx::PointLight light;
            for (int i = 0; i < 3; ++i) {
                light.position[i] = at[i];
                light.colour[i] = one.colour[i];
            }
            // MU's light is two-dimensional: R tiles on the ground, height ignored. So the reach
            // is R measured flat, and the light's height above the ground under it is the band
            // in which height stays ignored. lights.sh.
            light.reach = one.reach;
            light.height = std::max(0.0f, at[1] - ground.heightAt(at[0], at[2]));
            set_.push_back(light);
            Light state;
            state.flicker.low = one.low;
            state.flicker.high = one.high;
            state.flicker.hz = one.flickerHz;
            state.flicker.smooth = one.smoothSeconds;
            state.flicker.current = state.flicker.target = one.high;
            lights_.push_back(state);
        }
        if (one.kind == content::EmitterKind::Fire) {
            Fire fire;
            for (int i = 0; i < 3; ++i) {
                fire.at[i] = at[i];
                fire.drift[i] = drift[i];
            }
            fire.spin = spin;
            fires_.push_back(fire);
        }
    };

    for (uint32_t index = 0; index < cooked.instances.size(); ++index) {
        const content::TownInstance& instance = cooked.instances[index];
        if (glowOf[instance.model] != nullptr) {
            const content::TownGlow& g = *glowOf[instance.model];
            Glow glow;
            glow.instance = index;
            glow.flicker.low = g.low;
            glow.flicker.high = g.high;
            glow.flicker.hz = g.flickerHz;
            glow.flicker.smooth = g.smoothSeconds;
            glows_.push_back(glow);
        }
        if (carried[instance.model].empty()) continue;
        // Turned and not scaled: CreateFire rotates the offset by the object's angle and never
        // multiplies it by the object's scale.
        float turn[16];
        content::placementTransform(instance.pitch, instance.yaw, instance.roll, 1.0f,
                                    instance.position, turn);
        // MU's drift is (0, -v, 0) in its own axes, which is (0, 0, +v) in ours.
        float drift[3];
        for (int j = 0; j < 3; ++j) drift[j] = turn[2 * 4 + j] / kPerMetre;
        for (const content::TownEmitter* one : carried[instance.model]) {
            float at[3];
            for (int j = 0; j < 3; ++j) {
                at[j] = one->at[0] * turn[0 * 4 + j] + one->at[1] * turn[1 * 4 + j] +
                        one->at[2] * turn[2 * 4 + j] + turn[3 * 4 + j];
            }
            place(*one, at, drift, instance.pitch);
        }
    }
    // The hidden anchors carry no angle in the cook, so their flames stand rather than drift.
    // Two in Lorencia. A gap, marked.
    const float still[3] = {0.0f, 0.0f, 0.0f};
    for (const content::TownEmitter* one : anchors) place(*one, one->at, still, 0.0f);

    levels_.assign(lights_.size(), 1.0f);
    for (size_t i = 0; i < lights_.size(); ++i) levels_[i] = lights_[i].flicker.current;
    flames_.reserve(kMostFlames);

    // The square the grid covers: the whole map, which is where every light is.
    minX_ = 0.0f;
    minZ_ = -float(ground.size()) * ground.metresPerTile();
    side_ = float(ground.size()) * ground.metresPerTile();

    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        if (const content::EffectSheet* fire = table.effect("fire")) {
            sheet_ = textures.load(assetDir + "/" + fire->path, content::TextureRole::Albedo);
        }
    }
    if (!bgfx::isValid(sheet_) && !fires_.empty()) {
        core::logError("no cooked 'fire' sheet: %zu fires will burn with no flame drawn "
                       "(tools/cook.py --only showing)", fires_.size());
    }
    core::logf("lamps: %zu lights, %zu fires, %zu flickering glows", lights_.size(),
               fires_.size(), glows_.size());
    return true;
}

void Lamps::shutdown() {
    set_.clear();
    lights_.clear();
    levels_.clear();
    glows_.clear();
    fires_.clear();
    flames_.clear();
    sheet_ = BGFX_INVALID_HANDLE;
}

void Lamps::light(gfx::Renderer& renderer) const {
    renderer.setPointLights(set_.data(), uint32_t(set_.size()), minX_, minZ_, side_);
}

void Lamps::step(Flicker& one, float seconds) {
    if (one.high <= one.low || one.hz <= 0.0f) return;
    one.wait -= seconds;
    if (one.wait <= 0.0f) {
        one.target = one.low + (one.high - one.low) * unit();
        one.wait = 1.0f / one.hz;
    }
    // Exponential, so the ease does not depend on the frame rate: a brightness that lands and
    // waits reads as a blink rather than as a flame. Lamps.cs.
    const float rate = one.smooth > 0.0f ? 1.0f - std::exp(-seconds / one.smooth) : 1.0f;
    one.current += (one.target - one.current) * rate;
}

void Lamps::spawn(const Fire& fire) {
    if (flames_.size() >= kMostFlames) {
        ++refused_;
        return;
    }
    Flame flame;
    // Random::RangeFloat(-8, 7) on each of MU's axes.
    for (int i = 0; i < 3; ++i) {
        flame.position[i] = fire.at[i] + (-kJitter + 15.0f * unit()) / kPerMetre;
    }
    flame.subType = uint8_t(next() % 4);
    const float luminosity = 0.6f + 0.5f * unit();
    flame.colour[0] = luminosity;
    flame.colour[1] = luminosity * 0.6f;
    flame.colour[2] = luminosity * 0.4f;
    flame.spin = fire.spin;
    if (flame.subType <= 1) {
        flame.life = kLifeFlame;
        const float speed = float(32 + next() % 16) * 0.1f;  // rand() % 16 + 32, a tenth
        for (int i = 0; i < 3; ++i) flame.drift[i] = fire.drift[i] * speed;
        flame.scale = flame.subType == 0 ? float(128 + next() % 64) * 0.01f
                                         : float(10 + next() % 4) * 0.01f;
    } else {
        flame.life = kLifeFlash;
        flame.scale = 1.0f;
    }
    flames_.push_back(flame);
}

void Lamps::update(float seconds, Town& town, gfx::Renderer& renderer) {
    for (size_t i = 0; i < lights_.size(); ++i) {
        step(lights_[i].flicker, seconds);
        levels_[i] = lights_[i].flicker.current;
    }
    if (!levels_.empty()) renderer.setPointLightLevels(levels_.data(), uint32_t(levels_.size()));
    for (Glow& glow : glows_) {
        step(glow.flicker, seconds);
        town.setGlowLevel(glow.instance, glow.flicker.current);
    }

    // The flames, on MU's clock. `frames` is how many reference frames this frame is worth,
    // which is what FPS_ANIMATION_FACTOR is in the client.
    const float frames = seconds * kReference;
    for (Flame& flame : flames_) {
        flame.life -= frames;
        if (flame.life <= 0.0f) continue;
        for (int i = 0; i < 3; ++i) flame.position[i] += flame.drift[i] * frames;
        flame.gravity += kLift * frames;
        if (flame.subType == 0) {
            flame.scale -= kShrink * frames;
        } else {
            flame.scale += flame.gravity * frames;
            const float decay = std::pow(kDriftDecay, frames);
            for (float& d : flame.drift) d *= decay;
        }
        flame.position[1] += flame.gravity * kLiftToRise * frames / kPerMetre;
    }
    flames_.erase(std::remove_if(flames_.begin(), flames_.end(),
                                 [](const Flame& f) { return f.life <= 0.0f || f.scale <= 0.0f; }),
                  flames_.end());

    // One chance a reference frame per fire, at a half. A frame worth several reference
    // frames takes several chances, and a fast frame carries its fraction to the next.
    for (Fire& fire : fires_) {
        fire.clock += frames;
        while (fire.clock >= 1.0f) {
            fire.clock -= 1.0f;
            if (unit() < kSpawnChance) spawn(fire);
        }
    }
}

void Lamps::gather(gfx::Effects& effects, const float near[3]) const {
    drawn_ = 0;
    if (!bgfx::isValid(sheet_)) return;
    const float range2 = kFlameMetres * kFlameMetres;
    for (const Flame& flame : flames_) {
        const float dx = flame.position[0] - near[0], dz = flame.position[2] - near[2];
        if (dx * dx + dz * dz > range2) continue;
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = flame.position[i];
        sprite.halfWidth = sprite.halfHeight = 0.5f * kCellUnits * flame.scale / kPerMetre;
        sprite.spin = flame.spin;
        const int cell = std::clamp(int((23.0f - flame.life) / kFramesPerCell), 0, kCells - 1);
        sprite.u0 = float(cell) / float(kCells);
        sprite.u1 = sprite.u0 + 1.0f / float(kCells);
        for (int i = 0; i < 3; ++i) sprite.colour[i] = flame.colour[i];
        sprite.colour[3] = 1.0f;
        sprite.sheet = sheet_;
        // BITMAP_FIRE is drawn added, as every MU flame is: its black is what cuts it out.
        sprite.blend = gfx::Blend::Additive;
        if (!effects.add(sprite)) break;
        ++drawn_;
    }
}

}  // namespace mu::game
