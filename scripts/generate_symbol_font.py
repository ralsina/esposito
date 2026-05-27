#!/usr/bin/env python3
"""Generate supplement VLW font headers from multiple source fonts.

Primary: DejaVu Sans Mono (box-drawing + symbols)
Secondary: Noto Emoji (emoji like magnifying glass)
Output: fonts/supplement-N.h for each font size (6-14px).
"""

import struct
import sys
from pathlib import Path

try:
    import freetype
except ImportError:
    print("Install freetype-py:  pip install freetype-py")
    sys.exit(1)

DEJAVU_CHARSET = (
    list(range(0x2500, 0x2580))
    + [0x2190, 0x2191, 0x2192, 0x2193, 0x2194, 0x2195]
    + [0x2713, 0x2717, 0x2718, 0x2261, 0x2699, 0x23CE, 0x2610, 0x2611]
    + [0x2315]
    + [0x2026, 0x2264, 0x2265, 0x2260]
    + [0x25CF, 0x25CB, 0x25A0, 0x25A1, 0x25C6, 0x25C7]
    + [0x25B2, 0x25BC, 0x25C0, 0x25B6]
    + [0x2B07]
    + [0x21E9]
)

NOTO_EMOJI_CHARSET = []

FONTS_DIR = Path(__file__).parent.parent / "source_fonts"
SYMBOL_FONT = Path("/usr/share/fonts/TTF/DejaVuSansMono.ttf")
EMOJI_FONT = Path("/usr/share/fonts/TTF/NotoEmoji-Regular.ttf")
OUT_DIR = Path(__file__).parent.parent / "fonts"


def collect_glyph_sources():
    """Return list of (codepoint, font_path) for all supplement glyphs."""
    sources = []
    dejavu = freetype.Face(str(SYMBOL_FONT))
    for cp in DEJAVU_CHARSET:
        if dejavu.get_char_index(cp) != 0:
            sources.append((cp, str(SYMBOL_FONT), cp))
    emoji = freetype.Face(str(EMOJI_FONT))
    for cp, mapped_cp in NOTO_EMOJI_CHARSET:
        if emoji.get_char_index(cp) != 0:
            sources.append((mapped_cp, str(EMOJI_FONT), cp))
    return sources


def render_glyph(face, vlw_cp, render_cp):
    """Render render_cp from font, store in VLW as vlw_cp."""
    if face.get_char_index(render_cp) == 0:
        return None
    load_flags = freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_LIGHT | freetype.FT_LOAD_FORCE_AUTOHINT
    face.load_char(chr(render_cp), load_flags)

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

    return {
        "unicode": vlw_cp,
        "height": height,
        "width": width,
        "advance": advance,
        "top_offset": top_offset,
        "left_offset": left_offset,
        "bitmap": alpha,
    }


def generate_vlw(pixel_size: int, glyph_sources: list) -> bytes:
    """Generate VLW binary by rendering glyphs from their respective fonts."""
    face_cache = {}

    glyphs = []
    for vlw_cp, font_path, render_cp in glyph_sources:
        if font_path not in face_cache:
            face = freetype.Face(font_path)
            face.set_pixel_sizes(0, pixel_size)
            face_cache[font_path] = face
        else:
            face = face_cache[font_path]

        glyph = render_glyph(face, vlw_cp, render_cp)
        if glyph:
            glyphs.append(glyph)

    # Use DejaVu Sans Mono for base metrics
    base_face = face_cache[str(SYMBOL_FONT)]
    ascent = base_face.size.ascender >> 6
    descent = -(base_face.size.descender >> 6)

    buf = bytearray()
    buf += struct.pack(">I", len(glyphs))
    buf += struct.pack(">I", 6)
    buf += struct.pack(">I", pixel_size)
    buf += struct.pack(">I", 0)
    buf += struct.pack(">I", ascent)
    buf += struct.pack(">I", descent)

    for g in glyphs:
        buf += struct.pack(">I", g["unicode"])
        buf += struct.pack(">I", g["height"])
        buf += struct.pack(">I", g["width"])
        buf += struct.pack(">I", g["advance"])
        buf += struct.pack(">i", g["top_offset"])
        buf += struct.pack(">i", g["left_offset"])
        buf += struct.pack(">I", 0)

    for g in glyphs:
        buf += g["bitmap"]

    return bytes(buf)


def vlw_to_header(name: str, vlw_data: bytes) -> str:
    lines = [
        f"// Auto-generated supplement VLW font: {name}",
        f"// Size: {len(vlw_data)} bytes ({len(vlw_data) / 1024:.1f} KB)",
        "#pragma once",
        "#include <pgmspace.h>",
        "",
        f"const uint8_t {name}[] PROGMEM = {{",
    ]
    for i in range(0, len(vlw_data), 16):
        chunk = vlw_data[i:i + 16]
        hex_vals = ", ".join(f"0x{b:02X}" for b in chunk)
        lines.append(f"    {hex_vals},")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def main():
    if not SYMBOL_FONT.exists():
        print(f"DejaVu Sans Mono not found: {SYMBOL_FONT}")
        sys.exit(1)
    if not EMOJI_FONT.exists():
        print(f"Noto Emoji not found: {EMOJI_FONT}")
        sys.exit(1)

    glyph_sources = collect_glyph_sources()
    if not glyph_sources:
        print("No glyphs found.")
        return

    print(f"Supplement glyphs ({len(glyph_sources)}):")
    for vlw_cp, fp, render_cp in glyph_sources:
        font_name = Path(fp).stem
        if vlw_cp != render_cp:
            print(f"  U+{vlw_cp:04X} (mapped from U+{render_cp:04X}) from {font_name}")
        else:
            print(f"  U+{vlw_cp:04X} from {font_name}")

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for size in range(6, 15):
        print(f"Generating supplement-{size}... ", end="")
        vlw_data = generate_vlw(size, glyph_sources)
        header = vlw_to_header(f"supplement_{size}", vlw_data)
        out_path = OUT_DIR / f"supplement-{size}.h"
        out_path.write_bytes(header.encode("utf-8"))
        print(f"{len(vlw_data)} bytes -> {out_path}")

    print("Done.")


if __name__ == "__main__":
    main()
