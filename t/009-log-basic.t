use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * blocks() * 3;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: log_by_wasm can observe final response status
--- config eval
qq{
    location /logged {
        log_by_wasm @{[ TestWasm::log_status_wasm() ]} on_log;
        return 201 "created";
    }
}
--- request
GET /logged
--- error_code: 201
--- response_body eval
"created"
--- error_log eval
qr/ngx_wasm guest: "log status=201"/


=== TEST 2: log_by_wasm runs only for the main request
--- config eval
qq{
    log_by_wasm @{[ TestWasm::log_status_wasm() ]} on_log;

    location /main {
        mirror /mirror;
        return 200 "ok";
    }

    location = /mirror {
        internal;
        return 204;
    }
}
--- request
GET /main
--- error_code: 200
--- response_body eval
"ok"
--- grep_error_log eval
qr/log status=\d+/
--- grep_error_log_out
log status=200


=== TEST 3: log_by_wasm failures do not alter response delivery
--- config eval
qq{
    location /logged {
        log_by_wasm @{[ TestWasm::log_set_status_wasm() ]} on_log;
        return 200 "ok";
    }
}
--- request
GET /logged
--- error_code: 200
--- response_body eval
"ok"
--- error_log eval
qr/ngx_wasm_resp_set_status not allowed in this phase|guest trapped/
