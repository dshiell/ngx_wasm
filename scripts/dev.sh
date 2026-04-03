#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Installing ngx_wasm development dependencies"

"${ROOT_DIR}/scripts/fetch-wasmtime.sh"
"${ROOT_DIR}/scripts/fetch-test-nginx.sh"
"${ROOT_DIR}/scripts/install-rust.sh"
"${ROOT_DIR}/scripts/install-perl-deps.sh"

echo "Done"
