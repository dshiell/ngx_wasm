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

=== TEST 1: content_by_wasm can set a request header
--- config eval
qq{
    location /content {
        add_header X-Wasm-Test \$http_x_wasm_test always;
        content_by_wasm @{[ TestWasm::req_header_set_wasm() ]} on_content;
    }
}
--- request
GET /content
--- error_code: 200
--- response_headers
X-Wasm-Test: set-by-guest
--- response_body
header set


=== TEST 2: yielding content_by_wasm preserves request header mutation
--- config eval
qq{
    location /content-yield {
        add_header X-Wasm-Test \$http_x_wasm_test always;
        content_by_wasm @{[ TestWasm::req_header_set_yield_wasm() ]} on_content;
    }
}
--- request
GET /content-yield
--- error_code: 200
--- response_headers
X-Wasm-Test: set-after-yield
--- response_body
header set after yield


=== TEST 3: rewrite_by_wasm can set a request header
--- config eval
qq{
    location /rewrite {
        add_header X-Wasm-Test \$http_x_wasm_test always;
        rewrite_by_wasm @{[ TestWasm::req_header_set_wasm() ]} on_content;
    }
}
--- request
GET /rewrite
--- error_code: 200
--- response_headers
X-Wasm-Test: set-by-guest
--- response_body
header set


=== TEST 4: yielding rewrite_by_wasm preserves request header mutation
--- config eval
qq{
    location /rewrite-yield {
        add_header X-Wasm-Test \$http_x_wasm_test always;
        rewrite_by_wasm @{[ TestWasm::req_header_set_yield_wasm() ]} on_content;
    }
}
--- request
GET /rewrite-yield
--- error_code: 200
--- response_headers
X-Wasm-Test: set-after-yield
--- response_body
header set after yield


=== TEST 5: rewrite_by_wasm header mutation is visible to content phase
--- config eval
qq{
    location /rewrite-to-content {
        rewrite_by_wasm @{[ TestWasm::req_header_set_only_wasm() ]} on_content;
        content_by_wasm @{[ TestWasm::req_header_echo_wasm() ]} on_content;
    }
}
--- request
GET /rewrite-to-content
--- error_code: 200
--- response_headers
Content-Type: text/plain
--- response_body
set-by-guest
