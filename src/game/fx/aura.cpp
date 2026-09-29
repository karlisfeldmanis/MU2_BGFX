#include "game/fx/aura.h"

#include <algorithm>
#include <cmath>

#include "content/ground.h"
#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

// MU's reference clock; every rate below is per tick of it (Aura.cs Reference).
constexpr float kReference = 25.0f;
// MU's units in a tile, which every length here is divided by on the way to metres.
constexpr float kPerTile = 100.0f;

// --- the joints, ZzzEffectJoint.cpp BITMAP_FLARE subtype 0 with Scale 40 ------------------
// LifeTime 50, Velocity 40 (the ring's radius) and Scale 40 (the cross is -/+ half of it) are
// the level-up's own and now live in `kRising`, the recipe; what is left here is what both
// recipes share.
constexpr float kKey = 2.0f;             // PKKey: count = (Direction[1] + LifeTime) / 2
constexpr float kPhases = 250.0f;        // rand() % 500 - 250
constexpr float kDims = 10.0f;           // the last ten ticks...
constexpr float kDim = 1.0f / 1.3f;      // ...take the light by this each

// Invention: how squarely a strip must face the eye to be drawn at full strength. Below the
// first it is not drawn; between the two it eases in. See the header on the edge-on fade.
constexpr float kEdgeGone = 0.06f;
constexpr float kEdgeFull = 0.35f;

// Invention: the flares' strength in this engine's light. MU adds Flare at 1 into an 8-bit
// target, where thirty overlapping strips clip to the sheet's own gold; here they add into HDR
// that is exposed at 1.5 and tonemapped, and at 1 the core of the burst burned white. The user
// liked that first bright version best and wanted it a little cleaner: at this it is the same
// golden bloom at MU's full width, with the core kept gold and the loops just apart. Judged on
// fixed-step shots against 1.0, 0.8 and narrower, dimmer strips (2026-09-22).
constexpr float kStrength = 0.65f;

// --- the circle, ZzzEffect.cpp BITMAP_MAGIC + 1 subtype 0 ----------------------------------
constexpr float kRingTicks = 20.0f;      // LifeTime
constexpr float kRingDims = 5.0f;        // full until five ticks are left...
constexpr float kRingDim = 0.2f;         // ...then a fifth a tick
constexpr float kRingLifts = 5.0f;       // RenderTerrainAlphaBitmap's own lift, in units
// The circle follows the land in cells no wider than this, as the marker's parts do: MU paints
// it onto the terrain tiles it covers, and one flat quad would sink into a slope.
constexpr float kCell = 0.5f;

// --- a learned skill's smoke, this engine's own (see Aura::learn) ---------------------------
// All in MU's units and reference ticks, turned to metres by the tile at the throw.
constexpr int kBodyPuffs = 7;            // smoke02, mixed: the smoke itself
constexpr int kGlowPuffs = 4;            // smoke01, added: a faint light in it
constexpr float kSecondStab = 0.35f;     // this share of them wait for the second stab
constexpr float kDrag = 0.90f;           // velocity kept a tick
constexpr float kBuoyancy = 0.06f;       // units a tick gained upward, every tick
constexpr float kBloom = 0.7f;           // the size a puff gains, times its own...
constexpr float kBloomTicks = 5.0f;      // ...most of it over this many
constexpr float kSpread = 0.5f;          // and units a tick after that, forever
// The colour: a cool blue smoke, mixed and not added, so it reads as smoke that is really
// there -- the user's call after gold (2026-09-29). The tint runs a little past one on blue so
// the puff is pale and lit rather than a grey-blue stain. The first try, a cloud of 22 gold
// puffs at 0.62, hid him and was too much, and ten at 0.30 was still a little much: he must
// stay readable through it. The added wisps are a faint cold light in it, kept low so the
// whole never reads as a glow.
constexpr float kSmokeBlue[3] = {0.62f, 0.82f, 1.25f};
constexpr float kBodyAlpha = 0.22f;
constexpr float kGlow[3] = {0.05f, 0.14f, 0.36f};
// The circle: MU's own blue, at half the level-up's width and kept low on lit ground.
constexpr float kLearnRing[3] = {0.30f, 0.48f, 0.85f};
constexpr float kLearnRingOpens = 0.075f;

float smoothstep(float edge0, float edge1, float x) {
    const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

float Aura::unit() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return float(seed_ & 0xFFFFFF) / float(0x1000000);
}

bool Aura::open(const std::string& assetDir, content::Textures& textures) {
    const std::string dir = assetDir + "/effects/levelup/";
    const auto take = [&](const char* name) -> bgfx::TextureHandle {
        const std::string path = dir + name;
        if (!core::fileExists(path)) {
            core::logError("aura: no %s (tools/sync.sh)", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    flare_ = take("flare.png");
    ground_ = take("magic_ground.png");
    // The fire's smoke sheets, which the skill's puff is made of.
    smoke_ = take("../fire/smoke02.png");
    wisp_ = take("../fire/smoke01.png");
    core::logf("aura: flare %s, circle %s", bgfx::isValid(flare_) ? "in hand" : "MISSING",
               bgfx::isValid(ground_) ? "in hand" : "MISSING");
    return bgfx::isValid(flare_);
}

void Aura::shutdown() {
    // The sheets belong to Textures, which destroys them.
    for (Burst& b : bursts_) b.living = false;
    for (Puff& p : puffs_) p.living = false;
}

int Aura::live() const {
    int n = 0;
    for (const Burst& b : bursts_) n += b.living ? 1 : 0;
    return n;
}

void Aura::at(const Burst& b, const Joint& j, float back, float out[3]) {
    // MoveJoint, in closed form: at LifeTime L the joint is at
    // TargetPosition + (cos(count), -sin(count)) * Velocity with count = (Direction[1] + L) / 2,
    // and has climbed Direction[2] for every tick since it was made. `back` ticks ago LifeTime
    // was that much higher and the climb that much shorter. MU's Y is this world's -z, so its
    // -sin arrives as +sin and the ring turns MU's way.
    const float count = (j.phase + (b.r.ticks - b.age) + back) / kKey;
    out[0] = std::cos(count) * b.r.orbit * b.per;
    out[1] = j.height + j.rise * std::max(0.0f, b.age - back);
    out[2] = std::sin(count) * b.r.orbit * b.per;
}

Aura::Burst* Aura::throwOne(const Recipe& recipe, const float feet[3], float yaw,
                            float metresPerTile) {
    if (!bgfx::isValid(flare_) || metresPerTile <= 0.0001f) return nullptr;
    Burst* slot = nullptr;
    for (Burst& b : bursts_) {
        if (!b.living) {
            slot = &b;
            break;
        }
    }
    if (slot == nullptr) return nullptr;  // a third level in two seconds is not drawn
    Burst& b = *slot;
    b.living = true;
    b.r = recipe;
    for (int k = 0; k < 3; ++k) b.feet[k] = feet[k];
    // Forward is (sin yaw, cos yaw), so the joint's local X lies at (cos yaw, -sin yaw).
    b.across[0] = std::cos(yaw);
    b.across[1] = 0.0f;
    b.across[2] = -std::sin(yaw);
    b.yaw = yaw;
    b.per = metresPerTile / kPerTile;
    b.age = 0.0f;
    for (int k = 0; k < b.r.joints; ++k) {
        Joint& j = b.joints[k];
        // Drawn for the level-up, where where a flare starts on its ring is not a fact; laid
        // evenly for the guard, where five ribbons standing at five even angles IS the shape.
        // `count` divides the phase by kKey, so an even step round the ring is that much wider.
        j.phase = b.r.spread ? float(k) * 6.28318530718f * kKey / float(b.r.joints)
                             : unit() * 2.0f * kPhases - kPhases;
        j.rise = (b.r.slowRise + (b.r.spread ? 0.0f : unit() * (b.r.fastRise - b.r.slowRise))) *
                 b.per;
        // A band's ribbons stand at their own heights up the body instead of climbing: five
        // rings from his feet to over his head, which is what a guard looks like.
        j.height = b.r.band > 0.0f && b.r.joints > 1
                       ? float(k) / float(b.r.joints - 1) * b.r.band * b.per
                       : 0.0f;
    }
    return &b;
}

void Aura::rise(const float feet[3], float yaw, float metresPerTile) {
    Recipe recipe = kRising;
    recipe.circle = circle_;  // where the switch has always been
    throwOne(recipe, feet, yaw, metresPerTile);
}

void Aura::learn(const float feet[3], float metresPerTile) {
    if (!bgfx::isValid(smoke_) || metresPerTile <= 0.0001f) return;
    const float per = metresPerTile / kPerTile;
    // The circle alone, as a burst with no flares; it lies square to the world, not to him.
    Recipe circle{};
    circle.joints = 0;
    circle.circle = true;
    circle.ticks = 0.0f;
    circle.ringOpens = kLearnRingOpens;
    for (int k = 0; k < 3; ++k) circle.ring[k] = kLearnRing[k];
    throwOne(circle, feet, 0.0f, metresPerTile);
    const int count = kBodyPuffs + (bgfx::isValid(wisp_) ? kGlowPuffs : 0);
    int made = 0;
    for (Puff& p : puffs_) {
        if (made == count) break;
        if (p.living) continue;
        const bool glow = made >= kBodyPuffs;
        ++made;
        p = Puff{};
        p.living = true;
        p.glow = glow;
        p.per = per;
        // Born on the first stab or the second, a tick either side of it.
        const bool second = unit() < kSecondStab;
        p.age = -(second ? 3.0f + unit() * 2.0f : unit() * 2.0f);
        // Inside him: a hand from his middle, the smoke from his knees to his crown and the
        // light kept to his chest.
        const float a = unit() * 6.28318530718f;
        const float from = (8.0f + unit() * 16.0f) * per;
        const float height = (glow ? 60.0f + unit() * 70.0f : 30.0f + unit() * 120.0f) * per;
        p.at[0] = feet[0] + std::cos(a) * from;
        p.at[1] = feet[1] + height;
        p.at[2] = feet[2] + std::sin(a) * from;
        // A little out and up; with the drag it travels ten times this before it hangs, and
        // the buoyancy then carries it on up past his shoulders.
        const float out = (1.2f + unit() * 1.3f) * per;
        p.velocity[0] = std::cos(a) * out;
        p.velocity[1] = (0.8f + unit() * 1.4f) * per;
        p.velocity[2] = std::sin(a) * out;
        p.size = (glow ? 38.0f + unit() * 22.0f : 34.0f + unit() * 22.0f) * per;
        p.holds = 17.0f + unit() * 4.0f;
        p.life = p.holds + 8.0f + unit() * 3.0f;
        p.spin = unit() * 6.28318530718f;
        p.turns = (unit() - 0.5f) * 0.08f;
    }
}

void Aura::guard(const float feet[3], float yaw, float metresPerTile, float seconds) {
    // One at a time: a guard thrown again replaces the one standing, which is what the realm
    // does with the boon (`throwSkill` overwrites it) and what MU does with its own
    // `g_isCharacterBuff` test -- the circle flashes again and no second set of ribbons is made.
    release();
    Recipe recipe = kGuarding;
    recipe.ticks = std::max(1.0f, seconds * kReference);
    // A hundred units over the whole of it, however long that is: the climb is the boon's.
    recipe.slowRise = recipe.fastRise = 100.0f / recipe.ticks;
    Burst* b = throwOne(recipe, feet, yaw, metresPerTile);
    if (b != nullptr) guarding_ = int(b - bursts_);
}

void Aura::follow(const float feet[3]) {
    if (guarding_ < 0 || !bursts_[guarding_].living) return;
    for (int k = 0; k < 3; ++k) bursts_[guarding_].feet[k] = feet[k];
}

void Aura::release() {
    if (guarding_ >= 0) bursts_[guarding_].living = false;
    guarding_ = -1;
}

void Aura::update(float seconds) {
    // No cap on a long frame, where MU2 had one: nothing is laid per step any more, so a hitch
    // moves the burst on and cannot leave a streak behind it.
    const float ticks = seconds * kReference;
    for (Burst& b : bursts_) {
        if (!b.living) continue;
        b.age += ticks;
        const float ends = b.r.circle ? std::max(b.r.ticks, kRingTicks) : b.r.ticks;
        if (b.age > ends) {
            b.living = false;
            if (guarding_ >= 0 && &b == &bursts_[guarding_]) guarding_ = -1;
        }
    }
    for (Puff& p : puffs_) {
        if (!p.living) continue;
        const float was = p.age;
        p.age += ticks;
        if (p.age >= p.life) {
            p.living = false;
            continue;
        }
        if (p.age <= 0.0f) continue;
        // Moved only for the part of the frame it has been born.
        const float moving = p.age - std::max(0.0f, was);
        p.velocity[1] += kBuoyancy * p.per * moving;
        const float drag = std::pow(kDrag, moving);
        for (int d = 0; d < 3; ++d) {
            p.at[d] += p.velocity[d] * moving;
            p.velocity[d] *= drag;
        }
        p.spin += p.turns * moving;
    }
}

void Aura::gatherPuffs(gfx::Effects& effects) const {
    for (const Puff& p : puffs_) {
        if (!p.living || p.age <= 0.0f) continue;
        // Up in a tick and a half, held, and out by its life's end, eased both ways.
        const float in = smoothstep(0.0f, 1.5f, p.age);
        const float out = 1.0f - smoothstep(p.holds, p.life, p.age);
        const float fade = in * out;
        if (fade <= 0.0f) continue;
        const float bloom = 1.0f + kBloom * (1.0f - std::exp(-p.age / kBloomTicks));
        const float across = p.size * bloom + kSpread * p.per * p.age;
        gfx::Sprite sprite;
        for (int d = 0; d < 3; ++d) sprite.position[d] = p.at[d];
        sprite.halfWidth = sprite.halfHeight = across * 0.5f;
        sprite.spin = p.spin;
        if (p.glow) {
            for (int d = 0; d < 3; ++d) sprite.colour[d] = kGlow[d] * fade;
            sprite.colour[3] = 1.0f;
            sprite.sheet = wisp_;
            sprite.blend = gfx::Blend::Additive;
        } else {
            for (int d = 0; d < 3; ++d) sprite.colour[d] = kSmokeBlue[d];
            sprite.colour[3] = kBodyAlpha * fade;
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Smoke;
        }
        if (!effects.add(sprite)) return;
    }
}

void Aura::gather(gfx::Effects& effects, const content::Ground& ground,
                  const float eye[3]) const {
    gatherPuffs(effects);
    for (const Burst& b : bursts_) {
        if (!b.living) continue;
        if (b.r.circle) gatherCircle(effects, ground, b);
        if (b.age > b.r.ticks) continue;

        // Light *= 1/1.3 a tick while under ten ticks of LifeTime are left, one light for all
        // fifteen: the same curve, read at the burst's age rather than multiplied down.
        const float light =
            kStrength * std::pow(kDim, std::max(0.0f, b.age - (b.r.ticks - kDims)));
        // How long the trail is, in ticks: it grows from nothing to its own length and holds.
        const float length = std::min(b.age, b.r.trail);
        if (length <= 0.0f) continue;
        const int steps = std::max(1, int(std::ceil(length * float(kSamples))));
        const float half = b.r.wide * 0.5f * b.per;
        const float up[3] = {0.0f, 1.0f, 0.0f};

        gfx::Sprite sprite;
        sprite.placed = true;
        sprite.sheet = flare_;
        sprite.blend = gfx::Blend::Additive;
        sprite.colour[3] = 1.0f;

        // One segment of one strip, between two points of the trail `axis` wide either side.
        // U walks the sheet from its dark rim at the head through its bright middle to the dark
        // rim at the tail. V runs across the strip, flipped on the upright one as MU's
        // RENDER_FACE_ONE has it.
        //
        // **Invention, while a trail grows only.** MU's U is (NumTails - j) / (MaxTails - 1):
        // the whole sheet over the full nineteen ticks, so a trail younger than that shows only
        // its tail end of the sheet, and the bright middle stands at ONE height near the feet
        // for about 0.4 s -- the effect getting stuck, as the user saw it (2026-09-22). Here
        // the whole sheet is spread over the trail's length so far, so the bright middle stays
        // mid-trail and climbs from the first frame; from nineteen ticks on the two are the same
        // number and the look is MU's. (A comet, bright at the head, also took the hold out and
        // changed the look; the user kept MU's.)
        const auto segment = [&](const float* p0, const float* p1, const float* axis, float u0,
                                 float u1, bool flipped) {
            // How squarely the strip faces the eye: the component along the view of its
            // normal, tangent x axis, taken over the tangent's length. That is small both when
            // the strip is seen edge-on and when the path runs along the strip's own width and
            // the strip folds up into a line -- the two slivers MU's fixed cross makes.
            const float t[3] = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};
            const float n[3] = {t[1] * axis[2] - t[2] * axis[1], t[2] * axis[0] - t[0] * axis[2],
                                t[0] * axis[1] - t[1] * axis[0]};
            float v[3];
            for (int d = 0; d < 3; ++d) v[d] = eye[d] - (b.feet[d] + (p0[d] + p1[d]) * 0.5f);
            const float tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
            const float vl = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (tl <= 1e-6f || vl <= 1e-6f) return;
            const float facing = std::fabs(n[0] * v[0] + n[1] * v[1] + n[2] * v[2]) / (tl * vl);
            const float fade = smoothstep(kEdgeGone, kEdgeFull, facing);
            if (fade <= 0.0f) return;
            for (int d = 0; d < 3; ++d) sprite.colour[d] = b.r.light[d] * light * fade;

            const float top = flipped ? 1.0f : 0.0f, bottom = 1.0f - top;
            const float side[4] = {-half, half, half, -half};
            const float* from[4] = {p0, p0, p1, p1};
            const float uv[4][2] = {{u0, top}, {u0, bottom}, {u1, bottom}, {u1, top}};
            for (int k = 0; k < 4; ++k) {
                for (int d = 0; d < 3; ++d) {
                    sprite.corner[k][d] = b.feet[d] + from[k][d] + axis[d] * side[k];
                }
                sprite.cornerUv[k][0] = uv[k][0];
                sprite.cornerUv[k][1] = uv[k][1];
            }
            for (int d = 0; d < 3; ++d) {
                sprite.position[d] = (sprite.corner[0][d] + sprite.corner[2][d]) * 0.5f;
            }
            effects.add(sprite);
        };

        for (int k = 0; k < b.r.joints; ++k) {
            const Joint& j = b.joints[k];
            float p0[3], p1[3];
            at(b, j, 0.0f, p0);
            float u0 = 1.0f;
            for (int s = 1; s <= steps; ++s) {
                const float back = length * float(s) / float(steps);
                at(b, j, back, p1);
                const float u1 = (length - back) / length;
                segment(p0, p1, up, u0, u1, true);        // the upright strip
                segment(p0, p1, b.across, u0, u1, false); // and the flat one across it
                std::copy(p1, p1 + 3, p0);
                u0 = u1;
            }
        }
    }
}

void Aura::gatherCircle(gfx::Effects& effects, const content::Ground& ground,
                        const Burst& b) const {
    const float ringLeft = kRingTicks - b.age;
    if (!bgfx::isValid(ground_) || ringLeft <= 0.0f) return;
    // Opens from nothing to three tiles across, and is turned by the hero's facing and left
    // there: only subtype 7 spins.
    const float across = b.age * b.r.ringOpens * kPerTile * b.per;
    if (across <= 0.0f) return;
    const float lit =
        ringLeft < kRingDims ? std::max(0.0f, 1.0f - (kRingDims - ringLeft) * kRingDim) : 1.0f;
    const int cells = std::clamp(int(std::ceil(across / kCell)), 1, 8);
    const float c = std::cos(-b.yaw), s = std::sin(-b.yaw);
    const float lift = kRingLifts * b.per;
    const auto place = [&](float u, float v, float* out) {
        const float x = b.feet[0] + (u * c - v * s) * across;
        const float z = b.feet[2] + (u * s + v * c) * across;
        out[0] = x;
        out[1] = ground.heightAt(x, z) + lift;
        out[2] = z;
    };
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = ground_;
    sprite.blend = gfx::Blend::Additive;
    for (int k = 0; k < 3; ++k) sprite.colour[k] = b.r.ring[k] * lit;
    sprite.colour[3] = 1.0f;
    const float inv = 1.0f / float(cells);
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const float u0 = float(i) * inv - 0.5f, u1 = u0 + inv;
            const float v0 = float(j) * inv - 0.5f, v1 = v0 + inv;
            const float uv[4][2] = {{u0, v0}, {u1, v0}, {u1, v1}, {u0, v1}};
            for (int k = 0; k < 4; ++k) {
                place(uv[k][0], uv[k][1], sprite.corner[k]);
                sprite.cornerUv[k][0] = uv[k][0] + 0.5f;
                sprite.cornerUv[k][1] = 0.5f - uv[k][1];
            }
            place((u0 + u1) * 0.5f, (v0 + v1) * 0.5f, sprite.position);
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
