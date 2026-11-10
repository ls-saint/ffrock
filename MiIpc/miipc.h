#ifndef MIIPC_H_
#define MIIPC_H_

#include <stdint.h>

#include "Soc/IHard.h"

#if defined(_WIN32)
#define MIIPC_API
#else
#define MIIPC_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct miipc_config {
    const char* http_listen_url;
    const char* storage_root;
    const char* log_path;
    const char* sn;
    const char* pricode;
    const char* bindcode;
    int storage_queue_limit;
    int signal_consumer_queue_limit;
    /* Controls whether incoming samples are recorded. */
    int storage_enable;
    int enable_log;
    int enable_http;
    int enable_signal;
    int enable_broadcast;
    /* Retained for API compatibility. On non-minimal MiIpc builds the
     * recording worker always starts regardless of this field. */
    int enable_storage;
    int enable_rtsp;
    int enable_onvif;
    int enable_hardware;
    int install_signal_handlers;
    int install_fatal_signal_handlers;
    const char* rtsp_listen_ip;
    uint16_t rtsp_listen_port;
    const char* rtsp_advertise_ip;
    const char* rtsp_stream_path;
    int rtsp_max_clients;
    int rtsp_source_chn;
    int rtsp_enable_udp;
    int rtsp_enable_tcp_interleaved;
    int rtsp_sub_enable;
    uint16_t rtsp_sub_listen_port;
    const char* rtsp_sub_stream_path;
    int rtsp_sub_source_chn;
    const char* onvif_listen_ip;
    uint16_t onvif_http_port;
    const char* onvif_advertise_ip;
    const char* onvif_device_path;
    const char* onvif_device_service_url;
    const char* onvif_rtsp_url;
    const char* onvif_uuid;
    const char* onvif_manufacturer;
    const char* onvif_model;
    const char* onvif_firmware_version;
    const char* onvif_serial_number;
    const char* onvif_hardware_id;
    const char* onvif_device_name;
    const char* onvif_scopes;
    int onvif_enable_http;
    int onvif_enable_discovery;
    uint16_t onvif_discovery_port;
    int onvif_max_http_clients;
    uint32_t onvif_thread_stack_size;
    uint32_t run_poll_sleep_ms;
    IHardSoc* hard_soc;
    FpOnGetAudio read_audio;
    struct SVideoCfg video_cfg;
    struct SCaptureCfg capture_cfg;
} miipc_config_t;

MIIPC_API void miipc_default_config(miipc_config_t* config);
/* Starts MiIpc services and returns; call miipc_request_stop/miipc_stop from the owner. */
MIIPC_API int miipc_start(const miipc_config_t* config);
MIIPC_API void miipc_request_stop(void);
MIIPC_API void miipc_stop(void);
MIIPC_API int miipc_is_started(void);
MIIPC_API IHardSoc* miipc_get_hard_soc(void);
MIIPC_API int miipc_input_av_stream(int chn, struct Stream_Info* info, int count);
MIIPC_API int miipc_get_rtsp_url(char* url, int url_len);
MIIPC_API int miipc_get_rtsp_url_by_index(int stream_index, char* url, int url_len);
MIIPC_API int miipc_get_onvif_device_service_url(char* url, int url_len);

#ifdef __cplusplus
}
#endif

#endif
