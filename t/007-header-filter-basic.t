use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * 13;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: header_filter_by_wasm can mutate status and headers on plain responses
--- config eval
qq{
    location /plain {
        header_filter_by_wasm @{[ TestWasm::resp_status_set_wasm() ]} on_header;
        return 204;
    }
}
--- request
GET /plain
--- error_code: 201
--- response_headers
X-Wasm-Filter: status-mutated


=== TEST 2: header_filter_by_wasm can observe upstream response headers
--- http_config
server {
    listen 1985;
    server_name upstream;

    location /origin {
        add_header X-Origin upstream always;
        return 200 "proxied";
    }
}
--- config eval
qq{
    location /proxy {
        header_filter_by_wasm @{[ TestWasm::resp_header_echo_wasm() ]} on_header;
        proxy_pass http://127.0.0.1:1985/origin;
    }
}
--- request
GET /proxy
--- error_code: 200
--- response_headers
X-Wasm-Observed: upstream
--- response_body eval
"proxied"


=== TEST 3: header_filter_by_wasm updates cached Location handling
--- config eval
qq{
    location /redirect {
        header_filter_by_wasm @{[ TestWasm::resp_location_set_wasm() ]} on_header;
        return 204;
    }
}
--- request
GET /redirect
--- error_code: 302
--- response_headers
Location: http://localhost:1984/moved


=== TEST 4: header_filter_by_wasm runs on content_by_wasm responses
--- config eval
qq{
    location /content {
        header_filter_by_wasm @{[ TestWasm::resp_header_set_wasm() ]} on_header;
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /content
--- error_code: 200
--- response_headers
X-Wasm-Filter: set-by-header-filter
--- response_body
hello from guest wasm


=== TEST 5: header_filter_by_wasm runs after yielding content_by_wasm
--- config eval
qq{
    location /yield {
        header_filter_by_wasm @{[ TestWasm::resp_header_set_wasm() ]} on_header;
        content_by_wasm @{[ TestWasm::manual_yield_wasm() ]} on_content;
    }
}
--- request
GET /yield
--- error_code: 200
--- response_headers
X-Wasm-Filter: set-by-header-filter
--- response_body
hello after manual yield
