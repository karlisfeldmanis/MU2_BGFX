#include "game/world/lamps.h"

#include <algorithm>
#include <cmath>

#include "content/placement.h"
#include "content/showing.h"
#include "core/log.h"
#include "game/world/sway.h"
#include "game/world/town.h"

namespace mu::game {
namespace {

// MU's units. Its rates are per frame of its 25 Hz reference clock, and docs/conventions.md
// carries the trap: a speed takes one factor of 25, an acceleration two.
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
// A dragon's breath (Fire::breath): how far a torch may stand from an Object17's origin and
// be one of its mouths -- the heads' torches stand 2.5 to 2.9 m off it, the nearest other
// torch 6 m -- and how its flames go: thrown out along the mouth at kBreathSpeed, slowing,
// rising only a little, and gone sooner than a torch's. Ours, judged by eye.
constexpr float kBreathReach = 3.5f;
constexpr float kBreathSpeed[2] = {3.0f, 4.2f};
constexpr float kBreathLift = 0.45f;
constexpr float kBreathDrag = 1.1f;
// The mouth, in the dragon's own frame, ours (x, up, forward) in metres: Object17's two
// deep_dragon heads are centred at MU local x -150 and +150, end at local y -290, and open
// their jaws between z 88 and 139 -- so between the jaws at 1.13 m, and a little inside the
// tip, as the user asked ("a little bit inside the dragon's mouth").
constexpr float kMouthSide = 1.5f;
constexpr float kMouthHeight = 1.13f;
constexpr float kMouthForward = 2.9f - 0.15f;
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

// The bone a model's light hangs from, where the model sways. Read off the rig: the street
// lamp's lantern and its glow (streetlight.jpg's lamp end, streetlight_brightness2.jpg whole)
// are weighted to Bone02 alone, and the light is MU's lamp offset, on that lantern. The candle
// is not here: its flames hang off three bones and its one light is at none of them.
// How far off its holder's middle a fire or a window's light must sit to be given a side:
// a quarter-metre, so a bonfire's or a brazier's flame, which stands on its holder, lights all
// round and a torch on a wall does not.
constexpr float kSidedFrom = 0.25f;

// The side of its holder a torch or a window's light lights, into `away`: flat, unit length,
// in the world. A torch on a wall or a rail, and a window, lights its own side of the thing
// that carries it and not through it (lights.sh). Lamps and candles hang free and light all
// round, and so does a fire standing on its holder's middle -- a bonfire, a brazier.
//
// The side is the face of the holder's box the light is NEAREST, and not the way out from the
// holder's middle: a bridge's torches stand 0.9 m out to its side and 2 m along it, so "away
// from the middle" pointed along the bridge and cut the light off over half the river. The
// nearest face is the rail they hang on.
bool sideOf(const content::TownEmitter& one, const content::TownModel& holder,
            const float turn[16], float away[3]) {
    if (one.kind != content::EmitterKind::Fire && one.kind != content::EmitterKind::Window) {
        return false;
    }
    float nearest = 1e9f;
    int axis = -1;
    float sign = 0.0f;
    for (int k : {0, 2}) {
        if (std::fabs(one.at[k]) <= kSidedFrom) continue;
        const float toMax = std::fabs(holder.max[k] - one.at[k]);
        const float toMin = std::fabs(one.at[k] - holder.min[k]);
        if (toMax < nearest) { nearest = toMax; axis = k; sign = 1.0f; }
        if (toMin < nearest) { nearest = toMin; axis = k; sign = -1.0f; }
    }
    if (axis < 0) return false;
    // That face's outward axis, turned into the world by the placement's own rotation.
    float x = sign * turn[axis * 4 + 0], z = sign * turn[axis * 4 + 2];
    const float length = std::sqrt(x * x + z * z);
    if (length < 1e-4f) return false;
    away[0] = x / length;
    away[1] = 0.0f;
    away[2] = z / length;
    return true;
}

const char* riddenBone(const std::string& model) {
    if (model == "StreetLight01") return "Bone02";
    // The Lost Tower's lamp post: its orb and re_008's light dots are weighted to Box03 alone,
    // which its clip bobs 12 cm down and back every three seconds; the light (ours) bobs with it.
    // The name is only the tower's here: no other world's Object24 carries a light.
    if (model == "Object24") return "Box03";
    return nullptr;
}

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
    size_t sided = 0;
    for (const content::TownGlow& one : cooked.glows) glowOf[one.model] = &one;

    // One emitter, at a world point, turned the way its holder is turned.
    // `away` is the side of its holder it lights, flat and unit length in the world, or null
    // for a light that lights all round. See sideOf.
    auto place = [&](const content::TownEmitter& one, const float at[3], const float drift[3],
                     float spin, bool bonfire, bool crackles, const float* away) {
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
            if (away != nullptr) {
                light.away[0] = away[0];
                light.away[2] = away[2];
                ++sided;
            }
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
            fire.crackles = crackles;
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
            // Started at its own top, as a light is. step() leaves a flicker whose range is
            // empty where it stands, and from the default 1.0 a steady glow -- low == high --
            // drew at full brightness whatever level it named.
            glow.flicker.current = glow.flicker.target = g.high;
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
        const std::string& model = cooked.models[instance.model].name;
        const char* rides = riddenBone(model);
        // OURS: the Dungeon's dragons breathe. MU hid a wall torch, Object42, inside each of
        // the two heads an Object17 carries -- at the mouth, turned the way the head faces
        // (docs/dungeon-port.md) -- and its fire rises there like any torch's, which on a
        // dragon reads as a head on fire (the user, 2026-09-30: "it has to go from mouth and
        // more horizontally"). A torch within kBreathReach of an Object17's origin throws its
        // flames out along its drift instead. Only the Dungeon's Object42 carries a fire.
        bool breathes = false;
        float mouth[3] = {0, 0, 0}, facing[3] = {0, 0, 0};
        if (model == "Object42") {
            for (const content::TownInstance& other : cooked.instances) {
                if (cooked.models[other.model].name != "Object17") continue;
                const float dx = other.position[0] - instance.position[0];
                const float dz = other.position[2] - instance.position[2];
                if (dx * dx + dz * dz >= kBreathReach * kBreathReach) continue;
                breathes = true;
                float head[16];
                content::placementTransform(other.pitch, other.yaw, other.roll, 1.0f,
                                            other.position, head);
                // The head of the two this torch is in: the side nearer the torch.
                const float side = (-dx) * head[0] + (-dz) * head[2] >= 0.0f ? kMouthSide
                                                                              : -kMouthSide;
                const float local[3] = {side, kMouthHeight, kMouthForward};
                for (int j = 0; j < 3; ++j) {
                    mouth[j] = local[0] * head[0 * 4 + j] + local[1] * head[1 * 4 + j] +
                               local[2] * head[2 * 4 + j] + head[3 * 4 + j];
                }
                const float fl = std::sqrt(head[8] * head[8] + head[10] * head[10]);
                facing[0] = fl > 1e-4f ? head[8] / fl : 0.0f;
                facing[2] = fl > 1e-4f ? head[10] / fl : 0.0f;
            }
        }
        for (const content::TownEmitter* one : carried[instance.model]) {
            float at[3];
            for (int j = 0; j < 3; ++j) {
                at[j] = one->at[0] * turn[0 * 4 + j] + one->at[1] * turn[1 * 4 + j] +
                        one->at[2] * turn[2 * 4 + j] + turn[3 * 4 + j];
            }
            const size_t before = set_.size();
            float away[3];
            const bool sided = sideOf(*one, cooked.models[instance.model], turn, away);
            const bool bonfire = model == "Bonfire01";
            const size_t firesBefore = fires_.size();
            place(*one, at, drift, instance.pitch, bonfire, bonfire || model == "Object67",
                  sided ? away : nullptr);
            if (breathes && fires_.size() > firesBefore) {
                Fire& fire = fires_.back();
                fire.breath = true;
                for (int j = 0; j < 3; ++j) {
                    fire.mouth[j] = mouth[j];
                    fire.facing[j] = facing[j];
                }
            }
            if (rides != nullptr && set_.size() > before) {
                Rider rider;
                rider.light = uint32_t(before);
                rider.instance = index;
                rider.boneName = rides;
                // The offset is laid unscaled (CreateFire's rule, above), and the pose scales
                // what it carries, so it goes in divided by the scale: at rest the two agree.
                const float scale = std::max(instance.scale, 0.01f);
                for (int j = 0; j < 3; ++j) rider.model[j] = one->at[j] / scale;
                riders_.push_back(rider);
            }
        }
    }
    // The hidden anchors carry no angle in the cook, so their flames stand rather than drift.
    // Two in Lorencia. A gap, marked.
    const float still[3] = {0.0f, 0.0f, 0.0f};
    for (const content::TownEmitter* one : anchors) {
        const size_t before = lights_.size();
        place(*one, one->at, still, 0.0f, false, false, nullptr);
        if (one->kind == content::EmitterKind::Vent && lights_.size() > before) {
            Vent vent;
            vent.light = uint32_t(before);
            for (int i = 0; i < 3; ++i) vent.at[i] = one->at[i];
            vents_.push_back(vent);
        }
    }

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
    core::logf("lamps: %zu lights (%zu lighting only their own side of their holder), %zu fires, "
               "%zu flickering glows; sheets: fire %s, ember %s, smoke %s", lights_.size(), sided,
               fires_.size(), glows_.size(),
               bgfx::isValid(sheet_) ? "yes" : "NO", bgfx::isValid(spark_) ? "yes" : "NO",
               bgfx::isValid(smoke_) ? "yes" : "NO");
    return true;
}

void Lamps::shutdown() {
    riders_.clear();
    set_.clear();
    lights_.clear();
    levels_.clear();
    glows_.clear();
    fires_.clear();
    particles_.clear();
    sheet_ = spark_ = smoke_ = BGFX_INVALID_HANDLE;
}

void Lamps::add(const gfx::PointLight& light, float low, float high, float hz, float smooth) {
    set_.push_back(light);
    Light state;
    state.flicker.low = low;
    state.flicker.high = high;
    state.flicker.hz = hz;
    state.flicker.smooth = smooth;
    state.flicker.current = state.flicker.target = high;
    lights_.push_back(state);
    levels_.push_back(high);
}

void Lamps::light(gfx::Renderer& renderer) const {
    renderer.setPointLights(set_.data(), uint32_t(set_.size()), minX_, minZ_, side_);
}

void Lamps::follow(const Sway& sway, gfx::Renderer& renderer) {
    for (Rider& rider : riders_) {
        const Figure* figure = sway.posedAt(rider.instance);
        if (figure == nullptr) continue;
        if (rider.bone == -2) {
            rider.bone = -1;
            const FigureBody* body = figure->body();
            if (body && body->skeletonMesh) {
                const auto& bones = body->skeletonMesh->bones();
                for (size_t b = 0; b < bones.size(); ++b) {
                    if (bones[b].name == rider.boneName) rider.bone = int(b);
                }
            }
            if (rider.bone < 0) {
                core::logError("lamps: no bone '%s' to hang a light from; it stays put",
                               rider.boneName);
            }
        }
        float at[3];
        if (rider.bone >= 0 && figure->pointOnBind(rider.bone, rider.model, at)) {
            renderer.setPointLightPosition(rider.light, at);
        }
    }
}

bool Lamps::nearestBonfire(const float from[3], float at[3], Heard heard,
                           void* context) const {
    float best = -1.0f;
    for (const Fire& fire : fires_) {
        if (!fire.crackles) continue;
        if (heard != nullptr && !heard(context, fire.at)) continue;
        const float dx = fire.at[0] - from[0], dz = fire.at[2] - from[2];
        const float d = dx * dx + dz * dz;
        if (best >= 0.0f && d >= best) continue;
        best = d;
        for (int j = 0; j < 3; ++j) at[j] = fire.at[j];
    }
    return best >= 0.0f;
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
        if (fire.breath) {
            // Out of the mouth, flat, the way the head looks.
            const float out[3] = {fire.facing[0], 0.0f, fire.facing[2]};
            const float speed = mix(kBreathSpeed[0], kBreathSpeed[1], unit());
            one.breath = true;
            // From between the jaws, a hand's width of jitter across, none along.
            const float across = (unit() - 0.5f) * 0.1f;
            one.position[0] = fire.mouth[0] - out[2] * across;
            one.position[1] = fire.mouth[1] + (unit() - 0.5f) * 0.08f;
            one.position[2] = fire.mouth[2] + out[0] * across;
            one.life *= 1.15f;
            one.velocity[0] = out[0] * speed + (unit() - 0.5f) * 0.35f;
            one.velocity[1] = (unit() - 0.3f) * 0.25f;
            one.velocity[2] = out[2] * speed + (unit() - 0.5f) * 0.35f;
        }
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
    // The vents: each rolled as MU rolls it, one reference frame in 64, and burning forty. Its
    // light is the Flame's (1, 0.4, 0) at the preamble's 0.7-1.0 (MoveHandlers.cpp:1815), eased
    // in over the first frames and out over the last so it does not snap; ours, the easing.
    ventsLit_.clear();
    const float frames = seconds * kVentFps;
    const float odds = 1.0f - std::pow(1.0f - 1.0f / kVentOdds, frames);
    for (Vent& vent : vents_) {
        if (vent.left > 0.0f) {
            vent.left = std::max(0.0f, vent.left - frames);
        } else if (unit() < odds) {
            vent.left = kVentFrames;
            const float dx = vent.at[0] - near[0], dz = vent.at[2] - near[2];
            if (dx * dx + dz * dz < kVentNear * kVentNear) {
                ventsLit_.push_back({{vent.at[0], vent.at[1], vent.at[2]}, unit() * 6.2831853f});
            }
        }
        float level = 0.0f;
        if (vent.left > 0.0f) {
            const float age = kVentFrames - vent.left;
            const float ease = std::min({1.0f, age / 3.0f, vent.left / 8.0f});
            level = ease * (0.7f + 0.3f * unit());
        }
        levels_[vent.light] = level;
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
        if (one.kind == kFlame && one.breath) {
            // A breath slows as it leaves the mouth and curls up a little at its end.
            const float keep = std::exp(-kBreathDrag * dt);
            one.velocity[0] *= keep;
            one.velocity[2] *= keep;
            one.velocity[1] += kBreathLift * dt;
        } else if (one.kind == kFlame) {
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
