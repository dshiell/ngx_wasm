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

=== TEST 1: content_by_wasm can increment a declared counter
--- http_config
wasm_metrics_zone observability 1m;
wasm_counter requests_total;
--- config eval
qq{
    location /inc {
        content_by_wasm @{[ TestWasm::metric_counter_inc_wasm() ]} on_content;
    }

    location /metrics {
        wasm_metrics;
    }
}
--- request eval
[
    "GET /inc",
    "GET /metrics",
]
--- error_code eval
[
    200,
    200,
]
--- response_body_like eval
[
    qr/^ok$/,
    qr/# TYPE requests_total counter\nrequests_total 1\n$/,
]


=== TEST 2: content_by_wasm can set and add a declared gauge
--- http_config
wasm_metrics_zone observability 1m;
wasm_gauge in_flight;
--- config eval
qq{
    location /gauge {
        content_by_wasm @{[ TestWasm::metric_gauge_ops_wasm() ]} on_content;
    }

    location /metrics {
        wasm_metrics;
    }
}
--- request eval
[
    "GET /gauge",
    "GET /metrics",
]
--- error_code eval
[
    200,
    200,
]
--- response_body_like eval
[
    qr/^ok$/,
    qr/# TYPE in_flight gauge\nin_flight 7\n$/,
]


=== TEST 3: log_by_wasm can increment a declared counter
--- http_config
wasm_metrics_zone observability 1m;
wasm_counter logged_total;
--- config eval
qq{
    location /logged {
        log_by_wasm @{[ TestWasm::metric_log_inc_wasm() ]} on_log;
        return 200 "ok";
    }

    location /metrics {
        wasm_metrics;
    }
}
--- request eval
[
    "GET /logged",
    "GET /metrics",
]
--- error_code eval
[
    200,
    200,
]
--- response_body_like eval
[
    qr/^ok$/,
    qr/# TYPE logged_total counter\nlogged_total 1\n$/,
]


=== TEST 4: duplicate metric definitions fail at config time
--- http_config
wasm_metrics_zone observability 1m;
wasm_counter requests_total;
wasm_gauge requests_total;
--- config
location /metrics {
    wasm_metrics;
}
--- request
GET /metrics
--- must_die
--- error_log eval
qr/metric is duplicate/


=== TEST 5: declaring metrics without a metrics zone fails at config time
--- http_config
wasm_counter requests_total;
--- config
location /metrics {
    wasm_metrics;
}
--- request
GET /metrics
--- must_die
--- error_log eval
qr/"wasm_metrics_zone" is required/


=== TEST 6: unknown metrics fail cleanly in guest code
--- http_config
wasm_metrics_zone observability 1m;
wasm_counter requests_total;
--- config eval
qq{
    location /unknown {
        content_by_wasm @{[ TestWasm::metric_unknown_wasm() ]} on_content;
    }
}
--- request
GET /unknown
--- error_code: 200
--- response_body eval
"error"
