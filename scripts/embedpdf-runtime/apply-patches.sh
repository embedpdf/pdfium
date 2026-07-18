#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
PATCH_DIR="$SOURCE_DIR/patches/embedpdf-runtime"
TARGET="${1:-}"

if [[ -z "$TARGET" ]]; then
  echo "usage: $0 <target>" >&2
  exit 1
fi

apply_patch_file() {
  local workdir="$1"
  local patch_file="$2"

  if [[ ! -d "$workdir" ]]; then
    echo "patch workdir does not exist: $workdir" >&2
    exit 1
  fi

  if [[ ! -f "$patch_file" ]]; then
    echo "patch file does not exist: $patch_file" >&2
    exit 1
  fi

  (
    cd "$workdir"

    if patch --dry-run --forward --batch -p1 < "$patch_file" >/dev/null; then
      echo "Applying $patch_file"
      patch --forward --batch -p1 < "$patch_file"
      return
    fi

    if patch --dry-run --reverse --batch -p1 < "$patch_file" >/dev/null; then
      echo "Already applied $patch_file"
      return
    fi

    echo "Could not apply $patch_file" >&2
    patch --dry-run --forward --batch -p1 < "$patch_file" >&2 || true
    exit 1
  )
}

copy_patch_file() {
  local src="$1"
  local dst="$2"

  if [[ ! -f "$src" ]]; then
    echo "patch file does not exist: $src" >&2
    exit 1
  fi

  mkdir -p "$(dirname "$dst")"

  if [[ -f "$dst" ]] && cmp -s "$src" "$dst"; then
    echo "Already copied $dst"
    return
  fi

  echo "Copying $src to $dst"
  cp "$src" "$dst"
}

"$SOURCE_DIR/scripts/embedpdf-runtime/unapply-patches.sh"

case "$TARGET" in
  wasm32)
    apply_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/wasm/build.patch"
    copy_patch_file "$PATCH_DIR/wasm/config.gn" "$SOURCE_DIR/build/config/wasm/BUILD.gn"
    ;;
  linuxmusl-*)
    apply_patch_file "$SOURCE_DIR" "$PATCH_DIR/musl/pdfium.patch"
    apply_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/musl/build.patch"
    copy_patch_file "$PATCH_DIR/musl/toolchain.gn" "$SOURCE_DIR/build/toolchain/linux/musl/BUILD.gn"
    ;;
  darwin-*)
    apply_patch_file "$SOURCE_DIR" "$PATCH_DIR/shared-library.patch"
    apply_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/mac/build.patch"
    ;;
  win32-*)
    apply_patch_file "$SOURCE_DIR" "$PATCH_DIR/shared-library.patch"
    apply_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/win/build.patch"
    copy_patch_file "$PATCH_DIR/win/resources.rc" "$SOURCE_DIR/resources.rc"
    ;;
  linux-*)
    apply_patch_file "$SOURCE_DIR" "$PATCH_DIR/shared-library.patch"
    ;;
  android-* | arm64-v8a | armeabi-v7a | x86 | x86_64)
    apply_patch_file "$SOURCE_DIR" "$PATCH_DIR/shared-library.patch"
    apply_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/android/build.patch"
    ;;
  ios-*)
    apply_patch_file "$SOURCE_DIR/third_party/libjpeg_turbo" "$PATCH_DIR/ios/libjpeg_turbo.patch"
    apply_patch_file "$SOURCE_DIR/buildtools/third_party/libc++" "$PATCH_DIR/ios/libcxx_config_site.patch"
    ;;
  *)
    echo "unknown target: $TARGET" >&2
    exit 1
    ;;
esac
