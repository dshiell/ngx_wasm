extern int ngx_wasm_log(int level, const char *ptr, int len);
extern int ngx_wasm_resp_set_status(int status);
extern int ngx_wasm_resp_write(const char *ptr, int len);

#define NGX_HTTP_WASM_LOG_NOTICE 6

__attribute__((export_name("on_content")))
int on_content(void) {
    static const char log_message[] = "hello-world guest invoked";
    static const char body[] = "hello from guest wasm\n";

    ngx_wasm_log(NGX_HTTP_WASM_LOG_NOTICE, log_message, sizeof(log_message) - 1);
    ngx_wasm_resp_set_status(200);
    ngx_wasm_resp_write(body, sizeof(body) - 1);

    return 0;
}
