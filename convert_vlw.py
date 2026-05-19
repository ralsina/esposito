#!/usr/bin/env python3
"""
Convert VLW font file to C header file for embedded use.
"""

import sys
import os

def vlw_to_header(input_file, output_file, array_name="hack_font"):
    """Convert VLW font file to C header file"""

    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found")
        return False

    # Read the VLW file
    with open(input_file, 'rb') as f:
        data = f.read()

    # Create the header file
    with open(output_file, 'w') as f:
        # Write header guard
        guard = os.path.basename(output_file).upper().replace('.', '_')
        f.write(f"#ifndef {guard}\n")
        f.write(f"#define {guard}\n\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"// VLW Font: {os.path.basename(input_file)}\n")
        f.write(f"// Size: {len(data)} bytes\n\n")

        # Write array declaration
        f.write(f"static const uint8_t {array_name}[] = {{\n")

        # Write data as hex bytes
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_bytes = ', '.join(f'0x{b:02X}' for b in chunk)
            f.write(f"    {hex_bytes}")
            if i + 12 < len(data):
                f.write(',')
            f.write('\n')

        # Close array
        f.write("};\n\n")

        # Write size constant
        f.write(f"static const size_t {array_name}_size = {len(data)};\n\n")

        # Close header guard
        f.write(f"#endif // {guard}\n")

    print(f"Converted {input_file} to {output_file}")
    print(f"Array name: {array_name}")
    print(f"Size: {len(data)} bytes ({len(data)/1024:.1f} KB)")

    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 convert_vlw.py <input.vlw> [output.h] [array_name]")
        sys.exit(1)

    input_file = sys.argv[1]

    if len(sys.argv) >= 3:
        output_file = sys.argv[2]
    else:
        output_file = input_file.replace('.vlw', '.h')

    if len(sys.argv) >= 4:
        array_name = sys.argv[3]
    else:
        array_name = "hack_font"

    success = vlw_to_header(input_file, output_file, array_name)
    sys.exit(0 if success else 1)