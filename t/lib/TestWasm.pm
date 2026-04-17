package TestWasm;

use strict;
use warnings;

use Exporter qw(import);
use File::Basename qw(dirname);
use File::Spec;

our @EXPORT_OK = qw(
    wasm_root
    hello_world_wasm
    fuel_yield_wasm
    fuel_yield_rust_wasm
    fuel_multi_yield_wasm
    manual_yield_wasm
    multi_yield_wasm
    req_header_set_wasm
    req_header_set_yield_wasm
    req_header_set_only_wasm
    req_header_echo_wasm
    req_body_echo_wasm
    var_get_request_uri_wasm
    var_set_only_wasm
    var_get_missing_wasm
    var_set_readonly_wasm
    var_set_forbidden_wasm
    time_basic_wasm
    time_short_buf_wasm
    time_log_wasm
    shm_roundtrip_wasm
    shm_set_wasm
    shm_set_only_wasm
    shm_get_wasm
    shm_delete_wasm
    shm_error_check_wasm
    shm_rich_wasm
    metric_counter_inc_wasm
    metric_gauge_ops_wasm
    metric_unknown_wasm
    metric_log_inc_wasm
    hello_world_reload_wasm
    resp_header_set_wasm
    resp_header_echo_wasm
    resp_location_set_wasm
    resp_status_set_wasm
    log_status_wasm
    log_set_status_wasm
    resp_body_upper_wasm
    resp_body_append_eof_wasm
    resp_body_window_ab_to_x_wasm
    access_auth_gate_wasm
    balancer_select_header_wasm
    balancer_noop_wasm
    balancer_invalid_peer_wasm
    balancer_yield_wasm
    ssl_select_cert_wasm
    ssl_reject_blocked_wasm
    subreq_body_echo_wasm
    subreq_header_echo_wasm
    subreq_method_post_wasm
    subreq_status_404_wasm
    subreq_rewrite_auth_wasm
    subreq_forbidden_wasm
    missing_export_wasm
    missing_memory_wasm
    guest_trap_wasm
    nonzero_return_wasm
    fuel_exhaust_wasm
);

sub wasm_root {
    my $root;

    if (defined $ENV{NGX_WASM_ROOT} && length $ENV{NGX_WASM_ROOT}) {
        return $ENV{NGX_WASM_ROOT};
    }

    $root = File::Spec->rel2abs(
        File::Spec->catdir(dirname(__FILE__), '..', '..')
    );

    return $root;
}

sub hello_world_wasm {
    return wasm_root() . "/wasm/http-guests/build/hello_world.wasm";
}

sub fuel_yield_wasm {
    return wasm_root() . "/wasm/http-guests/src/fuel_yield.wat";
}

sub fuel_yield_rust_wasm {
    return wasm_root() . "/wasm/http-guests/build/fuel_yield_rust.wasm";
}

sub fuel_multi_yield_wasm {
    return wasm_root() . "/wasm/http-guests/src/fuel_multi_yield.wat";
}

sub manual_yield_wasm {
    return wasm_root() . "/wasm/http-guests/src/manual_yield.wat";
}

sub multi_yield_wasm {
    return wasm_root() . "/wasm/http-guests/src/multi_yield.wat";
}

sub req_header_set_wasm {
    return wasm_root() . "/wasm/http-guests/src/req_header_set.wat";
}

sub req_header_set_yield_wasm {
    return wasm_root() . "/wasm/http-guests/src/req_header_set_yield.wat";
}

sub req_header_set_only_wasm {
    return wasm_root() . "/wasm/http-guests/src/req_header_set_only.wat";
}

sub req_header_echo_wasm {
    return wasm_root() . "/wasm/http-guests/src/req_header_echo.wat";
}

sub req_body_echo_wasm {
    return wasm_root() . "/wasm/http-guests/src/req_body_echo.wat";
}

sub var_get_request_uri_wasm {
    return wasm_root() . "/wasm/http-guests/src/var_get_request_uri.wat";
}

sub var_set_only_wasm {
    return wasm_root() . "/wasm/http-guests/src/var_set_only.wat";
}

sub var_get_missing_wasm {
    return wasm_root() . "/wasm/http-guests/src/var_get_missing.wat";
}

sub var_set_readonly_wasm {
    return wasm_root() . "/wasm/http-guests/src/var_set_readonly.wat";
}

sub var_set_forbidden_wasm {
    return wasm_root() . "/wasm/http-guests/src/var_set_forbidden.wat";
}

sub time_basic_wasm {
    return wasm_root() . "/wasm/http-guests/src/time_basic.wat";
}

sub time_short_buf_wasm {
    return wasm_root() . "/wasm/http-guests/src/time_short_buf.wat";
}

sub time_log_wasm {
    return wasm_root() . "/wasm/http-guests/src/time_log.wat";
}

sub shm_roundtrip_wasm {
    return wasm_root() . "/wasm/http-guests/src/shm_roundtrip.wat";
}

sub shm_set_wasm {
    return wasm_root() . "/wasm/http-guests/src/shm_set.wat";
}

sub shm_set_only_wasm {
    return wasm_root() . "/wasm/http-guests/src/shm_set_only.wat";
}

sub shm_get_wasm {
    return wasm_root() . "/wasm/http-guests/src/shm_get.wat";
}

sub shm_delete_wasm {
    return wasm_root() . "/wasm/http-guests/src/shm_delete.wat";
}

sub shm_error_check_wasm {
    return wasm_root() . "/wasm/http-guests/build/shm_error_check.wasm";
}

sub shm_rich_wasm {
    return wasm_root() . "/wasm/http-guests/src/shm_rich.wat";
}

sub metric_counter_inc_wasm {
    return wasm_root() . "/wasm/http-guests/src/metric_counter_inc.wat";
}

sub metric_gauge_ops_wasm {
    return wasm_root() . "/wasm/http-guests/src/metric_gauge_ops.wat";
}

sub metric_unknown_wasm {
    return wasm_root() . "/wasm/http-guests/src/metric_unknown.wat";
}

sub metric_log_inc_wasm {
    return wasm_root() . "/wasm/http-guests/src/metric_log_inc.wat";
}

sub hello_world_reload_wasm {
    return wasm_root() . "/wasm/http-guests/src/hello_world_reload.wat";
}

sub resp_header_set_wasm {
    return wasm_root() . "/wasm/http-guests/src/resp_header_set.wat";
}

sub resp_header_echo_wasm {
    return wasm_root() . "/wasm/http-guests/src/resp_header_echo.wat";
}

sub resp_location_set_wasm {
    return wasm_root() . "/wasm/http-guests/src/resp_location_set.wat";
}

sub resp_status_set_wasm {
    return wasm_root() . "/wasm/http-guests/src/resp_status_set.wat";
}

sub log_status_wasm {
    return wasm_root() . "/wasm/http-guests/src/log_status.wat";
}

sub log_set_status_wasm {
    return wasm_root() . "/wasm/http-guests/src/log_set_status.wat";
}

sub resp_body_upper_wasm {
    return wasm_root() . "/wasm/http-guests/src/resp_body_upper.wat";
}

sub resp_body_append_eof_wasm {
    return wasm_root() . "/wasm/http-guests/src/resp_body_append_eof.wat";
}

sub resp_body_window_ab_to_x_wasm {
    return wasm_root() . "/wasm/http-guests/src/resp_body_window_ab_to_x.wat";
}

sub access_auth_gate_wasm {
    return wasm_root() . "/wasm/http-guests/build/access_auth_gate.wasm";
}

sub balancer_select_header_wasm {
    return wasm_root() . "/wasm/http-guests/src/balancer_select_header.wat";
}

sub balancer_noop_wasm {
    return wasm_root() . "/wasm/http-guests/src/balancer_noop.wat";
}

sub balancer_invalid_peer_wasm {
    return wasm_root() . "/wasm/http-guests/src/balancer_invalid_peer.wat";
}

sub balancer_yield_wasm {
    return wasm_root() . "/wasm/http-guests/src/balancer_yield.wat";
}

sub ssl_select_cert_wasm {
    return wasm_root() . "/wasm/http-guests/build/ssl_select_cert.wasm";
}

sub ssl_reject_blocked_wasm {
    return wasm_root() . "/wasm/http-guests/build/ssl_reject_blocked.wasm";
}

sub subreq_body_echo_wasm {
    return wasm_root() . "/wasm/http-guests/src/subreq_body_echo.wat";
}

sub subreq_header_echo_wasm {
    return wasm_root() . "/wasm/http-guests/src/subreq_header_echo.wat";
}

sub subreq_method_post_wasm {
    return wasm_root() . "/wasm/http-guests/src/subreq_method_post.wat";
}

sub subreq_status_404_wasm {
    return wasm_root() . "/wasm/http-guests/src/subreq_status_404.wat";
}

sub subreq_rewrite_auth_wasm {
    return wasm_root() . "/wasm/http-guests/src/subreq_rewrite_auth.wat";
}

sub subreq_forbidden_wasm {
    return wasm_root() . "/wasm/http-guests/src/subreq_forbidden.wat";
}

sub missing_export_wasm {
    return wasm_root() . "/wasm/failures/build/missing_export.wasm";
}

sub missing_memory_wasm {
    return wasm_root() . "/wasm/failures/src/missing_memory.wat";
}

sub guest_trap_wasm {
    return wasm_root() . "/wasm/failures/build/guest_trap.wasm";
}

sub nonzero_return_wasm {
    return wasm_root() . "/wasm/failures/build/nonzero_return.wasm";
}

sub fuel_exhaust_wasm {
    return wasm_root() . "/wasm/failures/src/fuel_exhaust.wat";
}

1;
