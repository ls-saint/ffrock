#include "miipcsignalconfig.h"
#include "miipccfgdb.h"

#include <stdint.h>
#include <string.h>

#define MIIPC_SIGNAL_CFG_SECTION "signal"

static char g_signal_server_ip[64];

static void signal_cfg_copy(char* dst, int dst_len, const char* src)
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

static int signal_cfg_read_int(const char* key, int default_value)
{
    int value = default_value;
    int ret = miipc_cfg_db_read_int(MIIPC_SIGNAL_CFG_SECTION, key, default_value, &value);
    if (ret == 0) {
        (void)miipc_cfg_db_write_int(MIIPC_SIGNAL_CFG_SECTION, key, default_value);
    }
    return value;
}

static void signal_cfg_read_string(const char* key, const char* default_value, char* out, int out_len)
{
    int ret;
    signal_cfg_copy(out, out_len, default_value);
    ret = miipc_cfg_db_read_string(MIIPC_SIGNAL_CFG_SECTION, key, default_value, out, out_len);
    if (ret == 0) {
        (void)miipc_cfg_db_write_string(MIIPC_SIGNAL_CFG_SECTION, key, default_value);
    }
}

static uint32_t signal_cfg_u32(int value, uint32_t fallback)
{
    return value > 0 ? (uint32_t)value : fallback;
}

static uint16_t signal_cfg_u16(int value, uint16_t fallback)
{
    return value > 0 && value <= 65535 ? (uint16_t)value : fallback;
}

void miipc_signal_config_load(mi_device_signal_config_t* cfg)
{
    int value;

    if (cfg == NULL) {
        return;
    }

    mi_device_signal_default_config(cfg);
    signal_cfg_read_string("server_ip", cfg->server_ip, g_signal_server_ip, sizeof(g_signal_server_ip));
    cfg->server_ip = g_signal_server_ip;

    value = signal_cfg_read_int("server_port", cfg->server_port);
    cfg->server_port = signal_cfg_u16(value, cfg->server_port);

    value = signal_cfg_read_int("channel", cfg->channel);
    cfg->channel = signal_cfg_u16(value, cfg->channel);

    value = signal_cfg_read_int("ctl_ver", cfg->ctl_ver);
    cfg->ctl_ver = value > 2 ? 3 : 2;

    value = signal_cfg_read_int("reconnect_ms", (int)cfg->reconnect_ms);
    cfg->reconnect_ms = signal_cfg_u32(value, cfg->reconnect_ms);

    value = signal_cfg_read_int("reconnect_max_ms", (int)cfg->reconnect_max_ms);
    cfg->reconnect_max_ms = signal_cfg_u32(value, cfg->reconnect_max_ms);

    value = signal_cfg_read_int("connect_timeout_ms", (int)cfg->connect_timeout_ms);
    cfg->connect_timeout_ms = signal_cfg_u32(value, cfg->connect_timeout_ms);

    value = signal_cfg_read_int("devheart_enable", cfg->heartbeat_enable);
    cfg->heartbeat_enable = value ? 1 : 0;

    value = signal_cfg_read_int("devheart_ms", (int)cfg->heartbeat_ms);
    cfg->heartbeat_ms = signal_cfg_u32(value, cfg->heartbeat_ms);

    value = signal_cfg_read_int("worker_idle_sleep_ms", (int)cfg->worker_idle_sleep_ms);
    cfg->worker_idle_sleep_ms = signal_cfg_u32(value, cfg->worker_idle_sleep_ms);

    value = signal_cfg_read_int("worker_loop_interval_ms", (int)cfg->worker_loop_interval_ms);
    cfg->worker_loop_interval_ms = signal_cfg_u32(value, cfg->worker_loop_interval_ms);
}
