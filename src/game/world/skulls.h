// The Lost Tower's skulls and stone chips, which the hero kicks along the floor.
//
// MoveObject, case WD_4LOSTTOWER (ZzzObject.cpp:3996-4001), runs CheckSkull for types 38 and
// 39 -- Object39, the 24 cm skull, 777 placed, and Object40, the 18 cm chip, 335
// (Render/Effects/ZzzEffectFireLeave.cpp:94-120). Every frame, while the hero walks or runs
// and the piece is at rest (`Direction[0] < 0.1`), a piece within 50 units of him is kicked:
//
//   Direction = (-dx * 0.4, -dy * 0.4, 0)     dx, dy = hero - piece, MU units
//   HeadAngle = (-dy * 4, -dx * 4, 0)         degrees a frame about MU's x and y
//   PlayBuffer(SOUND_BONE2, o)                mBone2.wav
//
// and then, kicked or not, both are damped by 0.6 and added: Position += Direction,
// Angle += HeadAngle. So a piece skids 0.6 of the way it stood from him, tumbles up to 300
// degrees, and stays where it stops. No gravity and no walls: MU's piece slides at its own
// height, and so does this one.
//
// MU's rest test reads Direction's x alone and signed, so a piece sliding towards -x counts
// as at rest and can be kicked again mid-skid. Transcribed as it is.
//
// What is not MU's, and marked: MU's frame is 25 Hz and these numbers are per frame; here the
// steps run on a 25 Hz clock and the drawn pose is eased between the last two, so a kick
// does not stutter at 200 fps. And the sound: MU loads mBone2 with two voices
// (ZzzOpenData.cpp:4807), so at most two clatters start in one step here. The chunk bounds
// are the cook's and stay; a piece kicked out of its chunk's box can be culled at the edge.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace mu::game {

class Town;

class Skulls {
public:
    // Finds the world's skulls and chips among the town's placements. Only the tower has any.
    void open(const std::string& world, const Town& town, float metresPerTile);
    // Says how many kicks the visit saw, which is what a headless run reads to know it worked.
    void shutdown();
    bool isOpen() const { return !pieces_.empty(); }

    // One frame: the hero's feet in metres and whether he walks or runs. Writes each moving
    // piece's pose into the town.
    void update(float seconds, float heroX, float heroZ, bool heroMoving, Town& town);

    // The clatters the last update started, in metres. Read and cleared by whoever plays them.
    struct Clatter {
        float at[3];
    };
    const std::vector<Clatter>& clatters() const { return clatters_; }

private:
    struct Piece {
        uint32_t instance = 0;
        // MU's own state, in MU units and degrees: x east, y north (our -z).
        float position[2] = {0.0f, 0.0f};
        float direction[2] = {0.0f, 0.0f};
        float angle[2] = {0.0f, 0.0f};      // MU's Angle[0] and [1]
        float headAngle[2] = {0.0f, 0.0f};  // MU's HeadAngle[0] and [1]
        // The pose one step back, which the drawn pose eases from.
        float wasPosition[2] = {0.0f, 0.0f};
        float wasAngle[2] = {0.0f, 0.0f};
        float height = 0.0f;  // metres, as cooked
        float yaw = 0.0f;     // radians, as cooked
        bool moving = false;
    };
    void step(bool heroMoving, float heroX, float heroY);
    void write(Piece& piece, float blend, Town& town) const;

    std::vector<Piece> pieces_;
    std::vector<uint32_t> moving_;  // indices into pieces_ still sliding or tumbling
    std::vector<Clatter> clatters_;
    float metresPerUnit_ = 0.01f;
    float clock_ = 0.0f;  // seconds into the current 25 Hz step
    uint32_t kicks_ = 0;
};

}  // namespace mu::game
