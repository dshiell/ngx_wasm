use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * 26;

our $HttpConfig = '';

run_tests();

__DATA__

=== TEST 1: body_filter_by_wasm can transform a plain response body
--- config eval
qq{
    location /plain {
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        return 200 "hello";
    }
}
--- request
GET /plain
--- error_code: 200
--- response_body eval
"HELLO"


=== TEST 2: body_filter_by_wasm can transform content_by_wasm output
--- config eval
qq{
    location /content {
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /content
--- error_code: 200
--- response_body
HELLO FROM GUEST WASM


=== TEST 3: body_filter_by_wasm can transform upstream response bodies
--- http_config
server {
    listen 1985;
    server_name upstream;

    location /origin {
        return 200 "proxied";
    }
}
--- config eval
qq{
    location /proxy {
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        proxy_pass http://127.0.0.1:1985/origin;
    }
}
--- request
GET /proxy
--- error_code: 200
--- response_body eval
"PROXIED"


=== TEST 4: header_filter_by_wasm and body_filter_by_wasm can run together
--- config eval
qq{
    location /combo {
        header_filter_by_wasm @{[ TestWasm::resp_header_set_wasm() ]} on_header;
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        content_by_wasm @{[ TestWasm::hello_world_wasm() ]} on_content;
    }
}
--- request
GET /combo
--- error_code: 200
--- response_headers
X-Wasm-Filter: set-by-header-filter
--- response_body
HELLO FROM GUEST WASM


=== TEST 5: body_filter_by_wasm can detect EOF and force chunked output
--- more_headers
Connection: close
--- config eval
qq{
    location /eof {
        body_filter_by_wasm @{[ TestWasm::resp_body_append_eof_wasm() ]} on_body;
        return 200 "hello";
    }
}
--- request
GET /eof
--- error_code: 200
--- response_headers
Transfer-Encoding: chunked
--- response_body eval
"hello-eof"


=== TEST 6: body_filter_by_wasm can transform static file responses via aio threads
--- main_config
thread_pool default threads=2 max_queue=65536;
--- user_files
>>> files/static.txt
hello from file
--- config eval
qq{
    location /files/ {
        root html;
        aio threads;
        wasm_body_filter_file_chunk_size 4;
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
    }
}
--- request
GET /files/static.txt
--- error_code: 200
--- response_body
HELLO FROM FILE


=== TEST 7: file-backed body materialization preserves EOF on the final split chunk only
--- main_config
thread_pool default threads=2 max_queue=65536;
--- user_files
>>> files/eof.txt
abcdefghi
--- config eval
qq{
    location /files/ {
        root html;
        aio threads;
        wasm_body_filter_file_chunk_size 3;
        body_filter_by_wasm @{[ TestWasm::resp_body_append_eof_wasm() ]} on_body;
    }
}
--- request
GET /files/eof.txt
--- error_code: 200
--- response_body eval
"abcdefghi\n-eof"


=== TEST 8: file-backed body materialization can be disabled per location
--- main_config
thread_pool default threads=2 max_queue=65536;
--- user_files
>>> files/disabled.txt
hello disabled
--- config eval
qq{
    location /files/ {
        root html;
        aio threads;
        wasm_body_filter_file_chunk_size 0;
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
    }
}
--- request
GET /files/disabled.txt
--- error_code: 200
--- response_body eval
"hello disabled\n"


=== TEST 9: cache fill stays raw while cache-hit output can be transformed
--- main_config
thread_pool default threads=2 max_queue=65536;
--- http_config
proxy_cache_path /tmp/ngx-wasm-cache levels=1:2 keys_zone=wasm_cache:1m max_size=10m inactive=1h use_temp_path=off;

server {
    listen 1985;
    server_name upstream;

    location /origin {
        return 200 "cached body";
    }
}
--- config eval
qq{
    location /filtered {
        aio threads;
        wasm_body_filter_file_chunk_size 4;
        proxy_cache wasm_cache;
        proxy_cache_key shared-cache-key;
        add_header X-Cache \$upstream_cache_status always;
        body_filter_by_wasm @{[ TestWasm::resp_body_upper_wasm() ]} on_body;
        proxy_pass http://127.0.0.1:1985/origin;
    }

    location /raw {
        aio threads;
        proxy_cache wasm_cache;
        proxy_cache_key shared-cache-key;
        add_header X-Cache \$upstream_cache_status always;
        proxy_pass http://127.0.0.1:1985/origin;
    }
}
--- request eval
[
    "GET /filtered",
    "GET /raw",
    "GET /filtered",
]
--- error_code eval
[
    200,
    200,
    200,
]
--- response_body eval
[
    "CACHED BODY",
    "cached body",
    "CACHED BODY",
]


=== TEST 10: body_filter_by_wasm can keep guest state across chunk callbacks
--- main_config
thread_pool default threads=2 max_queue=65536;
--- user_files
>>> files/window.txt
12ab34
--- config eval
qq{
    location /files/ {
        root html;
        aio threads;
        wasm_body_filter_file_chunk_size 3;
        body_filter_by_wasm @{[ TestWasm::resp_body_window_ab_to_x_wasm() ]} on_body;
    }
}
--- request
GET /files/window.txt
--- error_code: 200
--- response_body eval
"12X34\n"
