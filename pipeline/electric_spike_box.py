"""Electric Spike's hit table, WebZen's own, as a C++ header.

    python3 pipeline/electric_spike_box.py <skillElect.hit> src/sim/electric_spike_box.h

WebZen GameServer 1.00.93 strikes with Electric Spike whatever its table marks (`CSkillHitBox`,
SkillHitBox.cpp:39-58, loaded from Data/skillElect.hit at Gamemain.cpp:3221): 36 directions of ten
degrees, each a 21 x 21 grid of bytes centred on the caster, read `m_Table[dir][y * 21 + x]`. The
table is drawn by hand and no strip rule fits it (docs/plans/dark-lord-spells/electric-spike.md),
so it is carried as it is: a row a 32-bit mask, bit x the column.
"""

import sys

SIZE, DIRS = 21, 36


def main(source, header):
    data = open(source, "rb").read()
    if len(data) != SIZE * SIZE * DIRS:
        sys.exit(f"{source}: {len(data)} bytes, not {SIZE * SIZE * DIRS}")
    rows = []
    for d in range(DIRS):
        masks = []
        for y in range(SIZE):
            bits = 0
            for x in range(SIZE):
                if data[d * SIZE * SIZE + y * SIZE + x]:
                    bits |= 1 << x
            masks.append(f"0x{bits:06x}")
        rows.append("    {" + ", ".join(masks) + "},")
    with open(header, "w") as out:
        out.write("// Electric Spike's hit table: WebZen GameServer 1.00.93's Data/skillElect.hit, as it is\n")
        out.write("// (pipeline/electric_spike_box.py made this; do not edit it by hand). 36 directions of ten\n")
        out.write("// degrees, each 21 rows of a 21 x 21 grid centred on the caster, bit x the column\n")
        out.write("// (`CSkillHitBox::HitCheck`, SkillHitBox.cpp:39-58).\n")
        out.write("#pragma once\n\n#include <cstdint>\n\nnamespace mu::sim {\n\n")
        out.write(f"constexpr int kSparkBoxSize = {SIZE}, kSparkBoxDirs = {DIRS};\n")
        out.write("constexpr uint32_t kSparkBox[kSparkBoxDirs][kSparkBoxSize] = {\n")
        out.write("\n".join(rows) + "\n};\n\n}  // namespace mu::sim\n")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
