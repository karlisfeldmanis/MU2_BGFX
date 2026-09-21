#include "sim/route.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mu::sim {
namespace {

// Route.cs:85-88. The client truncates 5 x 1.414 to 7.
constexpr int kStraight = 5;
constexpr int kDiagonal = 7;

// Route.cs:96-102, "neighbour offsets, in the client's order".
constexpr int8_t kAround[8][2] = {
    {-1, -1}, {0, -1}, {1, -1},
    {-1, 0},           {1, 0},
    {-1, 1},  {0, 1},  {1, 1},
};

// Route.cs:700-724, the client's own estimate. Octile, scaled by three quarters so it
// under-estimates and A* stays admissible, with the one-step-diagonal quirk kept.
int estimate(int goalColumn, int goalRow, int column, int row) {
    int across = std::abs(column - goalColumn);
    int down = std::abs(row - goalRow);
    if (across == 1 && down == 1) down = 0;
    return ((std::abs(across - down) * kStraight) + (std::min(across, down) * kDiagonal) + 1) *
           3 / 4;
}

}  // namespace

void Router::open(const content::Grid* grid) {
    grid_ = grid;
    size_ = grid ? grid->size() : 0;
    const size_t tiles = size_t(size_) * size_t(size_);
    stamp_.assign(tiles, 0);
    cost_.assign(tiles, 0);
    from_.assign(tiles, -1);
    closed_.assign(tiles, 0);
    heap_.clear();
    heap_.reserve(tiles / 4);
    generation_ = 0;
    searches_ = failures_ = expansions_ = 0;
    worstExpansions_ = 0;
}

// A diagonal may not slip between two walls. A deliberate departure from the client, which
// allows the slip because its server plans the same way and the two agree with each other;
// here there is nobody to agree with and the only judge is whether it looks like walking
// through the corner of a building. Route.cs:458-461.
bool Router::corner(int column, int row, int dx, int dy, uint16_t wall) const {
    return grid_->open(column + dx, row, wall) && grid_->open(column, row + dy, wall);
}

bool Router::sees(float fromX, float fromY, float toX, float toY, uint16_t wall) const {
    if (!grid_ || grid_->empty()) return false;
    // Into a space where a tile is the unit square from its own index, so the boundaries are
    // whole numbers. Double, because a line exactly through a corner is decided by equality.
    const double x = double(fromX) + 0.5, y = double(fromY) + 0.5;
    const double dx = double(toX) - double(fromX), dy = double(toY) - double(fromY);
    int column = int(std::floor(x)), row = int(std::floor(y));
    const int lastColumn = int(std::floor(double(toX) + 0.5));
    const int lastRow = int(std::floor(double(toY) + 0.5));
    if (!grid_->open(column, row, wall)) return false;

    const int stepX = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    const int stepY = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    constexpr double kNever = 1e300;
    double farX = stepX == 0 ? kNever : (stepX > 0 ? column + 1 - x : x - column) / std::fabs(dx);
    double farY = stepY == 0 ? kNever : (stepY > 0 ? row + 1 - y : y - row) / std::fabs(dy);
    const double perX = stepX == 0 ? kNever : 1.0 / std::fabs(dx);
    const double perY = stepY == 0 ? kNever : 1.0 / std::fabs(dy);

    const int most = std::abs(lastColumn - column) + std::abs(lastRow - row) + 2;
    for (int taken = 0; taken < most; ++taken) {
        if (column == lastColumn && row == lastRow) return true;
        if (std::fabs(farX - farY) < 1e-9) {
            // Exactly through the corner: the diagonal squeeze `corner` refuses.
            if (!grid_->open(column + stepX, row, wall) || !grid_->open(column, row + stepY, wall)) {
                return false;
            }
            column += stepX;
            row += stepY;
            farX += perX;
            farY += perY;
        } else if (farX < farY) {
            column += stepX;
            farX += perX;
        } else {
            row += stepY;
            farY += perY;
        }
        if (!grid_->open(column, row, wall)) return false;
    }
    return column == lastColumn && row == lastRow;
}

void Router::pull(float fromX, float fromY, uint16_t wall, std::vector<Step>& route) const {
    if (route.size() < 2) return;
    float atX = fromX, atY = fromY;
    size_t next = 0, kept = 0;
    while (next < route.size()) {
        size_t furthest = next;
        for (size_t test = next + 1; test < route.size(); ++test) {
            if (!sees(atX, atY, float(route[test].column), float(route[test].row), wall)) break;
            furthest = test;
        }
        // In place: `kept` never passes `furthest`, so nothing is overwritten before it is read.
        route[kept++] = route[furthest];
        atX = float(route[furthest].column);
        atY = float(route[furthest].row);
        next = furthest + 1;
    }
    route.resize(kept);
}

bool Router::nearestOpen(int column, int row, uint16_t wall, int rings, int* outColumn,
                         int* outRow) const {
    if (!grid_ || grid_->empty()) return false;
    if (grid_->open(column, row, wall)) {
        *outColumn = column;
        *outRow = row;
        return true;
    }
    // Outward in rings, and within a ring straight before diagonal -- the direction something
    // was heading is likelier to be the one it wants. Route.cs:225-244.
    for (int ring = 1; ring <= rings; ++ring) {
        int best = -1, bestColumn = 0, bestRow = 0;
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::abs(dx) != ring && std::abs(dy) != ring) continue;
                if (!grid_->open(column + dx, row + dy, wall)) continue;
                const int away = dx * dx + dy * dy;
                if (best < 0 || away < best) {
                    best = away;
                    bestColumn = column + dx;
                    bestRow = row + dy;
                }
            }
        }
        if (best >= 0) {
            *outColumn = bestColumn;
            *outRow = bestRow;
            return true;
        }
    }
    return false;
}

bool Router::plan(int fromColumn, int fromRow, int toColumn, int toRow, uint16_t wall,
                  std::vector<Step>& out) {
    out.clear();
    if (!grid_ || grid_->empty()) return false;
    if (!grid_->inside(fromColumn, fromRow) || !grid_->inside(toColumn, toRow)) return false;
    if (fromColumn == toColumn && fromRow == toRow) return false;
    // The goal is resolved to somewhere standable BEFORE the search, so that the plan, the
    // marker and the walk all agree about where the walk ends. A search toward a tile inside a
    // wall explores the whole map and then fails.
    if (!grid_->open(toColumn, toRow, wall)) {
        if (!nearestOpen(toColumn, toRow, wall, 4, &toColumn, &toRow)) return false;
        if (fromColumn == toColumn && fromRow == toRow) return false;
    }

    ++searches_;
    // Adding one IS clearing. The arrays are wiped for real only when the number wraps, which
    // is once in a few years of ticks.
    if (++generation_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        std::fill(closed_.begin(), closed_.end(), 0);
        generation_ = 1;
    }

    const int start = index(fromColumn, fromRow);
    const int goal = index(toColumn, toRow);
    stamp_[start] = generation_;
    cost_[start] = 0;
    from_[start] = -1;

    heap_.clear();
    // (estimate << 32) | tile, so the cheapest tile is the smallest number and a tie is broken
    // by the tile's own index. Deterministic, and NOT the client's insertion order: the
    // neighbour order below decides which equal-cost parent a tile keeps, and this decides
    // which of two equally promising tiles is opened first. Said out loud because the second
    // one is a real difference from a client that scans its open list in order.
    const auto push = [&](int f, int tile) {
        heap_.push_back((uint64_t(uint32_t(f)) << 32) | uint32_t(tile));
        std::push_heap(heap_.begin(), heap_.end(), std::greater<uint64_t>());
    };
    push(estimate(toColumn, toRow, fromColumn, fromRow), start);

    uint32_t expanded = 0;
    bool found = false;
    while (!heap_.empty()) {
        std::pop_heap(heap_.begin(), heap_.end(), std::greater<uint64_t>());
        const int tile = int(uint32_t(heap_.back() & 0xffffffffu));
        heap_.pop_back();
        // A tile can be in the heap more than once -- an entry is never removed when a cheaper
        // way to the same tile is found, only outvoted -- so a stale entry is skipped here.
        if (stamp_[tile] != generation_ || closed_[tile] == generation_) continue;
        if (tile == goal) {
            found = true;
            break;
        }
        closed_[tile] = generation_;
        ++expanded;

        const int column = tile % size_;
        const int row = tile / size_;
        for (const int8_t* step : kAround) {
            const int dx = step[0], dy = step[1];
            const int nextColumn = column + dx, nextRow = row + dy;
            if (!grid_->open(nextColumn, nextRow, wall)) continue;
            if (dx != 0 && dy != 0 && !corner(column, row, dx, dy, wall)) continue;
            const int next = index(nextColumn, nextRow);
            const int candidate = cost_[tile] + ((dx == 0 || dy == 0) ? kStraight : kDiagonal);
            if (stamp_[next] == generation_ && cost_[next] <= candidate) continue;
            stamp_[next] = generation_;
            cost_[next] = candidate;
            from_[next] = tile;
            // Reopened if it had already been expanded: the estimate above is consistent and
            // this should never fire, and a search that silently kept the dearer route would
            // be a bug nothing could see.
            closed_[next] = 0;
            push(candidate + estimate(toColumn, toRow, nextColumn, nextRow), next);
        }
    }

    expansions_ += expanded;
    worstExpansions_ = std::max(worstExpansions_, expanded);
    if (!found) {
        ++failures_;
        return false;
    }

    // Walked back from the goal and reversed in place: `out` keeps its capacity between plans,
    // which is what stops a route allocating in a step.
    for (int tile = goal; tile != start && tile >= 0; tile = from_[tile]) {
        out.push_back({int16_t(tile % size_), int16_t(tile / size_)});
    }
    std::reverse(out.begin(), out.end());
    return !out.empty();
}

}  // namespace mu::sim
