#!/usr/bin/env bash
# Create a release: bump version, tag, push (triggers CI release workflow).
#
# Usage:
#   scripts/release.sh v0.3
#   scripts/release.sh v0.3-beta.1
#
# What this script does:
#   1. Verifies working tree is clean
#   2. Updates the version string in main/main.c
#   3. Commits the version bump
#   4. Creates an annotated git tag
#   5. Pushes commit + tag to origin
#   6. CI (.github/workflows/release.yml) builds firmware + apps, signs,
#      and creates the GitHub Release automatically.

set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <version_tag>"
    echo "Examples:"
    echo "  $0 v0.3"
    echo "  $0 v0.3-beta.1"
    exit 1
fi

TAG="$1"

if [[ ! "$TAG" =~ ^v[0-9]+\.[0-9]+ ]]; then
    echo "Error: tag must start with 'v' and a major.minor number, e.g. v0.3"
    exit 1
fi

cd "$(dirname "$0")/.."

if [ -n "$(git status --porcelain)" ]; then
    echo "Error: working tree is not clean. Commit or stash first."
    git status --short
    exit 1
fi

if git tag -l "$TAG" | grep -q "$TAG"; then
    echo "Error: tag $TAG already exists"
    exit 1
fi

echo "=== Updating version string ==="
sed -i "s/Esposito OS v[0-9][^\"']*\"/Esposito OS ${TAG}\"/" main/main.c
grep "Esposito OS" main/main.c

echo "=== Committing version bump ==="
git add main/main.c
git commit -m "Release ${TAG}"

echo "=== Tagging ==="
git tag -a "$TAG" -m "Release ${TAG}"

echo "=== Pushing ==="
git push origin main
git push origin "$TAG"

echo ""
echo "Done! CI will build and publish the release."
echo "Monitor at: https://github.com/$(git remote get-url origin | sed 's/.*github.com[:/]\(.*\)\.git/\1/' | sed 's/.*github.com[:/]\(.*\)/\1/')/actions"
