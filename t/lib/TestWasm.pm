package TestWasm;

use strict;
use warnings;

use Exporter qw(import);
use File::Basename qw(dirname);
use File::Spec;

our @EXPORT_OK = qw(
    wasm_root
    hello_world_wasm
    missing_export_wasm
    missing_memory_wasm
    guest_trap_wasm
    nonzero_return_wasm
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

1;
