#include "miipcappconfig.h"
#include "../../MiBase/include/ini/ini.h"

#include <stdio.h>
#include <string.h>

#ifdef WIN32
#define MIIPC_APP_INI_FILE "F:/work/MiIot/MiIpc/app.ini"
#else
#define MIIPC_APP_INI_FILE "/sys_app/config/app.ini"
#endif

#define MIIPC_APP_INI_SECTION "device"
#define MIIPC_APP_DEFAULT_SN "lh2220008000"
#define MIIPC_APP_DEFAULT_PRICODE "qib2b1000a02b0f"
#define MIIPC_APP_DEFAULT_PRIKEY ""
#define MIIPC_APP_DEFAULT_BINDCODE ""

static void miipc_app_config_copy(char* dst, int dst_len, const char* src)
{
    if (dst == NULL || dst_len <= 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, (size_t)dst_len - 1u);
    dst[dst_len - 1] = '\0';
}

static void miipc_app_config_set_defaults(miipc_app_config_t* cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    miipc_app_config_copy(cfg->sn, sizeof(cfg->sn), MIIPC_APP_DEFAULT_SN);
    miipc_app_config_copy(cfg->pricode, sizeof(cfg->pricode), MIIPC_APP_DEFAULT_PRICODE);
    miipc_app_config_copy(cfg->prikey, sizeof(cfg->prikey), MIIPC_APP_DEFAULT_PRIKEY);
    miipc_app_config_copy(cfg->bindcode, sizeof(cfg->bindcode), MIIPC_APP_DEFAULT_BINDCODE);
}

/*****
确定文件是否存在.
*****/
static void miipc_app_config_ensure_file(const miipc_app_config_t* cfg)
{
    FILE* fp = fopen(MIIPC_APP_INI_FILE, "r");
    if (fp != NULL) {
        fclose(fp);
        return;
    }

    ini_write_string(MIIPC_APP_INI_SECTION, "sn", cfg != NULL ? cfg->sn : "", MIIPC_APP_INI_FILE);
    ini_write_string(MIIPC_APP_INI_SECTION, "pricode", cfg != NULL ? cfg->pricode : "", MIIPC_APP_INI_FILE);
    ini_write_string(MIIPC_APP_INI_SECTION, "prikey", cfg != NULL ? cfg->prikey : "", MIIPC_APP_INI_FILE);
    ini_write_string(MIIPC_APP_INI_SECTION, "bindcode", cfg != NULL ? cfg->bindcode : "", MIIPC_APP_INI_FILE);
}

void miipc_app_config_load(miipc_app_config_t* cfg)
{
    if (cfg == NULL) {
        return;
    }

    miipc_app_config_set_defaults(cfg);
    miipc_app_config_ensure_file(cfg);

    ini_read_string(MIIPC_APP_INI_SECTION, "sn", cfg->sn, sizeof(cfg->sn), MIIPC_APP_INI_FILE);
    ini_read_string(MIIPC_APP_INI_SECTION, "pricode", cfg->pricode, sizeof(cfg->pricode), MIIPC_APP_INI_FILE);
    ini_read_string(MIIPC_APP_INI_SECTION, "prikey", cfg->prikey, sizeof(cfg->prikey), MIIPC_APP_INI_FILE);
    ini_read_string(MIIPC_APP_INI_SECTION, "bindcode", cfg->bindcode, sizeof(cfg->bindcode), MIIPC_APP_INI_FILE);

    printf("MiIpc app.ini device loaded sn=%s has_prikey=%d\n",
           cfg->sn,
           cfg->prikey[0] != '\0');
}
