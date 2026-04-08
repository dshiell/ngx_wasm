#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${PORT:-18081}"
HOST="${HOST:-127.0.0.1}"
RUN_DIR="${RUN_DIR:-${ROOT_DIR}/run/bench-ab}"
OUT_DIR="${OUT_DIR:-${RUN_DIR}/benchmarks}"
REQUESTS="${REQUESTS:-100000}"
CONCURRENCIES="${CONCURRENCIES:-50 200 500 1000}"
ENDPOINTS="${ENDPOINTS:-hello health}"
KEEPALIVE="${KEEPALIVE:-1}"
START_SCRIPT="${ROOT_DIR}/scripts/start-loadtest.sh"
STOP_SCRIPT="${ROOT_DIR}/scripts/stop-loadtest.sh"
TIMESTAMP="$(date +%Y%m%d-%H%M%S)"
RESULT_DIR="${OUT_DIR}/${TIMESTAMP}"
SUMMARY_FILE="${RESULT_DIR}/summary.tsv"

require_command() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "required command not found: $1" >&2
        exit 1
    fi
}

cleanup() {
    RUN_DIR="${RUN_DIR}" "${STOP_SCRIPT}" >/dev/null 2>&1 || true
}

parse_ab_field() {
    local label="$1"
    local file="$2"

    awk -F: -v key="${label}" '$1 == key { gsub(/^[ \t]+/, "", $2); print $2; exit }' "${file}"
}

parse_ab_rps() {
    local file="$1"

    awk '/^Requests per second:/ { print $4; exit }' "${file}"
}

parse_ab_mean_ms() {
    local file="$1"

    awk '/^Time per request:/ { print $4; exit }' "${file}"
}

parse_ab_transfer_rate() {
    local file="$1"

    awk '/^Transfer rate:/ { print $3 " " $4; exit }' "${file}"
}

parse_ab_non2xx() {
    local file="$1"

    awk '/^Non-2xx responses:/ { print $3; exit }' "${file}"
}

require_command ab
require_command awk
require_command curl

mkdir -p "${RESULT_DIR}"
trap cleanup EXIT

PORT="${PORT}" HOST="${HOST}" RUN_DIR="${RUN_DIR}" "${START_SCRIPT}"

printf 'endpoint\tconcurrency\trequests\trps\tmean_ms\ttransfer_rate\tfailed\tnon_2xx\traw_output\n' > "${SUMMARY_FILE}"

for endpoint in ${ENDPOINTS}; do
    url="http://${HOST}:${PORT}/${endpoint}"

    if ! curl -sf "${url}" >/dev/null; then
        echo "failed to warm endpoint: ${url}" >&2
        exit 1
    fi

    for concurrency in ${CONCURRENCIES}; do
        output_file="${RESULT_DIR}/${endpoint}-c${concurrency}.txt"
        ab_args=("-n" "${REQUESTS}" "-c" "${concurrency}")

        if [ "${KEEPALIVE}" = "1" ]; then
            ab_args+=("-k")
        fi

        echo "benchmarking ${endpoint} with concurrency ${concurrency}"
        ab "${ab_args[@]}" "${url}" | tee "${output_file}"

        failed="$(parse_ab_field "Failed requests" "${output_file}")"
        non_2xx="$(parse_ab_non2xx "${output_file}")"
        if [ -z "${non_2xx}" ]; then
            non_2xx="0"
        fi

        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "${endpoint}" \
            "${concurrency}" \
            "${REQUESTS}" \
            "$(parse_ab_rps "${output_file}")" \
            "$(parse_ab_mean_ms "${output_file}")" \
            "$(parse_ab_transfer_rate "${output_file}")" \
            "${failed}" \
            "${non_2xx}" \
            "${output_file}" >> "${SUMMARY_FILE}"
    done
done

echo
echo "benchmark summary"
column -t -s $'\t' "${SUMMARY_FILE}"
echo
echo "raw outputs: ${RESULT_DIR}"
