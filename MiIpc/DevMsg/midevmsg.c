#include "midevmsg.h"
#include "../App/miipcappinfo.h"
#include "../App/miipcosd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct mi_devmsg_method_entry {
    const char* name;
    mi_devmsg_method_group_t group;
} mi_devmsg_method_entry_t;

static const mi_devmsg_method_entry_t g_method_table[] = {
    { "GetDevInfo", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetBroadcast", MI_DEVMSG_METHOD_GROUP_BASE },
    { "DevCMD", MI_DEVMSG_METHOD_GROUP_BASE },
    { "GetDevLog", MI_DEVMSG_METHOD_GROUP_BASE },
    { "Reboot", MI_DEVMSG_METHOD_GROUP_BASE },
    { "RebootPlan", MI_DEVMSG_METHOD_GROUP_BASE },
    { "GetRebootPlan", MI_DEVMSG_METHOD_GROUP_BASE },
    { "ReqIdr", MI_DEVMSG_METHOD_GROUP_BASE },
    { "UpdateDevName", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetWDog", MI_DEVMSG_METHOD_GROUP_BASE },
    { "RunCmd", MI_DEVMSG_METHOD_GROUP_BASE },
    { "GetModuleInfo", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetModule", MI_DEVMSG_METHOD_GROUP_BASE },
    { "TestMic", MI_DEVMSG_METHOD_GROUP_BASE },
    { "CloseTelnet", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetNtpTime", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetAutoFillLignt", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetIpcId", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetSerInfo", MI_DEVMSG_METHOD_GROUP_BASE },
    { "GetSerInfo", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetMiscInfo", MI_DEVMSG_METHOD_GROUP_BASE },
    { "GetMiscInfo", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetNoFtpInterDir", MI_DEVMSG_METHOD_GROUP_BASE },
    { "GetNoFtpInterDir", MI_DEVMSG_METHOD_GROUP_BASE },

    { "SetIpInfo", MI_DEVMSG_METHOD_GROUP_NET },
    { "GetIpInfo", MI_DEVMSG_METHOD_GROUP_NET },
    { "SetNtpServer", MI_DEVMSG_METHOD_GROUP_NET },
    { "GetNtpServer", MI_DEVMSG_METHOD_GROUP_NET },
    { "GetWifiInfo", MI_DEVMSG_METHOD_GROUP_NET },
    { "SearchWifi", MI_DEVMSG_METHOD_GROUP_NET },
    { "SetWifiInfo", MI_DEVMSG_METHOD_GROUP_NET },
    { "SetRtspSource", MI_DEVMSG_METHOD_GROUP_NET },
    { "GetRtspSource", MI_DEVMSG_METHOD_GROUP_NET },
    { "SetCtlCfg", MI_DEVMSG_METHOD_GROUP_NET },
    { "GetCtlCfg", MI_DEVMSG_METHOD_GROUP_NET },

    { "SetMirror", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "GetMirror", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetIsp", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetLDCH", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetHDR", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetBkComp", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetExposTime", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetAWB", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetAntiFlicker", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetHLC", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "GetPicInfo", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetVideoCfg", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "GetVideoCfg", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetCaptureCfg", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "GetCaptureCfg", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetOsdInfo", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "GetOsdInfo", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetVAlarmLong", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "GetVAlarmLong", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "SetTimerInfo", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "GetTimerInfos", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "DelTimerInfo", MI_DEVMSG_METHOD_GROUP_VIDEO },
    { "OpWRLED", MI_DEVMSG_METHOD_GROUP_VIDEO },

    { "SetAiAoVol", MI_DEVMSG_METHOD_GROUP_AUDIO },
    { "SetAiAoGain", MI_DEVMSG_METHOD_GROUP_AUDIO },
    { "SetAudioCfg", MI_DEVMSG_METHOD_GROUP_AUDIO },
    { "GetAudioCfg", MI_DEVMSG_METHOD_GROUP_AUDIO },
    { "PlayAudioAlarm", MI_DEVMSG_METHOD_GROUP_AUDIO },
    { "BcAudioNty", MI_DEVMSG_METHOD_GROUP_AUDIO },

    { "GetRtcSdpOffer", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "GetCandis", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "AddCandidate", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "AddCandidates", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "SetRtcSdpAnser", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "SetRtcSdpAnswer", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "CloseRtcDialogue", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "SetRtcTurn", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "GetRtcDialogueInfos", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "SetRtcHisMode", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "SeekRtcHis", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "PauseRtcHis", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "PushRtcCloud", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "PullRtcCloud", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "GetRtcCloud", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "SetRtcVbr", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "GetRtcVbr", MI_DEVMSG_METHOD_GROUP_WEBRTC },
    { "VideoChnloadAudio", MI_DEVMSG_METHOD_GROUP_WEBRTC },

    { "GetSDDirInfo", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "GetSDBaseInfo", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "SDFormat", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "GetFsByTime", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "GetHisDays", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "SetStoreInfo", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "GetStoreInfo", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "GetStoreTimeCtl", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "SetStoreTimeCtl", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "DownToSeg", MI_DEVMSG_METHOD_GROUP_STORAGE },
    { "DownFile", MI_DEVMSG_METHOD_GROUP_STORAGE },

    { "SetGPIOCfg", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "GetGPIOCfg", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "KeySwitchIo", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "SetAICfg", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "GetAICfg", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "GetAITimeCtl", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "SetAITimeCtl", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "SetCheckReg", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "GetCheckReg", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "GetIoAlaEvent", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "SetIoAlaEvent", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "SetAlaUpFileCfg", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "GetAlaUpFileCfg", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "SetVAlarmLong", MI_DEVMSG_METHOD_GROUP_ALARM },
    { "GetVAlarmLong", MI_DEVMSG_METHOD_GROUP_ALARM },

    { "SetCfgFile", MI_DEVMSG_METHOD_GROUP_CONFIG },
    { "GetCfgFile", MI_DEVMSG_METHOD_GROUP_CONFIG },
    { "GetCfgList", MI_DEVMSG_METHOD_GROUP_CONFIG },
    { "GetFileInfo", MI_DEVMSG_METHOD_GROUP_CONFIG },

    { "GetUartInfo", MI_DEVMSG_METHOD_GROUP_BUS },
    { "SetUartInfo", MI_DEVMSG_METHOD_GROUP_BUS },
    { "SetDtuCfg", MI_DEVMSG_METHOD_GROUP_BUS },
    { "GetDtuCfg", MI_DEVMSG_METHOD_GROUP_BUS },
    { "SetMBusInfo", MI_DEVMSG_METHOD_GROUP_BUS },
    { "GetMBusInfo", MI_DEVMSG_METHOD_GROUP_BUS },
    { "PutTtlData", MI_DEVMSG_METHOD_GROUP_BUS },

    { "SetPushRtmp", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "GetPushRtmpInfo", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "SetCloudStoreCfg", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "GetCloudStoreInfo", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "SetHttpPostCfg", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "GetHttpPostCfg", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "SetGb28181Cfg", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "GetGb28181Cfg", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "SetMqttCfg", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "GetMqttCfg", MI_DEVMSG_METHOD_GROUP_CLOUD },
    { "AppUpdate", MI_DEVMSG_METHOD_GROUP_CLOUD },

    { "DevStatusNify", MI_DEVMSG_METHOD_GROUP_BASE },
    { "NotifyWsInfo", MI_DEVMSG_METHOD_GROUP_BASE },
    { "GetCourseList", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetCourseList", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetCourse", MI_DEVMSG_METHOD_GROUP_BASE },
    { "DelCourse", MI_DEVMSG_METHOD_GROUP_BASE },
    { "GetAlClockList", MI_DEVMSG_METHOD_GROUP_BASE },
    { "SetAlClockList", MI_DEVMSG_METHOD_GROUP_BASE }
};

static mi_devmsg_login_info_t g_last_login;
static char g_last_method[96];
static mi_devmsg_response_send_cb g_response_send_cb;
static void* g_response_send_user;
static mi_devmsg_get_rtc_sdp_offer_cb g_get_rtc_sdp_offer_cb;
static void* g_get_rtc_sdp_offer_user;
static mi_devmsg_get_rtc_candidates_cb g_get_rtc_candidates_cb;
static void* g_get_rtc_candidates_user;
static mi_devmsg_set_rtc_sdp_answer_cb g_set_rtc_sdp_answer_cb;
static void* g_set_rtc_sdp_answer_user;
static mi_devmsg_add_rtc_candidate_cb g_add_rtc_candidate_cb;
static void* g_add_rtc_candidate_user;
static mi_devmsg_close_rtc_dialogue_cb g_close_rtc_dialogue_cb;
static void* g_close_rtc_dialogue_user;

static int mi_devmsg_streq_ci(const char* a, const char* b)
{
    unsigned char ca;
    unsigned char cb;

    if (a == NULL || b == NULL) {
        return 0;
    }
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (unsigned char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (unsigned char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static const char* mi_devmsg_get_string(cJSON* root, const char* key)
{
    cJSON* item;

    if (root == NULL || key == NULL) {
        return "";
    }
    item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return "";
}

static int mi_devmsg_get_int(cJSON* root, const char* key, int default_value)
{
    cJSON* item;

    if (root == NULL || key == NULL) {
        return default_value;
    }
    item = cJSON_GetObjectItem(root, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return default_value;
}

static void mi_devmsg_copy_text(char* dst, int dst_len, const char* src)
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

static int mi_devmsg_parse_positive_int_text(const char* text)
{
    char* end;
    long value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value <= 0 || value > 0x7fffffffL) {
        return 0;
    }
    return (int)value;
}

static const char* mi_devmsg_get_string2(cJSON* root, cJSON* payload, const char* key)
{
    const char* value = mi_devmsg_get_string(root, key);
    if (value[0] != '\0') {
        return value;
    }
    return mi_devmsg_get_string(payload, key);
}

static int mi_devmsg_get_int2(cJSON* root, cJSON* payload, const char* key, int default_value)
{
    cJSON* item;

    if (root != NULL) {
        item = cJSON_GetObjectItem(root, key);
        if (cJSON_IsNumber(item)) {
            return item->valueint;
        }
    }
    if (payload != NULL) {
        item = cJSON_GetObjectItem(payload, key);
        if (cJSON_IsNumber(item)) {
            return item->valueint;
        }
    }
    return default_value;
}

static cJSON* mi_devmsg_get_item2(cJSON* root, cJSON* payload, const char* key)
{
    cJSON* item;

    if (key == NULL) {
        return NULL;
    }
    item = root != NULL ? cJSON_GetObjectItem(root, key) : NULL;
    if (item != NULL) {
        return item;
    }
    return payload != NULL ? cJSON_GetObjectItem(payload, key) : NULL;
}

static mi_devmsg_url_type_t mi_devmsg_parse_url(const char* url)
{
    if (url == NULL || url[0] == '\0') {
        return MI_DEVMSG_URL_UNKNOWN;
    }
    if (mi_devmsg_streq_ci(url, "OnDevLogin")) {
        return MI_DEVMSG_URL_DEV_LOGIN;
    }
    if (mi_devmsg_streq_ci(url, "OnDevCtrl") ||
        mi_devmsg_streq_ci(url, "DevCtrl") ||
        mi_devmsg_streq_ci(url, "CtrlDev") ||
        mi_devmsg_streq_ci(url, "OnDevMsg") ||
        mi_devmsg_streq_ci(url, "SetComm")) {
        return MI_DEVMSG_URL_DEV_CTRL;
    }
    if (mi_devmsg_streq_ci(url, "OnDevStatus") ||
        mi_devmsg_streq_ci(url, "DevStatusNify")) {
        return MI_DEVMSG_URL_DEV_STATUS;
    }
    if (mi_devmsg_streq_ci(url, "OnWebRtc") ||
        mi_devmsg_streq_ci(url, "WebRtcCtrl")) {
        return MI_DEVMSG_URL_WEBRTC;
    }
    if (mi_devmsg_streq_ci(url, "OnConfig") ||
        mi_devmsg_streq_ci(url, "Cfg")) {
        return MI_DEVMSG_URL_CONFIG;
    }
    return MI_DEVMSG_URL_UNKNOWN;
}

static mi_devmsg_method_group_t mi_devmsg_find_method_group(const char* method)
{
    int i;

    if (method == NULL || method[0] == '\0') {
        return MI_DEVMSG_METHOD_GROUP_UNKNOWN;
    }
    for (i = 0; i < (int)(sizeof(g_method_table) / sizeof(g_method_table[0])); ++i) {
        if (mi_devmsg_streq_ci(method, g_method_table[i].name)) {
            return g_method_table[i].group;
        }
    }
    return MI_DEVMSG_METHOD_GROUP_UNKNOWN;
}

const char* mi_devmsg_url_type_name(mi_devmsg_url_type_t type)
{
    switch (type) {
    case MI_DEVMSG_URL_DEV_LOGIN:
        return "dev_login";
    case MI_DEVMSG_URL_DEV_CTRL:
        return "dev_ctrl";
    case MI_DEVMSG_URL_DEV_STATUS:
        return "dev_status";
    case MI_DEVMSG_URL_WEBRTC:
        return "webrtc";
    case MI_DEVMSG_URL_CONFIG:
        return "config";
    default:
        return "unknown";
    }
}

const char* mi_devmsg_method_group_name(mi_devmsg_method_group_t group)
{
    switch (group) {
    case MI_DEVMSG_METHOD_GROUP_BASE:
        return "base";
    case MI_DEVMSG_METHOD_GROUP_NET:
        return "net";
    case MI_DEVMSG_METHOD_GROUP_VIDEO:
        return "video";
    case MI_DEVMSG_METHOD_GROUP_AUDIO:
        return "audio";
    case MI_DEVMSG_METHOD_GROUP_WEBRTC:
        return "webrtc";
    case MI_DEVMSG_METHOD_GROUP_STORAGE:
        return "storage";
    case MI_DEVMSG_METHOD_GROUP_ALARM:
        return "alarm";
    case MI_DEVMSG_METHOD_GROUP_CONFIG:
        return "config";
    case MI_DEVMSG_METHOD_GROUP_BUS:
        return "bus";
    case MI_DEVMSG_METHOD_GROUP_CLOUD:
        return "cloud";
    default:
        return "unknown";
    }
}

static cJSON* mi_devmsg_get_payload(cJSON* root)
{
    cJSON* data;

    if (root == NULL) {
        return NULL;
    }
    data = cJSON_GetObjectItem(root, "data");
    if (data == NULL) {
        data = cJSON_GetObjectItem(root, "Data");
    }
    return cJSON_IsObject(data) ? data : root;
}

static const char* mi_devmsg_get_method(cJSON* root, cJSON* payload)
{
    const char* method = mi_devmsg_get_string(root, "method");
    if (method[0] != '\0') {
        return method;
    }
    method = mi_devmsg_get_string(root, "Method");
    if (method[0] != '\0') {
        return method;
    }
    method = mi_devmsg_get_string(payload, "method");
    if (method[0] != '\0') {
        return method;
    }
    return mi_devmsg_get_string(payload, "Method");
}

const char* mi_devmsg_get_last_method(void)
{
    return g_last_method;
}

cJSON* mi_devmsg_build_response_header(cJSON* request, int status, int code)
{
    cJSON* response;
    cJSON* payload;
    const char* url;
    const char* dev_id;
    const char* method;
    int conid;
    int rid;
    int uid;

    if (request == NULL || !cJSON_IsObject(request)) {
        return NULL;
    }

    payload = mi_devmsg_get_payload(request);
    url = mi_devmsg_get_string(request, "Url");
    dev_id = mi_devmsg_get_string(request, "DevId");
    method = mi_devmsg_get_method(request, payload);
    conid = mi_devmsg_get_int(request, "ConId", 0);
    rid = mi_devmsg_get_int(request, "RId", 0);
    uid = mi_devmsg_get_int(request, "UId", 0);

    response = cJSON_CreateObject();
    if (response == NULL) {
        return NULL;
    }

    cJSON_AddNumberToObject(response, "ConId", conid);
    cJSON_AddStringToObject(response, "Url", url);
    cJSON_AddStringToObject(response, "DevId", dev_id);
    cJSON_AddNumberToObject(response, "RId", rid);
    cJSON_AddNumberToObject(response, "UId", uid);
    cJSON_AddNumberToObject(response, "DevRandId", (double)miipc_app_info_get_dev_rand_id());
    if (method[0] != '\0') {
        cJSON_AddStringToObject(response, "method", method);
    }
    cJSON_AddNumberToObject(response, "status", status);
    cJSON_AddNumberToObject(response, "code", code);
    return response;
}

int mi_devmsg_parse_login(cJSON* root, mi_devmsg_login_info_t* info)
{
    if (root == NULL || info == NULL || !cJSON_IsObject(root)) {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->rid = mi_devmsg_get_int(root, "RId", 0);
    info->uid = mi_devmsg_get_int(root, "UId", 0);
    info->dev_rand_id = mi_devmsg_get_int(root, "DevRandId", 0);

    mi_devmsg_copy_text(info->dev_id, sizeof(info->dev_id), mi_devmsg_get_string(root, "DevId"));
    mi_devmsg_copy_text(info->series, sizeof(info->series), mi_devmsg_get_string(root, "Series"));
    mi_devmsg_copy_text(info->bindcode, sizeof(info->bindcode), mi_devmsg_get_string(root, "BindCode"));

    return 0;
}

void mi_devmsg_get_last_login(mi_devmsg_login_info_t* info)
{
    if (info == NULL) {
        return;
    }
    *info = g_last_login;
}

void mi_devmsg_set_get_rtc_sdp_offer_callback(mi_devmsg_get_rtc_sdp_offer_cb callback, void* user)
{
    g_get_rtc_sdp_offer_cb = callback;
    g_get_rtc_sdp_offer_user = user;
}

void mi_devmsg_set_get_rtc_candidates_callback(mi_devmsg_get_rtc_candidates_cb callback, void* user)
{
    g_get_rtc_candidates_cb = callback;
    g_get_rtc_candidates_user = user;
}

void mi_devmsg_set_set_rtc_sdp_answer_callback(mi_devmsg_set_rtc_sdp_answer_cb callback, void* user)
{
    g_set_rtc_sdp_answer_cb = callback;
    g_set_rtc_sdp_answer_user = user;
}

void mi_devmsg_set_add_rtc_candidate_callback(mi_devmsg_add_rtc_candidate_cb callback, void* user)
{
    g_add_rtc_candidate_cb = callback;
    g_add_rtc_candidate_user = user;
}

void mi_devmsg_set_close_rtc_dialogue_callback(mi_devmsg_close_rtc_dialogue_cb callback, void* user)
{
    g_close_rtc_dialogue_cb = callback;
    g_close_rtc_dialogue_user = user;
}

void mi_devmsg_set_response_send_callback(mi_devmsg_response_send_cb callback, void* user)
{
    g_response_send_cb = callback;
    g_response_send_user = user;
}

static int mi_devmsg_emit_response(cJSON* response, const char* log_prefix)
{
    char* response_text;
    int ret = 0;

    if (response == NULL) {
        return -1;
    }

    response_text = cJSON_PrintUnformatted(response);
    if (response_text == NULL) {
        return -1;
    }

    printf("%s %s\n", log_prefix != NULL ? log_prefix : "MiIpc devmsg response", response_text);
    if (g_response_send_cb != NULL) {
        ret = g_response_send_cb(response_text, g_response_send_user);
        if (ret <= 0) {
            printf("MiIpc devmsg response send failed ret=%d\n", ret);
        }
    }
    cJSON_free(response_text);
    return ret < 0 ? ret : 0;
}

static const char* mi_devmsg_get_first_string2(cJSON* root,
                                               cJSON* payload,
                                               const char* key1,
                                               const char* key2,
                                               const char* key3)
{
    const char* value;

    value = mi_devmsg_get_string2(root, payload, key1);
    if (value[0] != '\0') {
        return value;
    }
    value = mi_devmsg_get_string2(root, payload, key2);
    if (value[0] != '\0') {
        return value;
    }
    return mi_devmsg_get_string2(root, payload, key3);
}

static int mi_devmsg_get_first_int2(cJSON* root,
                                    cJSON* payload,
                                    const char* key1,
                                    const char* key2,
                                    const char* key3,
                                    int default_value)
{
    int value;

    value = mi_devmsg_get_int2(root, payload, key1, default_value);
    if (value != default_value) {
        return value;
    }
    value = mi_devmsg_get_int2(root, payload, key2, default_value);
    if (value != default_value) {
        return value;
    }
    return mi_devmsg_get_int2(root, payload, key3, default_value);
}

static void mi_devmsg_copy_if_not_empty(char* dst, int dst_len, const char* src)
{
    if (src != NULL && src[0] != '\0') {
        mi_devmsg_copy_text(dst, dst_len, src);
    }
}

static void mi_devmsg_apply_rtc_sdp_offer_params(cJSON* root,
                                                 cJSON* payload,
                                                 mi_devmsg_rtc_sdp_offer_request_t* request)
{
    const char* text;

    if (request == NULL) {
        return;
    }

    request->audio_enable = mi_devmsg_get_first_int2(root, payload, "audio", "Audio", "NeedAudio", request->audio_enable);
    request->video_enable = mi_devmsg_get_first_int2(root, payload, "video", "Video", "NeedVideo", request->video_enable);
    request->send_audio_mode = mi_devmsg_get_first_int2(root, payload, "sendaudio", "SendAudio", "SendAudioMode", request->send_audio_mode);
    request->data_channel = mi_devmsg_get_first_int2(root, payload, "data", "DataChannel", "NeedDataChannel", request->data_channel);
    request->close_all = mi_devmsg_get_first_int2(root, payload, "CloseAll", "closeall", "CloseRtc", request->close_all);

    text = mi_devmsg_get_first_string2(root, payload, "iceuname", "IceUname", "IceUser");
    mi_devmsg_copy_if_not_empty(request->ice_ufrag, sizeof(request->ice_ufrag), text);
    text = mi_devmsg_get_first_string2(root, payload, "iceupass", "IceUpass", "IcePwd");
    mi_devmsg_copy_if_not_empty(request->ice_pwd, sizeof(request->ice_pwd), text);
    text = mi_devmsg_get_first_string2(root, payload, "icerealm", "IceRealm", "Realm");
    mi_devmsg_copy_if_not_empty(request->ice_realm, sizeof(request->ice_realm), text);
    text = mi_devmsg_get_first_string2(root, payload, "iceurl", "IceUrl", "TurnUrl");
    mi_devmsg_copy_if_not_empty(request->ice_url, sizeof(request->ice_url), text);
}

static void mi_devmsg_apply_rtc_sdp_offer_param_text(const char* text,
                                                     mi_devmsg_rtc_sdp_offer_request_t* request)
{
    cJSON* json;

    if (text == NULL || text[0] == '\0' || request == NULL) {
        return;
    }

    json = cJSON_Parse(text);
    if (json == NULL || !cJSON_IsObject(json)) {
        if (json != NULL) {
            cJSON_Delete(json);
        }
        return;
    }
    mi_devmsg_apply_rtc_sdp_offer_params(json, json, request);
    cJSON_Delete(json);
}

static int mi_devmsg_parse_get_rtc_sdp_offer(cJSON* root, cJSON* payload, mi_devmsg_rtc_sdp_offer_request_t* request)
{
    const char* text;

    if (root == NULL || request == NULL) {
        return -1;
    }
    memset(request, 0, sizeof(*request));
    request->channel = -1;
    request->audio_enable = 1;
    request->video_enable = 1;

    request->conid = mi_devmsg_get_int(root, "ConId", 0);
    request->rid = mi_devmsg_get_int(root, "RId", 0);
    request->uid = mi_devmsg_get_int(root, "UId", 0);
    request->channel = mi_devmsg_get_first_int2(root, payload, "Chn", "Channel", "VideoChn", request->channel);
    request->stream_type = mi_devmsg_get_first_int2(root, payload, "StreamType", "Stream", "StreamId", 0);
    request->audio_enable = mi_devmsg_get_first_int2(root, payload, "Audio", "AudioEnable", "NeedAudio", request->audio_enable);
    request->video_enable = mi_devmsg_get_first_int2(root, payload, "Video", "VideoEnable", "NeedVideo", request->video_enable);
    request->send_audio_mode = mi_devmsg_get_first_int2(root, payload, "SendAudio", "SendAudioMode", "sendaudio", 0);
    request->data_channel = mi_devmsg_get_first_int2(root, payload, "DataChannel", "NeedDataChannel", "data", 0);
    request->close_all = mi_devmsg_get_first_int2(root, payload, "CloseAll", "closeall", "CloseRtc", 0);

    mi_devmsg_copy_text(request->url, sizeof(request->url), mi_devmsg_get_string(root, "Url"));
    mi_devmsg_copy_text(request->method, sizeof(request->method), mi_devmsg_get_method(root, payload));
    mi_devmsg_copy_text(request->dev_id, sizeof(request->dev_id), mi_devmsg_get_string(root, "DevId"));
    text = mi_devmsg_get_first_string2(root, payload, "Codec", "VCodec", "VideoCodec");
    mi_devmsg_copy_text(request->codec, sizeof(request->codec), text);
    text = mi_devmsg_get_first_string2(root, payload, "sdp", "Sdp", "SdpInfo");
    mi_devmsg_copy_text(request->raw_sdp_param, sizeof(request->raw_sdp_param), text);

    mi_devmsg_apply_rtc_sdp_offer_param_text(request->raw_sdp_param, request);
    /* Top-level fields override data fields; data remains a compatibility fallback. */
    mi_devmsg_apply_rtc_sdp_offer_params(root, payload, request);

    return 0;
}

cJSON* mi_devmsg_build_get_rtc_sdp_offer_response(cJSON* request, const mi_devmsg_rtc_sdp_offer_response_t* offer)
{
    cJSON* response;
    cJSON* data;
    int status = 200;
    int code = 200;

    if (offer != NULL) {
        status = offer->status ? offer->status : 200;
        code = offer->code ? offer->code : status;
    }

    response = mi_devmsg_build_response_header(request, status, code);
    if (response == NULL) {
        return NULL;
    }

    data = cJSON_CreateObject();
    if (data == NULL) {
        cJSON_Delete(response);
        return NULL;
    }
    cJSON_AddNumberToObject(data, "status", status);
    if (offer != NULL) {
        if (offer->dialog_id > 0) {
            cJSON_AddNumberToObject(data, "DialogueId", offer->dialog_id);
        }
        cJSON_AddStringToObject(data, "ipccand", offer->candidate);
        cJSON_AddStringToObject(data, "ipcsdp", offer->sdp_offer);
        if (offer->err_msg[0] != '\0') {
            cJSON_AddStringToObject(data, "ErrMsg", offer->err_msg);
        }
    }
    cJSON_AddItemToObject(response, "data", data);
    return response;
}

static int mi_devmsg_handle_get_rtc_sdp_offer(cJSON* root, cJSON* payload)
{
    mi_devmsg_rtc_sdp_offer_request_t request;
    mi_devmsg_rtc_sdp_offer_response_t response_info;
    mi_devmsg_get_rtc_sdp_offer_cb callback;
    void* callback_user;
    cJSON* response;
    int ret = 0;

    if (mi_devmsg_parse_get_rtc_sdp_offer(root, payload, &request) != 0) {
        printf("MiIpc devmsg GetRtcSdpOffer parse failed\n");
        return -1;
    }

    memset(&response_info, 0, sizeof(response_info));
    response_info.status = 200;
    response_info.code = 200;

    callback = g_get_rtc_sdp_offer_cb;
    callback_user = g_get_rtc_sdp_offer_user;
    if (callback != NULL) {
        ret = callback(&request, &response_info, callback_user);
        if (ret != 0 && response_info.err_msg[0] == '\0') {
            mi_devmsg_copy_text(response_info.err_msg, sizeof(response_info.err_msg), "GetRtcSdpOffer callback failed");
        }
        if (ret != 0 && response_info.status == 200) {
            response_info.status = 500;
            response_info.code = 500;
        }
    } else {
        response_info.status = 501;
        response_info.code = 501;
        mi_devmsg_copy_text(response_info.err_msg, sizeof(response_info.err_msg), "GetRtcSdpOffer callback not set");
        printf("MiIpc devmsg GetRtcSdpOffer callback not set chn=%d stream=%d audio=%d video=%d sendaudio=%d data=%d close_all=%d codec=%s iceurl=%s\n",
               request.channel,
               request.stream_type,
               request.audio_enable,
               request.video_enable,
               request.send_audio_mode,
               request.data_channel,
               request.close_all,
               request.codec,
               request.ice_url);
    }

    response = mi_devmsg_build_get_rtc_sdp_offer_response(root, &response_info);
    if (response != NULL) {
        mi_devmsg_emit_response(response, "MiIpc devmsg GetRtcSdpOffer response");
        cJSON_Delete(response);
    }

    return callback != NULL ? ret : 0;
}

static int mi_devmsg_get_dialogue_id(cJSON* root, cJSON* payload)
{
    int value;
    const char* text;

    value = mi_devmsg_get_first_int2(root, payload, "DialogueId", "DlgId", "DialogId", 0);
    if (value <= 0) {
        value = mi_devmsg_get_first_int2(root, payload, "RtcId", "SessionId", "Session", 0);
    }
    if (value > 0) {
        return value;
    }

    text = mi_devmsg_get_first_string2(root, payload, "DialogueId", "DlgId", "DialogId");
    if (text[0] == '\0') {
        text = mi_devmsg_get_first_string2(root, payload, "RtcId", "SessionId", "Session");
    }
    return mi_devmsg_parse_positive_int_text(text);
}

static cJSON* mi_devmsg_build_get_rtc_candidates_response(cJSON* request,
                                                          const mi_devmsg_rtc_candidates_response_t* candidates)
{
    cJSON* response;
    cJSON* data;
    cJSON* array;
    int status = 200;
    int code = 200;
    int i;

    if (candidates != NULL) {
        status = candidates->status ? candidates->status : 200;
        code = candidates->code ? candidates->code : status;
    }

    response = mi_devmsg_build_response_header(request, status, code);
    if (response == NULL) {
        return NULL;
    }
    data = cJSON_CreateObject();
    array = cJSON_CreateArray();
    if (data == NULL || array == NULL) {
        cJSON_Delete(data);
        cJSON_Delete(array);
        cJSON_Delete(response);
        return NULL;
    }

    cJSON_AddNumberToObject(data, "status", status);
    if (candidates != NULL) {
        if (candidates->dialog_id > 0) {
            cJSON_AddNumberToObject(data, "DialogueId", candidates->dialog_id);
        }
        cJSON_AddNumberToObject(data, "IceStatus", candidates->ice_status);
        for (i = 0; i < candidates->candidate_count && i < MI_DEVMSG_RTC_CANDIDATE_MAX; ++i) {
            cJSON_AddItemToArray(array, cJSON_CreateString(candidates->candidates[i]));
        }
        if (candidates->candidate_count > 0 && candidates->candidates[0][0] != '\0') {
            cJSON_AddStringToObject(data, "ipccand", candidates->candidates[0]);
        }
        if (candidates->err_msg[0] != '\0') {
            cJSON_AddStringToObject(data, "ErrMsg", candidates->err_msg);
        }
    }
    cJSON_AddItemToObject(data, "Candis", array);
    cJSON_AddItemReferenceToObject(data, "cands", array);
    cJSON_AddItemReferenceToObject(data, "candis", array);
    cJSON_AddItemToObject(response, "data", data);
    return response;
}

static int mi_devmsg_handle_get_candis(cJSON* root, cJSON* payload)
{
    mi_devmsg_rtc_candidates_response_t response_info;
    mi_devmsg_get_rtc_candidates_cb callback;
    void* callback_user;
    cJSON* response;
    int dialogue_id = mi_devmsg_get_dialogue_id(root, payload);
    int ret = 0;

    memset(&response_info, 0, sizeof(response_info));
    response_info.status = 200;
    response_info.code = 200;
    response_info.dialog_id = dialogue_id;

    if (dialogue_id <= 0) {
        response_info.status = 400;
        response_info.code = 400;
        mi_devmsg_copy_text(response_info.err_msg, sizeof(response_info.err_msg), "missing DialogueId");
        ret = -1;
    } else {
        callback = g_get_rtc_candidates_cb;
        callback_user = g_get_rtc_candidates_user;
        if (callback != NULL) {
            ret = callback(dialogue_id, &response_info, callback_user);
            if (ret != 0 && response_info.err_msg[0] == '\0') {
                mi_devmsg_copy_text(response_info.err_msg, sizeof(response_info.err_msg), "GetCandis callback failed");
            }
            if (ret != 0 && response_info.status == 200) {
                response_info.status = 500;
                response_info.code = 500;
            }
        } else {
            response_info.status = 501;
            response_info.code = 501;
            mi_devmsg_copy_text(response_info.err_msg, sizeof(response_info.err_msg), "GetCandis callback not set");
            ret = -2;
        }
    }

    response = mi_devmsg_build_get_rtc_candidates_response(root, &response_info);
    if (response != NULL) {
        mi_devmsg_emit_response(response, "MiIpc devmsg GetCandis response");
        cJSON_Delete(response);
    }
    return ret;
}

static const char* mi_devmsg_get_sdp_answer_text(cJSON* root, cJSON* payload)
{
    const char* text;

    text = mi_devmsg_get_first_string2(root, payload, "SdpAnswer", "SdpAnser", "RtcSdpAnswer");
    if (text[0] != '\0') {
        return text;
    }
    text = mi_devmsg_get_first_string2(root, payload, "RtcSdpAnser", "Answer", "answer");
    if (text[0] != '\0') {
        return text;
    }
    return mi_devmsg_get_first_string2(root, payload, "Sdp", "sdp", "SdpInfo");
}

static const char* mi_devmsg_candidate_from_item(cJSON* item)
{
    cJSON* child;

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    if (!cJSON_IsObject(item)) {
        return "";
    }
    child = cJSON_GetObjectItem(item, "candidate");
    if (cJSON_IsString(child) && child->valuestring != NULL) {
        return child->valuestring;
    }
    child = cJSON_GetObjectItem(item, "Candidate");
    if (cJSON_IsString(child) && child->valuestring != NULL) {
        return child->valuestring;
    }
    return "";
}

static const char* mi_devmsg_get_candidate_text(cJSON* root, cJSON* payload)
{
    cJSON* item;
    const char* text;

    text = mi_devmsg_get_first_string2(root, payload, "Candidate", "candidate", "Cand");
    if (text[0] != '\0') {
        return text;
    }
    text = mi_devmsg_get_first_string2(root, payload, "Candi", "IceCandidate", "IceCand");
    if (text[0] != '\0') {
        return text;
    }
    item = mi_devmsg_get_item2(root, payload, "candidate");
    text = mi_devmsg_candidate_from_item(item);
    if (text[0] != '\0') {
        return text;
    }
    item = mi_devmsg_get_item2(root, payload, "Candidate");
    return mi_devmsg_candidate_from_item(item);
}

static cJSON* mi_devmsg_build_rtc_action_response(cJSON* request,
                                                  const char* type,
                                                  int dialogue_id,
                                                  int status,
                                                  int code,
                                                  const char* err_msg)
{
    cJSON* response;
    cJSON* data;
    cJSON* item;

    response = mi_devmsg_build_response_header(request, status, code);
    if (response == NULL) {
        return NULL;
    }
    data = cJSON_CreateObject();
    if (data == NULL) {
        cJSON_Delete(response);
        return NULL;
    }
    cJSON_AddNumberToObject(data, "status", status);
    cJSON_AddStringToObject(data, "Type", type != NULL ? type : "rtc");
    if (dialogue_id != 0) {
        cJSON_AddNumberToObject(data, "DialogueId", (double)dialogue_id);
    }
    if (err_msg != NULL && err_msg[0] != '\0') {
        cJSON_AddStringToObject(data, "ErrMsg", err_msg);
    }
    item = cJSON_Duplicate(data, 1);
    if (item == NULL) {
        cJSON_Delete(data);
        cJSON_Delete(response);
        return NULL;
    }
    cJSON_AddItemToObject(response, "data", data);
    cJSON_AddItemToObject(response, "Item", item);
    return response;
}

static int mi_devmsg_emit_rtc_action_response(cJSON* root,
                                              const char* type,
                                              int dialogue_id,
                                              int ret,
                                              const char* err_msg)
{
    cJSON* response;
    int status = ret == 0 ? 200 : 500;
    int code = status;

    response = mi_devmsg_build_rtc_action_response(root, type, dialogue_id, status, code, ret == 0 ? NULL : err_msg);
    if (response == NULL) {
        return -1;
    }
    mi_devmsg_emit_response(response, "MiIpc devmsg WebRTC response");
    cJSON_Delete(response);
    return ret;
}

static int mi_devmsg_get_osd_index(cJSON* root, cJSON* payload, int default_value)
{
    int value;
    const char* text;
    char* end;
    long parsed;

    value = mi_devmsg_get_first_int2(root, payload, "Index", "OsdId", "OSDId", default_value);
    if (value != default_value) {
        return value;
    }
    value = mi_devmsg_get_first_int2(root, payload, "Id", "id", "OsdIndex", default_value);
    if (value != default_value) {
        return value;
    }

    text = mi_devmsg_get_first_string2(root, payload, "Index", "OsdId", "OSDId");
    if (text[0] == '\0') {
        text = mi_devmsg_get_first_string2(root, payload, "Id", "id", "OsdIndex");
    }
    if (text[0] == '\0') {
        return default_value;
    }
    parsed = strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed < 0 || parsed > 0x7fffffffL) {
        return default_value;
    }
    return (int)parsed;
}

static int mi_devmsg_emit_osd_response(cJSON* root,
                                       const char* log_prefix,
                                       int selected_index,
                                       int status,
                                       int code,
                                       const char* err_msg)
{
    cJSON* response;
    cJSON* data;
    int ret = 0;

    response = mi_devmsg_build_response_header(root, status, code);
    if (response == NULL) {
        return -1;
    }
    data = miipc_osd_build_data(selected_index);
    if (data == NULL) {
        cJSON_Delete(response);
        return -1;
    }
    cJSON_AddNumberToObject(data, "status", status);
    if (err_msg != NULL && err_msg[0] != '\0') {
        cJSON_AddStringToObject(data, "ErrMsg", err_msg);
    }
    cJSON_AddItemToObject(response, "data", data);
    ret = mi_devmsg_emit_response(response, log_prefix);
    cJSON_Delete(response);
    return ret;
}

static int mi_devmsg_handle_set_osd_info(cJSON* root, cJSON* payload)
{
    miipc_osd_entry_t entry;
    char err_msg[128];
    int ret;
    int status;
    int code;

    memset(&entry, 0, sizeof(entry));
    err_msg[0] = '\0';
    ret = miipc_osd_apply_json(root, payload, &entry, err_msg, (int)sizeof(err_msg));
    status = ret == 0 ? 200 : (ret == -2 ? 400 : 500);
    code = status;
    (void)mi_devmsg_emit_osd_response(root,
                                      "MiIpc devmsg SetOsdInfo response",
                                      ret == 0 ? entry.index : -1,
                                      status,
                                      code,
                                      ret == 0 ? NULL : err_msg);
    return ret;
}

static int mi_devmsg_handle_get_osd_info(cJSON* root, cJSON* payload)
{
    int selected_index = mi_devmsg_get_osd_index(root, payload, -1);
    int status = 200;
    int code = 200;
    const char* err_msg = NULL;

    if (selected_index >= MIIPC_OSD_MAX_ENTRIES) {
        selected_index = -1;
        status = 400;
        code = 400;
        err_msg = "invalid OSD index";
    }
    return mi_devmsg_emit_osd_response(root,
                                       "MiIpc devmsg GetOsdInfo response",
                                       selected_index,
                                       status,
                                       code,
                                       err_msg);
}

static int mi_devmsg_handle_set_rtc_sdp_answer(cJSON* root, cJSON* payload)
{
    int dialogue_id = mi_devmsg_get_dialogue_id(root, payload);
    const char* answer = mi_devmsg_get_sdp_answer_text(root, payload);
    int ret;

    if (dialogue_id == 0 || answer[0] == '\0') {
        return mi_devmsg_emit_rtc_action_response(root, "answer", dialogue_id, -1, "missing DialogueId or answer");
    }
    if (g_set_rtc_sdp_answer_cb == NULL) {
        return mi_devmsg_emit_rtc_action_response(root, "answer", dialogue_id, -2, "answer callback not set");
    }

    ret = g_set_rtc_sdp_answer_cb(dialogue_id, answer, g_set_rtc_sdp_answer_user);
    return mi_devmsg_emit_rtc_action_response(root, "answer", dialogue_id, ret, "apply answer failed");
}

static int mi_devmsg_add_one_candidate(int dialogue_id, const char* candidate)
{
    if (candidate == NULL || candidate[0] == '\0') {
        return 0;
    }
    if (g_add_rtc_candidate_cb == NULL) {
        return -2;
    }
    return g_add_rtc_candidate_cb(dialogue_id, candidate, g_add_rtc_candidate_user);
}

static int mi_devmsg_handle_add_candidate(cJSON* root, cJSON* payload, int multi)
{
    int dialogue_id = mi_devmsg_get_dialogue_id(root, payload);
    cJSON* candidates;
    int ret = 0;
    int i;

    if (dialogue_id == 0) {
        return mi_devmsg_emit_rtc_action_response(root, "candidate", dialogue_id, -1, "missing DialogueId");
    }

    if (multi) {
        candidates = mi_devmsg_get_item2(root, payload, "Candidates");
        if (!cJSON_IsArray(candidates)) {
            candidates = mi_devmsg_get_item2(root, payload, "Candis");
        }
        if (!cJSON_IsArray(candidates)) {
            candidates = mi_devmsg_get_item2(root, payload, "candis");
        }
        if (!cJSON_IsArray(candidates)) {
            candidates = mi_devmsg_get_item2(root, payload, "candidates");
        }
        if (cJSON_IsArray(candidates)) {
            int count = cJSON_GetArraySize(candidates);
            for (i = 0; i < count; ++i) {
                const char* candidate = mi_devmsg_candidate_from_item(cJSON_GetArrayItem(candidates, i));
                int one_ret = mi_devmsg_add_one_candidate(dialogue_id, candidate);
                if (one_ret != 0) {
                    ret = one_ret;
                }
            }
            return mi_devmsg_emit_rtc_action_response(root, "candidate", dialogue_id, ret, "add candidate failed");
        }
    }

    ret = mi_devmsg_add_one_candidate(dialogue_id, mi_devmsg_get_candidate_text(root, payload));
    return mi_devmsg_emit_rtc_action_response(root, "candidate", dialogue_id, ret, "add candidate failed");
}

static int mi_devmsg_handle_close_rtc_dialogue(cJSON* root, cJSON* payload)
{
    int dialogue_id = mi_devmsg_get_dialogue_id(root, payload);
    int close_all = mi_devmsg_get_first_int2(root, payload, "CloseAll", "closeall", "CloseRtc", 0);
    int ret;

    if (g_close_rtc_dialogue_cb == NULL) {
        return mi_devmsg_emit_rtc_action_response(root, "close", dialogue_id, -2, "close callback not set");
    }
    if (dialogue_id == 0 && !close_all) {
        return mi_devmsg_emit_rtc_action_response(root, "close", dialogue_id, -1, "missing DialogueId");
    }

    ret = g_close_rtc_dialogue_cb(dialogue_id, close_all, g_close_rtc_dialogue_user);
    return mi_devmsg_emit_rtc_action_response(root, "close", dialogue_id, ret, "close dialogue failed");
}

static int mi_devmsg_handle_login(cJSON* root)
{
    mi_devmsg_login_info_t info;

    if (mi_devmsg_parse_login(root, &info) != 0) {
        printf("MiIpc devmsg login parse failed\n");
        return -1;
    }

    g_last_login = info;
    printf("MiIpc devmsg OnDevLogin RId=%d UId=%d DevId=%s DevRandId=%d Series=%s BindCode=%s\n",
           info.rid,
           info.uid,
           info.dev_id,
           info.dev_rand_id,
           info.series,
           info.bindcode);

    if (info.dev_id[0] == '\0' || info.series[0] == '\0') {
        printf("MiIpc devmsg OnDevLogin missing field DevId=%s Series=%s\n",
               info.dev_id,
               info.series);
        return -1;
    }
    return 0;
}

int mi_devmsg_dispatch(cJSON* root)
{
    const char* url;
    const char* method;
    const char* devid;
    cJSON* response;
    int rid;
    int uid;
    cJSON* payload;
    mi_devmsg_url_type_t url_type;
    mi_devmsg_method_group_t method_group;

    if (root == NULL || !cJSON_IsObject(root)) {
        return -1;
    }

    url = mi_devmsg_get_string(root, "Url");
    devid = mi_devmsg_get_string(root, "DevId");
    rid = mi_devmsg_get_int(root, "RId", 0);
    uid = mi_devmsg_get_int(root, "UId", 0);
    url_type = mi_devmsg_parse_url(url);
    payload = mi_devmsg_get_payload(root);
    method = mi_devmsg_get_method(root, payload);
    method_group = mi_devmsg_find_method_group(method);
    if (method[0] != '\0') {
        mi_devmsg_copy_text(g_last_method, sizeof(g_last_method), method);
    }

    printf("MiIpc devmsg dispatch Url=%s type=%s method=%s group=%s RId=%d UId=%d DevId=%s\n",
           url,
           mi_devmsg_url_type_name(url_type),
           method,
           mi_devmsg_method_group_name(method_group),
           rid,
           uid,
           devid);

    if (url_type == MI_DEVMSG_URL_DEV_LOGIN) {
        return mi_devmsg_handle_login(root);
    }

    if (method_group == MI_DEVMSG_METHOD_GROUP_VIDEO &&
        mi_devmsg_streq_ci(method, "SetOsdInfo")) {
        return mi_devmsg_handle_set_osd_info(root, payload);
    }
    if (method_group == MI_DEVMSG_METHOD_GROUP_VIDEO &&
        mi_devmsg_streq_ci(method, "GetOsdInfo")) {
        return mi_devmsg_handle_get_osd_info(root, payload);
    }

    if (method_group == MI_DEVMSG_METHOD_GROUP_WEBRTC &&
        mi_devmsg_streq_ci(method, "GetRtcSdpOffer")) {
        return mi_devmsg_handle_get_rtc_sdp_offer(root, payload);
    }
    if (method_group == MI_DEVMSG_METHOD_GROUP_WEBRTC &&
        mi_devmsg_streq_ci(method, "GetCandis")) {
        return mi_devmsg_handle_get_candis(root, payload);
    }
    if (method_group == MI_DEVMSG_METHOD_GROUP_WEBRTC &&
        (mi_devmsg_streq_ci(method, "SetRtcSdpAnser") ||
         mi_devmsg_streq_ci(method, "SetRtcSdpAnswer"))) {
        return mi_devmsg_handle_set_rtc_sdp_answer(root, payload);
    }
    if (method_group == MI_DEVMSG_METHOD_GROUP_WEBRTC &&
        mi_devmsg_streq_ci(method, "AddCandidate")) {
        return mi_devmsg_handle_add_candidate(root, payload, 0);
    }
    if (method_group == MI_DEVMSG_METHOD_GROUP_WEBRTC &&
        mi_devmsg_streq_ci(method, "AddCandidates")) {
        return mi_devmsg_handle_add_candidate(root, payload, 1);
    }
    if (method_group == MI_DEVMSG_METHOD_GROUP_WEBRTC &&
        mi_devmsg_streq_ci(method, "CloseRtcDialogue")) {
        return mi_devmsg_handle_close_rtc_dialogue(root, payload);
    }

    response = mi_devmsg_build_response_header(root, 200, 200);
    if (response != NULL) {
        mi_devmsg_emit_response(response, "MiIpc devmsg response header");
        cJSON_Delete(response);
    }

    if (url_type == MI_DEVMSG_URL_UNKNOWN && method_group == MI_DEVMSG_METHOD_GROUP_UNKNOWN) {
        printf("MiIpc devmsg unsupported message\n");
        return -2;
    }

    if (method_group == MI_DEVMSG_METHOD_GROUP_WEBRTC) {
        printf("MiIpc devmsg route to webrtc method=%s\n", method);
    } else if (method_group == MI_DEVMSG_METHOD_GROUP_CONFIG || url_type == MI_DEVMSG_URL_CONFIG) {
        printf("MiIpc devmsg route to config method=%s\n", method);
    } else if (url_type == MI_DEVMSG_URL_DEV_STATUS) {
        printf("MiIpc devmsg route to status method=%s\n", method);
    } else {
        printf("MiIpc devmsg route to device group=%s method=%s\n",
               mi_devmsg_method_group_name(method_group),
               method);
    }

    return 0;
}
