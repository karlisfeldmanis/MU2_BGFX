#include "json.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace armory {

void quote(std::string& out, const std::string& s) {
    out += '"';
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += char(c);
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20) {
            out += ' ';
        } else if (c >= 0x80) {
            // A whole UTF-8 sequence passes as it is; a lone Latin-1 byte (the card's " \xB7 "
            // joiner, which the face bakes as Latin-1) becomes \u00XX.
            const int len = (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
            bool ok = len > 0 && i + size_t(len) <= s.size();
            for (int k = 1; ok && k < len; ++k) ok = ((unsigned char)s[i + size_t(k)] & 0xC0) == 0x80;
            if (ok) {
                out.append(s, i, size_t(len));
                i += size_t(len - 1);
            } else {
                char b[8];
                std::snprintf(b, sizeof b, "\\u%04x", c);
                out += b;
            }
        } else {
            out += char(c);
        }
    }
    out += '"';
}

bool J::flat() const {
    if (t_ == T::Arr) {
        for (const J& one : a_) {
            if (one.t_ == T::Arr || one.t_ == T::Obj) return false;
        }
        return true;
    }
    return t_ != T::Obj;
}

void J::write(std::string& out, int depth) const {
    switch (t_) {
        case T::Null: out += "null"; return;
        case T::Bool: out += b_ ? "true" : "false"; return;
        case T::Int: out += std::to_string(i_); return;
        case T::Real: {
            if (!std::isfinite(d_)) {
                out += "null";
                return;
            }
            char b[40];
            std::snprintf(b, sizeof b, "%.*g", digits_, d_);
            // A float's widening noise (1.149999976 for 1.15): the short form when it is the same
            // number to a float's precision.
            char shortForm[40];
            std::snprintf(shortForm, sizeof shortForm, "%.6g", d_);
            if (std::fabs(std::strtod(shortForm, nullptr) - d_) <= std::fabs(d_) * 2e-7) std::strcpy(b, shortForm);
            // -0 and 1e-05 read the same to JSON; plain is kinder to a diff.
            if (std::strcmp(b, "-0") == 0) std::strcpy(b, "0");
            out += b;
            return;
        }
        case T::Str: quote(out, s_); return;
        case T::Arr: {
            if (a_.empty()) {
                out += "[]";
                return;
            }
            if (flat()) {
                out += '[';
                for (size_t i = 0; i < a_.size(); ++i) {
                    if (i) out += ", ";
                    a_[i].write(out, depth + 1);
                }
                out += ']';
                return;
            }
            out += "[\n";
            for (size_t i = 0; i < a_.size(); ++i) {
                out.append(size_t(depth + 1), ' ');
                a_[i].write(out, depth + 1);
                out += i + 1 < a_.size() ? ",\n" : "\n";
            }
            out.append(size_t(depth), ' ');
            out += ']';
            return;
        }
        case T::Obj: {
            if (o_.empty()) {
                out += "{}";
                return;
            }
            bool allFlat = o_.size() <= 8;
            for (const auto& [k, v] : o_) allFlat = allFlat && v.t_ != T::Obj && v.t_ != T::Arr;
            if (allFlat) {
                out += '{';
                bool first = true;
                for (const auto& [k, v] : o_) {
                    if (!first) out += ", ";
                    first = false;
                    quote(out, k);
                    out += ": ";
                    v.write(out, depth + 1);
                }
                out += '}';
                return;
            }
            out += "{\n";
            size_t n = 0;
            for (const auto& [k, v] : o_) {
                out.append(size_t(depth + 1), ' ');
                quote(out, k);
                out += ": ";
                v.write(out, depth + 1);
                out += ++n < o_.size() ? ",\n" : "\n";
            }
            out.append(size_t(depth), ' ');
            out += '}';
            return;
        }
    }
}

bool J::save(const std::string& path) const {
    std::string out;
    write(out);
    out += '\n';
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace armory
