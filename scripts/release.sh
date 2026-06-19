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
#   3. Downloads the 4 flash binaries into site/assets/firmware/
#   4. Regenerates site/assets/firmware/manifest.json
#   5. Commits + pushes the firmware files
#   6. Builds the site (nicolino build)
#   7. Deploys via rsync to production
#
set -euo pipefail

# --- config ------------------------------------------------------------------

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

# Flash offsets from partitions.csv (stable unless partition table changes)
OFFSET_BOOTLOADER=4096      # 0x1000
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
mkdir -p "${FIRMWARE_DIR}"

for f in bootloader.bin partition-table.bin ota_data_initial.bin firmware.bin; do
    gh release download "$TAG" --pattern "$f" --clobber --dir "${FIRMWARE_DIR}"
    echo "    + ${f} ($(du -h "${FIRMWARE_DIR}/${f}" | cut -f1))"
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
        { "path": "/firmware/bootloader.bin", "offset": ${OFFSET_BOOTLOADER} },
        { "path": "/firmware/partition-table.bin", "offset": ${OFFSET_PARTITIONS} },
        { "path": "/firmware/firmware.bin", "offset": ${OFFSET_FIRMWARE} },
        { "path": "/firmware/ota_data_initial.bin", "offset": ${OFFSET_OTA_DATA} }
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
