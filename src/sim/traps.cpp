#include "sim/traps.h"

#include <cmath>

namespace mu::sim {
namespace {

// Dungeon.cs:955-1034. Every one: MoveRange 0, AttackDelay 1000 ms, RespawnDelay 3 s (moot, a trap
// does not die), level 80, 1000 health, attack rate 400, defence rate 500, no drops.
constexpr TrapKind kKinds[] = {
    {100, "Lance Trap", "Object40", 4, false, 80, 100, 110, 400.0f, 500.0f},
    {101, "Iron Stick Trap", "Object41", 0, true, 80, 110, 130, 400.0f, 500.0f},
    {102, "Fire Trap", "Object52", 2, false, 80, 130, 150, 400.0f, 500.0f},
};

// Dungeon.cs:560-618, spawn ids 701-768, one trap each, in the file's order.
constexpr TrapSpot kSpots[] = {
    {1, 101, 10, 26, 0, -1},  // 701 SouthWest
    {1, 101, 11, 26, 0, -1},  // 702 SouthWest
    {1, 101, 27, 12, 0, -1},  // 703 SouthWest
    {1, 101, 24, 5, 0, -1},  // 704 SouthWest
    {1, 101, 24, 4, 0, -1},  // 705 SouthWest
    {1, 101, 27, 11, 0, -1},  // 706 SouthWest
    {1, 101, 23, 24, 0, -1},  // 707 SouthWest
    {1, 101, 27, 21, 0, -1},  // 708 SouthWest
    {1, 101, 19, 19, 0, -1},  // 709 SouthWest
    {1, 101, 22, 24, 0, -1},  // 720 SouthWest
    {1, 101, 23, 29, 0, -1},  // 721 SouthWest
    {1, 101, 23, 28, 0, -1},  // 722 SouthWest
    {1, 101, 33, 9, 0, -1},  // 723 SouthWest
    {1, 101, 35, 9, 0, -1},  // 724 SouthWest
    {1, 101, 39, 18, 0, -1},  // 725 SouthWest
    {1, 101, 39, 17, 0, -1},  // 726 SouthWest
    {1, 101, 39, 16, 0, -1},  // 727 SouthWest
    {1, 102, 45, 224, 1, 0},  // 728 SouthEast
    {1, 101, 48, 193, 0, -1},  // 729 SouthWest
    {1, 101, 49, 193, 0, -1},  // 730 SouthWest
    {1, 102, 66, 71, 1, 0},  // 731 SouthEast
    {1, 102, 80, 61, 0, -1},  // 732 SouthWest
    {1, 101, 90, 164, 0, -1},  // 733 SouthWest
    {1, 101, 92, 164, 0, -1},  // 734 SouthWest
    {1, 101, 91, 164, 0, -1},  // 735 SouthWest
    {1, 100, 126, 99, 0, -1},  // 736 SouthWest
    {1, 100, 123, 99, 0, -1},  // 737 SouthWest
    {1, 100, 120, 99, 0, -1},  // 738 SouthWest
    {1, 100, 117, 99, 0, -1},  // 739 SouthWest
    {1, 100, 136, 95, 0, -1},  // 740 SouthWest
    {1, 100, 139, 95, 0, -1},  // 741 SouthWest
    {1, 101, 128, 212, 0, -1},  // 742 SouthWest
    {1, 101, 130, 213, 0, -1},  // 743 SouthWest
    {1, 101, 143, 214, 0, -1},  // 744 SouthWest
    {1, 101, 155, 230, 0, 1},  // 745 NorthEast
    {1, 100, 172, 12, 0, -1},  // 746 SouthWest
    {1, 100, 166, 12, 0, -1},  // 747 SouthWest
    {1, 102, 169, 12, 0, -1},  // 748 SouthWest
    {1, 102, 175, 12, 0, -1},  // 749 SouthWest
    {1, 100, 178, 12, 0, -1},  // 750 SouthWest
    {1, 100, 177, 103, 0, -1},  // 751 SouthWest
    {1, 100, 180, 103, 0, -1},  // 752 SouthWest
    {1, 100, 183, 103, 0, -1},  // 753 SouthWest
    {1, 100, 186, 103, 0, -1},  // 754 SouthWest
    {1, 100, 189, 103, 0, -1},  // 755 SouthWest
    {1, 100, 186, 151, 1, 0},  // 756 SouthEast
    {1, 100, 196, 33, 0, -1},  // 757 SouthWest
    {1, 100, 193, 33, 0, -1},  // 758 SouthWest
    {1, 100, 202, 93, 0, -1},  // 759 SouthWest
    {1, 100, 205, 93, 0, -1},  // 760 SouthWest
    {1, 100, 198, 130, 0, -1},  // 761 SouthWest
    {1, 100, 202, 150, 0, -1},  // 762 SouthWest
    {1, 100, 232, 46, 1, 0},  // 763 SouthEast
    {1, 100, 232, 40, 1, 0},  // 764 SouthEast
    {1, 100, 232, 37, 1, 0},  // 765 SouthEast
    {1, 100, 227, 61, 0, -1},  // 766 SouthWest
    {1, 100, 229, 93, 0, -1},  // 767 SouthWest
    {1, 100, 232, 93, 0, -1},  // 768 SouthWest
};

}  // namespace

const TrapKind* trapKind(int32_t number) {
    for (const TrapKind& kind : kKinds) {
        if (kind.number == number) return &kind;
    }
    return nullptr;
}

const TrapSpot* trapSpots(size_t* count) {
    *count = sizeof(kSpots) / sizeof(kSpots[0]);
    return kSpots;
}

int octantOf(int dx, int dy) {
    if (dx == 0 && dy == 0) return -1;
    // atan2 + pi over pi/4, rounded as Convert.ToInt32 rounds: a half to the even integer, which
    // is std::nearbyint under the default rounding mode.
    const double eighths = (std::atan2(double(dy), double(dx)) + 3.14159265358979323846) /
                           (3.14159265358979323846 / 4.0);
    return int(std::nearbyint(eighths)) % 8;
}

}  // namespace mu::sim
