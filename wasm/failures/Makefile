RUSTC ?= $(shell command -v rustc)
RUSTUP ?= $(shell command -v rustup)
WASM_TARGET ?= wasm32-unknown-unknown
RUSTFLAGS ?=
OUT_DIR ?= build

TARGETS = \
	$(OUT_DIR)/missing_export.wasm \
	$(OUT_DIR)/guest_trap.wasm \
	$(OUT_DIR)/nonzero_return.wasm

.PHONY: build clean check-toolchain

RUSTC_GUIDE = \
	echo "error: rustc not found in PATH"; \
	echo ""; \
	echo "Install Rust with rustup, then rerun: make deps"; \
	false

TARGET_GUIDE = \
	echo "error: Rust target $(WASM_TARGET) is not installed"; \
	echo ""; \
	echo "Run: rustup target add $(WASM_TARGET)"; \
	echo "Or rerun: make deps"; \
	false

build: check-toolchain $(TARGETS)

check-toolchain:
ifndef RUSTC
	@$(RUSTC_GUIDE)
endif
ifdef RUSTUP
	@$(RUSTUP) target list --installed | grep -qx '$(WASM_TARGET)' || \
	($(TARGET_GUIDE))
endif

$(OUT_DIR)/missing_export.wasm: src/missing_export.rs
	mkdir -p $(OUT_DIR)
	$(RUSTC) --edition=2021 --target $(WASM_TARGET) --crate-type cdylib \
		-C opt-level=z -C panic=abort $(RUSTFLAGS) -o $@ $<

$(OUT_DIR)/guest_trap.wasm: src/guest_trap.rs
	mkdir -p $(OUT_DIR)
	$(RUSTC) --edition=2021 --target $(WASM_TARGET) --crate-type cdylib \
		-C opt-level=z -C panic=abort $(RUSTFLAGS) -o $@ $<

$(OUT_DIR)/nonzero_return.wasm: src/nonzero_return.rs
	mkdir -p $(OUT_DIR)
	$(RUSTC) --edition=2021 --target $(WASM_TARGET) --crate-type cdylib \
		-C opt-level=z -C panic=abort $(RUSTFLAGS) -o $@ $<

clean:
	rm -rf $(OUT_DIR) target
