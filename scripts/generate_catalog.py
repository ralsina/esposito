#!/usr/bin/env python3
"""
Generate catalog.json and gather app ELFs for the App Store server.

Usage:
  python scripts/generate_catalog.py [--output-dir DIR] [--app NAME]

Quick start (from project root):
  1. Source ESP-IDF and build all apps first:  bash scripts/build_test.sh
  2. Run this script:  python3 scripts/generate_catalog.py

Output: site/assets/apps/ with catalog.json + app subdirectories containing
program.elf and manifest.cfg, ready to upload to esposito.ralsina.me/apps/
"""

import json
import sys
import subprocess
import argparse
import shutil
from pathlib import Path


def build_app(app_dir):
    """Build a single app ELF via build_app.sh."""
    deps_file = app_dir / "deps"
    deps = []
    if deps_file.exists():
        with open(deps_file) as f:
            deps = [line.strip() for line in f if line.strip()]

    app_src = app_dir / "app.c"
    if not app_src.exists():
        app_src = app_dir / "app.cpp"
    if not app_src.exists():
        return False

    cmd = ["bash", "scripts/build_app.sh"]
    for lib in deps:
        cmd.extend(["-l", lib])
    cmd.append(str(app_src))

    print(f"  Building {app_dir.name}...")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  FAILED:\n{result.stderr}")
        return False
    return True


def read_manifest(app_dir):
    """Read manifest.cfg from an app directory."""
    manifest_path = app_dir / "manifest.cfg"
    if not manifest_path.exists():
        return {}
    data = {}
    with open(manifest_path) as f:
        for line in f:
            line = line.strip()
            if "=" not in line:
                continue
            key, _, value = line.partition("=")
            data[key.strip()] = value.strip()
    return data


def generate(apps, output_dir):
    """Generate catalog.json and copy ELFs/manifests to output_dir."""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    catalog = []

    for app_name in sorted(apps):
        app_dir = Path("apps") / app_name
        manifest = read_manifest(app_dir)

        if manifest.get("launcher") != "yes":
            print(f"  Skipping {app_name}: not in launcher")
            continue

        elf_path = Path("build/apps") / f"{app_name}.elf"
        if not elf_path.exists():
            print(f"  Skipping {app_name}: ELF not found in build/apps/")
            continue

        size = elf_path.stat().st_size

        entry = {
            "id": app_name,
            "name": manifest.get("name", app_name),
            "short_description": manifest.get("short_description", ""),
            "long_description": manifest.get("long_description", ""),
            "homepage": manifest.get("homepage", ""),
            "version": manifest.get("version", ""),
            "requires": manifest.get("requires", ""),
            "extensions": manifest.get("extensions", ""),
            "size": size,
        }

        app_out = output_dir / app_name
        app_out.mkdir(exist_ok=True)

        shutil.copy2(str(elf_path), str(app_out / "program.elf"))
        manifest_src = app_dir / "manifest.cfg"
        if manifest_src.exists():
            shutil.copy2(str(manifest_src), str(app_out / "manifest.cfg"))

        print(f"  {app_name}: {size} bytes")
        catalog.append(entry)

    catalog_path = output_dir / "catalog.json"
    with open(catalog_path, "w") as f:
        json.dump(catalog, f, indent=2, ensure_ascii=False)
    print(f"\nWrote {catalog_path} ({len(catalog)} apps)")


def main():
    parser = argparse.ArgumentParser(
        description="Generate App Store catalog and gather ELFs for upload"
    )
    parser.add_argument(
        "--output-dir", "-o",
        default="site/assets/apps",
        help="Output directory for server files (default: site/assets/apps)"
    )
    parser.add_argument(
        "--app", "-a",
        help="Generate for a single app only (by directory name)"
    )
    args = parser.parse_args()

    apps_dir = Path("apps")
    if not apps_dir.is_dir():
        print("ERROR: run from project root (apps/ directory not found)")
        sys.exit(1)

    if args.app:
        app_names = [args.app]
    else:
        app_names = sorted([
            d.name for d in apps_dir.iterdir()
            if d.is_dir() and ((d / "app.c").exists() or (d / "app.cpp").exists())
        ])

    if not app_names:
        print("No apps found!")
        sys.exit(1)

    generate(app_names, args.output_dir)

    print(f"\nDone. Generated in '{args.output_dir}/'")
    print(f"Upload to esposito.ralsina.me/apps/")
    print(f"  rsync -av {args.output_dir}/ user@server:/path/to/apps/")


if __name__ == "__main__":
    main()
