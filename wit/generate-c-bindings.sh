#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${CARQUET_WIT_BINDINGS_OUT:-"${SOURCE_DIR}/Core/Api/.deps/carquet-bindings/c"}"

tool_from_path_or_cargo_home() {
  local tool="$1"
  local cargo_bin="${CARGO_HOME:-"${HOME}/.cargo"}/bin/${tool}"

  if command -v "${tool}" >/dev/null 2>&1; then
    command -v "${tool}"
  elif [[ -x "${cargo_bin}" ]]; then
    echo "${cargo_bin}"
  else
    return 1
  fi
}

if ! wit_bindgen="$(tool_from_path_or_cargo_home wit-bindgen)"; then
  echo "wit-bindgen not found; install with: cargo install wit-bindgen-cli" >&2
  exit 1
fi

rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}"
"${wit_bindgen}" c \
  --world carquet \
  --out-dir "${OUT_DIR}" \
  "${SCRIPT_DIR}"

echo "${OUT_DIR}"
