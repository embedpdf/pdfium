#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
TARGET="${1:-}"
OUTPUT_DIR="${PDF_RUNTIME_ARTIFACT_DIR:-$SOURCE_DIR/out/embedpdf-runtime-artifacts}"

if [[ -z "$TARGET" ]]; then
  echo "usage: $0 <target>" >&2
  exit 1
fi

OUT="$SOURCE_DIR/out/embedpdf-runtime/$TARGET"
STAGING="$SOURCE_DIR/out/embedpdf-runtime-staging/$TARGET"
SHA="$(git -C "$SOURCE_DIR" rev-parse HEAD)"
SHORT_SHA="$(git -C "$SOURCE_DIR" rev-parse --short HEAD)"
ARCHIVE="$OUTPUT_DIR/libembedpdf-pdf-runtime-$TARGET-$SHORT_SHA.tar.gz"

case "$TARGET" in
  win32-*)
    LIB_SOURCE="$OUT/pdfium.dll.lib"
    LIB_DEST="$STAGING/lib/pdfium.dll.lib"
    DLL_SOURCE="$OUT/pdfium.dll"
    DLL_DEST="$STAGING/bin/pdfium.dll"
    ;;
  darwin-*)
    LIB_SOURCE="$OUT/libpdfium.dylib"
    LIB_DEST="$STAGING/lib/libpdfium.dylib"
    ;;
  linux-*|android-*|arm64-v8a|armeabi-v7a|x86_64|x86)
    LIB_SOURCE="$OUT/libpdfium.so"
    LIB_DEST="$STAGING/lib/libpdfium.so"
    ;;
  ios-*|linuxmusl-*|wasm32)
    LIB_SOURCE="$OUT/obj/libpdfium.a"
    LIB_DEST="$STAGING/lib/libpdfium.a"
    ;;
  *)
    LIB_SOURCE="$OUT/obj/libpdfium.a"
    LIB_DEST="$STAGING/lib/libpdfium.a"
    ;;
esac

if [[ ! -f "$LIB_SOURCE" ]]; then
  echo "missing $LIB_SOURCE; run build-target.sh $TARGET first" >&2
  exit 1
fi
if [[ -n "${DLL_SOURCE:-}" && ! -f "$DLL_SOURCE" ]]; then
  echo "missing $DLL_SOURCE; run build-target.sh $TARGET first" >&2
  exit 1
fi

rm -rf "$STAGING"
mkdir -p "$STAGING/include" "$STAGING/lib" "$STAGING/LICENSES" "$OUTPUT_DIR"

cp -R "$SOURCE_DIR/public/." "$STAGING/include/"
cp "$LIB_SOURCE" "$LIB_DEST"
case "$TARGET" in
  darwin-*)
    install_name_tool -id "@rpath/libpdfium.dylib" "$LIB_DEST"
    ;;
esac
if [[ -n "${DLL_SOURCE:-}" ]]; then
  mkdir -p "$(dirname "$DLL_DEST")"
  cp "$DLL_SOURCE" "$DLL_DEST"
fi
cp "$OUT/args.gn" "$STAGING/args.gn"
cp "$SOURCE_DIR/LICENSE" "$STAGING/LICENSES/PDFIUM_LICENSE"

cat > "$STAGING/BUILD-METADATA.json" <<EOF
{
  "name": "embedpdf-pdf-runtime",
  "target": "$TARGET",
  "sha": "$SHA",
  "createdAt": "$(date -u +"%Y-%m-%dT%H:%M:%SZ")"
}
EOF

tar -czf "$ARCHIVE" -C "$STAGING" .
echo "$ARCHIVE"
