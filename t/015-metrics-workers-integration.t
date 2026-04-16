use strict;
use warnings;

use Test::More;

use lib 't/lib';
use TestWasm ();
use TestWasmIntegration;

sub random_port {
    return 20000 + int(rand(10000));
}

sub run_parallel_gets {
    my ($url, $count, $parallelism) = @_;
    my @children;
    my $started = 0;
    my $failed = 0;

    while ($started < $count || @children) {
        while ($started < $count && @children < $parallelism) {
            my $pid = fork();
            die "fork failed: $!" unless defined $pid;

            if ($pid == 0) {
                exec 'curl', '-sf', $url;
                exit 127;
            }

            push @children, $pid;
            $started++;
        }

        my $done = wait();
        my $status = $?;
        @children = grep { $_ != $done } @children;
        $failed = 1 if $done > 0 && $status != 0;
    }

    return !$failed;
}

subtest 'metric updates remain shared across workers' => sub {
    my $port = random_port();
    my $requests = 20;
    my $parallelism = 8;
    my $expected = "# TYPE requests_total counter\nrequests_total $requests\n";
    my $nginx = TestWasmIntegration->new(root => TestWasm::wasm_root());
    my $failed = 0;

    eval {
        $nginx->write_conf(<<"EOF");
worker_processes 2;
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
            url => "http://127.0.0.1:$port/metrics",
        );
        ok($ok, 'metrics endpoint is ready') or do { diag $body; $failed = 1; };

        ok(
            run_parallel_gets("http://127.0.0.1:$port/inc", $requests, $parallelism),
            'parallel increment requests succeed',
        ) or $failed = 1;

        ($ok, $body) = $nginx->wait_for_body(
            url => "http://127.0.0.1:$port/metrics",
            expected => $expected,
        );
        ok($ok, 'metrics total matches across workers') or do { diag $body; $failed = 1; };
    };

    if ($@) {
        diag $@;
        $failed = 1;
    }

    diag $nginx->error_log() if $failed;
    $nginx->cleanup();
};

done_testing();
