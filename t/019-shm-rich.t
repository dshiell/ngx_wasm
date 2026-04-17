use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * 32;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: shared memory exists tracks add visibility
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /exists {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_exists_flag;
    }

    location /add {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_add_flag;
    }
}
--- request eval
[
    "GET /exists",
    "GET /add",
    "GET /exists",
]
--- error_code eval
[
    200,
    200,
    200,
]
--- response_body eval
[
    "missing",
    "added",
    "present",
]


=== TEST 2: shared memory add fails when the key already exists
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /add {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_add_flag;
    }

    location /add-again {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_add_flag_again;
    }

    location /get {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_get_flag;
    }
}
--- request eval
[
    "GET /add",
    "GET /add-again",
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
    "added",
    "exists",
    "first-value",
]


=== TEST 3: shared memory replace fails on missing keys and updates existing ones
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /replace-missing {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_replace_missing;
    }

    location /add {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_add_flag;
    }

    location /replace {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_replace_flag;
    }

    location /get {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_get_flag;
    }
}
--- request eval
[
    "GET /replace-missing",
    "GET /add",
    "GET /replace",
    "GET /get",
]
--- error_code eval
[
    200,
    200,
    200,
    200,
]
--- response_body eval
[
    "missing",
    "added",
    "replaced",
    "second-value",
]


=== TEST 4: shared memory incr is atomic across repeated requests
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /seed {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_seed_counter;
    }

    location /incr {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_incr_counter;
    }
}
--- request eval
[
    "GET /seed",
    "GET /incr",
    "GET /incr",
    "GET /incr",
]
--- error_code eval
[
    200,
    200,
    200,
    200,
]
--- response_body eval
[
    "0",
    "2",
    "4",
    "6",
]


=== TEST 5: shared memory incr rejects non-integer values
--- http_config
wasm_shm_zone shared 1m;
--- config eval
qq{
    location /seed {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_seed_bad_counter;
    }

    location /incr {
        content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_incr_bad_counter;
    }
}
--- request eval
[
    "GET /seed",
    "GET /incr",
]
--- error_code eval
[
    200,
    200,
]
--- response_body eval
[
    "oops",
    "error",
]
