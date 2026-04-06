use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * 14;

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


=== TEST 2: content_by_wasm honors explicit fuel limit
--- config eval
qq{
    location /wasm {
        wasm_fuel_limit 1000000;
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 200
--- response_body
hello from guest wasm


=== TEST 3: same wasm can be used from multiple locations
--- config eval
qq{
    location /wasm-a {
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }

    location /wasm-b {
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- pipelined_requests eval
[
    "GET /wasm-a",
    "GET /wasm-b",
]
--- error_code eval
[200, 200]
--- response_body eval
[
    "hello from guest wasm\n",
    "hello from guest wasm\n",
]


=== TEST 4: content_by_wasm honors explicit timeslice fuel
--- config eval
qq{
    location /wasm {
        wasm_fuel_limit 1000000;
        wasm_timeslice_fuel 1000000;
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 200
--- response_body
hello from guest wasm


=== TEST 5: manual yield resumes and completes
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ TestWasm::manual_yield_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 200
--- response_body
hello after manual yield


=== TEST 6: multiple yields resume and complete
--- config eval
qq{
    location /wasm {
        content_by_wasm @{[ TestWasm::multi_yield_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 200
--- response_body
hello after two yields
