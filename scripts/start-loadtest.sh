#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_DIR="${NGINX_DIR:-${ROOT_DIR}/../nginx}"
NGINX_BIN="${NGINX_BIN:-${NGINX_DIR}/objs/nginx}"
PORT="${PORT:-18080}"
HOST="${HOST:-127.0.0.1}"
RUN_DIR="${RUN_DIR:-${ROOT_DIR}/run/loadtest}"
CONF_PATH="${RUN_DIR}/nginx.conf"
LOG_DIR="${RUN_DIR}/logs"
PID_FILE="${LOG_DIR}/nginx.pid"
HELLO_MODULE="${HELLO_MODULE:-${ROOT_DIR}/wasm/http-guests/build/hello_world.wasm}"
HEALTH_MODULE="${HEALTH_MODULE:-${ROOT_DIR}/wasm/http-guests/build/health.wasm}"

if [ ! -x "${NGINX_BIN}" ]; then
    echo "nginx binary not found: ${NGINX_BIN}" >&2
    echo "build it first with: make build" >&2
    exit 1
fi

if [ ! -f "${HELLO_MODULE}" ]; then
    echo "hello wasm module not found: ${HELLO_MODULE}" >&2
    echo "build it first with: make wasm" >&2
    exit 1
fi

if [ ! -f "${HEALTH_MODULE}" ]; then
    echo "health wasm module not found: ${HEALTH_MODULE}" >&2
    echo "build it first with: make wasm" >&2
    exit 1
fi

mkdir -p "${LOG_DIR}"

if [ -f "${PID_FILE}" ]; then
    if kill -0 "$(cat "${PID_FILE}")" 2>/dev/null; then
        echo "nginx loadtest instance is already running" >&2
        echo "pid: $(cat "${PID_FILE}")" >&2
        echo "prefix: ${RUN_DIR}" >&2
        exit 0
    fi

    rm -f "${PID_FILE}"
fi

cat > "${CONF_PATH}" <<EOF
worker_processes  1;

error_log  logs/error.log crit;
pid        logs/nginx.pid;

events {
    worker_connections  4096;
}

http {
    access_log off;
    keepalive_timeout 65;

    server {
        listen ${HOST}:${PORT};
        server_name localhost;

        location = /hello {
            content_by_wasm ${HELLO_MODULE} on_content;
        }

        location = /health {
            content_by_wasm ${HEALTH_MODULE} on_content;
        }
    }
}
EOF

"${NGINX_BIN}" -t -p "${RUN_DIR}" -c "${CONF_PATH}" >/dev/null
"${NGINX_BIN}" -p "${RUN_DIR}" -c "${CONF_PATH}"

echo "nginx started"
echo "hello:  http://${HOST}:${PORT}/hello"
echo "health: http://${HOST}:${PORT}/health"
echo "pid:    $(cat "${PID_FILE}")"
echo "prefix: ${RUN_DIR}"
