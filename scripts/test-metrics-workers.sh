#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_DIR="${NGINX_DIR:-${ROOT_DIR}/../nginx}"
NGINX_BIN="${NGINX_BIN:-${NGINX_DIR}/objs/nginx}"
METRIC_MODULE="${METRIC_MODULE:-${ROOT_DIR}/wasm/http-guests/src/metric_counter_inc.wat}"
PORT="${PORT:-$((20000 + RANDOM % 10000))}"
HOST="${HOST:-127.0.0.1}"
REQUESTS="${REQUESTS:-20}"
PARALLELISM="${PARALLELISM:-8}"
PREFIX_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ngx-wasm-metrics-workers.XXXXXX")"
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

wait_until_ready() {
    local attempt

    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        if request_body /metrics >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.2
    done

    return 1
}

expected_metrics() {
    printf '# TYPE requests_total counter\nrequests_total %s' "${REQUESTS}"
}

trap cleanup EXIT

mkdir -p "${LOG_DIR}"

cat > "${CONF_PATH}" <<EOF
worker_processes 2;

error_log logs/error.log info;
pid logs/nginx.pid;

events {
    worker_connections 64;
}

http {
    access_log off;
    wasm_metrics_zone observability 1m;
    wasm_counter requests_total;

    server {
        listen ${PORT};
        server_name localhost;

        location /inc {
            content_by_wasm ${METRIC_MODULE} on_content;
        }

        location /metrics {
            wasm_metrics;
        }
    }
}
EOF

"${NGINX_BIN}" -t -p "${PREFIX_DIR}" -c "${CONF_PATH}" >/dev/null
"${NGINX_BIN}" -p "${PREFIX_DIR}" -c "${CONF_PATH}" -g 'daemon off;' &
NGINX_MASTER_PID=$!

wait_until_ready

seq "${REQUESTS}" | xargs -n1 -P "${PARALLELISM}" -I{} \
    curl -sf "http://${HOST}:${PORT}/inc" >/dev/null

if [ "$(request_body /metrics)" != "$(expected_metrics)" ]; then
    echo "shared metrics total mismatch across workers" >&2
    exit 1
fi

echo "metrics worker test passed"
