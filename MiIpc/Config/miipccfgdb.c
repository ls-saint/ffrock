#include "miipccfgdb.h"
#include "../../MiBase/include/ini/ini.h"
#include "../../MiBase/include/mimutex.h"

#include <stdio.h>
#include <string.h>

#define MIIPC_CFG_DB_SYSTEM_SECTION "system"
#define MIIPC_CFG_DB_CREATED_KEY "created"

static mi_Mutex* g_cfg_db_lock = NULL;

static int miipc_cfg_db_ensure_lock(void)
{
    if (g_cfg_db_lock != NULL) {
        return 1;
    }
    g_cfg_db_lock = mi_CreateMutex(1);
    return g_cfg_db_lock != NULL;
}

static void miipc_cfg_db_lock(void)
{
    if (g_cfg_db_lock != NULL) {
        g_cfg_db_lock->Lock(g_cfg_db_lock);
    }
}

static void miipc_cfg_db_unlock(void)
{
    if (g_cfg_db_lock != NULL) {
        g_cfg_db_lock->UnLock(g_cfg_db_lock);
    }
}

static void miipc_cfg_db_copy_default(char* out, int out_len, const char* default_value)
{
    if (out == NULL || out_len <= 0) {
        return;
    }
    if (default_value == NULL) {
        out[0] = '\0';
        return;
    }
    strncpy(out, default_value, (size_t)out_len - 1u);
    out[out_len - 1] = '\0';
}

int miipc_cfg_db_init(void)
{
    FILE* fp;

    if (!miipc_cfg_db_ensure_lock()) {
        return -1;
    }

    miipc_cfg_db_lock();
    fp = fopen(MIIPC_CFG_DB_FILE, "r");
    if (fp != NULL) {
        fclose(fp);
        miipc_cfg_db_unlock();
        return 0;
    }
    if (!ini_write_int(MIIPC_CFG_DB_SYSTEM_SECTION, MIIPC_CFG_DB_CREATED_KEY, 1, MIIPC_CFG_DB_FILE)) {
        miipc_cfg_db_unlock();
        return -1;
    }
    miipc_cfg_db_unlock();
    return 0;
}

int miipc_cfg_db_read_string(const char* section, const char* key, const char* default_value, char* out, int out_len)
{
    int ret;

    if (section == NULL || key == NULL || out == NULL || out_len <= 0) {
        return -1;
    }
    if (miipc_cfg_db_init() != 0) {
        miipc_cfg_db_copy_default(out, out_len, default_value);
        return -1;
    }

    miipc_cfg_db_lock();
    ret = ini_read_string(section, key, out, out_len, MIIPC_CFG_DB_FILE);
    miipc_cfg_db_unlock();
    if (!ret) {
        miipc_cfg_db_copy_default(out, out_len, default_value);
        return 0;
    }
    return 1;
}

int miipc_cfg_db_read_int(const char* section, const char* key, int default_value, int* out_value)
{
    int ret;

    if (section == NULL || key == NULL || out_value == NULL) {
        return -1;
    }
    if (miipc_cfg_db_init() != 0) {
        *out_value = default_value;
        return -1;
    }

    miipc_cfg_db_lock();
    ret = ini_read_int(section, key, MIIPC_CFG_DB_FILE, out_value);
    miipc_cfg_db_unlock();
    if (!ret) {
        *out_value = default_value;
        return 0;
    }
    return 1;
}

int miipc_cfg_db_write_string(const char* section, const char* key, const char* value)
{
    int ret;

    if (section == NULL || key == NULL) {
        return -1;
    }
    if (miipc_cfg_db_init() != 0) {
        return -1;
    }

    miipc_cfg_db_lock();
    ret = ini_write_string(section, key, value != NULL ? value : "", MIIPC_CFG_DB_FILE);
    miipc_cfg_db_unlock();
    return ret ? 0 : -1;
}

int miipc_cfg_db_write_int(const char* section, const char* key, int value)
{
    int ret;

    if (section == NULL || key == NULL) {
        return -1;
    }
    if (miipc_cfg_db_init() != 0) {
        return -1;
    }

    miipc_cfg_db_lock();
    ret = ini_write_int(section, key, value, MIIPC_CFG_DB_FILE);
    miipc_cfg_db_unlock();
    return ret ? 0 : -1;
}
