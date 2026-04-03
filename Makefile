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
TEST_NGINX_RANDOMIZE ?= 1
TEST_NGINX_NO_CLEAN ?= 0
TEST_NGINX_PORT ?=
TEST_NGINX_SERVER_PORT ?=
TEST_NGINX_CLIENT_PORT ?=
TEST_NGINX_SERVROOT ?=
NGINX_DIR ?= $(abspath ../nginx)
NGINX_BIN ?= $(NGINX_DIR)/objs/nginx
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

FORMAT_FILES = $(sort $(wildcard src/*.c include/*.h))
HELLO_WORLD_DIR = wasm/hello-world
FAILURES_DIR = wasm/failures

.PHONY: format check-format wasm deps smoke test clean

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
	$(MAKE) -C $(HELLO_WORLD_DIR) RUSTC="$(RUSTC)" RUSTUP="$(RUSTUP)" WASM_TARGET="$(WASM_TARGET)" build
	$(MAKE) -C $(FAILURES_DIR) RUSTC="$(RUSTC)" RUSTUP="$(RUSTUP)" WASM_TARGET="$(WASM_TARGET)" build

deps:
	WASMTIME_VERSION=$(WASMTIME_VERSION) TEST_NGINX_REF=$(TEST_NGINX_REF) ./scripts/dev.sh

smoke:
	NGINX_DIR="$(NGINX_DIR)" NGINX_BIN="$(NGINX_BIN)" ./scripts/smoke-content-by-wasm.sh

test: wasm
ifeq ($(wildcard $(TEST_NGINX_PERL_LIB)/Test/Nginx/Socket.pm),)
	$(error Test::Nginx not found under $(TEST_NGINX_PERL_LIB); run `make deps` or set TEST_NGINX_PERL_LIB=/path/to/test-nginx/lib)
endif
	PERL5LIB=$(PERL_DEPS_LIB):$(TEST_NGINX_PERL_LIB):t/lib$(if $(PERL5LIB),:$(PERL5LIB)) NGX_WASM_ROOT=$(PWD) TEST_NGINX_BINARY=$(NGINX_BIN) TEST_NGINX_RANDOMIZE=$(TEST_NGINX_RANDOMIZE) TEST_NGINX_NO_CLEAN=$(TEST_NGINX_NO_CLEAN) TEST_NGINX_PORT="$(TEST_NGINX_PORT)" TEST_NGINX_SERVER_PORT="$(TEST_NGINX_SERVER_PORT)" TEST_NGINX_CLIENT_PORT="$(TEST_NGINX_CLIENT_PORT)" TEST_NGINX_SERVROOT="$(TEST_NGINX_SERVROOT)" $(PROVE) -r t/001-content-basic.t
	PERL5LIB=$(PERL_DEPS_LIB):$(TEST_NGINX_PERL_LIB):t/lib$(if $(PERL5LIB),:$(PERL5LIB)) NGX_WASM_ROOT=$(PWD) TEST_NGINX_BINARY=$(NGINX_BIN) TEST_NGINX_RANDOMIZE=$(TEST_NGINX_RANDOMIZE) TEST_NGINX_NO_CLEAN=$(TEST_NGINX_NO_CLEAN) TEST_NGINX_PORT="$(TEST_NGINX_PORT)" TEST_NGINX_SERVER_PORT="$(TEST_NGINX_SERVER_PORT)" TEST_NGINX_CLIENT_PORT="$(TEST_NGINX_CLIENT_PORT)" TEST_NGINX_SERVROOT="$(TEST_NGINX_SERVROOT)" $(PROVE) -r t/002-content-failures.t
	@if [ "$(TEST_NGINX_NO_CLEAN)" = "1" ]; then \
		if [ -n "$(TEST_NGINX_SERVROOT)" ]; then \
			servroot="$(TEST_NGINX_SERVROOT)"; \
		else \
			servroot=$$(find "$(PWD)/t" -maxdepth 1 -type d -name 'servroot*' -print | sort | tail -n 1); \
		fi; \
		if [ -n "$$servroot" ] && [ -d "$$servroot" ]; then \
			port=$$(awk '/listen[[:space:]]+[0-9]+;/ { gsub(/;/, "", $$2); print $$2; exit }' "$$servroot/conf/nginx.conf" 2>/dev/null); \
			pid=$$(cat "$$servroot/logs/nginx.pid" 2>/dev/null || true); \
			info_file="$$servroot/ngx_wasm_test_run.txt"; \
			{ \
				printf 'servroot=%s\n' "$$servroot"; \
				printf 'port=%s\n' "$$port"; \
				printf 'pid=%s\n' "$$pid"; \
				printf 'nginx_bin=%s\n' "$(NGINX_BIN)"; \
			} > "$$info_file"; \
			printf 'left test harness running\n'; \
			printf '  servroot: %s\n' "$$servroot"; \
			printf '  port: %s\n' "$$port"; \
			printf '  pid: %s\n' "$$pid"; \
			printf '  metadata: %s\n' "$$info_file"; \
		fi; \
	fi

clean:
	$(MAKE) -C $(HELLO_WORLD_DIR) clean
	$(MAKE) -C $(FAILURES_DIR) clean
	rm -rf t/servroot_*
