// A cursor over a cooked file that cannot be walked off the end of.
//
// Every read is checked and the first failure makes every later one fail too, so a parser
// reads straight down the file and asks once, at the end, whether any of it was out of
// bounds. A file this engine wrote is not a file this engine may trust: it is on disk, where
// anything can edit it.
//
// It lived inside content/cooked.cpp until the rules tables wanted the same thing; two copies
// of a bounds check are two chances to fix only one of them.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

namespace mu::content {

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool take(void* out, size_t bytes) {
        if (failed_ || at_ + bytes > size_) {
            failed_ = true;
            return false;
        }
        std::memcpy(out, data_ + at_, bytes);
        at_ += bytes;
        return true;
    }

    template <typename T>
    bool read(T& value) {
        return take(&value, sizeof(T));
    }

    // A length-prefixed string, as write_string() in tools/cook.py writes it.
    bool readString(std::string& value) {
        uint16_t length = 0;
        if (!read(length)) return false;
        if (failed_ || at_ + length > size_) {
            failed_ = true;
            return false;
        }
        value.assign(reinterpret_cast<const char*>(data_ + at_), length);
        at_ += length;
        return true;
    }

    bool failed() const { return failed_; }
    size_t left() const { return failed_ ? 0 : size_ - at_; }

private:
    const uint8_t* data_;
    size_t size_;
    size_t at_ = 0;
    bool failed_ = false;
};

// A count is the one number in these files that can make the reader allocate, so it is
// checked against what is left to read before anything is reserved. Without this a file
// claiming four billion vertices asks for 192 GB before it fails.
inline bool plausible(const Reader& reader, uint32_t count, size_t each) {
    return each == 0 || count <= reader.left() / each;
}

}  // namespace mu::content
