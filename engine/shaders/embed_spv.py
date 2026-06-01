#!/usr/bin/env python3
"""Embed a SPIR-V binary as a C++ uint32_t array."""
import sys, struct

spv_path = sys.argv[1]
out_path = sys.argv[2]

with open(spv_path, 'rb') as f:
    data = f.read()

assert len(data) % 4 == 0, "SPIR-V size must be multiple of 4"
words = len(data) // 4

with open(out_path, 'w') as out:
    out.write('#include <cstdint>\n')
    # Derive array name from output filename (sans .cpp)
    import os
    name = os.path.splitext(os.path.basename(out_path))[0]
    out.write(f'extern const uint32_t {name}[] = {{\n')
    for i in range(words):
        val = struct.unpack_from('<I', data, i * 4)[0]
        out.write(f'0x{val:08X},')
    out.write('};\n')
    out.write(f'extern const uint32_t {name}_count = {words};\n')
