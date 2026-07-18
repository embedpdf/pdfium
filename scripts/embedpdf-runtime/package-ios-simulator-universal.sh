#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
ARTIFACT_DIR="${PDF_RUNTIME_ARTIFACT_DIR:-$SOURCE_DIR/out/embedpdf-runtime-artifacts}"

# This script combines ios-simulator-arm64 and ios-simulator-x64 into a single
# universal library and packages it. It expects that both targets have already
# been built and packaged with package-target.sh.

STAGING="$SOURCE_DIR/out/embedpdf-runtime-staging/ios-simulator-universal"
mkdir -p "$STAGING"

# Find the latest artifacts for both targets
ARM64_ARTIFACT=$(ls -t "$ARTIFACT_DIR"/libembedpdf-pdf-runtime-ios-simulator-arm64-*.tar.gz 2>/dev/null | head -n 1 || true)
X64_ARTIFACT=$(ls -t "$ARTIFACT_DIR"/libembedpdf-pdf-runtime-ios-simulator-x64-*.tar.gz 2>/dev/null | head -n 1 || true)

if [[ -z "$ARM64_ARTIFACT" || -z "$X64_ARTIFACT" ]]; then
  echo "Error: Missing required artifacts for ios-simulator-arm64 and/or ios-simulator-x64" >&2
  exit 1
fi

echo "Combining artifacts:"
echo "  $ARM64_ARTIFACT"
echo "  $X64_ARTIFACT"

TMP_ARM64="$STAGING/arm64"
TMP_X64="$STAGING/x64"
mkdir -p "$TMP_ARM64" "$TMP_X64"

tar -xzf "$ARM64_ARTIFACT" -C "$TMP_ARM64"
tar -xzf "$X64_ARTIFACT" -C "$TMP_X64"

# Combine with lipo
mkdir -p "$STAGING/lib"
lipo -create "$TMP_ARM64/lib/libpdfium.a" "$TMP_X64/lib/libpdfium.a" -output "$STAGING/lib/libpdfium.a"

# Copy headers and other metadata from arm64 (they should be identical)
cp -R "$TMP_ARM64/include" "$STAGING/include"
cp "$TMP_ARM64/args.gn" "$STAGING/args.gn"
if [[ -d "$TMP_ARM64/LICENSES" ]]; then
  cp -R "$TMP_ARM64/LICENSES" "$STAGING/LICENSES"
fi

# Write metadata
cat > "$STAGING/BUILD-METADATA.json" <<EOF
{
  "name": "embedpdf-pdf-runtime",
  "target": "ios-arm64_x86_64-simulator",
  "sha": "$(git -C "$SOURCE_DIR" rev-parse HEAD)",
  "createdAt": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
}
EOF

# Package
SHORT_SHA=$(git -C "$SOURCE_DIR" rev-parse --short HEAD)
OUT_FILE="$ARTIFACT_DIR/libembedpdf-pdf-runtime-ios-arm64_x86_64-simulator-$SHORT_SHA.tar.gz"
mkdir -p "$(dirname "$OUT_FILE")"
tar -czf "$OUT_FILE" -C "$STAGING" .

echo "Created universal artifact: $OUT_FILE"
