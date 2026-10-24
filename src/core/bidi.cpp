#include "core/bidi.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "core/utf8.h"

namespace mu::core::bidi {
namespace {

// An Arabic letter's four presentation forms; 0 where it has none. A letter with a final form
// and no initial joins on its right only (alef, dal, reh, waw); one with neither, not at all.
struct Forms {
    uint32_t code;
    uint32_t isolated, final, initial, medial;
};

constexpr Forms kForms[] = {
    {0x0621, 0xFE80, 0, 0, 0},                  // hamza
    {0x0622, 0xFE81, 0xFE82, 0, 0},             // alef with madda
    {0x0623, 0xFE83, 0xFE84, 0, 0},             // alef with hamza above
    {0x0624, 0xFE85, 0xFE86, 0, 0},             // waw with hamza
    {0x0625, 0xFE87, 0xFE88, 0, 0},             // alef with hamza below
    {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C},   // yeh with hamza
    {0x0627, 0xFE8D, 0xFE8E, 0, 0},             // alef
    {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92},   // beh
    {0x0629, 0xFE93, 0xFE94, 0, 0},             // teh marbuta
    {0x062A, 0xFE95, 0xFE96, 0xFE97, 0xFE98},   // teh
    {0x062B, 0xFE99, 0xFE9A, 0xFE9B, 0xFE9C},   // theh
    {0x062C, 0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0},   // jeem
    {0x062D, 0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4},   // hah
    {0x062E, 0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8},   // khah
    {0x062F, 0xFEA9, 0xFEAA, 0, 0},             // dal
    {0x0630, 0xFEAB, 0xFEAC, 0, 0},             // thal
    {0x0631, 0xFEAD, 0xFEAE, 0, 0},             // reh
    {0x0632, 0xFEAF, 0xFEB0, 0, 0},             // zain
    {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4},   // seen
    {0x0634, 0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8},   // sheen
    {0x0635, 0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC},   // sad
    {0x0636, 0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0},   // dad
    {0x0637, 0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4},   // tah
    {0x0638, 0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8},   // zah
    {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC},   // ain
    {0x063A, 0xFECD, 0xFECE, 0xFECF, 0xFED0},   // ghain
    {0x0641, 0xFED1, 0xFED2, 0xFED3, 0xFED4},   // feh
    {0x0642, 0xFED5, 0xFED6, 0xFED7, 0xFED8},   // qaf
    {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC},   // kaf
    {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0},   // lam
    {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},   // meem
    {0x0646, 0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8},   // noon
    {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC},   // heh
    {0x0648, 0xFEED, 0xFEEE, 0, 0},             // waw
    {0x0649, 0xFEEF, 0xFEF0, 0, 0},             // alef maksura
    {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},   // yeh
    {0x067E, 0xFB56, 0xFB57, 0xFB58, 0xFB59},   // peh
    {0x0686, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D},   // tcheh
    {0x0698, 0xFB8A, 0xFB8B, 0, 0},             // jeh
    {0x06A9, 0xFB8E, 0xFB8F, 0xFB90, 0xFB91},   // keheh
    {0x06AF, 0xFB92, 0xFB93, 0xFB94, 0xFB95},   // gaf
    {0x06CC, 0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF},   // farsi yeh
};

const Forms* formsOf(uint32_t cp) {
    for (const Forms& f : kForms) {
        if (f.code == cp) return &f;
    }
    return nullptr;
}

// A mark drawn over or under the letter before it, which stays after that letter however the
// line is turned, and which Arabic joins across.
bool mark(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0591 && cp <= 0x05BD) || cp == 0x05BF ||
           cp == 0x05C1 || cp == 0x05C2 || cp == 0x05C4 || cp == 0x05C5 || cp == 0x05C7 ||
           (cp >= 0x0610 && cp <= 0x061A) || (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
           (cp >= 0x06D6 && cp <= 0x06DC) || (cp >= 0x06DF && cp <= 0x06E4) || cp == 0x06E7 ||
           cp == 0x06E8 || (cp >= 0x06EA && cp <= 0x06ED) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F);
}

enum class Join { None, Right, Dual, Causing, Through };

Join joinOf(uint32_t cp) {
    if (cp == 0x0640) return Join::Causing;  // tatweel
    if (mark(cp)) return Join::Through;
    const Forms* f = formsOf(cp);
    if (!f) return Join::None;
    return f->initial ? Join::Dual : f->final ? Join::Right : Join::None;
}

// Each Arabic letter as the form its neighbours ask for: joined to the letter before when that
// one reaches forward, to the letter after when it reaches back. Lam then alef is one letter.
void join(const std::vector<uint32_t>& in, std::vector<uint32_t>& out) {
    out.clear();
    const int n = int(in.size());
    const auto past = [&](int i, int step) {
        for (i += step; i >= 0 && i < n && joinOf(in[size_t(i)]) == Join::Through; i += step) {}
        return i;
    };
    for (int i = 0; i < n; ++i) {
        const uint32_t cp = in[size_t(i)];
        const Join self = joinOf(cp);
        if (self != Join::Right && self != Join::Dual) {
            out.push_back(cp);
            continue;
        }
        const int before = past(i, -1), after = past(i, 1);
        const Join b = before >= 0 ? joinOf(in[size_t(before)]) : Join::None;
        const Join a = after < n ? joinOf(in[size_t(after)]) : Join::None;
        const bool back = b == Join::Dual || b == Join::Causing;
        if (cp == 0x0644 && after < n) {
            const uint32_t alef = in[size_t(after)];
            const uint32_t lig = alef == 0x0622 ? 0xFEF5 : alef == 0x0623 ? 0xFEF7
                               : alef == 0x0625 ? 0xFEF9 : alef == 0x0627 ? 0xFEFB : 0;
            if (lig) {
                out.push_back(lig + (back ? 1 : 0));
                for (int m = i + 1; m < after; ++m) out.push_back(in[size_t(m)]);
                i = after;
                continue;
            }
        }
        const bool forward = self == Join::Dual &&
                             (a == Join::Dual || a == Join::Right || a == Join::Causing);
        const Forms& f = *formsOf(cp);
        out.push_back(back && forward ? f.medial : back ? f.final : forward ? f.initial : f.isolated);
    }
}

bool rtl(uint32_t cp) {
    if ((cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06F0 && cp <= 0x06F9)) return false;
    return (cp >= 0x0590 && cp <= 0x08FF) || (cp >= 0xFB1D && cp <= 0xFDFF) ||
           (cp >= 0xFE70 && cp <= 0xFEFF);
}

enum class Dir : uint8_t { L, R, Number, Neutral };

Dir dirOf(uint32_t cp) {
    if (rtl(cp)) return Dir::R;
    if ((cp >= '0' && cp <= '9') || (cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06F0 && cp <= 0x06F9))
        return Dir::Number;
    if (cp < 0x80) return ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') ? Dir::L : Dir::Neutral;
    if (cp < 0xC0 || cp == 0xD7 || cp == 0xF7 || (cp >= 0x2000 && cp <= 0x2BFF) ||
        (cp >= 0x3000 && cp <= 0x303F) || (cp >= 0xFE10 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF20) || cp >= 0x1F000) {
        return Dir::Neutral;
    }
    return Dir::L;
}

uint32_t mirrored(uint32_t cp) {
    switch (cp) {
        case '(': return ')';
        case ')': return '(';
        case '[': return ']';
        case ']': return '[';
        case '{': return '}';
        case '}': return '{';
        case '<': return '>';
        case '>': return '<';
        case 0xAB: return 0xBB;
        case 0xBB: return 0xAB;
        default: return cp;
    }
}

// A letter and the marks on it: what is moved as one.
struct Cluster {
    uint32_t from = 0, count = 0;
    Dir dir = Dir::Neutral;
    uint8_t level = 0;
};

// Whether a line holds a right-to-left letter at all: a byte scan first, as nearly every line
// drawn has none, then the letters of the few that might.
bool hasRtl(const std::string& s) {
    bool maybe = false;
    for (char c : s) {
        const uint8_t b = uint8_t(c);
        if ((b >= 0xD6 && b <= 0xDF) || b == 0xE0 || b == 0xEF) {
            maybe = true;
            break;
        }
    }
    if (!maybe) return false;
    for (size_t at = 0; at < s.size();) {
        if (rtl(utf8::next(s, at))) return true;
    }
    return false;
}

}  // namespace

bool rightToLeft(const std::string& s) {
    if (!hasRtl(s)) return false;
    for (size_t at = 0; at < s.size();) {
        const Dir d = dirOf(utf8::next(s, at));
        if (d == Dir::L) return false;
        if (d == Dir::R) return true;
    }
    return false;
}

const std::string& visual(const std::string& s) {
    if (!hasRtl(s)) return s;
    thread_local std::vector<uint32_t> typed, joined;
    thread_local std::vector<Cluster> cl;
    thread_local std::string out;
    typed.clear();
    for (size_t at = 0; at < s.size();) typed.push_back(utf8::next(s, at));
    join(typed, joined);

    cl.clear();
    for (uint32_t i = 0; i < joined.size(); ++i) {
        if (!cl.empty() && mark(joined[i])) {
            ++cl.back().count;
            continue;
        }
        cl.push_back({i, 1, dirOf(joined[i]), 0});
    }
    const int n = int(cl.size());

    Dir base = Dir::L;
    for (const Cluster& c : cl) {
        if (c.dir == Dir::L || c.dir == Dir::R) {
            base = c.dir;
            break;
        }
    }
    // A number's separator between two digits is the number's ("1,000", "12:30"), and so is a
    // sign or a percent beside one.
    const auto at = [&](int i) { return joined[cl[size_t(i)].from]; };
    for (int i = 1; i + 1 < n; ++i) {
        const uint32_t cp = at(i);
        if (cl[size_t(i)].dir == Dir::Neutral && cl[size_t(i - 1)].dir == Dir::Number &&
            cl[size_t(i + 1)].dir == Dir::Number && (cp == ',' || cp == '.' || cp == ':' || cp == '/')) {
            cl[size_t(i)].dir = Dir::Number;
        }
    }
    for (int i = 0; i < n; ++i) {
        const uint32_t cp = at(i);
        if (cl[size_t(i)].dir != Dir::Neutral || !(cp == '%' || cp == '$' || cp == '#' || cp == '+' ||
                                                   cp == '-' || cp == 0xB0)) {
            continue;
        }
        if ((i > 0 && cl[size_t(i - 1)].dir == Dir::Number) ||
            (i + 1 < n && cl[size_t(i + 1)].dir == Dir::Number)) {
            cl[size_t(i)].dir = Dir::Number;
        }
    }
    // A number in a left-to-right line after left-to-right letters is simply left to right.
    if (base == Dir::L) {
        Dir strong = Dir::L;
        for (Cluster& c : cl) {
            if (c.dir == Dir::L || c.dir == Dir::R) strong = c.dir;
            else if (c.dir == Dir::Number && strong == Dir::L) c.dir = Dir::L;
        }
    }
    // A run of neutrals between two of one direction is that direction, a number counting as
    // right to left; between two that differ, the line's.
    for (int i = 0; i < n;) {
        if (cl[size_t(i)].dir != Dir::Neutral) {
            ++i;
            continue;
        }
        int j = i;
        while (j < n && cl[size_t(j)].dir == Dir::Neutral) ++j;
        const auto side = [&](int k) {
            if (k < 0 || k >= n) return base;
            return cl[size_t(k)].dir == Dir::L ? Dir::L : Dir::R;
        };
        const Dir before = side(i - 1), after = side(j);
        for (int k = i; k < j; ++k) cl[size_t(k)].dir = before == after ? before : base;
        i = j;
    }
    uint8_t top = 0;
    for (Cluster& c : cl) {
        if (base == Dir::L) c.level = c.dir == Dir::L ? 0 : c.dir == Dir::R ? 1 : 2;
        else c.level = c.dir == Dir::R ? 1 : 2;
        top = std::max(top, c.level);
    }
    // Spaces at the end stay at the line's end.
    for (int i = n - 1; i >= 0 && at(i) == ' '; --i) cl[size_t(i)].level = base == Dir::L ? 0 : 1;
    for (uint8_t level = top; level >= 1; --level) {
        for (int i = 0; i < n;) {
            if (cl[size_t(i)].level < level) {
                ++i;
                continue;
            }
            int j = i;
            while (j < n && cl[size_t(j)].level >= level) ++j;
            std::reverse(cl.begin() + i, cl.begin() + j);
            i = j;
        }
    }

    out.clear();
    for (const Cluster& c : cl) {
        const uint32_t first = joined[c.from];
        utf8::put(out, (c.level & 1) ? mirrored(first) : first);
        for (uint32_t m = 1; m < c.count; ++m) utf8::put(out, joined[c.from + m]);
    }
    return out;
}

}  // namespace mu::core::bidi
