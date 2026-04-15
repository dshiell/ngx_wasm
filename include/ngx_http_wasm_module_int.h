#ifndef _NGX_HTTP_WASM_MODULE_INT_H_INCLUDED_
#define _NGX_HTTP_WASM_MODULE_INT_H_INCLUDED_

#include <ngx_http_wasm_runtime.h>

typedef struct ngx_http_wasm_ctx_s ngx_http_wasm_ctx_t;
typedef struct ngx_http_wasm_worker_timer_s ngx_http_wasm_worker_timer_t;
typedef struct ngx_http_wasm_worker_state_s ngx_http_wasm_worker_state_t;
typedef void (*ngx_http_wasm_write_event_handler_pt)(ngx_http_request_t *r);

struct ngx_http_wasm_worker_timer_s {
    ngx_event_t event;
    ngx_http_wasm_worker_state_t *worker_state;
    ngx_http_wasm_exec_ctx_t exec;
    ngx_http_wasm_phase_conf_t phase;
    ngx_str_t export_name;
    ngx_int_t timer_id;
    ngx_msec_t delay;
    ngx_flag_t repeat;
    ngx_flag_t active;
};

struct ngx_http_wasm_worker_state_s {
    ngx_cycle_t *cycle;
    ngx_pool_t *pool;
    ngx_log_t *log;
    ngx_http_wasm_main_conf_t *main_conf;
    ngx_array_t *timers;
    ngx_flag_t shutting_down;
};

struct ngx_http_wasm_ctx_s {
    ngx_http_wasm_exec_ctx_t exec;
    ngx_http_wasm_exec_ctx_t header_filter_exec;
    ngx_http_wasm_exec_ctx_t body_filter_exec;
    ngx_http_wasm_exec_ctx_t log_exec;
    ngx_uint_t header_filter_exec_set;
    ngx_uint_t body_filter_exec_set;
    ngx_uint_t log_exec_set;
    ngx_uint_t waiting;
    ngx_uint_t request_body_reading;
    ngx_uint_t request_body_ready;
    ngx_uint_t request_body_async;
    ngx_int_t request_body_status;
    ngx_chain_t *body_filter_pending;
    ngx_chain_t **body_filter_pending_last;
    ngx_chain_t *body_filter_busy;
    ngx_chain_t *body_filter_free;
    ngx_thread_task_t *body_filter_thread_task;
    ngx_file_t body_filter_file;
    u_char *body_filter_read_buf;
    size_t body_filter_read_size;
    off_t body_filter_read_offset;
    ngx_uint_t body_filter_waiting;
    ngx_uint_t body_filter_chunk_flush;
    ngx_uint_t body_filter_chunk_sync;
    ngx_uint_t body_filter_chunk_last_buf;
    ngx_uint_t body_filter_chunk_last_in_chain;
    ngx_http_wasm_write_event_handler_pt body_filter_saved_write_handler;
};

extern ngx_module_t ngx_http_wasm_module;

ngx_int_t ngx_http_wasm_header_filter_init_process(void);
ngx_int_t ngx_http_wasm_header_filter_handler(ngx_http_request_t *r);
ngx_int_t ngx_http_wasm_body_filter_init_process(void);
ngx_int_t ngx_http_wasm_body_filter_handler(ngx_http_request_t *r,
                                            ngx_chain_t *in);
ngx_int_t ngx_http_wasm_prepare_request_body(ngx_http_request_t *r,
                                             ngx_http_wasm_conf_t *wcf,
                                             ngx_http_wasm_ctx_t *ctx);
void ngx_http_wasm_resume_handler(ngx_http_request_t *r);
void ngx_http_wasm_cleanup_ctx(void *data);
ngx_int_t ngx_http_wasm_worker_timer_set(ngx_http_wasm_exec_ctx_t *ctx,
                                         ngx_int_t timer_id,
                                         ngx_msec_t delay,
                                         ngx_flag_t repeat,
                                         const u_char *export_name,
                                         size_t export_name_len);
ngx_int_t ngx_http_wasm_worker_timer_cancel(ngx_http_wasm_exec_ctx_t *ctx,
                                            ngx_int_t timer_id);

#endif /* _NGX_HTTP_WASM_MODULE_INT_H_INCLUDED_ */
