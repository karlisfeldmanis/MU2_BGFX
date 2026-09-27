// The vault: what Baz keeps for the account, and the Zen beside it.
//
// MU's warehouse is `CNewUIStorageInventory`'s control, `Create(STORAGE_TYPE::VAULT, ..., 8, 15)`
// -- eight across and fifteen down, OpenMU's `InventoryConstants.WarehouseRows` 15 times its
// `RowSize` 8 -- with an item recorded once at the top-left of the rectangle it covers, as in the
// bag. A cell here is `row * 8 + column`, from 0 to 119, with no worn slots in front of it.
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
    kVaultColumns = 8,
    kVaultRows = 15,
    kVaultCells = kVaultColumns * kVaultRows,  // 120, WarehouseSize
};

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
