CLANG_FORMAT ?= $(shell \
	if command -v clang-format >/dev/null 2>&1; then \
		command -v clang-format; \
	elif command -v xcrun >/dev/null 2>&1; then \
		xcrun -f clang-format 2>/dev/null || true; \
	fi)
WASMTIME_VERSION ?= 36.0.3
TEST_NGINX_REF ?= master
PROVE ?= prove
TEST_NGINX_PERL_LIB ?= $(PWD)/third_party/test-nginx/lib
PERL_DEPS_LIB ?= $(PWD)/third_party/perl5/lib/perl5
TEST_NGINX_RANDOMIZE ?= 0
TEST_NGINX_NO_CLEAN ?= 0
TEST_NGINX_PORT ?=
TEST_NGINX_SERVER_PORT ?=
TEST_NGINX_CLIENT_PORT ?=
TEST_NGINX_SERVROOT ?=
NGINX_DIR ?= $(abspath ../nginx)
NGINX_BIN ?= $(NGINX_DIR)/objs/nginx
NGINX_BUILD_INFO ?= $(NGINX_DIR)/objs/ngx_wasm_build.env
NGINX_BUILD_JOBS ?= 2
RUSTC ?= $(shell \
	if [ -x "$(HOME)/.cargo/bin/rustc" ]; then \
		echo "$(HOME)/.cargo/bin/rustc"; \
	elif command -v rustc >/dev/null 2>&1; then \
		command -v rustc; \
	fi)
RUSTUP ?= $(shell \
	if [ -x "$(HOME)/.cargo/bin/rustup" ]; then \
		echo "$(HOME)/.cargo/bin/rustup"; \
	elif command -v rustup >/dev/null 2>&1; then \
		command -v rustup; \
	fi)
WASM_TARGET ?= wasm32-unknown-unknown
BUILD_SANITIZE ?= 0
SANITIZER_CC ?= clang
SANITIZER_IGNORELIST ?= $(CURDIR)/sanitizers/ubsan.ignorelist
SANITIZER_COMMON_FLAGS ?= -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
SANITIZER_CC_FLAGS ?= $(SANITIZER_COMMON_FLAGS) -fno-sanitize=nonnull-attribute -fsanitize-ignorelist=$(SANITIZER_IGNORELIST)
SANITIZER_LD_FLAGS ?= -fsanitize=address,undefined
UNAME_S := $(shell uname -s)
LSAN_SUPPRESSIONS ?= $(CURDIR)/sanitizers/lsan.supp
ifeq ($(UNAME_S),Linux)
ASAN_OPTIONS ?= detect_leaks=1:abort_on_error=1
LSAN_OPTIONS ?= suppressions=$(LSAN_SUPPRESSIONS):print_suppressions=0
else
ASAN_OPTIONS ?= detect_leaks=0:abort_on_error=1
LSAN_OPTIONS ?=
endif
UBSAN_OPTIONS ?= print_stacktrace=1:halt_on_error=1

FORMAT_FILES = $(sort $(wildcard src/*.c include/*.h))
HTTP_GUESTS_DIR = wasm/http-guests
FAILURES_DIR = wasm/failures
LOADTEST_RUN_DIR ?= $(CURDIR)/run/loadtest
LOADTEST_PORT ?= 18080
LOADTEST_HOST ?= 127.0.0.1
BENCH_AB_RUN_DIR ?= $(CURDIR)/run/bench-ab
BENCH_AB_PORT ?= 18081
BENCH_AB_REQUESTS ?= 100000
BENCH_AB_CONCURRENCIES ?= 50 200 500 1000
BENCH_AB_ENDPOINTS ?= hello health
BENCH_AB_KEEPALIVE ?= 1
BENCH_AB_OUTPUT_DIR ?= $(BENCH_AB_RUN_DIR)/benchmarks

ifeq ($(BUILD_SANITIZE),1)
NGINX_CONFIGURE_ARGS = \
	--with-cc="$(SANITIZER_CC)" \
	--with-cc-opt='$(SANITIZER_CC_FLAGS)' \
	--with-ld-opt='$(SANITIZER_LD_FLAGS)' \
	--add-module="$(CURDIR)"
else
NGINX_CONFIGURE_ARGS = --with-threads --add-module="$(CURDIR)"
endif

.PHONY: format check-format wasm deps nginx-build build start stop bench-ab smoke test test-reload clean

format:
ifeq ($(strip $(CLANG_FORMAT)),)
	$(error clang-format not found in PATH; set CLANG_FORMAT=/path/to/clang-format)
endif
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

check-format:
ifeq ($(strip $(CLANG_FORMAT)),)
	$(error clang-format not found in PATH; set CLANG_FORMAT=/path/to/clang-format)
endif
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

wasm:
	$(MAKE) -C $(HTTP_GUESTS_DIR) RUSTC="$(RUSTC)" RUSTUP="$(RUSTUP)" WASM_TARGET="$(WASM_TARGET)" build
	$(MAKE) -C $(FAILURES_DIR) RUSTC="$(RUSTC)" RUSTUP="$(RUSTUP)" WASM_TARGET="$(WASM_TARGET)" build

deps:
	WASMTIME_VERSION=$(WASMTIME_VERSION) TEST_NGINX_REF=$(TEST_NGINX_REF) ./scripts/dev.sh

nginx-build:
	cd "$(NGINX_DIR)" && \
		auto/configure $(NGINX_CONFIGURE_ARGS)
	$(MAKE) -C "$(NGINX_DIR)" -j"$(NGINX_BUILD_JOBS)"
	@mkdir -p "$(dir $(NGINX_BUILD_INFO))"
	@{ \
		printf 'BUILD_SANITIZE=%s\n' "$(BUILD_SANITIZE)"; \
		printf 'ASAN_OPTIONS=%s\n' "$(ASAN_OPTIONS)"; \
		printf 'LSAN_OPTIONS=%s\n' "$(LSAN_OPTIONS)"; \
		printf 'UBSAN_OPTIONS=%s\n' "$(UBSAN_OPTIONS)"; \
	} > "$(NGINX_BUILD_INFO)"

build: wasm nginx-build

start: wasm
	PORT="$(LOADTEST_PORT)" HOST="$(LOADTEST_HOST)" RUN_DIR="$(LOADTEST_RUN_DIR)" \
	NGINX_DIR="$(NGINX_DIR)" NGINX_BIN="$(NGINX_BIN)" ./scripts/start-loadtest.sh

stop:
	RUN_DIR="$(LOADTEST_RUN_DIR)" NGINX_DIR="$(NGINX_DIR)" NGINX_BIN="$(NGINX_BIN)" \
	./scripts/stop-loadtest.sh

bench-ab:
	PORT="$(BENCH_AB_PORT)" HOST="$(LOADTEST_HOST)" RUN_DIR="$(BENCH_AB_RUN_DIR)" \
	OUT_DIR="$(BENCH_AB_OUTPUT_DIR)" REQUESTS="$(BENCH_AB_REQUESTS)" \
	CONCURRENCIES="$(BENCH_AB_CONCURRENCIES)" ENDPOINTS="$(BENCH_AB_ENDPOINTS)" \
	KEEPALIVE="$(BENCH_AB_KEEPALIVE)" NGINX_DIR="$(NGINX_DIR)" NGINX_BIN="$(NGINX_BIN)" \
	./scripts/bench-ab.sh

smoke:
	NGINX_DIR="$(NGINX_DIR)" NGINX_BIN="$(NGINX_BIN)" ./scripts/smoke-content-by-wasm.sh

test: build
	NGINX_DIR="$(NGINX_DIR)" \
	./scripts/run-tests.sh
	NGINX_DIR="$(NGINX_DIR)" NGINX_BIN="$(NGINX_BIN)" ./scripts/test-reload-content-by-wasm.sh

test-reload: build
	NGINX_DIR="$(NGINX_DIR)" NGINX_BIN="$(NGINX_BIN)" ./scripts/test-reload-content-by-wasm.sh

clean:
	$(MAKE) -C $(HTTP_GUESTS_DIR) clean
	$(MAKE) -C $(FAILURES_DIR) clean
	rm -f "$(NGINX_BUILD_INFO)"
	rm -rf t/servroot* run
