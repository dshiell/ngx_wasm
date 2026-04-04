use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * blocks() * 2;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: missing export
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ TestWasm::missing_export_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 500
--- error_log eval
qr/export "on_content" not found or not a function/


=== TEST 2: missing memory export
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ TestWasm::missing_memory_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 500
--- error_log eval
qr/guest memory export not found/


=== TEST 3: guest trap
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ TestWasm::guest_trap_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 500
--- error_log eval
qr/guest trapped/


=== TEST 4: non-zero return
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ TestWasm::nonzero_return_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 500
--- error_log eval
qr/returned 7/


=== TEST 5: bad module path
--- config
location /wasm {
    content_by_wasm /tmp/ngx-wasm-does-not-exist.wasm on_content;
}
--- request
GET /wasm
--- must_die
--- error_log eval
qr/failed to open module/


=== TEST 6: fuel exhaustion interrupts guest
--- config eval
qq{
    location /wasm {
        wasm_fuel_limit 1000;
        content_by_wasm @{[ TestWasm::fuel_exhaust_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 500
--- error_log eval
qr/guest interrupted: fuel_limit=1000/
