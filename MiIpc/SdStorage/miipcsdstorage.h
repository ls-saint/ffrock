#ifndef MIIPC_SD_STORAGE_H
#define MIIPC_SD_STORAGE_H

#include "../Soc/IHard.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_SD_STORAGE_DEFAULT_QUEUE_LIMIT 32
#define MIIPC_SD_STORAGE_MAX_PACKET_SIZE (1024 * 1024)

typedef struct miipc_sd_storage_stats {
    int started;
    int enabled;
    int queue_count;
    int queue_limit;
    uint32_t posted;
    uint32_t written;
    uint32_t dropped;
    uint32_t write_failed;
    int available;
} miipc_sd_storage_stats_t;

int miipc_sd_storage_start(const char* root_path, int queue_limit);
void miipc_sd_storage_stop(void);
/* Gates recording without stopping the worker thread. */
int miipc_sd_storage_set_enabled(int enabled);
int miipc_sd_storage_is_enabled(void);
/* Keeps the worker alive while preventing any filesystem write when false. */
int miipc_sd_storage_set_available(int available);
int miipc_sd_storage_post(int chn, const struct Stream_Info* info, int count);
void miipc_sd_storage_get_stats(miipc_sd_storage_stats_t* stats);
int miipc_sd_storage_get_info(char* out, int out_len);

#ifdef __cplusplus
}
#endif

#endif
