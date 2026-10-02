#include "content/grid.h"

namespace mu::content {

void Grid::set(int size, std::vector<uint16_t> words) {
    if (size <= 0 || words.size() != size_t(size) * size_t(size)) {
        clear();
        return;
    }
    size_ = size;
    words_ = std::move(words);
}

void Grid::clear() {
    size_ = 0;
    words_.clear();
}

void Grid::change(int x1, int y1, int x2, int y2, uint16_t bits, bool set) {
    for (int row = y1; row <= y2; ++row) {
        for (int column = x1; column <= x2; ++column) {
            if (!inside(column, row)) continue;
            uint16_t& word = words_[size_t(row) * size_t(size_) + size_t(column)];
            word = set ? uint16_t(word | bits) : uint16_t(word & ~bits);
        }
    }
}

size_t Grid::blocked() const {
    size_t count = 0;
    for (int row = 0; row < size_; ++row) {
        for (int column = 0; column < size_; ++column) {
            if (!open(column, row)) ++count;
        }
    }
    return count;
}

}  // namespace mu::content
