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

=== TEST 1: content_by_wasm returns guest body
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 200
--- response_body
hello from guest wasm
