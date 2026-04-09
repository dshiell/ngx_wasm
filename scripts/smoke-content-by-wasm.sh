#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_DIR="${NGINX_DIR:-${ROOT_DIR}/../nginx}"
NGINX_BIN="${NGINX_BIN:-${NGINX_DIR}/objs/nginx}"
WASM_MODULE="${WASM_MODULE:-${ROOT_DIR}/wasm/http-guests/build/hello_world.wasm}"
PORT="${PORT:-$((20000 + RANDOM % 10000))}"
HOST="${HOST:-127.0.0.1}"
EXPECTED_BODY="${EXPECTED_BODY:-hello from guest wasm}"
PREFIX_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ngx-wasm-smoke.XXXXXX")"
CONF_PATH="${PREFIX_DIR}/nginx.conf"
LOG_DIR="${PREFIX_DIR}/logs"
PID_FILE="${LOG_DIR}/nginx.pid"

cleanup() {
    if [ -f "${PID_FILE}" ]; then
        "${NGINX_BIN}" -p "${PREFIX_DIR}" -c "${CONF_PATH}" -s quit >/dev/null 2>&1 || true
        sleep 1
    fi

    if [ -n "${NGINX_PID:-}" ]; then
        wait "${NGINX_PID}" 2>/dev/null || true
    fi

    rm -rf "${PREFIX_DIR}"
}

trap cleanup EXIT

if [ ! -x "${NGINX_BIN}" ]; then
    echo "nginx binary not found: ${NGINX_BIN}" >&2
    exit 1
fi

if [ ! -f "${WASM_MODULE}" ]; then
    echo "wasm module not found: ${WASM_MODULE}" >&2
    echo "build it first with: make wasm" >&2
    exit 1
fi

mkdir -p "${LOG_DIR}"

cat > "${CONF_PATH}" <<EOF
worker_processes  1;

error_log  logs/error.log info;
pid        logs/nginx.pid;

events {
    worker_connections  64;
}

http {
    access_log off;

    server {
        listen ${PORT};
        server_name localhost;

        location /wasm {
            content_by_wasm ${WASM_MODULE} on_content;
        }
    }
}
EOF

"${NGINX_BIN}" -t -p "${PREFIX_DIR}" -c "${CONF_PATH}" >/dev/null
"${NGINX_BIN}" -p "${PREFIX_DIR}" -c "${CONF_PATH}" -g 'daemon off; master_process off;' &
NGINX_PID=$!

for _ in 1 2 3 4 5 6 7 8 9 10; do
    if ! kill -0 "${NGINX_PID}" 2>/dev/null; then
        echo "nginx exited before the smoke request completed" >&2
        if [ -f "${LOG_DIR}/error.log" ]; then
            echo "--- error log ---" >&2
            tail -n 100 "${LOG_DIR}/error.log" >&2 || true
        fi
        exit 1
    fi

    if RESPONSE="$(curl -sf "http://${HOST}:${PORT}/wasm" 2>/dev/null)"; then
        if [ "${RESPONSE}" != "${EXPECTED_BODY}" ]; then
            echo "unexpected response body: ${RESPONSE}" >&2
            exit 1
        fi

        echo "smoke test passed"
        exit 0
    fi

    sleep 0.2
done

echo "failed to fetch http://${HOST}:${PORT}/wasm" >&2
if [ -f "${LOG_DIR}/error.log" ]; then
    echo "--- error log ---" >&2
    tail -n 100 "${LOG_DIR}/error.log" >&2 || true
fi
exit 1
