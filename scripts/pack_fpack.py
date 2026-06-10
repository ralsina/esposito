#!/usr/bin/env python3
"""Bundle VLW font variants + supplement into self-describing .fpack files.

Scans a directory for .vlw files, groups them by family+size, and writes
.fpack bundles with a header containing name, family, size, and pre-computed
metrics (char_width, char_height).

Usage:
  python scripts/pack_fpack.py /path/to/vlw_files/ /path/to/output/
"""

import struct
import sys
from pathlib import Path

FPACK_MAGIC = 0x4650414B  # "FPAK"
FPACK_VERSION = 1
FPACK_HEADER_SIZE = 116


def read_be32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def read_be_i32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">i", data, offset)[0]


def vlw_parse_metrics(vlw_data: bytes):
    """Extract glyph count, column width, and line height from VLW binary.

    The column width is set to the most common x_advance value (modal advance),
    which gives the correct cell width for monospaced fonts. Max advance would
        be inflated by wide box-drawing characters.
        """
    if len(vlw_data) < 24:
        return 0, 0, 0, 0
    glyph_count = read_be32(vlw_data, 0)
    ascent = abs(read_be_i32(vlw_data, 16))
    descent = abs(read_be_i32(vlw_data, 20))
    height = max(ascent + descent, 9)

    advance_counts = {}
    for i in range(glyph_count):
        off = 24 + i * 28
        if off + 20 > len(vlw_data):
            break
        xa = read_be32(vlw_data, off + 12)
        if xa < 100:
            advance_counts[xa] = advance_counts.get(xa, 0) + 1

    modal_advance = max(advance_counts, key=advance_counts.get) if advance_counts else 6
    width = max(modal_advance, 4)
    return glyph_count, width, height


def load_vlw(path: Path) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def parse_variant_name(vlw_path: Path):
    """Given a filename like 'hack_bold-10.vlw', return (family_base, variant_suffix, size)."""
    stem = vlw_path.stem  # e.g. "hack_bold-10"

    # Split on last dash to get size
    if "-" not in stem:
        return None, None, None
    base, size_str = stem.rsplit("-", 1)
    try:
        size = int(size_str)
    except ValueError:
        return None, None, None

    # The variant suffix is everything after the base family name
    # For "hack_bold": family="hack", suffix="_bold"
    # For "hack": family="hack", suffix=""
    # For "supplement": family="supplement", suffix=""
    # For "lucide-10": family="lucide", suffix=""
    family_base = base  # default: no suffix
    variant_suffix = ""

    # Check known variant suffixes
    for suffix in ["_bolditalic", "_bold", "_italic"]:
        if base.endswith(suffix):
            family_base = base[: -len(suffix)]
            variant_suffix = suffix
            break

    return family_base, variant_suffix, size


def variant_suffix_to_index(suffix: str) -> int:
    mapping = {"": 0, "_bold": 1, "_italic": 2, "_bolditalic": 3}
    return mapping.get(suffix, -1)


def pack_fpack(vlw_dir: Path, output_dir: Path):
    """Scan vlw_dir for .vlw files and produce .fpack bundles in output_dir."""
    vlw_files = sorted(vlw_dir.glob("*.vlw"))
    if not vlw_files:
        print(f"No .vlw files found in {vlw_dir}")
        return

    # Group by (family_base, size)
    groups: dict[tuple[str, int], dict[str, Path]] = {}
    supplement_by_size: dict[int, Path] = {}

    for f in vlw_files:
        family, suffix, size = parse_variant_name(f)
        if family is None:
            print(f"  Skipping unrecognized .vlw file: {f.name}")
            continue

        if family == "supplement":
            supplement_by_size[size] = load_vlw(f)
            continue

        key = (family, size)
        if key not in groups:
            groups[key] = {}
        groups[key][suffix] = f

    output_dir.mkdir(parents=True, exist_ok=True)

    for (family, size), variants in sorted(groups.items()):
        # Sanity: require at least regular variant
        if "" not in variants:
            print(f"  Skipping {family} {size}: no regular variant found")
            continue

        # Build the name like "hack 10", family like "hack"
        # The family name from filenames is like "hack" (from HackNerdFont)
        name = f"{family} {size}"

        # Load all VLW data
        regular_data = load_vlw(variants[""])
        _, char_width, char_height = vlw_parse_metrics(regular_data)

        # Collect variant data in index order: regular, bold, italic, bolditalic
        variant_order = ["", "_bold", "_italic", "_bolditalic"]
        variant_data = []
        for suffix in variant_order:
            p = variants.get(suffix)
            if p:
                variant_data.append(load_vlw(p))
            else:
                variant_data.append(b"")

        # Load supplement data for this size
        supp_data = supplement_by_size.get(size, b"")

        # Build offsets
        offset = FPACK_HEADER_SIZE
        variant_offsets = []
        variant_sizes = []
        for vd in variant_data:
            variant_offsets.append(offset if vd else 0)
            sz = len(vd)
            variant_sizes.append(sz)
            if vd:
                offset += sz

        supp_offset = offset if supp_data else 0
        supp_size = len(supp_data)

        # Sanity check
        if all(s == 0 for s in variant_sizes):
            print(f"  Skipping {name}: no variant data")
            continue

        # Build header
        header = struct.pack(">II", FPACK_MAGIC, FPACK_VERSION)
        name_bytes = name.encode("utf-8")
        family_bytes = family.encode("utf-8")
        header += name_bytes.ljust(32, b"\x00")[:32]
        header += family_bytes.ljust(32, b"\x00")[:32]
        header += struct.pack("BBBB", size, char_width, char_height, 0)
        for i in range(4):
            header += struct.pack(">II", variant_offsets[i], variant_sizes[i])
        header += struct.pack(">II", supp_offset, supp_size)

        assert len(header) == FPACK_HEADER_SIZE, f"header size mismatch: {len(header)} != {FPACK_HEADER_SIZE}"

        # Write .fpack
        out_path = output_dir / f"{name.replace(' ', '-')}.fpack"
        with open(out_path, "wb") as f:
            f.write(header)
            for vd in variant_data:
                if vd:
                    f.write(vd)
            if supp_data:
                f.write(supp_data)

        variant_names = [f"{family}{s}-{size}" for s in variant_order if variants.get(s)]
        supp_note = f"+ supplement" if supp_data else ""
        print(f"  {out_path.name}: {', '.join(variant_names)} {supp_note}")
        print(f"    -> {name}: {char_width}x{char_height}, {len(header) + sum(variant_sizes) + supp_size} bytes")

    total = len(groups)
    print(f"\nPacked {total} font{'s' if total != 1 else ''}.")


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)

    vlw_dir = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])

    if not vlw_dir.is_dir():
        print(f"Error: {vlw_dir} is not a directory")
        sys.exit(1)

    pack_fpack(vlw_dir, output_dir)


if __name__ == "__main__":
    main()
