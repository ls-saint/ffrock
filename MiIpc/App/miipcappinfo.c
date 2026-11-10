#include "miipcappinfo.h"
#include "../../MiBase/include/mibase.h"
#include "../../MiBase/include/mithread.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static miipc_app_info_t g_app_info;
static int g_app_info_inited = 0;

static void miipc_app_info_copy(char* dst, int dst_len, const char* src)
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

static uint32_t miipc_app_info_seed(void)
{
    uint32_t seed = mi_getTick32();
    seed ^= (uint32_t)mi_thread_get_current_id();
    seed ^= (uint32_t)(uintptr_t)&g_app_info;
    seed ^= 0x5a17c9e3u;
    return seed;
}

void miipc_app_info_init(void)
{
    uint32_t value;

    if (g_app_info_inited) {
        return;
    }

    srand((unsigned int)miipc_app_info_seed());
    value = ((uint32_t)(rand() & 0xffff) << 16) ^ (uint32_t)(rand() & 0xffff);
    if (value == 0) {
        value = miipc_app_info_seed();
        if (value == 0) {
            value = 1;
        }
    }
    g_app_info.dev_rand_id = value;
    g_app_info_inited = 1;
}

void miipc_app_info_set_device(const char* dev_id,
                               const char* series,
                               const char* prikey,
                               const char* bindcode)
{
    if (!g_app_info_inited) {
        miipc_app_info_init();
    }
    miipc_app_info_copy(g_app_info.dev_id, sizeof(g_app_info.dev_id), dev_id);
    miipc_app_info_copy(g_app_info.series, sizeof(g_app_info.series), series);
    miipc_app_info_copy(g_app_info.prikey, sizeof(g_app_info.prikey), prikey);
    miipc_app_info_copy(g_app_info.bindcode, sizeof(g_app_info.bindcode), bindcode);
}

void miipc_app_info_set_uid(uint32_t uid)
{
    if (!g_app_info_inited) {
        miipc_app_info_init();
    }
    g_app_info.uid = uid;
}

uint32_t miipc_app_info_get_dev_rand_id(void)
{
    if (!g_app_info_inited) {
        miipc_app_info_init();
    }
    return g_app_info.dev_rand_id;
}

void miipc_app_info_get(miipc_app_info_t* info)
{
    if (info == NULL) {
        return;
    }
    if (!g_app_info_inited) {
        miipc_app_info_init();
    }
    *info = g_app_info;
}

int miipc_app_info_save(void)
{
    FILE* fp;

    if (!g_app_info_inited) {
        miipc_app_info_init();
    }

    fp = fopen(MIIPC_APP_INFO_FILE, "w");
    if (fp == NULL) {
        return -1;
    }
    fprintf(fp, "devid=%s\n", g_app_info.dev_id);
    fprintf(fp, "series=%s\n", g_app_info.series);
    fprintf(fp, "prikey=%s\n", g_app_info.prikey);
    fprintf(fp, "bindcode=%s\n", g_app_info.bindcode);
    fprintf(fp, "uid=%u\n", (unsigned int)g_app_info.uid);
    fprintf(fp, "randid=%u\n", (unsigned int)g_app_info.dev_rand_id);
    fclose(fp);
    return 0;
}
