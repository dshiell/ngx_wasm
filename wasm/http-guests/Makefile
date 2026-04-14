RUSTC ?= $(shell command -v rustc)
RUSTUP ?= $(shell command -v rustup)
WASM_TARGET ?= wasm32-unknown-unknown
RUSTFLAGS ?=
OUT_DIR ?= build

TARGETS = \
	$(OUT_DIR)/hello_world.wasm \
	$(OUT_DIR)/health.wasm \
	$(OUT_DIR)/fuel_yield_rust.wasm \
	$(OUT_DIR)/ssl_select_cert.wasm \
	$(OUT_DIR)/ssl_reject_blocked.wasm

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

$(OUT_DIR)/hello_world.wasm: src/hello_world.rs
	mkdir -p $(OUT_DIR)
	$(RUSTC) --edition=2021 --target $(WASM_TARGET) --crate-type cdylib \
		-C opt-level=z -C panic=abort $(RUSTFLAGS) -o $@ $<

$(OUT_DIR)/health.wasm: src/health.rs
	mkdir -p $(OUT_DIR)
	$(RUSTC) --edition=2021 --target $(WASM_TARGET) --crate-type cdylib \
		-C opt-level=z -C panic=abort $(RUSTFLAGS) -o $@ $<

$(OUT_DIR)/fuel_yield_rust.wasm: src/fuel_yield.rs
	mkdir -p $(OUT_DIR)
	$(RUSTC) --edition=2021 --target $(WASM_TARGET) --crate-type cdylib \
		-C opt-level=z -C panic=abort $(RUSTFLAGS) -o $@ $<

$(OUT_DIR)/ssl_select_cert.wasm: src/ssl_select_cert.rs src/ssl/selected.local.crt src/ssl/selected.local.key
	mkdir -p $(OUT_DIR)
	$(RUSTC) --edition=2021 --target $(WASM_TARGET) --crate-type cdylib \
		-C opt-level=z -C panic=abort $(RUSTFLAGS) -o $@ $<

$(OUT_DIR)/ssl_reject_blocked.wasm: src/ssl_reject_blocked.rs
	mkdir -p $(OUT_DIR)
	$(RUSTC) --edition=2021 --target $(WASM_TARGET) --crate-type cdylib \
		-C opt-level=z -C panic=abort $(RUSTFLAGS) -o $@ $<

clean:
	rm -rf $(OUT_DIR)
