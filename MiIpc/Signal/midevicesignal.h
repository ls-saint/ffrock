#ifndef MIIPC_DEVICE_SIGNAL_H_
#define MIIPC_DEVICE_SIGNAL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mi_device_signal_config {
    const char* server_ip;
    uint16_t server_port;
    uint16_t channel;
    int ctl_ver;
    uint32_t reconnect_ms;
    uint32_t reconnect_max_ms;
    uint32_t connect_timeout_ms;
    int heartbeat_enable;
    uint32_t heartbeat_ms;
    uint32_t worker_idle_sleep_ms;
    uint32_t worker_loop_interval_ms;
} mi_device_signal_config_t;

typedef enum mi_device_signal_status {
    MI_DEVICE_SIGNAL_STATUS_STOPPED = 0,
    MI_DEVICE_SIGNAL_STATUS_CONNECTING = 1,
    MI_DEVICE_SIGNAL_STATUS_CONNECTED = 2,
    MI_DEVICE_SIGNAL_STATUS_RECONNECT_WAIT = 3
} mi_device_signal_status_t;

typedef struct mi_device_signal_stats {
    mi_device_signal_status_t status;
    int started;
    int connected;
    int login_sent;
    uint32_t conid;
    uint32_t connect_attempts;
    uint32_t connect_failures;
    uint32_t disconnects;
    uint32_t reconnects;
    uint32_t last_connect_ms;
    uint32_t next_connect_ms;
    uint32_t current_reconnect_ms;
    int heartbeat_enable;
    uint32_t heartbeat_sent;
    uint32_t heartbeat_ms;
} mi_device_signal_stats_t;

typedef void (*mi_device_signal_message_cb)(const char* data, int len, void* user);
typedef void (*mi_device_signal_status_cb)(mi_device_signal_status_t status, uint32_t conid, const char* reason, void* user);

void mi_device_signal_default_config(mi_device_signal_config_t* cfg);
void mi_device_signal_set_message_callback(mi_device_signal_message_cb callback, void* user);
void mi_device_signal_set_status_callback(mi_device_signal_status_cb callback, void* user);

int mi_device_signal_start(const mi_device_signal_config_t* cfg);
void mi_device_signal_stop(void);
int mi_device_signal_is_connected(void);
int mi_device_signal_send_json(const char* json);
void mi_device_signal_get_stats(mi_device_signal_stats_t* stats);
int mi_device_signal_get_info(char* out, int out_len);

#ifdef __cplusplus
}
#endif

#endif
