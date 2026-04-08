#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_DIR="${NGINX_DIR:-${ROOT_DIR}/../nginx}"
NGINX_BIN="${NGINX_BIN:-${NGINX_DIR}/objs/nginx}"
RUN_DIR="${RUN_DIR:-${ROOT_DIR}/run/loadtest}"
CONF_PATH="${RUN_DIR}/nginx.conf"
PID_FILE="${RUN_DIR}/logs/nginx.pid"

if [ ! -f "${PID_FILE}" ]; then
    echo "nginx loadtest instance is not running"
    exit 0
fi

if [ ! -x "${NGINX_BIN}" ]; then
    echo "nginx binary not found: ${NGINX_BIN}" >&2
    exit 1
fi

"${NGINX_BIN}" -p "${RUN_DIR}" -c "${CONF_PATH}" -s quit >/dev/null

for _ in 1 2 3 4 5 6 7 8 9 10; do
    if [ ! -f "${PID_FILE}" ]; then
        echo "nginx stopped"
        exit 0
    fi

    sleep 0.2
done

echo "nginx stop signal sent; pid file still present at ${PID_FILE}" >&2
exit 1
