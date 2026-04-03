#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NGINX_DIR="${NGINX_DIR:-${ROOT_DIR}/../nginx}"
PROVE_BIN="${PROVE:-prove}"
TEST_NGINX_PERL_LIB="${TEST_NGINX_PERL_LIB:-${ROOT_DIR}/third_party/test-nginx/lib}"
PERL_DEPS_LIB="${PERL_DEPS_LIB:-${ROOT_DIR}/third_party/perl5/lib/perl5}"
TEST_NGINX_BINARY="${TEST_NGINX_BINARY:-${NGINX_DIR}/objs/nginx}"
TEST_NGINX_RANDOMIZE="${TEST_NGINX_RANDOMIZE:-1}"
TEST_NGINX_NO_CLEAN="${TEST_NGINX_NO_CLEAN:-0}"
TEST_NGINX_PORT="${TEST_NGINX_PORT:-}"
TEST_NGINX_SERVER_PORT="${TEST_NGINX_SERVER_PORT:-}"
TEST_NGINX_CLIENT_PORT="${TEST_NGINX_CLIENT_PORT:-}"
TEST_NGINX_SERVROOT="${TEST_NGINX_SERVROOT:-}"
NGX_WASM_ROOT="${NGX_WASM_ROOT:-${ROOT_DIR}}"
NGINX_BUILD_INFO="${NGINX_BUILD_INFO:-${NGINX_DIR}/objs/ngx_wasm_build.env}"

if [ ! -f "${TEST_NGINX_PERL_LIB}/Test/Nginx/Socket.pm" ]; then
    echo "Test::Nginx not found under ${TEST_NGINX_PERL_LIB}" >&2
    echo "Run make deps or set TEST_NGINX_PERL_LIB=/path/to/test-nginx/lib" >&2
    exit 1
fi

if ! compgen -G "${ROOT_DIR}/t/*.t" >/dev/null; then
    echo "no test files found under ${ROOT_DIR}/t" >&2
    exit 1
fi

PERL5LIB_VALUE="${PERL_DEPS_LIB}:${TEST_NGINX_PERL_LIB}:${ROOT_DIR}/t/lib"
if [ -n "${PERL5LIB:-}" ]; then
    PERL5LIB_VALUE="${PERL5LIB_VALUE}:${PERL5LIB}"
fi

if [ -f "${NGINX_BUILD_INFO}" ]; then
    # shellcheck disable=SC1090
    . "${NGINX_BUILD_INFO}"
fi

if [ "${BUILD_SANITIZE:-0}" = "1" ]; then
    if [ -z "${ASAN_OPTIONS:-}" ]; then
        if [ "$(uname -s)" = "Linux" ]; then
            ASAN_OPTIONS="detect_leaks=1:abort_on_error=1"
        else
            ASAN_OPTIONS="detect_leaks=0:abort_on_error=1"
        fi
    fi

    if [ -z "${UBSAN_OPTIONS:-}" ]; then
        UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1"
    fi

    export ASAN_OPTIONS
    export UBSAN_OPTIONS
fi

export PERL5LIB="${PERL5LIB_VALUE}"
export NGX_WASM_ROOT
export TEST_NGINX_BINARY
export TEST_NGINX_RANDOMIZE
export TEST_NGINX_NO_CLEAN
export TEST_NGINX_PORT
export TEST_NGINX_SERVER_PORT
export TEST_NGINX_CLIENT_PORT
export TEST_NGINX_SERVROOT

test_status=0
while IFS= read -r test_file; do
    if ! "${PROVE_BIN}" -r "${test_file}"; then
        test_status=$?
        break
    fi
done < <(find "${ROOT_DIR}/t" -maxdepth 1 -type f -name '*.t' | sort)

if [ "${test_status}" != "0" ]; then
    if [ -n "${TEST_NGINX_SERVROOT}" ]; then
        servroot="${TEST_NGINX_SERVROOT}"
    else
        servroot="$(find "${ROOT_DIR}/t" -maxdepth 1 -type d -name 'servroot*' -print | sort | tail -n 1)"
    fi

    if [ -n "${servroot:-}" ] && [ -f "${servroot}/logs/error.log" ]; then
        printf '\nnginx error log: %s\n' "${servroot}/logs/error.log" >&2
        cat "${servroot}/logs/error.log" >&2
    fi

    exit "${test_status}"
fi

if [ "${TEST_NGINX_NO_CLEAN}" != "1" ]; then
    exit 0
fi

if [ -n "${TEST_NGINX_SERVROOT}" ]; then
    servroot="${TEST_NGINX_SERVROOT}"
else
    servroot="$(find "${ROOT_DIR}/t" -maxdepth 1 -type d -name 'servroot*' -print | sort | tail -n 1)"
fi

if [ -z "${servroot}" ] || [ ! -d "${servroot}" ]; then
    exit 0
fi

port="$(awk '/listen[[:space:]]+[0-9]+;/ { gsub(/;/, "", $2); print $2; exit }' "${servroot}/conf/nginx.conf" 2>/dev/null || true)"
pid="$(cat "${servroot}/logs/nginx.pid" 2>/dev/null || true)"
info_file="${servroot}/ngx_wasm_test_run.txt"

{
    printf 'servroot=%s\n' "${servroot}"
    printf 'port=%s\n' "${port}"
    printf 'pid=%s\n' "${pid}"
    printf 'nginx_bin=%s\n' "${TEST_NGINX_BINARY}"
} > "${info_file}"

printf 'left test harness running\n'
printf '  servroot: %s\n' "${servroot}"
printf '  port: %s\n' "${port}"
printf '  pid: %s\n' "${pid}"
printf '  metadata: %s\n' "${info_file}"
