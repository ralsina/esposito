#!/usr/bin/env python3
"""Generate VLW binary font files and pack them into self-describing .fpack bundles.

Scans source_fonts/ directory for TTF files and generates VLW font data at
multiple sizes (6-14px). Outputs raw .vlw binaries and .fpack bundles.

VLW format (Bodmer/TFT_eSPI smooth font):
  Header:  glyph_count(u32) | version(u32=6) | font_size(u32) | padding(u32)
           ascent(u32) | descent(u32)
  Per glyph metrics (28 bytes each, 7 x uint32 big-endian):
           unicode(u32) | height(u32) | width(u32) | advance(u32)
           top_offset(i32) | left_offset(i32) | padding(u32, ignored by reader)
  Glyph bitmaps: 8-bit alpha, row-major, width*height bytes each.

Usage:
  python scripts/generate_vlw_fonts.py [output_dir]

If output_dir is given, .vlw and .fpack files are written there.
Otherwise they go to fonts/ (default).
"""

import struct
import subprocess
import sys
from pathlib import Path

try:
    import freetype
except ImportError:
    print("Install freetype-py:  pip install freetype-py")
    sys.exit(1)

FONTS_DIR = Path(__file__).parent.parent / "source_fonts"
FALLBACK_FONT = Path("/usr/share/fonts/TTF/DejaVuSansMono.ttf")
LUCIDE_FONT = Path(__file__).parent.parent / "source_fonts/lucide.woff"
LUCIDE_CODEPOINTS = set([
    0xE070, 0xE06D, 0xE07C, 0xE0F5,  # chevron-up, chevron-down, check-circle, home
    0xE05F, 0xE104, 0xE042, 0xE049,  # book-open, search, arrow-down, arrow-left
    0xE048, 0xE04A, 0xE053, 0xE054, 0xE061, 0xE06A, 0xE06B,
    0xE06E, 0xE06F, 0xE076, 0xE077, 0xE082, 0xE084, 0xE0B2, 0xE0B6, 0xE0B7,
    0xE0B9, 0xE0C0, 0xE0C9, 0xE0D9,  # file-plus, folder-plus
    0xE0CE, 0xE086, 0xE0CA, 0xE12A,  # folder, external-link, file-minus, copy
    0xE12D, 0xE0E9, 0xE12F,          # trash-2, download, edit-2
    0xE112, 0xE115, 0xE11A, 0xE11C, 0xE13D, 0xE140, 0xE145, 0xE14D, 0xE151, 0xE154, 0xE178, 0xE19E,
    0xE1AE, 0xE1AF, 0xE1B2, 0xE2A5, 0xE400, 0xE4D3
])

# Character set: printable ASCII + Latin-1 + typographic chars
# Use Lucide icons for arrows, checks, etc. instead of unicode glyphs
CHARSET = (
    list(range(0x20, 0x7F))            # Printable ASCII
    + list(range(0xA0, 0x100))         # Latin-1 Supplement
    + [0x20AC]                          # €
    + [0x2013, 0x2014]                  # en dash, em dash
    + [0x2018, 0x2019]                  # single curly quotes
    + [0x201C, 0x201D]                  # double curly quotes
    + list(range(0x2500, 0x2580))      # Box drawing
    # Use Lucide icons for arrows instead of unicode glyphs
    + [0x2261]                          # ≡
    + [0x2699]                          # ⚙
    + [0x23CE]                          # ⏎
    + [0x2315]                          # ⌕ (find/search)
    + [0x2610, 0x2611]                  # ☐ ☑
    + [0x2026]                          # …
    + [0x2264, 0x2265, 0x2260]         # ≤ ≥ ≠
    + [0x25CF, 0x25CB]                  # ● ○
    + [0x25A0, 0x25A1]                  # ■ □
    + [0x25C6, 0x25C7]                  # ◆ ◇
    + [0x25B2, 0x25BC]                  # ▲ ▼
    + [0x25C0, 0x25B6]                  # ◀ ▶
    + [0x2B07]                          # ⬇ (download)
    + [0x21E7, 0x21E9]                  # ⇧ ⇩ (shift, download)
    + [0x232B]                          # ⌫ (backspace)
    # Lucide icons (PUA range 0xE000-0xF8FF)
    + list(LUCIDE_CODEPOINTS)
)

# Font sizes to generate: (name, ttf_filename, pixel_size)
# Mixed weights match TFT_eSPI bitmap fonts: Font 1/2 were medium, Font 4 was bold.


  # Mapped codepoint → real emoji codepoint


def render_codepoint(face, codepoint):
    """Render a single codepoint and return glyph dict or None."""
    if face.get_char_index(codepoint) == 0:
        return None
    load_flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT | freetype.FT_LOAD_FORCE_AUTOHINT
    face.load_char(chr(codepoint), load_flags)

    g = face.glyph
    bmp = g.bitmap

    width = bmp.width
    height = bmp.rows
    advance = g.advance.x >> 6
    top_offset = g.bitmap_top
    left_offset = g.bitmap_left

    if width > 0 and height > 0:
        alpha = bytes(bmp.buffer)
        if bmp.pitch != width:
            alpha = b""
            for row in range(height):
                start = row * bmp.pitch
                alpha += bytes(bmp.buffer[start:start + width])
    else:
        alpha = b""

    if alpha:
        boosted = bytearray(alpha)
        for i in range(len(boosted)):
            v = boosted[i]
            if v > 0:
                if v < 128:
                    new_v = v * 3 // 2
                else:
                    new_v = v * 5 // 4
                boosted[i] = 255 if new_v > 255 else new_v
        alpha = bytes(boosted)

    if width > 0 and advance > 0:
        if left_offset < 0:
            shift_amount = -left_offset
            new_width = width + shift_amount
            shifted = bytearray(new_width * height)
            for row in range(height):
                for col in range(shift_amount):
                    shifted[row * new_width + col] = 0
                for col in range(width):
                    shifted[row * new_width + col + shift_amount] = alpha[row * width + col]
            alpha = bytes(shifted)
            width = new_width
            left_offset = 0

        right_edge = left_offset + width
        if right_edge > advance:
            overflow = right_edge - advance
            while overflow > 0 and width > 1:
                right_col_empty = True
                for row in range(height):
                    if alpha[row * width + (width - 1)] != 0:
                        right_col_empty = False
                        break
                if not right_col_empty:
                    break
                new_alpha = bytearray((width - 1) * height)
                for row in range(height):
                    src_start = row * width
                    dst_start = row * (width - 1)
                    new_alpha[dst_start:dst_start + width - 1] = alpha[src_start:src_start + width - 1]
                alpha = bytes(new_alpha)
                width -= 1
                overflow -= 1

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

    return {
        "unicode": codepoint,
        "height": height,
        "width": width,
        "advance": advance,
        "top_offset": top_offset,
        "left_offset": left_offset,
        "bitmap": alpha,
    }


def box_drawing_glyph(codepoint, advance, cell_height, ascent):
    """Generate a synthetic box-drawing glyph bitmap.

    Only handles the 6 light single-line box-drawing characters
    used by the UI. Other codepoints return an empty glyph.
    """
    w = advance
    h = cell_height
    alpha = bytearray(w * h)

    cx = w // 2
    cy = h // 2

    def set_pixel(x, y):
        if 0 <= x < w and 0 <= y < h:
            alpha[y * w + x] = 255

    def hline(y, x1, x2):
        for x in range(x1, x2 + 1):
            set_pixel(x, y)

    def vline(x, y1, y2):
        for y in range(y1, y2 + 1):
            set_pixel(x, y)

    if codepoint == 0x2500:       # ─ light horizontal
        hline(cy, 0, w - 1)
    elif codepoint == 0x2502:     # │ light vertical
        vline(cx, 0, h - 1)
    elif codepoint == 0x250C:     # ┌ light down and right
        vline(cx, cy, h - 1)
        hline(cy, cx, w - 1)
    elif codepoint == 0x2510:     # ┐ light down and left
        vline(cx, cy, h - 1)
        hline(cy, 0, cx)
    elif codepoint == 0x2514:     # └ light up and right
        vline(cx, 0, cy)
        hline(cy, cx, w - 1)
    elif codepoint == 0x2518:     # ┘ light up and left
        vline(cx, 0, cy)
        hline(cy, 0, cx)

    return {
        "unicode": codepoint,
        "height": h,
        "width": w,
        "advance": advance,
        "top_offset": ascent,
        "left_offset": 0,
        "bitmap": bytes(alpha),
    }


def generate_vlw(ttf_path: str, pixel_size: int) -> bytes:
    """Generate VLW binary data for the given font and size."""
    face = freetype.Face(str(ttf_path))
    face.set_pixel_sizes(0, pixel_size)

    fallback_face = None
    if FALLBACK_FONT.exists():
        fallback_face = freetype.Face(str(FALLBACK_FONT))
        fallback_face.set_pixel_sizes(0, pixel_size + 2)

    lucide_face = None
    if LUCIDE_FONT.exists():
        lucide_face = freetype.Face(str(LUCIDE_FONT))
        lucide_face.set_pixel_sizes(0, pixel_size)

    glyphs = []

    # Compute font metrics from the main font
    ascent = face.size.ascender >> 6
    descent = -(face.size.descender >> 6)  # TFT_eSPI expects positive descent
    cell_height = ascent + descent

    # Compute modal advance for synthetic glyphs
    face.load_char("0", freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT)
    modal_advance = face.glyph.advance.x >> 6

    for codepoint in CHARSET:
        glyph = render_codepoint(face, codepoint)
        if glyph:
            glyphs.append(glyph)
            continue

        # Synthesize box-drawing characters that the font lacks
        if 0x2500 <= codepoint < 0x2580:
            glyph = box_drawing_glyph(codepoint, modal_advance, cell_height, ascent)
            if glyph and glyph["bitmap"]:
                glyphs.append(glyph)
                continue

        # Try Lucide font for icon codepoints
        if codepoint in LUCIDE_CODEPOINTS and lucide_face:
            glyph = render_codepoint(lucide_face, codepoint)
            if glyph:
                glyphs.append(glyph)
                continue

        # Try fallback font (DejaVu Sans Mono) for any missing glyphs
        if fallback_face:
            glyph = render_codepoint(fallback_face, codepoint)
            if glyph:
                glyphs.append(glyph)
                continue

    glyphs.sort(key=lambda g: g["unicode"])

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
    lines = [
        f"// Auto-generated VLW font: {name}",
        f"// Size: {len(vlw_data)} bytes ({len(vlw_data) / 1024:.1f} KB)",
        "#pragma once",
        "#include <pgmspace.h>",
        "",
        f"const uint8_t {name}[] PROGMEM = {{",
    ]
    for i in range(0, len(vlw_data), 16):
        chunk = vlw_data[i: i + 16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main():
    # Allow specifying output directory as argument
    if len(sys.argv) > 1:
        out_dir = Path(sys.argv[1])
    else:
        out_dir = Path(__file__).parent.parent / "fonts"
    out_dir.mkdir(parents=True, exist_ok=True)

    # Each font family: (TTF basename, short prefix, dict of {variant_display_name: suffix})
    # The TTF filename is "{TTF_basename}-{variant_display_name}.ttf"
    # The VLW output name is "{short_prefix}{suffix}-{size}.vlw"
    font_families = [
        ("IBMPlexMono",  "ibmplex",   {"Regular": "", "Bold": "_bold", "Italic": "_italic", "BoldItalic": "_bolditalic"}),
        ("HackNerdFont", "hack",      {"Regular": "", "Bold": "_bold", "Italic": "_italic", "BoldItalic": "_bolditalic"}),
        ("IoskeleyMono", "ioskeley",  {"Regular": "", "Bold": "_bold", "Italic": "_italic", "BoldItalic": "_bolditalic"}),
        ("KodeMono",    "kode",      {"Regular": "", "Bold": "_bold"}),
        ("Inconsolata", "inconsolata", {"Regular": "", "Bold": "_bold"}),
        ("NovaMono",    "nova",      {"Regular": ""}),
    ]

    boot_data = None
    boot_name = None

    for family, short, variants in font_families:
        for variant, suffix in variants.items():
            for size in range(6, 15):
                ttf_filename = f"{family}-{variant}.ttf"
                name = f"{short}{suffix}"
                ttf_path = FONTS_DIR / ttf_filename
                if not ttf_path.exists():
                    print(f"TTF not found, skipping: {ttf_path}")
                    continue

                print(f"Generating {name} ({ttf_filename} @ {size}px)...", end=" ")
                vlw_data = generate_vlw(str(ttf_path), size)

                out_path = out_dir / f"{name}-{size}.vlw"
                out_path.write_bytes(vlw_data)
                print(f"{len(vlw_data)} bytes -> {out_path}")

                # Keep the smallest boot font (hack regular 6px)
                if family == "HackNerdFont" and variant == "Regular" and size == 6:
                    boot_data = vlw_data
                    boot_name = f"{name}_{size}"

    # Generate the embedded boot font PROGMEM header (hack-6 regular)
    if boot_data and boot_name:
        boot_header = vlw_to_header(boot_name, boot_data)
        boot_path = out_dir / "boot_font.h"
        boot_path.write_bytes(boot_header.encode("utf-8"))
        print(f"\nBoot font: {boot_path} ({len(boot_data)} bytes)")

    # Pack .vlw files into .fpack bundles
    print("\n=== Packing .fpack bundles ===")
    pack_script = Path(__file__).parent / "pack_fpack.py"
    subprocess.run([sys.executable, str(pack_script), str(out_dir), str(out_dir)], check=True)


if __name__ == "__main__":
    main()
