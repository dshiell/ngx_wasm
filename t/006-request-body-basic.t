use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * 5;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: content_by_wasm can read a fixed-length request body
--- config eval
qq{
    location /echo {
        wasm_request_body_buffer_size 32;
        content_by_wasm @{[ TestWasm::req_body_echo_wasm() ]} on_content;
    }
}
--- raw_request eval
"POST /echo HTTP/1.1\r\nHost: localhost\r\nContent-Length: 10\r\nConnection: close\r\n\r\nhello body"
--- error_code: 200
--- response_body eval
"hello body"


=== TEST 2: content_by_wasm can read a chunked request body
--- config eval
qq{
    location /chunked {
        wasm_request_body_buffer_size 32;
        content_by_wasm @{[ TestWasm::req_body_echo_wasm() ]} on_content;
    }
}
--- raw_request eval
"POST /chunked HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n5\r\nhello\r\n5\r\n body\r\n0\r\n\r\n"
--- error_code: 200
--- response_body eval
"hello body"


=== TEST 3: request body larger than configured content-length limit is rejected
--- config eval
qq{
    location /too-large {
        wasm_request_body_buffer_size 4;
        content_by_wasm @{[ TestWasm::req_body_echo_wasm() ]} on_content;
    }
}
--- raw_request eval
"POST /too-large HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\nConnection: close\r\n\r\nhello"
--- error_code: 413
