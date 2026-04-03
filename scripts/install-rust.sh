#!/usr/bin/env bash

set -euo pipefail

DEFAULT_RUSTUP_HOME="${HOME}/.rustup"
DEFAULT_CARGO_HOME="${HOME}/.cargo"

RUSTUP_HOME="${RUSTUP_HOME:-${DEFAULT_RUSTUP_HOME}}"
CARGO_HOME="${CARGO_HOME:-${DEFAULT_CARGO_HOME}}"
CARGO_BIN_DIR="${CARGO_HOME}/bin"
RUSTUP_BIN="${CARGO_BIN_DIR}/rustup"
TARGET="wasm32-unknown-unknown"

export RUSTUP_HOME
export CARGO_HOME
export PATH="${CARGO_BIN_DIR}:${PATH}"

install_rustup() {
    echo "Installing Rust toolchain with rustup into ${CARGO_HOME}"

    if ! command -v curl >/dev/null 2>&1; then
        echo "error: curl is required to install rustup" >&2
        exit 1
    fi

    curl --proto '=https' --tlsv1.2 -fsSL https://sh.rustup.rs | \
        sh -s -- -y --profile minimal
}

if [ ! -x "${RUSTUP_BIN}" ] && ! command -v rustup >/dev/null 2>&1; then
    install_rustup
fi

if [ ! -x "${RUSTUP_BIN}" ]; then
    RUSTUP_BIN="$(command -v rustup)"
fi

if [ ! -x "${RUSTUP_BIN}" ]; then
    echo "error: rustup is not available after installation" >&2
    exit 1
fi

echo "Ensuring Rust stable toolchain is installed"
"${RUSTUP_BIN}" toolchain install stable --profile minimal

echo "Ensuring Rust target ${TARGET} is installed"
"${RUSTUP_BIN}" target add "${TARGET}" --toolchain stable

echo "Rust toolchain ready"
