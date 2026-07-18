#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
PATCH_DIR="$SOURCE_DIR/patches/embedpdf-runtime"

reverse_patch_file() {
  local workdir="$1"
  local patch_file="$2"

  if [[ ! -d "$workdir" || ! -f "$patch_file" ]]; then
    return
  fi

  (
    cd "$workdir"

    if patch --dry-run --reverse --forward --batch -p1 < "$patch_file" >/dev/null 2>&1; then
      echo "Reversing $patch_file"
      patch --reverse --forward --batch -p1 < "$patch_file"
      return
    fi

    echo "Not applied $patch_file"
  )
}

remove_copied_file() {
  local src="$1"
  local dst="$2"

  if [[ ! -f "$dst" ]]; then
    return
  fi

  if [[ ! -f "$src" ]] || ! cmp -s "$src" "$dst"; then
    echo "Refusing to remove copied patch output with local changes: $dst" >&2
    exit 1
  fi

  echo "Removing copied patch output $dst"
  rm "$dst"
  rmdir "$(dirname "$dst")" 2>/dev/null || true
}

remove_patch_backup() {
  local path="$1"

  if [[ -f "$path" ]]; then
    echo "Removing patch backup $path"
    rm "$path"
  fi
}

# Remove copied files before reversing patches so their parent directories can
# disappear cleanly if they were introduced only by the runtime patch set.
remove_copied_file "$PATCH_DIR/wasm/config.gn" "$SOURCE_DIR/build/config/wasm/BUILD.gn"
remove_copied_file "$PATCH_DIR/musl/toolchain.gn" "$SOURCE_DIR/build/toolchain/linux/musl/BUILD.gn"
remove_copied_file "$PATCH_DIR/win/resources.rc" "$SOURCE_DIR/resources.rc"

# Reverse every known runtime patch. Each reverse is independently optional so
# this script is safe to run before any target build, regardless of which target
# was built last.
reverse_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/win/build.patch"
reverse_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/mac/build.patch"
reverse_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/wasm/build.patch"
reverse_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/musl/build.patch"
reverse_patch_file "$SOURCE_DIR/build" "$PATCH_DIR/android/build.patch"
reverse_patch_file "$SOURCE_DIR/third_party/libjpeg_turbo" "$PATCH_DIR/ios/libjpeg_turbo.patch"
reverse_patch_file "$SOURCE_DIR/buildtools/third_party/libc++" "$PATCH_DIR/ios/libcxx_config_site.patch"
reverse_patch_file "$SOURCE_DIR" "$PATCH_DIR/musl/pdfium.patch"
reverse_patch_file "$SOURCE_DIR" "$PATCH_DIR/shared-library.patch"

# GNU patch can leave backup files after failed or manual patch attempts.
# Remove only the exact backup paths created by the known runtime patch set.
remove_patch_backup "$SOURCE_DIR/build/config/BUILDCONFIG.gn.orig"
remove_patch_backup "$SOURCE_DIR/build/config/compiler/BUILD.gn.orig"
remove_patch_backup "$SOURCE_DIR/build/toolchain/apple/toolchain.gni.orig"
remove_patch_backup "$SOURCE_DIR/build/toolchain/wasm/BUILD.gn.orig"
