CC ?= clang
WASM_CFLAGS ?= --target=wasm32-unknown-unknown -O2 -nostdlib
WASM_LD ?=
ifeq ($(strip $(WASM_LD)),)
WASM_LINKER_FLAGS ?= -fuse-ld=lld
LINK_WASM = $(CC) $(WASM_LINKER_FLAGS) $(WASM_CFLAGS) $(WASM_LDFLAGS) -o $@ $<
else
WASM_LINKER_FLAGS ?=
LINK_WASM = $(WASM_LD) --no-entry --export=on_content --export-memory --allow-undefined -o $@ $<
endif
WASM_LDFLAGS ?= -Wl,--no-entry -Wl,--export=on_content -Wl,--export-memory -Wl,--allow-undefined
OUT_DIR ?= build
OUT ?= $(OUT_DIR)/hello_world.wasm
OBJ ?= $(OUT_DIR)/hello_world.o
SRC ?= src/hello_world.c

.PHONY: build clean check-toolchain

COMPILE_GUIDE = \
	echo "error: $(CC) cannot compile for wasm32-unknown-unknown"; \
	echo ""; \
	echo "You need an LLVM/Clang toolchain with the WebAssembly backend."; \
	echo ""; \
	echo "Common fixes:"; \
	echo "  macOS (Homebrew): brew install llvm"; \
	echo "  then run: make CC=\$$(brew --prefix llvm)/bin/clang wasm"; \
	echo ""; \
	echo "  Linux: install an LLVM/Clang package with WebAssembly support, then run:"; \
	echo "  make CC=/path/to/clang wasm"; \
	echo ""; \
	echo "Current compiler:"; \
	echo "  $(CC)"; \
	echo ""; \
	echo "Expected compile target:"; \
	echo "  --target=wasm32-unknown-unknown"; \
	false

LINK_GUIDE = \
	echo "error: $(CC) compiled the wasm object, but linking a wasm module failed"; \
	echo ""; \
	echo "You need lld/wasm-ld for the final link step."; \
	echo ""; \
	echo "Common fixes:"; \
	echo "  macOS (Homebrew): brew install lld"; \
	echo "  then run one of:"; \
	echo "  make CC=\$$(brew --prefix llvm)/bin/clang WASM_LD=\$$(brew --prefix lld)/bin/wasm-ld wasm"; \
	echo "  make CC=\$$(brew --prefix llvm)/bin/clang WASM_LD=\$$(brew --prefix lld)/bin/ld.lld wasm"; \
	echo ""; \
	echo "  Linux: install lld or wasm-ld, then run:"; \
	echo "  make CC=/path/to/clang WASM_LD=/path/to/wasm-ld wasm"; \
	echo ""; \
	echo "Current compiler:"; \
	echo "  $(CC)"; \
	echo "Configured wasm linker:"; \
	echo "  $(if $(strip $(WASM_LD)),$(WASM_LD),auto via -fuse-ld=lld)"; \
	echo "Current linker flags:"; \
	echo "  $(WASM_LINKER_FLAGS) $(WASM_LDFLAGS)"; \
	echo ""; \
	echo "Expected linker support:"; \
	echo "  -fuse-ld=lld"; \
	echo "  -Wl,--no-entry -Wl,--export=on_content -Wl,--export-memory -Wl,--allow-undefined"; \
	false

build: check-toolchain $(OUT)

check-toolchain:
	@mkdir -p $(OUT_DIR)
	@printf '__attribute__((export_name("on_content"))) int on_content(void) { return 0; }\n' | \
	$(CC) $(WASM_CFLAGS) -x c -c -o $(OUT_DIR)/.toolchain-check.o - >/dev/null 2>&1 || \
	($(COMPILE_GUIDE))
	@$(if $(strip $(WASM_LD)),\
	$(WASM_LD) --no-entry --export=on_content --export-memory --allow-undefined -o $(OUT_DIR)/.toolchain-check.wasm $(OUT_DIR)/.toolchain-check.o,\
	$(CC) $(WASM_LINKER_FLAGS) $(WASM_CFLAGS) $(WASM_LDFLAGS) $(OUT_DIR)/.toolchain-check.o -o $(OUT_DIR)/.toolchain-check.wasm) >/dev/null 2>&1 || \
	($(LINK_GUIDE))
	@rm -f $(OUT_DIR)/.toolchain-check.o $(OUT_DIR)/.toolchain-check.wasm

$(OBJ): $(SRC)
	mkdir -p $(OUT_DIR)
	$(CC) $(WASM_CFLAGS) -c -o $@ $<

$(OUT): $(OBJ)
	$(LINK_WASM)

clean:
	rm -rf $(OUT_DIR)
