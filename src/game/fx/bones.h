// What a skeleton leaves when it dies: eleven pieces of bone thrown into the air, tumbling,
// landing, popping, skidding and fading into the grass.
//
// **There is no corpse.** `SetPlayerDie` (ZzzCharacter.cpp:1454-1474) tests the sub-type of a
// body drawn on the player rig, and for MODEL_SKELETON1..3 it sets `o->Live = false` -- the
// model stops drawing on that instruction -- then makes one MODEL_BONE1 and ten MODEL_BONE2
// and plays SOUND_BONE2. The death clip in its rig is never reached. So the burst IS the fall
// for this breed: whatever waits on a body going down -- the loot, the readout -- waits on
// this instead.
//
// The arithmetic is MU's shared ballistic arm (ZzzEffect.cpp:7307-7358), the same one its big
// stones, its broken ice and a meteor's debris ride; `game/meteor.cpp` carries it too, and the
// two were written from one reading so they cannot drift. What belongs to the skeleton alone
// is which models, how many, the two lifts and the sound.
//
// MU2's `client/core/Bones.cs` is the second source, as it was for the meteor: a judged Godot
// port whose remarks record both the numbers and the ways of getting them wrong. Its one
// departure kept here is marked `invention` below.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "content/ground.h"
#include "content/mesh.h"
#include "content/missiles.h"
#include "content/texture.h"
#include "gfx/renderer.h"

namespace mu::game {

class Bones {
public:
    // Reads the cooked missiles table and takes Bone01 and Bone02 out of it, with their lifts.
    // False when neither could be had, which costs the burst and nothing else.
    bool open(const std::string& assetDir, content::Textures& textures,
              const content::Ground* ground);
    void shutdown();
    bool isOpen() const { return large_.mesh != nullptr || small_.mesh != nullptr; }

    // A skeleton came apart at `x, z` (world metres), standing on `floorY`. `bodyScale` is the
    // breed's own draw scale -- 0.95 for the Skeleton Warrior.
    //
    // **That scale is MU2's invention and is kept.** MU passes `CreateEffect` a position, an
    // angle and a light, and the arm rolls its own 0.8-1.1 with nothing about the body in it;
    // MU has one skeleton at one size and can afford that. We draw breeds at a third of their
    // authored size, where an unscaled pile is bigger than the animal it came out of. Note
    // the distinction that makes this different from the trap the meteor fell into: a
    // skeleton's scale is a fact about the body that came apart, where a meteor's 1.0-1.7 is
    // a roll on the thing that threw the debris, and multiplying by THAT put a field of
    // boulders on the grass.
    void burst(float x, float z, float floorY, float bodyScale);

    void update(float seconds);
    // Lit, opaque, and not a caster: eleven small shadows on the frame a fight is busiest is
    // not worth the shadow map's time, which is MU2's call and is recorded as one.
    void gather(std::vector<gfx::Drawable>& out) const;

    uint32_t live() const;
    uint32_t refused() const { return refused_; }

private:
    // One piece in the air or on the grass.
    struct Piece {
        bool alive = false;
        const content::Mesh* mesh = nullptr;
        float position[3];
        float velocity[3];
        float gravity;      // metres a second squared, its own roll
        float size;         // 0.8-1.1, its own roll, times the body's scale
        float left;         // reference frames, counting DOWN
        float yaw;          // set once at birth and never turned again
        float lean[2];      // the two axes MU tumbles
        float fade;         // 0 until it lands, then up to 1
        bool landed;
    };

    struct Model {
        const content::Mesh* mesh = nullptr;
        float lift = 0.0f;  // metres, from the cooked row's MU units
    };

    // MU's numbers, all of them from the arm above unless said otherwise.
    static constexpr float kReferenceFps = 25.0f;
    static constexpr float kUnit = 0.01f;
    // One large and ten small: `CreateEffect(MODEL_BONE1, ...)` once, then a loop of ten
    // MODEL_BONE2 (ZzzCharacter.cpp:1468-1470).
    static constexpr int kLarge = 1, kSmall = 10;
    static constexpr float kLeastFrames = 32.0f, kMoreFrames = 16.0f;   // rand()%16 + 32
    static constexpr float kSmallestPiece = 0.8f, kLargestPiece = 1.1f; // (rand()%4 + 8) * 0.1
    static constexpr float kLeastGravity = 8.0f, kMoreGravity = 16.0f;  // rand()%16 + 8
    static constexpr float kLeastScatter = 64.0f, kMoreScatter = 256.0f;// (rand()%256+64)*0.1
    static constexpr float kTumble = 0.5f;        // x LifeTime, degrees a frame, on two axes
    static constexpr float kBounceDrag = 0.6f;    // horizontal, a frame, once landed
    static constexpr float kRestUnder = 0.5f;     // units a frame; under it the piece lies still
    static constexpr float kFade = 0.1f;          // a frame, and only once landed
    static constexpr int kMaxPieces = 44;         // four bursts at once, and no cap inside one

    Model large_, small_;
    // The two meshes themselves, kept alive for the run: a Model holds a bare pointer because
    // eleven pieces share two meshes and a piece must not own one.
    std::vector<std::unique_ptr<content::Mesh>> owned_;
    const content::Ground* ground_ = nullptr;
    Piece pieces_[kMaxPieces] = {};
    // The drawing's own dice, never the sim's.
    uint32_t dice_ = 0x1BEEF17u;
    uint32_t roll();
    float unit();
    float between(float a, float b);
    uint32_t refused_ = 0;

    void throwOne(const Model& model, float x, float z, float floorY, float bodyScale);
};

}  // namespace mu::game
