"""Writes a C++ file that holds other files' bytes, for MU2Launcher's single exe (src/launcher/ui.cpp):
its two shaders, its fonts, the wallpaper and the logo.

    python3 embed.py OUT.cpp NAME=PATH [NAME=PATH ...]

The table is mu::launcher::embedded(), each file under NAME. Run by CMake when an input changes.
"""
import os
import sys


def main():
    out, pairs = sys.argv[1], sys.argv[2:]
    lines = ['// Written by tools/cmake/embed.py; not to be edited.', '#include "launcher/embedded.h"', '',
             'namespace mu::launcher {', 'namespace {', '']
    entries = []
    for i, pair in enumerate(pairs):
        name, path = pair.split("=", 1)
        data = open(path, "rb").read()
        lines.append(f"// {name}: {os.path.basename(path)}, {len(data)} bytes")
        lines.append(f"alignas(16) const unsigned char k{i}[] = {{")
        for at in range(0, len(data), 24):
            lines.append("    " + ",".join(str(b) for b in data[at:at + 24]) + ",")
        lines.append("    0};")
        entries.append(f'    {{"{name}", k{i}, {len(data)}}},')
    lines += ['', '}  // namespace', '', 'const std::vector<Embedded>& embedded() {',
              '    static const std::vector<Embedded> all = {'] + entries + ['    };', '    return all;', '}', '',
             '}  // namespace mu::launcher', '']
    text = "\n".join(lines)
    if not os.path.exists(out) or open(out).read() != text:
        with open(out, "w") as f:
            f.write(text)


if __name__ == "__main__":
    main()
