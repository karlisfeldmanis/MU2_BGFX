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
    // LostTower.cs:919-945: the Meteorite Trap, level 90, 160-190, attack rate 450, AttackRange 3
    // -- OpenMU's 3 and not WebZen's 5, the user's (2026-10-01; docs/lost-tower-port.md
    // decision 4) -- AttackDelay 1000 ms, Flame of Evil.
    {103, "Meteorite Trap", "Object26", 3, true, 90, 160, 190, 450.0f, 500.0f},
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
    // LostTower.cs:535-697, spawn ids 550-697, the Meteorite Traps, one each in the file's order:
    // 14 on Lost Tower 1, 7 on 2, 37 on 3, 20 on 4 and 70 on 7. 45,110 carries three rows, as
    // OpenMU has it.
    {4, 103, 5, 175, 1, 0},  // 550 SouthEast
    {4, 103, 6, 175, 1, 0},  // 551 SouthEast
    {4, 103, 4, 175, 1, 0},  // 552 SouthEast
    {4, 103, 15, 172, 1, 0},  // 553 SouthEast
    {4, 103, 15, 173, 1, 0},  // 554 SouthEast
    {4, 103, 15, 174, 1, 0},  // 555 SouthEast
    {4, 103, 14, 207, 1, 0},  // 556 SouthEast
    {4, 103, 4, 194, 1, 0},  // 557 SouthEast
    {4, 103, 5, 194, 1, 0},  // 558 SouthEast
    {4, 103, 6, 194, 1, 0},  // 559 SouthEast
    {4, 103, 14, 208, 1, 0},  // 560 SouthEast
    {4, 103, 5, 237, 1, 0},  // 561 SouthEast
    {4, 103, 6, 237, 1, 0},  // 562 SouthEast
    {4, 103, 7, 237, 1, 0},  // 563 SouthEast
    {4, 103, 26, 104, 1, 0},  // 564 SouthEast
    {4, 103, 26, 103, 1, 0},  // 565 SouthEast
    {4, 103, 26, 102, 1, 0},  // 566 SouthEast
    {4, 103, 26, 101, 1, 0},  // 567 SouthEast
    {4, 103, 30, 101, 1, 0},  // 568 SouthEast
    {4, 103, 30, 100, 1, 0},  // 569 SouthEast
    {4, 103, 30, 99, 1, 0},  // 570 SouthEast
    {4, 103, 29, 125, 0, -1},  // 571 SouthWest
    {4, 103, 28, 125, 0, -1},  // 572 SouthWest
    {4, 103, 27, 125, 0, -1},  // 573 SouthWest
    {4, 103, 26, 125, 0, -1},  // 574 SouthWest
    {4, 103, 27, 128, 0, -1},  // 575 SouthWest
    {4, 103, 27, 129, 0, -1},  // 576 SouthWest
    {4, 103, 27, 130, 0, -1},  // 577 SouthWest
    {4, 103, 21, 243, 1, 0},  // 578 SouthEast
    {4, 103, 22, 243, 1, 0},  // 579 SouthEast
    {4, 103, 23, 243, 1, 0},  // 580 SouthEast
    {4, 103, 45, 110, 0, -1},  // 581 SouthWest
    {4, 103, 45, 110, 0, -1},  // 582 SouthWest
    {4, 103, 45, 110, 0, 1},  // 583 NorthEast
    {4, 103, 45, 109, 0, 1},  // 584 NorthEast
    {4, 103, 45, 108, 0, 1},  // 585 NorthEast
    {4, 103, 38, 110, 0, 1},  // 586 NorthEast
    {4, 103, 38, 109, 0, 1},  // 587 NorthEast
    {4, 103, 38, 108, 0, 1},  // 588 NorthEast
    {4, 103, 35, 125, 0, -1},  // 589 SouthWest
    {4, 103, 34, 125, 0, -1},  // 590 SouthWest
    {4, 103, 33, 125, 0, -1},  // 591 SouthWest
    {4, 103, 36, 125, 0, -1},  // 592 SouthWest
    {4, 103, 37, 119, 1, 0},  // 593 SouthEast
    {4, 103, 37, 120, 1, 0},  // 594 SouthEast
    {4, 103, 34, 128, 0, -1},  // 595 SouthWest
    {4, 103, 34, 129, 0, -1},  // 596 SouthWest
    {4, 103, 34, 130, 0, -1},  // 597 SouthWest
    {4, 103, 47, 131, 0, -1},  // 598 SouthWest
    {4, 103, 47, 132, 0, -1},  // 599 SouthWest
    {4, 103, 41, 132, 0, -1},  // 600 SouthWest
    {4, 103, 41, 133, 0, -1},  // 601 SouthWest
    {4, 103, 36, 133, 0, -1},  // 602 SouthWest
    {4, 103, 36, 134, 0, -1},  // 603 SouthWest
    {4, 103, 46, 169, 1, 0},  // 604 SouthEast
    {4, 103, 46, 170, 1, 0},  // 605 SouthEast
    {4, 103, 46, 171, 1, 0},  // 606 SouthEast
    {4, 103, 46, 172, 1, 0},  // 607 SouthEast
    {4, 103, 42, 245, 1, 0},  // 608 SouthEast
    {4, 103, 42, 246, 1, 0},  // 609 SouthEast
    {4, 103, 52, 114, 1, 0},  // 610 SouthEast
    {4, 103, 53, 114, 1, 0},  // 611 SouthEast
    {4, 103, 54, 114, 1, 0},  // 612 SouthEast
    {4, 103, 53, 178, 1, 0},  // 613 SouthEast
    {4, 103, 54, 178, 1, 0},  // 614 SouthEast
    {4, 103, 52, 178, 1, 0},  // 615 SouthEast
    {4, 103, 51, 198, 1, 0},  // 616 SouthEast
    {4, 103, 51, 199, 1, 0},  // 617 SouthEast
    {4, 103, 51, 200, 1, 0},  // 618 SouthEast
    {4, 103, 51, 201, 1, 0},  // 619 SouthEast
    {4, 103, 85, 120, 1, 0},  // 620 SouthEast
    {4, 103, 86, 120, 1, 0},  // 621 SouthEast
    {4, 103, 84, 120, 1, 0},  // 622 SouthEast
    {4, 103, 93, 131, 1, 0},  // 623 SouthEast
    {4, 103, 93, 130, 1, 0},  // 624 SouthEast
    {4, 103, 93, 132, 1, 0},  // 625 SouthEast
    {4, 103, 82, 175, 1, 0},  // 626 SouthEast
    {4, 103, 83, 175, 1, 0},  // 627 SouthEast
    {4, 103, 84, 175, 1, 0},  // 628 SouthEast
    {4, 103, 85, 175, 1, 0},  // 629 SouthEast
    {4, 103, 82, 184, 1, 0},  // 630 SouthEast
    {4, 103, 83, 184, 1, 0},  // 631 SouthEast
    {4, 103, 84, 184, 1, 0},  // 632 SouthEast
    {4, 103, 82, 201, 1, 0},  // 633 SouthEast
    {4, 103, 83, 201, 1, 0},  // 634 SouthEast
    {4, 103, 84, 201, 1, 0},  // 635 SouthEast
    {4, 103, 93, 243, 1, 0},  // 636 SouthEast
    {4, 103, 93, 244, 1, 0},  // 637 SouthEast
    {4, 103, 93, 245, 1, 0},  // 638 SouthEast
    {4, 103, 93, 246, 1, 0},  // 639 SouthEast
    {4, 103, 100, 185, 1, 0},  // 640 SouthEast
    {4, 103, 101, 185, 1, 0},  // 641 SouthEast
    {4, 103, 102, 185, 1, 0},  // 642 SouthEast
    {4, 103, 103, 185, 1, 0},  // 643 SouthEast
    {4, 103, 98, 225, 1, 0},  // 644 SouthEast
    {4, 103, 99, 225, 1, 0},  // 645 SouthEast
    {4, 103, 100, 225, 1, 0},  // 646 SouthEast
    {4, 103, 101, 225, 1, 0},  // 647 SouthEast
    {4, 103, 111, 227, 1, 0},  // 648 SouthEast
    {4, 103, 110, 227, 1, 0},  // 649 SouthEast
    {4, 103, 107, 242, 1, 0},  // 650 SouthEast
    {4, 103, 107, 243, 1, 0},  // 651 SouthEast
    {4, 103, 107, 244, 1, 0},  // 652 SouthEast
    {4, 103, 126, 104, 1, 0},  // 653 SouthEast
    {4, 103, 126, 105, 1, 0},  // 654 SouthEast
    {4, 103, 126, 106, 1, 0},  // 655 SouthEast
    {4, 103, 126, 107, 1, 0},  // 656 SouthEast
    {4, 103, 126, 115, 1, 0},  // 657 SouthEast
    {4, 103, 127, 115, 1, 0},  // 658 SouthEast
    {4, 103, 125, 115, 1, 0},  // 659 SouthEast
    {4, 103, 123, 135, 1, 0},  // 660 SouthEast
    {4, 103, 123, 134, 1, 0},  // 661 SouthEast
    {4, 103, 123, 133, 1, 0},  // 662 SouthEast
    {4, 103, 123, 132, 1, 0},  // 663 SouthEast
    {4, 103, 121, 208, 1, 0},  // 664 SouthEast
    {4, 103, 122, 208, 1, 0},  // 665 SouthEast
    {4, 103, 120, 208, 1, 0},  // 666 SouthEast
    {4, 103, 112, 227, 1, 0},  // 667 SouthEast
    {4, 103, 113, 227, 1, 0},  // 668 SouthEast
    {4, 103, 132, 126, 1, 0},  // 669 SouthEast
    {4, 103, 133, 126, 1, 0},  // 670 SouthEast
    {4, 103, 134, 126, 1, 0},  // 671 SouthEast
    {4, 103, 132, 178, 1, 0},  // 672 SouthEast
    {4, 103, 133, 178, 1, 0},  // 673 SouthEast
    {4, 103, 128, 221, 1, 0},  // 674 SouthEast
    {4, 103, 129, 221, 1, 0},  // 675 SouthEast
    {4, 103, 130, 221, 1, 0},  // 676 SouthEast
    {4, 103, 202, 45, 1, 0},  // 677 SouthEast
    {4, 103, 202, 46, 1, 0},  // 678 SouthEast
    {4, 103, 202, 47, 1, 0},  // 679 SouthEast
    {4, 103, 196, 62, 1, 0},  // 680 SouthEast
    {4, 103, 198, 62, 1, 0},  // 681 SouthEast
    {4, 103, 197, 62, 1, 0},  // 682 SouthEast
    {4, 103, 199, 62, 1, 0},  // 683 SouthEast
    {4, 103, 196, 58, 1, 0},  // 684 SouthEast
    {4, 103, 196, 57, 1, 0},  // 685 SouthEast
    {4, 103, 196, 56, 1, 0},  // 686 SouthEast
    {4, 103, 202, 48, 1, 0},  // 687 SouthEast
    {4, 103, 219, 234, 0, -1},  // 688 SouthWest
    {4, 103, 219, 235, 0, -1},  // 689 SouthWest
    {4, 103, 219, 236, 0, -1},  // 690 SouthWest
    {4, 103, 219, 237, 0, -1},  // 691 SouthWest
    {4, 103, 226, 115, 1, 0},  // 692 SouthEast
    {4, 103, 226, 116, 1, 0},  // 693 SouthEast
    {4, 103, 226, 117, 1, 0},  // 694 SouthEast
    {4, 103, 226, 233, 0, -1},  // 695 SouthWest
    {4, 103, 227, 233, 0, -1},  // 696 SouthWest
    {4, 103, 228, 233, 0, -1},  // 697 SouthWest
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
