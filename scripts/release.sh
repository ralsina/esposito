#!/bin/bash
#
# release.sh — Tag a release, wait for CI, sync firmware to the site, deploy.
#
# Usage: scripts/release.sh <version>
#   e.g.  scripts/release.sh 0.5.3
#
# What it does:
#   1. Bumps version in main/main.c, commits, tags v<version>, pushes
#   2. Waits for the GitHub Actions release workflow to complete
#   3. Downloads per-board firmware binaries into site/assets/firmware/
#   4. Regenerates site/assets/firmware/manifest.json
#   5. Commits + pushes the firmware files
#   6. Builds the site (nicolino build)
#   7. Deploys via rsync to production
#
set -euo pipefail

# --- config ------------------------------------------------------------------

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

# Flash offsets from partitions.csv (stable unless partition table changes).
# ESP32 (CYD)
OFFSET_BOOTLOADER=4096      # 0x1000
# ESP32-S3 (Guition)
OFFSET_BOOTLOADER_S3=0      # 0x0
OFFSET_PARTITIONS=32768     # 0x8000
OFFSET_FIRMWARE=65536       # 0x10000
OFFSET_OTA_DATA=2162688     # 0x210000

RSYNC_TARGET="root@rocky:/data/stacks/web/websites/esposito.ralsina.me/"
FIRMWARE_DIR="site/assets/firmware"

# --- args --------------------------------------------------------------------

if [ $# -lt 1 ]; then
    echo "Usage: $0 <version>"
    echo "  e.g.  $0 0.5.3"
    exit 1
fi

VERSION="$1"
TAG="v${VERSION}"

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  Esposito release: ${TAG}                                      ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# --- 1. Bump version, commit, tag, push -------------------------------------

CURRENT=$(grep -oP 'Esposito OS v\K[0-9]+\.[0-9]+\.[0-9]+' main/main.c || echo "unknown")
echo ">>> Current version: v${CURRENT}"
echo ">>> Bumping to:      ${TAG}"

sed -i "s/Esposito OS v[0-9]\+\.[0-9]\+\.[0-9]\+/Esposito OS v${VERSION}/" main/main.c

if git diff --quiet main/main.c; then
    echo "ERROR: version was already ${VERSION} in main.c"
    exit 1
fi

git add main/main.c
git commit -m "Bump version to ${TAG}"
git tag "$TAG"
git push origin main
git push origin "$TAG"

echo ">>> Tagged ${TAG} and pushed."
echo ""

# --- 2. Wait for GitHub Actions ----------------------------------------------

echo ">>> Waiting for release workflow to complete..."
sleep 5  # let the run register

RUN_ID=$(gh run list --workflow=release.yml --limit=1 --json databaseId,headBranch \
         --jq ".[0].databaseId")
echo "    Workflow run: ${RUN_ID}"

if ! gh run watch "$RUN_ID" --exit-status; then
    echo ""
    echo "❌ Release workflow FAILED."
    echo "   Check: https://github.com/ralsina/esposito/actions/runs/${RUN_ID}"
    echo ""
    echo "   The tag ${TAG} was pushed but the build failed."
    echo "   Fix the issue, then re-run:"
    echo "     git tag -f ${TAG} HEAD && git push origin ${TAG} --force"
    echo "     gh workflow run release.yml --ref ${TAG} -f tag=${TAG}"
    exit 1
fi

echo "✅ Workflow succeeded."
echo ""

# --- 3. Download firmware binaries ------------------------------------------

echo ">>> Downloading release assets..."

# CYD (ESP32) firmware into esp32/ subdirectory
mkdir -p "${FIRMWARE_DIR}/esp32"
for f in bootloader.bin partition-table.bin ota_data_initial.bin; do
    gh release download "$TAG" --pattern "esp32/$f" --clobber --dir "${FIRMWARE_DIR}/esp32"
    # gh may flatten; if file ends up in root, move it
    if [ -f "${FIRMWARE_DIR}/esp32/$f" ]; then
        echo "    + esp32/$f ($(du -h "${FIRMWARE_DIR}/esp32/$f" | cut -f1))"
    elif [ -f "${FIRMWARE_DIR}/$f" ]; then
        mv "${FIRMWARE_DIR}/$f" "${FIRMWARE_DIR}/esp32/$f"
    fi
done

# Guition (ESP32-S3) firmware into esp32s3/ subdirectory
mkdir -p "${FIRMWARE_DIR}/esp32s3"
for f in bootloader.bin partition-table.bin ota_data_initial.bin firmware-esp32s3.bin; do
    gh release download "$TAG" --pattern "esp32s3/$f" --clobber --dir "${FIRMWARE_DIR}/esp32s3"
    if [ -f "${FIRMWARE_DIR}/esp32s3/$f" ]; then
        echo "    + esp32s3/$f ($(du -h "${FIRMWARE_DIR}/esp32s3/$f" | cut -f1))"
    fi
done

# Rename firmware-esp32s3.bin → firmware.bin in the esp32s3 subdirectory
if [ -f "${FIRMWARE_DIR}/esp32s3/firmware-esp32s3.bin" ]; then
    mv "${FIRMWARE_DIR}/esp32s3/firmware-esp32s3.bin" "${FIRMWARE_DIR}/esp32s3/firmware.bin"
fi

# Download OTA firmware (flat, for site root / OTA server)
for f in firmware.bin firmware-esp32s3.bin firmware.bin.sig firmware-esp32s3.bin.sig; do
    gh release download "$TAG" --pattern "$f" --clobber --dir "${FIRMWARE_DIR}"
    echo "    + $f ($(du -h "${FIRMWARE_DIR}/${f}" | cut -f1))"
done

echo ""

# --- 3b. Download app store bundle ------------------------------------------

echo ">>> Downloading app store bundle..."
mkdir -p /tmp/esposito-release
gh release download "$TAG" --pattern "app-store-bundle.zip" --clobber --dir /tmp/esposito-release
rm -rf site/assets/apps/*
unzip -q -o /tmp/esposito-release/app-store-bundle.zip -d /tmp/esposito-release/
cp -r /tmp/esposito-release/app-store/* site/assets/apps/
rm -rf /tmp/esposito-release
echo "    + $(ls site/assets/apps/ | grep -v catalog | wc -l) apps + catalog.json"
echo ""

# --- 4. Regenerate manifest.json ---------------------------------------------

echo ">>> Regenerating manifest.json..."

cat > "${FIRMWARE_DIR}/manifest.json" <<EOF
{
  "name": "Esposito OS",
  "version": "${VERSION}",
  "home_assistant_domain": "esposito",
  "new_install_prompt_erase": false,
  "builds": [
    {
      "chipFamily": "ESP32",
      "parts": [
        { "path": "esp32/bootloader.bin", "offset": ${OFFSET_BOOTLOADER} },
        { "path": "esp32/partition-table.bin", "offset": ${OFFSET_PARTITIONS} },
        { "path": "esp32/firmware.bin", "offset": ${OFFSET_FIRMWARE} },
        { "path": "esp32/ota_data_initial.bin", "offset": ${OFFSET_OTA_DATA} }
      ]
    },
    {
      "chipFamily": "ESP32-S3",
      "parts": [
        { "path": "esp32s3/bootloader.bin", "offset": ${OFFSET_BOOTLOADER_S3} },
        { "path": "esp32s3/partition-table.bin", "offset": ${OFFSET_PARTITIONS} },
        { "path": "esp32s3/firmware.bin", "offset": ${OFFSET_FIRMWARE} },
        { "path": "esp32s3/ota_data_initial.bin", "offset": ${OFFSET_OTA_DATA} }
      ]
    }
  ]
}
EOF

echo "    + manifest.json (v${VERSION})"
echo ""

# --- 5. Commit firmware files, push -----------------------------------------

echo ">>> Committing firmware + app store files to main..."
git add "${FIRMWARE_DIR}/" site/assets/apps/
git commit -m "Update site firmware + app store assets for ${TAG}

Downloaded from release ${TAG} and staged for same-origin hosting."
git push origin main

echo ""

# --- 6. Build site -----------------------------------------------------------

echo ">>> Building site..."
cd site
nicolino build
echo ""

# --- 7. Deploy via rsync -----------------------------------------------------

echo ">>> Deploying to production..."
echo "    rsync → ${RSYNC_TARGET}"
echo ""

rsync -rav --delete output/* "${RSYNC_TARGET}"

echo ""
echo "╔══════════════════════════════════════════════════════════════╗"
echo "║  ✅  ${TAG} released and deployed!                               ║"
echo "╠══════════════════════════════════════════════════════════════╣"
echo "║                                                              ║"
echo "║  GitHub release: https://github.com/ralsina/esposito/releases/tag/${TAG}"
echo "║  Install page:   https://esposito.ralsina.me/install/        ║"
echo "║                                                              ║"
echo "╚══════════════════════════════════════════════════════════════╝"
