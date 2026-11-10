// MiIpc.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(MIIPC_TXW82X_MINIMAL)
#include <signal.h>
#include <errno.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#endif
#include "miipc.h"
#include "App/miipcosd.h"
#include "Http/mihttpserver.h"
#include "Http/miwebrtcbridge.h"
#include "Rtsp/miipcrtspserver.h"
#if !defined(MIIPC_TXW82X_MINIMAL)
#include "mionvif.h"
#include "DevMsg/midevmsg.h"
#include "Signal/midevicesignal.h"
#include "Signal/midevicesignalconsumer.h"
#include "App/miipcappinfo.h"
#include "Broadcast/miipcbroadcast.h"
#include "Config/miipcappconfig.h"
#include "Config/miipccfgdb.h"
#include "Config/miipcsignalconfig.h"
#include "Log/miipclog.h"
#include "Network/miipcnetroute.h"
#include "SdMgr.h"
#include "SdStorage/miipcsdstorage.h"
#if defined(__linux__)
#include <fcntl.h>
#endif
#endif
#include "Soc/IHard.h"
#include "mithread.h"

#if defined(MIIPC_TXW82X_MINIMAL)
#define MIIPC_SD_STORAGE_DEFAULT_QUEUE_LIMIT 0
#define MIIPC_SIGNAL_CONSUMER_DEFAULT_QUEUE_LIMIT 0
#endif

#define MIIPC_STORAGE_CFG_SECTION "storage"
#define MIIPC_STORAGE_CFG_ENABLE_KEY "enable"
#define MIIPC_STORAGE_CFG_AUTO_MOUNT_KEY "auto_mount"
#define MIIPC_STORAGE_CFG_DEVICE_KEY "device"
#define MIIPC_STORAGE_CFG_MOUNT_PATH_KEY "mount_path"
#define MIIPC_STORAGE_CFG_FILESYSTEM_KEY "filesystem"
#define MIIPC_STORAGE_DEFAULT_MOUNT_PATH "/mnt/sd"
#define MIIPC_STORAGE_DEFAULT_FILESYSTEM "exfat"
#define MIIPC_RTSP_CFG_SECTION "rtsp"
#define MIIPC_RTSP_CFG_ENABLE_KEY "enable"
#define MIIPC_RTSP_CFG_LISTEN_IP_KEY "listen_ip"
#define MIIPC_RTSP_CFG_PORT_KEY "port"
#define MIIPC_RTSP_CFG_ADVERTISE_IP_KEY "advertise_ip"
#define MIIPC_RTSP_CFG_PATH_KEY "path"
#define MIIPC_RTSP_CFG_MAX_CLIENTS_KEY "max_clients"
#define MIIPC_RTSP_CFG_SOURCE_CHN_KEY "source_chn"
#define MIIPC_RTSP_CFG_UDP_KEY "enable_udp"
#define MIIPC_RTSP_CFG_TCP_KEY "enable_tcp_interleaved"
#define MIIPC_RTSP_SUB_CFG_SECTION "rtsp_sub"
#define MIIPC_RTSP_SUB_CFG_ENABLE_KEY "enable"
#define MIIPC_RTSP_SUB_CFG_PORT_KEY "port"
#define MIIPC_RTSP_SUB_CFG_PATH_KEY "path"
#define MIIPC_RTSP_SUB_CFG_SOURCE_CHN_KEY "source_chn"
#define MIIPC_ONVIF_CFG_SECTION "onvif"
#define MIIPC_ONVIF_CFG_ENABLE_KEY "enable"
#define MIIPC_ONVIF_CFG_LISTEN_IP_KEY "listen_ip"
#define MIIPC_ONVIF_CFG_HTTP_PORT_KEY "http_port"
#define MIIPC_ONVIF_CFG_ADVERTISE_IP_KEY "advertise_ip"
#define MIIPC_ONVIF_CFG_DEVICE_PATH_KEY "device_path"
#define MIIPC_ONVIF_CFG_DEVICE_SERVICE_URL_KEY "device_service_url"
#define MIIPC_ONVIF_CFG_RTSP_URL_KEY "rtsp_url"
#define MIIPC_ONVIF_CFG_UUID_KEY "uuid"
#define MIIPC_ONVIF_CFG_MANUFACTURER_KEY "manufacturer"
#define MIIPC_ONVIF_CFG_MODEL_KEY "model"
#define MIIPC_ONVIF_CFG_FIRMWARE_KEY "firmware_version"
#define MIIPC_ONVIF_CFG_SERIAL_KEY "serial_number"
#define MIIPC_ONVIF_CFG_HARDWARE_KEY "hardware_id"
#define MIIPC_ONVIF_CFG_DEVICE_NAME_KEY "device_name"
#define MIIPC_ONVIF_CFG_SCOPES_KEY "scopes"
#define MIIPC_ONVIF_CFG_ENABLE_HTTP_KEY "enable_http"
#define MIIPC_ONVIF_CFG_ENABLE_DISCOVERY_KEY "enable_discovery"
#define MIIPC_ONVIF_CFG_DISCOVERY_PORT_KEY "discovery_port"
#define MIIPC_ONVIF_CFG_MAX_HTTP_CLIENTS_KEY "max_http_clients"
#define MIIPC_ONVIF_CFG_THREAD_STACK_KEY "thread_stack_size"
#define MIIPC_ONVIF_DEFAULT_LISTEN_IP "0.0.0.0"
#define MIIPC_ONVIF_DEFAULT_MAX_HTTP_CLIENTS 4
#define MIIPC_ONVIF_DEFAULT_THREAD_STACK_SIZE (64u * 1024u)
#define MIIPC_ONVIF_DEFAULT_MANUFACTURER "MiniRTC"
#define MIIPC_ONVIF_DEFAULT_MODEL "MiIpc"
#define MIIPC_ONVIF_DEFAULT_FIRMWARE "1.0.0"
#define MIIPC_ONVIF_DEFAULT_HARDWARE "MiIpc"
#define MIIPC_ONVIF_DEFAULT_DEVICE_NAME "MiIpc Camera"
#define MIIPC_ONVIF_DEFAULT_SCOPES \
    "onvif://www.onvif.org/type/video_encoder " \
    "onvif://www.onvif.org/type/NetworkVideoTransmitter"
#if defined(_WIN32)
#define MIIPC_DEFAULT_HTTP_PORT 10010
#define MIIPC_DEFAULT_HTTP_URL "http://0.0.0.0:10010"
#else
#define MIIPC_DEFAULT_HTTP_PORT 80
#define MIIPC_DEFAULT_HTTP_URL "http://0.0.0.0:80"
#endif
#if defined(MIIPC_TXW82X_MINIMAL)
#define MIIPC_DEFAULT_RUN_SLEEP_MS 10u
#else
#define MIIPC_DEFAULT_RUN_SLEEP_MS 100u
#endif
#if defined(_WIN32)
#define MIIPC_DEFAULT_CHN1_PIC_WH H640_360
#define MIIPC_DEFAULT_CHN1_FPS 30
#define MIIPC_DEFAULT_CHN1_BITRATE_KBPS 1024
#else
#define MIIPC_DEFAULT_CHN1_PIC_WH H1920_1080
#define MIIPC_DEFAULT_CHN1_FPS 15
#define MIIPC_DEFAULT_CHN1_BITRATE_KBPS 1200
#endif
#define MIIPC_DEFAULT_CHN2_PIC_WH H640_360
#define MIIPC_DEFAULT_CHN2_FPS 5
#define MIIPC_DEFAULT_CHN2_BITRATE_KBPS 512
#ifndef MIIPC_PROCESS_THREAD_STACK_SIZE
#define MIIPC_PROCESS_THREAD_STACK_SIZE (16u * 1024u)
#endif
#define MIIPC_PROCESS_THREAD_JOIN_TIMEOUT_MS 3000u

typedef struct miipc_runtime {
    miipc_config_t config;
    IHardSoc* hard_soc;
    mi_thread_t process_thread;
    volatile int started;
    volatile int stopping;
    int process_thread_started;
    int log_started;
    int signal_consumer_started;
    int storage_started;
    int broadcast_started;
    int net_route_started;
    int http_started;
    int rtsp_started;
#if !defined(MIIPC_TXW82X_MINIMAL)
    int onvif_started;
#endif
    int signal_started;
    int hardware_started;
    char rtsp_listen_ip[64];
    char rtsp_advertise_ip[64];
    char rtsp_stream_path[MIIPC_RTSP_PATH_MAX];
    char rtsp_sub_stream_path[MIIPC_RTSP_PATH_MAX];
#if !defined(MIIPC_TXW82X_MINIMAL)
    MiOnvifServer* onvif_server;
    char onvif_listen_ip[64];
    char onvif_advertise_ip[64];
    char onvif_device_path[96];
    char onvif_device_service_url[MI_ONVIF_URL_MAX];
    char onvif_rtsp_url[MI_ONVIF_URL_MAX];
    char onvif_uuid[MI_ONVIF_TEXT_MAX];
    char onvif_manufacturer[MI_ONVIF_TEXT_MAX];
    char onvif_model[MI_ONVIF_TEXT_MAX];
    char onvif_firmware_version[MI_ONVIF_TEXT_MAX];
    char onvif_serial_number[MI_ONVIF_TEXT_MAX];
    char onvif_hardware_id[MI_ONVIF_TEXT_MAX];
    char onvif_device_name[MI_ONVIF_TEXT_MAX];
    char onvif_scopes[MI_ONVIF_URL_MAX];
    int sd_mounted_by_miipc;
    char sd_device[MIIPC_SD_MGR_PATH_MAX];
    char sd_mount_path[MIIPC_SD_MGR_PATH_MAX];
    char storage_root[MIIPC_SD_MGR_PATH_MAX];
#endif
} miipc_runtime_t;

#if !defined(MIIPC_TXW82X_MINIMAL)
typedef struct miipc_storage_config {
    int recording_enabled;
    int auto_mount;
    miipc_sd_mgr_fs_type_t fs_type;
    char device[MIIPC_SD_MGR_PATH_MAX];
    char mount_path[MIIPC_SD_MGR_PATH_MAX];
} miipc_storage_config_t;
#endif

static miipc_runtime_t g_miipc;

static void MiIpcPollInternal(void);
static int MiIpcStartProcessThread(void);
static void MiIpcStopProcessThread(void);

#if !defined(MIIPC_TXW82X_MINIMAL)
static void MiIpcSetTextIfEmpty(char* text, int text_len, const char* fallback)
{
    if (text == NULL || text_len <= 0 || text[0] != '\0') {
        return;
    }
    snprintf(text, (size_t)text_len, "%s", fallback != NULL ? fallback : "");
    text[text_len - 1] = '\0';
}

static uint32_t MiIpcHashText(const char* text)
{
    uint32_t hash = 2166136261u;

    if (text == NULL || text[0] == '\0') {
        return hash;
    }
    while (*text != '\0') {
        hash ^= (uint8_t)*text;
        hash *= 16777619u;
        ++text;
    }
    return hash;
}

static void MiIpcBuildOnvifUuid(char* out, int out_len, const char* serial)
{
    uint32_t hash;
    uint32_t rand_id;

    if (out == NULL || out_len <= 0) {
        return;
    }
    hash = MiIpcHashText(serial);
    rand_id = miipc_app_info_get_dev_rand_id();
    snprintf(out,
             (size_t)out_len,
             "%08x-%04x-%04x-%04x-%04x%08x",
             (unsigned int)hash,
             (unsigned int)((hash >> 16) & 0xffffu),
             (unsigned int)(hash & 0xffffu),
             (unsigned int)((rand_id >> 16) & 0xffffu),
             (unsigned int)(rand_id & 0xffffu),
             (unsigned int)(hash ^ rand_id));
    out[out_len - 1] = '\0';
}
#endif

#if !defined(MIIPC_TXW82X_MINIMAL)
static void MiIpcRuntimeSignalHandler(int sig)
{
    (void)sig;
    g_miipc.stopping = 1;
}
#endif

#if !defined(_WIN32)
__attribute__((weak)) struct IHardSoc* CreateHardImp(void)
{
    return NULL;
}
#endif

static int MiIpcDefaultGetAudio(uint8_t* buf, int buflen, int rz, int chn)
{
    (void)buf;
    (void)buflen;
    (void)rz;
    (void)chn;
    return 0;
}

static void OnAVStream(int chn, struct Stream_Info* info, int count)
{
    (void)miipc_input_av_stream(chn, info, count);
}

#if !defined(MIIPC_TXW82X_MINIMAL)
static void MiIpcInstallSignalHandlers(void)
{
#if !defined(_WIN32)
    struct sigaction ignore_action;
    struct sigaction stop_action;

    memset(&ignore_action, 0, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    memset(&stop_action, 0, sizeof(stop_action));
    stop_action.sa_handler = MiIpcRuntimeSignalHandler;
    sigemptyset(&stop_action.sa_mask);
#if defined(SIGHUP)
    sigaction(SIGHUP, &ignore_action, NULL);
#endif
#if defined(SIGPIPE)
    sigaction(SIGPIPE, &ignore_action, NULL);
#endif
#if defined(SIGINT)
    sigaction(SIGINT, &stop_action, NULL);
#endif
#if defined(SIGTERM)
    sigaction(SIGTERM, &stop_action, NULL);
#endif
#else
#if defined(SIGHUP)
    signal(SIGHUP, SIG_IGN);
#endif
#if defined(SIGPIPE)
    signal(SIGPIPE, SIG_IGN);
#endif
#if defined(SIGINT)
    signal(SIGINT, MiIpcRuntimeSignalHandler);
#endif
#if defined(SIGTERM)
    signal(SIGTERM, MiIpcRuntimeSignalHandler);
#endif
#endif
}

#if !defined(_WIN32)
static void MiIpcFatalSignalHandler(int sig)
{
    const char* name = "UNKNOWN";

    switch (sig) {
#if defined(SIGSEGV)
    case SIGSEGV:
        name = "SIGSEGV";
        break;
#endif
#if defined(SIGBUS)
    case SIGBUS:
        name = "SIGBUS";
        break;
#endif
#if defined(SIGABRT)
    case SIGABRT:
        name = "SIGABRT";
        break;
#endif
#if defined(SIGILL)
    case SIGILL:
        name = "SIGILL";
        break;
#endif
#if defined(SIGFPE)
    case SIGFPE:
        name = "SIGFPE";
        break;
#endif
    default:
        break;
    }

    (void)write(STDERR_FILENO, "MiIpc fatal signal: ", 20);
    (void)write(STDERR_FILENO, name, strlen(name));
    (void)write(STDERR_FILENO, "\n", 1);
    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

static void MiIpcInstallFatalSignalHandlers(void)
{
#if !defined(_WIN32)
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = MiIpcFatalSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
#if defined(SIGSEGV)
    sigaction(SIGSEGV, &action, NULL);
#endif
#if defined(SIGBUS)
    sigaction(SIGBUS, &action, NULL);
#endif
#if defined(SIGABRT)
    sigaction(SIGABRT, &action, NULL);
#endif
#if defined(SIGILL)
    sigaction(SIGILL, &action, NULL);
#endif
#if defined(SIGFPE)
    sigaction(SIGFPE, &action, NULL);
#endif
#endif
}
#else
static void MiIpcInstallSignalHandlers(void)
{
}

static void MiIpcInstallFatalSignalHandlers(void)
{
}
#endif

static void OnWebRtcRequestIdr(int chn, void* user_data)
{
    IHardSoc* soc = (IHardSoc*)user_data;

    if (soc != NULL && soc->ReqIDR != NULL) {
        soc->ReqIDR(soc, chn);
    }
}

static void OnRtspRequestIdr(int chn, void* user_data)
{
    IHardSoc* soc = (IHardSoc*)user_data;

    if (soc != NULL && soc->ReqIDR != NULL) {
        soc->ReqIDR(soc, chn);
    }
}

#if !defined(MIIPC_TXW82X_MINIMAL)
static uint16_t MiIpcHttpPortFromUrl(const char* url)
{
    const char* host;
    const char* slash;
    const char* colon;
    unsigned long port;

    if (url == NULL || url[0] == '\0') {
        return (uint16_t)MIIPC_DEFAULT_HTTP_PORT;
    }
    host = strstr(url, "://");
    host = host != NULL ? host + 3 : url;
    slash = strchr(host, '/');
    colon = strchr(host, ':');
    if (colon == NULL || (slash != NULL && colon > slash)) {
        return (uint16_t)MIIPC_DEFAULT_HTTP_PORT;
    }
    port = strtoul(colon + 1, NULL, 10);
    if (port == 0 || port > 65535ul) {
        return (uint16_t)MIIPC_DEFAULT_HTTP_PORT;
    }
    return (uint16_t)port;
}

static int MiIpcOnvifHttpHandler(const char* method,
                                 const char* uri,
                                 const char* body,
                                 int body_len,
                                 char* response,
                                 int response_len,
                                 int* status,
                                 const char** content_type,
                                 void* user_data)
{
    return mi_onvif_server_handle_http_request((MiOnvifServer*)user_data,
                                               method,
                                               uri,
                                               body,
                                               body_len,
                                               response,
                                               response_len,
                                               status,
                                               content_type);
}
#endif

static void OnWebRtcTargetBitrate(int chn, uint32_t bitrate_bps, void* user_data)
{
    IHardSoc* soc = (IHardSoc*)user_data;
    int bitrate_kbps;

    if (soc == NULL || soc->AdjustEncoderRate == NULL) {
        return;
    }
    bitrate_kbps = (int)((bitrate_bps + 999u) / 1000u);
    soc->AdjustEncoderRate(soc, chn, bitrate_kbps);
}

#if !defined(MIIPC_TXW82X_MINIMAL)
static void OnDeviceSignalMessage(const char* data, int len, void* user_data)
{
    (void)user_data;
    if (mi_device_signal_consumer_post(data, len) != 0) {
        printf("MiIpc device control queue failed len=%d\n", len);
      //  miipc_log_write("WARN", "MiIpc device control queue failed len=%d", len);
        fflush(stdout);
    }
}

static int OnGetRtcSdpOffer(const mi_devmsg_rtc_sdp_offer_request_t* request,
    mi_devmsg_rtc_sdp_offer_response_t* response,
    void* user_data)
{
    mi_webrtc_offer_prepare_cfg_t cfg;
    mi_webrtc_offer_prepare_result_t result;
    int ret;

    (void)user_data;
    if (request == NULL || response == NULL) {
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    memset(&result, 0, sizeof(result));
    cfg.source_chn = request->channel;
    cfg.audio_enable = request->audio_enable;
    cfg.video_enable = request->video_enable;
    cfg.send_audio_mode = request->send_audio_mode;
    cfg.data_channel = request->data_channel;
    cfg.close_all = request->close_all;
    cfg.codec = request->codec;
    cfg.ice_url = request->ice_url;
    cfg.ice_username = request->ice_ufrag;
    cfg.ice_password = request->ice_pwd;
    cfg.ice_realm = request->ice_realm;

    ret = mi_webrtc_prepare_sdp_offer(&cfg, &result);
    if (ret != 0) {
        response->status = 500;
        response->code = 500;
        snprintf(response->err_msg, sizeof(response->err_msg), "prepare rtc offer failed: %d", ret);
        printf("MiIpc GetRtcSdpOffer prepare failed ret=%d chn=%d audio=%d video=%d iceurl=%s\n",
            ret,
            request->channel,
            request->audio_enable,
            request->video_enable,
            request->ice_url);
        fflush(stdout);
        return ret;
    }

    response->status = 200;
    response->code = 200;
    response->dialog_id = (int)result.dialogue_id;
    snprintf(response->candidate, sizeof(response->candidate), "%s", result.candidate);
    snprintf(response->sdp_offer, sizeof(response->sdp_offer), "%s", result.sdp_offer);
    return 0;
}

static int OnDevMsgResponseSend(const char* response_json, void* user_data)
{
    (void)user_data;
    return mi_device_signal_send_json(response_json);
}

static int OnGetRtcCandis(int dialogue_id, mi_devmsg_rtc_candidates_response_t* response, void* user_data)
{
    int count = 0;
    int ice_status = 0;
    int ret;

    (void)user_data;
    if (dialogue_id <= 0 || response == NULL) {
        return -1;
    }

    ret = mi_webrtc_get_local_candidates((uint32_t)dialogue_id,
        response->candidates,
        MI_DEVMSG_RTC_CANDIDATE_MAX,
        &count,
        &ice_status);
    response->dialog_id = dialogue_id;
    response->ice_status = ice_status;
    response->candidate_count = count;
    if (ret != 0 && ret != -4 && ice_status == 0) {
        response->status = 500;
        response->code = 500;
        snprintf(response->err_msg, sizeof(response->err_msg), "get candidates failed: %d", ret);
        return ret;
    }
    response->status = 200;
    response->code = 200;
    return 0;
}

static int OnSetRtcSdpAnswer(int dialogue_id, const char* sdp_answer, void* user_data)
{
    (void)user_data;
    if (dialogue_id <= 0 || sdp_answer == NULL) {
        return -1;
    }
    return mi_webrtc_apply_sdp_answer((uint32_t)dialogue_id, sdp_answer, (int)strlen(sdp_answer));
}

static int OnAddRtcCandidate(int dialogue_id, const char* candidate, void* user_data)
{
    (void)user_data;
    if (dialogue_id <= 0 || candidate == NULL || candidate[0] == '\0') {
        return 0;
    }
    return mi_webrtc_add_candidate((uint32_t)dialogue_id, candidate, (int)strlen(candidate));
}

static int OnCloseRtcDialogue(int dialogue_id, int close_all, void* user_data)
{
    (void)user_data;
    if (close_all) {
        mi_webrtc_shutdown();
        return 0;
    }
    if (dialogue_id <= 0) {
        return -1;
    }
    return mi_webrtc_close_session((uint32_t)dialogue_id);
}

static void MiIpcRegisterDevMsgCallbacks(void)
{
    miipc_osd_init(g_miipc.hard_soc);
    mi_devmsg_set_response_send_callback(OnDevMsgResponseSend, NULL);
    mi_devmsg_set_get_rtc_sdp_offer_callback(OnGetRtcSdpOffer, NULL);
    mi_devmsg_set_get_rtc_candidates_callback(OnGetRtcCandis, NULL);
    mi_devmsg_set_set_rtc_sdp_answer_callback(OnSetRtcSdpAnswer, NULL);
    mi_devmsg_set_add_rtc_candidate_callback(OnAddRtcCandidate, NULL);
    mi_devmsg_set_close_rtc_dialogue_callback(OnCloseRtcDialogue, NULL);
}

static void MiIpcClearCallbacks(void)
{
    mi_devmsg_set_response_send_callback(NULL, NULL);
    mi_devmsg_set_get_rtc_sdp_offer_callback(NULL, NULL);
    mi_devmsg_set_get_rtc_candidates_callback(NULL, NULL);
    mi_devmsg_set_set_rtc_sdp_answer_callback(NULL, NULL);
    mi_devmsg_set_add_rtc_candidate_callback(NULL, NULL);
    mi_devmsg_set_close_rtc_dialogue_callback(NULL, NULL);
    mi_device_signal_set_message_callback(NULL, NULL);
    miipc_osd_set_hard_soc(NULL);
    mi_webrtc_set_request_idr_callback(NULL, NULL);
    mi_webrtc_set_encoder_bitrate_callback(NULL, NULL);
}
#else
static void MiIpcRegisterDevMsgCallbacks(void)
{
    miipc_osd_init(g_miipc.hard_soc);
}

static void MiIpcClearCallbacks(void)
{
    miipc_osd_set_hard_soc(NULL);
    mi_webrtc_set_request_idr_callback(NULL, NULL);
    mi_webrtc_set_encoder_bitrate_callback(NULL, NULL);
}
#endif

static void MiIpcNormalizeConfig(miipc_config_t* cfg)
{
    if (cfg == NULL) {
        return;
    }
    if (cfg->http_listen_url == NULL || cfg->http_listen_url[0] == '\0') {
        cfg->http_listen_url = MIIPC_DEFAULT_HTTP_URL;
    }
    if (cfg->storage_root == NULL || cfg->storage_root[0] == '\0') {
        cfg->storage_root = "sd";
    }
    if (cfg->storage_queue_limit <= 0) {
        cfg->storage_queue_limit = MIIPC_SD_STORAGE_DEFAULT_QUEUE_LIMIT;
    }
    if (cfg->signal_consumer_queue_limit <= 0) {
        cfg->signal_consumer_queue_limit = MIIPC_SIGNAL_CONSUMER_DEFAULT_QUEUE_LIMIT;
    }
    if (cfg->rtsp_listen_ip == NULL || cfg->rtsp_listen_ip[0] == '\0') {
        cfg->rtsp_listen_ip = MIIPC_RTSP_DEFAULT_LISTEN_IP;
    }
    if (cfg->rtsp_listen_port == 0) {
        cfg->rtsp_listen_port = MIIPC_RTSP_DEFAULT_PORT;
    }
    if (cfg->rtsp_stream_path == NULL || cfg->rtsp_stream_path[0] == '\0') {
        cfg->rtsp_stream_path = MIIPC_RTSP_DEFAULT_PATH;
    }
    if (cfg->rtsp_sub_listen_port == 0) {
        cfg->rtsp_sub_listen_port = MIIPC_RTSP_DEFAULT_SUB_PORT;
    }
    if (cfg->rtsp_sub_stream_path == NULL || cfg->rtsp_sub_stream_path[0] == '\0') {
        cfg->rtsp_sub_stream_path = MIIPC_RTSP_DEFAULT_SUB_PATH;
    }
    if (cfg->rtsp_sub_source_chn < 0) {
        cfg->rtsp_sub_source_chn = 1;
    }
    if (cfg->rtsp_max_clients <= 0) {
        cfg->rtsp_max_clients = MIIPC_RTSP_DEFAULT_MAX_CLIENTS;
    }
    if (cfg->rtsp_enable_udp == 0 && cfg->rtsp_enable_tcp_interleaved == 0) {
        cfg->rtsp_enable_udp = 1;
        cfg->rtsp_enable_tcp_interleaved = 1;
    }
#if !defined(MIIPC_TXW82X_MINIMAL)
    if (cfg->onvif_listen_ip == NULL || cfg->onvif_listen_ip[0] == '\0') {
        cfg->onvif_listen_ip = MIIPC_ONVIF_DEFAULT_LISTEN_IP;
    }
    if (cfg->onvif_http_port == 0) {
        cfg->onvif_http_port = MI_ONVIF_DEFAULT_HTTP_PORT;
    }
    if (cfg->onvif_device_path == NULL || cfg->onvif_device_path[0] == '\0') {
        cfg->onvif_device_path = MI_ONVIF_DEFAULT_DEVICE_PATH;
    }
    if (cfg->onvif_enable_http == 0 && cfg->onvif_enable_discovery == 0) {
        cfg->onvif_enable_http = 1;
        cfg->onvif_enable_discovery = 1;
    }
    if (cfg->onvif_discovery_port == 0) {
        cfg->onvif_discovery_port = MI_ONVIF_DEFAULT_DISCOVERY_PORT;
    }
    if (cfg->onvif_max_http_clients <= 0) {
        cfg->onvif_max_http_clients = MIIPC_ONVIF_DEFAULT_MAX_HTTP_CLIENTS;
    }
    if (cfg->onvif_thread_stack_size == 0) {
        cfg->onvif_thread_stack_size = MIIPC_ONVIF_DEFAULT_THREAD_STACK_SIZE;
    }
#endif
    if (cfg->run_poll_sleep_ms == 0) {
        cfg->run_poll_sleep_ms = MIIPC_DEFAULT_RUN_SLEEP_MS;
    }
    if (cfg->read_audio == NULL) {
        cfg->read_audio = MiIpcDefaultGetAudio;
    }
}

static void MiIpcNormalizeVideoCfg(struct SVideoCfg* cfg)
{
    if (cfg == NULL) {
        return;
    }
    if (cfg->chn1PicWh == HV_UnDef || cfg->chn1PicWh == 0) {
        cfg->chn1PicWh = MIIPC_DEFAULT_CHN1_PIC_WH;
    }
    if (cfg->chn1MaxFrameRate <= 0) {
        cfg->chn1MaxFrameRate = MIIPC_DEFAULT_CHN1_FPS;
    }
    if (cfg->chn1MaxCodeRate <= 0) {
        cfg->chn1MaxCodeRate = MIIPC_DEFAULT_CHN1_BITRATE_KBPS;
    }
    if (cfg->chn1Encoder == CI_UnDef) {
        cfg->chn1Encoder = CI_H264;
    }
    if (cfg->chn2PicWh == HV_UnDef || cfg->chn2PicWh == 0) {
        cfg->chn2PicWh = MIIPC_DEFAULT_CHN2_PIC_WH;
    }
    if (cfg->chn2FrameRate <= 0) {
        cfg->chn2FrameRate = MIIPC_DEFAULT_CHN2_FPS;
    }
    if (cfg->chn2CodeRate <= 0) {
        cfg->chn2CodeRate = MIIPC_DEFAULT_CHN2_BITRATE_KBPS;
    }
    if (cfg->chn2Encoder == CI_UnDef) {
        cfg->chn2Encoder = CI_H264;
    }
}

#if !defined(MIIPC_TXW82X_MINIMAL)
static void MiIpcCfgDbReadStringDefault(const char* section,
    const char* key,
    const char* default_value,
    char* out,
    int out_len)
{
    int ret;

    ret = miipc_cfg_db_read_string(section, key, default_value, out, out_len);
    if (ret == 0) {
        miipc_cfg_db_write_string(section, key, default_value != NULL ? default_value : "");
    }
}

static void MiIpcCfgDbReadIntDefault(const char* section,
    const char* key,
    int default_value,
    int* out_value)
{
    int ret;

    ret = miipc_cfg_db_read_int(section, key, default_value, out_value);
    if (ret == 0) {
        miipc_cfg_db_write_int(section, key, default_value);
    }
}

static int MiIpcParseStorageFilesystem(const char* name, miipc_sd_mgr_fs_type_t* fs_type)
{
    if (fs_type == NULL) {
        return -1;
    }
    if (name == NULL || name[0] == '\0' || strcmp(name, "auto") == 0) {
        *fs_type = MIIPC_SD_MGR_FS_AUTO;
        return 0;
    }
    if (strcmp(name, "fat32") == 0 || strcmp(name, "vfat") == 0) {
        *fs_type = MIIPC_SD_MGR_FS_FAT32;
        return 0;
    }
    if (strcmp(name, "exfat") == 0 || strcmp(name, "exfat32") == 0) {
        *fs_type = MIIPC_SD_MGR_FS_EXFAT;
        return 0;
    }
    return -1;
}

static int MiIpcLoadStorageConfig(const miipc_config_t* cfg,
                                  miipc_storage_config_t* storage_cfg)
{
    char filesystem[MIIPC_SD_MGR_FS_TYPE_MAX];
    int value;

    if (cfg == NULL || storage_cfg == NULL) {
        return -1;
    }
    memset(storage_cfg, 0, sizeof(*storage_cfg));

    value = cfg->storage_enable ? 1 : 0;
    MiIpcCfgDbReadIntDefault(MIIPC_STORAGE_CFG_SECTION,
                             MIIPC_STORAGE_CFG_ENABLE_KEY,
                             value,
                             &value);
    storage_cfg->recording_enabled = value ? 1 : 0;

    value = 1;
    MiIpcCfgDbReadIntDefault(MIIPC_STORAGE_CFG_SECTION,
                             MIIPC_STORAGE_CFG_AUTO_MOUNT_KEY,
                             value,
                             &value);
    storage_cfg->auto_mount = value ? 1 : 0;

    MiIpcCfgDbReadStringDefault(MIIPC_STORAGE_CFG_SECTION,
                                MIIPC_STORAGE_CFG_DEVICE_KEY,
                                "",
                                storage_cfg->device,
                                sizeof(storage_cfg->device));
    MiIpcCfgDbReadStringDefault(MIIPC_STORAGE_CFG_SECTION,
                                MIIPC_STORAGE_CFG_MOUNT_PATH_KEY,
                                MIIPC_STORAGE_DEFAULT_MOUNT_PATH,
                                storage_cfg->mount_path,
                                sizeof(storage_cfg->mount_path));
    MiIpcCfgDbReadStringDefault(MIIPC_STORAGE_CFG_SECTION,
                                MIIPC_STORAGE_CFG_FILESYSTEM_KEY,
                                MIIPC_STORAGE_DEFAULT_FILESYSTEM,
                                filesystem,
                                sizeof(filesystem));
    if (MiIpcParseStorageFilesystem(filesystem, &storage_cfg->fs_type) != 0) {
        printf("MiIpc storage filesystem is invalid: %s\n", filesystem);
        return -1;
    }
    return 0;
}

static int MiIpcCheckStorageWritable(const char* path)
{
#if defined(__linux__)
    static const unsigned char probe_data = 0x5a;
    char probe_path[MIIPC_SD_MGR_PATH_MAX + 32];
    int fd = -1;
    int attempt;
    int ret = -1;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    for (attempt = 0; attempt < 4; ++attempt) {
        int length = snprintf(probe_path,
                              sizeof(probe_path),
                              "%s/.miipc_sd_probe_%ld_%d",
                              path,
                              (long)getpid(),
                              attempt);
        if (length <= 0 || length >= (int)sizeof(probe_path)) {
            return -1;
        }
        fd = open(probe_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            break;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    if (fd < 0) {
        return -1;
    }
    if (write(fd, &probe_data, sizeof(probe_data)) == (ssize_t)sizeof(probe_data) &&
        fsync(fd) == 0) {
        ret = 0;
    }
    if (close(fd) != 0) {
        ret = -1;
    }
    if (unlink(probe_path) != 0) {
        ret = -1;
    }
    return ret;
#else
    (void)path;
    return 0;
#endif
}

/* A writable directory is not sufficient on Linux: without this check a
 * missing card could fall back to a directory on the root filesystem. */
static int MiIpcCheckStorageMountedWritable(const char* path)
{
#if defined(__linux__)
    miipc_sd_mgr_status_t status;
    int ret;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    memset(&status, 0, sizeof(status));
    ret = miipc_sd_mgr_get_status(NULL, path, &status);
    if (ret != MIIPC_SD_MGR_OK || !status.mounted ||
        strcmp(status.mount_path, path) != 0 || !status.writable) {
        return -1;
    }
#endif
    return MiIpcCheckStorageWritable(path);
}

static int MiIpcMountSdCard(miipc_config_t* cfg,
                             const miipc_storage_config_t* storage_cfg)
{
    if (cfg == NULL || storage_cfg == NULL) {
        return -1;
    }
    if (!storage_cfg->auto_mount) {
        return 0;
    }

#if defined(__linux__)
    {
        miipc_sd_mgr_status_t status;
        miipc_sd_mgr_mount_config_t mount_cfg;
        int ret;
        int was_mounted;
        int mounted_by_miipc = 0;
        const char* device = storage_cfg->device;

        if (storage_cfg->mount_path[0] == '\0') {
            printf("MiIpc storage mount path is empty\n");
            return -1;
        }
        if (device[0] == '\0') {
            ret = miipc_sd_mgr_find_mmc_partition(g_miipc.sd_device,
                                                  sizeof(g_miipc.sd_device));
            if (ret != MIIPC_SD_MGR_OK) {
                printf("MiIpc SD partition discovery failed: %s\n",
                       miipc_sd_mgr_result_text(ret));
                return -1;
            }
            device = g_miipc.sd_device;
        } else {
            snprintf(g_miipc.sd_device, sizeof(g_miipc.sd_device), "%s", device);
            g_miipc.sd_device[sizeof(g_miipc.sd_device) - 1] = '\0';
            device = g_miipc.sd_device;
        }

        snprintf(g_miipc.sd_mount_path,
                 sizeof(g_miipc.sd_mount_path),
                 "%s",
                 storage_cfg->mount_path);
        g_miipc.sd_mount_path[sizeof(g_miipc.sd_mount_path) - 1] = '\0';

        memset(&status, 0, sizeof(status));
        ret = miipc_sd_mgr_get_status(device, g_miipc.sd_mount_path, &status);
        if (ret != MIIPC_SD_MGR_OK) {
            printf("MiIpc SD status query failed: %s\n",
                   miipc_sd_mgr_result_text(ret));
            return -1;
        }
        was_mounted = status.mounted;
        if (was_mounted && strcmp(status.mount_path, g_miipc.sd_mount_path) != 0) {
            printf("MiIpc SD card is already mounted at %s, expected %s\n",
                   status.mount_path,
                   g_miipc.sd_mount_path);
            return -1;
        }

        miipc_sd_mgr_default_mount_config(&mount_cfg);
        mount_cfg.device = device;
        mount_cfg.mount_path = g_miipc.sd_mount_path;
        mount_cfg.fs_type = storage_cfg->fs_type;
        ret = miipc_sd_mgr_mount(&mount_cfg);
        if (ret != MIIPC_SD_MGR_OK) {
            printf("MiIpc SD mount failed device=%s path=%s: %s\n",
                   device,
                   g_miipc.sd_mount_path,
                   miipc_sd_mgr_result_text(ret));
            return -1;
        }
        mounted_by_miipc = was_mounted ? 0 : 1;

        memset(&status, 0, sizeof(status));
        ret = miipc_sd_mgr_get_status(device, g_miipc.sd_mount_path, &status);
        if (ret != MIIPC_SD_MGR_OK || !status.mounted ||
            strcmp(status.mount_path, g_miipc.sd_mount_path) != 0) {
            printf("MiIpc SD mount verification failed path=%s\n", g_miipc.sd_mount_path);
            if (mounted_by_miipc) {
                (void)miipc_sd_mgr_unmount(g_miipc.sd_mount_path);
            }
            g_miipc.sd_mounted_by_miipc = 0;
            return -1;
        }

        g_miipc.sd_mounted_by_miipc = mounted_by_miipc;
        snprintf(g_miipc.storage_root,
                 sizeof(g_miipc.storage_root),
                 "%s",
                 g_miipc.sd_mount_path);
        g_miipc.storage_root[sizeof(g_miipc.storage_root) - 1] = '\0';
        cfg->storage_root = g_miipc.storage_root;
        printf("MiIpc SD card mounted device=%s path=%s owner=%d\n",
               g_miipc.sd_device,
               g_miipc.storage_root,
               g_miipc.sd_mounted_by_miipc);
    }
#else
    (void)cfg;
    (void)storage_cfg;
#endif
    return 0;
}

static void MiIpcUnmountOwnedStorage(void)
{
    if (!g_miipc.sd_mounted_by_miipc) {
        return;
    }
    {
        int ret = miipc_sd_mgr_unmount(g_miipc.sd_mount_path);
        if (ret != MIIPC_SD_MGR_OK) {
            printf("MiIpc SD unmount failed path=%s: %s\n",
                   g_miipc.sd_mount_path,
                   miipc_sd_mgr_result_text(ret));
        }
    }
    g_miipc.sd_mounted_by_miipc = 0;
}

static void MiIpcLoadRtspConfig(miipc_config_t* cfg)
{
    int value;
    const char* default_advertise_ip;

    if (cfg == NULL) {
        return;
    }

    value = cfg->enable_rtsp;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_CFG_SECTION, MIIPC_RTSP_CFG_ENABLE_KEY, value, &value);
    cfg->enable_rtsp = value ? 1 : 0;

    MiIpcCfgDbReadStringDefault(MIIPC_RTSP_CFG_SECTION,
        MIIPC_RTSP_CFG_LISTEN_IP_KEY,
        cfg->rtsp_listen_ip != NULL ? cfg->rtsp_listen_ip : MIIPC_RTSP_DEFAULT_LISTEN_IP,
        g_miipc.rtsp_listen_ip,
        sizeof(g_miipc.rtsp_listen_ip));
    cfg->rtsp_listen_ip = g_miipc.rtsp_listen_ip;

    value = (int)cfg->rtsp_listen_port;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_CFG_SECTION, MIIPC_RTSP_CFG_PORT_KEY, value, &value);
    if (value <= 0 || value > 65535) {
        value = MIIPC_RTSP_DEFAULT_PORT;
    }
    cfg->rtsp_listen_port = (uint16_t)value;

    default_advertise_ip = cfg->rtsp_advertise_ip != NULL ? cfg->rtsp_advertise_ip : "";
    MiIpcCfgDbReadStringDefault(MIIPC_RTSP_CFG_SECTION,
        MIIPC_RTSP_CFG_ADVERTISE_IP_KEY,
        default_advertise_ip,
        g_miipc.rtsp_advertise_ip,
        sizeof(g_miipc.rtsp_advertise_ip));
    cfg->rtsp_advertise_ip = g_miipc.rtsp_advertise_ip[0] != '\0' ?
        g_miipc.rtsp_advertise_ip : NULL;

    MiIpcCfgDbReadStringDefault(MIIPC_RTSP_CFG_SECTION,
        MIIPC_RTSP_CFG_PATH_KEY,
        cfg->rtsp_stream_path != NULL ? cfg->rtsp_stream_path : MIIPC_RTSP_DEFAULT_PATH,
        g_miipc.rtsp_stream_path,
        sizeof(g_miipc.rtsp_stream_path));
    cfg->rtsp_stream_path = g_miipc.rtsp_stream_path;

    value = cfg->rtsp_max_clients;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_CFG_SECTION, MIIPC_RTSP_CFG_MAX_CLIENTS_KEY, value, &value);
    cfg->rtsp_max_clients = value > 0 ? value : MIIPC_RTSP_DEFAULT_MAX_CLIENTS;

    value = cfg->rtsp_source_chn;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_CFG_SECTION, MIIPC_RTSP_CFG_SOURCE_CHN_KEY, value, &value);
    cfg->rtsp_source_chn = value;

    value = cfg->rtsp_enable_udp;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_CFG_SECTION, MIIPC_RTSP_CFG_UDP_KEY, value, &value);
    cfg->rtsp_enable_udp = value ? 1 : 0;

    value = cfg->rtsp_enable_tcp_interleaved;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_CFG_SECTION, MIIPC_RTSP_CFG_TCP_KEY, value, &value);
    cfg->rtsp_enable_tcp_interleaved = value ? 1 : 0;

    value = cfg->rtsp_sub_enable;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_SUB_CFG_SECTION, MIIPC_RTSP_SUB_CFG_ENABLE_KEY, value, &value);
    cfg->rtsp_sub_enable = value ? 1 : 0;

    value = (int)cfg->rtsp_sub_listen_port;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_SUB_CFG_SECTION, MIIPC_RTSP_SUB_CFG_PORT_KEY, value, &value);
    if (value <= 0 || value > 65535) {
        value = MIIPC_RTSP_DEFAULT_SUB_PORT;
    }
    cfg->rtsp_sub_listen_port = (uint16_t)value;

    MiIpcCfgDbReadStringDefault(MIIPC_RTSP_SUB_CFG_SECTION,
        MIIPC_RTSP_SUB_CFG_PATH_KEY,
        cfg->rtsp_sub_stream_path != NULL ? cfg->rtsp_sub_stream_path : MIIPC_RTSP_DEFAULT_SUB_PATH,
        g_miipc.rtsp_sub_stream_path,
        sizeof(g_miipc.rtsp_sub_stream_path));
    cfg->rtsp_sub_stream_path = g_miipc.rtsp_sub_stream_path;

    value = cfg->rtsp_sub_source_chn;
    MiIpcCfgDbReadIntDefault(MIIPC_RTSP_SUB_CFG_SECTION, MIIPC_RTSP_SUB_CFG_SOURCE_CHN_KEY, value, &value);
    cfg->rtsp_sub_source_chn = value;

    MiIpcNormalizeConfig(cfg);
}

static void MiIpcLoadOnvifConfig(miipc_config_t* cfg)
{
    int value;
    char default_uuid[MI_ONVIF_TEXT_MAX];
    const char* default_serial;
    const char* default_text;

    if (cfg == NULL) {
        return;
    }

    default_serial = cfg->onvif_serial_number != NULL && cfg->onvif_serial_number[0] != '\0' ?
        cfg->onvif_serial_number :
        (cfg->sn != NULL && cfg->sn[0] != '\0' ? cfg->sn : "0000000000");
    MiIpcBuildOnvifUuid(default_uuid, sizeof(default_uuid), default_serial);

    value = cfg->enable_onvif;
    MiIpcCfgDbReadIntDefault(MIIPC_ONVIF_CFG_SECTION, MIIPC_ONVIF_CFG_ENABLE_KEY, value, &value);
    cfg->enable_onvif = value ? 1 : 0;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_LISTEN_IP_KEY,
        cfg->onvif_listen_ip != NULL ? cfg->onvif_listen_ip : MIIPC_ONVIF_DEFAULT_LISTEN_IP,
        g_miipc.onvif_listen_ip,
        sizeof(g_miipc.onvif_listen_ip));
    cfg->onvif_listen_ip = g_miipc.onvif_listen_ip;

    value = (int)cfg->onvif_http_port;
    MiIpcCfgDbReadIntDefault(MIIPC_ONVIF_CFG_SECTION, MIIPC_ONVIF_CFG_HTTP_PORT_KEY, value, &value);
    if (value <= 0 || value > 65535) {
        value = MI_ONVIF_DEFAULT_HTTP_PORT;
    }
    cfg->onvif_http_port = (uint16_t)value;

    default_text = cfg->onvif_advertise_ip != NULL ? cfg->onvif_advertise_ip : "";
    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_ADVERTISE_IP_KEY,
        default_text,
        g_miipc.onvif_advertise_ip,
        sizeof(g_miipc.onvif_advertise_ip));
    cfg->onvif_advertise_ip = g_miipc.onvif_advertise_ip[0] != '\0' ?
        g_miipc.onvif_advertise_ip : NULL;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_DEVICE_PATH_KEY,
        cfg->onvif_device_path != NULL ? cfg->onvif_device_path : MI_ONVIF_DEFAULT_DEVICE_PATH,
        g_miipc.onvif_device_path,
        sizeof(g_miipc.onvif_device_path));
    cfg->onvif_device_path = g_miipc.onvif_device_path;

    default_text = cfg->onvif_device_service_url != NULL ? cfg->onvif_device_service_url : "";
    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_DEVICE_SERVICE_URL_KEY,
        default_text,
        g_miipc.onvif_device_service_url,
        sizeof(g_miipc.onvif_device_service_url));
    cfg->onvif_device_service_url = g_miipc.onvif_device_service_url[0] != '\0' ?
        g_miipc.onvif_device_service_url : NULL;

    default_text = cfg->onvif_rtsp_url != NULL ? cfg->onvif_rtsp_url : "";
    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_RTSP_URL_KEY,
        default_text,
        g_miipc.onvif_rtsp_url,
        sizeof(g_miipc.onvif_rtsp_url));
    cfg->onvif_rtsp_url = g_miipc.onvif_rtsp_url[0] != '\0' ?
        g_miipc.onvif_rtsp_url : NULL;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_UUID_KEY,
        cfg->onvif_uuid != NULL && cfg->onvif_uuid[0] != '\0' ? cfg->onvif_uuid : default_uuid,
        g_miipc.onvif_uuid,
        sizeof(g_miipc.onvif_uuid));
    MiIpcSetTextIfEmpty(g_miipc.onvif_uuid, sizeof(g_miipc.onvif_uuid), default_uuid);
    cfg->onvif_uuid = g_miipc.onvif_uuid;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_MANUFACTURER_KEY,
        cfg->onvif_manufacturer != NULL ? cfg->onvif_manufacturer : MIIPC_ONVIF_DEFAULT_MANUFACTURER,
        g_miipc.onvif_manufacturer,
        sizeof(g_miipc.onvif_manufacturer));
    MiIpcSetTextIfEmpty(g_miipc.onvif_manufacturer,
        sizeof(g_miipc.onvif_manufacturer),
        MIIPC_ONVIF_DEFAULT_MANUFACTURER);
    cfg->onvif_manufacturer = g_miipc.onvif_manufacturer;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_MODEL_KEY,
        cfg->onvif_model != NULL ? cfg->onvif_model : MIIPC_ONVIF_DEFAULT_MODEL,
        g_miipc.onvif_model,
        sizeof(g_miipc.onvif_model));
    MiIpcSetTextIfEmpty(g_miipc.onvif_model, sizeof(g_miipc.onvif_model), MIIPC_ONVIF_DEFAULT_MODEL);
    cfg->onvif_model = g_miipc.onvif_model;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_FIRMWARE_KEY,
        cfg->onvif_firmware_version != NULL ? cfg->onvif_firmware_version : MIIPC_ONVIF_DEFAULT_FIRMWARE,
        g_miipc.onvif_firmware_version,
        sizeof(g_miipc.onvif_firmware_version));
    MiIpcSetTextIfEmpty(g_miipc.onvif_firmware_version,
        sizeof(g_miipc.onvif_firmware_version),
        MIIPC_ONVIF_DEFAULT_FIRMWARE);
    cfg->onvif_firmware_version = g_miipc.onvif_firmware_version;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_SERIAL_KEY,
        default_serial,
        g_miipc.onvif_serial_number,
        sizeof(g_miipc.onvif_serial_number));
    MiIpcSetTextIfEmpty(g_miipc.onvif_serial_number,
        sizeof(g_miipc.onvif_serial_number),
        default_serial);
    cfg->onvif_serial_number = g_miipc.onvif_serial_number;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_HARDWARE_KEY,
        cfg->onvif_hardware_id != NULL ? cfg->onvif_hardware_id : MIIPC_ONVIF_DEFAULT_HARDWARE,
        g_miipc.onvif_hardware_id,
        sizeof(g_miipc.onvif_hardware_id));
    MiIpcSetTextIfEmpty(g_miipc.onvif_hardware_id,
        sizeof(g_miipc.onvif_hardware_id),
        MIIPC_ONVIF_DEFAULT_HARDWARE);
    cfg->onvif_hardware_id = g_miipc.onvif_hardware_id;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_DEVICE_NAME_KEY,
        cfg->onvif_device_name != NULL ? cfg->onvif_device_name : MIIPC_ONVIF_DEFAULT_DEVICE_NAME,
        g_miipc.onvif_device_name,
        sizeof(g_miipc.onvif_device_name));
    MiIpcSetTextIfEmpty(g_miipc.onvif_device_name,
        sizeof(g_miipc.onvif_device_name),
        MIIPC_ONVIF_DEFAULT_DEVICE_NAME);
    cfg->onvif_device_name = g_miipc.onvif_device_name;

    MiIpcCfgDbReadStringDefault(MIIPC_ONVIF_CFG_SECTION,
        MIIPC_ONVIF_CFG_SCOPES_KEY,
        cfg->onvif_scopes != NULL ? cfg->onvif_scopes : MIIPC_ONVIF_DEFAULT_SCOPES,
        g_miipc.onvif_scopes,
        sizeof(g_miipc.onvif_scopes));
    MiIpcSetTextIfEmpty(g_miipc.onvif_scopes, sizeof(g_miipc.onvif_scopes), MIIPC_ONVIF_DEFAULT_SCOPES);
    cfg->onvif_scopes = g_miipc.onvif_scopes;

    value = cfg->onvif_enable_http;
    MiIpcCfgDbReadIntDefault(MIIPC_ONVIF_CFG_SECTION, MIIPC_ONVIF_CFG_ENABLE_HTTP_KEY, value, &value);
    cfg->onvif_enable_http = value ? 1 : 0;

    value = cfg->onvif_enable_discovery;
    MiIpcCfgDbReadIntDefault(MIIPC_ONVIF_CFG_SECTION, MIIPC_ONVIF_CFG_ENABLE_DISCOVERY_KEY, value, &value);
    cfg->onvif_enable_discovery = value ? 1 : 0;

    value = (int)cfg->onvif_discovery_port;
    MiIpcCfgDbReadIntDefault(MIIPC_ONVIF_CFG_SECTION, MIIPC_ONVIF_CFG_DISCOVERY_PORT_KEY, value, &value);
    if (value <= 0 || value > 65535) {
        value = MI_ONVIF_DEFAULT_DISCOVERY_PORT;
    }
    cfg->onvif_discovery_port = (uint16_t)value;

    value = cfg->onvif_max_http_clients;
    MiIpcCfgDbReadIntDefault(MIIPC_ONVIF_CFG_SECTION, MIIPC_ONVIF_CFG_MAX_HTTP_CLIENTS_KEY, value, &value);
    cfg->onvif_max_http_clients = value > 0 ? value : MIIPC_ONVIF_DEFAULT_MAX_HTTP_CLIENTS;

    value = (int)cfg->onvif_thread_stack_size;
    MiIpcCfgDbReadIntDefault(MIIPC_ONVIF_CFG_SECTION, MIIPC_ONVIF_CFG_THREAD_STACK_KEY, value, &value);
    cfg->onvif_thread_stack_size = value > 0 ? (uint32_t)value : MIIPC_ONVIF_DEFAULT_THREAD_STACK_SIZE;

    MiIpcNormalizeConfig(cfg);
}
#endif

MIIPC_API void miipc_default_config(miipc_config_t* config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->http_listen_url = MIIPC_DEFAULT_HTTP_URL;
    config->storage_root = "sd";
    config->storage_queue_limit = MIIPC_SD_STORAGE_DEFAULT_QUEUE_LIMIT;
    config->signal_consumer_queue_limit = MIIPC_SIGNAL_CONSUMER_DEFAULT_QUEUE_LIMIT;
#if defined(MIIPC_TXW82X_MINIMAL)
    config->storage_enable = 0;
    config->enable_log = 0;
    config->enable_http = 1;
    config->enable_signal = 0;
    config->enable_broadcast = 0;
    config->enable_storage = 0;
    config->enable_rtsp = 1;
    config->enable_onvif = 0;
    config->enable_hardware = 0;
    config->install_signal_handlers = 0;
    config->install_fatal_signal_handlers = 0;
#else
    config->storage_enable = 1;
    config->enable_log = 1;
    config->enable_http = 1;
    config->enable_signal = 1;
    config->enable_broadcast = 1;
    config->enable_storage = 1;
    config->enable_rtsp = 1;
    config->enable_onvif = 1;
    config->enable_hardware = 1;
    config->install_signal_handlers = 1;
    config->install_fatal_signal_handlers = 1;
#endif
    config->run_poll_sleep_ms = MIIPC_DEFAULT_RUN_SLEEP_MS;
    config->rtsp_listen_ip = MIIPC_RTSP_DEFAULT_LISTEN_IP;
    config->rtsp_listen_port = MIIPC_RTSP_DEFAULT_PORT;
    config->rtsp_advertise_ip = NULL;
    config->rtsp_stream_path = MIIPC_RTSP_DEFAULT_PATH;
    config->rtsp_max_clients = MIIPC_RTSP_DEFAULT_MAX_CLIENTS;
    config->rtsp_source_chn = -1;
    config->rtsp_enable_udp = 1;
    config->rtsp_enable_tcp_interleaved = 1;
#if defined(MIIPC_TXW82X_MINIMAL)
    config->rtsp_sub_enable = 0;
#else
    config->rtsp_sub_enable = 1;
#endif
    config->rtsp_sub_listen_port = MIIPC_RTSP_DEFAULT_SUB_PORT;
    config->rtsp_sub_stream_path = MIIPC_RTSP_DEFAULT_SUB_PATH;
    config->rtsp_sub_source_chn = 1;
#if !defined(MIIPC_TXW82X_MINIMAL)
    config->onvif_listen_ip = MIIPC_ONVIF_DEFAULT_LISTEN_IP;
    config->onvif_http_port = MI_ONVIF_DEFAULT_HTTP_PORT;
    config->onvif_advertise_ip = NULL;
    config->onvif_device_path = MI_ONVIF_DEFAULT_DEVICE_PATH;
    config->onvif_device_service_url = NULL;
    config->onvif_rtsp_url = NULL;
    config->onvif_uuid = NULL;
    config->onvif_manufacturer = MIIPC_ONVIF_DEFAULT_MANUFACTURER;
    config->onvif_model = MIIPC_ONVIF_DEFAULT_MODEL;
    config->onvif_firmware_version = MIIPC_ONVIF_DEFAULT_FIRMWARE;
    config->onvif_serial_number = NULL;
    config->onvif_hardware_id = MIIPC_ONVIF_DEFAULT_HARDWARE;
    config->onvif_device_name = MIIPC_ONVIF_DEFAULT_DEVICE_NAME;
    config->onvif_scopes = MIIPC_ONVIF_DEFAULT_SCOPES;
    config->onvif_enable_http = 1;
    config->onvif_enable_discovery = 1;
    config->onvif_discovery_port = MI_ONVIF_DEFAULT_DISCOVERY_PORT;
    config->onvif_max_http_clients = MIIPC_ONVIF_DEFAULT_MAX_HTTP_CLIENTS;
    config->onvif_thread_stack_size = MIIPC_ONVIF_DEFAULT_THREAD_STACK_SIZE;
#endif
    config->read_audio = MiIpcDefaultGetAudio;
}

#if !defined(MIIPC_TXW82X_MINIMAL)
static int MiIpcStartOnvif(const miipc_config_t* cfg)
{
    MiOnvifServerCfg onvif_cfg;
    MiOnvifServerStats stats;
    miipc_rtsp_info_t rtsp_info;
    miipc_rtsp_info_t rtsp_sub_info;
    char rtsp_url[MI_ONVIF_URL_MAX];
    char service_url[MI_ONVIF_URL_MAX];
    int have_rtsp_info;
    int have_rtsp_sub_info;
    int shared_http;
    int ret;

    if (cfg == NULL || !cfg->enable_onvif) {
        return 0;
    }
    if (g_miipc.onvif_server != NULL) {
        return 0;
    }

    memset(&onvif_cfg, 0, sizeof(onvif_cfg));
    memset(&rtsp_info, 0, sizeof(rtsp_info));
    memset(&rtsp_sub_info, 0, sizeof(rtsp_sub_info));
    rtsp_url[0] = '\0';
    service_url[0] = '\0';
    have_rtsp_info = g_miipc.rtsp_started && miipc_rtsp_server_get_info(&rtsp_info) == 0;
    have_rtsp_sub_info = g_miipc.rtsp_started &&
        miipc_rtsp_server_get_info_by_index(MIIPC_RTSP_STREAM_SUB, &rtsp_sub_info) == 0;
    shared_http = g_miipc.http_started != 0;

    onvif_cfg.listen_ip = cfg->onvif_listen_ip;
    onvif_cfg.http_port = shared_http ? MiIpcHttpPortFromUrl(cfg->http_listen_url) : cfg->onvif_http_port;
    onvif_cfg.device_path = cfg->onvif_device_path;
    onvif_cfg.advertise_ip = cfg->onvif_advertise_ip;
    onvif_cfg.device_service_url = cfg->onvif_device_service_url;
    onvif_cfg.uuid = cfg->onvif_uuid;
    onvif_cfg.manufacturer = cfg->onvif_manufacturer;
    onvif_cfg.model = cfg->onvif_model;
    onvif_cfg.firmware_version = cfg->onvif_firmware_version;
    onvif_cfg.serial_number = cfg->onvif_serial_number;
    onvif_cfg.hardware_id = cfg->onvif_hardware_id;
    onvif_cfg.device_name = cfg->onvif_device_name;
    onvif_cfg.scopes = cfg->onvif_scopes;
    onvif_cfg.enable_http = shared_http ? 0 : cfg->onvif_enable_http;
    onvif_cfg.enable_discovery = cfg->onvif_enable_discovery;
    onvif_cfg.discovery_port = cfg->onvif_discovery_port;
    onvif_cfg.max_http_clients = cfg->onvif_max_http_clients;
    onvif_cfg.thread_stack_size = cfg->onvif_thread_stack_size;
    onvif_cfg.audio_enabled = 1;

    if (cfg->onvif_rtsp_url != NULL && cfg->onvif_rtsp_url[0] != '\0') {
        onvif_cfg.rtsp_url = cfg->onvif_rtsp_url;
    } else if (have_rtsp_info && rtsp_info.url[0] != '\0') {
        onvif_cfg.rtsp_url = rtsp_info.url;
    } else if (g_miipc.rtsp_started &&
               miipc_rtsp_server_get_url(rtsp_url, sizeof(rtsp_url)) == 0 &&
               rtsp_url[0] != '\0') {
        onvif_cfg.rtsp_url = rtsp_url;
    }
    if (have_rtsp_info) {
        onvif_cfg.video_encoding = rtsp_info.video_codec == MIIPC_RTSP_VIDEO_CODEC_H265 ? "H265" : "H264";
        onvif_cfg.video_width = rtsp_info.video_width;
        onvif_cfg.video_height = rtsp_info.video_height;
        onvif_cfg.video_fps = rtsp_info.video_fps;
        onvif_cfg.video_bitrate_kbps = rtsp_info.video_bitrate_kbps;
        onvif_cfg.audio_enabled = rtsp_info.audio_enabled;
        onvif_cfg.audio_encoding = rtsp_info.audio_codec == MIIPC_RTSP_AUDIO_CODEC_G711A ? "G711" : NULL;
        onvif_cfg.audio_sample_rate = rtsp_info.audio_sample_rate;
        onvif_cfg.audio_channels = rtsp_info.audio_channels;
    }
    if (have_rtsp_sub_info && rtsp_sub_info.url[0] != '\0') {
        onvif_cfg.sub_enabled = 1;
        onvif_cfg.sub_rtsp_url = rtsp_sub_info.url;
        onvif_cfg.sub_video_encoding = rtsp_sub_info.video_codec == MIIPC_RTSP_VIDEO_CODEC_H265 ? "H265" : "H264";
        onvif_cfg.sub_video_width = rtsp_sub_info.video_width;
        onvif_cfg.sub_video_height = rtsp_sub_info.video_height;
        onvif_cfg.sub_video_fps = rtsp_sub_info.video_fps;
        onvif_cfg.sub_video_bitrate_kbps = rtsp_sub_info.video_bitrate_kbps;
        onvif_cfg.sub_audio_enabled = rtsp_sub_info.audio_enabled;
        onvif_cfg.sub_audio_encoding = rtsp_sub_info.audio_codec == MIIPC_RTSP_AUDIO_CODEC_G711A ? "G711" : NULL;
        onvif_cfg.sub_audio_sample_rate = rtsp_sub_info.audio_sample_rate;
        onvif_cfg.sub_audio_channels = rtsp_sub_info.audio_channels;
    }

    g_miipc.onvif_server = mi_onvif_server_create(&onvif_cfg);
    if (g_miipc.onvif_server == NULL) {
        printf("MiIpc ONVIF server create failed\n");
        return -1;
    }

    ret = mi_onvif_server_start(g_miipc.onvif_server);
    if (ret != 0) {
        memset(&stats, 0, sizeof(stats));
        (void)mi_onvif_server_get_stats(g_miipc.onvif_server, &stats);
        printf("MiIpc ONVIF server start failed ret=%d last_error=%d\n",
               ret,
               stats.last_error);
        mi_onvif_server_destroy(g_miipc.onvif_server);
        g_miipc.onvif_server = NULL;
        return -1;
    }

    g_miipc.onvif_started = 1;
    if (shared_http) {
        mi_http_server_set_onvif_handler(cfg->onvif_device_path,
                                         MiIpcOnvifHttpHandler,
                                         g_miipc.onvif_server);
    }
    if (mi_onvif_server_get_device_service_url(g_miipc.onvif_server,
                                               service_url,
                                               sizeof(service_url)) == 0) {
        printf("MiIpc ONVIF started service=%s http=%s rtsp=%s video=%s %dx%d fps=%d bitrate=%d audio=%s/%d/%d sub=%s %dx%d fps=%d bitrate=%d\n",
               service_url,
               shared_http ? "shared" : "own",
               onvif_cfg.rtsp_url != NULL ? onvif_cfg.rtsp_url : "",
               onvif_cfg.video_encoding != NULL ? onvif_cfg.video_encoding : "H264",
               onvif_cfg.video_width > 0 ? onvif_cfg.video_width : 1280,
               onvif_cfg.video_height > 0 ? onvif_cfg.video_height : 720,
               onvif_cfg.video_fps > 0 ? onvif_cfg.video_fps : 25,
               onvif_cfg.video_bitrate_kbps > 0 ? onvif_cfg.video_bitrate_kbps : 1024,
               onvif_cfg.audio_encoding != NULL ? onvif_cfg.audio_encoding : "G711",
               onvif_cfg.audio_sample_rate > 0 ? onvif_cfg.audio_sample_rate : 8000,
               onvif_cfg.audio_channels > 0 ? onvif_cfg.audio_channels : 1,
               onvif_cfg.sub_rtsp_url != NULL ? onvif_cfg.sub_rtsp_url : "",
               onvif_cfg.sub_video_width > 0 ? onvif_cfg.sub_video_width : 0,
               onvif_cfg.sub_video_height > 0 ? onvif_cfg.sub_video_height : 0,
               onvif_cfg.sub_video_fps > 0 ? onvif_cfg.sub_video_fps : 0,
               onvif_cfg.sub_video_bitrate_kbps > 0 ? onvif_cfg.sub_video_bitrate_kbps : 0);
    } else {
        printf("MiIpc ONVIF started\n");
    }
    fflush(stdout);
    return 0;
}

static void MiIpcStopOnvif(void)
{
    mi_http_server_set_onvif_handler(NULL, NULL, NULL);
    if (g_miipc.onvif_server != NULL) {
        mi_onvif_server_stop(g_miipc.onvif_server);
        mi_onvif_server_destroy(g_miipc.onvif_server);
        g_miipc.onvif_server = NULL;
    }
    g_miipc.onvif_started = 0;
}
#endif

MIIPC_API int miipc_input_av_stream(int chn, struct Stream_Info* info, int count)
{
    if (!g_miipc.started || info == NULL || count <= 0) {
        return -1;
    }

#if !defined(MIIPC_TXW82X_MINIMAL)
    if (g_miipc.storage_started) {
        if (miipc_sd_storage_post(chn, info, count) < 0) {
            printf("MiIpc SD storage queue failed chn=%d count=%d\n", chn, count);
            fflush(stdout);
        }
    }
#endif
    mi_webrtc_input_av_stream(chn, info, count);
    if (g_miipc.rtsp_started) {
        (void)miipc_rtsp_server_input_av_stream(chn, info, count);
    }
    return 0;
}

MIIPC_API int miipc_start(const miipc_config_t* config)
{
    miipc_config_t cfg;
#if !defined(MIIPC_TXW82X_MINIMAL)
    miipc_app_config_t app_cfg;
    miipc_broadcast_config_t broadcast_cfg;
    mi_device_signal_config_t signal_cfg;
    miipc_net_route_config_t net_route_cfg;
    miipc_storage_config_t storage_cfg;
    int storage_config_ok = 1;
    int sd_mount_ok = 0;
    int sd_storage_available = 0;
#endif
    miipc_rtsp_config_t rtsp_cfg;
    int ret = 0;

    if (g_miipc.started) {
        return 0;
    }

    miipc_default_config(&cfg);
    if (config != NULL) {
        cfg = *config;
    }
    MiIpcNormalizeConfig(&cfg);

    memset(&g_miipc, 0, sizeof(g_miipc));
    g_miipc.config = cfg;

    if (cfg.install_signal_handlers) {
        MiIpcInstallSignalHandlers();
    }
    if (cfg.install_fatal_signal_handlers) {
        MiIpcInstallFatalSignalHandlers();
    }
#if !defined(MIIPC_TXW82X_MINIMAL)
    if (cfg.enable_log && miipc_log_init(cfg.log_path) != 0) {
        printf("MiIpc log init failed\n");
        ret = -1;
    } else if (cfg.enable_log) {
        g_miipc.log_started = 1;
    }

    miipc_app_info_init();
    miipc_app_config_load(&app_cfg);
    if (cfg.sn != NULL && cfg.sn[0] != '\0') {
        snprintf(app_cfg.sn, sizeof(app_cfg.sn), "%s", cfg.sn);
    }
    if (cfg.pricode != NULL && cfg.pricode[0] != '\0') {
        snprintf(app_cfg.pricode, sizeof(app_cfg.pricode), "%s", cfg.pricode);
    }
    if (cfg.bindcode != NULL && cfg.bindcode[0] != '\0') {
        snprintf(app_cfg.bindcode, sizeof(app_cfg.bindcode), "%s", cfg.bindcode);
    }
    miipc_app_info_set_device(app_cfg.sn,
                              app_cfg.pricode,
                              app_cfg.prikey,
                              app_cfg.bindcode);
    if (miipc_app_info_save() != 0) {
        printf("MiIpc appinfo save failed\n");
    }
    if (miipc_cfg_db_init() != 0) {
        printf("MiIpc Cfg.ini init failed\n");
        ret = -1;
    }
    miipc_net_route_load_config(&net_route_cfg);

    if (MiIpcLoadStorageConfig(&cfg, &storage_cfg) != 0) {
        printf("MiIpc storage configuration is invalid; automatic mount disabled\n");
        storage_cfg.auto_mount = 0;
        storage_cfg.fs_type = MIIPC_SD_MGR_FS_AUTO;
        storage_config_ok = 0;
        ret = -1;
    }
    cfg.storage_enable = storage_cfg.recording_enabled;
    MiIpcLoadRtspConfig(&cfg);
    if (cfg.onvif_serial_number == NULL || cfg.onvif_serial_number[0] == '\0') {
        cfg.onvif_serial_number = app_cfg.sn;
    }
    MiIpcLoadOnvifConfig(&cfg);
    /* SD mounting belongs to process startup, not to the first A/V sample.
     * A missing card is normal: the recorder remains running but unavailable. */
    if (storage_config_ok) {
        if (storage_cfg.auto_mount) {
            sd_mount_ok = MiIpcMountSdCard(&cfg, &storage_cfg) == 0;
            if (!sd_mount_ok) {
                printf("MiIpc SD card is not mounted; recording will discard samples\n");
            }
        } else {
            /* The platform owns the mount in this mode. Validate its root
             * before allowing the recording worker to write. */
            sd_mount_ok = 1;
        }
    }
    if (sd_mount_ok && MiIpcCheckStorageMountedWritable(cfg.storage_root) == 0) {
        sd_storage_available = 1;
    } else if (sd_mount_ok) {
        printf("MiIpc storage is not a writable mounted filesystem path=%s; "
               "recording will discard samples\n",
               cfg.storage_root != NULL ? cfg.storage_root : "");
    }
    g_miipc.config = cfg;
#endif

    MiIpcRegisterDevMsgCallbacks();

#if !defined(MIIPC_TXW82X_MINIMAL)
    if (net_route_cfg.enable) {
        if (miipc_net_route_start(&net_route_cfg) != 0) {
            printf("MiIpc route monitor start failed\n");
            ret = -1;
        } else {
            g_miipc.net_route_started = 1;
        }
    }

    if (cfg.enable_signal &&
        mi_device_signal_consumer_start(cfg.signal_consumer_queue_limit) != 0) {
        printf("MiIpc device signal consumer start failed\n");
        ret = -1;
    } else if (cfg.enable_signal) {
        g_miipc.signal_consumer_started = 1;
    }

    /* The recording worker is permanent for the MiIpc process. The storage
     * enable flag gates writes, while SD availability gates all filesystem I/O. */
    if (miipc_sd_storage_set_available(sd_storage_available) != 0 ||
        miipc_sd_storage_start(cfg.storage_root, cfg.storage_queue_limit) != 0) {
        printf("MiIpc SD storage start failed\n");
        ret = -1;
    } else {
        g_miipc.storage_started = 1;
        miipc_sd_storage_set_enabled(storage_cfg.recording_enabled);
    }

    if (cfg.enable_broadcast) {
        miipc_broadcast_default_config(&broadcast_cfg);
        if (miipc_broadcast_start(&broadcast_cfg) != 0) {
            printf("MiIpc broadcast receiver start failed\n");
            ret = -1;
        } else {
            g_miipc.broadcast_started = 1;
        }
    }
#endif

    if (cfg.enable_http && mi_http_server_start(cfg.http_listen_url) != 0) {
        printf("MiIpc http server start failed url=%s\n", cfg.http_listen_url);
        ret = -1;
    } else if (cfg.enable_http) {
        g_miipc.http_started = 1;
    }

#if !defined(MIIPC_TXW82X_MINIMAL)
    if (cfg.enable_signal) {
        miipc_signal_config_load(&signal_cfg);
        mi_device_signal_set_message_callback(OnDeviceSignalMessage, NULL);
        if (mi_device_signal_start(&signal_cfg) != 0) {
            printf("MiIpc device signal start failed\n");
            ret = -1;
        } else {
            g_miipc.signal_started = 1;
        }
    }
#endif

    g_miipc.hard_soc = cfg.hard_soc;
    if (g_miipc.hard_soc == NULL && cfg.enable_hardware) {
        g_miipc.hard_soc = CreateHardImp();
    }
    if (g_miipc.hard_soc != NULL && g_miipc.hard_soc->GetVPCfg != NULL) {
        g_miipc.hard_soc->GetVPCfg(g_miipc.hard_soc, &cfg.video_cfg, &cfg.capture_cfg);
    }
    MiIpcNormalizeVideoCfg(&cfg.video_cfg);
    g_miipc.config = cfg;
    if (g_miipc.hard_soc != NULL) {
        mi_webrtc_set_request_idr_callback(OnWebRtcRequestIdr, g_miipc.hard_soc);
        mi_webrtc_set_encoder_bitrate_callback(OnWebRtcTargetBitrate, g_miipc.hard_soc);
    }

    if (cfg.enable_rtsp) {
        miipc_rtsp_default_config(&rtsp_cfg);
        rtsp_cfg.enable = cfg.enable_rtsp;
        rtsp_cfg.listen_ip = cfg.rtsp_listen_ip;
        rtsp_cfg.listen_port = cfg.rtsp_listen_port;
        rtsp_cfg.advertise_ip = cfg.rtsp_advertise_ip;
        rtsp_cfg.stream_path = cfg.rtsp_stream_path;
        rtsp_cfg.max_clients = cfg.rtsp_max_clients;
        rtsp_cfg.source_chn = cfg.rtsp_source_chn;
        rtsp_cfg.enable_udp = cfg.rtsp_enable_udp;
        rtsp_cfg.enable_tcp_interleaved = cfg.rtsp_enable_tcp_interleaved;
        rtsp_cfg.enable_sub = cfg.rtsp_sub_enable;
        rtsp_cfg.sub_listen_port = cfg.rtsp_sub_listen_port;
        rtsp_cfg.sub_stream_path = cfg.rtsp_sub_stream_path;
        rtsp_cfg.sub_source_chn = cfg.rtsp_sub_source_chn;
        rtsp_cfg.request_idr = OnRtspRequestIdr;
        rtsp_cfg.request_idr_user = g_miipc.hard_soc;
        if (miipc_rtsp_server_start(&rtsp_cfg, &cfg.video_cfg) != 0) {
            printf("MiIpc RTSP server start failed\n");
            ret = -1;
        } else {
            g_miipc.rtsp_started = 1;
        }
    }

#if !defined(MIIPC_TXW82X_MINIMAL)
    if (cfg.enable_onvif && MiIpcStartOnvif(&cfg) != 0) {
        ret = -1;
    }
#endif

    g_miipc.started = 1;
    if (cfg.enable_hardware) {
        if (g_miipc.hard_soc == NULL || g_miipc.hard_soc->InitHard == NULL) {
            printf("MiIpc hardware adapter not available\n");
            ret = -1;
        } else {
            g_miipc.hard_soc->InitHard(g_miipc.hard_soc,
                OnAVStream,
                cfg.read_audio,
                &cfg.video_cfg,
                &cfg.capture_cfg);
            g_miipc.hardware_started = 1;
            miipc_osd_set_hard_soc(g_miipc.hard_soc);
        }
    } else if (g_miipc.hard_soc != NULL) {
        miipc_osd_set_hard_soc(g_miipc.hard_soc);
    }
    if (MiIpcStartProcessThread() != 0) {
        printf("MiIpc process thread start failed\n");
        ret = -1;
    }

    printf("MiIpc library started ret=%d\n", ret);
    fflush(stdout);
    return ret;
}

static void MiIpcPollInternal(void)
{
    if (!g_miipc.started) {
        return;
    }
    mi_webrtc_process();
}

static void* MiIpcProcessThread(void* arg)
{
    uint32_t sleep_ms;

    (void)arg;
    if (!g_miipc.started) {
        return NULL;
    }
    sleep_ms = g_miipc.config.run_poll_sleep_ms ? g_miipc.config.run_poll_sleep_ms : MIIPC_DEFAULT_RUN_SLEEP_MS;
    while (!g_miipc.stopping) {
        MiIpcPollInternal();
        mi_thread_sleep(sleep_ms);
    }
    return NULL;
}

static int MiIpcStartProcessThread(void)
{
    thread_config_t thread_config;

    if (g_miipc.process_thread_started) {
        return 0;
    }
    memset(&thread_config, 0, sizeof(thread_config));
    thread_config.name = "MiIpcProcess";
    thread_config.priority = 0;
    thread_config.stack_size = MIIPC_PROCESS_THREAD_STACK_SIZE;
    thread_config.detached = 0;
    if (mi_thread_create(&g_miipc.process_thread, MiIpcProcessThread, NULL, &thread_config) != MI_THREAD_OK) {
        return -1;
    }
    g_miipc.process_thread_started = 1;
    return 0;
}

static void MiIpcStopProcessThread(void)
{
    mi_thread_err_t join_ret;

    if (!g_miipc.process_thread_started) {
        return;
    }
    join_ret = mi_thread_join(&g_miipc.process_thread, NULL, MIIPC_PROCESS_THREAD_JOIN_TIMEOUT_MS);
    if (join_ret != MI_THREAD_OK) {
        printf("MiIpc process thread join failed: %d\n", (int)join_ret);
    }
    g_miipc.process_thread_started = 0;
}

MIIPC_API void miipc_request_stop(void)
{
    g_miipc.stopping = 1;
}

MIIPC_API void miipc_stop(void)
{
    if (!g_miipc.started) {
        return;
    }

    g_miipc.stopping = 1;
    MiIpcStopProcessThread();
#if !defined(MIIPC_TXW82X_MINIMAL)
    if (g_miipc.net_route_started) {
        miipc_net_route_stop();
        g_miipc.net_route_started = 0;
    }
    if (g_miipc.signal_started) {
        mi_device_signal_stop();
        g_miipc.signal_started = 0;
    }
    if (g_miipc.signal_consumer_started) {
        mi_device_signal_consumer_stop();
        g_miipc.signal_consumer_started = 0;
    }
    if (g_miipc.broadcast_started) {
        miipc_broadcast_stop();
        g_miipc.broadcast_started = 0;
    }
#endif
#if !defined(MIIPC_TXW82X_MINIMAL)
    if (g_miipc.onvif_started || g_miipc.onvif_server != NULL) {
        MiIpcStopOnvif();
    }
#endif
    if (g_miipc.rtsp_started) {
        miipc_rtsp_server_stop();
        g_miipc.rtsp_started = 0;
    }
    if (g_miipc.http_started) {
        mi_http_server_stop();
        g_miipc.http_started = 0;
    } else {
        mi_webrtc_shutdown();
    }
#if !defined(MIIPC_TXW82X_MINIMAL)
    if (g_miipc.storage_started) {
        miipc_sd_storage_stop();
        g_miipc.storage_started = 0;
    }
    MiIpcUnmountOwnedStorage();
#endif

    MiIpcClearCallbacks();
    miipc_osd_deinit();
#if !defined(MIIPC_TXW82X_MINIMAL)
    if (g_miipc.log_started) {
        miipc_log_close();
        g_miipc.log_started = 0;
    }
#endif

    memset(&g_miipc, 0, sizeof(g_miipc));
    printf("MiIpc library stopped\n");
    fflush(stdout);
}

MIIPC_API int miipc_is_started(void)
{
    return g_miipc.started ? 1 : 0;
}

MIIPC_API IHardSoc* miipc_get_hard_soc(void)
{
    return g_miipc.hard_soc;
}

MIIPC_API int miipc_get_rtsp_url(char* url, int url_len)
{
    return miipc_rtsp_server_get_url(url, url_len);
}

MIIPC_API int miipc_get_rtsp_url_by_index(int stream_index, char* url, int url_len)
{
    return miipc_rtsp_server_get_url_by_index(stream_index, url, url_len);
}

MIIPC_API int miipc_get_onvif_device_service_url(char* url, int url_len)
{
    if (url == NULL || url_len <= 0) {
        return -1;
    }
#if !defined(MIIPC_TXW82X_MINIMAL)
    if (g_miipc.onvif_server != NULL) {
        return mi_onvif_server_get_device_service_url(g_miipc.onvif_server, url, url_len);
    }
#endif
    url[0] = '\0';
    return -1;
}
