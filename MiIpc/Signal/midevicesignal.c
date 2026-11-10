#include "midevicesignal.h"
#include "../App/miipcappinfo.h"
#include "../DevMsg/midevmsg.h"
#include "../JSON/cJSON.h"
#include "../../uNet/uNetService.h"
#include "../../MiBase/include/mibase.h"
#include "../../MiBase/include/mimutex.h"
#include "../../MiSig/include/misig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIIPC_SIGNAL_DEFAULT_IP "82.157.168.94"
#define MIIPC_SIGNAL_DEFAULT_PORT 5000
#define MIIPC_SIGNAL_DEFAULT_RECONNECT_MS 3000u
#define MIIPC_SIGNAL_DEFAULT_RECONNECT_MAX_MS 30000u
#define MIIPC_SIGNAL_DEFAULT_CONNECT_TIMEOUT_MS 10000u
#define MIIPC_SIGNAL_DEFAULT_HEARTBEAT_MS 60000u
#define MIIPC_SIGNAL_DEFAULT_WORKER_IDLE_MS 50u
#define MIIPC_SIGNAL_DEFAULT_WORKER_LOOP_MS 500u
#define MIIPC_SIGNAL_MIN_RECONNECT_MS 500u
#define MIIPC_SIGNAL_JSON_MAX 512

typedef struct mi_device_signal_state {
    mi_unet_t* net;
    mi_Mutex* lock;
    uint32_t conid;
    uint32_t last_connect_ms;
    uint32_t next_connect_ms;
    uint32_t reconnect_ms;
    uint32_t reconnect_max_ms;
    uint32_t current_reconnect_ms;
    uint32_t connect_timeout_ms;
    uint32_t heartbeat_ms;
    uint32_t last_heartbeat_ms;
    uint32_t heartbeat_sent;
    uint32_t worker_idle_sleep_ms;
    uint32_t worker_loop_interval_ms;
    uint32_t connect_attempts;
    uint32_t connect_failures;
    uint32_t disconnects;
    uint32_t reconnects;
    uint16_t server_port;
    uint16_t channel;
    int ctl_ver;
    int started;
    int connected;
    int connecting;
    int login_sent;
    int heartbeat_enable;
    int stop_registered;
    mi_device_signal_status_t status;
    char server_ip[64];
    mi_device_signal_message_cb on_message;
    void* on_message_user;
    mi_device_signal_status_cb on_status;
    void* on_status_user;
} mi_device_signal_state_t;

static mi_device_signal_state_t g_signal;

static void signal_lock(void)
{
    if (g_signal.lock != NULL) {
        g_signal.lock->Lock(g_signal.lock);
    }
}

static void signal_unlock(void)
{
    if (g_signal.lock != NULL) {
        g_signal.lock->UnLock(g_signal.lock);
    }
}

static void copy_text(char* dst, int dst_len, const char* src)
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

static uint32_t normalize_reconnect_ms(uint32_t value)
{
    if (value == 0) {
        return MIIPC_SIGNAL_DEFAULT_RECONNECT_MS;
    }
    return value < MIIPC_SIGNAL_MIN_RECONNECT_MS ? MIIPC_SIGNAL_MIN_RECONNECT_MS : value;
}

static uint32_t normalize_reconnect_max_ms(uint32_t base_ms, uint32_t max_ms)
{
    if (max_ms == 0) {
        max_ms = MIIPC_SIGNAL_DEFAULT_RECONNECT_MAX_MS;
    }
    return max_ms < base_ms ? base_ms : max_ms;
}

static uint32_t signal_backoff_next(uint32_t current_ms, uint32_t base_ms, uint32_t max_ms)
{
    uint32_t next;
    if (current_ms == 0) {
        return base_ms;
    }
    next = current_ms * 2u;
    if (next < current_ms || next > max_ms) {
        next = max_ms;
    }
    return next < base_ms ? base_ms : next;
}

static const char* signal_status_name(mi_device_signal_status_t status)
{
    switch (status) {
    case MI_DEVICE_SIGNAL_STATUS_STOPPED:
        return "stopped";
    case MI_DEVICE_SIGNAL_STATUS_CONNECTING:
        return "connecting";
    case MI_DEVICE_SIGNAL_STATUS_CONNECTED:
        return "connected";
    case MI_DEVICE_SIGNAL_STATUS_RECONNECT_WAIT:
        return "reconnect_wait";
    default:
        return "unknown";
    }
}

static void signal_set_status(mi_device_signal_status_t status, uint32_t conid, const char* reason)
{
    mi_device_signal_status_cb callback;
    void* callback_user;
    int changed;

    signal_lock();
    changed = (g_signal.status != status);
    g_signal.status = status;
    callback = g_signal.on_status;
    callback_user = g_signal.on_status_user;
    signal_unlock();

    if (!changed) {
        return;
    }

    printf("MiIpc device signal status=%s conid=%u reason=%s\n",
           signal_status_name(status),
           conid,
           reason != NULL ? reason : "");
    fflush(stdout);
    if (callback != NULL) {
        callback(status, conid, reason, callback_user);
    }
}

void mi_device_signal_default_config(mi_device_signal_config_t* cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->server_ip = MIIPC_SIGNAL_DEFAULT_IP;
    cfg->server_port = MIIPC_SIGNAL_DEFAULT_PORT;
    cfg->channel = 0;
    cfg->ctl_ver = 2;
    cfg->reconnect_ms = MIIPC_SIGNAL_DEFAULT_RECONNECT_MS;
    cfg->reconnect_max_ms = MIIPC_SIGNAL_DEFAULT_RECONNECT_MAX_MS;
    cfg->connect_timeout_ms = MIIPC_SIGNAL_DEFAULT_CONNECT_TIMEOUT_MS;
    cfg->heartbeat_enable = 1;
    cfg->heartbeat_ms = MIIPC_SIGNAL_DEFAULT_HEARTBEAT_MS;
    cfg->worker_idle_sleep_ms = MIIPC_SIGNAL_DEFAULT_WORKER_IDLE_MS;
    cfg->worker_loop_interval_ms = MIIPC_SIGNAL_DEFAULT_WORKER_LOOP_MS;
}

void mi_device_signal_set_message_callback(mi_device_signal_message_cb callback, void* user)
{
    signal_lock();
    g_signal.on_message = callback;
    g_signal.on_message_user = user;
    signal_unlock();
}

void mi_device_signal_set_status_callback(mi_device_signal_status_cb callback, void* user)
{
    signal_lock();
    g_signal.on_status = callback;
    g_signal.on_status_user = user;
    signal_unlock();
}

static int signal_send_dev_login(void)
{
    miipc_app_info_t app_info;
    cJSON* root;
    char* json;
    int ret;

    miipc_app_info_get(&app_info);
    root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }

    cJSON_AddStringToObject(root, "Url", "OnDevLogin");
    cJSON_AddNumberToObject(root, "RId", 0);
    cJSON_AddNumberToObject(root, "UId", app_info.uid);
    cJSON_AddStringToObject(root, "DevId", app_info.dev_id);
    cJSON_AddNumberToObject(root, "DevRandId", app_info.dev_rand_id);
    cJSON_AddStringToObject(root, "Series", app_info.series);
    cJSON_AddStringToObject(root, "BindCode", app_info.bindcode);

    //ch2000000040:fe70c36866ad:1111  4d4e3f84091e2fb2c2a3a3e0b98493a8c48dfb4b
    {
        char Sig[128] = "";
        char TipStr[128] = "";
       // sprintf(TipStr, "%s:%s:%d", app_info.dev_id, app_info.prikey, 1111);// app_info.dev_rand_id);
        sprintf(TipStr, "%s:%s:%d", app_info.dev_id, app_info.prikey, app_info.dev_rand_id);

       int len = ComputeHmacToBuffer("sha-1", TipStr, Sig,128);
       cJSON_AddStringToObject(root, "Sign", Sig);
    }

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return -1;
    }
    ret = mi_device_signal_send_json(json);
    cJSON_free(json);
    return ret;
}

static int signal_send_dev_heart(void)
{
    char last_url[96];
    miipc_app_info_t app_info;
    mi_unet_t* net;
    cJSON* root;
    char* json;
    uint32_t request_id;
    int rec_count;
    int ret;

    signal_lock();
    net = g_signal.net;
    signal_unlock();

    miipc_app_info_get(&app_info);
    request_id = mi_unet_next_request_id(net);
    rec_count = mi_unet_recv_count(net);
    copy_text(last_url, sizeof(last_url), mi_devmsg_get_last_method());

    root = cJSON_CreateObject();
    if (root == NULL) {
        return -1;
    }
    cJSON_AddStringToObject(root, "Url", "DevHeart");
    cJSON_AddStringToObject(root, "DevId", app_info.dev_id);
    cJSON_AddNumberToObject(root, "DevRandId", (double)app_info.dev_rand_id);
    cJSON_AddNumberToObject(root, "UId", (double)app_info.uid);
    cJSON_AddNumberToObject(root, "RecCount", rec_count);
    cJSON_AddNumberToObject(root, "RId", (double)request_id);
    cJSON_AddStringToObject(root, "LstUrl", last_url);
    cJSON_AddNumberToObject(root, "Cloud", 0);

    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return -1;
    }
    ret = mi_device_signal_send_json(json);
    cJSON_free(json);
    return ret;
}

static void signal_maybe_send_heartbeat(void)
{
    uint32_t now = mi_getTick32();
    uint32_t heartbeat_ms;
    uint32_t last_heartbeat_ms;
    int should_send = 0;

    signal_lock();
    if (g_signal.connected && g_signal.login_sent && g_signal.heartbeat_enable && g_signal.net != NULL) {
        heartbeat_ms = g_signal.heartbeat_ms ? g_signal.heartbeat_ms : MIIPC_SIGNAL_DEFAULT_HEARTBEAT_MS;
        last_heartbeat_ms = g_signal.last_heartbeat_ms;
        if (last_heartbeat_ms == 0) {
            g_signal.last_heartbeat_ms = now;
        } else if ((uint32_t)(now - last_heartbeat_ms) >= heartbeat_ms) {
            g_signal.last_heartbeat_ms = now;
            should_send = 1;
        }
    }
    signal_unlock();

    if (should_send && signal_send_dev_heart() > 0) {
        signal_lock();
        g_signal.heartbeat_sent++;
        signal_unlock();
        printf("MiIpc device signal DevHeart sent\n");
        fflush(stdout);
    }
}

static void signal_enter_reconnect_wait(const char* reason)
{
    uint32_t now = mi_getTick32();
    uint32_t conid;
    uint32_t wait_ms;
    int started;

    signal_lock();
    started = g_signal.started;
    conid = g_signal.conid;
    g_signal.connecting = 0;
    g_signal.connected = 0;
    g_signal.login_sent = 0;
    g_signal.last_heartbeat_ms = 0;
    g_signal.conid = 0;
    if (!started) {
        signal_unlock();
        signal_set_status(MI_DEVICE_SIGNAL_STATUS_STOPPED, conid, reason);
        return;
    }
    if (g_signal.status != MI_DEVICE_SIGNAL_STATUS_RECONNECT_WAIT) {
        g_signal.reconnects++;
    }
    wait_ms = g_signal.current_reconnect_ms ? g_signal.current_reconnect_ms : g_signal.reconnect_ms;
    if (wait_ms == 0) {
        wait_ms = MIIPC_SIGNAL_DEFAULT_RECONNECT_MS;
    }
    if (wait_ms > g_signal.reconnect_max_ms) {
        wait_ms = g_signal.reconnect_max_ms;
    }
    g_signal.next_connect_ms = now + wait_ms;
    g_signal.current_reconnect_ms = signal_backoff_next(wait_ms, g_signal.reconnect_ms, g_signal.reconnect_max_ms);
    signal_unlock();

    signal_set_status(MI_DEVICE_SIGNAL_STATUS_RECONNECT_WAIT, conid, reason);
}

static void signal_try_connect(void)
{
    char server_ip[64];
    uint16_t server_port;
    uint16_t channel;
    uint32_t now = mi_getTick32();
    uint32_t reconnect_ms;
    uint32_t connect_timeout_ms;
    uint32_t elapsed;
    mi_unet_t* net;

    signal_lock();
    if (!g_signal.started || g_signal.connected || g_signal.net == NULL) {
        signal_unlock();
        return;
    }
    connect_timeout_ms = g_signal.connect_timeout_ms ? g_signal.connect_timeout_ms : MIIPC_SIGNAL_DEFAULT_CONNECT_TIMEOUT_MS;
    if (g_signal.connecting) {
        elapsed = (uint32_t)(now - g_signal.last_connect_ms);
        signal_unlock();
        if (elapsed >= connect_timeout_ms) {
            signal_lock();
            g_signal.connecting = 0;
            g_signal.connect_failures++;
            signal_unlock();
            printf("MiIpc device signal connect timeout after %u ms\n", (unsigned int)elapsed);
            fflush(stdout);
            signal_enter_reconnect_wait("connect_timeout");
        }
        return;
    }
    reconnect_ms = g_signal.current_reconnect_ms ? g_signal.current_reconnect_ms : g_signal.reconnect_ms;
    if (g_signal.next_connect_ms != 0 && (int32_t)(now - g_signal.next_connect_ms) < 0) {
        signal_unlock();
        return;
    }
    g_signal.last_connect_ms = now;
    g_signal.next_connect_ms = 0;
    g_signal.connecting = 1;
    g_signal.connect_attempts++;
    net = g_signal.net;
    copy_text(server_ip, sizeof(server_ip), g_signal.server_ip);
    server_port = g_signal.server_port;
    channel = g_signal.channel;
    signal_unlock();

    signal_set_status(MI_DEVICE_SIGNAL_STATUS_CONNECTING, 0, "connect");
    printf("MiIpc device signal connecting %s:%u wait=%u\n",
           server_ip,
           (unsigned int)server_port,
           (unsigned int)reconnect_ms);
    fflush(stdout);
    if (!mi_unet_connect(net, server_ip, server_port, channel)) {
        signal_lock();
        g_signal.connecting = 0;
        g_signal.connect_failures++;
        signal_unlock();
        signal_enter_reconnect_wait("connect_call_failed");
    }
}

static void signal_on_loop(void* user)
{
    (void)user;
    signal_try_connect();
    signal_maybe_send_heartbeat();
}

static void signal_on_connected(void* user, uint32_t conid, const char* ip_port)
{
    (void)user;
    signal_lock();
    g_signal.conid = conid;
    g_signal.connected = 1;
    g_signal.connecting = 0;
    g_signal.login_sent = 0;
    g_signal.last_heartbeat_ms = 0;
    g_signal.next_connect_ms = 0;
    g_signal.current_reconnect_ms = g_signal.reconnect_ms;
    signal_unlock();

    printf("MiIpc device signal connected conid=%u addr=%s\n", conid, ip_port != NULL ? ip_port : "");
    fflush(stdout);
    signal_set_status(MI_DEVICE_SIGNAL_STATUS_CONNECTED, conid, "connected");
    if (signal_send_dev_login() > 0) {
        signal_lock();
        g_signal.login_sent = 1;
        g_signal.last_heartbeat_ms = mi_getTick32();
        signal_unlock();
        printf("MiIpc device signal login sent\n");
        fflush(stdout);
    }
}

static void signal_on_incoming(void* user, uint32_t conid, const char* ip_port)
{
    signal_on_connected(user, conid, ip_port);
}

static void signal_on_disconnect(void* user, uint32_t conid, const char* ip_port)
{
    (void)user;
    int matched = 0;
    signal_lock();
    if (g_signal.conid == conid) {
        matched = 1;
        g_signal.disconnects++;
    }
    signal_unlock();
    printf("MiIpc device signal disconnected conid=%u addr=%s\n", conid, ip_port != NULL ? ip_port : "");
    fflush(stdout);
    if (matched) {
        signal_enter_reconnect_wait("disconnect");
    }
}

static void signal_on_connect_fail(void* user, const char* ip_port)
{
    (void)user;
    int should_wait;
    signal_lock();
    should_wait = g_signal.started && g_signal.connecting && !g_signal.connected;
    if (should_wait) {
        g_signal.connect_failures++;
    }
    signal_unlock();
    printf("MiIpc device signal connect failed addr=%s\n", ip_port != NULL ? ip_port : "");
    fflush(stdout);
    if (should_wait) {
        signal_enter_reconnect_wait("connect_fail");
    }
}

static void signal_on_data(void* user, uint32_t conid, const uint8_t* data, int len)
{
    mi_device_signal_message_cb callback;
    void* callback_user;
    (void)user;

    printf("MiIpc device signal data conid=%u len=%d %.*s\n",
           conid,
           len,
           len > 256 ? 256 : len,
           data != NULL ? (const char*)data : "");
    fflush(stdout);

    signal_lock();
    callback = g_signal.on_message;
    callback_user = g_signal.on_message_user;
    signal_unlock();
    if (callback != NULL && data != NULL && len > 0) {
        callback((const char*)data, len, callback_user);
    }
}

int mi_device_signal_start(const mi_device_signal_config_t* cfg)
{
    mi_unet_callbacks_t callbacks;
    mi_device_signal_config_t default_cfg;
    uint32_t reconnect_ms;
    uint32_t reconnect_max_ms;

    if (cfg == NULL) {
        mi_device_signal_default_config(&default_cfg);
        cfg = &default_cfg;
    }

    if (g_signal.lock == NULL) {
        g_signal.lock = mi_CreateMutex(1);
        if (g_signal.lock == NULL) {
            return -1;
        }
    }

    signal_lock();
    if (g_signal.started) {
        signal_unlock();
        return 0;
    }
    reconnect_ms = normalize_reconnect_ms(cfg->reconnect_ms);
    reconnect_max_ms = normalize_reconnect_max_ms(reconnect_ms, cfg->reconnect_max_ms);
    copy_text(g_signal.server_ip, sizeof(g_signal.server_ip), cfg->server_ip ? cfg->server_ip : MIIPC_SIGNAL_DEFAULT_IP);
    g_signal.server_port = cfg->server_port ? cfg->server_port : MIIPC_SIGNAL_DEFAULT_PORT;
    g_signal.channel = cfg->channel;
    g_signal.ctl_ver = cfg->ctl_ver > 2 ? 3 : 2;
    g_signal.reconnect_ms = reconnect_ms;
    g_signal.reconnect_max_ms = reconnect_max_ms;
    g_signal.current_reconnect_ms = reconnect_ms;
    g_signal.connect_timeout_ms = cfg->connect_timeout_ms ? cfg->connect_timeout_ms : MIIPC_SIGNAL_DEFAULT_CONNECT_TIMEOUT_MS;
    g_signal.heartbeat_enable = cfg->heartbeat_enable ? 1 : 0;
    g_signal.heartbeat_ms = cfg->heartbeat_ms ? cfg->heartbeat_ms : MIIPC_SIGNAL_DEFAULT_HEARTBEAT_MS;
    g_signal.worker_idle_sleep_ms = cfg->worker_idle_sleep_ms ? cfg->worker_idle_sleep_ms : MIIPC_SIGNAL_DEFAULT_WORKER_IDLE_MS;
    g_signal.worker_loop_interval_ms = cfg->worker_loop_interval_ms ? cfg->worker_loop_interval_ms : MIIPC_SIGNAL_DEFAULT_WORKER_LOOP_MS;
    g_signal.connect_attempts = 0;
    g_signal.connect_failures = 0;
    g_signal.disconnects = 0;
    g_signal.reconnects = 0;
    g_signal.connecting = 0;
    g_signal.connected = 0;
    g_signal.login_sent = 0;
    g_signal.last_heartbeat_ms = 0;
    g_signal.heartbeat_sent = 0;
    g_signal.conid = 0;
    g_signal.next_connect_ms = 0;
    g_signal.status = MI_DEVICE_SIGNAL_STATUS_STOPPED;
    signal_unlock();

    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.on_loop = signal_on_loop;
    callbacks.on_connected = signal_on_connected;
    callbacks.on_incoming = signal_on_incoming;
    callbacks.on_disconnect = signal_on_disconnect;
    callbacks.on_connect_fail = signal_on_connect_fail;
    callbacks.on_data = signal_on_data;

    g_signal.net = mi_unet_create(&callbacks);
    if (g_signal.net == NULL) {
        return -1;
    }
    mi_unet_set_ctl_ver(g_signal.net, g_signal.ctl_ver);
    mi_unet_set_recv_queue_limit(g_signal.net, 256);
    mi_unet_set_worker_timing(g_signal.net, g_signal.worker_idle_sleep_ms, g_signal.worker_loop_interval_ms);
    if (!mi_unet_start(g_signal.net, "0.0.0.0", 0)) {
        mi_unet_destroy(g_signal.net);
        g_signal.net = NULL;
        return -1;
    }

    signal_lock();
    g_signal.started = 1;
    g_signal.last_connect_ms = 0;
    g_signal.next_connect_ms = 0;
    signal_unlock();
    if (!g_signal.stop_registered) {
        atexit(mi_device_signal_stop);
        g_signal.stop_registered = 1;
    }
    signal_try_connect();
    return 0;
}

void mi_device_signal_stop(void)
{
    mi_unet_t* net;
    uint32_t conid;

    signal_lock();
    net = g_signal.net;
    conid = g_signal.conid;
    g_signal.net = NULL;
    g_signal.started = 0;
    g_signal.connected = 0;
    g_signal.connecting = 0;
    g_signal.login_sent = 0;
    g_signal.last_heartbeat_ms = 0;
    g_signal.conid = 0;
    g_signal.next_connect_ms = 0;
    signal_unlock();

    if (net != NULL) {
        mi_unet_destroy(net);
    }
    signal_set_status(MI_DEVICE_SIGNAL_STATUS_STOPPED, conid, "stop");
}

int mi_device_signal_is_connected(void)
{
    int connected;
    signal_lock();
    connected = g_signal.connected;
    signal_unlock();
    return connected;
}

int mi_device_signal_send_json(const char* json)
{
    mi_unet_t* net;
    uint32_t conid;
    int connected;
    int len;

    if (json == NULL) {
        return -1;
    }
    len = (int)strlen(json);
    if (len <= 0) {
        return -1;
    }

    signal_lock();
    net = g_signal.net;
    conid = g_signal.conid;
    connected = g_signal.connected;
    signal_unlock();

    if (net == NULL || conid == 0 || !connected) {
        return -1;
    }
    return mi_unet_send(net, conid, (const uint8_t*)json, len);
}

void mi_device_signal_get_stats(mi_device_signal_stats_t* stats)
{
    if (stats == NULL) {
        return;
    }
    memset(stats, 0, sizeof(*stats));
    signal_lock();
    stats->status = g_signal.status;
    stats->started = g_signal.started;
    stats->connected = g_signal.connected;
    stats->login_sent = g_signal.login_sent;
    stats->conid = g_signal.conid;
    stats->connect_attempts = g_signal.connect_attempts;
    stats->connect_failures = g_signal.connect_failures;
    stats->disconnects = g_signal.disconnects;
    stats->reconnects = g_signal.reconnects;
    stats->last_connect_ms = g_signal.last_connect_ms;
    stats->next_connect_ms = g_signal.next_connect_ms;
    stats->current_reconnect_ms = g_signal.current_reconnect_ms;
    stats->heartbeat_enable = g_signal.heartbeat_enable;
    stats->heartbeat_sent = g_signal.heartbeat_sent;
    stats->heartbeat_ms = g_signal.heartbeat_ms;
    signal_unlock();
}

int mi_device_signal_get_info(char* out, int out_len)
{
    mi_unet_t* net;
    miipc_app_info_t app_info;
    int n = 0;
    uint32_t now;
    uint32_t next_in_ms;

    if (out == NULL || out_len <= 0) {
        return 0;
    }
    out[0] = '\0';

    now = mi_getTick32();
    miipc_app_info_get(&app_info);
    signal_lock();
    next_in_ms = 0;
    if (g_signal.next_connect_ms != 0 && (int32_t)(g_signal.next_connect_ms - now) > 0) {
        next_in_ms = (uint32_t)(g_signal.next_connect_ms - now);
    }
    n = snprintf(out,
                 (size_t)out_len,
                 "device_signal status=%s started=%d connected=%d connecting=%d login=%d conid=%u server=%s:%u device=%s attempts=%u failures=%u disconnects=%u reconnects=%u devheart_enable=%d devheart_sent=%u devheart_ms=%u reconnect_ms=%u next_in_ms=%u worker_idle_ms=%u loop_ms=%u\n",
                 signal_status_name(g_signal.status),
                 g_signal.started,
                 g_signal.connected,
                 g_signal.connecting,
                 g_signal.login_sent,
                 g_signal.conid,
                 g_signal.server_ip,
                 (unsigned int)g_signal.server_port,
                 app_info.dev_id,
                 g_signal.connect_attempts,
                 g_signal.connect_failures,
                 g_signal.disconnects,
                 g_signal.reconnects,
                 g_signal.heartbeat_enable,
                 g_signal.heartbeat_sent,
                 g_signal.heartbeat_ms,
                 g_signal.current_reconnect_ms,
                 next_in_ms,
                 g_signal.worker_idle_sleep_ms,
                 g_signal.worker_loop_interval_ms);
    net = g_signal.net;
    signal_unlock();

    if (n < 0) {
        out[0] = '\0';
        return 0;
    }
    if (n < out_len && net != NULL) {
        mi_unet_get_info(net, out + n, out_len - n, 0);
    }
    return (int)strlen(out);
}
