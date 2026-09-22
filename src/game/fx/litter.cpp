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
void sleeping(const content::Bounds& b, float* basis) {
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
}

}  // namespace

size_t Litter::pieces() const {
    size_t n = 0;
    for (const Drop& d : drops_) n += d.pieces.size();
    return n;
}

void Litter::buildItem(const sim::Lying& one, Drop& drop) {
    const content::Mesh* mesh = models_->of(one.what.item);
    if (!mesh) return;
    const content::ItemRow& row = models_->tables()->items[size_t(one.what.item)];
    const content::Bounds& b = mesh->bounds();
    const float centre[3] = {(b.max[0] + b.min[0]) * 0.5f, (b.max[1] + b.min[1]) * 0.5f,
                             (b.max[2] + b.min[2]) * 0.5f};
    float basis[16];
    bx::mtxIdentity(basis);
    float standing = b.max[1] - b.min[1];
    if (sleeps(row)) {
        sleeping(b, basis);
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
            drawable.inProbe = false;
            out.push_back(drawable);
        }
        return;
    }
}

}  // namespace mu::game
