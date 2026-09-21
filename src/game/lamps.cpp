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

// --- the fire, sprint 8b --------------------------------------------------------------------
//
// Sprint 8a drew MU's own BITMAP_FIRE: Effect/Fire01's dim red strip, tinted (L, 0.6L, 0.4L),
// drifting a metre SIDEWAYS along the holder's -y while barely rising, and fading to nothing
// within a quarter of its life because three of the sheet's four cells are 9-19x darker than
// the first. Faithful, and not fire. What follows is ours, marked invention throughout, and
// keeps from MU what reads: its painted flame shapes, the four cells, the lean off the wall
// along the holder's -y, the size, and where every fire burns.
//
// A flame is born at the fuel, hot, and rises on its own buoyancy, swaying, tapering and
// cooling from yellow-white at the base to red at the tip; fs_flame turns that heat into
// colour and the bloom catches the hot part. Embers leave a bonfire now and then and a torch
// rarely; a bonfire smokes.
struct Profile {
    float flamesPerSecond;
    float size[2];          // full width of a flame at birth, metres
    float life[2];          // seconds
    float rise[2];          // m/s at birth, straight up
    float lift;             // m/s^2 of buoyancy
    float spread;           // metres of jitter round the fuel, across
    float lean;             // m/s along MU's drift, the holder's -y
    float base;             // metres the fuel sits below MU's emitter point
    float embersPerSecond;
    float smokePerSecond;
};
// A torch: the cages and the bridges' and the gate's fires, FireLight01/02 and the rest.
constexpr Profile kTorch = {38.0f, {0.42f, 0.62f}, {0.38f, 0.62f}, {0.45f, 0.8f}, 1.8f,
                            0.07f, 0.12f, 0.05f, 0.7f, 0.0f};
// A bonfire, Bonfire01: wider, taller, more of it, embers and smoke. MU's emitter point is
// 60 units over the logs, where the light belongs; the flames start at the logs.
constexpr Profile kBonfire = {60.0f, {0.75f, 1.15f}, {0.6f, 1.0f}, {0.55f, 0.95f}, 2.0f,
                              0.15f, 0.05f, 0.35f, 12.0f, 2.6f};

// Fire01's four cells in linear light, by their 99th percentile: 0.631, 0.072, 0.033, 0.033.
// Each is scaled to the first so every cell is a whole flame. fs_flame reads gain / 20.
constexpr float kCellGain[4] = {1.0f, 8.8f, 19.0f, 19.0f};
constexpr int kCells = 4;

// Flames live only near the camera. MU's camera sees about 25 m of ground; a fire starts
// burning at kFlameMetres + 5 and is drawn inside kFlameMetres, so it is already alight when
// it comes into view.
constexpr size_t kMostParticles = 3072;

float mix(float a, float b, float t) { return a + (b - a) * t; }
float smooth(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

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
                     float spin, bool bonfire) {
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
            fire.bonfire = bonfire;
            // Staggered, so eighty fires do not all spawn on the same frame.
            fire.clock = unit();
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
            place(*one, at, drift, instance.pitch,
                  cooked.models[instance.model].name == "Bonfire01");
        }
    }
    // The hidden anchors carry no angle in the cook, so their flames stand rather than drift.
    // Two in Lorencia. A gap, marked.
    const float still[3] = {0.0f, 0.0f, 0.0f};
    for (const content::TownEmitter* one : anchors) place(*one, one->at, still, 0.0f, false);

    levels_.assign(lights_.size(), 1.0f);
    for (size_t i = 0; i < lights_.size(); ++i) levels_[i] = lights_[i].flicker.current;
    particles_.reserve(kMostParticles);

    // The square the grid covers: the whole map, which is where every light is.
    minX_ = 0.0f;
    minZ_ = -float(ground.size()) * ground.metresPerTile();
    side_ = float(ground.size()) * ground.metresPerTile();

    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        const auto take = [&](const char* name) -> bgfx::TextureHandle {
            const content::EffectSheet* sheet = table.effect(name);
            if (sheet == nullptr) return BGFX_INVALID_HANDLE;
            return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        };
        sheet_ = take("fire");     // Effect/Fire01, MU's own flame
        spark_ = take("light");    // Effect/flare01, a soft round dot: an ember
        smoke_ = take("smoke");    // Effect/smoke02
    }
    if (!bgfx::isValid(sheet_) && !fires_.empty()) {
        core::logError("no cooked 'fire' sheet: %zu fires will burn with no flame drawn "
                       "(tools/cook.py --only showing)", fires_.size());
    }
    core::logf("lamps: %zu lights, %zu fires, %zu flickering glows; sheets: fire %s, ember %s, "
               "smoke %s", lights_.size(), fires_.size(), glows_.size(),
               bgfx::isValid(sheet_) ? "yes" : "NO", bgfx::isValid(spark_) ? "yes" : "NO",
               bgfx::isValid(smoke_) ? "yes" : "NO");
    return true;
}

void Lamps::shutdown() {
    set_.clear();
    lights_.clear();
    levels_.clear();
    glows_.clear();
    fires_.clear();
    particles_.clear();
    sheet_ = spark_ = smoke_ = BGFX_INVALID_HANDLE;
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

void Lamps::spawn(const Fire& fire, uint8_t kind) {
    if (particles_.size() >= particles_.capacity()) {
        ++refused_;
        return;
    }
    const Profile& p = fire.bonfire ? kBonfire : kTorch;
    Particle one;
    one.kind = kind;
    one.phase = 6.2831853f * unit();
    const float angle = 6.2831853f * unit();
    const float radius = p.spread * std::sqrt(unit());
    one.position[0] = fire.at[0] + std::cos(angle) * radius;
    one.position[1] = fire.at[1] - p.base;
    one.position[2] = fire.at[2] + std::sin(angle) * radius;
    const float lean[3] = {fire.drift[0] * kPerMetre * p.lean, 0.0f,
                           fire.drift[2] * kPerMetre * p.lean};
    if (kind == kFlame) {
        one.life = mix(p.life[0], p.life[1], unit());
        one.size = mix(p.size[0], p.size[1], unit());
        one.heat = mix(0.85f, 1.05f, unit());
        one.cell = uint8_t(next() % kCells);
        one.spin = (unit() - 0.5f) * 0.5f;
        one.spinRate = (unit() - 0.5f) * 1.2f;
        one.velocity[0] = lean[0] + (unit() - 0.5f) * 0.16f;
        one.velocity[1] = mix(p.rise[0], p.rise[1], unit());
        one.velocity[2] = lean[2] + (unit() - 0.5f) * 0.16f;
    } else if (kind == kEmber) {
        one.life = mix(1.2f, 2.4f, unit());
        one.size = mix(0.08f, 0.14f, unit());
        one.heat = mix(0.8f, 1.0f, unit());
        one.position[1] += 0.1f;
        one.velocity[0] = lean[0] + (unit() - 0.5f) * 0.9f;
        one.velocity[1] = mix(1.0f, 2.2f, unit());
        one.velocity[2] = lean[2] + (unit() - 0.5f) * 0.9f;
    } else {
        // Smoke leaves from over the flames rather than out of the logs.
        one.life = mix(2.6f, 3.8f, unit());
        one.size = mix(0.5f, 0.8f, unit());
        one.position[1] += 0.9f;
        one.spin = 6.2831853f * unit();
        one.spinRate = (unit() - 0.5f) * 0.5f;
        one.velocity[0] = lean[0] + (unit() - 0.5f) * 0.15f;
        one.velocity[1] = mix(0.45f, 0.7f, unit());
        one.velocity[2] = lean[2] + (unit() - 0.5f) * 0.15f;
    }
    particles_.push_back(one);
}

void Lamps::update(float seconds, Town& town, gfx::Renderer& renderer, const float near[3]) {
    for (size_t i = 0; i < lights_.size(); ++i) {
        step(lights_[i].flicker, seconds);
        levels_[i] = lights_[i].flicker.current;
    }
    if (!levels_.empty()) renderer.setPointLightLevels(levels_.data(), uint32_t(levels_.size()));
    for (Glow& glow : glows_) {
        step(glow.flicker, seconds);
        town.setGlowLevel(glow.instance, glow.flicker.current);
    }

    // A long hitch would move every flame a metre at once. Capped, as a fire does not need to
    // be right about a frame it never drew.
    const float dt = std::min(seconds, 0.1f);
    for (Particle& one : particles_) {
        one.age += dt;
        if (one.age >= one.life) continue;
        const float t = one.age / one.life;
        // A sway that is the particle's own: two sines, so no two flames beat together.
        const float swayX = std::sin(one.phase + one.age * 7.0f) + 0.5f * std::sin(one.phase * 2.3f + one.age * 13.0f);
        const float swayZ = std::cos(one.phase * 1.7f + one.age * 6.0f);
        if (one.kind == kFlame) {
            one.velocity[1] += kTorch.lift * dt;
            one.velocity[0] += swayX * 0.9f * dt;
            one.velocity[2] += swayZ * 0.9f * dt;
        } else if (one.kind == kEmber) {
            // Carried up by the heat and then losing it; kicked about by the air.
            one.velocity[1] += (0.6f - 1.4f * t) * dt;
            one.velocity[0] += swayX * 2.2f * dt;
            one.velocity[2] += swayZ * 2.2f * dt;
        } else {
            one.velocity[0] += swayX * 0.12f * dt;
            one.velocity[2] += swayZ * 0.12f * dt;
            one.velocity[1] *= std::pow(0.8f, dt);
        }
        for (int i = 0; i < 3; ++i) one.position[i] += one.velocity[i] * dt;
        one.spin += one.spinRate * dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.age >= p.life; }),
                     particles_.end());

    const float wake2 = (kFlameMetres + 5.0f) * (kFlameMetres + 5.0f);
    for (Fire& fire : fires_) {
        const float dx = fire.at[0] - near[0], dz = fire.at[2] - near[2];
        if (dx * dx + dz * dz > wake2) {
            fire.clock = 0.0f;
            continue;
        }
        const Profile& p = fire.bonfire ? kBonfire : kTorch;
        // Each kind keeps its own debt of particles owed, and pays it whole.
        fire.clock += dt * p.flamesPerSecond;
        while (fire.clock >= 1.0f) {
            fire.clock -= 1.0f;
            spawn(fire, kFlame);
        }
        fire.embers += dt * p.embersPerSecond;
        while (fire.embers >= 1.0f) {
            fire.embers -= 1.0f;
            // Not on the clock: embers come in no rhythm.
            if (unit() < 0.7f) spawn(fire, kEmber);
        }
        fire.smoke += dt * p.smokePerSecond;
        while (fire.smoke >= 1.0f) {
            fire.smoke -= 1.0f;
            if (bgfx::isValid(smoke_)) spawn(fire, kSmoke);
        }
    }
}

void Lamps::gather(gfx::Effects& effects, const float near[3], float daylight) const {
    drawn_ = 0;
    if (!bgfx::isValid(sheet_)) return;
    const float range2 = kFlameMetres * kFlameMetres;
    for (const Particle& one : particles_) {
        const float dx = one.position[0] - near[0], dz = one.position[2] - near[2];
        if (dx * dx + dz * dz > range2) continue;
        const float t = one.age / one.life;
        gfx::Sprite sprite;
        for (int i = 0; i < 3; ++i) sprite.position[i] = one.position[i];
        sprite.spin = one.spin;
        if (one.kind == kFlame) {
            // Swells as it leaves the fuel, then tapers to a tongue; taller than wide.
            const float size = one.size * (0.55f + 0.45f * smooth(0.0f, 0.2f, t)) *
                               (1.0f - 0.72f * std::pow(t, 1.4f));
            sprite.halfWidth = 0.5f * size * 0.85f;
            sprite.halfHeight = 0.5f * size * 1.3f;
            sprite.u0 = float(one.cell) / float(kCells);
            sprite.u1 = sprite.u0 + 1.0f / float(kCells);
            sprite.colour[0] = kCellGain[one.cell] / 20.0f;
            sprite.colour[1] = one.heat * std::pow(1.0f - t, 1.1f);
            sprite.colour[2] = 0.0f;
            sprite.colour[3] = smooth(0.0f, 0.06f, t) * (1.0f - smooth(0.7f, 1.0f, t));
            sprite.sheet = sheet_;
            sprite.blend = gfx::Blend::Flame;
        } else if (one.kind == kEmber) {
            if (!bgfx::isValid(spark_)) continue;
            sprite.halfWidth = sprite.halfHeight = 0.5f * one.size * (1.0f - 0.5f * t);
            // flare01 is white; fs_flame reads its red as the shape, and the heat cools it
            // from yellow to a dying red.
            sprite.colour[0] = 1.0f / 20.0f * 5.0f;
            sprite.colour[1] = one.heat * (1.0f - 0.75f * t);
            sprite.colour[2] = 0.0f;
            // Twinkles: an ember turning over in the air.
            const float twinkle = 0.65f + 0.35f * std::sin(one.phase + one.age * 23.0f);
            sprite.colour[3] = twinkle * (1.0f - smooth(0.6f, 1.0f, t));
            sprite.sheet = spark_;
            sprite.blend = gfx::Blend::Flame;
        } else {
            const float size = one.size * (1.0f + 1.8f * t);
            sprite.halfWidth = sprite.halfHeight = 0.5f * size;
            // Warm grey near the fire, cooling to ash grey.
            const float warm = 1.0f - t;
            // Smoke is not lit by the pass, so it is lit here: grey by the day's light, and warm
            // from the fire under it while it is young and low.
            const float fire = warm * warm * 0.35f;
            sprite.colour[0] = 0.30f * daylight + fire;
            sprite.colour[1] = 0.29f * daylight + fire * 0.45f;
            sprite.colour[2] = 0.30f * daylight + fire * 0.12f;
            sprite.colour[3] = 0.85f * smooth(0.0f, 0.15f, t) * (1.0f - smooth(0.35f, 1.0f, t));
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Smoke;
        }
        if (!effects.add(sprite)) break;
        ++drawn_;
    }
}

}  // namespace mu::game
