package TestWasm;

use strict;
use warnings;

use Exporter qw(import);
use File::Basename qw(dirname);
use File::Spec;

our @EXPORT_OK = qw(
    wasm_root
    hello_world_wasm
    fuel_yield_wasm
    fuel_yield_rust_wasm
    fuel_multi_yield_wasm
    manual_yield_wasm
    multi_yield_wasm
    req_header_set_wasm
    req_header_set_yield_wasm
    missing_export_wasm
    missing_memory_wasm
    guest_trap_wasm
    nonzero_return_wasm
    fuel_exhaust_wasm
);

sub wasm_root {
    my $root;

    if (defined $ENV{NGX_WASM_ROOT} && length $ENV{NGX_WASM_ROOT}) {
        return $ENV{NGX_WASM_ROOT};
    }

    $root = File::Spec->rel2abs(
        File::Spec->catdir(dirname(__FILE__), '..', '..')
    );

    return $root;
}

sub hello_world_wasm {
    return wasm_root() . "/wasm/hello-world/build/hello_world.wasm";
}

sub fuel_yield_wasm {
    return wasm_root() . "/wasm/hello-world/src/fuel_yield.wat";
}

sub fuel_yield_rust_wasm {
    return wasm_root() . "/wasm/hello-world/build/fuel_yield_rust.wasm";
}

sub fuel_multi_yield_wasm {
    return wasm_root() . "/wasm/hello-world/src/fuel_multi_yield.wat";
}

sub manual_yield_wasm {
    return wasm_root() . "/wasm/hello-world/src/manual_yield.wat";
}

sub multi_yield_wasm {
    return wasm_root() . "/wasm/hello-world/src/multi_yield.wat";
}

sub req_header_set_wasm {
    return wasm_root() . "/wasm/hello-world/src/req_header_set.wat";
}

sub req_header_set_yield_wasm {
    return wasm_root() . "/wasm/hello-world/src/req_header_set_yield.wat";
}

sub missing_export_wasm {
    return wasm_root() . "/wasm/failures/build/missing_export.wasm";
}

sub missing_memory_wasm {
    return wasm_root() . "/wasm/failures/src/missing_memory.wat";
}

sub guest_trap_wasm {
    return wasm_root() . "/wasm/failures/build/guest_trap.wasm";
}

sub nonzero_return_wasm {
    return wasm_root() . "/wasm/failures/build/nonzero_return.wasm";
}

sub fuel_exhaust_wasm {
    return wasm_root() . "/wasm/failures/src/fuel_exhaust.wat";
}

1;
