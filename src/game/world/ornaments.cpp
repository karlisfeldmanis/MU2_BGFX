#include "game/world/ornaments.h"

#include <algorithm>
#include <cmath>

#include "content/showing.h"
#include "content/placement.h"
#include "core/log.h"
#include "game/world/sway.h"
#include "game/world/town.h"

namespace mu::game {

namespace {

// MU's 25, which is what a reference frame means.
constexpr float kFramesPerSecond = 25.0f;

// ---- the spray: every number is MU2's Spray.cs, which read each one out of MuMain --------
//
// Where the landing puff is born and how its scatter lies, in the model's own metres at its
// bind pose: bone 4 (Box03), eighty units down ITS Y -- up the fall, (0, 0.87, -0.5) in our
// axes -- which is 0.61 m over the pool and 1.19 m out from the statue. The scatter is
// `rand() % 32 - 16` in the bone's X and Z: X (0, -0.5, -0.87) and Z (1, 0, 0), square across
// the stream where it lands.
//
// The mouth's puff MU also throws, at bone 1 (Box04), twenty units down its Y, is left out as
// MU2 left it out ("reads as smoke coming off the statue's beak"); to put it back it is bone 1,
// origin (0.0591, 2.7646, -0.0295), scatter X (0, -1, 0) and Z (1, 0, 0).
constexpr int kLandingBone = 4;
constexpr float kLanding[3] = {0.0599f, 0.6111f, 1.1932f};
constexpr float kLandingAcross[2][3] = {{0.0f, -0.5f, -0.866f}, {1.0f, 0.0f, 0.0f}};
constexpr float kScatter = 0.16f;  // half of MU's 32 units

// One puff: BITMAP_SMOKE at subtype 0, smoke01.jpg. LifeTime 16 reference frames; born at
// Scale 0.48 to 0.80 and growing 0.05 a frame; `Gravity += 0.2` a frame added to its height,
// so an acceleration of 0.2 x 25 x 25 = 125 units/s/s, 1.25 m/s/s up; no velocity; turned once
// at birth. `Luminosity = LifeTime / 8` in all three channels, so full for the first half and
// a straight line to nothing over the second. Added: smoke01 is a JPEG, three components,
// and RenderParticles blends those GL_ONE, GL_ONE.
constexpr float kPuffLife = 16.0f / kFramesPerSecond;
constexpr float kPuffGrowth = 0.05f * kFramesPerSecond;  // Scale a second
constexpr float kPuffLift = 1.25f;
constexpr float kSheetMetres = 64.0f / 100.0f;  // a 64-texel sheet at Scale 1, in metres
constexpr size_t kMostPuffs = 64;

// ---- the mill's fall: ours ------------------------------------------------------------
//
// House05's ston02 sheet runs level along its flume and turns down off the end: its lowest
// row is x 1.70 to 2.31, 1.40 m under the model's origin, at z 2.38 to 2.53 (House05.obj,
// already on our axes, in metres). The landing is the middle of that row, and the scatter is
// across the fall's 0.6 m width and the fountain's 0.16 m through it. It is only raised to the
// land, never lowered: the sheet reaches into the river, and the puff wants the surface -- and
// then kMillRise over it, as born on the water it sat under the ferns at the bank.
// The fall is twice the fountain's width, so a flip throws a puff on each half of it.
constexpr float kMillLanding[3] = {2.004f, -1.40f, 2.45f};
constexpr float kMillRise = 0.2f;
constexpr float kMillAcross[2][3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};
constexpr float kMillReach[2] = {0.30f, kScatter};

// ---- the lanterns ---------------------------------------------------------------------
//
// MODEL_MERCHANT_ANIMAL01's two BITMAP_LIGHT sprites, at bones 48 (Box04) and 57 (Box10) --
// the two lamps 3.3 m up either side of the animal's load. MU transforms `Position` through
// each bone, and in RenderObjectVisual `Position` is never set before this case: whatever the
// stack held. Read here as the bone's own origin, which is the only offset the line can
// have meant. Ours, and marked as ours.
// MU's own bones for the two lights were 48 and 57, the markers named below.
// Where the two lamps actually hang, which is not where MU's bones are: 48 and 57 are empty
// markers at the pole tips, and the lantern boxes swing about 0.75 m out from them on bones
// 47 (Box05) and 56 (Box11). A glow at the tip floated in the air beside the lamp. So each is
// put on its lantern's own middle -- the bind-pose centroid of the 66 vertices each of those
// bones carries, read out of MerchantAnimal01.glb -- and rides the lantern bone. Ours.
constexpr int kLanternCarriers[2] = {47, 56};
constexpr float kLanternMiddles[2][3] = {{1.0642f, 3.0403f, 1.0146f},
                                         {-0.8804f, 2.9950f, -1.0739f}};
constexpr float kLanternColour[3] = {0.6f, 0.3f, 0.1f};
constexpr float kLanternGlowShare = 0.25f;
// `Luminosity = (rand() % 30 + 70) * 0.01f`, rolled every frame MU draws. MU drew at its
// reference 25; re-rolled per OUR frame it is a strobe at the monitor's rate, so it is
// re-rolled 25 times a second. The rate is MU's frame, not an invention -- but it is a reading.
constexpr float kLanternHz = kFramesPerSecond;

// ---- Noria's glows: RenderObjectVisual, case WD_3NORIA (ZzzObject.cpp:2864) -------------
//
// Every one is `b->TransformPosition(BoneTransform[n], p, Position)` with p left at (0, 0, 0):
// the bone's own origin, which is (0, 0, 0) in the bone's frame. Type N is Object{N+1}. The
// colour is `Luminosity * (0.4, 0.7, 1.0)` with RenderObjectVisual's local Luminosity, the
// lanterns' roll; the machine's five are `(1, 1, 1)` and do not breathe; its star is
// `Luminosity * (0.4, 0.8, 1.0)`. Drawn at MU's full scale, unlike the merchant animal's --
// these are small, and a flower head's glow at a third of a metre is what MU painted.
struct NoriaGlow {
    const char* model;
    int bones[5];
    int count;
    float scale;
    float colour[3];
    bool steady;
    int sheet;
};
constexpr NoriaGlow kNoriaGlows[] = {
    {"Object02", {2, 4, 6}, 3, 0.5f, {0.4f, 0.7f, 1.0f}, false, 0},        // case 1
    {"Object10", {1}, 1, 1.5f, {0.4f, 0.7f, 1.0f}, false, 0},              // case 9
    {"Object18", {4, 7, 10, 13}, 4, 1.0f, {0.4f, 0.7f, 1.0f}, false, 0},   // case 17
    {"Object36", {3}, 1, 1.5f, {0.4f, 0.7f, 1.0f}, false, 0},              // case 35
    {"Object40", {61, 62, 63, 64, 65}, 5, 1.0f, {1.0f, 1.0f, 1.0f}, true, 0},  // case 39
};
// ---- The Lost Tower's floor machines: RenderObjectVisual, case WD_4LOSTTOWER -----------
//
// Types 19 and 20 (Object20, Object21; ZzzObject.cpp:2931-2956): at bones 15, 19 and 21, each
// at the bone's own origin, two sprites of one sheet turning opposite ways at `WorldTime * 0.1`
// degrees, 0.3, 0.3 and 1.5 of the sheet, in `Luminosity` times the colour. Object20's are
// BITMAP_MAGIC+1 (Magic_Ground2) in (1, 0.2, 0), Object21's BITMAP_LIGHTNING+1 in (0.4, 0.8, 1).
// Bones 15 and 19 hang under the two orbs the clip spins, so those stars swing round with them.
struct TowerGlow {
    const char* model;
    float colour[3];
    int sheet;  // 1 lightning2, 2 magic_ground
};
constexpr TowerGlow kTowerGlows[] = {
    {"Object20", {1.0f, 0.2f, 0.0f}, 2},
    {"Object21", {0.4f, 0.8f, 1.0f}, 1},
};
constexpr int kTowerBones[3] = {15, 19, 21};
constexpr float kTowerScales[3] = {0.3f, 0.3f, 1.5f};

// case 39's star, `WorldTime * 0.1` degrees with WorldTime in milliseconds.
constexpr int kStarBone = 57;
constexpr float kStarColour[3] = {0.4f, 0.8f, 1.0f};
constexpr float kStarDegreesPerSecond = 100.0f;
// Devias's Lost Tower beacon, RenderObjectVisual case 100 (ZzzObject.cpp:2821-2828): two
// BITMAP_LIGHTNING+1 sprites at scale 2.5, one at +Rotation and one at -Rotation on the star's
// own 100 degrees a second, 150 units over bone 0, in Luminosity white. The placement is
// devias.json's one type 100, MU (328.9, 24764.4, 255.2) -- tile 3.3, 247.6 at the Lost Tower
// gate -- and its bone 0 is taken as its origin.
constexpr float kBeaconAt[3] = {3.289f, 2.552f + 1.5f, -247.644f};
constexpr float kBeaconScale = 2.5f;
// Ours, not MU's (the user, 2026-09-30: "make beacon more blurry, with some smoke effect"): the
// two stars a little wider and softer, a broad flare01 bloom behind them, and a slow column of
// pale mist curling up through the light. Wisps are born a few a second round the beacon, rise
// and drift, swell and fade in and out again, added as vapour lit by the beacon.
constexpr float kBeaconSoften = 1.5f;       // the stars' size over MU's 2.5
constexpr float kBeaconStarLevel = 0.5f;    // and their light, of the luminosity roll
constexpr float kBeaconBloom = 3.2f;        // the flare's half width, metres
constexpr float kBeaconBloomLevel = 0.7f;
constexpr float kBeaconBloomColour[3] = {0.75f, 0.85f, 1.0f};
constexpr float kMistEvery = 0.3f;          // seconds between wisps
constexpr float kMistLife = 4.5f;           // seconds
constexpr float kMistRise = 0.3f;           // metres a second
constexpr float kMistDrift = 0.15f;         // metres a second, sideways
constexpr float kMistBorn = 1.4f, kMistGrown = 3.6f;  // the wisp's width, metres
constexpr float kMistPeak = 0.35f;         // its most light, mid-life
constexpr float kMistColour[3] = {0.82f, 0.88f, 0.96f};
constexpr size_t kMostMist = 24;
// And its throwers. Bones 61 to 65 roll rand_fps_check(32) each and throw two BITMAP_SHINY,
// subtypes 0 and 1, in white; bone 58 rolls rand_fps_check(8) and throws a burst of eight
// spark pairs. A glint lives 18 frames at Scale `sin(LifeTime * 10 deg)` -- nothing, up to one
// at the ninth frame, nothing again -- and subtype 1 is that times 0.75 (MovePartices sets the
// sin afresh each frame, then scales it once) and turns 12 degrees a frame against the clock.
// RenderParticles sizes a particle by its bitmap: Shiny01 is 16 texels, so Scale 1 is 0.16 m.
constexpr float kShinyMetres = 16.0f / 100.0f;
constexpr float kGlintSmall = 0.75f;
constexpr int kGlintBones[5] = {61, 62, 63, 64, 65};
constexpr int kGlintEvery = 32;
constexpr int kSparkBone = 58;
constexpr int kSparkEvery = 8;
constexpr float kGlintLife = 18.0f;
constexpr size_t kMostGlints = 32;

// A row-vector point or direction through a 4x4, as core::mulMatrix composes them.
void through(const float* m, const float* v, float w, float* out) {
    for (int j = 0; j < 3; ++j) {
        out[j] = v[0] * m[0 * 4 + j] + v[1] * m[1 * 4 + j] + v[2] * m[2 * 4 + j] +
                 w * m[3 * 4 + j];
    }
}

}  // namespace

uint32_t Ornaments::next() {
    seed_ ^= seed_ << 13;
    seed_ ^= seed_ >> 17;
    seed_ ^= seed_ << 5;
    return seed_;
}

float Ornaments::unit() { return float(next() & 0xFFFFFF) / float(0x1000000); }

bool Ornaments::open(const std::string& assetDir, const std::string& world, const Town& town,
                     const content::Ground& ground, content::Textures& textures) {
    const auto& models = town.cooked().models;
    // The bind pose's model-space point, carried into the bone's own frame through its
    // inverse bind; Figure::pointOn carries it back out through the posed bone.
    const auto anchor = [&](uint32_t townIndex, const content::Mesh* mesh, int bone,
                            const float point[3], const float (*across)[3]) {
        Anchor out;
        out.townIndex = townIndex;
        if (!mesh || bone < 0 || size_t(bone) >= mesh->bones().size()) return out;
        const float* inverse = mesh->bones()[size_t(bone)].inverseBind;
        out.bone = bone;
        through(inverse, point, 1.0f, out.point);
        through(inverse, across[0], 0.0f, out.across[0]);
        through(inverse, across[1], 0.0f, out.across[1]);
        return out;
    };

    for (uint32_t i = 0; i < town.cooked().instances.size(); ++i) {
        const uint32_t model = town.cooked().instances[i].model;
        if (model >= models.size()) continue;
        const std::string& name = models[model].name;
        const content::Mesh* mesh = town.meshAt(model);
        if (name == "Waterspout01") {
            const content::TownInstance& at = town.cooked().instances[i];
            Place place;
            for (int k = 0; k < 3; ++k) place.at[k] = at.position[k];
            fountains_.push_back(place);
            Spout spout;
            spout.anchor = anchor(i, mesh, kLandingBone, kLanding, kLandingAcross);
            if (spout.anchor.bone >= 0) spouts_.push_back(spout);
        } else if (name == "House05") {
            const content::TownInstance& at = town.cooked().instances[i];
            float m[16];
            content::placementTransform(at.pitch, at.yaw, at.roll, at.scale, at.position, m);
            Fall fall;
            through(m, kMillLanding, 1.0f, fall.point);
            fall.point[1] =
                std::max(fall.point[1], ground.heightAt(fall.point[0], fall.point[2])) + kMillRise;
            for (int a = 0; a < 2; ++a) {
                through(m, kMillAcross[a], 0.0f, fall.across[a]);
                const float length = std::sqrt(fall.across[a][0] * fall.across[a][0] +
                                               fall.across[a][1] * fall.across[a][1] +
                                               fall.across[a][2] * fall.across[a][2]);
                for (int k = 0; k < 3; ++k) fall.across[a][k] /= std::max(length, 1e-6f);
                fall.reach[a] = kMillReach[a];
            }
            falls_.push_back(fall);
        } else if (name == "MerchantAnimal01") {
            static const float kNoAcross[2][3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
            for (int k = 0; k < 2; ++k) {
                Lantern lantern;
                lantern.anchor = anchor(i, mesh, kLanternCarriers[k], kLanternMiddles[k], kNoAcross);
                // CreateSprite's `Luminosity * 5`, a quarter of it: see gather().
                lantern.scale = 5.0f * kLanternGlowShare;
                lantern.swells = true;
                for (int a = 0; a < 3; ++a) lantern.colour[a] = kLanternColour[a];
                if (lantern.anchor.bone >= 0) lanterns_.push_back(lantern);
            }
        } else if (world == "losttower") {
            static const float kOrigin[3] = {0.0f, 0.0f, 0.0f};
            static const float kNoAcross[2][3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
            for (const TowerGlow& glow : kTowerGlows) {
                if (name != glow.model) continue;
                for (int b = 0; b < 3; ++b) {
                    for (float way : {1.0f, -1.0f}) {
                        Lantern star;
                        star.anchor = anchor(i, mesh, kTowerBones[b], kOrigin, kNoAcross);
                        for (int a = 0; a < 3; ++a) star.anchor.point[a] = 0.0f;
                        star.scale = kTowerScales[b];
                        for (int a = 0; a < 3; ++a) star.colour[a] = glow.colour[a];
                        star.sheet = glow.sheet;
                        star.spin = way * kStarDegreesPerSecond;
                        if (star.anchor.bone >= 0) lanterns_.push_back(star);
                    }
                }
            }
        } else if (world == "noria") {
            static const float kOrigin[3] = {0.0f, 0.0f, 0.0f};
            static const float kNoAcross[2][3] = {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
            for (const NoriaGlow& glow : kNoriaGlows) {
                if (name != glow.model) continue;
                for (int b = 0; b < glow.count; ++b) {
                    Lantern lantern;
                    // The origin is the bone's own, so it goes in unchanged: anchor() would
                    // carry a MODEL-space point, and this is already in the bone's frame.
                    lantern.anchor = anchor(i, mesh, glow.bones[b], kOrigin, kNoAcross);
                    for (int a = 0; a < 3; ++a) lantern.anchor.point[a] = 0.0f;
                    lantern.scale = glow.scale;
                    for (int a = 0; a < 3; ++a) lantern.colour[a] = glow.colour[a];
                    lantern.steady = glow.steady;
                    if (lantern.anchor.bone >= 0) lanterns_.push_back(lantern);
                }
                if (name == "Object40") {
                    for (int bone : kGlintBones) {
                        Thrower glinter;
                        glinter.anchor = anchor(i, mesh, bone, kOrigin, kNoAcross);
                        for (int a = 0; a < 3; ++a) glinter.anchor.point[a] = 0.0f;
                        glinter.every = kGlintEvery;
                        if (glinter.anchor.bone >= 0) throwers_.push_back(glinter);
                    }
                    Thrower sparker;
                    sparker.anchor = anchor(i, mesh, kSparkBone, kOrigin, kNoAcross);
                    for (int a = 0; a < 3; ++a) sparker.anchor.point[a] = 0.0f;
                    sparker.every = kSparkEvery;
                    sparker.sparks = true;
                    if (sparker.anchor.bone >= 0) throwers_.push_back(sparker);
                    for (float way : {1.0f, -1.0f}) {
                        Lantern star;
                        star.anchor = anchor(i, mesh, kStarBone, kOrigin, kNoAcross);
                        for (int a = 0; a < 3; ++a) star.anchor.point[a] = 0.0f;
                        for (int a = 0; a < 3; ++a) star.colour[a] = kStarColour[a];
                        star.sheet = 1;
                        star.spin = way * kStarDegreesPerSecond;
                        if (star.anchor.bone >= 0) lanterns_.push_back(star);
                    }
                }
            }
        }
    }
    beacon_ = world == "devias";
    puffs_.reserve(kMostPuffs);
    glints_.reserve(kMostGlints);

    content::Showing table;
    std::string error;
    if (content::loadShowing(assetDir + "/cooked/showing/showing.mus", table, error)) {
        const auto take = [&](const char* name) -> bgfx::TextureHandle {
            const content::EffectSheet* sheet = table.effect(name);
            if (sheet == nullptr) return BGFX_INVALID_HANDLE;
            return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
        };
        smoke_ = take("smoke01");  // Effect/smoke01, MU's BITMAP_SMOKE
        light_ = take("light");    // Effect/flare01, MU's BITMAP_LIGHT
        lightning_ = take("lightning_2");  // Effect/lightning2, MU's BITMAP_LIGHTNING+1
        magic_ = take("magic_ground");     // Effect/Magic_Ground2, MU's BITMAP_MAGIC+1
        shiny_ = take("shiny");    // Effect/Shiny01, MU's BITMAP_SHINY
    }
    if (!spouts_.empty() || !lanterns_.empty() || !falls_.empty()) {
        core::logf("ornaments: %zu fountain spray, %zu mill falls, %zu lanterns; sheets: "
                   "smoke01 %s, light %s",
                   spouts_.size(), falls_.size(), lanterns_.size(), bgfx::isValid(smoke_) ? "yes" : "NO",
                   bgfx::isValid(light_) ? "yes" : "NO");
    }
    return true;
}

bool Ornaments::nearestFountain(const float from[3], float at[3]) const {
    float best = -1.0f;
    for (const Place& one : fountains_) {
        const float dx = one.at[0] - from[0], dz = one.at[2] - from[2];
        const float d = dx * dx + dz * dz;
        if (best >= 0.0f && d >= best) continue;
        best = d;
        for (int k = 0; k < 3; ++k) at[k] = one.at[k];
    }
    return best >= 0.0f;
}

void Ornaments::shutdown() {
    fountains_.clear();
    spouts_.clear();
    lanterns_.clear();
    beacon_ = beaconSeen_ = false;
    falls_.clear();
    puffs_.clear();
    mist_.clear();
    glints_.clear();
    throwers_.clear();
    strikeCount_ = 0;
    smoke_ = light_ = lightning_ = shiny_ = magic_ = BGFX_INVALID_HANDLE;
}

void Ornaments::update(float seconds, const Sway& sway) {
    // A long hitch would throw a second's puffs in one place at once. Capped, as the spray
    // does not need to be right about a frame nobody saw.
    const float dt = std::min(seconds, 0.1f);

    for (Puff& puff : puffs_) {
        // The lift is MU's accumulated gravity, which is an acceleration: the height gained
        // over this step is the integral of 1.25 t, not the step times the current speed.
        const float before = puff.age;
        puff.age += dt;
        puff.position[1] += 0.5f * kPuffLift * (puff.age * puff.age - before * before);
    }
    puffs_.erase(std::remove_if(puffs_.begin(), puffs_.end(),
                                [](const Puff& p) { return p.age >= kPuffLife; }),
                 puffs_.end());

    // The beacon's mist: aged and moved, and new wisps while it is shown.
    for (Mist& one : mist_) {
        one.age += dt;
        one.position[0] += one.drift[0] * dt;
        one.position[1] += kMistRise * dt;
        one.position[2] += one.drift[1] * dt;
        one.spin += one.turn * dt;
    }
    mist_.erase(std::remove_if(mist_.begin(), mist_.end(),
                               [](const Mist& m) { return m.age >= kMistLife; }),
                mist_.end());
    if (beacon_ && beaconSeen_) {
        mistClock_ += dt;
        while (mistClock_ >= kMistEvery) {
            mistClock_ -= kMistEvery;
            if (mist_.size() >= kMostMist) continue;
            Mist one;
            const float way = unit() * 6.2831853f;
            const float off = unit() * 0.6f;
            one.position[0] = kBeaconAt[0] + std::cos(way) * off;
            one.position[1] = kBeaconAt[1] - 1.2f + unit() * 0.6f;
            one.position[2] = kBeaconAt[2] + std::sin(way) * off;
            const float drift = unit() * 6.2831853f;
            one.drift[0] = std::cos(drift) * kMistDrift;
            one.drift[1] = std::sin(drift) * kMistDrift;
            one.spin = unit() * 6.2831853f;
            one.turn = (unit() - 0.5f) * 0.6f;
            mist_.push_back(one);
        }
    } else {
        mistClock_ = 0.0f;
    }

    for (Spout& spout : spouts_) {
        const Figure* figure = sway.posedAt(spout.anchor.townIndex);
        spout.clock += dt * kFramesPerSecond;
        while (spout.clock >= 1.0f) {
            spout.clock -= 1.0f;
            // rand_fps_check(2): a coin flip every reference frame.
            if (!figure || (next() & 1u) || puffs_.size() >= kMostPuffs) continue;
            float local[3];
            const float u = (unit() * 2.0f - 1.0f) * kScatter;
            const float w = (unit() * 2.0f - 1.0f) * kScatter;
            for (int k = 0; k < 3; ++k) {
                local[k] = spout.anchor.point[k] + spout.anchor.across[0][k] * u +
                           spout.anchor.across[1][k] * w;
            }
            Puff puff;
            if (!figure->pointOn(spout.anchor.bone, local, puff.position)) continue;
            puff.scale = 0.48f + 0.32f * unit();
            puff.spin = 6.2831853f * unit();
            puffs_.push_back(puff);
        }
    }

    for (Fall& fall : falls_) {
        fall.clock += dt * kFramesPerSecond;
        while (fall.clock >= 1.0f) {
            fall.clock -= 1.0f;
            // The fountain's own coin flip, so the two read as the same water.
            if (next() & 1u) continue;
            for (float half : {-1.0f, 1.0f}) {
                if (puffs_.size() >= kMostPuffs) break;
                const float u = half * unit() * fall.reach[0];
                const float w = (unit() * 2.0f - 1.0f) * fall.reach[1];
                Puff puff;
                for (int k = 0; k < 3; ++k) {
                    puff.position[k] =
                        fall.point[k] + fall.across[0][k] * u + fall.across[1][k] * w;
                }
                puff.scale = 0.48f + 0.32f * unit();
                puff.spin = 6.2831853f * unit();
                puffs_.push_back(puff);
            }
        }
    }

    spun_ = std::fmod(spun_ + seconds, 360.0f / kStarDegreesPerSecond);

    for (Glint& glint : glints_) glint.age += dt * kFramesPerSecond;
    glints_.erase(std::remove_if(glints_.begin(), glints_.end(),
                                 [](const Glint& g) { return g.age >= kGlintLife; }),
                  glints_.end());
    strikeCount_ = 0;
    for (Thrower& thrower : throwers_) {
        const Figure* figure = sway.posedAt(thrower.anchor.townIndex);
        thrower.clock += dt * kFramesPerSecond;
        while (thrower.clock >= 1.0f) {
            thrower.clock -= 1.0f;
            if (!figure || next() % uint32_t(thrower.every) != 0u) continue;
            float at[3];
            if (!figure->pointOn(thrower.anchor.bone, thrower.anchor.point, at)) continue;
            if (thrower.sparks) {
                if (strikeCount_ < 4) {
                    for (int k = 0; k < 3; ++k) strikes_[strikeCount_][k] = at[k];
                    ++strikeCount_;
                }
                continue;
            }
            for (bool small : {false, true}) {
                if (glints_.size() >= kMostGlints) break;
                Glint glint;
                for (int k = 0; k < 3; ++k) glint.position[k] = at[k];
                glint.small = small;
                glint.spin = 6.2831853f * unit();
                glints_.push_back(glint);
            }
        }
    }
    lanternWait_ -= seconds;
    if (lanternWait_ <= 0.0f) {
        lanternWait_ = 1.0f / kLanternHz;
        luminosity_ = float(next() % 30u + 70u) * 0.01f;
    }
}

void Ornaments::gather(gfx::Effects& effects, const Sway& sway) const {
    if (bgfx::isValid(smoke_)) {
        for (const Puff& puff : puffs_) {
            const float frames = puff.age * kFramesPerSecond;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = puff.position[k];
            sprite.halfWidth = sprite.halfHeight =
                0.5f * kSheetMetres * (puff.scale + kPuffGrowth * puff.age);
            sprite.spin = puff.spin;
            // LifeTime / 8, clamped as glColor clamps it.
            const float light = std::clamp((16.0f - frames) / 8.0f, 0.0f, 1.0f);
            sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = 1.0f;
            sprite.colour[3] = light;
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    if (bgfx::isValid(shiny_)) {
        for (const Glint& glint : glints_) {
            const float life = kGlintLife - glint.age;
            float scale = std::sin(life * 10.0f * 3.14159265f / 180.0f);
            if (glint.small) scale *= kGlintSmall;
            if (scale <= 0.0f) continue;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = glint.position[k];
            sprite.halfWidth = sprite.halfHeight = 0.5f * kShinyMetres * scale;
            sprite.spin = glint.spin - (glint.small ? glint.age * 12.0f * 3.14159265f / 180.0f : 0.0f);
            sprite.sheet = shiny_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    // The beacon's mist, under its light.
    if (bgfx::isValid(smoke_)) {
        for (const Mist& one : mist_) {
            const float t = one.age / kMistLife;
            gfx::Sprite sprite;
            for (int k = 0; k < 3; ++k) sprite.position[k] = one.position[k];
            sprite.halfWidth = sprite.halfHeight = 0.5f * (kMistBorn + (kMistGrown - kMistBorn) * t);
            sprite.spin = one.spin;
            // Added, lit by the beacon: mixed over the light as smoke it laid a grey hole in the
            // core, and vapour round a lamp glows rather than shades.
            const float level = kMistPeak * std::sin(t * 3.14159265f);
            for (int k = 0; k < 3; ++k) sprite.colour[k] = kMistColour[k] * level;
            sprite.colour[3] = 1.0f;
            sprite.sheet = smoke_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    // And its bloom, a broad soft flare behind the stars (ours, with the mist above).
    if (beacon_ && beaconSeen_ && bgfx::isValid(light_)) {
        gfx::Sprite sprite;
        for (int a = 0; a < 3; ++a) sprite.position[a] = kBeaconAt[a];
        sprite.halfWidth = sprite.halfHeight = kBeaconBloom;
        for (int k = 0; k < 3; ++k) {
            sprite.colour[k] = kBeaconBloomColour[k] * kBeaconBloomLevel * luminosity_;
        }
        sprite.colour[3] = 1.0f;
        sprite.sheet = light_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    if (beacon_ && beaconSeen_ && bgfx::isValid(lightning_)) {
        for (float way : {1.0f, -1.0f}) {
            gfx::Sprite sprite;
            for (int a = 0; a < 3; ++a) sprite.position[a] = kBeaconAt[a];
            sprite.halfWidth = sprite.halfHeight =
                0.5f * kSheetMetres * kBeaconScale * kBeaconSoften;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = luminosity_ * kBeaconStarLevel;
            sprite.colour[3] = 1.0f;
            sprite.spin = way * kStarDegreesPerSecond * spun_ * 3.14159265f / 180.0f;
            sprite.sheet = lightning_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    if (bgfx::isValid(light_)) {
        for (const Lantern& lantern : lanterns_) {
            const bgfx::TextureHandle sheet =
                lantern.sheet == 2 ? magic_ : lantern.sheet == 1 ? lightning_ : light_;
            if (!bgfx::isValid(sheet)) continue;
            const Figure* figure = sway.posedAt(lantern.anchor.townIndex);
            if (!figure) continue;
            gfx::Sprite sprite;
            if (!figure->pointOn(lantern.anchor.bone, lantern.anchor.point, sprite.position)) continue;
            // CreateSprite's Scale is `Luminosity * 5` over a 64-texel sheet -- a quad up to
            // 3.2 m across, and taken whole it cut through the canopy and the crates beside
            // the lamp: a hard-edged orange patch on the load and half a disc hanging past the
            // corner. MU's picture hid that at 25 frames in low range. A quarter of it, the
            // lamp's own glow; ours, and marked as ours.
            // The merchant animal's is `Luminosity * 5` in scale; Noria's are a plain Scale.
            sprite.halfWidth = sprite.halfHeight =
                0.5f * kSheetMetres * lantern.scale * (lantern.swells ? luminosity_ : 1.0f);
            const float level = lantern.steady ? 1.0f : luminosity_;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = lantern.colour[k] * level;
            sprite.colour[3] = 1.0f;
            sprite.spin = lantern.spin * spun_ * 3.14159265f / 180.0f;
            sprite.sheet = sheet;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
}

}  // namespace mu::game
