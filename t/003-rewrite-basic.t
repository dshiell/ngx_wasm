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

=== TEST 1: rewrite_by_wasm returns guest body
--- config eval
qq{
    location /wasm {
        rewrite_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /wasm
--- error_code: 200
--- response_body
hello from guest wasm


=== TEST 2: rewrite_by_wasm inherits from server scope
--- config eval
qq{
    rewrite_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;

    location /inherited {
    }
}
--- request
GET /inherited
--- error_code: 200
--- response_body
hello from guest wasm


=== TEST 3: rewrite_by_wasm resumes after manual yield
--- config eval
qq{
    location /yield {
        rewrite_by_wasm @{[ TestWasm::manual_yield_wasm() ]} on_content;
    }
}
--- request
GET /yield
--- error_code: 200
--- response_body
hello after manual yield


=== TEST 4: rewrite_by_wasm runs before content_by_wasm
--- config eval
qq{
    location /phases {
        rewrite_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
        content_by_wasm @{[ TestWasm::nonzero_return_wasm() ]} on_content;
    }
}
--- request
GET /phases
--- error_code: 200
--- response_body
hello from guest wasm


=== TEST 5: yielding rewrite_by_wasm still suppresses content_by_wasm
--- config eval
qq{
    location /yield-phases {
        rewrite_by_wasm @{[ TestWasm::manual_yield_wasm() ]} on_content;
        content_by_wasm @{[ TestWasm::nonzero_return_wasm() ]} on_content;
    }
}
--- request
GET /yield-phases
--- error_code: 200
--- response_body
hello after manual yield


=== TEST 6: inherited rewrite_by_wasm and local content_by_wasm coexist
--- config eval
qq{
    rewrite_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;

    location /mixed {
        content_by_wasm @{[ TestWasm::manual_yield_wasm() ]} on_content;
    }
}
--- request
GET /mixed
--- error_code: 200
--- response_body
hello from guest wasm
