#!/usr/bin/env bash
#
# INFINIVERB — renders the user manual.
#
#   docs/manual/manual.html  ->  plugin/assets/INFINIVERB-Manual.pdf
#
# The version printed in the manual is the one in CMakeLists.txt; the version
# a PDF was rendered for is recorded in docs/manual/rendered-version.txt, and
# package_mac.sh refuses to ship a manual rendered for another version. The
# PDF is embedded in the plug-in, so rebuild after rendering.
#
# Needs Google Chrome (headless). It runs with a temporary profile and leaves
# the user's own Chrome profile alone.
#
set -euo pipefail

BASE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MANUAL_DIR="$BASE_DIR/docs/manual"
OUT="$BASE_DIR/plugin/assets/INFINIVERB-Manual.pdf"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"

VERSION="$(sed -n 's/^project(RVB1 VERSION \([0-9.]*\).*/\1/p' "$BASE_DIR/CMakeLists.txt")"
[ -n "$VERSION" ] || { echo "Could not read the version from CMakeLists.txt" >&2; exit 1; }
[ -x "$CHROME" ]  || { echo "Google Chrome not found at: $CHROME (set CHROME=...)" >&2; exit 1; }

# Rendered from a copy beside the source, so its relative paths (images, the
# Space Mono font in plugin/assets) still resolve.
RENDER_HTML="$MANUAL_DIR/.render.html"
PROFILE="$(mktemp -d)"
trap 'rm -rf "$PROFILE" "$RENDER_HTML"' EXIT

sed "s/{{VERSION}}/$VERSION/g" "$MANUAL_DIR/manual.html" > "$RENDER_HTML"
URL="file://$(python3 -c 'import sys, urllib.parse; print(urllib.parse.quote(sys.argv[1]))' "$RENDER_HTML")"

rm -f "$OUT"
# No keychain (a fresh profile would otherwise ask for access to Chrome's
# keychain item). Headless Chrome does not always exit after printing, so the
# script waits for the PDF and then closes the instance it started.
"$CHROME" --headless=new --disable-gpu --no-first-run --no-default-browser-check \
          --use-mock-keychain --password-store=basic \
          --user-data-dir="$PROFILE" --no-pdf-header-footer \
          --print-to-pdf="$OUT" "$URL" >/dev/null 2>&1 &
CHROME_PID=$!
for _ in $(seq 1 120); do
  kill -0 "$CHROME_PID" 2>/dev/null || break
  if [ -s "$OUT" ]; then sleep 1; kill "$CHROME_PID" 2>/dev/null || true; break; fi
  sleep 0.5
done
kill "$CHROME_PID" 2>/dev/null || true
wait "$CHROME_PID" 2>/dev/null || true

[ -s "$OUT" ] || { echo "Chrome produced no PDF" >&2; exit 1; }
echo "$VERSION" > "$MANUAL_DIR/rendered-version.txt"
echo "Rendered $OUT (version $VERSION, $(du -h "$OUT" | cut -f1))"
