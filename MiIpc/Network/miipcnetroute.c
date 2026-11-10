#include "miipcnetroute.h"

#include "../Config/miipccfgdb.h"
#include "../../MiBase/include/mithread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if MI_BASE_OS_POSIX && !defined(MIIPC_TXW82X_MINIMAL)
#include <errno.h>
#include <unistd.h>
#endif

#define MIIPC_NET_ROUTE_SECTION "network_route"
#define MIIPC_NET_ROUTE_ENABLE_KEY "enable"
#define MIIPC_NET_ROUTE_INTERVAL_KEY "check_interval_ms"
#define MIIPC_NET_ROUTE_PING_TIMEOUT_KEY "ping_timeout_ms"
#define MIIPC_NET_ROUTE_FAIL_THRESHOLD_KEY "fail_threshold"
#define MIIPC_NET_ROUTE_RECOVER_THRESHOLD_KEY "recover_threshold"
#define MIIPC_NET_ROUTE_SWITCH_COOLDOWN_KEY "switch_cooldown_ms"
#define MIIPC_NET_ROUTE_PRIMARY_KEY "primary_iface"
#define MIIPC_NET_ROUTE_PRIMARY_GATEWAY_KEY "primary_gateway"
#define MIIPC_NET_ROUTE_BACKUP_KEY "backup_ifaces"
#define MIIPC_NET_ROUTE_BACKUP_GATEWAYS_KEY "backup_gateways"
#define MIIPC_NET_ROUTE_PROBE_KEY "probe_host"

#define MIIPC_NET_ROUTE_DEFAULT_INTERVAL_MS 10000
#define MIIPC_NET_ROUTE_DEFAULT_PING_TIMEOUT_MS 1500
#define MIIPC_NET_ROUTE_DEFAULT_FAIL_THRESHOLD 2
#define MIIPC_NET_ROUTE_DEFAULT_RECOVER_THRESHOLD 2
#define MIIPC_NET_ROUTE_DEFAULT_SWITCH_COOLDOWN_MS 60000
#define MIIPC_NET_ROUTE_THREAD_STACK_SIZE (16u * 1024u)
#define MIIPC_NET_ROUTE_JOIN_TIMEOUT_MS 3000u
#define MIIPC_NET_ROUTE_GATEWAY_CACHE_MAX 4

typedef struct miipc_net_route_gateway_cache {
    char iface[MIIPC_NET_ROUTE_IFACE_MAX];
    char gateway[MIIPC_NET_ROUTE_GATEWAY_MAX];
} miipc_net_route_gateway_cache_t;

typedef struct miipc_net_route_snapshot {
    int valid;
    char iface[MIIPC_NET_ROUTE_IFACE_MAX];
    char gateway[MIIPC_NET_ROUTE_GATEWAY_MAX];
} miipc_net_route_snapshot_t;

typedef struct miipc_net_route_runtime {
    miipc_net_route_config_t cfg;
    mi_thread_t thread;
    volatile int running;
    int thread_started;
    int active_is_primary;
    int primary_ok_count;
    int active_fail_count;
    uint32_t last_switch_tick;
    char current_iface[MIIPC_NET_ROUTE_IFACE_MAX];
    miipc_net_route_gateway_cache_t gateway_cache[MIIPC_NET_ROUTE_GATEWAY_CACHE_MAX];
} miipc_net_route_runtime_t;

static miipc_net_route_runtime_t g_net_route;

static void miipc_net_route_copy_text(char* dst, int dst_len, const char* src)
{
    if (dst == NULL || dst_len <= 0) {
        return;
    }
    snprintf(dst, (size_t)dst_len, "%s", src != NULL ? src : "");
    dst[dst_len - 1] = '\0';
}

#if !defined(MIIPC_TXW82X_MINIMAL)
static int miipc_net_route_positive_or_default(int value, int default_value)
{
    return value > 0 ? value : default_value;
}

static void miipc_net_route_read_string_default(const char* key, char* out, int out_len)
{
    char default_value[MIIPC_NET_ROUTE_TEXT_MAX];
    int ret;

    if (key == NULL || out == NULL || out_len <= 0) {
        return;
    }
    miipc_net_route_copy_text(default_value, sizeof(default_value), out);
    ret = miipc_cfg_db_read_string(MIIPC_NET_ROUTE_SECTION, key, default_value, out, out_len);
    if (ret == 0) {
        (void)miipc_cfg_db_write_string(MIIPC_NET_ROUTE_SECTION, key, default_value);
    }
}
#endif

#if MI_BASE_OS_POSIX && !defined(MIIPC_TXW82X_MINIMAL)
static int miipc_net_route_is_safe_iface(const char* text)
{
    const unsigned char* p = (const unsigned char*)text;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' ||
              *p == '-' ||
              *p == '.')) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int miipc_net_route_is_safe_host(const char* text)
{
    const unsigned char* p = (const unsigned char*)text;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    while (*p != '\0') {
        if (!((*p >= 'a' && *p <= 'z') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' ||
              *p == '-' ||
              *p == '.' ||
              *p == ':')) {
            return 0;
        }
        ++p;
    }
    return 1;
}
#endif

void miipc_net_route_default_config(miipc_net_route_config_t* cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->enable = 1;
    cfg->check_interval_ms = MIIPC_NET_ROUTE_DEFAULT_INTERVAL_MS;
    cfg->ping_timeout_ms = MIIPC_NET_ROUTE_DEFAULT_PING_TIMEOUT_MS;
    cfg->fail_threshold = MIIPC_NET_ROUTE_DEFAULT_FAIL_THRESHOLD;
    cfg->recover_threshold = MIIPC_NET_ROUTE_DEFAULT_RECOVER_THRESHOLD;
    cfg->switch_cooldown_ms = MIIPC_NET_ROUTE_DEFAULT_SWITCH_COOLDOWN_MS;
    miipc_net_route_copy_text(cfg->primary_iface, sizeof(cfg->primary_iface), "eth0");
    miipc_net_route_copy_text(cfg->backup_ifaces, sizeof(cfg->backup_ifaces), "wlan0,usb0");
    miipc_net_route_copy_text(cfg->probe_host, sizeof(cfg->probe_host), "82.157.168.94");
}

void miipc_net_route_load_config(miipc_net_route_config_t* cfg)
{
#if !defined(MIIPC_TXW82X_MINIMAL)
    int value;

    if (cfg == NULL) {
        return;
    }
    miipc_net_route_default_config(cfg);
    if (miipc_cfg_db_read_int(MIIPC_NET_ROUTE_SECTION,
                              MIIPC_NET_ROUTE_ENABLE_KEY,
                              cfg->enable,
                              &value) == 0) {
        (void)miipc_cfg_db_write_int(MIIPC_NET_ROUTE_SECTION, MIIPC_NET_ROUTE_ENABLE_KEY, cfg->enable);
    } else {
        cfg->enable = value ? 1 : 0;
    }
    if (miipc_cfg_db_read_int(MIIPC_NET_ROUTE_SECTION,
                              MIIPC_NET_ROUTE_INTERVAL_KEY,
                              cfg->check_interval_ms,
                              &value) == 0) {
        (void)miipc_cfg_db_write_int(MIIPC_NET_ROUTE_SECTION, MIIPC_NET_ROUTE_INTERVAL_KEY, cfg->check_interval_ms);
    } else {
        cfg->check_interval_ms = miipc_net_route_positive_or_default(value, MIIPC_NET_ROUTE_DEFAULT_INTERVAL_MS);
    }
    if (miipc_cfg_db_read_int(MIIPC_NET_ROUTE_SECTION,
                              MIIPC_NET_ROUTE_PING_TIMEOUT_KEY,
                              cfg->ping_timeout_ms,
                              &value) == 0) {
        (void)miipc_cfg_db_write_int(MIIPC_NET_ROUTE_SECTION, MIIPC_NET_ROUTE_PING_TIMEOUT_KEY, cfg->ping_timeout_ms);
    } else {
        cfg->ping_timeout_ms = miipc_net_route_positive_or_default(value, MIIPC_NET_ROUTE_DEFAULT_PING_TIMEOUT_MS);
    }
    if (miipc_cfg_db_read_int(MIIPC_NET_ROUTE_SECTION,
                              MIIPC_NET_ROUTE_FAIL_THRESHOLD_KEY,
                              cfg->fail_threshold,
                              &value) == 0) {
        (void)miipc_cfg_db_write_int(MIIPC_NET_ROUTE_SECTION, MIIPC_NET_ROUTE_FAIL_THRESHOLD_KEY, cfg->fail_threshold);
    } else {
        cfg->fail_threshold = miipc_net_route_positive_or_default(value, MIIPC_NET_ROUTE_DEFAULT_FAIL_THRESHOLD);
    }
    if (miipc_cfg_db_read_int(MIIPC_NET_ROUTE_SECTION,
                              MIIPC_NET_ROUTE_RECOVER_THRESHOLD_KEY,
                              cfg->recover_threshold,
                              &value) == 0) {
        (void)miipc_cfg_db_write_int(MIIPC_NET_ROUTE_SECTION, MIIPC_NET_ROUTE_RECOVER_THRESHOLD_KEY, cfg->recover_threshold);
    } else {
        cfg->recover_threshold = miipc_net_route_positive_or_default(value, MIIPC_NET_ROUTE_DEFAULT_RECOVER_THRESHOLD);
    }
    if (miipc_cfg_db_read_int(MIIPC_NET_ROUTE_SECTION,
                              MIIPC_NET_ROUTE_SWITCH_COOLDOWN_KEY,
                              cfg->switch_cooldown_ms,
                              &value) == 0) {
        (void)miipc_cfg_db_write_int(MIIPC_NET_ROUTE_SECTION, MIIPC_NET_ROUTE_SWITCH_COOLDOWN_KEY, cfg->switch_cooldown_ms);
    } else {
        cfg->switch_cooldown_ms = miipc_net_route_positive_or_default(value, MIIPC_NET_ROUTE_DEFAULT_SWITCH_COOLDOWN_MS);
    }
    miipc_net_route_read_string_default(MIIPC_NET_ROUTE_PRIMARY_KEY,
                                        cfg->primary_iface,
                                        sizeof(cfg->primary_iface));
    miipc_net_route_read_string_default(MIIPC_NET_ROUTE_PRIMARY_GATEWAY_KEY,
                                        cfg->primary_gateway,
                                        sizeof(cfg->primary_gateway));
    miipc_net_route_read_string_default(MIIPC_NET_ROUTE_BACKUP_KEY,
                                        cfg->backup_ifaces,
                                        sizeof(cfg->backup_ifaces));
    miipc_net_route_read_string_default(MIIPC_NET_ROUTE_BACKUP_GATEWAYS_KEY,
                                        cfg->backup_gateways,
                                        sizeof(cfg->backup_gateways));
    miipc_net_route_read_string_default(MIIPC_NET_ROUTE_PROBE_KEY,
                                        cfg->probe_host,
                                        sizeof(cfg->probe_host));
#else
    miipc_net_route_default_config(cfg);
    cfg->enable = 0;
#endif
}

#if MI_BASE_OS_POSIX && !defined(MIIPC_TXW82X_MINIMAL)
static int miipc_net_route_split_next(char** cursor, char* out, int out_len);

static int miipc_net_route_run_command(const char* cmd, int print_error)
{
    int rc;

    if (cmd == NULL || cmd[0] == '\0') {
        return -1;
    }
    rc = system(cmd);
    if (rc != 0 && print_error) {
        printf("MiIpc route command failed rc=%d cmd=%s\n", rc, cmd);
        fflush(stdout);
    }
    return rc == 0 ? 0 : -1;
}

static int miipc_net_route_read_first_line(const char* cmd, char* out, int out_len)
{
    FILE* fp;
    char* newline;

    if (cmd == NULL || out == NULL || out_len <= 0) {
        return -1;
    }
    out[0] = '\0';
    fp = popen(cmd, "r");
    if (fp == NULL) {
        return -1;
    }
    if (fgets(out, out_len, fp) == NULL) {
        (void)pclose(fp);
        out[0] = '\0';
        return -1;
    }
    (void)pclose(fp);
    newline = strchr(out, '\n');
    if (newline != NULL) {
        *newline = '\0';
    }
    newline = strchr(out, '\r');
    if (newline != NULL) {
        *newline = '\0';
    }
    return out[0] != '\0' ? 0 : -1;
}

static int miipc_net_route_token_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int miipc_net_route_parse_ip_route_default(const char* line,
                                                  char* iface,
                                                  int iface_len,
                                                  char* gw,
                                                  int gw_len)
{
    char text[256];
    char* save;
    char* tok;
    char* next_is = NULL;
    char via[MIIPC_NET_ROUTE_GATEWAY_MAX];
    char dev[MIIPC_NET_ROUTE_IFACE_MAX];
    int is_default = 0;

    if (line == NULL || (iface == NULL && gw == NULL) ||
        (iface != NULL && iface_len <= 0) || (gw != NULL && gw_len <= 0)) {
        return -1;
    }
    miipc_net_route_copy_text(text, sizeof(text), line);
    via[0] = '\0';
    dev[0] = '\0';
    save = text;
    while (*save != '\0') {
        while (miipc_net_route_token_is_space(*save)) {
            ++save;
        }
        if (*save == '\0') {
            break;
        }
        tok = save;
        while (*save != '\0' && !miipc_net_route_token_is_space(*save)) {
            ++save;
        }
        if (*save != '\0') {
            *save++ = '\0';
        }
        if (next_is != NULL) {
            if (strcmp(next_is, "via") == 0) {
                miipc_net_route_copy_text(via, sizeof(via), tok);
            } else if (strcmp(next_is, "dev") == 0) {
                miipc_net_route_copy_text(dev, sizeof(dev), tok);
            }
            next_is = NULL;
            continue;
        }
        if (strcmp(tok, "default") == 0) {
            is_default = 1;
        } else if (strcmp(tok, "via") == 0 || strcmp(tok, "dev") == 0) {
            next_is = tok;
        }
    }
    if (!is_default || dev[0] == '\0' || !miipc_net_route_is_safe_iface(dev)) {
        return -1;
    }
    if (iface != NULL) {
        miipc_net_route_copy_text(iface, iface_len, dev);
    }
    if (gw != NULL) {
        if (strcmp(via, "0.0.0.0") == 0) {
            via[0] = '\0';
        }
        miipc_net_route_copy_text(gw, gw_len, via);
    }
    return 0;
}

static int miipc_net_route_parse_ip_route_gateway(const char* line,
                                                  const char* iface,
                                                  char* gw,
                                                  int gw_len)
{
    char route_iface[MIIPC_NET_ROUTE_IFACE_MAX];
    char route_gateway[MIIPC_NET_ROUTE_GATEWAY_MAX];

    if (line == NULL || iface == NULL || gw == NULL || gw_len <= 0 ||
        miipc_net_route_parse_ip_route_default(line,
                                               route_iface,
                                               sizeof(route_iface),
                                               route_gateway,
                                               sizeof(route_gateway)) != 0 ||
        strcmp(route_iface, iface) != 0 ||
        (route_gateway[0] != '\0' && !miipc_net_route_is_safe_host(route_gateway))) {
        return -1;
    }
    miipc_net_route_copy_text(gw, gw_len, route_gateway);
    return 0;
}

static int miipc_net_route_parse_route_n_default(const char* line,
                                                 char* iface,
                                                 int iface_len,
                                                 char* gw,
                                                 int gw_len)
{
    char text[256];
    char* save;
    char* cols[8];
    int count = 0;

    if (line == NULL || (iface == NULL && gw == NULL) ||
        (iface != NULL && iface_len <= 0) || (gw != NULL && gw_len <= 0)) {
        return -1;
    }
    if (iface != NULL) {
        iface[0] = '\0';
    }
    if (gw != NULL) {
        gw[0] = '\0';
    }
    miipc_net_route_copy_text(text, sizeof(text), line);
    save = text;
    while (*save != '\0' && count < 8) {
        while (miipc_net_route_token_is_space(*save)) {
            ++save;
        }
        if (*save == '\0') {
            break;
        }
        cols[count++] = save;
        while (*save != '\0' && !miipc_net_route_token_is_space(*save)) {
            ++save;
        }
        if (*save != '\0') {
            *save++ = '\0';
        }
    }
    if (count >= 8 &&
        strcmp(cols[0], "0.0.0.0") == 0 &&
        cols[7][0] != '\0' &&
        miipc_net_route_is_safe_iface(cols[7])) {
        if (iface != NULL) {
            miipc_net_route_copy_text(iface, iface_len, cols[7]);
        }
        if (gw != NULL && strcmp(cols[1], "0.0.0.0") != 0) {
            miipc_net_route_copy_text(gw, gw_len, cols[1]);
        }
        return 0;
    }
    return -1;
}

static int miipc_net_route_parse_route_n_gateway(const char* line,
                                                 const char* iface,
                                                 char* gw,
                                                 int gw_len)
{
    char route_iface[MIIPC_NET_ROUTE_IFACE_MAX];
    char route_gateway[MIIPC_NET_ROUTE_GATEWAY_MAX];

    if (line == NULL || iface == NULL || gw == NULL || gw_len <= 0 ||
        miipc_net_route_parse_route_n_default(line,
                                              route_iface,
                                              sizeof(route_iface),
                                              route_gateway,
                                              sizeof(route_gateway)) != 0 ||
        strcmp(route_iface, iface) != 0 ||
        (route_gateway[0] != '\0' && !miipc_net_route_is_safe_host(route_gateway))) {
        return -1;
    }
    miipc_net_route_copy_text(gw, gw_len, route_gateway);
    return 0;
}

static int miipc_net_route_read_gateway_lines(const char* cmd,
                                              const char* iface,
                                              char* gw,
                                              int gw_len,
                                              int route_n_format)
{
    FILE* fp;
    char line[256];

    if (cmd == NULL || iface == NULL || gw == NULL || gw_len <= 0) {
        return -1;
    }
    fp = popen(cmd, "r");
    if (fp == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (route_n_format) {
            if (miipc_net_route_parse_route_n_gateway(line, iface, gw, gw_len) == 0) {
                (void)pclose(fp);
                return 0;
            }
        } else if (miipc_net_route_parse_ip_route_gateway(line, iface, gw, gw_len) == 0) {
            (void)pclose(fp);
            return 0;
        }
    }
    (void)pclose(fp);
    return -1;
}

static int miipc_net_route_get_default_route(char* iface,
                                             int iface_len,
                                             char* gateway,
                                             int gateway_len)
{
    FILE* fp;
    char line[256];
    char route_iface[MIIPC_NET_ROUTE_IFACE_MAX];
    char route_gateway[MIIPC_NET_ROUTE_GATEWAY_MAX];

    if (iface == NULL || iface_len <= 0 ||
        (gateway != NULL && gateway_len <= 0)) {
        return -1;
    }
    iface[0] = '\0';
    if (gateway != NULL) {
        gateway[0] = '\0';
    }

    fp = popen("ip route show default 2>/dev/null", "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            route_iface[0] = '\0';
            route_gateway[0] = '\0';
            if (miipc_net_route_parse_ip_route_default(line,
                                                       route_iface,
                                                       sizeof(route_iface),
                                                       route_gateway,
                                                       sizeof(route_gateway)) == 0 &&
                (route_gateway[0] == '\0' ||
                 miipc_net_route_is_safe_host(route_gateway))) {
                miipc_net_route_copy_text(iface, iface_len, route_iface);
                if (gateway != NULL) {
                    miipc_net_route_copy_text(gateway, gateway_len, route_gateway);
                }
                (void)pclose(fp);
                return 0;
            }
        }
        (void)pclose(fp);
    }

    fp = popen("route -n 2>/dev/null", "r");
    if (fp != NULL) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            route_iface[0] = '\0';
            route_gateway[0] = '\0';
            if (miipc_net_route_parse_route_n_default(line,
                                                      route_iface,
                                                      sizeof(route_iface),
                                                      route_gateway,
                                                      sizeof(route_gateway)) == 0 &&
                (route_gateway[0] == '\0' ||
                 miipc_net_route_is_safe_host(route_gateway))) {
                miipc_net_route_copy_text(iface, iface_len, route_iface);
                if (gateway != NULL) {
                    miipc_net_route_copy_text(gateway, gateway_len, route_gateway);
                }
                (void)pclose(fp);
                return 0;
            }
        }
        (void)pclose(fp);
    }
    iface[0] = '\0';
    if (gateway != NULL) {
        gateway[0] = '\0';
    }
    return -1;
}

static int miipc_net_route_iface_exists(const char* iface)
{
    char path[128];

    if (iface == NULL || iface[0] == '\0') {
        return 0;
    }
    snprintf(path, sizeof(path), "/sys/class/net/%s", iface);
    return access(path, F_OK) == 0;
}

static int miipc_net_route_iface_has_ipv4(const char* iface)
{
    char cmd[192];
    char line[64];

    if (!miipc_net_route_iface_exists(iface)) {
        return 0;
    }
    snprintf(cmd,
             sizeof(cmd),
             "ip -4 addr show dev '%s' 2>/dev/null | grep -m1 ' inet '",
             iface);
    if (miipc_net_route_read_first_line(cmd, line, sizeof(line)) == 0) {
        return 1;
    }
    snprintf(cmd,
             sizeof(cmd),
             "ifconfig '%s' 2>/dev/null | grep -m1 'inet addr\\|inet '",
             iface);
    return miipc_net_route_read_first_line(cmd, line, sizeof(line)) == 0;
}

static int miipc_net_route_get_configured_gateway(const char* iface,
                                                  char* gw,
                                                  int gw_len)
{
    char mappings[MIIPC_NET_ROUTE_TEXT_MAX];
    char entry[MIIPC_NET_ROUTE_TEXT_MAX];
    char* cursor;
    char* separator;

    if (iface == NULL || gw == NULL || gw_len <= 0) {
        return -1;
    }
    if (strcmp(iface, g_net_route.cfg.primary_iface) == 0 &&
        g_net_route.cfg.primary_gateway[0] != '\0' &&
        miipc_net_route_is_safe_host(g_net_route.cfg.primary_gateway)) {
        miipc_net_route_copy_text(gw,
                                  gw_len,
                                  g_net_route.cfg.primary_gateway);
        return 0;
    }

    miipc_net_route_copy_text(mappings,
                              sizeof(mappings),
                              g_net_route.cfg.backup_gateways);
    cursor = mappings;
    while (miipc_net_route_split_next(&cursor, entry, sizeof(entry))) {
        separator = strchr(entry, '=');
        if (separator == NULL) {
            continue;
        }
        *separator = '\0';
        if (strcmp(entry, iface) == 0 &&
            miipc_net_route_is_safe_host(separator + 1)) {
            miipc_net_route_copy_text(gw, gw_len, separator + 1);
            return 0;
        }
    }
    return -1;
}

static void miipc_net_route_remember_gateway(const char* iface, const char* gateway)
{
    int i;
    int empty = -1;

    if (!miipc_net_route_is_safe_iface(iface) ||
        gateway == NULL ||
        gateway[0] == '\0' ||
        strcmp(gateway, "0.0.0.0") == 0 ||
        !miipc_net_route_is_safe_host(gateway)) {
        return;
    }
    for (i = 0; i < MIIPC_NET_ROUTE_GATEWAY_CACHE_MAX; ++i) {
        if (g_net_route.gateway_cache[i].iface[0] == '\0' && empty < 0) {
            empty = i;
        }
        if (strcmp(g_net_route.gateway_cache[i].iface, iface) == 0) {
            miipc_net_route_copy_text(g_net_route.gateway_cache[i].gateway,
                                      sizeof(g_net_route.gateway_cache[i].gateway),
                                      gateway);
            return;
        }
    }
    if (empty >= 0) {
        miipc_net_route_copy_text(g_net_route.gateway_cache[empty].iface,
                                  sizeof(g_net_route.gateway_cache[empty].iface),
                                  iface);
        miipc_net_route_copy_text(g_net_route.gateway_cache[empty].gateway,
                                  sizeof(g_net_route.gateway_cache[empty].gateway),
                                  gateway);
    }
}

static int miipc_net_route_get_cached_gateway(const char* iface, char* gw, int gw_len)
{
    int i;

    if (iface == NULL || gw == NULL || gw_len <= 0) {
        return -1;
    }
    for (i = 0; i < MIIPC_NET_ROUTE_GATEWAY_CACHE_MAX; ++i) {
        if (strcmp(g_net_route.gateway_cache[i].iface, iface) == 0 &&
            g_net_route.gateway_cache[i].gateway[0] != '\0') {
            miipc_net_route_copy_text(gw, gw_len, g_net_route.gateway_cache[i].gateway);
            return 0;
        }
    }
    return -1;
}

static int miipc_net_route_ping_iface(const char* iface)
{
    char cmd[320];
    int timeout_s;

    if (iface == NULL || iface[0] == '\0') {
        return 0;
    }
    if (!miipc_net_route_is_safe_iface(iface) ||
        !miipc_net_route_is_safe_host(g_net_route.cfg.probe_host)) {
        return 0;
    }
    if (!miipc_net_route_iface_has_ipv4(iface)) {
        return 0;
    }
    timeout_s = (g_net_route.cfg.ping_timeout_ms + 999) / 1000;
    if (timeout_s <= 0) {
        timeout_s = 1;
    }
    snprintf(cmd,
             sizeof(cmd),
             "ping -I '%s' -c 1 -W %d '%s' >/dev/null 2>&1",
             iface,
             timeout_s,
             g_net_route.cfg.probe_host);
    return miipc_net_route_run_command(cmd, 0) == 0;
}

static int miipc_net_route_get_gateway(const char* iface, char* gw, int gw_len)
{
    char cmd[256];

    if (iface == NULL || iface[0] == '\0' || gw == NULL || gw_len <= 0) {
        return -1;
    }
    if (!miipc_net_route_is_safe_iface(iface)) {
        return -1;
    }
    snprintf(cmd,
             sizeof(cmd),
             "ip route show default dev '%s' 2>/dev/null",
             iface);
    if (miipc_net_route_read_gateway_lines(cmd, iface, gw, gw_len, 0) == 0) {
        miipc_net_route_remember_gateway(iface, gw);
        return 0;
    }
    snprintf(cmd,
             sizeof(cmd),
             "ip route show default 2>/dev/null");
    if (miipc_net_route_read_gateway_lines(cmd, iface, gw, gw_len, 0) == 0) {
        miipc_net_route_remember_gateway(iface, gw);
        return 0;
    }
    snprintf(cmd,
             sizeof(cmd),
             "route -n 2>/dev/null");
    if (miipc_net_route_read_gateway_lines(cmd, iface, gw, gw_len, 1) == 0) {
        miipc_net_route_remember_gateway(iface, gw);
        return 0;
    }
    if (miipc_net_route_get_configured_gateway(iface, gw, gw_len) == 0) {
        miipc_net_route_remember_gateway(iface, gw);
        return 0;
    }
    return miipc_net_route_get_cached_gateway(iface, gw, gw_len);
}

static int miipc_net_route_snapshot_default(miipc_net_route_snapshot_t* route)
{
    if (route == NULL) {
        return -1;
    }
    memset(route, 0, sizeof(*route));
    if (miipc_net_route_get_default_route(route->iface,
                                          sizeof(route->iface),
                                          route->gateway,
                                          sizeof(route->gateway)) != 0 ||
        !miipc_net_route_is_safe_iface(route->iface) ||
        (route->gateway[0] != '\0' &&
         !miipc_net_route_is_safe_host(route->gateway))) {
        memset(route, 0, sizeof(*route));
        return -1;
    }
    route->valid = 1;
    miipc_net_route_remember_gateway(route->iface, route->gateway);
    return 0;
}

static int miipc_net_route_gateway_equal(const char* left, const char* right)
{
    const char* lhs = left != NULL ? left : "";
    const char* rhs = right != NULL ? right : "";
    return strcmp(lhs, rhs) == 0;
}

static int miipc_net_route_route_add(const char* iface,
                                     const char* gateway,
                                     int print_error)
{
    char cmd[320];

    if (!miipc_net_route_is_safe_iface(iface) ||
        (gateway != NULL && gateway[0] != '\0' &&
         !miipc_net_route_is_safe_host(gateway))) {
        return -1;
    }
    if (gateway != NULL && gateway[0] != '\0') {
        snprintf(cmd,
                 sizeof(cmd),
                 "route add default gw '%s' dev '%s' 2>/dev/null",
                 gateway,
                 iface);
    } else {
        snprintf(cmd,
                 sizeof(cmd),
                 "route add default dev '%s' 2>/dev/null",
                 iface);
    }
    return miipc_net_route_run_command(cmd, print_error);
}

static int miipc_net_route_route_delete(const char* iface,
                                        const char* gateway,
                                        int print_error)
{
    char cmd[320];
    int ret;

    if (!miipc_net_route_is_safe_iface(iface) ||
        (gateway != NULL && gateway[0] != '\0' &&
         !miipc_net_route_is_safe_host(gateway))) {
        return -1;
    }
    if (gateway != NULL && gateway[0] != '\0') {
        snprintf(cmd,
                 sizeof(cmd),
                 "route del default gw '%s' dev '%s' 2>/dev/null",
                 gateway,
                 iface);
    } else {
        snprintf(cmd,
                 sizeof(cmd),
                 "route del default dev '%s' 2>/dev/null",
                 iface);
    }
    ret = miipc_net_route_run_command(cmd, 0);
    if (ret != 0 && gateway != NULL && gateway[0] != '\0') {
        snprintf(cmd,
                 sizeof(cmd),
                 "route del default dev '%s' 2>/dev/null",
                 iface);
        ret = miipc_net_route_run_command(cmd, 0);
    }
    if (ret != 0 && print_error) {
        printf("MiIpc route delete failed iface=%s gateway=%s\n",
               iface,
               gateway != NULL ? gateway : "");
        fflush(stdout);
    }
    return ret;
}

static int miipc_net_route_apply_default(const char* iface,
                                         const char* gateway,
                                         const miipc_net_route_snapshot_t* previous,
                                         int print_error)
{
    char cmd[320];
    int ret;

    if (!miipc_net_route_is_safe_iface(iface) ||
        (gateway != NULL && gateway[0] != '\0' &&
         !miipc_net_route_is_safe_host(gateway))) {
        return -1;
    }
    if (gateway != NULL && gateway[0] != '\0') {
        snprintf(cmd,
                 sizeof(cmd),
                 "ip route replace default via '%s' dev '%s' 2>/dev/null",
                 gateway,
                 iface);
    } else {
        snprintf(cmd,
                 sizeof(cmd),
                 "ip route replace default dev '%s' 2>/dev/null",
                 iface);
    }
    ret = miipc_net_route_run_command(cmd, 0);
    if (ret == 0) {
        return 0;
    }

    /* The legacy route command has no replace operation. Delete only the
     * route we inspected, then restore it if adding the new route fails.
     * Without a valid snapshot, adding a default route could leave an
     * unrelated default route in place, so do not use the legacy fallback. */
    if (previous == NULL || !previous->valid) {
        if (print_error) {
            printf("MiIpc route legacy fallback skipped: default route snapshot unavailable\n");
            fflush(stdout);
        }
        return -1;
    }
    if (strcmp(previous->iface, iface) == 0 &&
        miipc_net_route_gateway_equal(previous->gateway, gateway)) {
        return 0;
    }
    if (miipc_net_route_route_delete(previous->iface,
                                     previous->gateway,
                                     print_error) != 0) {
        return -1;
    }

    ret = miipc_net_route_route_add(iface, gateway, print_error);
    if (ret == 0) {
        return 0;
    }

    if (previous != NULL && previous->valid) {
        if (miipc_net_route_route_add(previous->iface,
                                      previous->gateway,
                                      0) != 0) {
            printf("MiIpc route rollback failed iface=%s gateway=%s\n",
                   previous->iface,
                   previous->gateway);
            fflush(stdout);
        }
    }
    return -1;
}

static int miipc_net_route_delete_default(const char* iface,
                                          const char* gateway,
                                          int print_error)
{
    char cmd[320];
    int ret;

    if (!miipc_net_route_is_safe_iface(iface) ||
        (gateway != NULL && gateway[0] != '\0' &&
         !miipc_net_route_is_safe_host(gateway))) {
        return -1;
    }
    if (gateway != NULL && gateway[0] != '\0') {
        snprintf(cmd,
                 sizeof(cmd),
                 "ip route del default via '%s' dev '%s' 2>/dev/null",
                 gateway,
                 iface);
    } else {
        snprintf(cmd,
                 sizeof(cmd),
                 "ip route del default dev '%s' 2>/dev/null",
                 iface);
    }
    ret = miipc_net_route_run_command(cmd, 0);
    if (ret == 0) {
        return 0;
    }
    if (gateway != NULL && gateway[0] != '\0') {
        snprintf(cmd,
                 sizeof(cmd),
                 "ip route del default dev '%s' 2>/dev/null",
                 iface);
        ret = miipc_net_route_run_command(cmd, 0);
    }
    if (ret == 0) {
        return 0;
    }
    return miipc_net_route_route_delete(iface, gateway, print_error);
}

static int miipc_net_route_restore_snapshot(const miipc_net_route_snapshot_t* target,
                                            const char* current_iface,
                                            const char* current_gateway)
{
    miipc_net_route_snapshot_t current;

    memset(&current, 0, sizeof(current));
    if (miipc_net_route_is_safe_iface(current_iface)) {
        current.valid = 1;
        miipc_net_route_copy_text(current.iface,
                                  sizeof(current.iface),
                                  current_iface);
        miipc_net_route_copy_text(current.gateway,
                                  sizeof(current.gateway),
                                  current_gateway);
    }
    if (target != NULL && target->valid) {
        return miipc_net_route_apply_default(target->iface,
                                             target->gateway,
                                             current.valid ? &current : NULL,
                                             1);
    }
    if (current.valid) {
        return miipc_net_route_delete_default(current.iface,
                                              current.gateway,
                                              1);
    }
    return 0;
}

static int miipc_net_route_can_switch(void)
{
    uint32_t now;

    if (g_net_route.last_switch_tick == 0 || g_net_route.cfg.switch_cooldown_ms <= 0) {
        return 1;
    }
    now = mi_getTick32();
    return (uint32_t)(now - g_net_route.last_switch_tick) >= (uint32_t)g_net_route.cfg.switch_cooldown_ms;
}

static void miipc_net_route_mark_switch(void)
{
    g_net_route.last_switch_tick = mi_getTick32();
}

static void miipc_net_route_commit_active(const char* iface, int print_log)
{
    char gateway[MIIPC_NET_ROUTE_GATEWAY_MAX];

    if (!miipc_net_route_is_safe_iface(iface)) {
        return;
    }
    gateway[0] = '\0';
    (void)miipc_net_route_get_gateway(iface, gateway, sizeof(gateway));
    miipc_net_route_copy_text(g_net_route.current_iface,
                              sizeof(g_net_route.current_iface),
                              iface);
    miipc_net_route_mark_switch();
    if (print_log) {
        printf("MiIpc route switched default iface=%s gateway=%s probe=%s\n",
               iface,
               gateway,
               g_net_route.cfg.probe_host);
        fflush(stdout);
    }
}

static int miipc_net_route_try_interface(const char* iface,
                                         const miipc_net_route_snapshot_t* previous,
                                         int print_log)
{
    char gateway[MIIPC_NET_ROUTE_GATEWAY_MAX];

    if (!miipc_net_route_is_safe_iface(iface)) {
        return -1;
    }
    gateway[0] = '\0';
    if (miipc_net_route_get_gateway(iface,
                                    gateway,
                                    sizeof(gateway)) != 0 ||
        (gateway[0] != '\0' && strcmp(gateway, "0.0.0.0") == 0)) {
        return -1;
    }
    if (!miipc_net_route_iface_has_ipv4(iface) ||
        miipc_net_route_apply_default(iface,
                                      gateway,
                                      previous,
                                      0) != 0) {
        return -1;
    }
    if (miipc_net_route_ping_iface(iface)) {
        miipc_net_route_commit_active(iface, print_log);
        return 0;
    }
    if (miipc_net_route_restore_snapshot(previous, iface, gateway) != 0) {
        printf("MiIpc route probe rollback failed iface=%s\n", iface);
        fflush(stdout);
        return -2;
    }
    return -1;
}

static int miipc_net_route_split_next(char** cursor, char* out, int out_len)
{
    char* p;
    char* start;
    int len;

    if (cursor == NULL || *cursor == NULL || out == NULL || out_len <= 0) {
        return 0;
    }
    p = *cursor;
    while (*p == ' ' || *p == '\t' || *p == ',') {
        ++p;
    }
    if (*p == '\0') {
        *cursor = p;
        out[0] = '\0';
        return 0;
    }
    start = p;
    while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t') {
        ++p;
    }
    len = (int)(p - start);
    if (len >= out_len) {
        len = out_len - 1;
    }
    memcpy(out, start, (size_t)len);
    out[len] = '\0';
    *cursor = p;
    return out[0] != '\0';
}

static int miipc_net_route_find_backup(char* iface, int iface_len)
{
    miipc_net_route_snapshot_t previous;
    char backups[MIIPC_NET_ROUTE_TEXT_MAX];
    char* cursor;
    char candidate[MIIPC_NET_ROUTE_IFACE_MAX];
    int ret;

    (void)miipc_net_route_snapshot_default(&previous);
    miipc_net_route_copy_text(backups, sizeof(backups), g_net_route.cfg.backup_ifaces);
    cursor = backups;
    while (miipc_net_route_split_next(&cursor, candidate, sizeof(candidate))) {
        if (!miipc_net_route_is_safe_iface(candidate) ||
            !miipc_net_route_iface_has_ipv4(candidate)) {
            continue;
        }
        ret = miipc_net_route_try_interface(candidate,
                                             previous.valid ? &previous : NULL,
                                             1);
        if (ret == 0) {
            miipc_net_route_copy_text(iface, iface_len, candidate);
            return 0;
        }
        if (ret == -2) {
            return -1;
        }
    }
    return -1;
}

int miipc_net_route_check_once(void)
{
    miipc_net_route_snapshot_t previous;
    char backup[MIIPC_NET_ROUTE_IFACE_MAX];
    int active_ok;
    int primary_ok;
    int ret;

    if (!g_net_route.running || !g_net_route.cfg.enable) {
        return 0;
    }
    if (g_net_route.current_iface[0] == '\0') {
        miipc_net_route_copy_text(g_net_route.current_iface,
                                  sizeof(g_net_route.current_iface),
                                  g_net_route.cfg.primary_iface);
        g_net_route.active_is_primary = 1;
    }

    active_ok = miipc_net_route_ping_iface(g_net_route.current_iface);

    if (g_net_route.active_is_primary) {
        if (active_ok) {
            g_net_route.active_fail_count = 0;
            return 0;
        }
        ++g_net_route.active_fail_count;
        if (g_net_route.active_fail_count < g_net_route.cfg.fail_threshold) {
            return 0;
        }
        if (miipc_net_route_can_switch() &&
            miipc_net_route_find_backup(backup, sizeof(backup)) == 0) {
            g_net_route.active_is_primary = 0;
            g_net_route.active_fail_count = 0;
            g_net_route.primary_ok_count = 0;
            return 1;
        }
        return -1;
    }

    primary_ok = miipc_net_route_ping_iface(g_net_route.cfg.primary_iface);
    if (primary_ok) {
        ++g_net_route.primary_ok_count;
        if (g_net_route.primary_ok_count >= g_net_route.cfg.recover_threshold &&
            miipc_net_route_can_switch()) {
            (void)miipc_net_route_snapshot_default(&previous);
            ret = miipc_net_route_try_interface(g_net_route.cfg.primary_iface,
                                                previous.valid ? &previous : NULL,
                                                1);
            if (ret == 0) {
                g_net_route.active_is_primary = 1;
                g_net_route.active_fail_count = 0;
                g_net_route.primary_ok_count = 0;
                return 1;
            }
            if (ret == -2) {
                return -1;
            }
            g_net_route.primary_ok_count = 0;
        }
    } else {
        g_net_route.primary_ok_count = 0;
    }

    if (active_ok) {
        g_net_route.active_fail_count = 0;
        return 0;
    }
    ++g_net_route.active_fail_count;
    if (g_net_route.active_fail_count >= g_net_route.cfg.fail_threshold &&
        miipc_net_route_can_switch() &&
        miipc_net_route_find_backup(backup, sizeof(backup)) == 0) {
        g_net_route.active_fail_count = 0;
        return 1;
    }
    return active_ok ? 0 : -1;
}

static void* miipc_net_route_thread(void* arg)
{
    uint32_t sleep_left;
    uint32_t step;

    (void)arg;
    while (g_net_route.running) {
        (void)miipc_net_route_check_once();
        sleep_left = (uint32_t)g_net_route.cfg.check_interval_ms;
        while (g_net_route.running && sleep_left > 0) {
            step = sleep_left > 200u ? 200u : sleep_left;
            mi_thread_sleep(step);
            sleep_left -= step;
        }
    }
    return NULL;
}
#else
int miipc_net_route_check_once(void)
{
    return 0;
}
#endif

int miipc_net_route_start(const miipc_net_route_config_t* cfg)
{
#if MI_BASE_OS_POSIX && !defined(MIIPC_TXW82X_MINIMAL)
    thread_config_t thread_cfg;
    miipc_net_route_snapshot_t initial_route;

    if (g_net_route.thread_started) {
        return 0;
    }
    memset(&g_net_route, 0, sizeof(g_net_route));
    if (cfg != NULL) {
        g_net_route.cfg = *cfg;
    } else {
        miipc_net_route_default_config(&g_net_route.cfg);
    }
    g_net_route.cfg.check_interval_ms =
        miipc_net_route_positive_or_default(g_net_route.cfg.check_interval_ms,
                                            MIIPC_NET_ROUTE_DEFAULT_INTERVAL_MS);
    g_net_route.cfg.ping_timeout_ms =
        miipc_net_route_positive_or_default(g_net_route.cfg.ping_timeout_ms,
                                            MIIPC_NET_ROUTE_DEFAULT_PING_TIMEOUT_MS);
    g_net_route.cfg.fail_threshold =
        miipc_net_route_positive_or_default(g_net_route.cfg.fail_threshold,
                                            MIIPC_NET_ROUTE_DEFAULT_FAIL_THRESHOLD);
    g_net_route.cfg.recover_threshold =
        miipc_net_route_positive_or_default(g_net_route.cfg.recover_threshold,
                                            MIIPC_NET_ROUTE_DEFAULT_RECOVER_THRESHOLD);
    g_net_route.cfg.switch_cooldown_ms =
        miipc_net_route_positive_or_default(g_net_route.cfg.switch_cooldown_ms,
                                            MIIPC_NET_ROUTE_DEFAULT_SWITCH_COOLDOWN_MS);
    if (!g_net_route.cfg.enable) {
        return 0;
    }
    if (g_net_route.cfg.primary_iface[0] == '\0') {
        miipc_net_route_copy_text(g_net_route.cfg.primary_iface,
                                  sizeof(g_net_route.cfg.primary_iface),
                                  "eth0");
    }
    if (g_net_route.cfg.backup_ifaces[0] == '\0') {
        miipc_net_route_copy_text(g_net_route.cfg.backup_ifaces,
                                  sizeof(g_net_route.cfg.backup_ifaces),
                                  "wlan0,usb0");
    }
    if (g_net_route.cfg.probe_host[0] == '\0') {
        miipc_net_route_copy_text(g_net_route.cfg.probe_host,
                                  sizeof(g_net_route.cfg.probe_host),
                                  "82.157.168.94");
    }
    if (!miipc_net_route_is_safe_iface(g_net_route.cfg.primary_iface) ||
        (g_net_route.cfg.primary_gateway[0] != '\0' &&
         !miipc_net_route_is_safe_host(g_net_route.cfg.primary_gateway)) ||
        !miipc_net_route_is_safe_host(g_net_route.cfg.probe_host)) {
        printf("MiIpc route monitor invalid config primary=%s gateway=%s probe=%s\n",
               g_net_route.cfg.primary_iface,
               g_net_route.cfg.primary_gateway,
               g_net_route.cfg.probe_host);
        fflush(stdout);
        return -1;
    }

    g_net_route.running = 1;
    if (miipc_net_route_snapshot_default(&initial_route) == 0) {
        miipc_net_route_copy_text(g_net_route.current_iface,
                                  sizeof(g_net_route.current_iface),
                                  initial_route.iface);
        g_net_route.active_is_primary =
            strcmp(initial_route.iface, g_net_route.cfg.primary_iface) == 0;
    } else {
        g_net_route.active_is_primary = 1;
        miipc_net_route_copy_text(g_net_route.current_iface,
                                  sizeof(g_net_route.current_iface),
                                  g_net_route.cfg.primary_iface);
    }

    memset(&thread_cfg, 0, sizeof(thread_cfg));
    thread_cfg.name = "MiIpcRoute";
    thread_cfg.stack_size = MIIPC_NET_ROUTE_THREAD_STACK_SIZE;
    thread_cfg.detached = 0;
    if (mi_thread_create(&g_net_route.thread, miipc_net_route_thread, NULL, &thread_cfg) != MI_THREAD_OK) {
        g_net_route.running = 0;
        return -1;
    }
    g_net_route.thread_started = 1;
    printf("MiIpc route monitor started primary=%s backups=%s probe=%s interval=%dms cooldown=%dms\n",
           g_net_route.cfg.primary_iface,
           g_net_route.cfg.backup_ifaces,
           g_net_route.cfg.probe_host,
           g_net_route.cfg.check_interval_ms,
           g_net_route.cfg.switch_cooldown_ms);
    fflush(stdout);
    return 0;
#else
    (void)cfg;
    return 0;
#endif
}

void miipc_net_route_stop(void)
{
#if MI_BASE_OS_POSIX && !defined(MIIPC_TXW82X_MINIMAL)
    if (!g_net_route.thread_started) {
        return;
    }
    g_net_route.running = 0;
    if (mi_thread_join(&g_net_route.thread, NULL, MIIPC_NET_ROUTE_JOIN_TIMEOUT_MS) != MI_THREAD_OK) {
        printf("MiIpc route monitor join failed\n");
        fflush(stdout);
    }
    g_net_route.thread_started = 0;
#endif
}

const char* miipc_net_route_current_iface(void)
{
    return g_net_route.current_iface;
}
