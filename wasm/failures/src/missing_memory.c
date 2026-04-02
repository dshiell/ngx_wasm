extern int ngx_wasm_resp_set_status(int status);

__attribute__((export_name("on_content")))
int on_content(void) {
    ngx_wasm_resp_set_status(200);
    return 0;
}
