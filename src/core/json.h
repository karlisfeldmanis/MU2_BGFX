// A small JSON reader. It exists because index.json, every world's json and every sheet is
// one, and because the game must carry no sqlite and no parser library.
//
// It builds a whole tree and owns its strings: fine for a sheet read at start or on change,
// and not for anything read per frame.
#pragma once

#include <string>
#include <vector>

namespace mu::core {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> items;                        // Array
    std::vector<std::pair<std::string, Json>> members;  // Object

    bool isNull() const { return type == Type::Null; }

    // A member by name, or null. Never returns nullptr, so a chain of them is safe on a
    // file that is missing a whole branch.
    const Json& operator[](const char* key) const;
    const Json& at(size_t index) const;
    size_t size() const;

    double numberOr(double fallback) const { return type == Type::Number ? number : fallback; }
    bool boolOr(bool fallback) const { return type == Type::Bool ? boolean : fallback; }
    std::string stringOr(const char* fallback) const {
        return type == Type::String ? string : std::string(fallback);
    }

    // For a sheet: reads `key` as a number, leaving `out` alone when it is absent, so a
    // sheet naming one field overrides one field.
    void readInto(const char* key, float* out) const;
    void readInto(const char* key, double* out) const;
    void readInto(const char* key, int* out) const;
    void readInto(const char* key, bool* out) const;
    // An array of three numbers into a float[3]; left alone when absent or the wrong shape.
    void readVec3Into(const char* key, float* out) const;
};

// Parses text. On a syntax error the result is null and the reason is in the log with the
// line it was on.
Json parseJson(const char* text, size_t length, const char* whatItIs);
Json parseJsonFile(const std::string& path);

}  // namespace mu::core
