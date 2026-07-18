#!/usr/bin/env bash
set -euo pipefail

SOURCE_DIR="${PDF_RUNTIME_SOURCE_DIR:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
WORKSPACE_DIR="$(cd "$SOURCE_DIR/.." && pwd)"
SOLUTION_NAME="$(basename "$SOURCE_DIR")"
FORK_URL="${PDF_RUNTIME_FORK_URL:-https://github.com/embedpdf/pdfium.git}"
REF="${PDF_RUNTIME_REF:-HEAD}"
TARGET_OS_LIST="${PDF_RUNTIME_TARGET_OS_LIST:-}"

write_target_os() {
  local first=true

  printf 'target_os = ['
  for target_os in $TARGET_OS_LIST; do
    if [[ "$first" == true ]]; then
      first=false
    else
      printf ','
    fi
    printf ' "%s"' "$target_os"
  done
  printf ' ]\n'
}

cat > "$WORKSPACE_DIR/.gclient" <<EOF
solutions = [
  { "name": "$SOLUTION_NAME",
    "url":  "$FORK_URL",
    "deps_file": "DEPS",
    "managed": False,
    "custom_vars": {
      "checkout_configuration": "small",
      $( [[ "$TARGET_OS_LIST" == *"android"* ]] && echo '"checkout_android": True,' )
      $( [[ "$TARGET_OS_LIST" == *"ios"* ]] && echo '"checkout_ios": True,' )
    },
  },
]
EOF

if [[ -n "$TARGET_OS_LIST" ]]; then
  write_target_os >> "$WORKSPACE_DIR/.gclient"
fi

(
  cd "$SOURCE_DIR"
  gclient sync -r "$REF" --no-history --shallow
)
