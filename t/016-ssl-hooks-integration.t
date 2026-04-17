use strict;
use warnings;

use File::Spec;
use Test::More;

use lib 't/lib';
use TestWasm ();
use TestWasmIntegration;

sub random_port {
    return 20000 + int(rand(10000));
}

subtest 'ssl hooks can select certificates and reject blocked SNI' => sub {
    my $port = random_port();
    my $nginx = TestWasmIntegration->new(root => TestWasm::wasm_root());
    my $crt = File::Spec->catfile($nginx->prefix(), 'default.local.crt');
    my $key = File::Spec->catfile($nginx->prefix(), 'default.local.key');
    my $failed = 0;

    eval {
        my ($ok, undef, $stderr) = $nginx->capture_cmd(
            'openssl', 'req', '-x509', '-newkey', 'rsa:2048',
            '-keyout', $key,
            '-out', $crt,
            '-sha256', '-days', '3650',
            '-nodes',
            '-subj', '/CN=default.local',
        );
        ok($ok, 'default certificate generated') or do { diag $stderr; $failed = 1; };

        $nginx->write_conf(<<"EOF");
worker_processes 1;
master_process off;
error_log logs/error.log notice;
pid logs/nginx.pid;

events {
    worker_connections 16;
}

http {
    server {
        listen $port ssl;
        server_name _;

        ssl_certificate $crt;
        ssl_certificate_key $key;

        ssl_client_hello_by_wasm @{[ TestWasm::ssl_reject_blocked_wasm() ]} on_ssl_client_hello;
        ssl_certificate_by_wasm @{[ TestWasm::ssl_select_cert_wasm() ]} on_ssl_certificate;

        location / {
            return 200 "ok";
        }
    }
}
EOF

        ($ok, undef, $stderr) = $nginx->nginx_test();
        ok($ok, 'config test succeeds') or do { diag $stderr; $failed = 1; };

        $nginx->start();
        ok($nginx->wait_for_pid_file(timeout => 3), 'pid file created') or $failed = 1;

        ($ok, my $subject) = $nginx->openssl_subject(
            port => $port,
            server_name => 'default.local',
        );
        ok($ok, 'can fetch default certificate') or $failed = 1;
        like($subject, qr/CN\s*=\s*default\.local|CN=default\.local/, 'default certificate subject matches')
            or $failed = 1;

        ($ok, $subject) = $nginx->openssl_subject(
            port => $port,
            server_name => 'selected.local',
        );
        ok($ok, 'can fetch selected certificate') or $failed = 1;
        like($subject, qr/CN\s*=\s*selected\.local|CN=selected\.local/, 'selected certificate subject matches')
            or $failed = 1;

        ($ok, my $stdout, $stderr) = $nginx->capture_cmd(
            'openssl', 's_client',
            '-connect', "127.0.0.1:$port",
            '-servername', 'blocked.local',
        );
        like(
            $stdout . $stderr,
            qr/alert|handshake failure|unrecognized name/i,
            'blocked.local is rejected during handshake',
        ) or $failed = 1;
    };

    if ($@) {
        diag $@;
        $failed = 1;
    }

    diag $nginx->error_log() if $failed;
    $nginx->cleanup();
};

done_testing();
