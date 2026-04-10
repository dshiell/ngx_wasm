use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * (blocks() * 2 + 1);

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: server_rewrite_by_wasm returns guest body before content
--- config eval
qq{
    server_rewrite_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;

    location /wasm {
        content_by_wasm @{[ TestWasm::nonzero_return_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 200
--- response_body
hello from guest wasm


=== TEST 2: server_rewrite_by_wasm inherits from http scope
--- http_config eval
qq{
    server_rewrite_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
}
--- config
location /inherited {
}
--- request
GET /inherited
--- error_code: 200
--- response_body
hello from guest wasm


=== TEST 3: server_rewrite_by_wasm resumes after manual yield
--- config eval
qq{
    server_rewrite_by_wasm @{[ TestWasm::manual_yield_wasm() ]} on_content;

    location /yield {
        content_by_wasm @{[ TestWasm::nonzero_return_wasm() ]} on_content;
    }
}
--- request
GET /yield
--- error_code: 200
--- response_body
hello after manual yield


=== TEST 4: server_rewrite_by_wasm falls through when no response is produced
--- config eval
qq{
    server_rewrite_by_wasm @{[ TestWasm::req_header_set_only_wasm() ]} on_content;

    location /fallthrough {
        content_by_wasm @{[ TestWasm::req_header_echo_wasm() ]} on_content;
    }
}
--- request
GET /fallthrough
--- error_code: 200
--- response_headers
Content-Type: text/plain
--- response_body
set-by-guest


=== TEST 5: server_rewrite_by_wasm accepts POST requests
--- config eval
qq{
    server_rewrite_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;

    location /post {
        content_by_wasm @{[ TestWasm::nonzero_return_wasm() ]} on_content;
    }
}
--- request
POST /post
hello request body
--- error_code: 200
--- response_body
hello from guest wasm
