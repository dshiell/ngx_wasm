#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_DIR="${NGINX_DIR:-${ROOT_DIR}/../nginx}"
NGINX_BIN="${NGINX_BIN:-${NGINX_DIR}/objs/nginx}"
NGINX_BUILD_INFO="${NGINX_BUILD_INFO:-${NGINX_DIR}/objs/ngx_wasm_build.env}"
SET_MODULE="${SET_MODULE:-${ROOT_DIR}/wasm/http-guests/src/shm_set.wat}"
GET_MODULE="${GET_MODULE:-${ROOT_DIR}/wasm/http-guests/src/shm_get.wat}"
PORT="${PORT:-$((20000 + RANDOM % 10000))}"
HOST="${HOST:-127.0.0.1}"
PREFIX_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ngx-wasm-shm-reload.XXXXXX")"
CONF_PATH="${PREFIX_DIR}/nginx.conf"
LOG_DIR="${PREFIX_DIR}/logs"
PID_FILE="${LOG_DIR}/nginx.pid"

if [ -f "${NGINX_BUILD_INFO}" ]; then
    # shellcheck disable=SC1090
    . "${NGINX_BUILD_INFO}"
fi

sanitize_asan_options_for_oneshot() {
    local options="${ASAN_OPTIONS:-}"
    local part
    local filtered=""

    if [ -z "${options}" ]; then
        printf '%s\n' "detect_leaks=0"
        return 0
    fi

    IFS=':' read -r -a parts <<< "${options}"
    for part in "${parts[@]}"; do
        if [[ "${part}" == detect_leaks=* ]]; then
            continue
        fi
        if [ -n "${filtered}" ]; then
            filtered="${filtered}:${part}"
        else
            filtered="${part}"
        fi
    done

    if [ -n "${filtered}" ]; then
        filtered="${filtered}:detect_leaks=0"
    else
        filtered="detect_leaks=0"
    fi

    printf '%s\n' "${filtered}"
}

run_nginx_oneshot() {
    local oneshot_asan_options=""

    if [ "${BUILD_SANITIZE:-0}" = "1" ]; then
        oneshot_asan_options="$(sanitize_asan_options_for_oneshot)"
        ASAN_OPTIONS="${oneshot_asan_options}" \
        LSAN_OPTIONS="${LSAN_OPTIONS:-}" \
        UBSAN_OPTIONS="${UBSAN_OPTIONS:-}" \
        "${NGINX_BIN}" "$@"
        return $?
    fi

    "${NGINX_BIN}" "$@"
}

cleanup() {
    if [ -f "${PID_FILE}" ]; then
        run_nginx_oneshot -p "${PREFIX_DIR}" -c "${CONF_PATH}" -s quit >/dev/null 2>&1 || true
        sleep 1
    fi

    if [ -n "${NGINX_MASTER_PID:-}" ]; then
        wait "${NGINX_MASTER_PID}" 2>/dev/null || true
    fi

    rm -rf "${PREFIX_DIR}"
}

print_error_log() {
    if [ -f "${LOG_DIR}/error.log" ]; then
        echo "--- error log ---" >&2
        tail -n 200 "${LOG_DIR}/error.log" >&2 || true
    fi
}

wait_for_pid_file() {
    local attempt

    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        if [ -f "${PID_FILE}" ]; then
            return 0
        fi
        sleep 0.2
    done

    echo "nginx pid file was not created: ${PID_FILE}" >&2
    return 1
}

request_body() {
    curl -sf "http://${HOST}:${PORT}$1"
}

wait_for_body() {
    local path="$1"
    local expected="$2"
    local attempt
    local body=""

    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        if body="$(request_body "${path}" 2>/dev/null)"; then
            if [ "${body}" = "${expected}" ]; then
                return 0
            fi
        fi
        sleep 0.2
    done

    if [ -n "${body}" ]; then
        echo "unexpected response for ${path}: ${body}" >&2
    else
        echo "request never succeeded for ${path}" >&2
    fi
    return 1
}

trap cleanup EXIT

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
    wasm_shm_zone shared 1m;

    server {
        listen ${PORT};
        server_name localhost;

        location /set {
            content_by_wasm ${SET_MODULE} on_content;
        }

        location /get {
            content_by_wasm ${GET_MODULE} on_content;
        }
    }
}
EOF

run_nginx_oneshot -t -p "${PREFIX_DIR}" -c "${CONF_PATH}" >/dev/null || {
    print_error_log
    exit 1
}

"${NGINX_BIN}" -p "${PREFIX_DIR}" -c "${CONF_PATH}" -g 'daemon off;' &
NGINX_MASTER_PID=$!

wait_for_pid_file || {
    print_error_log
    exit 1
}

if ! wait_for_body /set stored; then
    echo "failed to store shared value before reload" >&2
    print_error_log
    exit 1
fi

if ! wait_for_body /get persisted-value; then
    echo "failed to read shared value before reload" >&2
    print_error_log
    exit 1
fi

run_nginx_oneshot -p "${PREFIX_DIR}" -c "${CONF_PATH}" -s reload >/dev/null || {
    print_error_log
    exit 1
}

if ! kill -0 "${NGINX_MASTER_PID}" 2>/dev/null; then
    echo "nginx master exited during reload" >&2
    print_error_log
    exit 1
fi

sleep 1

if ! wait_for_body /get persisted-value; then
    echo "shared value did not persist across reload" >&2
    print_error_log
    exit 1
fi

echo "shared memory reload test passed"
