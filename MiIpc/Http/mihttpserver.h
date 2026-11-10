#ifndef MIIPC_HTTP_SERVER_H_
#define MIIPC_HTTP_SERVER_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*mi_http_server_onvif_handler_t)(const char* method,
                                              const char* uri,
                                              const char* body,
                                              int body_len,
                                              char* response,
                                              int response_len,
                                              int* status,
                                              const char** content_type,
                                              void* user_data);

void mi_http_server_set_onvif_handler(const char* path,
                                      mi_http_server_onvif_handler_t handler,
                                      void* user_data);
int mi_http_server_start(const char* listen_url);
void mi_http_server_stop(void);
int mi_http_server_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
