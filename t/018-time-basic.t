use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * 7;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: content_by_wasm can read cached unix and monotonic time
--- config eval
qq{
    location /time {
        content_by_wasm @{[ TestWasm::time_basic_wasm() ]} on_content;
    }
}
--- request
GET /time
--- error_code: 200
--- response_body eval
"ok"


=== TEST 2: time abi rejects short output buffers
--- config eval
qq{
    location /short {
        content_by_wasm @{[ TestWasm::time_short_buf_wasm() ]} on_content;
    }
}
--- request
GET /short
--- error_code: 200
--- response_body eval
"error"


=== TEST 3: log_by_wasm can read time
--- config eval
qq{
    location /logged {
        log_by_wasm @{[ TestWasm::time_log_wasm() ]} on_log;
        return 200 "ok";
    }
}
--- request
GET /logged
--- error_code: 200
--- response_body eval
"ok"
--- error_log eval
qr/ngx_wasm guest: "time-ok"/
