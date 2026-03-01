#!/usr/bin/env bash
# Downloads CoreCAD proprietary branding assets from the private corecad-assets repo
# and extracts them to a target directory.
#
# The target directory can then be passed to cmake via:
#   cmake -DCORECAD_ASSETS_DIR=<target_dir> ...
#
# For local development the cmake build auto-detects ~/Repos/corecad-assets
# so this script is only needed for CI or when you don't have a local clone.
#
# Usage:
#   CORECAD_ASSETS_TOKEN=<pat> bash scripts/fetch-branding.sh [target_dir]
#
# If the token is absent or the download fails the script exits 0 so CI
# continues with the placeholder assets already in the repo.

set -uo pipefail

TARGET_DIR="${1:-${HOME}/Repos/corecad-assets-ci}"
TMPFILE=$(mktemp)

cleanup() { rm -f "$TMPFILE"; }
trap cleanup EXIT

if [ -z "${CORECAD_ASSETS_TOKEN:-}" ]; then
    echo "CORECAD_ASSETS_TOKEN not set — skipping branding asset download"
    exit 0
fi

echo "Fetching CoreCAD branding assets..."

HTTP_STATUS=$(curl -s -o "$TMPFILE" -w "%{http_code}" \
    -H "Authorization: Bearer $CORECAD_ASSETS_TOKEN" \
    -H "Accept: application/vnd.github+json" \
    -L "https://api.github.com/repos/CoreCAD/corecad-assets/tarball/main")

if [ "$HTTP_STATUS" != "200" ]; then
    echo "Warning: Failed to download branding assets (HTTP $HTTP_STATUS) — using placeholders"
    exit 0
fi

mkdir -p "$TARGET_DIR"
tar -xzf "$TMPFILE" --strip-components=1 -C "$TARGET_DIR"

echo "CoreCAD branding assets extracted to: $TARGET_DIR"
echo "Pass to cmake with: -DCORECAD_ASSETS_DIR=$TARGET_DIR"
