#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_DIR="${ROOT_DIR}/third_party"
VERSION="${WASMTIME_VERSION:-36.0.3}"
TARGET_DIR="${THIRD_PARTY_DIR}/wasmtime-${VERSION}"

case "$(uname -s)" in
Darwin)
    PLATFORM="macos"
    ;;
Linux)
    PLATFORM="linux"
    ;;
*)
    echo "unsupported platform: $(uname -s)" >&2
    echo "supported platforms: Darwin, Linux" >&2
    exit 1
    ;;
esac

case "$(uname -m)" in
arm64 | aarch64)
    ARCH="aarch64"
    ;;
x86_64 | amd64)
    ARCH="x86_64"
    ;;
*)
    echo "unsupported architecture: $(uname -m)" >&2
    echo "supported architectures: aarch64/arm64, x86_64/amd64" >&2
    exit 1
    ;;
esac

ARCHIVE_BASENAME="wasmtime-v${VERSION}-${ARCH}-${PLATFORM}-c-api"
ARCHIVE_NAME="${ARCHIVE_BASENAME}.tar.xz"
ARCHIVE_URL="https://github.com/bytecodealliance/wasmtime/releases/download/v${VERSION}/${ARCHIVE_NAME}"
ARCHIVE_PATH="${THIRD_PARTY_DIR}/${ARCHIVE_NAME}"

mkdir -p "${THIRD_PARTY_DIR}"

EXTRACT_PARENT="$(mktemp -d "${THIRD_PARTY_DIR}/wasmtime-extract.XXXXXX")"
EXTRACTED_DIR="${EXTRACT_PARENT}/${ARCHIVE_BASENAME}"

cleanup() {
    rm -rf "${EXTRACT_PARENT}"
}

trap cleanup EXIT

if [ -d "${TARGET_DIR}" ] && [ "${FORCE:-0}" != "1" ]; then
    echo "Wasmtime C API already present at ${TARGET_DIR}"
    echo "Remove it first or rerun with FORCE=1 to replace it."
    exit 0
fi

if [ "${FORCE:-0}" = "1" ]; then
    rm -rf "${TARGET_DIR}"
fi

echo "Fetching ${ARCHIVE_URL}"

if command -v curl >/dev/null 2>&1; then
    curl -fL "${ARCHIVE_URL}" -o "${ARCHIVE_PATH}"
elif command -v wget >/dev/null 2>&1; then
    wget -O "${ARCHIVE_PATH}" "${ARCHIVE_URL}"
else
    echo "curl or wget is required to fetch Wasmtime" >&2
    exit 1
fi

tar -xJf "${ARCHIVE_PATH}" -C "${EXTRACT_PARENT}"

if [ ! -d "${EXTRACTED_DIR}" ]; then
    echo "unexpected archive layout: ${ARCHIVE_NAME}" >&2
    exit 1
fi

mv "${EXTRACTED_DIR}" "${TARGET_DIR}"
rm -f "${ARCHIVE_PATH}"

echo "Installed Wasmtime C API to ${TARGET_DIR}"
