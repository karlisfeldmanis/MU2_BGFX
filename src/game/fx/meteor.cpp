#include "game/fx/meteor.h"

#include "game/fx/effect_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {
namespace {

constexpr float kPi = 3.14159265f;
constexpr float kTwoPi = 6.28318531f;

// The ground light's colour and every ember's, before the frame's own flicker:
// `Vector(Luminosity * 1.f, Luminosity * 0.1f, Luminosity * 0.f, Light)` -- a deep orange-red.
// It belongs to those two and to NOTHING else: the flame cone is `BodyLight x BlendMeshLight`,
// and multiplying the cone by this as well crushes an already dark sheet toward black, leaving
// its flame pattern read as banding across a dim surface.
constexpr float kGlow[3] = {1.0f, 0.1f, 0.0f};

// The light the effect is created with -- the caster's own, near white out of doors -- and so
// the base tint of the rock and of its cone.
constexpr float kDaylight[3] = {1.0f, 0.95f, 0.9f};

// What the fireball lays on the ground: `(L*0.5, L*0.3, L*0.1)` with `L = LifeTime / 20`. A
// warm orange rather than the rock's near-pure red, and it is the blast's own doing -- every
// BITMAP_EXPLOTION in the game feeds the terrain light this way, whatever threw it.
constexpr float kBlastGlow[3] = {0.5f, 0.3f, 0.1f};

}  // namespace

uint32_t Meteor::roll() {
    dice_ ^= dice_ << 13;
    dice_ ^= dice_ >> 17;
    dice_ ^= dice_ << 5;
    return dice_;
}

float Meteor::unit() {
    return float(roll() & 0xFFFFFFu) / float(0x1000000u);
}

float Meteor::between(float a, float b) {
    return a + unit() * (b - a);
}

bool Meteor::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table, const content::Ground* ground) {
    ground_ = ground;
    const std::string dir = assetDir + "/effects/meteor/";
    const auto raw = [&](const char* name) -> bgfx::TextureHandle {
        const std::string path = dir + name;
        if (!core::fileExists(path)) {
            core::logError("meteor: no %s", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    const auto cooked = [&](const char* name) -> bgfx::TextureHandle {
        const content::EffectSheet* sheet = table.effect(name);
        if (sheet == nullptr) {
            core::logError("meteor: no cooked effect named '%s'", name);
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(assetDir + "/" + sheet->path, content::TextureRole::Albedo);
    };

    // Fire01 is ONE model drawing the whole streak: a compact 44-unit ball, which is the rock,
    // and a tapering 166-unit cone drawn additively, which is the flame. Two groups, two
    // blends, two independent flickers -- which is why they are kept apart here rather than
    // merged into one mesh.
    fireGroupCount_ = 0;
    if (loadEffectObj(dir + "Fire01.obj", kUnit, "fire02", fireGroups_[0].triangles)) {
        fireGroups_[0].sheet = raw("fire02.png");
        fireGroups_[0].blend = gfx::Blend::Alpha;
        fireGroupCount_ = 1;
    }
    if (loadEffectObj(dir + "Fire01.obj", kUnit, "fire01", fireGroups_[1].triangles)) {
        fireGroups_[1].sheet = raw("fire01.png");
        fireGroups_[1].blend = gfx::Blend::Additive;
        fireGroupCount_ = 2;
    }
    for (int s = 0; s < 2; ++s) {
        const std::string name = s == 0 ? "Stone01.obj" : "Stone02.obj";
        if (loadEffectObj(dir + name, kUnit, "", stoneGroups_[s].triangles)) {
            stoneGroups_[s].sheet = raw("fire02.png");
            stoneGroups_[s].blend = gfx::Blend::Alpha;
        }
    }
    blastSheet_ = cooked("explosion");
    emberSheet_ = cooked("fire");
    blueEmberSheet_ = cooked("comet_fire");
    glowSheet_ = cooked("light");  // a soft white flare, the fireball's burning heart
    smokeSheet_ = cooked("smoke");  // Effect/smoke02, the arrows' wisps' sheet

    int fireTris = 0;
    for (int g = 0; g < fireGroupCount_; ++g) {
        fireTris += int(fireGroups_[g].triangles.size()) / 3;
    }
    core::logf("meteor: Fire01 %d triangles (%d groups), Stone01 %zu, Stone02 %zu, "
               "blast %s, embers %s, glow %s",
               fireTris, fireGroupCount_, stoneGroups_[0].triangles.size() / 3,
               stoneGroups_[1].triangles.size() / 3,
               bgfx::isValid(blastSheet_) ? "yes" : "NO", bgfx::isValid(emberSheet_) ? "yes" : "NO",
               bgfx::isValid(glowSheet_) ? "yes" : "NO");
    return fireGroupCount_ > 0;
}

void Meteor::shutdown() {
    for (auto& g : fireGroups_) g.triangles.clear();
    for (auto& g : stoneGroups_) g.triangles.clear();
    fireGroupCount_ = 0;
    for (auto& m : meteors_) m.alive = false;
    for (auto& f : fireballs_) f.alive = false;
    for (auto& s : stones_) s.alive = false;
    for (auto& m : motes_) m.alive = false;
    quake_ = 0.0f;
}

void Meteor::cast(float targetX, float targetZ, uint32_t attacker, float weight) {
    if (fireGroupCount_ == 0 || ground_ == nullptr) return;
    Live* slot = nullptr;
    for (auto& m : meteors_) {
        if (!m.alive) { slot = &m; break; }
    }
    if (slot == nullptr) {
        ++refused_;
        return;
    }

    // **It comes in at an angle, and reading the spawn without that makes it look like a miss.**
    // `CreateEffect` puts the rock 400 units up and 130 + rand % 32 units aside and hands it
    // `Direction (0, 0, -50)` -- straight down, which against that offset would drop it a tile
    // and a half wide of whatever it was thrown at. The turn is elsewhere: MODEL_FIRE has no
    // case of its own in the mover, so the default arm runs `MoveParticle(o, true)`, and the
    // `true` turns the direction by the object's own angle before integrating it. With
    // `Angle = (0, 20, 0)` that is 50*sin20 = 17.10 units of drift a frame against
    // 50*cos20 = 46.98 of descent, so 400 units of fall take 8.513 frames and carry it 145.6
    // sideways -- the middle of its own spawn offset.
    //
    // So the rock lands ON the target, within 16 centimetres. This engine fell straight down at
    // first and every rock landed a metre and a half east of the man it was thrown at.
    const float radians = kEntryDegrees * kPi / 180.0f;
    slot->alive = true;
    // A shower's rock is drawn at the root of its weight and not its own roll on top, so its
    // largest (2) is MU's largest rock, 1.7, and its smallest a pebble at 0.76 (the user,
    // 2026-10-04: "some of rocks was to big", at the weight times the roll, up to 3.4). Its
    // stones, burst and light take the same root.
    const float drawn = std::sqrt(weight);
    slot->size = weight == 1.0f ? between(kSmallestRock, kLargestRock)
                                : kLargestRock / std::sqrt(2.0f) * drawn;
    slot->weight = drawn;
    slot->shower = weight != 1.0f;
    slot->floorY = ground_->heightAt(targetX, targetZ);
    slot->x = targetX + (kSideways + float(roll() % uint32_t(kSidewaysSpread))) * kUnit;
    slot->y = slot->floorY + kLift * kUnit;
    slot->z = targetZ;
    // Metres a second: one factor of 25 on a speed, and the sideways one is negative because
    // the heading points back at the target the offset put it beside.
    slot->driftX = -std::sin(radians) * kFallSpeed * kUnit * kReferenceFps;
    slot->fallY = -std::cos(radians) * kFallSpeed * kUnit * kReferenceFps;
    slot->driftZ = 0.0f;
    slot->stretch = slot->wide = 1.0f;
    for (float& c : slot->tint) c = 1.0f;
    if (slot->shower) {
        // Its own slant and quarter (kSlantDegrees, kYawDegrees). Down at MU's own speed, so
        // the landing keeps fallSeconds(); set beside the target by the slant's tangent, so it
        // lands on it.
        const float slant = between(kSlantDegrees[0], kSlantDegrees[1]) * kPi / 180.0f;
        const float yaw = between(-kYawDegrees, kYawDegrees) * kPi / 180.0f;
        const float side = kLift * std::tan(slant) * kUnit;
        slot->x = targetX + std::cos(yaw) * side;
        slot->z = targetZ + std::sin(yaw) * side;
        const float across = -std::fabs(slot->fallY) * std::tan(slant);
        slot->driftX = std::cos(yaw) * across;
        slot->driftZ = std::sin(yaw) * across;
        slot->stretch = between(kTrailLength[0], kTrailLength[1]);
        slot->wide = between(kTrailGirth[0], kTrailGirth[1]);
        slot->tint[1] = between(kTrailGreen[0], kTrailGreen[1]);
        slot->tint[2] = between(kTrailBlue[0], kTrailBlue[1]);
    }
    slot->flown = 0.0f;
    slot->sparked = 0.0f;
    slot->smoked = 0.0f;
    slot->left = kRockFrames;
    slot->bodyLight = kBrightestGlow;
    slot->flameLight = kBrightestFlame;
    slot->attacker = attacker;
}

Meteor::Mote* Meteor::freeMote() {
    for (auto& m : motes_) {
        if (!m.alive) return &m;
    }
    ++refused_;
    return nullptr;
}

void Meteor::ember(const Live& rock) {
    const float at[3] = {rock.x, rock.y, rock.z};
    const float heading[3] = {rock.driftX, rock.fallY, rock.driftZ};
    emberAt(at, heading, rock.bodyLight);
}

Meteor::Mote* Meteor::emberAt(const float at[3], const float heading[3], float light,
                              bool fireball) {
    if (!bgfx::isValid(emberSheet_)) return nullptr;
    Mote* mote = freeMote();
    if (mote == nullptr) return nullptr;
    mote->alive = true;
    mote->kind = Mote::Kind::Ember;
    for (int a = 0; a < 3; ++a) mote->position[a] = at[a];
    // Along the rock's own travel, at (rand()%16+32)*0.1 units a frame.
    const float speed = between(kSlowestDrift, kFastestDrift) * kUnit * kReferenceFps;
    const float length = std::sqrt(heading[0] * heading[0] + heading[1] * heading[1] +
                                   heading[2] * heading[2]);
    for (int a = 0; a < 3; ++a) {
        mote->velocity[a] = length > 0.0f ? heading[a] / length * speed : 0.0f;
    }
    mote->size = between(kSmallestEmber, kLargestEmber) * kEmberSheetUnits * kUnit *
                 (fireball ? kFireEmberShare : 1.0f);
    mote->spin = unit() * kTwoPi;
    mote->left = mote->born = kEmberFrames;
    mote->rise = 0.0f;
    mote->cools = fireball;
    // The frame's own body light, HELD for the whole life: an ember does not fade its colour,
    // what shrinks is its size. A fireball's is born hotter and cools (see `cools`).
    for (int c = 0; c < 3; ++c) mote->colour[c] = (fireball ? kFireEmber[c] : kGlow[c]) * light;
    return mote;
}

void Meteor::trailSmokeAt(const Live& rock) {
    if (!bgfx::isValid(smokeSheet_)) return;
    Mote* mote = freeMote();
    if (mote == nullptr) return;
    *mote = Mote{};
    mote->alive = true;
    mote->kind = Mote::Kind::Smoke;
    mote->position[0] = rock.x + between(-0.1f, 0.1f);
    mote->position[1] = rock.y + between(-0.1f, 0.1f);
    mote->position[2] = rock.z + between(-0.1f, 0.1f);
    // Left behind, drifting up a little: the rock goes on and the smoke stays in the sky.
    mote->velocity[0] = between(-0.1f, 0.1f);
    mote->velocity[1] = between(0.1f, 0.3f);
    mote->velocity[2] = between(-0.1f, 0.1f);
    mote->scale = kTrailSmokeScale * rock.weight * rock.wide;
    mote->size = kSmokeBorn * mote->scale;
    mote->spin = unit() * kTwoPi;
    mote->left = mote->born = kTrailSmokeFrames * between(0.8f, 1.2f);
    for (int c = 0; c < 3; ++c) mote->colour[c] = kTrailSmokeGrey[c];
}

void Meteor::burn(const float feet[3], float tall, float seconds, bool blue) {
    // The light on him, held a little past the last call so it does not blink between frames.
    for (int k = 0; k < 3; ++k) burnAt_[k] = feet[k];
    burnAt_[1] += tall * 0.55f;
    burnLit_ = 0.1f;
    burnRoll_ = between(kDimmestGlow, kBrightestGlow);
    burnBlue_ = blue;
    if (!bgfx::isValid(emberSheet_)) return;
    if (blue && !bgfx::isValid(blueEmberSheet_)) blue = false;
    burnDue_ -= seconds * kReferenceFps;
    while (burnDue_ <= 0.0f) {
        burnDue_ += kBurnEvery;
        Mote* mote = freeMote();
        if (mote == nullptr) return;
        *mote = Mote{};
        mote->alive = true;
        mote->kind = Mote::Kind::Ember;
        const float turn = unit() * kTwoPi;
        const float reach = kBurnRadius * (0.4f + unit() * 0.6f);
        mote->position[0] = feet[0] + std::cos(turn) * reach;
        mote->position[1] = feet[1] + tall * (0.05f + unit() * 0.8f);
        mote->position[2] = feet[2] + std::sin(turn) * reach;
        // Up off him and a little out: the fire climbs his body.
        const float speed = between(kSlowestDrift, kFastestDrift) * kUnit * kReferenceFps * kBurnClimb;
        mote->velocity[0] = std::cos(turn) * speed * 0.3f;
        mote->velocity[1] = speed;
        mote->velocity[2] = std::sin(turn) * speed * 0.3f;
        mote->size = between(kSmallestEmber, kLargestEmber) * kEmberSheetUnits * kUnit *
                     kBurnEmberShare;
        mote->spin = unit() * kTwoPi;
        mote->left = mote->born = kEmberFrames;
        mote->rise = 0.0f;
        mote->cools = false;
        mote->blue = blue;
        const float light = between(kDimmestGlow, kBrightestGlow);
        const float* tint = blue ? kBurnBlueEmber : kBurnEmber;
        for (int c = 0; c < 3; ++c) mote->colour[c] = tint[c] * light;
    }
}

void Meteor::hurl(const float from[3], const float to[3], uint32_t target, bool atHand) {
    if (fireGroupCount_ == 0) return;
    Hurled* ball = nullptr;
    for (auto& one : fireballs_) {
        if (!one.alive) { ball = &one; break; }
    }
    if (ball == nullptr) {
        ++refused_;
        return;
    }
    *ball = Hurled{};
    ball->alive = true;
    ball->at[0] = from[0];
    ball->at[1] = from[1] + (atHand ? 0.0f : kHurlLift * kUnit);
    ball->at[2] = from[2];
    // At the middle of the body, height and all -- ours, as the bolt's is. MU's direction has no
    // vertical term and flies level, which on this camera passes over a spider and under a dragon.
    float way[3] = {to[0] - ball->at[0], to[1] - ball->at[1], to[2] - ball->at[2]};
    const float far = std::sqrt(way[0] * way[0] + way[1] * way[1] + way[2] * way[2]);
    if (far > 1e-4f) {
        for (int k = 0; k < 3; ++k) ball->along[k] = way[k] / far;
    } else {
        ball->along[0] = 1.0f;
    }
    ball->floorY = ground_ ? ground_->heightAt(to[0], to[2]) : from[1];
    ball->size = between(kSmallestBall, kLargestBall);
    ball->left = kHurlFrames;
    ball->bodyLight = kBrightestGlow;
    ball->flameLight = kBrightestFlame;
    ball->tumble = unit() * kTwoPi;
    ball->target = target;
    for (int k = 0; k < 3; ++k) ball->aim[k] = to[k];
}

void Meteor::missHurl(uint32_t target) {
    // The one nearest arriving: fireballs at one body are let go in order and land in order.
    Hurled* first = nullptr;
    for (auto& ball : fireballs_) {
        if (!ball.alive || ball.target != target || ball.missing) continue;
        if (first == nullptr || ball.left < first->left) first = &ball;
    }
    if (first != nullptr) first->missing = true;
}

bool Meteor::hurling(Hurled& ball, float seconds, bool standing, const float* there) {
    const float refFrames = seconds * kReferenceFps;
    const float went = kHurlSpeed * kUnit * kReferenceFps * seconds;
    // Steered after the body as it moves, the bolt's way -- ours. Not once it is past.
    const bool chasing = standing && there != nullptr && !ball.passed;
    if (chasing) {
        float want[3] = {there[0] - ball.at[0], there[1] - ball.at[1], there[2] - ball.at[2]};
        const float far = std::sqrt(want[0] * want[0] + want[1] * want[1] + want[2] * want[2]);
        if (far > 1e-4f) {
            const float share = std::min(1.0f, kHurlSteer * refFrames);
            for (int k = 0; k < 3; ++k) ball.along[k] += (want[k] / far - ball.along[k]) * share;
            const float norm = std::max(1e-4f, std::sqrt(ball.along[0] * ball.along[0] +
                                                         ball.along[1] * ball.along[1] +
                                                         ball.along[2] * ball.along[2]));
            for (int k = 0; k < 3; ++k) ball.along[k] /= norm;
        }
    }
    for (int k = 0; k < 3; ++k) ball.at[k] += ball.along[k] * went;
    ball.left -= refFrames;
    if (ball.left <= 0.0f) return false;

    // The rock's 0.7-1.0 roll, re-rolled every frame, and its last five frames going out.
    ball.bodyLight = between(kDimmestGlow, kBrightestGlow);
    ball.flameLight = between(kDimmestFlame, kBrightestFlame);
    ball.tumble += kBallSpin * refFrames;
    if (ball.left < kFadesUnder) {
        ball.bodyLight = std::max(0.0f, ball.bodyLight - (kFadesUnder - ball.left) * kFadeStep);
    }

    // Arrived: at the body's middle or past it this frame. Two stones fall from the body to the
    // floor (MU's whole arrival for subtype 1) and a half-size burst goes up there (ours); the
    // ball ends ON the body, as the bolt does, where MU flies it on through and out. One the
    // realm said missed flies by, and one whose target died in the air goes out on its own.
    if (chasing) {
        const float left[3] = {there[0] - ball.at[0], there[1] - ball.at[1],
                               there[2] - ball.at[2]};
        const float ahead = left[0] * ball.along[0] + left[1] * ball.along[1] +
                            left[2] * ball.along[2];
        const float gap = std::sqrt(left[0] * left[0] + left[1] * left[1] + left[2] * left[2]);
        if (gap <= kHurlStrikes || ahead <= 0.0f) {
            if (ball.missing) {
                ball.passed = true;
                ball.left = std::min(ball.left, kHurlPastFrames);
            } else {
                const float floor = ground_ ? ground_->heightAt(there[0], there[2]) : ball.floorY;
                stonesAt(there[0], there[2], floor, kHurlStones);
                blastAt(there[0], there[1], there[2], kHurlBlastShare);
                return false;
            }
        }
    }

    // An ember every fifty units, which at fifty a frame is MU's one a frame at any frame rate.
    ball.flown += went;
    ball.travelled += went;
    while (ball.flown >= kEmberSpacingUnits * kUnit) {
        ball.flown -= kEmberSpacingUnits * kUnit;
        emberAt(ball.at, ball.along, ball.bodyLight, true);
    }
    return true;
}

void Meteor::land(const Live& rock) {
    const float floor = rock.floorY;

    // 1. The six stones, on MU's shared ballistic arm -- the one its bones and its broken ice
    //    ride too.
    //    A shower's heavy rock throws more, a light one fewer (cast's `weight`).
    stonesAt(rock.x, rock.z, floor, std::max(2, int(std::lround(6.0f * rock.weight))),
             rock.shower ? kShowerStone : 1.0f);
    // 2. The blast, and it is asked for after the stones so that a nearly full pool spends its
    //    last slots on the debris rather than on one sprite.
    blastAt(rock.x, floor + kBlastLift * kUnit, rock.z, rock.weight, rock.shower);
    // 3. And a little smoke rising out of it (kSmokePuffs, ours), last, so a full pool drops it
    //    first.
    smokeAt(rock.x, floor, rock.z, rock.weight, rock.shower);
}

void Meteor::smokeAt(float x, float floor, float z, float weight, bool shower) {
    if (!bgfx::isValid(smokeSheet_)) return;
    // MU's rock its three; a shower's by its weight, fainter (kShowerPuff*, kShowerSmoke*).
    const int puffs =
        shower ? std::max(1, int(weight / kShowerPuffWeight)) : kSmokePuffs;
    for (int p = 0; p < puffs; ++p) {
        Mote* mote = freeMote();
        if (mote == nullptr) return;
        mote->alive = true;
        mote->kind = Mote::Kind::Smoke;
        const float yaw = unit() * kTwoPi;
        const float out = 0.15f + 0.25f * unit();
        mote->position[0] = x + std::sin(yaw) * out;
        mote->position[1] = floor + 0.3f + 0.3f * unit();
        mote->position[2] = z + std::cos(yaw) * out;
        mote->velocity[0] = std::sin(yaw) * kSmokeDrift * unit();
        mote->velocity[1] = kSmokeRise * (0.7f + 0.6f * unit());
        mote->velocity[2] = std::cos(yaw) * kSmokeDrift * unit();
        mote->scale = shower ? kShowerSmokeSize * weight : 1.0f;
        mote->opacity = shower ? kShowerSmokeOpacity : 1.0f;
        mote->size = kSmokeBorn * mote->scale;
        mote->spin = unit() * kTwoPi;
        // Staggered, so the three do not open as one.
        mote->left = mote->born = kSmokeFrames * (0.8f + 0.2f * float(p) / float(puffs));
        mote->rise = 0.0f;
        mote->cools = false;
        for (int c = 0; c < 3; ++c) mote->colour[c] = kSmokeGrey[c];
    }
}

void Meteor::stonesAt(float x, float z, float floor, int count, float scale) {
    // Each rolls its own size, its own gravity and its own scatter; none of them takes the
    // rock's roll, which would put a field of boulders on the grass.
    for (int s = 0; s < count; ++s) {
        Stone* slot = nullptr;
        for (auto& st : stones_) {
            if (!st.alive) { slot = &st; break; }
        }
        if (slot == nullptr) { ++refused_; continue; }
        slot->alive = true;
        slot->landed = false;
        slot->fade = 0.0f;
        slot->which = int(roll() & 1u);  // MODEL_STONE1 + rand() % 2
        slot->left = kStoneLeastFrames + float(roll() % uint32_t(kStoneMoreFrames));
        slot->size = between(kSmallestStone, kLargestStone) * scale;
        // An ACCELERATION: units a frame SQUARED, so two factors of 25 and not one. Read as a
        // velocity it comes out twenty-five times too small and the debris drifts in slow
        // motion, weightless.
        slot->gravity = (kLeastGravity + float(roll() % uint32_t(kMoreGravity))) * 0.1f * kUnit *
                        kReferenceFps * kReferenceFps;
        const float scatter = (kLeastScatter + float(roll() % uint32_t(kMoreScatter))) * 0.1f *
                              kUnit * kReferenceFps;
        const float yaw = unit() * kTwoPi;
        slot->position[0] = x;
        slot->position[1] = floor;  // a stone's own lift is nought: they start on the floor
        slot->position[2] = z;
        slot->velocity[0] = std::sin(yaw) * scatter;
        slot->velocity[1] = 0.0f;  // flat, and the gravity is what lifts nothing and drops it
        slot->velocity[2] = std::cos(yaw) * scatter;
        slot->lean[0] = unit() * kTwoPi;
        slot->lean[1] = unit() * kTwoPi;
    }
}

void Meteor::blastAt(float x, float y, float z, float share, bool shower) {
    if (bgfx::isValid(blastSheet_)) {
        if (Mote* mote = freeMote()) {
            mote->alive = true;
            mote->kind = Mote::Kind::Blast;
            mote->position[0] = x;
            mote->position[1] = y;
            mote->position[2] = z;
            for (int a = 0; a < 3; ++a) mote->velocity[a] = 0.0f;
            mote->size = kBlastUnits * kUnit * share;
            mote->spin = 0.0f;
            mote->left = mote->born = kBlastFrames;
            mote->rise = 0.0f;
            // White. MU's colour argument here is uninitialised stack memory, so there is
            // nothing to port, and tinting it with the trail's red takes the heat out of an
            // already orange core.
            float heat = 1.0f;
            // A shower's is smaller and dimmer, and no two alike: its own turn, size, heat and
            // life (kShowerBlast*, ours). The heat is also its light's (lights()).
            if (shower) {
                mote->size *= kShowerBlast * between(kShowerBlastSize[0], kShowerBlastSize[1]);
                mote->spin = unit() * kTwoPi;
                mote->left = mote->born =
                    between(kShowerBlastFrames[0], kShowerBlastFrames[1]);
                heat = between(kShowerBlastHeat[0], kShowerBlastHeat[1]);
            }
            mote->lit = shower ? kShowerLight : 1.0f;
            for (int c = 0; c < 3; ++c) mote->colour[c] = heat;
        }
    }
}

void Meteor::update(float seconds, std::vector<Impact>& impacts) {
    const float refFrames = seconds * kReferenceFps;
    burnLit_ = std::max(0.0f, burnLit_ - seconds);

    for (auto& m : meteors_) {
        if (!m.alive) continue;
        m.x += m.driftX * seconds;
        m.y += m.fallY * seconds;
        m.z += m.driftZ * seconds;
        m.left -= refFrames;

        // Two rolls, every frame, and they are independent on purpose: the body's light is
        // what the rock and its ground glow are lit by, the cone's is its own, so the flame
        // shimmers against itself rather than in step with the stone.
        m.bodyLight = between(kDimmestGlow, kBrightestGlow);
        if (m.left < kFadesUnder) {
            m.bodyLight = std::max(0.0f, m.bodyLight - (kFadesUnder - m.left) * kFadeStep);
        }
        m.flameLight = between(kDimmestFlame, kBrightestFlame);

        // The trail, stepped by DISTANCE and never by time: fifty units of spacing at fifty
        // units a frame is MU's own one-an-frame, and it stays that at any frame rate.
        const float speed =
            std::sqrt(m.driftX * m.driftX + m.fallY * m.fallY + m.driftZ * m.driftZ);
        m.flown += speed * seconds;
        while (m.flown >= kEmberSpacingUnits * kUnit) {
            m.flown -= kEmberSpacingUnits * kUnit;
            ember(m);
        }
        // A shower's ribbon of cooling sparks (kTrailSpacing, ours), off the line a little.
        if (m.shower) {
            m.sparked += speed * seconds;
            m.smoked += speed * seconds;
            const float heading[3] = {m.driftX, m.fallY, m.driftZ};
            while (m.sparked >= kTrailSpacing * kUnit) {
                m.sparked -= kTrailSpacing * kUnit;
                const float off = kTrailScatter * m.weight * m.wide;
                const float at[3] = {m.x + between(-off, off), m.y + between(-off, off),
                                     m.z + between(-off, off)};
                // Flung off the line too, some further than others.
                if (Mote* spark = emberAt(at, heading, m.bodyLight * m.weight, true)) {
                    for (int a = 0; a < 3; ++a) spark->velocity[a] += between(-kSparkKick, kSparkKick);
                }
            }
            while (m.smoked >= kTrailSmokeSpacing * kUnit) {
                m.smoked -= kTrailSmokeSpacing * kUnit;
                trailSmokeAt(m);
            }
        }

        // Landing. MU makes the blast either way -- on the ground or on running out of life.
        const float floor = ground_ ? ground_->heightAt(m.x, m.z) : m.floorY;
        if (m.y <= floor || m.left <= 0.0f) {
            m.y = floor;
            m.floorY = floor;
            land(m);
            impacts.push_back(Impact{m.x, m.z, m.attacker});
            // `EarthQuake = (rand() % 4 - 4) * 0.1` (ZzzEffect.cpp:7749): degrees of camera
            // pitch and not a length, which is why no unit conversion belongs on it. A second
            // landing while the first one's jolt still runs takes the new value whole, as MU's
            // one global does.
            const float jolt = float(int(roll() % 4) - 4) * 0.1f;
            // A shower's pebbles land without one; past kQuakeFrom it grows to MU's whole.
            if (!m.shower) quake_ = jolt;
            else if (m.weight > kQuakeFrom) {
                quake_ = jolt * std::min(1.0f, (m.weight - kQuakeFrom) / (kQuakeFull - kQuakeFrom));
            }
            m.alive = false;
        }
    }

    for (auto& s : stones_) {
        if (!s.alive) continue;
        s.left -= refFrames;
        if (s.left <= 0.0f) { s.alive = false; continue; }
        // Gravity into the velocity FIRST and the position after it, which is MU's own order
        // (ZzzEffect.cpp:7335-7339) and not a detail: the other way round every piece flies one
        // frame of gravity further than it should, every frame.
        s.velocity[1] -= s.gravity * seconds;
        for (int a = 0; a < 3; ++a) s.position[a] += s.velocity[a] * seconds;
        // `Angle += 0.5 * LifeTime` on two axes, so a stone tumbles fast while it is young and
        // slows as it ages -- which is the life counting DOWN and not an added damping.
        const float tumble = kStoneTumble * s.left * kReferenceFps * kPi / 180.0f * seconds;
        s.lean[0] += tumble;
        s.lean[1] += tumble;

        const float floor = ground_ ? ground_->heightAt(s.position[0], s.position[2]) : 0.0f;
        if (s.position[1] <= floor) {
            s.position[1] = floor;
            s.landed = true;
            // It POPS, less each time: the horizontal is dragged and the vertical is set from
            // what is left of its life, so the bounce shrinks as the stone ages out.
            const float drag = std::pow(kStoneBounceDrag, refFrames);
            s.velocity[0] *= drag;
            s.velocity[2] *= drag;
            // `HeadAngle[2] += 1.0 * LifeTime` -- a VELOCITY in units a frame, so ONE factor
            // of 25 and not two. It carried two here, which made a stone leave the ground at
            // two hundred and fifty metres a second. The pop shrinks each time it lands
            // because the life it is taken from is smaller each time.
            const float bounce = s.left * kUnit * kReferenceFps;
            s.velocity[1] = bounce < kStoneRestUnder * kUnit * kReferenceFps ? 0.0f : bounce;
        }
        if (s.landed) s.fade = std::min(1.0f, s.fade + kStoneFade * refFrames);
    }

    for (auto& m : motes_) {
        if (!m.alive) continue;
        m.left -= refFrames;
        if (m.left <= 0.0f) { m.alive = false; continue; }
        for (int a = 0; a < 3; ++a) m.position[a] += m.velocity[a] * seconds;
        if (m.kind == Mote::Kind::Smoke) {
            const float t = 1.0f - m.left / m.born;
            m.size = (kSmokeBorn + (kSmokeGrown - kSmokeBorn) * std::sqrt(t)) * m.scale;
            m.spin += 0.012f * refFrames;
        }
        if (m.kind == Mote::Kind::Ember) {
            // An accelerating LIFT and not a pull, whatever the field it is kept in is called:
            // `o->Gravity += 0.004` and then `Position[2] += Gravity * 10`, which is about
            // eleven units of climb over a whole life.
            m.rise += kEmberRise * refFrames;
            m.position[1] += m.rise * kEmberRiseScale * refFrames * kUnit;
            m.spin += kEmberSpin * kPi / 180.0f * refFrames;
            m.size -= kEmberShrink * kEmberSheetUnits * kUnit * refFrames;
            // MU lets the size go negative and draws the sprite inside out; this does not.
            if (m.size <= 0.0f) m.alive = false;
        }
    }

    // MU's `EarthQuake *= 0.2f` (MainScene.cpp:199), once a frame, taken here against the
    // reference frame so the jolt lasts the same fifth of a second whatever this machine draws
    // at. It is a violent decay and that is the point: three frames and it is a hundredth of
    // what it was.
    quake_ *= std::pow(kQuakeDecay, refFrames);
    if (std::fabs(quake_) < kQuakeEpsilon) quake_ = 0.0f;
}

void Meteor::submit(gfx::Effects& effects, const std::vector<Corner>& tris,
                    bgfx::TextureHandle sheet, gfx::Blend blend, const float at[3], float lean,
                    float tumble, float scale, const float colour[3], float alpha) const {
    if (tris.empty() || !bgfx::isValid(sheet)) return;
    // `lean` is about the axis across its travel, which here is Z: MU's `Angle.y` is this
    // engine's Z. Leaned about X instead, the rock tips out of its own plane of travel and the
    // burning end points where the trail does not go.
    //
    // `tumble` is the second axis, and debris needs both: MU turns `Angle[0]` AND `Angle[1]`
    // at the same rate (ZzzEffect.cpp:7342-7343), so a stone rolls as well as spins. On one
    // axis it reads as a coin.
    const float c = std::cos(lean), s = std::sin(lean);
    const float cx = std::cos(tumble), sx = std::sin(tumble);
    gfx::Sprite sprite;
    sprite.placed = true;
    sprite.sheet = sheet;
    sprite.blend = blend;
    for (int k = 0; k < 3; ++k) sprite.colour[k] = colour[k];
    sprite.colour[3] = alpha;
    // An added mesh sorts as one piece, at `at` (submitEffectAlong says why).
    const bool added = blend == gfx::Blend::Additive || blend == gfx::Blend::Flame ||
                       blend == gfx::Blend::Breath;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        for (int k = 0; k < 4; ++k) {
            const Corner& p = tris[i + size_t(std::min(k, 2))];
            const float x = p.x * scale, y = p.y * scale, z = p.z * scale;
            const float ry = x * s + y * c;
            sprite.corner[k][0] = at[0] + x * c - y * s;
            sprite.corner[k][1] = at[1] + ry * cx - z * sx;
            sprite.corner[k][2] = at[2] + ry * sx + z * cx;
            sprite.cornerUv[k][0] = p.u;
            sprite.cornerUv[k][1] = p.v;
        }
        for (int a = 0; a < 3; ++a) {
            sprite.position[a] =
                added ? at[a]
                      : (sprite.corner[0][a] + sprite.corner[1][a] + sprite.corner[2][a]) / 3.0f;
        }
        effects.add(sprite);
    }
}

void Meteor::gatherShowerRock(gfx::Effects& effects, const Live& m, const float* eye) const {
    // Its basis off its own heading, as MU's lean puts it on the 20 degree one: the model's Y
    // back up the fall, Z level across it, X the third. For MU's own heading this is the lean.
    const float speed = std::sqrt(m.driftX * m.driftX + m.fallY * m.fallY + m.driftZ * m.driftZ);
    if (speed <= 0.0f) return;
    const float y[3] = {-m.driftX / speed, -m.fallY / speed, -m.driftZ / speed};
    float z[3] = {-y[2], 0.0f, y[0]};
    const float level = std::sqrt(z[0] * z[0] + z[2] * z[2]);
    if (level < 1e-4f) return;
    z[0] /= level;
    z[2] /= level;
    const float x[3] = {y[1] * z[2] - y[2] * z[1], y[2] * z[0] - y[0] * z[2],
                        y[0] * z[1] - y[1] * z[0]};
    const float white[3] = {1.0f, 1.0f, 1.0f};
    const float at[3] = {m.x, m.y, m.z};
    submitEffectAlong(effects, fireGroups_[0].triangles, fireGroups_[0].sheet,
                      fireGroups_[0].blend, at, x, y, z, m.size, white, 1.0f);
    // Its blur: the rock again a few hundredths of a second back, fainter (kRockGhosts).
    for (int g = 0; g < kRockGhosts; ++g) {
        const float back = kGhostSeconds[g];
        const float was[3] = {m.x - m.driftX * back, m.y - m.fallY * back, m.z - m.driftZ * back};
        submitEffectAlong(effects, fireGroups_[0].triangles, fireGroups_[0].sheet,
                          fireGroups_[0].blend, was, x, y, z, m.size, white, kGhostAlpha[g]);
    }
    if (fireGroupCount_ < 2) return;
    // The flame, its own length and girth and heat.
    const float cx[3] = {x[0] * m.wide, x[1] * m.wide, x[2] * m.wide};
    const float cy[3] = {y[0] * m.stretch, y[1] * m.stretch, y[2] * m.stretch};
    const float cz[3] = {z[0] * m.wide, z[1] * m.wide, z[2] * m.wide};
    const float cone[3] = {kDaylight[0] * m.flameLight * m.tint[0],
                           kDaylight[1] * m.flameLight * m.tint[1],
                           kDaylight[2] * m.flameLight * m.tint[2]};
    submitEffectAlong(effects, fireGroups_[1].triangles, fireGroups_[1].sheet,
                      fireGroups_[1].blend, at, cx, cy, cz, m.size, cone, 1.0f);
    // The molten head: the Fire Ball's halo and heart, drawn toward the eye so the stone does
    // not sort over its middle (kHeadGlow).
    if (!bgfx::isValid(glowSheet_)) return;
    float toward[3] = {0.0f, 0.0f, 0.0f};
    if (eye != nullptr) {
        const float d[3] = {eye[0] - m.x, eye[1] - m.y, eye[2] - m.z};
        const float far = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (far > 1e-3f) for (int a = 0; a < 3; ++a) toward[a] = d[a] / far * kGlowForward;
    }
    for (int layer = 0; layer < 2; ++layer) {
        gfx::Sprite glow;
        for (int a = 0; a < 3; ++a) glow.position[a] = at[a] + toward[a];
        const float wide =
            (layer == 0 ? kHaloWide * kHeadHalo : kHeartWide) * m.size * kHeadGlow;
        glow.halfWidth = glow.halfHeight = wide * 0.5f;
        glow.spin = m.left * 0.4f;
        const float* tint = layer == 0 ? kHalo : kHeart;
        for (int c = 0; c < 3; ++c) glow.colour[c] = tint[c] * m.bodyLight * m.tint[c];
        glow.sheet = glowSheet_;
        glow.blend = gfx::Blend::Additive;
        effects.add(glow);
    }
}

void Meteor::gather(gfx::Effects& effects, const float* eye) const {
    // The rock, leaning the 20 degrees it was thrown with and never turning after: MU writes
    // `o->Angle` once at the spawn and never again.
    const float lean = -kEntryDegrees * kPi / 180.0f;
    for (const auto& m : meteors_) {
        if (!m.alive) continue;
        const float at[3] = {m.x, m.y, m.z};
        if (m.shower) {
            gatherShowerRock(effects, m, eye);
            continue;
        }
        if (fireGroupCount_ > 0) {
            // The rock's albedo is WHITE -- its sheet, unshaded, and nothing multiplied into
            // it. The body's luminosity roll is the ground light's energy and not a tint;
            // multiplied into the mesh it turns the stone into a black hole at the head of
            // its own flame, which is exactly what the first shot of this showed.
            const float white[3] = {1.0f, 1.0f, 1.0f};
            submit(effects, fireGroups_[0].triangles, fireGroups_[0].sheet,
                   fireGroups_[0].blend, at, lean, 0.0f, m.size, white, 1.0f);
        }
        if (fireGroupCount_ > 1) {
            // Its OWN roll, over the caster's daylight -- never over the trail's orange.
            const float cone[3] = {kDaylight[0] * m.flameLight, kDaylight[1] * m.flameLight,
                                   kDaylight[2] * m.flameLight};
            submit(effects, fireGroups_[1].triangles, fireGroups_[1].sheet,
                   fireGroups_[1].blend, at, lean, 0.0f, m.size, cone, 1.0f);
        }
    }

    // The fireballs: the rock alone, white, at its own roll -- no cone (subtype 1's
    // `BlendMeshLight = 0`, and a yaw cannot lay the cone's vertical column down anyway).
    for (const auto& f : fireballs_) {
        if (!f.alive || fireGroupCount_ == 0) continue;
        const float white[3] = {1.0f, 1.0f, 1.0f};
        submit(effects, fireGroups_[0].triangles, fireGroups_[0].sheet, fireGroups_[0].blend,
               f.at, f.tumble, f.tumble * 0.7f, f.size, white, 1.0f);
        if (fireGroupCount_ > 1) {
            // The cone's Y is laid back along the flight; X is level and across it; Z completes
            // the turn. A ball flying straight up or down has no level across, and takes X.
            const float back[3] = {-f.along[0], -f.along[1], -f.along[2]};
            // **The tail grows as it flies.** At full length it is three metres behind the ball,
            // and a ball just out of his hand drew its fire back through him and out behind him
            // (the user, 2026-09-28: "looks like fireball trail is behind character"). So the
            // cone is squeezed to the ground it has covered, never shorter than a tenth.
            const float full = (kFlameAhead + kFlameBehind) * kFlameStretch * f.size;
            const float grown = std::clamp((f.travelled + kFlameLeads * f.size) / full, 0.1f, 1.0f);
            const float stretch = kFlameStretch * grown;
            const float drawn[3] = {back[0] * stretch, back[1] * stretch, back[2] * stretch};
            float across[3] = {back[2], 0.0f, -back[0]};
            float wide = std::sqrt(across[0] * across[0] + across[2] * across[2]);
            if (wide < 1e-3f) {
                across[0] = 1.0f;
                across[2] = 0.0f;
                wide = 1.0f;
            }
            across[0] /= wide;
            across[2] /= wide;
            const float third[3] = {across[1] * back[2] - across[2] * back[1],
                                    across[2] * back[0] - across[0] * back[2],
                                    across[0] * back[1] - across[1] * back[0]};
            const float cone[3] = {kDaylight[0] * f.flameLight * kFireFlame,
                                   kDaylight[1] * f.flameLight * kFireFlame,
                                   kDaylight[2] * f.flameLight * kFireFlame};
            const float shift = (kFlameAhead * stretch - kFlameLeads) * f.size;
            const float from[3] = {f.at[0] + back[0] * shift, f.at[1] + back[1] * shift,
                                   f.at[2] + back[2] * shift};
            submitEffectAlong(effects, fireGroups_[1].triangles, fireGroups_[1].sheet,
                        fireGroups_[1].blend, from, across, drawn, third, f.size, cone, 1.0f);
            for (int g = 0; g < kFlameGhosts; ++g) {
                const float w = kGhostWide[g];
                const float ax[3] = {across[0] * w, across[1] * w, across[2] * w};
                const float az[3] = {third[0] * w, third[1] * w, third[2] * w};
                const float dim[3] = {cone[0] * kGhostLight[g], cone[1] * kGhostLight[g],
                                      cone[2] * kGhostLight[g]};
                submitEffectAlong(effects, fireGroups_[1].triangles, fireGroups_[1].sheet,
                            fireGroups_[1].blend, from, ax, drawn, az, f.size, dim, 1.0f);
            }
        }
        if (!bgfx::isValid(glowSheet_)) continue;
        // Drawn IN FRONT of the rock, half a metre toward the eye: the flare's light is in its
        // middle, and at the rock's own centre the alpha-blended stone sorted over exactly that
        // part and the fire was invisible. Burning is on the outside of a thing.
        float toward[3] = {0.0f, 0.0f, 0.0f};
        if (eye != nullptr) {
            const float d[3] = {eye[0] - f.at[0], eye[1] - f.at[1], eye[2] - f.at[2]};
            const float far = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (far > 1e-3f) for (int a = 0; a < 3; ++a) toward[a] = d[a] / far * kGlowForward;
        }
        for (int layer = 0; layer < 2; ++layer) {
            gfx::Sprite glow;
            for (int a = 0; a < 3; ++a) glow.position[a] = f.at[a] + toward[a];
            const float wide = (layer == 0 ? kHaloWide : kHeartWide) * f.size;
            glow.halfWidth = glow.halfHeight = wide * 0.5f;
            glow.spin = f.left * 0.4f;
            const float* tint = layer == 0 ? kHalo : kHeart;
            for (int c = 0; c < 3; ++c) glow.colour[c] = tint[c] * f.bodyLight;
            glow.sheet = glowSheet_;
            glow.blend = gfx::Blend::Additive;
            effects.add(glow);
        }
    }

    for (const auto& s : stones_) {
        if (!s.alive) continue;
        const Group& g = stoneGroups_[s.which];
        const float white[3] = {1.0f, 1.0f, 1.0f};
        submit(effects, g.triangles, g.sheet, g.blend, s.position, s.lean[0], s.lean[1],
               s.size, white, 1.0f - s.fade);
    }

    for (const auto& m : motes_) {
        if (!m.alive) continue;
        gfx::Sprite sprite;
        for (int a = 0; a < 3; ++a) sprite.position[a] = m.position[a];
        sprite.halfWidth = sprite.halfHeight = m.size * 0.5f;
        sprite.spin = m.spin;
        for (int c = 0; c < 3; ++c) sprite.colour[c] = m.colour[c];
        sprite.colour[3] = 1.0f;
        if (m.cools && m.born > 0.0f) {
            // From its own orange toward the meteor's red, and down to nothing, over its life.
            // The square root, so they hold their heat for longer before going out and the
            // trail of sparks runs further back behind the flame.
            const float life = std::sqrt(std::max(0.0f, m.left / m.born));
            for (int c = 0; c < 3; ++c) {
                sprite.colour[c] = (kGlow[c] + (m.colour[c] - kGlow[c]) * life) * life;
            }
        }
        sprite.blend = gfx::Blend::Additive;
        if (m.kind == Mote::Kind::Smoke) {
            // In quickly under the flash, out over the last two thirds.
            const float t = 1.0f - m.left / m.born;
            const float in = std::min(1.0f, t / 0.12f);
            const float out = 1.0f - std::clamp((t - 0.33f) / 0.67f, 0.0f, 1.0f);
            sprite.colour[3] = kSmokeAlpha * m.opacity * in * out;
            sprite.sheet = smokeSheet_;
            sprite.blend = gfx::Blend::Smoke;
            effects.add(sprite);
            continue;
        }
        if (m.kind == Mote::Kind::Ember) {
            sprite.sheet = m.blue ? blueEmberSheet_ : emberSheet_;
            // A 256x64 strip of four square cells, one every six frames: MU's
            // `Frame = (23 - LifeTime) / 6`.
            const int cell = std::clamp(int((m.born - 1.0f - m.left) / float(kEmberHeld)), 0,
                                        kEmberCells - 1);
            sprite.u0 = float(cell) / float(kEmberCells);
            sprite.u1 = sprite.u0 + 1.0f / float(kEmberCells);
            sprite.v0 = 0.0f;
            sprite.v1 = 1.0f;
        } else {
            sprite.sheet = blastSheet_;
            // Explotion01's 4x4 grid walked one cell every two frames, so ten of its sixteen
            // are ever seen: `o->Frame = (20 - LifeTime) / 2`, in integers.
            const int step =
                std::clamp(int((m.born - m.left) / float(kBlastHeld)), 0, kBlastLastCell);
            const float side = 1.0f / float(kBlastGrid);
            sprite.u0 = float(step % kBlastGrid) * side + kBlastInset;
            sprite.v0 = float(step / kBlastGrid) * side + kBlastInset;
            sprite.u1 = sprite.u0 + side - kBlastInset * 2.0f;
            sprite.v1 = sprite.v0 + side - kBlastInset * 2.0f;
        }
        if (bgfx::isValid(sprite.sheet)) effects.add(sprite);
    }
}

uint32_t Meteor::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    // The rocks first and the fireballs after them, so that when more is burning than the
    // frame will carry, what is lost is the light of a blast already on the ground rather
    // than that of a rock still coming down.
    for (const auto& m : meteors_) {
        if (!m.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        light.position[0] = m.x;
        light.position[1] = m.y;
        light.position[2] = m.z;
        // `AddTerrainLight(..., 2, ...)`: two tiles, which is two metres here.
        light.reach = kGlowTiles * m.weight * (m.shower ? kShowerReach : 1.0f);
        // How far it hangs over the ground under it. The rock is four metres up when it is
        // thrown and on the floor when it lands, so this is what it has fallen to -- without
        // it the light would be flat on the ground the whole way down and the pool would not
        // tighten as the rock came in.
        light.height = std::max(0.0f, m.y - m.floorY);
        // The deep orange-red, dimmed by the frame's own body roll. It is the ONE place that
        // colour belongs besides the embers: over the flame cone it crushes the sheet toward
        // black.
        const float share = m.shower ? kShowerLight : 1.0f;
        for (int c = 0; c < 3; ++c) light.colour[c] = kGlow[c] * m.bodyLight * share;
    }

    // The fire on a wizard calling one down: his chest, three tiles, flickering.
    if (burnLit_ > 0.0f && count < max) {
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = burnAt_[k];
        light.reach = kBurnGlowTiles;
        light.height = 1.0f;
        const float* glow = burnBlue_ ? kBurnBlueGlow : kBurnGlow;
        for (int c = 0; c < 3; ++c) light.colour[c] = glow[c] * burnRoll_;
    }

    // And the wizard's fireballs in the air, MU's `AddTerrainLight` on the same deep orange-red
    // the rock throws, flickering with its body roll -- and wider than the meteor's two tiles on
    // the user's word (2026-09-28, "most of DW spells are light emitters"), so the ball carries a
    // pool of light across the ground as it goes.
    for (const auto& f : fireballs_) {
        if (!f.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int a = 0; a < 3; ++a) light.position[a] = f.at[a];
        light.reach = kHurlGlowTiles;
        const float under = ground_ ? ground_->heightAt(f.at[0], f.at[2]) : f.floorY;
        light.height = std::max(0.0f, f.at[1] - under);
        for (int c = 0; c < 3; ++c) light.colour[c] = kHurlGlow[c] * f.bodyLight;
    }

    // And the fireball, which lights the ground it is standing on. This is the particle's own
    // and not the skill's: `BITMAP_EXPLOTION` feeds the terrain light every frame it lives --
    // `Luminosity = LifeTime / 20` and
    // `AddTerrainLight(x, y, (L*0.5, L*0.3, L*0.1), 4)` (ZzzEffectParticle.cpp:4264-4268) --
    // so it is a WARMER and much WIDER light than the rock's: four tiles against two, orange
    // rather than red, and falling off linearly to nothing over its four fifths of a second
    // rather than flickering.
    //
    // Read only from the meteor's side this looks like a skill that creates no light when it
    // lands, and it was written that way first. The blast is where it comes from.
    for (const auto& m : motes_) {
        if (!m.alive || m.kind != Mote::Kind::Blast || count >= max) continue;
        gfx::PointLight& light = out[count++];
        for (int a = 0; a < 3; ++a) light.position[a] = m.position[a];
        // A shower's dimmer blast lights less, by its heat (1 for every other).
        light.reach = kBlastGlowTiles * (m.lit < 1.0f ? kShowerReach : 1.0f);
        light.height = kBlastLift * kUnit;
        const float luminosity = m.born > 0.0f ? m.left / m.born : 0.0f;
        for (int c = 0; c < 3; ++c) light.colour[c] = kBlastGlow[c] * luminosity * m.colour[0] * m.lit;
    }
    return count;
}

uint32_t Meteor::liveMeteors() const {
    uint32_t count = 0;
    for (const auto& m : meteors_) if (m.alive) ++count;
    return count;
}

uint32_t Meteor::liveFireballs() const {
    uint32_t count = 0;
    for (const auto& f : fireballs_) if (f.alive) ++count;
    return count;
}

uint32_t Meteor::liveStones() const {
    uint32_t count = 0;
    for (const auto& s : stones_) if (s.alive) ++count;
    return count;
}

uint32_t Meteor::liveMotes() const {
    uint32_t count = 0;
    for (const auto& m : motes_) if (m.alive) ++count;
    return count;
}

}  // namespace mu::game
