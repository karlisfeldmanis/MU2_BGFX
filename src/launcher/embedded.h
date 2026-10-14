// Files built into MU2Launcher (tools/cmake/embed.py), so it is one exe: its shaders, fonts,
// the wallpaper and the logo.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace mu::launcher {

struct Embedded {
    const char* name;
    const unsigned char* bytes;
    size_t size;
};

const std::vector<Embedded>& embedded();

}  // namespace mu::launcher
