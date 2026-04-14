#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_DIR="${NGINX_DIR:-${ROOT_DIR}/../nginx}"
NGINX_BIN="${NGINX_BIN:-${NGINX_DIR}/objs/nginx}"
RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ngx-wasm-ssl.XXXXXX")"
PORT="${TEST_NGINX_SSL_PORT:-$((20000 + RANDOM % 10000))}"

cleanup() {
    local pid=""

    if [ -f "${RUN_DIR}/logs/nginx.pid" ]; then
        pid="$(cat "${RUN_DIR}/logs/nginx.pid" 2>/dev/null || true)"
        if [ -n "${pid}" ]; then
            kill "${pid}" >/dev/null 2>&1 || true
            wait "${pid}" >/dev/null 2>&1 || true
        fi
    fi
    rm -rf "${RUN_DIR}"
}

trap cleanup EXIT

mkdir -p "${RUN_DIR}/conf" "${RUN_DIR}/logs" "${RUN_DIR}/html"

openssl req -x509 -newkey rsa:2048 \
    -keyout "${RUN_DIR}/default.local.key" \
    -out "${RUN_DIR}/default.local.crt" \
    -sha256 -days 3650 -nodes -subj "/CN=default.local" >/dev/null 2>&1

cat > "${RUN_DIR}/conf/nginx.conf" <<EOF
worker_processes 1;
master_process off;
daemon off;
error_log ${RUN_DIR}/logs/error.log notice;
pid ${RUN_DIR}/logs/nginx.pid;

events {
    worker_connections 16;
}

http {
    server {
        listen ${PORT} ssl;
        server_name _;

        ssl_certificate ${RUN_DIR}/default.local.crt;
        ssl_certificate_key ${RUN_DIR}/default.local.key;

        ssl_client_hello_by_wasm ${ROOT_DIR}/wasm/http-guests/build/ssl_reject_blocked.wasm on_ssl_client_hello;
        ssl_certificate_by_wasm ${ROOT_DIR}/wasm/http-guests/build/ssl_select_cert.wasm on_ssl_certificate;

        location / {
            return 200 "ok";
        }
    }
}
EOF

"${NGINX_BIN}" -p "${RUN_DIR}" -c "${RUN_DIR}/conf/nginx.conf" &

extract_subject() {
    local server_name="$1"
    local pem_file="${RUN_DIR}/${server_name}.pem"
    local attempt

    : > "${pem_file}"

    for attempt in $(seq 1 20); do
        openssl s_client -connect "127.0.0.1:${PORT}" \
            -servername "${server_name}" -showcerts </dev/null 2>/dev/null \
            | awk '
                /-----BEGIN CERTIFICATE-----/ { capture = 1 }
                capture { print }
                /-----END CERTIFICATE-----/ { exit }
            ' > "${pem_file}"

        if [ -s "${pem_file}" ]; then
            break
        fi

        sleep 0.1
    done

    openssl x509 -in "${pem_file}" -noout -subject
}

default_subject="$(extract_subject default.local)"
case "${default_subject}" in
    *"CN = default.local"*|*"CN=default.local"*) ;;
    *)
        echo "unexpected default certificate subject: ${default_subject}" >&2
        exit 1
        ;;
esac

selected_subject="$(extract_subject selected.local)"
case "${selected_subject}" in
    *"CN = selected.local"*|*"CN=selected.local"*) ;;
    *)
        echo "unexpected selected certificate subject: ${selected_subject}" >&2
        exit 1
        ;;
esac

set +e
openssl s_client -connect "127.0.0.1:${PORT}" -servername blocked.local \
    </dev/null > "${RUN_DIR}/blocked.out" 2>&1
blocked_rc=$?
set -e

if ! grep -Eiq 'alert|handshake failure|unrecognized name' "${RUN_DIR}/blocked.out"; then
    echo "blocked.local handshake was not rejected as expected" >&2
    cat "${RUN_DIR}/blocked.out" >&2
    exit 1
fi

echo "ssl hook test passed"
