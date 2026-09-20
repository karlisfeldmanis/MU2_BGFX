// MU's attribute grid, and the one test that says whether a thing may stand on a tile.
//
// This file exists because there were two definitions of "blocked" in this engine and they
// were not the same one. `content/ground.cpp` asked `(a & 0x04) == 0 && (a & 0x08) == 0`;
// MU asks `(word & ~NonBlocking) < wall`, a numeric comparison against a level, so any
// attribute at or above that level blocks -- including ones with nothing to do with
// movement. Measured on our own copies of Lorencia and Noria the two agree on every tile but
// one (the single Character tile at (221, 11), which the bit test called walkable), because
// the only words those maps use are 0, 1, 2, 4 and 5. They differ everywhere on the first
// map that sets Height on a NoMove tile. MU's is the one kept, and it is kept in exactly one
// function so the ground and the sim cannot drift apart.
//
// The grid is data and not a picture: no mips, no filtering, and the bounds test comes ahead
// of the bit read. Off the map is NOT open ground, although MU's word for open ground is 0,
// which is the trap `ground.cpp` already names.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mu::content {

// MU's own bits, as MU2's Route.cs:65-82 names them.
enum GridFlags : uint16_t {
    kSafeZone = 0x0001,
    kCharacter = 0x0002,
    kNoMove = 0x0004,
    kNoGround = 0x0008,
    kAction = 0x0020,
    kHeight = 0x0040,
    kCameraUp = 0x0080,
};

// The three that never block. Route.cs:82.
constexpr uint16_t kNonBlocking = kAction | kHeight | kCameraUp;

// The strict pass: a person may not stand where another body is. Route.cs:186 with `wall` at
// Character, which is the pass a walk uses. The relaxed pass, `kNoMove`, is MU's own second
// argument -- it lets a route touch a tile another body claims -- and it is why this is an
// argument and not a constant.
constexpr uint16_t kWallCharacter = kCharacter;
constexpr uint16_t kWallNoMove = kNoMove;

// MU's test, and the only one in this engine.
inline bool passable(uint16_t word, uint16_t wall) {
    return uint16_t(word & ~kNonBlocking) < wall;
}

class Grid {
public:
    // `words` is row-major [row][column], one MU attribute word a tile.
    void set(int size, std::vector<uint16_t> words);
    void clear();

    bool empty() const { return words_.empty(); }
    int size() const { return size_; }

    bool inside(int column, int row) const {
        return column >= 0 && row >= 0 && column < size_ && row < size_;
    }
    // 0 off the map, which is also the word for open ground: ask open() instead.
    uint16_t at(int column, int row) const {
        if (!inside(column, row)) return 0;
        return words_[size_t(row) * size_t(size_) + size_t(column)];
    }
    bool open(int column, int row, uint16_t wall = kWallCharacter) const {
        return inside(column, row) && passable(at(column, row), wall);
    }
    // Where nothing attacks and nothing is attacked. Terrain.cs:102.
    bool safe(int column, int row) const { return (at(column, row) & kSafeZone) != 0; }

    size_t blocked() const;
    const std::vector<uint16_t>& words() const { return words_; }

private:
    int size_ = 0;
    std::vector<uint16_t> words_;
};

}  // namespace mu::content
