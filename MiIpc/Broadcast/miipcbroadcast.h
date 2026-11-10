#ifndef MIIPC_BROADCAST_H_
#define MIIPC_BROADCAST_H_

#include <stdint.h>
#include "../../MiBase/include/socket/miaddr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_BROADCAST_DEFAULT_PORT 5895u

typedef struct miipc_broadcast_config {
    const char* bind_ip;
    uint16_t port;
    uint32_t idle_sleep_ms;
    uint32_t bind_retry_ms;
    int recv_buffer_size;
} miipc_broadcast_config_t;

typedef struct miipc_broadcast_stats {
    int started;
    uint16_t port;
    uint32_t recv_count;
    uint32_t recv_bytes;
    uint32_t recv_errors;
    uint32_t bind_errors;
    uint32_t response_count;
    uint32_t response_errors;
    int raw_enabled;
    uint32_t raw_recv_count;
    uint32_t raw_send_count;
    uint32_t raw_errors;
} miipc_broadcast_stats_t;

typedef void (*miipc_broadcast_recv_cb)(const uint8_t* data, int len, const mi_SockAddr* from, void* user);

void miipc_broadcast_default_config(miipc_broadcast_config_t* cfg);
void miipc_broadcast_set_callback(miipc_broadcast_recv_cb callback, void* user);
int miipc_broadcast_start(const miipc_broadcast_config_t* cfg);
void miipc_broadcast_stop(void);
void miipc_broadcast_get_stats(miipc_broadcast_stats_t* stats);

#ifdef __cplusplus
}
#endif

#endif
