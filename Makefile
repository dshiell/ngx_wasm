CLANG_FORMAT ?= $(shell command -v clang-format 2>/dev/null)
WASMTIME_VERSION ?= 36.0.3

FORMAT_FILES = src/ngx_http_wasm_module.c
HELLO_WORLD_DIR = examples/hello-world

.PHONY: format check-format hello-world wasm wasmtime-fetch

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

hello-world:
	$(MAKE) -C $(HELLO_WORLD_DIR) build

wasm:
	$(MAKE) -C $(HELLO_WORLD_DIR) build

wasmtime-fetch:
	WASMTIME_VERSION=$(WASMTIME_VERSION) ./scripts/fetch-wasmtime.sh
