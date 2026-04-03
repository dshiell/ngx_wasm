#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTY_DIR="${ROOT_DIR}/third_party"
REF="${TEST_NGINX_REF:-master}"
TARGET_DIR="${THIRD_PARTY_DIR}/test-nginx"
REF_FILE="${TARGET_DIR}/.ngx-wasm-ref"
ARCHIVE_NAME="test-nginx-${REF}.tar.gz"
ARCHIVE_URL="https://codeload.github.com/openresty/test-nginx/tar.gz/${REF}"
ARCHIVE_PATH="${THIRD_PARTY_DIR}/${ARCHIVE_NAME}"
EXTRACT_PARENT="$(mktemp -d "${THIRD_PARTY_DIR}/test-nginx-extract.XXXXXX")"

cleanup() {
    rm -rf "${EXTRACT_PARENT}"
}

trap cleanup EXIT

mkdir -p "${THIRD_PARTY_DIR}"

if [ -d "${TARGET_DIR}" ] && [ "${FORCE:-0}" != "1" ]; then
    if [ -f "${REF_FILE}" ] && [ "$(cat "${REF_FILE}")" = "${REF}" ]; then
        echo "test-nginx already present at ${TARGET_DIR} for ref ${REF}"
        exit 0
    fi

    echo "test-nginx already present at ${TARGET_DIR}, but ref does not match ${REF}"
    echo "Refreshing local checkout."
    rm -rf "${TARGET_DIR}"
fi

if [ "${FORCE:-0}" = "1" ]; then
    echo "Refreshing test-nginx at ${TARGET_DIR}"
    rm -rf "${TARGET_DIR}"
fi

echo "Fetching ${ARCHIVE_URL}"

if command -v curl >/dev/null 2>&1; then
    curl -fL "${ARCHIVE_URL}" -o "${ARCHIVE_PATH}"
elif command -v wget >/dev/null 2>&1; then
    wget -O "${ARCHIVE_PATH}" "${ARCHIVE_URL}"
else
    echo "curl or wget is required to fetch test-nginx" >&2
    exit 1
fi

tar -xzf "${ARCHIVE_PATH}" -C "${EXTRACT_PARENT}"

EXTRACTED_DIR="$(find "${EXTRACT_PARENT}" -mindepth 1 -maxdepth 1 -type d | head -1)"

if [ -z "${EXTRACTED_DIR}" ] || [ ! -d "${EXTRACTED_DIR}/lib/Test/Nginx" ]; then
    echo "unexpected archive layout: ${ARCHIVE_NAME}" >&2
    exit 1
fi

mv "${EXTRACTED_DIR}" "${TARGET_DIR}"
printf '%s\n' "${REF}" > "${REF_FILE}"
rm -f "${ARCHIVE_PATH}"

echo "Installed test-nginx to ${TARGET_DIR}"
echo "Pinned ref: ${REF}"
