#include "sim/vault.h"

#include <algorithm>

namespace mu::sim {
namespace {

const Held kNothing{};

const content::ItemRow* rowOf(const content::Tables& tables, const Held& what) {
    if (what.empty() || size_t(what.item) >= tables.items.size()) return nullptr;
    return &tables.items[size_t(what.item)];
}

}  // namespace

const Held& Vault::operator[](int cell) const {
    return cell >= 0 && cell < kVaultCells ? cells_[cell] : kNothing;
}

void Vault::put(int cell, const Held& what) {
    if (cell < 0 || cell >= kVaultCells) return;
    cells_[cell] = what;
    ++version_;
}

Held Vault::lift(int cell) {
    if (cell < 0 || cell >= kVaultCells || cells_[cell].empty()) return Held{};
    const Held was = cells_[cell];
    cells_[cell] = Held{};
    ++version_;
    return was;
}

void Vault::clear() {
    for (Held& one : cells_) one = Held{};
    zen_ = 0;
    ++version_;
}

int Vault::holder(const content::Tables& tables, int cell) const {
    if (cell < 0 || cell >= kVaultCells) return -1;
    const int cc = cell % kVaultColumns, cr = cell / kVaultColumns;
    for (int at = 0; at < kVaultCells; ++at) {
        const content::ItemRow* row = rowOf(tables, cells_[at]);
        if (!row) continue;
        const int c = at % kVaultColumns, r = at / kVaultColumns;
        if (cc >= c && cc < c + row->width && cr >= r && cr < r + row->height) return at;
    }
    return -1;
}

bool Vault::room(const content::Tables& tables, int cell, int width, int height,
                 int ignoring) const {
    if (cell < 0 || cell >= kVaultCells) return false;
    const int column = cell % kVaultColumns, row = cell / kVaultColumns;
    width = std::max(1, width);
    height = std::max(1, height);
    if (column + width > kVaultColumns || row + height > kVaultRows) return false;
    for (int down = 0; down < height; ++down) {
        for (int across = 0; across < width; ++across) {
            const int held = holder(tables, (row + down) * kVaultColumns + column + across);
            if (held >= 0 && held != ignoring) return false;
        }
    }
    return true;
}

int Vault::free(const content::Tables& tables, int width, int height) const {
    for (int cell = 0; cell < kVaultCells; ++cell) {
        if (room(tables, cell, width, height)) return cell;
    }
    return -1;
}

}  // namespace mu::sim
