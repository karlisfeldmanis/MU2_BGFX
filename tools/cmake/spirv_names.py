#!/usr/bin/env python3
"""spirv_names.py SHADER.bin...: give a SPIR-V shader's buffer types names of their own.

bgfx's shaderc names a compute buffer's block type and its variable alike (`s_castResident` both),
which a Vulkan driver never minds -- a name is debug information -- but MoltenVK turns the SPIR-V
into Metal source, where a struct and a variable of one name do not compile ("must use 'struct'
tag"), and Wine on this Mac runs Vulkan through MoltenVK (docs/plans/windows-client.md). So a
struct type whose name a variable also has is renamed, its first letter upper-cased: the same
length, so nothing in the file moves. Run by CMake after each spirv shader is built; a shader with
no such clash is left byte for byte as it was.
"""
import struct
import sys

MAGIC = 0x07230203
OP_NAME, OP_TYPE_STRUCT, OP_VARIABLE = 5, 30, 59


def fix(path):
    data = bytearray(open(path, 'rb').read())
    at = data.find(struct.pack('<I', MAGIC))
    if at < 0:
        return 0
    count = (len(data) - at) // 4
    words = struct.unpack_from('<%dI' % count, data, at)
    names, structs, variables = {}, set(), set()
    i = 5
    while i < count:
        op, n = words[i] & 0xffff, words[i] >> 16
        if n == 0 or i + n > count:
            break
        if op == OP_NAME:
            names[words[i + 1]] = (at + (i + 2) * 4, bytes(data[at + (i + 2) * 4:at + (i + n) * 4]).split(b'\0')[0])
        elif op == OP_TYPE_STRUCT:
            structs.add(words[i + 1])
        elif op == OP_VARIABLE:
            variables.add(words[i + 2])
        i += n
    taken = {names[v][1] for v in variables if v in names}
    fixed = 0
    for s in structs:
        if s in names and names[s][1] in taken and names[s][1][:1].islower():
            offset = names[s][0]
            data[offset] = ord(chr(data[offset]).upper())
            fixed += 1
    if fixed:
        open(path, 'wb').write(data)
    return fixed


if __name__ == '__main__':
    for one in sys.argv[1:]:
        fix(one)
