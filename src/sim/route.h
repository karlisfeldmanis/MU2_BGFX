// A* over MU's tiles, with MU's own costs and MU's own tie-breaking.
//
// Three things here are the client's rather than a textbook's, and each changes where a walker
// actually puts its feet:
//
//   * **5 straight and 7 diagonal.** MU truncates 5 x 1.414 to 7, and that truncation is the
//     behaviour: a diagonal is cheaper here than it is in the world, so a route prefers a
//     staircase where a fair cost would go round. Route.cs:85-88.
//   * **The client's estimate**, an octile distance scaled by three quarters with one quirk: a
//     tile exactly one step diagonally away is measured as though it were one step straight,
//     which makes the final approach slightly cheaper than it is. Under-estimating keeps A*
//     admissible, so the route is still shortest under these costs.
//   * **The neighbour order**, which is only a tie-break and is exactly why it is copied: ties
//     are everywhere on an open field, and a different order gives a different-looking walk
//     through the same number of tiles.
//
// And one thing that is not the client's: a diagonal step is refused when both of its
// orthogonal neighbours are wall, so nothing slips through the corner of a building.
//
// State is generation-stamped rather than cleared. MU2 measured a quarter of a millisecond a
// tick spent wiping 64 KB up to four times in one plan; here a search is a number, a tile's
// state counts only if it was written under that number, and beginning a search is adding one.
// The scratch is allocated once and never again -- foundation 8's "no allocation in a step"
// includes the router.
#pragma once

#include <cstdint>
#include <vector>

#include "content/grid.h"

namespace mu::sim {

// One tile of a route.
struct Step {
    int16_t column = 0;
    int16_t row = 0;
};

class Router {
public:
    // The grid is borrowed and outlives the router. Allocates here and nowhere else.
    void open(const content::Grid* grid);

    // The route from one tile to another, `out` holding the tiles to walk THROUGH, the start
    // excluded and the goal included. False when there is no route, when the goal is the start,
    // or when either end is off the map.
    //
    // `wall` is MU's own threshold argument: kWallCharacter for a body that may not share a
    // tile, kWallNoMove for the relaxed pass.
    bool plan(int fromColumn, int fromRow, int toColumn, int toRow, uint16_t wall,
              std::vector<Step>& out);

    // Whether a straight line between two points, in tiles, touches only open tiles. Exact: the
    // grid is walked boundary to boundary as a ray through voxels, and a line through a corner
    // exactly needs both tiles beside it open, the same rule `corner` keeps. MU2's Route.Sees.
    bool sees(float fromX, float fromY, float toX, float toY, uint16_t wall) const;

    // The route pulled tight, in place: each leg runs from where the last one ended to the
    // furthest tile of the route still in a straight clear line, stopping at the first that is
    // not (carrying on past it would let a later tile that comes back into view cut a corner).
    // The first leg starts where the body really stands. MU2's Route.Along -- the zig-zag of an
    // eight-way search on a tile grid, straightened. Allocates nothing.
    void pull(float fromX, float fromY, uint16_t wall, std::vector<Step>& route) const;

    // The nearest tile to (column, row) that something may stand on, searched outward in
    // rings, straight before diagonal within a ring. Answered before the search rather than
    // after, so the plan, the marker and the walk all agree about where the walk ends.
    bool nearestOpen(int column, int row, uint16_t wall, int rings, int* outColumn,
                     int* outRow) const;

    // What the router has done since it opened. In the log because a route's cost is a claim
    // and an unmeasured claim is a guess.
    uint64_t searches() const { return searches_; }
    uint64_t failures() const { return failures_; }
    uint64_t expansions() const { return expansions_; }
    uint32_t worstExpansions() const { return worstExpansions_; }

private:
    int index(int column, int row) const { return row * size_ + column; }
    bool corner(int column, int row, int dx, int dy, uint16_t wall) const;

    const content::Grid* grid_ = nullptr;
    int size_ = 0;

    // Generation-stamped scratch: `stamp_[i] == generation_` says the rest is this search's.
    std::vector<uint32_t> stamp_;
    std::vector<int32_t> cost_;
    std::vector<int32_t> from_;
    std::vector<uint32_t> closed_;  // the generation this tile was expanded under
    // The open set as a binary heap of (estimate, tile). A heap rather than a scan because
    // Lorencia's open field makes the frontier hundreds of tiles wide.
    std::vector<uint64_t> heap_;
    uint32_t generation_ = 0;

    uint64_t searches_ = 0;
    uint64_t failures_ = 0;
    uint64_t expansions_ = 0;
    uint32_t worstExpansions_ = 0;
};

}  // namespace mu::sim
