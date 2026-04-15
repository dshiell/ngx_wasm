#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_DIR="${NGINX_DIR:-${ROOT_DIR}/../nginx}"
NGINX_BIN="${NGINX_BIN:-${NGINX_DIR}/objs/nginx}"
MODULE_PATH="${MODULE_PATH:-${ROOT_DIR}/wasm/http-guests/src/lifecycle_timers.wat}"
STATE_MODULE_PATH="${STATE_MODULE_PATH:-${ROOT_DIR}/wasm/http-guests/src/timer_state_get.wat}"
PORT="${PORT:-$((20000 + RANDOM % 10000))}"
HOST="${HOST:-127.0.0.1}"
PREFIX_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ngx-wasm-lifecycle.XXXXXX")"
CONF_PATH="${PREFIX_DIR}/nginx.conf"
LOG_DIR="${PREFIX_DIR}/logs"
PID_FILE="${LOG_DIR}/nginx.pid"

cleanup() {
    if [ -f "${PID_FILE}" ]; then
        "${NGINX_BIN}" -p "${PREFIX_DIR}" -c "${CONF_PATH}" -s quit >/dev/null 2>&1 || true
        sleep 1
    fi

    if [ -n "${NGINX_MASTER_PID:-}" ]; then
        wait "${NGINX_MASTER_PID}" 2>/dev/null || true
    fi

    rm -rf "${PREFIX_DIR}"
}

request_body() {
    curl -sf "http://${HOST}:${PORT}$1"
}

wait_for_contains() {
    local path="$1"
    local expected="$2"
    local attempt
    local body=""

    for attempt in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do
        if body="$(request_body "${path}" 2>/dev/null)"; then
            if printf '%s' "${body}" | grep -Fq "${expected}"; then
                return 0
            fi
        fi
        sleep 0.2
    done

    echo "timed out waiting for ${expected} on ${path}" >&2
    if [ -n "${body}" ]; then
        printf '%s\n' "${body}" >&2
    fi
    return 1
}

wait_for_exact() {
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

    echo "timed out waiting for exact body on ${path}" >&2
    if [ -n "${body}" ]; then
        printf '%s\n' "${body}" >&2
    fi
    return 1
}

require_log_line() {
    local pattern="$1"
    if ! grep -Fq "${pattern}" "${LOG_DIR}/error.log"; then
        echo "missing log line: ${pattern}" >&2
        tail -n 200 "${LOG_DIR}/error.log" >&2 || true
        return 1
    fi
}

trap cleanup EXIT

mkdir -p "${LOG_DIR}"

cat > "${CONF_PATH}" <<EOF
worker_processes 1;

error_log logs/error.log notice;
pid logs/nginx.pid;

events {
    worker_connections 64;
}

http {
    access_log off;
    wasm_shm_zone shared 1m;
    wasm_metrics_zone observability 1m;
    wasm_counter worker_starts_total;
    wasm_counter timer_ticks_total;
    wasm_gauge timer_state_gauge;
    wasm_counter timer_replaced_total;
    wasm_counter timer_original_total;

    init_by_wasm ${MODULE_PATH} on_init;
    init_worker_by_wasm ${MODULE_PATH} on_init_worker;
    exit_worker_by_wasm ${MODULE_PATH} on_exit_worker;

    server {
        listen ${PORT};
        server_name localhost;

        location /metrics {
            wasm_metrics;
        }

        location /state {
            content_by_wasm ${STATE_MODULE_PATH} on_content;
        }
    }
}
EOF

"${NGINX_BIN}" -t -p "${PREFIX_DIR}" -c "${CONF_PATH}" >/dev/null
"${NGINX_BIN}" -p "${PREFIX_DIR}" -c "${CONF_PATH}" -g 'daemon off;' &
NGINX_MASTER_PID=$!

wait_for_contains /metrics "worker_starts_total 1"
wait_for_contains /metrics "timer_ticks_total 3"
wait_for_contains /metrics "timer_state_gauge 3"
wait_for_contains /metrics "timer_replaced_total 1"
wait_for_contains /metrics "timer_original_total 0"
wait_for_exact /state "done"
require_log_line 'ngx_wasm guest: "init hook ran"'
require_log_line 'ngx_wasm_req_get_header not allowed in this phase'

"${NGINX_BIN}" -p "${PREFIX_DIR}" -c "${CONF_PATH}" -s reload >/dev/null
sleep 1

wait_for_contains /metrics "worker_starts_total 2"
wait_for_contains /metrics "timer_ticks_total 6"
wait_for_contains /metrics "timer_replaced_total 2"
wait_for_contains /metrics "timer_original_total 0"
wait_for_exact /state "done"

"${NGINX_BIN}" -p "${PREFIX_DIR}" -c "${CONF_PATH}" -s quit >/dev/null
sleep 1
require_log_line 'ngx_wasm guest: "exit hook ran"'

echo "lifecycle timer test passed"
