use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * 11;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: content_by_wasm can read a built-in nginx variable
--- config eval
qq{
    location /read {
        content_by_wasm @{[ TestWasm::var_get_request_uri_wasm() ]} on_content;
    }
}
--- request
GET /read?x=1
--- error_code: 200
--- response_body eval
"/read?x=1"


=== TEST 2: rewrite_by_wasm can set a changeable variable for later nginx use
--- config eval
qq{
    set \$wasm_value "";

    location /set {
        rewrite_by_wasm @{[ TestWasm::var_set_only_wasm() ]} on_rewrite;
        return 200 \$wasm_value;
    }
}
--- request
GET /set
--- error_code: 200
--- response_body eval
"from-wasm"


=== TEST 3: unknown variables return not found
--- config eval
qq{
    location /missing {
        content_by_wasm @{[ TestWasm::var_get_missing_wasm() ]} on_content;
    }
}
--- request
GET /missing
--- error_code: 200
--- response_body eval
"missing"


=== TEST 4: non-changeable variables cannot be written
--- config eval
qq{
    location /readonly {
        content_by_wasm @{[ TestWasm::var_set_readonly_wasm() ]} on_content;
    }
}
--- request
GET /readonly
--- error_code: 200
--- response_body eval
"error"


=== TEST 5: var_set is forbidden in log phase
--- config eval
qq{
    set \$wasm_value "";

    location /logged {
        log_by_wasm @{[ TestWasm::var_set_forbidden_wasm() ]} on_log;
        return 200 "ok";
    }
}
--- request
GET /logged
--- error_code: 200
--- response_body eval
"ok"
--- error_log eval
qr/ngx_wasm_var_set not allowed in this phase|guest trapped/
