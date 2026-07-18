#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
TARGET="${1:-}"
TEST_SUITE="${PDFIUM_TEST_SUITE:-all}"

if [[ -z "$TARGET" ]]; then
  echo "usage: $0 <target>" >&2
  exit 1
fi

case "$TEST_SUITE" in
  all|unit|embedder) ;;
  *)
    echo "PDFIUM_TEST_SUITE must be one of: all, unit, embedder" >&2
    exit 1
    ;;
esac

case "$TARGET" in
  darwin-arm64)
    GN_TARGET_OS="mac"
    GN_TARGET_CPU="arm64"
    ;;
  darwin-x64)
    GN_TARGET_OS="mac"
    GN_TARGET_CPU="x64"
    ;;
  linux-x64)
    GN_TARGET_OS="linux"
    GN_TARGET_CPU="x64"
    ;;
  linux-arm64)
    GN_TARGET_OS="linux"
    GN_TARGET_CPU="arm64"
    EXTRA_ARGS=$'\narm_control_flow_integrity="none"'
    ;;
  android-arm64 | arm64-v8a)
    GN_TARGET_OS="android"
    GN_TARGET_CPU="arm64"
    ;;
  android-arm | armeabi-v7a)
    GN_TARGET_OS="android"
    GN_TARGET_CPU="arm"
    ;;
  android-x64 | x86_64)
    GN_TARGET_OS="android"
    GN_TARGET_CPU="x64"
    ;;
  android-x86 | x86)
    GN_TARGET_OS="android"
    GN_TARGET_CPU="x86"
    ;;
  ios-arm64)
    GN_TARGET_OS="ios"
    GN_TARGET_CPU="arm64"
    EXTRA_ARGS=$'\nios_enable_code_signing=false'
    ;;
  ios-simulator-arm64)
    GN_TARGET_OS="ios"
    GN_TARGET_CPU="arm64"
    EXTRA_ARGS=$'\nios_enable_code_signing=false\nuse_ios_simulator=true'
    ;;
  ios-simulator-x64)
    GN_TARGET_OS="ios"
    GN_TARGET_CPU="x64"
    EXTRA_ARGS=$'\nios_enable_code_signing=false\nuse_ios_simulator=true'
    ;;
  *)
    echo "test-target.sh only supports host-native and cross-buildable targets: darwin-arm64, darwin-x64, linux-x64, linux-arm64, android-*, ios-*" >&2
    exit 1
    ;;
esac

PDF_RUNTIME_TARGET_OS_LIST="${PDF_RUNTIME_TARGET_OS_LIST:-$GN_TARGET_OS}" \
  "$SOURCE_DIR/scripts/embedpdf-runtime/ensure-deps.sh"

"$SOURCE_DIR/scripts/embedpdf-runtime/apply-patches.sh" "$TARGET"

if [[ "$TARGET" == linux-* || "$TARGET" == android-* || "$TARGET" == arm64-v8a || "$TARGET" == armeabi-v7a || "$TARGET" == x86 || "$TARGET" == x86_64 ]]; then
  (
    cd "$SOURCE_DIR"
    if [[ "$TARGET" == android-* || "$TARGET" == arm64-v8a || "$TARGET" == armeabi-v7a || "$TARGET" == x86 || "$TARGET" == x86_64 ]]; then
       # For android, we might need --android but it's often slow and interactive.
       # Most runners have what's needed. Let's try without --android first but keep the base linux deps.
       build/install-build-deps.sh --no-prompt --no-arm --no-chromeos-fonts
    else
       build/install-build-deps.sh --no-prompt
    fi
    if [[ "$GN_TARGET_OS" == "linux" ]]; then
      build/linux/sysroot_scripts/install-sysroot.py "--arch=$GN_TARGET_CPU"
    fi
  )
fi

OUT="$SOURCE_DIR/out/embedpdf-runtime-tests-$TARGET"
RESULTS="$SOURCE_DIR/out/embedpdf-runtime-test-results/$TARGET"
mkdir -p "$OUT" "$RESULTS"

cat > "$OUT/args.gn" <<EOF
is_debug=true
treat_warnings_as_errors=false
pdf_use_skia=false
pdf_enable_xfa=false
pdf_enable_v8=false
is_component_build=false
clang_use_chrome_plugins=false
pdf_is_standalone=true
use_debug_fission=false
pdf_is_complete_lib=false
pdf_use_partition_alloc=false
symbol_level=1
target_os="$GN_TARGET_OS"
target_cpu="$GN_TARGET_CPU"${EXTRA_ARGS:-}
EOF

targets=()

# iOS builds skip tests entirely (test targets are excluded from the iOS build).
if [[ "$GN_TARGET_OS" == "ios" ]]; then
  echo "=== tests are not supported on iOS, skipping ==="
  echo "$RESULTS"
  exit 0
fi

case "$TEST_SUITE" in
  all)
    targets+=(pdfium_unittests pdfium_embeddertests)
    ;;
  unit)
    targets+=(pdfium_unittests)
    ;;
  embedder)
    targets+=(pdfium_embeddertests)
    ;;
esac

(
  cd "$SOURCE_DIR"
  gn gen "$OUT"
  ninja -C "$OUT" "${targets[@]}"
)

run_gtest() {
  local binary="$1"
  local filter="$2"
  local xml="$RESULTS/$binary.xml"
  local log="$RESULTS/$binary.log"
  local cmd=("$OUT/$binary" "--gtest_output=xml:$xml")
  local extra_gtest_args=()

  if [[ -n "$filter" ]]; then
    cmd+=("--gtest_filter=$filter")
  fi

  if [[ -n "${PDFIUM_GTEST_ARGS:-}" ]]; then
    # Intentionally shell-split simple gtest flags such as "--write-pngs".
    # Do not use this for arguments containing spaces.
    extra_gtest_args=($PDFIUM_GTEST_ARGS)
    cmd+=("${extra_gtest_args[@]}")
  fi

  if [[ "$GN_TARGET_OS" == "android" || "$GN_TARGET_OS" == "ios" ]]; then
    echo "=== building $binary (execution skipped on $GN_TARGET_OS) ==="
    # We already built it with ninja, so we just exit here.
    return 0
  fi

  echo "=== running $binary ==="
  "${cmd[@]}" 2>&1 | tee "$log"
}

case "$TEST_SUITE" in
  all)
    run_gtest pdfium_unittests "${PDFIUM_UNIT_FILTER:-}"
    run_gtest pdfium_embeddertests "${PDFIUM_EMBEDDER_FILTER:-}"
    ;;
  unit)
    run_gtest pdfium_unittests "${PDFIUM_UNIT_FILTER:-}"
    ;;
  embedder)
    run_gtest pdfium_embeddertests "${PDFIUM_EMBEDDER_FILTER:-}"
    ;;
esac

echo "$RESULTS"
