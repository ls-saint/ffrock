#ifndef MIIPC_APP_INFO_H_
#define MIIPC_APP_INFO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_APP_INFO_TEXT_MAX 64
#define MIIPC_APP_INFO_FILE "appinfo"

typedef struct miipc_app_info {
    uint32_t dev_rand_id;
    uint32_t uid;
    char dev_id[MIIPC_APP_INFO_TEXT_MAX];
    char series[MIIPC_APP_INFO_TEXT_MAX];
    /* Local signing secret; do not include this field directly in messages. */
    char prikey[MIIPC_APP_INFO_TEXT_MAX];
    char bindcode[32];
} miipc_app_info_t;

void miipc_app_info_init(void);
void miipc_app_info_set_device(const char* dev_id,
                               const char* series,
                               const char* prikey,
                               const char* bindcode);
void miipc_app_info_set_uid(uint32_t uid);
uint32_t miipc_app_info_get_dev_rand_id(void);
void miipc_app_info_get(miipc_app_info_t* info);
int miipc_app_info_save(void);

#ifdef __cplusplus
}
#endif

#endif
