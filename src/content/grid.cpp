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
