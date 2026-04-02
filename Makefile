CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null)
WASMTIME_VERSION ?= 36.0.3
TEST_NGINX_REF ?= master
PROVE ?= prove
TEST_NGINX_PERL_LIB ?= $(PWD)/third_party/test-nginx/lib

FORMAT_FILES = src/ngx_http_wasm_module.c
HELLO_WORLD_DIR = wasm/hello-world
FAILURES_DIR = wasm/failures

.PHONY: format check-format wasm deps smoke test clean

format:
ifndef CLANG_FORMAT
	$(error clang-format not found in PATH; set CLANG_FORMAT=/path/to/clang-format)
endif
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

check-format:
ifndef CLANG_FORMAT
	$(error clang-format not found in PATH; set CLANG_FORMAT=/path/to/clang-format)
endif
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

wasm:
	$(MAKE) -C $(HELLO_WORLD_DIR) build
	$(MAKE) -C $(FAILURES_DIR) build

deps:
	WASMTIME_VERSION=$(WASMTIME_VERSION) TEST_NGINX_REF=$(TEST_NGINX_REF) ./scripts/dev.sh

smoke:
	./scripts/smoke-content-by-wasm.sh

test: wasm
ifeq ($(wildcard $(TEST_NGINX_PERL_LIB)/Test/Nginx/Socket.pm),)
	$(error Test::Nginx not found under $(TEST_NGINX_PERL_LIB); run `make deps` or set TEST_NGINX_PERL_LIB=/path/to/test-nginx/lib)
endif
	PERL5LIB=$(TEST_NGINX_PERL_LIB):t/lib NGX_WASM_ROOT=$(PWD) TEST_NGINX_BINARY=$(PWD)/../nginx/objs/nginx $(PROVE) -r t/001-content-basic.t
	PERL5LIB=$(TEST_NGINX_PERL_LIB):t/lib NGX_WASM_ROOT=$(PWD) TEST_NGINX_BINARY=$(PWD)/../nginx/objs/nginx $(PROVE) -r t/002-content-failures.t

clean:
	$(MAKE) -C $(HELLO_WORLD_DIR) clean
	$(MAKE) -C $(FAILURES_DIR) clean
	rm -rf t/servroot
