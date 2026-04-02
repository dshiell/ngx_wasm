CC ?= clang
WASM_CFLAGS ?= --target=wasm32-unknown-unknown -O2 -nostdlib
WASM_LD ?=
ifeq ($(strip $(WASM_LD)),)
WASM_LINKER_FLAGS ?= -fuse-ld=lld
else
WASM_LINKER_FLAGS ?=
endif
OUT_DIR ?= build

TARGETS = \
	$(OUT_DIR)/missing_export.wasm \
	$(OUT_DIR)/missing_memory.wasm \
	$(OUT_DIR)/guest_trap.wasm \
	$(OUT_DIR)/nonzero_return.wasm

.PHONY: build clean

build: $(TARGETS)

$(OUT_DIR)/.dir:
	mkdir -p $(OUT_DIR)
	touch $@

$(OUT_DIR)/missing_export.o: src/missing_export.c | $(OUT_DIR)/.dir
	$(CC) $(WASM_CFLAGS) -c -o $@ $<

$(OUT_DIR)/missing_memory.o: src/missing_memory.c | $(OUT_DIR)/.dir
	$(CC) $(WASM_CFLAGS) -c -o $@ $<

$(OUT_DIR)/guest_trap.o: src/guest_trap.c | $(OUT_DIR)/.dir
	$(CC) $(WASM_CFLAGS) -c -o $@ $<

$(OUT_DIR)/nonzero_return.o: src/nonzero_return.c | $(OUT_DIR)/.dir
	$(CC) $(WASM_CFLAGS) -c -o $@ $<

$(OUT_DIR)/missing_export.wasm: $(OUT_DIR)/missing_export.o
ifeq ($(strip $(WASM_LD)),)
	$(CC) $(WASM_LINKER_FLAGS) $(WASM_CFLAGS) -Wl,--no-entry -Wl,--export=not_on_content -Wl,--export-memory -Wl,--allow-undefined -o $@ $<
else
	$(WASM_LD) --no-entry --export=not_on_content --export-memory --allow-undefined -o $@ $<
endif

$(OUT_DIR)/missing_memory.wasm: $(OUT_DIR)/missing_memory.o
ifeq ($(strip $(WASM_LD)),)
	$(CC) $(WASM_LINKER_FLAGS) $(WASM_CFLAGS) -Wl,--no-entry -Wl,--export=on_content -Wl,--allow-undefined -o $@ $<
else
	$(WASM_LD) --no-entry --export=on_content --allow-undefined -o $@ $<
endif

$(OUT_DIR)/guest_trap.wasm: $(OUT_DIR)/guest_trap.o
ifeq ($(strip $(WASM_LD)),)
	$(CC) $(WASM_LINKER_FLAGS) $(WASM_CFLAGS) -Wl,--no-entry -Wl,--export=on_content -Wl,--export-memory -Wl,--allow-undefined -o $@ $<
else
	$(WASM_LD) --no-entry --export=on_content --export-memory --allow-undefined -o $@ $<
endif

$(OUT_DIR)/nonzero_return.wasm: $(OUT_DIR)/nonzero_return.o
ifeq ($(strip $(WASM_LD)),)
	$(CC) $(WASM_LINKER_FLAGS) $(WASM_CFLAGS) -Wl,--no-entry -Wl,--export=on_content -Wl,--export-memory -Wl,--allow-undefined -o $@ $<
else
	$(WASM_LD) --no-entry --export=on_content --export-memory --allow-undefined -o $@ $<
endif

clean:
	rm -rf $(OUT_DIR)
