#ifndef MIIPC_DEV_MSG_H_
#define MIIPC_DEV_MSG_H_

#include "JSON/cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MI_DEVMSG_RTC_CANDIDATE_MAX 16
#define MI_DEVMSG_RTC_CANDIDATE_TEXT_MAX 2048

typedef enum mi_devmsg_url_type {
    MI_DEVMSG_URL_UNKNOWN = 0,
    MI_DEVMSG_URL_DEV_LOGIN,
    MI_DEVMSG_URL_DEV_CTRL,
    MI_DEVMSG_URL_DEV_STATUS,
    MI_DEVMSG_URL_WEBRTC,
    MI_DEVMSG_URL_CONFIG
} mi_devmsg_url_type_t;

typedef enum mi_devmsg_method_group {
    MI_DEVMSG_METHOD_GROUP_UNKNOWN = 0,
    MI_DEVMSG_METHOD_GROUP_BASE,
    MI_DEVMSG_METHOD_GROUP_NET,
    MI_DEVMSG_METHOD_GROUP_VIDEO,
    MI_DEVMSG_METHOD_GROUP_AUDIO,
    MI_DEVMSG_METHOD_GROUP_WEBRTC,
    MI_DEVMSG_METHOD_GROUP_STORAGE,
    MI_DEVMSG_METHOD_GROUP_ALARM,
    MI_DEVMSG_METHOD_GROUP_CONFIG,
    MI_DEVMSG_METHOD_GROUP_BUS,
    MI_DEVMSG_METHOD_GROUP_CLOUD
} mi_devmsg_method_group_t;

typedef struct mi_devmsg_login_info {
    int rid;
    int uid;
    int dev_rand_id;
    char dev_id[128];
    char series[128];
    char bindcode[128];
} mi_devmsg_login_info_t;

typedef struct mi_devmsg_rtc_sdp_offer_request {
    int conid;
    int rid;
    int uid;
    int channel;
    int stream_type;
    int audio_enable;
    int video_enable;
    int send_audio_mode;
    int data_channel;
    int close_all;
    char url[64];
    char method[64];
    char dev_id[128];
    char codec[32];
    char ice_ufrag[128];
    char ice_pwd[128];
    char ice_realm[128];
    char ice_url[256];
    char raw_sdp_param[1024];
} mi_devmsg_rtc_sdp_offer_request_t;

typedef struct mi_devmsg_rtc_sdp_offer_response {
    int status;
    int code;
    int dialog_id;
    char candidate[2048];
    char sdp_offer[16384];
    char err_msg[128];
} mi_devmsg_rtc_sdp_offer_response_t;

typedef struct mi_devmsg_rtc_candidates_response {
    int status;
    int code;
    int dialog_id;
    int ice_status;
    int candidate_count;
    char candidates[MI_DEVMSG_RTC_CANDIDATE_MAX][MI_DEVMSG_RTC_CANDIDATE_TEXT_MAX];
    char err_msg[128];
} mi_devmsg_rtc_candidates_response_t;

typedef int (*mi_devmsg_get_rtc_sdp_offer_cb)(const mi_devmsg_rtc_sdp_offer_request_t* request,
                                              mi_devmsg_rtc_sdp_offer_response_t* response,
                                              void* user);
typedef int (*mi_devmsg_get_rtc_candidates_cb)(int dialogue_id,
                                               mi_devmsg_rtc_candidates_response_t* response,
                                               void* user);
typedef int (*mi_devmsg_set_rtc_sdp_answer_cb)(int dialogue_id, const char* sdp_answer, void* user);
typedef int (*mi_devmsg_add_rtc_candidate_cb)(int dialogue_id, const char* candidate, void* user);
typedef int (*mi_devmsg_close_rtc_dialogue_cb)(int dialogue_id, int close_all, void* user);
typedef int (*mi_devmsg_response_send_cb)(const char* response_json, void* user);

int mi_devmsg_dispatch(cJSON* root);
int mi_devmsg_parse_login(cJSON* root, mi_devmsg_login_info_t* info);
void mi_devmsg_get_last_login(mi_devmsg_login_info_t* info);
cJSON* mi_devmsg_build_response_header(cJSON* request, int status, int code);
void mi_devmsg_set_response_send_callback(mi_devmsg_response_send_cb callback, void* user);
void mi_devmsg_set_get_rtc_sdp_offer_callback(mi_devmsg_get_rtc_sdp_offer_cb callback, void* user);
void mi_devmsg_set_get_rtc_candidates_callback(mi_devmsg_get_rtc_candidates_cb callback, void* user);
void mi_devmsg_set_set_rtc_sdp_answer_callback(mi_devmsg_set_rtc_sdp_answer_cb callback, void* user);
void mi_devmsg_set_add_rtc_candidate_callback(mi_devmsg_add_rtc_candidate_cb callback, void* user);
void mi_devmsg_set_close_rtc_dialogue_callback(mi_devmsg_close_rtc_dialogue_cb callback, void* user);
cJSON* mi_devmsg_build_get_rtc_sdp_offer_response(cJSON* request, const mi_devmsg_rtc_sdp_offer_response_t* offer);
const char* mi_devmsg_get_last_method(void);
const char* mi_devmsg_url_type_name(mi_devmsg_url_type_t type);
const char* mi_devmsg_method_group_name(mi_devmsg_method_group_t group);

#ifdef __cplusplus
}
#endif

#endif
