#include "game/fx/litter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <bx/math.h>


namespace mu::game {
namespace {

// MU's units: a hundred of them to a tile, and a tile is a metre. Drops.UnitsPerTile.
constexpr float kUnits = 100.0f;
// MU's animation clock, which every rate below is per frame of. Drops.ReferenceFps.
constexpr float kFps = 25.0f;
// Drops.DropHeight, Toss and Gravity: 90 units up, thrown up at 8 a frame against 6 a frame.
// Written here as metres and metres a second, which is what the update integrates in.
constexpr float kDropHeight = 90.0f / kUnits;
constexpr float kToss = 8.0f / kUnits * kFps;
constexpr float kGravity = 6.0f / kUnits * kFps * kFps;
// Drops.Bounces, Damping and BounceThreshold: one rebound at a third of the arrival speed,
// and none at all below a speed there is no bounce left in.
constexpr int kBounces = 1;
constexpr float kDamping = 0.32f;
constexpr float kBounceThreshold = 6.0f / kUnits * kFps;
// Drops.Clearance: a hair, so the lowest face does not fight the ground for depth.
constexpr float kClearance = 0.5f / kUnits;
// Drops.FewestCoins, MostCoins, CoinSpread, CoinLowest, CoinTossLeast and CoinTossMost.
constexpr int kFewestCoins = 3, kMostCoins = 80;
constexpr float kCoinSpread = 20.0f;
constexpr float kCoinLowest = 0.35f;
constexpr float kCoinTossLeast = 2.0f / kUnits * kFps, kCoinTossMost = 14.0f / kUnits * kFps;

// Drops.Turn: the yaw is hashed from the drop's own id, so it needs no state and is the same
// answer every frame for as long as the thing lies there.
float turn(uint32_t id) {
    return float((id * 2654435761u) % 3600u) / 3600.0f * 2.0f * bx::kPi;
}

// A scatter of its own for each heap, for the same reason: stable, and different per drop.
struct Dice {
    uint32_t state;
    explicit Dice(uint32_t seed) : state(seed * 2654435761u + 1u) {}
    float next() {
        state = state * 1664525u + 1013904223u;
        return float((state >> 8) & 0xFFFFFFu) / float(0x1000000u);
    }
};

// Drops.Sleeps: the weapons, shields, armour, trousers and gloves lie down; a helm and a pair
// of boots land standing, which is where MU's own ARMOR..GLOVES+512 range leaves them, and a
// potion or a coin is authored sitting on its base.
bool sleeps(const content::ItemRow& row) {
    return row.group >= 0 && row.group <= sim::kGroupGloves && row.group != sim::kGroupHelms;
}

// Drops.Sleeping: the longest local extent along the ground's x, the middle along its z, the
// shortest pointing up. Rows of a row-vector matrix are where the local axes go.
void sleeping(const content::Bounds& b, bool shield, float* basis) {
    const float size[3] = {b.max[0] - b.min[0], b.max[1] - b.min[1], b.max[2] - b.min[2]};
    int order[3] = {0, 1, 2};
    std::sort(order, order + 3, [&](int a, int c) { return size[a] > size[c]; });
    const float world[3][3] = {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    bx::mtxIdentity(basis);
    for (int i = 0; i < 16; ++i) basis[i] = 0.0f;
    basis[15] = 1.0f;
    for (int k = 0; k < 3; ++k) {
        for (int c = 0; c < 3; ++c) basis[order[k] * 4 + c] = world[k][c];
    }
    // A permutation can come out mirrored, which turns a model inside out. Flipping the axis
    // that points up is the flip nobody can see from above.
    const float det = basis[0] * (basis[5] * basis[10] - basis[6] * basis[9]) -
                      basis[1] * (basis[4] * basis[10] - basis[6] * basis[8]) +
                      basis[2] * (basis[4] * basis[9] - basis[5] * basis[8]);
    if (det < 0.0f) {
        for (int r = 0; r < 3; ++r) basis[r * 4 + 1] = -basis[r * 4 + 1];
    }
    // Except for a shield, whose up side is seen: its painted face and boss are local +x, as in
    // the bag (ItemStage::render). +x is put to the sky, and where that makes a mirror the
    // ground's x is flipped with it -- a half turn about the vertical, hidden by the yaw.
    if (shield && order[2] == 0 && basis[1] < 0.0f) {
        for (int r = 0; r < 3; ++r) {
            basis[r * 4 + 1] = -basis[r * 4 + 1];
            basis[r * 4 + 0] = -basis[r * 4 + 0];
        }
    }
}

}  // namespace

size_t Litter::pieces() const {
    size_t n = 0;
    for (const Drop& d : drops_) n += d.pieces.size();
    return n;
}

namespace {

// A lying jewel's glow: its light's reach in tiles and level, and how fast it breathes. Faint,
// the user's 'minimal'; a Rune of Creation the same (its flare and stars were 'overkill').
constexpr float kJewelReach = 1.4f, kJewelLevel = 0.14f;
// A quest item's: wider and several times brighter, a violet the beam shares.
constexpr float kQuestReach = 3.0f, kQuestLevel = 0.7f;
constexpr float kQuestViolet[3] = {0.72f, 0.38f, 1.0f};  // the user's purple
constexpr float kLegendaryGreen[3] = {0.12f, 1.0f, 0.08f};  // legendary's green since 2026-10-04
constexpr float kEpicPurple[3] = {0.64f, 0.21f, 0.93f};      // WoW's epic, an excellent's name
// Its beam: metres tall and wide, and the core's share of the width.
constexpr float kBeamTall = 5.5f, kBeamWide = 1.6f, kCoreShare = 0.45f;
constexpr float kGlowLift = 0.15f;        // metres over the jewel's middle
constexpr float kBreath = 1.6f;           // radians a second

// Each jewel in a colour of its own, as its stone is painted: Bless warm white, Soul pale blue,
// Chaos violet, Life red; anything else the flag holds, white.
void jewelColour(const content::ItemRow& row, float out[3]) {
    float c[3] = {0.9f, 0.92f, 1.0f};
    if (row.group == 14 && row.number == 13) { c[0] = 1.0f; c[1] = 0.9f; c[2] = 0.7f; }
    if (row.group == 14 && row.number == 14) { c[0] = 0.55f; c[1] = 0.75f; c[2] = 1.0f; }
    if (row.group == 12 && row.number == 15) { c[0] = 0.75f; c[1] = 0.45f; c[2] = 1.0f; }
    if (row.group == 14 && row.number == 16) { c[0] = 1.0f; c[1] = 0.35f; c[2] = 0.3f; }
    for (int k = 0; k < 3; ++k) out[k] = c[k];
}

// Whether a drop is legendary, as the card's quality says it (game/ui/describe.cpp qualityOf): a
// Rune of Creation of a legendary power, or of none yet, and a ring or pendant of four powers.
bool legendary(const content::ItemRow& row, const sim::Held& what) {
    if (sim::creation(row)) {
        const sim::PowerRow* power = sim::powerOf(what.powers[0]);
        return !power || power->rarity == sim::Rarity::Legendary;
    }
    return sim::powered(row) && sim::affixCount(row, what) >= 4;
}

// A rune's rarity in the colours its name is drawn in: Rare blue, Epic purple, Legendary green.
void runeColour(const sim::Held& what, float out[3]) {
    const sim::PowerRow* power = sim::powerOf(what.powers[0]);
    const sim::Rarity rarity = power ? power->rarity : sim::Rarity::Legendary;
    const float rare[3] = {0.3f, 0.55f, 1.0f}, epic[3] = {0.7f, 0.35f, 1.0f},
                legendary[3] = {0.12f, 1.0f, 0.08f};
    const float* c = rarity == sim::Rarity::Rare ? rare : rarity == sim::Rarity::Epic ? epic : legendary;
    for (int k = 0; k < 3; ++k) out[k] = c[k];
}

}  // namespace

void Litter::buildItem(const sim::Lying& one, Drop& drop) {
    const content::Mesh* mesh = models_->of(one.what.item);
    if (!mesh) return;
    const content::ItemRow& row = models_->tables()->items[size_t(one.what.item)];
    drop.shine = shineOf(row, one.what.refinement, one.what.excellent != 0);
    // Loch's Feather too, which is rare (the user, 2026-10-04: 'feather is pretty rare item use
    // same ligh effect which quest items has').
    if (sim::classTreasure(row) || sim::lochsFeather(row)) {
        drop.glow = 3;
        for (int k = 0; k < 3; ++k) drop.glowColour[k] = kQuestViolet[k];
    } else if (legendary(row, one.what)) {
        // A legendary drop stands in the same column, in its own green (the user, 2026-10-04:
        // 'we need also that light for legendary drops'): the colour its name is drawn in.
        drop.glow = 3;
        for (int k = 0; k < 3; ++k) drop.glowColour[k] = kLegendaryGreen[k];
    } else if (one.what.excellent != 0) {
        // And an excellent one, in the epic purple its name is drawn in (the user, 2026-10-04:
        // 'excelnt drops needs also light').
        drop.glow = 3;
        for (int k = 0; k < 3; ++k) drop.glowColour[k] = kEpicPurple[k];
    } else if (sim::creation(row)) {
        drop.glow = 2;
        runeColour(one.what, drop.glowColour);
    } else if (row.jewel() && row.group != sim::kGroupPets) {
        drop.glow = 1;
        jewelColour(row, drop.glowColour);
    }
    const content::Bounds& b = mesh->bounds();
    const float centre[3] = {(b.max[0] + b.min[0]) * 0.5f, (b.max[1] + b.min[1]) * 0.5f,
                             (b.max[2] + b.min[2]) * 0.5f};
    float basis[16];
    bx::mtxIdentity(basis);
    float standing = b.max[1] - b.min[1];
    if (sleeps(row)) {
        sleeping(b, row.shield(), basis);
        // Laid down, what points up is the shortest extent.
        const float size[3] = {b.max[0] - b.min[0], b.max[1] - b.min[1], b.max[2] - b.min[2]};
        standing = std::min(size[0], std::min(size[1], size[2]));
    }
    const float metresPerTile = ground_->metresPerTile();
    const float x = (float(one.column) + 0.5f) * metresPerTile;
    const float z = -(float(one.row) + 0.5f) * metresPerTile;
    const float y = ground_->heightAt(x, z) + standing * 0.5f + kClearance;

    float toCentre[16], yaw[16], place[16], a[16];
    bx::mtxTranslate(toCentre, -centre[0], -centre[1], -centre[2]);
    bx::mtxRotateY(yaw, turn(one.id));
    bx::mtxTranslate(place, x, y, z);
    Piece piece;
    piece.mesh = mesh;
    bx::mtxMul(a, toCentre, basis);
    float turned[16];
    bx::mtxMul(turned, a, yaw);
    bx::mtxMul(piece.rest, turned, place);
    piece.above = kDropHeight;
    piece.speed = kToss;
    drop.pieces.push_back(piece);
}

void Litter::buildHeap(const sim::Lying& one, Drop& drop) {
    const content::Mesh* coin = models_->coin();
    if (!coin) return;
    const content::Bounds& b = coin->bounds();
    const float centre[3] = {(b.max[0] + b.min[0]) * 0.5f, (b.max[1] + b.min[1]) * 0.5f,
                             (b.max[2] + b.min[2]) * 0.5f};
    const float standing = b.max[1] - b.min[1];
    const float metresPerTile = ground_->metresPerTile();
    const float x = (float(one.column) + 0.5f) * metresPerTile;
    const float z = -(float(one.row) + 0.5f) * metresPerTile;
    const float floor = ground_->heightAt(x, z) + standing * 0.5f + kClearance;

    // Drops.Heap: how many coins a pile is drawn with, whatever it is worth, and how wide.
    const int many = std::clamp(int(std::sqrt(double(std::max<int64_t>(one.zen, 0))) / 2.0),
                                kFewestCoins, kMostCoins);
    const float spread = (float(many) + kCoinSpread) / kUnits;
    Dice dice(one.id);
    for (int i = 0; i < many; ++i) {
        const float angle = dice.next() * 2.0f * bx::kPi;
        const float radius = dice.next() * spread;
        float toCentre[16], yaw[16], place[16], a[16];
        bx::mtxTranslate(toCentre, -centre[0], -centre[1], -centre[2]);
        // A coin's own turn, so a heap does not read as a stack of identical discs.
        bx::mtxRotateY(yaw, dice.next() * 2.0f * bx::kPi);
        bx::mtxTranslate(place, x + std::cos(angle) * radius, floor,
                         z + std::sin(angle) * radius);
        Piece piece;
        piece.mesh = coin;
        bx::mtxMul(a, toCentre, yaw);
        bx::mtxMul(piece.rest, a, place);
        // A coin falls inside a heap that is already on the ground, from lower than an item
        // and with a toss of its own, so the pile fills rather than arriving whole.
        piece.above = kDropHeight * (kCoinLowest + (1.0f - kCoinLowest) * dice.next());
        piece.speed = kCoinTossLeast + (kCoinTossMost - kCoinTossLeast) * dice.next();
        drop.pieces.push_back(piece);
    }
}

void Litter::build(const sim::Lying& one, Drop& drop) {
    drop.id = one.id;
    if (one.what.empty()) {
        buildHeap(one, drop);
    } else {
        buildItem(one, drop);
    }
}

void Litter::update(const sim::Realm& realm, double seconds,
                    const std::vector<uint32_t>& held) {
    if (!models_ || !ground_) return;
    for (Drop& drop : drops_) drop.present = false;
    for (const sim::Lying& one : realm.lying()) {
        // Not yet: its monster is still falling. Built the frame it is let go, so its own
        // toss out of the corpse starts then.
        if (std::find(held.begin(), held.end(), one.id) != held.end()) continue;
        auto found = std::find_if(drops_.begin(), drops_.end(),
                                  [&](const Drop& d) { return d.id == one.id; });
        if (found == drops_.end()) {
            drops_.emplace_back();
            build(one, drops_.back());
            drops_.back().present = true;
        } else {
            found->present = true;
        }
    }
    // Picked up, or lain its minute: the realm stopped carrying it and so does this.
    drops_.erase(std::remove_if(drops_.begin(), drops_.end(),
                                [](const Drop& d) { return !d.present; }),
                 drops_.end());

    const float dt = float(seconds);
    clock_ = std::fmod(clock_ + dt, 3600.0f);  // an hour, past any float drift that shows
    settled_.clear();
    for (Drop& drop : drops_) {
        bool still = true;
        for (Piece& piece : drop.pieces) {
            if (piece.above <= 0.0f && piece.speed == 0.0f) continue;
            piece.speed -= kGravity * dt;
            piece.above += piece.speed * dt;
            if (piece.above > 0.0f) continue;
            piece.above = 0.0f;
            if (piece.bounced < kBounces && -piece.speed > kBounceThreshold) {
                piece.speed = -piece.speed * kDamping;
                ++piece.bounced;
            } else {
                piece.speed = 0.0f;
            }
        }
        // Down means its first touch of the grass: the bounces after it are small and quick,
        // and a label held through them read as late.
        for (const Piece& piece : drop.pieces) {
            const bool touched = piece.bounced > 0 || (piece.above <= 0.0f && piece.speed == 0.0f);
            if (!touched) still = false;
        }
        if (still) settled_.push_back(drop.id);
    }
}

void Litter::gather(std::vector<gfx::Drawable>& out,
                    std::vector<gfx::Drawable>* casters) const {
    for (const Drop& drop : drops_) {
        for (const Piece& piece : drop.pieces) {
            if (!piece.mesh) continue;
            gfx::Drawable drawable;
            drawable.mesh = piece.mesh;
            std::memcpy(drawable.transform, piece.rest, sizeof(drawable.transform));
            // Row-vector matrices: the translation is the last row, so the fall is added to it
            // rather than composed as another matrix.
            drawable.transform[13] += piece.above;
            wear(drop.shine, drawable);
            // A thing that is not the town is not in the probe: a cube taken at the player's
            // chest holds the street, and a sword at his feet would be reflected out of it
            // twice the size it is.
            drawable.inProbe = false;
            out.push_back(drawable);
            if (casters) casters->push_back(drawable);
        }
    }
}

void Litter::gatherOne(uint32_t id, std::vector<gfx::Drawable>& out) const {
    if (id == 0) return;
    for (const Drop& drop : drops_) {
        if (drop.id != id) continue;
        for (const Piece& piece : drop.pieces) {
            if (!piece.mesh) continue;
            gfx::Drawable drawable;
            drawable.mesh = piece.mesh;
            std::memcpy(drawable.transform, piece.rest, sizeof(drawable.transform));
            drawable.transform[13] += piece.above;
            wear(drop.shine, drawable);
            drawable.inProbe = false;
            out.push_back(drawable);
        }
        return;
    }
}

}  // namespace mu::game

namespace mu::game {

uint32_t Litter::lights(gfx::PointLight* out, uint32_t max, const float near[3]) const {
    if (max == 0) return 0;
    struct Lit {
        const Drop* drop;
        float d2;
    };
    std::vector<Lit> lit;
    for (const Drop& drop : drops_) {
        if (drop.glow == 0 || drop.pieces.empty()) continue;
        if (std::find(settled_.begin(), settled_.end(), drop.id) == settled_.end()) continue;
        const float* at = drop.pieces.front().rest + 12;
        const float dx = at[0] - near[0], dz = at[2] - near[2];
        lit.push_back({&drop, dx * dx + dz * dz});
    }
    // A quest item first, whatever is nearer: it is the one that must be seen.
    std::sort(lit.begin(), lit.end(), [](const Lit& a, const Lit& b) {
        if ((a.drop->glow == 3) != (b.drop->glow == 3)) return a.drop->glow == 3;
        return a.d2 < b.d2;
    });
    uint32_t count = 0;
    for (const Lit& one : lit) {
        if (count == max) break;
        const Drop& drop = *one.drop;
        const float* at = drop.pieces.front().rest + 12;
        const float breath = 0.85f + 0.15f * std::sin(clock_ * kBreath + float(drop.id % 7));
        gfx::PointLight& light = out[count++];
        light = gfx::PointLight();
        light.position[0] = at[0];
        light.position[1] = at[1] + kGlowLift;
        light.position[2] = at[2];
        const bool quest = drop.glow == 3;
        light.reach = (quest ? kQuestReach : kJewelReach) * ground_->metresPerTile();
        light.height = kGlowLift + (quest ? 1.2f : 0.3f);
        const float level = (quest ? kQuestLevel : kJewelLevel) * breath;
        for (int k = 0; k < 3; ++k) light.colour[k] = drop.glowColour[k] * level;
    }
    return count;
}


void Litter::gatherBeams(gfx::Effects& effects, const float eye[3]) const {
    if (!bgfx::isValid(beam_) || !ground_) return;
    for (const Drop& drop : drops_) {
        if (drop.glow != 3 || drop.pieces.empty()) continue;
        if (std::find(settled_.begin(), settled_.end(), drop.id) == settled_.end()) continue;
        const float* at = drop.pieces.front().rest + 12;
        const float footY = ground_->heightAt(at[0], at[2]);
        // Turned about its own upright to face the eye: a column, not a billboard leaning
        // with the camera's pitch.
        float side[2] = {-(eye[2] - at[2]), eye[0] - at[0]};
        const float length = std::sqrt(side[0] * side[0] + side[1] * side[1]);
        if (length < 1e-4f) continue;
        side[0] /= length;
        side[1] /= length;
        const float breath = 0.8f + 0.2f * std::sin(clock_ * 2.2f + float(drop.id % 5));
        // Three layers. The sheet is opaque and holds its streak in its colour, so it is only
        // ever added or taken away -- laid over the ground it is a grey slab. First the violet's
        // complement taken away, which on Devias's snow, where added light shows nothing, turns
        // the white under the column violet and on dark ground changes nothing; then a wide
        // violet glow added, and a narrow paler core.
        for (int layer = 0; layer < 3; ++layer) {
            const float half = kBeamWide * 0.5f * (layer == 2 ? kCoreShare : 1.0f);
            gfx::Sprite beam;
            beam.sheet = beam_;
            beam.blend = layer == 0 ? gfx::Blend::Minus : gfx::Blend::Additive;
            beam.placed = true;
            for (int k = 0; k < 3; ++k) {
                const float c = drop.glowColour[k];
                beam.colour[k] = layer == 0 ? (1.0f - c) * 0.8f : layer == 1 ? c : 0.5f + 0.5f * c;
            }
            beam.colour[3] = (layer == 2 ? 1.0f : 0.9f) * breath;
            const float low = footY - 0.05f, high = footY + kBeamTall;
            const float corners[4][3] = {
                {at[0] - side[0] * half, low, at[2] - side[1] * half},
                {at[0] + side[0] * half, low, at[2] + side[1] * half},
                {at[0] + side[0] * half, high, at[2] + side[1] * half},
                {at[0] - side[0] * half, high, at[2] - side[1] * half},
            };
            // The streak's sheet is brightest at its middle and fades to both ends: its lower
            // half is laid down the column, so it is hottest at the ground and gone at the top.
            const float uv[4][2] = {{0.0f, 0.5f}, {1.0f, 0.5f}, {1.0f, 0.0f}, {0.0f, 0.0f}};
            for (int c = 0; c < 4; ++c) {
                for (int k = 0; k < 3; ++k) beam.corner[c][k] = corners[c][k];
                beam.cornerUv[c][0] = uv[c][0];
                beam.cornerUv[c][1] = uv[c][1];
            }
            beam.position[0] = at[0];
            beam.position[1] = footY + kBeamTall * 0.5f;
            beam.position[2] = at[2];
            effects.add(beam);
        }
    }
}

}  // namespace mu::game
