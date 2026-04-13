use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * 12;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: body_filter_by_wasm can transform a plain response body
--- config eval
qq{
    location /plain {
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        return 200 "hello";
    }
}
--- request
GET /plain
--- error_code: 200
--- response_body eval
"HELLO"


=== TEST 2: body_filter_by_wasm can transform content_by_wasm output
--- config eval
qq{
    location /content {
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /content
--- error_code: 200
--- response_body
HELLO FROM GUEST WASM


=== TEST 3: body_filter_by_wasm can transform upstream response bodies
--- http_config
server {
    listen 1985;
    server_name upstream;

    location /origin {
        return 200 "proxied";
    }
}
--- config eval
qq{
    location /proxy {
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        proxy_pass http://127.0.0.1:1985/origin;
    }
}
--- request
GET /proxy
--- error_code: 200
--- response_body eval
"PROXIED"


=== TEST 4: header_filter_by_wasm and body_filter_by_wasm can run together
--- config eval
qq{
    location /combo {
        header_filter_by_wasm @{[ TestWasm::resp_header_set_wasm() ]} on_header;
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /combo
--- error_code: 200
--- response_headers
X-Wasm-Filter: set-by-header-filter
--- response_body
HELLO FROM GUEST WASM


=== TEST 5: body_filter_by_wasm can detect EOF and force chunked output
--- more_headers
Connection: close
--- config eval
qq{
    location /eof {
        body_filter_by_wasm @{[ TestWasm::resp_body_append_eof_wasm() ]} on_body;
        return 200 "hello";
    }
}
--- request
GET /eof
--- error_code: 200
--- response_headers
Transfer-Encoding: chunked
--- response_body eval
"hello-eof"
