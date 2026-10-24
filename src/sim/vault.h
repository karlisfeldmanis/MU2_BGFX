// The vault: what Baz keeps for the account, and the Zen beside it.
//
// MU's warehouse is `CNewUIStorageInventory`'s control, `Create(STORAGE_TYPE::VAULT, ..., 8, 15)`
// -- eight across and fifteen down, OpenMU's `InventoryConstants.WarehouseRows` 15 times its
// `RowSize` 8 -- with an item recorded once at the top-left of the rectangle it covers, as in the
// bag.
//
// Ours is wider: eighteen across, the two columns of windows beside the bag at the bag's own
// pitch, and MU's fifteen down, so its rows end on the bag's line (the user, 2026-10-24: "we
// want more space", from the Wide vault proposal, claude.ai/artifact/YQtc78Viuysd2qSnrCS1r4). A
// cell is `row * 18 + column`, from 0 to 269, with no worn slots in front of it. A vault kept at
// MU's eight across -- every one before kKeptLayout 15 and every save without "columns" -- is
// read into the same column and row of this one (widenedCell), so nothing moves on the screen.
//
// It belongs to the ACCOUNT and not to the character, as MU's does (OpenMU keeps it on
// `Account.Vault`), which is why the save writes it to a file of its own and why nothing here
// asks who is looking in it.
//
// Not here, and why: the lock and its PIN keypad (one player on one Mac has nobody to lock it
// against), the extended vault (a Season 6 purchase), and the Season-era storage fee
// (`CNewUIStorageInventory::RenderText` prints level^2 x 0.04; 0.75 and OpenMU charge nothing).
#pragma once

#include <cstdint>

#include "content/tables.h"
#include "sim/items.h"

namespace mu::sim {

enum : int {
    kVaultColumns = 18,
    kVaultRows = 15,
    kVaultCells = kVaultColumns * kVaultRows,  // 270
    // MU's eight across, WarehouseSize 120: the vault as it was kept until 2026-10-24.
    kOldVaultColumns = 8,
    kOldVaultCells = kOldVaultColumns * kVaultRows,
};

// A cell of an eight-across vault, as the cell at the same column and row of this one.
constexpr int widenedCell(int old) {
    return old / kOldVaultColumns * kVaultColumns + old % kOldVaultColumns;
}

// Baz The Vault Keeper, MU's NPC 240, whose window is OpenMU's NpcWindow.VaultStorage.
constexpr int kVaultKeeper = 240;

class Vault {
public:
    const Held& operator[](int cell) const;
    // Counted, as the satchel's is, so the window redraws when something moved.
    uint32_t version() const { return version_; }
    int64_t zen() const { return zen_; }

    void put(int cell, const Held& what);
    Held lift(int cell);
    void setZen(int64_t zen) {
        zen_ = zen;
        ++version_;
    }
    void clear();

    // The cell whose item covers this one, or -1. The satchel's walk.
    int holder(const content::Tables& tables, int cell) const;
    // Whether a thing of this size would sit with its top-left at `cell`, ignoring one item
    // already there (the one being moved). Off the grid is a refusal, not a clipped item.
    bool room(const content::Tables& tables, int cell, int width, int height,
              int ignoring = -1) const;
    // The first cell it fits in, top-left first, or -1.
    int free(const content::Tables& tables, int width, int height) const;

private:
    Held cells_[kVaultCells];
    int64_t zen_ = 0;
    uint32_t version_ = 0;
};

}  // namespace mu::sim
