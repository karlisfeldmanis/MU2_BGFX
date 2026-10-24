// UTF-8, as the game's text is held: letters by code point for whoever draws, measures, wraps or
// cleans a line (Gordichan on Discord, 2026-10-24: "the 'ñ' key ... and accent marks", and the
// user: "basically allow to speak on any language on chat").
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mu::core::utf8 {

// The code point at `at`, and `at` moved past it. A byte that starts no good sequence is taken
// as Latin-1, alone: the few strings that still write the middle dot as "\xB7" keep drawing it.
inline uint32_t next(const std::string& s, size_t& at) {
    const auto b = [&](size_t i) { return uint8_t(s[i]); };
    const uint8_t lead = b(at);
    int more = 0;
    uint32_t cp = 0, least = 0;
    if (lead < 0x80) {
        ++at;
        return lead;
    } else if ((lead & 0xE0) == 0xC0) {
        more = 1, cp = lead & 0x1Fu, least = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
        more = 2, cp = lead & 0x0Fu, least = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
        more = 3, cp = lead & 0x07u, least = 0x10000;
    } else {
        ++at;
        return lead;
    }
    if (at + size_t(more) >= s.size()) {  // cut short
        ++at;
        return lead;
    }
    for (int i = 1; i <= more; ++i) {
        if ((b(at + size_t(i)) & 0xC0) != 0x80) {
            ++at;
            return lead;
        }
        cp = (cp << 6) | (b(at + size_t(i)) & 0x3Fu);
    }
    // Overlong, a surrogate, or past Unicode: not a letter, so the lone byte.
    if (cp < least || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) {
        ++at;
        return lead;
    }
    at += size_t(more) + 1;
    return cp;
}

// Where the letter after the one at `at` starts.
inline size_t after(const std::string& s, size_t at) {
    if (at >= s.size()) return s.size();
    next(s, at);
    return at;
}

// A code point as UTF-8, appended.
inline void put(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

// A letter of a script written without spaces -- Chinese, Japanese, Korean and their full-width
// forms -- which a line may break before or after.
inline bool unspaced(uint32_t cp) {
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
           (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF) ||
           (cp >= 0x20000 && cp <= 0x3FFFF);
}

// The next place past `at` a line may break: the space that ends the word starting at `at` (a
// space at `at` itself is the word's own), or either side of an unspaced letter.
inline size_t nextBreak(const std::string& s, size_t at) {
    if (at >= s.size()) return s.size();
    bool was = unspaced(next(s, at));
    while (at < s.size() && s[at] != ' ') {
        size_t probe = at;
        const bool is = unspaced(next(s, probe));
        if (was || is) break;
        at = probe;
    }
    return at;
}

}  // namespace mu::core::utf8
