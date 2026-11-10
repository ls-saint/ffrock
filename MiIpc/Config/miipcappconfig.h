#ifndef MIIPC_APP_CONFIG_H_
#define MIIPC_APP_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define MIIPC_APP_CONFIG_TEXT_MAX 128

typedef struct miipc_app_config {
    char sn[MIIPC_APP_CONFIG_TEXT_MAX];
    char pricode[MIIPC_APP_CONFIG_TEXT_MAX];
    char prikey[MIIPC_APP_CONFIG_TEXT_MAX];
    char bindcode[MIIPC_APP_CONFIG_TEXT_MAX];
} miipc_app_config_t;

void miipc_app_config_load(miipc_app_config_t* cfg);

#ifdef __cplusplus
}
#endif

#endif
