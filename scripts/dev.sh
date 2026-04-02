#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "Installing ngx_wasm development dependencies"

"${ROOT_DIR}/scripts/fetch-wasmtime.sh"
"${ROOT_DIR}/scripts/fetch-test-nginx.sh"

echo "Done"
