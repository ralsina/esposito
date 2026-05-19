#!/usr/bin/env python3
"""Check all VLW font headers for glyphs that overflow the advance width."""

import re
import struct
from pathlib import Path

FONTS_DIR = Path(__file__).parent.parent / "fonts"


def parse_vlw_header(filepath: Path) -> bytes:
    """Extract raw VLW bytes from a C PROGMEM header."""
    text = filepath.read_text()
    # Match hex bytes inside the uint8_t array
    hex_vals = re.findall(r"0x([0-9A-Fa-f]{2})", text)
    return bytes(int(h, 16) for h in hex_vals)


def check_vlw(name: str, data: bytes) -> list:
    """Return list of (char, unicode, width, advance, left_offset, overflow_right) for overflowing glyphs."""
    if len(data) < 24:
        return []
    glyph_count = struct.unpack(">I", data[0:4])[0]
    if glyph_count == 0:
        return []

    issues = []
    for i in range(glyph_count):
        off = 24 + i * 28
        if off + 24 > len(data):
            break
        unicode_val = struct.unpack(">I", data[off:off+4])[0]
        height = struct.unpack(">I", data[off+4:off+8])[0]
        width = struct.unpack(">I", data[off+8:off+12])[0]
        advance = struct.unpack(">I", data[off+12:off+16])[0]
        top_offset = struct.unpack(">i", data[off+16:off+20])[0]  # signed
        left_offset = struct.unpack(">i", data[off+20:off+24])[0]  # signed

        if width > 0 and advance > 0:
            right_edge = left_offset + width
            overflow = right_edge - advance
            if overflow > 0:
                ch = chr(unicode_val) if 0x20 <= unicode_val < 0x7F else f"U+{unicode_val:04X}"
                issues.append((ch, unicode_val, width, advance, left_offset, overflow))
    return issues


def main():
    total_issues = 0
    for hfile in sorted(FONTS_DIR.glob("*.h")):
        name = hfile.stem
        data = parse_vlw_header(hfile)
        issues = check_vlw(name, data)
        if issues:
            print(f"\n=== {name} ({len(issues)} glyphs overflow) ===")
            for ch, uc, w, adv, lo, ov in issues:
                print(f"  '{ch}' (U+{uc:04X}): bitmap={w}px, advance={adv}, left_offset={lo} -> overflows right by {ov}px")
                total_issues += 1

    if total_issues == 0:
        print("No overflowing glyphs found — all glyphs fit within their advance width.")
    else:
        print(f"\n{total_issues} total overflowing glyphs across all fonts.")


if __name__ == "__main__":
    main()
