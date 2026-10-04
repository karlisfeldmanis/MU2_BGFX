#include "game/fx/arrow.h"

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"

namespace mu::game {

namespace {

constexpr float kTwoPi = 6.28318530718f;

void normalise(float v[3]) {
    const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length < 1e-6f) return;
    for (int k = 0; k < 3; ++k) v[k] /= length;
}

}  // namespace

bool Arrows::open(const std::string& assetDir, content::Textures& textures,
                  const content::Showing& table, float metresPerTile) {
    metresPerTile_ = metresPerTile > 0.0f ? metresPerTile : 1.0f;
    const std::string dir = assetDir + "/effects/missiles/";
    const auto sheet = [&](const char* name) -> bgfx::TextureHandle {
        const std::string path = dir + name;
        if (!core::fileExists(path)) {
            core::logError("arrows: no %s", path.c_str());
            return BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    // One part a mesh group, each with the sheet and the blend its JSON names. "Opaque" is the
    // alpha blend on a sheet with no alpha, as the meteor's rock is drawn.
    struct Recipe {
        Model model;
        const char* obj;
        const char* group;
        const char* sheet;
        gfx::Blend blend;
        bool flipped = false;
    };
    static const Recipe kRecipes[] = {
        {Wood, "Arrow01.obj", "arrow", "arrow.png", gfx::Blend::Alpha, true},
        {Wood, "Arrow01.obj", "fire01", "fire01.png", gfx::Blend::Additive},
        {Steel, "ArrowSteel01.obj", "", "bow_b.png", gfx::Blend::Alpha},
        {Saw, "ArrowSaw01.obj", "", "toothed_whee.png", gfx::Blend::Alpha},
        {Laser, "ArrowLaser01.obj", "", "bow_c.png", gfx::Blend::Additive},
    };
    int loaded = 0;
    for (const Recipe& r : kRecipes) {
        Part part;
        if (!loadEffectObj(dir + r.obj, kUnit, r.group, part.triangles)) {
            core::logError("arrows: %s (%s) did not load", r.obj, r.group);
            continue;
        }
        part.sheet = sheet(r.sheet);
        part.blend = r.blend;
        part.flipped = r.flipped;
        shapes_[r.model].parts.push_back(std::move(part));
        shapes_[r.model].reversed = r.model == Steel;
        ++loaded;
    }
    const content::EffectSheet* fire = table.effect("fire");
    if (fire != nullptr) {
        emberSheet_ = textures.load(assetDir + "/" + fire->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* grey = table.effect("fire_grey")) {
        greyFireSheet_ = textures.load(assetDir + "/" + grey->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* smoke = table.effect("smoke")) {
        smokeSheet_ = textures.load(assetDir + "/" + smoke->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* light = table.effect("light")) {
        glintSheet_ = textures.load(assetDir + "/" + light->path, content::TextureRole::Albedo);
    }
    if (const content::EffectSheet* pierce = table.effect("pierce")) {
        bandSheet_ = textures.load(assetDir + "/" + pierce->path, content::TextureRole::Albedo);
    }
    core::logf("arrows: %d parts of 5, embers %s, smoke %s, bolt trail %s", loaded,
               bgfx::isValid(emberSheet_) ? "yes" : "NO",
               bgfx::isValid(smokeSheet_) ? "yes" : "NO",
               bgfx::isValid(glintSheet_) ? "yes" : "NO");
    return loaded > 0;
}

void Arrows::shutdown() {
    for (Shape& shape : shapes_) shape.parts.clear();
    for (Shot& one : shots_) one.alive = false;
    for (Ember& one : embers_) one.alive = false;
    for (Lick& one : licks_) one.alive = false;
    for (Wisp& one : wisps_) one.alive = false;
    for (Glint& one : glints_) one.alive = false;
    for (Band& one : bands_) one.alive = false;
}

Arrows::Model Arrows::modelFor(int32_t group, int32_t number) {
    if (group != 4) return Wood;
    switch (number) {
        case 8:   // Crossbow
        case 9:   // Golden Crossbow
            return Steel;
        case 10:  // Arquebus
            return Saw;
        case 11:  // Light Crossbow
            return Laser;
        default:
            // Ours: the Serpent, Bluewing and Aquagold crossbows throw bolts not built here.
            return number >= 8 ? Steel : Wood;
    }
}

const float* Arrows::tintFor(int32_t group, int32_t number) {
    if (group != 4) return nullptr;
    switch (number) {
        case 5: return kSilverFire;       // Silver Bow
        case 6: return kNatureFire;       // Chaos Nature Bow
        case 12: return kSerpentStreak;   // Serpent Crossbow
        case 13: return kBluewingStreak;  // Bluewing Crossbow
        default: return nullptr;
    }
}

float Arrows::roll() {
    dice_ = dice_ * 1664525u + 1013904223u;
    return float(dice_ >> 8) / float(1u << 24);
}

void Arrows::loose(const float from[3], const float to[3], uint32_t whom, Model model,
                   uint32_t shooter, const float* tint, float seconds, bool pierce) {
    Shot* shot = nullptr;
    for (Shot& one : shots_) {
        if (!one.alive) {
            shot = &one;
            break;
        }
    }
    if (shot == nullptr) return;
    *shot = Shot{};
    shot->alive = true;
    shot->model = model;
    shot->whom = whom;
    shot->shooter = shooter;
    for (int k = 0; k < 3; ++k) {
        shot->at[k] = from[k];
        shot->to[k] = to[k];
        shot->along[k] = to[k] - from[k];
    }
    normalise(shot->along);
    shot->left = kFrames;
    shot->flown = 0.0f;
    shot->licked = kLickSpacing;  // one at the muzzle
    shot->smoked = 0.0f;
    shot->glinted = kGlintSpacing;
    shot->chipped = 0.0f;
    shot->travelled = 0.0f;
    shot->speed = kTilesASecond * metresPerTile_;
    if (seconds > 0.0f) {
        // Over the flat way to the tile short of it, never slower than the realm's own.
        const float dx = to[0] - from[0], dz = to[2] - from[2];
        const float way = std::sqrt(dx * dx + dz * dz) - kStopsShort * metresPerTile_;
        shot->speed = std::max(shot->speed, way / seconds);
    }
    shot->tinted = tint != nullptr;
    for (int k = 0; k < 3; ++k) shot->tint[k] = tint ? tint[k] : 1.0f;
    shot->glow = 0.7f + 0.1f * float(int(roll() * 4.0f));
    shot->pierce = pierce;
    if (pierce) wind(int(shot - shots_));
}

void Arrows::wind(int shot) {
    if (!bgfx::isValid(bandSheet_)) return;
    int made = 0;
    for (Band& band : bands_) {
        if (band.alive) continue;
        band = Band{};
        band.alive = true;
        band.shot = shot;
        band.turn = kBandPhases[made];
        band.gold = made % 2 == 0;
        lay(band, shots_[shot], 0.0f);  // its first pair at the muzzle
        if (++made == 4) return;
    }
}

void Arrows::lay(Band& band, const Shot& shot, float back) {
    // The line it winds round, `back` metres behind the head; level across the flight, and the
    // rest of the way round, as the arrow's own drawing lays them (gather).
    const float up[3] = {0.0f, 1.0f, 0.0f};
    float across[3] = {up[1] * shot.along[2] - up[2] * shot.along[1],
                       up[2] * shot.along[0] - up[0] * shot.along[2],
                       up[0] * shot.along[1] - up[1] * shot.along[0]};
    normalise(across);
    float lift[3] = {shot.along[1] * across[2] - shot.along[2] * across[1],
                     shot.along[2] * across[0] - shot.along[0] * across[2],
                     shot.along[0] * across[1] - shot.along[1] * across[0]};
    normalise(lift);
    const float angle = band.turn * kTwoPi / 360.0f;
    if (band.count == kBandTails) {
        for (int i = 1; i < kBandTails; ++i) {
            for (int k = 0; k < 3; ++k) {
                band.axis[i - 1][k] = band.axis[i][k];
                band.radial[i - 1][k] = band.radial[i][k];
                band.across[i - 1][k] = band.across[i][k];
            }
        }
        --band.count;
    }
    // The radius is the drawing's, by where the pair now sits in the spiral (gather).
    for (int k = 0; k < 3; ++k) {
        band.axis[band.count][k] = shot.at[k] - shot.along[k] * back;
        band.radial[band.count][k] = std::cos(angle) * across[k] + std::sin(angle) * lift[k];
        band.across[band.count][k] = across[k];
    }
    ++band.count;
}

void Arrows::update(float seconds, const std::function<bool(uint32_t, float*)>& middle) {
    const float frames = seconds * kReference;
    const float spacing = kEmberSpacingUnits * kUnit;
    landed_.clear();
    for (Shot& shot : shots_) {
        if (!shot.alive) continue;
        shot.left -= frames;
        if (shot.fading > 0.0f) {
            shot.fading -= frames;
            if (shot.fading <= 0.0f) {
                shot.alive = false;
                continue;
            }
        } else if (shot.left <= 0.0f) {
            if (!shot.pierce) {
                shot.alive = false;
                continue;
            }
            shot.fading = kPierceFade;
        }
        // Straight on the line it was loosed on, as MU's flies (the user, 2026-10-04: 'arrow
        // still change suddenly angle to monster during flying'). The body is followed for
        // where it ends, not for which way it points: re-aimed each frame at a middle that walks
        // and bobs, it swung round as it neared it.
        float seen[3];
        if (middle && middle(shot.whom, seen)) {
            for (int k = 0; k < 3; ++k) shot.to[k] = seen[k];
        }
        const float step = shot.speed * seconds;
        for (int k = 0; k < 3; ++k) shot.at[k] += shot.along[k] * step;
        shot.travelled += step;
        const bool clear = shot.travelled - kWispBehind >= kWispFromMuzzle;
        shot.glow = 0.7f + 0.1f * float(int(roll() * 4.0f));
        if (shot.pierce) {
            // No fire: the bands and the thin smoke are its whole wake.
            if (clear) shot.smoked += step;
            while (shot.smoked >= kWispSpacing) {
                shot.smoked -= kWispSpacing;
                smoke(shot);
            }
        } else if (shot.model == Wood) {
            shot.flown += step;
            while (shot.flown >= spacing) {
                shot.flown -= spacing;
                shed(shot);
            }
            shot.licked += step;
            while (shot.licked >= kLickSpacing) {
                shot.licked -= kLickSpacing;
                lick(shot);
            }
            if (clear) shot.smoked += step;
            while (shot.smoked >= kWispSpacing) {
                shot.smoked -= kWispSpacing;
                smoke(shot);
            }
        } else if (shot.model == Steel || shot.model == Saw) {
            shot.glinted += step;
            // Each laid where the head was when it was due, back along this frame's way, or a
            // frame's worth bunch at its end and the streak reads as beads.
            while (shot.glinted >= kGlintSpacing) {
                shot.glinted -= kGlintSpacing;
                glint(shot, false, shot.glinted);
            }
            shot.chipped += step;
            while (shot.chipped >= kSparkSpacing) {
                shot.chipped -= kSparkSpacing;
                glint(shot, true, shot.chipped);
            }
            // And the wooden arrow's thin grey smoke behind the streak (the user: 'minimal
            // smoke effect similar like for arrows').
            if (clear) shot.smoked += step;
            while (shot.smoked >= kWispSpacing) {
                shot.smoked -= kWispSpacing;
                smoke(shot);
            }
        }
        // A fan's arrow a tile short of its far point, on the ground plane: CheckClientArrow.
        // **Ours:** a shot at a body goes into it -- ends at its middle, or once past it --
        // where MU's tile short ended a shot at a body beside her on the frame it left the
        // string, and it never flew.
        const float dx = shot.to[0] - shot.at[0], dz = shot.to[2] - shot.at[2];
        bool arrived = std::sqrt(dx * dx + dz * dz) <= kStopsShort * metresPerTile_;
        if (shot.whom != 0) {
            const float dy = shot.to[1] - shot.at[1];
            const float left = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float ahead = dx * shot.along[0] + dy * shot.along[1] + dz * shot.along[2];
            arrived = left <= kIntoBody || ahead <= 0.0f;
        }
        if (arrived && shot.fading <= 0.0f) {
            if (shot.shooter != 0) landed_.push_back(shot.shooter);
            // Penetration's flies on past the end of its way, fading, straight on.
            if (shot.pierce) {
                shot.fading = kPierceFade;
                shot.whom = 0;
                for (int k = 0; k < 3; ++k) shot.to[k] = shot.at[k] + shot.along[k] * 1000.0f;
            } else {
                shot.alive = false;
            }
        }
    }
    // The bands: wound on while their arrow flies, a pair every thirty degrees of their turn,
    // each laid where the arrow was when it was due; then left in the air to dim.
    for (Band& band : bands_) {
        if (!band.alive) continue;
        if (band.shot >= 0 && !shots_[band.shot].alive) {
            band.shot = -1;
            band.age = std::max(band.age, kBandDimFrom);
        }
        // Held bright while its arrow flies -- which crosses the screen, past MU's thirty frames
        // -- and dimmed from the moment it is gone. ours.
        if (band.shot < 0) band.age += frames;
        if (band.age >= kBandFrames) {
            band.alive = false;
            continue;
        }
        if (band.shot < 0) continue;
        const Shot& shot = shots_[band.shot];
        const float perFrame = shot.speed / kReference;  // metres a reference frame
        band.due += frames;
        const float every = 1.0f / kBandTurnsAFrame;
        while (band.due >= every) {
            band.due -= every;
            band.turn += kBandTurn;
            lay(band, shot, band.due * perFrame);
        }
    }
    for (Ember& e : embers_) {
        if (!e.alive) continue;
        e.left -= frames;
        e.size -= kEmberShrink * kEmberSheetUnits * kUnit * kEmberShare * frames;
        if (e.left <= 0.0f || e.size <= 0.0f) {
            e.alive = false;
            continue;
        }
        e.rise += kEmberRise * frames;
        e.at[1] += e.rise * kEmberRiseScale * kUnit * frames;
        for (int k = 0; k < 3; ++k) e.at[k] += e.velocity[k] * seconds;
    }
    for (Lick& l : licks_) {
        if (!l.alive) continue;
        l.left -= frames;
        if (l.left <= 0.0f) {
            l.alive = false;
            continue;
        }
        for (int k = 0; k < 3; ++k) l.at[k] += l.velocity[k] * seconds;
    }
    for (Wisp& w : wisps_) {
        if (!w.alive) continue;
        w.age += frames;
        if (w.age >= kWispFrames) {
            w.alive = false;
            continue;
        }
        w.at[1] += kWispRise * seconds;
    }
    for (Glint& g : glints_) {
        if (!g.alive) continue;
        g.left -= frames;
        if (g.left <= 0.0f) {
            g.alive = false;
            continue;
        }
        if (g.spark) g.velocity[1] -= kSparkFall * seconds;
        for (int k = 0; k < 3; ++k) g.at[k] += g.velocity[k] * seconds;
    }
}

void Arrows::glint(const Shot& shot, bool spark, float back) {
    if (!bgfx::isValid(glintSheet_)) return;
    for (Glint& g : glints_) {
        if (g.alive) continue;
        g.alive = true;
        g.spark = spark;
        for (int k = 0; k < 3; ++k) {
            g.at[k] = shot.at[k] - shot.along[k] * ((spark ? 0.0f : kGlintBehind) + back);
            g.velocity[k] = 0.0f;
        }
        // The streak in the bolt's tone, its sparks half way from it to white.
        for (int k = 0; k < 3; ++k) {
            g.colour[k] = !shot.tinted ? (spark ? kSparkLight[k] : kGlintLight[k])
                          : spark      ? 0.5f * (shot.tint[k] + 1.0f)
                                       : shot.tint[k];
        }
        if (spark) {
            // Off either side and a little up, and on with a share of the bolt's way.
            for (int k = 0; k < 3; ++k) {
                g.velocity[k] = (roll() * 2.0f - 1.0f) * kSparkThrow + shot.along[k] * 1.5f;
            }
            g.velocity[1] = kSparkThrow * (0.3f + 0.7f * roll());
            g.size = kSparkSize * (0.7f + 0.6f * roll());
            g.frames = kSparkFrames * (0.7f + 0.6f * roll());
        } else {
            g.size = kSmallestGlint + (kLargestGlint - kSmallestGlint) * roll();
            g.frames = kGlintFrames;
        }
        g.spin = roll() * kTwoPi;
        g.left = g.frames;
        return;
    }
}

void Arrows::smoke(const Shot& shot) {
    if (!bgfx::isValid(smokeSheet_)) return;
    for (Wisp& w : wisps_) {
        if (w.alive) continue;
        w.alive = true;
        for (int k = 0; k < 3; ++k) {
            w.at[k] = shot.at[k] - shot.along[k] * kWispBehind + (roll() - 0.5f) * 0.03f;
        }
        w.spin = roll() * kTwoPi;
        w.age = 0.0f;
        return;
    }
}

void Arrows::lick(const Shot& shot) {
    if (!bgfx::isValid(emberSheet_)) return;
    for (Lick& l : licks_) {
        if (l.alive) continue;
        l.alive = true;
        for (int k = 0; k < 3; ++k) {
            l.at[k] = shot.at[k] - shot.along[k] * kLickBehind + (roll() - 0.5f) * 0.04f;
        }
        l.velocity[0] = (roll() * 2.0f - 1.0f) * kLickJitter;
        l.velocity[1] = kLickRise * (0.7f + 0.6f * roll());
        l.velocity[2] = (roll() * 2.0f - 1.0f) * kLickJitter;
        l.size = kSmallestLick + (kLargestLick - kSmallestLick) * roll();
        l.spin = roll() * kTwoPi;
        l.glow = shot.glow;
        l.grey = shot.tinted && bgfx::isValid(greyFireSheet_);
        for (int k = 0; k < 3; ++k) {
            l.colour[k] = l.grey ? shot.tint[k] * kGreyFireShare : kEmberLight[k];
        }
        l.left = kLickFrames;
        return;
    }
}

void Arrows::shed(const Shot& shot) {
    if (!bgfx::isValid(emberSheet_)) return;
    for (Ember& e : embers_) {
        if (e.alive) continue;
        e.alive = true;
        for (int k = 0; k < 3; ++k) e.at[k] = shot.at[k];
        // On after the arrow at (rand()%16+32)*0.1 units a frame.
        const float drift =
            (kSlowestDrift + (kFastestDrift - kSlowestDrift) * roll()) * kUnit * kReference;
        for (int k = 0; k < 3; ++k) e.velocity[k] = shot.along[k] * drift;
        e.size = (kSmallestEmber + (kLargestEmber - kSmallestEmber) * roll()) *
                 kEmberSheetUnits * kUnit * kEmberShare;
        e.spin = roll() * kTwoPi;
        e.rise = 0.0f;
        e.grey = shot.tinted && bgfx::isValid(greyFireSheet_);
        for (int k = 0; k < 3; ++k) {
            e.colour[k] = e.grey ? shot.tint[k] * kGreyFireShare : kEmberLight[k];
        }
        e.left = kEmberFrames;
        return;
    }
}

void Arrows::gather(gfx::Effects& effects) const {
    const float up[3] = {0.0f, 1.0f, 0.0f};
    for (const Shot& shot : shots_) {
        if (!shot.alive) continue;
        // The model's head is its loaded +Z (the fire sprite trails at -Z), so Z is laid along
        // the flight, X across it and Y the rest of the way round.
        float across[3] = {up[1] * shot.along[2] - up[2] * shot.along[1],
                           up[2] * shot.along[0] - up[0] * shot.along[2],
                           up[0] * shot.along[1] - up[1] * shot.along[0]};
        normalise(across);
        float lift[3] = {shot.along[1] * across[2] - shot.along[2] * across[1],
                         shot.along[2] * across[0] - shot.along[0] * across[2],
                         shot.along[0] * across[1] - shot.along[1] * across[0]};
        normalise(lift);
        const Shape& shape = shapes_[shot.model];
        float ahead[3] = {shot.along[0], shot.along[1], shot.along[2]};
        if (shape.reversed) {
            for (int k = 0; k < 3; ++k) {
                ahead[k] = -ahead[k];
                across[k] = -across[k];
            }
        }
        for (const Part& part : shape.parts) {
            if (!bgfx::isValid(part.sheet)) continue;
            if (shot.pierce && part.blend == gfx::Blend::Additive && shot.model == Wood) continue;
            // A toned arrow's added tail, multiplied to its colour; the shaft stays wood.
            float colour[3] = {1.0f, 1.0f, 1.0f};
            if (shot.tinted && part.blend == gfx::Blend::Additive && shot.model == Wood) {
                for (int k = 0; k < 3; ++k) colour[k] = shot.tint[k] * kTailTone;
            }
            float partAcross[3], partAhead[3];
            for (int k = 0; k < 3; ++k) {
                partAcross[k] = part.flipped ? -across[k] : across[k];
                partAhead[k] = part.flipped ? -ahead[k] : ahead[k];
            }
            const float fade = shot.fading > 0.0f ? shot.fading / kPierceFade : 1.0f;
            submitEffectAlong(effects, part.triangles, part.sheet, part.blend, shot.at,
                              partAcross, lift, partAhead, kScale, colour, fade);
        }
    }
    for (const Ember& e : embers_) {
        if (!e.alive) continue;
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = e.at[k];
        sprite.halfWidth = sprite.halfHeight = e.size * 0.5f;
        sprite.spin = e.spin;
        // Frame = (23 - LifeTime) / 6 across the strip's four cells.
        const int cell =
            std::clamp(int((kEmberFrames - 1.0f - e.left) / float(kEmberHeld)), 0, kEmberCells - 1);
        sprite.u0 = float(cell) / float(kEmberCells);
        sprite.u1 = float(cell + 1) / float(kEmberCells);
        // Held, not faded: what an ember loses is its size.
        for (int k = 0; k < 3; ++k) sprite.colour[k] = e.colour[k] * 0.85f;
        sprite.colour[3] = 1.0f;
        sprite.sheet = e.grey ? greyFireSheet_ : emberSheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    for (const Lick& l : licks_) {
        if (!l.alive) continue;
        const float life = l.left / kLickFrames;  // 1 at birth, 0 at the end
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = l.at[k];
        // Grows a little as it leaves the tail, then dims away.
        sprite.halfWidth = sprite.halfHeight = l.size * (0.75f + 0.25f * (1.0f - life)) * 0.5f;
        sprite.spin = l.spin;
        const int cell = std::clamp(int((1.0f - life) * float(kEmberCells)), 0, kEmberCells - 1);
        sprite.u0 = float(cell) / float(kEmberCells);
        sprite.u1 = float(cell + 1) / float(kEmberCells);
        for (int k = 0; k < 3; ++k) sprite.colour[k] = l.colour[k] * l.glow * life;
        sprite.colour[3] = 1.0f;
        sprite.sheet = l.grey ? greyFireSheet_ : emberSheet_;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    // The bolts' streak, shrinking and dimming behind the head; their sparks, held bright and
    // winking out at the end.
    for (const Glint& g : glints_) {
        if (!g.alive) continue;
        const float life = g.left / g.frames;  // 1 at birth, 0 at the end
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = g.at[k];
        sprite.spin = g.spin;
        if (g.spark) {
            sprite.halfWidth = sprite.halfHeight = g.size * 0.5f;
            const float lit = std::min(1.0f, life * 3.0f);
            for (int k = 0; k < 3; ++k) sprite.colour[k] = g.colour[k] * lit;
        } else {
            sprite.halfWidth = sprite.halfHeight = g.size * (0.4f + 0.6f * life) * 0.5f;
            for (int k = 0; k < 3; ++k) sprite.colour[k] = g.colour[k] * life;
        }
        sprite.sheet = glintSheet_;
        sprite.colour[3] = 1.0f;
        sprite.blend = gfx::Blend::Additive;
        effects.add(sprite);
    }
    // The bands, Flare02 along each and across its width, added at MU's half white; from fifteen
    // frames on dimmed by a third a frame, as the joint dims.
    for (const Band& band : bands_) {
        if (!band.alive || band.count < 2) continue;
        const float over = std::max(0.0f, band.age - kBandDimFrom);
        float light = kBandLight * std::pow(1.0f / kBandDim, over);
        // Dimming with its arrow while it flies on, fading.
        if (band.shot >= 0 && shots_[band.shot].fading > 0.0f) {
            light *= shots_[band.shot].fading / kPierceFade;
        }
        if (light < 0.01f) continue;
        const float half = kBandWidthUnits * kUnit * 0.5f;
        // Each pair's place: tight round the head, open at the back end.
        float middle[kBandTails][3];
        for (int i = 0; i < band.count; ++i) {
            const float t = band.count > 1 ? float(i) / float(band.count - 1) : 1.0f;
            const float radius = kBandRadiusUnits * kUnit *
                                 (kBandTailRadius + (kBandHeadRadius - kBandTailRadius) * t);
            for (int k = 0; k < 3; ++k) {
                middle[i][k] = band.axis[i][k] + band.radial[i][k] * radius;
            }
        }
        for (int i = 1; i < band.count; ++i) {
            gfx::Sprite sprite;
            sprite.placed = true;
            const float* a = middle[i - 1];
            const float* b = middle[i];
            const float* wa = band.across[i - 1];
            const float* wb = band.across[i];
            for (int k = 0; k < 3; ++k) {
                sprite.corner[0][k] = a[k] - wa[k] * half;
                sprite.corner[1][k] = a[k] + wa[k] * half;
                sprite.corner[2][k] = b[k] + wb[k] * half;
                sprite.corner[3][k] = b[k] - wb[k] * half;
                sprite.position[k] = 0.5f * (a[k] + b[k]);
            }
            const float u0 = float(i - 1) / float(band.count - 1);
            const float u1 = float(i) / float(band.count - 1);
            sprite.cornerUv[0][0] = u0;
            sprite.cornerUv[0][1] = 0.0f;
            sprite.cornerUv[1][0] = u0;
            sprite.cornerUv[1][1] = 1.0f;
            sprite.cornerUv[2][0] = u1;
            sprite.cornerUv[2][1] = 1.0f;
            sprite.cornerUv[3][0] = u1;
            sprite.cornerUv[3][1] = 0.0f;
            // Bright at the head, nothing at the back end: the spiral moves, it is not laid.
            const float along = float(i) / float(band.count - 1);
            for (int k = 0; k < 3; ++k) {
                sprite.colour[k] = light * along * (band.gold ? kBandGold[k] : 1.0f);
            }
            sprite.colour[3] = 1.0f;
            sprite.sheet = bandSheet_;
            sprite.blend = gfx::Blend::Additive;
            effects.add(sprite);
        }
    }
    // The smoke, smoke02 read as grey (fs_smoke), opening as it rises, quick in and out.
    for (const Wisp& w : wisps_) {
        if (!w.alive) continue;
        const float t = std::clamp(w.age / kWispFrames, 0.0f, 1.0f);
        gfx::Sprite sprite;
        for (int k = 0; k < 3; ++k) sprite.position[k] = w.at[k];
        sprite.halfWidth = sprite.halfHeight = 0.5f * (kWispBorn + (kWispGrown - kWispBorn) * t);
        sprite.spin = w.spin + t * 0.3f;
        for (int k = 0; k < 3; ++k) sprite.colour[k] = kWispGrey[k];
        const float in = std::min(1.0f, t / 0.15f);
        const float out = 1.0f - std::clamp((t - 0.3f) / 0.7f, 0.0f, 1.0f);
        sprite.colour[3] = kWispAlpha * in * out;
        sprite.sheet = smokeSheet_;
        sprite.blend = gfx::Blend::Smoke;
        effects.add(sprite);
    }
}

uint32_t Arrows::lights(gfx::PointLight* out, uint32_t max) const {
    if (out == nullptr) return 0;
    uint32_t count = 0;
    for (const Shot& shot : shots_) {
        if (!shot.alive || count >= max) continue;
        if (shot.pierce) {
            gfx::PointLight& light = out[count++];
            for (int k = 0; k < 3; ++k) light.position[k] = shot.at[k];
            light.reach = kPierceLightReach;
            light.height = kBoltLightHeight;
            const float fade = shot.fading > 0.0f ? shot.fading / kPierceFade : 1.0f;
            for (int k = 0; k < 3; ++k) light.colour[k] = kPierceLight[k] * shot.glow * fade;
            continue;
        }
        if (shot.model != Steel && shot.model != Saw) continue;
        gfx::PointLight& light = out[count++];
        for (int k = 0; k < 3; ++k) light.position[k] = shot.at[k];
        light.reach = kBoltLightReach;
        light.height = kBoltLightHeight;
        for (int k = 0; k < 3; ++k) {
            light.colour[k] =
                (shot.tinted ? shot.tint[k] * kToneLight : kBoltLight[k]) * shot.glow;
        }
    }
    return count;
}

bool Arrows::flyingAt(uint32_t shooter, uint32_t whom) const {
    for (const Shot& one : shots_) {
        if (one.alive && one.fading <= 0.0f && one.shooter == shooter && one.whom == whom) {
            return true;
        }
    }
    return false;
}

uint32_t Arrows::flying() const {
    uint32_t count = 0;
    for (const Shot& one : shots_) count += one.alive ? 1u : 0u;
    return count;
}

}  // namespace mu::game
