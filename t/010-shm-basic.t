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

=== TEST 1: content_by_wasm can round-trip shared memory in one request
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /roundtrip {
        content_by_wasm @{[ TestWasm::shm_roundtrip_wasm() ]} on_content;
    }
}
--- request
GET /roundtrip
--- error_code: 200
--- response_body eval
"roundtrip-value"


=== TEST 2: shared memory persists across requests
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /set {
        content_by_wasm @{[ TestWasm::shm_set_wasm() ]} on_content;
    }

    location /get {
        content_by_wasm @{[ TestWasm::shm_get_wasm() ]} on_content;
    }
}
--- request eval
[
    "GET /set",
    "GET /get",
]
--- error_code eval
[
    200,
    200,
]
--- response_body eval
[
    "stored",
    "persisted-value",
]


=== TEST 3: rewrite_by_wasm can populate shared memory for content_by_wasm
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /rewrite-to-content {
        rewrite_by_wasm @{[ TestWasm::shm_set_only_wasm() ]} on_content;
        content_by_wasm @{[ TestWasm::shm_get_wasm() ]} on_content;
    }
}
--- request
GET /rewrite-to-content
--- error_code: 200
--- response_body eval
"persisted-value"


=== TEST 4: shared memory delete removes keys
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /set {
        content_by_wasm @{[ TestWasm::shm_set_wasm() ]} on_content;
    }

    location /delete {
        content_by_wasm @{[ TestWasm::shm_delete_wasm() ]} on_content;
    }

    location /get {
        content_by_wasm @{[ TestWasm::shm_get_wasm() ]} on_content;
    }
}
--- request eval
[
    "GET /set",
    "GET /delete",
    "GET /get",
]
--- error_code eval
[
    200,
    200,
    200,
]
--- response_body eval
[
    "stored",
    "deleted",
    "not found",
]


=== TEST 5: missing shared memory zone fails cleanly
--- config eval
qq{
    location /missing {
        content_by_wasm @{[ TestWasm::shm_error_check_wasm() ]} on_missing_zone;
    }
}
--- request
GET /missing
--- error_code: 200
--- response_body eval
"error"


=== TEST 6: oversized shared memory key fails cleanly
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /oversize {
        content_by_wasm @{[ TestWasm::shm_error_check_wasm() ]} on_oversize_key;
    }
}
--- request
GET /oversize
--- error_code: 200
--- response_body eval
"error"
