#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
INSTALL_BASE="${ROOT_DIR}/third_party/perl5"

PERL_MODULES=(
    Test::Base
    Text::Diff
    Test::LongString
    IPC::Run
    List::MoreUtils
)

mkdir -p "${INSTALL_BASE}"

export PERL_MM_USE_DEFAULT=1
export PERL_MM_OPT="INSTALL_BASE=${INSTALL_BASE}"
export PERL_MB_OPT="--install_base ${INSTALL_BASE}"

if command -v cpanm >/dev/null 2>&1; then
    echo "Installing Perl test dependencies with cpanm into ${INSTALL_BASE}"
    cpanm --notest --local-lib-contained "${INSTALL_BASE}" "${PERL_MODULES[@]}"
    exit 0
fi

if command -v cpan >/dev/null 2>&1; then
    echo "Installing Perl test dependencies with cpan into ${INSTALL_BASE}"
    cpan -T "${PERL_MODULES[@]}"
    exit 0
fi

echo "error: neither cpanm nor cpan is available" >&2
echo "Install one of them, then rerun: make deps" >&2
exit 1
