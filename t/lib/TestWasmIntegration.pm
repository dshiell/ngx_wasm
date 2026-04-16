package TestWasmIntegration;

use strict;
use warnings;

use Carp qw(croak);
use File::Path qw(make_path remove_tree);
use File::Spec;
use File::Temp qw(tempdir);
use IPC::Open3 qw(open3);
use Symbol qw(gensym);
use Time::HiRes qw(sleep);

sub new {
    my ($class, %args) = @_;
    my $root = $args{root} // $ENV{NGX_WASM_ROOT}
        // croak('root is required');
    my $nginx_dir = $args{nginx_dir}
        // $ENV{NGINX_DIR}
        // File::Spec->catdir($root, '..', 'nginx');
    my $nginx_bin = $args{nginx_bin}
        // $ENV{TEST_NGINX_BINARY}
        // File::Spec->catfile($nginx_dir, 'objs', 'nginx');
    my $build_info = $args{build_info}
        // $ENV{NGINX_BUILD_INFO}
        // File::Spec->catfile($nginx_dir, 'objs', 'ngx_wasm_build.env');
    my $prefix = $args{prefix}
        // tempdir('ngx-wasm-test.XXXXXX', TMPDIR => 1, CLEANUP => 0);
    my $self = bless {
        root => $root,
        nginx_dir => $nginx_dir,
        nginx_bin => $nginx_bin,
        build_info => $build_info,
        prefix => $prefix,
        conf_path => File::Spec->catfile($prefix, 'nginx.conf'),
        log_dir => File::Spec->catdir($prefix, 'logs'),
        pid_file => File::Spec->catfile($prefix, 'logs', 'nginx.pid'),
        master_pid => undef,
        keep => $args{keep} // 0,
        build_env => {},
    }, $class;

    make_path($self->{log_dir});
    $self->_load_build_info();

    return $self;
}

sub prefix {
    my ($self) = @_;
    return $self->{prefix};
}

sub conf_path {
    my ($self) = @_;
    return $self->{conf_path};
}

sub pid_file {
    my ($self) = @_;
    return $self->{pid_file};
}

sub error_log_path {
    my ($self) = @_;
    return File::Spec->catfile($self->{log_dir}, 'error.log');
}

sub write_conf {
    my ($self, $conf) = @_;
    open my $out, '>', $self->{conf_path}
        or croak("cannot open $self->{conf_path} for writing: $!");
    print {$out} $conf;
    close $out or croak("cannot close $self->{conf_path}: $!");
}

sub write_file {
    my ($self, $path, $content) = @_;
    my ($volume, $dirs, undef) = File::Spec->splitpath($path);
    if (defined $dirs && length $dirs) {
        make_path(File::Spec->catpath($volume, $dirs, ''));
    }
    open my $out, '>', $path or croak("cannot open $path for writing: $!");
    binmode $out;
    print {$out} $content;
    close $out or croak("cannot close $path: $!");
}

sub copy_file {
    my ($self, $src, $dst) = @_;
    open my $in, '<', $src or croak("cannot open $src for reading: $!");
    binmode $in;
    local $/;
    my $content = <$in>;
    close $in or croak("cannot close $src: $!");
    $self->write_file($dst, $content);
}

sub nginx_test {
    my ($self) = @_;
    return $self->_run_nginx_oneshot('-t', '-p', $self->{prefix}, '-c', $self->{conf_path});
}

sub start {
    my ($self) = @_;

    my $pid = fork();
    croak("fork failed: $!") unless defined $pid;

    if ($pid == 0) {
        open STDIN, '<', '/dev/null' or die "open stdin failed: $!";
        open STDOUT, '>', '/dev/null' or die "open stdout failed: $!";
        open STDERR, '>', '/dev/null' or die "open stderr failed: $!";
        exec { $self->{nginx_bin} } $self->{nginx_bin},
            '-p', $self->{prefix},
            '-c', $self->{conf_path},
            '-g', 'daemon off;';
        die "exec nginx failed: $!";
    }

    $self->{master_pid} = $pid;
    return $pid;
}

sub wait_for_pid_file {
    my ($self, %args) = @_;
    my $timeout = $args{timeout} // 2;
    my $attempts = int($timeout / 0.1);
    my $i;

    for ($i = 0; $i < $attempts; $i++) {
        return 1 if -f $self->{pid_file};
        sleep 0.1;
    }

    return 0;
}

sub http_get_body {
    my ($self, $url, %args) = @_;
    my @cmd = ('curl', '-sf');
    my $headers = $args{headers} // [];
    my $header;

    for $header (@{$headers}) {
        push @cmd, '-H', $header;
    }

    push @cmd, $url;
    return $self->_capture_cmd(@cmd);
}

sub http_get_response {
    my ($self, $url, %args) = @_;
    my @cmd = ('curl', '-si');
    my $headers = $args{headers} // [];
    my $header;

    for $header (@{$headers}) {
        push @cmd, '-H', $header;
    }

    push @cmd, $url;
    return $self->_capture_cmd(@cmd);
}

sub wait_for_body {
    my ($self, %args) = @_;
    my $url = $args{url} // croak('url is required');
    my $expected = $args{expected};
    my $headers = $args{headers} // [];
    my $timeout = $args{timeout} // 3;
    my $attempts = int($timeout / 0.2);
    my $i;
    my $body = '';

    for ($i = 0; $i < $attempts; $i++) {
        my ($ok, $stdout) = $self->http_get_body($url, headers => $headers);
        if ($ok) {
            $body = $stdout;
            return (1, $body) if !defined $expected || $body eq $expected;
        }
        sleep 0.2;
    }

    return (0, $body);
}

sub reload {
    my ($self) = @_;
    return $self->_run_nginx_oneshot('-p', $self->{prefix}, '-c', $self->{conf_path}, '-s', 'reload');
}

sub quit {
    my ($self) = @_;
    return $self->_run_nginx_oneshot('-p', $self->{prefix}, '-c', $self->{conf_path}, '-s', 'quit');
}

sub stop {
    my ($self) = @_;

    if (-f $self->{pid_file}) {
        $self->quit();
        sleep 1;
    }

    if (defined $self->{master_pid}) {
        waitpid($self->{master_pid}, 0);
        $self->{master_pid} = undef;
    }
}

sub cleanup {
    my ($self) = @_;
    $self->stop();
    return if $self->{keep};
    remove_tree($self->{prefix});
}

sub error_log {
    my ($self) = @_;
    my $path = $self->error_log_path();
    return '' unless -f $path;

    open my $in, '<', $path or croak("cannot open $path for reading: $!");
    local $/;
    my $content = <$in>;
    close $in or croak("cannot close $path: $!");
    return $content;
}

sub openssl_subject {
    my ($self, %args) = @_;
    my $port = $args{port} // croak('port is required');
    my $server_name = $args{server_name} // croak('server_name is required');
    my $attempts = $args{attempts} // 20;
    my $i;

    for ($i = 0; $i < $attempts; $i++) {
        my ($ok, $stdout) = $self->_capture_cmd(
            'openssl', 's_client',
            '-connect', "127.0.0.1:$port",
            '-servername', $server_name,
            '-showcerts',
        );
        if ($ok && $stdout =~ /(-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----)/s) {
            my $cert = $1;
            my ($subject_ok, $subject_out) = $self->_capture_cmd_with_stdin(
                $cert,
                'openssl', 'x509', '-noout', '-subject'
            );
            return (1, $subject_out) if $subject_ok;
        }

        sleep 0.1;
    }

    return (0, '');
}

sub capture_cmd {
    my ($self, @cmd) = @_;
    return $self->_capture_cmd(@cmd);
}

sub _load_build_info {
    my ($self) = @_;
    my $path = $self->{build_info};
    my %env;

    return unless -f $path;

    open my $in, '<', $path or croak("cannot open $path for reading: $!");
    while (my $line = <$in>) {
        chomp $line;
        next unless $line =~ /^([^=]+)=(.*)$/;
        $env{$1} = $2;
    }
    close $in or croak("cannot close $path: $!");

    $self->{build_env} = \%env;
}

sub _sanitize_asan_options_for_oneshot {
    my ($self) = @_;
    my $options = $ENV{ASAN_OPTIONS} // '';
    my @parts;
    my @filtered;

    return 'detect_leaks=0' if $options eq '';

    @parts = split /:/, $options;
    @filtered = grep { $_ !~ /^detect_leaks=/ } @parts;
    push @filtered, 'detect_leaks=0';

    return join ':', @filtered;
}

sub _run_nginx_oneshot {
    my ($self, @args) = @_;
    local %ENV = %ENV;

    if (($self->{build_env}->{BUILD_SANITIZE} // '0') eq '1') {
        $ENV{ASAN_OPTIONS} = $self->_sanitize_asan_options_for_oneshot();
        $ENV{LSAN_OPTIONS} = $self->{build_env}->{LSAN_OPTIONS}
            if exists $self->{build_env}->{LSAN_OPTIONS};
        $ENV{UBSAN_OPTIONS} = $self->{build_env}->{UBSAN_OPTIONS}
            if exists $self->{build_env}->{UBSAN_OPTIONS};
    }

    return $self->_capture_cmd($self->{nginx_bin}, @args);
}

sub _capture_cmd {
    my ($self, @cmd) = @_;
    return $self->_capture_cmd_with_stdin(undef, @cmd);
}

sub _capture_cmd_with_stdin {
    my ($self, $stdin, @cmd) = @_;
    my ($in, $out, $err, $pid, $stdout, $stderr, $wait);

    $err = gensym();
    $pid = open3($in, $out, $err, @cmd);

    if (defined $stdin) {
        print {$in} $stdin;
    }
    close $in;

    {
        local $/;
        $stdout = <$out>;
        $stderr = <$err>;
    }

    close $out;
    close $err;
    $wait = waitpid($pid, 0);

    return ($? == 0, $stdout // '', $stderr // '');
}

1;
