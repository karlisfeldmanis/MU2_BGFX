#include "game/pedestals.h"

#include <bx/math.h>

#include <algorithm>
#include <cmath>

#include "core/files.h"
#include "core/log.h"
#include "sim/items.h"

namespace mu::game {
namespace {

// Where each slot stands, in tiles, and its facing in MU's degrees. WSclient.cpp:687.
constexpr float kStands[kRosterSlots][3] = {
    {80.08f, 188.85f, 115.0f},
    {79.86f, 191.45f, 90.0f},
    {80.46f, 194.00f, 75.0f},
    {81.33f, 196.45f, 60.0f},
    {82.82f, 198.45f, 35.0f},
};

// The camera, in tiles, and its rise over the ground under it: MU's (9758.93, 18913.11, 675.5)
// over figures pinned at 163. CameraConfig.h:44.
constexpr float kEyeX = 97.5893f, kEyeY = 189.1311f, kEyeRise = 5.125f;
// MU's angle[0] of -84.5 is 5.5 below level, and angle[2] of -75 is this yaw.
constexpr float kPitchDegrees = 5.5f, kYawDegrees = 75.0f;
// HFovToVFov(40) against MU's 4:3, and it is the vertical field bx::mtxProj takes.
constexpr float kFovDegrees = 30.75f;
// The scene's own scale, whatever the class. ZzzCharacter.cpp:12131.
constexpr float kSceneScale = 1.2f;
// The plate's foot this far over the crown, metres. MU2's BalloonClear.
constexpr float kBalloonClear = 0.55f;
// The rings sit this far up, or the grass swallows them.
constexpr float kRingLift = 0.12f;
// MU's frame, which the particles are counted in.
constexpr float kMuFrame = 1.0f / 25.0f;
// The greeting a figure gives when it is picked, by class (the user, 2026-10-01: 'some elegant
// greeting animations for each class'). Ours: MU's pick only lights the figure. MU's action
// numbers, read off the bench and not off the labels, which run a slot off in this library
// (see Play's salute): 218 the knight's hand brought to the brow and held, 187 the wizard's
// bow, 188 the elf's greeting, a lean and a wave. Faded in and out so the idle is not cut.
constexpr int kGreetKnight = 218, kGreetWizard = 187, kGreetElf = 188;
constexpr float kGreetIn = 0.25f, kGreetOut = 0.45f;
// MU2's SparkDrawn: MU's own spark is right in world size and was drawn into a 430-line viewport,
// where it was an eight-pixel dot; on a 1080-line frame the sheet's flat core becomes a shape.
// Drawn smaller by hand so it still reads as dust.
constexpr float kSparkDrawn = 0.55f;
// And both drawn far smaller again, ours: the user, 2026-09-27, "sparkles are too big, they had
// to be very small". MU's streak is a column of light a body tall; here it is a glint.
constexpr float kBlobDrawn = 0.22f, kSparkSmaller = 0.4f;
// The sheets' own pixels, which is their size in MU units (RenderParticles: Width =
// pBitmap->Width * Scale). chasellight is 16x128 and Impack03 64x64 (lobby_cut.py).
constexpr float kBlobW = 16.0f, kBlobH = 128.0f, kSparkW = 64.0f, kSparkH = 64.0f;

const char* bareOf(sim::Kin kin) {
    // index.json's spellings: the elf's bare body has no suffix.
    return kin == sim::Kin::DarkWizard ? "DarkWizardBare"
           : kin == sim::Kin::FairyElf ? "FairyElf"
                                       : "DarkKnightBare";
}

void tileToWorld(const content::Ground& ground, float column, float row, float out[3]) {
    const float m = ground.metresPerTile();
    out[0] = column * m;
    out[2] = -row * m;
    out[1] = ground.heightAt(out[0], out[2]);
}

bool project(const float* viewProj, float width, float height, const float at[3], float* x,
             float* y) {
    const float point[4] = {at[0], at[1], at[2], 1.0f};
    float clip[4];
    bx::vec4MulMtx(clip, point, viewProj);
    if (clip[3] <= 1e-4f) return false;
    *x = (clip[0] / clip[3] * 0.5f + 0.5f) * width;
    *y = (0.5f - clip[1] / clip[3] * 0.5f) * height;
    return true;
}

}  // namespace

bool Pedestals::open(Figures* figures, const content::Ground* ground,
                     const content::Tables* tables, const std::string& assetDir,
                     content::Textures& textures) {
    figures_ = figures;
    ground_ = ground;
    tables_ = tables;
    // MU's own sheets, cut from MuMain by MU2's lobby_cut.py: BITMAP_GM_AURORA (Skill/gmmzine)
    // and the two particle sheets.
    const auto sheet = [&](const char* name) {
        const std::string path = core::join(assetDir, std::string("effects/lobby/") + name);
        if (!core::fileExists(path)) {
            core::logError("lobby: %s is not there; the pick draws without it", path.c_str());
            return bgfx::TextureHandle BGFX_INVALID_HANDLE;
        }
        return textures.load(path, content::TextureRole::Albedo);
    };
    aurora_ = sheet("gmmzine.png");
    blob_ = sheet("chasellight.png");
    spark_ = sheet("impack03.png");
    motes_.reserve(128);
    return figures_ && figures_->isOpen();
}

const Pedestals::Stand* Pedestals::subject() const {
    // The create window's class, while it stands; otherwise the pick.
    if (previewSlot_ >= 0 && stands_[kRosterSlots].up) return &stands_[kRosterSlots];
    if (picked_ >= 0 && picked_ < kRosterSlots && stands_[picked_].up) return &stands_[picked_];
    return nullptr;
}

void Pedestals::shutdown() {
    for (Stand& stand : stands_) stand = Stand{};
    motes_.clear();
    picked_ = previewSlot_ = -1;
}

bool Pedestals::standing(int slot) const {
    if (slot < 0 || slot >= kRosterSlots) return false;
    if (stands_[slot].up) return true;
    return previewSlot_ == slot && stands_[kRosterSlots].up;
}

const FigureBody* Pedestals::dressed(int slot, sim::Kin kin,
                                     const std::vector<Saved::Item>& items) {
    // The hands by Beast.Rearm's rule (sim/realm_items.cpp's rearm): the weapon is what swings,
    // the right hand's first, and never ammunition; the shield is the left hand's when it is one,
    // or a knight's second weapon.
    // The five armour pieces by the asset their rows name, as Play::redress wears them.
    std::string weapon, shield;
    std::vector<std::string> worn;
    std::vector<ShineLook> wornShine;
    ShineLook weaponShine, shieldShine;
    const content::ItemRow* hand[2] = {nullptr, nullptr};
    int handPlus[2] = {0, 0};
    bool handExcellent[2] = {false, false};
    const content::ItemRow* armour[sim::kBoots + 1] = {};
    int armourPlus[sim::kBoots + 1] = {};
    bool armourExcellent[sim::kBoots + 1] = {};
    for (const Saved::Item& item : items) {
        if (item.slot < 0 || item.slot > sim::kBoots || !tables_) continue;
        const int32_t row = tables_->itemAt(item.group, item.number);
        if (row < 0) continue;
        const content::ItemRow* r = &tables_->items[size_t(row)];
        if (item.slot <= sim::kWeaponLeft) {
            hand[item.slot] = r;
            handPlus[item.slot] = item.plus;
            handExcellent[item.slot] = item.excellent != 0;
        } else {
            armour[item.slot] = r;
            armourPlus[item.slot] = item.plus;
            armourExcellent[item.slot] = item.excellent != 0;
        }
    }
    const auto swung = [](const content::ItemRow* r) {
        return r && r->weapon() && !sim::ammunition(*r);
    };
    const int weaponHand = swung(hand[0]) ? 0 : (swung(hand[1]) ? 1 : -1);
    if (weaponHand >= 0) {
        weapon = hand[weaponHand]->name;
        weaponShine = shineOf(*hand[weaponHand], handPlus[weaponHand], handExcellent[weaponHand]);
    }
    // And a knight's second weapon, held in the left as the shield would be.
    if (hand[1] && (hand[1]->shield() || (weaponHand == 0 && sim::offHanded(*hand[1], kin)))) {
        shield = hand[1]->name;
        shieldShine = shineOf(*hand[1], handPlus[1], handExcellent[1]);
    }
    for (int s = sim::kHelm; s <= sim::kBoots; ++s) {
        if (!armour[s]) continue;
        worn.push_back(armour[s]->name);
        wornShine.push_back(shineOf(*armour[s], armourPlus[s], armourExcellent[s]));
    }
    return figures_->dress("Lobby" + std::to_string(slot), bareOf(kin), weapon, shield, worn,
                           wornShine, weaponShine, shieldShine);
}

void Pedestals::standAt(int slot, const FigureBody* body, sim::Kin kin) {
    const int where = slot == kRosterSlots ? previewSlot_ : slot;
    Stand& stand = stands_[slot];
    stand = Stand{};
    if (!body || !ground_ || where < 0) return;
    float at[3];
    tileToWorld(*ground_, kStands[where][0], kStands[where][1], at);
    // MU's angle[2] as a Figure's yaw: 180 less it. The axis swap is a mirror, which turns the
    // angle's sense, and a character model faces the other way to a placement at angle 0. Taken
    // as it is, the way the cook takes an object's, or negated alone, every figure stood with
    // its back to the camera; both were tried and photographed.
    const float yaw = (180.0f - kStands[where][2]) * 3.14159265f / 180.0f;
    stand.figure.stand(body, at, yaw, body->scale * kSceneScale, true);
    stand.height = body->height * body->scale * kSceneScale;
    stand.up = true;
    stand.kin = kin;
    stand.idle = stand.figure.clip();
    // A seeded phase each, so five idles do not breathe in step.
    stand.figure.setClock(0.37f * float(slot));
}

void Pedestals::raise(const std::vector<Seat>& roster) {
    for (int slot = 0; slot < kRosterSlots; ++slot) stands_[slot] = Stand{};
    for (const Seat& one : roster) {
        if (one.slot < 0 || one.slot >= kRosterSlots) continue;
        standAt(one.slot, dressed(one.slot, one.kin, one.items), one.kin);
    }
    if (picked_ >= 0 && !standing(picked_)) picked_ = -1;
}

void Pedestals::preview(int slot, sim::Kin kin) {
    stands_[kRosterSlots] = Stand{};
    previewSlot_ = -1;
    if (slot < 0 || slot >= kRosterSlots || stands_[slot].up) return;
    previewSlot_ = slot;
    standAt(kRosterSlots, dressed(kRosterSlots, kin, {}), kin);
}

void Pedestals::pick(int slot) {
    const int was = picked_;
    picked_ = slot >= 0 && slot < kRosterSlots && stands_[slot].up ? slot : -1;
    if (picked_ < 0 || picked_ == was) return;
    Stand& stand = stands_[picked_];
    const FigureBody* body = stand.figure.body();
    if (!body || !body->library) return;
    const int action = stand.kin == sim::Kin::DarkWizard ? kGreetWizard
                       : stand.kin == sim::Kin::FairyElf ? kGreetElf
                                                          : kGreetKnight;
    const int clip = body->library->find(action);
    if (clip < 0) return;
    stand.figure.play(clip, true, kGreetIn);
    stand.greeting = stand.figure.length();
}

void Pedestals::aim(gfx::Camera& camera) const {
    float eye[3];
    tileToWorld(*ground_, kEyeX, kEyeY, eye);
    eye[1] += kEyeRise;
    const float pitch = kPitchDegrees * 3.14159265f / 180.0f;
    const float yaw = kYawDegrees * 3.14159265f / 180.0f;
    // MU2's Aim: the look is -(sin yaw cos pitch, sin pitch, cos yaw cos pitch) on Y-up axes whose
    // z is MU's y negated, which are this engine's.
    const float toward[3] = {-std::sin(yaw) * std::cos(pitch), -std::sin(pitch),
                             -std::cos(yaw) * std::cos(pitch)};
    for (int a = 0; a < 3; ++a) {
        camera.position[a] = eye[a];
        camera.target[a] = eye[a] + toward[a] * 10.0f;
    }
    camera.up[0] = 0.0f;
    camera.up[1] = 1.0f;
    camera.up[2] = 0.0f;
    camera.fovDegrees = kFovDegrees;
    camera.nearPlane = 0.1f;
    camera.farPlane = 400.0f;
}

int Pedestals::hover(const float* viewProj, float width, float height, float x, float y) const {
    // Nearest first, which on this row is the lowest slot: the camera stands at its near end.
    for (int slot = 0; slot < kRosterSlots; ++slot) {
        const Stand* stand = stands_[slot].up ? &stands_[slot]
                             : (previewSlot_ == slot && stands_[kRosterSlots].up)
                                 ? &stands_[kRosterSlots]
                                 : nullptr;
        if (!stand) continue;
        const float* feet = stand->figure.position();
        const float tall = std::max(stand->height, 3.0f * ground_->metresPerTile());
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        bool seen = true;
        for (int corner = 0; corner < 8 && seen; ++corner) {
            const float at[3] = {feet[0] + ((corner & 1) ? 0.72f : -0.72f),
                                 feet[1] + ((corner & 2) ? tall : 0.0f),
                                 feet[2] + ((corner & 4) ? 0.72f : -0.72f)};
            float px, py;
            seen = project(viewProj, width, height, at, &px, &py);
            minX = std::min(minX, px);
            maxX = std::max(maxX, px);
            minY = std::min(minY, py);
            maxY = std::max(maxY, py);
        }
        if (seen && x >= minX && x <= maxX && y >= minY && y <= maxY) return slot;
    }
    return -1;
}

bool Pedestals::crown(int slot, const float* viewProj, float width, float height, float* x,
                      float* y) const {
    if (slot < 0 || slot >= kRosterSlots || !stands_[slot].up) return false;
    const Stand& stand = stands_[slot];
    const float* feet = stand.figure.position();
    const float over[3] = {feet[0], feet[1] + stand.height + kBalloonClear, feet[2]};
    return project(viewProj, width, height, over, x, y);
}

float Pedestals::roll(int n) {
    dice_ = dice_ * 1664525u + 1013904223u;
    return float((dice_ >> 8) % uint32_t(n));
}

void Pedestals::spawn(const float feet[3], bool blob) {
    // One particle of type 4 or 5, as CreateParticle makes it for BITMAP_EFFECT: 20 to 22 frames
    // of life, +-25 units on the ground and 150 to 350 up less a hundred; the blob two to two and
    // a half sheets across and still, the spark a quarter of one rising 1.3 to 1.9 units a frame.
    if (motes_.size() >= motes_.capacity()) return;
    Mote mote;
    mote.at[0] = feet[0] + (roll(50) - 25.0f) / 100.0f;
    mote.at[1] = feet[1] + ((roll(200) - 100.0f) + 250.0f - 100.0f) / 100.0f;
    mote.at[2] = feet[2] + (roll(50) - 25.0f) / 100.0f;
    float scale = 1.0f;
    if (blob) {
        mote.at[0] = feet[0] + (roll(80) - 40.0f) / 100.0f;
        scale = (roll(5) + 20.0f) * 0.1f;
        mote.rise = 0.0f;
        mote.half[0] = kBlobW / 100.0f * scale * 0.5f * kBlobDrawn;
        mote.half[1] = kBlobH / 100.0f * scale * 0.5f * kBlobDrawn;
    } else {
        scale = (roll(10) + 20.0f) * 0.01f * 1.3f;
        mote.rise = (roll(10) + 20.0f) * 0.05f * 1.3f / 100.0f;
        mote.half[0] = kSparkW / 100.0f * scale * kSparkDrawn * kSparkSmaller * 0.5f;
        mote.half[1] = kSparkH / 100.0f * scale * kSparkDrawn * kSparkSmaller * 0.5f;
    }
    mote.life = 20.0f + roll(3);
    mote.light = 0.15f;
    mote.blob = blob;
    motes_.push_back(mote);
}

void Pedestals::update(float seconds) {
    clock_ += seconds;
    for (Stand& stand : stands_) {
        if (!stand.up) continue;
        stand.figure.update(seconds);
        // Back to the idle as the greeting ends, the fade begun before its last key so the
        // clip never wraps round to its start.
        if (stand.greeting > 0.0f) {
            stand.greeting -= seconds;
            if (stand.greeting <= kGreetOut && stand.idle >= 0) {
                stand.figure.play(stand.idle, false, kGreetOut);
                stand.greeting = 0.0f;
            }
        }
    }
    const Stand* pick = subject();
    if (pick) {
        owed_ += seconds;
        while (owed_ >= kMuFrame) {
            owed_ -= kMuFrame;
            spawn(pick->figure.position(), true);
            spawn(pick->figure.position(), false);
        }
    } else {
        owed_ = 0.0f;
    }
    // glColor3fv(o->Light): brighter by 1.16 a frame while ten or more are left, then dimmer.
    const float frames = seconds / kMuFrame;
    for (size_t i = 0; i < motes_.size();) {
        Mote& mote = motes_[i];
        mote.life -= frames;
        if (mote.life <= 0.0f) {
            motes_[i] = motes_.back();
            motes_.pop_back();
            continue;
        }
        mote.light *= std::pow(mote.life >= 10.0f ? 1.16f : 1.0f / 1.16f, frames);
        mote.at[1] += mote.rise * frames;
        ++i;
    }
}

void Pedestals::gather(gfx::Renderer& renderer, std::vector<gfx::Drawable>& out,
                       std::vector<gfx::Drawable>* casters) {
    for (Stand& stand : stands_) {
        if (!stand.up || !stand.figure.body()) continue;
        const size_t need = stand.figure.body()->boneCount() * 12;
        if (scratch_.size() < need) scratch_.resize(need);
        const int bones = stand.figure.pose(scratch_.data());
        const int row = bones > 0 ? renderer.addPalette(scratch_.data(), bones) : -1;
        stand.figure.poseHeld(renderer, scratch_.data());
        stand.figure.gather(row, out);
        if (casters) stand.figure.gather(row, *casters);
    }
}

void Pedestals::gatherEffects(gfx::Effects& effects) const {
    const Stand* pick = subject();
    if (!pick) return;
    const float* feet = pick->figure.position();
    const float glow = std::sin(clock_ * 1.5f) * 0.3f + 0.5f;
    if (bgfx::isValid(aurora_)) {
        // Two discs turning opposite ways, ten degrees a second.
        for (int ring = 0; ring < 2; ++ring) {
            const float across = (ring == 0 ? 1.8f : 1.2f) * ground_->metresPerTile();
            const float turn = (ring == 0 ? 1.0f : -1.0f) * clock_ * 10.0f * 3.14159265f / 180.0f;
            gfx::Sprite disc;
            disc.placed = true;
            disc.sheet = aurora_;
            disc.blend = gfx::Blend::Additive;
            disc.colour[0] = disc.colour[1] = disc.colour[2] = glow;
            disc.colour[3] = 1.0f;
            const float half = across * 0.5f;
            const float c = std::cos(turn) * half, s = std::sin(turn) * half;
            const float offsets[4][2] = {{-c + s, -s - c}, {c + s, s - c}, {c - s, s + c},
                                         {-c - s, -s + c}};
            const float uvs[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
            for (int k = 0; k < 4; ++k) {
                disc.corner[k][0] = feet[0] + offsets[k][0];
                disc.corner[k][2] = feet[2] + offsets[k][1];
                disc.corner[k][1] =
                    ground_->heightAt(disc.corner[k][0], disc.corner[k][2]) + kRingLift;
                disc.cornerUv[k][0] = uvs[k][0];
                disc.cornerUv[k][1] = uvs[k][1];
            }
            disc.position[0] = feet[0];
            disc.position[1] = feet[1] + kRingLift;
            disc.position[2] = feet[2];
            effects.add(disc);
        }
    }
    for (const Mote& mote : motes_) {
        const bgfx::TextureHandle sheet = mote.blob ? blob_ : spark_;
        if (!bgfx::isValid(sheet)) continue;
        gfx::Sprite sprite;
        std::copy(mote.at, mote.at + 3, sprite.position);
        sprite.halfWidth = mote.half[0];
        sprite.halfHeight = mote.half[1];
        sprite.sheet = sheet;
        sprite.blend = gfx::Blend::Additive;
        const float lit = std::max(mote.light, 0.0f);
        sprite.colour[0] = sprite.colour[1] = sprite.colour[2] = lit;
        sprite.colour[3] = 1.0f;
        effects.add(sprite);
    }
}

uint32_t Pedestals::lights(gfx::PointLight* out, uint32_t room) const {
    uint32_t count = 0;
    const float mpt = ground_->metresPerTile();
    // The row's own fill, two lamps a few tiles in front of it on the camera's side, reaching the
    // figures and the ground round them and stopping short of the buildings behind -- the user,
    // 2026-09-27: "lighting has to be darker behind characters". Ours: MU lights every figure
    // flat at 0.4 (ZzzCharacter.cpp:9252) whatever stands behind it, which is the same wish.
    for (int slot : {1, 3}) {
        if (count >= room) break;
        gfx::PointLight& fill = out[count++];
        fill.position[0] = (kStands[slot][0] + 2.5f) * mpt;
        fill.position[2] = -kStands[slot][1] * mpt;
        fill.position[1] = ground_->heightAt(fill.position[0], fill.position[2]) + 2.4f;
        fill.reach = 6.0f * mpt;
        fill.height = 2.6f;
        // Lighting the row's side only, down -x, away from the camera: lit all round, it caught
        // the foreground tree's flat branch cards between it and the eye and drew them pale.
        fill.away[0] = -1.0f;
        fill.away[1] = 0.0f;
        fill.away[2] = 0.0f;
        // Through the lamps' own strength (lighting.json's lamp_strength), so a third of white:
        // at full the floor and the figures blew out.
        fill.colour[0] = 0.34f;
        fill.colour[1] = 0.31f;
        fill.colour[2] = 0.27f;
    }
    const Stand* stand = subject();
    if (count >= room || !stand) return count;
    const float* feet = stand->figure.position();
    gfx::PointLight& light = out[count++];
    light.position[0] = feet[0];
    light.position[1] = feet[1] + 1.6f;
    light.position[2] = feet[2];
    light.reach = 3.0f * mpt;
    light.height = 2.2f;
    light.colour[0] = light.colour[1] = light.colour[2] = 0.7f;
    return count;
}

}  // namespace mu::game
