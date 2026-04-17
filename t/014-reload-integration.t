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

subtest 'shared memory richer primitives survive reload' => sub {
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

        location /seed {
            content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_seed_counter;
        }

        location /incr {
            content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_incr_counter;
        }
    }
}
EOF

        my ($ok, undef, $stderr) = $nginx->nginx_test();
        ok($ok, 'config test succeeds') or do { diag $stderr; $failed = 1; };
        $nginx->start();
        ok($nginx->wait_for_pid_file(timeout => 3), 'pid file created') or $failed = 1;

        ($ok, my $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/seed",
            expected => '0',
        );
        ok($ok, 'can seed counter before reload') or do { diag $body; $failed = 1; };

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/incr",
            expected => '2',
        );
        ok($ok, 'can increment counter before reload') or do { diag $body; $failed = 1; };

        ($ok, undef, $stderr) = $nginx->reload();
        ok($ok, 'reload succeeds') or do { diag $stderr; $failed = 1; };

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/incr",
            expected => '4',
        );
        ok($ok, 'counter value persists across reload for incr') or do { diag $body; $failed = 1; };
    };

    if ($@) {
        diag $@;
        $failed = 1;
    }
    diag_error_log($nginx) if $failed;
    $nginx->cleanup();
};

subtest 'shared memory ttl expires lazily and non-expired ttl survives reload' => sub {
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

        location /set-short {
            content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_set_ttl_short;
        }

        location /set-long {
            content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_set_ttl_long;
        }

        location /get {
            content_by_wasm @{[ TestWasm::shm_rich_wasm() ]} on_get_ttl;
        }
    }
}
EOF

        my ($ok, undef, $stderr) = $nginx->nginx_test();
        ok($ok, 'config test succeeds') or do { diag $stderr; $failed = 1; };
        $nginx->start();
        ok($nginx->wait_for_pid_file(timeout => 3), 'pid file created') or $failed = 1;

        ($ok, my $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/set-short",
            expected => 'added',
        );
        ok($ok, 'can store short ttl value') or do { diag $body; $failed = 1; };

        select undef, undef, undef, 0.12;

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/get",
            expected => 'missing',
        );
        ok($ok, 'expired ttl key is removed on read') or do { diag $body; $failed = 1; };

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/set-long",
            expected => 'added',
        );
        ok($ok, 'can store long ttl value') or do { diag $body; $failed = 1; };

        ($ok, undef, $stderr) = $nginx->reload();
        ok($ok, 'reload succeeds') or do { diag $stderr; $failed = 1; };

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/get",
            expected => 'second-value',
        );
        ok($ok, 'non-expired ttl key persists across reload') or do { diag $body; $failed = 1; };
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
