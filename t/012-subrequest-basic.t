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

=== TEST 1: content_by_wasm can capture a subrequest body
--- config eval
qq{
    location /body {
        content_by_wasm @{[ TestWasm::subreq_body_echo_wasm() ]} on_content;
    }

    location /sub/source {
        internal;
        return 200 "from subrequest";
    }
}
--- request
GET /body
--- error_code: 200
--- response_body: from subrequest


=== TEST 2: content_by_wasm can read a subrequest response header
--- config eval
qq{
    location /header {
        content_by_wasm @{[ TestWasm::subreq_header_echo_wasm() ]} on_content;
    }

    location /sub/header {
        internal;
        header_filter_by_wasm @{[ TestWasm::resp_header_set_wasm() ]} on_header;
        return 200 "ignored";
    }
}
--- request
GET /header
--- error_code: 200
--- response_body: set-by-header-filter


=== TEST 3: content_by_wasm can set subrequest request headers
--- config eval
qq{
    location /method {
        content_by_wasm @{[ TestWasm::subreq_method_post_wasm() ]} on_content;
    }

    location /sub/method {
        internal;
        return 200 "\$http_x_wasm_test";
    }
}
--- request
GET /method
--- error_code: 200
--- response_body: POST


=== TEST 4: rewrite_by_wasm subrequest can feed content_by_wasm through request headers
--- config eval
qq{
    location /rewrite-auth {
        rewrite_by_wasm @{[ TestWasm::subreq_rewrite_auth_wasm() ]} on_content;
        content_by_wasm @{[ TestWasm::req_header_echo_wasm() ]} on_content;
    }

    location /sub/auth {
        internal;
        return 200 "ok";
    }
}
--- request
GET /rewrite-auth
--- error_code: 200
--- response_body eval
"authorized\n"


=== TEST 5: non-2xx subrequest status is surfaced as a normal result
--- config eval
qq{
    location /status {
        content_by_wasm @{[ TestWasm::subreq_status_404_wasm() ]} on_content;
    }

    location /sub/missing {
        internal;
        return 404;
    }
}
--- request
GET /status
--- error_code: 200
--- response_body: 404


=== TEST 6: subrequests are forbidden from log_by_wasm
--- config eval
qq{
    location /forbidden {
        log_by_wasm @{[ TestWasm::subreq_forbidden_wasm() ]} on_log;
        return 200 "log-ok";
    }

    location /sub/source {
        internal;
        return 200 "from subrequest";
    }
}
--- request
GET /forbidden
--- error_code: 200
--- response_body: log-ok
--- error_log
ngx_wasm_subreq not allowed in this phase
