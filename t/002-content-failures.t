use strict;
use warnings;

use lib 't/lib';
use TestWasm qw(
    missing_export_wasm
    missing_memory_wasm
    guest_trap_wasm
    nonzero_return_wasm
);
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * blocks();

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: missing export
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ missing_export_wasm() ]} on_content;
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
        content_by_wasm @{[ missing_memory_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 500
--- error_log eval
qr/guest trapped/


=== TEST 3: guest trap
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ guest_trap_wasm() ]} on_content;
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
        content_by_wasm @{[ nonzero_return_wasm() ]} on_content;
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
--- error_code: 500
--- error_log eval
qr/failed to open module/
