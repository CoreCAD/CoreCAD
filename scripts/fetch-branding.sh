#!/usr/bin/env bash
# Downloads CoreCAD proprietary branding assets from the private corecad-assets repo
# and overlays them onto the source tree.
#
# Requires CORECAD_ASSETS_TOKEN to be set in the environment (a GitHub PAT with
# Contents: Read-only access to CoreCAD/corecad-assets).
#
# If the token is absent or the download fails the script exits 0 so the build
# continues with the placeholder assets already in the repo.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TMPFILE="$REPO_ROOT/.corecad-assets-download.tar.gz"

cleanup() { rm -f "$TMPFILE"; }
trap cleanup EXIT

if [ -z "${CORECAD_ASSETS_TOKEN:-}" ]; then
    echo "CORECAD_ASSETS_TOKEN not set — skipping branding asset download (using placeholder assets)"
    exit 0
fi

echo "Fetching CoreCAD branding assets..."

HTTP_STATUS=$(curl -s -o "$TMPFILE" -w "%{http_code}" \
    -H "Authorization: Bearer $CORECAD_ASSETS_TOKEN" \
    -H "Accept: application/vnd.github+json" \
    -L "https://api.github.com/repos/CoreCAD/corecad-assets/tarball/main")

if [ "$HTTP_STATUS" != "200" ]; then
    echo "Warning: Failed to download branding assets (HTTP $HTTP_STATUS) — using placeholder assets"
    exit 0
fi

tar -xzf "$TMPFILE" --strip-components=1 -C "$REPO_ROOT" \
    --exclude='README.md' --exclude='README*' --exclude='LICENSE*' --exclude='.git*'
echo "CoreCAD branding assets applied."
