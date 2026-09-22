#include "game/meteor.h"

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

bool Meteor::loadObj(const std::string& path, float scale, const std::string& groupFilter,
                     std::vector<Corner>& out) {
    const std::vector<uint8_t> bytes = core::readFile(path);
    if (bytes.empty()) {
        core::logError("meteor: no %s", path.c_str());
        return false;
    }
    std::vector<float> at, uv;
    std::string line;
    std::string currentGroup;
    const auto corner = [&](const char* token) {
        int v = 0, t = 0;
        if (std::sscanf(token, "%d/%d", &v, &t) < 1) return;
        if (v <= 0 || size_t(v) * 3 > at.size()) return;
        Corner c;
        c.x = at[size_t(v - 1) * 3 + 0] * scale;
        c.y = at[size_t(v - 1) * 3 + 1] * scale;
        // MU's Y runs south and this engine's Z runs north: docs/conventions.md, and the one
        // negation belongs here at the edge rather than in the motion.
        c.z = -at[size_t(v - 1) * 3 + 2] * scale;
        c.u = t > 0 && size_t(t) * 2 <= uv.size() ? uv[size_t(t - 1) * 2] : 0.5f;
        c.v = t > 0 && size_t(t) * 2 <= uv.size() ? 1.0f - uv[size_t(t - 1) * 2 + 1] : 0.5f;
        out.push_back(c);
    };
    for (size_t i = 0; i <= bytes.size(); ++i) {
        if (i < bytes.size() && bytes[i] != '\n') {
            line.push_back(char(bytes[i]));
            continue;
        }
        float a = 0, b = 0, c = 0;
        char p[3][32];
        if (std::sscanf(line.c_str(), "v %f %f %f", &a, &b, &c) == 3) {
            at.insert(at.end(), {a, b, c});
        } else if (std::sscanf(line.c_str(), "vt %f %f", &a, &b) == 2) {
            uv.insert(uv.end(), {a, b});
        } else if (line.size() > 2 && line[0] == 'g' && line[1] == ' ') {
            currentGroup = line.substr(2);
            while (!currentGroup.empty() &&
                   (currentGroup.back() == '\r' || currentGroup.back() == ' ')) {
                currentGroup.pop_back();
            }
        } else if (std::sscanf(line.c_str(), "f %31s %31s %31s", p[0], p[1], p[2]) == 3) {
            if (groupFilter.empty() || currentGroup == groupFilter) {
                const size_t before = out.size();
                for (auto& token : p) corner(token);
                if (out.size() != before + 3) out.resize(before);
            }
        }
        line.clear();
    }
    return !out.empty();
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
    if (loadObj(dir + "Fire01.obj", kUnit, "fire02", fireGroups_[0].triangles)) {
        fireGroups_[0].sheet = raw("fire02.png");
        fireGroups_[0].blend = gfx::Blend::Alpha;
        fireGroupCount_ = 1;
    }
    if (loadObj(dir + "Fire01.obj", kUnit, "fire01", fireGroups_[1].triangles)) {
        fireGroups_[1].sheet = raw("fire01.png");
        fireGroups_[1].blend = gfx::Blend::Additive;
        fireGroupCount_ = 2;
    }
    for (int s = 0; s < 2; ++s) {
        const std::string name = s == 0 ? "Stone01.obj" : "Stone02.obj";
        if (loadObj(dir + name, kUnit, "", stoneGroups_[s].triangles)) {
            stoneGroups_[s].sheet = raw("fire02.png");
            stoneGroups_[s].blend = gfx::Blend::Alpha;
        }
    }
    blastSheet_ = cooked("explosion");
    emberSheet_ = cooked("fire");

    int fireTris = 0;
    for (int g = 0; g < fireGroupCount_; ++g) {
        fireTris += int(fireGroups_[g].triangles.size()) / 3;
    }
    core::logf("meteor: Fire01 %d triangles (%d groups), Stone01 %zu, Stone02 %zu, "
               "blast %s, embers %s",
               fireTris, fireGroupCount_, stoneGroups_[0].triangles.size() / 3,
               stoneGroups_[1].triangles.size() / 3,
               bgfx::isValid(blastSheet_) ? "yes" : "NO", bgfx::isValid(emberSheet_) ? "yes" : "NO");
    return fireGroupCount_ > 0;
}

void Meteor::shutdown() {
    for (auto& g : fireGroups_) g.triangles.clear();
    for (auto& g : stoneGroups_) g.triangles.clear();
    fireGroupCount_ = 0;
    for (auto& m : meteors_) m.alive = false;
    for (auto& s : stones_) s.alive = false;
    for (auto& m : motes_) m.alive = false;
    quake_ = 0.0f;
}

void Meteor::cast(float targetX, float targetZ, uint32_t attacker) {
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
    slot->size = between(kSmallestRock, kLargestRock);
    slot->floorY = ground_->heightAt(targetX, targetZ);
    slot->x = targetX + (kSideways + float(roll() % uint32_t(kSidewaysSpread))) * kUnit;
    slot->y = slot->floorY + kLift * kUnit;
    slot->z = targetZ;
    // Metres a second: one factor of 25 on a speed, and the sideways one is negative because
    // the heading points back at the target the offset put it beside.
    slot->driftX = -std::sin(radians) * kFallSpeed * kUnit * kReferenceFps;
    slot->fallY = -std::cos(radians) * kFallSpeed * kUnit * kReferenceFps;
    slot->flown = 0.0f;
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
    if (!bgfx::isValid(emberSheet_)) return;
    Mote* mote = freeMote();
    if (mote == nullptr) return;
    mote->alive = true;
    mote->kind = Mote::Kind::Ember;
    mote->position[0] = rock.x;
    mote->position[1] = rock.y;
    mote->position[2] = rock.z;
    // Along the rock's own travel, at (rand()%16+32)*0.1 units a frame.
    const float speed = between(kSlowestDrift, kFastestDrift) * kUnit * kReferenceFps;
    const float heading = std::sqrt(rock.driftX * rock.driftX + rock.fallY * rock.fallY);
    mote->velocity[0] = heading > 0.0f ? rock.driftX / heading * speed : 0.0f;
    mote->velocity[1] = heading > 0.0f ? rock.fallY / heading * speed : 0.0f;
    mote->velocity[2] = 0.0f;
    mote->size = between(kSmallestEmber, kLargestEmber) * kEmberSheetUnits * kUnit;
    mote->spin = unit() * kTwoPi;
    mote->left = mote->born = kEmberFrames;
    mote->rise = 0.0f;
    // The frame's own body light, HELD for the whole life: an ember does not fade its colour,
    // what shrinks is its size.
    for (int c = 0; c < 3; ++c) mote->colour[c] = kGlow[c] * rock.bodyLight;
}

void Meteor::land(const Live& rock) {
    const float floor = rock.floorY;

    // 1. The six stones, on MU's shared ballistic arm -- the one its bones and its broken ice
    //    ride too. Each rolls its own size, its own gravity and its own scatter; none of them
    //    takes the meteor's 1.0-1.7, which would put a field of boulders on the grass.
    for (int s = 0; s < 6; ++s) {
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
        slot->size = between(kSmallestStone, kLargestStone);
        // An ACCELERATION: units a frame SQUARED, so two factors of 25 and not one. Read as a
        // velocity it comes out twenty-five times too small and the debris drifts in slow
        // motion, weightless.
        slot->gravity = (kLeastGravity + float(roll() % uint32_t(kMoreGravity))) * 0.1f * kUnit *
                        kReferenceFps * kReferenceFps;
        const float scatter = (kLeastScatter + float(roll() % uint32_t(kMoreScatter))) * 0.1f *
                              kUnit * kReferenceFps;
        const float yaw = unit() * kTwoPi;
        slot->position[0] = rock.x;
        slot->position[1] = floor;  // a stone's own lift is nought: they start on the floor
        slot->position[2] = rock.z;
        slot->velocity[0] = std::sin(yaw) * scatter;
        slot->velocity[1] = 0.0f;  // flat, and the gravity is what lifts nothing and drops it
        slot->velocity[2] = std::cos(yaw) * scatter;
        slot->lean[0] = unit() * kTwoPi;
        slot->lean[1] = unit() * kTwoPi;
    }

    // 2. The blast, and it is asked for after the stones so that a nearly full pool spends its
    //    last slots on the debris rather than on one sprite.
    if (bgfx::isValid(blastSheet_)) {
        if (Mote* mote = freeMote()) {
            mote->alive = true;
            mote->kind = Mote::Kind::Blast;
            mote->position[0] = rock.x;
            mote->position[1] = floor + kBlastLift * kUnit;
            mote->position[2] = rock.z;
            for (int a = 0; a < 3; ++a) mote->velocity[a] = 0.0f;
            mote->size = kBlastUnits * kUnit;
            mote->spin = 0.0f;
            mote->left = mote->born = kBlastFrames;
            mote->rise = 0.0f;
            // White. MU's colour argument here is uninitialised stack memory, so there is
            // nothing to port, and tinting it with the trail's red takes the heat out of an
            // already orange core.
            for (int c = 0; c < 3; ++c) mote->colour[c] = 1.0f;
        }
    }
}

void Meteor::update(float seconds, std::vector<Impact>& impacts) {
    const float refFrames = seconds * kReferenceFps;

    for (auto& m : meteors_) {
        if (!m.alive) continue;
        m.x += m.driftX * seconds;
        m.y += m.fallY * seconds;
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
        m.flown += std::sqrt(m.driftX * m.driftX + m.fallY * m.fallY) * seconds;
        while (m.flown >= kEmberSpacingUnits * kUnit) {
            m.flown -= kEmberSpacingUnits * kUnit;
            ember(m);
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
            quake_ = float(int(roll() % 4) - 4) * 0.1f;
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
                (sprite.corner[0][a] + sprite.corner[1][a] + sprite.corner[2][a]) / 3.0f;
        }
        effects.add(sprite);
    }
}

void Meteor::gather(gfx::Effects& effects) const {
    // The rock, leaning the 20 degrees it was thrown with and never turning after: MU writes
    // `o->Angle` once at the spawn and never again.
    const float lean = -kEntryDegrees * kPi / 180.0f;
    for (const auto& m : meteors_) {
        if (!m.alive) continue;
        const float at[3] = {m.x, m.y, m.z};
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
        sprite.blend = gfx::Blend::Additive;
        if (m.kind == Mote::Kind::Ember) {
            sprite.sheet = emberSheet_;
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
            const int step = std::clamp(int((m.born - m.left) / float(kBlastHeld)), 0,
                                        kBlastGrid * kBlastGrid - 1);
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
    for (const auto& m : meteors_) {
        if (!m.alive || count >= max) continue;
        gfx::PointLight& light = out[count++];
        light.position[0] = m.x;
        light.position[1] = m.y;
        light.position[2] = m.z;
        // `AddTerrainLight(..., 2, ...)`: two tiles, which is two metres here.
        light.reach = kGlowTiles;
        // How far it hangs over the ground under it. The rock is four metres up when it is
        // thrown and on the floor when it lands, so this is what it has fallen to -- without
        // it the light would be flat on the ground the whole way down and the pool would not
        // tighten as the rock came in.
        light.height = std::max(0.0f, m.y - m.floorY);
        // The deep orange-red, dimmed by the frame's own body roll. It is the ONE place that
        // colour belongs besides the embers: over the flame cone it crushes the sheet toward
        // black.
        for (int c = 0; c < 3; ++c) light.colour[c] = kGlow[c] * m.bodyLight;
    }
    return count;
}

uint32_t Meteor::liveMeteors() const {
    uint32_t count = 0;
    for (const auto& m : meteors_) if (m.alive) ++count;
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
