#!/usr/bin/env python3
"""Generate TFT_eSPI-compatible VLW font PROGMEM headers from a TTF file.

VLW format (Bodmer/TFT_eSPI smooth font):
  Header:  glyph_count(u32) | version(u32=6) | font_size(u32) | padding(u32)
           ascent(u32) | descent(u32)
  Per glyph metrics (28 bytes each, 7 x uint32 big-endian):
           unicode(u32) | height(u32) | width(u32) | advance(u32)
           top_offset(i32) | left_offset(i32) | padding(u32, ignored by reader)
  Glyph bitmaps: 8-bit alpha, row-major, width*height bytes each.

Usage:
  python scripts/generate_vlw_fonts.py
"""

import struct
import sys
from pathlib import Path

try:
    import freetype
except ImportError:
    print("Install freetype-py:  pip install freetype-py")
    sys.exit(1)

# Character set: printable ASCII + degree symbol
CHARSET = list(range(0x20, 0x7F)) + [0xB0]

# Font sizes to generate: (name, ttf_filename, pixel_size)
# Mixed weights match TFT_eSPI bitmap fonts: Font 1/2 were medium, Font 4 was bold.

FONTS_DIR = Path(__file__).parent.parent / "source_fonts"
OUT_DIR = Path(__file__).parent.parent / "fonts"


def generate_vlw(ttf_path: str, pixel_size: int) -> bytes:
    """Generate VLW binary data for the given font and size."""
    face = freetype.Face(str(ttf_path))
    face.set_pixel_sizes(0, pixel_size)

    glyphs = []
    for codepoint in CHARSET:
        # Use lighter hinting and autohinting for better rendering on small displays
        # FT_LOAD_TARGET_LIGHT uses lighter hinting which can improve stroke visibility
        # FT_LOAD_FORCE_AUTOHINT ensures consistent auto-hinting
        load_flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT | freetype.FT_LOAD_FORCE_AUTOHINT
        face.load_char(chr(codepoint), load_flags)

        g = face.glyph
        bmp = g.bitmap

        width = bmp.width
        height = bmp.rows
        advance = g.advance.x >> 6  # 26.6 fixed point to pixels
        top_offset = g.bitmap_top
        left_offset = g.bitmap_left

        # Extract 8-bit alpha bitmap
        if width > 0 and height > 0:
            alpha = bytes(bmp.buffer)
            # Handle pitch != width (padding per row)
            if bmp.pitch != width:
                alpha = b""
                for row in range(height):
                    start = row * bmp.pitch
                    alpha += bytes(bmp.buffer[start : start + width])
        else:
            alpha = b""

        # Boost alpha brightness more aggressively for better visibility on LCD
        # Apply a curve that boosts darker pixels more than lighter ones
        if alpha:
            boosted = bytearray(alpha)
            for i in range(len(boosted)):
                v = boosted[i]
                if v > 0:
                    # Apply a more aggressive boost curve
                    # Pixels with value > 0 get boosted, with smaller values getting more boost
                    if v < 128:
                        # Dark pixels: boost by 50%
                        new_v = v * 3 // 2
                    else:
                        # Light pixels: boost by 25%
                        new_v = v * 5 // 4
                    boosted[i] = 255 if new_v > 255 else new_v
            alpha = bytes(boosted)

        # Smart glyph placement: ensure glyphs fit within their cell bounds
        if width > 0 and advance > 0:
            # Goal: Make sure glyph fits within [0, advance] bounds
            # Current: glyph spans [left_offset, left_offset + width]
            # Target: glyph should span within [0, advance]

            # Step 1: If glyph extends left of 0 (negative left_offset), shift bitmap right
            if left_offset < 0:
                shift_amount = -left_offset
                # Add shift_amount empty columns on the left
                new_width = width + shift_amount
                shifted = bytearray(new_width * height)
                for row in range(height):
                    # Add empty columns on left
                    for col in range(shift_amount):
                        shifted[row * new_width + col] = 0
                    # Copy original data
                    for col in range(width):
                        shifted[row * new_width + col + shift_amount] = alpha[row * width + col]
                alpha = bytes(shifted)
                width = new_width
                left_offset = 0  # Now starts at 0

            # Step 2: If glyph extends right of advance, try trimming empty columns first
            right_edge = left_offset + width
            if right_edge > advance:
                overflow = right_edge - advance

                # Try removing empty columns from right side
                while overflow > 0 and width > 1:
                    right_col_empty = True
                    for row in range(height):
                        if alpha[row * width + (width - 1)] != 0:
                            right_col_empty = False
                            break
                    if not right_col_empty:
                        break
                    # Remove rightmost column
                    new_alpha = bytearray((width - 1) * height)
                    for row in range(height):
                        src_start = row * width
                        dst_start = row * (width - 1)
                        new_alpha[dst_start:dst_start + width - 1] = alpha[src_start:src_start + width - 1]
                    alpha = bytes(new_alpha)
                    width -= 1
                    overflow -= 1

            # Step 3: If still too wide, scale horizontally to fit
            right_edge = left_offset + width
            if right_edge > advance and width > 1:
                available_width = advance - left_offset
                if available_width < 1:
                    available_width = 1

                new_width = available_width
                new_alpha = bytearray(new_width * height)
                for row in range(height):
                    for dst_col in range(new_width):
                        src_center = (dst_col + 0.5) * width / new_width
                        src_l = int(src_center)
                        src_r = src_l + 1 if src_l + 1 < width else src_l
                        frac = src_center - src_l
                        lv = alpha[row * width + src_l]
                        rv = alpha[row * width + src_r] if src_r < width else lv
                        new_alpha[row * new_width + dst_col] = int(lv * (1.0 - frac) + rv * frac + 0.5)
                width = new_width
                alpha = bytes(new_alpha)

        glyphs.append(
            {
                "unicode": codepoint,
                "height": height,
                "width": width,
                "advance": advance,
                "top_offset": top_offset,
                "left_offset": left_offset,
                "bitmap": alpha,
            }
        )

    # Compute font metrics
    ascent = face.size.ascender >> 6
    descent = -(face.size.descender >> 6)  # TFT_eSPI expects positive descent

    # Build VLW binary
    buf = bytearray()

    # Header (6 x uint32 big-endian)
    glyph_count = len(glyphs)
    buf += struct.pack(">I", glyph_count)
    buf += struct.pack(">I", 6)  # version
    buf += struct.pack(">I", pixel_size)
    buf += struct.pack(">I", 0)  # padding
    buf += struct.pack(">I", ascent)
    buf += struct.pack(">I", descent)

    # Glyph metrics table (28 bytes each — 7 x uint32)
    # TFT_eSPI reads: unicode, height, width, xAdvance, dY, dX, padding(ignored)
    for g in glyphs:
        buf += struct.pack(">I", g["unicode"])
        buf += struct.pack(">I", g["height"])
        buf += struct.pack(">I", g["width"])
        buf += struct.pack(">I", g["advance"])
        buf += struct.pack(">i", g["top_offset"])
        buf += struct.pack(">i", g["left_offset"])
        buf += struct.pack(">I", 0)  # padding — TFT_eSPI reads and discards

    # Glyph bitmaps (sequential, same order as metrics)
    for g in glyphs:
        buf += g["bitmap"]

    return bytes(buf)


def vlw_to_header(name: str, vlw_data: bytes) -> str:
    """Convert VLW binary to a C PROGMEM header."""
    lines = [
        f"// Auto-generated VLW font: {name}",
        f"// Size: {len(vlw_data)} bytes ({len(vlw_data) / 1024:.1f} KB)",
        "#pragma once",
        "#include <pgmspace.h>",
        "",
        f"const uint8_t {name}[] PROGMEM = {{",
    ]

    # Emit bytes, 16 per line
    for i in range(0, len(vlw_data), 16):
        chunk = vlw_data[i : i + 16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")

    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    font_families = {"IBMPlexMono": "ibmplex", "HackNerdFont": "hack"}
    variants = {
        "Regular": "",
        "Bold": "_bold",
        "Italic": "_italic",
        "BoldItalic": "_bolditalic",
    }

    for family in font_families:
        for variant in variants:
            for size in range(6, 15):
                ttf_filename = f"{family}-{variant}.ttf"
                name = f"{font_families[family]}{variants[variant]}"
                ttf_path = FONTS_DIR / ttf_filename
                if not ttf_path.exists():
                    print(f"TTF not found: {ttf_path}")
                    sys.exit(1)

                print(f"Generating {name} ({ttf_filename} @ {size}px)...", end=" ")
                vlw_data = generate_vlw(str(ttf_path), size)
                header = vlw_to_header(f"{name}_{size}", vlw_data)

                out_path = OUT_DIR / f"{name}-{size}.h"
                # Force LF line endings so generated headers are consistent across
                # platforms and don't trigger `git diff --check` whitespace warnings.
                out_path.write_bytes(header.encode("utf-8"))
                print(f"{len(vlw_data)} bytes -> {out_path}")

    print("Done.")


if __name__ == "__main__":
    main()
