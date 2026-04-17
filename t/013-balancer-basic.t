use strict;
use warnings;

use lib 't/lib';
use TestWasm ();
use Test::Nginx::Socket -Base;

repeat_each(1);
plan tests => repeat_each() * blocks() * 3;

our $BalancerHttpConfig = qq{
upstream wasm_backend_select {
    server 127.0.0.1:1985;
    server 127.0.0.1:1986;
    balancer_by_wasm @{[ TestWasm::balancer_select_header_wasm() ]} on_balancer;
}

upstream wasm_backend_noop {
    server 127.0.0.1:1985;
    balancer_by_wasm @{[ TestWasm::balancer_noop_wasm() ]} on_balancer;
}

upstream wasm_backend_invalid {
    server 127.0.0.1:1985;
    server 127.0.0.1:1986;
    balancer_by_wasm @{[ TestWasm::balancer_invalid_peer_wasm() ]} on_balancer;
}

upstream wasm_backend_yield {
    server 127.0.0.1:1985;
    balancer_by_wasm @{[ TestWasm::balancer_yield_wasm() ]} on_balancer;
}

server {
    listen 1985;
    server_name backend_a;

    location / {
        return 200 "backend-a";
    }
}

server {
    listen 1986;
    server_name backend_b;

    location / {
        return 200 "backend-b";
    }
}
};

run_tests();

__DATA__

=== TEST 1: balancer_by_wasm selects the default peer
--- http_config eval
$::BalancerHttpConfig
--- config
location /select-a {
    proxy_pass http://wasm_backend_select;
}
--- request
GET /select-a
--- more_headers
X-Route: a
--- error_code: 200
--- response_body: backend-a
--- error_log eval
qr/balancer handler/


=== TEST 2: balancer_by_wasm can route to an alternate peer
--- http_config eval
$::BalancerHttpConfig
--- config
location /select-b {
    proxy_pass http://wasm_backend_select;
}
--- request
GET /select-b
--- more_headers
X-Route: b
--- error_code: 200
--- response_body: backend-b
--- error_log eval
qr/balancer handler/


=== TEST 3: balancer_by_wasm falls back cleanly when no peer is selected
--- http_config eval
$::BalancerHttpConfig
--- config
location /noop {
    proxy_pass http://wasm_backend_noop;
}
--- request
GET /noop
--- error_code: 200
--- response_body: backend-a
--- error_log eval
qr/balancer handler/


=== TEST 4: balancer_by_wasm rejects invalid peer indexes
--- http_config eval
$::BalancerHttpConfig
--- config
location /invalid {
    proxy_pass http://wasm_backend_invalid;
}
--- request
GET /invalid
--- error_code: 500
--- response_body_like: 500 Internal Server Error
--- error_log eval
qr/invalid balancer peer index/


=== TEST 5: balancer_by_wasm disallows yielding
--- http_config eval
$::BalancerHttpConfig
--- config
location /yield {
    proxy_pass http://wasm_backend_yield;
}
--- request
GET /yield
--- error_code: 500
--- response_body_like: 500 Internal Server Error
--- error_log eval
qr/ngx_wasm_yield not allowed in this phase/
