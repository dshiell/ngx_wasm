use strict;
use warnings;

use Test::More;

use lib 't/lib';
use TestWasm ();
use TestWasmIntegration;

sub random_port {
    return 20000 + int(rand(10000));
}

sub diag_error_log {
    my ($nginx) = @_;
    my $log = $nginx->error_log();
    diag $log if length $log;
}

subtest 'shared memory survives reload' => sub {
    my $port = random_port();
    my $nginx = TestWasmIntegration->new(root => TestWasm::wasm_root());
    my $failed = 0;

    eval {
        $nginx->write_conf(<<"EOF");
worker_processes 1;
error_log logs/error.log info;
pid logs/nginx.pid;

events {
    worker_connections 64;
}

http {
    access_log off;
    wasm_shm_zone shared 1m;

    server {
        listen $port;
        server_name localhost;

        location /set {
            content_by_wasm @{[ TestWasm::shm_set_wasm() ]} on_content;
        }

        location /get {
            content_by_wasm @{[ TestWasm::shm_get_wasm() ]} on_content;
        }
    }
}
EOF

        my ($ok, undef, $stderr) = $nginx->nginx_test();
        ok($ok, 'config test succeeds') or do { diag $stderr; $failed = 1; };
        $nginx->start();
        ok($nginx->wait_for_pid_file(timeout => 3), 'pid file created') or $failed = 1;

        ($ok, my $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/set",
            expected => 'stored',
        );
        ok($ok, 'can store shared value before reload') or do { diag $body; $failed = 1; };

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/get",
            expected => 'persisted-value',
        );
        ok($ok, 'can read shared value before reload') or do { diag $body; $failed = 1; };

        ($ok, undef, $stderr) = $nginx->reload();
        ok($ok, 'reload succeeds') or do { diag $stderr; $failed = 1; };

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/get",
            expected => 'persisted-value',
        );
        ok($ok, 'shared value persists across reload') or do { diag $body; $failed = 1; };
    };

    if ($@) {
        diag $@;
        $failed = 1;
    }
    diag_error_log($nginx) if $failed;
    $nginx->cleanup();
};

subtest 'metrics survive reload' => sub {
    my $port = random_port();
    my $nginx = TestWasmIntegration->new(root => TestWasm::wasm_root());
    my $expected = "# TYPE requests_total counter\nrequests_total 1\n";
    my $failed = 0;

    eval {
        $nginx->write_conf(<<"EOF");
worker_processes 1;
error_log logs/error.log info;
pid logs/nginx.pid;

events {
    worker_connections 64;
}

http {
    access_log off;
    wasm_metrics_zone observability 1m;
    wasm_counter requests_total;

    server {
        listen $port;
        server_name localhost;

        location /inc {
            content_by_wasm @{[ TestWasm::metric_counter_inc_wasm() ]} on_content;
        }

        location /metrics {
            wasm_metrics;
        }
    }
}
EOF

        my ($ok, undef, $stderr) = $nginx->nginx_test();
        ok($ok, 'config test succeeds') or do { diag $stderr; $failed = 1; };
        $nginx->start();
        ok($nginx->wait_for_pid_file(timeout => 3), 'pid file created') or $failed = 1;

        ($ok, my $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/inc",
            expected => 'ok',
        );
        ok($ok, 'counter increment request succeeds') or do { diag $body; $failed = 1; };

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/metrics",
            expected => $expected,
        );
        ok($ok, 'metrics endpoint shows increment before reload') or do { diag $body; $failed = 1; };

        ($ok, undef, $stderr) = $nginx->reload();
        ok($ok, 'reload succeeds') or do { diag $stderr; $failed = 1; };

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/metrics",
            expected => $expected,
        );
        ok($ok, 'metric value persists across reload') or do { diag $body; $failed = 1; };
    };

    if ($@) {
        diag $@;
        $failed = 1;
    }
    diag_error_log($nginx) if $failed;
    $nginx->cleanup();
};

done_testing();
