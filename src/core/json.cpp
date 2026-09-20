#include "core/json.h"

#include <cstdlib>
#include <cstring>

#include "core/files.h"
#include "core/log.h"

namespace mu::core {
namespace {

const Json kNull;

struct Parser {
    const char* p;
    const char* end;
    const char* what;
    bool ok = true;

    void fail(const char* why) {
        if (!ok) return;
        ok = false;
        logError("%s: %s", what, why);
    }

    void skipSpace() {
        while (p < end) {
            char c = *p;
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++p;
            } else {
                break;
            }
        }
    }

    bool literal(const char* text) {
        size_t n = std::strlen(text);
        if (size_t(end - p) < n || std::memcmp(p, text, n) != 0) return false;
        p += n;
        return true;
    }

    std::string parseString() {
        std::string out;
        if (p >= end || *p != '"') {
            fail("a string was expected");
            return out;
        }
        ++p;
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (p >= end) break;
            char e = *p++;
            switch (e) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': {
                    // Only the basic plane, encoded as UTF-8. A surrogate pair comes through
                    // as two replacement characters rather than as nonsense; nothing in MU2's
                    // content needs one, and a wrong guess here would be silent.
                    if (end - p < 4) {
                        fail("a \\u escape ran off the end");
                        return out;
                    }
                    char digits[5] = {p[0], p[1], p[2], p[3], 0};
                    unsigned code = unsigned(std::strtoul(digits, nullptr, 16));
                    p += 4;
                    if (code < 0x80) {
                        out.push_back(char(code));
                    } else if (code < 0x800) {
                        out.push_back(char(0xC0 | (code >> 6)));
                        out.push_back(char(0x80 | (code & 0x3F)));
                    } else {
                        out.push_back(char(0xE0 | (code >> 12)));
                        out.push_back(char(0x80 | ((code >> 6) & 0x3F)));
                        out.push_back(char(0x80 | (code & 0x3F)));
                    }
                    break;
                }
                default: out.push_back(e); break;  // \" \\ \/ and anything else, as written
            }
        }
        if (p >= end) {
            fail("a string was not closed");
            return out;
        }
        ++p;  // the closing quote
        return out;
    }

    Json parseValue(int depth) {
        Json v;
        if (depth > 200) {
            fail("nested too deep");
            return v;
        }
        skipSpace();
        if (p >= end) {
            fail("ended early");
            return v;
        }
        char c = *p;
        if (c == '{') {
            ++p;
            v.type = Json::Type::Object;
            skipSpace();
            if (p < end && *p == '}') {
                ++p;
                return v;
            }
            while (ok) {
                skipSpace();
                std::string key = parseString();
                if (!ok) return v;
                skipSpace();
                if (p >= end || *p != ':') {
                    fail("a ':' was expected");
                    return v;
                }
                ++p;
                Json child = parseValue(depth + 1);
                if (!ok) return v;
                v.members.emplace_back(std::move(key), std::move(child));
                skipSpace();
                if (p < end && *p == ',') {
                    ++p;
                    continue;
                }
                if (p < end && *p == '}') {
                    ++p;
                    return v;
                }
                fail("a ',' or '}' was expected");
                return v;
            }
        } else if (c == '[') {
            ++p;
            v.type = Json::Type::Array;
            skipSpace();
            if (p < end && *p == ']') {
                ++p;
                return v;
            }
            while (ok) {
                Json child = parseValue(depth + 1);
                if (!ok) return v;
                v.items.push_back(std::move(child));
                skipSpace();
                if (p < end && *p == ',') {
                    ++p;
                    continue;
                }
                if (p < end && *p == ']') {
                    ++p;
                    return v;
                }
                fail("a ',' or ']' was expected");
                return v;
            }
        } else if (c == '"') {
            v.type = Json::Type::String;
            v.string = parseString();
        } else if (literal("true")) {
            v.type = Json::Type::Bool;
            v.boolean = true;
        } else if (literal("false")) {
            v.type = Json::Type::Bool;
            v.boolean = false;
        } else if (literal("null")) {
            v.type = Json::Type::Null;
        } else {
            // strtod takes no length and must read the byte past the number to know it
            // ended. The buffer is the file as read, with nothing after it, so a document
            // whose last byte is a digit -- a sheet saved without a trailing newline --
            // over-reads the heap, and crashes outright when the allocation ends on a page
            // boundary. The number is copied out first; no real one comes near this length.
            char digits[64];
            const size_t room = size_t(end - p) < sizeof(digits) - 1 ? size_t(end - p)
                                                                     : sizeof(digits) - 1;
            std::memcpy(digits, p, room);
            digits[room] = '\0';
            char* stop = nullptr;
            double d = std::strtod(digits, &stop);
            if (stop == digits) {
                fail("not a value");
                return v;
            }
            // NaN and Infinity are not JSON, and strtod accepts them: a sheet saying "inf"
            // for a light range would otherwise reach the shader as a uniform nothing checks.
            if (d != d || d > 1e308 || d < -1e308) {
                fail("a number is not finite");
                return v;
            }
            v.type = Json::Type::Number;
            v.number = d;
            p += size_t(stop - digits);
        }
        return v;
    }
};

}  // namespace

const Json& Json::operator[](const char* key) const {
    if (type == Type::Object) {
        for (const auto& m : members) {
            if (m.first == key) return m.second;
        }
    }
    return kNull;
}

const Json& Json::at(size_t index) const {
    if (type == Type::Array && index < items.size()) return items[index];
    return kNull;
}

size_t Json::size() const {
    if (type == Type::Array) return items.size();
    if (type == Type::Object) return members.size();
    return 0;
}

void Json::readInto(const char* key, float* out) const {
    const Json& v = (*this)[key];
    if (v.type == Type::Number) *out = float(v.number);
}
void Json::readInto(const char* key, double* out) const {
    const Json& v = (*this)[key];
    if (v.type == Type::Number) *out = v.number;
}
void Json::readInto(const char* key, int* out) const {
    const Json& v = (*this)[key];
    if (v.type == Type::Number) *out = int(v.number);
}
void Json::readInto(const char* key, bool* out) const {
    const Json& v = (*this)[key];
    if (v.type == Type::Bool) *out = v.boolean;
}

void Json::readVec3Into(const char* key, float* out) const {
    const Json& v = (*this)[key];
    if (v.type != Type::Array || v.items.size() != 3) return;
    for (int i = 0; i < 3; ++i) {
        if (v.items[size_t(i)].type != Type::Number) return;
    }
    for (int i = 0; i < 3; ++i) out[i] = float(v.items[size_t(i)].number);
}

Json parseJson(const char* text, size_t length, const char* whatItIs) {
    Parser parser{text, text + length, whatItIs};
    Json v = parser.parseValue(0);
    if (!parser.ok) return Json{};
    parser.skipSpace();
    if (parser.p != parser.end) {
        logError("%s: there is more after the value", whatItIs);
        return Json{};
    }
    return v;
}

Json parseJsonFile(const std::string& path) {
    std::vector<uint8_t> bytes = readFile(path);
    if (bytes.empty()) return Json{};
    return parseJson(reinterpret_cast<const char*>(bytes.data()), bytes.size(), path.c_str());
}

}  // namespace mu::core
